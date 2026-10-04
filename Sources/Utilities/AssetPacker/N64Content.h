#pragma once

#include "../../Main.h"

#include <Containers/String.h>
#include <Containers/StringView.h>
#include <IO/PakFile.h>

using namespace Death::Containers;

namespace Jazz2::AssetPacker
{
	/**
		@brief Turns a converted content tree into the formats the Nintendo 64 plays natively

		The console streams its audio from the cartridge through libdragon's RSP mixer and plays its cinematics
		with libdragon's RSP-assisted video decoder, and all three of those read formats of libdragon's own. They
		are produced either with libdragon's converters (`audioconv64`, `videoconv64`, which needs `ffmpeg`) or
		with the encoders built into this tool (@ref Wav64Writer, @ref Xm64Converter and @ref Mpeg1Encoder), which
		need nothing installed - the web build has only those. A tree made for this console is exactly what goes
		into the ROM:

		- Every **sound effect** leaves the package and becomes a `.wav64` file of its own under `Animations/`,
		  VADPCM the mixer streams from the cartridge and decodes on the RSP while it plays - nothing is decoded
		  into the console's RAM.
		- **Music** in the game's own Galaxy format is translated into XM (see @ref ModuleConverter) and then into
		  `.xm64`, whose instruments stream from the cartridge as well; an XM track goes straight to `.xm64`.
		  Tracks in the other tracker formats are rendered with libopenmpt, where the tool was built with it,
		  into a looping ULC-compressed `.wav64`.
		- The **cinematics** become MPEG-1 video with their music and sound effects mixed into one audio track.
	*/
	class N64Content
	{
	public:
		N64Content() = delete;

		/** @brief The converters the formats are produced with */
		struct Tools
		{
			/** @brief The toolchain prefix, which `videoconv64` needs in `N64_INST` to find `audioconv64` */
			String Root;
			String AudioConv;
			String VideoConv;
			/** @brief Whether the cinematics can be encoded as video */
			bool CanEncodeVideo = false;
			/** @brief Whether the encoders built into this tool are used instead of libdragon's converters */
			bool BuiltIn = false;
		};

		/**
			@brief Whether this build of the tool has encoders of its own for libdragon's formats

			They are compiled wherever the C codecs they are built on are known to compile (GCC, Clang and
			Emscripten); elsewhere libdragon's converters are the only way.
		*/
		static bool HasBuiltInEncoders();

		/**
			@brief Picks the converters to use

			libdragon's converters are preferred where they are installed, so a tree made on a development machine
			stays what the console's toolchain produces; otherwise the built-in encoders are used where this build has
			them, which is always the case for the web build, as it cannot run other programs at all.

			@param hint		A libdragon toolchain prefix or its `bin` directory, empty to use `N64_INST`, or `builtin`
							to use the built-in encoders even where libdragon's converters are installed
		*/
		static bool FindTools(StringView hint, Tools& tools);

		/**
			@brief Copies a package into @p target, except for its sound samples, which become loose `.wav64` files

			@param sourcePak	A package written with a name index, so its contents can be listed
			@param outputPath	The content tree the `.wav64` files are written into, under `Animations/`
			@param tempPath		Scratch directory, which only libdragon's converters need
		*/
		static bool SplitPackage(StringView sourcePak, Death::IO::PakWriter& target, StringView outputPath, StringView tempPath, const Tools& tools);

		/** @brief Replaces every track in @p musicPath with its `.xm64` or `.wav64` equivalent */
		static void ConvertMusic(StringView musicPath, StringView tempPath, const Tools& tools);

		/**
			@brief Encodes the cinematics as full-motion video into `Cinematics/` of @p outputPath

			@param originalsPath	Where the original `.j2v` files are
			@param sourcePak		The name-indexed package the sound effects and playlists are read from
			@param musicPath		Where the cinematics' music (`intro.j2b`, `ending.j2b`) is, before conversion
			@returns `false` for a cinematic that could not be encoded, which is then left for the caller to deploy
				in the engine's own format instead
		*/
		static bool ConvertCinematic(StringView name, StringView originalsPath, StringView outputPath, StringView sourcePak,
			StringView musicPath, StringView tempPath, const Tools& tools);
	};
}
