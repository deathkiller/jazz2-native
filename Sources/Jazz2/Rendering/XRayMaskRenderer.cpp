#include "XRayMaskRenderer.h"

#if defined(TILEMAP_LAYER_EFFECTS)

#include "PlayerViewport.h"
#include "../ContentResolver.h"

#include "../../nCine/tracy.h"
#include "../../nCine/Graphics/RenderResources.h"

namespace Jazz2::Rendering
{
	namespace
	{
		// The interleaved per-vertex format of the tile-layer meshes (position.xy, texcoords.xy, color.rgba), see
		// ContentResolver::CompileLayerEffectShaders(). The color carries the palette offset in .r and the alpha in .a
		constexpr std::uint32_t FloatsPerVertex = 8;
		constexpr std::uint32_t FloatsPerQuad = RenderResources::VerticesPerQuad * FloatsPerVertex;
	}

	XRayMaskRenderer::XRayMaskRenderer(PlayerViewport* owner)
		: _owner(owner)
	{
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}

	bool XRayMaskRenderer::OnDraw(RenderQueue& renderQueue)
	{
		ZoneScopedC(0x4876AF);

		// Reported by the tile map while the scene of the previous frame was visited - the scene is visited after
		// this pass, as it's drawn after it. The mask is needed only while an x-ray tile is on screen.
		auto& target = _owner->_layerEffects;
		const std::uint16_t depth = target.XRayDepth;
		target.XRayDepth = 0;

		auto& resolver = ContentResolver::Get();
		Shader* shader = resolver.GetShader(PrecompiledShader::Silhouette);
		Shader* shaderPalette = resolver.GetShader(PrecompiledShader::SilhouettePalette);
		if (depth == 0 || shader == nullptr || shaderPalette == nullptr) {
			// Nothing samples the buffer this frame, so it isn't even cleared
			target.XRayMask = nullptr;
			_owner->_xrayMaskView->SetClearMode(Viewport::ClearMode::Never);
			return true;
		}

		target.XRayMask = _owner->_xrayMaskBuffer.get();
		_owner->_xrayMaskView->SetClearMode(Viewport::ClearMode::EveryFrame);

		for (const auto& group : _groups) {
			_vertices[group.VerticesIndex].clear();
		}
		_groups.clear();

		const Rectf cullingRect = RenderResources::GetCurrentViewport()->GetCullingRect();
		auto actors = _owner->_levelHandler->GetActors();
		for (const auto& actor : actors) {
			const auto& renderer = actor->_renderer;
			// Only what is behind the frontmost x-ray layer shows through it
			if (!renderer.isDrawEnabled() || renderer.parent() == nullptr || renderer.absLayer() >= depth) {
				continue;
			}
			const Texture* texture = renderer.texture();
			const float alpha = renderer.absAlpha();
			if (texture == nullptr || alpha <= 0.0f || !renderer.aabb().Overlaps(cullingRect)) {
				continue;
			}

			const Vector2i texSize = texture->GetSize();
			if (texSize.X <= 0 || texSize.Y <= 0) {
				continue;
			}
			// An indexed texture is transparent where its palette is, which only the palette variant can tell
			const bool indexed = (texture->GetChannelCount() <= 2);

			std::int32_t verticesIndex = -1;
			for (const auto& group : _groups) {
				if (group.DiffuseTexture == texture && group.Indexed == indexed) {
					verticesIndex = group.VerticesIndex;
					break;
				}
			}
			if (verticesIndex < 0) {
				verticesIndex = (std::int32_t)_groups.size();
				if (verticesIndex >= (std::int32_t)_vertices.size()) {
					_vertices.emplace_back();
				}
				_groups.push_back({ texture, indexed, verticesIndex });
			}

			// The quad the sprite vertex stage synthesizes (see the canvas template in ShaderParser.cpp): the
			// corners of the sprite's size through its model matrix, the flips already folded into the texture
			// rectangle. Written out column-major, as the vertex stage multiplies them
			const Matrix4x4f& m = renderer.worldMatrix();
			const float w = renderer.width(), h = renderer.height();
			const Recti texRect = renderer.texRect();
			const float texScaleX = texRect.W / float(texSize.X), texBiasX = texRect.X / float(texSize.X);
			const float texScaleY = texRect.H / float(texSize.Y), texBiasY = texRect.Y / float(texSize.Y);
			const float paletteOffset = renderer.paletteOffset();

			auto& vertices = _vertices[verticesIndex];
			const std::size_t base = vertices.size();
			vertices.resize_for_overwrite(base + FloatsPerQuad);
			float* v = vertices.data() + base;
			auto put = [&](float cx, float cy) {
				const float lx = cx * w, ly = cy * h;
				*v++ = m[0][0] * lx + m[1][0] * ly + m[3][0];
				*v++ = m[0][1] * lx + m[1][1] * ly + m[3][1];
				*v++ = cx * texScaleX + texBiasX;
				*v++ = cy * texScaleY + texBiasY;
				*v++ = paletteOffset; *v++ = 0.0f; *v++ = 0.0f; *v++ = alpha;
			};
			// The four corners in the order of the shared quad index pattern (see RenderResources::GetQuadIndices())
			put(0.0f, 0.0f);
			put(1.0f, 0.0f);
			put(1.0f, 1.0f);
			put(0.0f, 1.0f);
		}

		const std::uint32_t maxQuadsPerChunk = RenderResources::GetMaxQuadsPerDraw(FloatsPerVertex);
		const std::uint16_t* indices = RenderResources::GetQuadIndices();
		Texture* paletteTexture = resolver.GetPaletteTexture();

		std::int32_t commandIndex = 0;
		for (const auto& group : _groups) {
			auto& vertices = _vertices[group.VerticesIndex];
			const std::uint32_t totalQuads = (std::uint32_t)(vertices.size() / FloatsPerQuad);
			for (std::uint32_t firstQuad = 0; firstQuad < totalQuads; firstQuad += maxQuadsPerChunk) {
				const std::uint32_t count = std::min(maxQuadsPerChunk, totalQuads - firstQuad);

				RenderCommand* command = RentRenderCommand(commandIndex++, group.Indexed ? shaderPalette : shader);
				// Corners are in world space already. Re-set every frame, because committing it also folds in the
				// depth of the command's layer, derived from the camera's clip planes rebuilt on every resize
				command->SetTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
				command->GetMaterial().SetTexture(0, *group.DiffuseTexture);
				if (group.Indexed && paletteTexture != nullptr) {
					command->GetMaterial().SetTexture(1, *paletteTexture);
				}

				auto& geometry = command->GetGeometry();
				geometry.SetElementsPerVertex(FloatsPerVertex);
				geometry.SetHostVertexPointer(vertices.data() + firstQuad * FloatsPerQuad);
				geometry.SetIndexCount(count * RenderResources::IndicesPerQuad);
				geometry.SetHostIndexPointer(indices);
				geometry.SetDrawParameters(PrimitiveType::Triangles, 0, count * RenderResources::VerticesPerQuad);

				renderQueue.AddCommand(command);
			}
		}

		return true;
	}

