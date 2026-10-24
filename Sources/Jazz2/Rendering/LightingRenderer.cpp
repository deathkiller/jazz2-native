#include "LightingRenderer.h"
#include "PlayerViewport.h"

#include "../../nCine/Base/Algorithms.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/RenderResources.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Rendering
{
#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
	namespace
	{
		// Interleaved per-vertex format shared with the tile-layer meshes (position.xy, texcoords.xy, color.rgba),
		// so the mesh shaders declare the same attributes - see ContentResolver::CompileShaders()
		constexpr std::uint32_t FloatsPerVertex = 8;
		// A light goes out as its four distinct corners plus the shared two-triangle index pattern, instead of
		// the six vertices the same triangles need without indices
		constexpr std::uint32_t FloatsPerQuad = RenderResources::VerticesPerQuad * FloatsPerVertex;
	}
#endif

	LightingRenderer::LightingRenderer(PlayerViewport* owner)
		: _owner(owner)
	{
		_emittedLightsCache.reserve(32);
		setVisitOrderState(SceneNode::VisitOrderState::Disabled);
	}

	bool LightingRenderer::OnDraw(RenderQueue& renderQueue)
	{
#if !defined(RHI_CAP_SHADERS) || !defined(RHI_CAP_FRAMEBUFFERS)
		// Dynamic lighting is composited by a full-screen shader pass into an off-screen buffer, which the
		// combine shader later applies; on backends without cheap programmable shaders (the software renderer)
		// there is no combine pass, so the scene is drawn without dynamic lighting
		return true;
#else
		_emittedLightsCache.clear();

		// Collect all active light emitters
		auto actors = _owner->_levelHandler->GetActors();
		std::size_t actorsCount = actors.size();
		for (std::size_t i = 0; i < actorsCount; i++) {
			actors[i]->EmitLights(_emittedLightsCache);
		}

		// Every actor in the level emits, wherever it is, and in splitscreen each viewport collects the same set,
		// so a light whose circle cannot reach this view is dropped before it costs any geometry. Nothing else
		// culls them: the render queue only culls drawable nodes, and these are raw commands.
		const Rectf cullingRect = RenderResources::GetCurrentViewport()->GetCullingRect();
		const float cullMinX = cullingRect.X, cullMaxX = cullingRect.X + cullingRect.W;
		const float cullMinY = cullingRect.Y, cullMaxY = cullingRect.Y + cullingRect.H;

		_vertices.clear();
		for (auto& light : _emittedLightsCache) {
			// Skip lights that cover no pixels (a zero far radius would also divide by zero below) or add nothing
			// (the lighting buffer is unsigned, so blending clamps a non-positive contribution to zero)
			if (light.RadiusFar <= 0.0f || (light.Intensity <= 0.0f && light.Brightness <= 0.0f)) {
				continue;
			}
			const float extent = light.RingRadius + light.RadiusFar;
			if (light.Pos.X + extent <= cullMinX || light.Pos.X - extent >= cullMaxX ||
				light.Pos.Y + extent <= cullMinY || light.Pos.Y - extent >= cullMaxY) {
				continue;
			}

			if (light.RingRadius > 0.0f) {
				AppendRingQuads(light, cullingRect);
			} else {
				AppendLightQuad(light);
			}
		}

		if (_vertices.empty()) {
			return true;
		}

		// Cap quads per command to what one indexed draw can reach through the shared streaming buffers, which
		// the buffers manager knows at runtime rather than this being a fixed size. Splitting on whole quads
		// keeps every chunk describable by the same shared index pattern - each segment of a ring light is a
		// quad of its own, so a ring may well be split between two chunks.
		const std::uint32_t maxQuadsPerChunk = RenderResources::GetMaxQuadsPerDraw(FloatsPerVertex);
		// Indices count from the chunk's own first vertex, which the base vertex of the draw supplies, so every
		// chunk reads the shared array from its start
		const std::uint16_t* indices = RenderResources::GetQuadIndices();

		const std::uint32_t totalQuads = (std::uint32_t)(_vertices.size() / FloatsPerQuad);
		std::int32_t commandIndex = 0;
		for (std::uint32_t firstQuad = 0; firstQuad < totalQuads; firstQuad += maxQuadsPerChunk) {
			const std::uint32_t count = std::min(maxQuadsPerChunk, totalQuads - firstQuad);

			RenderCommand* command = RentRenderCommand(commandIndex++);

			// Vertex positions are already in world space, so the model matrix is identity. Re-set every frame
			// because committing it also folds in the depth of the command's layer, which is derived from the
			// camera's clip planes - those are rebuilt whenever the viewport is resized.
			command->SetTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));

			auto& geometry = command->GetGeometry();
			geometry.SetElementsPerVertex(FloatsPerVertex);
			geometry.SetHostVertexPointer(_vertices.data() + firstQuad * FloatsPerQuad);
			geometry.SetIndexCount(count * RenderResources::IndicesPerQuad);
			geometry.SetHostIndexPointer(indices);
			geometry.SetDrawParameters(PrimitiveType::Triangles, 0, count * RenderResources::VerticesPerQuad);

			renderQueue.AddCommand(command);
		}

		return true;
