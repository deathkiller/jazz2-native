#include "CombineRenderer.h"
#include "PlayerViewport.h"
#include "../PreferencesCache.h"

#include "../../nCine/Graphics/RenderQueue.h"

#if defined(DEATH_TARGET_PSP)
#	include <cstring>
#else
#	include "SoftwareLightSplat.h"
#endif

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
		// Software renderer and the console fixed-function tiers: the scene is rasterized straight to the screen
		// buffer and the bloom/blur chain is gated out, so the commands carry no post-processing textures. They
		// bind the compositor program (so the device recognizes the draw and runs the CPU dynamic-lighting
		// combine in its place) and a 4-vertex quad so the draw actually reaches the device in the Draw phase.
		if (_renderCommand.GetMaterial().SetShader(_owner->_levelHandler->_combineShader)) {
			_renderCommand.GetMaterial().ReserveUniformsDataMemory();
			_renderCommand.GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
		}
		// The water variant is a separate program on this tier too, because on the console backends its
		// fixed_function block is what draws the water - the plain Combine has no water description at all. The
		// software backend maps both labels onto SwEffect::Combine and keeps doing its own per-row water, so
		// binding this one there changes nothing.
		if (_renderCommandWithWater.GetMaterial().SetShader(_owner->_levelHandler->_combineWithWaterShader)) {
			_renderCommandWithWater.GetMaterial().ReserveUniformsDataMemory();
			_renderCommandWithWater.GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, 4);
		}
		_renderCommand.SetTransformation(Matrix4x4f::Translation((float)x, (float)y, 0.0f));
		_renderCommandWithWater.SetTransformation(Matrix4x4f::Translation((float)x, (float)y, 0.0f));
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
		// Software renderer and the console fixed-function tiers: the scene was rasterized straight to the
		// screen buffer and bloom stays dropped (too heavy without shaders). The lighting cannot be composited
		// here - OnDraw runs in nCine's Visit (queue-building) phase, before the scene is rasterized into the
		// screen buffer, so an in-place combine here would be overwritten when the queue is actually drawn.
		// Instead we build the lightmap now (pure CPU work) and hand it to the device; the compositor command
		// queued below is dispatched in the Draw phase - after the scene and before the HUD - where the device
		// applies the combine in place. The underwater effect is the device's own: a per-row CPU pass on the
		// software backend, the water program's fixed_function block on the console tiers.
		if (!PrepareSoftwareLighting()) {
			return false;	// Fully lit with no lights and no water: nothing to composite
		}

		// The variant is selected by the SAME test the shader path uses, so which program is bound says whether
		// this viewport has water - on the console tiers that is not cosmetic bookkeeping but the mechanism: the
		// water is a fixed_function block of the water program, and the plain Combine has none. The software
		// backend keeps its own richer per-row water from the queued light either way.
		const float viewWaterLevel = _owner->_levelHandler->_waterLevel - _owner->_cameraPos.Y + _bounds.H * 0.5f;
		const bool viewHasWater = (viewWaterLevel < _bounds.H);
		const bool waterProgramReady = (_owner->_levelHandler->_combineWithWaterShader != nullptr);
		auto& command = (viewHasWater && waterProgramReady ? _renderCommandWithWater : _renderCommand);

		// The block reads the viewport rectangle off its own quad (quad_origin/quad_axis_*), so the instance
		// block has to describe it here exactly as the shader path does - it is the only source those have
		auto* instanceBlock = command.GetMaterial().UniformBlock(Material::InstanceBlockName);
		if (instanceBlock != nullptr) {
			instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
			instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(_bounds.W, _bounds.H);
			instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());
		}
		if (viewHasWater) {
			// Read back by NAME on the console tiers (uniform_float(uWaterLevel) / uniform_vec4(uAmbientColor)),
			// so the water description consumes the shader's own uniforms instead of the fixed-function
			// language growing a facility per game concept. Same values the shader path's fragment stage reads.
			auto* waterLevelUniform = command.GetMaterial().Uniform("uWaterLevel");
			if (waterLevelUniform != nullptr) {
				waterLevelUniform->SetFloatValue(viewWaterLevel / _bounds.H);
			}
			auto* ambientUniform = command.GetMaterial().Uniform("uAmbientColor");
			if (ambientUniform != nullptr) {
				ambientUniform->SetFloatVector(_owner->_ambientLight.Data());
			}
		}

		renderQueue.AddCommand(&command);
		return true;
