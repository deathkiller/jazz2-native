#include "UpscaleRenderPass.h"
#include "../ContentResolver.h"
#include "../PreferencesCache.h"

#include "../../nCine/Application.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/Viewport.h"

namespace Jazz2::Rendering
{
	// Layer assigned to an overlay layer's composite command so it is drawn on top of the scene composite (layer 0)
	static constexpr std::uint16_t OverlayCompositeLayer = 60000;

	void UpscaleRenderPass::Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight, std::int32_t supersample, bool overlay)
	{
		_logicalSize = Vector2i(width, height);
		// An overlay layer (the HUD) is always at native resolution, only the scene is supersampled
		_supersample = (overlay ? 1 : (supersample > 1 ? supersample : 1));

		// The scene is always drawn in [0, width]x[0, height] coordinates (the ortho projection below), but the target
		// texture may be rendered at a higher resolution. This is used for splitscreen zoom-out, where each player's
		// composite would otherwise be minified into a small region of a native-resolution framebuffer (effectively
		// halving its resolution); supersampling the framebuffer keeps the per-player image at full resolution, while
		// the HUD and combine geometry keep using the unchanged logical coordinate space.
		std::int32_t textureWidth = width * _supersample;
		std::int32_t textureHeight = height * _supersample;

		// An overlay layer needs an alpha channel so the scene shows through where nothing is drawn
		Texture::Format targetFormat = (overlay ? Texture::Format::RGBA8 : Texture::Format::RGB8);

		_targetSize = Vector2f((float)targetWidth, (float)targetHeight);

		// The antialiasing subpass renders the (possibly rescaled) image to an integer multiple of the source resolution
		// and then bicubically resolves it down to the window. It runs when explicitly enabled, and always when the
		// scene is supersampled: there the source resolution doesn't evenly divide the window, so a plain upscale
		// shimmers as the camera moves; the subpass turns it into a clean integer upscale followed by a smooth
		// downsample. The integer multiple is computed from the actual (supersampled) texture size. Never for the overlay.
		bool useAntialiasing = ((PreferencesCache::ActiveRescaleMode & RescaleMode::UseAntialiasing) == RescaleMode::UseAntialiasing);
		if (!overlay && (_supersample > 1 || useAntialiasing)) {
			float widthRatio, heightRatio;
			float widthFrac = modff((float)targetWidth / textureWidth, &widthRatio);
			float heightFrac = modff((float)targetHeight / textureHeight, &heightRatio);

			std::int32_t requiredWidth = (std::int32_t)(widthFrac > 0.004f ? (textureWidth * (widthRatio + 1)) : targetWidth);
			std::int32_t requiredHeight = (std::int32_t)(heightFrac > 0.004f ? (textureHeight * (heightRatio + 1)) : targetHeight);

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

		_camera.SetOrthoProjection(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f);
		_camera.SetView(0, 0, 0, 1);

		if (_view == nullptr) {
			_node = std::make_unique<SceneNode>();
			_node->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

			_target = std::make_unique<Texture>(nullptr, targetFormat, textureWidth, textureHeight);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->SetRootNode(_node.get());
			_view->SetCamera(&_camera);
			_view->SetClearMode(Viewport::ClearMode::Never);
		} else {
			_view->RemoveAllTextures();
			_target->Init(nullptr, targetFormat, textureWidth, textureHeight);
			_view->SetTexture(_target.get());
		}
		_target->SetMinFiltering(SamplerFilter::Nearest);
		_target->SetMagFiltering(SamplerFilter::Nearest);
		_target->SetWrap(SamplerWrapping::ClampToEdge);

		// The overlay layer is only partially covered by the HUD, so unlike the scene (which is fully repainted every
		// frame and uses ClearMode::Never) it must be cleared to fully transparent each frame
		if (overlay) {
			_view->SetClearMode(Viewport::ClearMode::EveryFrame);
			_view->SetClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		}

		if (_antialiasing._target != nullptr) {
			_antialiasing._camera.SetOrthoProjection(0.0f, _targetSize.X, _targetSize.Y, 0.0f);
			_antialiasing._camera.SetView(0, 0, 0, 1);

			_antialiasing._view = std::make_unique<Viewport>(_antialiasing._target.get(), Viewport::DepthStencilFormat::None);
			_antialiasing._view->SetRootNode(this);
			_antialiasing._view->SetCamera(&_antialiasing._camera);
			//_antialiasing._view->setClearMode(Viewport::ClearMode::Never);

			SceneNode& rootNode = theApplication().GetRootNode();
			_antialiasing.setParent(&rootNode);
			setParent(nullptr);

			if (_antialiasing._renderCommand.GetMaterial().SetShader(ContentResolver::Get().GetShader(PrecompiledShader::Antialiasing))) {
				_antialiasing._renderCommand.GetMaterial().ReserveUniformsDataMemory();
				_antialiasing._renderCommand.GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
				// Required to reset render command properly
				_antialiasing._renderCommand.SetTransformation(_antialiasing._renderCommand.GetTransformation());

				auto* textureUniform = _antialiasing._renderCommand.GetMaterial().Uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->GetIntValue(0) != 0) {
					textureUniform->SetIntValue(0); // GL_TEXTURE0
				}
			}
		} else {
			_antialiasing._view = nullptr;

			SceneNode& rootNode = theApplication().GetRootNode();
			_antialiasing.setParent(nullptr);
			setParent(&rootNode);
		}

		// Prepare render command
#if !defined(DISABLE_RESCALE_SHADERS)
		_resizeAtLogicalScale = false;
		if (overlay) {
			// The overlay layer is composited at the native logical resolution straight onto the screen, so it must
			// never go through a rescale (pixel-art upscaling) shader
			_resizeShader = nullptr;
		} else {
			switch (PreferencesCache::ActiveRescaleMode & RescaleMode::TypeMask) {
				// Edge-detection scalers derive their sample offsets from the input size; when the scene is supersampled
				// they must use the logical size, otherwise they operate at the supersampled texel scale (offsets too
				// small) and the effect comes out far too weak
				case RescaleMode::HQ2x: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeHQ2x); _resizeAtLogicalScale = true; break;
				case RescaleMode::_3xBrz: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::Resize3xBrz); _resizeAtLogicalScale = true; break;
				case RescaleMode::CrtScanlines: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeCrtScanlines); break;
				case RescaleMode::CrtShadowMask: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeCrtShadowMask); break;
				case RescaleMode::CrtApertureGrille: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeCrtApertureGrille); break;
				case RescaleMode::Monochrome: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeMonochrome); break;
				case RescaleMode::Sabr: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeSabr); _resizeAtLogicalScale = true; break;
				case RescaleMode::CleanEdge: _resizeShader = ContentResolver::Get().GetShader(PrecompiledShader::ResizeCleanEdge); _resizeAtLogicalScale = true; break;
				default: _resizeShader = nullptr; break;
			}
		}

		bool shaderChanged = (_resizeShader != nullptr
			? _renderCommand.GetMaterial().SetShader(_resizeShader)
			: _renderCommand.GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite));
