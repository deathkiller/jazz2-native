#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Definitions.h"
#include "../../ContentResolver.h"

namespace Jazz2::Compatibility
{
	class AnimSetMapping;
}

namespace Jazz2::Scripting::Legacy
{
	/**
		@brief Animation tables of the level scripts of JJ2+, `jjAnimSets`, `jjAnimations` and `jjAnimFrames`

		Three tables, the sets by their set ID, the animations and the frames by their index. A set has a run of
		animations from its `firstAnim`, an animation a run of frames from its `firstFrame` - which is all the
		arithmetic the scripts do (`jjAnimations[jjAnimSets[ANIM::BAT] + 2].firstFrame + 1`). An entry is made on
		first use and is the only one of its index, so a change a script makes to it stays.

		The sets of the original are always loaded: a set gets its animations and frames, read from the converted
		sprite sheets, the first time it's used. Each animation of a set of the original has the graphics of the game
		(@ref Resources::GenericGraphicResource) that the actors draw it with, and those show whatever animation is at
		`firstAnim + n` of the set - after a script changes the frames, loads other animations into the set or makes
		an animation share the frames of another (`jjAnimations[a] = jjAnimations[b]`), the graphics are made of
		them again once the script returns (see @ref Flush()). The number of frames of the graphics stays as the
		game has it, the frames of the animation are repeated over them, and a frame of its own size and hotspot
		gets a packed rectangle of the sheet, so nothing that keeps a pointer to the graphics notices. The graphics
		are restored when the tables are destroyed, so the next level gets them as they were.

		An animation that isn't in the graphics of the game, e.g. one of a custom set, is drawn by scripts with a
		sprite sheet of its own, made on first use.

		The set IDs are those of the animation file of the game (the "Set ID" of Jazz Sprite Dynamite, from 0),
		which differ between 1.23 and 1.24 after the dragon. The sets of `plus.j2a` follow, the custom sets start
		at @ref CustomFirstSetID.
	*/
	class AnimationTables
	{
	public:
		/** @brief Set ID of `ANIM::CUSTOM[0]` */
		static constexpr std::uint32_t CustomFirstSetID = 256;
		/** @brief Number of set IDs */
		static constexpr std::uint32_t SetCount = CustomFirstSetID + 256;
		/** @brief Number of entries the animation table can have, `jjOBJ::curAnim` is 16-bit */
		static constexpr std::uint32_t MaxAnimations = INT16_MAX;
		/** @brief Number of entries the frame table can have, the animation file of the game has less than 15 000 */
		static constexpr std::uint32_t MaxFrames = 1u << 17;

		/** @brief Creates the tables for the animation file of the given version of the game */
		explicit AnimationTables(bool isTSF);
		/** @brief Destroys the tables, restoring the graphics of the game they changed */
		~AnimationTables();

		AnimationTables(const AnimationTables&) = delete;
		AnimationTables& operator=(const AnimationTables&) = delete;

		/** @brief Returns the set ID of a set of the original, -1 if the version of the game doesn't have it */
		static std::int32_t GetSetID(anim set, bool isTSF);

		/**
			@brief Returns the entry of a set, AddRef'd, or `nullptr` with a script exception if there's no such set

			A set of the original gets its animations here, on first use.
		*/
		jjANIMSET* GetSet(std::uint32_t setID);
		/** @brief Returns the entry of an animation, AddRef'd, or `nullptr` with a script exception if it's out of the table */
		jjANIMATION* GetAnimation(std::uint32_t index);
		/** @brief Returns the entry of a frame with its image read, AddRef'd, or `nullptr` with a script exception if it's out of the table */
		jjANIMFRAME* GetFrame(std::uint32_t index);

		/** @brief Returns the index of the first animation of a set, 0 if the set isn't loaded */
		std::uint32_t GetFirstAnim(std::int32_t setID);
		/** @brief Returns the frame count and the first frame of an animation, `false` if there's no such animation */
		bool GetAnimationFrames(std::uint32_t animIndex, std::uint32_t& frameCount, std::uint32_t& firstFrame);
		/**
			@brief Returns the rectangle of a frame and the one of its pixels, both from its hotspot

			Returns `false` if there's no such frame or it has no pixels.
		*/
		bool GetFrameBounds(std::uint32_t frameIndex, Recti& frameRect, Recti& pixelBounds);

		/** @brief Loads a set of an animation file into a set, as `jjANIMSET::load()` */
		bool LoadSet(jjANIMSET& set, std::uint32_t fileSetID, StringView filename, std::int32_t firstAnimToOverwrite, std::int32_t firstFrameToOverwrite);
		/** @brief Gives a set new animations with empty frames, as `jjANIMSET::allocate()` */
		void AllocateSet(jjANIMSET& set, ArrayView<const std::uint32_t> frameCounts);

