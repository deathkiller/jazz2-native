#include "Icons.h"

#if defined(WITH_UI_WINDOWS)

#include <Containers/SmallVector.h>

namespace Jazz2::UI::Windows
{
	namespace
	{
		// Vector art of the stock icons on a square of 16 units, see VectorArt
		const char* GetStockVectorArt(StockIcon icon)
		{
			switch (icon) {
				case StockIcon::Close: return "stroke # 2 round: M4 4 L12 12 M12 4 L4 12";
				case StockIcon::CloseSmall: return "stroke # 1.5 round: M5 5 L11 11 M11 5 L5 11";
				case StockIcon::Check: return "stroke # 2.2 round: M2 8.5 L6 12.5 L14 4";
				case StockIcon::ChevronLeft: return "stroke # 2 round: M10 4 L6 8 L10 12";
				case StockIcon::ChevronRight: return "stroke # 2 round: M6 4 L10 8 L6 12";
				case StockIcon::ChevronDown: return "stroke # 2 round: M4 6 L8 10 L12 6";
				case StockIcon::ArrowUp: return "fill #: M8 2 L14 8 H10 V13 H6 V8 H2 Z";
				case StockIcon::ArrowDown: return "fill #: M8 14 L14 8 H10 V3 H6 V8 H2 Z";
				case StockIcon::Up: return "fill #: M11 2.5 L15 6.5 H12 V12 H3 V10 H10 V6.5 H7 Z";
				case StockIcon::Plus: return "stroke # 3: M8 3 V13 M3 8 H13";
				case StockIcon::Minus: return "stroke # 3: M3 8 H13";
				case StockIcon::Folder:
					return "stroke # 1 miter: M3.5 13.5 V2.5 H10.5 L11.5 3.5 V7.5 L12.5 8.5 V12.5 L11.5 13.5 H7; fill # 0.62: "
						"rect 4 3 2 4; fill # 0.5: rect 4 7 2 4; fill # 0.38: rect 4 11 2 2; fill # 0.65: rect 4 13 3 1; fill "
						"# 0.86: rect 6 3 1 3; fill # 0.74: rect 6 6 1 3; fill # 0.62: rect 6 9 1 2; fill # 0.5: rect 6 11 1 "
						"2; fill # 0.38: rect 7 3 1 1; fill # 0.62: rect 7 4 1 3; fill # 0.76: rect 7 7 1 3; fill # 0.86: "
						"rect 7 10 1 3; fill # 0.13: rect 8 3 2 10; fill # 0.25: rect 10 3 1 10; fill # 0.7: rect 11 8 1 4; "
						"fill # 0.25: rect 11 12 1 1; fill # 0.4: rect 3 14 2 1; fill # 0.72: rect 5 14 3 1";
				case StockIcon::File: return "stroke # 1 miter: M3.5 1.5 H9.5 L12.5 4.5 V14.5 H3.5 Z M9.5 1.5 V4.5 H12.5; "
					"stroke # 1: M5 6.5 H11 M5 8.5 H11 M5 10.5 H11 M5 12.5 H9";
				case StockIcon::Delete: return "fill #: rect 2 3 12 1.6 0.6; stroke # 1.2 round: M6 3 V1.8 H10 V3; "
					"stroke # 1.2 round-join: M3.5 6 H12.5 L11.5 14.5 H4.5 Z; stroke # 1.2 round: M6.5 8 V12.5 M9.5 8 V12.5";
				case StockIcon::Search: return "stroke # 1.5: circle 8 6 4.25; stroke # 2 round: M11 9.5 L14 12.5";
				case StockIcon::Info: return "outline 1; fill w: rect 7 2 3 3 0.5; fill w: M6 8 H10 V14 H11 V15 H6 V14 H7 V9 H6 Z";
				case StockIcon::Warning: return "stroke # 1.5 round-join: M8 2 L14.5 14 H1.5 Z; stroke # 2 round: M8 6.5 V9.5; fill #: circle 8 12 1.1";
				case StockIcon::Undock:
					return "stroke # 1 square: M5.5 6.5 H4.5 V11.5 H11.5 V9.5; stroke # 1.4: M6.3 8.7 L9.6 5.4; fill #: M8 4 H12 "
						"V8 Z";
				case StockIcon::Dock: return "stroke # 1: rect 4.5 4.5 7 7; fill #: rect 4 4 2.5 8";
				case StockIcon::CursorResizeHorizontal: return "fill #: M1 7.5 L5 3.5 V7 H11 V3.5 L15 7.5 L11 11.5 V8 H5 V11.5 Z";
				case StockIcon::CursorResizeVertical: return "fill #: M7.5 1 L11.5 5 H8 V11 H11.5 L7.5 15 L3.5 11 H7 V5 H3.5 Z";
				case StockIcon::CursorResizeNWSE: return "fill #: M3 3 H7.5 L3 7.5 Z M13 13 H8.5 L13 8.5 Z; stroke # 1.4: M4.5 4.5 L11.5 11.5";
				case StockIcon::CursorResizeNESW: return "fill #: M13 3 H8.5 L13 7.5 Z M3 13 H7.5 L3 8.5 Z; stroke # 1.4: M11.5 4.5 L4.5 11.5";
				case StockIcon::CursorMove: return "stroke # 1: M7.5 3 V12 M3 7.5 H12; "
					"fill #: M7.5 1 L10 3.5 H5 Z M7.5 14 L10 11.5 H5 Z M1 7.5 L3.5 5 V10 Z M14 7.5 L11.5 5 V10 Z";
				default: return nullptr;
			}
		}

