#pragma once

#include "Alignment.h"
#include "BlendMode.h"
#include "../../nCine/Graphics/RenderBatcher.h"
#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/SceneNode.h"
#include "../../nCine/Primitives/Rect.h"

using namespace nCine;

/**
	@brief Whether a canvas keeps what it drew for the next frame, see @ref Jazz2::UI::Canvas

	A textured rectangle drawn with the same arguments as in the previous frame reuses the command of that
	frame, a string drawn again leaves its glyphs in the batch of its record. Anything that changed is drawn
	as before. On the Nintendo 64 it took the HUD from 1.6 to 0.9 ms a frame.

	Needs a backend that reads the uniform memory of a command in place, the others copy it on every commit.
*/
#if defined(WITH_RHI_RDP) || defined(DOXYGEN_GENERATING_OUTPUT)
#	define CANVAS_RETAINED_DRAWS
#endif

namespace Jazz2::UI
{
	/**
		@brief Canvas
		
		Base drawing surface for on-screen UI rendering. It collects textured and solid rectangle draw calls as render
		commands and submits them to the render queue, also providing alignment helpers and palette-based recoloring.
	*/
	class Canvas : public SceneNode
	{
		friend class Font;

	public:
		/** @brief Creates a new instance */
		Canvas();

		/** @brief View size of the canvas */
		Vector2i ViewSize;
		/** @brief Animation time of the canvas */
		float AnimTime;

		/**
		 * @brief Translation added to every drawn primitive in screen-space
		 *
		 * Together with @ref LayerScale and @ref LayerColor forms an optional draw transform applied to all
		 * primitives, including text drawn through @ref Font. Used by the menu section transition system to slide,
		 * scale and fade an entire section without per-section cooperation. Identity by default, so non-menu
		 * canvases (HUD, cinematics, …) are unaffected. A final screen position is `worldPos * LayerScale + LayerOffset`.
		 */
		Vector2f LayerOffset = Vector2f::Zero;
		/** @brief Uniform scale applied to every drawn primitive */
		float LayerScale = 1.0f;
		/**
		 * @brief Pixels of the target for a unit drawn, 1 by default
		 *
		 * Glyphs are put on whole pixels of the target, a canvas drawn into a target larger than the units it's drawn
		 * in --- by its camera --- puts them on its pixels with it.
		 */
		float PixelScale = 1.0f;
		/** @brief Tint multiplied into the color of every drawn primitive */
		Colorf LayerColor = Colorf::White;

		/**
		 * @brief Rectangle in screen-space every drawn primitive is clipped to, one without area clips nothing
		 *
		 * Applied after the layer transform, on the CPU, so it doesn't stop primitives from being batched --- one
		 * outside of it is left out, one across its edge is cut down together with its texture coordinates. Text
		 * drawn through @ref Font is clipped glyph by glyph. A rotated primitive is only left out when all of it is
		 * outside, it's never cut. Nothing is clipped by default.
		 */
		Rectf ClipRect = Rectf(0.0f, 0.0f, 0.0f, 0.0f);
		/**
		 * @brief Whether primitives of the same layer are drawn in the order they were drawn in
		 *
		 * Otherwise their order is unspecified, so overlapping primitives have to be on different layers. Off by
		 * default, as only the level editor and what level scripts draw need it.
		 */
		bool OrderedDraws = false;
		/** @brief Whether text drawn through @ref Font is added to what's behind it instead of drawn over it, off by default */
		bool AdditiveText = false;

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		/**
		 * @brief Draws a textured rectangle
		 *
		 * `paletteOffset` >= 0 marks the texture as indexed (palette index in the red channel), recolored at draw time
		 * through the shared palette texture at that offset; -1 = plain RGBA. A `shader` draws it in place of the
		 * sprite shaders, it has to be the palette variant for an indexed texture. Only @ref BlendMode::Normal,
		 * @ref BlendMode::Additive and @ref BlendMode::Difference are supported for textures, with
		 * @ref BlendMode::Difference the color of a transparent texel must be black.
		 */
		void DrawTexture(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color, BlendMode blending = BlendMode::Normal, float angle = 0.0f, std::int32_t paletteOffset = -1,
			Shader* shader = nullptr);
		/**
		 * @brief Draws a textured rectangle into one batch with the rectangles drawn right before it on the same layer
		 *
		 * Draws the same as @ref DrawTexture() without rotation, palette and additive blending, but rectangles drawn one
		 * after another on a layer with the same texture share a single command, written straight into its batch, as
		 * the glyphs of @ref Font are. Anything else drawn on the layer in between --- by @ref DrawTexture(),
		 * @ref DrawSolid(), @ref DrawRenderCommand() or a text --- ends the batch, the next rectangle starts another one,
		 * so everything is drawn in the order it was drawn in as before. Meant for an interface of many small shapes.
		 */
		void DrawTextureBatched(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color);
		/**
		 * @brief Draws an indexed textured rectangle recolored through a palette texture (PaletteRemap shader)
		 *
		 * The palette offset is the flat index of the first color in the palette texture (0 = the first row/palette).
		 */
		void DrawTextureWithPalette(const Texture& texture, const Texture& palette, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color, float paletteOffset = 0.0f);
		/** @brief Draws a solid rectangle, turned by `angle` in radians about its center */
		void DrawSolid(Vector2f pos, std::uint16_t z, Vector2f size, const Colorf& color, BlendMode blending = BlendMode::Normal, float angle = 0.0f);
		
