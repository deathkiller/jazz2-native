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
		Canvas();

		/** @brief View size of the canvas */
		Vector2i ViewSize;
		/** @brief Animation time of the canvas */
		float AnimTime;

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		/** @brief Draws a textured rectangle */
		void DrawTexture(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color, bool additiveBlending = false, float angle = 0.0f);
		/** @brief Draws a solid rectangle */
		void DrawSolid(Vector2f pos, std::uint16_t z, Vector2f size, const Colorf& color, bool additiveBlending = false);
		
		/** @brief Applies alignment settings to a given position vector */
		static Vector2f ApplyAlignment(Alignment align, Vector2f vec, Vector2f size);

		/** @brief Rents a render command for rendering on the canvas */
		RenderCommand* RentRenderCommand();
		/** @brief Draws a raw render command */
		void DrawRenderCommand(RenderCommand* command);

	private:
		std::int32_t _renderCommandsCount;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		RenderQueue* _currentRenderQueue;
	};
}