#pragma once

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "Canvas.h"
#include "../ContentResolver.h"
#include "../Rendering/UpscaleRenderPass.h"
#include "../../nCine/Base/TimeStamp.h"

namespace Jazz2::UI
{
	/**
		@brief Handler that only shows the loading indicator

		State handler for the loading screen shown while content is being prepared, drawing an animated loading
		indicator over a light or dark background. An optional callback is invoked once loading completes.

		A conversion of the original game files that is still running is shown as a progress bar as well, because
		on a slow device it can take long enough that an indicator alone says too little --- see
		@relativeref{Jazz2,IRootController::GetInitializationProgress()}.

		Loading that is still going after a minute is treated as a long wait: a light background turns dark and an
		aura of slowly drifting color blobs fades in behind the indicator. The aura is made only of textured quads
		with alpha blending, so it renders the same on every backend, the fixed-function ones included.
	*/
	class LoadingHandler : public IStateHandler
	{
	public:
		/** @{ @name Constants */

		/** @brief Default width of viewport (see @ref Rendering::UpscaleRenderPass::DefaultViewWidth) */
		static constexpr std::int32_t DefaultWidth = Rendering::UpscaleRenderPass::DefaultViewWidth;
		/** @brief Default height of viewport */
		static constexpr std::int32_t DefaultHeight = Rendering::UpscaleRenderPass::DefaultViewHeight;

		/** @} */

		/**
		 * @brief Creates a new instance
		 *
		 * @param root      Root controller
		 * @param darkMode  Whether to start with the dark theme, the light one turns dark during a long wait
		 */
		LoadingHandler(IRootController* root, bool darkMode);
		/**
		 * @brief Creates a new instance with a completion callback
		 *
		 * @param root      Root controller
		 * @param darkMode  Whether to start with the dark theme, the light one turns dark during a long wait
		 * @param callback  Called when the loading finishes
		 */
		LoadingHandler(IRootController* root, bool darkMode, Function<bool(IRootController*)>&& callback);
		~LoadingHandler() override;

		Vector2i GetViewSize() const override;

		void OnBeginFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;

	private:
		IRootController* _root;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class BackgroundCanvas : public Canvas
		{
		public:
			BackgroundCanvas(LoadingHandler* owner) : _owner(owner) {}

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			LoadingHandler* _owner;

			void DrawAura();
		};
#endif

		Rendering::UpscaleRenderPass _upscalePass;
		// Declared before the canvas so it is destroyed after it, the pooled commands of the canvas keep pointing to it
		std::unique_ptr<Texture> _auraTexture;
		std::unique_ptr<BackgroundCanvas> _canvasBackground;
		Metadata* _metadata;
		Function<bool(IRootController*)> _callback;
		// The wall clock rather than the frame time, which is capped, because a device slow enough to load for
		// minutes is also likely to render slowly
		TimeStamp _startTime;
		float _time;
		float _transition;
		// Eased towards the last reported conversion progress, and negative until the first one arrives, which
		// is also how "there is no progress bar to draw" is carried
		float _progress;
		float _progressTransition;
		// 0 for the light theme and 1 for the dark one, anything in between while a long wait turns it dark
		float _darkness;
		// Opacity of the aura, which stays 0 until the loading turns into a long wait
		float _aura;
	};
}