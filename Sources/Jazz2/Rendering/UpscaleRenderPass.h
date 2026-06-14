#pragma once

#include "../../Main.h"

#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/SceneNode.h"
#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Viewport.h"

using namespace nCine;

namespace Jazz2::Rendering
{
	/**
		@brief Upscales input image usually to a native resolution
		
		Final output pass that renders the low-resolution scene through a rescale shader to the native output
		resolution, optionally routing it through an @ref AntialiasingSubpass. Exposes the input scene node
		that the rest of the rendering attaches to.
	*/
	class UpscaleRenderPass : public SceneNode
	{
	public:
		/** @brief Creates a new instance */
		UpscaleRenderPass()
			: _resizeShader(nullptr)
		{
			setVisitOrderState(SceneNode::VisitOrderState::Disabled);
		}

		/**
		 * @brief Initializes the render pass
		 *
		 * @param width         Width of the input image
		 * @param height        Height of the input image
		 * @param targetWidth   Width of the upscaled target image
		 * @param targetHeight  Height of the upscaled target image
		 */
		virtual void Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight);
		/** @brief Registers the render pass into the viewport chain */
		virtual void Register();

		bool OnDraw(RenderQueue& renderQueue) override;

		/** @brief Returns the input scene node */
		SceneNode* GetNode() const {
			return _node.get();
		}

		/** @brief Returns size of the input image */
		Vector2i GetViewSize() const {
			return _view->GetSize();
		}

		/** @brief Returns size of the upscaled target image */
		Vector2f GetTargetSize() const {
			return (_antialiasing._target != nullptr ? _antialiasing._targetSize : _targetSize);
		}

	protected:
		/**
		 * @brief Optional antialiasing subpass
		 *
		 * Extra pass inserted after upscaling that renders the upscaled target through an antialiasing shader
		 * into its own target, smoothing the final image when enabled.
		 */
		class AntialiasingSubpass : public SceneNode
		{
			friend class UpscaleRenderPass;

		public:
			/** @brief Creates a new instance */
			AntialiasingSubpass();

			/** @brief Registers the subpass into the viewport chain */
			void Register();

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			std::unique_ptr<Texture> _target;
			std::unique_ptr<Viewport> _view;
			std::unique_ptr<Camera> _camera;
			RenderCommand _renderCommand;
			Vector2f _targetSize;
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Camera> _camera;
		std::unique_ptr<Texture> _target;
		Vector2f _targetSize;
		AntialiasingSubpass _antialiasing;
#endif

	private:
		std::unique_ptr<SceneNode> _node;
#if !defined(DISABLE_RESCALE_SHADERS)
		Shader* _resizeShader;
#endif
		RenderCommand _renderCommand;
	};

	/**
		@brief Upscales input image usually to a native resolution, additionaly supports 3 independent layers (background layer, clipped main layer, overlay layer)
		
		Variant of @ref UpscaleRenderPass that upscales three separate scene nodes (background, scissor-clipped
		main layer, and overlay) so the main layer can be clipped to a rectangle independently of the
		surrounding content.
	*/
	class UpscaleRenderPassWithClipping : public UpscaleRenderPass
	{
	public:
		/** @brief Creates a new instance */
		UpscaleRenderPassWithClipping();

		void Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight) override;
		void Register() override;

		/** @brief Returns the clipped main layer node */
		SceneNode* GetClippedNode() const {
			return _clippedNode.get();
		}

		/** @brief Returns the overlay layer node */
		SceneNode* GetOverlayNode() const {
			return _overlayNode.get();
		}

		/** @brief Sets the clipping rectangle of the main layer */
		void SetClipRectangle(const Recti& scissorRect) {
			_clippedView->SetScissorRect(scissorRect);
		}

	private:
		std::unique_ptr<Viewport> _clippedView;
		std::unique_ptr<Viewport> _overlayView;
		std::unique_ptr<SceneNode> _clippedNode;
		std::unique_ptr<SceneNode> _overlayNode;
	};
}