#include "CombineRenderer.h"
#include "PlayerViewport.h"
#include "../PreferencesCache.h"

#include "../../nCine/Graphics/RenderQueue.h"

#include "../../nCine/Graphics/RHI/RHI.h"
#if !defined(RHI_CAP_SHADERS)
#	include "../../nCine/Graphics/RHI/SW/TileRenderer.h"
#endif

#include <cmath>
#include <algorithm>

namespace Jazz2::Rendering
{
#if !defined(RHI_CAP_SHADERS)
	void CombineRenderer::ApplyLightingPostPass()
	{
		// Flush any pending tile renderer commands before modifying the buffer
		RHI::TileRenderer::Flush();

		std::uint8_t* fb = RHI::GetMutableColorBuffer();
		const std::int32_t fbW = RHI::GetColorBufferWidth();
		const std::int32_t fbH = RHI::GetColorBufferHeight();
		if (fb == nullptr || fbW <= 0 || fbH <= 0) return;

		auto* lightTex = _owner->_lightingBuffer.get();
		if (lightTex == nullptr) return;
		const std::uint8_t* lightPixels = lightTex->GetPixels(0);
		if (lightPixels == nullptr) return;
		const std::int32_t lightW = lightTex->GetWidth();
		const std::int32_t lightH = lightTex->GetHeight();
		if (lightW <= 0 || lightH <= 0) return;

		const float ambientR = _owner->_ambientLight.X;
		const float ambientG = _owner->_ambientLight.Y;
		const float ambientB = _owner->_ambientLight.Z;
		const float ambientW = _owner->_ambientLight.W;
		const float invDarknessDiv = 1.0f / std::sqrt(std::max(ambientW, 0.35f));

		// Fixed-point scale factors for lighting texture lookup (16.16)
		const std::uint32_t lightScaleX_fp = static_cast<std::uint32_t>((static_cast<std::int64_t>(lightW) << 16) / std::max(fbW, 1));
		const std::uint32_t lightScaleY_fp = static_cast<std::uint32_t>((static_cast<std::int64_t>(lightH) << 16) / std::max(fbH, 1));
		const bool needsBilinear = (lightW != fbW || lightH != fbH);

		// Pre-compute integer ambient values (scaled by 256 for fixed-point blending)
		const std::int32_t ambR256 = static_cast<std::int32_t>(ambientR * 255.0f * 256.0f);
		const std::int32_t ambG256 = static_cast<std::int32_t>(ambientG * 255.0f * 256.0f);
		const std::int32_t ambB256 = static_cast<std::int32_t>(ambientB * 255.0f * 256.0f);

		for (std::int32_t y = 0; y < fbH; y++) {
			std::uint8_t* row = fb + y * fbW * 4;
			// Lighting buffer is bottom-up (FBO), so flip Y for lookup
			const std::int32_t lightY = fbH - 1 - y;

			for (std::int32_t x = 0; x < fbW; x++) {
				std::uint8_t* px = row + x * 4;

				// Sample lighting texture
				std::int32_t lightIntensity, lightBrightness;
				if (!needsBilinear) {
					const std::int32_t lx = std::min(x, lightW - 1);
					const std::int32_t ly = std::min(lightY, lightH - 1);
					const std::uint8_t* lp = lightPixels + (ly * lightW + lx) * 4;
					lightIntensity = lp[0];
					lightBrightness = lp[1];
				} else {
					std::int32_t fx16 = static_cast<std::int32_t>(static_cast<std::int64_t>(x) * lightScaleX_fp) - 0x8000;
					std::int32_t fy16 = static_cast<std::int32_t>(static_cast<std::int64_t>(lightY) * lightScaleY_fp) - 0x8000;
					if (fx16 < 0) fx16 = 0;
					if (fy16 < 0) fy16 = 0;

					std::int32_t x0 = fx16 >> 16;
					std::int32_t y0 = fy16 >> 16;
					std::int32_t fracX = (fx16 >> 8) & 0xFF;
					std::int32_t fracY = (fy16 >> 8) & 0xFF;

					std::int32_t x1 = x0 + 1;
					std::int32_t y1 = y0 + 1;
					if (x0 >= lightW) x0 = lightW - 1;
					if (x1 >= lightW) x1 = lightW - 1;
					if (y0 >= lightH) y0 = lightH - 1;
					if (y1 >= lightH) y1 = lightH - 1;

					const std::uint8_t* p00 = lightPixels + (y0 * lightW + x0) * 4;
					const std::uint8_t* p10 = lightPixels + (y0 * lightW + x1) * 4;
					const std::uint8_t* p01 = lightPixels + (y1 * lightW + x0) * 4;
					const std::uint8_t* p11 = lightPixels + (y1 * lightW + x1) * 4;

					std::int32_t ifx = 256 - fracX;
					std::int32_t ify = 256 - fracY;
					std::int32_t w00 = ifx * ify;
					std::int32_t w10 = fracX * ify;
					std::int32_t w01 = ifx * fracY;
					std::int32_t w11 = fracX * fracY;

					lightIntensity = (p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11) >> 16;
					lightBrightness = (p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11) >> 16;
				}

				std::int32_t mainR = px[0];
				std::int32_t mainG = px[1];
				std::int32_t mainB = px[2];

				// Brightness boost
				std::int32_t brightnessBoost = std::max(lightBrightness - 179, 0);
				std::int32_t litR = mainR + ((mainR * lightBrightness) >> 8) + brightnessBoost;
				std::int32_t litG = mainG + ((mainG * lightBrightness) >> 8) + brightnessBoost;
				std::int32_t litB = mainB + ((mainB * lightBrightness) >> 8) + brightnessBoost;

				// Darkness
				float darknessFrac = static_cast<float>(255 - lightIntensity) * invDarknessDiv * (1.0f / 255.0f);
				if (darknessFrac > 1.0f) darknessFrac = 1.0f;
				else if (darknessFrac < 0.0f) darknessFrac = 0.0f;
				std::int32_t darkMul = static_cast<std::int32_t>((1.0f - darknessFrac) * 256.0f);

				std::int32_t darkenedR = (litR * darkMul) >> 8;
				std::int32_t darkenedG = (litG * darkMul) >> 8;
				std::int32_t darkenedB = (litB * darkMul) >> 8;

				// Ambient blend
				float ambientStrength = static_cast<float>(255 - lightIntensity) * (1.0f / 255.0f);
				std::int32_t ambMul = static_cast<std::int32_t>(ambientStrength * 256.0f);
				std::int32_t invAmbMul = 256 - ambMul;

				std::int32_t outR = (darkenedR * invAmbMul + ambR256 * ambMul / 256) >> 8;
				std::int32_t outG = (darkenedG * invAmbMul + ambG256 * ambMul / 256) >> 8;
				std::int32_t outB = (darkenedB * invAmbMul + ambB256 * ambMul / 256) >> 8;

				px[0] = static_cast<std::uint8_t>(outR > 255 ? 255 : (outR < 0 ? 0 : outR));
				px[1] = static_cast<std::uint8_t>(outG > 255 ? 255 : (outG < 0 ? 0 : outG));
				px[2] = static_cast<std::uint8_t>(outB > 255 ? 255 : (outB < 0 ? 0 : outB));
			}
		}
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
		// SW path: scene already rendered directly to screen buffer.
		// Apply lighting as an in-place post-pass.
		//ApplyLightingPostPass();
		return false;
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