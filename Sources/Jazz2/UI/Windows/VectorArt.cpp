#include "VectorArt.h"

#if defined(WITH_UI_WINDOWS)

#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/SmallVector.h>

#include <algorithm>
#include <cmath>

// Third-party code, only its rasterizer is used here
#if defined(DEATH_TARGET_MSVC)
#	pragma warning(push)
#	pragma warning(disable: 4100 4127 4189 4244 4245 4456 4457 4701 4702)
#elif defined(DEATH_TARGET_GCC)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-function"
#	pragma GCC diagnostic ignored "-Wsign-compare"
#	pragma GCC diagnostic ignored "-Wconversion"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb/stb_truetype.h>
#if defined(DEATH_TARGET_MSVC)
#	pragma warning(pop)
#elif defined(DEATH_TARGET_GCC)
#	pragma GCC diagnostic pop
#endif

using namespace Death::Containers::Literals;

namespace Jazz2::UI::Windows
{
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;
	using nCine::Vector2f;

	namespace
	{
		constexpr float Pi = 3.14159265358979323846f;

		enum class LineCap : std::uint8_t
		{
			Butt,
			Round,
			Square
		};

		enum class LineJoin : std::uint8_t
		{
			Miter,
			Round,
			Bevel
		};

		struct SubPath
		{
			SmallVector<Vector2f, 0> Points;
			bool Closed = false;
		};

		// Closed polygons in pixels, as the rasterizer takes them
		struct Polygons
		{
			SmallVector<stbtt__point, 0> Points;
			SmallVector<int, 0> Counts;

			void Begin()
			{
				Counts.push_back(0);
			}

			void Add(Vector2f p)
			{
				Points.push_back({ p.X, p.Y });
				Counts.back()++;
			}

			void End()
			{
				// Less than a triangle has no area
				if (!Counts.empty() && Counts.back() < 3) {
					Points.resize(Points.size() - std::size_t(Counts.back()));
					Counts.pop_back();
				}
			}

			void AddPolygon(const SmallVectorImpl<Vector2f>& points)
			{
				Begin();
				for (const Vector2f& p : points) {
					Add(p);
				}
				End();
			}
		};

		// Segments of a whole circle of a radius in pixels, its edge is then never more than about 0.08 pixel off
		std::int32_t GetCircleSegments(float radius)
		{
			return std::clamp(std::int32_t(std::ceil(std::sqrt(std::max(radius, 0.0f)) * 8.0f)), 12, 512);
		}

		void AddArc(SmallVectorImpl<Vector2f>& out, Vector2f center, float radius, float startAngle, float sweep, bool withStart)
		{
			std::int32_t n = std::max(1, std::int32_t(std::ceil(float(GetCircleSegments(radius)) * std::abs(sweep) / (2.0f * Pi))));
			for (std::int32_t i = (withStart ? 0 : 1); i <= n; i++) {
				float angle = startAngle + sweep * float(i) / float(n);
				out.push_back(center + Vector2f(std::cos(angle), std::sin(angle)) * radius);
			}
		}

		float Cross(Vector2f a, Vector2f b)
		{
			return a.X * b.Y - a.Y * b.X;
		}

		float AngleOf(Vector2f v)
		{
			return std::atan2(v.Y, v.X);
		}

		// Points of one side of a stroke at a corner between two segments, `side` is +1 for the left and -1 for the right
		void AddJoin(SmallVectorImpl<Vector2f>& out, Vector2f p, Vector2f dirA, Vector2f dirB, float side, float halfWidth, LineJoin join)
		{
			Vector2f na = Vector2f(-dirA.Y, dirA.X) * (side * halfWidth);
			Vector2f nb = Vector2f(-dirB.Y, dirB.X) * (side * halfWidth);
			float cross = Cross(dirA, dirB);
			float dot = Vector2f::Dot(dirA, dirB);
			if (dot > 0.9999f) {
				// Nearly straight on
				out.push_back(p + (na + nb).Normalized() * halfWidth);
				return;
			}
			if (side * cross > 0.0f) {
				// The inner side, through the corner itself, the overlap is inside of the stroke
				out.push_back(p + na);
				out.push_back(p);
				out.push_back(p + nb);
				return;
			}
			switch (join) {
				case LineJoin::Round: {
					float start = AngleOf(na);
					float sweep = AngleOf(nb) - start;
					while (sweep > Pi) sweep -= 2.0f * Pi;
					while (sweep < -Pi) sweep += 2.0f * Pi;
					AddArc(out, p, halfWidth, start, sweep, true);
					break;
				}
				case LineJoin::Miter: {
					Vector2f m = (na + nb).Normalized();
					float cosHalf = Vector2f::Dot(m, na) / halfWidth;
					// The usual limit of SVG, a sharper corner is cut off
					if (cosHalf > 0.25f) {
						out.push_back(p + m * (halfWidth / cosHalf));
						break;
					}
					out.push_back(p + na);
					out.push_back(p + nb);
					break;
				}
				default: {
					out.push_back(p + na);
					out.push_back(p + nb);
					break;
				}
			}
		}

