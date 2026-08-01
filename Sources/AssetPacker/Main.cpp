// AssetPacker --- converts original Jazz Jackrabbit 2 data into the layout a given platform loads.
//
// The game performs the same conversion on its first run (see GameEventHandler::RefreshCache), which stays
// the second, in-game way of doing it. This tool exists so the data can be prepared ahead of time - for the
// platforms that cannot convert anything themselves, and for build pipelines.

#include "../Main.h"
#include "../Jazz2/ContentFileTypes.h"
#include "../Jazz2/Compatibility/AssetConverter.h"
#include "../Jazz2/Compatibility/J2vRecompressor.h"
#include "../Jazz2/Compatibility/JJ2Anims.h"
#include "../Jazz2/EventType.h"
#include "../nCine/Base/Algorithms.h"

#include <Containers/DateTime.h>
#include <Containers/String.h>
#include <Containers/StringUtils.h>
#include <Containers/StringConcatenable.h>
#include <Core/Logger.h>
#include <IO/FileSystem.h>

#include <cstdio>
#include <cstdlib>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;
using namespace Death::Trace;
using namespace Jazz2;

#define NCINE_VERSION_s DEATH_PASTE(NCINE_VERSION, _s)

namespace
{
	/**
		@brief Prints trace messages to the console

		The converters report their progress through the usual logging macros, which go nowhere without a sink
		attached - the game attaches the application itself. This prints plainly to stdout (errors to stderr),
		which is what a command-line tool wants.
	*/
	class ConsoleSink : public ITraceSink
	{
	protected:
		void OnTraceFlushed() override
		{
			std::fflush(stdout);
			std::fflush(stderr);
		}

		void OnTraceReceived(TraceLevel level, std::uint64_t timestamp, StringView threadId,
			StringView functionName, StringView content) override
		{
			static_cast<void>(timestamp);
			static_cast<void>(threadId);
			static_cast<void>(functionName);

			FILE* target = (level >= TraceLevel::Error ? stderr : stdout);
			if (level == TraceLevel::Warning) {
				std::fputs("Warning: ", target);
			} else if (level >= TraceLevel::Error) {
				std::fputs("Error: ", target);
			}
			std::fwrite(content.data(), 1, content.size(), target);
			std::fputc('\n', target);
			std::fflush(target);
		}
	};

	/** @brief What the output directory is going to be loaded by */
	enum class TargetProfile {
		/** @brief Desktop `Cache/` layout, including the index the game checks on startup */
		Desktop,
		/** @brief Staged `Content/` tree for the consoles, which cannot rewrite a cache of their own */
		Console,
		/** @brief Layout for the web build, likewise prepared entirely ahead of time */
		Emscripten
	};

	struct Options {
		String SourcePath;
		String TargetPath;
		TargetProfile Profile = TargetProfile::Desktop;
		/** @brief How much the cinematics are downscaled; 1 leaves them alone (and copies nothing) */
		std::int32_t VideoDownscale = 1;
	};

	bool TryParseProfile(StringView value, TargetProfile& profile)
	{
		if (value == "desktop"_s) {
			profile = TargetProfile::Desktop;
		} else if (value == "console"_s || value == "dreamcast"_s || value == "wii"_s || value == "gamecube"_s) {
			// The consoles all consume the same staged tree, so they share one profile
			profile = TargetProfile::Console;
		} else if (value == "emscripten"_s || value == "web"_s) {
			profile = TargetProfile::Emscripten;
		} else {
			return false;
		}
		return true;
	}

	void PrintUsage()
	{
		LOGI("Usage: AssetPacker <source directory> <target directory> [--target=<profile>]");
		LOGI("");
		LOGI("  <source directory>   Directory containing the original game files (Anims.j2a, *.j2l, *.j2t, ...)");
		LOGI("  <target directory>   Directory the converted data is written to (created if needed)");
		LOGI("  --target=<profile>   desktop (default) | console | dreamcast | wii | gamecube | emscripten");
		LOGI("  --video-downscale=N  Downscale cinematics by N (1-4); defaults to 2 for console, 1 otherwise");
	}

	bool ParseOptions(std::int32_t argc, char** argv, Options& options)
	{
		bool videoDownscaleSet = false;

		for (std::int32_t i = 1; i < argc; i++) {
			StringView arg = argv[i];
			if (arg.hasPrefix("--target="_s)) {
				if (!TryParseProfile(arg.exceptPrefix("--target="_s), options.Profile)) {
					LOGE("Unknown target profile \"{}\"", arg.exceptPrefix("--target="_s));
					return false;
				}
			} else if (arg.hasPrefix("--video-downscale="_s)) {
				options.VideoDownscale = std::atoi(String(arg.exceptPrefix("--video-downscale="_s)).data());
				if (options.VideoDownscale < 1 || options.VideoDownscale > 4) {
					LOGE("Video downscale must be between 1 and 4");
					return false;
				}
				videoDownscaleSet = true;
			} else if (arg == "--help"_s || arg == "-h"_s) {
				return false;
			} else if (options.SourcePath.empty()) {
				options.SourcePath = arg;
			} else if (options.TargetPath.empty()) {
				options.TargetPath = arg;
			} else {
				LOGE("Unexpected argument \"{}\"", arg);
				return false;
			}
		}

		if (!videoDownscaleSet) {
			// Only the Dreamcast cannot keep up with the full-resolution videos; everything else plays them
			// as they are, and downscaling would only throw detail away
			options.VideoDownscale = (options.Profile == TargetProfile::Console ? 2 : 1);
		}

		return !options.SourcePath.empty() && !options.TargetPath.empty();
	}

