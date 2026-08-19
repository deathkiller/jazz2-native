#include "FirstRunSection.h"
#include "MenuResources.h"
#include "../Font.h"
#include "../../PreferencesCache.h"

#include "../../../nCine/I18n.h"

#include <algorithm>

#include <Utf8.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	// Standard divider line insets, with the frame pushed down to make room for the welcome header
	static constexpr std::int32_t TopLine = 31;
	static constexpr std::int32_t BottomLine = 42;
	// This section is the most text-heavy one in the menu: a header, a three-line paragraph and two rows
	// that each carry a name and a sentence. None of that fits a panel as small as the PSP's 480x272 at
	// full size, so the block above the frame is shortened and every string is fitted (see below). The
	// threshold is the one the other sections and MenuContainerBase::UpdateContentBounds() already use.
	static constexpr float HeaderOffset = 66.0f;
	static constexpr float HeaderOffsetCompact = 44.0f;
	static constexpr float ItemHeight = 68.0f;
	static constexpr float ItemHeightCompact = 52.0f;
	// The welcome line still fits a 480 px panel at its designed size, but next to a paragraph that had to
	// be scaled down to fit it reads as oversized - so it is shrunk with the rest of the section
	static constexpr float HeaderScale = 0.9f;
	static constexpr float HeaderScaleCompact = 0.75f;

	namespace
	{
		bool IsCompactLayout(Vector2i viewSize)
		{
			return (viewSize.Y < 300);
		}

		float GetHeaderOffsetFor(Vector2i viewSize)
		{
			return (IsCompactLayout(viewSize) ? HeaderOffsetCompact : HeaderOffset);
		}

		/**
			@brief Width a string is expected to stay inside

			The menu frame is 680 px wide (see @ref IMenuContainer::DrawMenuFrame()), but never wider than the
			view - and a small view is exactly where the strings of this section stop fitting.
		*/
		float GetAvailableTextWidth(const Recti& contentBounds)
		{
			constexpr float MenuFrameWidth = 680.0f;
			constexpr float Margin = 16.0f;
			return std::min((float)contentBounds.W, MenuFrameWidth) - 2.0f * Margin;
		}

		/**
			@brief Returns the largest scale up to @p scale at which @p text is no wider than @p availableWidth

			Scaling the whole string is what keeps a sentence written for a 680 px panel inside a 480 px one
			without rewrapping it, and it does the same for a translation that is longer than the English
			original on any panel. The floor keeps a pathologically long string from becoming unreadable -
			past that it is left to overflow, which is at least still legible.
		*/
		float FitToWidth(IMenuContainer* root, StringView text, float scale, float charSpacing, float availableWidth)
		{
			float width = root->MeasureString(text, scale, charSpacing).X;
			if (width > availableWidth && width > 0.0f && availableWidth > 0.0f) {
				return std::max(scale * availableWidth / width, scale * 0.6f);
			}
			return scale;
		}
	}

	FirstRunSection::FirstRunSection()
		: _presetItems{}
	{
	}

	Recti FirstRunSection::GetClipRectangle(const Recti& contentBounds)
	{
		// Always called after OnShow() has attached the container, so the view size is known; the full-size
		// offset is only a defensive default
		float headerOffset = (_root != nullptr ? GetHeaderOffsetFor(_root->GetViewSize()) : HeaderOffset);
		std::int32_t topLine = TopLine + (std::int32_t)headerOffset;
		return Recti(contentBounds.X, contentBounds.Y + topLine - 1, contentBounds.W, contentBounds.H - topLine - BottomLine + 2);
	}

	void FirstRunSection::OnShow(IMenuContainer* root)
	{
		MenuSection::OnShow(root);

		if (_content != nullptr) {
			return;
		}

		auto list = std::make_unique<ScrollView>();

		struct Preset {
			bool Reforged;
			StringView Name;
			StringView Description;
		};
		Preset presets[] = {
			// TRANSLATORS: Menu item in First Run section
			{ false, _("Legacy"), _("I want to play the game the way it used to be.") },
			// TRANSLATORS: Menu item in First Run section
			{ true, _("Reforged"), _("I want to play the game with something new.") }
		};

		bool compactLayout = IsCompactLayout(root->GetViewSize());

		std::int32_t index = 0;
		for (const auto& preset : presets) {
			bool reforged = preset.Reforged;
			StringView name = preset.Name;
			StringView description = preset.Description;

			auto* item = list->Add<CanvasWidget>(compactLayout ? ItemHeightCompact : ItemHeight);
			item->OnDrawContent = [reforged, name, description](IMenuContainer* r, Canvas* canvas, const Rectf& bounds, std::int32_t& charOffset, bool selected, float animation) {
				float centerX = bounds.X + bounds.W * 0.5f;
				float y = bounds.Y + bounds.H * 0.5f;

				// Read per draw, so a resized window (or a rotated handheld) relays out immediately
				bool compact = IsCompactLayout(r->GetViewSize());
				float availableWidth = GetAvailableTextWidth(r->GetContentBounds());
				float textScale = (compact ? 0.85f : 1.0f);
				float descriptionOffset = (compact ? 17.0f : 22.0f);

				float nameScale = FitToWidth(r, name, textScale, 1.0f, availableWidth);
				if (selected) {
					float size = (0.7f + Easing::OutElastic(animation) * 0.6f) * nameScale;
					r->DrawElement(MenuGlow, 0, centerX, y + (compact ? 8.0f : 10.0f), IMenuContainer::MainLayer, Alignment::Center,
						Colorf(1.0f, 1.0f, 1.0f, 0.2f), 22.0f, 12.0f * textScale, true, true);
					r->DrawStringShadow(name, charOffset, centerX, y, IMenuContainer::FontLayer + 10,
						Alignment::Center, Font::RandomColor, size, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);
				} else {
					r->DrawStringShadow(name, charOffset, centerX, y, IMenuContainer::FontLayer,
						Alignment::Center, reforged ? Colorf(0.62f, 0.44f, 0.34f, 0.5f) : Font::DefaultColor, nameScale);
				}

				float descriptionScale = FitToWidth(r, description, 0.8f * textScale, 0.94f, availableWidth);
				r->DrawStringShadow(description, charOffset, centerX, y + descriptionOffset, IMenuContainer::FontLayer,
					Alignment::Center, Font::DefaultColor, descriptionScale, 0.0f, 0.0f, 0.0f, 0.0f, 0.94f);
			};
			item->OnSelectedChanged = [this, reforged]() {
				// Live-preview the main menu style of the highlighted preset
				bool wasReforged = PreferencesCache::EnableReforgedMainMenu;
				PreferencesCache::EnableReforgedMainMenu = reforged;
				if (reforged != wasReforged) {
					_root->ApplyPreferencesChanges(ChangedPreferencesType::MainMenu);
				}
			};
			item->OnActivate = [this, reforged]() {
				PreferencesCache::EnableReforgedGameplay = reforged;
				PreferencesCache::EnableReforgedHUD = reforged;
				PreferencesCache::EnableLedgeClimb = reforged;
				PreferencesCache::Save();
				_root->LeaveSection();
			};

			if (index < (std::int32_t)arraySize(_presetItems)) {
				_presetItems[index++] = item;
			}
		}

		list->SetSelectedIndex(PreferencesCache::EnableReforgedGameplay ? 1 : 0);

		SetContent(std::move(list));
	}

	void FirstRunSection::OnDraw(Canvas* canvas)
	{
		Recti contentBounds = _root->GetContentBounds();
		bool compactLayout = IsCompactLayout(_root->GetViewSize());
		float centerX = contentBounds.X + contentBounds.W * 0.5f;
		float topLine = contentBounds.Y + TopLine + GetHeaderOffsetFor(_root->GetViewSize());
		float bottomLine = contentBounds.Y + contentBounds.H - BottomLine;

		// The rows follow the view size as well, so that both presets stay inside the shorter list
		for (auto* item : _presetItems) {
			if (item != nullptr) {
				item->Height = (compactLayout ? ItemHeightCompact : ItemHeight);
			}
		}

		_root->DrawMenuFrame(centerX, topLine, bottomLine);

		float availableWidth = GetAvailableTextWidth(contentBounds);

		std::int32_t charOffset = 0;
		// TRANSLATORS: Header in First Run section
		StringView header = _("Welcome to \f[c:#9e7056]Jazz Jackrabbit 2\f[/c] reimplementation!");
		float headerScale = FitToWidth(_root, header, (compactLayout ? HeaderScaleCompact : HeaderScale), 0.9f, availableWidth);
		// The header line sits in the standard top inset, which is why it doesn't move with the offset below
		float headerY = contentBounds.Y + TopLine - 21.0f;
		_root->DrawStringShadow(header, charOffset, centerX, headerY, IMenuContainer::FontLayer,
			Alignment::Center, Font::DefaultColor, headerScale, 0.7f, 1.1f, 1.1f, 0.4f, 0.9f);

		// TRANSLATORS: Subheader in First Run section
		// \uE000 is the Discord icon of the menu font's private use area, written as an escape so that
		// the character itself cannot be lost when the file is edited
		String subheader = _f("You can choose your preferred play style.\nThis option can be changed at any time in \f[c:#707070]{}\f[/c] > \f[c:#707070]{}\f[/c] > \f[c:#707070]{}\f[/c].\nFor more information, visit {} and \uE000 Discord!", _("Options"), _("Gameplay"), _("Enhancements"), "\f[c:#707070]https://de4th.dev/jazz2/\f[/c]"_s);
		// The paragraph is fitted into the band between the header and the frame in both directions: on a
		// 480 px panel its middle line is otherwise ~60 px wider than the whole screen, and a translation
		// that needs one line more would grow into the header line above it. On a full-size panel the band
		// is roomy enough that neither limit applies, so the paragraph keeps its designed size and position.
		float bandTop = headerY + _root->MeasureString(header, headerScale, 0.9f).Y * 0.5f + 4.0f;
		float bandBottom = topLine - (compactLayout ? 10.0f : 14.0f);
		float subheaderScale = FitToWidth(_root, subheader, 0.86f, 0.9f, availableWidth);
		Vector2f subheaderSize = _root->MeasureString(subheader, subheaderScale, 0.9f);
		if (subheaderSize.Y > (bandBottom - bandTop) && subheaderSize.Y > 0.0f) {
			subheaderScale *= (bandBottom - bandTop) / subheaderSize.Y;
			subheaderSize = _root->MeasureString(subheader, subheaderScale, 0.9f);
		}
		_root->DrawStringShadow(subheader, charOffset, centerX, bandBottom - subheaderSize.Y * 0.5f, IMenuContainer::FontLayer - 2,
			Alignment::Center, Font::DefaultColor, subheaderScale, 0.7f, 0.0f, 0.0f, 0.4f, 0.9f);
	}

	void FirstRunSection::OnBackPressed()
	{
		// Can't go back from here
	}
}
