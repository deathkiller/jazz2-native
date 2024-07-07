﻿#include "PlayerViewport.h"
#include "PreferencesCache.h"
#include "Actors/Player.h"

#include "../nCine/tracy.h"
#include "../nCine/ServiceLocator.h"
#include "../nCine/Base/Random.h"
#include "../nCine/Graphics/RenderQueue.h"

namespace Jazz2
{
	bool LightingRenderer::OnDraw(RenderQueue& renderQueue)
	{
		_renderCommandsCount = 0;
		_emittedLightsCache.clear();

		// Collect all active light emitters
		auto& actors = _owner->_levelHandler->GetActors();
		std::size_t actorsCount = actors.size();
		for (std::size_t i = 0; i < actorsCount; i++) {
			actors[i]->OnEmitLights(_emittedLightsCache);
		}

		for (auto& light : _emittedLightsCache) {
			auto command = RentRenderCommand();
			auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(light.Pos.X, light.Pos.Y, light.RadiusNear / light.RadiusFar, 0.0f);
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(light.RadiusFar * 2.0f, light.RadiusFar * 2.0f);
			instanceBlock->uniform(Material::ColorUniformName)->setFloatValue(light.Intensity, light.Brightness, 0.0f, 0.0f);
			command->setTransformation(Matrix4x4f::Translation(light.Pos.X, light.Pos.Y, 0));

			renderQueue.addCommand(command);
		}

		return true;
	}

