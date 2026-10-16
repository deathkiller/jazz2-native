#include "FindReplaceDialog.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Document/Commands.h"
#include "../Events/EventCatalog.h"
#include "../Events/EventPreviews.h"
#include "../Tools/EventTools.h"
#include "../UI/UiDialogs.h"
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
		};

		/** A tile or nothing, as the brush has it */
		class TileSlot : public UiElement
		{
		public:
			TileSlot(EditorSession& session, Function<const TileRef*()>&& tile)
				: _session(session), _tile(std::move(tile))
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
				painter.Tile(*tileMap, _session.GetDocument().ToTileId(*tile), tile->Flags,
					Rectf(Bounds.X + inset, Bounds.Y + inset, Bounds.W - inset * 2.0f, Bounds.H - inset * 2.0f));
			}

		private:
			EditorSession& _session;
			Function<const TileRef*()> _tile;
		};

		const TileRef* GetBrushTile(EditorSession& session)
		{
			const TileStamp& stamp = session.GetStamp();
			return (stamp.IsEmpty() ? nullptr : &stamp.Tiles[0]);
		}

		// The event of the brush, or the first selected one
		EventType GetSelectedEventType(EditorSession& session)
		{
			EventSelection& selection = session.GetEventSelection();
			LevelDocument& document = session.GetDocument();
			Vector2i gridSize = document.GetEventGridSize();
			if (!selection.IsEmpty() && gridSize.X > 0) {
				std::uint32_t cell = selection.GetCells()[0];
				return EventType(document.GetEvent(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X))).Type);
			}
			if (selection.HasOffGrid() && selection.GetOffGrid() < std::int32_t(document.OffGridEvents.size())) {
				return EventType(document.OffGridEvents[selection.GetOffGrid()].Event.Type);
			}
			const EventStamp& brush = session.GetEventBrush();
			return (brush.IsEmpty() ? EventType::Empty : EventType(brush.Items[0].Event.Type));
		}
	}

	std::int32_t ReplaceTiles(EditorSession& session, std::int32_t layer, const TileRef& find, const TileRef& replace, bool anyFlip)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t replaced = 0;
		CommandHistory& history = session.GetHistory();
		history.BeginTransaction(_("Replace Tiles"));
		for (std::int32_t l = 0; l < std::int32_t(document.Layers.size()); l++) {
			if ((layer >= 0 && l != layer) || !session.IsLayerEditable(l)) {
				continue;
			}
			const DocumentLayer& source = document.Layers[l];
			auto command = std::make_unique<SetTilesCommand>(document, l, _("Replace Tiles"));
			std::int32_t count = 0;
			for (std::int32_t y = 0; y < source.GetHeight(); y++) {
				for (std::int32_t x = 0; x < source.GetWidth(); x++) {
					const TileRef& tile = source.GetTile(x, y);
					if (!Matches(tile, find, anyFlip)) {
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
				session.Execute(std::move(command));
				replaced += count;
			}
		}
		history.EndTransaction();
		return replaced;
	}

	std::int32_t ReplaceEvents(EditorSession& session, EventType find, EventType replace)
	{
		LevelDocument& document = session.GetDocument();
		Vector2i gridSize = document.GetEventGridSize();
		auto replaceEvent = [replace](const Formats::EventRecord& event) {
			Formats::EventRecord result = EventCatalog::MakeDefault(replace);
			result.Flags = event.Flags;
			result.GeneratorFlags = event.GeneratorFlags;
			result.GeneratorDelay = event.GeneratorDelay;
			return result;
		};

		std::int32_t replaced = 0;
		auto command = std::make_unique<SetEventsCommand>(document, _("Replace Events"));
		for (const DocumentEvent& event : document.Events) {
			if (EventType(event.Record.Type) == find) {
				command->Set(std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X)), replaceEvent(event.Record));
				replaced++;
			}
		}

		SmallVector<Formats::OffGridEventRecord, 0> offGrid;
		bool offGridChanged = false;
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			Formats::OffGridEventRecord& copy = offGrid.emplace_back(event);
			if (EventType(event.Event.Type) == find) {
				copy.Event = replaceEvent(event.Event);
				offGridChanged = true;
				replaced++;
			}
		}

		CommandHistory& history = session.GetHistory();
		history.BeginTransaction(_("Replace Events"));
		session.Execute(std::move(command));
		if (offGridChanged) {
			session.Execute(std::make_unique<SetOffGridEventsCommand>(document, std::move(offGrid), _("Replace Events")));
		}
		history.EndTransaction();
		return replaced;
	}

	void ShowFindReplaceDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto state = std::make_shared<FindState>();
		if (const TileRef* tile = GetBrushTile(session)) {
			state->FindTile = *tile;
			state->HasFindTile = true;
		}
		state->FindEvent = GetSelectedEventType(session);

		auto dialog = std::make_unique<UiDialog>(_("Find and Replace"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();

		// Tiles
		content->Add<UiLabel>(_("Tiles"), UiTheme::TextHeader);
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<UiLabel>(_("Find"))->Width = 95.0f;
			row->Add<TileSlot>(session, Function<const TileRef*()>([state]() -> const TileRef* { return (state->HasFindTile ? &state->FindTile : nullptr); }));
			row->Add<UiButton>(_("From Brush"), EditorIcon::Brush, [s, state]() {
				if (const TileRef* tile = GetBrushTile(*s)) {
					state->FindTile = *tile;
					state->HasFindTile = true;
				}
			})->Tooltip = _("The first tile of the brush, pick it in the tiles or in the level with the eyedropper");
		}
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<UiLabel>(_("Replace with"))->Width = 95.0f;
			row->Add<TileSlot>(session, Function<const TileRef*()>([state]() -> const TileRef* { return (state->HasReplaceTile ? &state->ReplaceTile : nullptr); }));
			row->Add<UiButton>(_("From Brush"), EditorIcon::Brush, [s, state]() {
				if (const TileRef* tile = GetBrushTile(*s)) {
					state->ReplaceTile = *tile;
					state->HasReplaceTile = true;
				}
			});
		}
		content->Add<UiCheckBox>(_("Flipped any way"), [state]() { return state->AnyFlip; }, [state](bool value) { state->AnyFlip = value; })
			->Tooltip = _("Otherwise only tiles flipped the same way are found");
		content->Add<UiCheckBox>(_("In all layers"), [state]() { return state->AllLayers; }, [state](bool value) { state->AllLayers = value; })
			->Tooltip = _("Otherwise only in the layer being edited, selecting is always in it");
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			auto* select = row->Add<UiButton>(_("Select"), EditorIcon::Select, [s, state]() {
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
				s->ShowMessage(_fn("{} tile selected", "{} tiles selected", count, count));
			});
			select->IsEnabledFunc = [state]() { return state->HasFindTile; };
			select->Tooltip = _("Select the tiles found in the layer being edited");
			auto* replace = row->Add<UiButton>(_("Replace"), EditorIcon::None, [s, state]() {
				std::int32_t count = ReplaceTiles(*s, state->AllLayers ? -1 : s->GetCurrentLayer(), state->FindTile, state->ReplaceTile, state->AnyFlip);
				s->ShowMessage(count == 0 ? String(_("No tile was found")) : _fn("{} tile replaced", "{} tiles replaced", count, count));
			});
			replace->IsEnabledFunc = [state]() { return state->HasFindTile && state->HasReplaceTile; };
		}

		content->Add<UiSeparator>();

		// Events
		content->Add<UiLabel>(_("Events"), UiTheme::TextHeader);
		auto addEventRow = [s, content](StringView label, EventType FindState::*member, std::shared_ptr<FindState> state) {
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<UiLabel>(label)->Width = 95.0f;
			auto* name = row->Add<UiLabel>(StringView());
			name->Flex = 1.0f;
			name->TextFunc = [state, member]() {
				EventType type = (*state).*member;
				return (type == EventType::Empty ? String(_("Nothing")) : EventPreviews::GetEventName(std::uint16_t(type)));
			};
			row->Add<UiButton>(_("Choose..."), EditorIcon::Event, [s, state, member]() {
				s->PickEventFromList([state, member](EventType type) { (*state).*member = type; });
			});
			row->Add<UiButton>(_("Selected"), EditorIcon::None, [s, state, member]() {
				(*state).*member = GetSelectedEventType(*s);
			})->Tooltip = _("The first selected event, or the event to place");
		};
		addEventRow(_("Find"), &FindState::FindEvent, state);
		addEventRow(_("Replace with"), &FindState::ReplaceEvent, state);
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			auto* select = row->Add<UiButton>(_("Select"), EditorIcon::Select, [s, state]() {
				LevelDocument& document = s->GetDocument();
				SmallVector<std::uint32_t, 0> cells;
				for (const DocumentEvent& event : document.Events) {
					if (EventType(event.Record.Type) == state->FindEvent) {
						cells.push_back(event.Cell);
					}
				}
				std::int32_t count = std::int32_t(cells.size());
				if (!s->IsEditingEvents()) {
					s->GetActions().Execute("tool.event"_s);
				}
				s->GetEventSelection().Assign(std::move(cells));
				s->ShowMessage(_fn("{} event selected", "{} events selected", count, count));
			});
			select->IsEnabledFunc = [state]() { return state->FindEvent != EventType::Empty; };
			auto* replace = row->Add<UiButton>(_("Replace"), EditorIcon::None, [s, state]() {
				std::int32_t count = ReplaceEvents(*s, state->FindEvent, state->ReplaceEvent);
				s->GetEventSelection().Clear();
				s->ShowMessage(count == 0 ? String(_("No event was found")) : _fn("{} event replaced", "{} events replaced", count, count));
			});
			replace->IsEnabledFunc = [state]() { return state->FindEvent != EventType::Empty && state->ReplaceEvent != EventType::Empty && state->FindEvent != state->ReplaceEvent; };
			replace->Tooltip = _("The new events get the default parameters of their kind, and keep their difficulties and flags");
		}

		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		// The level is seen and the brush can be picked while it's open
		UiOverlayOptions options;
		options.Position = Vector2f(1.0e6f, 1.0e6f);
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
