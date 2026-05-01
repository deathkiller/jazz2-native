#include "CombineRenderer.h"
#include "PlayerViewport.h"
#include "../PreferencesCache.h"

#include "../../nCine/Graphics/RenderQueue.h"

#include <cmath>
#include <algorithm>

namespace Jazz2::Rendering
{
#if !defined(RHI_CAP_SHADERS)
	/// Samples a pixel from a texture at integer coordinates, clamped to bounds
	static inline void SampleTexel(const RHI::Texture* tex, std::int32_t x, std::int32_t y, std::uint8_t out[4])
	{
		const std::uint8_t* pixels = tex->GetPixels(0);
		std::int32_t w = tex->GetWidth();
		std::int32_t h = tex->GetHeight();
		if (x < 0) x = 0; else if (x >= w) x = w - 1;
		if (y < 0) y = 0; else if (y >= h) y = h - 1;
		const std::uint8_t* src = pixels + (y * w + x) * 4;
		out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; out[3] = src[3];
	}

	/// SW fragment shader implementing the combine pass (lighting + water)
	static void FragmentCombine(const RHI::FragmentShaderInput& input)
	{
		std::uint8_t* rgba = input.rgba;
		auto* data = static_cast<const CombineShaderData*>(input.userData);

		float yNorm = input.v;
		bool isBelowWater = data->hasWater && (yNorm > data->waterLevelNorm);

		std::int32_t mainR = rgba[0];
		std::int32_t mainG = rgba[1];
		std::int32_t mainB = rgba[2];

		// Water displacement: re-sample view texture at displaced X
		if (isBelowWater) {
			float invW = 1.0f / (float)input.texWidth;
			float uvWorldY = yNorm + data->camY * invW;
			float offset = 0.008f * std::sin(data->time * 16.0f + uvWorldY * 20.0f);
			std::int32_t srcX = (std::int32_t)((float)input.x + offset * (float)input.texWidth + 0.5f);
			const RHI::Texture* viewTex = input.textures[0];
			if (viewTex != nullptr) {
				std::uint8_t displaced[4];
				SampleTexel(viewTex, srcX, input.texHeight - input.y - 1, displaced);
				mainR = displaced[0];
				mainG = displaced[1];
				mainB = displaced[2];
			}
		}

		// Sample lighting texture using pre-computed data from CombineShaderData
		std::int32_t lightIntensity, lightBrightness;  // 0-255 range
		if (!data->lightNeedsBilinear) {
			// Same resolution: direct lookup (fast path)
			std::int32_t lx = input.x;
			std::int32_t ly = data->lightH - input.y - 1;
			if (lx < 0) lx = 0; else if (lx >= data->lightW) lx = data->lightW - 1;
			if (ly < 0) ly = 0; else if (ly >= data->lightH) ly = data->lightH - 1;
			const std::uint8_t* lp = data->lightPixels + (ly * data->lightW + lx) * 4;
			lightIntensity = lp[0];
			lightBrightness = lp[1];
		} else {
			// Fixed-point bilinear interpolation (8.8 weights)
			// Map view pixel to lighting texel coordinates using 16.16 fixed-point scale
			std::int32_t fxRaw = (std::int32_t)((std::int64_t)input.x * data->lightScaleX_fp >> 16);
			std::int32_t fyRaw = (std::int32_t)((std::int64_t)(input.texHeight - 1 - input.y) * data->lightScaleY_fp >> 16);

			// Split into integer + 8-bit fraction (mapping half-texel offset)
			std::int32_t fx16 = (std::int32_t)((std::int64_t)input.x * data->lightScaleX_fp) - 0x8000;
			std::int32_t fy16 = (std::int32_t)((std::int64_t)(input.texHeight - 1 - input.y) * data->lightScaleY_fp) - 0x8000;

			std::int32_t x0 = fx16 >> 16;
			std::int32_t y0 = fy16 >> 16;
			std::int32_t fracX = (fx16 >> 8) & 0xFF;  // 8-bit fraction
			std::int32_t fracY = (fy16 >> 8) & 0xFF;

			if (fx16 < 0) { x0 = 0; fracX = 0; }
			if (fy16 < 0) { y0 = 0; fracY = 0; }

			std::int32_t x1 = x0 + 1;
			std::int32_t y1 = y0 + 1;
			std::int32_t lw = data->lightW;
			std::int32_t lh = data->lightH;
			if (x0 >= lw) x0 = lw - 1;
			if (x1 >= lw) x1 = lw - 1;
			if (y0 >= lh) y0 = lh - 1;
			if (y1 >= lh) y1 = lh - 1;

			const std::uint8_t* p00 = data->lightPixels + (y0 * lw + x0) * 4;
			const std::uint8_t* p10 = data->lightPixels + (y0 * lw + x1) * 4;
			const std::uint8_t* p01 = data->lightPixels + (y1 * lw + x0) * 4;
			const std::uint8_t* p11 = data->lightPixels + (y1 * lw + x1) * 4;

			// Bilinear blend using 8.8 fixed-point weights
			std::int32_t ifx = 256 - fracX;
			std::int32_t ify = 256 - fracY;
			std::int32_t w00 = ifx * ify;  // max 65536
			std::int32_t w10 = fracX * ify;
			std::int32_t w01 = ifx * fracY;
			std::int32_t w11 = fracX * fracY;

			lightIntensity = (p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11) >> 16;
			lightBrightness = (p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11) >> 16;
		}

		// Water color tinting (integer math, 0-255 range)
		if (isBelowWater) {
			// main * 0.7 + tint * 0.3 (using 179/256 and 77/256 approximations)
			mainR = (mainR * 179 + 102 * 77) >> 8;  // tint = 0.4*255 = 102
			mainG = (mainG * 179 + 153 * 77) >> 8;  // tint = 0.6*255 = 153
			mainB = (mainB * 179 + 204 * 77) >> 8;  // tint = 0.8*255 = 204

			float invH = 1.0f / (float)input.texHeight;
			float topDist = std::abs(yNorm - data->waterLevelNorm);
			if (topDist < invH * 2.0f) {
				mainR = std::min(mainR + 51, 255);  // +0.2*255
				mainG = std::min(mainG + 51, 255);
				mainB = std::min(mainB + 51, 255);
			}
		}

		// Combine formula (integer-optimized):
		// lit = main * (1 + brightness/255) + max(brightness - 179, 0)  [179 = 0.7*255]
		// Using: lit = main + main*brightness/256 + max(brightness-179, 0)
		std::int32_t brightnessBoost = std::max(lightBrightness - 179, 0);
		std::int32_t litR = mainR + ((mainR * lightBrightness) >> 8) + brightnessBoost;
		std::int32_t litG = mainG + ((mainG * lightBrightness) >> 8) + brightnessBoost;
		std::int32_t litB = mainB + ((mainB * lightBrightness) >> 8) + brightnessBoost;

		// Darkness: darken lit by (1-intensity) scaled by invDarknessDiv
		// darkness = clamp((255 - lightIntensity) * invDarknessDiv / 255, 0, 1)
		float darknessFrac = (float)(255 - lightIntensity) * data->invDarknessDiv * (1.0f / 255.0f);
		if (darknessFrac > 1.0f) darknessFrac = 1.0f;
		else if (darknessFrac < 0.0f) darknessFrac = 0.0f;
		std::int32_t darkMul = (std::int32_t)((1.0f - darknessFrac) * 256.0f);  // 0..256

		std::int32_t darkenedR = (litR * darkMul) >> 8;
		std::int32_t darkenedG = (litG * darkMul) >> 8;
		std::int32_t darkenedB = (litB * darkMul) >> 8;

		// Ambient blend: out = darkened * (1-ambientStrength) + ambient * ambientStrength
		float ambientStrength = (float)(255 - lightIntensity) * (1.0f / 255.0f);
		if (data->hasWater && data->waterLevelNorm < 0.4f && !isBelowWater) {
			ambientStrength = std::min(1.0f, ambientStrength + 0.4f - data->waterLevelNorm);
		}

		std::int32_t ambMul = (std::int32_t)(ambientStrength * 256.0f);  // 0..256
		std::int32_t invAmbMul = 256 - ambMul;
		// Pre-scaled ambient (0..255 * 256 = up to 65280)
		std::int32_t ambR256 = (std::int32_t)(data->ambientR * 255.0f * 256.0f);
		std::int32_t ambG256 = (std::int32_t)(data->ambientG * 255.0f * 256.0f);
		std::int32_t ambB256 = (std::int32_t)(data->ambientB * 255.0f * 256.0f);

		std::int32_t outR = (darkenedR * invAmbMul + ambR256 * ambMul / 256) >> 8;
		std::int32_t outG = (darkenedG * invAmbMul + ambG256 * ambMul / 256) >> 8;
		std::int32_t outB = (darkenedB * invAmbMul + ambB256 * ambMul / 256) >> 8;

		rgba[0] = (std::uint8_t)(outR > 255 ? 255 : (outR < 0 ? 0 : outR));
		rgba[1] = (std::uint8_t)(outG > 255 ? 255 : (outG < 0 ? 0 : outG));
		rgba[2] = (std::uint8_t)(outB > 255 ? 255 : (outB < 0 ? 0 : outB));
		rgba[3] = 255;
	}
#endif

