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
		 * One entry of the frame table, `jjAnimFrames[index]`, with its image, its size and its hotspot, coldspot and
		 * gunspot. There's only one instance of an entry, as in JJ2+, so what a script changes stays - and the sprites
		 * the game draws of the frame show it too, once the script returns (see @ref AnimationTables).
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

			/** @brief Copies the image, the size and the spots of another frame */
			jjANIMFRAME& operator=(const jjANIMFRAME& o);

			/** @brief Returns the animation frame at the specified global index */
			static jjANIMFRAME* get_jjAnimFrames(std::uint32_t index);

			/** @brief Horizontal hotspot offset */
			std::int16_t hotSpotX = 0;
			/** @brief Vertical hotspot offset */
			std::int16_t hotSpotY = 0;
			/** @brief Horizontal coldspot offset */
			std::int16_t coldSpotX = 0;
			/** @brief Vertical coldspot offset */
			std::int16_t coldSpotY = 0;
			/** @brief Horizontal gun muzzle offset */
			std::int16_t gunSpotX = 0;
			/** @brief Vertical gun muzzle offset */
			std::int16_t gunSpotY = 0;
			/** @brief Frame width in pixels */
			std::uint16_t width = 0;
			/** @brief Frame height in pixels */
			std::uint16_t height = 0;

			/** @brief Returns whether the frame is drawn translucent */
			bool get_transparent() const;
			/** @brief Sets whether the frame is drawn translucent */
			bool set_transparent(bool value);
			/** @brief Returns whether the frame has colors of its own instead of palette indices, see @ref jjCOLORMAP */
			bool get_is32Bit() const;
			/** @brief Returns whether this frame collides with another frame at the given positions */
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
		 * One entry of the animation table, `jjAnimations[index]`: the number of frames, the playback speed and the
		 * index of the first frame in the frame table. There's only one instance of an entry, as in JJ2+.
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

			/** @brief Copies the frame count, the speed and the first frame of another animation */
			jjANIMATION& operator=(const jjANIMATION& o);

			/** @brief Saves the animation to a file using the given palette */
			bool save(const String& filename, const jjPAL& palette) const;
			/** @brief Loads animation frames from a file, overwriting starting at the given frame */
			bool load(const String& filename, std::int32_t hotSpotX, std::int32_t hotSpotY, std::int32_t coldSpotYOffset, std::int32_t firstFrameToOverwrite);

			/** @brief Returns the animation at the specified global index */
			static jjANIMATION* get_jjAnimations(std::uint32_t index);

			/** @brief Number of frames in the animation */
			std::uint16_t frameCount = 0;
			/** @brief Playback speed in frames per second */
			std::int16_t fps = 0;

			/** @brief Returns the global index of the first frame */
			std::uint32_t get_firstFrame() const;
			/** @brief Sets the global index of the first frame */
			std::uint32_t set_firstFrame(std::uint32_t index);

			/** @brief Returns the global index of the first frame of the animation */
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
		 * One entry of the set table, `jjAnimSets[setID]`, with the index of its first @ref jjANIMATION. A set of the
		 * original is always loaded, a custom one (`ANIM::CUSTOM[n]`) once a script loads or allocates it.
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

			/** @brief Returns the animation set at the specified global index */
			static jjANIMSET* get_jjAnimSets(std::uint32_t index);

			/** @brief Returns the index of the first animation of the set */
			std::uint32_t convertAnimSetToUint();

			/** @brief Loads animations from a file into this set, overwriting starting at the given animation */
			jjANIMSET* load(std::uint32_t fileSetID, const String& filename, int32_t firstAnimToOverwrite, int32_t firstFrameToOverwrite);
			/** @brief Allocates animations in this set with the given per-animation frame counts */
			jjANIMSET* allocate(const CScriptArray& frameCounts);
			/**
			 * @brief Loads animations from a sprite sheet into this set
			 *
			 * Each row of cells is an animation, its cells up to the first empty one are its frames, the hotspot at the
			 * middle of the bottom of a cell and the coldspot above it by the offset given for the animation. The animations
			 * and frames are new ones, unless @p firstAnimToOverwrite and @p firstFrameToOverwrite say which to overwrite.
			 */
			jjANIMSET* loadSheet(const jjPIXELMAP& sheet, std::uint32_t frameWidth, std::uint32_t frameHeight, std::uint32_t frameSpacingX,
				std::uint32_t frameSpacingY, std::uint32_t startX, std::uint32_t startY, const CScriptArray& coldSpotYOffsets,
				std::int32_t firstAnimToOverwrite, std::int32_t firstFrameToOverwrite);

			/** @brief Index of the first animation of the set in the animation table, 0 if the set isn't loaded */
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

	/** @brief Returns the custom animation set id at the given index */
	std::uint32_t getCustomSetID(std::uint8_t index);
}

#endif
