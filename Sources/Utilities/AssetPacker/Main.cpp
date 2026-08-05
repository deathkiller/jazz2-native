// AssetPacker - converts original Jazz Jackrabbit 2 data into the layout a given platform loads.
//
// The game performs the same conversion on its first run (see GameEventHandler::RefreshCache), which stays
// the second, in-game way of doing it. This tool exists so the data can be prepared ahead of time - for the
// platforms that cannot convert anything themselves, and for build pipelines.

#include "FontPacker.h"

#include "../../Main.h"
#include "../../Jazz2/ContentFileTypes.h"
#include "../../Jazz2/Compatibility/AssetConverter.h"
#include "../../Jazz2/Compatibility/J2vRecompressor.h"
#include "../../Jazz2/Compatibility/JJ2Anims.h"
#include "../../Jazz2/EventType.h"
#include "../../nCine/Base/Algorithms.h"

#include <Containers/Array.h>
#include <Containers/DateTime.h>
#include <Containers/SmallVector.h>
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

	/** @brief How the cinematics end up in the output */
	enum class VideoHandling {
		/** @brief Not at all - the game reads the original files where they are */
		None,
		/** @brief Copied across unchanged */
		Copy,
		/**
			@brief Re-encoded into the container the game decodes cheaply

			Only the Dreamcast needs this: the original container costs 55-115 ms a frame to inflate there
			against a 42 ms budget, where the re-encoded one costs under one. Everything else decodes the
			original perfectly well and is better off with the smaller file. Downscaling also requires it,
			since the frames have to be re-encoded either way.
		*/
		Recompress
	};

	/** @brief What the tool was asked to do */
	enum class Command {
		/** @brief Convert the original game data, which is what the tool exists for */
		Convert,
		/** @brief Pack an authored bitmap font into the file the game loads */
		PackFont,
		/** @brief Unpack a bitmap font back into the form it is authored in */
		UnpackFont,
		/** @brief Replace the palette indices of an image with the colors they stand for */
		ApplyPalette,
		/** @brief Resolve the colors of an image back to palette indices */
		ToIndices,
		/** @brief Re-encode one cinematic into the container the game plays, optionally downscaling it */
		RecompressVideo
	};

	struct Options {
		Command Action = Command::Convert;
		String SourcePath;
		String TargetPath;
		TargetProfile Profile = TargetProfile::Desktop;
		/** @brief How much the cinematics are downscaled; 1 keeps their original resolution */
		std::int32_t VideoDownscale = 1;
		/** @brief What happens to the cinematics */
		VideoHandling Videos = VideoHandling::None;
		/** @brief Deploy every cinematic present, not just the two the game plays */
		bool AllVideos = false;
		/** @brief Convert only the levels the original game shipped */
		bool OriginalsOnly = false;
		/** @brief Convert only what the Shareware Demo shipped */
		bool SharewareOnly = false;
		/** @brief Skip the levels that belong to no episode */
		bool SkipNonEpisodeLevels = false;
	};

	/**
		@brief Where the original files, and optionally the game's own content, live under a given directory

		The source can be either a directory of original game files or a whole game installation, which keeps
		them in a `Source` subdirectory next to the `Content` the game ships and the `Cache` it converts into.
		Pointing the tool at the installation is the convenient thing to do, so it looks for both layouts.
	*/
	struct SourceLayout {
		/** @brief Directory holding `Anims.j2a` and the rest of the original files */
		String OriginalsPath;
		/** @brief The game's own content directory, if the source turned out to be an installation */
		String ContentPath;
	};

	bool TryParseCommand(StringView value, Command& command)
	{
		if (value == "convert"_s) {
			command = Command::Convert;
		} else if (value == "pack-font"_s) {
			command = Command::PackFont;
		} else if (value == "unpack-font"_s) {
			command = Command::UnpackFont;
		} else if (value == "apply-palette"_s) {
			command = Command::ApplyPalette;
		} else if (value == "to-indices"_s) {
			command = Command::ToIndices;
		} else if (value == "recompress-video"_s) {
			command = Command::RecompressVideo;
		} else {
			return false;
		}
		return true;
	}

	bool TryParseProfile(StringView value, TargetProfile& profile, bool& isDreamcast)
	{
		isDreamcast = false;
		if (value == "desktop"_s) {
			profile = TargetProfile::Desktop;
		} else if (value == "console"_s || value == "dreamcast"_s || value == "wii"_s || value == "gamecube"_s || value == "psp"_s) {
			// The consoles all consume the same staged tree, so they share one profile - only the cinematics
			// are decided per platform, so that is tracked separately
			profile = TargetProfile::Console;
			isDreamcast = (value == "dreamcast"_s);
		} else if (value == "emscripten"_s || value == "web"_s) {
			profile = TargetProfile::Emscripten;
		} else {
			return false;
		}
		return true;
	}

	void PrintUsage()
	{
		LOGI("Usage: AssetPacker [<command>] <source> <target> [options]");
		LOGI("");
		LOGI("  convert <source directory> <target directory>   (the default command)");
		LOGI("    <source directory>   Directory containing the original game files (Anims.j2a, *.j2l, *.j2t, ...),");
		LOGI("                         or a game installation that keeps them in a \"Source\" subdirectory - in which");
		LOGI("                         case its \"Content\" is copied to the target as well");
		LOGI("    <target directory>   Directory the converted data is written to (created if needed)");
		LOGI("    --target=<profile>   desktop (default) | console | dreamcast | wii | gamecube | psp | emscripten");
		LOGI("    --video-downscale=N  Downscale cinematics by N (1-4); 1 (the default) keeps them as they are.");
		LOGI("                         Cinematics are re-encoded for dreamcast (or any N > 1) and otherwise");
		LOGI("                         copied unchanged; desktop gets none, as the game reads the originals");
		LOGI("    --originals-only     Convert only the episodes and levels the original game shipped");
		LOGI("    --shareware-only     Convert only what the Shareware Demo shipped (implies --originals-only)");
		LOGI("    --all-videos         Deploy every cinematic found, not just the two the game plays");
		LOGI("    --skip-non-episode-levels");
		LOGI("                         Convert only levels that belong to an episode");
		LOGI("");
		LOGI("  pack-font <source .png> <target .font>");
		LOGI("    Packs a grid image and the character list next to it into a single file. The list is read from");
		LOGI("    <source .png>.json, or from the binary <source .png>.font if there is no JSON next to the image");
		LOGI("  unpack-font <source .font> <target .png>");
		LOGI("    Unpacks a font back into a grid image and a character list, ready to be edited and packed again.");
		LOGI("    The list is written both as <target .png>.json, which is the one to edit, and as the binary");
		LOGI("    <target .png>.font");
		LOGI("  apply-palette <source .png> <target .png>");
		LOGI("    Replaces the palette indices of an image with the colors they stand for, so it can be edited");
		LOGI("  to-indices <source .png> <target .png>");
		LOGI("    Resolves the colors of an edited image back to the nearest palette indices");
		LOGI("  recompress-video <source .j2v> <target .j2v> [--video-downscale=N]");
		LOGI("    Re-encodes one cinematic on its own; N defaults to 1, which keeps the original resolution");
	}

	bool ParseOptions(std::int32_t argc, char** argv, Options& options)
	{
		bool videoDownscaleSet = false;
		bool isDreamcast = false;

		std::int32_t firstArgument = 1;
		if (argc > 1 && TryParseCommand(argv[1], options.Action)) {
			firstArgument = 2;
		}

		for (std::int32_t i = firstArgument; i < argc; i++) {
			StringView arg = argv[i];
			if (arg.hasPrefix("--target="_s)) {
				if (!TryParseProfile(arg.exceptPrefix("--target="_s), options.Profile, isDreamcast)) {
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
			} else if (arg == "--originals-only"_s) {
				options.OriginalsOnly = true;
			} else if (arg == "--shareware-only"_s) {
				options.SharewareOnly = true;
			} else if (arg == "--all-videos"_s) {
				options.AllVideos = true;
			} else if (arg == "--skip-non-episode-levels"_s) {
				options.SkipNonEpisodeLevels = true;
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

		// The desktop game finds the originals on its own, so nothing has to be done for it unless a downscale
		// was asked for; every other target needs them in the output tree
		if (options.VideoDownscale > 1 || isDreamcast) {
			options.Videos = VideoHandling::Recompress;
		} else if (options.Profile != TargetProfile::Desktop) {
			options.Videos = VideoHandling::Copy;
		} else if (videoDownscaleSet) {
			options.Videos = VideoHandling::Recompress;
		}

		return !options.SourcePath.empty() && !options.TargetPath.empty();
	}

	/** @brief Locates `Anims.j2a`, or the shareware `AnimsSw.j2a`, in the specified directory */
	String FindAnimsFile(StringView path)
	{
		String result = fs::FindPathCaseInsensitive(fs::CombinePath(path, "Anims.j2a"_s));
		if (!fs::IsReadableFile(result)) {
			result = fs::FindPathCaseInsensitive(fs::CombinePath(path, "AnimsSw.j2a"_s));
		}
		return result;
	}

	/** @brief Works out whether the source is a directory of original files or a whole game installation */
	SourceLayout ResolveSourceLayout(StringView sourcePath)
	{
		SourceLayout layout;

		// An installation keeps the original files one level down, so that is where to look first - a directory
		// that has them at the top is taken as they are
		String nested = fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, "Source"_s));
		if (fs::DirectoryExists(nested) && fs::IsReadableFile(FindAnimsFile(nested))) {
			layout.OriginalsPath = std::move(nested);

			String content = fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, "Content"_s));
			if (fs::DirectoryExists(content)) {
				layout.ContentPath = std::move(content);
			}
		} else {
			layout.OriginalsPath = sourcePath;
		}

		return layout;
	}

	/**
		@brief Copies a directory tree, adding to whatever is already at the target

		@param skippedNames	Entries of the top level that are not copied at all
	*/
	bool CopyDirectoryRecursive(StringView sourcePath, StringView targetPath, ArrayView<const StringView> skippedNames = {})
	{
		if (!fs::CreateDirectories(targetPath)) {
			return false;
		}

		bool success = true;
		for (auto item : fs::Directory(sourcePath)) {
			StringView itemName = fs::GetFileName(item);
			bool skipped = false;
			for (StringView skippedName : skippedNames) {
				if (itemName == skippedName) {
					skipped = true;
					break;
				}
			}
			if (skipped) {
				continue;
			}

			String targetItem = fs::CombinePath(targetPath, itemName);
			if (fs::DirectoryExists(item)) {
				success &= CopyDirectoryRecursive(item, targetItem);
			} else if (!fs::Copy(item, targetItem)) {
				LOGW("Cannot copy \"{}\" to \"{}\"", item, targetItem);
				success = false;
			}
		}
		return success;
	}

	/**
		@brief Adds a directory tree to a package under the specified path

		A file the package already carries is left out --- the conversion runs first, so what the original data
		provides is what a path present in both resolves to, exactly as it does when the two are separate.
	*/
	bool AddDirectoryToPak(PakWriter& pakWriter, StringView sourcePath, StringView targetPath)
	{
		bool success = true;
		for (auto item : fs::Directory(sourcePath)) {
			String targetItem = fs::CombinePath(targetPath, fs::GetFileName(item));
			if (fs::DirectoryExists(item)) {
				success &= AddDirectoryToPak(pakWriter, item, targetItem);
				continue;
			}

			if (pakWriter.FileExists(targetItem)) {
				continue;
			}

			auto s = fs::Open(item, FileAccess::Read);
			if (!s->IsValid() || !pakWriter.AddFile(*s, targetItem, PakPreferredCompression::Deflate)) {
				LOGW("Cannot add \"{}\" to the package", item);
				success = false;
			}
		}
		return success;
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

	// The asset-level commands work on single files and share nothing with the conversion below
	if (options.Action != Command::Convert) {
		bool success;
		switch (options.Action) {
			case Command::PackFont: success = AssetPacker::FontPacker::Pack(options.SourcePath, options.TargetPath); break;
			case Command::UnpackFont: success = AssetPacker::FontPacker::Unpack(options.SourcePath, options.TargetPath); break;
			case Command::ApplyPalette: success = AssetPacker::FontPacker::ApplyPalette(options.SourcePath, options.TargetPath); break;
			case Command::RecompressVideo:
				success = Compatibility::J2vRecompressor::Recompress(options.SourcePath, options.TargetPath, options.VideoDownscale);
				if (success) {
					LOGI("\"{}\" re-encoded to \"{}\" at 1/{} scale, {} bytes", options.SourcePath, options.TargetPath,
						options.VideoDownscale, fs::GetFileSize(options.TargetPath));
				}
				break;
			default: success = AssetPacker::FontPacker::ConvertToIndices(options.SourcePath, options.TargetPath); break;
		}
		if (!success) {
			return 1;
		}
		LOGI("Done");
		return 0;
	}

	if (!fs::DirectoryExists(options.SourcePath)) {
		LOGE("Source directory \"{}\" does not exist", options.SourcePath);
		return 1;
	}

	SourceLayout layout = ResolveSourceLayout(options.SourcePath);
	String animsPath = FindAnimsFile(layout.OriginalsPath);
	if (!fs::IsReadableFile(animsPath)) {
		LOGE("Cannot find \"Anims.j2a\" in \"{}\" or in its \"Source\" subdirectory. Make sure a supported Jazz Jackrabbit 2 version is present there.", options.SourcePath);
		return 1;
	}

	// The desktop game keeps the converted data in a "Cache" subdirectory and looks for it there; the other
	// profiles are consumed as a prepared content tree, so they are written directly into the target
	String outputPath = (options.Profile == TargetProfile::Desktop
		? String(fs::CombinePath(options.TargetPath, "Cache"_s))
		: options.TargetPath);
	fs::CreateDirectories(outputPath);

	LOGI("Converting \"{}\" to \"{}\"...", layout.OriginalsPath, outputPath);

	// The two directories the game reads through the package layer go inside the package instead of next to
	// it, which is one file to open rather than a few hundred - the difference a console actually pays for.
	// Everything else the tree carries is read as a loose file and has to stay one.
	static const StringView PackedContentDirectories[] = { "Animations"_s, "Metadata"_s };

	// The game's own content (fonts, animations, metadata, translations) is not derived from anything in the
	// original data, so a target that has to be self-contained needs it carried over alongside
	const bool selfContained = (!layout.ContentPath.empty() && options.Profile != TargetProfile::Desktop);
	if (selfContained) {
		LOGI("Copying \"{}\"...", layout.ContentPath);
		CopyDirectoryRecursive(layout.ContentPath, outputPath, PackedContentDirectories);
	}

	// A tree that is loaded as it is gets the package name the game recognizes as "already converted", so it
	// never tries to rebuild a cache of its own from original files that are not deployed with it
	StringView packageName = (options.Profile == TargetProfile::Desktop
		? Compatibility::AssetConverter::SourcePackage
		: Compatibility::AssetConverter::PrebakedPackage);

	PakWriter pakWriter(fs::CombinePath(outputPath, packageName), true);
	if (!pakWriter.IsValid()) {
		LOGE("Cannot open \"{}\" for writing", fs::CombinePath(outputPath, packageName));
		return 1;
	}

	Compatibility::JJ2Version version;
	if (Compatibility::AssetConverter::ConvertSourceAssets(animsPath, layout.OriginalsPath, pakWriter, version) ==
			Compatibility::AssetConverter::Result::UnsupportedVersion) {
		LOGE("Provided Jazz Jackrabbit 2 version is not supported");
		return 1;
	}

	// Added after the conversion, so a path both of them have resolves to what the original data provided,
	// which is what it does when the two are kept apart
	if (selfContained) {
		for (StringView packedDirectory : PackedContentDirectories) {
			String packedPath = fs::CombinePath(layout.ContentPath, packedDirectory);
			if (fs::DirectoryExists(packedPath)) {
				LOGI("Packing \"{}\"...", packedPath);
				AddDirectoryToPak(pakWriter, packedPath, packedDirectory);
			}
		}
	}

	pakWriter.Finalize();

	SmallVector<String, 0> skippedLevels;
	Compatibility::AssetConverter::ConversionOptions conversionOptions;
	conversionOptions.OriginalsOnly = options.OriginalsOnly;
	conversionOptions.SharewareOnly = options.SharewareOnly;
	conversionOptions.SkipNonEpisodeLevels = options.SkipNonEpisodeLevels;
	// The desktop game reads music from its own content directory, not from the cache this writes
	conversionOptions.CopyUsedMusic = (options.Profile != TargetProfile::Desktop);
	conversionOptions.SkippedLevels = &skippedLevels;
	Compatibility::AssetConverter::ConvertLevels(layout.OriginalsPath, outputPath, true, conversionOptions);

	if (!skippedLevels.empty()) {
		// Listed rather than only counted, because the list of levels the original game shipped is maintained
		// by hand and this is how a name missing from it shows up
		LOGI("{} levels were skipped:", skippedLevels.size());
		for (String& levelName : skippedLevels) {
			LOGI("  {}", levelName);
		}
	}

	if (options.Videos != VideoHandling::None) {
		// Only the first two are ever played (see the Cinematics handlers in Main.cpp); "Logo" is in the
		// original data but nothing asks for it, so it is left out unless everything was asked for
		static const StringView everyVideo[] = { "Intro"_s, "Ending"_s, "Logo"_s };
		const ArrayView<const StringView> videoNames = ArrayView<const StringView>(everyVideo)
			.prefix(options.AllVideos ? arraySize(everyVideo) : 2);

		String cinematicsPath = fs::CombinePath(outputPath, "Cinematics"_s);
		fs::CreateDirectories(cinematicsPath);

		if (options.Videos == VideoHandling::Recompress) {
			LOGI("Recompressing cinematics...");
		} else {
			LOGI("Copying cinematics...");
		}

		for (StringView name : videoNames) {
			String videoPath = fs::FindPathCaseInsensitive(fs::CombinePath(layout.OriginalsPath, String(name + ".j2v"_s)));
			if (!fs::IsReadableFile(videoPath)) {
				continue;
			}

			// The player looks the files up in lower case
			String targetVideoPath = fs::CombinePath(cinematicsPath, StringUtils::lowercase(name + ".j2v"_s));
			if (options.Videos == VideoHandling::Recompress) {
				if (!Compatibility::J2vRecompressor::Recompress(videoPath, targetVideoPath, options.VideoDownscale)) {
					LOGW("Cannot recompress \"{}\", skipping it", videoPath);
				}
			} else if (!fs::Copy(videoPath, targetVideoPath)) {
				LOGW("Cannot copy \"{}\", skipping it", videoPath);
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