		/** @brief Applies alignment settings to a given position vector */
		static Vector2f ApplyAlignment(Alignment align, Vector2f vec, Vector2f size);

		/** @brief Rents a render command for rendering on the canvas */
		RenderCommand* RentRenderCommand();
		/** @brief Draws a raw render command */
		void DrawRenderCommand(RenderCommand* command);

		/**
		 * @brief Clips a rectangle to @ref ClipRect, returns `false` if nothing of it is left
		 *
		 * The position and size are in screen-space, after the layer transform. Texture coordinates in the layout
		 * the sprite shaders take (horizontal scale and offset, then vertical) are cut down along with the rectangle.
		 */
		bool ClipQuad(Vector2f& pos, Vector2f& size, Vector4f* texCoords) const {
			// Nothing is clipped by default, only that is inline, as it's checked for every glyph of every text
			if (ClipRect.W <= 0.0f || ClipRect.H <= 0.0f) {
				return true;
			}
			return ClipQuadToRect(pos, size, texCoords);
		}

		/** @brief Multiplier of game time for canvas rendering, @ref AnimTime grows by it a frame */
		static constexpr float AnimTimeMultiplier = 0.014f;

	private:
		/**
			@brief Glyphs of one font, shader and layer, written straight into batched commands

			See @ref Font::DrawString(). Every string drawn into the canvas with the same font, shader and layer goes
			through the same record, so they share their batches just like the commands of their glyphs used to be
			batched together. A record outlives the frame so that a batch can reserve the room its record needed in
			the previous one: a menu draws the same text frame after frame, so it fits a single batch per record
			without the batch reserving more than it uses.
		*/
		struct GlyphBatch
		{
			/** @brief Texture of the font */
			const Texture* FontTexture = nullptr;
			/** @brief Whether the glyphs are drawn with the colorization shader */
			bool Colorized = false;
			/** @brief Whether the glyphs are added to what's behind them, see @ref AdditiveText */
			bool Additive = false;
			/** @brief Layer of the glyphs */
			std::uint16_t Layer = 0;
			/** @brief Whether the record is in use in this frame */
			bool Active = false;
			/** @brief Whether the glyphs are batched, otherwise each one is drawn by a command of its own */
			bool Direct = true;
			/** @brief Glyphs drawn through the record in this frame */
			std::uint32_t Count = 0;
			/** @brief Glyphs drawn through the record in the previous frame */
			std::uint32_t LastCount = 0;
			/** @brief Pooled command that carries the material of the batches, it is never queued itself */
			RenderCommand* StandIn = nullptr;
			/** @brief Batch the glyphs are written into */
			RenderBatcher::DirectBatch Batch;
			/** @brief Commands that draw the batches, kept from frame to frame with their material set up */
			SmallVector<std::unique_ptr<RenderCommand>, 0> BatchCommands;
			/** @brief Number of the batch commands in use in this frame */
			std::uint32_t BatchCommandsUsed = 0;
			/** @brief Depth of the layer, the z translation of every glyph */
			float Depth = 0.0f;
			/** @brief Offsets of the instance block members in an instance */
			std::uint32_t ModelMatrixOffset = 0, ColorOffset = 0, TexRectOffset = 0, SpriteSizeOffset = 0;
			/** @brief Offset of the palette offset member, or -1 if the shader has none */
			std::int32_t PaletteOffsetOffset = -1;
#if defined(CANVAS_RETAINED_DRAWS)
			/** @brief Batch of a record with its instances in memory kept from frame to frame, see @ref CANVAS_RETAINED_DRAWS */
			struct RetainedChunk
			{
				std::unique_ptr<RenderCommand> Command;
				std::unique_ptr<std::uint8_t[]> Memory;
				RHI::UniformBlockCache* InstancesBlock = nullptr;
				std::uint8_t* Instances = nullptr;
				std::uint32_t Capacity = 0;
			};