#else
		bool shaderChanged = _renderCommand.GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite);
#endif
		if (shaderChanged) {
			_renderCommand.GetMaterial().ReserveUniformsDataMemory();
			_renderCommand.GetGeometry().SetDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			_renderCommand.SetTransformation(_renderCommand.GetTransformation());

			auto* textureUniform = _renderCommand.GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}

		if (overlay) {
			// Composite the overlay over whatever it is drawn onto (the player viewports in the scene buffer). Canvas
			// draws into the overlay texture with the standard SRC_ALPHA/ONE_MINUS_SRC_ALPHA blend over a transparent
			// clear, which leaves the texture's color premultiplied by alpha, so it must be composited with a
			// premultiplied blend (ONE, ONE_MINUS_SRC_ALPHA); this keeps opaque HUD pixels exact (a straight-alpha
			// blend would darken them by the alpha twice). Force it to the top so it is drawn after the viewport
			// composites (which stay at the default layer 0).
			_renderCommand.GetMaterial().SetBlendingEnabled(true);
			_renderCommand.GetMaterial().SetBlendingFactors(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			_renderCommand.SetLayer(OverlayCompositeLayer);
		}
	}

	void UpscaleRenderPass::Register()
	{
		_antialiasing.Register();

		Viewport::GetChain().push_back(_view.get());
	}

	bool UpscaleRenderPass::OnDraw(RenderQueue& renderQueue)
	{
		auto instanceBlock = _renderCommand.GetMaterial().UniformBlock(Material::InstanceBlockName);
#if !defined(DISABLE_RESCALE_SHADERS)
		if (_resizeShader != nullptr) {
			// TexRectUniformName is reused for the input size that the shader derives its sample offsets from. Edge
			// detection scalers use the logical size so they operate at the native pixel scale even when the scene
			// texture is supersampled; others use the actual texture size.
			Vector2i size = (_resizeAtLogicalScale ? _logicalSize : _target->GetSize());
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue((float)size.X, (float)size.Y, 0.0f, 0.0f);
		} else
#endif
		{
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		}

		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(_targetSize.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

		_renderCommand.GetMaterial().SetTexture(0, *_target);

		renderQueue.AddCommand(&_renderCommand);

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
		auto instanceBlock = _renderCommand.GetMaterial().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue((float)size.X, (float)size.Y, 0.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(_targetSize.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

		_renderCommand.GetMaterial().SetTexture(0, *_target);

		renderQueue.AddCommand(&_renderCommand);

		return true;
	}

	UpscaleRenderPassWithClipping::UpscaleRenderPassWithClipping()
		: UpscaleRenderPass()
	{
	}

	void UpscaleRenderPassWithClipping::Initialize(std::int32_t width, std::int32_t height, std::int32_t targetWidth, std::int32_t targetHeight, std::int32_t supersample, bool overlay)
	{
		if (_clippedView != nullptr) {
			_clippedView->RemoveAllTextures();
		}
		if (_overlayView != nullptr) {
			_overlayView->RemoveAllTextures();
		}

		UpscaleRenderPass::Initialize(width, height, targetWidth, targetHeight, supersample, overlay);

		if (_clippedView == nullptr) {
			_clippedNode = std::make_unique<SceneNode>();
			_clippedNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

			_clippedView = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_clippedView->SetRootNode(_clippedNode.get());
			_clippedView->SetCamera(&_camera);
			_clippedView->SetClearMode(Viewport::ClearMode::Never);
		} else {
			_clippedView->SetTexture(_target.get());
		}

		if (_overlayView == nullptr) {
			_overlayNode = std::make_unique<SceneNode>();
			_overlayNode->setVisitOrderState(SceneNode::VisitOrderState::Disabled);

			_overlayView = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_overlayView->SetRootNode(_overlayNode.get());
			_overlayView->SetCamera(&_camera);
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