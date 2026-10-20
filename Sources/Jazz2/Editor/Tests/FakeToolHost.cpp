#include "FakeToolHost.h"

#if defined(WITH_EDITOR) && defined(DEATH_DEBUG)

#include "../Document/Commands.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float TileSize = 32.0f;
	}

	FakeToolHost::FakeToolHost(LevelDocument& document)
		: _document(document), _history(document, SIZE_MAX), _currentLayer(std::max<std::int32_t>(document.GetSpriteLayerIndex(), 0))
	{
	}

	FakeToolHost::~FakeToolHost()
	{
	}

	LevelDocument& FakeToolHost::GetDocument()
	{
		return _document;
	}

	std::int32_t FakeToolHost::GetCurrentLayer() const
	{
		return _currentLayer;
	}

	bool FakeToolHost::IsLayerEditable(std::int32_t layer) const
	{
		return (layer >= 0 && layer < std::int32_t(_document.Layers.size()) &&
			(_document.Layers[layer].EditorFlags & (DocumentLayer::EditorHidden | DocumentLayer::EditorLocked)) == 0);
	}

	LayerView FakeToolHost::GetLayerView(std::int32_t layer)
	{
		LayerView view;
		if (layer < 0 || layer >= std::int32_t(_document.Layers.size())) {
			return view;
		}
		// As the editor has it, only without the speeds and the camera, see EditorSession::GetLayerView()
		const DocumentLayer& documentLayer = _document.Layers[layer];
		view.Size = Vector2i(documentLayer.GetWidth(), documentLayer.GetHeight());
		if (!documentLayer.IsSpriteLayer()) {
			view.RepeatX = (documentLayer.Record.Flags & Formats::LayerRecord::RepeatX) != 0;
			view.RepeatY = (documentLayer.Record.Flags & Formats::LayerRecord::RepeatY) != 0;
		}
		return view;
	}

	TileStamp& FakeToolHost::GetStamp()
	{
		return _stamp;
	}

	void FakeToolHost::SetStamp(TileStamp&& stamp)
	{
		// Painting at random stays on for any other tiles, as in the editor
		bool random = _stamp.Random;
		std::uint32_t seed = _stamp.Seed;
		_stamp = Death::move(stamp);
		_stamp.Random = random;
		_stamp.Seed = seed;
	}

	TileSelection& FakeToolHost::GetSelection()
	{
		return _selection;
	}

	void FakeToolHost::Execute(std::unique_ptr<EditCommand> command, bool allowMerge)
	{
		DocumentChanges changes;
		ExecuteGrowingTileIdSpace(_history, Death::move(command), changes, allowMerge);
		OnChanged(changes);
	}

	void FakeToolHost::BeginChanges(String name)
	{
		_history.BeginTransaction(Death::move(name));
	}

	void FakeToolHost::EndChanges()
	{
		_history.EndTransaction();
	}

	void FakeToolHost::PreviewTile(std::int32_t layer, std::int32_t x, std::int32_t y, const TileRef& tile)
	{
		Previews.push_back(PreviewedTile{ layer, Vector2i(x, y), tile });
	}

	void FakeToolHost::RevertPreview(std::int32_t layer, const Recti& area)
	{
		Reverts.push_back(RevertedArea{ layer, area });
	}

	Tiles::TileMap* FakeToolHost::GetTileMap()
	{
		return nullptr;
	}

	EditorCamera* FakeToolHost::GetCamera()
	{
		return nullptr;
	}

	void FakeToolHost::ShowMessage(StringView text)
	{
		Messages.push_back(text);
	}

	EventStamp& FakeToolHost::GetEventBrush()
	{
		return _eventBrush;
	}

	EventSelection& FakeToolHost::GetEventSelection()
	{
		return _eventSelection;
	}

	EventPreviews& FakeToolHost::GetEventPreviews()
	{
		return _eventPreviews;
	}

	float FakeToolHost::GetAnimTime() const
	{
		return 0.0f;
	}

	UiSurface* FakeToolHost::GetSurface() const
	{
		return nullptr;
	}

	SmartTileModel& FakeToolHost::GetSmartTiles()
	{
		// Never asked to learn anything, there are no tile sets to learn from
		return _smartTiles;
	}

	const TileShapes& FakeToolHost::GetTileShapes()
	{
		return _tileShapes;
	}

	ToolPointer FakeToolHost::MakePointer(Vector2i cell, MouseButton button, std::int32_t modifiers) const
	{
		ToolPointer pointer = MakePointerAt(Vector2f(cell.X * TileSize + TileSize * 0.5f, cell.Y * TileSize + TileSize * 0.5f), button, modifiers);
		pointer.Cell = cell;
		return pointer;
	}

	ToolPointer FakeToolHost::MakePointerAt(Vector2f world, MouseButton button, std::int32_t modifiers) const
	{
		ToolPointer pointer;
		pointer.Screen = world;
		pointer.World = world;
		pointer.Cell = Vector2i(std::int32_t(std::floor(world.X / TileSize)), std::int32_t(std::floor(world.Y / TileSize)));
		pointer.Button = button;
		pointer.Modifiers = modifiers;
		return pointer;
	}

	void FakeToolHost::Click(EditorTool& tool, Vector2i cell, MouseButton button, std::int32_t modifiers)
	{
		ToolPointer pointer = MakePointer(cell, button, modifiers);
		tool.OnPointerDown(pointer);
		tool.OnPointerUp(pointer);
	}

	void FakeToolHost::Drag(EditorTool& tool, Vector2i from, Vector2i to, MouseButton button, std::int32_t modifiers)
	{
		tool.OnPointerDown(MakePointer(from, button, modifiers));
		ToolPointer end = MakePointer(to, button, modifiers);
		tool.OnPointerMove(end);
		tool.OnPointerUp(end);
	}

	bool FakeToolHost::Undo()
	{
		DocumentChanges changes;
		if (!_history.Undo(changes)) {
			return false;
		}
		OnChanged(changes);
		return true;
	}

	bool FakeToolHost::Redo()
	{
		DocumentChanges changes;
		if (!_history.Redo(changes)) {
			return false;
		}
		OnChanged(changes);
		return true;
	}

	void FakeToolHost::ClearRecorded()
	{
		Previews.clear();
		Reverts.clear();
		Messages.clear();
	}

	void FakeToolHost::OnChanged(const DocumentChanges& changes)
	{
		// What EditorSession::ApplyDocumentChanges() does with the selections, the rest is of the shown level
		if (changes.HasAny(DocumentChangeKind::LayerStructure)) {
			_selection.Clear();
			_eventSelection.Clear();
			_currentLayer = std::clamp<std::int32_t>(_currentLayer, 0, std::max<std::int32_t>(std::int32_t(_document.Layers.size()) - 1, 0));
		}
		if (changes.HasAny(DocumentChangeKind::Events)) {
			_eventSelection.DropEmpty(_document);
		}
	}
}

#endif