		/**
		 * Outline of a stroked path as polygons. `extension` is how far a flat end reaches past the end point, a round
		 * end always reaches the half width.
		 */
		void StrokePath(const SubPath& path, float halfWidth, LineCap cap, float extension, LineJoin join, Polygons& out)
		{
			SmallVector<Vector2f, 32> points;
			for (const Vector2f& p : path.Points) {
				if (points.empty() || (p - points.back()).SqrLength() > 1e-8f) {
					points.push_back(p);
				}
			}
			bool closed = path.Closed;
			if (closed && points.size() > 1 && (points.front() - points.back()).SqrLength() <= 1e-8f) {
				points.pop_back();
			}
			if (halfWidth <= 0.0f || points.empty()) {
				return;
			}

			if (points.size() == 1) {
				// A dot, only a round or a square end shows it
				SmallVector<Vector2f, 64> dot;
				if (cap == LineCap::Round) {
					AddArc(dot, points[0], halfWidth, 0.0f, 2.0f * Pi, false);
				} else if (extension > 0.0f) {
					Vector2f c = points[0];
					dot.push_back(c + Vector2f(-extension, -halfWidth));
					dot.push_back(c + Vector2f(extension, -halfWidth));
					dot.push_back(c + Vector2f(extension, halfWidth));
					dot.push_back(c + Vector2f(-extension, halfWidth));
				}
				out.AddPolygon(dot);
				return;
			}
			if (closed && points.size() < 3) {
				closed = false;
			}

			std::size_t n = points.size();
			std::size_t segments = (closed ? n : n - 1);
			SmallVector<Vector2f, 32> dirs;
			for (std::size_t i = 0; i < segments; i++) {
				dirs.push_back((points[(i + 1) % n] - points[i]).Normalized());
			}

			SmallVector<Vector2f, 64> left, right;
			if (closed) {
				for (std::size_t i = 0; i < n; i++) {
					Vector2f prev = dirs[(i + segments - 1) % segments];
					AddJoin(left, points[i], prev, dirs[i], 1.0f, halfWidth, join);
					AddJoin(right, points[i], prev, dirs[i], -1.0f, halfWidth, join);
				}
				out.AddPolygon(left);
				std::reverse(right.begin(), right.end());
				out.AddPolygon(right);
				return;
			}

			Vector2f first = points[0], last = points[n - 1];
			Vector2f firstDir = dirs[0], lastDir = dirs[segments - 1];
			if (cap != LineCap::Round) {
				first -= firstDir * extension;
				last += lastDir * extension;
			}
			Vector2f firstNormal = Vector2f(-firstDir.Y, firstDir.X) * halfWidth;
			Vector2f lastNormal = Vector2f(-lastDir.Y, lastDir.X) * halfWidth;

			SmallVector<Vector2f, 128> outline;
			outline.push_back(first + firstNormal);
			for (std::size_t i = 1; i + 1 < n; i++) {
				AddJoin(outline, points[i], dirs[i - 1], dirs[i], 1.0f, halfWidth, join);
			}
			outline.push_back(last + lastNormal);
			if (cap == LineCap::Round) {
				// Around the end, from the left side through the direction of the path to the right side
				AddArc(outline, last, halfWidth, AngleOf(lastNormal), -Pi, false);
				outline.pop_back();
			}
			outline.push_back(last - lastNormal);
			for (std::size_t i = n - 2; i >= 1; i--) {
				AddJoin(outline, points[i], -dirs[i], -dirs[i - 1], 1.0f, halfWidth, join);
			}
			outline.push_back(first - firstNormal);
			if (cap == LineCap::Round) {
				AddArc(outline, first, halfWidth, AngleOf(-firstNormal), -Pi, false);
				outline.pop_back();
			}
			out.AddPolygon(outline);
		}

		// Cuts a polygon to one side of a line, x or y at least or at most a value, the rasterizer asserts on points outside of the square
		void ClipPolygon(SmallVectorImpl<stbtt__point>& points, bool vertical, float limit, bool keepLess)
		{
			SmallVector<stbtt__point, 64> result;
			std::size_t n = points.size();
			for (std::size_t i = 0; i < n; i++) {
				const stbtt__point& a = points[i];
				const stbtt__point& b = points[(i + 1) % n];
				float va = (vertical ? a.x : a.y), vb = (vertical ? b.x : b.y);
				bool insideA = (keepLess ? va <= limit : va >= limit);
				bool insideB = (keepLess ? vb <= limit : vb >= limit);
				if (insideA) {
					result.push_back(a);
				}
				if (insideA != insideB) {
					float t = (limit - va) / (vb - va);
					result.push_back({ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t });
				}
			}
			points.clear();
			for (const stbtt__point& p : result) {
				points.push_back(p);
			}
		}

