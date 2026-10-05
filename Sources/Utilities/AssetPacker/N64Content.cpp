#include "N64Content.h"
#include "../../Jazz2/Compatibility/JJ2Anims.h"
#include "ModuleConverter.h"

#include "../../Jazz2/Compatibility/J2vRecompressor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <vector>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <Core/Logger.h>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>

#if defined(ASSETPACKER_WITH_N64_ENCODERS)
#	include "Mpeg1Encoder.h"
#	include "Wav64Writer.h"
#	include "Xm64Converter.h"
#endif

#if defined(ASSETPACKER_WITH_OPENMPT)
#	include <libopenmpt.h>
#	include <string>
#endif

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::AssetPacker
{
	namespace
	{
		/** @brief Rate every pre-rendered track and cinematic soundtrack is produced at, the console's mixing rate */
		constexpr std::int32_t RenderRate = 32000;
		/** @brief Width the cinematics are encoded at, the console's framebuffer */
		constexpr std::int32_t VideoWidth = 320;
		/** @brief Frame rate the cinematics are encoded at, the nearest one MPEG-1 allows */
		constexpr std::int32_t OutputFps = 24;
		/** @brief Bitrate the built-in encoder gives the cinematics, the one videoconv64 picks at its default quality */
		constexpr std::int32_t VideoBitrateKbps = 800;
		/**
			@brief Bitrate the console's decoder keeps up with, and how far it may fall behind it

			videoconv64 keeps its encoder's buffer just as tight, to stay clear of bitrate spikes. Measured in ares, the
			busiest scenes of the ending at twice the average bitrate and more dropped a frame every few pictures - 12 drops
			without the limit, 7 with it - while a lone picture of 50 KB did not.
		*/
		constexpr std::int32_t VideoPeakBitrateKbps = 1600;
		constexpr std::int32_t VideoPeakBufferKbits = 800;
		/** @brief Default volumes of the game's preferences, which balance the music against the effects */
		constexpr float MusicGain = 0.4f;
		constexpr float SfxGain = 0.8f;

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		String Quote(StringView arg)
		{
#if defined(DEATH_TARGET_WINDOWS)
			return "\""_s + arg + "\""_s;
#else
			String result = "\""_s;
			for (char c : arg) {
				if (c == '"' || c == '\\' || c == '$' || c == '`') {
					result += "\\"_s;
				}
				result += StringView(&c, 1);
			}
			result += "\""_s;
			return result;
#endif
		}

		/** @brief Runs a tool and waits for it, returning whether it succeeded */
		bool RunTool(ArrayView<const String> args, bool quiet = false)
		{
			String command;
			for (const String& arg : args) {
				if (!command.empty()) {
					command += " "_s;
				}
				command += Quote(arg);
			}
			if (quiet) {
#if defined(DEATH_TARGET_WINDOWS)
				command += " > NUL 2>&1"_s;
#else
				command += " > /dev/null 2>&1"_s;
#endif
			}
			return (std::system(command.data()) == 0);
		}

		String FindExecutable(StringView directory, StringView name)
		{
#if defined(DEATH_TARGET_WINDOWS)
			String path = fs::CombinePath(directory, String(name + ".exe"_s));
#else
			String path = fs::CombinePath(directory, name);
#endif
			return (fs::IsReadableFile(path) ? path : String());
		}
#endif

		/** @brief Converts a float sample to 16 bits, as a recording written for the converters stores it */
		std::int16_t ToSample16(float value)
		{
			return std::int16_t(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
		}

#if defined(ASSETPACKER_WITH_N64_ENCODERS)
		bool WriteBuffer(StringView path, const BigEndianWriter& buffer)
		{
			auto s = fs::Open(path, FileAccess::Write);
			if (!s->IsValid() || s->Write(buffer.Data.data(), std::int64_t(buffer.Data.size())) != std::int64_t(buffer.Data.size())) {
				LOGE("Cannot write \"{}\"", path);
				return false;
			}
			return true;
		}

		/**
			@brief Writes interleaved stereo float audio as a `.wav64` file

			@param loopStart	First frame of the loop, or negative for audio that plays once
		*/
		bool WriteStereoWav64(StringView path, const float* stereo, std::int64_t frames, std::int32_t rate, std::int64_t loopStart, Wav64Format format)
		{
			Wav64Audio audio;
			audio.Channels = 2;
			audio.SampleRate = rate;
			audio.Frames = std::int32_t(frames);
			audio.Samples.resize(std::size_t(frames) * 2);
			for (std::size_t i = 0; i < audio.Samples.size(); i++) {
				audio.Samples[i] = ToSample16(stereo[i]);
			}
			if (loopStart >= 0) {
				audio.Looping = true;
				audio.LoopOffset = std::int32_t(loopStart);
				audio.LoopEnd = audio.Frames;
			}
			BigEndianWriter out;
			return Wav64Writer::Write(out, audio, format, fs::GetFileName(path)) && WriteBuffer(path, out);
		}
#endif

		/** @brief A decoded mono sound effect */
		struct SoundSample
		{
			std::int32_t Frequency = 0;
			SmallVector<float, 0> Samples;
		};

		bool ReadWave(Stream& s, SoundSample& sample)
		{
			char id[4];
			if (s.Read(id, 4) != 4 || std::memcmp(id, "RIFF", 4) != 0) {
				return false;
			}
			s.ReadValueAsLE<std::uint32_t>();
			if (s.Read(id, 4) != 4 || std::memcmp(id, "WAVE", 4) != 0) {
				return false;
			}
			std::int32_t channels = 1, bits = 8;
			while (s.Read(id, 4) == 4) {
				const std::uint32_t length = s.ReadValueAsLE<std::uint32_t>();
				if (std::memcmp(id, "fmt ", 4) == 0) {
					s.ReadValueAsLE<std::uint16_t>();
					channels = s.ReadValueAsLE<std::uint16_t>();
					sample.Frequency = std::int32_t(s.ReadValueAsLE<std::uint32_t>());
					s.ReadValueAsLE<std::uint32_t>();
					s.ReadValueAsLE<std::uint16_t>();
					bits = s.ReadValueAsLE<std::uint16_t>();
					if (length > 16) {
						s.Seek(length - 16, SeekOrigin::Current);
					}
				} else if (std::memcmp(id, "data", 4) == 0) {
					const std::int32_t frameBytes = channels * (bits / 8);
					if (frameBytes <= 0) {
						return false;
					}
					Array<std::uint8_t> data(NoInit, length);
					const std::int64_t read = s.Read(data.data(), length);
					const std::int32_t frames = std::int32_t(read / frameBytes);
					sample.Samples.resize(std::size_t(frames));
					for (std::int32_t i = 0; i < frames; i++) {
						const std::uint8_t* p = &data[std::size_t(i) * frameBytes];
						sample.Samples[i] = (bits == 8 ? (float(p[0]) - 128.0f) / 128.0f : float(std::int16_t(p[0] | (p[1] << 8))) / 32768.0f);
					}
					return sample.Frequency > 0;
				} else {
					s.Seek(length + (length & 1), SeekOrigin::Current);
				}
			}
			return false;
		}

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		void WriteWave(StringView path, const float* stereo, std::int64_t frames, std::int32_t rate)
		{
			auto s = fs::Open(path, FileAccess::Write);
			const std::uint32_t dataBytes = std::uint32_t(frames * 4);
			s->Write("RIFF", 4);
			s->WriteValueAsLE<std::uint32_t>(36 + dataBytes);
			s->Write("WAVEfmt ", 8);
			s->WriteValueAsLE<std::uint32_t>(16);
			s->WriteValueAsLE<std::uint16_t>(1);
			s->WriteValueAsLE<std::uint16_t>(2);
			s->WriteValueAsLE<std::uint32_t>(std::uint32_t(rate));
			s->WriteValueAsLE<std::uint32_t>(std::uint32_t(rate * 4));
			s->WriteValueAsLE<std::uint16_t>(4);
			s->WriteValueAsLE<std::uint16_t>(16);
			s->Write("data", 4);
			s->WriteValueAsLE<std::uint32_t>(dataBytes);
			SmallVector<std::int16_t, 0> block;
			block.resize(8192);
			for (std::int64_t i = 0; i < frames * 2; i += std::int64_t(block.size())) {
				const std::int64_t n = std::min<std::int64_t>(std::int64_t(block.size()), frames * 2 - i);
				for (std::int64_t k = 0; k < n; k++) {
					block[std::size_t(k)] = ToSample16(stereo[i + k]);
				}
				s->Write(block.data(), n * 2);
			}
		}
#endif

#if defined(ASSETPACKER_WITH_OPENMPT)
		/** @brief Makes the module stop right at the end of the song, without the short fade-out libopenmpt adds by default */
		void SetStopAtEnd(openmpt_module* mod)
		{
#	if OPENMPT_API_VERSION_AT_LEAST(0, 5, 0)
			openmpt_module_ctl_set_text(mod, "play.at_end", "stop");
#	else
			openmpt_module_ctl_set(mod, "play.at_end", "stop");
#	endif
		}

#	if !OPENMPT_API_VERSION_AT_LEAST(0, 8, 0)
		/**
			@brief Finds the frame a song repeats from, for libopenmpt versions that cannot tell it (e.g. 0.6 in Ubuntu
				22.04, or the 0.7 the web build is compiled with)

			The song is played again, now repeating, to a few frames past the end of its first pass - the row playing
			then is the one it repeats from, and where that row begins is what it is turned into.
		*/
		std::int64_t FindRestartFrame(const std::vector<char>& data, std::int64_t firstPassFrames)
		{
			openmpt_module* mod = openmpt_module_create_from_memory2(data.data(), data.size(), nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
			if (mod == nullptr) {
				return 0;
			}
			openmpt_module_set_repeat_count(mod, 1);
			SetStopAtEnd(mod);

			const std::int64_t target = firstPassFrames + 16;
			float buffer[2 * 4096];
			std::int64_t rendered = 0;
			while (rendered < target) {
				const std::size_t count = openmpt_module_read_interleaved_float_stereo(mod, RenderRate,
					std::size_t(std::min<std::int64_t>(4096, target - rendered)), buffer);
				if (count == 0) {
					break;
				}
				rendered += std::int64_t(count);
			}

			std::int64_t restartFrame = 0;
			if (rendered >= target) {
				const double seconds = openmpt_module_set_position_order_row(mod, openmpt_module_get_current_order(mod), openmpt_module_get_current_row(mod));
				restartFrame = std::int64_t(std::llround(std::max(0.0, seconds) * RenderRate));
			}
			openmpt_module_destroy(mod);
			return restartFrame;
		}
#	endif

		/**
			@brief Renders a module once through with libopenmpt, the player the game uses on the other platforms

			@param loopStartFrame	Receives where the song jumps back to when it repeats, in output frames - the
				recording then ends exactly where the song does, so the loop is seamless
		*/
		bool RenderModule(StringView path, SmallVector<float, 0>& stereo, std::int64_t* loopStartFrame)
		{
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				return false;
			}
			std::vector<char> data(std::size_t(s->GetSize()));
			s->Read(data.data(), std::int64_t(data.size()));
			const char* errorMessage = nullptr;
			openmpt_module* mod = openmpt_module_create_from_memory2(data.data(), data.size(), nullptr, nullptr, nullptr, nullptr, nullptr, &errorMessage, nullptr);
			if (mod == nullptr) {
				LOGW("libopenmpt cannot render \"{}\": {}", path, errorMessage != nullptr ? errorMessage : "unknown error");
				openmpt_free_string(errorMessage);
				return false;
			}
			openmpt_free_string(errorMessage);

			openmpt_module_set_repeat_count(mod, 0);
			if (loopStartFrame != nullptr) {
				// By default the module fades out for a moment past the end of the song, which would then be heard
				// every time the recording loops
				SetStopAtEnd(mod);
				*loopStartFrame = 0;
#	if OPENMPT_API_VERSION_AT_LEAST(0, 8, 0)
				const std::int32_t restartOrder = openmpt_module_get_restart_order(mod, 0);
				const std::int32_t restartRow = openmpt_module_get_restart_row(mod, 0);
				const double seconds = (restartOrder > 0 || restartRow > 0 ? openmpt_module_set_position_order_row(mod, restartOrder, restartRow) : 0.0);
				*loopStartFrame = std::int64_t(std::llround(std::max(0.0, seconds) * RenderRate));
				openmpt_module_set_position_seconds(mod, 0.0);
#	endif
			}
			float buffer[2 * 4096];
			bool ended = false;
			while (true) {
				const std::size_t count = openmpt_module_read_interleaved_float_stereo(mod, RenderRate, 4096, buffer);
				if (count == 0) {
					ended = true;
					break;
				}
				stereo.append(buffer, buffer + count * 2);
				// Anything longer than ten minutes is a module that never ends by itself
				if (stereo.size() > std::size_t(RenderRate) * 2 * 600) {
					break;
				}
			}
			openmpt_module_destroy(mod);
#	if !OPENMPT_API_VERSION_AT_LEAST(0, 8, 0)
			// A module that never ends has no first pass to repeat after, it loops from the start
			if (loopStartFrame != nullptr && ended) {
				*loopStartFrame = FindRestartFrame(data, std::int64_t(stereo.size() / 2));
			}
#	else
			static_cast<void>(ended);
#	endif
			return !stereo.empty();
		}
#endif

		/** @brief Converts the palette indices of one frame into limited-range BT.601 YUV 4:2:0, box-filtered */
		void ConvertFrame(const std::uint8_t* indices, std::int32_t width, std::int32_t height, std::int32_t factor,
			const std::uint8_t* palette, SmallVector<std::uint8_t, 0>& yuv)
		{
			const std::int32_t w = width / factor, h = height / factor;
			SmallVector<float, 0> rgb;
			rgb.resize(std::size_t(w) * h * 3);
			const float area = 1.0f / float(factor * factor);
			for (std::int32_t y = 0; y < h; y++) {
				for (std::int32_t x = 0; x < w; x++) {
					float r = 0, g = 0, b = 0;
					for (std::int32_t dy = 0; dy < factor; dy++) {
						for (std::int32_t dx = 0; dx < factor; dx++) {
							const std::uint8_t* c = &palette[indices[std::size_t(y * factor + dy) * width + (x * factor + dx)] * 4];
							r += c[0]; g += c[1]; b += c[2];
						}
					}
					float* dst = &rgb[(std::size_t(y) * w + x) * 3];
					dst[0] = r * area; dst[1] = g * area; dst[2] = b * area;
				}
			}

			const std::int32_t cw = w / 2, ch = h / 2;
			yuv.resize(std::size_t(w) * h + std::size_t(cw) * ch * 2);
			std::uint8_t* Y = yuv.data();
			std::uint8_t* U = Y + std::size_t(w) * h;
			std::uint8_t* V = U + std::size_t(cw) * ch;
			for (std::int32_t i = 0; i < w * h; i++) {
				const float* c = &rgb[std::size_t(i) * 3];
				Y[i] = std::uint8_t(std::clamp(16.0f + (65.481f * c[0] + 128.553f * c[1] + 24.966f * c[2]) / 255.0f + 0.5f, 0.0f, 255.0f));
			}
			for (std::int32_t y = 0; y < ch; y++) {
				for (std::int32_t x = 0; x < cw; x++) {
					float r = 0, g = 0, b = 0;
					for (std::int32_t k = 0; k < 4; k++) {
						const float* c = &rgb[(std::size_t(y * 2 + (k >> 1)) * w + (x * 2 + (k & 1))) * 3];
						r += c[0]; g += c[1]; b += c[2];
					}
					r *= 0.25f; g *= 0.25f; b *= 0.25f;
					U[std::size_t(y) * cw + x] = std::uint8_t(std::clamp(128.0f + (-37.797f * r - 74.203f * g + 112.0f * b) / 255.0f + 0.5f, 0.0f, 255.0f));
					V[std::size_t(y) * cw + x] = std::uint8_t(std::clamp(128.0f + (112.0f * r - 93.786f * g - 18.214f * b) / 255.0f + 0.5f, 0.0f, 255.0f));
				}
			}
		}

	}

	bool N64Content::HasBuiltInEncoders()
	{
#if defined(ASSETPACKER_WITH_N64_ENCODERS)
		return true;
#else
		return false;
#endif
	}

	bool N64Content::FindTools(StringView hint, Tools& tools)
	{
		const bool builtInRequested = (hint == "builtin"_s);
		if (builtInRequested && !HasBuiltInEncoders()) {
			LOGE("This build of the tool has no encoders of its own for the Nintendo 64, it needs libdragon's converters");
			return false;
		}

#if defined(DEATH_TARGET_EMSCRIPTEN)
		// No other program can be run here, the built-in encoders are all there is
		static_cast<void>(builtInRequested);
		tools.BuiltIn = true;
		tools.CanEncodeVideo = true;
		return true;
#else
		if (builtInRequested) {
			tools.BuiltIn = true;
			tools.CanEncodeVideo = true;
			return true;
		}

		SmallVector<String, 4> candidates;
		if (!hint.empty()) {
			candidates.push_back(fs::CombinePath(hint, "bin"_s));
			candidates.push_back(hint);
		}
#if defined(DEATH_TARGET_WINDOWS)
		char* inst; std::size_t instLength;
		if (_dupenv_s(&inst, &instLength, "N64_INST") == 0 && inst != nullptr) {
			if (inst[0] != '\0') {
				candidates.push_back(fs::CombinePath(StringView(inst), "bin"_s));
			}
			std::free(inst);
		}
#else
		if (const char* inst = std::getenv("N64_INST")) {
			candidates.push_back(fs::CombinePath(StringView(inst), "bin"_s));
		}
#endif

		for (const String& dir : candidates) {
			String audioConv = FindExecutable(dir, "audioconv64"_s);
			if (audioConv.empty()) {
				continue;
			}
			tools.AudioConv = std::move(audioConv);
			tools.VideoConv = FindExecutable(dir, "videoconv64"_s);
			tools.Root = (fs::GetFileName(dir) == "bin"_s ? String(fs::GetDirectoryName(dir)) : String(dir));
			break;
		}
		if (tools.AudioConv.empty()) {
			if (!HasBuiltInEncoders()) {
				return false;
			}
			LOGI("libdragon's converters were not found, the encoders built into the tool are used instead");
			tools.BuiltIn = true;
			tools.CanEncodeVideo = true;
			return true;
		}

		// videoconv64 drives ffmpeg and ffprobe, and finds audioconv64 through N64_INST. Without them the cinematics
		// are left to the built-in encoder, where there is one.
		const String ffmpegCheck[] = { "ffmpeg"_s, "-version"_s };
		const String ffprobeCheck[] = { "ffprobe"_s, "-version"_s };
		if (tools.VideoConv.empty() || !RunTool(ffmpegCheck, true) || !RunTool(ffprobeCheck, true)) {
			tools.VideoConv = {};
		}
		tools.CanEncodeVideo = (!tools.VideoConv.empty() || HasBuiltInEncoders());
#	if defined(DEATH_TARGET_WINDOWS)
		_putenv_s("N64_INST", String(tools.Root).data());
#	else
		setenv("N64_INST", String(tools.Root).data(), 1);
#	endif
		return true;
#endif
	}

	bool N64Content::SplitPackage(StringView sourcePak, PakWriter& target, StringView outputPath, StringView tempPath, const Tools& tools)
	{
		PakFile source(sourcePak);
		if (!source.IsValid()) {
			LOGE("Cannot open \"{}\"", sourcePak);
			return false;
		}

		const String soundsPath = fs::CombinePath(tempPath, "Sounds"_s);
		std::int32_t soundCount = 0, fileCount = 0;
		bool success = true;

		// Depth-first over the name index; the entries come back as full paths inside the package
		SmallVector<String, 16> pending;
		pending.push_back(String());
		while (!pending.empty()) {
			String directory = pending.pop_back_val();
			for (StringView entry : PakFile::Directory(source, directory)) {
				String path = fs::ToNativeSeparators(entry);
				if (source.DirectoryExists(path)) {
					pending.push_back(std::move(path));
					continue;
				}
				auto file = source.OpenFile(path);
				if (file == nullptr || !file->IsValid()) {
					LOGW("Cannot read \"{}\" from the package", path);
					success = false;
					continue;
				}
				const String normalized = StringUtils::replaceAll(path, "\\"_s, "/"_s);
				if (normalized.hasPrefix("Animations/"_s) && fs::GetExtension(normalized) == "wav"_s) {
					const StringView relativePath = StringView(normalized).exceptPrefix("Animations/"_s);
#if defined(ASSETPACKER_WITH_N64_ENCODERS)
					if (tools.BuiltIn) {
						// Encoded right away, with the same settings as audioconv64 below
						const String targetPath = fs::CombinePath({ outputPath, "Animations"_s,
							String(relativePath.exceptSuffix(4) + ".wav64"_s) });
						Wav64Audio audio;
						BigEndianWriter encoded;
						if (!Wav64Writer::ReadWave(*file, audio, normalized) || !Wav64Writer::Write(encoded, audio, Wav64Format::Vadpcm, normalized) ||
							!fs::CreateDirectories(fs::GetDirectoryName(targetPath)) || !WriteBuffer(targetPath, encoded)) {
							success = false;
							continue;
						}
						soundCount++;
						continue;
					}
#endif
					// Sound samples are written out for audioconv64 instead of being packed
					const String loosePath = fs::CombinePath(soundsPath, relativePath);
					fs::CreateDirectories(fs::GetDirectoryName(loosePath));
					// audioconv64 mirrors the directory tree, but it does not check whether its stat() succeeded
					// before deciding a directory exists, so the directories are created for it up front
					fs::CreateDirectories(fs::GetDirectoryName(fs::CombinePath({ outputPath, "Animations"_s, relativePath })));
					auto out = fs::Open(loosePath, FileAccess::Write);
					std::uint8_t buffer[16384];
					std::int64_t n;
					while ((n = file->Read(buffer, sizeof(buffer))) > 0) {
						out->Write(buffer, n);
					}
					soundCount++;
					continue;
				}
				// A sprite sheet in LZ4 stays stored as it is (see JJ2Anims::ImageCompression)
				const bool storedAsIs = (Compatibility::JJ2Anims::PreferredImageCompression == Compatibility::JJ2Anims::ImageCompression::Lz4 &&
					fs::GetExtension(path) == "aura"_s);
				if (!target.AddFile(*file, path, storedAsIs ? PakPreferredCompression::None : PakPreferredCompression::Deflate)) {
					LOGW("Cannot add \"{}\" to the package", path);
					success = false;
				}
				fileCount++;
			}
		}

		// VADPCM without the Huffman stage. Raw PCM would look cheaper, but the mixer cannot fetch more than about 120
		// samples of a raw stream at once, so while a single raw effect played every mixing round was cut that short -
		// and each round costs every playing channel, the sixteen of a module included: music and effects together
		// cost 6.4 ms a frame, against 5.8 ms for the music alone and 1.1 ms for the effects alone (castle1 in ares).
		// A VADPCM stream allows full rounds, 3.1 ms for both, and is half the size. The mixer decodes VADPCM on the
		// RSP; the Huffman stage on top would be decoded on the CPU, which costs more (0.4 ms a frame) than the
		// cartridge space it saves is worth here.
#if !defined(DEATH_TARGET_EMSCRIPTEN)
		if (soundCount > 0 && !tools.BuiltIn) {
			LOGI("Converting {} sound effects to .wav64...", soundCount);
			const String args[] = { tools.AudioConv, "--wav-compress"_s, "vadpcm,huffman=false"_s, "-o"_s,
				fs::CombinePath(outputPath, "Animations"_s), soundsPath };
			if (!RunTool(args)) {
				LOGE("audioconv64 failed to convert the sound effects");
				success = false;
			}
		}
#endif
		LOGI("{} files packed, {} sound effects stored as .wav64", fileCount, soundCount);
		return success;
	}

	void N64Content::ConvertMusic(StringView musicPath, StringView tempPath, const Tools& tools)
	{
		if (!fs::DirectoryExists(musicPath)) {
			return;
		}
		const String modulesPath = fs::CombinePath(tempPath, "Modules"_s);
		const String rendersPath = fs::CombinePath(tempPath, "Renders"_s);
		fs::CreateDirectories(modulesPath);
		fs::CreateDirectories(rendersPath);

		SmallVector<String, 0> files;
		for (auto item : fs::Directory(musicPath, fs::EnumerationOptions::SkipDirectories)) {
			files.push_back(item);
		}
		std::sort(files.begin(), files.end());

		std::int32_t modules = 0, renders = 0, dropped = 0;
		for (const String& file : files) {
			const String extension = StringUtils::lowercase(fs::GetExtension(file));
			const StringView stem = fs::GetFileNameWithoutExtension(file);
			if (extension == "xm64"_s || extension == "wav64"_s) {
				continue;
			}

			if (extension == "j2b"_s) {
				LOGI("Converting \"{}\" to XM...", fs::GetFileName(file));
				if (ModuleConverter::ConvertJ2bToXm(file, fs::CombinePath(modulesPath, String(stem + ".xm"_s)))) {
					modules++;
					fs::RemoveFile(file);
					continue;
				}
				LOGW("Cannot convert \"{}\" to XM, trying to pre-render it instead", fs::GetFileName(file));
			} else if (extension == "xm"_s) {
				fs::Move(file, fs::CombinePath(modulesPath, fs::GetFileName(file)));
				modules++;
				continue;
			}

			if (extension == "wav"_s) {
				// Played as it is, compressed the same way as the rendered tracks
				bool converted = false;
#if defined(ASSETPACKER_WITH_N64_ENCODERS)
				if (tools.BuiltIn) {
					// As audioconv64 is told below: the whole recording loops, unless it names a loop of its own
					auto s = fs::Open(file, FileAccess::Read);
					Wav64Audio audio;
					if (s->IsValid() && Wav64Writer::ReadWave(*s, audio, fs::GetFileName(file))) {
						audio.Looping = true;
						if (audio.LoopEnd == 0) {
							audio.LoopEnd = audio.Frames;
						}
						std::sort(audio.SkipPoints.begin(), audio.SkipPoints.end());
						audio.SkipPoints.erase(std::unique(audio.SkipPoints.begin(), audio.SkipPoints.end()), audio.SkipPoints.end());
						BigEndianWriter encoded;
						converted = Wav64Writer::Write(encoded, audio, Wav64Format::Ulc, fs::GetFileName(file)) &&
							WriteBuffer(fs::CombinePath(musicPath, String(stem + ".wav64"_s)), encoded);
					}
				}
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
				if (!tools.BuiltIn) {
					const String args[] = { tools.AudioConv, "--wav-compress"_s, "2"_s, "--wav-loop"_s, "true"_s, "-o"_s, musicPath, file };
					converted = RunTool(args);
				}
#endif
				if (converted) {
					renders++;
				} else {
					dropped++;
				}
				fs::RemoveFile(file);
				continue;
			}

#if defined(ASSETPACKER_WITH_OPENMPT)
			SmallVector<float, 0> stereo;
			std::int64_t loopStart = 0;
			if (RenderModule(file, stereo, &loopStart)) {
				const std::int64_t frames = std::int64_t(stereo.size() / 2);
				LOGI("Pre-rendered \"{}\": {} s, looping from {} s", fs::GetFileName(file), frames / RenderRate, loopStart / RenderRate);
				// ULC is decoded with the RSP's help and costs the CPU far less than Opus
				loopStart = std::clamp<std::int64_t>(loopStart, 0, frames > 0 ? frames - 1 : 0);
				bool converted = false;
#	if defined(ASSETPACKER_WITH_N64_ENCODERS)
				if (tools.BuiltIn) {
					converted = WriteStereoWav64(fs::CombinePath(musicPath, String(stem + ".wav64"_s)), stereo.data(), frames, RenderRate,
						loopStart, Wav64Format::Ulc);
				}
#	endif
#	if !defined(DEATH_TARGET_EMSCRIPTEN)
				if (!tools.BuiltIn) {
					const String wavPath = fs::CombinePath(rendersPath, String(stem + ".wav"_s));
					WriteWave(wavPath, stereo.data(), frames, RenderRate);
					const String loopOffset = String(std::to_string(loopStart).c_str());
					const String args[] = { tools.AudioConv, "--wav-compress"_s, "2"_s, "--wav-loop"_s, "true"_s,
						"--wav-loop-offset"_s, loopOffset, "-o"_s, musicPath, wavPath };
					converted = RunTool(args);
				}
#	endif
				if (converted) {
					renders++;
					fs::RemoveFile(file);
					continue;
				}
			}
#endif
			LOGW("\"{}\" cannot be played on the Nintendo 64 and was left out", fs::GetFileName(file));
			fs::RemoveFile(file);
			dropped++;
		}

		if (modules > 0) {
			LOGI("Converting {} XM modules to .xm64...", modules);
			SmallVector<String, 0> xmFiles;
			for (auto item : fs::Directory(modulesPath, fs::EnumerationOptions::SkipDirectories)) {
				xmFiles.push_back(item);
			}
			for (const String& xmFile : xmFiles) {
#if defined(ASSETPACKER_WITH_N64_ENCODERS)
				if (tools.BuiltIn) {
					const String targetPath = fs::CombinePath(musicPath, String(fs::GetFileNameWithoutExtension(xmFile) + ".xm64"_s));
					if (!Xm64Converter::Convert(xmFile, targetPath)) {
						LOGE("Cannot convert \"{}\" to .xm64", fs::GetFileName(xmFile));
					}
					continue;
				}
#endif
#if !defined(DEATH_TARGET_EMSCRIPTEN)
				// One audioconv64 run per module: its XM writer names the offsets it patches in afterwards without
				// the file they belong to and never forgets them, so every module after the first one of a run gets
				// the first one's header - xm64player_open() then reads its context sizes from the wrong place and
				// hangs or crashes on a NULL allocation. The .wav64 writer keys them by file and is safe to batch.
				const String args[] = { tools.AudioConv, "-o"_s, musicPath, xmFile };
				if (!RunTool(args)) {
					LOGE("audioconv64 failed to convert \"{}\"", fs::GetFileName(xmFile));
				}
#endif
			}
		}
		LOGI("Music: {} modules, {} pre-rendered, {} left out", modules, renders, dropped);
	}

	bool N64Content::ConvertCinematic(StringView name, StringView originalsPath, StringView outputPath, StringView sourcePak,
		StringView musicPath, StringView tempPath, const Tools& tools)
	{
		if (!tools.CanEncodeVideo) {
			return false;
		}
		const String videoPath = fs::FindPathCaseInsensitive(fs::CombinePath(originalsPath, String(name + ".j2v"_s)));
		if (!fs::IsReadableFile(videoPath)) {
			return false;
		}
		const String lowerName = StringUtils::lowercase(name);
		const String yuvPath = fs::CombinePath(tempPath, String(lowerName + ".y4m"_s));
		const String wavPath = fs::CombinePath(tempPath, String(lowerName + ".wav"_s));
		const String cinematicsPath = fs::CombinePath(outputPath, "Cinematics"_s);
		const String videoOutputPath = fs::CombinePath(cinematicsPath, String(lowerName + ".m1v"_s));
		const String audioOutputPath = fs::CombinePath(cinematicsPath, String(lowerName + ".wav64"_s));
		fs::CreateDirectories(cinematicsPath);

		// Without videoconv64 the built-in encoder takes the frames as they are decoded, otherwise they are written
		// as uncompressed YUV for ffmpeg to read
#if defined(ASSETPACKER_WITH_N64_ENCODERS)
		const bool builtIn = (tools.BuiltIn || tools.VideoConv.empty());
		Mpeg1Encoder encoder;
		std::vector<std::uint8_t> encoded;
		if (builtIn) {
			LOGI("Encoding \"{}\" as full-motion video...", name);
		}
#else
		const bool builtIn = false;
#endif
		auto videoFile = fs::Open(builtIn ? StringView(videoOutputPath) : StringView(yuvPath), FileAccess::Write);
		if (!videoFile->IsValid()) {
			return false;
		}

		SmallVector<std::uint8_t, 0> yuv;
		Compatibility::J2vVideoInfo info;
		std::int64_t outputFrames = 0;
		bool encodingFailed = false;
		const bool decoded = Compatibility::J2vRecompressor::DecodeFrames(videoPath, [&](const Compatibility::J2vVideoInfo& video,
			std::int32_t frameIndex, const std::uint8_t* indices, const std::uint8_t* palette, bool paletteChanged) {
			static_cast<void>(paletteChanged);
			const std::int32_t factor = std::max(1, video.Width / VideoWidth);
			const std::int32_t width = video.Width / factor;
			const std::int32_t height = video.Height / factor;
			if (frameIndex == 0) {
				// MPEG-1 only allows a handful of frame rates, and the cinematics run at 1000/42 fps, which is not one
				// of them - the encoder then inserts frames of its own and its two passes disagree. So the video is
				// written at 24 fps instead, each output frame showing the source frame of its moment in time: a
				// frame is shown twice now and then, and the soundtrack, built on the original timing, stays in sync.
				if (builtIn) {
#if defined(ASSETPACKER_WITH_N64_ENCODERS)
					Mpeg1Encoder::Options options;
					options.Width = width;
					options.Height = height;
					options.FrameRateNum = OutputFps;
					options.FrameRateDen = 1;
					options.BitrateKbps = VideoBitrateKbps;
					options.PeakBitrateKbps = VideoPeakBitrateKbps;
					options.PeakBufferKbits = VideoPeakBufferKbits;
					if (!encoder.Begin(options, encoded)) {
						encodingFailed = true;
						return false;
					}
#endif
				} else {
					char header[128];
					const std::int32_t length = std::snprintf(header, sizeof(header), "YUV4MPEG2 W%d H%d F%d:1 Ip A1:1 C420jpeg XCOLORRANGE=LIMITED\n",
						width, height, OutputFps);
					videoFile->Write(header, length);
				}
			}
			ConvertFrame(indices, video.Width, video.Height, factor, palette, yuv);
			const std::int64_t frameEndMs = std::int64_t(frameIndex + 1) * video.FrameDelay;
			while (outputFrames * 1000 < frameEndMs * OutputFps) {
				if (builtIn) {
#if defined(ASSETPACKER_WITH_N64_ENCODERS)
					const std::uint8_t* planeY = yuv.data();
					const std::uint8_t* planeU = planeY + std::size_t(width) * height;
					const std::uint8_t* planeV = planeU + std::size_t(width / 2) * (height / 2);
					if (!encoder.EncodeFrame(planeY, planeU, planeV, encoded)) {
						encodingFailed = true;
						return false;
					}
					videoFile->Write(encoded.data(), std::int64_t(encoded.size()));
					encoded.clear();
#endif
				} else {
					videoFile->Write("FRAME\n", 6);
					videoFile->Write(yuv.data(), std::int64_t(yuv.size()));
				}
				outputFrames++;
			}
			return true;
		}, &info);
#if defined(ASSETPACKER_WITH_N64_ENCODERS)
		if (builtIn && decoded && !encodingFailed) {
			encodingFailed = !encoder.End(encoded);
			videoFile->Write(encoded.data(), std::int64_t(encoded.size()));
		}
#endif
		videoFile = nullptr;
		if (!decoded || encodingFailed) {
			fs::RemoveFile(builtIn ? StringView(videoOutputPath) : StringView(yuvPath));
			return false;
		}

		// The soundtrack: the cinematic's music and its sound effects, mixed the way the player would at the
		// default volumes - the whole track is then played at the master volume
		const std::int64_t frames = std::int64_t(info.FrameCount) * info.FrameDelay * RenderRate / 1000;
		SmallVector<float, 0> mix;
		mix.resize(std::size_t(frames) * 2);
		std::fill(mix.begin(), mix.end(), 0.0f);

#if defined(ASSETPACKER_WITH_OPENMPT)
		const String musicFile = fs::FindPathCaseInsensitive(fs::CombinePath(musicPath, String(lowerName + ".j2b"_s)));
		SmallVector<float, 0> music;
		if (fs::IsReadableFile(musicFile) && RenderModule(musicFile, music, nullptr)) {
			const std::size_t n = std::min(music.size(), mix.size());
			for (std::size_t i = 0; i < n; i++) {
				mix[i] += music[i] * MusicGain;
			}
		} else {
			LOGW("No music could be rendered for \"{}\"", name);
		}
#else
		static_cast<void>(musicPath);
		LOGW("The music of \"{}\" is left out of its soundtrack - AssetPacker was built without libopenmpt", name);
#endif

		PakFile pak(sourcePak);
		auto playlist = (pak.IsValid() ? pak.OpenFile(fs::CombinePath("Cinematics"_s, String(lowerName + ".j2sfx"_s))) : nullptr);
		std::int32_t effectCount = 0;
		if (playlist != nullptr && playlist->IsValid()) {
			playlist->ReadValueAsLE<std::uint64_t>();	// Signature
			playlist->ReadValue<std::uint8_t>();		// File type
			playlist->ReadValueAsLE<std::uint16_t>();	// Version
			const std::uint32_t sampleCount = playlist->ReadValueAsLE<std::uint16_t>();
			SmallVector<SoundSample, 0> samples;
			samples.resize(sampleCount);
			for (std::uint32_t i = 0; i < sampleCount; i++) {
				const std::uint8_t length = playlist->ReadValue<std::uint8_t>();
				if (length == 0) {
					continue;
				}
				String samplePath(NoInit, length);
				playlist->Read(samplePath.data(), length);
				auto s = pak.OpenFile(fs::CombinePath("Animations"_s, fs::ToNativeSeparators(samplePath)));
				if (s != nullptr && s->IsValid()) {
					ReadWave(*s, samples[i]);
				}
			}
			const std::uint32_t itemCount = playlist->ReadValueAsLE<std::uint16_t>();
			for (std::uint32_t i = 0; i < itemCount; i++) {
				const std::uint32_t frame = playlist->ReadVariableUint32();
				const std::uint16_t sampleIndex = playlist->ReadValueAsLE<std::uint16_t>();
				const float gain = playlist->ReadValue<std::uint8_t>() / 255.0f * SfxGain;
				const float panning = playlist->ReadValue<std::int8_t>() / 127.0f;
				if (sampleIndex >= samples.size() || samples[sampleIndex].Samples.empty()) {
					continue;
				}
				// The player's stereo model for a 2D source: centred when not panned, otherwise all the way to
				// the side it leans to (see AudioMixer::ComputeStereoGains())
				const float pan = (std::abs(panning) > 0.0001f ? (panning > 0.0f ? 1.0f : 0.0f) : 0.5f);
				const float left = gain * std::sqrt(1.0f - pan);
				const float right = gain * std::sqrt(pan);

				const SoundSample& sample = samples[sampleIndex];
				const std::int64_t start = std::int64_t(frame) * info.FrameDelay * RenderRate / 1000;
				const double step = double(sample.Frequency) / RenderRate;
				for (std::int64_t k = 0; ; k++) {
					const double position = k * step;
					const std::size_t index = std::size_t(position);
					if (index + 1 >= sample.Samples.size() || start + k >= frames) {
						break;
					}
					const float t = float(position - double(index));
					const float value = sample.Samples[index] * (1.0f - t) + sample.Samples[index + 1] * t;
					mix[std::size_t(start + k) * 2] += value * left;
					mix[std::size_t(start + k) * 2 + 1] += value * right;
				}
				effectCount++;
			}
		}

		// Loud moments are compressed rather than clipped
		for (float& v : mix) {
			if (v > 0.9f || v < -0.9f) {
				const float sign = (v < 0.0f ? -1.0f : 1.0f);
				v = sign * (0.9f + 0.1f * std::tanh((std::abs(v) - 0.9f) / 0.1f));
			}
		}

		// The audio track is VADPCM, which the mixer decodes on the RSP, without the Huffman stage the CPU would have
		// to undo - the video decoder needs all of the CPU it can get
		if (builtIn) {
#if defined(ASSETPACKER_WITH_N64_ENCODERS)
			// Encoded at the rate it was mixed at, the console's own (videoconv64 goes through 48 kHz and back)
			if (!WriteStereoWav64(audioOutputPath, mix.data(), frames, RenderRate, -1, Wav64Format::Vadpcm)) {
				fs::RemoveFile(videoOutputPath);
				return false;
			}
#endif
		} else {
#if !defined(DEATH_TARGET_EMSCRIPTEN)
			WriteWave(wavPath, mix.data(), frames, RenderRate);

			LOGI("Encoding \"{}\" as full-motion video ({} frames, {} sound effects)...", name, info.FrameCount, effectCount);
			// MPEG-1 at the console's width
			const String args[] = { tools.VideoConv, "-c"_s, "mpeg1"_s, "-w"_s, String(std::to_string(VideoWidth).c_str()),
				"--profile"_s, "cartoon"_s, "--audio-compress"_s, "vadpcm,huffman=false"_s, "--audio-parms"_s, "32000,2"_s,
				"--no-progress"_s, "-o"_s, cinematicsPath, yuvPath, wavPath };
			const bool encoded = RunTool(args);
			fs::RemoveFile(yuvPath);
			fs::RemoveFile(wavPath);
			if (!encoded || !fs::IsReadableFile(videoOutputPath)) {
				LOGW("videoconv64 could not encode \"{}\"", name);
				return false;
			}
#endif
		}
		LOGI("\"{}\" encoded: {} KB of video, {} KB of audio ({} frames, {} sound effects)", name,
			fs::GetFileSize(videoOutputPath) / 1024, fs::GetFileSize(audioOutputPath) / 1024, info.FrameCount, effectCount);
		return true;
	}
}
