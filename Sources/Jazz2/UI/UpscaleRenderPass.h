#pragma once

#include "../ContentResolver.h"

#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/SceneNode.h"
#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Viewport.h"

using namespace nCine;

namespace Jazz2::UI
{
	class UpscaleRenderPass : public SceneNode
	{
	public:
		UpscaleRenderPass()
			:
			_resizeShader(nullptr)
		{
			setVisitOrderState(SceneNode::VisitOrderState::Disabled);
		}

		virtual void Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight);
		virtual void Register();

		bool OnDraw(RenderQueue& renderQueue) override;

		SceneNode* GetNode() const {
			return _node.get();
		}

		Vector2i GetViewSize() const {
			return _view->size();
		}

		Vector2f GetTargetSize() const {
			return (_antialiasing._target != nullptr ? _antialiasing._targetSize : _targetSize);
		}

	protected:
		class AntialiasingSubpass : public SceneNode
		{
			friend class UpscaleRenderPass;

		public:
			AntialiasingSubpass()
			{
				setVisitOrderState(SceneNode::VisitOrderState::Disabled);
			}

			void Register();

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			std::unique_ptr<Texture> _target;
			std::unique_ptr<Viewport> _view;
			std::unique_ptr<Camera> _camera;
			RenderCommand _renderCommand;
			Vector2f _targetSize;
		};

		std::unique_ptr<Viewport> _view;
		std::unique_ptr<Camera> _camera;
		std::unique_ptr<Texture> _target;
		Vector2f _targetSize;
		AntialiasingSubpass _antialiasing;

	private:
		std::unique_ptr<SceneNode> _node;
#if !defined(DISABLE_RESCALE_SHADERS)
		Shader* _resizeShader;
#endif
		RenderCommand _renderCommand;
	};

	class UpscaleRenderPassWithClipping : public UpscaleRenderPass
	{
	public:
		UpscaleRenderPassWithClipping() : UpscaleRenderPass()
		{
		}

		void Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight) override;
		void Register() override;

		SceneNode* GetClippedNode() const {
			return _clippedNode.get();
		}

		SceneNode* GetOverlayNode() const {
			return _overlayNode.get();
		}

		void SetClipRectangle(const Recti& scissorRect) {
			_clippedView->setScissorRect(scissorRect);
		}

	private:
		std::unique_ptr<Viewport> _clippedView;
		std::unique_ptr<Viewport> _overlayView;
		std::unique_ptr<SceneNode> _clippedNode;
		std::unique_ptr<SceneNode> _overlayNode;
	};
}