		void Rasterize(const Polygons& source, std::int32_t size, SmallVectorImpl<std::uint8_t>& coverage)
		{
			coverage.assign(std::size_t(size) * std::size_t(size), 0);
			if (source.Counts.empty()) {
				return;
			}

			// Only what is inside of the square is drawn, so the polygons are cut to it, the coverage inside stays the same
			Polygons polygons;
			std::size_t offset = 0;
			float limit = float(size);
			for (int count : source.Counts) {
				SmallVector<stbtt__point, 64> points;
				for (int i = 0; i < count; i++) {
					points.push_back(source.Points[offset + std::size_t(i)]);
				}
				offset += std::size_t(count);
				ClipPolygon(points, true, 0.0f, false);
				ClipPolygon(points, true, limit, true);
				ClipPolygon(points, false, 0.0f, false);
				ClipPolygon(points, false, limit, true);
				if (points.size() < 3) {
					continue;
				}
				polygons.Counts.push_back(int(points.size()));
				for (const stbtt__point& p : points) {
					polygons.Points.push_back(p);
				}
			}
			if (polygons.Counts.empty()) {
				return;
			}
			stbtt__bitmap bitmap;
			bitmap.w = size;
			bitmap.h = size;
			bitmap.stride = size;
			bitmap.pixels = coverage.data();
			// The rasterizer doesn't change the points, it only takes them as mutable
			stbtt__rasterize(&bitmap, const_cast<stbtt__point*>(polygons.Points.data()), const_cast<int*>(polygons.Counts.data()),
				std::int32_t(polygons.Counts.size()), 1.0f, 1.0f, 0.0f, 0.0f, 0, 0, 0, nullptr);
		}

		void MaxInto(SmallVectorImpl<std::uint8_t>& target, const SmallVectorImpl<std::uint8_t>& source)
		{
			for (std::size_t i = 0; i < target.size(); i++) {
				target[i] = std::max(target[i], source[i]);
			}
		}

		struct Paint
		{
			bool Stroke = false;
			// Takes away what is under the shapes instead of painting them
			bool Clear = false;
			std::uint32_t Color = 0;
			float Width = 1.0f;
			LineCap Cap = LineCap::Butt;
			LineJoin Join = LineJoin::Miter;
			float Outline = 0.0f;
			std::uint32_t OutlineColor = 0;
		};

		// Reads the text of the art, numbers and words, as SVG paths are written
		class Reader
		{
		public:
			Reader(const char* begin, const char* end)
				: _pos(begin), _end(end)
			{
			}

			const char* GetPos() const
			{
				return _pos;
			}

			void SkipSpaces()
			{
				while (_pos < _end && (*_pos == ' ' || *_pos == '\t' || *_pos == '\r' || *_pos == '\n' || *_pos == ',')) {
					_pos++;
				}
			}

			bool AtEnd()
			{
				SkipSpaces();
				return (_pos >= _end);
			}

			char Peek()
			{
				SkipSpaces();
				return (_pos < _end ? *_pos : '\0');
			}

			bool IsNumberNext()
			{
				char c = Peek();
				return ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.');
			}

			bool Number(float& value)
			{
				SkipSpaces();
				const char* p = _pos;
				bool negative = false;
				if (p < _end && (*p == '-' || *p == '+')) {
					negative = (*p == '-');
					p++;
				}
				double result = 0.0;
				bool digits = false;
				while (p < _end && *p >= '0' && *p <= '9') {
					result = result * 10.0 + double(*p - '0');
					p++;
					digits = true;
				}
				if (p < _end && *p == '.') {
					p++;
					double scale = 0.1;
					while (p < _end && *p >= '0' && *p <= '9') {
						result += double(*p - '0') * scale;
						scale *= 0.1;
						p++;
						digits = true;
					}
				}
				if (!digits) {
					return false;
				}
				if (p < _end && (*p == 'e' || *p == 'E') && p + 1 < _end && (p[1] == '-' || p[1] == '+' || (p[1] >= '0' && p[1] <= '9'))) {
					p++;
					bool negativeExponent = false;
					if (*p == '-' || *p == '+') {
						negativeExponent = (*p == '-');
						p++;
					}
					std::int32_t exponent = 0;
					while (p < _end && *p >= '0' && *p <= '9') {
						exponent = exponent * 10 + (*p - '0');
						p++;
					}
					result *= std::pow(10.0, negativeExponent ? -exponent : exponent);
				}
				value = float(negative ? -result : result);
				_pos = p;
				return true;
			}

			// A flag of an arc is a single digit, it may be written right next to the next one
			bool Flag(bool& value)
			{
				SkipSpaces();
				if (_pos < _end && (*_pos == '0' || *_pos == '1')) {
					value = (*_pos == '1');
					_pos++;
					return true;
				}
				return false;
			}

			// Letters and dashes, a keyword, a single letter is a command of a path or a color
			StringView Word()
			{
				SkipSpaces();
				const char* start = _pos;
				// A dash only inside of a keyword, as "round-cap", "M-2" is a command and a number
				while (_pos < _end && ((*_pos >= 'a' && *_pos <= 'z') || (*_pos >= 'A' && *_pos <= 'Z') ||
					(*_pos == '-' && _pos != start && _pos + 1 < _end && ((_pos[1] >= 'a' && _pos[1] <= 'z') || (_pos[1] >= 'A' && _pos[1] <= 'Z'))))) {
					_pos++;
				}
				return StringView(start, std::size_t(_pos - start));
			}