#endif
	}

#if defined(RHI_CAP_SHADERS) && defined(RHI_CAP_FRAMEBUFFERS)
	void LightingRenderer::AppendLightQuad(const LightEmitter& light)
	{
		// What the single-light shader derived from its instance block, written out per vertex instead: the quad
		// spans the far radius around the light, the corner offset it measures the distance from is carried in the
		// vertex color's .zw (normalized to [-1, 1], exactly as the old vertex stage handed it over), and the
		// normalized near radius rides in the texture coordinates, next to a ring radius of zero
		const float x0 = light.Pos.X - light.RadiusFar, x1 = light.Pos.X + light.RadiusFar;
		const float y0 = light.Pos.Y - light.RadiusFar, y1 = light.Pos.Y + light.RadiusFar;
		const float radiusNear = light.RadiusNear / light.RadiusFar;

		std::size_t base = _vertices.size();
		// Every float is written below, so the zero-initialization resize() would do first is wasted work
		_vertices.resize_for_overwrite(base + FloatsPerQuad);
		float* v = _vertices.data() + base;
		auto put = [&](float px, float py, float cx, float cy) {
			*v++ = px; *v++ = py; *v++ = radiusNear; *v++ = 0.0f;
			*v++ = light.Intensity; *v++ = light.Brightness; *v++ = cx; *v++ = cy;
		};
		// Corner order of the shared quad index pattern (see RenderResources::GetQuadIndices()), which is also
		// the order the tile and particle meshes use
		put(x0, y0, -1.0f, -1.0f);
		put(x1, y0,  1.0f, -1.0f);
		put(x1, y1,  1.0f,  1.0f);
		put(x0, y1, -1.0f,  1.0f);
	}

	void LightingRenderer::AppendRingQuads(const LightEmitter& light, const Rectf& cullingRect)
	{
		// A ring light is drawn as its band alone - a quad around the whole ring would spend most of its pixels on
		// the hole in the middle. The band is cut into angular segments, each one a trapezoid from the inner edge of
		// the band to past its outer edge: the inner corners lie on the inner circle, so the straight inner edge
		// runs inside the hole, and the outer ones at outer / cos(pi / n), so the outer edge clears the band. The
		// fragment stage measures the distance from the ring's circle instead of from the centre, so the band comes
		// out as smooth as a single light and everything a trapezoid covers outside the band adds exactly nothing.
		//
		// How far the corners of the outer edge reach past the band, about outer * (pi / n)^2 / 2, is fill spent on
		// nothing, so the segments are as many as keep that within a tenth of the far radius - a dozen for a ring
		// that has just started to expand, under twenty for the largest the game emits.
		const float invRadiusFar = 1.0f / light.RadiusFar;
		const float outerRadius = light.RingRadius + light.RadiusFar;
		const std::int32_t segments = std::clamp<std::int32_t>((std::int32_t)(fPi * std::sqrt(5.0f * outerRadius * invRadiusFar)) + 1, 8, 64);

		float halfStepSin, halfStepCos;
		sincosApprox(fPi / (float)segments, halfStepSin, halfStepCos);
		const float stepSin = 2.0f * halfStepSin * halfStepCos;
		const float stepCos = halfStepCos * halfStepCos - halfStepSin * halfStepSin;
		// A pixel of margin on both edges absorbs the error of the approximated sine and of the rotation below
		const float innerCorner = std::max(light.RingRadius - light.RadiusFar - 1.0f, 0.0f);
		const float outerCorner = (outerRadius + 1.0f) / halfStepCos;
		const float innerNorm = innerCorner * invRadiusFar;
		const float outerNorm = outerCorner * invRadiusFar;

		const float radiusNear = light.RadiusNear * invRadiusFar;
		const float ringRadius = light.RingRadius * invRadiusFar;
		const float cullMinX = cullingRect.X, cullMaxX = cullingRect.X + cullingRect.W;
		const float cullMinY = cullingRect.Y, cullMaxY = cullingRect.Y + cullingRect.H;

		_vertices.reserve(_vertices.size() + segments * FloatsPerQuad);

		// The direction of each segment boundary is rotated from the previous one. Both segments that share a
		// boundary build its corners from the very same floats, and the last one closes on the exact direction the
		// first started from, so the trapezoids meet without a crack or an overlap the additive blend would show.
		float dirX = 1.0f, dirY = 0.0f;
		for (std::int32_t i = 0; i < segments; i++) {
			float nextX = 1.0f, nextY = 0.0f;
			if (i < segments - 1) {
				nextX = dirX * stepCos - dirY * stepSin;
				nextY = dirY * stepCos + dirX * stepSin;
			}

			const float ax = light.Pos.X + dirX * innerCorner, ay = light.Pos.Y + dirY * innerCorner;
			const float bx = light.Pos.X + nextX * innerCorner, by = light.Pos.Y + nextY * innerCorner;
			const float cx = light.Pos.X + nextX * outerCorner, cy = light.Pos.Y + nextY * outerCorner;
			const float dx = light.Pos.X + dirX * outerCorner, dy = light.Pos.Y + dirY * outerCorner;

			// An expanding ring soon outgrows the view, and its segments outside it are dropped one by one
			if (std::max(std::max(ax, bx), std::max(cx, dx)) > cullMinX && std::min(std::min(ax, bx), std::min(cx, dx)) < cullMaxX &&
				std::max(std::max(ay, by), std::max(cy, dy)) > cullMinY && std::min(std::min(ay, by), std::min(cy, dy)) < cullMaxY) {
				std::size_t base = _vertices.size();
				_vertices.resize_for_overwrite(base + FloatsPerQuad);
				float* v = _vertices.data() + base;
				auto put = [&](float px, float py, float ox, float oy) {
					*v++ = px; *v++ = py; *v++ = radiusNear; *v++ = ringRadius;
					*v++ = light.Intensity; *v++ = light.Brightness; *v++ = ox; *v++ = oy;
				};
				// Any cyclic order of the four corners triangulates a convex quad correctly
				put(ax, ay, dirX * innerNorm, dirY * innerNorm);
				put(bx, by, nextX * innerNorm, nextY * innerNorm);
				put(cx, cy, nextX * outerNorm, nextY * outerNorm);
				put(dx, dy, dirX * outerNorm, dirY * outerNorm);
			}

			dirX = nextX;
			dirY = nextY;
		}
	}

	RenderCommand* LightingRenderer::RentRenderCommand(std::int32_t index)
	{
		if (index < (std::int32_t)_renderCommands.size()) {
			return _renderCommands[index].get();
		}

		// A command's material never changes again after this: the shader is fixed and the mesh needs no texture,
		// so per frame only the geometry pointer and the transformation are rewritten
		auto& command = _renderCommands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::Lighting));
		command->GetMaterial().SetShader(_owner->_levelHandler->_lightingMeshShader);
		command->GetMaterial().SetBlendingEnabled(true);
		command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::One);
		command->GetMaterial().ReserveUniformsDataMemory();

		auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
		if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
			textureUniform->SetIntValue(0); // GL_TEXTURE0
		}
		// Unused by the mesh shader, which takes every light parameter from the vertex stream, but the instance
		// block is shared with the sprite family and a stale tint would be visible if that ever changed
		auto* instanceBlock = command->GetInstanceBlock();
		if (instanceBlock != nullptr) {
			auto* colorUniform = instanceBlock->GetUniform(Material::ColorUniformName);
			if (colorUniform != nullptr) {
				colorUniform->SetFloatValue(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}

		return command.get();
	}
#endif
}
