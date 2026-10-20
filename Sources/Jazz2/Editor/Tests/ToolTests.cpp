#include "ToolTests.h"

#if defined(WITH_EDITOR) && defined(DEATH_DEBUG)

#include "FakeToolHost.h"
#include "../Document/Commands.h"
#include "../Events/EventCatalog.h"
#include "../Runtime/RuntimeSync.h"
#include "../Tools/ShapeTool.h"
#include "../Common/StampClipboard.h"
#include "../Tools/TileTools.h"
#include "../../EventType.h"
#include "../../Formats/LevelFile.h"
#include "../../../nCine/Base/TimeStamp.h"

#include <IO/MemoryStream.h>

#include <algorithm>
#include <initializer_list>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor::ToolTests
{
	namespace
	{
		constexpr TileRef TileA{ 5, TileRef::DefaultTileSet, 0 };
		constexpr TileRef TileB{ 6, TileRef::DefaultTileSet, Formats::TileRecord::FlipX };
		constexpr TileRef TileC{ 7, TileRef::DefaultTileSet, 0 };
		constexpr TileRef TileD{ 8, TileRef::DefaultTileSet, Formats::TileRecord::FlipY };
		constexpr TileRef Wall{ 9, TileRef::DefaultTileSet, 0 };

		// Counts the problems of a test, each is logged with what was expected
		class Expect
		{
		public:
			explicit Expect(const char* test)
				: _test(test), _problems(0) {}

			void operator()(bool condition, const char* what) {
				if (!condition) {
					LOGE("{}: {}", _test, what);
					_problems++;
				}
			}

			std::int32_t GetProblems() const {
				return _problems;
			}

		private:
			const char* _test;
			std::int32_t _problems;
		};

		std::int32_t CountTiles(const DocumentLayer& layer, const TileRef& tile)
		{
			return std::int32_t(std::count(layer.Tiles.begin(), layer.Tiles.end(), tile));
		}

		std::int32_t CountEvents(const LevelDocument& document)
		{
			return std::int32_t(document.Events.size());
		}

		TileStamp MakeStamp(std::int32_t width, std::int32_t height, std::initializer_list<TileRef> tiles)
		{
			TileStamp stamp;
			stamp.Width = width;
			stamp.Height = height;
			for (const TileRef& tile : tiles) {
				stamp.Tiles.push_back(tile);
				stamp.Present.push_back(true);
			}
			return stamp;
		}

		std::uint32_t GetCellIndex(const LevelDocument& document, Vector2i cell)
		{
			return std::uint32_t(cell.X + cell.Y * document.GetEventGridSize().X);
		}

		// A stroke is shown while it's dragged and is one step once it's released, it's undone and redone as one; a
		// straight line with Shift, a stamp of more tiles, a canceled stroke, a locked layer and a selection
		std::int32_t TestBrush()
		{
			Expect expect("Brush tool");
			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(32, 24));
			FakeToolHost host(document);
			ToolManager tools(host);
			tools.SetActive(EditorToolType::Brush);
			EditorTool& brush = *tools.GetActive();
			CommandHistory& history = host.GetHistory();
			host.SetCurrentLayer(0);
			host.SetStamp(TileStamp::FromTile(TileA));
			const DocumentLayer& layer = document.Layers[0];

			brush.OnPointerDown(host.MakePointer(Vector2i(2, 2)));
			brush.OnPointerMove(host.MakePointer(Vector2i(6, 2)));
			expect(brush.IsBusy() && layer.GetTile(2, 2).IsEmpty() && history.GetDoneCount() == 0, "a stroke changes nothing until it's released");
			expect(host.Previews.size() == 5 && host.Previews[0].Layer == 0 && host.Previews[0].Tile == TileA, "every tile of a stroke is shown once");
			brush.OnPointerUp(host.MakePointer(Vector2i(6, 2)));
			expect(!brush.IsBusy() && history.GetDoneCount() == 1 && CountTiles(layer, TileA) == 5 && layer.GetTile(6, 2) == TileA, "a stroke is one step");
			expect(host.Undo() && CountTiles(layer, TileA) == 0, "a stroke undone");
			expect(host.Redo() && CountTiles(layer, TileA) == 5, "a stroke redone");

			// With Shift a line from where it was pressed, only painted when it's released, and never shown before
			host.ClearRecorded();
			brush.OnPointerDown(host.MakePointer(Vector2i(0, 5), MouseButton::Left, KeyMod::Shift));
			brush.OnPointerMove(host.MakePointer(Vector2i(4, 9), MouseButton::Left, KeyMod::Shift));
			brush.OnPointerUp(host.MakePointer(Vector2i(4, 9), MouseButton::Left, KeyMod::Shift));
			expect(host.Previews.empty() && CountTiles(layer, TileA) == 10 && layer.GetTile(2, 7) == TileA && layer.GetTile(4, 9) == TileA, "a straight line");

			// A stamp of 2x2 tiles has its middle at the pointer
			host.SetStamp(MakeStamp(2, 2, { TileA, TileB, TileC, TileD }));
			host.Click(brush, Vector2i(10, 15));
			expect(layer.GetTile(9, 14) == TileA && layer.GetTile(10, 14) == TileB && layer.GetTile(9, 15) == TileC && layer.GetTile(10, 15) == TileD,
				"a stamp of more tiles");

			// Canceled, the tiles shown are shown as the level has them again, nothing is changed
			host.SetStamp(TileStamp::FromTile(TileC));
			host.ClearRecorded();
			std::int32_t doneCount = history.GetDoneCount();
			brush.OnPointerDown(host.MakePointer(Vector2i(10, 10)));
			brush.OnPointerMove(host.MakePointer(Vector2i(12, 10)));
			brush.Cancel();
			expect(!brush.IsBusy() && history.GetDoneCount() == doneCount && layer.GetTile(11, 10).IsEmpty(), "a canceled stroke changes nothing");
			expect(host.Reverts.size() == 1 && host.Reverts[0].Layer == 0 && host.Reverts[0].Area == Recti(10, 10, 3, 1), "a canceled stroke shown as it was");

			// The right button picks the tile there
			brush.OnPointerDown(host.MakePointer(Vector2i(2, 2), MouseButton::Right));
			brush.OnPointerUp(host.MakePointer(Vector2i(2, 2), MouseButton::Right));
			expect(host.GetStamp().Width == 1 && host.GetStamp().Tiles[0] == TileA && history.GetDoneCount() == doneCount, "a tile picked");

			// A locked layer isn't painted
			host.ClearRecorded();
			document.Layers[0].EditorFlags = DocumentLayer::EditorLocked;
			host.Click(brush, Vector2i(20, 20));
			expect(!brush.IsBusy() && layer.GetTile(20, 20).IsEmpty() && host.Messages.size() == 1 && history.GetDoneCount() == doneCount, "a locked layer");
			document.Layers[0].EditorFlags = 0;

			// Only the selected tiles are painted while something is selected on the layer
			host.GetSelection().Select(0, Vector2i(32, 24), Recti(20, 0, 2, 2), false);
			host.Drag(brush, Vector2i(18, 0), Vector2i(25, 0));
			expect(CountTiles(layer, TileA) == 13 && layer.GetTile(20, 0) == TileA && layer.GetTile(21, 0) == TileA && layer.GetTile(22, 0).IsEmpty(),
				"only the selected tiles painted");
			host.GetSelection().Clear();

			// The eraser paints the empty tile
			EditorTool& eraser = *tools.Get(EditorToolType::Eraser);
			host.Drag(eraser, Vector2i(2, 2), Vector2i(4, 2));
			expect(layer.GetTile(2, 2).IsEmpty() && layer.GetTile(4, 2).IsEmpty() && layer.GetTile(5, 2) == TileA && CountTiles(layer, TileA) == 10,
				"tiles erased");
			expect(host.Undo() && layer.GetTile(3, 2) == TileA, "erasing undone");

			while (host.Undo()) {}
			expect(CountTiles(layer, TileRef{}) == layer.GetWidth() * layer.GetHeight(), "everything undone");
			return expect.GetProblems();
		}

		// A rectangle repeats the stamp from its corner, the right button erases one, and only the empty tiles are painted
		// if it's chosen
		std::int32_t TestRectangle()
		{
			Expect expect("Rectangle tool");
			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(32, 24));
			FakeToolHost host(document);
			ToolManager tools(host);
			tools.SetActive(EditorToolType::Rectangle);
			EditorTool& rectangle = *tools.GetActive();
			host.SetCurrentLayer(0);
			const DocumentLayer& layer = document.Layers[0];

			host.SetStamp(MakeStamp(2, 1, { TileA, TileB }));
			host.Drag(rectangle, Vector2i(4, 2), Vector2i(1, 1));
			expect(host.Previews.empty() && host.GetHistory().GetDoneCount() == 1, "a rectangle is one step");
			expect(layer.GetTile(1, 1) == TileA && layer.GetTile(2, 1) == TileB && layer.GetTile(3, 2) == TileA && layer.GetTile(4, 2) == TileB &&
				CountTiles(layer, TileA) == 4 && CountTiles(layer, TileB) == 4, "the stamp repeated from the corner");

			host.Drag(rectangle, Vector2i(2, 1), Vector2i(3, 2), MouseButton::Right);
			expect(layer.GetTile(2, 1).IsEmpty() && layer.GetTile(3, 2).IsEmpty() && CountTiles(layer, TileA) == 2 && CountTiles(layer, TileB) == 2,
				"a rectangle erased");

			tools.GetSettings().OnlyEmpty = true;
			host.SetStamp(TileStamp::FromTile(TileC));
			host.Drag(rectangle, Vector2i(0, 1), Vector2i(5, 1));
			tools.GetSettings().OnlyEmpty = false;
			expect(CountTiles(layer, TileC) == 4 && layer.GetTile(1, 1) == TileA && layer.GetTile(4, 1) == TileB && layer.GetTile(0, 1) == TileC,
				"only the empty tiles painted");

			expect(host.Undo() && host.Undo() && host.Undo() && !host.Undo() && CountTiles(layer, TileRef{}) == layer.GetWidth() * layer.GetHeight(),
				"three steps undone");
			return expect.GetProblems();
		}

		// A fill stops at other tiles, goes across the edges of a layer that repeats only if it's chosen, keeps to the
		// selection, and the right button erases the area
		std::int32_t TestFill()
		{
			Expect expect("Fill tool");
			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(32, 24));
			FakeToolHost host(document);
			ToolManager tools(host);
			tools.SetActive(EditorToolType::Fill);
			EditorTool& fill = *tools.GetActive();
			host.SetCurrentLayer(0);
			DocumentLayer& layer = document.Layers[0];

			// A box of 6x5 tiles, its walls are 18 tiles around 12 inside
			for (std::int32_t x = 10; x <= 15; x++) {
				layer.GetTile(x, 10) = Wall;
				layer.GetTile(x, 14) = Wall;
			}
			for (std::int32_t y = 11; y <= 13; y++) {
				layer.GetTile(10, y) = Wall;
				layer.GetTile(15, y) = Wall;
			}
			host.SetStamp(TileStamp::FromTile(TileA));
			host.Click(fill, Vector2i(12, 12));
			expect(CountTiles(layer, TileA) == 12 && layer.GetTile(11, 11) == TileA && layer.GetTile(14, 13) == TileA && CountTiles(layer, Wall) == 18,
				"the inside of a box filled");
			host.SetStamp(TileStamp::FromTile(TileB));
			host.Click(fill, Vector2i(0, 0));
			expect(CountTiles(layer, TileB) == 32 * 24 - 18 - 12 && CountTiles(layer, TileA) == 12, "the outside of a box filled");
			host.Click(fill, Vector2i(12, 12), MouseButton::Right);
			expect(CountTiles(layer, TileA) == 0 && layer.GetTile(12, 12).IsEmpty() && host.GetHistory().GetDoneCount() == 3, "an area erased");

			// Only the selected tiles of the area
			host.GetSelection().Select(0, Vector2i(32, 24), Recti(11, 11, 2, 2), false);
			host.SetStamp(TileStamp::FromTile(TileC));
			host.Click(fill, Vector2i(12, 12));
			expect(CountTiles(layer, TileC) == 4 && layer.GetTile(13, 13).IsEmpty(), "only the selected tiles filled");
			host.GetSelection().Clear();

			// Columns of walls in a layer that repeats, between them across its edges or not, as chosen
			host.SetCurrentLayer(2);
			DocumentLayer& repeating = document.Layers[2];
			expect(repeating.GetWidth() == 32 && (repeating.Record.Flags & Formats::LayerRecord::RepeatX) != 0, "a layer that repeats");
			for (std::int32_t y = 0; y < repeating.GetHeight(); y++) {
				repeating.GetTile(5, y) = Wall;
				repeating.GetTile(20, y) = Wall;
			}
			tools.GetSettings().AcrossSeams = false;
			host.SetStamp(TileStamp::FromTile(TileA));
			host.Click(fill, Vector2i(25, 0));
			expect(CountTiles(repeating, TileA) == 11 * 32 && repeating.GetTile(2, 0).IsEmpty(), "filled up to the edge");
			host.Undo();
			tools.GetSettings().AcrossSeams = true;
			host.Click(fill, Vector2i(25, 0));
			expect(CountTiles(repeating, TileA) == 16 * 32 && repeating.GetTile(2, 0) == TileA && repeating.GetTile(10, 0).IsEmpty(), "filled across the edge");
			// Clicked past the layer, the copy of it there is filled
			host.SetStamp(TileStamp::FromTile(TileB));
			host.Click(fill, Vector2i(32 + 10, -5));
			expect(CountTiles(repeating, TileB) == 14 * 32, "filled in a copy of the layer");
			return expect.GetProblems();
		}

		// The eyedropper picks a rectangle as the stamp, the tiles outside of the layer are left out of it, and an area
		// larger than a stamp can be isn't picked at all
		std::int32_t TestEyedropper()
		{
			Expect expect("Eyedropper tool");
			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(32, 24));
			FakeToolHost host(document);
			ToolManager tools(host);
			tools.SetActive(EditorToolType::Eyedropper);
			EditorTool& eyedropper = *tools.GetActive();
			host.SetCurrentLayer(0);
			DocumentLayer& layer = document.Layers[0];
			layer.GetTile(0, 0) = TileA;
			layer.GetTile(1, 0) = TileB;
			layer.GetTile(2, 1) = TileC;
			layer.GetTile(31, 23) = TileD;

			host.Drag(eyedropper, Vector2i(0, 0), Vector2i(2, 1));
			const TileStamp& stamp = host.GetStamp();
			expect(stamp.Width == 3 && stamp.Height == 2 && stamp.GetTile(0, 0) == TileA && stamp.GetTile(1, 0) == TileB && stamp.GetTile(2, 1) == TileC &&
				stamp.IsPresent(0, 1) && stamp.GetTile(0, 1).IsEmpty(), "a rectangle picked");

			host.Drag(eyedropper, Vector2i(33, 24), Vector2i(30, 22));
			expect(stamp.Width == 4 && stamp.Height == 3 && stamp.GetTile(1, 1) == TileD && stamp.IsPresent(1, 1) && !stamp.IsPresent(2, 1) &&
				!stamp.IsPresent(0, 2), "the tiles outside of the layer left out");
			expect(host.GetHistory().GetDoneCount() == 0 && host.Messages.empty(), "picking changes nothing");

			// More tiles than a stamp can have, or a side longer than it can be
			host.Drag(eyedropper, Vector2i(0, 0), Vector2i(1024, 1023));
			expect(stamp.Width == 4 && host.Messages.size() == 1, "too many tiles aren't picked");
			host.Drag(eyedropper, Vector2i(-2, 0), Vector2i(StampClipboard::MaxStampSide - 2, 0));
			expect(stamp.Width == 4 && host.Messages.size() == 2, "a side too long isn't picked");
			host.Drag(eyedropper, Vector2i(-1, 0), Vector2i(StampClipboard::MaxStampSide - 2, 0));
			expect(stamp.Width == StampClipboard::MaxStampSide && stamp.Height == 1 && stamp.GetTile(1, 0) == TileA && host.Messages.size() == 2,
				"the longest side picked");
			return expect.GetProblems();
		}

		// Selected tiles dragged to another place, as one step, take the events on them with them on the sprite layer,
		// those off the grid too, and nothing moves if any of them would be outside of the layer
		std::int32_t TestSelectMove()
		{
			Expect expect("Select tool");
			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(32, 24));
			FakeToolHost host(document);
			ToolManager tools(host);
			tools.SetActive(EditorToolType::Select);
			EditorTool& select = *tools.GetActive();
			std::int32_t spriteLayer = document.GetSpriteLayerIndex();
			host.SetCurrentLayer(spriteLayer);
			const DocumentLayer& layer = document.Layers[spriteLayer];

			document.Layers[spriteLayer].GetTile(3, 3) = TileA;
			document.Layers[spriteLayer].GetTile(4, 3) = TileB;
			Formats::EventRecord gem = EventCatalog::MakeDefault(EventType::Gem);
			document.SetEvent(3, 3, gem);
			document.SetEvent(10, 10, gem);
			Formats::OffGridEventRecord& offGrid = document.OffGridEvents.emplace_back();
			offGrid.X = 4 * 32 + 5;
			offGrid.Y = 3 * 32 + 7;
			offGrid.Event = gem;

			host.Drag(select, Vector2i(3, 3), Vector2i(4, 3));
			TileSelection& selection = host.GetSelection();
			expect(selection.GetLayer() == spriteLayer && selection.GetBounds() == Recti(3, 3, 2, 1), "a rectangle selected");

			host.Drag(select, Vector2i(3, 3), Vector2i(6, 5));
			expect(host.GetHistory().GetDoneCount() == 1, "moving is one step");
			expect(layer.GetTile(6, 5) == TileA && layer.GetTile(7, 5) == TileB && layer.GetTile(3, 3).IsEmpty() && layer.GetTile(4, 3).IsEmpty(),
				"the tiles moved");
			expect(document.GetEvent(6, 5).Type == std::uint16_t(EventType::Gem) && LevelDocument::IsEmptyEvent(document.GetEvent(3, 3)) &&
				document.GetEvent(10, 10).Type == std::uint16_t(EventType::Gem) && CountEvents(document) == 2, "the event on them moved");
			expect(document.OffGridEvents.size() == 1 && document.OffGridEvents[0].X == 7 * 32 + 5 && document.OffGridEvents[0].Y == 5 * 32 + 7,
				"the event off the grid on them moved");
			expect(selection.GetBounds() == Recti(6, 5, 2, 1), "the selection moved with them");

			host.ClearRecorded();
			expect(!MoveSelectedTiles(host, Vector2i(-10, 0)) && host.Messages.size() == 1 && host.GetHistory().GetDoneCount() == 1,
				"nothing moved out of the layer");

			expect(host.Undo() && layer.GetTile(3, 3) == TileA && layer.GetTile(6, 5).IsEmpty() && document.GetEvent(3, 3).Type == std::uint16_t(EventType::Gem) &&
				LevelDocument::IsEmptyEvent(document.GetEvent(6, 5)) && document.OffGridEvents[0].X == 4 * 32 + 5, "moving undone at once");
			expect(host.Redo() && layer.GetTile(7, 5) == TileB && document.GetEvent(6, 5).Type == std::uint16_t(EventType::Gem), "moving redone");

			// A click without dragging ends the selection
			host.Click(select, Vector2i(20, 20));
			expect(selection.IsEmpty(), "the selection ended");
			return expect.GetProblems();
		}

		// Events placed by a click and a drag, erased by the right button, and a stroke canceled leaves the events and the
		// selection as they were
		std::int32_t TestEvents()
		{
			Expect expect("Event tool");
			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(32, 24));
			FakeToolHost host(document);
			ToolManager tools(host);
			tools.SetActive(EditorToolType::Event);
			EditorTool& tool = *tools.GetActive();
			CommandHistory& history = host.GetHistory();
			host.GetEventBrush() = EventStamp::FromEvent(EventCatalog::MakeDefault(EventType::Gem));
			EventSelection& selection = host.GetEventSelection();

			host.Click(tool, Vector2i(5, 5));
			expect(document.GetEvent(5, 5).Type == std::uint16_t(EventType::Gem) && CountEvents(document) == 1 && history.GetDoneCount() == 1, "an event placed");
			host.Drag(tool, Vector2i(5, 6), Vector2i(8, 6));
			expect(CountEvents(document) == 5 && document.GetEvent(7, 6).Type == std::uint16_t(EventType::Gem) && history.GetDoneCount() == 2,
				"events placed along a drag, as one step");
			host.Click(tool, Vector2i(5, 5), MouseButton::Right);
			expect(LevelDocument::IsEmptyEvent(document.GetEvent(5, 5)) && CountEvents(document) == 4 && history.GetDoneCount() == 3, "an event erased");

			// A click on an event selects it
			host.Click(tool, Vector2i(6, 6));
			expect(selection.GetCellCount() == 1 && selection.Contains(GetCellIndex(document, Vector2i(6, 6))) && history.GetDoneCount() == 3, "an event selected");

			// Erased while dragged, they're back when it's canceled, the selected one selected again
			tool.OnPointerDown(host.MakePointer(Vector2i(6, 6), MouseButton::Right));
			tool.OnPointerMove(host.MakePointer(Vector2i(8, 6), MouseButton::Right));
			expect(tool.IsBusy() && CountEvents(document) == 1 && selection.IsEmpty(), "events erased while dragged");
			tool.Cancel();
			expect(!tool.IsBusy() && CountEvents(document) == 4 && document.GetEvent(8, 6).Type == std::uint16_t(EventType::Gem) && history.GetDoneCount() == 3,
				"canceled erasing leaves the events");
			expect(selection.GetCellCount() == 1 && selection.Contains(GetCellIndex(document, Vector2i(6, 6))), "canceled erasing leaves the selection");

			// The first click away from the selection only ends it, a stroke canceled leaves no event
			host.Click(tool, Vector2i(12, 12));
			expect(selection.IsEmpty() && LevelDocument::IsEmptyEvent(document.GetEvent(12, 12)), "a click away ends the selection");
			tool.OnPointerDown(host.MakePointer(Vector2i(12, 12)));
			tool.OnPointerMove(host.MakePointer(Vector2i(14, 12)));
			expect(CountEvents(document) == 7, "events placed while dragged");
			tool.Cancel();
			expect(CountEvents(document) == 4 && history.GetDoneCount() == 3, "canceled placing leaves no event");

			// Undone, the events selected and gone aren't selected anymore
			host.Click(tool, Vector2i(5, 6));
			expect(selection.Contains(GetCellIndex(document, Vector2i(5, 6))), "a placed event selected");
			expect(host.Undo() && host.Undo() && selection.IsEmpty() && CountEvents(document) == 1, "undone events not selected");
			return expect.GetProblems();
		}

		// A line across a large layer paints each of its tiles once, the part of it outside of the layer is left out, and
		// a shape far larger than a layer could be paints nothing
		std::int32_t TestShape()
		{
			Expect expect("Shape tool");
			LevelDocument document;
			document.CreateNew("test"_s, 100, Vector2i(4096, 64));
			FakeToolHost host(document);
			ToolManager tools(host);
			tools.SetActive(EditorToolType::Shape);
			tools.GetSettings().Shape = ToolShape::Line;
			EditorTool& shape = *tools.GetActive();
			host.SetCurrentLayer(0);
			host.SetStamp(TileStamp::FromTile(TileA));
			const DocumentLayer& layer = document.Layers[0];

			host.Drag(shape, Vector2i(0, 0), Vector2i(4095, 63));
			expect(CountTiles(layer, TileA) == 4096 && layer.GetTile(0, 0) == TileA && layer.GetTile(4095, 63) == TileA && host.GetHistory().GetDoneCount() == 1,
				"a line across a large layer");
			host.Drag(shape, Vector2i(4000, 20), Vector2i(100000, 20));
			expect(CountTiles(layer, TileA) == 4096 + 96 && layer.GetTile(4095, 20) == TileA && host.GetHistory().GetDoneCount() == 2,
				"a line out of the layer");
			host.Drag(shape, Vector2i(0, 30), Vector2i(3000000, 30));
			expect(CountTiles(layer, TileA) == 4096 + 96 && host.GetHistory().GetDoneCount() == 2 && !shape.IsBusy(), "a line far too long");

			// Erased with the right button, and all of it undone
			host.Drag(shape, Vector2i(0, 0), Vector2i(4095, 63), MouseButton::Right);
			expect(CountTiles(layer, TileA) == 96, "a line erased");
			expect(host.Undo() && host.Undo() && host.Undo() && CountTiles(layer, TileA) == 0, "the lines undone");
			return expect.GetProblems();
		}

		// Tiles copied from a level are found in another one by the tile sets they're of, in whatever slots it has them,
		// tiles of a tile set it hasn't, of one a level has in itself, and animated tiles it hasn't are left out
		std::int32_t TestStampClipboard()
		{
			Expect expect("Stamp clipboard");
			auto addTileSet = [](LevelDocument& document, StringView name, std::uint16_t offset, std::uint16_t count, bool embedded) {
				Formats::ExtraTileSetRecord tileSet = {};
				tileSet.Name = name;
				tileSet.Offset = offset;
				tileSet.Count = count;
				tileSet.Flags = (embedded ? Formats::ExtraTileSetRecord::Embedded : 0);
				document.ExtraTileSets.push_back(Death::move(tileSet));
			};

			LevelDocument source;
			source.CreateNew("jungle"_s, 100, Vector2i(16, 16));
			addTileSet(source, "extra"_s, 0, 50, false);
			addTileSet(source, "shared"_s, 10, 20, false);
			addTileSet(source, ":level"_s, 0, 4, true);
			source.Animations.emplace_back().Frames.push_back(TileA);

			LevelDocument target;
			target.CreateNew("Jungle"_s, 100, Vector2i(16, 16));
			addTileSet(target, "shared"_s, 0, 40, false);
			addTileSet(target, ":level"_s, 0, 4, true);

			TileStamp stamp = MakeStamp(6, 1, {
				TileRef{ 7, TileRef::DefaultTileSet, 0 },
				TileRef{ 3, 1, 0 },
				TileRef{ 5, 2, Formats::TileRecord::FlipX },
				TileRef{ 2, 3, 0 },
				TileRef{ 0, TileRef::Animated, 0 },
				TileRef{ 1, TileRef::DefaultTileSet, 0 }
			});
			stamp.Present[5] = false;
			String text = StampClipboard::WriteTiles(stamp, &source);

			TileStamp same;
			bool sameRead = StampClipboard::ReadTiles(text, same, &source);
			bool sameTiles = (sameRead && same.Width == 6 && same.Height == 1);
			for (std::size_t i = 0; sameTiles && i < stamp.Tiles.size(); i++) {
				sameTiles = (same.Present[i] == stamp.Present[i] && (!stamp.Present[i] || same.Tiles[i] == stamp.Tiles[i]));
			}
			expect(sameTiles, "tiles read into the same level are the same");

			TileStamp read;
			expect(StampClipboard::ReadTiles(text, read, &target) && read.Width == 6 && read.Height == 1, "tiles read into another level");
			if (read.Tiles.size() == 6 && read.Present.size() == 6) {
				expect(read.Present[0] && read.Tiles[0] == TileRef{ 7, TileRef::DefaultTileSet, 0 }, "a tile of the default tile set by its name");
				expect(!read.Present[1], "a tile of a tile set the level hasn't left out");
				expect(read.Present[2] && read.Tiles[2] == TileRef{ 15, 1, Formats::TileRecord::FlipX }, "a tile found in another slot and range");
				expect(!read.Present[3], "a tile of a tile set of the other level itself left out");
				expect(read.Present[4] && read.Tiles[4].IsAnimated() && !read.Present[5], "an animated tile kept until it's checked");
				StampClipboard::RemoveUnknownTiles(target, read);
				expect(!read.Present[4] && read.Present[2], "an animated tile the level hasn't left out");
			}

			// Painted into the other level, the tiles left out leave those there as they are
			FakeToolHost host(target);
			ToolManager tools(host);
			host.SetCurrentLayer(0);
			target.Layers[0].GetTile(1, 0) = Wall;
			host.SetStamp(Death::move(read));
			host.Click(*tools.Get(EditorToolType::Brush), Vector2i(3, 0));
			const DocumentLayer& layer = target.Layers[0];
			expect(layer.GetTile(0, 0) == TileRef{ 7, TileRef::DefaultTileSet, 0 } && layer.GetTile(1, 0) == Wall &&
				layer.GetTile(2, 0) == TileRef{ 15, 1, Formats::TileRecord::FlipX } && layer.GetTile(3, 0).IsEmpty(), "the tiles pasted");
			return expect.GetProblems();
		}

		// The level shown as the editor loads it, see ContentResolver::LoadLevel(), but only its layers: the tile set isn't
		// there, its tiles are known only by their IDs
		std::unique_ptr<Tiles::TileMap> LoadShownLevel(const LevelDocument& document)
		{
			MemoryStream saved;
			if (!document.Save(saved, true)) {
				return nullptr;
			}
			saved.Seek(0, SeekOrigin::Begin);
			Formats::LevelHeader header;
			if (!Formats::LevelFile::ReadHeader(saved, header)) {
				return nullptr;
			}
			Formats::LevelReader reader(saved, header);
			Formats::LevelProperties properties;
			reader.ReadProperties(properties);
			std::uint32_t palette[Formats::LevelFile::ColorsPerPalette];
			reader.ReadPalettes(palette, nullptr);
			SmallVector<Formats::ExtraTileSetRecord, 0> extraTileSets;
			reader.ReadExtraTileSets(extraTileSets);
			reader.ReadTileDiffuseOverrides(nullptr);
			reader.ReadTileMaskOverrides(nullptr);
			SmallVector<String, 0> texts;
			reader.ReadTexts(texts);
			std::uint16_t animatedTilesOffset = 0;
			SmallVector<Formats::AnimatedTileRecord, 0> animatedTiles;
			reader.ReadAnimatedTiles(animatedTilesOffset, animatedTiles);

			auto tileMap = std::make_unique<Tiles::TileMap>(properties.DefaultTileSet, properties.CaptionTileId, false);
			tileMap->SetAnimatedTiles(animatedTilesOffset, animatedTiles);
			std::int32_t layerCount = reader.ReadLayerCount();
			for (std::int32_t i = 0; i < layerCount && reader.IsValid(); i++) {
				tileMap->LoadLayer(reader);
			}
			if (!reader.IsValid() || tileMap->HasLoadFailed()) {
				return nullptr;
			}
			return tileMap;
		}

		// Whether the level shown has the tiles the level would have if it was loaded again
		bool IsShownAsLoaded(const LevelDocument& document, const Tiles::TileMap& shown)
		{
			std::unique_ptr<Tiles::TileMap> loaded = LoadShownLevel(document);
			if (loaded == nullptr || loaded->GetLayerCount() != shown.GetLayerCount()) {
				return false;
			}
			for (std::int32_t i = 0; i < loaded->GetLayerCount(); i++) {
				Vector2i size = loaded->GetLayerSize(i);
				if (shown.GetLayerSize(i) != size) {
					return false;
				}
				for (std::int32_t y = 0; y < size.Y; y++) {
					for (std::int32_t x = 0; x < size.X; x++) {
						if (shown.GetTile(i, x, y) != loaded->GetTile(i, x, y)) {
							LOGE("RuntimeSync: layer {} shows {} at {}, {} instead of {}", i, shown.GetTile(i, x, y), x, y, loaded->GetTile(i, x, y));
							return false;
						}
					}
				}
			}
			return true;
		}
	}

	std::int32_t TestTools()
	{
		TimeStamp start = TimeStamp::now();
		std::int32_t problems = TestBrush();
		problems += TestRectangle();
		problems += TestFill();
		problems += TestEyedropper();
		problems += TestSelectMove();
		problems += TestEvents();
		problems += TestShape();
		problems += TestStampClipboard();
		if (problems == 0) {
			LOGI("Tools test passed in {:.1f} ms", start.millisecondsSince());
		}
		return problems;
	}

	std::int32_t TestRuntimeSync()
	{
		Expect expect("RuntimeSync");
		// The tile set of the level isn't anywhere, the level is shown without it
		LOGI("RuntimeSync test: levels are shown without their tile set, failing to load it is expected");

		LevelDocument document;
		document.CreateNew("selftest-no-tileset"_s, 100, Vector2i(24, 16));
		document.Layers[0].GetTile(2, 3) = TileB;
		std::unique_ptr<Tiles::TileMap> shown = LoadShownLevel(document);
		expect(shown != nullptr && shown->GetLayerCount() == std::int32_t(document.Layers.size()), "the level shown");
		if (shown == nullptr) {
			return expect.GetProblems();
		}

		// Tiles of more layers changed, undone and redone, are shown in place
		CommandHistory history(document, SIZE_MAX);
		DocumentChanges changes;
		auto paint = std::make_unique<SetTilesCommand>(document, 0, "Paint"_s);
		paint->Set(1, 1, TileA);
		paint->Set(2, 3, TileRef{});
		history.Execute(Death::move(paint), changes);
		paint = std::make_unique<SetTilesCommand>(document, 2, "Paint"_s);
		paint->Set(31, 31, TileD);
		history.Execute(Death::move(paint), changes);
		expect(RuntimeSync::Apply(document, *shown, changes), "tiles applied in place");
		expect(shown->GetTile(0, 1, 1) == TileA.Index && shown->GetTile(0, 2, 3) == 0, "the tiles shown");
		changes.Clear();
		history.Undo(changes);
		history.Undo(changes);
		history.Redo(changes);
		expect(RuntimeSync::Apply(document, *shown, changes), "tiles undone and redone applied in place");
		expect(IsShownAsLoaded(document, *shown), "tiles shown as loaded");

		// Layers only moved are moved in place
		changes.Clear();
		history.Execute(std::make_unique<MoveLayerCommand>(0, 2, "Move layer"_s), changes);
		expect(RuntimeSync::Apply(document, *shown, changes), "a layer moved in place");
		expect(IsShownAsLoaded(document, *shown), "moved layers shown as loaded");

		// A layer hidden in the editor isn't shown, the level isn't changed by it
		DocumentLayer& hidden = document.Layers[1];
		hidden.EditorFlags = DocumentLayer::EditorHidden;
		changes.Clear();
		changes.Add(DocumentChangeKind::EditorState);
		expect(RuntimeSync::Apply(document, *shown, changes) && !shown->IsLayerVisible(1) && shown->IsLayerVisible(0), "a hidden layer");
		hidden.EditorFlags = 0;
		expect(RuntimeSync::Apply(document, *shown, changes) && shown->IsLayerVisible(1), "a layer shown again");

		// Other tile sets need the level loaded again
		changes.Clear();
		changes.Add(DocumentChangeKind::TileSets);
		expect(!RuntimeSync::Apply(document, *shown, changes), "other tile sets aren't applied in place");

		// The animated tiles move up as the last tile of the tile set is placed, the tiles showing them follow, see
		// ExecuteGrowingTileIdSpace()
		LevelDocument growing;
		growing.CreateNew("selftest-no-tileset"_s, 20, Vector2i(8, 8));
		growing.Animations.emplace_back().Frames.push_back(TileRef{ 2, TileRef::DefaultTileSet, 0 });
		growing.TileIdSpace = 20;
		growing.Layers[0].GetTile(1, 1) = TileRef{ 0, TileRef::Animated, 0 };
		std::unique_ptr<Tiles::TileMap> growingShown = LoadShownLevel(growing);
		expect(growingShown != nullptr, "a level with animated tiles shown");
		if (growingShown != nullptr) {
			CommandHistory growingHistory(growing, SIZE_MAX);
			changes.Clear();
			paint = std::make_unique<SetTilesCommand>(growing, 0, "Paint"_s);
			paint->Set(3, 2, TileRef{ 19, TileRef::DefaultTileSet, 0 });
			ExecuteGrowingTileIdSpace(growingHistory, Death::move(paint), changes);
			expect(changes.HasAny(DocumentChangeKind::AnimationStructure) && RuntimeSync::Apply(growing, *growingShown, changes), "animated tiles moved in place");
			expect((growingShown->GetTile(0, 1, 1) & Tiles::TileMap::TileFlagAnimated) != 0 && growingShown->GetTile(0, 3, 2) == 19,
				"the animated tile and the last tile shown");
			expect(IsShownAsLoaded(growing, *growingShown), "moved animated tiles shown as loaded");
		}

		if (expect.GetProblems() == 0) {
			LOGI("RuntimeSync test passed");
		}
		return expect.GetProblems();
	}
}

#endif