			// A color, a letter or `#` and `+`, or `#` with hexadecimal digits
			bool Color(VectorArt::ColorFunc colors, std::uint32_t& value)
			{
				SkipSpaces();
				if (_pos >= _end) {
					return false;
				}
				if (*_pos == '#' && _pos + 1 < _end && IsHex(_pos[1])) {
					const char* p = _pos + 1;
					std::uint32_t hex = 0;
					std::int32_t count = 0;
					while (p < _end && IsHex(*p) && count < 8) {
						hex = (hex << 4) | HexValue(*p);
						p++;
						count++;
					}
					if (count == 6) {
						value = ((hex >> 16) & 0xFF) | (hex & 0xFF00) | ((hex & 0xFF) << 16) | 0xFF000000u;
					} else if (count == 8) {
						value = ((hex >> 24) & 0xFF) | (((hex >> 16) & 0xFF) << 8) | (((hex >> 8) & 0xFF) << 16) | ((hex & 0xFF) << 24);
					} else {
						return false;
					}
					_pos = p;
					return true;
				}
				char c = *_pos;
				if (c >= '0' && c <= '9') {
					return false;
				}
				std::uint32_t color = colors(c);
				if (color == 0) {
					return false;
				}
				// A single character, not the first letter of a keyword
				if (_pos + 1 < _end && ((_pos[1] >= 'a' && _pos[1] <= 'z') || (_pos[1] >= 'A' && _pos[1] <= 'Z') || _pos[1] == '-')) {
					return false;
				}
				value = color;
				_pos++;
				return true;
			}

		private:
			const char* _pos;
			const char* _end;

			static bool IsHex(char c)
			{
				return ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
			}

			static std::uint32_t HexValue(char c)
			{
				if (c >= '0' && c <= '9') return std::uint32_t(c - '0');
				if (c >= 'a' && c <= 'f') return std::uint32_t(c - 'a' + 10);
				return std::uint32_t(c - 'A' + 10);
			}
		};

		class Drawer
		{
		public:
			Drawer(StringView source, float scale, VectorArt::ColorFunc colors)
				: _source(source), _scale(scale), _colors(colors), _errorAt(nullptr)
			{
			}

			// Reads every statement, the paints and the shapes in pixels are kept if `keep` is set
			bool Read(bool keep)
			{
				const char* p = _source.data();
				const char* end = p + _source.size();
				float outline = 0.0f;
				std::uint32_t outlineColor = _colors('o');
				while (p < end) {
					const char* statementEnd = p;
					while (statementEnd < end && *statementEnd != ';') {
						statementEnd++;
					}
					const char* colon = p;
					while (colon < statementEnd && *colon != ':') {
						colon++;
					}

					Reader head(p, colon);
					if (head.AtEnd() && colon == statementEnd) {
						// An empty statement, as after the last `;`
						p = statementEnd + 1;
						continue;
					}
					StringView kind = head.Word();
					if (kind == "outline"_s) {
						if (colon != statementEnd || !head.Number(outline) || outline < 0.0f) {
							return Fail(head.GetPos());
						}
						if (!head.AtEnd() && !head.Color(_colors, outlineColor)) {
							return Fail(head.GetPos());
						}
						if (!head.AtEnd()) {
							return Fail(head.GetPos());
						}
						p = statementEnd + 1;
						continue;
					}

					Paint paint;
					paint.Outline = outline;
					paint.OutlineColor = outlineColor;
					if (kind == "clear"_s && colon != statementEnd) {
						paint.Clear = true;
						paint.Outline = 0.0f;
						if (!head.AtEnd()) {
							return Fail(head.GetPos());
						}
						Reader body(colon + 1, statementEnd);
						SmallVector<SubPath, 4> paths;
						if (!ReadShapes(body, paths)) {
							return false;
						}
						if (keep) {
							_paints.push_back(paint);
							_paths.push_back(Death::move(paths));
						}
						p = statementEnd + 1;
						continue;
					}
					if (kind == "stroke"_s) {
						paint.Stroke = true;
					} else if (kind != "fill"_s || colon == statementEnd) {
						return Fail(head.GetPos());
					}
					if (!head.Color(_colors, paint.Color)) {
						return Fail(head.GetPos());
					}
					bool hasWidth = !paint.Stroke;
					while (!head.AtEnd()) {
						if (head.IsNumberNext()) {
							float value;
							if (!head.Number(value)) {
								return Fail(head.GetPos());
							}
							if (!hasWidth) {
								paint.Width = value;
								hasWidth = true;
							} else {
								// An opacity
								float alpha = float(paint.Color >> 24) * std::clamp(value, 0.0f, 1.0f);
								paint.Color = (paint.Color & 0x00FFFFFFu) | (std::uint32_t(alpha + 0.5f) << 24);
							}
							continue;
						}
						StringView word = head.Word();
						if (word == "round"_s) {
							paint.Cap = LineCap::Round;
							paint.Join = LineJoin::Round;
						} else if (word == "round-cap"_s) {
							paint.Cap = LineCap::Round;
						} else if (word == "round-join"_s) {
							paint.Join = LineJoin::Round;
						} else if (word == "butt"_s) {
							paint.Cap = LineCap::Butt;
						} else if (word == "square"_s) {
							paint.Cap = LineCap::Square;
						} else if (word == "miter"_s) {
							paint.Join = LineJoin::Miter;
						} else if (word == "bevel"_s) {
							paint.Join = LineJoin::Bevel;
						} else {
							return Fail(head.GetPos());
						}
					}
					if (!hasWidth || (paint.Stroke && paint.Width <= 0.0f)) {
						return Fail(head.GetPos());
					}

					Reader body(colon + 1, statementEnd);
					SmallVector<SubPath, 4> paths;
					if (!ReadShapes(body, paths)) {
						return false;
					}
					if (keep) {
						_paints.push_back(paint);
						_paths.push_back(Death::move(paths));
					}
					p = statementEnd + 1;
				}
				return true;
			}