	RenderCommand* LightingRenderer::RentRenderCommand()
	{
		if (_renderCommandsCount < _renderCommands.size()) {
			RenderCommand* command = _renderCommands[_renderCommandsCount].get();
			_renderCommandsCount++;
			return command;
		} else {
			std::unique_ptr<RenderCommand>& command = _renderCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Lighting));
			_renderCommandsCount++;
			command->material().setShader(_owner->_levelHandler->_lightingShader);
			command->material().setBlendingEnabled(true);
			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE);
			command->material().reserveUniformsDataMemory();
			command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
			return command.get();
		}
	}

	void BlurRenderPass::Initialize(Texture* source, std::int32_t width, std::int32_t height, const Vector2f& direction)
	{
		_source = source;
		_downsampleOnly = (direction.X <= std::numeric_limits<float>::epsilon() && direction.Y <= std::numeric_limits<float>::epsilon());
		_direction = direction;

		bool notInitialized = (_view == nullptr);

		if (notInitialized) {
			_camera = std::make_unique<Camera>();
		}
		_camera->setOrthoProjection(0.0f, (float)width, (float)height, 0.0f);
		_camera->setView(0.0f, 0.0f, 0.0f, 1.0f);

		if (notInitialized) {
			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_target->setWrap(SamplerWrapping::ClampToEdge);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->setRootNode(this);
			_view->setCamera(_camera.get());
			//_view->setClearMode(Viewport::ClearMode::Never);
		} else {
			_view->removeAllTextures();
			_target->init(nullptr, Texture::Format::RGB8, width, height);
			_view->setTexture(_target.get());
		}
		_target->setMagFiltering(SamplerFilter::Linear);

		// Prepare render command
		_renderCommand.material().setShader(_downsampleOnly ? _owner->_levelHandler->_downsampleShader : _owner->_levelHandler->_blurShader);
		//_renderCommand.material().setBlendingEnabled(true);
		_renderCommand.material().reserveUniformsDataMemory();
		_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

		GLUniformCache* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
		if (textureUniform && textureUniform->intValue(0) != 0) {
			textureUniform->setIntValue(0); // GL_TEXTURE0
		}
	}

	void BlurRenderPass::Register()
	{
		Viewport::chain().push_back(_view.get());
	}

	bool BlurRenderPass::OnDraw(RenderQueue& renderQueue)
	{
		Vector2i size = _target->size();

		auto* instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(static_cast<float>(size.X), static_cast<float>(size.Y));
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

		_renderCommand.material().uniform("uPixelOffset")->setFloatValue(1.0f / size.X, 1.0f / size.Y);
		if (!_downsampleOnly) {
			_renderCommand.material().uniform("uDirection")->setFloatValue(_direction.X, _direction.Y);
		}
		_renderCommand.material().setTexture(0, *_source);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}

	void CombineRenderer::Initialize(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_bounds = Rectf(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));

		if (_renderCommand.material().setShader(_owner->_levelHandler->_combineShader)) {
			_renderCommand.material().reserveUniformsDataMemory();
			_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			//_renderCommand.setTransformation(_renderCommand.transformation());
			_renderCommand.setTransformation(Matrix4x4f::Translation(x, y, 0.0f));

			GLUniformCache* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
			GLUniformCache* lightTexUniform = _renderCommand.material().uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->intValue(0) != 1) {
				lightTexUniform->setIntValue(1); // GL_TEXTURE1
			}
			GLUniformCache* blurHalfTexUniform = _renderCommand.material().uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->intValue(0) != 2) {
				blurHalfTexUniform->setIntValue(2); // GL_TEXTURE2
			}
			GLUniformCache* blurQuarterTexUniform = _renderCommand.material().uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->intValue(0) != 3) {
				blurQuarterTexUniform->setIntValue(3); // GL_TEXTURE3
			}
		}

		if (_renderCommandWithWater.material().setShader(_owner->_levelHandler->_combineWithWaterShader)) {
			_renderCommandWithWater.material().reserveUniformsDataMemory();
			_renderCommandWithWater.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
			// Required to reset render command properly
			//_renderCommandWithWater.setTransformation(_renderCommandWithWater.transformation());
			_renderCommand.setTransformation(Matrix4x4f::Translation(x, y, 0.0f));

			GLUniformCache* textureUniform = _renderCommandWithWater.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
			GLUniformCache* lightTexUniform = _renderCommandWithWater.material().uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->intValue(0) != 1) {
				lightTexUniform->setIntValue(1); // GL_TEXTURE1
			}
			GLUniformCache* blurHalfTexUniform = _renderCommandWithWater.material().uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->intValue(0) != 2) {
				blurHalfTexUniform->setIntValue(2); // GL_TEXTURE2
			}
			GLUniformCache* blurQuarterTexUniform = _renderCommandWithWater.material().uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->intValue(0) != 3) {
				blurQuarterTexUniform->setIntValue(3); // GL_TEXTURE3
			}
			GLUniformCache* displacementTexUniform = _renderCommandWithWater.material().uniform("uTextureDisplacement");
			if (displacementTexUniform && displacementTexUniform->intValue(0) != 4) {
				displacementTexUniform->setIntValue(4); // GL_TEXTURE4
			}
		}
	}

	Rectf CombineRenderer::GetBounds() const
	{
		return _bounds;
	}

	bool CombineRenderer::OnDraw(RenderQueue& renderQueue)
	{
		float viewWaterLevel = _owner->_levelHandler->_waterLevel - _owner->_cameraPos.Y + _bounds.H * 0.5f;
		bool viewHasWater = (viewWaterLevel < _bounds.H);
		auto& command = (viewHasWater ? _renderCommandWithWater : _renderCommand);

		command.material().setTexture(0, *_owner->_viewTexture);
		command.material().setTexture(1, *_owner->_lightingBuffer);
		command.material().setTexture(2, *_owner->_blurPass2.GetTarget());
		command.material().setTexture(3, *_owner->_blurPass4.GetTarget());
		if (viewHasWater && !PreferencesCache::LowWaterQuality) {
			command.material().setTexture(4, *_owner->_levelHandler->_noiseTexture);
		}

		auto* instanceBlock = command.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(_bounds.W, _bounds.H);
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

		command.material().uniform("uAmbientColor")->setFloatVector(_owner->_ambientLight.Data());
		command.material().uniform("uTime")->setFloatValue(_owner->_levelHandler->_elapsedFrames * 0.0018f);

		if (viewHasWater) {
			command.material().uniform("uWaterLevel")->setFloatValue(viewWaterLevel / _bounds.H);
			command.material().uniform("uCameraPos")->setFloatVector(_owner->_cameraPos.Data());
		}

		renderQueue.addCommand(&command);

		return true;
	}

	PlayerViewport::PlayerViewport(LevelHandler* levelHandler, Actors::Player* targetPlayer)
		: _levelHandler(levelHandler), _targetPlayer(targetPlayer),
		_downsamplePass(this), _blurPass1(this), _blurPass2(this), _blurPass3(this), _blurPass4(this),
		_cameraResponsiveness(1.0f, 1.0f), _shakeDuration(0.0f)
	{
		_ambientLight = levelHandler->_defaultAmbientLight;
		_ambientLightTarget = _ambientLight.W;
	}

	Rectf PlayerViewport::GetBounds() const
	{
		return _combineRenderer->GetBounds();
	}

	Actors::Player* PlayerViewport::GetTargetPlayer() const
	{
		return _targetPlayer;
	}

	void PlayerViewport::OnFrameEnd()
	{
		_lightingView->setClearColor(_ambientLight.W, 0.0f, 0.0f, 1.0f);
	}

	void PlayerViewport::UpdateCamera(float timeMult)
	{
		ZoneScopedC(0x4876AF);

		constexpr float ResponsivenessChange = 0.04f;
		constexpr float ResponsivenessMin = 0.3f;
		constexpr float SlowRatioX = 0.3f;
		constexpr float SlowRatioY = 0.3f;
		constexpr float FastRatioX = 0.2f;
		constexpr float FastRatioY = 0.04f;

		// Ambient Light Transition
		if (_ambientLight.W != _ambientLightTarget) {
			float step = timeMult * 0.012f;
			if (std::abs(_ambientLight.W - _ambientLightTarget) < step) {
				_ambientLight.W = _ambientLightTarget;
			} else {
				_ambientLight.W += step * ((_ambientLightTarget < _ambientLight.W) ? -1.0f : 1.0f);
			}
		}

		// Viewport bounds animation
		auto& viewBounds = _levelHandler->_viewBounds;

		if (viewBounds != _levelHandler->_viewBoundsTarget) {
			if (std::abs(viewBounds.X - _levelHandler->_viewBoundsTarget.X) < 2.0f) {
				viewBounds = _levelHandler->_viewBoundsTarget;
			} else {
				constexpr float TransitionSpeed = 0.02f;
				float dx = (_levelHandler->_viewBoundsTarget.X - viewBounds.X) * TransitionSpeed * timeMult;
				viewBounds.X += dx;
				viewBounds.W -= dx;
			}
		}

		// The position to focus on
		Vector2i halfView = _view->size() / 2;
		Vector2f focusPos = _targetPlayer->_pos;

		// If player doesn't move but has some speed, it's probably stuck, so reset the speed
		Vector2f focusSpeed = _targetPlayer->_speed;
		if (std::abs(_cameraLastPos.X - focusPos.X) < 1.0f) {
			focusSpeed.X = 0.0f;
		}
		if (std::abs(_cameraLastPos.Y - focusPos.Y) < 1.0f) {
			focusSpeed.Y = 0.0f;
		}

		Vector2f focusVelocity = Vector2f(std::abs(focusSpeed.X), std::abs(focusSpeed.Y));

		// Camera responsiveness (smoothing unexpected movements)
		if (focusVelocity.X < 1.0f) {
			if (_cameraResponsiveness.X > ResponsivenessMin) {
				_cameraResponsiveness.X = std::max(_cameraResponsiveness.X - ResponsivenessChange * timeMult, ResponsivenessMin);
			}
		} else {
			if (_cameraResponsiveness.X < 1.0f) {
				_cameraResponsiveness.X = std::min(_cameraResponsiveness.X + ResponsivenessChange * timeMult, 1.0f);
			}
		}
		if (focusVelocity.Y < 1.0f) {
			if (_cameraResponsiveness.Y > ResponsivenessMin) {
				_cameraResponsiveness.Y = std::max(_cameraResponsiveness.Y - ResponsivenessChange * timeMult, ResponsivenessMin);
			}
		} else {
			if (_cameraResponsiveness.Y < 1.0f) {
				_cameraResponsiveness.Y = std::min(_cameraResponsiveness.Y + ResponsivenessChange * timeMult, 1.0f);
			}
		}

		_cameraLastPos.X = lerpByTime(_cameraLastPos.X, focusPos.X, _cameraResponsiveness.X, timeMult);
		_cameraLastPos.Y = lerpByTime(_cameraLastPos.Y, focusPos.Y, _cameraResponsiveness.Y, timeMult);

		_cameraDistanceFactor.X = lerpByTime(_cameraDistanceFactor.X, focusSpeed.X * 8.0f, (focusVelocity.X < 2.0f ? SlowRatioX : FastRatioX), timeMult);
		_cameraDistanceFactor.Y = lerpByTime(_cameraDistanceFactor.Y, focusSpeed.Y * 5.0f, (focusVelocity.Y < 2.0f ? SlowRatioY : FastRatioY), timeMult);

		if (_shakeDuration > 0.0f) {
			_shakeDuration -= timeMult;

			if (_shakeDuration <= 0.0f) {
				_shakeOffset = Vector2f::Zero;
			} else {
				float shakeFactor = 0.1f * timeMult;
				_shakeOffset.X = lerp(_shakeOffset.X, nCine::Random().NextFloat(-0.2f, 0.2f) * halfView.X, shakeFactor) * std::min(_shakeDuration * 0.1f, 1.0f);
				_shakeOffset.Y = lerp(_shakeOffset.Y, nCine::Random().NextFloat(-0.2f, 0.2f) * halfView.Y, shakeFactor) * std::min(_shakeDuration * 0.1f, 1.0f);
			}
		}

		// Clamp camera position to level bounds
		if (viewBounds.W > halfView.X * 2) {
			_cameraPos.X = std::clamp(_cameraLastPos.X + _cameraDistanceFactor.X, viewBounds.X + halfView.X, viewBounds.X + viewBounds.W - halfView.X) + _shakeOffset.X;
			if (!PreferencesCache::UnalignedViewport || std::abs(_cameraDistanceFactor.X) < 1.0f) {
				_cameraPos.X = std::floor(_cameraPos.X);
			}
		} else {
			_cameraPos.X = std::floor(viewBounds.X + viewBounds.W * 0.5f + _shakeOffset.X);
		}
		if (viewBounds.H > halfView.Y * 2) {
			_cameraPos.Y = std::clamp(_cameraLastPos.Y + _cameraDistanceFactor.Y, viewBounds.Y + halfView.Y - 1.0f, viewBounds.Y + viewBounds.H - halfView.Y - 2.0f) + _shakeOffset.Y;
			if (!PreferencesCache::UnalignedViewport || std::abs(_cameraDistanceFactor.Y) < 1.0f) {
				_cameraPos.Y = std::floor(_cameraPos.Y);
			}
		} else {
			_cameraPos.Y = std::floor(viewBounds.Y + viewBounds.H * 0.5f + _shakeOffset.Y);
		}

		_camera->setView(_cameraPos - halfView.As<float>(), 0.0f, 1.0f);

		// TODO: Viewports - Audio listener
		if (_targetPlayer->_playerIndex == 0) {
			// Update audio listener position
			IAudioDevice& device = theServiceLocator().GetAudioDevice();
			device.updateListener(Vector3f(_cameraPos, 0.0f), Vector3f(focusSpeed, 0.0f));
		}
	}

	void PlayerViewport::ShakeCameraView(float duration)
	{
		if (_shakeDuration < duration) {
			_shakeDuration = duration;
		}
	}

	void PlayerViewport::WarpCameraToTarget(bool fast)
	{
		Vector2f focusPos = _targetPlayer->GetPos();
		if (!fast) {
			_cameraPos = focusPos;
			_cameraLastPos = _cameraPos;
			_cameraDistanceFactor = Vector2f(0.0f, 0.0f);
			_cameraResponsiveness = Vector2f(1.0f, 1.0f);
		} else {
			Vector2f diff = _cameraLastPos - _cameraPos;
			_cameraPos = focusPos;
			_cameraLastPos = _cameraPos + diff;
		}
	}
}