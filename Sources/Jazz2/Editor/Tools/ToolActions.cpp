#include "ToolActions.h"

#if defined(WITH_EDITOR)

#include "EditorContext.h"
#include "EventTools.h"
#include "MeasureTool.h"
#include "ShapeTool.h"
#include "TileTools.h"
#include "../Common/ToolMath.h"
#include "../Document/Commands.h"
#include "../Events/EventCatalog.h"
#include "../Files/EditorSettings.h"
#include "../Input/ActionRegistry.h"
#include "../Runtime/TileDrawing.h"
#include "../Smart/TilePrefabs.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"
#include "../../../nCine/Input/IInputManager.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <memory>

#if !defined(NC_)
#	define NC_(context, text) text
#endif

using namespace Death::Containers::Literals;

namespace Jazz2::Editor::ToolActions
{
	namespace
	{
		// Tiles of several layers copied last in this editor, pasted when the clipboard of the system has none
		LayeredStamp& LayeredClipboard()
		{
			static LayeredStamp clipboard;
			return clipboard;
		}

		void LoadSettings(IEditorContext& session)
		{
			const EditorSettings::ToolOptions& options = session.GetSettings().Tools;
			ToolSettings& settings = session.GetTools().GetSettings();
			settings.AcrossSeams = options.AcrossSeams;
			settings.IgnoreFlips = options.IgnoreFlips;
			settings.OnlyEmpty = options.OnlyEmpty;
			settings.WandGlobal = options.WandGlobal;
			settings.Shape = ToolShape(std::clamp<std::int32_t>(options.Shape, 0, std::int32_t(ToolShape::FilledEllipse)));
			settings.EventMode = EventPlaceMode(std::clamp<std::int32_t>(options.EventMode, 0, std::int32_t(EventPlaceMode::Select)));
			settings.ShownArcs = std::uint8_t(options.MeasureArcs & ToolSettings::ArcAll);
			settings.ArcCollisions = options.MeasureCollisions;
			settings.MirrorSwapTeams = options.MirrorSwapTeams;
		}

		void SaveSettings(IEditorContext& session)
		{
			EditorSettings::ToolOptions& options = session.GetSettings().Tools;
			const ToolSettings& settings = session.GetTools().GetSettings();
			options.AcrossSeams = settings.AcrossSeams;
			options.IgnoreFlips = settings.IgnoreFlips;
			options.OnlyEmpty = settings.OnlyEmpty;
			options.WandGlobal = settings.WandGlobal;
			options.Shape = std::int32_t(settings.Shape);
			options.EventMode = std::int32_t(settings.EventMode);
			options.MeasureArcs = std::int32_t(settings.ShownArcs);
			options.MeasureCollisions = settings.ArcCollisions;
			options.MirrorSwapTeams = settings.MirrorSwapTeams;
			session.GetSettings().Save();
		}

		Vector2i CellOf(std::uint32_t cell, Vector2i gridSize)
		{
			return Vector2i(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
		}

		bool IsInGrid(Vector2i cell, Vector2i gridSize)
		{
			return (cell.X >= 0 && cell.Y >= 0 && cell.X < gridSize.X && cell.Y < gridSize.Y);
		}

		// The parameters of an event as mirroring it changes them, and its team swapped if chosen
		void MirrorEvent(Formats::EventRecord& event, bool horizontal, bool swapTeams)
		{
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
			EventCatalog::MirrorParams(descriptor, event, horizontal);
			if (swapTeams) {
				EventCatalog::SwapTeamParams(descriptor, event.Params);
			}
		}

		// Selects the events of the level that aren't selected, on the grid and off it, and only them
		void InvertEventSelection(IEditorContext& session)
		{
			const LevelDocument& document = session.GetDocument();
			EventSelection& selection = session.GetEventSelection();
			SmallVector<std::uint32_t, 0> cells;
			for (const DocumentEvent& event : document.Events) {
				if (!selection.Contains(event.Cell)) {
					cells.push_back(event.Cell);
				}
			}
			SmallVector<std::int32_t, 0> offGrid;
			for (std::int32_t i = 0; i < std::int32_t(document.OffGridEvents.size()); i++) {
				if (!selection.ContainsOffGrid(i)) {
					offGrid.push_back(i);
				}
			}
			selection.Assign(Death::move(cells));
			selection.AssignOffGrid(Death::move(offGrid));
		}

		// The selected tiles of the layer being edited, `false` with a message if there are none
		bool GetTileSelection(IEditorContext& session, std::int32_t& layerIndex, bool editable)
		{
			TileSelection& selection = session.GetSelection();
			LevelDocument& document = session.GetDocument();
			layerIndex = session.GetCurrentLayer();
			if (selection.IsEmpty() || selection.GetLayer() != layerIndex || layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
				session.ShowMessage(_x("editor", "Select tiles of the layer first"));
				return false;
			}
			const DocumentLayer& layer = document.Layers[layerIndex];
			if (selection.GetLayerSize() != Vector2i(layer.GetWidth(), layer.GetHeight())) {
				selection.Clear();
				return false;
			}
			if (editable && !session.IsLayerEditable(layerIndex)) {
				session.ShowMessage(_x("editor", "The layer is locked or hidden"));
				return false;
			}
			return true;
		}

		/** The tiles of the brush that paints at random, with how often each of them is chosen */
		class WeightTile : public UiElement
		{
		public:
			WeightTile(IEditorContext& session, const TileRef& tile)
				: _session(session), _tile(tile)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(context.Px(24.0f), context.Px(24.0f));
			}

			void Draw(UiPainter& painter) override
			{
				painter.FillRect(Bounds, Colorf(0.0f, 0.0f, 0.0f, 0.35f), UiLayer::Fill);
				Tiles::TileMap* tileMap = _session.GetTileMap();
				if (tileMap != nullptr && !_tile.IsEmpty()) {
					DrawTile(painter, *tileMap, _session.GetDocument(), _tile, _tile.Flags, Bounds, 1.0f, UiLayer::Content);
				}
			}

		private:
			IEditorContext& _session;
			TileRef _tile;
		};
	}

	const LayeredStamp& GetLayeredClipboard()
	{
		return LayeredClipboard();
	}

	void SetLayeredClipboard(const LayeredStamp& stamp)
	{
		LayeredClipboard() = stamp;
	}

	std::int32_t GetCurrentLayerFromSprite(IEditorContext& session)
	{
		return session.GetCurrentLayer() - std::max<std::int32_t>(session.GetDocument().GetSpriteLayerIndex(), 0);
	}

	bool CopyAllLayers(IEditorContext& session)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		std::int32_t base = std::max<std::int32_t>(spriteLayer, 0);
		Vector2i gridSize = document.GetEventGridSize();

		// The rectangle of the selected tiles, or of the selected events
		TileSelection& selection = session.GetSelection();
		EventSelection& eventSelection = session.GetEventSelection();
		Recti bounds;
		const TileSelection* mask = nullptr;
		if (!selection.IsEmpty() && selection.GetLayer() >= 0 && selection.GetLayer() < std::int32_t(document.Layers.size())) {
			bounds = selection.GetBounds();
			mask = &selection;
		} else if (eventSelection.HasCells() && gridSize.X > 0) {
			Vector2i from(INT32_MAX, INT32_MAX), to(INT32_MIN, INT32_MIN);
			for (std::uint32_t cell : eventSelection.GetCells()) {
				Vector2i pos = CellOf(cell, gridSize);
				from = Vector2i(std::min(from.X, pos.X), std::min(from.Y, pos.Y));
				to = Vector2i(std::max(to.X, pos.X), std::max(to.Y, pos.Y));
			}
			bounds = Recti(from.X, from.Y, to.X - from.X + 1, to.Y - from.Y + 1);
		} else {
			session.ShowMessage(_x("editor", "Select the tiles to copy first"));
			return false;
		}

