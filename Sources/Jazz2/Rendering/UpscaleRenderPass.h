﻿#pragma once

#include "../../Main.h"

#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/SceneNode.h"
#include "../../nCine/Graphics/Camera.h"
#include "../../nCine/Graphics/Viewport.h"

using namespace nCine;

namespace Jazz2::Rendering
{
	/** @brief Upscales input image usually to a native resolution */
	class UpscaleRenderPass : public SceneNode
	{
	public:
		UpscaleRenderPass()
			: _resizeShader(nullptr)
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
			return _view->GetSize();
		}

		Vector2f GetTargetSize() const {
			return (_antialiasing._target != nullptr ? _antialiasing._targetSize : _targetSize);
		}

	protected:
		/** @brief Optional antialiasing subpass */
		class AntialiasingSubpass : public SceneNode
		{
			friend class UpscaleRenderPass;

		public:
			AntialiasingSubpass();

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

	/** @brief Upscales input image usually to a native resolution, additionaly supports 3 independent layers (background layer, clipped main layer, overlay layer) */
	class UpscaleRenderPassWithClipping : public UpscaleRenderPass
	{
	public:
		UpscaleRenderPassWithClipping();

		void Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight) override;
		void Register() override;

		SceneNode* GetClippedNode() const {
			return _clippedNode.get();
		}

		SceneNode* GetOverlayNode() const {
			return _overlayNode.get();
		}

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