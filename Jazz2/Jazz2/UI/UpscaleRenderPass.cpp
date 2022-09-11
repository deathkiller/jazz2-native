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

		bool notInitialized = (_view == nullptr);

		if (notInitialized) {
			_camera = std::make_unique<Camera>();
		}
		_camera->setOrthoProjection(width * (-0.5f), width * (+0.5f), height * (-0.5f), height * (+0.5f));
		_camera->setView(0, 0, 0, 1);

		if (notInitialized) {
			_node = std::make_unique<SceneNode>();
			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::NONE);
			_view->setRootNode(_node.get());
			_view->setCamera(_camera.get());
			_view->setClearMode(Viewport::ClearMode::NEVER);

			SceneNode& rootNode = theApplication().rootNode();
			setParent(&rootNode);
		} else {
			_view->removeAllTextures();
			_target->init(nullptr, Texture::Format::RGB8, width, height);
			_view->setTexture(_target.get());
		}
		_target->setMagFiltering(SamplerFilter::Nearest);

		// Prepare render command
		// TODO: Reset render command as workaround
		(&_renderCommand)->~RenderCommand();
		new (&_renderCommand) RenderCommand();

		bool shaderChanged;
#if defined(ALLOW_RESCALE_SHADERS)
		PrecompiledShader shader;
		switch (PreferencesCache::ActiveRescaleMode) {
			case RescaleMode::_3xBrz: _resizeShader = ContentResolver::Current().GetShader(PrecompiledShader::Resize3xBrz); break;
			case RescaleMode::Monochrome: _resizeShader = ContentResolver::Current().GetShader(PrecompiledShader::ResizeMonochrome); break;
			default: _resizeShader = nullptr; break;
		}
		if (_resizeShader != nullptr) {
			shaderChanged = _renderCommand.material().setShader(_resizeShader);
		} else {
			shaderChanged = _renderCommand.material().setShaderProgramType(Material::ShaderProgramType::SPRITE);
		}
#else
		shaderChanged = _renderCommand.material().setShaderProgramType(Material::ShaderProgramType::SPRITE);
#endif
		if (shaderChanged) {
			_renderCommand.material().reserveUniformsDataMemory();
			_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			GLUniformCache* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
		}
	}

	void UpscaleRenderPass::Register()
	{
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
}