			/** @brief Batches of the record in the order they're filled, @ref Batch is the last one queued in this frame */
			SmallVector<RetainedChunk, 0> Chunks;
			/** @brief Number of the chunks queued in this frame */
			std::uint32_t ChunksQueued = 0;
			/** @brief Number that tells the record and its chunks from any other a string was drawn into */
			std::uint32_t Serial = 0;
			/** @brief Whether the record was set up in an earlier frame and needs no stand-in to go on */
			bool Retained = false;
			/** @brief Visit order of the chunks in this frame */
			std::uint16_t VisitOrder = 0;
			/** @brief Size of an instance in the chunks */
			std::uint32_t Stride = 0;
			/** @brief Shader the glyphs are colorized with, or `nullptr` */
			Shader* ColorizeShader = nullptr;
			/** @brief Clip planes @ref Depth was calculated for */
			float NearClip = 0.0f, FarClip = 0.0f;
#endif
		};

#if defined(CANVAS_RETAINED_DRAWS)
		/** @brief What a rented command was set up with by @ref DrawTexture(), every argument and everything of the canvas that goes into it */
		struct TextureMemo
		{
			const Texture* Source;
			const Texture* Palette;
			const Shader* CustomShader;
			float Values[26];
			std::int32_t PaletteOffset;
			std::uint16_t Layer;
			std::uint8_t Blending;
			/** @brief Frame the command was last queued in with these, 0 if never */
			std::uint32_t Frame = 0;
		};

		/** @brief What a string was drawn with by @ref Font::DrawString() and where its glyphs went */
		struct StringMemo
		{
			static constexpr std::uint32_t MaxLength = 48;

			struct Arguments
			{
				float Values[23];
				std::int32_t CharOffset;
				std::uint32_t FontSerial;
				std::uint16_t Layer;
				std::uint8_t Align;
				std::uint8_t Additive;
				std::uint8_t Length;
				char Text[MaxLength];
			};

			/** @brief Glyphs of the string in one record: a run of them from @ref First on */
			struct Part
			{
				std::int32_t Record = -1;
				std::uint32_t Serial = 0;
				std::uint32_t First = 0;
				std::uint32_t Count = 0;
			};

			Arguments Drawn;
			/** @brief By shader (plain, colorized) and by the parity of the glyph layer */
			Part Parts[4];
			/** @brief Character offset after the string */
			std::int32_t CharOffsetAfter = 0;
			/** @brief Frame the string was last drawn in with these, 0 if never or if it can't be repeated */
			std::uint32_t Frame = 0;
		};
#endif

		/**
			@brief Rectangles of one layer drawn one after another, written straight into batched commands

			See @ref DrawTextureBatched(). A record is kept from frame to frame with the commands of its batches. A run
			is the rectangles drawn in a row on the layer, each run gets a visit order of its own, so what was drawn on
			the layer between two runs stays between them.
		*/
		struct QuadBatch
		{
			/** @brief Layer of the rectangles */
			std::uint16_t Layer = 0;
			/** @brief Texture of the current run */
			const Texture* QuadTexture = nullptr;
			/** @brief Whether a run is open, the next rectangle on the layer joins it */
			bool Open = false;
			/** @brief Whether the rectangles can be batched in this frame, otherwise each one is drawn by a command of its own */
			bool Direct = true;
			/** @brief Pooled command that carries the material of the batches, it is never queued itself */
			RenderCommand* StandIn = nullptr;
			/** @brief Batch the rectangles of the run are written into */
			RenderBatcher::DirectBatch Batch;
			/** @brief Room asked for when the batch of the run is begun, it grows with every batch the run fills */
			std::uint32_t RunCapacity = 0;
			/** @brief Commands that draw the batches, kept from frame to frame with their material set up */
			SmallVector<std::unique_ptr<RenderCommand>, 0> BatchCommands;
			/** @brief Number of the batch commands in use in this frame */
			std::uint32_t BatchCommandsUsed = 0;
			/** @brief Depth of the layer, the z translation of every rectangle */
			float Depth = 0.0f;
			/** @brief Offsets of the instance block members in an instance */
			std::uint32_t ModelMatrixOffset = 0, ColorOffset = 0, TexRectOffset = 0, SpriteSizeOffset = 0;
			/** @brief Offset of the palette offset member, or -1 if the shader has none */
			std::int32_t PaletteOffsetOffset = -1;
		};

