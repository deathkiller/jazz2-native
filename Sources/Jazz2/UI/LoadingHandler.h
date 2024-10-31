#pragma once

#include "../IStateHandler.h"
#include "../IRootController.h"
#include "Canvas.h"
#include "UpscaleRenderPass.h"
#include "../ContentResolver.h"

namespace Jazz2::UI
{
	class LoadingHandler : public IStateHandler
	{
	public:
		static constexpr std::int32_t DefaultWidth = 720;
		static constexpr std::int32_t DefaultHeight = 405;

		LoadingHandler(IRootController* root);
		LoadingHandler(IRootController* root, Function<bool(IRootController*)>&& callback);
		~LoadingHandler() override;

		void OnBeginFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;

	private:
		IRootController* _root;

		class BackgroundCanvas : public Canvas
		{
		public:
			BackgroundCanvas(LoadingHandler* owner) : _owner(owner) { }

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			LoadingHandler* _owner;
		};

		UI::UpscaleRenderPass _upscalePass;
		std::unique_ptr<BackgroundCanvas> _canvasBackground;
		Metadata* _metadata;
		Function<bool(IRootController*)> _callback;
	};
}