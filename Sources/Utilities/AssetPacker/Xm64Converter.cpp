#include "Xm64Converter.h"
#include "BigEndianWriter.h"
#include "Wav64Writer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Core/Logger.h>
#include <IO/FileSystem.h>

#include <zlib.h>

// libxm is C and declares its structures with C11 static assertions
#define _Static_assert static_assert
extern "C" {
#include "../../Dependencies/libxm/xm.h"
#include "../../Dependencies/libxm/xm_internal.h"
}
#undef _Static_assert

using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	namespace
	{
		/** @brief Version of the format, the one libdragon's player reads */
		constexpr std::uint8_t FileVersion = 13;

		/** @brief Loops of 8-bit samples made of an odd number of bytes and shorter than this are doubled */
		constexpr std::uint32_t ShortOddLoopLength = 1024;

		// From libdragon's mixer.h and samplebuffer.h: how far the RSP reads past a loop, and the units of a
		// sample buffer that are always kept linear in memory
		constexpr std::int32_t MixerLoopOverread = 64;
		constexpr std::int32_t SampleBufferMarginUnits = 128;

		/** @brief Bytes of a VADPCM frame of 16 samples, with 4-bit residuals */
		constexpr std::int32_t VadpcmFrameBytes = 9;

		/** @brief Reference queue depth for the loop pinning budget: two buffers of the mixer at 44100 Hz */
		constexpr std::int32_t ReferenceInflightSamples = 3528;
		constexpr std::int32_t ReferenceOutputRate = 44100;
		/** @brief A loop is worth keeping in RAM when that costs no more than this long of streaming it */
		constexpr float PinLoopHorizonSeconds = 1.0f;
		/** @brief Frames of a sample kept in RAM for a fast note-on, matching the margin of a sample buffer */
		constexpr std::int32_t AttackFrames = 128;

		/**
			@brief Sizes of the context structures on a 64-bit host

			The player reads the sizes of the memory it needs from the file. libxm works them out with `sizeof`,
			and two of its structures hold pointers, so the result depends on the host: a 64-bit `audioconv64`
			declares more than the console uses (its pointers are 32-bit), which is fine - but a 32-bit build of
			this tool, such as the web one, would declare less than the console's context needs (it has two
			streaming fields more), and the player refuses to load a module whose memory does not suffice. So the
			sizes are always what a 64-bit build would declare, which keeps the output the same everywhere.
		*/
		constexpr std::size_t Lp64ContextSize = 960;
		constexpr std::size_t Lp64ChannelContextSize = 176;

		std::uint32_t GetSampleHash(const xm_sample_t* s)
		{
			const std::size_t bytes = (s->bits == 8 ? std::size_t(s->length) : std::size_t(s->length) * 2);
			return std::uint32_t(crc32(0, reinterpret_cast<const Bytef*>(s->data8), uInt(bytes)));
		}

		// ── LZ4 ──────────────────────────────────────────────────────────────────────────────────────

		constexpr std::int32_t Lz4MinMatch = 4;
		/** @brief The last bytes of a block are always literals */
		constexpr std::int32_t Lz4LastLiterals = 5;
		/** @brief No match may begin in the last bytes of a block */
		constexpr std::int32_t Lz4MatchFindLimit = 12;

		void WriteLz4Length(SmallVector<std::uint8_t, 0>& dst, std::int32_t length)
		{
			while (length >= 255) {
				dst.push_back(255);
				length -= 255;
			}
			dst.push_back(std::uint8_t(length));
		}

		void WriteLz4Sequence(SmallVector<std::uint8_t, 0>& dst, const std::uint8_t* literals, std::int32_t literalLength,
			std::int32_t matchLength, std::int32_t offset)
		{
			const std::int32_t matchCode = (matchLength > 0 ? matchLength - Lz4MinMatch : 0);
			dst.push_back(std::uint8_t((std::min(literalLength, 15) << 4) | std::min(matchCode, 15)));
			if (literalLength >= 15) {
				WriteLz4Length(dst, literalLength - 15);
			}
			dst.append(literals, literals + literalLength);
			if (matchLength > 0) {
				dst.push_back(std::uint8_t(offset));
				dst.push_back(std::uint8_t(offset >> 8));
				if (matchCode >= 15) {
					WriteLz4Length(dst, matchCode - 15);
				}
			}
		}

		/**
			@brief Compresses @p src into one LZ4 block whose matches reach at most @p maxDistance bytes back

			libdragon streams the structure of a module through a ring buffer of the window size its header names,
			so no match may reach further than that - the standard compressors can only be limited at compile time.
			Hash chains with lazy matching get close enough to LZ4 HC for data this small.
		*/
		void CompressLz4Block(const std::uint8_t* src, std::int32_t size, std::int32_t maxDistance, SmallVector<std::uint8_t, 0>& dst)
		{
			constexpr std::int32_t HashBits = 15;
			constexpr std::int32_t MaxAttempts = 512;

			dst.clear();
			if (size < Lz4MatchFindLimit + 1) {
				WriteLz4Sequence(dst, src, size, 0, 0);
				return;
			}

			std::vector<std::int32_t> head(std::size_t(1) << HashBits, -1);
			std::vector<std::int32_t> chain(std::size_t(size), -1);
			const auto hashAt = [src](std::int32_t position) {
				std::uint32_t value;
				std::memcpy(&value, src + position, sizeof(value));
				return std::uint32_t(value * 2654435761u) >> (32 - HashBits);
			};
			std::int32_t inserted = 0;
			// Every position up to (but not including) the given one becomes findable
			const auto insertUpTo = [&](std::int32_t position) {
				for (; inserted < position && inserted + Lz4MinMatch <= size; inserted++) {
					const std::uint32_t h = hashAt(inserted);
					chain[inserted] = head[h];
					head[h] = inserted;
				}
			};

			// A match may not cover the last literals, and none may begin within the find limit of the end
			const std::int32_t matchLimit = size - Lz4LastLiterals;
			const std::int32_t lastMatchStart = size - Lz4MatchFindLimit;
			const auto findMatch = [&](std::int32_t position, std::int32_t& bestOffset) {
				insertUpTo(position);
				std::int32_t bestLength = 0;
				std::int32_t candidate = head[hashAt(position)];
				for (std::int32_t attempts = MaxAttempts; candidate >= 0 && attempts > 0; attempts--, candidate = chain[candidate]) {
					const std::int32_t distance = position - candidate;
					if (distance > maxDistance) {
						break;
					}
					if (src[candidate + bestLength] != src[position + bestLength] || std::memcmp(src + candidate, src + position, Lz4MinMatch) != 0) {
						continue;
					}
					std::int32_t length = Lz4MinMatch;
					while (position + length < matchLimit && src[candidate + length] == src[position + length]) {
						length++;
					}
					if (length > bestLength) {
						bestLength = length;
						bestOffset = distance;
						if (position + length >= matchLimit) {
							break;
						}
					}
				}
				return (bestLength >= Lz4MinMatch ? bestLength : 0);
			};

			std::int32_t anchor = 0;
			std::int32_t position = 0;
			while (position < lastMatchStart) {
				std::int32_t offset = 0;
				std::int32_t length = findMatch(position, offset);
				if (length == 0) {
					position++;
					continue;
				}
				// One step of lazy matching: a longer match right after is worth a literal
				while (position + 1 < lastMatchStart) {
					std::int32_t nextOffset = 0;
					const std::int32_t nextLength = findMatch(position + 1, nextOffset);
					if (nextLength <= length) {
						break;
					}
					position++;
					length = nextLength;
					offset = nextOffset;
				}
				WriteLz4Sequence(dst, src + anchor, position - anchor, length, offset);
				position += length;
				anchor = position;
			}
			WriteLz4Sequence(dst, src + anchor, size - anchor, 0, 0);
		}

		/** @brief Decodes an LZ4 block, checking that it reproduces @p expected and keeps to @p maxDistance */
		bool VerifyLz4Block(const SmallVector<std::uint8_t, 0>& block, const std::uint8_t* expected, std::int32_t size, std::int32_t maxDistance)
		{
			SmallVector<std::uint8_t, 0> out;
			std::size_t i = 0;
			while (i < block.size()) {
				const std::uint8_t token = block[i++];
				std::size_t literals = token >> 4;
				if (literals == 15) {
					std::uint8_t b;
					do {
						if (i >= block.size()) return false;
						b = block[i++];
						literals += b;
					} while (b == 255);
				}
				if (i + literals > block.size()) return false;
				out.append(&block[i], &block[i] + literals);
				i += literals;
				if (i == block.size()) {
					break;
				}
				if (i + 2 > block.size()) return false;
				const std::size_t offset = block[i] | (block[i + 1] << 8);
				i += 2;
				std::size_t length = (token & 15);
				if (length == 15) {
					std::uint8_t b;
					do {
						if (i >= block.size()) return false;
						b = block[i++];
						length += b;
					} while (b == 255);
				}
				length += Lz4MinMatch;
				if (offset == 0 || offset > out.size() || offset > std::size_t(maxDistance)) return false;
				for (std::size_t k = 0; k < length; k++) {
					out.push_back(out[out.size() - offset]);
				}
			}
			return (out.size() == std::size_t(size) && (size == 0 || std::memcmp(out.data(), expected, std::size_t(size)) == 0));
		}

		std::uint8_t GetWindowSizeFlags(std::int32_t windowSize)
		{
			switch (windowSize) {
				case 2 * 1024: return 0x03;
				case 4 * 1024: return 0x02;
				case 8 * 1024: return 0x01;
				default: return 0x00;		// 16 KiB
			}
		}

		/**
			@brief Writes @p data in libdragon's compressed asset format, as `asset_compress_mem()` does at level 1

			@param[out] margin	Bytes a buffer needs beyond the decompressed size to decompress the data in place
		*/
		bool WriteCompressedAsset(BigEndianWriter& out, const std::uint8_t* data, std::int32_t size, std::int32_t& margin)
		{
			// The window the player streams it through: 8 KiB suits the console's data cache, less for smaller data
			std::int32_t windowSize = 8 * 1024;
			while (size < windowSize && windowSize > 2 * 1024) {
				windowSize /= 2;
			}

			SmallVector<std::uint8_t, 0> compressed;
			CompressLz4Block(data, size, windowSize, compressed);
			if (!VerifyLz4Block(compressed, data, size, windowSize)) {
				LOGE("Internal error: LZ4 compression produced an invalid block");
				return false;
			}

			// LZ4_DECOMPRESS_INPLACE_MARGIN()
			margin = (std::int32_t(compressed.size()) >> 8) + 32;

			out.Write("DCA5", 4);
			out.Write8(std::uint8_t(GetWindowSizeFlags(windowSize) | (1 << 4)));		// Algorithm 1 is LZ4
			out.WriteLeb128(compressed.size());
			out.WriteLeb128(std::uint64_t(size));
			out.WriteLeb128(std::uint64_t(margin));
			out.Align(2);
			out.Write(compressed.data(), compressed.size());
			return true;
		}

		// ── Conversion ───────────────────────────────────────────────────────────────────────────────

		/** @brief How much of the stream a channel has to keep buffered */
		struct ChannelSizing
		{
			/** @brief Stream bytes consumed per second of playback, at the top pitch */
			double Rate = 0.0;
			/** @brief Bytes that do not scale with the queue (prefetch, overread) */
			std::int32_t Base = 0;
			/** @brief Bytes of the longest sample: nothing on the channel needs more */
			std::int32_t Cap = 0;
			/** @brief Floor to hold a pinned loop, 0 if none */
			std::int32_t Pin = 0;
		};

		struct Converter
		{
			xm_context_t* Context = nullptr;
			/** @brief Positions the module starts samples at with 9xx, which become skip points of their `.wav64` */
			std::map<xm_sample_t*, std::set<std::int32_t>> SkipPoints;
			std::map<std::uint32_t, std::uint8_t> AttackByHash;
			std::map<std::uint32_t, bool> ResidentByHash;
			/** @brief Sample data rewritten by the preprocessing, which the context then points to */
			SmallVector<Array<std::uint8_t>, 0> SampleData;

			~Converter()
			{
				if (Context != nullptr) {
					xm_free_context(Context);
				}
			}

			/** @brief Bytes this sizing asks for when the CPU can run @p inflight output samples ahead */
			static std::int32_t GetSizingBytes(const ChannelSizing& s, std::int32_t inflight, std::int32_t outputRate)
			{
				if (s.Rate <= 0 && s.Pin == 0) {
					return 0;
				}
				std::int32_t n = s.Base + std::int32_t(std::ceil(s.Rate * inflight / outputRate));
				if (s.Cap != 0 && n > s.Cap) {
					n = s.Cap;
				}
				if (n < s.Pin) {
					n = s.Pin;
				}
				return (n + 7) / 8 * 8;
			}

			/** @brief Bytes needed to keep a whole loop of @p length samples in a channel's buffer */
			static std::int32_t GetLoopPinBytes(std::int32_t length)
			{
				std::int32_t frames = (length + 15) / 16;
				frames += (MixerLoopOverread + VadpcmFrameBytes - 1) / VadpcmFrameBytes + 1;
				return frames * VadpcmFrameBytes;
			}

			/** @brief RAM a sample takes when it is preloaded whole */
			static std::int32_t GetResidentBytes(const xm_sample_t* s)
			{
				return std::int32_t(((s->length + 15) / 16) * VadpcmFrameBytes);
			}

			void RemoveEmptySamples()
			{
				for (std::int32_t i = 0; i < Context->module.num_instruments; i++) {
					xm_instrument_t* ins = &Context->module.instruments[i];
					SmallVector<std::int32_t, 0> remap;
					remap.resize(ins->num_samples);

					std::int32_t j = 0;
					for (std::int32_t k = 0; k < ins->num_samples; k++) {
						if (ins->samples[k].length > 0) {
							remap[k] = j;
							if (j != k) {
								ins->samples[j] = ins->samples[k];
							}
							j++;
						} else {
							remap[k] = -1;
						}
					}
					for (std::int32_t k = 0; k < NUM_NOTES; k++) {
						if (ins->sample_of_notes[k] < ins->num_samples) {
							ins->sample_of_notes[k] = std::uint8_t(remap[ins->sample_of_notes[k]]);
						}
					}
					ins->num_samples = std::uint16_t(j);
				}
			}

			/**
				@brief Unrolls ping-pong loops and doubles short odd loops of 8-bit samples

				The RSP plays forward loops only. A loop of 8-bit samples made of an odd number of bytes cannot be
				copied to RAM without changing the 2-byte phase, and the player shortens it by a byte - which changes
				the pitch of a short loop audibly (a 13-byte loop played as 12 is 7% off), so those are doubled.
			*/
			bool PreprocessSamples()
			{
				for (std::int32_t i = 0; i < Context->module.num_instruments; i++) {
					xm_instrument_t* ins = &Context->module.instruments[i];
					for (std::int32_t j = 0; j < ins->num_samples; j++) {
						xm_sample_t* s = &ins->samples[j];
						const std::uint32_t bps = s->bits / 8;
						std::uint32_t length = s->length * bps;
						std::uint32_t loopLength = s->loop_length * bps;
						std::uint32_t loopEnd = s->loop_end * bps;
						const std::uint8_t* original = reinterpret_cast<const std::uint8_t*>(s->data8);

						Array<std::uint8_t> data;
						switch (s->loop_type) {
							case XM_NO_LOOP: {
								data = Array<std::uint8_t>{NoInit, length};
								std::memcpy(data.data(), original, length);
								break;
							}
							case XM_FORWARD_LOOP: {
								if (bps == 1 && (loopLength % 2) == 1 && loopLength < ShortOddLoopLength) {
									length = loopEnd + loopLength;
									data = Array<std::uint8_t>{NoInit, length};
									std::memcpy(data.data(), original, loopEnd);
									std::memmove(data.data() + loopEnd, original + loopEnd - loopLength, loopLength);
									loopEnd += loopLength;
									loopLength *= 2;
								} else {
									length = loopEnd;
									data = Array<std::uint8_t>{NoInit, length};
									std::memcpy(data.data(), original, loopEnd);
								}
								break;
							}
							case XM_PING_PONG_LOOP: {
								length = loopEnd + loopLength;
								data = Array<std::uint8_t>{NoInit, length};
								std::memcpy(data.data(), original, loopEnd);
								// Mirrored sample by sample, so the bytes of a 16-bit sample stay in order
								for (std::uint32_t x = 0; x < loopLength; x++) {
									data[loopEnd + x] = original[(loopEnd - x - 1) ^ (bps >> 1)];
								}
								loopEnd += loopLength;
								loopLength *= 2;
								s->loop_type = XM_FORWARD_LOOP;
								break;
							}
							default: {
								LOGE("Invalid loop type {}", std::int32_t(s->loop_type));
								return false;
							}
						}

						if (length != s->length * bps) {
							const auto align8 = [](std::uint32_t n) { return (n + 7) / 8 * 8; };
							Context->ctx_size -= align8(s->length * bps);
							Context->ctx_size_all_samples -= align8(s->length * bps);
							Context->ctx_size += align8(length);
							Context->ctx_size_all_samples += align8(length);
						}
						s->length = length / bps;
						s->loop_length = loopLength / bps;
						s->loop_end = loopEnd / bps;
						s->data8 = reinterpret_cast<std::int8_t*>(data.data());
						SampleData.push_back(std::move(data));
					}
				}
				return true;
			}

			/**
				@brief Plays every pattern order of the module to collect the inputs of the buffer sizing

				Streaming rates per channel, the top pitch of each looping sample on each channel, note-ons from the
				beginning of a sample and how many land on the same tick, 9xx skip points and the samples in use.
			*/
			void DryRun(ChannelSizing sizing[32], std::map<xm_sample_t*, float> loopFrequency[32],
				std::map<xm_sample_t*, std::int32_t>& noteOnsFromStart, std::map<xm_sample_t*, std::int32_t>& coldBurst,
				SmallVector<SmallVector<bool, 0>, 0>& usedSamples)
			{
				xm_context_t* ctx = Context;
				const std::int32_t orderCount = xm_get_module_length(ctx);
				bool playedOrders[PATTERN_ORDER_TABLE_LENGTH] = {};

				while (true) {
					do {
						xm_tick(ctx);

						std::uint8_t orderIndex;
						xm_get_position(ctx, &orderIndex, nullptr, nullptr, nullptr);
						playedOrders[orderIndex] = true;

						const std::int32_t sampleCount = std::int32_t(std::ceil(ctx->remaining_samples_in_tick));
						xm_sample_t* cold[32];
						std::int32_t coldCount = 0;
						for (std::int32_t i = 0; i < ctx->module.num_channels; i++) {
							xm_channel_context_t* ch = &ctx->channels[i];
							if (ch->instrument == nullptr || ch->sample == nullptr) {
								continue;
							}

							// libxm keeps the previous sample when a note names an instrument without one, so the sample is
							// not necessarily the instrument's - which is told by its address, as subtracting pointers into
							// different arrays is undefined (and optimizers do act on that)
							const std::ptrdiff_t instrumentIndex = ch->instrument - ctx->module.instruments;
							const std::uintptr_t firstSample = reinterpret_cast<std::uintptr_t>(ch->instrument->samples);
							const std::uintptr_t currentSample = reinterpret_cast<std::uintptr_t>(ch->sample);
							if (currentSample >= firstSample && currentSample < firstSample + ch->instrument->num_samples * sizeof(xm_sample_t)) {
								usedSamples[instrumentIndex][(currentSample - firstSample) / sizeof(xm_sample_t)] = true;
							}

							// Stream bytes per output sample at this pitch, and on top of that the bytes the queue depth
							// does not change: what the sample buffer keeps ready, the RSP's overread past a loop, and
							// the framing slack of a block codec
							const double bytesPerSample = VadpcmFrameBytes / 16.0;
							std::int32_t frames = (MixerLoopOverread + VadpcmFrameBytes - 1) / VadpcmFrameBytes + 2;
							if (ch->sample->loop_type != XM_NO_LOOP) {
								frames += 2;
							}
							const std::int32_t extra = frames * VadpcmFrameBytes + SampleBufferMarginUnits * VadpcmFrameBytes;

							ChannelSizing& s = sizing[i];
							const double rate = ch->step * bytesPerSample * ctx->rate;
							if (s.Rate < rate) {
								s.Rate = rate;
							}
							if (s.Base < extra) {
								s.Base = extra;
							}
							const std::int32_t cap = std::int32_t(std::ceil(ch->sample->length * bytesPerSample)) + extra;
							if (s.Cap < cap) {
								s.Cap = cap;
							}

							if (ch->sample->loop_type != XM_NO_LOOP && ch->sample->loop_length > 0) {
								float& frequency = loopFrequency[i][ch->sample];
								if (ch->frequency > frequency) {
									frequency = ch->frequency;
								}
							}

							const bool keyOn = (ch->current->note > 0 && ch->current->note < 97);
							if (keyOn && ch->current->effect_type == 0x9) {
								SkipPoints[ch->sample].insert(std::int32_t(ch->sample_position));
							}
							// Tick 0 of the row without 9xx (or with 9xx=0): the sample starts cold, from its beginning
							if (keyOn && ctx->current_tick == 0 && (ch->current->effect_type != 0x9 || ch->current->effect_param == 0)) {
								cold[coldCount++] = ch->sample;
							}
						}
						for (std::int32_t k = 0; k < coldCount; k++) {
							noteOnsFromStart[cold[k]]++;
							if (coldBurst[cold[k]] < coldCount) {
								coldBurst[cold[k]] = coldCount;
							}
						}
						ctx->remaining_samples_in_tick -= float(sampleCount);
					} while (xm_get_loop_count(ctx) == 0);

					// Every pattern order that has not played yet starts a sub-song of its own
					bool fullyPlayed = true;
					for (std::int32_t i = 0; i < orderCount; i++) {
						if (!playedOrders[i]) {
							xm_seek(ctx, std::uint8_t(i), 0, 0);
							fullyPlayed = false;
							break;
						}
					}
					if (fullyPlayed) {
						break;
					}
				}
			}

			/**
				@brief Sizes the sample buffer of each channel, and keeps loops in RAM where that pays off

				A loop is worth keeping whole in a channel's buffer when the RAM it adds is repaid by about a second
				of cartridge reads at that pitch. Candidates are spent in order of channels times pitch over loop
				length and RAM, within a budget of what streaming costs anyway; a sample preloaded once is preferred
				where it costs less than growing every buffer it plays in.

				@returns The bytes of all sample buffers at the reference queue depth
			*/
			std::int32_t SizeLoopBuffers(ChannelSizing sizing[32], std::map<xm_sample_t*, float> loopFrequency[32])
			{
				xm_context_t* ctx = Context;

				std::int32_t streamSize[32] = {};
				std::int32_t streamTotal = 0;
				for (std::int32_t i = 0; i < ctx->module.num_channels; i++) {
					sizing[i].Rate *= 1.05;
					streamSize[i] = GetSizingBytes(sizing[i], ReferenceInflightSamples, ReferenceOutputRate);
					streamTotal += streamSize[i];
				}

				const float bytesPerSample = VadpcmFrameBytes / 16.0f;

				// Identical looping samples are one waveform, whichever channels play them
				struct LoopInfo
				{
					xm_sample_t* Sample = nullptr;
					std::vector<std::int32_t> Channels;
					float ChannelFrequency[32];
					std::int32_t PinSize = 0, PinExtra = 0;
					float Score = 0.0f;
				};
				std::map<std::uint32_t, LoopInfo> byHash;
				for (std::int32_t i = 0; i < ctx->module.num_channels; i++) {
					for (const auto& kv : loopFrequency[i]) {
						LoopInfo& info = byHash[GetSampleHash(kv.first)];
						if (info.Sample == nullptr) {
							info.Sample = kv.first;
							std::memset(info.ChannelFrequency, 0, sizeof(info.ChannelFrequency));
						}
						info.Channels.push_back(i);
						if (kv.second > info.ChannelFrequency[i]) {
							info.ChannelFrequency[i] = kv.second;
						}
					}
				}

				std::vector<std::pair<float, std::uint32_t>> candidates;
				for (auto& kv : byHash) {
					LoopInfo& info = kv.second;
					const std::int32_t loopLength = std::int32_t(info.Sample->loop_length);
					if (loopLength <= 0) {
						continue;
					}
					info.PinSize = GetLoopPinBytes(loopLength);
					info.PinExtra = 0;
					bool worth = false;
					float maxFrequency = 0.0f;
					for (std::int32_t ch : info.Channels) {
						const std::int32_t extra = (info.PinSize > streamSize[ch] ? info.PinSize - streamSize[ch] : 0);
						info.PinExtra += extra;
						const float frequency = info.ChannelFrequency[ch];
						if (frequency > maxFrequency) {
							maxFrequency = frequency;
						}
						if (extra > 0 && double(extra) <= double(PinLoopHorizonSeconds) * double(frequency) * double(bytesPerSample)) {
							worth = true;
						}
					}
					if (!worth || info.PinExtra <= 0) {
						continue;
					}
					info.Score = float(info.Channels.size()) * maxFrequency / float(loopLength) / float(info.PinExtra);
					candidates.push_back({ info.Score, kv.first });
				}
				std::sort(candidates.begin(), candidates.end(), [](const std::pair<float, std::uint32_t>& a, const std::pair<float, std::uint32_t>& b) {
					return a.first > b.first;
				});

				std::int32_t grown[32];
				std::memcpy(grown, streamSize, sizeof(grown));
				const std::int32_t pinBudget = streamTotal;
				std::int32_t pinSpent = 0;
				for (const auto& candidate : candidates) {
					LoopInfo& info = byHash[candidate.second];
					std::int32_t cost = 0;
					for (std::int32_t ch : info.Channels) {
						if (info.PinSize > grown[ch]) {
							cost += info.PinSize - grown[ch];
						}
					}
					if (cost <= 0 || cost > pinBudget - pinSpent) {
						continue;
					}
					if (GetResidentBytes(info.Sample) <= cost) {
						ResidentByHash[candidate.second] = true;
						continue;
					}
					pinSpent += cost;
					for (std::int32_t ch : info.Channels) {
						if (info.PinSize > grown[ch]) {
							grown[ch] = info.PinSize;
						}
					}
				}

				// A channel grown to hold a pinned loop keeps that as a floor; the rest is worked out at playback
				// time from the queue depth in use
				std::int32_t totalSize = 0;
				for (std::int32_t i = 0; i < ctx->module.num_channels; i++) {
					if (grown[i] > streamSize[i]) {
						sizing[i].Pin = (grown[i] + 7) / 8 * 8;
					}
					ctx->ctx_stream_buf_rate[i] = std::uint32_t(std::ceil(sizing[i].Rate));
					ctx->ctx_stream_buf_base[i] = std::uint32_t(sizing[i].Base);
					ctx->ctx_stream_buf_min[i] = std::uint32_t(sizing[i].Pin);
					ctx->ctx_stream_buf_cap[i] = std::uint32_t(sizing[i].Cap);
					totalSize += GetSizingBytes(sizing[i], ReferenceInflightSamples, ReferenceOutputRate);
				}
				return totalSize;
			}

			/**
				@brief Keeps the first frames of the samples that most often start on the same tick in RAM

				A note that starts a sample from its beginning has to fetch those frames from the cartridge before it
				plays, and several on one tick queue up on the bus. The budget is an eighth of the sample buffers.
			*/
			void SelectAttackCache(const std::map<xm_sample_t*, std::int32_t>& noteOnsFromStart,
				const std::map<xm_sample_t*, std::int32_t>& coldBurst, std::int32_t sampleBufferSize)
			{
				struct Candidate
				{
					std::int32_t Score;
					xm_sample_t* Sample;
				};
				std::vector<Candidate> candidates;
				for (const auto& kv : noteOnsFromStart) {
					candidates.push_back({ kv.second * coldBurst.at(kv.first), kv.first });
				}
				std::sort(candidates.begin(), candidates.end(), [](Candidate a, Candidate b) {
					return a.Score > b.Score;
				});

				std::int32_t budget = sampleBufferSize / 8;
				const std::int32_t cost = AttackFrames * VadpcmFrameBytes;
				for (const Candidate& c : candidates) {
					if (budget < cost) {
						break;
					}
					std::uint8_t& frames = AttackByHash[GetSampleHash(c.Sample)];
					if (frames >= AttackFrames) {
						continue;
					}
					frames = AttackFrames;
					budget -= cost;
				}
			}

			bool SaveSample(xm_sample_t* s, BigEndianWriter& out, StringView displayName)
			{
				Wav64Audio audio;
				audio.Samples.resize(s->length);
				if (s->bits == 8) {
					for (std::uint32_t k = 0; k < s->length; k++) {
						audio.Samples[k] = std::int16_t(std::int32_t(s->data8[k]) * 256 | std::uint8_t(s->data8[k]));
					}
				} else {
					std::memcpy(audio.Samples.data(), s->data16, std::size_t(s->length) * sizeof(std::int16_t));
				}
				audio.Frames = std::int32_t(s->length);
				audio.Channels = 1;
				audio.BitsPerSample = s->bits;
				audio.SampleRate = 44100;
				audio.Looping = (s->loop_type != XM_NO_LOOP);
				audio.LoopOffset = std::int32_t(s->loop_start);
				audio.LoopEnd = (s->loop_type != XM_NO_LOOP ? std::int32_t(s->loop_end) : 0);
				for (std::int32_t position : SkipPoints[s]) {
					audio.SkipPoints.push_back(position);
				}
				const std::uint32_t hash = GetSampleHash(s);
				audio.AttackFrames = AttackByHash[hash];
				audio.Resident = ResidentByHash[hash];
				// A sample that lives in RAM needs no frames of it kept for its note-ons
				if (audio.Resident) {
					audio.AttackFrames = 0;
				}

				if (!Wav64Writer::Write(out, audio, Wav64Format::Vadpcm, displayName)) {
					return false;
				}

				if (audio.Looping) {
					// The loop may have moved to begin on a VADPCM boundary
					const std::int32_t loopEnd = (audio.LoopEnd != 0 ? audio.LoopEnd : audio.Frames);
					s->loop_start = std::uint32_t(audio.LoopOffset);
					s->loop_length = std::uint32_t(loopEnd - audio.LoopOffset);
					s->loop_end = std::uint32_t(loopEnd);
					s->length = std::uint32_t(audio.Frames);
				}
				return true;
			}

			bool Save(BigEndianWriter& xm64, StringView displayName)
			{
				xm_context_t* ctx = Context;

				xm64.Write("XM64", 4);
				xm64.Write8(FileVersion);
				const std::size_t metadataOffsetField = xm64.Reserve32();
				const std::size_t metadataSizeField = xm64.Reserve32();

				// The samples go first, as writing them may change them (a loop moved, the length padded); one that
				// is byte for byte the same as an earlier one is stored only once
				struct StoredSample
				{
					std::uint32_t Hash;
					std::uint32_t Position;
					xm_sample_t* Sample;
				};
				SmallVector<StoredSample, 0> stored;
				SmallVector<SmallVector<std::uint32_t, 0>, 0> sampleOffsets;
				sampleOffsets.resize(ctx->module.num_instruments);
				for (std::int32_t i = 0; i < ctx->module.num_instruments; i++) {
					xm_instrument_t* ins = &ctx->module.instruments[i];
					sampleOffsets[i].resize(ins->num_samples);
					for (std::int32_t j = 0; j < ins->num_samples; j++) {
						xm_sample_t* s = &ins->samples[j];
						const std::uint32_t hash = GetSampleHash(s);
						StoredSample* found = nullptr;
						for (StoredSample& candidate : stored) {
							if (candidate.Hash == hash) {
								found = &candidate;
								break;
							}
						}
						if (found == nullptr) {
							xm64.Align(2);
							stored.push_back({ hash, std::uint32_t(xm64.GetPosition()), s });
							found = &stored.back();
							if (!SaveSample(s, xm64, displayName)) {
								return false;
							}
						} else if (s->loop_type != XM_NO_LOOP) {
							s->length = found->Sample->length;
							s->loop_start = found->Sample->loop_start;
							s->loop_length = found->Sample->loop_length;
							s->loop_end = found->Sample->loop_end;
							s->loop_type = found->Sample->loop_type;
						}
						sampleOffsets[i][j] = found->Position;
					}
				}

				BigEndianWriter meta;
				meta.Write32(ctx->ctx_size);
				meta.Write32(ctx->ctx_size_all_patterns);
				meta.Write32(ctx->ctx_size_all_samples);
				const std::size_t patternBufferField = meta.Reserve32();
				for (std::int32_t i = 0; i < 32; i++) meta.Write32(ctx->ctx_stream_buf_rate[i]);
				for (std::int32_t i = 0; i < 32; i++) meta.Write32(ctx->ctx_stream_buf_base[i]);
				for (std::int32_t i = 0; i < 32; i++) meta.Write32(ctx->ctx_stream_buf_min[i]);
				for (std::int32_t i = 0; i < 32; i++) meta.Write32(ctx->ctx_stream_buf_cap[i]);

				meta.Write16(ctx->module.tempo);
				meta.Write16(ctx->module.bpm);
				meta.Write(ctx->module.name, sizeof(ctx->module.name));
				meta.Write(ctx->module.trackername, sizeof(ctx->module.trackername));
				meta.Write16(ctx->module.length);
				meta.Write16(ctx->module.restart_position);
				meta.Write16(ctx->module.num_channels);
				meta.Write16(ctx->module.num_patterns);
				meta.Write16(ctx->module.num_instruments);
				meta.Write32(std::uint32_t(ctx->module.frequency_type));
				meta.Write(ctx->module.pattern_table, sizeof(ctx->module.pattern_table));

				SmallVector<std::size_t, 0> patternOffsetFields, patternSizeFields;
				for (std::int32_t i = 0; i < ctx->module.num_patterns; i++) {
					meta.Write16(ctx->module.patterns[i].num_rows);
					patternOffsetFields.push_back(meta.Reserve32());
					patternSizeFields.push_back(meta.Reserve16());
				}

				const auto writeEnvelope = [&meta](const xm_envelope_t& envelope) {
					meta.Write8(envelope.num_points);
					for (std::int32_t j = 0; j < envelope.num_points; j++) {
						meta.Write16(envelope.points[j].frame);
						meta.Write16(envelope.points[j].value);
					}
					meta.Write8(envelope.sustain_point);
					meta.Write8(envelope.loop_start_point);
					meta.Write8(envelope.loop_end_point);
					meta.Write8(envelope.enabled ? 1 : 0);
					meta.Write8(envelope.sustain_enabled ? 1 : 0);
					meta.Write8(envelope.loop_enabled ? 1 : 0);
				};
				for (std::int32_t i = 0; i < ctx->module.num_instruments; i++) {
					const xm_instrument_t* ins = &ctx->module.instruments[i];
					meta.Write(ins->name, sizeof(ins->name));
					meta.Write(ins->sample_of_notes, sizeof(ins->sample_of_notes));
					writeEnvelope(ins->volume_envelope);
					writeEnvelope(ins->panning_envelope);
					meta.Write32(std::uint32_t(ins->vibrato_type));
					meta.Write8(ins->vibrato_sweep);
					meta.Write8(ins->vibrato_depth);
					meta.Write8(ins->vibrato_rate);
					meta.Write16(ins->volume_fadeout);
					meta.Write64(ins->latest_trigger);

					meta.Write16(ins->num_samples);
					for (std::int32_t j = 0; j < ins->num_samples; j++) {
						const xm_sample_t* s = &ins->samples[j];
						// The original sample size, even though VADPCM is always 16-bit: 9xx offsets are in its units
						meta.Write8(s->bits);
						meta.Write32(s->length);
						meta.Write32(s->loop_start);
						meta.Write32(s->loop_length);
						meta.Write32(s->loop_end);
						meta.WriteFloat32(s->volume);
						meta.Write8(std::uint8_t(s->finetune));
						meta.Write32(std::uint32_t(s->loop_type));
						meta.WriteFloat32(s->panning);
						meta.Write8(std::uint8_t(s->relative_note));
						meta.Write32(sampleOffsets[i][j]);
					}
				}
				meta.Write8(0);		// The samples are inside the file

				// Patterns, compressed one by one, channel by channel - the player loads one at a time
				std::int32_t maxInPlaceMargin = 0;
				for (std::int32_t i = 0; i < ctx->module.num_patterns; i++) {
					xm64.Align(2);
					const std::size_t position = xm64.GetPosition();

					const xm_pattern_t* p = &ctx->module.patterns[i];
					const std::int32_t patternSize = p->num_rows * ctx->module.num_channels * 5;
					SmallVector<std::uint8_t, 0> pattern;
					pattern.reserve(std::size_t(patternSize));
					const xm_pattern_slot_t* slot = &p->slots[0];
					for (std::int32_t k = 0; k < ctx->module.num_channels; k++) {
						for (std::int32_t j = 0; j < p->num_rows; j++) {
							pattern.push_back(slot->note);
							pattern.push_back(slot->instrument);
							pattern.push_back(slot->volume_column);
							pattern.push_back(slot->effect_type);
							pattern.push_back(slot->effect_param);
							slot++;
						}
					}

					std::int32_t margin;
					if (!WriteCompressedAsset(xm64, pattern.data(), patternSize, margin)) {
						return false;
					}
					maxInPlaceMargin = std::max(maxInPlaceMargin, margin);
					meta.Patch32(patternOffsetFields[i], std::uint32_t(position));
					meta.Patch16(patternSizeFields[i], std::uint16_t(xm64.GetPosition() - position));
				}

				// The pattern buffer holds the largest pattern plus what decompressing in place needs (see
				// asset_buf_size() in libdragon), 32-bit alignment for the decompressor and slack for its overwrites
				ctx->ctx_size_stream_pattern_buf += std::uint32_t(maxInPlaceMargin) + 4 + 8;
				ctx->ctx_size_stream_pattern_buf = (ctx->ctx_size_stream_pattern_buf + 15) / 16 * 16;
				meta.Patch32(patternBufferField, ctx->ctx_size_stream_pattern_buf);

				xm64.Align(2);
				xm64.Patch32(metadataOffsetField, std::uint32_t(xm64.GetPosition()));
				xm64.Patch32(metadataSizeField, std::uint32_t(meta.Data.size()));
				std::int32_t margin;
				return WriteCompressedAsset(xm64, meta.Data.data(), std::int32_t(meta.Data.size()), margin);
			}
		};
	}

	bool Xm64Converter::Convert(StringView sourcePath, StringView targetPath)
	{
		auto s = fs::Open(sourcePath, FileAccess::Read);
		const std::int64_t size = (s->IsValid() ? s->GetSize() : -1);
		if (size <= 60) {
			LOGE("Cannot read \"{}\"", sourcePath);
			return false;
		}
		Array<char> data{NoInit, std::size_t(size)};
		if (s->Read(data.data(), size) != size || std::memcmp(data.data(), "Extended Module: ", 17) != 0) {
			LOGE("\"{}\" is not a FastTracker II module", sourcePath);
			return false;
		}
		s = nullptr;

		Converter converter;
		// The playback rate makes no difference to the sizes, which depend on the notes and the speed only
		if (xm_create_context_safe(&converter.Context, data.data(), data.size(), 48000) != 0 || converter.Context == nullptr) {
			LOGE("Cannot load \"{}\"", sourcePath);
			converter.Context = nullptr;
			return false;
		}
		xm_context_t* ctx = converter.Context;
		ctx->ctx_size += std::uint32_t((Lp64ChannelContextSize - sizeof(xm_channel_context_t)) * ctx->module.num_channels +
			(Lp64ContextSize - sizeof(xm_context_t)));

		converter.RemoveEmptySamples();
		if (!converter.PreprocessSamples()) {
			return false;
		}

		ChannelSizing sizing[32] = {};
		std::map<xm_sample_t*, float> loopFrequency[32];
		std::map<xm_sample_t*, std::int32_t> noteOnsFromStart, coldBurst;
		SmallVector<SmallVector<bool, 0>, 0> usedSamples;
		usedSamples.resize(ctx->module.num_instruments);
		for (std::int32_t i = 0; i < ctx->module.num_instruments; i++) {
			usedSamples[i].resize(ctx->module.instruments[i].num_samples, false);
		}

		converter.DryRun(sizing, loopFrequency, noteOnsFromStart, coldBurst, usedSamples);
		const std::int32_t sampleBufferSize = converter.SizeLoopBuffers(sizing, loopFrequency);
		converter.SelectAttackCache(noteOnsFromStart, coldBurst, sampleBufferSize);

		// Samples nobody plays are left out, but only from the end of an instrument so nothing has to be renumbered
		// in the patterns - nearly every instrument has a single sample anyway
		for (std::int32_t i = 0; i < ctx->module.num_instruments; i++) {
			xm_instrument_t* ins = &ctx->module.instruments[i];
			while (ins->num_samples > 0 && !usedSamples[i][ins->num_samples - 1]) {
				std::memset(&ins->samples[ins->num_samples - 1], 0, sizeof(xm_sample_t));
				ins->num_samples--;
			}
		}

		BigEndianWriter out;
		if (!converter.Save(out, fs::GetFileName(sourcePath))) {
			return false;
		}

		auto so = fs::Open(targetPath, FileAccess::Write);
		if (!so->IsValid() || so->Write(out.Data.data(), std::int64_t(out.Data.size())) != std::int64_t(out.Data.size())) {
			LOGE("Cannot write \"{}\"", targetPath);
			return false;
		}
		return true;
	}
}
