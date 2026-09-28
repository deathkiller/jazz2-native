#include "ModuleConverter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Core/Logger.h>
#include <IO/FileSystem.h>

#include <zlib.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	namespace
	{
		// ── Module Model ─────────────────────────────────────────────────────────────────────────

		/** @brief Note values of a cell, in libopenmpt's numbering (1 = C-0, 61 = C-5) */
		constexpr std::uint8_t NoteNone = 0;
		constexpr std::uint8_t NoteKeyOff = 0xFE;
		constexpr std::uint8_t NoteFade = 0xFD;
		constexpr std::uint8_t VolumeNone = 0xFF;
		constexpr std::uint8_t CommandNone = 0xFF;

		/** @brief Effect commands of the Galaxy format, as libopenmpt names them */
		enum class Effect : std::uint8_t {
			Arpeggio, PortamentoUp, PortamentoDown, TonePortamento, Vibrato, TonePortamentoVolume, VibratoVolume,
			Tremolo, Panning, Offset, VolumeSlide, PositionJump, Volume, PatternBreak, Extended, Tempo,
			GlobalVolume, GlobalVolumeSlide, KeyOff, EnvelopePosition, ChannelVolume, ChannelVolumeSlide,
			PanningSlide, Retrigger, Tremor, ExtraFinePortamento, Count
		};

		struct Cell
		{
			std::uint8_t Note = NoteNone;
			std::uint8_t Instrument = 0;
			std::uint8_t Volume = VolumeNone;		// 0-64
			std::uint8_t Command = CommandNone;		// Effect
			std::uint8_t Param = 0;
		};

		struct Pattern
		{
			std::int32_t Rows = 0;
			SmallVector<Cell, 0> Cells;				// Rows x channels
		};

		struct Sample
		{
			String Name;
			std::uint32_t Length = 0, LoopStart = 0, LoopEnd = 0;
			std::uint32_t C5Speed = 8363;
			bool Is16Bit = false, Loop = false, PingPong = false, HasPanning = false;
			std::int32_t Panning = 128;				// 0-256
			std::int32_t Volume = 64;				// 0-64
			Array<std::uint8_t> Data;				// Signed little-endian PCM
		};

		struct Envelope
		{
			bool Enabled = false, Sustain = false, Loop = false;
			std::uint8_t SustainPoint = 0, LoopStart = 0, LoopEnd = 0;
			SmallVector<std::pair<std::uint16_t, std::uint16_t>, 12> Points;	// tick, value 0-64
		};

		struct Instrument
		{
			bool Present = false;
			String Name;
			std::uint8_t SampleMap[128] = {};		// Sample index within the instrument per note - 1
			SmallVector<Sample, 1> Samples;
			Envelope VolumeEnvelope, PanningEnvelope;
			bool HasPitchEnvelope = false;
			std::uint16_t Fadeout = 0;				// XM units
			std::uint8_t VibratoType = 0, VibratoSweep = 0, VibratoDepth = 0, VibratoRate = 0;
		};

		struct Module
		{
			String Name;
			bool LinearSlides = true;
			std::uint8_t Speed = 6, Tempo = 125;
			std::int32_t ChannelCount = 0;
			std::int32_t ChannelPanning[32] = {};	// 0-256
			bool ChannelMuted[32] = {};
			SmallVector<std::uint8_t, 0> Orders;	// Raw, with the 0xFE (skip) and 0xFF (end) markers
			SmallVector<Pattern, 0> Patterns;		// By pattern number
			SmallVector<Instrument, 0> Instruments;	// By instrument number - 1
		};

		// ── Reading ──────────────────────────────────────────────────────────────────────────────

		class Reader
		{
		public:
			Reader(const std::uint8_t* data, std::size_t size) : _data(data), _size(size), _pos(0) {}

			bool CanRead(std::size_t n) const { return _pos + n <= _size; }
			std::size_t Position() const { return _pos; }
			std::size_t Size() const { return _size; }
			void Seek(std::size_t pos) { _pos = std::min(pos, _size); }
			void Skip(std::size_t n) { Seek(_pos + n); }
			const std::uint8_t* Current() const { return _data + _pos; }

			std::uint8_t U8() { return (CanRead(1) ? _data[_pos++] : 0); }
			std::uint16_t U16() { std::uint16_t v = (CanRead(2) ? std::uint16_t(_data[_pos] | (_data[_pos + 1] << 8)) : 0); Skip(2); return v; }
			std::int16_t S16() { return std::int16_t(U16()); }
			std::uint32_t U32() {
				std::uint32_t v = (CanRead(4) ? std::uint32_t(_data[_pos]) | (std::uint32_t(_data[_pos + 1]) << 8) |
					(std::uint32_t(_data[_pos + 2]) << 16) | (std::uint32_t(_data[_pos + 3]) << 24) : 0);
				Skip(4);
				return v;
			}
			String FixedString(std::size_t n) {
				std::size_t len = 0;
				while (len < n && CanRead(len + 1) && _data[_pos + len] != 0) {
					len++;
				}
				String s(reinterpret_cast<const char*>(_data + _pos), len);
				Skip(n);
				return s;
			}
			Reader Sub(std::size_t n) {
				const std::size_t available = (CanRead(n) ? n : _size - _pos);
				Reader r(_data + _pos, available);
				Skip(available);
				return r;
			}

		private:
			const std::uint8_t* _data;
			std::size_t _size;
			std::size_t _pos;
		};

		constexpr std::uint32_t FourCC(const char (&s)[5])
		{
			return std::uint32_t(std::uint8_t(s[0])) | (std::uint32_t(std::uint8_t(s[1])) << 8) |
				(std::uint32_t(std::uint8_t(s[2])) << 16) | (std::uint32_t(std::uint8_t(s[3])) << 24);
		}

		/** @brief Reads the samples of one sample header into @p sample, which already knows its length and width */
		void ReadSampleData(Reader& r, Sample& sample)
		{
			const std::size_t bytes = std::size_t(sample.Length) * (sample.Is16Bit ? 2 : 1);
			const std::size_t available = (r.CanRead(bytes) ? bytes : r.Size() - r.Position());
			sample.Data = Array<std::uint8_t>(ValueInit, bytes);
			std::memcpy(sample.Data.data(), r.Current(), available);
			r.Skip(available);
		}

		void ApplySampleFlags(Sample& sample, std::uint16_t flags)
		{
			// Same bits in both variants of the format
			sample.Is16Bit = (flags & 0x04) != 0;
			sample.Loop = (flags & 0x08) != 0;
			sample.PingPong = (flags & 0x10) != 0;
			sample.HasPanning = (flags & 0x20) != 0;
			if (sample.LoopEnd > sample.Length) {
				sample.LoopEnd = sample.Length;
			}
			if (sample.LoopStart >= sample.LoopEnd) {
				sample.Loop = sample.PingPong = false;
			}
		}

		/** @brief Converts an envelope of the new format (@p kind: 0 volume, 1 pitch, 2 panning) */
		void ReadAmEnvelope(Reader& r, std::int32_t kind, Envelope& env, bool& present, std::uint16_t& fadeout)
		{
			const std::uint16_t flags = r.U16();
			const std::uint8_t numPoints = r.U8();
			env.SustainPoint = r.U8();
			env.LoopStart = r.U8();
			env.LoopEnd = r.U8();
			std::uint16_t ticks[10];
			std::int16_t values[10];
			for (std::int32_t i = 0; i < 10; i++) {
				ticks[i] = r.U16();
				values[i] = r.S16();
			}
			fadeout = r.U16();	// Only meaningful in the volume envelope

			present = false;
			if (numPoints == 0xFF || numPoints == 0) {
				return;
			}
			present = (flags & 0x01) != 0;
			const std::int32_t count = std::min(numPoints + 1, 10);
			std::int32_t scale = 32767 / 64, offset = 0;
			if (kind == 1) {
				scale = 8192 / 64; offset = 4096;
			} else if (kind == 2) {
				scale = 65536 / 64; offset = 32768;
			}
			std::int32_t lastTick = -1;
			for (std::int32_t i = 0; i < count; i++) {
				std::int32_t tick = ticks[i] >> 4;
				if (i == 0) {
					tick = 0;
				} else if (tick <= lastTick) {
					tick = lastTick + 1;
				}
				lastTick = tick;
				std::int32_t value = (values[i] + offset + scale / 2) / scale;
				env.Points.push_back({ std::uint16_t(tick), std::uint16_t(std::clamp(value, 0, 64)) });
			}
			env.Enabled = present;
			env.Sustain = (flags & 0x02) != 0 && env.SustainPoint < count;
			env.Loop = (flags & 0x04) != 0 && env.LoopStart <= env.LoopEnd && env.LoopEnd < count;
		}

		void ReadAmffEnvelope(std::uint8_t flags, std::uint8_t numPoints, std::uint8_t sustain, std::uint8_t loopStart,
			std::uint8_t loopEnd, const std::uint16_t* ticks, const std::uint8_t* values, Envelope& env)
		{
			const std::int32_t count = std::min<std::int32_t>(numPoints, 10);
			std::int32_t lastTick = -1;
			for (std::int32_t i = 0; i < count; i++) {
				std::int32_t tick = ticks[i] >> 4;
				if (i == 0) {
					tick = 0;
				} else if (tick <= lastTick) {
					tick = lastTick + 1;
				}
				lastTick = tick;
				env.Points.push_back({ std::uint16_t(tick), std::uint16_t(std::min<std::int32_t>(values[i], 64)) });
			}
			env.SustainPoint = sustain;
			env.LoopStart = loopStart;
			env.LoopEnd = loopEnd;
			env.Enabled = (flags & 0x01) != 0 && count > 0;
			env.Sustain = (flags & 0x02) != 0 && sustain < count;
			env.Loop = (flags & 0x04) != 0 && loopStart <= loopEnd && loopEnd < count;
		}

		void ReadPattern(Reader r, bool isAM, Module& module, Pattern& pattern)
		{
			if (!r.CanRead(1)) {
				return;
			}
			pattern.Rows = std::min<std::int32_t>(r.U8() + 1, 256);
			pattern.Cells = SmallVector<Cell, 0>(ValueInit, std::size_t(pattern.Rows) * module.ChannelCount);

			std::int32_t row = 0;
			while (row < pattern.Rows && r.CanRead(1)) {
				const std::uint8_t flags = r.U8();
				if (flags == 0) {
					row++;
					continue;
				}
				if ((flags & 0xE0) == 0) {
					continue;
				}
				const std::int32_t channel = std::min<std::int32_t>(flags & 0x1F, module.ChannelCount - 1);
				Cell& cell = pattern.Cells[std::size_t(row) * module.ChannelCount + channel];

				if (flags & 0x80) {
					const std::uint8_t param = r.U8();
					const std::uint8_t command = r.U8();
					if (command < std::uint8_t(Effect::Count)) {
						cell.Command = command;
						cell.Param = param;
					}
				}
				if (flags & 0x40) {
					cell.Instrument = r.U8();
					const std::uint8_t note = r.U8();
					cell.Note = (note == 0x80 ? NoteKeyOff : (note > 0x80 ? NoteFade : note));
				}
				if (flags & 0x20) {
					std::uint8_t volume = r.U8();
					if (isAM) {
						volume = std::uint8_t(volume * 64u / 127u);
					}
					cell.Volume = std::min<std::uint8_t>(volume, 64);
				}

				// Effects that libopenmpt's loader rewrites while reading
				switch (Effect(cell.Command)) {
					case Effect::Arpeggio:
						if (cell.Param == 0) {
							cell.Command = CommandNone;
						}
						break;
					case Effect::Volume:
						if (cell.Volume == VolumeNone) {
							cell.Volume = std::min<std::uint8_t>(cell.Param, 64);
							cell.Command = CommandNone;
						}
						break;
					case Effect::TonePortamentoVolume:
					case Effect::VibratoVolume:
					case Effect::VolumeSlide:
					case Effect::GlobalVolumeSlide:
					case Effect::PanningSlide:
						// An upward slide wins when both nibbles are set, as in MOD and XM
						if (cell.Param & 0xF0) {
							cell.Param &= 0xF0;
						}
						break;
					default:
						break;
				}
			}
		}

		bool ParseModule(const std::uint8_t* data, std::size_t size, Module& module)
		{
			Reader file(data, size);
			if (file.U32() != FourCC("RIFF")) {
				return false;
			}
			const std::uint32_t riffLength = file.U32();
			const std::uint32_t format = file.U32();
			const bool isAM = (format == FourCC("AM  "));
			if (!isAM && format != FourCC("AMFF")) {
				return false;
			}
			Reader chunks = file.Sub(riffLength >= 4 ? riffLength - 4 : 0);

			bool hasMain = false;
			while (chunks.CanRead(8)) {
				const std::uint32_t id = chunks.U32();
				const std::uint32_t length = chunks.U32();
				Reader chunk = chunks.Sub(length);
				if (isAM && (length & 1) != 0) {
					chunks.Skip(1);		// RIFF AM pads every chunk to an even size
				}

				if (id == (isAM ? FourCC("INIT") : FourCC("MAIN"))) {
					module.Name = chunk.FixedString(64);
					const std::uint8_t flags = chunk.U8();
					module.ChannelCount = std::min<std::int32_t>(chunk.U8(), 32);
					module.Speed = chunk.U8();
					module.Tempo = chunk.U8();
					chunk.U16();	// Minimum and maximum period, ignored by libopenmpt as well
					chunk.U16();
					chunk.U8();		// Global volume
					module.LinearSlides = (flags & 0x01) == 0;
					for (std::int32_t ch = 0; ch < module.ChannelCount; ch++) {
						const std::uint8_t pan = chunk.U8();
						if (isAM) {
							module.ChannelMuted[ch] = (pan > 128);
							module.ChannelPanning[ch] = (pan > 128 ? 128 : pan * 2);
						} else {
							module.ChannelMuted[ch] = (pan >= 128);
							module.ChannelPanning[ch] = std::min(pan * 4, 256);
						}
					}
					hasMain = (module.ChannelCount > 0);
				} else if (id == FourCC("ORDR")) {
					const std::int32_t count = chunk.U8() + 1;
					for (std::int32_t i = 0; i < count && chunk.CanRead(1); i++) {
						module.Orders.push_back(chunk.U8());
					}
				} else if (id == FourCC("PATT")) {
					if (!hasMain) {
						continue;
					}
					const std::uint8_t index = chunk.U8();
					const std::uint32_t patternSize = chunk.U32();
					if (module.Patterns.size() <= index) {
						module.Patterns.resize(std::size_t(index) + 1);
					}
					ReadPattern(chunk.Sub(patternSize), isAM, module, module.Patterns[index]);
				} else if (!isAM && id == FourCC("INST")) {
					// RIFF AMFF: header followed directly by the sample headers and their data
					chunk.U8();
					const std::int32_t index = chunk.U8();
					if (module.Instruments.size() <= std::size_t(index)) {
						module.Instruments.resize(std::size_t(index) + 1);
					}
					Instrument& ins = module.Instruments[index];
					ins.Present = true;
					ins.Name = chunk.FixedString(28);
					const std::int32_t numSamples = chunk.U8();
					for (std::int32_t i = 0; i < 120; i++) {
						ins.SampleMap[i] = chunk.U8();
					}
					ins.VibratoType = chunk.U8();
					ins.VibratoSweep = std::uint8_t(chunk.U16());
					ins.VibratoDepth = std::uint8_t(chunk.U16() / 4);
					ins.VibratoRate = std::uint8_t(chunk.U16() / 16);
					const std::uint8_t envFlags = chunk.U8(), envPoints = chunk.U8(), envSustain = chunk.U8();
					const std::uint8_t envLoopStart = chunk.U8(), envLoopEnd = chunk.U8();
					std::uint16_t volTicks[10], panTicks[10];
					std::uint8_t volValues[10], panValues[10];
					for (std::int32_t i = 0; i < 10; i++) { volTicks[i] = chunk.U16(); volValues[i] = chunk.U8(); }
					for (std::int32_t i = 0; i < 10; i++) { panTicks[i] = chunk.U16(); panValues[i] = chunk.U8(); }
					ReadAmffEnvelope(envFlags & 0x0F, envPoints & 0x0F, envSustain & 0x0F, envLoopStart & 0x0F, envLoopEnd & 0x0F,
						volTicks, volValues, ins.VolumeEnvelope);
					ReadAmffEnvelope(envFlags >> 4, envPoints >> 4, envSustain >> 4, envLoopStart >> 4, envLoopEnd >> 4,
						panTicks, panValues, ins.PanningEnvelope);
					ins.Fadeout = std::uint16_t(std::min<std::uint32_t>(std::uint32_t(chunk.U16()) << 5, 0xFFF));

					for (std::int32_t s = 0; s < numSamples && chunk.CanRead(64); s++) {
						Sample& smp = ins.Samples.emplace_back();
						if (chunk.U32() != FourCC("SAMP")) {
							break;
						}
						chunk.U32();	// Chunk size
						smp.Name = chunk.FixedString(28);
						smp.Panning = std::min(chunk.U8() * 4, 256);
						smp.Volume = std::min<std::int32_t>(chunk.U8(), 64);
						const std::uint16_t flags = chunk.U16();
						smp.Length = chunk.U32();
						smp.LoopStart = chunk.U32();
						smp.LoopEnd = chunk.U32();
						smp.C5Speed = chunk.U32();
						chunk.U32();
						chunk.U32();
						ApplySampleFlags(smp, flags);
						ReadSampleData(chunk, smp);
					}
				} else if (isAM && id == FourCC("RIFF")) {
					// RIFF AM: "AI  " form holding an "INST" chunk and one nested "AS  " form per sample
					if (chunk.U32() != FourCC("AI  ")) {
						continue;
					}
					if (chunk.U32() != FourCC("INST")) {
						continue;
					}
					// The chunk length counts the header without its own size field, so it is four bytes short of
					// the header - libopenmpt reads the 326 bytes regardless, and the samples follow right after them
					chunk.U32();
					Reader header = chunk.Sub(326);
					header.U32();	// Header size
					header.U8();
					const std::int32_t index = header.U8();
					if (module.Instruments.size() <= std::size_t(index)) {
						module.Instruments.resize(std::size_t(index) + 1);
					}
					Instrument& ins = module.Instruments[index];
					ins.Present = true;
					ins.Name = header.FixedString(32);
					for (std::int32_t i = 0; i < 128; i++) {
						ins.SampleMap[i] = header.U8();
					}
					ins.VibratoType = header.U8();
					ins.VibratoSweep = std::uint8_t(header.U16());
					ins.VibratoDepth = std::uint8_t(header.U16() / 4);
					ins.VibratoRate = std::uint8_t(header.U16() / 16);
					header.Skip(7);
					bool present;
					std::uint16_t fadeout, unused;
					ReadAmEnvelope(header, 0, ins.VolumeEnvelope, present, fadeout);
					ins.Fadeout = std::uint16_t(std::min<std::uint32_t>(std::uint32_t(fadeout) << 5, 0xFFF));
					Envelope pitchEnvelope;
					ReadAmEnvelope(header, 1, pitchEnvelope, ins.HasPitchEnvelope, unused);
					ReadAmEnvelope(header, 2, ins.PanningEnvelope, present, unused);
					std::int32_t numSamples = header.U16();
					if (numSamples == 0) {
						std::memset(ins.SampleMap, 0, sizeof(ins.SampleMap));
					}

					while (chunk.CanRead(8) && numSamples > 0) {
						const std::uint32_t sampleId = chunk.U32();
						const std::uint32_t sampleLength = chunk.U32();
						Reader sampleForm = chunk.Sub(sampleLength);
						if (sampleLength & 1) {
							chunk.Skip(1);
						}
						if (sampleId != FourCC("RIFF") || sampleForm.U32() != FourCC("AS  ")) {
							continue;
						}
						if (sampleForm.U32() != FourCC("SAMP")) {
							continue;
						}
						Reader samp = sampleForm.Sub(sampleForm.U32());
						numSamples--;

						Sample& smp = ins.Samples.emplace_back();
						const std::uint32_t headSize = samp.U32();
						smp.Name = samp.FixedString(32);
						smp.Panning = std::int32_t(std::min<std::uint32_t>(samp.U16(), 32767) * 256 / 32767);
						smp.Volume = std::int32_t((std::min<std::uint32_t>(samp.U16(), 32767) * 64 + 16383) / 32767);
						const std::uint16_t flags = samp.U16();
						samp.U16();
						smp.Length = samp.U32();
						smp.LoopStart = samp.U32();
						smp.LoopEnd = samp.U32();
						smp.C5Speed = samp.U32();
						ApplySampleFlags(smp, flags);
						samp.Seek(std::size_t(headSize) + 4);
						ReadSampleData(samp, smp);
					}
				}
			}
			return hasMain;
		}

		// ── Conversion ───────────────────────────────────────────────────────────────────────────

		/** @brief Pan position libopenmpt gives a 4-bit panning command (`S8x`, which `E8x` becomes) */
		std::int32_t Pan4Bit(std::int32_t value)
		{
			return (value * 256 + 8) / 15;
		}

		/** @brief Panning a pan effect of the cell sets the channel to, or -1 */
		std::int32_t CellPanning(const Cell& cell)
		{
			if (cell.Command == std::uint8_t(Effect::Panning)) {
				if (cell.Param <= 0x80) {
					return std::min(cell.Param * 2, 256);
				}
				if (cell.Param == 0xA4) {
					return 128;		// Surround, which XM has no notion of
				}
			} else if (cell.Command == std::uint8_t(Effect::Extended) && (cell.Param >> 4) == 0x8) {
				return Pan4Bit(cell.Param & 0x0F);
			}
			return -1;
		}

		/** @brief Largest panning difference (of 256) treated as the same position */
		constexpr std::int32_t PanningTolerance = 12;
		/** @brief Fewest notes a panning has to be heard on before an instrument is copied for it */
		constexpr std::int32_t MinVariantUses = 16;

		/** @brief One instrument as written into the XM: an original one, played at one panning */
		struct XmInstrument
		{
			std::int32_t Source = -1;		// Index into Module::Instruments
			std::int32_t Panning = 128;		// 0-256, applied to every sample that has none of its own
			std::int32_t NoteShift = 0;		// Octaves the notes are moved down by (samples up by) to fit XM's range
			std::int32_t Distinguish = 0;	// Makes the sample data differ from the other copies, see DistinguishSeekedVariants()
		};

		class Converter
		{
		public:
			Converter(Module& module) : _module(module) {}

			bool Convert(Stream& out)
			{
				BuildOrders();
				if (_orders.empty()) {
					LOGW("The module has no playable order list");
					return false;
				}
				SimulatePanning();
				CreateInstruments();
				DistinguishSeekedVariants();
				ComputeNoteShifts();
				return Write(out);
			}

		private:
			Module& _module;
			/** @brief Pattern of each XM order position, the original's with skip markers removed */
			SmallVector<std::int32_t, 0> _orders;
			/** @brief XM order position of each original order position, for position jumps */
			SmallVector<std::int32_t, 0> _orderRemap;
			/** @brief Panning each cell with an instrument is heard at, per XM order position (unrolled) */
			SmallVector<SmallVector<std::int16_t, 0>, 0> _cellPanning;
			SmallVector<XmInstrument, 0> _instruments;
			/** @brief XM instrument of each (original instrument, panning) pair that got one */
			SmallVector<SmallVector<std::pair<std::int32_t, std::int32_t>, 4>, 0> _variants;	// panning, XM index
			std::int32_t _droppedEffects[std::int32_t(Effect::Count)] = {};

			const Pattern* GetPattern(std::int32_t index) const
			{
				return (index >= 0 && std::size_t(index) < _module.Patterns.size() && _module.Patterns[index].Rows > 0
					? &_module.Patterns[index] : nullptr);
			}

			void BuildOrders()
			{
				_orderRemap.resize(_module.Orders.size());
				for (std::size_t i = 0; i < _module.Orders.size(); i++) {
					_orderRemap[i] = std::int32_t(_orders.size());
					const std::uint8_t pattern = _module.Orders[i];
					if (pattern == 0xFF) {
						_orderRemap.resize(i + 1);
						break;
					}
					if (pattern == 0xFE || GetPattern(pattern) == nullptr) {
						continue;
					}
					_orders.push_back(pattern);
				}
				// XM allows 256 order entries at most
				if (_orders.size() > 256) {
					_orders.resize(256);
				}
				_cellPanning.resize(_orders.size());
				for (std::size_t o = 0; o < _orders.size(); o++) {
					const Pattern& pattern = *GetPattern(_orders[o]);
					_cellPanning[o] = SmallVector<std::int16_t, 0>(ValueInit, pattern.Cells.size());
					std::fill(_cellPanning[o].begin(), _cellPanning[o].end(), std::int16_t(-1));
				}
			}

			/** @brief Follows the playback order to learn which panning every note is played at */
			void SimulatePanning()
			{
				std::int32_t pan[32];
				for (std::int32_t ch = 0; ch < _module.ChannelCount; ch++) {
					pan[ch] = _module.ChannelPanning[ch];
				}
				SmallVector<bool, 0> visitedOrder(ValueInit, _orders.size());

				std::int32_t order = 0, row = 0;
				for (std::int32_t steps = 0; steps < 65536 && order < std::int32_t(_orders.size()); steps++) {
					const Pattern& pattern = *GetPattern(_orders[order]);
					if (row == 0 || !visitedOrder[order]) {
						visitedOrder[order] = true;
					}
					if (row >= pattern.Rows) {
						order++;
						row = 0;
						continue;
					}

					std::int32_t jumpOrder = -1, breakRow = -1;
					for (std::int32_t ch = 0; ch < _module.ChannelCount; ch++) {
						const std::size_t index = std::size_t(row) * _module.ChannelCount + ch;
						const Cell& cell = pattern.Cells[index];
						const std::int32_t newPan = CellPanning(cell);
						if (newPan >= 0) {
							pan[ch] = newPan;
						}
						if (cell.Instrument != 0) {
							_cellPanning[order][index] = std::int16_t(pan[ch]);
						}
						if (cell.Command == std::uint8_t(Effect::PositionJump)) {
							jumpOrder = (cell.Param < _orderRemap.size() ? _orderRemap[cell.Param] : std::int32_t(_orders.size()));
						} else if (cell.Command == std::uint8_t(Effect::PatternBreak)) {
							breakRow = (cell.Param >> 4) * 10 + (cell.Param & 0x0F);
						}
					}

					if (jumpOrder >= 0 || breakRow >= 0) {
						const std::int32_t nextOrder = (jumpOrder >= 0 ? jumpOrder : order + 1);
						if (nextOrder <= order && nextOrder < std::int32_t(_orders.size()) && visitedOrder[nextOrder]) {
							break;		// The song loops from here
						}
						order = nextOrder;
						row = (breakRow >= 0 ? breakRow : 0);
					} else {
						row++;
					}
				}

				// A cell the playback never reaches plays at its channel's initial panning
				for (std::size_t o = 0; o < _orders.size(); o++) {
					const Pattern& pattern = *GetPattern(_orders[o]);
					for (std::size_t i = 0; i < pattern.Cells.size(); i++) {
						if (pattern.Cells[i].Instrument != 0 && _cellPanning[o][i] < 0) {
							_cellPanning[o][i] = std::int16_t(_module.ChannelPanning[i % _module.ChannelCount]);
						}
					}
				}
			}

			bool InstrumentHasFixedPanning(const Instrument& ins) const
			{
				// Samples with a panning of their own reset the channel's on every note in libopenmpt too, so an
				// instrument made only of those needs no variants
				for (const Sample& smp : ins.Samples) {
					if (!smp.HasPanning) {
						return false;
					}
				}
				return !ins.Samples.empty();
			}

			std::size_t InstrumentBytes(const Instrument& ins) const
			{
				std::size_t bytes = 0;
				for (const Sample& smp : ins.Samples) {
					bytes += smp.Data.size();
				}
				return bytes;
			}

			void CreateInstruments()
			{
				const std::size_t count = _module.Instruments.size();
				_variants.resize(count);

				// How often each instrument is heard at each panning
				SmallVector<SmallVector<std::pair<std::int32_t, std::int32_t>, 4>, 0> usage(count);	// panning, count
				for (std::size_t o = 0; o < _orders.size(); o++) {
					const Pattern& pattern = *GetPattern(_orders[o]);
					for (std::size_t i = 0; i < pattern.Cells.size(); i++) {
						const std::int32_t ins = pattern.Cells[i].Instrument - 1;
						if (ins < 0 || std::size_t(ins) >= count || !_module.Instruments[ins].Present) {
							continue;
						}
						const std::int32_t pan = (InstrumentHasFixedPanning(_module.Instruments[ins]) ? 128 : _cellPanning[o][i]);
						auto& list = usage[ins];
						auto it = std::find_if(list.begin(), list.end(), [pan](const auto& p) { return p.first == pan; });
						if (it != list.end()) {
							it->second++;
						} else {
							list.push_back({ pan, 1 });
						}
					}
				}

				// Pannings close to each other are one: a difference of a few percent of the stereo field is not
				// something anyone hears, while a variant costs the instrument's whole sample data again
				for (std::size_t ins = 0; ins < count; ins++) {
					auto& list = usage[ins];
					std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
					SmallVector<std::pair<std::int32_t, std::int32_t>, 4> clusters;
					for (const auto& u : list) {
						auto it = std::find_if(clusters.begin(), clusters.end(), [&u](const auto& c) { return std::abs(c.first - u.first) <= PanningTolerance; });
						if (it != clusters.end()) {
							it->second += u.second;
						} else {
							clusters.push_back(u);
						}
					}
					std::sort(clusters.begin(), clusters.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
					list = std::move(clusters);
				}

				// Every used instrument gets its most frequent panning in its own slot first, so the numbering of
				// the original survives wherever no variant is needed
				std::size_t baseBytes = 0;
				for (std::size_t ins = 0; ins < count; ins++) {
					baseBytes += InstrumentBytes(_module.Instruments[ins]);
				}
				_instruments.resize(count);
				for (std::size_t ins = 0; ins < count; ins++) {
					_instruments[ins].Source = std::int32_t(ins);
					_instruments[ins].Panning = (usage[ins].empty() ? 128 : usage[ins][0].first);
					if (!usage[ins].empty()) {
						_variants[ins].push_back({ _instruments[ins].Panning, std::int32_t(ins) });
					}
				}

				// Further pannings, most used first, as long as XM's 128 instruments and half as much sample data
				// again allow. A panning heard on only a few notes is not worth a copy of the instrument either: it
				// takes the nearest variant and a panning command, which is exact wherever the row has room for it.
				struct Candidate { std::int32_t Instrument, Panning, Uses; };
				SmallVector<Candidate, 0> candidates;
				for (std::size_t ins = 0; ins < count; ins++) {
					std::int32_t total = 0;
					for (const auto& u : usage[ins]) {
						total += u.second;
					}
					for (std::size_t k = 1; k < usage[ins].size(); k++) {
						if (usage[ins][k].second >= std::max(MinVariantUses, total / 10)) {
							candidates.push_back({ std::int32_t(ins), usage[ins][k].first, usage[ins][k].second });
						}
					}
				}
				std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.Uses > b.Uses; });
				std::size_t extraBytes = 0;
				std::int32_t skipped = 0;
				for (const Candidate& c : candidates) {
					const std::size_t bytes = InstrumentBytes(_module.Instruments[c.Instrument]);
					if (_instruments.size() >= 128 || extraBytes + bytes > baseBytes / 2) {
						skipped++;
						continue;
					}
					extraBytes += bytes;
					XmInstrument variant;
					variant.Source = c.Instrument;
					variant.Panning = c.Panning;
					_variants[c.Instrument].push_back({ c.Panning, std::int32_t(_instruments.size()) });
					_instruments.push_back(variant);
				}
				if (extraBytes > 0 || skipped > 0) {
					LOGI("  Channel panning kept with {} instrument variants (+{} KB of samples){}",
						std::int32_t(_instruments.size() - count), extraBytes / 1024,
						skipped > 0 ? " - some pannings approximated" : "");
				}
			}

			/**
				@brief Gives each panning variant of an instrument that is played from a sample offset data of its own

				A variant is a copy of its instrument's samples, and libdragon's audioconv64 stores identical sample
				data only once, keeping the seek points of the FIRST instrument that has it --- the points it
				collects for every 9xx a sample is started from, and the only places a compressed sample can be
				started at. A variant heard with an offset its original never uses then has no seek point for it,
				and the console asserts ("invalid VADPCM seeking point") the moment that note plays; `carrotus` did
				on its first rows. So such variants are made to differ, inaudibly, which costs a stored copy of just
				those samples.
			*/
			void DistinguishSeekedVariants()
			{
				SmallVector<bool, 0> seeked(ValueInit, _module.Instruments.size());
				bool any = false;
				for (std::size_t o = 0; o < _orders.size(); o++) {
					const Pattern& pattern = *GetPattern(_orders[o]);
					SmallVector<std::int32_t, 32> current(ValueInit, std::size_t(_module.ChannelCount));
					for (std::int32_t row = 0; row < pattern.Rows; row++) {
						for (std::int32_t ch = 0; ch < _module.ChannelCount; ch++) {
							const Cell& cell = pattern.Cells[std::size_t(row) * _module.ChannelCount + ch];
							if (cell.Instrument != 0) {
								current[ch] = cell.Instrument;
							}
							if (Effect(cell.Command) == Effect::Offset && cell.Param != 0 &&
								current[ch] > 0 && std::size_t(current[ch] - 1) < seeked.size()) {
								seeked[current[ch] - 1] = true;
								any = true;
							}
						}
					}
				}
				if (!any) {
					return;
				}

				// The original instrument keeps its slot, which comes before every variant, so it is the copy
				// audioconv64 keeps; the variants after it are told apart from it and from each other
				for (std::size_t ins = 0; ins < _variants.size(); ins++) {
					if (!seeked[ins]) {
						continue;
					}
					std::int32_t ordinal = 0;
					for (const auto& v : _variants[ins]) {
						if (v.second != std::int32_t(ins)) {
							_instruments[v.second].Distinguish = ++ordinal;
						}
					}
				}
			}

			/** @brief Returns the XM instrument for an original one at a panning, and whether that is exact */
			std::int32_t FindVariant(std::int32_t ins, std::int32_t pan, bool& exact) const
			{
				const auto& list = _variants[ins];
				std::int32_t best = -1, bestDistance = INT32_MAX;
				for (const auto& v : list) {
					const std::int32_t distance = std::abs(v.first - pan);
					if (distance < bestDistance) {
						bestDistance = distance;
						best = v.second;
					}
				}
				exact = (bestDistance <= PanningTolerance) || InstrumentHasFixedPanning(_module.Instruments[ins]);
				return best;
			}

			void ComputeNoteShifts()
			{
				// XM notes run from C-0 to B-7, which is libopenmpt's 13 to 108; an instrument played outside of that
				// has its notes moved down (or up) by whole octaves and its samples transposed the other way
				SmallVector<std::pair<std::int32_t, std::int32_t>, 0> range(ValueInit, _module.Instruments.size());
				std::fill(range.begin(), range.end(), std::pair<std::int32_t, std::int32_t>(255, 0));
				SmallVector<std::int32_t, 32> current(ValueInit, std::size_t(_module.ChannelCount));
				for (std::size_t o = 0; o < _orders.size(); o++) {
					const Pattern& pattern = *GetPattern(_orders[o]);
					for (std::size_t i = 0; i < pattern.Cells.size(); i++) {
						const Cell& cell = pattern.Cells[i];
						const std::int32_t ch = std::int32_t(i % _module.ChannelCount);
						if (cell.Instrument != 0) {
							current[ch] = cell.Instrument;
						}
						const std::int32_t ins = current[ch] - 1;
						if (ins >= 0 && std::size_t(ins) < range.size() && cell.Note >= 1 && cell.Note <= 120) {
							range[ins].first = std::min<std::int32_t>(range[ins].first, cell.Note);
							range[ins].second = std::max<std::int32_t>(range[ins].second, cell.Note);
						}
					}
				}
				for (XmInstrument& xmIns : _instruments) {
					const auto& r = range[xmIns.Source];
					std::int32_t shift = 0;
					if (r.first <= r.second) {
						while (r.second - shift > 108 && r.first - shift - 12 >= 13) shift += 12;
						while (r.first - shift < 13 && r.second - shift + 12 <= 108) shift -= 12;
					}
					xmIns.NoteShift = shift;
				}
			}

			// ── Writing ──────────────────────────────────────────────────────────────────────────

			static void WriteFixed(Stream& s, StringView value, std::size_t size)
			{
				char buffer[64] = {};
				std::memcpy(buffer, value.data(), std::min(value.size(), size));
				s.Write(buffer, std::int64_t(size));
			}

			bool TranslateEffect(const Cell& cell, std::uint8_t& command, std::uint8_t& param)
			{
				command = 0;
				param = cell.Param;
				switch (Effect(cell.Command)) {
					case Effect::Arpeggio: command = 0x0; return true;
					case Effect::PortamentoUp:
					case Effect::PortamentoDown: {
						const bool up = (Effect(cell.Command) == Effect::PortamentoUp);
						if (cell.Param >= 0xF0) {		// Fine, IT style
							command = 0xE; param = std::uint8_t((up ? 0x10 : 0x20) | (cell.Param & 0x0F));
						} else if (cell.Param >= 0xE0) {	// Extra fine
							command = 0x21; param = std::uint8_t((up ? 0x10 : 0x20) | (cell.Param & 0x0F));
						} else {
							command = (up ? 0x1 : 0x2);
						}
						return true;
					}
					case Effect::TonePortamento: command = 0x3; return true;
					case Effect::Vibrato: command = 0x4; return true;
					case Effect::TonePortamentoVolume: command = 0x5; return true;
					case Effect::VibratoVolume: command = 0x6; return true;
					case Effect::Tremolo: command = 0x7; return true;
					case Effect::Panning: command = 0x8; param = std::uint8_t(std::min(CellPanning(cell), 255)); return true;
					case Effect::Offset: command = 0x9; return true;
					case Effect::VolumeSlide: command = 0xA; return true;
					case Effect::PositionJump:
						command = 0xB;
						param = std::uint8_t(cell.Param < _orderRemap.size() ? std::min(_orderRemap[cell.Param], std::int32_t(_orders.size()) - 1) : 0);
						return true;
					case Effect::Volume: command = 0xC; param = std::min<std::uint8_t>(cell.Param, 64); return true;
					case Effect::PatternBreak: command = 0xD; return true;
					case Effect::Extended: {
						const std::uint8_t sub = cell.Param >> 4;
						switch (sub) {
							case 0x8:	// Coarse panning, which FT2 does not know - a full panning command instead
								command = 0x8; param = std::uint8_t(std::min(CellPanning(cell), 255)); return true;
							case 0x0:	// Amiga filter
							case 0xF:	// Invert loop
								_droppedEffects[std::int32_t(Effect::Extended)]++;
								return false;
							default:
								command = 0xE; return true;
						}
					}
					case Effect::Tempo: command = 0xF; return true;
					case Effect::GlobalVolume: command = 0x10; param = std::uint8_t(std::min(cell.Param / 2, 64)); return true;
					case Effect::GlobalVolumeSlide: command = 0x11; param = std::uint8_t(((cell.Param >> 5) << 4) | ((cell.Param & 0x0F) >> 1)); return true;
					case Effect::KeyOff: command = 0x14; return true;
					case Effect::EnvelopePosition: command = 0x15; return true;
					case Effect::PanningSlide: command = 0x19; return true;
					case Effect::Retrigger: command = 0x1B; return true;
					case Effect::Tremor: command = 0x1D; return true;
					case Effect::ExtraFinePortamento: {
						const std::uint8_t sub = cell.Param & 0xF0;
						if (sub == 0x10 || sub == 0x20) {
							command = 0x21; param = std::uint8_t(sub | (cell.Param & 0x0F)); return true;
						}
						return false;
					}
					default:
						if (cell.Command < std::uint8_t(Effect::Count)) {
							_droppedEffects[cell.Command]++;
						}
						return false;
				}
			}

			void WritePattern(Stream& s, std::int32_t order)
			{
				const Pattern& pattern = *GetPattern(_orders[order]);
				SmallVector<std::uint8_t, 0> packed;
				packed.reserve(std::size_t(pattern.Rows) * _module.ChannelCount * 3);
				SmallVector<std::int32_t, 32> currentXmIns(ValueInit, std::size_t(_module.ChannelCount));

				for (std::int32_t row = 0; row < pattern.Rows; row++) {
					for (std::int32_t ch = 0; ch < _module.ChannelCount; ch++) {
						const std::size_t index = std::size_t(row) * _module.ChannelCount + ch;
						const Cell& cell = pattern.Cells[index];
						if (_module.ChannelMuted[ch]) {
							packed.push_back(0x80);
							continue;
						}

						std::uint8_t note = 0, instrument = 0, volume = 0, command = 0, param = 0;
						bool hasCommand = TranslateEffect(cell, command, param);
						if (cell.Volume != VolumeNone) {
							volume = std::uint8_t(0x10 + cell.Volume);
						}

						std::int32_t xmIns = currentXmIns[ch];
						if (cell.Instrument != 0 && std::size_t(cell.Instrument - 1) < _module.Instruments.size() &&
							_module.Instruments[cell.Instrument - 1].Present) {
							bool exact;
							const std::int32_t pan = _cellPanning[order][index];
							xmIns = FindVariant(cell.Instrument - 1, pan, exact) + 1;
							if (xmIns > 0) {
								instrument = std::uint8_t(xmIns);
								currentXmIns[ch] = xmIns;
								if (!exact) {
									// No variant at exactly this panning: say it with a command where the row has room
									if (!hasCommand) {
										hasCommand = true; command = 0x8; param = std::uint8_t(std::min<std::int32_t>(pan, 255));
									} else if (volume == 0) {
										volume = std::uint8_t(0xC0 | std::min(pan / 16, 15));
									}
								}
							}
						}

						if (cell.Note == NoteKeyOff || cell.Note == NoteFade) {
							note = 97;
						} else if (cell.Note >= 1 && cell.Note <= 120) {
							const std::int32_t shift = (xmIns > 0 ? _instruments[xmIns - 1].NoteShift : 0);
							const std::int32_t xmNote = cell.Note - 12 - shift;
							note = std::uint8_t(xmNote >= 1 && xmNote <= 96 ? xmNote : 0);
						}

						std::uint8_t mask = 0;
						if (note != 0) mask |= 0x01;
						if (instrument != 0) mask |= 0x02;
						if (volume != 0) mask |= 0x04;
						if (hasCommand && command != 0) mask |= 0x08;
						if (hasCommand && param != 0) mask |= 0x10;
						if (mask == 0x1F) {
							packed.push_back(note); packed.push_back(instrument); packed.push_back(volume);
							packed.push_back(command); packed.push_back(param);
						} else {
							packed.push_back(std::uint8_t(0x80 | mask));
							if (mask & 0x01) packed.push_back(note);
							if (mask & 0x02) packed.push_back(instrument);
							if (mask & 0x04) packed.push_back(volume);
							if (mask & 0x08) packed.push_back(command);
							if (mask & 0x10) packed.push_back(param);
						}
					}
				}

				s.WriteValueAsLE<std::uint32_t>(9);
				s.WriteValue<std::uint8_t>(0);
				s.WriteValueAsLE<std::uint16_t>(std::uint16_t(pattern.Rows));
				s.WriteValueAsLE<std::uint16_t>(std::uint16_t(packed.size()));
				s.Write(packed.data(), std::int64_t(packed.size()));
			}

			static void WriteEnvelope(Stream& s, const Envelope& env)
			{
				for (std::int32_t i = 0; i < 12; i++) {
					const bool present = (std::size_t(i) < env.Points.size() && i < 12);
					s.WriteValueAsLE<std::uint16_t>(present ? env.Points[i].first : 0);
					s.WriteValueAsLE<std::uint16_t>(present ? env.Points[i].second : 0);
				}
			}

			static std::uint8_t EnvelopeFlags(const Envelope& env)
			{
				if (!env.Enabled || env.Points.empty()) {
					return 0;
				}
				return std::uint8_t(0x01 | (env.Sustain ? 0x02 : 0) | (env.Loop ? 0x04 : 0));
			}

			void WriteInstrument(Stream& s, const XmInstrument& xmIns)
			{
				const Instrument& ins = _module.Instruments[xmIns.Source];
				const std::int32_t sampleCount = (ins.Present ? std::min<std::int32_t>(std::int32_t(ins.Samples.size()), 16) : 0);

				if (sampleCount == 0) {
					// The full header even without samples, the way FastTracker II writes it. The short form is valid
					// XM too, but audioconv64 turns a module that uses it into an XM64 file the console cannot load -
					// xm64player_open() either hangs or crashes on a NULL allocation, depending on the module.
					s.WriteValueAsLE<std::uint32_t>(263);
					WriteFixed(s, ins.Name, 22);
					s.WriteValue<std::uint8_t>(0);
					s.WriteValueAsLE<std::uint16_t>(0);
					std::uint8_t padding[263 - 29] = {};
					s.Write(padding, sizeof(padding));
					return;
				}

				s.WriteValueAsLE<std::uint32_t>(263);
				WriteFixed(s, ins.Name, 22);
				s.WriteValue<std::uint8_t>(0);
				s.WriteValueAsLE<std::uint16_t>(std::uint16_t(sampleCount));
				s.WriteValueAsLE<std::uint32_t>(40);

				// XM note k plays the original's note k + 13 + shift, which is SampleMap[k + 12 + shift]
				for (std::int32_t k = 0; k < 96; k++) {
					const std::int32_t index = k + 12 + xmIns.NoteShift;
					std::uint8_t sample = (index >= 0 && index < 128 ? ins.SampleMap[index] : 0);
					s.WriteValue<std::uint8_t>(sample < sampleCount ? sample : 0);
				}
				// A key-off leaves a note without a volume envelope sounding in the Galaxy format (libopenmpt plays it
				// the way Impulse Tracker does: the note only starts fading out, and a fade-out of zero never ends
				// it), while FastTracker II silences such a note on the spot. A flat envelope makes FT2 do the same
				// as the original: after a key-off it only applies the fade-out.
				Envelope flatEnvelope;
				const Envelope* volumeEnvelope = &ins.VolumeEnvelope;
				if (!ins.VolumeEnvelope.Enabled || ins.VolumeEnvelope.Points.empty()) {
					flatEnvelope.Enabled = true;
					flatEnvelope.Points.push_back({ 0, 64 });
					flatEnvelope.Points.push_back({ 1, 64 });
					volumeEnvelope = &flatEnvelope;
				}
				WriteEnvelope(s, *volumeEnvelope);
				WriteEnvelope(s, ins.PanningEnvelope);
				s.WriteValue<std::uint8_t>(std::uint8_t(std::min<std::size_t>(volumeEnvelope->Points.size(), 12)));
				s.WriteValue<std::uint8_t>(std::uint8_t(std::min<std::size_t>(ins.PanningEnvelope.Points.size(), 12)));
				s.WriteValue<std::uint8_t>(volumeEnvelope->SustainPoint);
				s.WriteValue<std::uint8_t>(volumeEnvelope->LoopStart);
				s.WriteValue<std::uint8_t>(volumeEnvelope->LoopEnd);
				s.WriteValue<std::uint8_t>(ins.PanningEnvelope.SustainPoint);
				s.WriteValue<std::uint8_t>(ins.PanningEnvelope.LoopStart);
				s.WriteValue<std::uint8_t>(ins.PanningEnvelope.LoopEnd);
				s.WriteValue<std::uint8_t>(EnvelopeFlags(*volumeEnvelope));
				s.WriteValue<std::uint8_t>(EnvelopeFlags(ins.PanningEnvelope));
				// Vibrato waveforms: the Galaxy format orders sine, square, ramp up, ramp down, random; XM sine,
				// square, ramp down, ramp up
				static const std::uint8_t vibratoTypes[] = { 0, 1, 3, 2, 0 };
				s.WriteValue<std::uint8_t>(ins.VibratoType < 5 ? vibratoTypes[ins.VibratoType] : 0);
				s.WriteValue<std::uint8_t>(ins.VibratoSweep);
				s.WriteValue<std::uint8_t>(std::min<std::uint8_t>(ins.VibratoDepth, 15));
				s.WriteValue<std::uint8_t>(std::min<std::uint8_t>(ins.VibratoRate, 63));
				s.WriteValueAsLE<std::uint16_t>(ins.Fadeout);
				// Reserved, up to the 263 bytes of the header
				std::uint8_t reserved[22] = {};
				s.Write(reserved, sizeof(reserved));

				for (std::int32_t i = 0; i < sampleCount; i++) {
					const Sample& smp = ins.Samples[i];
					const std::uint32_t bytesPerSample = (smp.Is16Bit ? 2 : 1);
					const bool loop = (smp.Loop || smp.PingPong) && smp.LoopEnd > smp.LoopStart;

					// Relative note and finetune from the sample rate, against XM's 8363 Hz at C-4. In Amiga mode the
					// player the file is written for - libdragon's libxm - derives frequencies from the PAL Amiga clock
					// (7093789.2 Hz) where FastTracker II uses the NTSC one, which puts every note 15.9 cents flat; the
					// samples are tuned up by that much so the music plays at its pitch there.
					const double amigaClockCompensation = (_module.LinearSlides ? 0.0 : 12.0 * std::log2(7159090.5 / 7093789.2));
					const double transpose = 12.0 * std::log2(double(std::max<std::uint32_t>(smp.C5Speed, 1)) / 8363.0) +
						xmIns.NoteShift + amigaClockCompensation;
					std::int32_t relativeNote = std::int32_t(std::floor(transpose));
					std::int32_t finetune = std::int32_t(std::lround((transpose - relativeNote) * 128.0));
					if (finetune >= 64) {
						finetune -= 128;
						relativeNote++;
					}
					relativeNote = std::clamp(relativeNote, -96, 95);

					const std::int32_t pan = (smp.HasPanning ? smp.Panning : xmIns.Panning);
					// See DistinguishSeekedVariants(): an unlooped copy is told apart by silence past its end
					const std::uint32_t length = smp.Length + (xmIns.Distinguish > 0 && !loop ? std::uint32_t(xmIns.Distinguish) : 0);

					s.WriteValueAsLE<std::uint32_t>(length * bytesPerSample);
					s.WriteValueAsLE<std::uint32_t>(loop ? smp.LoopStart * bytesPerSample : 0);
					s.WriteValueAsLE<std::uint32_t>(loop ? (smp.LoopEnd - smp.LoopStart) * bytesPerSample : 0);
					s.WriteValue<std::uint8_t>(std::uint8_t(std::clamp(smp.Volume, 0, 64)));
					s.WriteValue<std::int8_t>(std::int8_t(finetune));
					s.WriteValue<std::uint8_t>(std::uint8_t((loop ? (smp.PingPong ? 2 : 1) : 0) | (smp.Is16Bit ? 0x10 : 0)));
					s.WriteValue<std::uint8_t>(std::uint8_t(std::clamp(pan, 0, 255)));
					s.WriteValue<std::int8_t>(std::int8_t(relativeNote));
					s.WriteValue<std::uint8_t>(0);
					WriteFixed(s, smp.Name, 22);
				}

				for (std::int32_t i = 0; i < sampleCount; i++) {
					const Sample& smp = ins.Samples[i];
					const std::uint32_t bytesPerSample = (smp.Is16Bit ? 2 : 1);
					const bool loop = (smp.Loop || smp.PingPong) && smp.LoopEnd > smp.LoopStart;

					// See DistinguishSeekedVariants(). audioconv64 cuts a looped sample at its loop end, so a looped
					// copy cannot be told apart past its end: one of its samples moves by the smallest step instead,
					// a different one for each copy, inside the part that is kept
					const std::uint8_t* source = smp.Data.data();
					std::size_t sourceSize = smp.Data.size();
					Array<std::uint8_t> distinct;
					if (xmIns.Distinguish > 0) {
						const std::size_t padding = (loop ? 0 : std::size_t(xmIns.Distinguish) * bytesPerSample);
						distinct = Array<std::uint8_t>(ValueInit, sourceSize + padding);
						std::copy(source, source + sourceSize, distinct.data());
						if (loop && smp.LoopEnd > 0) {
							const std::size_t at = std::size_t(xmIns.Distinguish - 1) % smp.LoopEnd * bytesPerSample;
							if (smp.Is16Bit) {
								const std::int16_t value = std::int16_t(distinct[at] | (distinct[at + 1] << 8));
								const std::int16_t moved = std::int16_t(value < INT16_MAX ? value + 1 : value - 1);
								distinct[at] = std::uint8_t(moved & 0xFF);
								distinct[at + 1] = std::uint8_t((std::uint16_t(moved) >> 8) & 0xFF);
							} else {
								const std::int8_t value = std::int8_t(distinct[at]);
								distinct[at] = std::uint8_t(std::int8_t(value < INT8_MAX ? value + 1 : value - 1));
							}
						}
						source = distinct.data();
						sourceSize = distinct.size();
					}

					// Delta-coded, as XM stores its samples
					if (smp.Is16Bit) {
						std::int16_t previous = 0;
						Array<std::uint8_t> delta(NoInit, sourceSize);
						for (std::size_t k = 0; k + 1 < sourceSize; k += 2) {
							const std::int16_t value = std::int16_t(source[k] | (source[k + 1] << 8));
							const std::int16_t d = std::int16_t(value - previous);
							previous = value;
							delta[k] = std::uint8_t(d & 0xFF);
							delta[k + 1] = std::uint8_t((std::uint16_t(d) >> 8) & 0xFF);
						}
						s.Write(delta.data(), std::int64_t(delta.size()));
					} else {
						std::int8_t previous = 0;
						Array<std::uint8_t> delta(NoInit, sourceSize);
						for (std::size_t k = 0; k < sourceSize; k++) {
							const std::int8_t value = std::int8_t(source[k]);
							delta[k] = std::uint8_t(std::int8_t(value - previous));
							previous = value;
						}
						s.Write(delta.data(), std::int64_t(delta.size()));
					}
				}
			}

			bool Write(Stream& s)
			{
				s.Write("Extended Module: ", 17);
				WriteFixed(s, _module.Name, 20);
				s.WriteValue<std::uint8_t>(0x1A);
				WriteFixed(s, "Jazz2 AssetPacker"_s, 20);
				s.WriteValueAsLE<std::uint16_t>(0x0104);
				s.WriteValueAsLE<std::uint32_t>(276);
				s.WriteValueAsLE<std::uint16_t>(std::uint16_t(_orders.size()));
				s.WriteValueAsLE<std::uint16_t>(0);		// Restart position
				s.WriteValueAsLE<std::uint16_t>(std::uint16_t(_module.ChannelCount));
				s.WriteValueAsLE<std::uint16_t>(std::uint16_t(_orders.size()));		// One pattern per order position
				s.WriteValueAsLE<std::uint16_t>(std::uint16_t(_instruments.size()));
				s.WriteValueAsLE<std::uint16_t>(_module.LinearSlides ? 1 : 0);
				s.WriteValueAsLE<std::uint16_t>(_module.Speed);
				s.WriteValueAsLE<std::uint16_t>(_module.Tempo);
				std::uint8_t orderTable[256] = {};
				for (std::size_t i = 0; i < _orders.size(); i++) {
					orderTable[i] = std::uint8_t(i);
				}
				s.Write(orderTable, sizeof(orderTable));

				for (std::int32_t o = 0; o < std::int32_t(_orders.size()); o++) {
					WritePattern(s, o);
				}
				for (const XmInstrument& xmIns : _instruments) {
					WriteInstrument(s, xmIns);
				}

				bool anyDropped = false;
				for (std::int32_t i = 0; i < std::int32_t(Effect::Count); i++) {
					if (_droppedEffects[i] > 0) {
						anyDropped = true;
						LOGW("  {} uses of effect {} have no XM counterpart and were left out", _droppedEffects[i], i);
					}
				}
				static_cast<void>(anyDropped);
				for (const Instrument& ins : _module.Instruments) {
					if (ins.HasPitchEnvelope) {
						LOGW("  Instrument \"{}\" has a pitch envelope, which XM cannot express", ins.Name);
					}
				}
				return true;
			}
		};
	}

	bool ModuleConverter::ConvertJ2bToXm(StringView sourcePath, StringView targetPath)
	{
		auto s = fs::Open(sourcePath, FileAccess::Read);
		if (!s->IsValid()) {
			return false;
		}
		const std::int64_t fileSize = s->GetSize();
		if (fileSize < 24 || fileSize > 64 * 1024 * 1024) {
			return false;
		}
		Array<std::uint8_t> file(NoInit, std::size_t(fileSize));
		if (s->Read(file.data(), fileSize) != fileSize) {
			return false;
		}

		// "MUSE" header, then a zlib stream holding the RIFF module
		if (std::memcmp(file.data(), "MUSE", 4) != 0) {
			LOGW("\"{}\" is not a Galaxy Music System module", sourcePath);
			return false;
		}
		const std::uint32_t magic = file[4] | (file[5] << 8) | (file[6] << 16) | (std::uint32_t(file[7]) << 24);
		if (magic != 0xAFBEADDEu && magic != 0xBEBAADDEu) {
			LOGW("\"{}\" has an unknown Galaxy Music System header", sourcePath);
			return false;
		}
		const std::uint32_t packedLength = file[16] | (file[17] << 8) | (file[18] << 16) | (std::uint32_t(file[19]) << 24);
		const std::uint32_t unpackedLength = file[20] | (file[21] << 8) | (file[22] << 16) | (std::uint32_t(file[23]) << 24);
		if (packedLength == 0 || std::int64_t(packedLength) + 24 > fileSize || unpackedLength == 0 || unpackedLength > 128 * 1024 * 1024) {
			return false;
		}

		Array<std::uint8_t> riff(NoInit, unpackedLength);
		uLongf destLength = unpackedLength;
		if (uncompress(riff.data(), &destLength, file.data() + 24, packedLength) != Z_OK) {
			LOGW("\"{}\" cannot be decompressed", sourcePath);
			return false;
		}

		Module module;
		if (!ParseModule(riff.data(), std::size_t(destLength), module)) {
			LOGW("\"{}\" is not a valid Galaxy Music System module", sourcePath);
			return false;
		}

		auto out = fs::Open(targetPath, FileAccess::Write);
		if (!out->IsValid()) {
			LOGW("Cannot write \"{}\"", targetPath);
			return false;
		}
		Converter converter(module);
		return converter.Convert(*out);
	}
}