#else
		float viewWaterLevel = _owner->_levelHandler->_waterLevel - _owner->_cameraPos.Y + _bounds.H * 0.5f;
		bool viewHasWater = (viewWaterLevel < _bounds.H);
		auto& command = (viewHasWater ? _renderCommandWithWater : _renderCommand);

		command.GetMaterial().SetTexture(0, *_owner->_viewTexture);
		command.GetMaterial().SetTexture(1, *_owner->_lightingBuffer);
		if (PreferencesCache::BlurEffects) {
			// The quarter-size level does not exist at a low lighting resolution, and the half-size one takes its place
			command.GetMaterial().SetTexture(2, *_owner->_blurPass2.GetTarget());
			command.GetMaterial().SetTexture(3, *_owner->GetBlurredTarget());
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
#if defined(RHI_LIGHTING_DARKEN_ONLY)
		// A device that can only darken (see RhiFwd.h) needs no lightmap at the full ambient level even with
		// lights in view: every texel starts out fully covered and the lights only ever add to it, so all the
		// factors would come out as exactly 1 - a whole-viewport multiply that leaves every pixel as it was,
		// paid for with a map reset, the splats and the conversion every frame
		const bool fullyLit = (ambientLevel >= 0.999f);
#else
		const bool fullyLit = (ambientLevel >= 0.999f && _swLightsCache.empty());
#endif
		if (fullyLit && !viewHasWater) {
			return false;
		}

		// Viewport rectangle inside the screen buffer, in the same coordinate space the scene was rasterized
		// into: rows counted from the top, like every store (see RhiFwd.h). The width/height are left unclamped
		// here; the device clamps them against the actual screen buffer when it applies the combine.
		const std::int32_t vpX = std::max<std::int32_t>(0, (std::int32_t)_bounds.X);
		const std::int32_t vpY = std::max<std::int32_t>(0, (std::int32_t)_bounds.Y);
		const std::int32_t vpW = (std::int32_t)_bounds.W;
		const std::int32_t vpH = (std::int32_t)_bounds.H;
		if (vpW <= 0 || vpH <= 0) {
			return false;
		}

		if (fullyLit) {
			// Water-only combine: no lightmap is needed, the device applies just the per-row water effect
			RHI::Device::SetPendingSoftwareLighting(nullptr, 0, 0, 1, vpX, vpY, vpW, vpH, ambR, ambG, ambB,
				true, viewWaterLevel, waterTime, _owner->_cameraPos.Y);
			return true;
		}

		// A reduced-resolution lightmap keeps the per-light splat cheap; the combine samples it point-wise
#if defined(DEATH_TARGET_PSP)
		// The PSP takes this one step further than the consoles below, for the same two reasons that put it
		// among them and then one measurement. A sixth of a 480x272 viewport is 80x46 texels - 3680 of them
		// against the 8160 a quarter gives - so both passes over the map cost 45% of what they did.
		//
		// Measured on hardware, that pair (the splat here in the Visit phase and the conversion in the
		// device) was 3.1 ms of a 13 ms CPU frame at a quarter, and the frame has to fit inside one 16.7 ms
		// vblank interval or the console shows it twice - so this was what stood between a level and a
		// locked 60 fps. Nothing in the map has sharp detail to lose: every light is a smooth cubic falloff
		// and the map is stretched over the viewport with bilinear filtering at any scale.
		constexpr std::int32_t Scale = 6;
#elif defined(DEATH_TARGET_3DS)
		// The 3DS sits with the PSP rather than with the consoles below. An Old 3DS / 2DS - which is the
		// model to budget for, since the 804 MHz clock and the L2 cache only exist on a New one - runs its
		// Arm11 at 268 MHz, slower than the Allegrex, and has nothing like the VFPU the PSP splats with, so
		// it pays more per texel than the console that already needed a sixth. Its 400x240 viewport makes a
		// sixth 67x40 = 2680 texels, fewer even than the PSP's 80x46, and the device pass over the padded
		// texture drops with it (see ApplyPendingSoftwareLighting, which converts only the live part).
		constexpr std::int32_t Scale = 6;
#elif defined(DEATH_TARGET_N64)
		// One twelfth: a 27x20 map for the 320x240 output. The 93 MHz CPU resets, splats and (in the
		// device) converts every texel, and the cost is the lit texel count, which made this the most
		// variable item of a frame in the torch-lit castle levels - 6.5 ms of a 48 ms frame at one
		// quarter, and still 2.5 to over 10 ms at one eighth depending on how many lights were in view.
		// The map is stretched over the screen with bilinear filtering and every light is a smooth cubic
		// falloff spanning eight-plus texels even here, so a coarser map only softens the light edges.
		// It also keeps the map under 1.1 KB of IA16, which fits TMEM in one load instead of being
		// blitted in chunks.
		constexpr std::int32_t Scale = 12;
#elif defined(DEATH_TARGET_PS2)
		// A sixth, like the PSP, and for the same reason: the map is converted into a texture and DMAed into
		// video memory every frame on top of the reset and the splat here. The 597x448 viewport made a quarter
		// 150x112 texels, and the device pass over them alone measured 1.8 ms of a 10 ms frame (in PCSX2);
		// a sixth is 100x75, under half of that. A texel then covers six game pixels exactly as on the PSP,
		// and every light still spans dozens of them under bilinear filtering. It also halves the surface
		// in local memory, which the texture cache is short of: 128x128 PSMT8 is two pages and 16 KB a frame
		// across the bus, where the quarter took four pages and 32 KB (and half resolution would take 16).
		constexpr std::int32_t Scale = 6;
#elif defined(DEATH_TARGET_WII) || defined(DEATH_TARGET_GAMECUBE) || defined(DEATH_TARGET_DREAMCAST)
		// The consoles pay for every texel twice on the CPU - once resetting and splatting it here, once
		// converting it into a texture in the device - and that pair of passes was the single largest cost
		// left in the frame. Quarter resolution trades a slightly softer light edge for a quarter of the
		// work; the map is stretched over the viewport with bilinear filtering either way, and the lights
		// themselves are smooth cubic falloffs with nothing sharp to lose.
		constexpr std::int32_t Scale = 4;
#else
		constexpr std::int32_t Scale = 2;
#endif
		const std::int32_t lmW = (vpW + Scale - 1) / Scale;
		const std::int32_t lmH = (vpH + Scale - 1) / Scale;
		const std::size_t texelCount = (std::size_t)lmW * lmH;
		// R (intensity) starts at the ambient level everywhere; G (brightness core) starts at zero. The
		// reset writes both channels in one sequential pass - clearing the whole buffer first and then
		// striding back over it to set R touched every cache line twice for no benefit.
		//
		// A texel the lights did not reach in the last frame still holds exactly that reset, so while the map
		// keeps its size and the ambient level, only the spans they did reach are reset again. A light covers a
		// fraction of the viewport, and the full pass rewrote every texel of the map each frame - as much work as
		// the splats, and on the consoles a whole buffer's worth of cache lines on top.
		const bool resetLitSpansOnly = (_swLitWidth == lmW && _swLitSpans.size() == std::size_t(lmH) * 2 &&
			_swLitAmbient == ambientLevel);
		_swLightmap.resize_for_overwrite(texelCount * 2);
		float* DEATH_RESTRICT lightmap = _swLightmap.data();
		if (resetLitSpansOnly) {
			for (std::int32_t y = 0; y < lmH; y++) {
				float* DEATH_RESTRICT row = lightmap + (std::size_t)y * lmW * 2;
				for (std::int32_t x = _swLitSpans[y * 2]; x <= _swLitSpans[y * 2 + 1]; x++) {
					row[x * 2] = ambientLevel;
					row[x * 2 + 1] = 0.0f;
				}
			}
		} else {
			for (std::size_t i = 0; i < texelCount; i++) {
				lightmap[i * 2] = ambientLevel;
				lightmap[i * 2 + 1] = 0.0f;
			}
			_swLitSpans.resize_for_overwrite(std::size_t(lmH) * 2);
			_swLitWidth = lmW;
			_swLitAmbient = ambientLevel;
		}
		for (std::int32_t y = 0; y < lmH; y++) {
			_swLitSpans[y * 2] = lmW;
			_swLitSpans[y * 2 + 1] = -1;
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
			// A light that has ramped to nothing still covered its whole radius, adding 0.0f to every texel
			// under it - and the clamp above is exactly what produces those: the fading player motion trail
			// ramps its Intensity down through zero and keeps emitting. Dropping them here costs one compare
			// per light and saves the entire splat of each one.
			if (intensity <= 0.0f && brightness <= 0.0f) {
				continue;
			}

			// Multiplying by the reciprocal of a compile-time constant, because dividing by one is not the
			// same thing on this hardware: 1/6 is not exactly representable, so without -ffast-math (off by
			// default here) GCC has to keep a real division, and the Allegrex has a single unpipelined
			// `div.s` of about 29 cycles. These three ran per light, and the band loop below runs the setup
			// once per band a light crosses.
			constexpr float InvScale = 1.0f / Scale;
			const float cx = (light.Pos.X - camX + halfW) * InvScale;
			const float cy = (camY - light.Pos.Y + halfH) * InvScale;
			const float rLm = radiusFar * InvScale;
			if (rLm < 0.5f) {
				continue;
			}

			// The texels a light can change are those within the bounds its splat clamps itself to, which is
			// what the next frame's reset has to cover (see above)
			const std::int32_t litX0 = std::max<std::int32_t>(0, (std::int32_t)(cx - rLm));
			const std::int32_t litX1 = std::min(lmW - 1, (std::int32_t)(cx + rLm));
			if (litX0 <= litX1) {
				const std::int32_t litY0 = std::max<std::int32_t>(0, (std::int32_t)(cy - rLm));
				const std::int32_t litY1 = std::min(lmH - 1, (std::int32_t)(cy + rLm));
				for (std::int32_t y = litY0; y <= litY1; y++) {
					_swLitSpans[y * 2] = std::min(_swLitSpans[y * 2], litX0);
					_swLitSpans[y * 2 + 1] = std::max(_swLitSpans[y * 2 + 1], litX1);
				}
			}

#if defined(DEATH_TARGET_PSP)
			// The PSP splats four texels at a time on the VFPU, the Allegrex's vector unit, which the main
			// thread owns through PSP_MAIN_THREAD_ATTR. A scalar loop here is bound by FPU latency, not by
			// instruction count: every texel is `add -> mul -> add -> compare -> branch` on a single-issue
			// in-order core, and `sqrt.s` costs ~58 cycles and is not pipelined, so unrolling it could not
			// have hidden anything. The vector unit computes one row segment with no branch at all, using the
			// same function of the squared distance: `clamp((1 - dist) * invDenom, 0, 1)` is 1 across the flat
			// core, 0 outside the circle (adding 0 leaves the texel bit-identical), and the cubic falloff
			// between. A light without falloff gets a huge invDenom, which gives the same 1 inside and 0
			// outside. Validated in PPSSPP against the scalar algorithm: 4,000 frames, 74 lights, worst
			// difference 1.4e-6.
			//
			// Register use: C100 = [invDenom, intensity, brightness, 4/rLm], C110 = [0, 1, 2, 3] / rLm,
			// C120 = ones; per row S130 = dx of the first lane, S131 = dy^2; C000 = the four current dx values;
			// C010/C020 temporaries; C200-C230 the eight texel floats (two RG pairs per quad) and their
			// increments. The lightmap rows are only 4-byte aligned, hence the unaligned quad load/store pair.
			const std::int32_t x0 = std::max<std::int32_t>(0, (std::int32_t)(cx - rLm));
			const std::int32_t x1 = std::min(lmW - 1, (std::int32_t)(cx + rLm));
			const std::int32_t y0 = std::max<std::int32_t>(0, (std::int32_t)(cy - rLm));
			const std::int32_t y1 = std::min(lmH - 1, (std::int32_t)(cy + rLm));

			// Both divisions the falloff needs are loop-invariant, so they become reciprocals once per
			// light instead of being issued per texel (a single unpipelined `div.s` of about 29 cycles)
			const float invRLm = 1.0f / rLm;
			const bool hasFalloff = (denom > 0.0f);
			const float invDenom = (hasFalloff ? 1.0f / denom : 0.0f);
			// Squared, so the scalar tail can recognize the light's flat core without a square root
			const float radiusNearNormSq = radiusNearNorm * radiusNearNorm;

			alignas(16) float vfpuConstants[8] = {
				(hasFalloff ? invDenom : 1.0e30f), intensity, brightness, 4.0f * invRLm,
				0.0f, invRLm, 2.0f * invRLm, 3.0f * invRLm
			};
			asm volatile(
				"lv.q C100, 0(%[k])\n\t"
				"lv.q C110, 16(%[k])\n\t"
				"vone.q C120\n\t"
				: : [k] "r"(vfpuConstants) : "memory");

			for (std::int32_t y = y0; y <= y1; y++) {
				const float dy = (y - cy) * invRLm;
				const float dySq = dy * dy;
				float dx = (x0 - cx) * invRLm;
				float* texelRow = &_swLightmap[((std::size_t)y * lmW + x0) * 2];
				std::int32_t x = x0;

				// Whole quads first. A row whose length is not a multiple of four is padded to one when the
				// extra texels still lie inside the lightmap row: they are beyond the light's bounding box, so
				// their distance exceeds 1 and the vector path adds exactly 0 to them. Only a row clipped by
				// the lightmap's right edge leaves a tail for the scalar loop below.
				const std::int32_t count = x1 - x0 + 1;
				const std::int32_t paddedCount = (count + 3) & ~3;
				const std::int32_t quads = (x0 + paddedCount <= lmW ? paddedCount : count) >> 2;
				if (quads > 0) {
					std::uint32_t dxBits, dySqBits;
					std::memcpy(&dxBits, &dx, sizeof(dxBits));
					std::memcpy(&dySqBits, &dySq, sizeof(dySqBits));
					asm volatile(
						"mtv %[dx], S130\n\t"
						"mtv %[dysq], S131\n\t"
						"vadd.q C000, C110, C130[x,x,x,x]\n\t"
						: : [dx] "r"(dxBits), [dysq] "r"(dySqBits));
					for (std::int32_t q = 0; q < quads; q++) {
						asm volatile(
							"vmul.q C010, C000, C000\n\t"					// dx^2
							"vadd.q C010, C010, C130[y,y,y,y]\n\t"			// + dy^2
							"vsqrt.q C010, C010\n\t"							// dist
							"vsub.q C010, C120, C010\n\t"					// 1 - dist
							"vscl.q C010, C010, S100\n\t"					// * invDenom
							"vsat0.q C010, C010\n\t"							// t = clamp(.., 0, 1)
							"vmul.q C020, C010, C010\n\t"
							"vmul.q C010, C020, C010\n\t"					// strength = t^3
							"ulv.q C200, 0(%[row])\n\t"						// [R0 G0 R1 G1]
							"ulv.q C210, 16(%[row])\n\t"						// [R2 G2 R3 G3]
							"vmul.q C220, C010[x,x,y,y], C100[y,z,y,z]\n\t"	// [s0*I s0*B s1*I s1*B]
							"vmul.q C230, C010[z,z,w,w], C100[y,z,y,z]\n\t"	// [s2*I s2*B s3*I s3*B]
							"vadd.q C200, C200, C220\n\t"
							"vadd.q C210, C210, C230\n\t"
							"usv.q C200, 0(%[row])\n\t"
							"usv.q C210, 16(%[row])\n\t"
							"vadd.q C000, C000, C100[w,w,w,w]\n\t"			// dx += 4/rLm
							: : [row] "r"(texelRow) : "memory");
						texelRow += 8;
					}
					x += quads * 4;
					dx += (float)(quads * 4) * invRLm;
				}

				// Scalar tail: the same falloff, `lightBlend()` in LightingFs.inc, with the flat core and the
				// outside recognized on the squared distance so they never pay for the square root
				for (; x <= x1; x++, texelRow += 2, dx += invRLm) {
					const float dist2 = dx * dx + dySq;
					if (dist2 > 1.0f) {
						continue;
					}
					if (dist2 <= radiusNearNormSq) {
						texelRow[0] += intensity;
						texelRow[1] += brightness;
						continue;
					}
					const float t = std::min((1.0f - std::sqrt(dist2)) * invDenom, 1.0f);
					const float strength = t * t * t;
					texelRow[0] += strength * intensity;
					texelRow[1] += strength * brightness;
				}
			}
#else
			// Every other console: the row-segmented scalar splat, with the platform's cheapest square root
			SoftwareLighting::SplatLight(lightmap, lmW, lmH, cx, cy, rLm, radiusNearNorm, intensity, brightness);
#endif
		}

		// Hand the finished lightmap, this viewport's rectangle and the water parameters to the software device.
		// The actual in-place combine (lit = main * (1 + g) + max(g - 0.7, 0); out = mix(lit, ambientRGB,
		// clamp(1 - r, 0, 1)), with the water tint/waves applied to the underwater rows first) runs there during
		// the Draw phase, once the scene is in the screen buffer and before the HUD. The lightmap pointer must
		// outlive this call: _swLightmap is a member reused across frames, and the device consumes the entry in
		// the same frame's Draw phase (before the next PrepareSoftwareLighting reassigns it).
		RHI::Device::SetPendingSoftwareLighting(_swLightmap.data(), lmW, lmH, Scale, vpX, vpY, vpW, vpH, ambR, ambG, ambB,
			viewHasWater, viewWaterLevel, waterTime, _owner->_cameraPos.Y);
		return true;
	}
#endif
}