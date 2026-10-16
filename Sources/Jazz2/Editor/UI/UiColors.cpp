#include "UiColors.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace Death::Containers::Literals;
using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float PickerSquareSize = 112.0f;
		constexpr float PickerBarWidth = 12.0f;

		std::uint8_t ToByte(float value)
		{
			return std::uint8_t(std::clamp<std::int32_t>(std::int32_t(std::round(value * 255.0f)), 0, 255));
		}

		std::uint32_t ToBytes(const Colorf& color)
		{
			return ToByte(color.R) | (ToByte(color.G) << 8) | (ToByte(color.B) << 16) | (std::uint32_t(ToByte(color.A)) << 24);
		}

		void RgbToHsv(const Colorf& color, float& h, float& s, float& v)
		{
			float max = std::max({ color.R, color.G, color.B }), min = std::min({ color.R, color.G, color.B }), delta = max - min;
			v = max;
			s = (max > 0.0f ? delta / max : 0.0f);
			if (delta <= 0.0f) {
				h = 0.0f;
			} else if (max == color.R) {
				h = std::fmod((color.G - color.B) / delta + 6.0f, 6.0f) / 6.0f;
			} else if (max == color.G) {
				h = ((color.B - color.R) / delta + 2.0f) / 6.0f;
			} else {
				h = ((color.R - color.G) / delta + 4.0f) / 6.0f;
			}
		}

		// Hue goes from 0 to 1 around the circle
		Colorf HsvToRgb(float h, float s, float v, float a)
		{
			float sector = std::clamp(h, 0.0f, 1.0f) * 6.0f;
			std::int32_t i = std::min<std::int32_t>(std::int32_t(sector), 5);
			float f = sector - float(i);
			float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
			switch (i) {
				case 0: return Colorf(v, t, p, a);
				case 1: return Colorf(q, v, p, a);
				case 2: return Colorf(p, v, t, a);
				case 3: return Colorf(p, q, v, a);
				case 4: return Colorf(t, p, v, a);
				default: return Colorf(v, p, q, a);
			}
		}

		// The texels are red, green, blue and alpha bytes
		std::unique_ptr<Texture> MakeGradient(const char* name, const std::uint8_t* texels, std::int32_t width, std::int32_t height)
		{
			auto texture = std::make_unique<Texture>(name, Texture::Format::RGBA8, width, height);
			texture->LoadFromTexels(texels, 0, 0, width, height);
			// Smooth from the middle of one texel to the next, which is the whole gradient
			texture->SetMinFiltering(SamplerFilter::Linear);
			texture->SetMagFiltering(SamplerFilter::Linear);
			texture->SetWrap(SamplerWrapping::ClampToEdge);
			return texture;
		}

		// Texture coordinates from the middle of the first texel to the middle of the last one
		float GradientScale(std::int32_t texels)
		{
			return (texels > 1 ? float(texels - 1) / float(texels) : 1.0f);
		}

		float GradientOffset(std::int32_t texels)
		{
			return (texels > 1 ? 0.5f / float(texels) : 0.0f);
		}

		// Draws a checkerboard, so translucent colors over it can be told from opaque ones
		void DrawChecker(UiPainter& painter, const Rectf& rect)
		{
			// Two cells across the narrower side, a tall bar is checkered along its length then
			float cell = std::max(2.0f, std::round(std::min(rect.W, rect.H) * 0.5f));
			std::int32_t columns = std::int32_t(std::ceil(rect.W / cell));
			std::int32_t rows = std::int32_t(std::ceil(rect.H / cell));
			for (std::int32_t y = 0; y < rows; y++) {
				for (std::int32_t x = 0; x < columns; x++) {
					float x0 = rect.X + x * cell;
					float y0 = rect.Y + y * cell;
					Rectf part(x0, y0, std::min(cell, rect.X + rect.W - x0), std::min(cell, rect.Y + rect.H - y0));
					painter.FillRect(part, ((x + y) & 1) != 0 ? Colorf(0.75f, 0.75f, 0.75f, 1.0f) : Colorf(0.45f, 0.45f, 0.45f, 1.0f), UiLayer::Fill);
				}
			}
		}
	}

	UiColorSwatch::UiColorSwatch(Function<Colorf()>&& color, Function<void()>&& onClick)
		: ColorFunc(std::move(color)), OnClick(std::move(onClick))
	{
	}

	Vector2f UiColorSwatch::Measure(const UiContext& context, Vector2f available)
	{
		Vector2f fixedSize = GetFixedSize(context);
		float height = (fixedSize.Y >= 0.0f ? fixedSize.Y : context.Px(UiTheme::RowHeight));
		float width = (fixedSize.X >= 0.0f ? fixedSize.X : std::round(height * 1.5f));
		return Vector2f(width, height);
	}

	void UiColorSwatch::Draw(UiPainter& painter)
	{
		Colorf color = (ColorFunc ? ColorFunc() : Colorf::White);
		float inset = std::max(1.0f, painter.Px(1.0f));
		painter.RoundRect(Bounds, OnClick && _hoverAmount > 0.0f ? UiTheme::InputBorder : UiTheme::InputFill);
		Rectf inner(Bounds.X + inset, Bounds.Y + inset, Bounds.W - inset * 2.0f, Bounds.H - inset * 2.0f);
		if (color.A < 1.0f) {
			DrawChecker(painter, inner);
		}
		painter.FillRect(inner, color, UiLayer::Content);
	}

	bool UiColorSwatch::AcceptsPointer() const
	{
		return (bool)OnClick;
	}

	bool UiColorSwatch::OnPointerDown(const UiPointerEvent& event)
	{
		return (event.Button == MouseButton::Left && OnClick && IsUsable());
	}

	void UiColorSwatch::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (inside && event.Button == MouseButton::Left && OnClick && IsUsable()) {
			OnClick();
		}
	}

	UiPaletteGrid::UiPaletteGrid(Function<const std::uint32_t*()>&& palette, Function<std::int32_t()>&& selected, Function<void(std::int32_t, bool)>&& onPick)
		: CellSize(DefaultCellSize), _palette(std::move(palette)), _selected(std::move(selected)), _onPick(std::move(onPick)), _dragIndex(-1)
	{
	}

	void UiPaletteGrid::ShowPopup(UiSurface& surface, Vector2f pos, Function<const std::uint32_t*()>&& palette,
		Function<std::int32_t()>&& selected, Function<void(std::int32_t)>&& onPick)
	{
		auto panel = std::make_unique<UiPanel>(UiPanelStyle::Floating);
		UiPanel* panelPtr = panel.get();
		UiSurface* surfacePtr = &surface;
		panel->Add<UiPaletteGrid>(std::move(palette), std::move(selected), [surfacePtr, panelPtr, onPick = std::move(onPick)](std::int32_t index, bool extend) mutable {
			onPick(index);
			surfacePtr->CloseOverlay(panelPtr);
		});
		UiOverlayOptions options;
		options.Position = pos;
		surface.PushOverlay(std::move(panel), std::move(options));
	}

	float UiPaletteGrid::GetCellPixels() const
	{
		// The square it measured, even if a stack stretches it wider
		return std::floor(std::min(Bounds.W, Bounds.H) / 16.0f);
	}

	Colorf UiPaletteGrid::ToColor(std::uint32_t entry)
	{
		return Colorf((entry & 0xFF) / 255.0f, ((entry >> 8) & 0xFF) / 255.0f, ((entry >> 16) & 0xFF) / 255.0f, ((entry >> 24) & 0xFF) / 255.0f);
	}

	Vector2f UiPaletteGrid::Measure(const UiContext& context, Vector2f available)
	{
		float size = context.Px(CellSize) * 16.0f;
		return Vector2f(size, size);
	}

	void UiPaletteGrid::Draw(UiPainter& painter)
	{
		const std::uint32_t* palette = (_palette ? _palette() : nullptr);
		if (palette == nullptr) {
			return;
		}
		float cell = GetCellPixels();
		for (std::int32_t i = 0; i < 256; i++) {
			Rectf rect(Bounds.X + (i % 16) * cell, Bounds.Y + (i / 16) * cell, cell, cell);
			Colorf color = ToColor(palette[i]);
			// Alpha isn't what the entry is picked by, it would only hide the color
			color.A = 1.0f;
			painter.FillRect(rect, color, UiLayer::Content);
		}
		if (IsCrossedFunc) {
			// A diagonal line in a contrasting color through each entry crossed out
			float thickness = std::max(1.0f, std::round(painter.GetScale()));
			for (std::int32_t i = 0; i < 256; i++) {
				if (!IsCrossedFunc(i)) {
					continue;
				}
				Rectf rect(Bounds.X + (i % 16) * cell, Bounds.Y + (i / 16) * cell, cell, cell);
				Colorf color = ToColor(palette[i]);
				float luma = color.R * 0.3f + color.G * 0.59f + color.B * 0.11f;
				Colorf line = (luma > 0.5f ? Colorf(0.0f, 0.0f, 0.0f, 0.8f) : Colorf(1.0f, 1.0f, 1.0f, 0.8f));
				painter.Line(Vector2f(rect.X + thickness, rect.Y + rect.H - thickness), Vector2f(rect.X + rect.W - thickness, rect.Y + thickness), thickness, line, UiLayer::Overlay);
			}
		}
		std::int32_t selected = (_selected ? _selected() : -1);
		std::int32_t selectedEnd = (SelectionEndFunc ? SelectionEndFunc() : selected);
		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		if (selected >= 0 && selected < 256 && selectedEnd >= 0 && selectedEnd < 256) {
			// The range is in the order of the entries, so it goes on from the end of a row to the start of the next one
			for (std::int32_t i = std::min(selected, selectedEnd); i <= std::max(selected, selectedEnd); i++) {
				Rectf rect(Bounds.X + (i % 16) * cell, Bounds.Y + (i / 16) * cell, cell, cell);
				painter.Frame(rect, i == selected ? UiTheme::Accent : UiTheme::AccentDim, thickness, UiLayer::Overlay);
			}
		}
		if (IsHovered() && GetSurface() != nullptr) {
			std::int32_t hovered = GetIndexAt(GetSurface()->GetPointerPos());
			if (hovered >= 0) {
				Rectf rect(Bounds.X + (hovered % 16) * cell, Bounds.Y + (hovered / 16) * cell, cell, cell);
				painter.Frame(rect, Colorf::White, thickness, UiLayer::Overlay);
			}
		}
	}

	bool UiPaletteGrid::AcceptsPointer() const
	{
		return true;
	}

	bool UiPaletteGrid::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button != MouseButton::Left) {
			return false;
		}
		std::int32_t index = GetIndexAt(event.Pos);
		if (index < 0 || !_onPick) {
			return false;
		}
		bool extend = (SelectionEndFunc && (event.Modifiers & KeyMod::Shift) != 0);
		_onPick(index, extend);
		// Dragging picks a range
		_dragIndex = index;
		return (bool)SelectionEndFunc;
	}

	void UiPaletteGrid::OnPointerMove(const UiPointerEvent& event)
	{
		std::int32_t index = GetIndexAt(event.Pos);
		if (index >= 0 && index != _dragIndex && _dragIndex >= 0 && _onPick) {
			_dragIndex = index;
			_onPick(index, true);
		}
	}

	StringView UiPaletteGrid::GetTooltip(Vector2f pos) const
	{
		std::int32_t index = GetIndexAt(pos);
		const std::uint32_t* palette = (_palette ? _palette() : nullptr);
		if (index < 0 || palette == nullptr) {
			return {};
		}
		std::uint32_t entry = palette[index];
		char buffer[32];
		std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%d: #%02X%02X%02X", index, entry & 0xFF, (entry >> 8) & 0xFF, (entry >> 16) & 0xFF);
		_tooltip = String(buffer, std::size_t(std::max(length, 0)));
		return _tooltip;
	}

	std::int32_t UiPaletteGrid::GetIndexAt(Vector2f pos) const
	{
		float cell = GetCellPixels();
		if (cell <= 0.0f) {
			return -1;
		}
		std::int32_t x = std::int32_t((pos.X - Bounds.X) / cell);
		std::int32_t y = std::int32_t((pos.Y - Bounds.Y) / cell);
		return (x >= 0 && x < 16 && y >= 0 && y < 16 && pos.X >= Bounds.X && pos.Y >= Bounds.Y ? y * 16 + x : -1);
	}

	UiColorPicker::UiColorPicker(Function<Colorf()>&& color, Function<void(const Colorf&)>&& onChange, bool withAlpha)
		: _color(std::move(color)), _onChange(std::move(onChange)), _withAlpha(withAlpha), _hue(0.0f), _saturation(0.0f),
			_value(0.0f), _alpha(1.0f), _knownColor(0), _dragPart(Part::None), _squareHue(-1.0f)
	{
		Colorf current = (_color ? _color() : Colorf::White);
		RgbToHsv(current, _hue, _saturation, _value);
		_alpha = current.A;
		_knownColor = ToBytes(current);
	}

	UiColorPicker::~UiColorPicker()
	{
	}

	void UiColorPicker::ShowPopup(UiSurface& surface, Vector2f pos, Function<Colorf()>&& color, Function<void(const Colorf&)>&& onChange, bool withAlpha)
	{
		auto shared = std::make_shared<Function<Colorf()>>(std::move(color));
		auto change = std::make_shared<Function<void(const Colorf&)>>(std::move(onChange));
		Colorf original = (*shared ? (*shared)() : Colorf::White);

		auto panel = std::make_unique<UiPanel>(UiPanelStyle::Floating);
		auto* column = panel->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		column->Add<UiColorPicker>([shared]() { return (*shared)(); }, [change](const Colorf& value) { (*change)(value); }, withAlpha);

		auto* row = column->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		auto* before = row->Add<UiColorSwatch>([original]() { return original; }, [change, original]() { (*change)(original); });
		before->Tooltip = _("The color before, click to bring it back");
		auto* now = row->Add<UiColorSwatch>([shared]() { return (*shared)(); });
		now->Tooltip = _("The color now");
		auto* hex = row->Add<UiTextBox>();
		hex->MaxLength = 7;
		hex->Flex = 1.0f;
		hex->Width = 52.0f;
		hex->TextFunc = [shared]() { return ToHex((*shared)()); };
		hex->OnCommit = [shared, change](StringView text) {
			Colorf value = (*shared)();
			if (FromHex(text, value)) {
				(*change)(value);
			}
		};
		hex->Tooltip = _("The color as #RRGGBB");

		UiOverlayOptions options;
		options.Position = pos;
		surface.PushOverlay(std::move(panel), std::move(options));
	}

	String UiColorPicker::ToHex(const Colorf& color)
	{
		char buffer[16];
		std::int32_t length = std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", ToByte(color.R), ToByte(color.G), ToByte(color.B));
		return String(buffer, std::size_t(std::max(length, 0)));
	}

	bool UiColorPicker::FromHex(StringView text, Colorf& color)
	{
		StringView digits = text.trimmed();
		if (digits.hasPrefix("#"_s)) {
			digits = digits.exceptPrefix(1);
		}
		if (digits.size() != 6) {
			return false;
		}
		String terminated = digits;
		char* end = nullptr;
		unsigned long parsed = std::strtoul(terminated.data(), &end, 16);
		if (end != terminated.data() + 6) {
			return false;
		}
		color.R = ((parsed >> 16) & 0xFF) / 255.0f;
		color.G = ((parsed >> 8) & 0xFF) / 255.0f;
		color.B = (parsed & 0xFF) / 255.0f;
		return true;
	}

	Vector2f UiColorPicker::Measure(const UiContext& context, Vector2f available)
	{
		float square = context.Px(PickerSquareSize);
		float bar = context.Px(PickerBarWidth);
		float spacing = context.Px(UiTheme::Spacing);
		float width = square + spacing + bar;
		if (_withAlpha) {
			width += spacing + bar;
		}
		return Vector2f(width, square);
	}

	Rectf UiColorPicker::GetSquareRect() const
	{
		// The bars and the gaps scale with the height, which is what the square measured
		float bar = std::round(Bounds.H * PickerBarWidth / PickerSquareSize);
		float spacing = std::round(Bounds.H * UiTheme::Spacing / PickerSquareSize);
		float width = Bounds.W - (bar + spacing) * (_withAlpha ? 2.0f : 1.0f);
		return Rectf(Bounds.X, Bounds.Y, std::max(width, 1.0f), Bounds.H);
	}

	Rectf UiColorPicker::GetHueRect() const
	{
		Rectf square = GetSquareRect();
		float bar = std::round(Bounds.H * PickerBarWidth / PickerSquareSize);
		float spacing = std::round(Bounds.H * UiTheme::Spacing / PickerSquareSize);
		return Rectf(square.X + square.W + spacing, Bounds.Y, bar, Bounds.H);
	}

	Rectf UiColorPicker::GetAlphaRect() const
	{
		Rectf hue = GetHueRect();
		float spacing = std::round(Bounds.H * UiTheme::Spacing / PickerSquareSize);
		return Rectf(hue.X + hue.W + spacing, Bounds.Y, hue.W, Bounds.H);
	}

	void UiColorPicker::SyncFromColor()
	{
		if (!_color) {
			return;
		}
		Colorf current = _color();
		std::uint32_t bytes = ToBytes(current);
		if (bytes == _knownColor) {
			return;
		}
		// Changed elsewhere, by the numbers or undo, the hue stays if the new color has none
		float h, s, v;
		RgbToHsv(current, h, s, v);
		if (s > 0.0f && v > 0.0f) {
			_hue = h;
		}
		if (v > 0.0f) {
			_saturation = s;
		}
		_value = v;
		_alpha = current.A;
		_knownColor = bytes;
	}

	void UiColorPicker::EnsureTextures()
	{
		if (_hueBar == nullptr) {
			// Red, yellow, green, cyan, blue, magenta and red again, from the top
			static const std::uint8_t HueTexels[] = {
				255, 0, 0, 255,  255, 255, 0, 255,  0, 255, 0, 255,  0, 255, 255, 255,
				0, 0, 255, 255,  255, 0, 255, 255,  255, 0, 0, 255
			};
			_hueBar = MakeGradient("EditorPickerHue", HueTexels, 1, 7);
		}
		if (_withAlpha && _alphaBar == nullptr) {
			// White, tinted by the color when drawn, from opaque at the top to transparent
			static const std::uint8_t AlphaTexels[] = { 255, 255, 255, 255,  255, 255, 255, 0 };
			_alphaBar = MakeGradient("EditorPickerAlpha", AlphaTexels, 1, 2);
		}
		if (_square == nullptr || _squareHue != _hue) {
			// White to the hue across the top, black along the bottom, smoothing between them is the rest of the square
			Colorf pure = HsvToRgb(_hue, 1.0f, 1.0f, 1.0f);
			std::uint8_t texels[] = {
				255, 255, 255, 255,  ToByte(pure.R), ToByte(pure.G), ToByte(pure.B), 255,
				0, 0, 0, 255,  0, 0, 0, 255
			};
			if (_square == nullptr) {
				_square = MakeGradient("EditorPickerSquare", texels, 2, 2);
			} else {
				_square->LoadFromTexels(texels, 0, 0, 2, 2);
			}
			_squareHue = _hue;
		}
	}

	void UiColorPicker::Draw(UiPainter& painter)
	{
		SyncFromColor();
		EnsureTextures();

		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		Rectf square = GetSquareRect();
		painter.Image(*_square, square, Vector4f(GradientScale(2), GradientOffset(2), GradientScale(2), GradientOffset(2)), Colorf::White, -1, UiLayer::Content);
		painter.Frame(square, UiTheme::InputBorder, thickness, UiLayer::Content);

		Rectf hue = GetHueRect();
		painter.Image(*_hueBar, hue, Vector4f(1.0f, 0.0f, GradientScale(7), GradientOffset(7)), Colorf::White, -1, UiLayer::Content);
		painter.Frame(hue, UiTheme::InputBorder, thickness, UiLayer::Content);

		Colorf color = HsvToRgb(_hue, _saturation, _value, 1.0f);
		if (_withAlpha) {
			Rectf alpha = GetAlphaRect();
			DrawChecker(painter, alpha);
			painter.Image(*_alphaBar, alpha, Vector4f(1.0f, 0.0f, GradientScale(2), GradientOffset(2)), color, -1, UiLayer::Content);
			painter.Frame(alpha, UiTheme::InputBorder, thickness, UiLayer::Content);
			float y = std::round(alpha.Y + (1.0f - _alpha) * alpha.H);
			painter.Frame(Rectf(alpha.X - thickness, y - thickness * 1.5f, alpha.W + thickness * 2.0f, thickness * 3.0f), Colorf::White, thickness, UiLayer::Overlay);
		}

		// Where the color is, a ring that can be seen on light and dark colors
		Vector2f pos(square.X + _saturation * square.W, square.Y + (1.0f - _value) * square.H);
		float radius = std::max(3.0f, painter.Px(4.0f));
		painter.Circle(pos, radius + thickness, Colorf(0.0f, 0.0f, 0.0f, 0.7f), UiLayer::Overlay);
		painter.Circle(pos, radius, Colorf::White, UiLayer::Overlay);
		painter.Circle(pos, radius - thickness, color, UiLayer::Overlay);

		float y = std::round(hue.Y + _hue * hue.H);
		painter.Frame(Rectf(hue.X - thickness, y - thickness * 1.5f, hue.W + thickness * 2.0f, thickness * 3.0f), Colorf::White, thickness, UiLayer::Overlay);
	}

	bool UiColorPicker::AcceptsPointer() const
	{
		return true;
	}

	bool UiColorPicker::OnPointerDown(const UiPointerEvent& event)
	{
		if (event.Button != MouseButton::Left || !IsUsable()) {
			return false;
		}
		SyncFromColor();
		if (GetSquareRect().Contains(event.Pos)) {
			_dragPart = Part::Square;
		} else if (GetHueRect().Contains(event.Pos)) {
			_dragPart = Part::Hue;
		} else if (_withAlpha && GetAlphaRect().Contains(event.Pos)) {
			_dragPart = Part::Alpha;
		} else {
			return false;
		}
		ChangeAt(event.Pos);
		return true;
	}

	void UiColorPicker::OnPointerMove(const UiPointerEvent& event)
	{
		if (_dragPart != Part::None) {
			ChangeAt(event.Pos);
		}
	}

	void UiColorPicker::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (_dragPart != Part::None) {
			ChangeAt(event.Pos);
			_dragPart = Part::None;
		}
	}

	void UiColorPicker::ChangeAt(Vector2f pos)
	{
		switch (_dragPart) {
			case Part::Square: {
				Rectf square = GetSquareRect();
				_saturation = std::clamp((pos.X - square.X) / square.W, 0.0f, 1.0f);
				_value = 1.0f - std::clamp((pos.Y - square.Y) / square.H, 0.0f, 1.0f);
				break;
			}
			case Part::Hue: {
				Rectf hue = GetHueRect();
				_hue = std::clamp((pos.Y - hue.Y) / hue.H, 0.0f, 1.0f);
				break;
			}
			case Part::Alpha: {
				Rectf alpha = GetAlphaRect();
				_alpha = 1.0f - std::clamp((pos.Y - alpha.Y) / alpha.H, 0.0f, 1.0f);
				break;
			}
			default:
				return;
		}
		Colorf color = HsvToRgb(_hue, _saturation, _value, _withAlpha ? _alpha : (_color ? _color().A : 1.0f));
		std::uint32_t bytes = ToBytes(color);
		if (bytes == _knownColor) {
			return;
		}
		_knownColor = bytes;
		if (_onChange) {
			_onChange(color);
		}
	}
}

#endif
