#include "UpscaleRenderPass.h"
#include "../PreferencesCache.h"

#include "../../nCine/Application.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/Viewport.h"

namespace Jazz2::UI
{
	void UpscaleRenderPass::Initialize(int32_t width, int32_t height, int32_t targetWidth, int32_t targetHeight)
	{
		_targetSize = Vector2f((float)targetWidth, (float)targetHeight);

		if ((PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) == RescaleMode::UseAntialiasing) {
			float widthRatio, heightRatio;
			float widthFrac = modff((float)targetWidth / width, &widthRatio);
			float heightFrac = modff((float)targetHeight / height, &heightRatio);

			int32_t requiredWidth = (int32_t)(widthFrac > 0.004f ? (width * (widthRatio + 1)) : targetWidth);
			int32_t requiredHeight = (int32_t)(heightFrac > 0.004f ? (height * (heightRatio + 1)) : targetHeight);

			if (std::abs(requiredWidth - targetWidth) > 2 || std::abs(requiredHeight - targetHeight) > 2) {
				if (_antialiasing._target == nullptr) {
					_antialiasing._target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, requiredWidth, requiredHeight);
				} else {
					_antialiasing._target->init(nullptr, Texture::Format::RGB8, requiredWidth, requiredHeight);
				}
				_antialiasing._target->setMinFiltering(SamplerFilter::Linear);
				_antialiasing._target->setMagFiltering(SamplerFilter::Linear);
				_antialiasing._target->setWrap(SamplerWrapping::ClampToEdge);

				_antialiasing._targetSize = _targetSize;
				_targetSize = Vector2f((float)requiredWidth, (float)requiredHeight);
			} else {
				_antialiasing._target = nullptr;
			}
		} else {
			_antialiasing._target = nullptr;
		}

		if (_camera == nullptr) {
			_camera = std::make_unique<Camera>();
		}
		_camera->setOrthoProjection(0.0f, width, height, 0.0f);
		_camera->setView(0, 0, 0, 1);

		if (_view == nullptr) {
			_node = std::make_unique<SceneNode>();
			_node->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->setRootNode(_node.get());
			_view->setCamera(_camera.get());
			_view->setClearMode(Viewport::ClearMode::Never);
		} else {
			_view->removeAllTextures();
			_target->init(nullptr, Texture::Format::RGB8, width, height);
			_view->setTexture(_target.get());
		}
		_target->setMinFiltering(SamplerFilter::Nearest);
		_target->setMagFiltering(SamplerFilter::Nearest);
		_target->setWrap(SamplerWrapping::ClampToEdge);

		if (_antialiasing._target != nullptr) {
			if (_antialiasing._camera == nullptr) {
				_antialiasing._camera = std::make_unique<Camera>();
			}
			_antialiasing._camera->setOrthoProjection(0.0f, _targetSize.X, _targetSize.Y, 0.0f);
			_antialiasing._camera->setView(0, 0, 0, 1);

			_antialiasing._view = std::make_unique<Viewport>(_antialiasing._target.get(), Viewport::DepthStencilFormat::None);
			_antialiasing._view->setRootNode(this);
			_antialiasing._view->setCamera(_antialiasing._camera.get());
			//_antialiasing._view->setClearMode(Viewport::ClearMode::Never);

			SceneNode& rootNode = theApplication().rootNode();
			_antialiasing.setParent(&rootNode);
			setParent(nullptr);

			if (_antialiasing._renderCommand.material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::Antialiasing))) {
				_antialiasing._renderCommand.material().reserveUniformsDataMemory();
				_antialiasing._renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
				// Required to reset render command properly
				_antialiasing._renderCommand.setTransformation(_antialiasing._renderCommand.transformation());

				GLUniformCache* textureUniform = _antialiasing._renderCommand.material().uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->intValue(0) != 0) {
					textureUniform->setIntValue(0); // GL_TEXTURE0
				}
			}
		} else {
			_antialiasing._camera = nullptr;
			_antialiasing._view = nullptr;

			SceneNode& rootNode = theApplication().rootNode();
			_antialiasing.setParent(nullptr);
			setParent(&rootNode);
		}

		// Prepare render command
