#include "ShellCommon.h"

#if defined(WITH_EDITOR)

#include "EditorShell.h"
#include "../Tools/EditorContext.h"
#include "../Document/LevelDocument.h"
#include "../Tools/EventTools.h"
#include "../Input/ActionRegistry.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../GameDifficulty.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/I18n.h"

#include <Containers/StringUtils.h>

#include <cstdio>
#include <cstring>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	UiMenuEntry MakeMenuEntry(EditorAction& action)
	{
		UiMenuEntry entry;
		entry.Label = action.Label;
		entry.Icon = action.Icon;
		entry.ShortcutText = action.Key.ToString();
		EditorAction* actionPtr = &action;
		entry.Execute = [actionPtr]() {
			if (actionPtr->CanExecute()) {
				actionPtr->Execute();
			}
		};
		entry.IsEnabled = [actionPtr]() {
			return actionPtr->CanExecute();
		};
		if (action.IsChecked) {
			entry.IsChecked = [actionPtr]() {
				return actionPtr->IsChecked();
			};
		}
		return entry;
	}

	String FormatInteger(std::int32_t value)
	{
		char buffer[16];
		std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%d", value);
		return String(buffer, std::size_t(std::max(length, 0)));
	}

	StringView GetUnitSuffix(ParamUnit unit)
	{
		switch (unit) {
			// TRANSLATORS: Unit after the value of a parameter of an event, short for pixels, keep it short
			case ParamUnit::Pixels: return _x("editor", "px");
			// TRANSLATORS: Unit after the value of a parameter of an event, a count of tiles, keep it short
			case ParamUnit::Tiles: return _x("editor", "tiles");
			// TRANSLATORS: Unit after the value of a parameter of an event, a count of game frames, keep it short
			case ParamUnit::Frames: return _x("editor", "frames");
			// TRANSLATORS: Unit after the value of a parameter of an event, symbol for seconds
			case ParamUnit::Seconds: return _x("editor", "s");
			case ParamUnit::Percent: return "%"_s;
			// TRANSLATORS: Unit after the value of a parameter of an event, short for degrees of an angle
			case ParamUnit::Degrees: return _x("editor", "deg");
			default: return {};
		}
	}

	UiStack* AddLabeledRow(UiElement* parent, StringView label, StringView tooltip, float labelWidth)
	{
		if (auto* form = runtime_cast<UiForm>(parent)) {
			return form->AddRow(label, tooltip);
		}
		auto* row = parent->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		auto* labelElement = row->Add<UiLabel>(label, UiTheme::TextDim);
		labelElement->Width = labelWidth;
		labelElement->Tooltip = tooltip;
		return row;
	}

	bool MatchesSearch(StringView text, StringView search)
	{
		if (search.empty()) {
			return true;
		}
		String lowerText = StringUtils::lowercase(text);
		String lowerSearch = StringUtils::lowercase(search);
		StringView rest = lowerSearch;
		while (!rest.empty()) {
			auto parts = rest.partition(' ');
			if (!parts[0].empty() && !lowerText.contains(parts[0])) {
				return false;
			}
			rest = parts[2];
		}
		return true;
	}

	const EventParamField* FindEventField(const EventDescriptor* descriptor, ParamType type)
	{
		if (descriptor != nullptr) {
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				if (descriptor->Fields[i].Type == type) {
					return &descriptor->Fields[i];
				}
			}
		}
		return nullptr;
	}

	std::uint8_t GetDifficultyFlag(std::int32_t difficulty)
	{
		if (difficulty < std::int32_t(GameDifficulty::Easy) || difficulty > std::int32_t(GameDifficulty::Hard)) {
			return 0;
		}
		return std::uint8_t(Formats::EventRecord::Easy << (difficulty - std::int32_t(GameDifficulty::Easy)));
	}

	bool GetPrimaryEvent(IEditorContext& session, Formats::EventRecord& event)
	{
		const EventSelection& selection = session.GetEventSelection();
		const LevelDocument& document = session.GetDocument();
		if (!selection.IsEmpty()) {
			// The first selected on the grid, then the first off it
			if (selection.HasCells()) {
				Vector2i gridSize = document.GetEventGridSize();
				if (gridSize.X > 0) {
					std::uint32_t cell = selection.GetCells()[0];
					event = document.GetEvent(std::int32_t(cell % std::uint32_t(gridSize.X)), std::int32_t(cell / std::uint32_t(gridSize.X)));
					if (!LevelDocument::IsEmptyEvent(event)) {
						return true;
					}
				}
			}
			for (std::int32_t index : selection.GetOffGrid()) {
				if (index >= 0 && index < std::int32_t(document.OffGridEvents.size())) {
					event = document.OffGridEvents[index].Event;
					return true;
				}
			}
			return false;
		}
		const Formats::EventRecord* first = session.GetEventBrush().GetFirst();
		if (first == nullptr) {
			return false;
		}
		event = *first;
		return true;
	}

	bool IsLayerLocked(const LevelDocument& document, std::int32_t layer)
	{
		return (layer >= 0 && layer < std::int32_t(document.Layers.size()) && (document.Layers[layer].EditorFlags & DocumentLayer::EditorLocked) != 0);
	}

	bool CheckLayerUnlocked(IEditorContext& session, std::int32_t layer)
	{
		if (IsLayerLocked(session.GetDocument(), layer)) {
			session.ShowMessage(_x("editor", "The layer is locked"));
			return false;
		}
		return true;
	}

	void ShowOpenImageDialog(IEditorContext& session, StringView title, Function<void(StringView)>&& onChosen)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}
		SmallVector<UiFileRoot, 4> roots;
		session.GetImageRoots(false, roots);
		UiFileDialog::Show(*surface, title, UiFileDialogMode::Open, Death::move(roots), ".png"_s, {}, {}, Death::move(onChosen));
	}

	UiElement* ShowSingleDialog(IEditorContext& session, const char* id, std::unique_ptr<UiElement> dialog, UiOverlayOptions&& options,
		Function<void()>&& onShownAgain)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return nullptr;
		}
		UiElement* element = dialog.get();
		EditorShell* shell = runtime_cast<EditorShell>(surface->GetRoot());
		if (shell != nullptr) {
			// Forgotten as it's closed, it's destroyed a little later
			Function<void()> onClosed = Death::move(options.OnClosed);
			options.OnClosed = [shell, element, onClosed = Death::move(onClosed)]() mutable {
				shell->ForgetDialog(element);
				if (onClosed) {
					onClosed();
				}
			};
		}
		surface->PushOverlay(Death::move(dialog), Death::move(options));
		if (shell != nullptr) {
			shell->TrackDialog(id, element, Death::move(onShownAgain));
		}
		return element;
	}

	bool ReuseSingleDialog(IEditorContext& session, const char* id, bool close)
	{
		UiSurface* surface = session.GetSurface();
		EditorShell* shell = (surface != nullptr ? runtime_cast<EditorShell>(surface->GetRoot()) : nullptr);
		UiElement* dialog = (shell != nullptr ? shell->FindDialog(id) : nullptr);
		if (dialog == nullptr) {
			return false;
		}
		if (close) {
			surface->CloseOverlay(dialog);
		} else {
			// In front of the other windows, it may have been left behind them
			surface->RaiseOverlay(dialog);
			shell->ShowDialogAgain(dialog);
		}
		return true;
	}

	UiUpdateWatcher::UiUpdateWatcher(Function<void(float)>&& onUpdate)
		: _onUpdate(Death::move(onUpdate))
	{
	}

	Vector2f UiUpdateWatcher::Measure(const UiContext& context, Vector2f available)
	{
		return Vector2f(0.0f, 0.0f);
	}

	void UiUpdateWatcher::Update(float timeMult)
	{
		if (_onUpdate) {
			_onUpdate(timeMult);
		}
		UiElement::Update(timeMult);
	}

	const SmallVectorImpl<Annotation>& AnnotationCache::Get(const LevelDocument& document)
	{
		// The chunk of the current version, or the one of 3.9.0 the level may still have
		const Formats::ChunkRecord* chunk = document.FindChunk(Annotation::Chunk);
		if (chunk == nullptr) {
			chunk = document.FindChunk(Annotation::LegacyChunk);
		}
		std::size_t size = (chunk != nullptr ? chunk->Data.size() : 0);
		// Asked for in every frame, by each row of a list of notes too, so the bytes are compared once a frame at most, a
		// change is seen in the next frame then
		std::uint32_t frame = theApplication().GetFrameCount();
		if (_valid && frame == _checkedFrame && &document == _document && chunk == _chunk && size == _data.size()) {
			return _annotations;
		}
		_checkedFrame = frame;
		_document = &document;
		_chunk = chunk;
		std::uint32_t tag = (chunk != nullptr ? chunk->Tag : 0);
		bool same =(_valid && _tag == tag && size == _data.size() && (size == 0 || std::memcmp(chunk->Data.data(), _data.data(), size) == 0));
		if (!same) {
			_valid = true;
			_tag = tag;
			_data.clear();
			if (size > 0) {
				_data.resize_for_overwrite(size);
				std::memcpy(_data.data(), chunk->Data.data(), size);
			}
			_annotations.clear();
			Annotation::Read(document, _annotations);
			_version++;
		}
		return _annotations;
	}
}

#endif
