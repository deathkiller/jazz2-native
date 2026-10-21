#include "LevelStatistics.h"

#if defined(WITH_EDITOR)

#include "ShellCommon.h"
#include "../Tools/EditorContext.h"
#include "../Document/LevelDocument.h"
#include "../Input/ActionRegistry.h"
#include "../Runtime/PlayPathHistory.h"
#include "../Tools/EventTools.h"
#include "../Document/DocumentChanges.h"
#include "../Events/EventCatalog.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cstdio>
#include <string>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float LabelWidth = 120.0f;
		constexpr float ValueWidth = 52.0f;

		std::int32_t GetGemValue(std::uint8_t color)
		{
			// Red, green, blue and purple
			static const std::int32_t Values[] = { 1, 5, 10, 1 };
			return Values[color & 0x03];
		}

		std::int32_t GetCoinValue(std::uint8_t type)
		{
			return (type == 1 ? 5 : 1);
		}

		// Adds what a single object of an event gives, a crate or a barrel passes its content here too
		void CountObject(LevelStatistics& stats, EventType type, const std::uint8_t* params, std::int32_t count)
		{
			switch (type) {
				case EventType::Gem: stats.Gems += GetGemValue(params[0]) * count; break;
				case EventType::GemGiant: stats.GiantGems += count; break;
				case EventType::GemRing: stats.Gems += (params[0] != 0 ? params[0] : 8) * count; break;
				case EventType::GemStomp: stats.Gems += 8 * count; break;
				case EventType::Coin: stats.Coins += GetCoinValue(params[0]) * count; break;
				case EventType::Food: stats.Food += count; break;
				case EventType::Carrot:
				case EventType::CarrotFly:
				case EventType::CarrotInvincible: stats.Carrots += count; break;
				case EventType::OneUp: stats.ExtraLives += count; break;
				case EventType::Ammo: stats.Ammo += count; break;
				default: {
					if (const EventDescriptor* descriptor = EventCatalog::Find(type)) {
						if (descriptor->Category == EventCategory::Enemy) {
							stats.Enemies += count;
						} else if (descriptor->Category == EventCategory::Boss) {
							stats.Bosses += count;
						}
					}
					break;
				}
			}
		}

		void CountEvent(LevelStatistics& stats, const Formats::EventRecord& event)
		{
			EventType type = EventType(event.Type);
			stats.Events++;
			if ((event.Flags & Formats::EventRecord::Generator) != 0) {
				stats.Generators++;
				return;
			}

			switch (type) {
				case EventType::LevelStart:
					for (std::int32_t i = 0; i < 3; i++) {
						if ((event.Params[0] & (1 << i)) != 0) {
							stats.Starts[i]++;
						}
					}
					break;
				case EventType::Checkpoint: stats.Checkpoints++; break;
				case EventType::WarpOrigin: stats.Warps++; break;
				case EventType::AreaEndOfLevel: stats.Exits++; break;
				case EventType::Crate:
				case EventType::Barrel: {
					// The content and how many of it, its parameters follow
					stats.Containers++;
					EventType content = EventType(event.Params[0] | (event.Params[1] << 8));
					std::int32_t count = std::max<std::int32_t>(event.Params[2], 1);
					CountObject(stats, content, &event.Params[3], count);
					break;
				}
				case EventType::CrateGem:
				case EventType::BarrelGem:
					stats.Containers++;
					stats.Gems += event.Params[0] + event.Params[1] * 5 + event.Params[2] * 10 + event.Params[3];
					break;
				case EventType::CrateAmmo:
				case EventType::BarrelAmmo:
					stats.Containers++;
					stats.Ammo++;
					break;
				case EventType::PowerUpWeapon:
				case EventType::PowerUpMorph:
				case EventType::PowerUpShield:
					stats.Containers++;
					break;
				default:
					CountObject(stats, type, event.Params, 1);
					break;
			}
		}

		bool IsPresent(const Formats::EventRecord& event, std::uint8_t difficultyBit)
		{
			if ((event.Flags & Formats::EventRecord::MultiplayerOnly) != 0) {
				return false;
			}
			std::uint8_t difficulties = event.Flags & Formats::EventRecord::DifficultyMask;
			return (difficulties == 0 || (difficulties & difficultyBit) != 0);
		}
	}

	LevelStatistics LevelStatistics::Compute(const LevelDocument& document, std::uint8_t difficultyBit)
	{
		LevelStatistics stats;
		for (const DocumentEvent& event : document.Events) {
			if (IsPresent(event.Record, difficultyBit)) {
				CountEvent(stats, event.Record);
			}
		}
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			if (IsPresent(event.Event, difficultyBit)) {
				CountEvent(stats, event.Event);
			}
		}
		return stats;
	}

	namespace
	{
		const std::uint8_t DifficultyBits[3] = { Formats::EventRecord::Easy, Formats::EventRecord::Normal, Formats::EventRecord::Hard };

		// Selects the events in a difficulty that a count of the statistics counts, on the grid and off it, each event
		// counted by itself, as a crate counts its contents
		void SelectCounted(IEditorContext& session, std::uint8_t difficultyBit, Function<bool(const LevelStatistics&)>&& counted)
		{
			const LevelDocument& document = session.GetDocument();
			SmallVector<std::uint32_t, 0> cells;
			for (const DocumentEvent& event : document.Events) {
				if (IsPresent(event.Record, difficultyBit)) {
					LevelStatistics single;
					CountEvent(single, event.Record);
					if (counted(single)) {
						cells.push_back(event.Cell);
					}
				}
			}
			SmallVector<std::int32_t, 0> offGrid;
			for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
				const Formats::EventRecord& event = document.OffGridEvents[i].Event;
				if (IsPresent(event, difficultyBit)) {
					LevelStatistics single;
					CountEvent(single, event);
					if (counted(single)) {
						offGrid.push_back(std::int32_t(i));
					}
				}
			}
			if (!session.IsEditingEvents()) {
				session.GetActions().Execute(ActionIds::ToolEvent);
			}
			std::int32_t count = std::int32_t(cells.size() + offGrid.size());
			EventSelection& selection = session.GetEventSelection();
			selection.Assign(Death::move(cells));
			selection.AssignOffGrid(Death::move(offGrid));
			session.ShowMessage(_fnx("editor", "{} event selected", "{} events selected", count, count));
		}

		void FillStatistics(UiStack* content, IEditorContext& session)
		{
			const LevelDocument& document = session.GetDocument();
			LevelStatistics stats[3] = {
				LevelStatistics::Compute(document, Formats::EventRecord::Easy),
				LevelStatistics::Compute(document, Formats::EventRecord::Normal),
				LevelStatistics::Compute(document, Formats::EventRecord::Hard)
			};
			content->ClearChildren();

			// All rows first, so the columns can be as wide as their widest text, the header included
			struct CountRow
			{
				String Label;
				String Values[3];
				Colorf Color;
				String Tooltip;
				std::shared_ptr<Function<bool(const LevelStatistics&)>> Counted;
			};
			SmallVector<CountRow, 0> rows;
			auto addCount = [&rows, &stats](String label, std::int32_t LevelStatistics::*member, String tooltip = {}) {
				CountRow& row = rows.emplace_back();
				row.Label = Death::move(label);
				for (std::int32_t i = 0; i < 3; i++) {
					row.Values[i] = FormatInteger(stats[i].*member);
				}
				row.Color = UiTheme::Text;
				row.Tooltip = Death::move(tooltip);
				row.Counted = std::make_shared<Function<bool(const LevelStatistics&)>>([member](const LevelStatistics& single) { return (single.*member > 0); });
			};
			// TRANSLATORS: Rows of the Statistics dialog, what is counted in the level for each difficulty, with tooltips of some of them
			addCount(_x("editor", "Enemies"), &LevelStatistics::Enemies);
			addCount(_x("editor", "Bosses"), &LevelStatistics::Bosses);
			addCount(_x("editor", "Gems"), &LevelStatistics::Gems, _x("editor", "By their value, red ones are worth 1, green 5, blue 10, those in crates and barrels too"));
			addCount(_x("editor", "Giant gems"), &LevelStatistics::GiantGems, _x("editor", "Each breaks into 5 to 12 red gems, which aren't counted in the gems"));
			addCount(_x("editor", "Coins"), &LevelStatistics::Coins, _x("editor", "By their value, silver ones are worth 1, gold 5"));
			addCount(_x("editor", "Food"), &LevelStatistics::Food);
			addCount(_x("editor", "Carrots"), &LevelStatistics::Carrots);
			addCount(_x("editor", "Extra lives"), &LevelStatistics::ExtraLives);
			addCount(_x("editor", "Ammo"), &LevelStatistics::Ammo);
			addCount(_x("editor", "Containers"), &LevelStatistics::Containers, _x("editor", "Crates, barrels and monitors"));
			addCount(_x("editor", "Checkpoints"), &LevelStatistics::Checkpoints);
			addCount(_x("editor", "Warp tiles"), &LevelStatistics::Warps);
			addCount(_x("editor", "Exit tiles"), &LevelStatistics::Exits);
			addCount(_x("editor", "Generators"), &LevelStatistics::Generators, _x("editor", "What they spawn isn't counted above, there's no end to it"));
			{
				// TRANSLATORS: Rows of the Statistics dialog, how many starts of each character the level has
				static const char* const Characters[] = { NC_("editor", "Starts of Jazz"), NC_("editor", "Starts of Spaz"), NC_("editor", "Starts of Lori") };
				for (std::int32_t c = 0; c < 3; c++) {
					CountRow& row = rows.emplace_back();
					row.Label = _x("editor", Characters[c]);
					for (std::int32_t i = 0; i < 3; i++) {
						row.Values[i] = FormatInteger(stats[i].Starts[c]);
					}
					row.Color = (stats[1].Starts[c] == 0 ? UiTheme::TextDanger : UiTheme::Text);
					row.Counted = std::make_shared<Function<bool(const LevelStatistics&)>>([c](const LevelStatistics& single) { return (single.Starts[c] > 0); });
				}
			}
			// TRANSLATORS: Last row of the Statistics dialog, the number of all events in the level, and its tooltip
			addCount(_x("editor", "Events"), &LevelStatistics::Events, _x("editor", "All events in the difficulty, only those in multiplayer are left out"));
			// TRANSLATORS: Column header in the Statistics dialog, the Normal difficulty between Easy and Hard
			String header[3] = { _("Easy"), _x("editor", "Normal"), _("Hard") };

			// A count is a flat button, its text is this far from its right edge, the header above it is placed the same
			const float inset = UiTheme::Padding * 1.5f;
			float labelWidth = LabelWidth;
			float valueWidth = ValueWidth;
			if (UiSurface* surface = session.GetSurface()) {
				// In units, with a little to spare, as the text is scaled in steps
				UiContext context = surface->GetContext();
				auto measure = [&context](StringView text) {
					return (context.Scale > 0.0f ? context.MeasureText(text) / context.Scale + 2.0f : 0.0f);
				};
				float widestLabel = 0.0f, widestValue = 0.0f;
				for (const String& text : header) {
					widestValue = std::max(widestValue, measure(text));
				}
				for (const CountRow& row : rows) {
					widestLabel = std::max(widestLabel, measure(row.Label));
					for (const String& value : row.Values) {
						widestValue = std::max(widestValue, measure(value));
					}
				}
				labelWidth = std::max(widestLabel + UiTheme::Spacing, 60.0f);
				valueWidth = std::max(widestValue + inset * 2.0f, 36.0f);
			}

			{
				auto* row = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
				row->Add<UiLabel>(StringView(), UiTheme::TextHeader)->Width = labelWidth;
				for (const String& text : header) {
					// As wide as the buttons below, so the columns don't drift apart by rounding
					auto* cell = row->Add<UiStack>(UiOrientation::Horizontal, 0.0f, UiThickness(0.0f, 0.0f, inset, 0.0f));
					cell->Width = valueWidth;
					cell->Add<UiLabel>(text, UiTheme::TextHeader, Alignment::Right)->Width = valueWidth - inset;
				}
			}
			IEditorContext* s = &session;
			for (CountRow& countRow : rows) {
				auto* row = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
				row->Add<UiLabel>(countRow.Label, countRow.Color)->Width = labelWidth;
				if (!countRow.Tooltip.empty()) {
					row->Tooltip = countRow.Tooltip;
				}
				for (std::int32_t i = 0; i < 3; i++) {
					std::uint8_t bit = DifficultyBits[i];
					auto counted = countRow.Counted;
					auto* button = row->Add<UiButton>(countRow.Values[i], EditorIcon::None, [s, bit, counted]() {
						SelectCounted(*s, bit, [counted](const LevelStatistics& single) { return (*counted)(single); });
					});
					button->Style = UiButtonStyle::Flat;
					button->TextAlign = Alignment::Right;
					button->TextColor = countRow.Color;
					button->Width = valueWidth;
					button->Tooltip = _x("editor", "Select the events counted here");
				}
			}
			content->Add<UiLabel>(_x("editor", "Events only in multiplayer aren't counted"), UiTheme::TextDim);
		}
	}

	void ShowLevelStatisticsDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr || ReuseSingleDialog(session, "statistics")) {
			return;
		}

		IEditorContext* s = &session;
		// Scrolled if it's taller than the screen, the title stays
		// TRANSLATORS: Title of the dialog with the counts of things in the level
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Statistics"), true);
		auto* table = dialog->GetContent()->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		FillStatistics(table, session);

		// The play-tests kept with the level, how fast it was finished and how often the player died
		const PlayPathHistory& history = session.GetPlayPathHistory();
		if (!history.IsEmpty()) {
			auto formatTime = [](float frames) {
				std::int32_t hundredths = std::int32_t(frames * 100.0f / 60.0f);
				char buffer[32];
				std::snprintf(buffer, sizeof(buffer), "%d:%02d.%02d", hundredths / 6000, (hundredths / 100) % 60, hundredths % 100);
				return String(buffer);
			};
			const PlayPath& last = history.GetPaths().back();
			UiStack* playTests = dialog->GetContent()->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			// TRANSLATORS: Header of the play-tests kept with the level in the Statistics dialog, {} is how many there are
			playTests->Add<UiLabel>(_fnx("editor", "Last {} play-test", "Last {} play-tests", std::int32_t(history.GetPaths().size()), std::int32_t(history.GetPaths().size())), UiTheme::TextHeader);
			float best = history.GetBestExitTime();
			// TRANSLATORS: In the Statistics dialog, {} is the best time of the play-tests as minutes:seconds.hundredths
			playTests->Add<UiLabel>(best >= 0.0f ? String(_fx("editor", "Finished in {} at best", formatTime(best))) : String(_x("editor", "Not finished yet")), UiTheme::Text);
			// TRANSLATORS: In the Statistics dialog, {} is how many times the player died in all the play-tests together
			playTests->Add<UiLabel>(_fx("editor", "{} deaths in them", history.GetTotalDeaths()), UiTheme::Text);
			// TRANSLATORS: In the Statistics dialog, the time the last play-test took as minutes:seconds.hundredths and how many times the player died in it
			playTests->Add<UiLabel>(_fx("editor", "The last one took {}, {} deaths", formatTime(last.Duration), last.GetDeathCount()), UiTheme::TextDim);
		}

		// Counted again once the events stop changing for a moment, the counts are always those of the level as it is
		struct RefreshState
		{
			bool Dirty = false;
			float Wait = 0.0f;
			std::shared_ptr<Function<void(const DocumentChanges&)>> Observer;
		};
		auto state = std::make_shared<RefreshState>();
		std::weak_ptr<RefreshState> weakState = state;
		state->Observer = std::make_shared<Function<void(const DocumentChanges&)>>([weakState](const DocumentChanges& changes) {
			std::shared_ptr<RefreshState> state = weakState.lock();
			if (state != nullptr && changes.HasAny(DocumentChangeKind::Events)) {
				state->Dirty = true;
				state->Wait = 20.0f;
			}
		});
		session.AddChangeObserver(state->Observer);
		dialog->GetContent()->Add<UiUpdateWatcher>([s, state, table](float timeMult) {
			if (state->Dirty) {
				state->Wait -= timeMult;
				if (state->Wait <= 0.0f) {
					state->Dirty = false;
					FillStatistics(table, *s);
				}
			}
		});

		// Closed by its cross or Escape, a window that stays while the level is changed, not a popup
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		ShowSingleDialog(session, "statistics", Death::move(dialog), Death::move(options));
	}
}

#endif