		struct IconRange
		{
			std::uint16_t First;
			std::uint16_t Count;
			Icons::ArtFunc GetArt;
		};

		struct IconRegistry
		{
			Death::Containers::SmallVector<IconRange, 4> Ranges;
			std::uint32_t Revision = 0;
		};

		IconRegistry& GetRegistry()
		{
			static IconRegistry registry;
			return registry;
		}
	}

	void Icons::Register(IconId first, std::uint16_t count, ArtFunc getArt)
	{
		IconRegistry& registry = GetRegistry();
		for (const IconRange& range : registry.Ranges) {
			if (range.First == first.GetValue() && range.Count == count && range.GetArt == getArt) {
				return;
			}
		}
		registry.Ranges.push_back({ first.GetValue(), count, getArt });
		registry.Revision++;
	}

	std::int32_t Icons::GetCount()
	{
		std::int32_t count = std::int32_t(StockIcon::Count);
		for (const IconRange& range : GetRegistry().Ranges) {
			count += range.Count;
		}
		return count;
	}

	std::int32_t Icons::GetIndex(IconId icon)
	{
		std::uint16_t value = icon.GetValue();
		if (value < std::uint16_t(StockIcon::Count)) {
			return std::int32_t(value);
		}
		std::int32_t index = std::int32_t(StockIcon::Count);
		for (const IconRange& range : GetRegistry().Ranges) {
			if (value >= range.First && value < range.First + range.Count) {
				return index + (value - range.First);
			}
			index += range.Count;
		}
		return -1;
	}

	IconArt Icons::GetArt(std::int32_t index)
	{
		IconArt art;
		if (index >= 0 && index < std::int32_t(StockIcon::Count)) {
			art.Vector = GetStockVectorArt(StockIcon(index));
		} else if (index >= 0) {
			index -= std::int32_t(StockIcon::Count);
			for (const IconRange& range : GetRegistry().Ranges) {
				if (index < range.Count) {
					art = range.GetArt(IconId::FromValue(std::uint16_t(range.First + index)));
					break;
				}
				index -= range.Count;
			}
		}
		return art;
	}

	std::uint32_t Icons::GetRevision()
	{
		return GetRegistry().Revision;
	}

	std::uint32_t Icons::GetPixelColor(char pixel)
	{
		constexpr auto rgba = [](std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a = 255) {
			return (r | (g << 8) | (b << 16) | (a << 24));
		};
		switch (pixel) {
			case '#': return rgba(255, 255, 255);
			case '+': return rgba(255, 255, 255, 128);
			case '1': case '2': case '3': case '4': case '5': case '6': case '7':
				return rgba(255, 255, 255, std::uint32_t(pixel - '0') * 32);
			// Dark, but not quite black, as the outlines of the sprites of the game
			case 'o': return rgba(20, 14, 28);
			case 'w': return rgba(255, 255, 255);
			case 'y': return rgba(255, 222, 70);
			case 'Y': return rgba(226, 148, 30);
			case 'r': return rgba(236, 52, 48);
			case 'R': return rgba(150, 22, 40);
			case 'p': return rgba(255, 150, 176);
			case 'P': return rgba(208, 90, 128);
			case 'g': return rgba(196, 200, 214);
			case 'G': return rgba(110, 112, 134);
			case 't': return rgba(246, 206, 150);
			case 'T': return rgba(196, 142, 92);
			// The shine on green, mixed with it already, a half transparent white would let the background through
			case 'm': return rgba(196, 248, 172);
			case 'n': return rgba(110, 220, 80);
			case 'N': return rgba(36, 132, 56);
			case 'b': return rgba(90, 170, 255);
			case 'B': return rgba(40, 80, 190);
			default: return 0;
		}
	}
}

#endif