			void Draw(std::int32_t size, std::uint8_t* pixels, std::int32_t stride)
			{
				// Premultiplied, so the layers add up as they're drawn over each other
				SmallVector<float, 0> color;
				color.assign(std::size_t(size) * std::size_t(size) * 4, 0.0f);
				for (std::int32_t y = 0; y < size; y++) {
					const std::uint8_t* row = pixels + std::size_t(y) * std::size_t(stride);
					for (std::int32_t x = 0; x < size; x++) {
						float a = float(row[x * 4 + 3]) / 255.0f;
						float* c = &color[(std::size_t(y) * std::size_t(size) + std::size_t(x)) * 4];
						c[0] = float(row[x * 4]) / 255.0f * a;
						c[1] = float(row[x * 4 + 1]) / 255.0f * a;
						c[2] = float(row[x * 4 + 2]) / 255.0f * a;
						c[3] = a;
					}
				}

				SmallVector<std::uint8_t, 0> coverage, part;
				// The outlines first, under everything
				for (std::size_t i = 0; i < _paints.size(); i++) {
					const Paint& paint = _paints[i];
					if (paint.Outline <= 0.0f) {
						continue;
					}
					float outline = paint.Outline * _scale;
					coverage.assign(std::size_t(size) * std::size_t(size), 0);
					for (const SubPath& path : _paths[i]) {
						Polygons polygons;
						if (paint.Stroke) {
							float halfWidth = paint.Width * _scale * 0.5f;
							StrokePath(path, halfWidth + outline, paint.Cap, (paint.Cap == LineCap::Square ? halfWidth : 0.0f) + outline, paint.Join, polygons);
						} else {
							SubPath closedPath = path;
							closedPath.Closed = true;
							polygons.AddPolygon(path.Points);
							Rasterize(polygons, size, part);
							MaxInto(coverage, part);
							polygons = {};
							StrokePath(closedPath, outline, LineCap::Round, 0.0f, LineJoin::Round, polygons);
						}
						Rasterize(polygons, size, part);
						MaxInto(coverage, part);
					}
					Composite(color, coverage, paint.OutlineColor);
				}

				for (std::size_t i = 0; i < _paints.size(); i++) {
					const Paint& paint = _paints[i];
					if (paint.Stroke) {
						coverage.assign(std::size_t(size) * std::size_t(size), 0);
						float halfWidth = paint.Width * _scale * 0.5f;
						for (const SubPath& path : _paths[i]) {
							Polygons polygons;
							StrokePath(path, halfWidth, paint.Cap, (paint.Cap == LineCap::Square ? halfWidth : 0.0f), paint.Join, polygons);
							Rasterize(polygons, size, part);
							MaxInto(coverage, part);
						}
					} else {
						// All the shapes of a fill together, so one going around another the other way cuts a hole
						Polygons polygons;
						for (const SubPath& path : _paths[i]) {
							polygons.AddPolygon(path.Points);
						}
						Rasterize(polygons, size, coverage);
					}
					if (paint.Clear) {
						Erase(color, coverage);
					} else {
						Composite(color, coverage, paint.Color);
					}
				}

				for (std::int32_t y = 0; y < size; y++) {
					std::uint8_t* row = pixels + std::size_t(y) * std::size_t(stride);
					for (std::int32_t x = 0; x < size; x++) {
						const float* c = &color[(std::size_t(y) * std::size_t(size) + std::size_t(x)) * 4];
						float a = std::clamp(c[3], 0.0f, 1.0f);
						std::uint8_t* out = &row[x * 4];
						if (a <= 0.0f) {
							out[0] = out[1] = out[2] = out[3] = 0;
							continue;
						}
						out[0] = std::uint8_t(std::clamp(c[0] / a, 0.0f, 1.0f) * 255.0f + 0.5f);
						out[1] = std::uint8_t(std::clamp(c[1] / a, 0.0f, 1.0f) * 255.0f + 0.5f);
						out[2] = std::uint8_t(std::clamp(c[2] / a, 0.0f, 1.0f) * 255.0f + 0.5f);
						out[3] = std::uint8_t(a * 255.0f + 0.5f);
					}
				}
			}

