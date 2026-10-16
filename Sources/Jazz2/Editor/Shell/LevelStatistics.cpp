#include "LevelStatistics.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Events/EventCatalog.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
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

	void ShowLevelStatisticsDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		const LevelDocument& document = session.GetDocument();
		LevelStatistics stats[3] = {
			LevelStatistics::Compute(document, Formats::EventRecord::Easy),
			LevelStatistics::Compute(document, Formats::EventRecord::Normal),
			LevelStatistics::Compute(document, Formats::EventRecord::Hard)
		};

		auto dialog = std::make_unique<UiDialog>(_("Statistics"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();

		auto addRow = [content](StringView label, const String (&values)[3], const Colorf& color) {
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			row->Add<UiLabel>(label, color)->Width = LabelWidth;
			for (const String& value : values) {
				row->Add<UiLabel>(value, color, Alignment::Right)->Width = ValueWidth;
			}
			return row;
		};
		auto addCount = [&addRow, &stats](StringView label, std::int32_t LevelStatistics::*member) {
			String values[3];
			for (std::int32_t i = 0; i < 3; i++) {
				values[i] = String(std::to_string(stats[i].*member).c_str());
			}
			return addRow(label, values, UiTheme::Text);
		};

		{
			String header[3] = { _("Easy"), _("Normal"), _("Hard") };
			addRow({}, header, UiTheme::TextHeader);
		}
		addCount(_("Enemies"), &LevelStatistics::Enemies);
		addCount(_("Bosses"), &LevelStatistics::Bosses);
		addCount(_("Gems"), &LevelStatistics::Gems)->Tooltip = _("By their value, red ones are worth 1, green 5, blue 10, those in crates and barrels too");
		addCount(_("Giant gems"), &LevelStatistics::GiantGems)->Tooltip = _("Each breaks into 5 to 12 red gems, which aren't counted in the gems");
		addCount(_("Coins"), &LevelStatistics::Coins)->Tooltip = _("By their value, silver ones are worth 1, gold 5");
		addCount(_("Food"), &LevelStatistics::Food);
		addCount(_("Carrots"), &LevelStatistics::Carrots);
		addCount(_("Extra lives"), &LevelStatistics::ExtraLives);
		addCount(_("Ammo"), &LevelStatistics::Ammo);
		addCount(_("Containers"), &LevelStatistics::Containers)->Tooltip = _("Crates, barrels and monitors");
		addCount(_("Checkpoints"), &LevelStatistics::Checkpoints);
		addCount(_("Warp tiles"), &LevelStatistics::Warps);
		addCount(_("Exit tiles"), &LevelStatistics::Exits);
		addCount(_("Generators"), &LevelStatistics::Generators)->Tooltip = _("What they spawn isn't counted above, there's no end to it");
		{
			static const char* const Characters[] = { N_("Starts of Jazz"), N_("Starts of Spaz"), N_("Starts of Lori") };
			for (std::int32_t c = 0; c < 3; c++) {
				String values[3];
				for (std::int32_t i = 0; i < 3; i++) {
					values[i] = String(std::to_string(stats[i].Starts[c]).c_str());
				}
				addRow(_(Characters[c]), values, stats[1].Starts[c] == 0 ? UiTheme::TextDanger : UiTheme::Text);
			}
		}
		addCount(_("Events"), &LevelStatistics::Events)->Tooltip = _("All events in the difficulty, only those in multiplayer are left out");
		content->Add<UiLabel>(_("Events only in multiplayer aren't counted"), UiTheme::TextDim);

		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = true;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
