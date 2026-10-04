// AssetPacker - converts original Jazz Jackrabbit 2 data into the layout a given platform loads.
//
// The game performs the same conversion on its first run (see GameEventHandler::RefreshCache), which stays
// the second, in-game way of doing it. This tool exists so the data can be prepared ahead of time - for the
// platforms that cannot convert anything themselves, and for build pipelines.

#include "CartridgeImage.h"
#include "DiscImage.h"
#include "FontPacker.h"
#include "ModuleConverter.h"
#include "N64Content.h"
#include "SpriteRepacker.h"

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
#include <Utf8.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(DEATH_TARGET_EMSCRIPTEN)
#	include <cerrno>
#	include <sys/types.h>

/**
	Emscripten's libc declares fallocate() without implementing it, and the base layer's FileSystem::Copy() calls it -
	only to preallocate the copy, and it carries on when the call says it is not supported, which is what this says
*/
extern "C" int fallocate(int fd, int mode, off_t offset, off_t length)
{
	static_cast<void>(fd);
	static_cast<void>(mode);
	static_cast<void>(offset);
	static_cast<void>(length);
	errno = ENOSYS;
	return -1;
}
#endif

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

			The consoles whose CPU cannot inflate the original container inside a frame need this: it costs
			55-115 ms a frame on the Dreamcast against a 42 ms budget, and the PlayStation 2 - which also
			draws the cinematics at a quarter of their pixels, see @ref DefaultVideoDownscaleForProfile() -
			is in the same position, where the re-encoded one costs under a millisecond on either. Everything
			else decodes the original perfectly well and is better off with the smaller file. Downscaling also
			requires it, since the frames have to be re-encoded either way.
		*/
		Recompress,
		/**
			@brief Encoded as full-motion video with the sound mixed in, for the Nintendo 64

			libdragon's decoder plays MPEG-1 with the RSP's help, which is the only way that console shows the
			cinematics at their own speed - and the video file is far smaller than the engine's container, which
			is what lets the ending fit on the cartridge at all. See @ref AssetPacker::N64Content.
		*/
		FullMotionVideo
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
		RecompressVideo,
		/** @brief Translate one of the game's Galaxy Music System modules into a FastTracker II module */
		ConvertMusic,
		/** @brief Replace the game content of an already built Dreamcast disc image */
		SwapDiscContent
	};

	struct Options {
		Command Action = Command::Convert;
		String SourcePath;
		String TargetPath;
		/** @brief The source directory named by `--source=` instead of as a positional argument */
		String SourceOverride;
		/** @brief The game's own content directory named by `--content=`, wherever the originals are */
		String ContentOverride;
		/** @brief The profile named by `--target=`, empty if none was */
		String ProfileName;
		/** @brief Whether `--video-downscale=` was given, which overrides what the profile asks for */
		bool VideoDownscaleSet = false;
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
		/** @brief Whether the tree is for the Nintendo 64, which gets its audio and cinematics in libdragon's formats */
		bool N64 = false;
		/** @brief Where libdragon's converters are, named by `--n64-tools=` (otherwise `N64_INST`) */
		String N64Tools;
		/** @brief Whether the sprite sheets and tilesets are written in LZ4, see @ref Compatibility::JJ2Anims::ImageCompression */
		bool Lz4Images = false;
	};

	/**
		@brief Where the original files, and optionally the game's own content, live under a given directory

		The source can be either a directory of original game files or a whole game installation, which keeps
		them in a `Source` subdirectory next to the `Content` the game ships and the `Cache` it converts into.
		Pointing the tool at the installation is the convenient thing to do, so it looks for both layouts.

		The two halves do not have to sit together, though, and outside an installation they usually do not ---
		a checkout has the game's content and no original data, and a copy of the original game is the other way
		round. `--content=` therefore names @ref ContentPath on its own, which is what lets a console tree be
		built from the two directories where they already are.
	*/
	struct SourceLayout {
		/** @brief Directory holding `Anims.j2a` and the rest of the original files */
		String OriginalsPath;
		/** @brief The game's own content directory, if one was found beside the originals or named explicitly */
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
		} else if (value == "convert-music"_s) {
			command = Command::ConvertMusic;
		} else if (value == "swap-content"_s) {
			command = Command::SwapDiscContent;
		} else {
			return false;
		}
		return true;
	}

	/**
		@brief How far a platform's cinematics are downscaled when nothing asks for a particular factor

		A console that cannot show a 640x480 frame decodes three quarters of every one of them for nothing:
		the player caps the frame texture at the drawable and picks every n-th index back out, per frame, on
		the CPU. Doing it here instead costs the same picture and none of the work - the decoder then walks a
		quarter of the pixels, the player's downscale pass disappears, and the file that has to come off the
		disc is smaller as well.

		The PlayStation 2 is 640x448 and fits the video into it, so a 640x480 cinematic is already halved at
		run time - 2 gives exactly what it displays. The Dreamcast halves at run time too, but its default
		stays 1 because it has been shipping that way and the re-encoded container alone is what it needed;
		`--video-downscale=` is there for anyone who wants the rest of the trade on that console as well.
		Zero means the profile makes no choice and the videos are copied across as they are.
	*/
	std::int32_t DefaultVideoDownscaleForProfile(StringView value)
	{
		if (value == "ps2"_s) {
			return 2;
		}
		if (value == "dreamcast"_s) {
			return 1;	// Re-encoded, but at the original resolution
		}
		return 0;
	}

	/**
		@brief Whether a target profile gets its sprite sheets and tilesets in LZ4

		The consoles that cannot convert on the device at all - their builds have no converter, and their tree
		is authored into the medium they boot from - so nothing but a tree made by this tool ever reaches
		them, and their builds carry an LZ4 decoder for it (`NCINE_WITH_LZ4`). The rest keep the game's own
		format, which every platform reads.
	*/
	bool ProfileUsesLz4Images(StringView value)
	{
		return (value == "n64"_s || value == "dreamcast"_s || value == "ps2"_s || value == "gamecube"_s);
	}

	bool TryParseProfile(StringView value, TargetProfile& profile, std::int32_t& defaultVideoDownscale, bool& isN64, bool& lz4Images)
	{
		defaultVideoDownscale = DefaultVideoDownscaleForProfile(value);
		isN64 = (value == "n64"_s);
		lz4Images = ProfileUsesLz4Images(value);
		if (value == "desktop"_s) {
			profile = TargetProfile::Desktop;
		} else if (value == "console"_s || value == "dreamcast"_s || value == "wii"_s || value == "gamecube"_s ||
				value == "psp"_s || value == "ps2"_s || value == "n64"_s) {
			// The consoles all consume the same staged tree, so they share one profile - only the cinematics
			// are decided per platform, so that is tracked separately
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
		LOGI("Usage: AssetPacker [<command>] [<source>] <target> [options]");
		LOGI("");
		LOGI("  convert [<source directory>] <target directory>   (the default command)");
		LOGI("    <source directory>   Directory containing the original game files (Anims.j2a, *.j2l, *.j2t, ...),");
		LOGI("                         or a game installation that keeps them in a \"Source\" subdirectory - in which");
		LOGI("                         case its \"Content\" is copied to the target as well. May be given as");
		LOGI("                         --source= instead, leaving <target directory> the only positional argument");
		LOGI("    <target directory>   Directory the converted data is written to (created if needed)");
		LOGI("    --source=<dir>       The same directory as <source directory>, named explicitly");
		LOGI("    --content=<dir>      Directory holding the game's own content - the fonts, \"Animations\",");
		LOGI("                         \"Metadata\" and translations that are not derived from the original data,");
		LOGI("                         which is the repository's \"Content\". Overrides one found beside the");
		LOGI("                         originals, and is what makes a console or web tree self-contained when");
		LOGI("                         the two halves are not kept together");
		LOGI("    --target=<profile>   desktop (default) | console | dreamcast | wii | gamecube | psp | ps2 | n64 |");
		LOGI("                         emscripten. dreamcast, ps2, gamecube and n64 get their sprite sheets and");
		LOGI("                         tilesets in LZ4, which only their builds decode");
		LOGI("    --n64-tools=<dir>    libdragon toolchain (or its \"bin\") for --target=n64, if N64_INST is not set.");
		LOGI("                         That profile stores the sound effects, the music and the cinematics in");
		LOGI("                         libdragon's formats, made with audioconv64, and videoconv64 with ffmpeg,");
		LOGI("                         where they are installed - and by the encoders built into the tool where");
		LOGI("                         not (if this build has them). \"builtin\" uses those even where libdragon's");
		LOGI("                         converters are installed");
		LOGI("    --video-downscale=N  Downscale cinematics by N (1-4); 1 keeps them at their original size.");
		LOGI("                         Cinematics are re-encoded for dreamcast and ps2 (or any N > 1) and");
		LOGI("                         otherwise copied unchanged; desktop gets none, as the game reads the");
		LOGI("                         originals. Defaults to what the profile asks for - 2 for ps2, which is");
		LOGI("                         what that console displays - and to 1 everywhere else");
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
		LOGI("  convert-music <source .j2b> <target .xm>");
		LOGI("    Translates one of the game's Galaxy Music System modules into a FastTracker II module");
		LOGI("  swap-content <source image> [<target image>] --content=<dir>");
		LOGI("    Replaces the \"Content\" directory of an already built console disc or ROM image with <dir>,");
		LOGI("    keeping its bootstrap and its executable exactly as they are - so the image can be given new game");
		LOGI("    data without the console toolchain it was built with. Reads a Dreamcast \".cdi\", a PlayStation 2");
		LOGI("    \".iso\" and a Nintendo 64 \".z64\"; <dir> is a directory prepared by \"convert --target=dreamcast\",");
		LOGI("    \"--target=ps2\" or \"--target=n64\". The image is rewritten in place if no target is given, and");
		LOGI("    a \".cdi\" grows only if the new content does not fit in the space the disc already has");
		LOGI("  swap-content <source image> [<target image>] --source=<dir> [--content=<dir>] [options]");
		LOGI("    The same, converting the original game files in --source= first, for the console the image is for");
		LOGI("    (a \".z64\" is n64, a \".cdi\" dreamcast, anything else ps2, unless --target= says otherwise);");
		LOGI("    --content= is then the game's own content, as for \"convert\", and the options of \"convert\" apply");
	}

	/** @brief Works out everything the profile named by `--target=` implies */
	bool ResolveProfile(Options& options)
	{
		std::int32_t profileVideoDownscale = 0;
		if (!options.ProfileName.empty() &&
			!TryParseProfile(options.ProfileName, options.Profile, profileVideoDownscale, options.N64, options.Lz4Images)) {
			LOGE("Unknown target profile \"{}\"", options.ProfileName);
			return false;
		}

		// A profile that names a downscale of its own supplies it unless the command line already did, so
		// "--target=ps2" alone produces what that console actually plays
		if (!options.VideoDownscaleSet && profileVideoDownscale > 0) {
			options.VideoDownscale = profileVideoDownscale;
		}

		// The desktop game finds the originals on its own, so nothing has to be done for it unless a downscale
		// was asked for; every other target needs them in the output tree
		if (options.N64) {
			// Where a cinematic cannot be encoded as video, it falls back to the engine's container at the size the
			// console displays (see the conversion below)
			options.Videos = VideoHandling::FullMotionVideo;
			if (!options.VideoDownscaleSet) {
				options.VideoDownscale = 2;
			}
		} else if (options.VideoDownscale > 1 || profileVideoDownscale > 0) {
			options.Videos = VideoHandling::Recompress;
		} else if (options.Profile != TargetProfile::Desktop) {
			options.Videos = VideoHandling::Copy;
		} else if (options.VideoDownscaleSet) {
			options.Videos = VideoHandling::Recompress;
		}
		return true;
	}

	bool ParseOptions(ArrayView<const StringView> args, Options& options)
	{
		std::size_t firstArgument = 1;
		if (args.size() > 1 && TryParseCommand(args[1], options.Action)) {
			firstArgument = 2;
		}

		for (std::size_t i = firstArgument; i < args.size(); i++) {
			StringView arg = args[i];
			if (arg.hasPrefix("--target="_s)) {
				options.ProfileName = arg.exceptPrefix("--target="_s);
			} else if (arg.hasPrefix("--source="_s)) {
				options.SourceOverride = arg.exceptPrefix("--source="_s);
			} else if (arg.hasPrefix("--content="_s)) {
				options.ContentOverride = arg.exceptPrefix("--content="_s);
			} else if (arg.hasPrefix("--video-downscale="_s)) {
				options.VideoDownscale = std::atoi(String(arg.exceptPrefix("--video-downscale="_s)).data());
				if (options.VideoDownscale < 1 || options.VideoDownscale > 4) {
					LOGE("Video downscale must be between 1 and 4");
					return false;
				}
				options.VideoDownscaleSet = true;
			} else if (arg == "--originals-only"_s) {
				options.OriginalsOnly = true;
			} else if (arg == "--shareware-only"_s) {
				options.SharewareOnly = true;
			} else if (arg == "--all-videos"_s) {
				options.AllVideos = true;
			} else if (arg == "--skip-non-episode-levels"_s) {
				options.SkipNonEpisodeLevels = true;
			} else if (arg.hasPrefix("--n64-tools="_s)) {
				options.N64Tools = arg.exceptPrefix("--n64-tools="_s);
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

		// `--source=` names exactly what the positional source argument does, so with it the target is the only
		// positional left and a lone one is that target. Giving both is a mistake rather than an override,
		// because there is no sensible reading in which the tool converts two different source directories.
		if (options.Action == Command::Convert && !options.SourceOverride.empty()) {
			if (options.TargetPath.empty()) {
				options.TargetPath = std::move(options.SourcePath);
				options.SourcePath = String{};
			} else if (!options.SourcePath.empty()) {
				LOGE("A source directory and \"--source=\" were both given, which name the same thing");
				return false;
			}
		}

		if (!ResolveProfile(options)) {
			return false;
		}

		if (options.Action == Command::Convert) {
			return !options.TargetPath.empty() && (!options.SourcePath.empty() || !options.SourceOverride.empty());
		}
		// The only command that rewrites what it is given, so it is also the only one whose target is optional
		if (options.Action == Command::SwapDiscContent) {
			if (options.ContentOverride.empty() && options.SourceOverride.empty()) {
				LOGE("\"swap-content\" needs the directory that is to become the content of the disc, named by \"--content=\", "
					"or the original game files to convert into it, named by \"--source=\"");
				return false;
			}
			return !options.SourcePath.empty();
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

		@param skippedNames			Entries of the top level that are not copied at all
		@param skippedExtensions	Extensions, lower-case and without the dot, that are not copied at any level
	*/
	bool CopyDirectoryRecursive(StringView sourcePath, StringView targetPath, ArrayView<const StringView> skippedNames = {},
		ArrayView<const StringView> skippedExtensions = {})
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
				// The name filter names top-level entries and so does not descend, the extension filter applies
				// to the tree as a whole - the files it is meant for are one level down
				success &= CopyDirectoryRecursive(item, targetItem, {}, skippedExtensions);
				continue;
			}

			if (!skippedExtensions.empty()) {
				String extension = fs::GetExtension(item);
				for (StringView skippedExtension : skippedExtensions) {
					if (extension == skippedExtension) {
						skipped = true;
						break;
					}
				}
				if (skipped) {
					continue;
				}
			}

			if (!fs::Copy(item, targetItem)) {
				LOGW("Cannot copy \"{}\" to \"{}\"", item, targetItem);
				success = false;
			}
		}
		return success;
	}

	/**
		@brief Adds a directory tree to a package under the specified path

		A file the package already carries is left out --- the conversion runs first, so what the original data
		provides is what a path present in both resolves to, exactly as it does when the two are separate. A
		hand-made sprite sheet that a platform would have to split into pages is re-laid out on the way in (see
		@ref AssetPacker::SpriteRepacker).
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
			if (s->IsValid() && fs::GetExtension(item) == "aura"_s) {
				// A sheet in LZ4 is stored as it is, compressing it again would only put an inflate in front of
				// the decoder it was chosen for (see JJ2Anims::ImageCompression)
				const bool lz4 = (Compatibility::JJ2Anims::PreferredImageCompression == Compatibility::JJ2Anims::ImageCompression::Lz4);
				const PakPreferredCompression sheetCompression = (lz4 ? PakPreferredCompression::None : PakPreferredCompression::Deflate);
				MemoryStream repacked(16384);
				bool rewritten = AssetPacker::SpriteRepacker::TryRepack(*s, repacked, item);
				if (!rewritten && lz4) {
					s->Seek(0, SeekOrigin::Begin);
					rewritten = AssetPacker::SpriteRepacker::TryConvertToLz4(*s, repacked, item);
				}
				if (rewritten) {
					repacked.Seek(0, SeekOrigin::Begin);
					if (!pakWriter.AddFile(repacked, targetItem, sheetCompression)) {
						LOGW("Cannot add \"{}\" to the package", item);
						success = false;
					}
					continue;
				}
				s->Seek(0, SeekOrigin::Begin);
			}
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

namespace
{
	/** @brief Converts the original game data the options name, which is what the tool exists for */
	bool ConvertGameData(const Options& options)
	{
		// `--source=` and the positional argument name the same thing, so both go through the same resolution
		// and a whole game installation is recognized either way
		StringView sourcePath = (options.SourceOverride.empty()
			? StringView(options.SourcePath)
			: StringView(options.SourceOverride));
		if (!fs::DirectoryExists(sourcePath)) {
			LOGE("Source directory \"{}\" does not exist", sourcePath);
			return false;
		}

		SourceLayout layout = ResolveSourceLayout(sourcePath);
		String animsPath = FindAnimsFile(layout.OriginalsPath);
		if (!fs::IsReadableFile(animsPath)) {
			LOGE("Cannot find \"Anims.j2a\" in \"{}\" or in its \"Source\" subdirectory. Make sure a supported Jazz Jackrabbit 2 version is present there.", sourcePath);
			return false;
		}

		// An explicit content directory wins over one found beside the originals, which is what allows the two
		// halves to be kept where they already are - a checkout's "Content" beside a copy of the original game
		if (!options.ContentOverride.empty()) {
			if (!fs::DirectoryExists(options.ContentOverride)) {
				LOGE("Content directory \"{}\" does not exist", options.ContentOverride);
				return false;
			}
			layout.ContentPath = options.ContentOverride;
		}

		// Producing a tree that is loaded as it is and has none of the game's own content is not an error
		// anywhere downstream - it converts, it writes, and the result cannot draw so much as a menu - so it is
		// worth saying out loud here, which is the only place that knows both halves were expected
		if (layout.ContentPath.empty() && options.Profile != TargetProfile::Desktop) {
			LOGW("No game content directory was found beside the original files, so the output will have no fonts, "
				"no \"Metadata\" and no translations. Pass \"--content=<dir>\" to point at the game's own \"Content\".");
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

		// The ".po" files beside the translations are the sources the ".mo" the game reads are compiled from, and
		// nothing loads one at run time - both the language list and the About section's translator credits
		// require the ".mo" extension. They are larger than the ".mo" they produce, so a tree that is deployed as
		// it is, onto a memory card or a disc that cannot be rewritten, is better off without them.
		static const StringView SkippedContentExtensions[] = { "po"_s };

		// The game's own content (fonts, animations, metadata, translations) is not derived from anything in the
		// original data, so a target that has to be self-contained needs it carried over alongside
		const bool selfContained = (!layout.ContentPath.empty() && options.Profile != TargetProfile::Desktop);
		if (selfContained) {
			LOGI("Copying \"{}\"...", layout.ContentPath);
			CopyDirectoryRecursive(layout.ContentPath, outputPath, PackedContentDirectories, SkippedContentExtensions);
		}

		// A tree that is loaded as it is gets the package name the game recognizes as "already converted", so it
		// never tries to rebuild a cache of its own from original files that are not deployed with it
		StringView packageName = (options.Profile == TargetProfile::Desktop
			? Compatibility::AssetConverter::SourcePackage
			: Compatibility::AssetConverter::PrebakedPackage);

		if (options.Lz4Images) {
#if defined(WITH_LZ4)
			Compatibility::JJ2Anims::PreferredImageCompression = Compatibility::JJ2Anims::ImageCompression::Lz4;
			LOGI("Sprite sheets and tilesets are written in LZ4");
#else
			LOGW("This build of the tool has no LZ4, so the sprite sheets and tilesets are written in the game's own format");
#endif
		}

		// The Nintendo 64 keeps its sound effects out of the package (see AssetPacker::N64Content), and a package
		// with a hash index cannot be listed afterwards - so for that console everything is written into a
		// temporary one with a name index first and split into the real one at the end
		AssetPacker::N64Content::Tools n64Tools;
		String n64TempPath, n64TempPak;
		if (options.N64) {
			if (!AssetPacker::N64Content::FindTools(options.N64Tools, n64Tools)) {
				LOGE("The n64 profile needs libdragon's audioconv64 - pass \"--n64-tools=<dir>\" or set N64_INST");
				return false;
			}
			if (!n64Tools.CanEncodeVideo) {
				LOGW("videoconv64, ffmpeg or ffprobe is missing, so the cinematics will not be encoded as video");
			} else if (!n64Tools.BuiltIn && n64Tools.VideoConv.empty()) {
				LOGI("videoconv64, ffmpeg or ffprobe is missing, so the cinematics are encoded by the tool itself");
			}
			n64TempPath = fs::CombinePath(outputPath, ".n64-temp"_s);
			fs::RemoveDirectoryRecursive(n64TempPath);
			fs::CreateDirectories(n64TempPath);
			n64TempPak = fs::CombinePath(n64TempPath, "Content.pak"_s);
		}
		const String pakPath = (options.N64 ? n64TempPak : String(fs::CombinePath(outputPath, packageName)));

		PakWriter pakWriter(pakPath, !options.N64);
		if (!pakWriter.IsValid()) {
			LOGE("Cannot open \"{}\" for writing", pakPath);
			return false;
		}

		Compatibility::JJ2Version version;
		if (Compatibility::AssetConverter::ConvertSourceAssets(animsPath, layout.OriginalsPath, pakWriter, version) ==
				Compatibility::AssetConverter::Result::UnsupportedVersion) {
			LOGE("Provided Jazz Jackrabbit 2 version is not supported");
			return false;
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

		if (options.N64) {
			PakWriter finalPak(fs::CombinePath(outputPath, packageName), true);
			if (!finalPak.IsValid() || !AssetPacker::N64Content::SplitPackage(n64TempPak, finalPak, outputPath, n64TempPath, n64Tools)) {
				LOGE("Cannot prepare the sound effects for the Nintendo 64");
				return false;
			}
			finalPak.Finalize();
		}

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

			if (options.Videos == VideoHandling::FullMotionVideo) {
				LOGI("Encoding cinematics as full-motion video...");
			} else if (options.Videos == VideoHandling::Recompress) {
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
				if (options.Videos == VideoHandling::FullMotionVideo) {
					if (AssetPacker::N64Content::ConvertCinematic(name, layout.OriginalsPath, outputPath, n64TempPak,
							fs::CombinePath(outputPath, "Music"_s), n64TempPath, n64Tools)) {
						continue;
					}
					// The engine's own container is the fallback, at the size the console shows it
					LOGW("Deploying \"{}\" in the engine's own format instead", name);
					if (!Compatibility::J2vRecompressor::Recompress(videoPath, targetVideoPath, options.VideoDownscale)) {
						LOGW("Cannot recompress \"{}\", skipping it", videoPath);
					}
				} else if (options.Videos == VideoHandling::Recompress) {
					if (!Compatibility::J2vRecompressor::Recompress(videoPath, targetVideoPath, options.VideoDownscale)) {
						LOGW("Cannot recompress \"{}\", skipping it", videoPath);
					}
				} else if (!fs::Copy(videoPath, targetVideoPath)) {
					LOGW("Cannot copy \"{}\", skipping it", videoPath);
				}
			}
		}

		if (options.N64) {
			// After the cinematics, which mix the original music into their soundtracks
			AssetPacker::N64Content::ConvertMusic(fs::CombinePath(outputPath, "Music"_s), n64TempPath, n64Tools);
			fs::RemoveDirectoryRecursive(n64TempPath);
		}

		if (options.Profile == TargetProfile::Desktop) {
			WriteCacheDescriptor(fs::CombinePath(outputPath, "Source.idx"_s),
				fs::GetLastModificationTime(animsPath).ToUnixMilliseconds());
		}

		return true;
	}

	/**
		@brief Converts the original game data for the console an image is for, and puts it into the image

		What the web build of the tool does in one go: the profile is the one the image calls for (unless
		`--target=` names another), the tree is converted into a temporary directory, and that directory becomes
		the content of the image.
	*/
	bool SwapContentFromSource(const Options& options)
	{
		if (!fs::IsReadableFile(options.SourcePath)) {
			LOGE("Cannot open \"{}\"", options.SourcePath);
			return false;
		}

		Options conversion;
		conversion.SourceOverride = options.SourceOverride;
		conversion.ContentOverride = options.ContentOverride;
		conversion.ProfileName = options.ProfileName;
		conversion.VideoDownscale = options.VideoDownscale;
		conversion.VideoDownscaleSet = options.VideoDownscaleSet;
		conversion.AllVideos = options.AllVideos;
		conversion.OriginalsOnly = options.OriginalsOnly;
		conversion.SharewareOnly = options.SharewareOnly;
		conversion.SkipNonEpisodeLevels = options.SkipNonEpisodeLevels;
		conversion.N64Tools = options.N64Tools;
		if (conversion.ProfileName.empty()) {
			conversion.ProfileName = (AssetPacker::CartridgeImage::IsCartridgeImage(options.SourcePath) ? "n64"_s
				: AssetPacker::DiscImage::IsDiscJugglerImage(options.SourcePath) ? "dreamcast"_s
				: "ps2"_s);
			LOGI("\"{}\" is an image for the \"{}\" profile", fs::GetFileName(options.SourcePath), conversion.ProfileName);
		}
		if (!ResolveProfile(conversion)) {
			return false;
		}

		// A build of the game older than the LZ4 sprite sheets and tilesets draws garbage from them, while every build
		// reads the game's own format - so a disc gets them only if the content already on it has them. A cartridge
		// always does: a build that plays the profile's sound and music (which are newer) reads them as well.
		if (conversion.Lz4Images && !AssetPacker::CartridgeImage::IsCartridgeImage(options.SourcePath) &&
			!AssetPacker::DiscImage::CarriesLz4Images(options.SourcePath)) {
			LOGI("The content on the disc keeps its sprite sheets and tilesets in the game's own format, which an older build "
				"of the game needs, so the new content keeps to it too");
			conversion.Lz4Images = false;
		}

		// Unique enough for two runs not to meet, the directory is removed again either way
		conversion.TargetPath = fs::CombinePath(fs::GetTempDirectory(),
			String("jazz2-content-"_s + String(std::to_string(DateTime::UtcNow().ToUnixMilliseconds()).c_str())));
		if (!fs::CreateDirectories(conversion.TargetPath)) {
			LOGE("Cannot create \"{}\"", conversion.TargetPath);
			return false;
		}

		bool success = ConvertGameData(conversion);
		if (success) {
			success = (AssetPacker::CartridgeImage::IsCartridgeImage(options.SourcePath)
				? AssetPacker::CartridgeImage::SwapContent(options.SourcePath, options.TargetPath, conversion.TargetPath)
				: AssetPacker::DiscImage::SwapContent(options.SourcePath, options.TargetPath, conversion.TargetPath));
		}
		fs::RemoveDirectoryRecursive(conversion.TargetPath);
		return success;
	}

	/** Runs the tool over the already-decoded UTF-8 command line */
	int RunAssetPacker(ArrayView<const StringView> args)
	{
		ConsoleSink consoleSink;
		Trace::AttachSink(&consoleSink);

		Options options;
		if (!ParseOptions(args, options)) {
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
				case Command::SwapDiscContent:
					if (!options.SourceOverride.empty()) {
						success = SwapContentFromSource(options);
					} else {
						success = (AssetPacker::CartridgeImage::IsCartridgeImage(options.SourcePath)
							? AssetPacker::CartridgeImage::SwapContent(options.SourcePath, options.TargetPath, options.ContentOverride)
							: AssetPacker::DiscImage::SwapContent(options.SourcePath, options.TargetPath, options.ContentOverride));
					}
					break;
				case Command::ConvertMusic:
					success = AssetPacker::ModuleConverter::ConvertJ2bToXm(options.SourcePath, options.TargetPath);
					if (success) {
						LOGI("\"{}\" converted to \"{}\", {} bytes", options.SourcePath, options.TargetPath, fs::GetFileSize(options.TargetPath));
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

		if (!ConvertGameData(options)) {
			return 1;
		}
		LOGI("Done");
		return 0;
	}
}

#if defined(DEATH_TARGET_WINDOWS)
/**
	Windows transcodes the narrow argv through the active code page, mangling every path it cannot
	represent, so the wide entry point is taken and the arguments are decoded to UTF-8 up front. The
	views are only taken once the strings are all in place, as String stores short ones inline.
*/
int wmain(std::int32_t argc, wchar_t** argv)
{
	SmallVector<String, 8> decoded;
	decoded.reserve(argc);
	for (std::int32_t i = 0; i < argc; i++) {
		decoded.push_back(Death::Utf8::FromUtf16(argv[i]));
	}

	SmallVector<StringView, 8> args;
	args.reserve(decoded.size());
	for (const String& arg : decoded) {
		args.push_back(arg);
	}
	return RunAssetPacker(args);
}
#else
int main(std::int32_t argc, char** argv)
{
	SmallVector<StringView, 8> args;
	args.reserve(argc);
	for (std::int32_t i = 0; i < argc; i++) {
		args.push_back(argv[i]);
	}
	return RunAssetPacker(args);
}
#endif