	/** @brief Locates `Anims.j2a`, or the shareware `AnimsSw.j2a`, in the source directory */
	String FindAnimsFile(StringView sourcePath)
	{
		String path = fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, "Anims.j2a"_s));
		if (!fs::IsReadableFile(path)) {
			path = fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, "AnimsSw.j2a"_s));
		}
		return path;
	}

	/**
		@brief Writes the index the desktop game checks before deciding to reconvert

		Deliberately not written for the other profiles: the consoles and the web build never rewrite their
		data, and an index there would only invite the game to try.
	*/
	void WriteCacheDescriptor(StringView path, std::int64_t animsModified)
	{
		// Must stay identical to GameEventHandler::WriteCacheDescriptor - the game compares every field and
		// reconverts everything if any of them disagrees, including the event count and the build version
		constexpr std::uint64_t currentVersion = nCine::parseVersion(NCINE_VERSION_s);

		auto so = fs::Open(path, FileAccess::Write);
		so->WriteValueAsLE<std::uint64_t>(0x2095A59FF0BFBBEF);	// Signature
		so->WriteValue<std::uint8_t>(ContentFileType::CacheIndex);
		so->WriteValueAsLE<std::uint16_t>(Compatibility::JJ2Anims::CacheVersion);
		so->WriteValue<std::uint8_t>(0x00);	// Flags
		so->WriteValueAsLE<std::int64_t>(animsModified);
		so->WriteValueAsLE<std::uint16_t>(std::uint16_t(EventType::Count));
		so->WriteValueAsLE<std::uint64_t>(currentVersion);
	}
}

int main(int argc, char** argv)
{
	ConsoleSink consoleSink;
	Trace::AttachSink(&consoleSink);

	Options options;
	if (!ParseOptions(argc, argv, options)) {
		PrintUsage();
		return 1;
	}

	if (!fs::DirectoryExists(options.SourcePath)) {
		LOGE("Source directory \"{}\" does not exist", options.SourcePath);
		return 1;
	}

	String animsPath = FindAnimsFile(options.SourcePath);
	if (!fs::IsReadableFile(animsPath)) {
		LOGE("Cannot find \"Anims.j2a\" in \"{}\". Make sure a supported Jazz Jackrabbit 2 version is present there.", options.SourcePath);
		return 1;
	}

	// The desktop game keeps the converted data in a "Cache" subdirectory and looks for it there; the other
	// profiles are consumed as a prepared content tree, so they are written directly into the target
	String outputPath = (options.Profile == TargetProfile::Desktop
		? String(fs::CombinePath(options.TargetPath, "Cache"_s))
		: options.TargetPath);
	fs::CreateDirectories(outputPath);

	LOGI("Converting \"{}\" to \"{}\"...", options.SourcePath, outputPath);

	Compatibility::JJ2Version version;
	switch (Compatibility::AssetConverter::ConvertSourceAssets(animsPath, options.SourcePath, outputPath, version)) {
		case Compatibility::AssetConverter::Result::CannotWriteTarget:
			LOGE("Cannot open \"{}\" for writing", fs::CombinePath(outputPath, "Source.pak"_s));
			return 1;
		case Compatibility::AssetConverter::Result::UnsupportedVersion:
			LOGE("Provided Jazz Jackrabbit 2 version is not supported");
			return 1;
		default:
			break;
	}

	Compatibility::AssetConverter::ConvertLevels(options.SourcePath, outputPath, true);

	// The cinematics are downscaled once here for the platforms that cannot afford to do it per frame while
	// playing. They keep the same container, so the result plays anywhere the original does.
	if (options.VideoDownscale > 1) {
		static const StringView videoNames[] = { "Intro"_s, "Ending"_s, "Logo"_s };
		String cinematicsPath = fs::CombinePath(outputPath, "Cinematics"_s);
		fs::CreateDirectories(cinematicsPath);

		for (StringView name : videoNames) {
			String videoPath = fs::FindPathCaseInsensitive(fs::CombinePath(options.SourcePath, String(name + ".j2v"_s)));
			if (!fs::IsReadableFile(videoPath)) {
				continue;
			}

			// The player looks the files up in lower case
			String targetVideoPath = fs::CombinePath(cinematicsPath, StringUtils::lowercase(name + ".j2v"_s));
			if (!Compatibility::J2vRecompressor::Recompress(videoPath, targetVideoPath, options.VideoDownscale)) {
				LOGW("Cannot recompress \"{}\", skipping it", videoPath);
			}
		}
	}

	if (options.Profile == TargetProfile::Desktop) {
		WriteCacheDescriptor(fs::CombinePath(outputPath, "Source.idx"_s),
			fs::GetLastModificationTime(animsPath).ToUnixMilliseconds());
	}

	LOGI("Done");
	return 0;
}
