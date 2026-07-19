#include "CombineRenderer.h"
#include "PlayerViewport.h"
#include "../PreferencesCache.h"

#include "../../nCine/Graphics/RenderQueue.h"

#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
#	include <algorithm>
#	include <cmath>
#endif

namespace Jazz2::Rendering
{
	CombineRenderer::CombineRenderer(PlayerViewport* owner)
		: _owner(owner)
	{
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}
		
	void CombineRenderer::Initialize(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_bounds = Rectf(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));

#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
		// Software renderer: the scene is rasterized straight to the screen buffer and the bloom/blur chain is
		// gated out, so the command carries no textures or uniforms. It only binds the Combine program (so the
		// device recognizes the draw and runs the CPU dynamic-lighting combine in its place) and a 4-vertex quad
		// so the draw actually reaches the device in the Draw phase.
		if (_renderCommand.GetMaterial().SetShader(_owner->_levelHandler->_combineShader)) {
			_renderCommand.GetMaterial().ReserveUniformsDataMemory();
			_renderCommand.GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
		}
		_renderCommand.SetTransformation(Matrix4x4f::Translation((float)x, (float)y, 0.0f));
#else
		if (_renderCommand.GetMaterial().SetShader(_owner->_levelHandler->_combineShader)) {
			_renderCommand.GetMaterial().ReserveUniformsDataMemory();
			_renderCommand.GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
			auto* textureUniform = _renderCommand.GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			auto* lightTexUniform = _renderCommand.GetMaterial().Uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->GetIntValue(0) != 1) {
				lightTexUniform->SetIntValue(1); // GL_TEXTURE1
			}
			auto* blurHalfTexUniform = _renderCommand.GetMaterial().Uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->GetIntValue(0) != 2) {
				blurHalfTexUniform->SetIntValue(2); // GL_TEXTURE2
			}
			auto* blurQuarterTexUniform = _renderCommand.GetMaterial().Uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->GetIntValue(0) != 3) {
				blurQuarterTexUniform->SetIntValue(3); // GL_TEXTURE3
			}
		}

		if (_renderCommandWithWater.GetMaterial().SetShader(_owner->_levelHandler->_combineWithWaterShader)) {
			_renderCommandWithWater.GetMaterial().ReserveUniformsDataMemory();
			_renderCommandWithWater.GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
			auto* textureUniform = _renderCommandWithWater.GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
			auto* lightTexUniform = _renderCommandWithWater.GetMaterial().Uniform("uTextureLighting");
			if (lightTexUniform && lightTexUniform->GetIntValue(0) != 1) {
				lightTexUniform->SetIntValue(1); // GL_TEXTURE1
			}
			auto* blurHalfTexUniform = _renderCommandWithWater.GetMaterial().Uniform("uTextureBlurHalf");
			if (blurHalfTexUniform && blurHalfTexUniform->GetIntValue(0) != 2) {
				blurHalfTexUniform->SetIntValue(2); // GL_TEXTURE2
			}
			auto* blurQuarterTexUniform = _renderCommandWithWater.GetMaterial().Uniform("uTextureBlurQuarter");
			if (blurQuarterTexUniform && blurQuarterTexUniform->GetIntValue(0) != 3) {
				blurQuarterTexUniform->SetIntValue(3); // GL_TEXTURE3
			}
			auto* noiseTexUniform = _renderCommandWithWater.GetMaterial().Uniform("uTextureNoise");
			if (noiseTexUniform && noiseTexUniform->GetIntValue(0) != 4) {
				noiseTexUniform->SetIntValue(4); // GL_TEXTURE4
			}
		}

		_renderCommand.SetTransformation(Matrix4x4f::Translation((float)x, (float)y, 0.0f));
		_renderCommandWithWater.SetTransformation(Matrix4x4f::Translation((float)x, (float)y, 0.0f));