		LayeredStamp stamp;
		stamp.Width = bounds.W;
		stamp.Height = bounds.H;
		stamp.Origin = Vector2i(bounds.X, bounds.Y);
		for (std::int32_t i = 0; i < std::int32_t(document.Layers.size()); i++) {
			// Only what's seen and can be changed, a hidden layer isn't what the user means to copy
			if (!session.IsLayerEditable(i)) {
				continue;
			}
			TileStamp tiles = TileStamp::FromLayer(document.Layers[i], bounds);
			bool any = false;
			for (std::int32_t y = 0; y < bounds.H; y++) {
				for (std::int32_t x = 0; x < bounds.W; x++) {
					std::size_t index = std::size_t(y) * std::size_t(bounds.W) + std::size_t(x);
					if (mask != nullptr && !mask->Contains(bounds.X + x, bounds.Y + y)) {
						tiles.Present[index] = false;
					}
					any |= (tiles.Present[index] && !tiles.Tiles[index].IsEmpty());
				}
			}
			// A layer with nothing there would only erase where it's pasted
			if (any) {
				LayeredStamp::Layer& layer = stamp.Layers.emplace_back();
				layer.FromSpriteLayer = i - base;
				layer.Tiles = Death::move(tiles);
			}
		}
		// The events are of the sprite layer, copied as its tiles are, only if it's seen and can be changed
		if (spriteLayer >= 0 && session.IsLayerEditable(spriteLayer)) {
			for (const DocumentEvent& event : document.Events) {
				Vector2i pos = CellOf(event.Cell, gridSize);
				if (pos.X < bounds.X || pos.Y < bounds.Y || pos.X >= bounds.X + bounds.W || pos.Y >= bounds.Y + bounds.H ||
					(mask != nullptr && !mask->Contains(pos.X, pos.Y))) {
					continue;
				}
				EventStamp::Item& item = stamp.Events.Items.emplace_back();
				item.Offset = pos - stamp.Origin;
				item.Event = event.Record;
			}
			// Those off the grid on the copied tiles too, by pixels from the corner
			for (const Formats::OffGridEventRecord& record : document.OffGridEvents) {
				Vector2i pos(std::int32_t(record.X / 32), std::int32_t(record.Y / 32));
				if (pos.X < bounds.X || pos.Y < bounds.Y || pos.X >= bounds.X + bounds.W || pos.Y >= bounds.Y + bounds.H ||
					(mask != nullptr && !mask->Contains(pos.X, pos.Y))) {
					continue;
				}
				EventStamp::OffGridItem& item = stamp.Events.OffGrid.emplace_back();
				item.Offset = Vector2i(std::int32_t(record.X) - stamp.Origin.X * 32, std::int32_t(record.Y) - stamp.Origin.Y * 32);
				item.Event = record.Event;
			}
		}
		if (stamp.IsEmpty()) {
			session.ShowMessage(_x("editor", "There's nothing to copy there"));
			return false;
		}
		theApplication().GetInputManager().setClipboardText(StampClipboard::WriteLayered(stamp, &document));
		LayeredClipboard() = Death::move(stamp);
		session.ShowMessage(_x("editor", "Copied from all layers, with the events"));
		return true;
	}

	bool PasteAllLayers(IEditorContext& session)
	{
		LayeredStamp stamp;
		String text = theApplication().GetInputManager().getClipboardText();
		// Copied from another level, its tiles are found in the tile sets of this one
		if (!StampClipboard::ReadLayered(text, stamp, &session.GetDocument())) {
			stamp = LayeredClipboard();
		}
		if (stamp.IsEmpty()) {
			session.ShowMessage(_x("editor", "Copy tiles of all layers first"));
			return false;
		}
		// Where the pointer is, otherwise where they were copied from
		Vector2i topLeft = stamp.Origin;
		ToolPointer hover;
		if (session.GetHoverPointer(hover)) {
			LayerView view = session.GetLayerView(session.GetCurrentLayer());
			Vector2i tile;
			if (view.Wrap(hover.Cell, tile)) {
				topLeft = tile;
			}
		}
		return PlaceLayered(session, stamp, topLeft);
	}

	bool PlaceLayered(IEditorContext& session, const LayeredStamp& stamp, Vector2i topLeft)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		std::int32_t base = std::max<std::int32_t>(spriteLayer, 0);
		Vector2i gridSize = document.GetEventGridSize();
		// TRANSLATORS: Name of an undo step in the history, after tiles and events copied from all layers were pasted
		String name = _x("editor", "Paste All Layers");
		std::int32_t skipped = 0;
		bool changed = false;

		session.GetHistory().BeginTransaction(name);
		for (const LayeredStamp::Layer& part : stamp.Layers) {
			std::int32_t layerIndex = base + part.FromSpriteLayer;
			if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size()) || !session.IsLayerEditable(layerIndex)) {
				skipped++;
				continue;
			}
			TileStamp tiles = part.Tiles;
			StampClipboard::RemoveUnknownTiles(document, tiles);
			// Around a layer that repeats it goes on from the other side, as the brush paints
			const DocumentLayer& layer = document.Layers[layerIndex];
			std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
			bool repeatX = (!layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatX) != 0 && width > 0);
			bool repeatY = (!layer.IsSpriteLayer() && (layer.Record.Flags & Formats::LayerRecord::RepeatY) != 0 && height > 0);
			auto command = std::make_unique<SetTilesCommand>(document, layerIndex, name);
			for (std::int32_t y = 0; y < tiles.Height; y++) {
				for (std::int32_t x = 0; x < tiles.Width; x++) {
					if (tiles.IsPresent(x, y)) {
						std::int32_t tx = topLeft.X + x, ty = topLeft.Y + y;
						command->Set(repeatX ? ToolMath::PositiveModulo(tx, width) : tx, repeatY ? ToolMath::PositiveModulo(ty, height) : ty, tiles.GetTile(x, y));
					}
				}
			}
			command->Seal();
			if (!command->IsEmpty()) {
				session.Execute(Death::move(command));
				changed = true;
			}
		}
		if (!stamp.Events.IsEmpty()) {
			if (spriteLayer < 0 || !session.IsLayerEditable(spriteLayer)) {
				skipped++;
			} else {
				// A whole warp or trigger pasted works on its own, with identifiers the level doesn't use yet
				EventStamp events = stamp.Events;
				RemapLinkedIds(events, document);
				auto command = std::make_unique<SetEventsCommand>(document, name);
				for (const EventStamp::Item& item : events.Items) {
					Vector2i target = topLeft + item.Offset;
					if (IsInGrid(target, gridSize)) {
						Formats::EventRecord event = item.Event;
						LevelDocument::NormalizeEvent(event);
						command->Set(target.X, target.Y, event);
					}
				}
				command->Seal();
				if (!command->IsEmpty()) {
					session.Execute(Death::move(command));
					changed = true;
				}
				if (!events.OffGrid.empty()) {
					SmallVector<Formats::OffGridEventRecord, 0> offGrid = CopyOffGridEvents(document);
					std::size_t before = offGrid.size();
					for (const EventStamp::OffGridItem& item : events.OffGrid) {
						std::int64_t x = std::int64_t(topLeft.X) * 32 + item.Offset.X, y = std::int64_t(topLeft.Y) * 32 + item.Offset.Y;
						if (x >= 0 && y >= 0 && x < std::int64_t(gridSize.X) * 32 && y < std::int64_t(gridSize.Y) * 32) {
							Formats::OffGridEventRecord& record = offGrid.emplace_back();
							record.X = std::uint32_t(x);
							record.Y = std::uint32_t(y);
							record.Event = item.Event;
							LevelDocument::NormalizeEvent(record.Event);
						}
					}
					if (offGrid.size() != before) {
						session.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(offGrid), name));
						changed = true;
					}
				}
			}
		}
		session.GetHistory().EndTransaction();

		// What was pasted stays selected, so it can be moved right away
		std::int32_t current = session.GetCurrentLayer();
		if (changed && current >= 0 && current < std::int32_t(document.Layers.size())) {
			const DocumentLayer& layer = document.Layers[current];
			session.GetSelection().Select(current, Vector2i(layer.GetWidth(), layer.GetHeight()), Recti(topLeft.X, topLeft.Y, stamp.Width, stamp.Height), false);
		}
		if (skipped > 0) {
			session.ShowMessage(_x("editor", "Pasted, but not into the layers that are locked, hidden or missing"));
		} else if (changed) {
			session.ShowMessage(_x("editor", "Pasted into all layers"));
		}
		return changed;
	}

	bool UsePrefab(IEditorContext& session, const TilePrefab& prefab)
	{
		const LevelDocument& document = session.GetDocument();
		TileStamp stamp = prefab.ToStamp(document);
		// As pasted, tiles the level doesn't have aren't painted
		StampClipboard::RemoveUnknownTiles(document, stamp);
		bool hasTiles = (!stamp.IsEmpty() && std::find(stamp.Present.begin(), stamp.Present.end(), true) != stamp.Present.end());
		if (prefab.HasLayers()) {
			LayeredStamp layered = prefab.ToLayered(document, GetCurrentLayerFromSprite(session));
			LayeredClipboard() = Death::move(layered);
			EditorAction* paste = session.GetActions().Find(ActionIds::EditPasteLayers);
			String key = (paste != nullptr ? paste->Key.ToString() : String());
			if (key.empty()) {
				session.ShowMessage(_x("editor", "Paste All Layers places the prefab with its layers and events where the pointer is"));
			} else {
				// TRANSLATORS: Status message after a prefab was chosen, {} is the keyboard shortcut of Paste All Layers, such as Ctrl+Shift+V
				session.ShowMessage(_fx("editor", "{} places the prefab with its layers and events where the pointer is", key));
			}
		}
		if (hasTiles) {
			session.SetStamp(Death::move(stamp));
			session.GetTools().SetActive(EditorToolType::Brush);
			return true;
		}
		return prefab.HasLayers();
	}

	bool MirrorSelection(IEditorContext& session, bool horizontal)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		Vector2i gridSize = document.GetEventGridSize();
		bool swapTeams = session.GetTools().GetSettings().MirrorSwapTeams;
		// TRANSLATORS: Names of undo steps in the history, after the selected tiles or events were mirrored
		String name = (horizontal ? _x("editor", "Mirror Horizontally") : _x("editor", "Mirror Vertically"));

		if (session.IsEditingEvents()) {
			EventSelection& selection = session.GetEventSelection();
			if (!session.IsLayerEditable(spriteLayer)) {
				session.ShowMessage(_x("editor", "The sprite layer is locked or hidden"));
				return false;
			}
			if (selection.IsEmpty() || gridSize.X <= 0) {
				session.ShowMessage(_x("editor", "Select events first"));
				return false;
			}
			// Within the tiles of the selected events of the grid, or among the positions of those off it alone, a single
			// event off the grid turns where it is
			Vector2i from(INT32_MAX, INT32_MAX), to(INT32_MIN, INT32_MIN);
			for (std::uint32_t cell : selection.GetCells()) {
				Vector2i pos = CellOf(cell, gridSize);
				from = Vector2i(std::min(from.X, pos.X), std::min(from.Y, pos.Y));
				to = Vector2i(std::max(to.X, pos.X), std::max(to.Y, pos.Y));
			}
			std::int64_t pixelSumX, pixelSumY;
			if (selection.HasCells()) {
				pixelSumX = std::int64_t(from.X + to.X + 1) * 32;
				pixelSumY = std::int64_t(from.Y + to.Y + 1) * 32;
			} else {
				std::int64_t minX = INT64_MAX, minY = INT64_MAX, maxX = INT64_MIN, maxY = INT64_MIN;
				for (std::int32_t index : selection.GetOffGrid()) {
					if (index < std::int32_t(document.OffGridEvents.size())) {
						const Formats::OffGridEventRecord& record = document.OffGridEvents[index];
						minX = std::min<std::int64_t>(minX, record.X);
						maxX = std::max<std::int64_t>(maxX, record.X);
						minY = std::min<std::int64_t>(minY, record.Y);
						maxY = std::max<std::int64_t>(maxY, record.Y);
					}
				}
				pixelSumX = minX + maxX;
				pixelSumY = minY + maxY;
			}
			session.GetHistory().BeginTransaction(name);
			if (selection.HasOffGrid()) {
				SmallVector<Formats::OffGridEventRecord, 0> events = CopyOffGridEvents(document);
				for (std::int32_t index : selection.GetOffGrid()) {
					if (index >= std::int32_t(events.size())) {
						continue;
					}
					Formats::OffGridEventRecord& record = events[index];
					if (horizontal) {
						record.X = std::uint32_t(std::clamp<std::int64_t>(pixelSumX - std::int64_t(record.X), 0, std::int64_t(gridSize.X) * 32 - 1));
					} else {
						record.Y = std::uint32_t(std::clamp<std::int64_t>(pixelSumY - std::int64_t(record.Y), 0, std::int64_t(gridSize.Y) * 32 - 1));
					}
					MirrorEvent(record.Event, horizontal, swapTeams);
				}
				session.Execute(std::make_unique<SetOffGridEventsCommand>(document, Death::move(events), name));
			}
			if (!selection.HasCells()) {
				session.GetHistory().EndTransaction();
				return true;
			}
			struct Moved
			{
				Vector2i From;
				Vector2i To;
				Formats::EventRecord Event;
			};
			SmallVector<Moved, 0> moved;
			for (std::uint32_t cell : selection.GetCells()) {
				Vector2i pos = CellOf(cell, gridSize);
				Formats::EventRecord event = document.GetEvent(pos.X, pos.Y);
				if (LevelDocument::IsEmptyEvent(event)) {
					continue;
				}
				MirrorEvent(event, horizontal, swapTeams);
				Vector2i target(horizontal ? from.X + to.X - pos.X : pos.X, horizontal ? pos.Y : from.Y + to.Y - pos.Y);
				moved.push_back(Moved{ pos, target, event });
			}
			auto command = std::make_unique<SetEventsCommand>(document, name);
			for (const Moved& item : moved) {
				command->Set(item.From.X, item.From.Y, LevelDocument::GetEmptyEvent());
			}
			SmallVector<std::uint32_t, 0> cells;
			for (const Moved& item : moved) {
				command->Set(item.To.X, item.To.Y, item.Event);
				cells.push_back(std::uint32_t(item.To.X + item.To.Y * gridSize.X));
			}
			session.Execute(Death::move(command));
			session.GetHistory().EndTransaction();
			// Those off the grid keep their indices
			SmallVector<std::int32_t, 0> offGrid;
			for (std::int32_t index : selection.GetOffGrid()) {
				offGrid.push_back(index);
			}
			selection.Assign(Death::move(cells));
			selection.AssignOffGrid(Death::move(offGrid));
			return true;
		}

		std::int32_t layerIndex;
		if (!GetTileSelection(session, layerIndex, true)) {
			return false;
		}
		TileSelection& selection = session.GetSelection();
		const DocumentLayer& layer = document.Layers[layerIndex];
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		Recti bounds = selection.GetBounds();
		auto mirror = [&](std::int32_t x, std::int32_t y) {
			return Vector2i(horizontal ? 2 * bounds.X + bounds.W - 1 - x : x, horizontal ? y : 2 * bounds.Y + bounds.H - 1 - y);
		};
		std::uint8_t flip = (horizontal ? Formats::TileRecord::FlipX : Formats::TileRecord::FlipY);

		BitArray target(ValueInit, std::size_t(width) * std::size_t(height));
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (selection.Contains(x, y)) {
					Vector2i to = mirror(x, y);
					target.set(std::size_t(to.Y) * std::size_t(width) + std::size_t(to.X));
				}
			}
		}

		session.GetHistory().BeginTransaction(name);
		auto tiles = std::make_unique<SetTilesCommand>(document, layerIndex, name);
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (selection.Contains(x, y) && !target[std::size_t(y) * std::size_t(width) + std::size_t(x)]) {
					tiles->Set(x, y, TileRef{});
				}
			}
		}
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (!selection.Contains(x, y)) {
					continue;
				}
				TileRef tile = layer.GetTile(x, y);
				if (!tile.IsEmpty()) {
					tile.Flags ^= flip;
				}
				Vector2i to = mirror(x, y);
				tiles->Set(to.X, to.Y, tile);
			}
		}
		tiles->Seal();
		if (!tiles->IsEmpty()) {
			session.Execute(Death::move(tiles));
		}

		// On the sprite layer, the events on the selected tiles turn with them, those off the grid within the rectangle of
		// the selection in pixels
		if (layerIndex == spriteLayer) {
			CarrySelectedEvents(session, selection, name, [&mirror](Vector2i cell) { return mirror(cell.X, cell.Y); },
				[horizontal, bounds](Vector2i pixel) {
					return Vector2i(horizontal ? (2 * bounds.X + bounds.W) * 32 - pixel.X : pixel.X, horizontal ? pixel.Y : (2 * bounds.Y + bounds.H) * 32 - pixel.Y);
				}, [horizontal, swapTeams](Formats::EventRecord& event) { MirrorEvent(event, horizontal, swapTeams); });
		}
		session.GetHistory().EndTransaction();
		selection.Select(layerIndex, Vector2i(width, height), target, false);
		return true;
	}

	bool SetSelectionModifier(IEditorContext& session, TileModifierMode modifier)
	{
		std::int32_t layerIndex;
		if (!GetTileSelection(session, layerIndex, true)) {
			return false;
		}
		LevelDocument& document = session.GetDocument();
		TileSelection& selection = session.GetSelection();
		const DocumentLayer& layer = document.Layers[layerIndex];
		std::uint8_t value = (modifier == TileModifierMode::Translucent ? Formats::TileRecord::ModifierTranslucent :
			(modifier == TileModifierMode::Invisible ? Formats::TileRecord::ModifierInvisible :
			(modifier == TileModifierMode::Heat ? Formats::TileRecord::ModifierHeat : 0)));
		const Recti& bounds = selection.GetBounds();
		// TRANSLATORS: Name of an undo step in the history, after the selected tiles were made normal, translucent, invisible or given heat
		auto command = std::make_unique<SetTilesCommand>(document, layerIndex, _x("editor", "Set Tile Modifier"));
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				const TileRef& tile = layer.GetTile(x, y);
				if (!selection.Contains(x, y) || tile.IsEmpty()) {
					continue;
				}
				TileRef changed = tile;
				changed.Flags = std::uint8_t((tile.Flags & ~Formats::TileRecord::ModifierMask) | value);
				command->Set(x, y, changed);
			}
		}
		command->Seal();
		if (command->IsEmpty()) {
			session.ShowMessage(_x("editor", "The selected tiles have it already"));
			return false;
		}
		session.Execute(Death::move(command));
		return true;
	}

	bool FillSelectionWithEvent(IEditorContext& session)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		TileSelection& selection = session.GetSelection();
		if (spriteLayer < 0 || selection.IsEmpty() || selection.GetLayer() != spriteLayer) {
			session.ShowMessage(_x("editor", "Select tiles of the sprite layer first"));
			return false;
		}
		if (!session.GetEventBrush().IsSingle()) {
			session.ShowMessage(_x("editor", "Choose the event to place first, in the list of events"));
			return false;
		}
		SmallVector<Vector2i, 0> cells;
		const Recti& bounds = selection.GetBounds();
		for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
			for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
				if (selection.Contains(x, y)) {
					cells.push_back(Vector2i(x, y));
				}
			}
		}
		auto* tool = static_cast<EventTool*>(session.GetTools().Get(EditorToolType::Event));
		std::int32_t placed = tool->PlaceOnCells(cells, false);
		session.ShowMessage(_fnx("editor", "{} event placed, tiles with an event keep it", "{} events placed, tiles with an event keep it", placed, placed));
		return (placed > 0);
	}

	bool SelectSameTiles(IEditorContext& session)
	{
		LevelDocument& document = session.GetDocument();
		std::int32_t layerIndex = session.GetCurrentLayer();
		if (layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
			return false;
		}
		const DocumentLayer& layer = document.Layers[layerIndex];
		Vector2i layerSize(layer.GetWidth(), layer.GetHeight());
		bool ignoreFlips = session.GetTools().GetSettings().IgnoreFlips;
		auto key = [ignoreFlips](const TileRef& tile) {
			constexpr std::uint8_t Flips = Formats::TileRecord::FlipX | Formats::TileRecord::FlipY;
			std::uint8_t flags = (ignoreFlips ? std::uint8_t(tile.Flags & ~Flips) : tile.Flags);
			return std::uint32_t(tile.Index) | (std::uint32_t(tile.Source) << 16) | (std::uint32_t(flags) << 24);
		};

		// The tiles of the selection, or the one under the pointer
		SmallVector<std::uint32_t, 0> wanted;
		TileSelection& selection = session.GetSelection();
		if (!selection.IsEmpty() && selection.GetLayer() == layerIndex && selection.GetLayerSize() == layerSize) {
			const Recti& bounds = selection.GetBounds();
			for (std::int32_t y = bounds.Y; y < bounds.Y + bounds.H; y++) {
				for (std::int32_t x = bounds.X; x < bounds.X + bounds.W; x++) {
					if (selection.Contains(x, y)) {
						wanted.push_back(key(layer.GetTile(x, y)));
					}
				}
			}
		} else {
			ToolPointer hover;
			Vector2i tile;
			if (!session.GetHoverPointer(hover) || !session.GetLayerView(layerIndex).Wrap(hover.Cell, tile)) {
				session.ShowMessage(_x("editor", "Select some tiles, or point at one"));
				return false;
			}
			wanted.push_back(key(layer.GetTile(tile.X, tile.Y)));
		}
		std::sort(wanted.begin(), wanted.end());
		wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());

		BitArray mask(ValueInit, std::size_t(layerSize.X) * std::size_t(layerSize.Y));
		for (std::int32_t y = 0; y < layerSize.Y; y++) {
			for (std::int32_t x = 0; x < layerSize.X; x++) {
				if (std::binary_search(wanted.begin(), wanted.end(), key(layer.GetTile(x, y)))) {
					mask.set(std::size_t(y) * std::size_t(layerSize.X) + std::size_t(x));
				}
			}
		}
		selection.Select(layerIndex, layerSize, mask, false);
		return true;
	}

	void ShowStampWeights(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		TileStamp& stamp = session.GetStamp();
		if (surface == nullptr) {
			return;
		}
		if (stamp.IsEmpty() || stamp.Tiles.size() < 2) {
			session.ShowMessage(_x("editor", "Pick a brush of more tiles first, the weights tell how often each of them is painted"));
			return;
		}

		// The tiles of the brush, each once
		struct Entry
		{
			TileRef Tile;
			std::int32_t Count;
		};
		SmallVector<Entry, 0> entries;
		for (std::size_t i = 0; i < stamp.Tiles.size() && i < stamp.Present.size(); i++) {
			if (!stamp.Present[i]) {
				continue;
			}
			auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& entry) { return entry.Tile == stamp.Tiles[i]; });
			if (it != entries.end()) {
				it->Count++;
			} else {
				entries.push_back(Entry{ stamp.Tiles[i], 1 });
			}
		}

		IEditorContext* s = &session;
		// TRANSLATORS: Title of the dialog that sets how often each tile of the brush is painted at random
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Weights of Tiles"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();
		// On as many lines as it needs, the window is only as wide as the list
		constexpr float ListWidth = 200.0f;
		content->Add<UiWrappedLabel>(_x("editor", "How often each tile is painted when the brush paints at random"), UiTheme::TextDim)->Width = ListWidth;
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = ListWidth;
		scroll->Height = std::min<float>(40.0f + 28.0f * float(entries.size()), 240.0f);
		// Shorter on a short screen, the check box below it stays in the window
		scroll->Flex = 1.0f;
		auto* list = scroll->SetContent<UiStack>(UiOrientation::Vertical, 2.0f);
		for (const Entry& entry : entries) {
			TileRef tile = entry.Tile;
			auto* row = list->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<WeightTile>(session, tile);
			// TRANSLATORS: Row in the Weights of Tiles dialog, {} is the number of the tile in the tile set
			row->Add<UiLabel>(_fx("editor", "Tile {}", std::int32_t(tile.Index)))->Flex = 1.0f;
			auto weightOf = [s, tile]() {
				// The weight of a tile is the sum of its places, each 1 unless set
				const TileStamp& current = s->GetStamp();
				float sum = 0.0f;
				for (std::size_t i = 0; i < current.Tiles.size() && i < current.Present.size(); i++) {
					if (current.Present[i] && current.Tiles[i] == tile) {
						sum += (current.Weights.size() == current.Tiles.size() ? current.Weights[i] : 1.0f);
					}
				}
				return sum;
			};
			auto* box = row->Add<UiNumberBox>(weightOf, [s, tile](float value, bool final) {
				TileStamp& current = s->GetStamp();
				if (current.Weights.size() != current.Tiles.size()) {
					current.Weights.clear();
					current.Weights.resize(current.Tiles.size(), 1.0f);
				}
				std::int32_t count = 0;
				for (std::size_t i = 0; i < current.Tiles.size() && i < current.Present.size(); i++) {
					count += (current.Present[i] && current.Tiles[i] == tile ? 1 : 0);
				}
				for (std::size_t i = 0; i < current.Tiles.size() && i < current.Present.size(); i++) {
					if (current.Present[i] && current.Tiles[i] == tile) {
						current.Weights[i] = std::max(value, 0.0f) / float(std::max<std::int32_t>(count, 1));
					}
				}
			}, 0.0f, 100.0f, 1.0f, 1);
			box->Width = 56.0f;
		}
		content->Add<UiCheckBox>(_x("editor", "Paint tiles at random"), [s]() { return s->GetStamp().Random; },
			[s](bool value) { s->GetStamp().Random = value; });
		dialog->AddButton(_("Reset"), [s]() { s->GetStamp().Weights.clear(); });
		// Closed by its cross or Escape, resetting isn't what they do
		dialog->OnCancel = [dialogPtr]() { dialogPtr->Close(); };

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(Death::move(dialog), Death::move(options));
	}

	void Register(IEditorContext& session)
	{
		ActionRegistry& a = session.GetActions();
		ToolManager& tools = session.GetTools();
		ToolSettings& settings = tools.GetSettings();
		IEditorContext* s = &session;
		ToolSettings* t = &settings;

		LoadSettings(session);
		settings.OnChanged = [s]() { SaveSettings(*s); };

		auto toggle = [&a, s, t](const char* id, StringView label, bool ToolSettings::* member) -> EditorAction& {
			auto& action = a.Add(id, label, EditorIcon::None, [s, t, member]() {
				t->*member = !(t->*member);
				t->NotifyChanged();
			});
			action.IsChecked = [t, member]() { return (t->*member); };
			return action;
		};
		auto hasTileSelection = [s]() { return (!s->IsEditingEvents() && !s->GetSelection().IsEmpty() && s->GetSelection().GetLayer() == s->GetCurrentLayer()); };

		// The shape tool
		{
			// TRANSLATORS: Name of the tool that draws lines, rectangles and ellipses of tiles, in the Tools menu and the tool strip
			auto& shape = a.Add(ActionIds::ToolShape, _x("editor", "Shapes"), EditorIcon::Shapes, [s]() {
				s->GetTools().SetActive(EditorToolType::Shape);
				s->SetPickerPanel(PickerPanel::Tiles);
			});
			shape.Key = { Keys::L, 0 };
			shape.IsChecked = [s]() { return (s->GetTools().GetActiveType() == EditorToolType::Shape); };
			struct ShapeChoice { const char* Id; const char* Label; ToolShape Shape; };
			static const ShapeChoice Shapes[] = {
				// TRANSLATORS: Commands in the menus and the list of keyboard shortcuts, the shape the shape tool draws
				{ ActionIds::ShapeLine, NC_("editor", "Shape: Line"), ToolShape::Line },
				{ ActionIds::ShapeRectangle, NC_("editor", "Shape: Rectangle Outline"), ToolShape::Rectangle },
				{ ActionIds::ShapeEllipse, NC_("editor", "Shape: Ellipse"), ToolShape::Ellipse },
				{ ActionIds::ShapeFilledEllipse, NC_("editor", "Shape: Filled Ellipse"), ToolShape::FilledEllipse },
			};
			for (const ShapeChoice& choice : Shapes) {
				ToolShape value = choice.Shape;
				auto& action = a.Add(choice.Id, _x("editor", choice.Label), EditorIcon::None, [s, t, value]() {
					t->Shape = value;
					t->NotifyChanged();
					s->GetTools().SetActive(EditorToolType::Shape);
				});
				action.IsChecked = [t, value]() { return (t->Shape == value); };
			}
		}

		// The stamp
		// The keymap like the Multi-Layer Level Editor gives it R, see ActionRegistry::ApplyPreset()
		// TRANSLATORS: Command in the menus and the list of keyboard shortcuts, turns the brush of tiles (stamp) by 90 degrees
		a.Add(ActionIds::StampRotate, _x("editor", "Turn Stamp Clockwise"), EditorIcon::RotateCW, [s]() { s->GetStamp().Rotate(true); }).Key = { Keys::R, KeyMod::Shift };
		// TRANSLATORS: Command in the menus and the list of keyboard shortcuts, turns the brush of tiles (stamp) back by 90 degrees
		a.Add(ActionIds::StampRotateCCW, _x("editor", "Turn Stamp Counter-Clockwise"), EditorIcon::RotateCCW, [s]() { s->GetStamp().Rotate(false); });
		{
			// TRANSLATORS: Toggle command, a brush painting at random flips some of its tiles horizontally
			auto& flipX = toggle(ActionIds::StampRandomFlipX,_x("editor", "Flip Tiles at Random Horizontally"), &ToolSettings::RandomFlipX);
			flipX.Icon = EditorIcon::RandomFlipX;
			flipX.IsEnabled = [s]() { return s->GetStamp().Random; };
			// TRANSLATORS: Toggle command, a brush painting at random flips some of its tiles vertically
			auto& flipY = toggle(ActionIds::StampRandomFlipY,_x("editor", "Flip Tiles at Random Vertically"), &ToolSettings::RandomFlipY);
			flipY.Icon = EditorIcon::RandomFlipY;
			flipY.IsEnabled = [s]() { return s->GetStamp().Random; };
		}
		// TRANSLATORS: Command that opens the dialog setting how often each tile of the brush is painted at random
		a.Add(ActionIds::StampWeights,_x("editor", "Weights of Tiles"), EditorIcon::Usage, [s]() { ShowStampWeights(*s); });

		// The modifier painted with the tiles
		{
			struct ModifierChoice { const char* Id; const char* Label; TileModifierMode Mode; };
			static const ModifierChoice Modifiers[] = {
				// TRANSLATORS: Commands in the menus and the list of keyboard shortcuts, the modifier the tools paint the tiles with
				{ ActionIds::ToolsModifierKeep, NC_("editor", "Paint Tiles as They Are"), TileModifierMode::Keep },
				{ ActionIds::ToolsModifierNormal, NC_("editor", "Paint Tiles Normal"), TileModifierMode::Normal },
				{ ActionIds::ToolsModifierTranslucent, NC_("editor", "Paint Tiles Translucent"), TileModifierMode::Translucent },
				{ ActionIds::ToolsModifierInvisible, NC_("editor", "Paint Tiles Invisible"), TileModifierMode::Invisible },
				{ ActionIds::ToolsModifierHeat, NC_("editor", "Paint Tiles with Heat"), TileModifierMode::Heat },
			};
			for (const ModifierChoice& choice : Modifiers) {
				TileModifierMode mode = choice.Mode;
				auto& action = a.Add(choice.Id, _x("editor", choice.Label), EditorIcon::None, [t, mode]() { t->Modifier = mode; });
				action.IsChecked = [t, mode]() { return (t->Modifier == mode); };
			}
			// TRANSLATORS: Toggle command, painting changes only the modifier of the tiles painted over and keeps the tiles
			auto& only = a.Add(ActionIds::ToolsModifierOnly,_x("editor", "Change Only the Modifier of Tiles"), EditorIcon::None, [t]() { t->ModifierOnly = !t->ModifierOnly; });
			only.IsChecked = [t]() { return t->ModifierOnly; };

			struct SetChoice { const char* Id; const char* Label; TileModifierMode Mode; };
			static const SetChoice Sets[] = {
				// TRANSLATORS: Commands in the Select menu, change the modifier of the selected tiles
				{ ActionIds::EditModifierNormal, NC_("editor", "Make Selected Tiles Normal"), TileModifierMode::Normal },
				{ ActionIds::EditModifierTranslucent, NC_("editor", "Make Selected Tiles Translucent"), TileModifierMode::Translucent },
				{ ActionIds::EditModifierInvisible, NC_("editor", "Make Selected Tiles Invisible"), TileModifierMode::Invisible },
				{ ActionIds::EditModifierHeat, NC_("editor", "Give Selected Tiles Heat"), TileModifierMode::Heat },
			};
			for (const SetChoice& choice : Sets) {
				TileModifierMode mode = choice.Mode;
				a.Add(choice.Id, _x("editor", choice.Label), EditorIcon::None, [s, mode]() { SetSelectionModifier(*s, mode); }).IsEnabled = hasTileSelection;
			}
		}

		// The fill and the magic wand
		// TRANSLATORS: Toggle command, the fill tool goes on across the edges of a layer that repeats
		toggle(ActionIds::ToolsAcrossSeams, _x("editor", "Fill Across Edges of Repeating Layers"), &ToolSettings::AcrossSeams);
		toggle(ActionIds::ToolsIgnoreFlips, _x("editor", "Fill and Select Flipped Tiles as the Same"), &ToolSettings::IgnoreFlips);
		// TRANSLATORS: Toggle command, the fill, rectangle and shape tools paint only the tiles that are empty
		toggle(ActionIds::ToolsOnlyEmpty, _x("editor", "Fill Only Empty Tiles"), &ToolSettings::OnlyEmpty);
		// TRANSLATORS: Toggle command, the magic wand selects all such tiles of the layer, not only those joined together
		toggle(ActionIds::ToolsWandGlobal,_x("editor", "Magic Wand Selects All Such Tiles"), &ToolSettings::WandGlobal);

		// The selection
		{
			// Of the events with the event tool, as Select All
			auto& invert = a.Add(ActionIds::SelectInvert,_x("editor", "Invert Selection"), EditorIcon::Select, [s]() {
				if (s->IsEditingEvents()) {
					InvertEventSelection(*s);
					return;
				}
				LevelDocument& document = s->GetDocument();
				std::int32_t layer = s->GetCurrentLayer();
				if (layer >= 0 && layer < std::int32_t(document.Layers.size())) {
					s->GetSelection().Invert(layer, Vector2i(document.Layers[layer].GetWidth(), document.Layers[layer].GetHeight()));
				}
			});
			invert.Key = { Keys::I, KeyMod::Ctrl };
			// TRANSLATORS: Command in the Select menu, makes the selection of tiles one tile larger on each side
			a.Add(ActionIds::SelectGrow, _x("editor", "Grow Selection"), EditorIcon::Plus, [s]() { s->GetSelection().Grow(false); }).IsEnabled = hasTileSelection;
			// TRANSLATORS: Command in the Select menu, makes the selection of tiles one tile smaller on each side
			a.Add(ActionIds::SelectShrink, _x("editor", "Shrink Selection"), EditorIcon::Minus, [s]() { s->GetSelection().Grow(true); }).IsEnabled = hasTileSelection;
			a.Add(ActionIds::SelectSameTiles,_x("editor", "Select All Tiles Like These"), EditorIcon::Wand, [s]() { SelectSameTiles(*s); }).IsEnabled = [s]() { return !s->IsEditingEvents(); };
			struct Nudge { const char* Id; const char* Label; Keys Key; Vector2i Offset; };
			static const Nudge Nudges[] = {
				// TRANSLATORS: Commands in the Select menu, move the selected tiles by one tile in the direction
				{ ActionIds::SelectMoveLeft, NC_("editor", "Move Selected Tiles Left"), Keys::Left, Vector2i(-1, 0) },
				{ ActionIds::SelectMoveRight, NC_("editor", "Move Selected Tiles Right"), Keys::Right, Vector2i(1, 0) },
				{ ActionIds::SelectMoveUp, NC_("editor", "Move Selected Tiles Up"), Keys::Up, Vector2i(0, -1) },
				{ ActionIds::SelectMoveDown, NC_("editor", "Move Selected Tiles Down"), Keys::Down, Vector2i(0, 1) },
			};
			for (const Nudge& nudge : Nudges) {
				Vector2i offset = nudge.Offset;
				auto& action = a.Add(nudge.Id, _x("editor", nudge.Label), EditorIcon::None, [s, offset]() { MoveSelectedTiles(*s, offset); });
				action.Key = { nudge.Key, KeyMod::Shift };
				action.IsEnabled = hasTileSelection;
			}
		}

		// Mirroring and copying all layers
		{
			auto canMirror = [s]() { return (s->IsEditingEvents() ? !s->GetEventSelection().IsEmpty() : !s->GetSelection().IsEmpty()); };
			auto& mirrorX = a.Add(ActionIds::EditMirrorH,_x("editor", "Mirror Selection Horizontally"), EditorIcon::FlipX, [s]() { MirrorSelection(*s, true); });
			mirrorX.Key = { Keys::X, KeyMod::Ctrl | KeyMod::Shift };
			mirrorX.IsEnabled = canMirror;
			auto& mirrorY = a.Add(ActionIds::EditMirrorV,_x("editor", "Mirror Selection Vertically"), EditorIcon::FlipY, [s]() { MirrorSelection(*s, false); });
			mirrorY.Key = { Keys::Y, KeyMod::Ctrl | KeyMod::Shift };
			mirrorY.IsEnabled = canMirror;
			// TRANSLATORS: Toggle command in the Select menu, mirrored events of a team get the other team, for team game modes
			toggle(ActionIds::EditMirrorSwapTeams,_x("editor", "Mirroring Swaps Teams of Events"), &ToolSettings::MirrorSwapTeams);

			// TRANSLATORS: Command in the Select menu, copies the selected area from all layers at once, with the events
			auto& copyLayers = a.Add(ActionIds::EditCopyLayers,_x("editor", "Copy All Layers"), EditorIcon::Copy, [s]() { CopyAllLayers(*s); });
			copyLayers.Key = { Keys::C, KeyMod::Ctrl | KeyMod::Shift };
			copyLayers.IsEnabled = [s]() { return (!s->GetSelection().IsEmpty() || s->GetEventSelection().HasCells()); };
			// TRANSLATORS: Command in the Select menu, pastes what Copy All Layers copied into all layers at once
			a.Add(ActionIds::EditPasteLayers,_x("editor", "Paste All Layers"), EditorIcon::Paste, [s]() { PasteAllLayers(*s); }).Key = { Keys::V, KeyMod::Ctrl | KeyMod::Shift };
		}

		// How the event tool places events, or that it only selects them
		{
			struct ModeChoice { const char* Id; const char* Label; EventPlaceMode Mode; };
			static const ModeChoice Modes[] = {
				// TRANSLATORS: Commands in the menus and the list of keyboard shortcuts, how the event tool places events, or that it only selects them
				{ ActionIds::EventsModePaint, NC_("editor", "Place Events by Clicking"), EventPlaceMode::Paint },
				{ ActionIds::EventsModeRectangle, NC_("editor", "Place Events in a Rectangle"), EventPlaceMode::Rectangle },
				{ ActionIds::EventsModeFill, NC_("editor", "Place Events by Filling an Area"), EventPlaceMode::Fill },
				{ ActionIds::EventsModeSelect, NC_("editor", "Select Events Without Placing Them"), EventPlaceMode::Select },
			};
			for (const ModeChoice& choice : Modes) {
				EventPlaceMode mode = choice.Mode;
				auto& action = a.Add(choice.Id, _x("editor", choice.Label), EditorIcon::Event, [s, t, mode]() {
					t->EventMode = mode;
					t->NotifyChanged();
					s->GetTools().SetActive(EditorToolType::Event);
					s->SetPickerPanel(PickerPanel::Events);
				});
				action.IsChecked = [t, mode]() { return (t->EventMode == mode); };
			}
			// TRANSLATORS: Command that places the chosen event on all selected tiles of the sprite layer
			a.Add(ActionIds::EventsFillSelection,_x("editor", "Fill Selection with Event"), EditorIcon::Event, [s]() { FillSelectionWithEvent(*s); }).IsEnabled = [s]() {
				return (!s->GetSelection().IsEmpty() && s->GetSelection().GetLayer() == s->GetDocument().GetSpriteLayerIndex());
			};
		}

		// The jumps the measure tool shows
		{
			struct ArcChoice { const char* Id; const char* Label; std::uint8_t Arc; };
			static const ArcChoice Arcs[] = {
				// TRANSLATORS: Toggle commands, the jumps of the player the measure tool shows
				{ ActionIds::MeasureWalk, NC_("editor", "Measure: Walking Jump"), ToolSettings::ArcWalk },
				{ ActionIds::MeasureDash, NC_("editor", "Measure: Dashing Jump"), ToolSettings::ArcDash },
				{ ActionIds::MeasureDoubleJump, NC_("editor", "Measure: Double Jump of Spaz"), ToolSettings::ArcDoubleJump },
				{ ActionIds::MeasureCopter, NC_("editor", "Measure: Copter of Jazz"), ToolSettings::ArcCopter },
			};
			for (const ArcChoice& choice : Arcs) {
				std::uint8_t arc = choice.Arc;
				auto& action = a.Add(choice.Id, _x("editor", choice.Label), EditorIcon::Measure, [t, arc]() {
					t->ShownArcs ^= arc;
					t->NotifyChanged();
				});
				action.IsChecked = [t, arc]() { return ((t->ShownArcs & arc) != 0); };
			}
			toggle(ActionIds::MeasureCollisions,_x("editor", "Measure: Jumps Stop at Solid Tiles"), &ToolSettings::ArcCollisions);
		}
	}

