#include "UpscaleRenderPass.h"
#include "../PreferencesCache.h"

#include "../../nCine/Application.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/Viewport.h"

namespace Jazz2::UI
{
	void UpscaleRenderPass::Initialize(int width, int height, int targetWidth, int targetHeight)
	{
		_targetSize = Vector2f(targetWidth, targetHeight);

		if ((PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) == RescaleMode::UseAntialiasing) {
			float widthRatio, heightRatio;
			float widthFrac = modff((float)targetWidth / width, &widthRatio);
			float heightFrac = modff((float)targetHeight / height, &heightRatio);

			float requiredWidth = (widthFrac > 0.004f ? (width * (widthRatio + 1)) : targetWidth);
			float requiredHeight = (heightFrac > 0.004f ? (height * (heightRatio + 1)) : targetHeight);

			if (std::abs(requiredWidth - targetWidth) > 2 || std::abs(requiredHeight - targetHeight) > 2) {
				if (_antialiasing._target == nullptr) {
					_antialiasing._target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, requiredWidth, requiredHeight);
				} else {
					_antialiasing._target->init(nullptr, Texture::Format::RGB8, requiredWidth, requiredHeight);
				}
				_antialiasing._target->setMinFiltering(SamplerFilter::Linear);
				_antialiasing._target->setMagFiltering(SamplerFilter::Linear);

				_antialiasing._targetSize = _targetSize;
				_targetSize = Vector2f(requiredWidth, requiredHeight);
			} else {
				_antialiasing._target = nullptr;
			}
		} else {
			_antialiasing._target = nullptr;
		}

		if (_camera == nullptr) {
			_camera = std::make_unique<Camera>();
		}
		_camera->setOrthoProjection(width * (-0.5f), width * (+0.5f), height * (-0.5f), height * (+0.5f));
		_camera->setView(0, 0, 0, 1);

		if (_view == nullptr) {
			_node = std::make_unique<SceneNode>();
			_node->setVisitOrderState(SceneNode::VisitOrderState::DISABLED);

			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::NONE);
			_view->setRootNode(_node.get());
			_view->setCamera(_camera.get());
			_view->setClearMode(Viewport::ClearMode::NEVER);
		} else {
			_view->removeAllTextures();
			_target->init(nullptr, Texture::Format::RGB8, width, height);
			_view->setTexture(_target.get());
		}
		_target->setMinFiltering(SamplerFilter::Nearest);
		_target->setMagFiltering(SamplerFilter::Nearest);

		if (_antialiasing._target != nullptr) {
			if (_antialiasing._camera == nullptr) {
				_antialiasing._camera = std::make_unique<Camera>();
			}
			_antialiasing._camera->setOrthoProjection(_targetSize.X * (-0.5f), _targetSize.X * (+0.5f), _targetSize.Y * (-0.5f), _targetSize.Y * (+0.5f));
			_antialiasing._camera->setView(0, 0, 0, 1);

			_antialiasing._view = std::make_unique<Viewport>(_antialiasing._target.get(), Viewport::DepthStencilFormat::NONE);
			_antialiasing._view->setRootNode(this);
			_antialiasing._view->setCamera(_antialiasing._camera.get());
			_antialiasing._view->setClearMode(Viewport::ClearMode::NEVER);

			SceneNode& rootNode = theApplication().rootNode();
			_antialiasing.setParent(&rootNode);
			setParent(nullptr);

			if (_antialiasing._renderCommand.material().setShaderProgramType(Material::ShaderProgramType::SPRITE)) {
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
#if defined(ALLOW_RESCALE_SHADERS)
		PrecompiledShader shader;
		switch (PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask) {
			case RescaleMode::HQ2x: _resizeShader = ContentResolver::Current().GetShader(PrecompiledShader::ResizeHQ2x); break;
			case RescaleMode::_3xBrz: _resizeShader = ContentResolver::Current().GetShader(PrecompiledShader::Resize3xBrz); break;
			case RescaleMode::Crt: _resizeShader = ContentResolver::Current().GetShader(PrecompiledShader::ResizeCrt); break;
			case RescaleMode::Monochrome: _resizeShader = ContentResolver::Current().GetShader(PrecompiledShader::ResizeMonochrome); break;
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
		if (_antialiasing._view != nullptr) {
			Viewport::chain().push_back(_antialiasing._view.get());
		}
		Viewport::chain().push_back(_view.get());
	}

	bool UpscaleRenderPass::OnDraw(RenderQueue& renderQueue)
	{
		auto size = _target->size();

		auto instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, -1.0f, 1.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(_targetSize.Data());
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

#if defined(ALLOW_RESCALE_SHADERS)
		if (_resizeShader != nullptr) {
			_renderCommand.material().uniform("uTextureSize")->setFloatValue(size.X, size.Y);
		}
#endif

		_renderCommand.material().setTexture(0, *_target);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}

	bool UpscaleRenderPass::AntialiasingSubpass::OnDraw(RenderQueue& renderQueue)
	{
		auto size = _target->size();

		auto instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, -1.0f, 1.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(_targetSize.Data());
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		_renderCommand.material().setTexture(0, *_target);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}
}