		/** @brief Makes an animation the same as another one, as `jjANIMATION::operator=()` */
		void AssignAnimation(jjANIMATION& target, const jjANIMATION& source);
		/** @brief Changes the first frame of an animation */
		void SetFirstFrame(jjANIMATION& anim, std::uint32_t firstFrame);
		/** @brief Makes a frame the same as another one, as `jjANIMFRAME::operator=()` */
		void AssignFrame(jjANIMFRAME& target, const jjANIMFRAME& source);
		/** @brief Replaces the image of a frame by palette indices, @p width × @p height of them */
		void SetFramePixels(jjANIMFRAME& frame, const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height);
		/**
			@brief Replaces the image of a frame by colors with alpha, @p width × @p height of them, a 32-bit frame of JJ2+

			@p indices are the nearest palette indices of the colors, 0 where there's no alpha, for what reads the frame
			as palette indices. The graphics an animation with such a frame is drawn with are true color, its 8-bit
			frames are in the colors of the palette, made again when it changes (see @ref OnPaletteChanged()).
		*/
		void SetFrameColors(jjANIMFRAME& frame, const jjPALCOLOR* colors, const std::uint8_t* indices, std::uint32_t width, std::uint32_t height);
		/** @brief Tells the tables the palette of the sprites changed, so do the 8-bit frames of true color graphics */
		void OnPaletteChanged();
		/** @brief Tells the tables a frame changed in a way they can't see themselves */
		void MarkChanged(jjANIMFRAME& frame);

		/**
			@brief Returns whether two frames collide at the given positions, as `jjANIMFRAME::doesCollide()`

			The frames are placed by their hotspots and flipped by their directions (`SPRITE::Direction`). With
			@p always a single common pixel is enough, otherwise it takes a few.
		*/
		bool DoFramesCollide(const jjANIMFRAME& frame1, std::int32_t x1, std::int32_t y1, std::int32_t direction1,
			const jjANIMFRAME& frame2, std::int32_t x2, std::int32_t y2, std::int32_t direction2, bool always);

		/**
			@brief Returns the graphics to draw a frame of an animation of a set with, and the frame of the graphics

			The frame is taken modulo the frame count of the animation, as JJ2+ does. Returns `nullptr` if there's
			nothing to draw.
		*/
		Resources::GenericGraphicResource* ResolveSprite(std::int32_t setID, std::int32_t animation, std::int32_t frame, std::int32_t& cell);
		/** @brief Returns the graphics to draw a frame of the frame table with, and the frame of the graphics */
		Resources::GenericGraphicResource* ResolveFrame(std::uint32_t frameIndex, std::int32_t& cell);
		/**
			@brief Returns the animation of the animation table that graphics of the game show, or 0

			An object of the game is drawn with the graphics of an animation of a set of the original, which is the one at
			`firstAnim + n` of the set - also after a script put another animation there. 0 for graphics of no animation of
			the original, e.g. those of the remastered content.
		*/
		std::uint32_t FindAnimationOfGraphics(const Resources::GenericGraphicResource* graphics);
		/**
			@brief Returns the graphics that play an animation of the animation table, for an actor that draws it

			The graphics have @p cellCount frames, which play the animation from the first one.
		*/
		Resources::GenericGraphicResource* ResolveAnimation(std::uint32_t animIndex, std::int32_t& cellCount);

		/**
			@brief Brings the graphics of the game up to date with what scripts changed

			Called after every call of a script, it costs nothing while nothing changed. A change a script makes by a
			method is known right away, the values a script writes to the properties are found out here, by comparing
			them to what they were.
		*/
		void Flush();
		/**
			@brief Gives the graphics of the game back as they were, and writes no more into them

			Called once the level is left (see @ref LevelScriptLoader::OnReplacing()), before the next one loads the same
			graphics, and when the tables go.
		*/
		void RestoreNativeGraphics();

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		// What the graphics of the game were before they were changed, the collision mask is made of the sheet again
		struct OriginalGraphics
		{
			ContentResolver::SpriteSheet Sheet;
			SmallVector<Resources::FrameRect, 0> FrameRects;
		};

		// Graphics of a single frame and the version of the frame they were made of
		struct FrameGraphics
		{
			std::unique_ptr<Resources::GenericGraphicResource> Graphics;
			std::uint32_t Version;
		};

		// An animation of a set of the original and the graphics of the game it has
		struct NativeAnimation
		{
			// The sprite sheet, relative to `Animations`, empty if the animation has none
			String Path;
			// The graphics, requested on first use
			Resources::GenericGraphicResource* Graphics;
			std::uint32_t SetID;
			std::uint32_t AnimInSet;
			// Number of frames of the graphics and the first frame made for them in the frame table
			std::uint32_t FrameCount;
			std::uint32_t FirstFrame;
			// The entry of the animation table made for the animation
			std::uint32_t OriginalAnim;
			// The entry of the animation table the graphics show, and its signature (see GetSignature())
			std::uint32_t ShownAnim;
			std::uint64_t ShownSignature;
			std::uint64_t OriginalSignature;
			// What the graphics had before they were changed, to restore them
			std::unique_ptr<OriginalGraphics> Original;
		};
#endif

