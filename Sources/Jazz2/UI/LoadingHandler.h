#pragma once

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "Canvas.h"
#include "../ContentResolver.h"
#include "../Rendering/UpscaleRenderPass.h"

namespace Jazz2::UI
{
	/** @brief Handler that only shows the loading indicator */
	class LoadingHandler : public IStateHandler
	{
	public:
		/** @{ @name Constants */

		/** @brief Default width of viewport */
		static constexpr std::int32_t DefaultWidth = 720;
		/** @brief Default height of viewport */
		static constexpr std::int32_t DefaultHeight = 405;

		/** @} */

		LoadingHandler(IRootController* root, bool darkMode);
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
			BackgroundCanvas(LoadingHandler* owner) : _owner(owner) { }

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			LoadingHandler* _owner;
		};
#endif

		Rendering::UpscaleRenderPass _upscalePass;
		std::unique_ptr<BackgroundCanvas> _canvasBackground;
		Metadata* _metadata;
		Function<bool(IRootController*)> _callback;
		float _transition;
		bool _darkMode;
	};
}