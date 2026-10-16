#pragma once

#include "../../Main.h"

#include "../../nCine/Graphics/RenderCommand.h"
#include "../../nCine/Graphics/Texture.h"
#include "../../nCine/Primitives/Color.h"
#include "../../nCine/Primitives/Vector2.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>

#include <memory>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Rendering
{
	/**
		@brief Draws thick antialiased polylines

		A polyline is drawn as a triangle strip across its thickness, sampling a feather texture whose edges fade
		out, so it has smooth edges at any angle. The commands and the vertices are kept from frame to frame,
		@ref BeginFrame() starts over. The direct rendering tier has no program to draw the strip with, a polyline
		has to be rasterized there instead.

		The race minimap draws its track this way, and the level editor the path the player took.
	*/
	class PolylineRenderer
	{
	public:
		/** @brief Creates a new instance that can draw up to @p maxVertexFloats floats of vertices a frame, 4 per vertex */
		explicit PolylineRenderer(std::int32_t maxVertexFloats = 65536);

		PolylineRenderer(const PolylineRenderer&) = delete;
		PolylineRenderer& operator=(const PolylineRenderer&) = delete;

		/** @brief Starts drawing a new frame, the vertices and commands of the previous one are reused */
		void BeginFrame();

		/**
		 * @brief Returns the texture that gives the lines their soft edges
		 *
		 * It's white, transparent at U 0 and 1, opaque from U 0.175 to 0.825, the V coordinate doesn't matter.
		 */
		Texture& GetFeatherTexture();

		/**
		 * @brief Prepares a command that draws a polyline
		 *
		 * The points are moved by @p offset, which saves a copy when they are in another space. Returns the command to
		 * queue, or `nullptr` if there are fewer than two points, more than a command can draw (see
		 * @ref GetMaxVerticesPerCommand()) or no room for the vertices left in this frame. The command stays valid until
		 * the next @ref BeginFrame().
		 */
		RenderCommand* Draw(ArrayView<const Vector2f> points, Vector2f offset, float halfThickness, std::uint16_t z, const Colorf& color);
		/** @brief Prepares a command that draws a polyline of a half-thickness for each of its points */
		RenderCommand* Draw(ArrayView<const Vector2f> points, Vector2f offset, ArrayView<const float> halfThicknesses, std::uint16_t z, const Colorf& color);
		/**
		 * @brief Prepares commands that draw a part of a polyline, from its point @p first to @p last, and appends them
		 *
		 * The points around the part decide its directions at both ends, so parts drawn one after another, each in
		 * a color of its own, join without a seam. A part longer than a command can draw at once is split into
		 * several commands that join the same way.
		 */
		void DrawRange(ArrayView<const Vector2f> points, std::int32_t first, std::int32_t last, ArrayView<const float> halfThicknesses,
			std::uint16_t z, const Colorf& color, SmallVectorImpl<RenderCommand*>& commands);
		/**
		 * @brief Prepares commands that draw many polylines of the same thickness and color, and appends them
		 *
		 * A polyline ends before the point its entry of @p ends gives, the next one starts there. A polyline that ends
		 * where it starts is closed, it has no seam there. As many polylines as a command can draw go into each one.
		 */
		void DrawMany(ArrayView<const Vector2f> points, ArrayView<const std::int32_t> ends, float halfThickness,
			std::uint16_t z, const Colorf& color, SmallVectorImpl<RenderCommand*>& commands);

		/**
		 * @brief Returns the most vertices a single command can draw
		 *
		 * The vertices of a command are copied into one buffer of the common vertex buffers, with one more float per
		 * vertex when it's batched with others, and a command that doesn't fit into one isn't drawn at all.
		 */
		static std::int32_t GetMaxVerticesPerCommand();
		/**
		 * @brief Appends a Catmull-Rom spline through the control points to @p out
		 *
		 * Every segment between two control points is divided into @p subdivisions points, the last control point is
		 * appended at the end. No more segments are started once @p out holds @p maxPoints points.
		 */
		static void SmoothCatmullRom(ArrayView<const Vector2f> controlPoints, std::int32_t subdivisions, SmallVectorImpl<Vector2f>& out,
			std::int32_t maxPoints = INT32_MAX);

		/**
		 * @brief Appends a simplified polyline to @p out
		 *
		 * Ramer-Douglas-Peucker: a point is dropped when it lies within @p tolerance of the line through its neighbours
		 * that are kept. The first and the last point are always kept.
		 */
		static void SimplifyRdp(ArrayView<const Vector2f> points, float tolerance, SmallVectorImpl<Vector2f>& out);

	private:
		std::unique_ptr<float[]> _vertices;
		std::int32_t _maxVertexFloats;
		std::int32_t _vertexFloatsUsed;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _commands;
		std::int32_t _commandsUsed;
		std::unique_ptr<Texture> _featherTexture;

		void EnsureFeatherTexture();
		// Points from the first to the last of a polyline that goes from the point boundsFirst to boundsLast, whose points are the neighbours
		RenderCommand* DrawStrip(ArrayView<const Vector2f> points, std::int32_t boundsFirst, std::int32_t boundsLast, std::int32_t first, std::int32_t last,
			Vector2f offset, float halfThickness, const float* halfThicknesses, std::uint16_t z, const Colorf& color);
		// The same, split into commands that each fit
		void DrawStripSplit(ArrayView<const Vector2f> points, std::int32_t boundsFirst, std::int32_t boundsLast, std::int32_t first, std::int32_t last,
			float halfThickness, const float* halfThicknesses, std::uint16_t z, const Colorf& color, SmallVectorImpl<RenderCommand*>& commands);
		// Whole polylines between two entries of ends, in a single command
		RenderCommand* DrawManyGroup(ArrayView<const Vector2f> points, ArrayView<const std::int32_t> ends, std::int32_t firstEnd, std::int32_t lastEnd,
			std::int32_t start, float halfThickness, std::uint16_t z, const Colorf& color);
		void AppendPair(Vector2f point, Vector2f side);
		RenderCommand* MakeCommand(std::int32_t startFloat, std::int32_t vertexCount, std::uint16_t z, const Colorf& color);

		// Offset from a point to the edges of the line there, mitered so the line keeps its thickness at a corner
		static Vector2f GetSideOffset(const Vector2f* previous, Vector2f point, const Vector2f* next, float halfThickness);
	};
}
