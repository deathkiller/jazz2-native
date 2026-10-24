#include "FindReplaceDialog.h"

#if defined(WITH_EDITOR)

#include "ShellCommon.h"
#include "../Tools/EditorContext.h"
#include "../Common/TileStamp.h"
#include "../Document/CommandHistory.h"
#include "../Input/ActionRegistry.h"
#include "../Document/Commands.h"
#include "../Events/EventCatalog.h"
#include "../Events/EventPreviews.h"
#include "../Runtime/TileDrawing.h"
#include "../Tools/EventTools.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../Tiles/TileMap.h"
#include "../../../nCine/Base/BitArray.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <memory>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::uint8_t FlipFlags = Formats::TileRecord::FlipX | Formats::TileRecord::FlipY;

		bool Matches(const TileRef& tile, const TileRef& find, bool anyFlip)
		{
			return (tile.Index == find.Index && tile.Source == find.Source &&
				(anyFlip || (tile.Flags & FlipFlags) == (find.Flags & FlipFlags)));
		}

		struct FindState
		{
			bool HasFindTile = false;
			bool HasReplaceTile = false;
			TileRef FindTile = {};
			TileRef ReplaceTile = {};
			bool AnyFlip = false;
			bool AllLayers = false;
			EventType FindEvent = EventType::Empty;
			EventType ReplaceEvent = EventType::Empty;
			bool TilesInSelection = false;
			// Values of the parameters of the kind found, only those of the mask are compared, and those the new events get
			Formats::EventRecord FindValues = {};
			std::uint32_t FindMask = 0;
			Formats::EventRecord ReplaceValues = {};
			bool EventsInSelection = false;
			bool KeepParams = true;
			// An event being chosen in the list for it, see EditorSession::PickEventFromList()
			std::weak_ptr<bool> PendingPick;

			EventFindOptions GetEventFind() const
			{
				EventFindOptions options;
				options.Type = FindEvent;
				options.Values = FindValues;
				options.OnlySelected = EventsInSelection;
				// A parameter not shown for the values of the others isn't compared, as the event properties don't show it either
				const EventDescriptor* descriptor = EventCatalog::Find(FindEvent);
				if (descriptor != nullptr && FindValues.Type == std::uint16_t(FindEvent)) {
					for (std::uint8_t i = 0; i < descriptor->FieldCount && i < 32; i++) {
						if ((FindMask & (1u << i)) != 0 && EventCatalog::IsFieldVisible(*descriptor, i, FindValues)) {
							options.FieldMask |= (1u << i);
						}
					}
				}
				return options;
			}

			Formats::EventRecord& GetValues(bool find)
			{
				return (find ? FindValues : ReplaceValues);
			}

			// Another kind, with the parameters of a new event, or those of an event of the kind
			void SetEvent(bool find, EventType type, const Formats::EventRecord* values = nullptr)
			{
				(find ? FindEvent : ReplaceEvent) = type;
				GetValues(find) = (values != nullptr && values->Type == std::uint16_t(type) ? *values : EventCatalog::MakeDefault(type));
				if (find) {
					FindMask = 0;
				}
			}
		};

		// Whether a parameter is shown and edited here, the values of those the event properties edit by a control of their own
		bool IsEditableField(const EventParamField& field)
		{
			if (field.Hidden) {
				return false;
			}
			switch (field.Type) {
				case ParamType::Bool:
				case ParamType::Enum:
				case ParamType::Flags:
				case ParamType::UInt:
				case ParamType::Int:
				case ParamType::TextId:
				case ParamType::TriggerId:
				case ParamType::WarpId:
					return true;
				default:
					return false;
			}
		}

		// A parameter of a kind of event as the controls refer to it, the catalog may be read again while they're shown,
		// which replaces all descriptors, so it's looked up each time and is gone once the catalog was read again
		struct FieldKey
		{
			EventType Type;
			std::uint8_t Index;
			std::uint32_t Revision;
		};

		const EventDescriptor* ResolveDescriptor(const FieldKey& key)
		{
			if (EventCatalog::GetRevision() != key.Revision) {
				return nullptr;
			}
			const EventDescriptor* descriptor = EventCatalog::Find(key.Type);
			return (descriptor != nullptr && key.Index < descriptor->FieldCount ? descriptor : nullptr);
		}

		const EventParamField* ResolveField(const FieldKey& key)
		{
			const EventDescriptor* descriptor = ResolveDescriptor(key);
			return (descriptor != nullptr ? &descriptor->Fields[key.Index] : nullptr);
		}

		// Whether a parameter of the new events is kept from the events found, as ReplaceEvents() keeps it
		bool IsKeptField(const EventDescriptor* found, const EventParamField& field)
		{
			if (found == nullptr || field.Type == ParamType::Raw || field.Type == ParamType::Contents || field.Type == ParamType::String) {
				return false;
			}
			for (std::uint8_t i = 0; i < found->FieldCount; i++) {
				if (found->Fields[i].Type == field.Type && StringView(found->Fields[i].Name) == StringView(field.Name)) {
					return true;
				}
			}
			return false;
		}

		/** The parameters of the event found or of the new one, edited by the same controls as in the event properties */
		class EventParamsEditor : public UiForm
		{
		public:
			// Under the kind they belong to
			static constexpr float Indent = 12.0f;

			EventParamsEditor(std::shared_ptr<FindState> state, bool find)
				: UiForm(UiTheme::Spacing, UiThickness(Indent, 0.0f, 0.0f, 0.0f)), _state(Death::move(state)),
					_find(find), _shownType(EventType::Empty), _shownRevision(0), _built(false)
			{
			}

			void Update(float timeMult) override
			{
				// The values are those of the kind shown, whichever way it was chosen
				EventType type = (_find ? _state->FindEvent : _state->ReplaceEvent);
				if (_state->GetValues(_find).Type != std::uint16_t(type)) {
					_state->SetEvent(_find, type);
				}
				std::uint32_t revision = EventCatalog::GetRevision();
				if (!_built || type != _shownType || revision != _shownRevision) {
					_built = true;
					_shownType = type;
					_shownRevision = revision;
					Rebuild(type);
				}
				UiStack::Update(timeMult);
			}

		private:
			std::shared_ptr<FindState> _state;
			bool _find;
			EventType _shownType;
			std::uint32_t _shownRevision;
			bool _built;

			void Rebuild(EventType type)
			{
				ClearChildren();
				const EventDescriptor* descriptor = EventCatalog::Find(type);
				if (descriptor == nullptr || type == EventType::Empty) {
					return;
				}
				std::int32_t count = 0;
				for (std::uint8_t i = 0; i < descriptor->FieldCount && i < 32; i++) {
					if (!IsEditableField(descriptor->Fields[i])) {
						continue;
					}
					if (count == 0 && _find) {
						// Short, the window is as wide as its content
						// TRANSLATORS: Above the parameters of the event to find in the Find and Replace dialog, only events with the ticked values are found, keep it short
						Add<UiLabel>(_x("editor", "Only those with the values ticked"), UiTheme::TextDim);
					}
					count++;
					std::int32_t firstChild = GetChildCount();
					AddField(descriptor, i);
					// Shown by the values of the others, as in the event properties
					std::shared_ptr<FindState> state = _state;
					bool find = _find;
					FieldKey key = { descriptor->Type, i, EventCatalog::GetRevision() };
					for (std::int32_t c = firstChild; c < GetChildCount(); c++) {
						GetChild(c)->IsVisibleFunc = [state, find, key]() {
							const EventDescriptor* current = ResolveDescriptor(key);
							Formats::EventRecord& values = state->GetValues(find);
							return (current != nullptr && values.Type == std::uint16_t(key.Type) && EventCatalog::IsFieldVisible(*current, key.Index, values));
						};
					}
				}
				if (count == 0) {
					// TRANSLATORS: In the Find and Replace dialog instead of the parameters of the event, when it has none
					Add<UiLabel>(_x("editor", "No parameters"), UiTheme::TextDisabled);
				}
			}

			void AddField(const EventDescriptor* descriptor, std::uint8_t index)
			{
				const EventParamField* f = &descriptor->Fields[index];
				std::shared_ptr<FindState> state = _state;
				bool find = _find;
				std::uint32_t bit = (1u << index);
				String label = f->Name;
				StringView unit = GetUnitSuffix(f->Unit);
				if (!unit.empty() && f->Type != ParamType::Bool && f->Type != ParamType::Enum && f->Type != ParamType::Flags) {
					label = label + " ("_s + unit + ")"_s;
				}
				String tooltip = (f->Description != nullptr ? String(f->Description) : String());
				// The controls outlive the descriptor if the catalog is read again, until they're made again
				FieldKey key = { descriptor->Type, index, EventCatalog::GetRevision() };
				auto read = [state, find, key]() -> std::int32_t {
					const EventParamField* field = ResolveField(key);
					return (field != nullptr ? EventCatalog::ReadField(state->GetValues(find), *field) : 0);
				};
				// A value set for the events found is what they're found by
				auto write = [state, find, key, bit](std::int32_t value) {
					const EventParamField* field = ResolveField(key);
					if (field == nullptr) {
						return;
					}
					EventCatalog::WriteField(state->GetValues(find), *field, value);
					if (find) {
						state->FindMask |= bit;
					}
				};
				auto isKept = [state, key]() {
					const EventParamField* field = ResolveField(key);
					return (field != nullptr && state->KeepParams && IsKeptField(EventCatalog::Find(state->FindEvent), *field));
				};

				// The label in the column of the form, a box that ticks the parameter for finding, and the control of the value
				UiStack* row = nullptr;
				if (find) {
					row = AddRow(label, tooltip);
					auto* check = row->Add<UiCheckBox>(StringView(), [state, bit]() {
						return (state->FindMask & bit) != 0;
					}, [state, bit](bool value) {
						state->FindMask = (value ? (state->FindMask | bit) : (state->FindMask & ~bit));
					});
					check->Tooltip = _x("editor", "Find only the events with this value");
				} else if (f->Type == ParamType::Bool) {
					// As in the event properties, the box is the value
					auto* check = Add<UiCheckBox>(label, [read]() { return (read() != 0); }, [write](bool value) { write(value ? 1 : 0); });
					check->Tooltip = tooltip;
					check->IsEnabledFunc = [isKept]() { return !isKept(); };
					return;
				} else {
					row = AddRow(label, tooltip);
					// Kept from each event found, its value here isn't used
					row->GetParent()->IsEnabledFunc = [isKept]() { return !isKept(); };
				}

				switch (f->Type) {
					case ParamType::Bool: {
						auto* dropdown = row->Add<UiDropdown>([read]() {
							return String(read() != 0 ? _("Yes") : _("No"));
						}, [read, write](SmallVectorImpl<UiMenuEntry>& entries) {
							std::int32_t current = (read() != 0 ? 1 : 0);
							for (std::int32_t value : { 1, 0 }) {
								UiMenuEntry entry;
								entry.Label = (value != 0 ? _("Yes") : _("No"));
								entry.IsChecked = [current, value]() { return (current == value); };
								entry.Execute = [write, value]() { write(value); };
								entries.push_back(Death::move(entry));
							}
						});
						dropdown->Flex = 1.0f;
						dropdown->Tooltip = tooltip;
						break;
					}
					case ParamType::Enum: {
						auto* dropdown = row->Add<UiDropdown>([read, key]() {
							std::int32_t value = read();
							if (const EventParamField* field = ResolveField(key)) {
								for (std::uint8_t i = 0; i < field->OptionCount; i++) {
									if (field->Options[i].Value == value) {
										return String(field->Options[i].Label);
									}
								}
							}
							return FormatInteger(value);
						}, [read, write, key](SmallVectorImpl<UiMenuEntry>& entries) {
							const EventParamField* field = ResolveField(key);
							if (field == nullptr) {
								return;
							}
							std::int32_t current = read();
							for (std::uint8_t i = 0; i < field->OptionCount; i++) {
								std::int32_t value = field->Options[i].Value;
								UiMenuEntry entry;
								entry.Label = field->Options[i].Label;
								entry.IsChecked = [current, value]() { return (current == value); };
								entry.Execute = [write, value]() { write(value); };
								entries.push_back(Death::move(entry));
							}
						});
						dropdown->Flex = 1.0f;
						dropdown->Tooltip = tooltip;
						break;
					}
					case ParamType::Flags: {
						// Each flag below, the parameter as a whole is compared
						row->Add<UiElement>()->Flex = 1.0f;
						auto* checks = Add<UiStack>(UiOrientation::Vertical, 1.0f, UiThickness(8.0f, 0.0f, 0.0f, 0.0f));
						if (!find) {
							checks->IsEnabledFunc = [isKept]() { return !isKept(); };
						}
						for (std::uint8_t i = 0; i < f->OptionCount; i++) {
							std::int32_t mask = f->Options[i].Value;
							checks->Add<UiCheckBox>(f->Options[i].Label, [read, mask]() {
								return ((read() & mask) == mask);
							}, [read, write, mask](bool value) {
								std::int32_t flags = read();
								write(value ? (flags | mask) : (flags & ~mask));
							});
						}
						break;
					}
					default: {
						// Shown multiplied by its scale, as the steps of a bridge in pixels, see EventParamField::DisplayScale
						auto* number = row->Add<UiNumberBox>([read, key]() {
							const EventParamField* field = ResolveField(key);
							return (field != nullptr ? float(EventCatalog::ToDisplayValue(*field, read())) : 0.0f);
						}, [write, key](float value, bool final) {
							if (const EventParamField* field = ResolveField(key)) {
								write(EventCatalog::FromDisplayValue(*field, std::int32_t(std::round(value))));
							}
						}, float(EventCatalog::ToDisplayValue(*f, f->Min)), float(EventCatalog::ToDisplayValue(*f, f->Max)),
							float(std::max<std::int32_t>(f->DisplayScale, 1)), 0);
						number->Flex = 1.0f;
						number->Tooltip = tooltip;
						break;
					}
				}
			}
		};

		/** A tile or nothing, as the brush has it */
		class TileSlot : public UiElement
		{
		public:
			TileSlot(IEditorContext& session, Function<const TileRef*()>&& tile)
				: _session(session), _tile(Death::move(tile))
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float size = context.Px(UiTheme::RowHeight) * 1.5f;
				return Vector2f(size, size);
			}

			void Draw(UiPainter& painter) override
			{
				painter.RoundRect(Bounds, UiTheme::InputFill);
				const TileRef* tile = _tile();
				Tiles::TileMap* tileMap = _session.GetTileMap();
				if (tile == nullptr || tileMap == nullptr) {
					return;
				}
				float inset = painter.Px(2.0f);
				DrawTile(painter, *tileMap, _session.GetDocument(), *tile, tile->Flags,
					Rectf(Bounds.X + inset, Bounds.Y + inset, Bounds.W - inset * 2.0f, Bounds.H - inset * 2.0f));
			}

		private:
			IEditorContext& _session;
			Function<const TileRef*()> _tile;
		};

		const TileRef* GetBrushTile(IEditorContext& session)
		{
			const TileStamp& stamp = session.GetStamp();
			return (stamp.IsEmpty() ? nullptr : &stamp.Tiles[0]);
		}
	}

	std::int32_t ReplaceTiles(IEditorContext& session, std::int32_t layer, const TileRef& find, const TileRef& replace, bool anyFlip, bool onlySelected)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t replaced = 0;
		// The same tile would only add a step that changes nothing, the flags of the found tiles are kept anyway
		if (replace.Index == find.Index && replace.Source == find.Source) {
			return 0;
		}
		// Only in the selection, which is always in one layer
		TileSelection& selection = session.GetSelection();
		if (onlySelected) {
			if (selection.IsEmpty()) {
				return 0;
			}
			layer = selection.GetLayer();
		}
		CommandHistory& history = session.GetHistory();
		// TRANSLATORS: Name of an undo step in the history, tiles were replaced in the Find and Replace dialog
		history.BeginTransaction(_x("editor", "Replace Tiles"));
		for (std::int32_t l = 0; l < std::int32_t(document.Layers.size()); l++) {
			if ((layer >= 0 && l != layer) || !session.IsLayerEditable(l)) {
				continue;
			}
			const DocumentLayer& source = document.Layers[l];
			// TRANSLATORS: Name of an undo step in the history, tiles were replaced in the Find and Replace dialog
			auto command = std::make_unique<SetTilesCommand>(document, l, _x("editor", "Replace Tiles"));
			std::int32_t count = 0;
			for (std::int32_t y = 0; y < source.GetHeight(); y++) {
				for (std::int32_t x = 0; x < source.GetWidth(); x++) {
					const TileRef& tile = source.GetTile(x, y);
					if (!Matches(tile, find, anyFlip) || (onlySelected && !selection.Contains(x, y))) {
						continue;
					}
					// Keeps how it was flipped and whether it was translucent
					TileRef result = replace;
					result.Flags = tile.Flags;
					command->Set(x, y, result);
					count++;
				}
			}
			if (count > 0) {
				session.Execute(Death::move(command));
				replaced += count;
			}
		}
		history.EndTransaction();
		return replaced;
	}

	bool MatchesEventFind(IEditorContext& session, const EventFindOptions& find, const Formats::EventRecord& event, std::uint32_t cell, std::int32_t offGrid)
	{
		if (EventType(event.Type) != find.Type) {
			return false;
		}
		if (find.OnlySelected) {
			const EventSelection& selection = session.GetEventSelection();
			if (offGrid >= 0 ? !selection.ContainsOffGrid(offGrid) : !selection.Contains(cell)) {
				return false;
			}
		}
		if (find.FieldMask != 0) {
			const EventDescriptor* descriptor = EventCatalog::Find(find.Type);
			if (descriptor == nullptr) {
				return false;
			}
			for (std::uint8_t i = 0; i < descriptor->FieldCount && i < 32; i++) {
				if ((find.FieldMask & (1u << i)) != 0 &&
					EventCatalog::ReadField(event, descriptor->Fields[i]) != EventCatalog::ReadField(find.Values, descriptor->Fields[i])) {
					return false;
				}
			}
		}
		return true;
	}

	std::int32_t CountFoundEvents(IEditorContext& session, const EventFindOptions& find)
	{
		const LevelDocument& document = session.GetDocument();
		std::int32_t count = 0;
		for (const DocumentEvent& event : document.Events) {
			count += (MatchesEventFind(session, find, event.Record, event.Cell, -1) ? 1 : 0);
		}
		for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
			count += (MatchesEventFind(session, find, document.OffGridEvents[i].Event, 0, std::int32_t(i)) ? 1 : 0);
		}
		return count;
	}

	std::int32_t ReplaceEvents(IEditorContext& session, EventType find, EventType replace)
	{
		EventFindOptions options;
		options.Type = find;
		return ReplaceEvents(session, options, replace, false);
	}

	std::int32_t ReplaceEvents(IEditorContext& session, const EventFindOptions& find, EventType replace, bool keepParams)
	{
		return ReplaceEvents(session, find, EventCatalog::MakeDefault(replace), keepParams);
	}

	std::int32_t ReplaceEvents(IEditorContext& session, const EventFindOptions& find, const Formats::EventRecord& replace, bool keepParams)
	{
		LevelDocument& document = session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		// Events are on the sprite layer, they're locked with it as the event properties have them
		if (!session.IsLayerEditable(document.GetSpriteLayerIndex())) {
			return -1;
		}
		// Of the same kind, keeping all of its parameters would change nothing
		if ((find.Type == EventType(replace.Type) && keepParams) || gridSize.X <= 0 || CountFoundEvents(session, find) == 0) {
			return 0;
		}
		const EventDescriptor* oldDescriptor = EventCatalog::Find(find.Type);
		const EventDescriptor* newDescriptor = EventCatalog::Find(EventType(replace.Type));
		auto replaceEvent = [&replace, keepParams, oldDescriptor, newDescriptor](const Formats::EventRecord& event) {
			Formats::EventRecord result = replace;
			result.Flags = event.Flags;
			result.GeneratorFlags = event.GeneratorFlags;
			result.GeneratorDelay = event.GeneratorDelay;
			// Parameters of the same name and kind mean the same, as the speed of two kinds of springs
			if (keepParams && oldDescriptor != nullptr && newDescriptor != nullptr) {
				for (std::uint8_t i = 0; i < newDescriptor->FieldCount; i++) {
					const EventParamField& newField = newDescriptor->Fields[i];
					if (newField.Type == ParamType::Raw || newField.Type == ParamType::Contents || newField.Type == ParamType::String) {
						continue;
					}
					for (std::uint8_t j = 0; j < oldDescriptor->FieldCount; j++) {
						const EventParamField& oldField = oldDescriptor->Fields[j];
						if (oldField.Type == newField.Type && StringView(oldField.Name) == StringView(newField.Name)) {
							EventCatalog::WriteField(result, newField, EventCatalog::ReadField(event, oldField));
							break;
						}
					}
				}
			}
			return result;
		};

		std::int32_t replaced = 0;
		// TRANSLATORS: Name of an undo step in the history, events were replaced in the Find and Replace dialog
		auto command = std::make_unique<SetEventsCommand>(document, _x("editor", "Replace Events"));
		for (const DocumentEvent& event : document.Events) {
			if (MatchesEventFind(session, find, event.Record, event.Cell, -1)) {
				command->Set(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X)), replaceEvent(event.Record));
				replaced++;
			}
		}

		SmallVector<Formats::OffGridEventRecord, 0> offGrid;
		bool offGridChanged = false;
		for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
			const Formats::OffGridEventRecord& event = document.OffGridEvents[i];
			Formats::OffGridEventRecord& copy = offGrid.emplace_back(event);
			if (MatchesEventFind(session, find, event.Event, 0, std::int32_t(i))) {
				copy.Event = replaceEvent(event.Event);
				offGridChanged = true;
				replaced++;
			}
		}

		CommandHistory& history = session.GetHistory();
		// TRANSLATORS: Name of an undo step in the history, events were replaced in the Find and Replace dialog
		history.BeginTransaction(_x("editor", "Replace Events"));
		command->Seal();
		if (!command->IsEmpty()) {
			session.Execute(Death::move(command));
		}
		if (offGridChanged) {
			// TRANSLATORS: Name of an undo step in the history, events were replaced in the Find and Replace dialog
			session.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(offGrid), _x("editor", "Replace Events")));
		}
		history.EndTransaction();
		return replaced;
	}

	void ShowFindReplaceDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr || ReuseSingleDialog(session, "findReplace")) {
			return;
		}

		IEditorContext* s = &session;
		auto state = std::make_shared<FindState>();
		if (const TileRef* tile = GetBrushTile(session)) {
			state->FindTile = *tile;
			state->HasFindTile = true;
		}
		Formats::EventRecord selected;
		if (GetPrimaryEvent(session, selected)) {
			state->SetEvent(true, EventType(selected.Type), &selected);
		}

		// The parameters of the events make it tall, it scrolls on a small screen
		// TRANSLATORS: Title of the dialog that finds and replaces tiles and events in the level
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Find and Replace"), true);
		auto* content = dialog->GetContent();

		// Tiles
		// TRANSLATORS: Header of the part of the Find and Replace dialog that replaces tiles
		content->Add<UiLabel>(_x("editor", "Tiles"), UiTheme::TextHeader);
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			// TRANSLATORS: Label in the Find and Replace dialog next to the tile to find (verb), keep it short
			row->Add<UiLabel>(_x("editor", "Find"))->Width = 95.0f;
			row->Add<TileSlot>(session, Function<const TileRef*()>([state]() -> const TileRef* { return (state->HasFindTile ? &state->FindTile : nullptr); }));
			// TRANSLATORS: Button in the Find and Replace dialog, takes the first tile of the brush
			row->Add<UiButton>(_x("editor", "From Brush"), EditorIcon::Brush, [s, state]() {
				if (const TileRef* tile = GetBrushTile(*s)) {
					state->FindTile = *tile;
					state->HasFindTile = true;
				}
			})->Tooltip = _x("editor", "The first tile of the brush, pick it in the tiles or in the level with the eyedropper");
		}
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			// TRANSLATORS: Label in the Find and Replace dialog next to the tile that replaces the found ones, keep it short
			row->Add<UiLabel>(_x("editor", "Replace with"))->Width = 95.0f;
			row->Add<TileSlot>(session, Function<const TileRef*()>([state]() -> const TileRef* { return (state->HasReplaceTile ? &state->ReplaceTile : nullptr); }));
			// TRANSLATORS: Button in the Find and Replace dialog, takes the first tile of the brush
			row->Add<UiButton>(_x("editor", "From Brush"), EditorIcon::Brush, [s, state]() {
				if (const TileRef* tile = GetBrushTile(*s)) {
					state->ReplaceTile = *tile;
					state->HasReplaceTile = true;
				}
			});
		}
		// TRANSLATORS: Check box in the Find and Replace dialog, finds the tile also when it's flipped in another way
		content->Add<UiCheckBox>(_x("editor", "Flipped any way"), [state]() { return state->AnyFlip; }, [state](bool value) { state->AnyFlip = value; })
			->Tooltip = _x("editor", "Otherwise only tiles flipped the same way are found");
		// TRANSLATORS: Check box in the Find and Replace dialog, replaces the tile in all layers of the level
		content->Add<UiCheckBox>(_x("editor", "In all layers"), [state]() { return state->AllLayers; }, [state](bool value) { state->AllLayers = value; })
			->Tooltip = _x("editor", "Otherwise only in the layer being edited, selecting is always in it");
		// TRANSLATORS: Check box in the Find and Replace dialog, replaces only the selected tiles
		content->Add<UiCheckBox>(_x("editor", "Only in the selection"), [state]() { return state->TilesInSelection; }, [state](bool value) { state->TilesInSelection = value; })
			->Tooltip = _x("editor", "Replaces only the selected tiles, in the layer they're selected in");
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			// TRANSLATORS: Button in the Find and Replace dialog, selects the found tiles (verb)
			auto* select = row->Add<UiButton>(_x("editor", "Select"), EditorIcon::Select, [s, state]() {
				std::int32_t layerIndex = s->GetCurrentLayer();
				LevelDocument& document = s->GetDocument();
				if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
					return;
				}
				const DocumentLayer& layer = document.Layers[layerIndex];
				BitArray mask(ValueInit, std::size_t(layer.GetWidth()) * std::size_t(layer.GetHeight()));
				std::int32_t count = 0;
				for (std::int32_t i = 0; i < std::int32_t(layer.Tiles.size()); i++) {
					if (Matches(layer.Tiles[i], state->FindTile, state->AnyFlip)) {
						mask.set(std::size_t(i));
						count++;
					}
				}
				s->GetSelection().Select(layerIndex, Vector2i(layer.GetWidth(), layer.GetHeight()), mask, false);
				s->ShowMessage(_fnx("editor", "{} tile selected", "{} tiles selected", count, count));
			});
			select->IsEnabledFunc = [state]() { return state->HasFindTile; };
			select->Tooltip = _x("editor", "Select the tiles found in the layer being edited");
			// TRANSLATORS: Button in the Find and Replace dialog, replaces the found tiles (verb)
			auto* replace = row->Add<UiButton>(_x("editor", "Replace"), EditorIcon::None, [s, state]() {
				std::int32_t count = ReplaceTiles(*s, state->AllLayers ? -1 : s->GetCurrentLayer(), state->FindTile, state->ReplaceTile, state->AnyFlip, state->TilesInSelection);
				s->ShowMessage(count == 0 ? String(_x("editor", "No tile was found")) : _fnx("editor", "{} tile replaced", "{} tiles replaced", count, count));
			});
			replace->IsEnabledFunc = [state]() { return state->HasFindTile && state->HasReplaceTile; };
		}

		content->Add<UiSeparator>();

		// Events
		// TRANSLATORS: Header of the part of the Find and Replace dialog that replaces events
		content->Add<UiLabel>(_x("editor", "Events"), UiTheme::TextHeader);
		// The kind and, under it, its parameters as the event properties show them
		auto addEventRows = [s, content](StringView label, bool find, std::shared_ptr<FindState> state) {
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<UiLabel>(label)->Width = 95.0f;
			auto* name = row->Add<UiLabel>(StringView());
			name->Flex = 1.0f;
			name->TextFunc = [state, find]() {
				EventType type = (find ? state->FindEvent : state->ReplaceEvent);
				// TRANSLATORS: In the Find and Replace dialog instead of the name of the event, when no event is chosen
				return (type == EventType::Empty ? String(_x("editor", "Nothing")) : EventPreviews::GetEventName(std::uint16_t(type)));
			};
			// TRANSLATORS: Button in the Find and Replace dialog, the event is then picked in the Events tab (verb)
			row->Add<UiButton>(_x("editor", "Choose"), EditorIcon::Event, [s, state, find]() {
				// The function waiting in the session holds it, it's gone once the pick ends in any way
				auto token = std::make_shared<bool>(true);
				state->PendingPick = token;
				s->PickEventFromList([state, find, token](EventType type) { state->SetEvent(find, type); });
			});
			// TRANSLATORS: Button in the Find and Replace dialog, takes the selected event with its parameters
			row->Add<UiButton>(_x("editor", "Selected"), EditorIcon::None, [s, state, find]() {
				// With its parameters too
				Formats::EventRecord event;
				if (GetPrimaryEvent(*s, event)) {
					state->SetEvent(find, EventType(event.Type), &event);
				} else {
					state->SetEvent(find, EventType::Empty);
				}
			})->Tooltip = _x("editor", "The first selected event, or the event to place, with its parameters");
			content->Add<EventParamsEditor>(state, find);
		};
		// TRANSLATORS: Label in the Find and Replace dialog next to the event to find (verb), keep it short
		addEventRows(_x("editor", "Find"), true, state);
		// TRANSLATORS: Label in the Find and Replace dialog next to the event that replaces the found ones, keep it short
		addEventRows(_x("editor", "Replace with"), false, state);
		// TRANSLATORS: Check box in the Find and Replace dialog, finds only among the selected events
		content->Add<UiCheckBox>(_x("editor", "Only the selected events"), [state]() { return state->EventsInSelection; }, [state](bool value) { state->EventsInSelection = value; })
			->Tooltip = _x("editor", "Finds only among the selected events");
		// TRANSLATORS: Check box in the Find and Replace dialog, the new events keep the values of the parameters of the same name
		content->Add<UiCheckBox>(_x("editor", "Keep matching parameters"), [state]() { return state->KeepParams; }, [state](bool value) { state->KeepParams = value; })
			->Tooltip = _x("editor", "The new events keep the values of the parameters of the same name, those are dimmed above, the others get the values set");
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			// TRANSLATORS: Button in the Find and Replace dialog, selects the found events (verb)
			auto* select = row->Add<UiButton>(_x("editor", "Select"), EditorIcon::Select, [s, state]() {
				LevelDocument& document = s->GetDocument();
				EventFindOptions find = state->GetEventFind();
				SmallVector<std::uint32_t, 0> cells;
				for (const DocumentEvent& event : document.Events) {
					if (MatchesEventFind(*s, find, event.Record, event.Cell, -1)) {
						cells.push_back(event.Cell);
					}
				}
				SmallVector<std::int32_t, 0> offGrid;
				for (std::size_t i = 0; i < document.OffGridEvents.size(); i++) {
					if (MatchesEventFind(*s, find, document.OffGridEvents[i].Event, 0, std::int32_t(i))) {
						offGrid.push_back(std::int32_t(i));
					}
				}
				std::int32_t count = std::int32_t(cells.size() + offGrid.size());
				if (!s->IsEditingEvents()) {
					s->GetActions().Execute(ActionIds::ToolEvent);
				}
				EventSelection& selection = s->GetEventSelection();
				selection.Assign(Death::move(cells));
				selection.AssignOffGrid(Death::move(offGrid));
				s->ShowMessage(_fnx("editor", "{} event selected", "{} events selected", count, count));
			});
			select->IsEnabledFunc = [state]() { return state->FindEvent != EventType::Empty; };
			// TRANSLATORS: Button in the Find and Replace dialog, replaces the found events (verb)
			auto* replace = row->Add<UiButton>(_x("editor", "Replace"), EditorIcon::None, [s, state]() {
				UiSurface* surface = s->GetSurface();
				EventFindOptions find = state->GetEventFind();
				std::int32_t found = CountFoundEvents(*s, find);
				if (found == 0 || surface == nullptr) {
					s->ShowMessage(_x("editor", "No event was found"));
					return;
				}
				if (!s->IsLayerEditable(s->GetDocument().GetSpriteLayerIndex())) {
					s->ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
					return;
				}
				// They get the parameters set for the new kind, or keep those of the same name
				Formats::EventRecord replaceWith = state->ReplaceValues;
				if (replaceWith.Type != std::uint16_t(state->ReplaceEvent)) {
					replaceWith = EventCatalog::MakeDefault(state->ReplaceEvent);
				}
				bool keepParams = state->KeepParams;
				String text = (keepParams
					// TRANSLATORS: Question before replacing events, {} are the count of found events, the name of the found event and the name of the new event
					? _fnx("editor", "Replace {} event \"{}\" with \"{}\"? It keeps the parameters of the same name, the others get the values set.",
						"Replace {} events \"{}\" with \"{}\"? They keep the parameters of the same name, the others get the values set.", found, found,
						EventPreviews::GetEventName(std::uint16_t(find.Type)), EventPreviews::GetEventName(replaceWith.Type))
					// TRANSLATORS: Question before replacing events, {} are the count of found events, the name of the found event and the name of the new event
					: _fnx("editor", "Replace {} event \"{}\" with \"{}\"? It gets the values of the parameters set.",
						"Replace {} events \"{}\" with \"{}\"? They get the values of the parameters set.", found, found,
						EventPreviews::GetEventName(std::uint16_t(find.Type)), EventPreviews::GetEventName(replaceWith.Type)));
				// TRANSLATORS: Title and buttons of the message box asking before replacing events
				UiMessageDialog::Show(*surface, _x("editor", "Replace Events"), text, { _x("editor", "Replace"), _x("editor", "Cancel") }, [s, find, replaceWith, keepParams](std::int32_t button) {
					if (button != 0) {
						return;
					}
					std::int32_t count = ReplaceEvents(*s, find, replaceWith, keepParams);
					if (count < 0) {
						s->ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
						return;
					}
					s->GetEventSelection().Clear();
					s->ShowMessage(count == 0 ? String(_x("editor", "No event was found")) : _fnx("editor", "{} event replaced", "{} events replaced", count, count));
				});
			});
			// Events of the same kind only get other values of their parameters, those it doesn't keep
			replace->IsEnabledFunc = [state]() {
				return (state->FindEvent != EventType::Empty && state->ReplaceEvent != EventType::Empty && (state->FindEvent != state->ReplaceEvent || !state->KeepParams));
			};
			replace->Tooltip = _x("editor", "The new events keep their difficulties and flags, and the parameters of the same name if chosen; events of the same kind get the values set once matching parameters aren't kept");
		}

		// The level is seen and the brush can be picked while it's open
		UiOverlayOptions options;
		options.Position = Vector2f(1.0e6f, 1.0e6f);
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		// An event still to be chosen for it would take the next click in the list otherwise
		options.OnClosed = [s, state]() {
			if (!state->PendingPick.expired()) {
				s->CancelEventPick();
			}
		};
		ShowSingleDialog(session, "findReplace", Death::move(dialog), Death::move(options));
	}
}

#endif
