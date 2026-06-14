#pragma once

#include "../../Main.h"

#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/SceneNode.h"
#include "../../nCine/Graphics/Texture.h"
#include "../../nCine/Graphics/Viewport.h"

#include <memory>

using namespace nCine;

namespace Jazz2::Rendering
{
	class PlayerViewport;
	
	/**
		@brief Applies blur to a scene
		
		Post-processing pass that renders a source texture through a downsample or directional Gaussian blur
		shader into its own off-screen target. Several instances are chained per @ref PlayerViewport to build
		the half- and quarter-resolution blur buffers used for bloom and underwater effects.
	*/
	class BlurRenderPass : public SceneNode
	{
	public:
		/** @brief Creates a new instance attached to a given viewport */
		BlurRenderPass(PlayerViewport* owner)
			: _owner(owner)
		{
			setVisitOrderState(SceneNode::VisitOrderState::Disabled);
		}

		/**
		 * @brief Initializes the render pass
		 *
		 * @param source     Source texture to blur
		 * @param width      Width of the target texture
		 * @param height     Height of the target texture
		 * @param direction  Blur direction (zero vector performs only downsampling)
		 */
		void Initialize(Texture* source, std::int32_t width, std::int32_t height, Vector2f direction);
		/** @brief Registers the render pass into the viewport chain */
		void Register();
		/** @brief Releases all allocated resources */
		void Dispose();

		bool OnDraw(RenderQueue& renderQueue) override;

		/** @brief Returns the resulting target texture */
		Texture* GetTarget() const {
			return _target.get();
		}

	private:
		PlayerViewport* _owner;
		std::unique_ptr<Texture> _target;
		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Camera> _camera;
		RenderCommand _renderCommand;

		Texture* _source;
		bool _downsampleOnly;
		Vector2f _direction;
	};
}