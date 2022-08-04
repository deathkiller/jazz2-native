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

	protected:
		void DrawTexture(const Texture& texture, const Vector2f& pos, uint16_t z, const Vector2f& size, const Vector4f& texCoords, const Colorf& color);
		static Vector2f ApplyAlignment(Alignment align, const Vector2f& vec, const Vector2f& size);
		RenderCommand* RentRenderCommand();

	private:
		SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
		int _renderCommandsCount;
		RenderQueue* _currentRenderQueue;
	};
}