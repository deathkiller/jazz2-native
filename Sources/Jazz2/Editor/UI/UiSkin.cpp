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

		// Pixels outside of the texture are left out, the sizes are computed from the scale, so a mistake there
		// mustn't write past the buffer
		void PutPixel(SmallVectorImpl<std::uint8_t>& pixels, std::int32_t width, std::int32_t height, std::int32_t x, std::int32_t y, float alpha)
		{
			if (x < 0 || y < 0 || x >= width || y >= height) {
				return;
			}
			std::uint8_t* p = &pixels[(std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4];
			p[0] = 255;
			p[1] = 255;
			p[2] = 255;
			p[3] = std::uint8_t(std::clamp(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
		}

		std::int32_t NextPowerOfTwo(std::int32_t value)
		{
			std::int32_t result = 1;
			while (result < value) {
				result <<= 1;
			}
			return result;
		}
	}

	UiSkin::UiSkin()
		: _atlasWidth(0), _atlasHeight(0), _scale(0.0f), _roundFill{}, _roundRing{}, _shadow{}, _shadowSpread(0), _iconSize(EditorIcons::Size),
			_iconsPerRow(1)
	{
	}

	UiSkin::~UiSkin()
	{
	}

	void UiSkin::Build(float scale)
	{
		if (_atlas != nullptr && std::abs(scale - _scale) < 0.001f) {
			return;
		}
		_scale = scale;

		std::int32_t radius = std::max<std::int32_t>(2, std::int32_t(std::round(UiTheme::CornerRadius * scale)));
		std::int32_t border = std::max<std::int32_t>(1, std::int32_t(std::round(scale)));
		std::int32_t spread = std::max<std::int32_t>(3, std::int32_t(std::round(UiTheme::ShadowSize * scale)));
		// As large as an icon is meant to be at this scale, the pixel art is enlarged to it, unevenly at a scale between
		// whole ones, which keeps it crisp and inside of what it's drawn in
		_iconSize = std::max<std::int32_t>(8, std::int32_t(std::round(UiTheme::IconSize * scale)));

		// The shapes in a row at the top, each with a gap around it, so the smoothing doesn't take in its neighbours
		constexpr std::int32_t CircleSize = 32;
		constexpr std::int32_t WhiteSize = 4;
		std::int32_t roundSize = radius * 2 + 2;
		std::int32_t shadowCorner = spread + radius;
		std::int32_t shadowSize = shadowCorner * 2 + 2;
		std::int32_t x = 1;
		_roundFill.Rect = Recti(x, 1, roundSize, roundSize);
		_roundFill.Corner = radius;
		x += roundSize + 2;
		_roundRing.Rect = Recti(x, 1, roundSize, roundSize);
		_roundRing.Corner = radius;
		x += roundSize + 2;
		_shadow.Rect = Recti(x, 1, shadowSize, shadowSize);
		_shadow.Corner = shadowCorner;
		_shadowSpread = spread;
		x += shadowSize + 2;
		_circle = Recti(x, 1, CircleSize, CircleSize);
		x += CircleSize + 2;
		_white = Recti(x, 1, WhiteSize, WhiteSize);
		x += WhiteSize + 1;
		std::int32_t shapesWidth = x;
		std::int32_t shapesHeight = 1 + std::max({ roundSize, shadowSize, CircleSize, WhiteSize }) + 1;

		// The icons below them, wrapped to more rows where they would make the texture too wide
		std::int32_t iconCell = _iconSize + 1;
		constexpr std::int32_t IconCount = std::int32_t(EditorIcon::Count);
		_iconsPerRow = std::clamp<std::int32_t>((MaxAtlasWidth - 1) / iconCell, 1, MaxIconsPerRow);
		std::int32_t iconRows = (IconCount + _iconsPerRow - 1) / _iconsPerRow;
		_iconsOrigin = Vector2i(1, shapesHeight + 1);

		std::int32_t width = NextPowerOfTwo(std::max(shapesWidth, 1 + _iconsPerRow * iconCell));
		std::int32_t height = NextPowerOfTwo(_iconsOrigin.Y + iconRows * iconCell);

		SmallVector<std::uint8_t, 0> pixels;
		pixels.resize(std::size_t(width) * std::size_t(height) * 4, 0);

		// Filled rounded rectangle, the corners and a stretched middle of two pixels
		{
			const Recti& rect = _roundFill.Rect;
			float half = rect.W * 0.5f;
			for (std::int32_t py = 0; py < rect.H; py++) {
				for (std::int32_t px = 0; px < rect.W; px++) {
					float d = RoundedBoxDistance(px + 0.5f - half, py + 0.5f - half, half, half, float(radius));
					PutPixel(pixels, width, height, rect.X + px, rect.Y + py, Coverage(d));
				}
			}
		}

		// Border of a rounded rectangle
		{
			const Recti& rect = _roundRing.Rect;
			float half = rect.W * 0.5f;
			for (std::int32_t py = 0; py < rect.H; py++) {
				for (std::int32_t px = 0; px < rect.W; px++) {
					float d = RoundedBoxDistance(px + 0.5f - half, py + 0.5f - half, half, half, float(radius));
					PutPixel(pixels, width, height, rect.X + px, rect.Y + py, Coverage(d) - Coverage(d + float(border)));
				}
			}
		}

		// Soft shadow, the corners include how far it reaches out
		{
			const Recti& rect = _shadow.Rect;
			float half = rect.W * 0.5f;
			float innerHalf = half - float(spread);
			for (std::int32_t py = 0; py < rect.H; py++) {
				for (std::int32_t px = 0; px < rect.W; px++) {
					float d = RoundedBoxDistance(px + 0.5f - half, py + 0.5f - half, innerHalf, innerHalf, float(radius));
					float t = std::clamp(d / float(spread), 0.0f, 1.0f);
					float alpha = (1.0f - t);
					PutPixel(pixels, width, height, rect.X + px, rect.Y + py, alpha * alpha);
				}
			}
		}

		// Filled circle
		{
			float half = CircleSize * 0.5f;
			for (std::int32_t py = 0; py < CircleSize; py++) {
				for (std::int32_t px = 0; px < CircleSize; px++) {
					float dx = px + 0.5f - half, dy = py + 0.5f - half;
					float d = std::sqrt(dx * dx + dy * dy) - (half - 0.5f);
					PutPixel(pixels, width, height, _circle.X + px, _circle.Y + py, Coverage(d));
				}
			}
		}

		// White, sampled in its middle, which no smoothing reaches into
		for (std::int32_t py = 0; py < WhiteSize; py++) {
			for (std::int32_t px = 0; px < WhiteSize; px++) {
				PutPixel(pixels, width, height, _white.X + px, _white.Y + py, 1.0f);
			}
		}

		// Icons, each pixel of the art becomes as many pixels as the size needs
		for (std::int32_t i = 0; i < IconCount; i++) {
			const char* const* rows = EditorIcons::GetRows(EditorIcon(i));
			std::int32_t ox = _iconsOrigin.X + (i % _iconsPerRow) * iconCell;
			std::int32_t oy = _iconsOrigin.Y + (i / _iconsPerRow) * iconCell;
			for (std::int32_t y = 0; y < _iconSize; y++) {
				const char* row = rows[y * EditorIcons::Size / _iconSize];
				std::int32_t rowLength = std::int32_t(std::strlen(row));
				for (std::int32_t x = 0; x < _iconSize; x++) {
					std::int32_t sourceX = x * EditorIcons::Size / _iconSize;
					if (sourceX >= rowLength) {
						break;
					}
					// White for those drawn in a single color, the colors of their own for those in the style of the game
					std::uint32_t color = EditorIcons::GetPixelColor(row[sourceX]);
					std::int32_t px = ox + x, py = oy + y;
					if ((color >> 24) != 0 && px < width && py < height) {
						std::uint8_t* p = &pixels[(std::size_t(py) * std::size_t(width) + std::size_t(px)) * 4];
						p[0] = std::uint8_t(color & 0xFF);
						p[1] = std::uint8_t((color >> 8) & 0xFF);
						p[2] = std::uint8_t((color >> 16) & 0xFF);
						p[3] = std::uint8_t(color >> 24);
					}
				}
			}
		}

		if (_atlas == nullptr || _atlasWidth != width || _atlasHeight != height) {
			_atlas = std::make_unique<Texture>("EditorAtlas", Texture::Format::RGBA8, width, height);
			_atlasWidth = width;
			_atlasHeight = height;
		}
		_atlas->LoadFromTexels(pixels.data(), 0, 0, width, height);
		// Everything is drawn at the size it's made in, only the circle and slanted lines are scaled, so smoothing
		// doesn't blur the icons
		_atlas->SetMinFiltering(SamplerFilter::Linear);
		_atlas->SetMagFiltering(SamplerFilter::Linear);
		_atlas->SetWrap(SamplerWrapping::ClampToEdge);
	}

	Vector4f UiSkin::GetShapeTexCoords(const Recti& rect) const
	{
		float width = float(std::max<std::int32_t>(_atlasWidth, 1)), height = float(std::max<std::int32_t>(_atlasHeight, 1));
		return Vector4f(float(rect.W) / width, float(rect.X) / width, float(rect.H) / height, float(rect.Y) / height);
	}

	Vector4f UiSkin::GetWhiteTexCoords() const
	{
		// No extent, every pixel samples the middle of the white patch
		float width = float(std::max<std::int32_t>(_atlasWidth, 1)), height = float(std::max<std::int32_t>(_atlasHeight, 1));
		return Vector4f(0.0f, (float(_white.X) + _white.W * 0.5f) / width, 0.0f, (float(_white.Y) + _white.H * 0.5f) / height);
	}

	Vector4f UiSkin::GetIconTexCoords(EditorIcon icon) const
	{
		std::int32_t index = std::int32_t(icon);
		std::int32_t x = _iconsOrigin.X + (index % _iconsPerRow) * (_iconSize + 1);
		std::int32_t y = _iconsOrigin.Y + (index / _iconsPerRow) * (_iconSize + 1);
		return GetShapeTexCoords(Recti(x, y, _iconSize, _iconSize));
	}
}

#endif
