#include "PrefabsDialog.h"

#if defined(WITH_EDITOR)

#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../Runtime/TileDrawing.h"
#include "../Smart/TilePrefabs.h"
#include "../Tools/ToolActions.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../UI/UiVirtualList.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		struct PrefabsState
		{
			String TileSet;
			SmallVector<TilePrefab, 0> Prefabs;
			std::uint32_t Revision = 1;
		};

		/** The tiles of a prefab, small */
		class PrefabPreview : public UiElement
		{
		public:
			PrefabPreview(IEditorContext& session, std::shared_ptr<PrefabsState> state)
				: _session(session), _state(Death::move(state)), _index(-1)
			{
			}

			/** Sets the prefab shown, -1 for none */
			void SetIndex(std::int32_t index)
			{
				_index = index;
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(context.Px(64.0f), context.Px(40.0f));
			}

			void Draw(UiPainter& painter) override
			{
				painter.FillRect(Bounds, Colorf(0.0f, 0.0f, 0.0f, 0.35f), UiLayer::Fill);
				Tiles::TileMap* tileMap = _session.GetTileMap();
				if (tileMap == nullptr || _index < 0 || _index >= std::int32_t(_state->Prefabs.size())) {
					return;
				}
				const LevelDocument& document = _session.GetDocument();
				TileStamp stamp = _state->Prefabs[_index].ToStamp(document);
				if (stamp.IsEmpty()) {
					return;
				}
				float cell = std::floor(std::min(Bounds.W / stamp.Width, Bounds.H / stamp.Height));
				cell = std::max(cell, 1.0f);
				float x0 = Bounds.X + std::floor((Bounds.W - cell * stamp.Width) * 0.5f);
				float y0 = Bounds.Y + std::floor((Bounds.H - cell * stamp.Height) * 0.5f);
				for (std::int32_t y = 0; y < stamp.Height; y++) {
					for (std::int32_t x = 0; x < stamp.Width; x++) {
						if (!stamp.IsPresent(x, y)) {
							continue;
						}
						const TileRef& tile = stamp.GetTile(x, y);
						if (!tile.IsEmpty()) {
							DrawTile(painter, *tileMap, document, tile, tile.Flags, Rectf(x0 + x * cell, y0 + y * cell, cell, cell), 1.0f, UiLayer::Content);
						}
					}
				}
			}

		private:
			IEditorContext& _session;
			std::shared_ptr<PrefabsState> _state;
			std::int32_t _index;
		};

		/** Row of the list of prefabs, shows the prefab it's bound to and acts on it, see UiVirtualList::CreateRow */
		class PrefabRow : public UiStack
		{
		public:
			// The space below it is the one between the rows of a stack
			static constexpr float BottomSpace = 2.0f;

			PrefabRow(IEditorContext& session, std::shared_ptr<PrefabsState> state)
				: UiStack(UiOrientation::Horizontal, UiTheme::Spacing, UiThickness(0.0f, 0.0f, 0.0f, BottomSpace)), _state(state), _index(-1)
			{
				IEditorContext* s = &session;
				PrefabRow* self = this;
				_preview = Add<PrefabPreview>(session, state);
				_name = Add<UiLabel>();
				_name->Flex = 1.0f;
				auto* use = Add<UiButton>(_("Use"), EditorIcon::Brush, [s, state, self]() {
					std::int32_t i = self->_index;
					if (i < 0 || i >= std::int32_t(state->Prefabs.size())) {
						return;
					}
					// A prefab of more layers or with events is pasted with them, see ToolActions::UsePrefab()
					if (!ToolActions::UsePrefab(*s, state->Prefabs[i])) {
						s->ShowMessage(_("The level doesn't have the tile sets of the prefab"));
					}
				});
				use->Tooltip = _("Paint with it");
				auto* remove = Add<UiButton>(StringView(), EditorIcon::Delete, [s, state, self]() {
					std::int32_t i = self->_index;
					if (i < 0 || i >= std::int32_t(state->Prefabs.size())) {
						return;
					}
					state->Prefabs.erase(state->Prefabs.begin() + i);
					if (!TilePrefab::Save(state->TileSet, state->Prefabs)) {
						s->ShowMessage(_("The prefabs couldn't be saved"));
					}
					state->Revision++;
				});
				remove->Tooltip = _("Remove the prefab");
			}

			/** Binds the row to a prefab */
			void Bind(std::int32_t index)
			{
				_index = index;
				_preview->SetIndex(index);
				_name->Text = (index >= 0 && index < std::int32_t(_state->Prefabs.size()) ? _state->Prefabs[index].Name : String());
			}

		private:
			std::shared_ptr<PrefabsState> _state;
			std::int32_t _index;
			PrefabPreview* _preview;
			UiLabel* _name;
		};

		/** The prefabs, only the rows seen have elements, the rows are bound again when the prefabs change */
		class PrefabList : public UiVirtualList
		{
		public:
			PrefabList(IEditorContext& session, std::shared_ptr<PrefabsState> state)
				: _state(Death::move(state)), _shownRevision(0)
			{
				// As high as the preview, and the space below it
				RowHeight = 40.0f + PrefabRow::BottomSpace;
				// The rows are only used by their buttons
				Selectable = false;
				EmptyText = _("The tile set has no prefabs yet");
				CountFunc = [this]() { return std::int32_t(_state->Prefabs.size()); };
				IEditorContext* s = &session;
				std::shared_ptr<PrefabsState> rowState = _state;
				CreateRow = [s, rowState]() -> std::unique_ptr<UiElement> {
					return std::make_unique<PrefabRow>(*s, rowState);
				};
				BindRow = [](UiElement& row, std::int32_t index) {
					static_cast<PrefabRow&>(row).Bind(index);
				};
			}

			void Update(float timeMult) override
			{
				if (_shownRevision != _state->Revision) {
					_shownRevision = _state->Revision;
					RebindRows();
				}
				UiVirtualList::Update(timeMult);
			}

		private:
			std::shared_ptr<PrefabsState> _state;
			std::uint32_t _shownRevision;
		};
	}

	void ShowPrefabsDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		IEditorContext* s = &session;
		auto state = std::make_shared<PrefabsState>();
		state->TileSet = session.GetDocument().Properties.DefaultTileSet;
		TilePrefab::Load(state->TileSet, state->Prefabs);

		auto dialog = std::make_unique<UiDialog>(_("Prefabs"));
		auto* content = dialog->GetContent();
		content->Add<UiLabel>(_f("Prefabs of tile set \"{}\", for any level of it", state->TileSet), UiTheme::TextDim);
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 280.0f;
		scroll->Height = 200.0f;
		scroll->SetContent<PrefabList>(session, state);

		content->Add<UiSeparator>();
		auto* row = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		auto* nameBox = row->Add<UiTextBox>();
		nameBox->Flex = 1.0f;
		nameBox->MaxLength = 48;
		nameBox->Placeholder = _("Name of the prefab");
		auto* save = row->Add<UiButton>(_("Save Brush"), EditorIcon::Plus, [s, state, nameBox]() {
			String name = nameBox->Text.trimmed();
			if (name.empty()) {
				s->ShowMessage(_("Name the prefab first"));
				return;
			}
			const TileStamp& stamp = s->GetStamp();
			if (stamp.IsEmpty()) {
				s->ShowMessage(_("The brush is empty"));
				return;
			}
			TilePrefab prefab = TilePrefab::FromStamp(s->GetDocument(), stamp, name);
			bool droppedTiles = (prefab.DroppedTiles > 0);
			// One of the same name is replaced
			auto it = std::find_if(state->Prefabs.begin(), state->Prefabs.end(), [&name](const TilePrefab& p) { return p.Name == name; });
			if (it != state->Prefabs.end()) {
				*it = Death::move(prefab);
			} else {
				state->Prefabs.push_back(Death::move(prefab));
			}
			if (!TilePrefab::Save(state->TileSet, state->Prefabs)) {
				s->ShowMessage(_("The prefabs couldn't be saved"));
				return;
			}
			state->Revision++;
			nameBox->Text = String();
			// Tiles of a tile set the level has in itself aren't in any other level, the prefab has empty cells there
			s->ShowMessage(droppedTiles
				? _f("Prefab \"{}\" saved with the tile set, the tiles of this level itself were left out", name)
				: _f("Prefab \"{}\" saved with the tile set", name));
		});
		save->Tooltip = _("Save the brush as a prefab of the tile set, the rules of the tile set in the user content keep it");
		auto* saveLayers = content->Add<UiButton>(_("Save Copy of All Layers"), EditorIcon::Layers, [s, state, nameBox]() {
			String name = nameBox->Text.trimmed();
			if (name.empty()) {
				s->ShowMessage(_("Name the prefab first"));
				return;
			}
			const LayeredStamp& copied = ToolActions::GetLayeredClipboard();
			if (copied.IsEmpty()) {
				s->ShowMessage(_("Copy tiles of all layers first"));
				return;
			}
			// The layer being edited is the one the prefab is used on, the others go with it
			TilePrefab prefab = TilePrefab::FromLayered(s->GetDocument(), copied, ToolActions::GetCurrentLayerFromSprite(*s), name);
			bool droppedTiles = (prefab.DroppedTiles > 0);
			auto it = std::find_if(state->Prefabs.begin(), state->Prefabs.end(), [&name](const TilePrefab& p) { return p.Name == name; });
			if (it != state->Prefabs.end()) {
				*it = Death::move(prefab);
			} else {
				state->Prefabs.push_back(Death::move(prefab));
			}
			if (!TilePrefab::Save(state->TileSet, state->Prefabs)) {
				s->ShowMessage(_("The prefabs couldn't be saved"));
				return;
			}
			state->Revision++;
			nameBox->Text = String();
			// Tiles of a tile set the level has in itself aren't in any other level, the prefab has empty cells there
			s->ShowMessage(droppedTiles
				? _f("Prefab \"{}\" saved with the tile set, the tiles of this level itself were left out", name)
				: _f("Prefab \"{}\" saved with the tile set", name));
		});
		saveLayers->Tooltip = _("Save what Copy All Layers copied last as a prefab, with its layers and events");

		ShowModeless(*surface, Death::move(dialog));
	}
}

#endif