#if defined(DEATH_DEBUG)
	std::int32_t SelfTest()
	{
		std::int32_t problems = 0;
		auto check = [&problems](bool condition, const char* what) {
			if (!condition) {
				LOGE("Tools self-test failed: {}", what);
				problems++;
			}
		};

		// A stamp turned four times is the same, turned once it's as wide as it was high
		{
			TileStamp stamp;
			stamp.Width = 3;
			stamp.Height = 2;
			for (std::int32_t i = 0; i < 6; i++) {
				stamp.Tiles.push_back(TileRef{ std::uint16_t(i + 1), TileRef::DefaultTileSet, 0 });
				stamp.Present.push_back(i != 4);
			}
			TileStamp turned = stamp;
			turned.Rotate(true);
			check(turned.Width == 2 && turned.Height == 3, "a turned stamp has the other size");
			// The top left tile goes to the top right
			check(turned.GetTile(1, 0).Index == 1, "a stamp turned clockwise has its top left at the top right");
			turned.Rotate(true);
			turned.Rotate(true);
			turned.Rotate(true);
			bool same = (turned.Width == stamp.Width && turned.Height == stamp.Height);
			for (std::size_t i = 0; same && i < stamp.Tiles.size(); i++) {
				same = (turned.Tiles[i] == stamp.Tiles[i] && turned.Present[i] == stamp.Present[i]);
			}
			check(same, "a stamp turned four times is the same");
			turned.Rotate(false);
			turned.Rotate(true);
			check(turned.Width == 3 && turned.GetTile(0, 0).Index == 1, "turning both ways gives the stamp back");
		}

		// A selection grown and shrunk is the same rectangle, inverted twice too
		{
			TileSelection selection;
			selection.Select(0, Vector2i(16, 16), Recti(4, 5, 3, 2), false);
			selection.Grow(false);
			check(selection.GetBounds().X == 3 && selection.GetBounds().W == 5 && selection.GetBounds().H == 4, "growing adds a tile around");
			selection.Grow(true);
			check(selection.GetBounds().X == 4 && selection.GetBounds().W == 3 && selection.GetBounds().H == 2, "shrinking takes it away again");
			selection.Invert(0, Vector2i(16, 16));
			check(!selection.Contains(4, 5) && selection.Contains(0, 0) && selection.GetBounds().W == 16, "inverting selects the rest");
			selection.Invert(0, Vector2i(16, 16));
			check(selection.Contains(4, 5) && !selection.Contains(0, 0) && selection.GetBounds().W == 3, "inverting twice gives it back");
		}

		// Shapes have their cells each once, a rectangle only its edge
		{
			SmallVector<Vector2i, 0> cells;
			ShapeTool::GetShapeCells(ToolShape::Rectangle, Vector2i(0, 0), Vector2i(3, 2), cells);
			check(cells.size() == 10, "the outline of a 4x3 rectangle has 10 tiles");
			ShapeTool::GetShapeCells(ToolShape::Line, Vector2i(0, 0), Vector2i(5, 2), cells);
			check(cells.size() == 6, "a line goes through a tile of each column");
			ShapeTool::GetShapeCells(ToolShape::FilledEllipse, Vector2i(0, 0), Vector2i(0, 0), cells);
			check(cells.size() == 1, "an ellipse of one tile is that tile");
			ShapeTool::GetShapeCells(ToolShape::Ellipse, Vector2i(0, 0), Vector2i(8, 8), cells);
			bool unique = true;
			for (std::size_t i = 0; i < cells.size() && unique; i++) {
				for (std::size_t j = i + 1; j < cells.size(); j++) {
					if (cells[i] == cells[j]) {
						unique = false;
						break;
					}
				}
			}
			check(unique && !cells.empty(), "the outline of an ellipse has each tile once");
			check(ShapeTool::Constrain(ToolShape::Rectangle, Vector2i(0, 0), Vector2i(3, -5)) == Vector2i(5, -5), "a square takes the longer side");
		}

		// Stamps go through the clipboard as they are, a damaged text isn't read
		{
			TileStamp stamp;
			stamp.Width = 2;
			stamp.Height = 1;
			stamp.Tiles.push_back(TileRef{ 7, 1, Formats::TileRecord::FlipX });
			stamp.Tiles.push_back(TileRef{ 0, 0, 0 });
			stamp.Present.push_back(true);
			stamp.Present.push_back(false);
			TileStamp read;
			check(StampClipboard::ReadTiles(StampClipboard::WriteTiles(stamp), read) && read.Width == 2 && read.Tiles[0] == stamp.Tiles[0] &&
				!read.Present[1], "tiles go through the clipboard");
			check(!StampClipboard::ReadTiles("{\"J2R\":1,\"w\":65536,\"h\":65536,\"t\":[]}"_s, read), "a stamp too large isn't read");
			check(!StampClipboard::ReadTiles("{\"J2R\":1,\"w\":3,\"h\":1,\"t\":[0,1,0]}"_s, read), "a stamp with tiles missing isn't read");

			LayeredStamp layered;
			layered.Width = 2;
			layered.Height = 1;
			layered.Origin = Vector2i(5, 6);
			layered.Layers.push_back(LayeredStamp::Layer{ -1, stamp });
			layered.Layers.push_back(LayeredStamp::Layer{ 0, stamp });
			EventStamp::Item& item = layered.Events.Items.emplace_back();
			item.Offset = Vector2i(1, 0);
			std::memset(&item.Event, 0, sizeof(item.Event));
			item.Event.Type = std::uint16_t(EventType::Gem);
			item.Event.Params[0] = 3;
			EventStamp::OffGridItem& offGridItem = layered.Events.OffGrid.emplace_back();
			offGridItem.Offset = Vector2i(40, 7);
			offGridItem.Event = item.Event;
			offGridItem.Event.Params[0] = 4;
			String text = StampClipboard::WriteLayered(layered);
			LayeredStamp readLayered;
			check(StampClipboard::ReadLayered(text, readLayered) && readLayered.Layers.size() == 2 && readLayered.Layers[0].FromSpriteLayer == -1 &&
				readLayered.Origin == Vector2i(5, 6) && readLayered.Events.Items.size() == 1 && readLayered.Events.Items[0].Event.Params[0] == 3,
				"tiles of several layers go through the clipboard");
			check(readLayered.Events.OffGrid.size() == 1 && readLayered.Events.OffGrid[0].Offset == Vector2i(40, 7) &&
				readLayered.Events.OffGrid[0].Event.Params[0] == 4, "events off the grid go through the clipboard");
			// A prefab keeps them too, by pixels from its corner
			LevelDocument emptyDocument;
			TilePrefab prefab = TilePrefab::FromLayered(emptyDocument, layered, 0, "Test"_s);
			check(prefab.OffGridEvents.size() == 1 && prefab.OffGridEvents[0].Offset == Vector2i(40, 7) && prefab.HasLayers(),
				"a prefab keeps the events off the grid");
			// An older version reads the first layer as tiles
			check(StampClipboard::ReadTiles(text, read) && read.Width == 2, "tiles of several layers are read as tiles too");

			// Tiles of an extra tile set are found in another level that has it in another slot, at another offset
			LevelDocument from, to;
			from.Properties.DefaultTileSet = "A"_s;
			to.Properties.DefaultTileSet = "A"_s;
			Formats::ExtraTileSetRecord extra{};
			extra.Name = "B"_s;
			extra.Offset = 10;
			extra.Count = 20;
			from.ExtraTileSets.push_back(extra);
			extra.Name = "C"_s;
			extra.Offset = 0;
			to.ExtraTileSets.push_back(extra);
			extra.Name = "b"_s;
			extra.Count = 40;
			to.ExtraTileSets.push_back(extra);
			TileStamp extraStamp = TileStamp::FromTile(TileRef{ 3, 1, 0 });
			check(StampClipboard::ReadTiles(StampClipboard::WriteTiles(extraStamp, &from), read, &to) && read.Present[0] &&
				read.Tiles[0].Source == 2 && read.Tiles[0].Index == 13, "tiles are found in the tile sets of another level by their names");
			to.ExtraTileSets.erase(to.ExtraTileSets.begin() + 1);
			check(StampClipboard::ReadTiles(StampClipboard::WriteTiles(extraStamp, &from), read, &to) && !read.Present[0],
				"tiles of a tile set another level hasn't are left out");
		}

		// The options of the tools change the tiles painted as chosen
		{
			ToolSettings settings;
			TileStamp stamp = TileStamp::FromTile(TileRef{ 5, 0, 0 });
			TileRef existing{ 9, 0, Formats::TileRecord::FlipY };
			TileRef result;
			settings.Modifier = TileModifierMode::Translucent;
			check(settings.ResolveTile(stamp, Vector2i(), Vector2i(), existing, true, false, result) && result.Index == 5 &&
				(result.Flags & Formats::TileRecord::ModifierMask) == Formats::TileRecord::ModifierTranslucent, "a tile is painted translucent");
			settings.ModifierOnly = true;
			check(settings.ResolveTile(stamp, Vector2i(), Vector2i(), existing, true, false, result) && result.Index == 9 &&
				(result.Flags & Formats::TileRecord::FlipY) != 0, "only the modifier of the tile there changes");
			check(!settings.ResolveTile(stamp, Vector2i(), Vector2i(), TileRef{}, true, false, result), "an empty tile gets no modifier");
			settings.ModifierOnly = false;
			check(!settings.ResolveTile(stamp, Vector2i(), Vector2i(), existing, true, true, result), "only empty tiles are filled");
		}

		// A walking jump reaches a tile up close, not far away
		{
			MeasureTool::JumpArc walk{ 4.0f, false, false };
			check(MeasureTool::Reaches(walk, Vector2f(0.0f, 0.0f), Vector2f(64.0f, -32.0f)), "a walking jump gets a tile up");
			check(!MeasureTool::Reaches(walk, Vector2f(0.0f, 0.0f), Vector2f(64.0f, -400.0f)), "a walking jump doesn't get twelve tiles up");
			check(std::uint8_t(0x1F & ToolSettings::ArcAll) == (ToolSettings::ArcWalk | ToolSettings::ArcDash | ToolSettings::ArcDoubleJump | ToolSettings::ArcCopter),
				"the jump of Lori kept from before isn't shown");
		}

		return problems;
	}
#endif
}

#endif