#endif
	}

	Rectf CombineRenderer::GetBounds() const
	{
		return _bounds;
	}

	bool CombineRenderer::OnDraw(RenderQueue& renderQueue)
	{
#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
		// Software renderer: the scene was rasterized straight to the screen buffer; bloom stays dropped (too
		// heavy for the CPU), and the underwater shader variant is replaced by a lightweight per-row CPU water
		// effect applied by the device together with the lighting. Neither can be composited here - OnDraw runs
		// in nCine's Visit (queue-building) phase, before the scene is rasterized into the screen buffer, so an
		// in-place combine here would be overwritten when the queue is actually drawn. Instead we build the
		// lightmap now (pure CPU work) and hand it plus the water parameters to the device; the Combine command
		// queued below is dispatched in the Draw phase - after the scene and before the HUD - where the device
		// applies the combine in place.
		if (!PrepareSoftwareLighting()) {
			return false;	// Fully lit with no lights and no water: nothing to composite
		}
		renderQueue.AddCommand(&_renderCommand);
		return true;
#else
		float viewWaterLevel = _owner->_levelHandler->_waterLevel - _owner->_cameraPos.Y + _bounds.H * 0.5f;
		bool viewHasWater = (viewWaterLevel < _bounds.H);
		auto& command = (viewHasWater ? _renderCommandWithWater : _renderCommand);

		command.GetMaterial().SetTexture(0, *_owner->_viewTexture);
		command.GetMaterial().SetTexture(1, *_owner->_lightingBuffer);
		if (PreferencesCache::BlurEffects) {
			command.GetMaterial().SetTexture(2, *_owner->_blurPass2.GetTarget());
			command.GetMaterial().SetTexture(3, *_owner->_blurPass4.GetTarget());
		} else {
			command.GetMaterial().SetTexture(2, nullptr);
			command.GetMaterial().SetTexture(3, nullptr);
		}
		if (viewHasWater && !PreferencesCache::LowWaterQuality) {
			command.GetMaterial().SetTexture(4, *_owner->_levelHandler->_noiseTexture);
		}

		auto* instanceBlock = command.GetMaterial().UniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(_bounds.W, _bounds.H);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

		command.GetMaterial().Uniform("uAmbientColor")->SetFloatVector(_owner->_ambientLight.Data());
		command.GetMaterial().Uniform("uTime")->SetFloatValue(_owner->_levelHandler->_elapsedFrames * 0.0018f);

		if (viewHasWater) {
			command.GetMaterial().Uniform("uWaterLevel")->SetFloatValue(viewWaterLevel / _bounds.H);
			command.GetMaterial().Uniform("uCameraPos")->SetFloatVector(_owner->_cameraPos.Data());
		}

		renderQueue.AddCommand(&command);

		return true;
#endif
	}

