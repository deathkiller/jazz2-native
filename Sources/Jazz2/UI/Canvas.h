#pragma once

#include "Alignment.h"
#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/SceneNode.h"

using namespace nCine;

namespace Jazz2::UI
{
	/** @brief Canvas */
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

	protected:
		/** @brief Multiplier of game time for canvas rendering */
		static constexpr float AnimTimeMultiplier = 0.014f;

	private:
		std::int32_t _renderCommandsCount;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		RenderQueue* _currentRenderQueue;
	};
}