		bool _isTSF;
		std::unique_ptr<Compatibility::AnimSetMapping> _mapping;
		std::unique_ptr<Compatibility::AnimSetMapping> _plusMapping;
		std::uint32_t _plusFirstSetID;
		std::uint32_t _plusSetCount;

		// The entries of the tables, each holds a reference released in the destructor; the first animation and the first
		// frame are empty ones, as a set that isn't loaded has firstAnim 0
		SmallVector<jjANIMSET*, 0> _sets;
		SmallVector<jjANIMATION*, 0> _animations;
		SmallVector<jjANIMFRAME*, 0> _frames;
		SmallVector<NativeAnimation, 0> _natives;
		// Graphics of single frames that no animation shows, see ResolveFrame()
		HashMap<std::uint32_t, FrameGraphics> _frameGraphics;
		// The set and the animation in it (set << 16 | animation) of graphics of the game, UINT32_MAX for graphics of none,
		// and the same by the path of the sprite sheet, made on first use (see FindAnimationOfGraphics())
		HashMap<const Resources::GenericGraphicResource*, std::uint32_t> _graphicsAnimations;
		HashMap<String, std::uint32_t> _pathAnimations;

		// The entries given to scripts, whose properties Flush() compares to what they were (as long as a script holds them)
		SmallVector<jjANIMSET*, 0> _watchedSets;
		SmallVector<jjANIMATION*, 0> _watchedAnimations;
		SmallVector<jjANIMFRAME*, 0> _watchedFrames;
		// Counts the changes, Flush() has nothing to do while it stays what it was the last time
		std::uint32_t _changes;
		std::uint32_t _flushedChanges;
		std::uint32_t _lastFrameVersion;
		// Counts the changes of the palette, a part of the signature of an animation with a 32-bit frame
		std::uint32_t _paletteVersion = 0;
		bool _warnedTrueColor;
		// The graphics of the game were given back, see RestoreNativeGraphics()
		bool _nativeGraphicsRestored = false;

		// Returns the mapping and the index in the file of a set of the original, or nullptr
		Compatibility::AnimSetMapping* GetFileSet(std::uint32_t setID, std::uint32_t& fileSet);
		// Makes the animations and the frames of a set of the original, the first time the set is used
		void ResolveSet(jjANIMSET& set);
		// Returns the entry of a set, making it if needed, but doesn't resolve it
		jjANIMSET* EnsureSet(std::uint32_t setID);
		// Returns the entry at an index of a table, making the entries up to it, or nullptr if it's too far
		jjANIMATION* EnsureAnimation(std::uint32_t index);
		jjANIMFRAME* EnsureFrame(std::uint32_t index);
		jjANIMATION* AddAnimation();
		jjANIMFRAME* AddFrame(std::uint32_t owner);
		// Reads the image of a frame if it has none yet (see jjANIMFRAME::_source)
		void EnsureFrameImage(jjANIMFRAME& frame);
		void ReadNativeFrames(std::uint32_t nativeIndex);

		void Watch(jjANIMSET& set);
		void Watch(jjANIMATION& anim);
		void Watch(jjANIMFRAME& frame);
		void MarkChanged(jjANIMATION& anim);

		// Returns a value that changes whenever what an animation shows changes
		std::uint64_t GetSignature(const jjANIMATION& anim) const;
		// Returns the graphics of the game of an animation of the original, requesting them on first use
		Resources::GenericGraphicResource* RequestNativeGraphics(NativeAnimation& native);
		// Makes the graphics of the game show an animation, or restores them
		void ShowInNativeGraphics(NativeAnimation& native, jjANIMATION& anim);
		void RestoreNativeGraphics(NativeAnimation& native);
		// Returns the sprite sheet of its own an animation is drawn with, made of its frames if needed
		Resources::GenericGraphicResource* RequestOwnGraphics(jjANIMATION& anim);
		// Returns the frames an animation shows, with their images
		void GetFrames(const jjANIMATION& anim, SmallVectorImpl<jjANIMFRAME*>& frames);
		// Lays the frames out in a sheet and uploads it into the graphics; `keepLayout` keeps the frame count, the
		// size of the cell and the hotspot of the graphics (of the game), the frames repeat over the cells, otherwise
		// the graphics are made to fit the frames
		void ComposeGraphics(Resources::GenericGraphicResource& graphics, ArrayView<jjANIMFRAME* const> frames, std::int32_t fps, bool keepLayout);
		// Uploads an image of (palette index, alpha) pairs, or of RGBA colors if `trueColor`, into the texture of graphics
		// and rebuilds the collision mask; the graphics are indexed or not as the image is
		static void UploadGraphics(Resources::GenericGraphicResource& graphics, const std::uint8_t* pixels, Vector2i size, const char* name, bool trueColor = false);
	};
}

#endif
