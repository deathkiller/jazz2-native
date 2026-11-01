#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Palette.h"

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		class jjPIXELMAP;

		/**
		 * @brief A single frame of a sprite animation
		 *
		 * One entry of the global frame table @ref jjAnimFrames, holding an image, its size and its hotspot, coldspot and
		 * gunspot. Each entry exists only once, so changes a script makes to it last and also show in the sprites the
		 * game draws with that frame.
		 */
		class jjANIMFRAME
		{
			friend class AnimationTables;
			friend class jjPIXELMAP;
			friend class jjCOLORMAP;

		public:
			/** @brief Creates a new instance, the entry at the given index of the frame table */
			explicit jjANIMFRAME(std::uint32_t index);
			~jjANIMFRAME();

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/**
			 * @brief Copies another frame into this one
			 *
			 * Copies the image, the size, the hotspot, coldspot and gunspot and the translucency.
			 */
			jjANIMFRAME& operator=(const jjANIMFRAME& o);

			/**
			 * @brief Returns the animation frame at a global index
			 *
			 * Indices are the ones used by @ref jjANIMATION::firstFrame and by the `curFrame` of objects and players.
			 */
			static jjANIMFRAME* get_jjAnimFrames(std::uint32_t index);

			/**
			 * @brief Horizontal position of the hotspot
			 *
			 * The hotspot is the point of the frame placed at the drawing position, also used for collisions. The value
			 * is added to the drawing position to get the top left corner, so it is usually negative.
			 */
			std::int16_t hotSpotX = 0;
			/** @brief Vertical position of the hotspot, see `hotSpotX` */
			std::int16_t hotSpotY = 0;
			/**
			 * @brief Horizontal position of the coldspot
			 *
			 * The coldspot is where the frame touches the ground, used by walking enemies, crates and players. Measured
			 * the same way as `hotSpotX`.
			 */
			std::int16_t coldSpotX = 0;
			/** @brief Vertical position of the coldspot, see @ref coldSpotX */
			std::int16_t coldSpotY = 0;
			/**
			 * @brief Horizontal position of the gunspot
			 *
			 * The gunspot is where projectiles start; mostly frames of players and shooting enemies define it. Measured
			 * the same way as `hotSpotX`.
			 */
			std::int16_t gunSpotX = 0;
			/** @brief Vertical position of the gunspot, see @ref gunSpotX */
			std::int16_t gunSpotY = 0;
			/** @brief Width of the frame in pixels */
			std::uint16_t width = 0;
			/** @brief Height of the frame in pixels */
			std::uint16_t height = 0;

			/**
			 * @brief Whether the frame is drawn translucent by default
			 *
			 * Objects drawn in @ref SPRITE::NORMAL with such a frame appear translucent, e.g. the freezer ammo pickups.
			 */
			bool get_transparent() const;
			/** @brief Sets whether the frame is drawn translucent by default */
			bool set_transparent(bool value);
			/** @brief Whether the frame stores 32-bit colors instead of palette indices */
			bool get_is32Bit() const;
			/**
			 * @brief Returns whether this frame would overlap another one
			 *
			 * Both frames are placed by their hotspots at the given positions and flipped as their directions say
			 * (negative flips horizontally, as in @ref jjCANVAS::drawSprite()). With `always`, a single shared pixel is a
			 * collision (as bullets use); otherwise at least eight overlapping pixels are needed.
			 */
			bool doesCollide(std::int32_t xPos, std::int32_t yPos, std::int32_t direction, const jjANIMFRAME* frame2, std::int32_t xPos2, std::int32_t yPos2, std::int32_t direction2, bool always) const;

		private:
			std::int32_t _refCount;
			std::uint32_t _index;
			// Palette indices, width * height of them, 0 is transparent
			std::unique_ptr<std::uint8_t[]> _pixels;
			// Colors with alpha of a 32-bit frame, width * height of them, or null - `_pixels` has the nearest palette indices
			// of them then, for what reads the frame as indices (collisions, jjPIXELMAP)
			std::unique_ptr<jjPALCOLOR[]> _colors;
			bool _transparent;
			// Whether the frame is in the list of frames AnimationTables::Flush() looks at
			bool _watched;
			// Index of the animation whose sprite sheet has the image of the frame, read on first use, -1 once read
			std::int32_t _source;
			// Index of the animation the frame was made for, which draws it best
			std::uint32_t _owner;
			// Distinguishes every change of the frame from any other, 0 for the frame as the game has it
			std::uint32_t _version;
			// The spots as AnimationTables::Flush() saw them last, the ones a script writes are told apart by them
			std::int16_t _seenSpots[6];
		};

		/**
		 * @brief A sprite animation consisting of a sequence of frames
		 *
		 * One entry of the global animation table @ref jjAnimations: the number of frames, the frame rate and the index
		 * of the first frame in @ref jjAnimFrames. Each entry exists only once, so changes a script makes to it last.
		 * Converts implicitly to the index of its first frame.
		 */
		class jjANIMATION
		{
			friend class AnimationTables;

		public:
			/** @brief Creates a new instance, the entry at the given index of the animation table */
			explicit jjANIMATION(std::uint32_t index);
			~jjANIMATION();

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/**
			 * @brief Copies another animation into this one
			 *
			 * Copies the frame count, the frame rate and the first frame index. The frames themselves aren't copied,
			 * both animations then share them.
			 */
			jjANIMATION& operator=(const jjANIMATION& o);

			/**
			 * @brief Saves the frames of the animation as an image file
			 *
			 * @partialsupport Currently has no effect and always returns `false`.
			 */
			bool save(const String& filename, const jjPAL& palette) const;
			/**
			 * @brief Loads the frames of the animation from an image file
			 *
			 * @partialsupport Currently has no effect and always returns `false`.
			 */
			bool load(const String& filename, std::int32_t hotSpotX, std::int32_t hotSpotY, std::int32_t coldSpotYOffset, std::int32_t firstFrameToOverwrite);

			/**
			 * @brief Returns the animation at a global index
			 *
			 * The animations of a set start at @ref jjANIMSET::firstAnim.
			 */
			static jjANIMATION* get_jjAnimations(std::uint32_t index);

			/** @brief Number of frames in the animation */
			std::uint16_t frameCount = 0;
			/** @brief Frame rate of the animation, usually 10 */
			std::int16_t fps = 0;

			/** @brief Returns the index of the first frame in @ref jjAnimFrames */
			std::uint32_t get_firstFrame() const;
			/** @brief Sets the index of the first frame in `jjAnimFrames` */
			std::uint32_t set_firstFrame(std::uint32_t index);

			/** @brief Converts the animation to the index of its first frame in @ref jjAnimFrames */
			std::uint32_t getAnimFirstFrame();

		private:
			std::int32_t _refCount;
			std::uint32_t _index;
			std::uint32_t _firstFrame;
			// Whether the animation is in the list of animations AnimationTables::Flush() looks at
			bool _watched;
			// The frame count as AnimationTables::Flush() saw it last
			std::uint16_t _seenFrameCount;
			// Index of the graphics of the game the animation is shown in (see AnimationTables), -1 if it's in none
			std::int32_t _native;
			// The sprite sheet a script draws the animation with when it isn't in the graphics of the game, made on
			// first use, and what it was made of
			std::unique_ptr<Resources::GenericGraphicResource> _graphics;
			std::uint64_t _graphicsSignature;
		};

		/**
		 * @brief A set of related sprite animations
		 *
		 * One entry of the global set table @ref jjAnimSets, indexed by @ref ANIM::Set, holding the index of its first
		 * animation in @ref jjAnimations. The sets of the original game are always available; a custom set
		 * (`ANIM::CUSTOM[n]`) exists once a script loads or allocates it. Converts implicitly to @ref firstAnim.
		 */
		class jjANIMSET
		{
			friend class AnimationTables;

		public:
			/** @brief Creates a new instance, the entry of the given set ID */
			explicit jjANIMSET(std::uint32_t index);
			~jjANIMSET();

			/** @brief Increments the reference count */
			void AddRef();
			/** @brief Decrements the reference count */
			void Release();

			/** @brief Returns the animation set of an @ref ANIM::Set value */
			static jjANIMSET* get_jjAnimSets(std::uint32_t index);

			/** @brief Converts the set to the index of its first animation, @ref firstAnim */
			std::uint32_t convertAnimSetToUint();

			/**
			 * @brief Loads animations into this set and returns the set
			 *
			 * Without a file name, copies the animations of the original game's set `fileSetID` (2048 means this set
			 * itself, which is already loaded). With a file name, reads set `fileSetID` from that `.j2a` file. New
			 * animations and frames are added at the end of @ref jjAnimations and @ref jjAnimFrames, unless
			 * `firstAnimToOverwrite` or `firstFrameToOverwrite` give the entries to overwrite.
			 */
			jjANIMSET* load(std::uint32_t fileSetID, const String& filename, int32_t firstAnimToOverwrite, int32_t firstFrameToOverwrite);
			/**
			 * @brief Creates empty animations for this set and returns the set
			 *
			 * Adds one animation per element of `frameCounts`, each with that many blank frames, and points @ref firstAnim
			 * at the first of them.
			 */
			jjANIMSET* allocate(const CScriptArray& frameCounts);
			/**
			 * @brief Loads animations into this set from a sprite sheet and returns the set
			 *
			 * The sheet is a grid of `frameWidth` by `frameHeight` cells starting at `startX`, `startY`, separated by
			 * `frameSpacingX` and `frameSpacingY` pixels. Each row is an animation whose frames are its cells up to the
			 * first empty one; the first row without any frame ends the set. Each frame's hotspot and coldspot X are
			 * at the middle of its cell, and its coldspot Y is moved by the row's entry of `coldSpotYOffsets`. Frames
			 * keep the full size of the cell. New animations and frames are added unless `firstAnimToOverwrite` and
			 * `firstFrameToOverwrite` give the entries to overwrite.
			 */
			jjANIMSET* loadSheet(const jjPIXELMAP& sheet, std::uint32_t frameWidth, std::uint32_t frameHeight, std::uint32_t frameSpacingX,
				std::uint32_t frameSpacingY, std::uint32_t startX, std::uint32_t startY, const CScriptArray& coldSpotYOffsets,
				std::int32_t firstAnimToOverwrite, std::int32_t firstFrameToOverwrite);

			/** @brief Index of the set's first animation in @ref jjAnimations, 0 if the set isn't loaded */
			std::uint32_t firstAnim = 0;

		private:
			std::int32_t _refCount;
			std::uint32_t _index;
			// Whether the set of the original was looked up already (see AnimationTables::ResolveSet()), and how many
			// animations it has
			bool _resolved;
			std::uint32_t _animCount = 0;
			// Whether the set is in the list of sets AnimationTables::Flush() looks at, and firstAnim as it saw it last
			bool _watched;
			std::uint32_t _seenFirstAnim;
		};
	}
}

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/**
	 * @brief Returns the @ref ANIM::Set value of a custom animation set
	 *
	 * The custom sets follow the sets of the original game; `index` selects one of them.
	 */
	std::uint32_t getCustomSetID(std::uint8_t index);
}

#endif