			const char* GetErrorAt() const
			{
				return _errorAt;
			}

		private:
			StringView _source;
			float _scale;
			VectorArt::ColorFunc _colors;
			const char* _errorAt;
			SmallVector<Paint, 8> _paints;
			SmallVector<SmallVector<SubPath, 4>, 8> _paths;

			bool Fail(const char* at)
			{
				_errorAt = at;
				return false;
			}

			static void Erase(SmallVectorImpl<float>& color, const SmallVectorImpl<std::uint8_t>& coverage)
			{
				for (std::size_t i = 0; i < coverage.size(); i++) {
					float keep = 1.0f - float(coverage[i]) / 255.0f;
					float* c = &color[i * 4];
					c[0] *= keep;
					c[1] *= keep;
					c[2] *= keep;
					c[3] *= keep;
				}
			}

			static void Composite(SmallVectorImpl<float>& color, const SmallVectorImpl<std::uint8_t>& coverage, std::uint32_t rgba)
			{
				float r = float(rgba & 0xFF) / 255.0f;
				float g = float((rgba >> 8) & 0xFF) / 255.0f;
				float b = float((rgba >> 16) & 0xFF) / 255.0f;
				float alpha = float(rgba >> 24) / 255.0f;
				for (std::size_t i = 0; i < coverage.size(); i++) {
					if (coverage[i] == 0) {
						continue;
					}
					float a = float(coverage[i]) / 255.0f * alpha;
					float* c = &color[i * 4];
					c[0] = r * a + c[0] * (1.0f - a);
					c[1] = g * a + c[1] * (1.0f - a);
					c[2] = b * a + c[2] * (1.0f - a);
					c[3] = a + c[3] * (1.0f - a);
				}
			}

			Vector2f ToPixels(Vector2f p) const
			{
				return p * _scale;
			}

			void AddEllipse(SmallVectorImpl<SubPath>& paths, Vector2f center, float rx, float ry)
			{
				SubPath& path = paths.emplace_back();
				path.Closed = true;
				std::int32_t n = GetCircleSegments(std::max(rx, ry) * _scale);
				for (std::int32_t i = 0; i < n; i++) {
					float angle = 2.0f * Pi * float(i) / float(n);
					path.Points.push_back(ToPixels(center + Vector2f(std::cos(angle) * rx, std::sin(angle) * ry)));
				}
			}

			void AddRect(SmallVectorImpl<SubPath>& paths, float x, float y, float w, float h, float radius)
			{
				SubPath& path = paths.emplace_back();
				path.Closed = true;
				radius = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
				if (radius <= 0.0f) {
					path.Points.push_back(ToPixels(Vector2f(x, y)));
					path.Points.push_back(ToPixels(Vector2f(x + w, y)));
					path.Points.push_back(ToPixels(Vector2f(x + w, y + h)));
					path.Points.push_back(ToPixels(Vector2f(x, y + h)));
					return;
				}
				SmallVector<Vector2f, 64> points;
				float r = radius * _scale;
				AddArc(points, ToPixels(Vector2f(x + w - radius, y + radius)), r, -0.5f * Pi, 0.5f * Pi, true);
				AddArc(points, ToPixels(Vector2f(x + w - radius, y + h - radius)), r, 0.0f, 0.5f * Pi, true);
				AddArc(points, ToPixels(Vector2f(x + radius, y + h - radius)), r, 0.5f * Pi, 0.5f * Pi, true);
				AddArc(points, ToPixels(Vector2f(x + radius, y + radius)), r, Pi, 0.5f * Pi, true);
				for (const Vector2f& p : points) {
					path.Points.push_back(p);
				}
			}

			void AddCubic(SubPath& path, Vector2f p0, Vector2f c1, Vector2f c2, Vector2f p1)
			{
				float length = ((c1 - p0).Length() + (c2 - c1).Length() + (p1 - c2).Length()) * _scale;
				std::int32_t n = std::clamp(std::int32_t(std::ceil(std::sqrt(length) * 2.5f)), 2, 128);
				for (std::int32_t i = 1; i <= n; i++) {
					float t = float(i) / float(n), u = 1.0f - t;
					Vector2f p = p0 * (u * u * u) + c1 * (3.0f * u * u * t) + c2 * (3.0f * u * t * t) + p1 * (t * t * t);
					path.Points.push_back(ToPixels(p));
				}
			}

			void AddQuadratic(SubPath& path, Vector2f p0, Vector2f c, Vector2f p1)
			{
				float length = ((c - p0).Length() + (p1 - c).Length()) * _scale;
				std::int32_t n = std::clamp(std::int32_t(std::ceil(std::sqrt(length) * 2.5f)), 2, 128);
				for (std::int32_t i = 1; i <= n; i++) {
					float t = float(i) / float(n), u = 1.0f - t;
					Vector2f p = p0 * (u * u) + c * (2.0f * u * t) + p1 * (t * t);
					path.Points.push_back(ToPixels(p));
				}
			}

