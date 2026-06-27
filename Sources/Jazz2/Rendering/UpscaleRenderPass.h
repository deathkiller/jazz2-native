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
			: _supersample(1), _resizeAtLogicalScale(false), _resizeShader(nullptr)
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
		 * @param supersample   Render-resolution multiplier for the input image (1 = native); the scene is still drawn
		 *                      in `width`×`height` coordinates, but rasterized into a `supersample`× larger texture
		 * @param overlay       If `true`, renders an RGBA layer at native resolution (transparent where nothing is
		 *                      drawn) and composites it alpha-blended on top of everything else; used for the HUD so
		 *                      it stays crisp regardless of the scene's supersampling
		 */
		virtual void Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight, std::int32_t supersample = 1, bool overlay = false);
		/** @brief Registers the render pass into the viewport chain */
		virtual void Register();

		bool OnDraw(RenderQueue& renderQueue) override;

		/** @brief Returns the input scene node */
		SceneNode* GetNode() const {
			return _node.get();
		}

		/** @brief Returns size of the input image (the logical coordinate space, not the supersampled texture) */
		Vector2i GetViewSize() const {
			return _logicalSize;
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
			Camera _camera;
			RenderCommand _renderCommand;
			Vector2f _targetSize;
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Members shared with UpscaleRenderPassWithClipping (its clipped/overlay views reuse the target and camera)
		std::unique_ptr<Texture> _target;	// The (possibly supersampled) render target the input scene is drawn into
		std::unique_ptr<Viewport> _view;	// Renders the input scene node into the target
		Camera _camera;						// View/projection shared by the target view (and the clipping variant's views)
		AntialiasingSubpass _antialiasing;	// Optional resolve pass that smooths the final upscale (see Initialize)
		std::int32_t _supersample;			// Render-resolution multiplier of the target texture over the logical size
#endif

	private:
		bool _resizeAtLogicalScale;			// Edge-detection rescale shaders (HQ2x/3xBRZ/SABR) sample at the logical, not supersampled, scale
		std::unique_ptr<SceneNode> _node;	// Input scene root that the rest of the rendering attaches to (GetNode())
		Vector2i _logicalSize;				// Logical coordinate space the scene is drawn in (GetViewSize())
		Vector2f _targetSize;				// Size this pass renders to: the window, or the antialiasing intermediate when active
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

		void Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight, std::int32_t supersample = 1, bool overlay = false) override;
		void Register() override;

		/** @brief Returns the clipped main layer node */
		SceneNode* GetClippedNode() const {
			return _clippedNode.get();
		}

		/** @brief Returns the overlay layer node */
		SceneNode* GetOverlayNode() const {
			return _overlayNode.get();
		}

		/** @brief Sets the clipping rectangle of the main layer (given in logical coordinates) */
		void SetClipRectangle(const Recti& scissorRect) {
			if (_supersample > 1) {
				// The scissor rectangle is supplied in logical coordinates, but it is applied to the supersampled
				// target texture, so it must be scaled up to match
				_clippedView->SetScissorRect(Recti(scissorRect.X * _supersample, scissorRect.Y * _supersample,
					scissorRect.W * _supersample, scissorRect.H * _supersample));
			} else {
				_clippedView->SetScissorRect(scissorRect);
			}
		}

	private:
		std::unique_ptr<Viewport> _clippedView;
		std::unique_ptr<Viewport> _overlayView;
		std::unique_ptr<SceneNode> _clippedNode;
		std::unique_ptr<SceneNode> _overlayNode;
	};
}