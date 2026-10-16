#pragma once

#include "Alignment.h"
#include "../../nCine/Graphics/RenderBatcher.h"
#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/SceneNode.h"
#include "../../nCine/Primitives/Rect.h"

using namespace nCine;

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
		 * default, as nothing but the level editor needs it.
		 */
		bool OrderedDraws = false;

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		/**
		 * @brief Draws a textured rectangle
		 *
		 * `paletteOffset` >= 0 marks the texture as indexed (palette index in the red channel), recolored at draw time
		 * through the shared palette texture at that offset; -1 = plain RGBA.
		 */
		void DrawTexture(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color, bool additiveBlending = false, float angle = 0.0f, std::int32_t paletteOffset = -1);
		/**
		 * @brief Draws an indexed textured rectangle recolored through a palette texture (PaletteRemap shader)
		 *
		 * The palette offset is the flat index of the first color in the palette texture (0 = the first row/palette).
		 */
		void DrawTextureWithPalette(const Texture& texture, const Texture& palette, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color, float paletteOffset = 0.0f);
		/** @brief Draws a solid rectangle */
		void DrawSolid(Vector2f pos, std::uint16_t z, Vector2f size, const Colorf& color, bool additiveBlending = false);
		
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
		bool ClipQuad(Vector2f& pos, Vector2f& size, Vector4f* texCoords) const;

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
		};

		std::int32_t _renderCommandsCount;
		// Visit order of the next rented command while OrderedDraws is set
		std::uint16_t _nextVisitOrder;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		RenderQueue* _currentRenderQueue;
		SmallVector<GlyphBatch, 0> _glyphBatches;

		/** @brief Rents a render command and gives it the material of a glyph of @p fontTexture */
		RenderCommand* RentGlyphCommand(const Texture& fontTexture, Shader* colorizeShader);
		/** @brief Returns the index of the glyph batch record for glyphs of @p fontTexture on @p layer, preparing it for this frame */
		std::int32_t GetGlyphBatch(const Texture& fontTexture, Shader* colorizeShader, std::uint16_t layer);
		/** @brief Returns the next command of @p glyphBatch to draw a batch with, creating it if needed */
		RenderCommand* NextGlyphBatchCommand(GlyphBatch& glyphBatch);
		/** @brief Finishes the batches of the records in use for the glyphs written so far */
		void FinishGlyphBatches();
	};
}