			// An arc of an ellipse as SVG gives it, by its end points, see "Elliptical arc implementation notes" of SVG
			void AddArcTo(SubPath& path, Vector2f p0, float rx, float ry, float rotation, bool largeArc, bool sweep, Vector2f p1)
			{
				if ((p1 - p0).SqrLength() < 1e-12f) {
					return;
				}
				rx = std::abs(rx);
				ry = std::abs(ry);
				if (rx < 1e-6f || ry < 1e-6f) {
					path.Points.push_back(ToPixels(p1));
					return;
				}
				float phi = rotation * Pi / 180.0f;
				float cosPhi = std::cos(phi), sinPhi = std::sin(phi);
				float dx = (p0.X - p1.X) * 0.5f, dy = (p0.Y - p1.Y) * 0.5f;
				float x1 = cosPhi * dx + sinPhi * dy;
				float y1 = -sinPhi * dx + cosPhi * dy;
				float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
				if (lambda > 1.0f) {
					float s = std::sqrt(lambda);
					rx *= s;
					ry *= s;
				}
				float numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
				float denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
				float coef = (denominator > 0.0f ? std::sqrt(std::max(numerator / denominator, 0.0f)) : 0.0f);
				if (largeArc == sweep) {
					coef = -coef;
				}
				float cx1 = coef * rx * y1 / ry;
				float cy1 = -coef * ry * x1 / rx;
				Vector2f center(cosPhi * cx1 - sinPhi * cy1 + (p0.X + p1.X) * 0.5f, sinPhi * cx1 + cosPhi * cy1 + (p0.Y + p1.Y) * 0.5f);
				float start = std::atan2((y1 - cy1) / ry, (x1 - cx1) / rx);
				float delta = std::atan2((-y1 - cy1) / ry, (-x1 - cx1) / rx) - start;
				if (sweep && delta < 0.0f) {
					delta += 2.0f * Pi;
				} else if (!sweep && delta > 0.0f) {
					delta -= 2.0f * Pi;
				}
				std::int32_t n = std::max(1, std::int32_t(std::ceil(float(GetCircleSegments(std::max(rx, ry) * _scale)) * std::abs(delta) / (2.0f * Pi))));
				for (std::int32_t i = 1; i <= n; i++) {
					float t = start + delta * float(i) / float(n);
					float ex = rx * std::cos(t), ey = ry * std::sin(t);
					path.Points.push_back(ToPixels(Vector2f(center.X + ex * cosPhi - ey * sinPhi, center.Y + ex * sinPhi + ey * cosPhi)));
				}
			}

			bool ReadPoint(Reader& reader, Vector2f& p)
			{
				return (reader.Number(p.X) && reader.Number(p.Y));
			}

