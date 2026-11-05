#include "Skin.h"

#if defined(WITH_UI_WINDOWS)

#include "Theme.h"
#include "VectorArt.h"

#include <Containers/SmallVector.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Jazz2::UI::Windows
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

	Skin::Skin()
		: _atlasWidth(0), _atlasHeight(0), _scale(0.0f), _roundFill{}, _roundRing{}, _shadow{}, _shadowSpread(0), _iconSize(Icons::PixelArtSize),
			_iconsPerRow(1), _iconsRevision(0)
	{
	}

	Skin::~Skin()
	{
	}

	void Skin::Build(float scale)
	{
		if (_atlas != nullptr && std::abs(scale - _scale) < 0.001f && _iconsRevision == Icons::GetRevision()) {
			return;
		}
		_scale = scale;
		_iconsRevision = Icons::GetRevision();

		std::int32_t radius = std::max<std::int32_t>(2, std::int32_t(std::round(Theme::CornerRadius * scale)));
		std::int32_t border = std::max<std::int32_t>(1, std::int32_t(std::round(scale)));
		std::int32_t spread = std::max<std::int32_t>(3, std::int32_t(std::round(Theme::ShadowSize * scale)));
		// As large as an icon is meant to be at this scale, vector art is drawn at it, pixel art is enlarged to it,
		// unevenly at a scale between whole ones, which keeps it crisp and inside of what it's drawn in
		_iconSize = std::max<std::int32_t>(8, std::int32_t(std::round(Theme::IconSize * scale)));

		// The shapes in a row at the top, each with a gap around it, so the smoothing doesn't take in its neighbours
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
		_white = Recti(x, 1, WhiteSize, WhiteSize);
		x += WhiteSize + 1;
		std::int32_t shapesWidth = x;
		std::int32_t shapesHeight = 1 + std::max({ roundSize, shadowSize, WhiteSize }) + 1;

		// Circles in a row below them, from the largest, each half as large as the one before
		std::int32_t circlesY = shapesHeight + 1;
		x = 1;
		for (std::int32_t i = 0; i < CircleCount; i++) {
			std::int32_t size = (LargestCircle >> i);
			_circles[i] = Recti(x, circlesY, size, size);
			x += size + 2;
		}
		shapesWidth = std::max(shapesWidth, x);
		std::int32_t circlesHeight = LargestCircle + 2;

		// The icons below them, wrapped to more rows where they would make the texture too wide
		std::int32_t iconCell = _iconSize + 1;
		const std::int32_t IconCount = Icons::GetCount();
		_iconsPerRow = std::clamp<std::int32_t>((MaxAtlasWidth - 1) / iconCell, 1, MaxIconsPerRow);
		std::int32_t iconRows = (IconCount + _iconsPerRow - 1) / _iconsPerRow;
		_iconsOrigin = Vector2i(1, circlesY + circlesHeight + 1);

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

		// Filled circles, each drawn at its size, so one at most half as large again is drawn smoothly from it
		for (const Recti& circle : _circles) {
			VectorArt::Draw("fill #: circle 0.5 0.5 0.5", 1.0f, circle.W, &pixels[(std::size_t(circle.Y) * std::size_t(width) + std::size_t(circle.X)) * 4],
				width * 4, Icons::GetPixelColor);
		}

		// White, sampled in its middle, which no smoothing reaches into
		for (std::int32_t py = 0; py < WhiteSize; py++) {
			for (std::int32_t px = 0; px < WhiteSize; px++) {
				PutPixel(pixels, width, height, _white.X + px, _white.Y + py, 1.0f);
			}
		}

		for (std::int32_t i = 0; i < IconCount; i++) {
			std::int32_t ox = _iconsOrigin.X + (i % _iconsPerRow) * iconCell;
			std::int32_t oy = _iconsOrigin.Y + (i / _iconsPerRow) * iconCell;
			if (ox + _iconSize <= width && oy + _iconSize <= height) {
				DrawIcon(Icons::GetArt(i), _iconSize, &pixels[(std::size_t(oy) * std::size_t(width) + std::size_t(ox)) * 4], width * 4);
			}
		}

		if (_atlas == nullptr || _atlasWidth != width || _atlasHeight != height) {
			_atlas = std::make_unique<Texture>("WindowsAtlas", Texture::Format::RGBA8, width, height);
			_atlasWidth = width;
			_atlasHeight = height;
		}
		_atlas->LoadFromTexels(pixels.data(), 0, 0, width, height);
		// Everything is drawn at the size it's made in, only the circles and slanted lines are scaled, so smoothing
		// doesn't blur the icons
		_atlas->SetMinFiltering(SamplerFilter::Linear);
		_atlas->SetMagFiltering(SamplerFilter::Linear);
		_atlas->SetWrap(SamplerWrapping::ClampToEdge);
	}

	void Skin::DrawIcon(const IconArt& art, std::int32_t size, std::uint8_t* pixels, std::int32_t stride)
	{
		// Vector art at the size of an icon, or each pixel of pixel art becomes as many pixels as the size needs
		if (art.Vector != nullptr && VectorArt::Draw(art.Vector, Icons::DesignSize, size, pixels, stride, Icons::GetPixelColor)) {
			return;
		}
		const char* const* rows = art.PixelArt;
		if (rows == nullptr) {
			return;
		}
		for (std::int32_t y = 0; y < size; y++) {
			const char* row = rows[y * Icons::PixelArtSize / size];
			std::int32_t rowLength = std::int32_t(std::strlen(row));
			for (std::int32_t x = 0; x < size; x++) {
				std::int32_t sourceX = x * Icons::PixelArtSize / size;
				if (sourceX >= rowLength) {
					break;
				}
				// White for those drawn in a single color, the colors of their own for those in the style of the game
				std::uint32_t color = Icons::GetPixelColor(row[sourceX]);
				if ((color >> 24) != 0) {
					std::uint8_t* p = &pixels[std::size_t(y) * std::size_t(stride) + std::size_t(x) * 4];
					p[0] = std::uint8_t(color & 0xFF);
					p[1] = std::uint8_t((color >> 8) & 0xFF);
					p[2] = std::uint8_t((color >> 16) & 0xFF);
					p[3] = std::uint8_t(color >> 24);
				}
			}
		}
	}
	Vector4f Skin::GetShapeTexCoords(const Recti& rect) const
	{
		float width = float(std::max<std::int32_t>(_atlasWidth, 1)), height = float(std::max<std::int32_t>(_atlasHeight, 1));
		return Vector4f(float(rect.W) / width, float(rect.X) / width, float(rect.H) / height, float(rect.Y) / height);
	}

	Vector4f Skin::GetWhiteTexCoords() const
	{
		// No extent, every pixel samples the middle of the white patch
		float width = float(std::max<std::int32_t>(_atlasWidth, 1)), height = float(std::max<std::int32_t>(_atlasHeight, 1));
		return Vector4f(0.0f, (float(_white.X) + _white.W * 0.5f) / width, 0.0f, (float(_white.Y) + _white.H * 0.5f) / height);
	}

	const Recti& Skin::GetCircle(float diameter) const
	{
		// The smallest one at least as large, it's made smaller smoothly, a larger one than all is enlarged
		std::int32_t i = CircleCount - 1;
		while (i > 0 && float(_circles[i].W) < diameter) {
			i--;
		}
		return _circles[i];
	}

	Vector4f Skin::GetIconTexCoords(IconId icon) const
	{
		std::int32_t index = Icons::GetIndex(icon);
		if (index < 0) {
			index = 0;
		}
		std::int32_t x = _iconsOrigin.X + (index % _iconsPerRow) * (_iconSize + 1);
		std::int32_t y = _iconsOrigin.Y + (index / _iconsPerRow) * (_iconSize + 1);
		return GetShapeTexCoords(Recti(x, y, _iconSize, _iconSize));
	}
}

#endif
