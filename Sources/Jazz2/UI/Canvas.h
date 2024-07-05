#pragma once

#include "Alignment.h"
#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/SceneNode.h"

using namespace nCine;

namespace Jazz2::UI
{
	class Canvas : public SceneNode
	{
		friend class Font;

	public:
		Canvas();

		Vector2i ViewSize;
		float AnimTime;

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;

		void DrawTexture(const Texture& texture, const Vector2f& pos, std::uint16_t z, const Vector2f& size, const Vector4f& texCoords, const Colorf& color, bool additiveBlending = false, float angle = 0.0f);
		void DrawSolid(const Vector2f& pos, std::uint16_t z, const Vector2f& size, const Colorf& color, bool additiveBlending = false);
		static Vector2f ApplyAlignment(Alignment align, const Vector2f& vec, const Vector2f& size);

		RenderCommand* RentRenderCommand();
		void DrawRenderCommand(RenderCommand* command);

	private:
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		std::int32_t _renderCommandsCount;
		RenderQueue* _currentRenderQueue;
	};
}