#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
	bool CombineRenderer::PrepareSoftwareLighting()
	{
		// Water covers (part of) this viewport when the waterline is above its bottom edge - the same test the
		// shader path uses to select the CombineWithWater variant. The waterline is passed in viewport-local
		// pixels from the top edge (it may be negative when the whole view is underwater), together with the
		// shader's uTime and the camera Y that anchor the wave animation to the world.
		const float viewWaterLevel = _owner->_levelHandler->_waterLevel - _owner->_cameraPos.Y + _bounds.H * 0.5f;
		const bool viewHasWater = (viewWaterLevel < _bounds.H);
		const float waterTime = _owner->_levelHandler->_elapsedFrames * 0.0018f;

		// Collect every active light emitter, exactly like the shader-path LightingRenderer does
		_swLightsCache.clear();
		auto actors = _owner->_levelHandler->GetActors();
		std::size_t actorsCount = actors.size();
		for (std::size_t i = 0; i < actorsCount; i++) {
			actors[i]->OnEmitLights(_swLightsCache);
		}

		// The shader path clears the lighting buffer to (ambientLevel, 0) and blends the scene toward the
		// ambient colour by (1 - light.r). A fully-lit level with no lights therefore leaves the scene
		// untouched, so it can be skipped entirely - this keeps normal levels at full speed. With water in
		// view the combine must still run (a fully-lit water level still tints), just without a lightmap.
		const float ambientLevel = _owner->_ambientLight.W;
		const float ambR = _owner->_ambientLight.X;
		const float ambG = _owner->_ambientLight.Y;
		const float ambB = _owner->_ambientLight.Z;
		const bool fullyLit = (ambientLevel >= 0.999f && _swLightsCache.empty());
		if (fullyLit && !viewHasWater) {
			return false;
		}

		// Viewport rectangle inside the screen buffer, in the same coordinate space the scene was rasterized
		// into. The screen buffer is stored the way SwRaster wrote it (non-FBO path: storeY == py, i.e. top-down
		// in rasterizer space); the final vertical flip happens at present, so no flip is applied - light rows and
		// scene rows already share the same convention. The width/height are left unclamped here; the device
		// clamps them against the actual screen buffer when it applies the combine.
		const std::int32_t vpX = std::max(0, (std::int32_t)_bounds.X);
		const std::int32_t vpY = std::max(0, (std::int32_t)_bounds.Y);
		const std::int32_t vpW = (std::int32_t)_bounds.W;
		const std::int32_t vpH = (std::int32_t)_bounds.H;
		if (vpW <= 0 || vpH <= 0) {
			return false;
		}

		if (fullyLit) {
			// Water-only combine: no lightmap is needed, the device applies just the per-row water effect
			Rhi::Device::SetPendingSoftwareLighting(nullptr, 0, 0, 1, vpX, vpY, vpW, vpH, ambR, ambG, ambB,
				true, viewWaterLevel, waterTime, _owner->_cameraPos.Y);
			return true;
		}

		// A half-resolution lightmap keeps the per-light splat cheap; the combine samples it point-wise
		constexpr std::int32_t Scale = 2;
		const std::int32_t lmW = (vpW + Scale - 1) / Scale;
		const std::int32_t lmH = (vpH + Scale - 1) / Scale;
		const std::size_t texelCount = (std::size_t)lmW * lmH;
		_swLightmap.assign(texelCount * 2, 0.0f);
		// R (intensity) starts at the ambient level everywhere; G (brightness core) starts at zero
		for (std::size_t i = 0; i < texelCount; i++) {
			_swLightmap[i * 2] = ambientLevel;
		}

		// World -> screen pixel mapping of the scene camera (orthographic, unit scale, Y flipped by the
		// projection): col = worldX - camX + vpW/2; row = camY - worldY + vpH/2. The lightmap is viewport-local
		// (the vp origin is dropped) and divided by Scale.
		const float camX = _owner->_cameraPos.X;
		const float camY = _owner->_cameraPos.Y;
		const float halfW = vpW * 0.5f;
		const float halfH = vpH * 0.5f;

		for (const LightEmitter& light : _swLightsCache) {
			const float radiusFar = light.RadiusFar;
			if (radiusFar <= 0.0f) {
				continue;
			}
			const float radiusNearNorm = light.RadiusNear / radiusFar;
			const float denom = (1.0f - radiusNearNorm);
			// Clamp each light's contribution to be non-negative, mirroring the GL/D3D11 lighting path: that
			// buffer is an unsigned RG8 render target and blending clamps the shader's source colour to [0,1]
			// before the additive blend, so a light whose Intensity/Brightness has ramped below zero (e.g. the
			// fading player motion trail) simply adds nothing. Summing the raw negative value here instead would
			// pull the local intensity below the ambient level, darkening the scene into a black trail rather
			// than fading it out.
			const float intensity = std::max(0.0f, light.Intensity);
			const float brightness = std::max(0.0f, light.Brightness);

			const float cx = (light.Pos.X - camX + halfW) / Scale;
			const float cy = (camY - light.Pos.Y + halfH) / Scale;
			const float rLm = radiusFar / Scale;
			if (rLm < 0.5f) {
				continue;
			}

			const std::int32_t x0 = std::max(0, (std::int32_t)(cx - rLm));
			const std::int32_t x1 = std::min(lmW - 1, (std::int32_t)(cx + rLm));
			const std::int32_t y0 = std::max(0, (std::int32_t)(cy - rLm));
			const std::int32_t y1 = std::min(lmH - 1, (std::int32_t)(cy + rLm));

			for (std::int32_t y = y0; y <= y1; y++) {
				const float dy = (y - cy) / rLm;
				float* texelRow = &_swLightmap[((std::size_t)y * lmW + x0) * 2];
				for (std::int32_t x = x0; x <= x1; x++, texelRow += 2) {
					const float dx = (x - cx) / rLm;
					const float dist = std::sqrt(dx * dx + dy * dy);
					if (dist > 1.0f) {
						continue;
					}
					// Cubic falloff, identical to lightBlend() in LightingFs.inc
					float t = (denom > 0.0f ? 1.0f - ((dist - radiusNearNorm) / denom) : 1.0f);
					t = std::clamp(t, 0.0f, 1.0f);
					const float strength = t * t * t;
					texelRow[0] += strength * intensity;
					texelRow[1] += strength * brightness;
				}
			}
		}

		// Hand the finished lightmap, this viewport's rectangle and the water parameters to the software device.
		// The actual in-place combine (lit = main * (1 + g) + max(g - 0.7, 0); out = mix(lit, ambientRGB,
		// clamp(1 - r, 0, 1)), with the water tint/waves applied to the underwater rows first) runs there during
		// the Draw phase, once the scene is in the screen buffer and before the HUD. The lightmap pointer must
		// outlive this call: _swLightmap is a member reused across frames, and the device consumes the entry in
		// the same frame's Draw phase (before the next PrepareSoftwareLighting reassigns it).
		Rhi::Device::SetPendingSoftwareLighting(_swLightmap.data(), lmW, lmH, Scale, vpX, vpY, vpW, vpH, ambR, ambG, ambB,
			viewHasWater, viewWaterLevel, waterTime, _owner->_cameraPos.Y);
		return true;
	}
#endif
}