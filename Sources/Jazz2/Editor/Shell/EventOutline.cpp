#include "EventOutline.h"

#if defined(WITH_EDITOR)

#include "ShellCommon.h"
#include "../Tools/EditorContext.h"
#include "../Document/LevelDocument.h"
#include "../Input/ActionRegistry.h"
#include "../Tools/EventTools.h"
#include "../Document/DocumentChanges.h"
#include "../Events/EventCatalog.h"
#include "../Events/EventPreviews.h"
#include "../Runtime/EditorCamera.h"
#include "../Runtime/EditorLevelHandler.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../UI/UiVirtualList.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <memory>
#include <unordered_map>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// Parameters told in a row, the first ones that are numbers or choices
		constexpr std::int32_t MaxDetailFields = 3;
		// Frames the list waits after it was made again before it's made again, a value dragged changes the events in every frame
		constexpr std::int32_t RebuildCooldownFrames = 15;

		struct OutlineItem
		{
			std::uint32_t Cell;
			std::int32_t OffGrid;
			Formats::EventRecord Record;
			Vector2i Position;
			// Told only once the row is seen, or filtered by, see Describe()
			String Details;
			String Difficulty;
			bool Described = false;
		};

		String DescribeParams(const Formats::EventRecord& event)
		{
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
			if (descriptor == nullptr) {
				return {};
			}
			String result;
			std::int32_t shown = 0;
			for (std::uint8_t i = 0; i < descriptor->FieldCount && shown < MaxDetailFields; i++) {
				const EventParamField& field = descriptor->Fields[i];
				if (field.Hidden || !EventCatalog::IsFieldVisible(*descriptor, i, event)) {
					continue;
				}
				String value;
				switch (field.Type) {
					case ParamType::UInt:
					case ParamType::Int:
					case ParamType::TextId:
					case ParamType::TriggerId:
					case ParamType::WarpId:
						value = FormatInteger(EventCatalog::ToDisplayValue(field, EventCatalog::ReadField(event, field)));
						break;
					case ParamType::Bool:
						if (EventCatalog::ReadField(event, field) == 0) {
							continue;
						}
						value = _("yes");
						break;
					case ParamType::Enum: {
						std::int32_t raw = EventCatalog::ReadField(event, field);
						value = FormatInteger(raw);
						for (std::uint8_t j = 0; j < field.OptionCount; j++) {
							if (field.Options[j].Value == raw) {
								value = _(field.Options[j].Label);
								break;
							}
						}
						break;
					}
					case ParamType::EventType: {
						std::int32_t type = EventCatalog::ReadField(event, field);
						if (type == 0) {
							continue;
						}
						value = EventPreviews::GetEventName(std::uint16_t(type));
						break;
					}
					default:
						continue;
				}
				if (!result.empty()) {
					result += ", "_s;
				}
				result += _(field.Name) + " "_s + value;
				shown++;
			}
			return result;
		}

		String DescribeDifficulty(const Formats::EventRecord& event)
		{
			String result;
			std::uint8_t difficulties = event.Flags & Formats::EventRecord::DifficultyMask;
			if (difficulties != 0 && difficulties != Formats::EventRecord::DifficultyMask) {
				// TRANSLATORS: First letters of Easy, Normal and Hard in the list of events, keep each one letter
				if ((difficulties & Formats::EventRecord::Easy) != 0) result += _("E");
				if ((difficulties & Formats::EventRecord::Normal) != 0) result += _("N");
				if ((difficulties & Formats::EventRecord::Hard) != 0) result += _("H");
			}
			if ((event.Flags & Formats::EventRecord::MultiplayerOnly) != 0) {
				if (!result.empty()) {
					result += " "_s;
				}
				// TRANSLATORS: Short for "multiplayer only" in the list of events
				result += _("MP");
			}
			return result;
		}

		void Describe(OutlineItem& item)
		{
			if (!item.Described) {
				item.Details = DescribeParams(item.Record);
				item.Difficulty = DescribeDifficulty(item.Record);
				item.Described = true;
			}
		}

		struct OutlineState
		{
			SmallVector<OutlineItem, 0> Items;
			// Names of the kinds of the events, a level has a few kinds of many events
			std::unordered_map<std::uint16_t, String> Names;
			String Filter;
			bool Dirty = true;
			// Frames before the list can be made again, see RebuildCooldownFrames
			std::int32_t Cooldown = 0;

			const String& GetName(std::uint16_t type)
			{
				auto it = Names.find(type);
				if (it == Names.end()) {
					it = Names.emplace(type, EventPreviews::GetEventName(type)).first;
				}
				return it->second;
			}
			// Row a range picked with Shift starts at
			std::int32_t Anchor = -1;
			// A row picked by the keys, chosen in the next frame unless it was a click, which chooses it itself
			std::int32_t PendingKeyPick = -1;
			std::shared_ptr<Function<void(const DocumentChanges&)>> Observer;
		};

		void GoTo(IEditorContext& session, const OutlineItem& item)
		{
			if (EditorLevelHandler* editHandler = session.GetEditHandler()) {
				Vector2f target = (item.OffGrid >= 0 ? Vector2f(float(item.Position.X), float(item.Position.Y))
					: Vector2f(item.Position.X * 32.0f + 16.0f, item.Position.Y * 32.0f + 16.0f));
				EditorCamera& camera = editHandler->GetCamera();
				camera.FlyTo(target, camera.GetScale());
			}
		}

		// Selects the event of a row, with Ctrl adds it or takes it away, with Shift all from the last one
		void ChooseRow(IEditorContext& session, OutlineState& state, std::int32_t index, std::int32_t modifiers)
		{
			if (index < 0 || index >= std::int32_t(state.Items.size())) {
				return;
			}
			const OutlineItem& item = state.Items[index];
			// The selection and the properties of events are shown only with the event tool
			if (!session.IsEditingEvents()) {
				session.GetActions().Execute(ActionIds::ToolEvent);
			}
			EventSelection& selection = session.GetEventSelection();
			bool ctrl = (modifiers & KeyMod::Ctrl) != 0;
			bool shift = (modifiers & KeyMod::Shift) != 0;
			if (shift && state.Anchor >= 0 && state.Anchor < std::int32_t(state.Items.size())) {
				SmallVector<std::uint32_t, 0> cells;
				SmallVector<std::int32_t, 0> offGrid;
				for (std::int32_t i = std::min(state.Anchor, index); i <= std::max(state.Anchor, index); i++) {
					if (state.Items[i].OffGrid >= 0) {
						offGrid.push_back(state.Items[i].OffGrid);
					} else {
						cells.push_back(state.Items[i].Cell);
					}
				}
				selection.Assign(Death::move(cells));
				selection.AssignOffGrid(Death::move(offGrid));
				return;
			}
			if (ctrl) {
				if (item.OffGrid >= 0) {
					if (selection.ContainsOffGrid(item.OffGrid)) {
						selection.RemoveOffGrid(item.OffGrid);
					} else {
						selection.AddOffGrid(item.OffGrid);
					}
				} else if (selection.Contains(item.Cell)) {
					selection.Remove(item.Cell);
				} else {
					selection.Add(item.Cell);
				}
				state.Anchor = index;
				return;
			}
			if (item.OffGrid >= 0) {
				selection.SetOffGrid(item.OffGrid);
			} else {
				selection.Set(item.Cell);
			}
			state.Anchor = index;
			GoTo(session, item);
		}
	}

	void ShowEventOutline(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr || ReuseSingleDialog(session, "eventList")) {
			return;
		}

		IEditorContext* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("Event List"));
		auto* content = dialog->GetContent();
		auto* header = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		auto* search = header->Add<UiTextBox>();
		search->Placeholder = _("Filter by name or parameters");
		search->Flex = 1.0f;
		auto* summary = header->Add<UiLabel>(StringView(), UiTheme::TextDim);
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 440.0f;
		scroll->Height = 260.0f;
		content->Add<UiLabel>(_("The selected events are changed together in the event properties"), UiTheme::TextDim);

		// Only the rows seen are drawn, a level can have thousands of events
		auto state = std::make_shared<OutlineState>();
		auto* list = scroll->SetContent<UiVirtualList>();
		list->RowHeight = UiTheme::RowHeight + 2.0f;
		list->CountFunc = [state]() { return std::int32_t(state->Items.size()); };
		list->RowText = [state](std::int32_t index) { return (index >= 0 && index < std::int32_t(state->Items.size()) ? state->GetName(state->Items[index].Record.Type) : String()); };
		list->RowTooltip = [](std::int32_t index) {
			return String(_("Click to select the event and go to it, Ctrl+click to add it to the selection, Shift+click to select all from the last one"));
		};
		list->DrawRow = [s, state](UiPainter& painter, std::int32_t index, const Rectf& row, bool picked, float hover) {
			if (index < 0 || index >= std::int32_t(state->Items.size())) {
				return;
			}
			OutlineItem& item = state->Items[index];
			Describe(item);
			const EventSelection& selection = s->GetEventSelection();
			bool selected = (item.OffGrid >= 0 ? selection.ContainsOffGrid(item.OffGrid) : selection.Contains(item.Cell));
			if (selected) {
				painter.RoundRect(row, UiTheme::Selection);
			} else if (hover > 0.0f) {
				Colorf color = UiTheme::Hover;
				color.A *= hover;
				painter.RoundRect(row, color);
			}
			if (picked) {
				painter.RoundRectBorder(row, UiTheme::AccentDim);
			}
			float padding = painter.Px(UiTheme::Padding);
			float iconSize = row.H - painter.Px(2.0f);
			float positionWidth = painter.Px(64.0f);
			float difficultyWidth = painter.Px(36.0f);
			s->GetEventPreviews().DrawFitted(painter, item.Record, Rectf(row.X + painter.Px(1.0f), row.Y + painter.Px(1.0f), iconSize, iconSize), 0.0f);

			float x = row.X + iconSize + padding;
			float nameWidth = std::max<float>((row.W - (x - row.X) - positionWidth - difficultyWidth) * 0.45f, 0.0f);
			float detailsX = x + nameWidth + positionWidth + difficultyWidth;
			painter.PushClip(Rectf(x, row.Y, std::max<float>(row.X + row.W - x, 0.0f), row.H));
			painter.TextInRect(state->GetName(item.Record.Type), Rectf(x, row.Y, nameWidth, row.H), item.OffGrid >= 0 ? UiTheme::TextAccent : UiTheme::Text);
			String position = FormatInteger(item.Position.X) + ", "_s + FormatInteger(item.Position.Y);
			painter.TextInRect(position, Rectf(x + nameWidth, row.Y, positionWidth, row.H), UiTheme::TextDim);
			painter.TextInRect(item.Difficulty, Rectf(x + nameWidth + positionWidth, row.Y, difficultyWidth, row.H), UiTheme::TextDim);
			painter.TextInRect(item.Details, Rectf(detailsX, row.Y, std::max<float>(row.X + row.W - detailsX, 0.0f), row.H), UiTheme::TextDim);
			painter.PopClip();
		};
		list->OnRowPressed = [s, state](std::int32_t index, Vector2f, const UiPointerEvent& event) {
			state->PendingKeyPick = -1;
			ChooseRow(*s, *state, index, event.Modifiers);
		};
		list->OnSelectionChanged = [state](std::int32_t index) {
			// A click follows with the modifiers, the keys choose the row themselves in the next frame
			state->PendingKeyPick = index;
		};

		auto rebuild = [s, state, summary]() {
			state->Dirty = false;
			state->Cooldown = RebuildCooldownFrames;
			state->Items.clear();
			// The language or the descriptions could have changed
			state->Names.clear();
			state->Anchor = -1;
			const LevelDocument& document = s->GetDocument();
			Vector2i gridSize = document.GetEventGridSize();
			std::int32_t total = 0;
			auto addItem = [&](const Formats::EventRecord& record, std::uint32_t cell, std::int32_t offGrid, Vector2i position) {
				total++;
				OutlineItem item;
				item.Cell = cell;
				item.OffGrid = offGrid;
				item.Record = record;
				item.Position = position;
				if (!state->Filter.empty()) {
					Describe(item);
					const EventDescriptor* descriptor = EventCatalog::Find(EventType(record.Type));
					String haystack = state->GetName(record.Type) + " "_s + item.Details + " "_s + (descriptor != nullptr ? StringView(descriptor->Key) : StringView());
					if (!MatchesSearch(haystack, state->Filter)) {
						return;
					}
				}
				state->Items.push_back(Death::move(item));
			};
			if (gridSize.X > 0) {
				for (const DocumentEvent& event : document.Events) {
					addItem(event.Record, event.Cell, -1, Vector2i(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X))));
				}
			}
			for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
				const Formats::OffGridEventRecord& event = document.OffGridEvents[i];
				addItem(event.Event, 0, std::int32_t(i), Vector2i(event.X, event.Y));
			}
			summary->Text = (std::int32_t(state->Items.size()) == total ? _fn("{} event", "{} events", total, total)
				: _f("{} of {}", std::int32_t(state->Items.size()), total));
		};
		search->OnChanged = [state](StringView text) {
			state->Filter = text;
			state->Dirty = true;
			// Typed, the list follows at once
			state->Cooldown = 0;
		};

		// Follows the events of the level as they change
		std::weak_ptr<OutlineState> weakState = state;
		state->Observer = std::make_shared<Function<void(const DocumentChanges&)>>([weakState](const DocumentChanges& changes) {
			if (std::shared_ptr<OutlineState> state = weakState.lock()) {
				if (changes.HasAny(DocumentChangeKind::Events | DocumentChangeKind::LayerStructure)) {
					state->Dirty = true;
				}
			}
		});
		session.AddChangeObserver(state->Observer);
		content->Add<UiUpdateWatcher>([s, state, rebuild, list](float) {
			if (state->PendingKeyPick >= 0) {
				std::int32_t index = state->PendingKeyPick;
				state->PendingKeyPick = -1;
				ChooseRow(*s, *state, index, 0);
			}
			// Made again at most every few frames, a value dragged changes the events in every frame
			if (state->Cooldown > 0) {
				state->Cooldown--;
			}
			if (state->Dirty && state->Cooldown <= 0) {
				rebuild();
				list->SetSelected(-1, false);
			}
		});
		rebuild();

		UiOverlayOptions options;
		options.Position = Vector2f(1.0e6f, 0.0f);
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		ShowSingleDialog(session, "eventList", Death::move(dialog), Death::move(options));
	}
}

#endif
