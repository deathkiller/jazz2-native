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
		/** @{ @name Constants */

		/**
		 * @brief Upper bound of the logical view, which every handler's own `DefaultWidth`/`DefaultHeight` is
		 *
		 * 720x405 is a 16:9 box, which is the shape of every display the game runs on except the televisions.
		 * Those are 4:3, and there the bound is what decides whether the view is rendered at something close
		 * to the panel's own resolution or at something much smaller that the hardware then stretches: a 4:3
		 * display fits a 405-line view as 540x405, which a 480-line framebuffer then upscales by a sixth.
		 * 640x480 lets the fit reach the panel instead - 640x480 on an NTSC Dreamcast, Wii and GameCube, and
		 * 597x448 on the PlayStation 2, whose 448 lines cap the height - so on those modes the vertical
		 * mapping is 1:1 and the sprites are drawn at the size they are shown at.
		 *
		 * "On those modes" is the caveat: the numbers here are the NTSC ones. A console running a 50 Hz PAL
		 * mode renders taller - libogc's `TVPal528IntDf` is 640x528 - and the fit still returns 640x480
		 * there, because the view's width is bounded by the framebuffer's 640 columns and its height then
		 * follows from the 4:3 display aspect. That leaves a 1.1x vertical upscale on the way out. It is a
		 * deliberate trade rather than an oversight: the alternative is a 704x528 view, which would map 1:1
		 * vertically but has to be squeezed horizontally into the same 640 columns, costing more fill and a
		 * softer picture for the same geometry. Shapes are correct either way - that is what
		 * @ref IGfxDevice::displayAspect() guarantees - and only the resampling differs.
		 *
		 * It costs fill rate in proportion: about 1.2x the pixels on the PlayStation 2 and 1.4x on the other
		 * three, which @ref PreferencesCache::RenderingResolutionPercent scales back down for anyone who
		 * would rather have the frame rate.
		 *
		 * Only the 4:3 consoles are listed. A widescreen Wii is unaffected either way (its view is bounded by
		 * the framebuffer's 640 columns, not by this, and comes out 640x360 whichever bound is in force), and
		 * every remaining console already has a panel shorter than 405 lines - the Nintendo 64, the 3DS and
		 * the PSP all render 1:1 as they are.
		 */
#if defined(DEATH_TARGET_PS2) || defined(DEATH_TARGET_DREAMCAST) || defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE)
		static constexpr std::int32_t DefaultViewWidth = 640;
		static constexpr std::int32_t DefaultViewHeight = 480;
#else
		static constexpr std::int32_t DefaultViewWidth = 720;
		static constexpr std::int32_t DefaultViewHeight = 405;
#endif

		/**
		 * @brief Height of the 16:9 view every screen in the game was laid out against
		 *
		 * The same as @ref DefaultViewHeight everywhere except the 4:3 televisions, which render taller than
		 * that - so anything drawn at a fixed pixel size (rather than as a fraction of the view) leaves the
		 * difference as bare background there. A layout that has to make up that difference measures it
		 * against this, not against the bound, which on those consoles is the taller number.
		 */
		static constexpr std::int32_t ReferenceViewHeight = 405;

		/** @} */

		/** @brief Creates a new instance */
		UpscaleRenderPass()
			: _supersample(1), _resizeAtLogicalScale(false)	
#if !defined(DISABLE_RESCALE_SHADERS)
				, _resizeShader(nullptr)
#endif
		{
			setVisitOrderState(VisitOrderState::Disabled);
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

		/**
		 * @brief Returns the logical view size for a target (drawable) of the given size
		 *
		 * The default size is the upper bound of the view - a smaller target gets a view of its own size, a
		 * larger one is aspect-fitted into the bound. The bound is scaled by
		 * @ref PreferencesCache::RenderingResolutionPercent, which is what makes the preference the most
		 * the scene (and everything drawn in the same coordinate space) is rendered at. Every handler that
		 * owns an upscale pass lays itself out through this, so the menu's view is the same size the level's
		 * will be, and the lighting buffer (a fraction of the viewport, see @ref PlayerViewport) follows it.
		 */
		static Vector2i CalculateViewSize(std::int32_t targetWidth, std::int32_t targetHeight, std::int32_t defaultWidth, std::int32_t defaultHeight);

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

		/**
		 * @brief Returns the render-resolution multiplier the pass actually applied (1 if not supersampled)
		 *
		 * Not necessarily the value requested in @ref Initialize() - supersampling is refused on the direct
		 * tier (no framebuffers) and on Vita, so callers that adapt to it must ask the pass, not assume.
		 */
		std::int32_t GetSupersample() const {
			return _supersample;
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