		std::int32_t _renderCommandsCount;
		// Visit order of the next rented command while OrderedDraws is set
		std::uint16_t _nextVisitOrder;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		RenderQueue* _currentRenderQueue;
		SmallVector<GlyphBatch, 0> _glyphBatches;
		SmallVector<QuadBatch, 0> _quadBatches;
		// Record that took the last rectangle, it most likely takes the next one too, and how many runs are open
		std::int32_t _lastQuadBatch = -1;
		std::int32_t _openQuadRuns = 0;
#if defined(CANVAS_RETAINED_DRAWS)
		// Number of the frame being drawn, a memo is good for the frame right after the one it was used in
		std::uint32_t _retainedFrame = 0;
		std::uint32_t _lastGlyphBatchSerial = 0;
		// By the index of the rented command
		SmallVector<TextureMemo, 0> _textureMemos;
		// By the order the strings are drawn in
		SmallVector<StringMemo, 0> _stringMemos;
		std::uint32_t _stringMemosCount = 0;

		/** @brief Queues the next chunk of a record and makes it the batch its glyphs are written into, returns `false` if they can't be batched */
		bool NextRetainedChunk(GlyphBatch& glyphBatch);
		/**
		 * @brief Returns `true` if the next string was drawn with the same arguments in the previous frame
		 *
		 * Its glyphs are counted in as drawn then and @p charOffset is moved past the string. Otherwise
		 * @p memo is to be filled in while the string is drawn.
		 */
		bool RepeatString(const StringMemo::Arguments& arguments, std::int32_t& charOffset, StringMemo*& memo);
#endif

		/** @brief Rents a render command and gives it the material of a glyph of @p fontTexture */
		RenderCommand* RentGlyphCommand(const Texture& fontTexture, Shader* colorizeShader);
		/** @brief Returns the index of the glyph batch record for glyphs of @p fontTexture on @p layer, preparing it for this frame */
		std::int32_t GetGlyphBatch(const Texture& fontTexture, Shader* colorizeShader, std::uint16_t layer);
		/** @brief Returns the next command of @p glyphBatch to draw a batch with, creating it if needed */
		RenderCommand* NextGlyphBatchCommand(GlyphBatch& glyphBatch);
		/** @brief Finishes the batches of the records in use for the glyphs written so far */
		void FinishGlyphBatches();
		/** @brief The part of @ref ClipQuad() that runs only with a clip rectangle set */
		bool ClipQuadToRect(Vector2f& pos, Vector2f& size, Vector4f* texCoords) const;
		/** @brief Returns the visit order of the next command while @ref OrderedDraws is set, 0 otherwise */
		std::uint16_t TakeVisitOrder();
		/** @brief Draws a rectangle already moved by the layer transform and clipped, by a command of its own */
		void DrawTransformedTexture(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords,
			const Colorf& color, BlendMode blending, float angle, std::int32_t paletteOffset, Shader* shader = nullptr);
		/** @brief Returns the record of a layer with a run open for a texture, opening one, or `nullptr` if rectangles can't be batched */
		QuadBatch* GetQuadRun(const Texture& texture, std::uint16_t layer);
		/** @brief Begins the next batch of the run of a record, returns `false` if it can't be batched */
		bool BeginQuadBatch(QuadBatch& record);
		/** @brief Ends the run of rectangles open on a layer, something else is drawn there */
		void EndQuadRun(std::uint16_t layer) {
			if (_openQuadRuns > 0) {
				EndQuadRunOnLayer(layer);
			}
		}
		/** @brief The part of @ref EndQuadRun() that runs only with a run open */
		void EndQuadRunOnLayer(std::uint16_t layer);
	};
}