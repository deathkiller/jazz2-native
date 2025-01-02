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

		Vector2i ViewSize;
		float AnimTime;

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		void DrawTexture(const Texture& texture, Vector2f pos, std::uint16_t z, Vector2f size, const Vector4f& texCoords, const Colorf& color, bool additiveBlending = false, float angle = 0.0f);
		void DrawSolid(Vector2f pos, std::uint16_t z, Vector2f size, const Colorf& color, bool additiveBlending = false);
		static Vector2f ApplyAlignment(Alignment align, Vector2f vec, Vector2f size);

		RenderCommand* RentRenderCommand();
		void DrawRenderCommand(RenderCommand* command);

	private:
		std::int32_t _renderCommandsCount;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		RenderQueue* _currentRenderQueue;
	};
}