#if !defined(DISABLE_RESCALE_SHADERS)
		switch (PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask) {
			case RescaleMode::HQ2x: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeHQ2x); break;
			case RescaleMode::_3xBrz: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::Resize3xBrz); break;
			case RescaleMode::CrtScanlines: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeCrtScanlines); break;
			case RescaleMode::CrtShadowMask: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeCrtShadowMask); break;
			case RescaleMode::CrtApertureGrille: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeCrtApertureGrille); break;
			case RescaleMode::Monochrome: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeMonochrome); break;
			default: _resizeShader = nullptr; break;
		}

		bool shaderChanged = (_resizeShader != nullptr
			? _renderCommand.material().setShader(_resizeShader)
			: _renderCommand.material().setShaderProgramType(Material::ShaderProgramType::SPRITE));
#else
		bool shaderChanged = _renderCommand.material().setShaderProgramType(Material::ShaderProgramType::SPRITE);
#endif
		if (shaderChanged) {
			_renderCommand.material().reserveUniformsDataMemory();
			_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			_renderCommand.setTransformation(_renderCommand.transformation());

			GLUniformCache* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
		}
	}

	void UpscaleRenderPass::Register()
	{
		_antialiasing.Register();

		Viewport::chain().push_back(_view.get());
	}

	bool UpscaleRenderPass::OnDraw(RenderQueue& renderQueue)
	{
		auto instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
#if !defined(DISABLE_RESCALE_SHADERS)
		if (_resizeShader != nullptr) {
			// TexRectUniformName is reused for input texture size
			Vector2i size = _target->size();
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue((float)size.X, (float)size.Y, 0.0f, 0.0f);
		} else
#endif
		{
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		}

		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(_targetSize.Data());
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		_renderCommand.material().setTexture(0, *_target);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}

	void UpscaleRenderPass::AntialiasingSubpass::Register()
	{
		if (_view != nullptr) {
			Viewport::chain().push_back(_view.get());
		}
	}

	bool UpscaleRenderPass::AntialiasingSubpass::OnDraw(RenderQueue& renderQueue)
	{
		Vector2i size = _target->size();
		auto instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue((float)size.X, (float)size.Y, 0.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(_targetSize.Data());
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		_renderCommand.material().setTexture(0, *_target);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}

	void UpscaleRenderPassWithClipping::Initialize(int32_t width, int32_t height, int32_t targetWidth, int32_t targetHeight)
	{
		if (_clippedView != nullptr) {
			_clippedView->removeAllTextures();
		}
		if (_overlayView != nullptr) {
			_overlayView->removeAllTextures();
		}

		UpscaleRenderPass::Initialize(width, height, targetWidth, targetHeight);

		if (_clippedView == nullptr) {
			_clippedNode = std::make_unique<SceneNode>();
			_clippedNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

			_clippedView = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_clippedView->setRootNode(_clippedNode.get());
			_clippedView->setCamera(_camera.get());
			_clippedView->setClearMode(Viewport::ClearMode::Never);
		} else {
			_clippedView->setTexture(_target.get());
		}

		if (_overlayView == nullptr) {
			_overlayNode = std::make_unique<SceneNode>();
			_overlayNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

			_overlayView = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_overlayView->setRootNode(_overlayNode.get());
			_overlayView->setCamera(_camera.get());
			_overlayView->setClearMode(Viewport::ClearMode::Never);
		} else {
			_overlayView->setTexture(_target.get());
		}
	}

	void UpscaleRenderPassWithClipping::Register()
	{
		_antialiasing.Register();

		Viewport::chain().push_back(_overlayView.get());
		Viewport::chain().push_back(_clippedView.get());
		Viewport::chain().push_back(_view.get());
	}
}