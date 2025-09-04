#include "UpscaleRenderPass.h"
#include "../ContentResolver.h"
#include "../PreferencesCache.h"

#include "../../nCine/Application.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/Viewport.h"

namespace Jazz2::Rendering
{
	void UpscaleRenderPass::Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight)
	{
		_targetSize = Vector2f((float)targetWidth, (float)targetHeight);

		if ((PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) == RescaleMode::UseAntialiasing) {
			float widthRatio, heightRatio;
			float widthFrac = modff((float)targetWidth / width, &widthRatio);
			float heightFrac = modff((float)targetHeight / height, &heightRatio);

			std::int32_t requiredWidth = (std::int32_t)(widthFrac > 0.004f ? (width * (widthRatio + 1)) : targetWidth);
			std::int32_t requiredHeight = (std::int32_t)(heightFrac > 0.004f ? (height * (heightRatio + 1)) : targetHeight);

			if (std::abs(requiredWidth - targetWidth) > 2 || std::abs(requiredHeight - targetHeight) > 2) {
				if (_antialiasing._target == nullptr) {
					_antialiasing._target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, requiredWidth, requiredHeight);
				} else {
					_antialiasing._target->Init(nullptr, Texture::Format::RGB8, requiredWidth, requiredHeight);
				}
				_antialiasing._target->SetMinFiltering(SamplerFilter::Linear);
				_antialiasing._target->SetMagFiltering(SamplerFilter::Linear);
				_antialiasing._target->SetWrap(SamplerWrapping::ClampToEdge);

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
		_camera->SetOrthoProjection(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);
		_camera->SetView(0, 0, 0, 1);

		if (_view == nullptr) {
			_node = std::make_unique<SceneNode>();
			_node->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->SetRootNode(_node.get());
			_view->SetCamera(_camera.get());
			_view->SetClearMode(Viewport::ClearMode::Never);
		} else {
			_view->RemoveAllTextures();
			_target->Init(nullptr, Texture::Format::RGB8, width, height);
			_view->SetTexture(_target.get());
		}
		_target->SetMinFiltering(SamplerFilter::Nearest);
		_target->SetMagFiltering(SamplerFilter::Nearest);
		_target->SetWrap(SamplerWrapping::ClampToEdge);

		if (_antialiasing._target != nullptr) {
			if (_antialiasing._camera == nullptr) {
				_antialiasing._camera = std::make_unique<Camera>();
			}
			_antialiasing._camera->SetOrthoProjection(0.0f, _targetSize.X, _targetSize.Y, 0.0f);
			_antialiasing._camera->SetView(0, 0, 0, 1);

			_antialiasing._view = std::make_unique<Viewport>(_antialiasing._target.get(), Viewport::DepthStencilFormat::None);
			_antialiasing._view->SetRootNode(this);
			_antialiasing._view->SetCamera(_antialiasing._camera.get());
			//_antialiasing._view->setClearMode(Viewport::ClearMode::Never);

			SceneNode& rootNode = theApplication().GetRootNode();
			_antialiasing.setParent(&rootNode);
			setParent(nullptr);

			if (_antialiasing._renderCommand.material().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::Antialiasing))) {
				_antialiasing._renderCommand.material().ReserveUniformsDataMemory();
				_antialiasing._renderCommand.geometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
				// Required to reset render command properly
				_antialiasing._renderCommand.setTransformation(_antialiasing._renderCommand.transformation());

				auto* textureUniform = _antialiasing._renderCommand.material().Uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0); // GL_TEXTURE0
				}
			}
		} else {
			_antialiasing._camera = nullptr;
			_antialiasing._view = nullptr;

			SceneNode& rootNode = theApplication().GetRootNode();
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
			case RescaleMode::Sabr: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeSabr); break;
			case RescaleMode::CleanEdge: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeCleanEdge); break;
			default: _resizeShader = nullptr; break;
		}

		bool shaderChanged = (_resizeShader != nullptr
			? _renderCommand.material().SetShader(_resizeShader)
			: _renderCommand.material().SetShaderProgramType(Material::ShaderProgramType::Sprite));
#else
		bool shaderChanged = _renderCommand.material().SetShaderProgramType(Material::ShaderProgramType::Sprite);
#endif
		if (shaderChanged) {
			_renderCommand.material().ReserveUniformsDataMemory();
			_renderCommand.geometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			_renderCommand.setTransformation(_renderCommand.transformation());

			auto* textureUniform = _renderCommand.material().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}
	}

	void UpscaleRenderPass::Register()
	{
		_antialiasing.Register();

		Viewport::GetChain().push_back(_view.get());
	}

	bool UpscaleRenderPass::OnDraw(RenderQueue& renderQueue)
	{
		auto instanceBlock = _renderCommand.material().UniformBlock(Material::InstanceBlockName);
#if !defined(DISABLE_RESCALE_SHADERS)
		if (_resizeShader != nullptr) {
			// TexRectUniformName is reused for input texture size
			Vector2i size = _target->GetSize();
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue((float)size.X, (float)size.Y, 0.0f, 0.0f);
		} else
#endif
		{
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		}

		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(_targetSize.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		_renderCommand.material().SetTexture(0, *_target);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}

	UpscaleRenderPass::AntialiasingSubpass::AntialiasingSubpass()
	{
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}

	void UpscaleRenderPass::AntialiasingSubpass::Register()
	{
		if (_view != nullptr) {
			Viewport::GetChain().push_back(_view.get());
		}
	}

	bool UpscaleRenderPass::AntialiasingSubpass::OnDraw(RenderQueue& renderQueue)
	{
		Vector2i size = _target->GetSize();
		auto instanceBlock = _renderCommand.material().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue((float)size.X, (float)size.Y, 0.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(_targetSize.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		_renderCommand.material().SetTexture(0, *_target);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}

	UpscaleRenderPassWithClipping::UpscaleRenderPassWithClipping()
		: UpscaleRenderPass()
	{
	}

	void UpscaleRenderPassWithClipping::Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight)
	{
		if (_clippedView != nullptr) {
			_clippedView->RemoveAllTextures();
		}
		if (_overlayView != nullptr) {
			_overlayView->RemoveAllTextures();
		}

		UpscaleRenderPass::Initialize(width, height, targetWidth, targetHeight);

		if (_clippedView == nullptr) {
			_clippedNode = std::make_unique<SceneNode>();
			_clippedNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

			_clippedView = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_clippedView->SetRootNode(_clippedNode.get());
			_clippedView->SetCamera(_camera.get());
			_clippedView->SetClearMode(Viewport::ClearMode::Never);
		} else {
			_clippedView->SetTexture(_target.get());
		}

		if (_overlayView == nullptr) {
			_overlayNode = std::make_unique<SceneNode>();
			_overlayNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

			_overlayView = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_overlayView->SetRootNode(_overlayNode.get());
			_overlayView->SetCamera(_camera.get());
			_overlayView->SetClearMode(Viewport::ClearMode::Never);
		} else {
			_overlayView->SetTexture(_target.get());
		}
	}

	void UpscaleRenderPassWithClipping::Register()
	{
		_antialiasing.Register();

		auto& chain = Viewport::GetChain();
		chain.push_back(_overlayView.get());
		chain.push_back(_clippedView.get());
		chain.push_back(_view.get());
	}
}