	RenderCommand* XRayMaskRenderer::RentRenderCommand(std::int32_t index, Shader* shader)
	{
		if (index >= (std::int32_t)_renderCommands.size()) {
			auto& newCommand = _renderCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Sprite));
			newCommand->GetMaterial().SetBlendingEnabled(true);
			// Premultiplied white of the sprite's alpha, so overlapping sprites add up to their union
			newCommand->GetMaterial().SetBlendingFactors(BlendingFactor::One, BlendingFactor::OneMinusSrcAlpha);
		}

		RenderCommand* command = _renderCommands[index].get();
		if (command->GetMaterial().SetShader(shader)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform != nullptr) {
				textureUniform->SetIntValue(0);
			}
			auto* paletteUniform = command->GetMaterial().Uniform("uTexturePalette");
			if (paletteUniform != nullptr) {
				paletteUniform->SetIntValue(1);
			}
			// Unused by the shader, which takes everything from the vertex stream, but the instance block is shared
			// with the tile-layer meshes and a stale value would be visible if that ever changed
			auto* instanceBlock = command->GetInstanceBlock();
			if (instanceBlock != nullptr) {
				auto* colorUniform = instanceBlock->GetUniform(Material::ColorUniformName);
				if (colorUniform != nullptr) {
					colorUniform->SetFloatValue(1.0f, 1.0f, 1.0f, 1.0f);
				}
			}
		}
		return command;
	}
}

#endif
