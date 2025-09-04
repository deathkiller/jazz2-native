#include "BlurRenderPass.h"
#include "PlayerViewport.h"

#include "../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Rendering
{
	void BlurRenderPass::Initialize(Texture* source, std::int32_t width, std::int32_t height, Vector2f direction)
	{
		_source = source;
		_downsampleOnly = (direction.X <= std::numeric_limits<float>::epsilon() && direction.Y <= std::numeric_limits<float>::epsilon());
		_direction = direction;

		bool notInitialized = (_view == nullptr);

		if (notInitialized) {
			_camera = std::make_unique<Camera>();
		}
		_camera->SetOrthoProjection(0.0f, (float)width, (float)height, 0.0f);
		_camera->SetView(0.0f, 0.0f, 0.0f, 1.0f);

		if (notInitialized) {
			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_target->SetWrap(SamplerWrapping::ClampToEdge);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->SetRootNode(this);
			_view->SetCamera(_camera.get());
			//_view->setClearMode(Viewport::ClearMode::Never);
		} else {
			_view->RemoveAllTextures();
			_target->Init(nullptr, Texture::Format::RGB8, width, height);
			_view->SetTexture(_target.get());
		}
		_target->SetMagFiltering(SamplerFilter::Linear);

		Shader* shader = _downsampleOnly ? _owner->_levelHandler->_downsampleShader : _owner->_levelHandler->_blurShader;

		// Prepare render command
		_renderCommand.GetMaterial().SetShader(shader);
		//_renderCommand.GetMaterial().SetBlendingEnabled(true);
		_renderCommand.GetMaterial().ReserveUniformsDataMemory();
		_renderCommand.GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

		auto* textureUniform = _renderCommand.GetMaterial().Uniform(Material::TextureUniformName);
		if (textureUniform && textureUniform->GetIntValue(0) != 0) {
			textureUniform->SetIntValue(0); // GL_TEXTURE0
		}
	}

	void BlurRenderPass::Register()
	{
		Viewport::GetChain().push_back(_view.get());
	}

	bool BlurRenderPass::OnDraw(RenderQueue& renderQueue)
	{
		Vector2i size = _target->GetSize();

		auto* instanceBlock = _renderCommand.GetMaterial().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(static_cast<float>(size.X), static_cast<float>(size.Y));
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

		_renderCommand.GetMaterial().Uniform("uPixelOffset")->SetFloatValue(1.0f / size.X, 1.0f / size.Y);
		if (!_downsampleOnly) {
			_renderCommand.GetMaterial().Uniform("uDirection")->SetFloatValue(_direction.X, _direction.Y);
		}
		_renderCommand.GetMaterial().SetTexture(0, *_source);

		renderQueue.AddCommand(&_renderCommand);

		return true;
	}
}