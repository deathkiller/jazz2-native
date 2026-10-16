#include "UiSkin.h"

#if defined(WITH_EDITOR)

#include "UiTheme.h"

#include <Containers/SmallVector.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Jazz2::Editor
{
	using Death::Containers::SmallVector;
	using Death::Containers::SmallVectorImpl;

	namespace
	{
		// Signed distance from a rounded box centered at the origin, negative inside
		float RoundedBoxDistance(float px, float py, float halfWidth, float halfHeight, float radius)
		{
			float qx = std::abs(px) - (halfWidth - radius);
			float qy = std::abs(py) - (halfHeight - radius);
			float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
			float inside = std::min(std::max(qx, qy), 0.0f);
			return outside + inside - radius;
		}

		float Coverage(float distance)
		{
			return std::clamp(0.5f - distance, 0.0f, 1.0f);
		}

		void PutPixel(SmallVectorImpl<std::uint8_t>& pixels, std::int32_t stride, std::int32_t x, std::int32_t y, float alpha)
		{
			std::uint8_t* p = &pixels[(std::size_t(y) * stride + x) * 4];
			p[0] = 255;
			p[1] = 255;
			p[2] = 255;
			p[3] = std::uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
		}
	}

	UiSkin::UiSkin()
		: _scale(0.0f), _roundFill{}, _roundRing{}, _shadow{}, _shadowSpread(0)
	{
	}

	UiSkin::~UiSkin()
	{
	}

	void UiSkin::Build(float scale)
	{
		if (_shapes != nullptr && std::abs(scale - _scale) < 0.001f) {
			return;
		}
		_scale = scale;

		std::int32_t radius = std::max(2, std::int32_t(std::round(UiTheme::CornerRadius * scale)));
		std::int32_t border = std::max(1, std::int32_t(std::round(scale)));
		std::int32_t spread = std::max(3, std::int32_t(std::round(UiTheme::ShadowSize * scale)));

		SmallVector<std::uint8_t, 0> pixels;
		pixels.resize(std::size_t(ShapesWidth) * ShapesHeight * 4, 0);

		std::int32_t x = 1;

		// Filled rounded rectangle, the corners and a stretched middle of two pixels
		{
			std::int32_t size = radius * 2 + 2;
			float half = size * 0.5f;
			for (std::int32_t py = 0; py < size; py++) {
				for (std::int32_t px = 0; px < size; px++) {
					float d = RoundedBoxDistance(px + 0.5f - half, py + 0.5f - half, half, half, float(radius));
					PutPixel(pixels, ShapesWidth, x + px, 1 + py, Coverage(d));
				}
			}
			_roundFill.Rect = Recti(x, 1, size, size);
			_roundFill.Corner = radius;
			x += size + 2;
		}

		// Border of a rounded rectangle
		{
			std::int32_t size = radius * 2 + 2;
			float half = size * 0.5f;
			for (std::int32_t py = 0; py < size; py++) {
				for (std::int32_t px = 0; px < size; px++) {
					float d = RoundedBoxDistance(px + 0.5f - half, py + 0.5f - half, half, half, float(radius));
					PutPixel(pixels, ShapesWidth, x + px, 1 + py, Coverage(d) - Coverage(d + float(border)));
				}
			}
			_roundRing.Rect = Recti(x, 1, size, size);
			_roundRing.Corner = radius;
			x += size + 2;
		}

		// Soft shadow, the corners include how far it reaches out
		{
			std::int32_t corner = spread + radius;
			std::int32_t size = corner * 2 + 2;
			float half = size * 0.5f;
			float innerHalf = half - float(spread);
			for (std::int32_t py = 0; py < size; py++) {
				for (std::int32_t px = 0; px < size; px++) {
					float d = RoundedBoxDistance(px + 0.5f - half, py + 0.5f - half, innerHalf, innerHalf, float(radius));
					float t = std::clamp(d / float(spread), 0.0f, 1.0f);
					float alpha = (1.0f - t);
					PutPixel(pixels, ShapesWidth, x + px, 1 + py, alpha * alpha);
				}
			}
			_shadow.Rect = Recti(x, 1, size, size);
			_shadow.Corner = corner;
			_shadowSpread = spread;
			x += size + 2;
		}

		// Filled circle
		{
			constexpr std::int32_t size = 32;
			float half = size * 0.5f;
			for (std::int32_t py = 0; py < size; py++) {
				for (std::int32_t px = 0; px < size; px++) {
					float dx = px + 0.5f - half, dy = py + 0.5f - half;
					float d = std::sqrt(dx * dx + dy * dy) - (half - 0.5f);
					PutPixel(pixels, ShapesWidth, x + px, 1 + py, Coverage(d));
				}
			}
			_circle = Recti(x, 1, size, size);
			x += size + 2;
		}

		DEATH_DEBUG_ASSERT(x <= ShapesWidth, "Shapes of the interface don't fit their texture", );

		if (_shapes == nullptr) {
			_shapes = std::make_unique<Texture>("EditorShapes", Texture::Format::RGBA8, ShapesWidth, ShapesHeight);
		}
		_shapes->LoadFromTexels(pixels.data(), 0, 0, ShapesWidth, ShapesHeight);
		_shapes->SetMinFiltering(SamplerFilter::Linear);
		_shapes->SetMagFiltering(SamplerFilter::Linear);
		_shapes->SetWrap(SamplerWrapping::ClampToEdge);

		if (_icons == nullptr) {
			BuildIcons();
		}
	}

	Vector4f UiSkin::GetShapeTexCoords(const Recti& rect) const
	{
		return Vector4f(float(rect.W) / ShapesWidth, float(rect.X) / ShapesWidth, float(rect.H) / ShapesHeight, float(rect.Y) / ShapesHeight);
	}

	Vector4f UiSkin::GetIconTexCoords(EditorIcon icon) const
	{
		Vector2i size = _icons->GetSize();
		std::int32_t index = std::int32_t(icon);
		std::int32_t x = (index % IconsPerRow) * EditorIcons::Size;
		std::int32_t y = (index / IconsPerRow) * EditorIcons::Size;
		return Vector4f(float(EditorIcons::Size) / size.X, float(x) / size.X, float(EditorIcons::Size) / size.Y, float(y) / size.Y);
	}

	void UiSkin::BuildIcons()
	{
		constexpr std::int32_t Count = std::int32_t(EditorIcon::Count);
		constexpr std::int32_t Rows = (Count + IconsPerRow - 1) / IconsPerRow;
		constexpr std::int32_t Width = IconsPerRow * EditorIcons::Size;
		constexpr std::int32_t Height = Rows * EditorIcons::Size;

		SmallVector<std::uint8_t, 0> pixels;
		pixels.resize(std::size_t(Width) * Height * 4, 0);

		for (std::int32_t i = 0; i < Count; i++) {
			const char* const* rows = EditorIcons::GetRows(EditorIcon(i));
			std::int32_t ox = (i % IconsPerRow) * EditorIcons::Size;
			std::int32_t oy = (i / IconsPerRow) * EditorIcons::Size;
			for (std::int32_t y = 0; y < EditorIcons::Size; y++) {
				const char* row = rows[y];
				for (std::int32_t x = 0; x < EditorIcons::Size && row[x] != '\0'; x++) {
					float alpha = (row[x] == '#' ? 1.0f : (row[x] == '+' ? 0.5f : 0.0f));
					if (alpha > 0.0f) {
						PutPixel(pixels, Width, ox + x, oy + y, alpha);
					}
				}
			}
		}

		_icons = std::make_unique<Texture>("EditorIcons", Texture::Format::RGBA8, Width, Height);
		_icons->LoadFromTexels(pixels.data(), 0, 0, Width, Height);
		_icons->SetMinFiltering(SamplerFilter::Linear);
		_icons->SetMagFiltering(SamplerFilter::Nearest);
		_icons->SetWrap(SamplerWrapping::ClampToEdge);
	}
}

#endif