	CombineRenderer::CombineRenderer(PlayerViewport* owner)
		: _owner(owner)
	{
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}
		
	void CombineRenderer::Initialize(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height)
	{
		_bounds = Rectf(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));

#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
		// Blur post-processing requires shader support and framebuffers
		_renderCommand.SetTransformation(Matrix4x4f::Translation((float)x, (float)y, 0.0f));
#else
		if (_renderCommand.GetMaterial().SetShader(_owner->_levelHandler->_combineShader)) {
			_renderCommand.GetMaterial().ReserveUniformsDataMemory();
			_renderCommand.GetGeometry().SetDrawParameters(RHI::PrimitiveType::TriangleStrip, 0, 4);
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
			_renderCommandWithWater.GetGeometry().SetDrawParameters(RHI::PrimitiveType::TriangleStrip, 0, 4);
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
		// SW fallback: set up blit with extended fragment shader for lighting+water combine
		if (_renderCommand.GetMaterial().SetShaderProgramType(Material::ShaderProgramType::Sprite)) {
			_renderCommand.GetMaterial().ReserveUniformsDataMemory();
			_renderCommand.GetGeometry().SetDrawParameters(RHI::PrimitiveType::TriangleStrip, 0, 4);
		}
		_renderCommand.GetMaterial().SetInstTexRect(1.0f, 0.0f, 1.0f, 0.0f);
		_renderCommand.GetMaterial().SetInstSpriteSize(_bounds.W, _bounds.H);
		_renderCommand.GetMaterial().SetInstColor(1.0f, 1.0f, 1.0f, 1.0f);
		_renderCommand.GetMaterial().SetTexture(*_owner->_viewTexture);
		_renderCommand.GetMaterial().SetTexture(1, *_owner->_lightingBuffer);

		// Populate combine shader uniform data
		float viewWaterLevel = _owner->_levelHandler->_waterLevel - _owner->_cameraPos.Y + _bounds.H * 0.5f;
		_combineData.ambientR = _owner->_ambientLight.X;
		_combineData.ambientG = _owner->_ambientLight.Y;
		_combineData.ambientB = _owner->_ambientLight.Z;
		_combineData.ambientW = _owner->_ambientLight.W;
		_combineData.invDarknessDiv = 1.0f / std::sqrt(std::max(_combineData.ambientW, 0.35f));
		_combineData.hasWater = (viewWaterLevel < _bounds.H);
		_combineData.waterLevelNorm = viewWaterLevel / _bounds.H;
		_combineData.time = _owner->_levelHandler->_elapsedFrames * 0.0018f;
		_combineData.camY = _owner->_cameraPos.Y;

		// Pre-compute lighting texture info to avoid per-pixel virtual calls
		auto* lightTex = _owner->_lightingBuffer.get();
		_combineData.lightPixels = lightTex->GetPixels(0);
		_combineData.lightW = lightTex->GetWidth();
		_combineData.lightH = lightTex->GetHeight();
		std::int32_t viewW = _owner->_viewTexture->GetWidth();
		std::int32_t viewH = _owner->_viewTexture->GetHeight();
		_combineData.lightNeedsBilinear = (_combineData.lightW != viewW || _combineData.lightH != viewH);
		_combineData.lightScaleX_fp = (std::uint32_t)(((std::int64_t)_combineData.lightW << 16) / std::max(viewW, 1));
		_combineData.lightScaleY_fp = (std::uint32_t)(((std::int64_t)_combineData.lightH << 16) / std::max(viewH, 1));

		_renderCommand.GetMaterial().SetFragmentShader(FragmentCombine, &_combineData);
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
}