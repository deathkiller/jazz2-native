#include "PolylineRenderer.h"

#include "../../nCine/Graphics/Material.h"
#include "../../nCine/Graphics/RenderResources.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Rendering
{
	PolylineRenderer::PolylineRenderer(std::int32_t maxVertexFloats)
		: _maxVertexFloats(maxVertexFloats), _vertexFloatsUsed(0), _commandsUsed(0)
	{
		_vertices = std::make_unique<float[]>(maxVertexFloats);
	}

	void PolylineRenderer::BeginFrame()
	{
		_vertexFloatsUsed = 0;
		_commandsUsed = 0;
	}

	Texture& PolylineRenderer::GetFeatherTexture()
	{
		EnsureFeatherTexture();
		return *_featherTexture;
	}

	void PolylineRenderer::EnsureFeatherTexture()
	{
		if (_featherTexture != nullptr) {
			return;
		}

		// A 1D alpha-feather texture (opaque core fading to transparent edges). Mapping it across the line's
		// thickness gives smooth, antialiased edges with linear filtering.
		constexpr std::int32_t FeatherWidth = 16;
		std::uint8_t texels[FeatherWidth * 4];
		for (std::int32_t x = 0; x < FeatherWidth; x++) {
			float d = std::abs((x + 0.5f) / FeatherWidth - 0.5f) * 2.0f; // 0 at center, ~1 at edges
			float a = std::min(1.0f, std::max(0.0f, 1.0f - (d - 0.65f) / 0.30f));
			texels[x * 4 + 0] = 255;
			texels[x * 4 + 1] = 255;
			texels[x * 4 + 2] = 255;
			texels[x * 4 + 3] = (std::uint8_t)(a * 255.0f);
		}
		_featherTexture = std::make_unique<Texture>("PolylineFeather", Texture::Format::RGBA8, FeatherWidth, 1);
		_featherTexture->LoadFromTexels(texels, 0, 0, FeatherWidth, 1);
		_featherTexture->SetMinFiltering(SamplerFilter::Linear);
		_featherTexture->SetMagFiltering(SamplerFilter::Linear);
		_featherTexture->SetWrap(SamplerWrapping::ClampToEdge);
	}

	std::int32_t PolylineRenderer::GetMaxVerticesPerCommand()
	{
		// 4 floats a vertex, and the index of the mesh when batched, a few less to be sure
		const std::uint32_t maxBytes = RenderResources::GetBuffersManager().Specs(RenderBuffersManager::BufferTypes::Array).maxSize;
		return std::max<std::int32_t>(std::int32_t(maxBytes / ((4 + 1) * sizeof(float))) - 8, 16);
	}

	RenderCommand* PolylineRenderer::Draw(ArrayView<const Vector2f> points, Vector2f offset, float halfThickness, std::uint16_t z, const Colorf& color)
	{
		std::int32_t last = std::int32_t(points.size()) - 1;
		return DrawStrip(points, 0, last, 0, last, offset, halfThickness, nullptr, z, color);
	}

	RenderCommand* PolylineRenderer::Draw(ArrayView<const Vector2f> points, Vector2f offset, ArrayView<const float> halfThicknesses, std::uint16_t z, const Colorf& color)
	{
		if (halfThicknesses.size() < points.size()) {
			return nullptr;
		}
		std::int32_t last = std::int32_t(points.size()) - 1;
		return DrawStrip(points, 0, last, 0, last, offset, 0.0f, halfThicknesses.data(), z, color);
	}

	void PolylineRenderer::DrawRange(ArrayView<const Vector2f> points, std::int32_t first, std::int32_t last, ArrayView<const float> halfThicknesses,
		std::uint16_t z, const Colorf& color, SmallVectorImpl<RenderCommand*>& commands)
	{
		if (halfThicknesses.size() < points.size() || first < 0 || last >= std::int32_t(points.size())) {
			return;
		}
		DrawStripSplit(points, 0, std::int32_t(points.size()) - 1, first, last, 0.0f, halfThicknesses.data(), z, color, commands);
	}

	void PolylineRenderer::DrawStripSplit(ArrayView<const Vector2f> points, std::int32_t boundsFirst, std::int32_t boundsLast, std::int32_t first,
		std::int32_t last, float halfThickness, const float* halfThicknesses, std::uint16_t z, const Colorf& color, SmallVectorImpl<RenderCommand*>& commands)
	{
		// Parts that share their last and first point, they join without a seam as their neighbours give the directions
		const std::int32_t maxPoints = GetMaxVerticesPerCommand() / 2;
		for (std::int32_t from = first; from < last; from += maxPoints - 1) {
			std::int32_t to = std::min(from + maxPoints - 1, last);
			if (RenderCommand* command = DrawStrip(points, boundsFirst, boundsLast, from, to, Vector2f(0.0f, 0.0f), halfThickness, halfThicknesses, z, color)) {
				commands.push_back(command);
			}
		}
	}

	void PolylineRenderer::DrawMany(ArrayView<const Vector2f> points, ArrayView<const std::int32_t> ends, float halfThickness,
		std::uint16_t z, const Colorf& color, SmallVectorImpl<RenderCommand*>& commands)
	{
		// Whole polylines go together into one command as long as it can draw them, a longer one is split on its own
		const std::int32_t maxVertices = GetMaxVerticesPerCommand();
		std::int32_t groupFirst = 0, groupStart = 0, groupVertices = 0;
		std::int32_t start = 0;
		auto flush = [&](std::int32_t endIndex) {
			if (groupVertices > 0) {
				if (RenderCommand* command = DrawManyGroup(points, ends, groupFirst, endIndex, groupStart, halfThickness, z, color)) {
					commands.push_back(command);
				}
			}
			groupVertices = 0;
		};
		for (std::int32_t i = 0; i < std::int32_t(ends.size()); i++) {
			std::int32_t end = ends[i];
			std::int32_t count = end - start;
			if (count >= 2) {
				std::int32_t vertices = count * 2 + 2;
				if (vertices > maxVertices) {
					flush(i);
					DrawStripSplit(points, start, end - 1, start, end - 1, halfThickness, nullptr, z, color, commands);
				} else {
					if (groupVertices + vertices > maxVertices) {
						flush(i);
					}
					if (groupVertices == 0) {
						groupFirst = i;
						groupStart = start;
					}
					groupVertices += vertices;
				}
			}
			start = end;
		}
		flush(std::int32_t(ends.size()));
	}

	RenderCommand* PolylineRenderer::DrawManyGroup(ArrayView<const Vector2f> points, ArrayView<const std::int32_t> ends, std::int32_t firstEnd,
		std::int32_t lastEnd, std::int32_t groupStart, float halfThickness, std::uint16_t z, const Colorf& color)
	{
		// Two vertices a point, and two more joining a polyline to the one before it by triangles of no area
		std::int32_t vertexCount = 0;
		std::int32_t start = groupStart;
		for (std::int32_t e = firstEnd; e < lastEnd; e++) {
			std::int32_t count = ends[e] - start;
			if (count >= 2) {
				vertexCount += count * 2 + (vertexCount > 0 ? 2 : 0);
			}
			start = ends[e];
		}
		if (vertexCount == 0 || _vertexFloatsUsed + vertexCount * 4 > _maxVertexFloats) {
			return nullptr;
		}

		EnsureFeatherTexture();

		const std::int32_t startFloat = _vertexFloatsUsed;
		start = groupStart;
		for (std::int32_t e = firstEnd; e < lastEnd; e++) {
			std::int32_t end = ends[e];
			std::int32_t count = end - start;
			if (count < 2) {
				start = end;
				continue;
			}
			bool closed = (count > 2 && points[start] == points[end - 1]);
			for (std::int32_t i = start; i < end; i++) {
				const Vector2f* previous = (i > start ? &points[i - 1] : (closed ? &points[end - 2] : nullptr));
				const Vector2f* next = (i < end - 1 ? &points[i + 1] : (closed ? &points[start + 1] : nullptr));
				Vector2f side = GetSideOffset(previous, points[i], next, halfThickness);
				if (i == start && _vertexFloatsUsed > startFloat) {
					// The last vertex again and the first of this one, so the strip goes on without drawing between them
					std::int32_t lastFloat = _vertexFloatsUsed - 4;
					for (std::int32_t j = 0; j < 4; j++) {
						_vertices[_vertexFloatsUsed++] = _vertices[lastFloat + j];
					}
					Vector2f left = points[i] + side;
					_vertices[_vertexFloatsUsed++] = left.X;
					_vertices[_vertexFloatsUsed++] = left.Y;
					_vertices[_vertexFloatsUsed++] = 0.0f;
					_vertices[_vertexFloatsUsed++] = 0.0f;
				}
				AppendPair(points[i], side);
			}
			start = end;
		}

		return MakeCommand(startFloat, vertexCount, z, color);
	}

	Vector2f PolylineRenderer::GetSideOffset(const Vector2f* previous, Vector2f point, const Vector2f* next, float halfThickness)
	{
		Vector2f normalIn, normalOut;
		bool hasIn = false, hasOut = false;
		if (previous != nullptr) {
			Vector2f dir = point - *previous;
			float len = dir.Length();
			if (len > 0.0001f) {
				normalIn = Vector2f(-dir.Y / len, dir.X / len);
				hasIn = true;
			}
		}
		if (next != nullptr) {
			Vector2f dir = *next - point;
			float len = dir.Length();
			if (len > 0.0001f) {
				normalOut = Vector2f(-dir.Y / len, dir.X / len);
				hasOut = true;
			}
		}
		if (!hasIn && !hasOut) {
			return Vector2f(0.0f, halfThickness);
		}
		if (!hasIn) {
			normalIn = normalOut;
		} else if (!hasOut) {
			normalOut = normalIn;
		}

		// Along the bisector of the corner, longer the sharper it is, but not more than twice, a spike would be worse
		Vector2f normal = normalIn + normalOut;
		float len = normal.Length();
		if (len < 0.001f) {
			// The line turns back on itself
			return normalIn * halfThickness;
		}
		normal /= len;
		float cosHalfAngle = normal.X * normalIn.X + normal.Y * normalIn.Y;
		return normal * (halfThickness / std::max(cosHalfAngle, 0.5f));
	}

	void PolylineRenderer::AppendPair(Vector2f point, Vector2f side)
	{
		Vector2f left = point + side;
		Vector2f right = point - side;
		// U runs across the thickness (0 = one edge, 1 = the other) to sample the feather texture; V is unused
		_vertices[_vertexFloatsUsed++] = left.X;
		_vertices[_vertexFloatsUsed++] = left.Y;
		_vertices[_vertexFloatsUsed++] = 0.0f;
		_vertices[_vertexFloatsUsed++] = 0.0f;
		_vertices[_vertexFloatsUsed++] = right.X;
		_vertices[_vertexFloatsUsed++] = right.Y;
		_vertices[_vertexFloatsUsed++] = 1.0f;
		_vertices[_vertexFloatsUsed++] = 0.0f;
	}

	RenderCommand* PolylineRenderer::DrawStrip(ArrayView<const Vector2f> points, std::int32_t boundsFirst, std::int32_t boundsLast, std::int32_t first,
		std::int32_t last, Vector2f offset, float halfThickness, const float* halfThicknesses, std::uint16_t z, const Colorf& color)
	{
		const std::int32_t count = last - first + 1;
		if (count < 2) {
			return nullptr;
		}

		const std::int32_t vertexCount = count * 2; // a left/right vertex pair per centerline point (triangle strip)
		if (vertexCount > GetMaxVerticesPerCommand()) {
			// More than a buffer the vertices are copied into holds, it wouldn't be drawn, or the driver would read past it
			return nullptr;
		}
		if (_vertexFloatsUsed + vertexCount * 4 > _maxVertexFloats) {
			return nullptr; // not enough room in the persistent vertex buffer this frame (4 floats per vertex: x, y, u, v)
		}

		EnsureFeatherTexture();

		const std::int32_t startFloat = _vertexFloatsUsed;
		// A polyline that ends where it starts is closed, its ends have the same direction then
		const bool closed = (boundsLast - boundsFirst + 1 > 2 && points[boundsFirst] == points[boundsLast]);
		for (std::int32_t i = first; i <= last; i++) {
			// The points around the range still give the direction at its ends
			const Vector2f* previous = (i > boundsFirst ? &points[i - 1] : (closed ? &points[boundsLast - 1] : nullptr));
			const Vector2f* next = (i < boundsLast ? &points[i + 1] : (closed ? &points[boundsFirst + 1] : nullptr));
			float pointHalfThickness = (halfThicknesses != nullptr ? halfThicknesses[i] : halfThickness);
			AppendPair(points[i] + offset, GetSideOffset(previous, points[i], next, pointHalfThickness));
		}

		return MakeCommand(startFloat, vertexCount, z, color);
	}

	RenderCommand* PolylineRenderer::MakeCommand(std::int32_t startFloat, std::int32_t vertexCount, std::uint16_t z, const Colorf& color)
	{
		RenderCommand* command;
		if (_commandsUsed < std::int32_t(_commands.size())) {
			command = _commands[_commandsUsed].get();
		} else {
			command = _commands.emplace_back(std::make_unique<RenderCommand>(RenderCommand::Type::MeshSprite)).get();
			command->GetMaterial().SetBlendingEnabled(true);
		}
		_commandsUsed++;

		if (command->GetMaterial().SetShaderProgramType(Material::ShaderProgramType::MeshSprite)) {
			command->GetMaterial().ReserveUniformsDataMemory();
			auto* textureUniform = command->GetMaterial().Uniform(Material::TextureUniformName);
			if (textureUniform != nullptr && textureUniform->GetIntValue(0) != 0) {
				textureUniform->SetIntValue(0); // GL_TEXTURE0
			}
		}
		command->GetMaterial().SetBlendingFactors(BlendingFactor::SrcAlpha, BlendingFactor::OneMinusSrcAlpha);
		command->GetGeometry().SetDrawParameters(PrimitiveType::TriangleStrip, 0, vertexCount);
		command->GetGeometry().SetElementsPerVertex(4);
		command->GetGeometry().SetHostVertexPointer(&_vertices[startFloat]);

		auto instanceBlock = command->GetInstanceBlock();
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatValue(1.0f, 1.0f);
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(color.Data());

		command->SetTransformation(Matrix4x4f::Identity);
		command->SetLayer(z);
		command->GetMaterial().SetTexture(0, *_featherTexture);

		return command;
	}

	void PolylineRenderer::SmoothCatmullRom(ArrayView<const Vector2f> controlPoints, std::int32_t subdivisions, SmallVectorImpl<Vector2f>& out, std::int32_t maxPoints)
	{
		const std::int32_t m = std::int32_t(controlPoints.size());
		if (m == 0) {
			return;
		}

		for (std::int32_t s = 0; s < m - 1 && std::int32_t(out.size()) < maxPoints; s++) {
			Vector2f p0 = controlPoints[std::max<std::int32_t>(0, s - 1)];
			Vector2f p1 = controlPoints[s];
			Vector2f p2 = controlPoints[s + 1];
			Vector2f p3 = controlPoints[std::min(m - 1, s + 2)];
			for (std::int32_t t = 0; t < subdivisions; t++) {
				float u = (float)t / subdivisions;
				float u2 = u * u, u3 = u2 * u;
				Vector2f pt = (p1 * 2.0f + (p2 - p0) * u + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * u2
					+ (p1 * 3.0f - p0 - p2 * 3.0f + p3) * u3) * 0.5f;
				out.push_back(pt);
			}
		}
		out.push_back(controlPoints[m - 1]);
	}

	void PolylineRenderer::SimplifyRdp(ArrayView<const Vector2f> points, float tolerance, SmallVectorImpl<Vector2f>& out)
	{
		const std::size_t count = points.size();
		if (count < 3) {
			for (const Vector2f& point : points) {
				out.push_back(point);
			}
			return;
		}

		SmallVector<std::uint8_t, 0> keep;
		keep.resize(count);
		std::fill(keep.begin(), keep.end(), std::uint8_t(0));
		keep[0] = 1;
		keep[count - 1] = 1;

		// Iterative, so a long path can't run out of stack
		struct Span {
			std::size_t First, Last;
		};
		SmallVector<Span, 32> spans;
		spans.push_back({ 0, count - 1 });
		const float toleranceSq = tolerance * tolerance;
		while (!spans.empty()) {
			Span span = spans.back();
			spans.pop_back();

			const Vector2f a = points[span.First];
			const Vector2f d = points[span.Last] - a;
			const float lengthSq = d.X * d.X + d.Y * d.Y;

			float maxDistanceSq = -1.0f;
			std::size_t farthest = span.First;
			for (std::size_t i = span.First + 1; i < span.Last; i++) {
				const Vector2f p = points[i] - a;
				float t = (lengthSq > 0.000001f ? (p.X * d.X + p.Y * d.Y) / lengthSq : 0.0f);
				t = std::clamp(t, 0.0f, 1.0f);
				const Vector2f q = p - d * t;
				const float distanceSq = q.X * q.X + q.Y * q.Y;
				if (distanceSq > maxDistanceSq) {
					maxDistanceSq = distanceSq;
					farthest = i;
				}
			}

			if (maxDistanceSq > toleranceSq) {
				keep[farthest] = 1;
				spans.push_back({ span.First, farthest });
				spans.push_back({ farthest, span.Last });
			}
		}

		for (std::size_t i = 0; i < count; i++) {
			if (keep[i] != 0) {
				out.push_back(points[i]);
			}
		}
	}
}
