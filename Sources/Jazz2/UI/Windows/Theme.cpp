#include "Theme.h"

#if defined(WITH_UI_WINDOWS)

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace Jazz2::UI::Windows
{
	namespace
	{
		struct ThemeToken
		{
			Colorf* Value;
			// Of the high contrast theme
			Colorf HighContrast;
		};

		struct ThemeTokens
		{
			ThemeToken* Tokens;
			// What the colors are by default, taken before any theme changed them
			Colorf* Defaults;
			std::size_t Count;
		};

		ThemeTokens& GetTokens()
		{
			static ThemeToken tokens[] = {
				{ &Theme::PanelFill, Colorf(0.0f, 0.0f, 0.0f, 1.0f) },
				{ &Theme::PanelFillAlt, Colorf(0.08f, 0.08f, 0.10f, 1.0f) },
				{ &Theme::PanelBorder, Colorf(1.0f, 0.92f, 0.0f, 1.0f) },
				{ &Theme::PanelBorderDim, Colorf(0.85f, 0.85f, 0.85f, 1.0f) },
				{ &Theme::PanelHeader, Colorf(0.10f, 0.10f, 0.16f, 1.0f) },
				{ &Theme::Shadow, Colorf(0.0f, 0.0f, 0.0f, 0.6f) },
				{ &Theme::Separator, Colorf(0.80f, 0.80f, 0.80f, 1.0f) },
				{ &Theme::ButtonFill, Colorf(0.12f, 0.12f, 0.14f, 1.0f) },
				{ &Theme::ButtonHover, Colorf(0.26f, 0.26f, 0.36f, 1.0f) },
				{ &Theme::ButtonPressed, Colorf(0.04f, 0.04f, 0.05f, 1.0f) },
				{ &Theme::ButtonChecked, Colorf(0.50f, 0.42f, 0.0f, 1.0f) },
				{ &Theme::ButtonCheckedHover, Colorf(0.64f, 0.54f, 0.0f, 1.0f) },
				{ &Theme::ButtonOutline, Colorf(1.0f, 1.0f, 1.0f, 1.0f) },
				{ &Theme::Accent, Colorf(1.0f, 0.92f, 0.0f, 1.0f) },
				{ &Theme::AccentDim, Colorf(0.80f, 0.72f, 0.0f, 1.0f) },
				{ &Theme::Selection, Colorf(1.0f, 0.92f, 0.0f, 0.45f) },
				{ &Theme::Hover, Colorf(1.0f, 1.0f, 1.0f, 0.20f) },
				{ &Theme::InputFill, Colorf(0.0f, 0.0f, 0.0f, 1.0f) },
				{ &Theme::InputBorder, Colorf(1.0f, 1.0f, 1.0f, 1.0f) },
				{ &Theme::FocusRing, Colorf(0.0f, 1.0f, 1.0f, 1.0f) },
				{ &Theme::Play, Colorf(0.20f, 0.95f, 0.30f, 1.0f) },
				{ &Theme::Danger, Colorf(1.0f, 0.30f, 0.25f, 1.0f) },
				{ &Theme::IconColor, Colorf(1.0f, 1.0f, 1.0f, 1.0f) },
				{ &Theme::IconDisabled, Colorf(1.0f, 1.0f, 1.0f, 0.40f) },
				{ &Theme::GridLine, Colorf(1.0f, 1.0f, 1.0f, 0.25f) },
				{ &Theme::GridLineStrong, Colorf(1.0f, 1.0f, 1.0f, 0.45f) },
				{ &Theme::Cursor, Colorf(1.0f, 1.0f, 1.0f, 1.0f) },
				// Text is tinted, 0.5 is the font as it is, so the high contrast text is only a little brighter
				{ &Theme::Text, Colorf(0.60f, 0.60f, 0.60f, 0.5f) },
				{ &Theme::TextDim, Colorf(0.56f, 0.56f, 0.56f, 0.5f) },
				{ &Theme::TextDisabled, Colorf(0.50f, 0.50f, 0.50f, 0.40f) },
				{ &Theme::TextHeader, Colorf(0.62f, 0.60f, 0.40f, 0.5f) },
				{ &Theme::TextOnAccent, Colorf(0.60f, 0.60f, 0.60f, 0.5f) },
				{ &Theme::TextAccent, Colorf(0.64f, 0.62f, 0.34f, 0.5f) },
				{ &Theme::TextDanger, Colorf(0.66f, 0.42f, 0.38f, 0.5f) },
				{ &Theme::TextPlay, Colorf(0.44f, 0.64f, 0.46f, 0.5f) },
				{ &Theme::OverlayGood, Colorf(0.20f, 1.0f, 0.30f, 1.0f) },
				{ &Theme::OverlayBad, Colorf(1.0f, 0.25f, 0.20f, 1.0f) },
				{ &Theme::OverlayCaution, Colorf(1.0f, 0.90f, 0.0f, 1.0f) },
				{ &Theme::OverlayInfo, Colorf(0.30f, 0.80f, 1.0f, 1.0f) },
				{ &Theme::OverlaySeries[0], Colorf(0.30f, 0.55f, 1.0f, 1.0f) },
				{ &Theme::OverlaySeries[1], Colorf(1.0f, 0.25f, 0.25f, 1.0f) },
				{ &Theme::OverlaySeries[2], Colorf(0.25f, 1.0f, 0.40f, 1.0f) },
				{ &Theme::OverlaySeries[3], Colorf(1.0f, 0.95f, 0.20f, 1.0f) },
			};
			constexpr std::size_t Count = sizeof(tokens) / sizeof(tokens[0]);
			static Colorf defaults[Count];
			static ThemeTokens result = [&]() {
				for (std::size_t i = 0; i < Count; i++) {
					defaults[i] = *tokens[i].Value;
				}
				return ThemeTokens{ tokens, defaults, Count };
			}();
			return result;
		}

		ThemeKind CurrentKind = ThemeKind::Default;

		// The default colors looked up by all of their channels, and by the color alone, which finds a color made from
		// one with another alpha, as a highlight faded in. Open addressing, an empty slot is -1.
		struct ResolveTables
		{
			static constexpr std::uint32_t Size = 256;

			std::int16_t Exact[Size];
			std::int16_t ByColor[Size];
		};

		std::uint32_t GetBits(float value)
		{
			std::uint32_t bits;
			std::memcpy(&bits, &value, sizeof(bits));
			return bits;
		}

		std::uint32_t HashColor(const Colorf& color, bool withAlpha)
		{
			std::uint32_t hash = 2166136261u;
			for (std::uint32_t bits : { GetBits(color.R), GetBits(color.G), GetBits(color.B), withAlpha ? GetBits(color.A) : 0u }) {
				hash = (hash ^ bits) * 16777619u;
				hash ^= (hash >> 15);
			}
			return hash;
		}

		bool SameColor(const Colorf& a, const Colorf& b, bool withAlpha)
		{
			return (a.R == b.R && a.G == b.G && a.B == b.B && (!withAlpha || a.A == b.A));
		}

		const ResolveTables& GetResolveTables()
		{
			static ResolveTables result = []() {
				ResolveTables tables;
				std::fill(std::begin(tables.Exact), std::end(tables.Exact), std::int16_t(-1));
				std::fill(std::begin(tables.ByColor), std::end(tables.ByColor), std::int16_t(-1));
				const ThemeTokens& tokens = GetTokens();
				for (std::size_t i = 0; i < tokens.Count; i++) {
					const Colorf& color = tokens.Defaults[i];
					for (bool withAlpha : { true, false }) {
						std::int16_t* table = (withAlpha ? tables.Exact : tables.ByColor);
						std::uint32_t slot = HashColor(color, withAlpha) & (ResolveTables::Size - 1);
						bool taken = false;
						while (table[slot] >= 0) {
							// The first of the colors with the same channels wins, a color comes before those made from it
							if (SameColor(tokens.Defaults[table[slot]], color, withAlpha)) {
								taken = true;
								break;
							}
							slot = (slot + 1) & (ResolveTables::Size - 1);
						}
						if (!taken) {
							table[slot] = std::int16_t(i);
						}
					}
				}
				return tables;
			}();
			return result;
		}

		std::int32_t FindColor(const std::int16_t* table, const Colorf& color, bool withAlpha)
		{
			const ThemeTokens& tokens = GetTokens();
			std::uint32_t slot = HashColor(color, withAlpha) & (ResolveTables::Size - 1);
			while (table[slot] >= 0) {
				if (SameColor(tokens.Defaults[table[slot]], color, withAlpha)) {
					return table[slot];
				}
				slot = (slot + 1) & (ResolveTables::Size - 1);
			}
			return -1;
		}
	}

	void Theme::Apply(ThemeKind kind, bool reducedMotion, bool readableFont)
	{
		ThemeTokens& tokens = GetTokens();
		for (std::size_t i = 0; i < tokens.Count; i++) {
			const ThemeToken& token = tokens.Tokens[i];
			*token.Value = (kind == ThemeKind::HighContrast ? token.HighContrast : tokens.Defaults[i]);
		}
		CurrentKind = kind;
		ReducedMotion = reducedMotion;
		if (ReadableFont != readableFont) {
			ReadableFont = readableFont;
			FontVersion++;
		}
	}

	ThemeKind Theme::GetKind()
	{
		return CurrentKind;
	}

	Colorf Theme::Resolve(const Colorf& color)
	{
		if (CurrentKind == ThemeKind::Default) {
			return color;
		}
		const ThemeTokens& tokens = GetTokens();
		const ResolveTables& tables = GetResolveTables();
		std::int32_t index = FindColor(tables.Exact, color, true);
		if (index >= 0) {
			return *tokens.Tokens[index].Value;
		}
		// Made from a color of the theme with another alpha, such as a highlight faded in, it keeps its alpha
		index = FindColor(tables.ByColor, color, false);
		if (index >= 0) {
			const Colorf& themed = *tokens.Tokens[index].Value;
			return Colorf(themed.R, themed.G, themed.B, color.A);
		}
		return color;
	}
}

#endif