			bool ReadShapes(Reader& reader, SmallVectorImpl<SubPath>& paths)
			{
				Vector2f current, start, lastControl;
				char lastCommand = '\0';
				SubPath* path = nullptr;

				auto beginPath = [&](Vector2f at) {
					path = &paths.emplace_back();
					path->Points.push_back(ToPixels(at));
					start = at;
				};
				auto ensurePath = [&]() {
					if (path == nullptr) {
						beginPath(current);
					}
				};

				while (!reader.AtEnd()) {
					const char* at = reader.GetPos();
					char command;
					if (reader.IsNumberNext()) {
						// The last command again, a move goes on as lines
						if (lastCommand == '\0') {
							return Fail(at);
						}
						command = (lastCommand == 'M' ? 'L' : (lastCommand == 'm' ? 'l' : lastCommand));
					} else {
						StringView word = reader.Word();
						if (word.empty()) {
							return Fail(at);
						}
						if (word.size() > 1) {
							float values[5] = {};
							if (word == "circle"_s) {
								if (!reader.Number(values[0]) || !reader.Number(values[1]) || !reader.Number(values[2]) || values[2] <= 0.0f) {
									return Fail(reader.GetPos());
								}
								AddEllipse(paths, Vector2f(values[0], values[1]), values[2], values[2]);
							} else if (word == "ellipse"_s) {
								if (!reader.Number(values[0]) || !reader.Number(values[1]) || !reader.Number(values[2]) || !reader.Number(values[3]) ||
									values[2] <= 0.0f || values[3] <= 0.0f) {
									return Fail(reader.GetPos());
								}
								AddEllipse(paths, Vector2f(values[0], values[1]), values[2], values[3]);
							} else if (word == "rect"_s) {
								if (!reader.Number(values[0]) || !reader.Number(values[1]) || !reader.Number(values[2]) || !reader.Number(values[3]) ||
									values[2] <= 0.0f || values[3] <= 0.0f) {
									return Fail(reader.GetPos());
								}
								if (reader.IsNumberNext() && !reader.Number(values[4])) {
									return Fail(reader.GetPos());
								}
								AddRect(paths, values[0], values[1], values[2], values[3], values[4]);
							} else {
								return Fail(at);
							}
							// A shape is a path of its own, a command after it starts at its end
							path = nullptr;
							lastCommand = '\0';
							continue;
						}
						command = word[0];
					}

					bool relative = (command >= 'a' && command <= 'z');
					Vector2f origin = (relative ? current : Vector2f::Zero);
					switch (command) {
						case 'M': case 'm': {
							Vector2f p;
							if (!ReadPoint(reader, p)) {
								return Fail(reader.GetPos());
							}
							current = origin + p;
							beginPath(current);
							lastControl = current;
							break;
						}
						case 'L': case 'l': {
							Vector2f p;
							if (!ReadPoint(reader, p)) {
								return Fail(reader.GetPos());
							}
							ensurePath();
							current = origin + p;
							path->Points.push_back(ToPixels(current));
							lastControl = current;
							break;
						}
						case 'H': case 'h': {
							float x;
							if (!reader.Number(x)) {
								return Fail(reader.GetPos());
							}
							ensurePath();
							current.X = (relative ? current.X + x : x);
							path->Points.push_back(ToPixels(current));
							lastControl = current;
							break;
						}
						case 'V': case 'v': {
							float y;
							if (!reader.Number(y)) {
								return Fail(reader.GetPos());
							}
							ensurePath();
							current.Y = (relative ? current.Y + y : y);
							path->Points.push_back(ToPixels(current));
							lastControl = current;
							break;
						}
						case 'C': case 'c': {
							Vector2f c1, c2, p;
							if (!ReadPoint(reader, c1) || !ReadPoint(reader, c2) || !ReadPoint(reader, p)) {
								return Fail(reader.GetPos());
							}
							ensurePath();
							AddCubic(*path, current, origin + c1, origin + c2, origin + p);
							lastControl = origin + c2;
							current = origin + p;
							break;
						}
						case 'S': case 's': {
							Vector2f c2, p;
							if (!ReadPoint(reader, c2) || !ReadPoint(reader, p)) {
								return Fail(reader.GetPos());
							}
							ensurePath();
							bool smooth = (lastCommand == 'C' || lastCommand == 'c' || lastCommand == 'S' || lastCommand == 's');
							Vector2f c1 = (smooth ? current * 2.0f - lastControl : current);
							AddCubic(*path, current, c1, origin + c2, origin + p);
							lastControl = origin + c2;
							current = origin + p;
							break;
						}
						case 'Q': case 'q': {
							Vector2f c, p;
							if (!ReadPoint(reader, c) || !ReadPoint(reader, p)) {
								return Fail(reader.GetPos());
							}
							ensurePath();
							AddQuadratic(*path, current, origin + c, origin + p);
							lastControl = origin + c;
							current = origin + p;
							break;
						}
						case 'T': case 't': {
							Vector2f p;
							if (!ReadPoint(reader, p)) {
								return Fail(reader.GetPos());
							}
							ensurePath();
							bool smooth = (lastCommand == 'Q' || lastCommand == 'q' || lastCommand == 'T' || lastCommand == 't');
							Vector2f c = (smooth ? current * 2.0f - lastControl : current);
							AddQuadratic(*path, current, c, origin + p);
							lastControl = c;
							current = origin + p;
							break;
						}
						case 'A': case 'a': {
							float rx, ry, rotation;
							bool largeArc, sweep;
							Vector2f p;
							if (!reader.Number(rx) || !reader.Number(ry) || !reader.Number(rotation) || !reader.Flag(largeArc) || !reader.Flag(sweep) ||
								!ReadPoint(reader, p)) {
								return Fail(reader.GetPos());
							}
							ensurePath();
							AddArcTo(*path, current, rx, ry, rotation, largeArc, sweep, origin + p);
							current = origin + p;
							lastControl = current;
							break;
						}
						case 'Z': case 'z': {
							if (path != nullptr) {
								path->Closed = true;
								path = nullptr;
							}
							current = start;
							lastControl = current;
							break;
						}
						default:
							return Fail(at);
					}
					lastCommand = command;
				}
				return true;
			}
		};

		void LogError(StringView source, const char* at)
		{
			std::size_t offset = (at != nullptr ? std::size_t(at - source.data()) : 0);
			LOGW("Vector art can't be read at {}: \"{}\"", offset, source);
		}
	}

	bool VectorArt::Draw(StringView source, float designSize, std::int32_t size, std::uint8_t* pixels, std::int32_t stride, ColorFunc colors)
	{
		if (size <= 0 || designSize <= 0.0f || pixels == nullptr) {
			return false;
		}
		Drawer drawer(source, float(size) / designSize, colors);
		if (!drawer.Read(true)) {
			LogError(source, drawer.GetErrorAt());
			return false;
		}
		drawer.Draw(size, pixels, stride);
		return true;
	}

	bool VectorArt::IsValid(StringView source, ColorFunc colors)
	{
		Drawer drawer(source, 1.0f, colors);
		return drawer.Read(false);
	}
}

#endif
