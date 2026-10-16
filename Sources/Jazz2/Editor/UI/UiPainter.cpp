#include "UiPainter.h"

#if defined(WITH_EDITOR)

#include "../../Rendering/PolylineRenderer.h"
#include "../../Tiles/TileMap.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	UiPainter::UiPainter(UI::Canvas& canvas, UiSkin& skin, UI::Font* font, float scale, Rendering::PolylineRenderer* lines)
		: _canvas(canvas), _skin(skin), _font(font), _scale(scale), _fontScale(scale * UiTheme::TextScale), _slot(0), _opacity(1.0f), _lines(lines)
	{
	}

	float UiPainter::Px(float units) const
	{
		return std::round(units * _scale);
	}

	std::uint16_t UiPainter::GetDepth(UiLayer layer) const
	{
		// The font draws every other glyph a depth lower (see UI::Font::DrawString()), so text takes two depths,
		// and nothing else may be drawn at the lower one --- a selection under text would cover half of its glyphs
		static constexpr std::uint8_t LayerDepths[] = {
			0,		// Back
			1,		// Fill
			2,		// Content
			4,		// Text, 3 and 4
			5,		// Overlay
			7		// OverlayText, 6 and 7
		};
		static_assert(LayerDepths[std::int32_t(UiLayer::OverlayText)] < SlotDepth, "Layers don't fit in a slot");
		return std::uint16_t(BaseDepth + _slot * SlotDepth + LayerDepths[std::int32_t(layer)]);
	}

	void UiPainter::PushClip(const Rectf& rect)
	{
		Rectf clip = rect;
		if (!_clips.empty()) {
			const Rectf& outer = _clips.back();
			float x1 = std::max(clip.X, outer.X);
			float y1 = std::max(clip.Y, outer.Y);
			float x2 = std::min(clip.X + clip.W, outer.X + outer.W);
			float y2 = std::min(clip.Y + clip.H, outer.Y + outer.H);
			// A clipping without any area would clip nothing, so an empty one is kept tiny instead
			clip = Rectf(x1, y1, std::max(x2 - x1, 0.001f), std::max(y2 - y1, 0.001f));
		}
		clip.W = std::max(clip.W, 0.001f);
		clip.H = std::max(clip.H, 0.001f);
		_clips.push_back(clip);
		_canvas.ClipRect = clip;
	}

	void UiPainter::PopClip()
	{
		if (!_clips.empty()) {
			_clips.pop_back();
		}
		_canvas.ClipRect = (_clips.empty() ? Rectf(0.0f, 0.0f, 0.0f, 0.0f) : _clips.back());
	}

	const Rectf& UiPainter::GetClip() const
	{
		return _canvas.ClipRect;
	}

	void UiPainter::PushOpacity(float opacity)
	{
		_opacities.push_back(_opacity);
		_opacity *= opacity;
	}

	void UiPainter::PopOpacity()
	{
		if (!_opacities.empty()) {
			_opacity = _opacities.back();
			_opacities.pop_back();
		}
	}

	void UiPainter::FillRect(const Rectf& rect, const Colorf& color, UiLayer layer)
	{
		if (rect.W <= 0.0f || rect.H <= 0.0f) {
			return;
		}
		_canvas.DrawSolid(Vector2f(rect.X, rect.Y), GetDepth(layer), Vector2f(rect.W, rect.H), ApplyOpacity(color));
	}

	void UiPainter::Frame(const Rectf& rect, const Colorf& color, float thickness, UiLayer layer)
	{
		FillRect(Rectf(rect.X, rect.Y, rect.W, thickness), color, layer);
		FillRect(Rectf(rect.X, rect.Y + rect.H - thickness, rect.W, thickness), color, layer);
		FillRect(Rectf(rect.X, rect.Y + thickness, thickness, rect.H - thickness * 2.0f), color, layer);
		FillRect(Rectf(rect.X + rect.W - thickness, rect.Y + thickness, thickness, rect.H - thickness * 2.0f), color, layer);
	}

	void UiPainter::RoundRect(const Rectf& rect, const Colorf& color, UiLayer layer)
	{
		DrawNineSlice(_skin.GetRoundFill(), rect, color, layer);
	}

	void UiPainter::RoundRectBorder(const Rectf& rect, const Colorf& color, UiLayer layer)
	{
		DrawNineSlice(_skin.GetRoundRing(), rect, color, layer);
	}

	void UiPainter::Shadow(const Rectf& rect, float opacity)
	{
		float spread = float(_skin.GetShadowSpread());
		Rectf shadowRect(rect.X - spread, rect.Y - spread + spread * 0.35f, rect.W + spread * 2.0f, rect.H + spread * 2.0f);
		Colorf color = UiTheme::Shadow;
		color.A *= opacity;
		DrawNineSlice(_skin.GetShadow(), shadowRect, color, UiLayer::Back);
	}

	void UiPainter::Panel(const Rectf& rect, bool shadow, const Colorf& fill, const Colorf& border)
	{
		if (shadow) {
			Shadow(rect);
		}
		RoundRect(rect, fill, UiLayer::Fill);
		RoundRectBorder(rect, border, UiLayer::Fill);
	}

	void UiPainter::Line(Vector2f from, Vector2f to, float thickness, const Colorf& color, UiLayer layer)
	{
		if (from.X == to.X) {
			float y1 = std::min(from.Y, to.Y), y2 = std::max(from.Y, to.Y);
			FillRect(Rectf(from.X - thickness * 0.5f, y1, thickness, y2 - y1), color, layer);
			return;
		}
		if (from.Y == to.Y) {
			float x1 = std::min(from.X, to.X), x2 = std::max(from.X, to.X);
			FillRect(Rectf(x1, from.Y - thickness * 0.5f, x2 - x1, thickness), color, layer);
			return;
		}

		if (_lines != nullptr) {
			Vector2f points[2] = { from, to };
			Polyline(points, thickness, color, layer);
			return;
		}

		// A rotated quad of the solid middle of the rounded rectangle
		Vector2f delta = to - from;
		float length = delta.Length();
		Vector2f center = (from + to) * 0.5f;
		Vector2f size(length, thickness);
		const UiSkin::NineSlice& slice = _skin.GetRoundFill();
		Recti solid(slice.Rect.X + slice.Corner, slice.Rect.Y + slice.Corner, 1, 1);
		_canvas.DrawTexture(_skin.GetShapes(), center - size * 0.5f, GetDepth(layer), size, _skin.GetShapeTexCoords(solid),
			ApplyOpacity(color), false, std::atan2(delta.Y, delta.X));
	}

	void UiPainter::Polyline(ArrayView<const Vector2f> points, float thickness, const Colorf& color, UiLayer layer)
	{
		if (points.size() < 2) {
			return;
		}
		if (_lines == nullptr) {
			for (std::size_t i = 1; i < points.size(); i++) {
				Line(points[i - 1], points[i], thickness, color, layer);
			}
			return;
		}
		// Not cut to the clipping like the rest, only left out if it's all outside of it
		if (!_clips.empty()) {
			const Rectf& clip = _clips.back();
			float minX = points[0].X, maxX = points[0].X, minY = points[0].Y, maxY = points[0].Y;
			for (const Vector2f& point : points) {
				minX = std::min(minX, point.X);
				maxX = std::max(maxX, point.X);
				minY = std::min(minY, point.Y);
				maxY = std::max(maxY, point.Y);
			}
			float margin = thickness;
			if (maxX + margin < clip.X || minX - margin > clip.X + clip.W || maxY + margin < clip.Y || minY - margin > clip.Y + clip.H) {
				return;
			}
		}
		// The soft edges take a pixel, the line looks as thick as asked
		Command(_lines->Draw(points, Vector2f(0.0f, 0.0f), thickness * 0.5f + 0.75f, GetDepth(layer), ApplyOpacity(color)));
	}

	void UiPainter::Circle(Vector2f center, float radius, const Colorf& color, UiLayer layer)
	{
		_canvas.DrawTexture(_skin.GetShapes(), Vector2f(center.X - radius, center.Y - radius), GetDepth(layer), Vector2f(radius * 2.0f, radius * 2.0f),
			_skin.GetShapeTexCoords(_skin.GetCircle()), ApplyOpacity(color));
	}

	void UiPainter::Icon(EditorIcon icon, const Rectf& rect, const Colorf& color, UiLayer layer)
	{
		if (icon == EditorIcon::None) {
			return;
		}
		// At a whole multiple of the size of the icon, so its pixels are all the same
		float size = float(EditorIcons::Size) * std::max(1.0f, std::floor(_scale + 0.25f));
		float x = std::round(rect.X + (rect.W - size) * 0.5f);
		float y = std::round(rect.Y + (rect.H - size) * 0.5f);
		_canvas.DrawTexture(_skin.GetIcons(), Vector2f(x, y), GetDepth(layer), Vector2f(size, size), _skin.GetIconTexCoords(icon), ApplyOpacity(color));
	}

	float UiPainter::GetTextSize() const
	{
		return _fontScale / (_scale * UiTheme::TextScale);
	}

	void UiPainter::SetTextSize(float size)
	{
		_fontScale = _scale * UiTheme::TextScale * size;
	}

	float UiPainter::GetTextHeight() const
	{
		return (_font != nullptr ? _font->GetSizeInPixels() * _fontScale : 0.0f);
	}

	float UiPainter::MeasureText(StringView text) const
	{
		return (_font != nullptr ? _font->MeasureString(text, _fontScale).X : 0.0f);
	}

	void UiPainter::Text(StringView text, Vector2f pos, const Colorf& color, Alignment align, UiLayer layer)
	{
		if (_font == nullptr || text.empty()) {
			return;
		}
		// A tinted text takes its opacity as 1 + (alpha - 0.5) * 4 (see the Colorized shader), only the default color
		// takes it as it is
		Colorf finalColor = color;
		if (color.R == UI::Font::DefaultColor.R && color.G == UI::Font::DefaultColor.G && color.B == UI::Font::DefaultColor.B) {
			finalColor.A *= _opacity;
		} else {
			if (color.A > 0.5f) {
				// A color made for anything else than a text (alpha above the 0.5 that's opaque for it) would glare, the
				// tint multiplies the glyphs up to 3 times, so it's toned down and its alpha is mapped to that of a text
				finalColor = Colorf(std::max(color.R - 0.3f, 0.0f), std::max(color.G - 0.3f, 0.0f), std::max(color.B - 0.3f, 0.0f),
					0.5f + (color.A - 1.0f) * 0.25f);
			}
			if (_opacity < 1.0f) {
				float opacity = std::clamp(1.0f + (finalColor.A - 0.5f) * 4.0f, 0.0f, 1.0f) * _opacity;
				finalColor.A = 0.5f + (opacity - 1.0f) * 0.25f;
			}
		}

		std::int32_t charOffset = 0;
		_font->DrawString(&_canvas, text, charOffset, std::round(pos.X), std::round(pos.Y), GetDepth(layer), align, finalColor,
			_fontScale, 0.0f, 0.0f, 0.0f, 0.0f);
	}

	void UiPainter::TextInRect(StringView text, const Rectf& rect, const Colorf& color, Alignment align, UiLayer layer)
	{
		Alignment horizontal = (align & Alignment::HorizontalMask);
		float x;
		switch (horizontal) {
			case Alignment::Right: x = rect.X + rect.W; break;
			case Alignment::Left: x = rect.X; break;
			default: x = rect.X + rect.W * 0.5f; break;
		}
		// Vertically centered, the alignment is only horizontal
		Text(text, Vector2f(x, rect.Y + rect.H * 0.5f), color, horizontal, layer);
	}

	void UiPainter::Image(const Texture& texture, const Rectf& rect, const Vector4f& texCoords, const Colorf& color, std::int32_t paletteOffset, UiLayer layer, float angle)
	{
		_canvas.DrawTexture(texture, Vector2f(rect.X, rect.Y), GetDepth(layer), Vector2f(rect.W, rect.H), texCoords, ApplyOpacity(color), false, angle, paletteOffset);
	}

	bool UiPainter::Tile(Tiles::TileMap& tileMap, std::int32_t tileId, std::uint8_t flags, const Rectf& rect, float alpha, UiLayer layer)
	{
		Texture* texture;
		Vector4f texCoords;
		bool indexed;
		if (!tileMap.GetTileQuad(tileId, Tiles::LayerTileFlags(flags & 0x03), texture, texCoords, indexed)) {
			return false;
		}
		Image(*texture, rect, texCoords, Colorf(1.0f, 1.0f, 1.0f, alpha), indexed ? 0 : -1, layer);
		return true;
	}

	void UiPainter::Command(RenderCommand* command)
	{
		if (command != nullptr) {
			_canvas.DrawRenderCommand(command);
		}
	}

	void UiPainter::DrawNineSlice(const UiSkin::NineSlice& slice, const Rectf& rect, const Colorf& color, UiLayer layer)
	{
		if (rect.W <= 0.0f || rect.H <= 0.0f) {
			return;
		}

		const Texture& texture = _skin.GetShapes();
		std::uint16_t z = GetDepth(layer);
		Colorf finalColor = ApplyOpacity(color);

		// A rectangle smaller than both corners gets smaller corners
		float corner = std::min({ float(slice.Corner), rect.W * 0.5f, rect.H * 0.5f });
		float source = float(slice.Corner);
		float xs[4] = { rect.X, rect.X + corner, rect.X + rect.W - corner, rect.X + rect.W };
		float ys[4] = { rect.Y, rect.Y + corner, rect.Y + rect.H - corner, rect.Y + rect.H };
		std::int32_t sx[4] = { slice.Rect.X, slice.Rect.X + slice.Corner, slice.Rect.X + slice.Rect.W - slice.Corner, slice.Rect.X + slice.Rect.W };
		std::int32_t sy[4] = { slice.Rect.Y, slice.Rect.Y + slice.Corner, slice.Rect.Y + slice.Rect.H - slice.Corner, slice.Rect.Y + slice.Rect.H };
		static_cast<void>(source);

		for (std::int32_t j = 0; j < 3; j++) {
			for (std::int32_t i = 0; i < 3; i++) {
				float w = xs[i + 1] - xs[i];
				float h = ys[j + 1] - ys[j];
				if (w <= 0.0f || h <= 0.0f) {
					continue;
				}
				Recti src(sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j]);
				// The stretched middle samples the center of its texels only, so the edges don't bleed into it
				Vector4f texCoords = _skin.GetShapeTexCoords(src);
				if (i == 1) {
					texCoords.Y += texCoords.X * 0.25f;
					texCoords.X *= 0.5f;
				}
				if (j == 1) {
					texCoords.W += texCoords.Z * 0.25f;
					texCoords.Z *= 0.5f;
				}
				_canvas.DrawTexture(texture, Vector2f(xs[i], ys[j]), z, Vector2f(w, h), texCoords, finalColor);
			}
		}
	}

	Colorf UiPainter::ApplyOpacity(const Colorf& color) const
	{
		if (_opacity >= 1.0f) {
			return color;
		}
		return Colorf(color.R, color.G, color.B, color.A * _opacity);
	}
}

#endif
