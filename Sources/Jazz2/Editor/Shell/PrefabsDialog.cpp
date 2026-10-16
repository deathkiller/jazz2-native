#include "PrefabsDialog.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Smart/TilePrefabs.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
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
			PrefabPreview(EditorSession& session, std::shared_ptr<PrefabsState> state, std::int32_t index)
				: _session(session), _state(std::move(state)), _index(index)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(context.Px(64.0f), context.Px(40.0f));
			}

			void Draw(UiPainter& painter) override
			{
				painter.FillRect(Bounds, Colorf(0.0f, 0.0f, 0.0f, 0.35f), UiLayer::Fill);
				Tiles::TileMap* tileMap = _session.GetTileMap();
				if (tileMap == nullptr || _index >= std::int32_t(_state->Prefabs.size())) {
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
						std::int32_t tileId = document.ToTileId(tile);
						if (tileId > 0) {
							painter.Tile(*tileMap, tileId, tile.Flags, Rectf(x0 + x * cell, y0 + y * cell, cell, cell), 1.0f, UiLayer::Content);
						}
					}
				}
			}

		private:
			EditorSession& _session;
			std::shared_ptr<PrefabsState> _state;
			std::int32_t _index;
		};

		/** The prefabs, made again when they change */
		class PrefabList : public UiStack
		{
		public:
			PrefabList(EditorSession& session, std::shared_ptr<PrefabsState> state)
				: UiStack(UiOrientation::Vertical, 2.0f), _session(session), _state(std::move(state)), _shownRevision(0)
			{
			}

			void Update(float timeMult) override
			{
				if (_shownRevision != _state->Revision) {
					_shownRevision = _state->Revision;
					Rebuild();
				}
				UiStack::Update(timeMult);
			}

		private:
			EditorSession& _session;
			std::shared_ptr<PrefabsState> _state;
			std::uint32_t _shownRevision;

			void Rebuild()
			{
				ClearChildren();
				if (_state->Prefabs.empty()) {
					Add<UiLabel>(_("The tile set has no prefabs yet"), UiTheme::TextDim);
					return;
				}
				EditorSession* s = &_session;
				std::shared_ptr<PrefabsState> state = _state;
				for (std::int32_t i = 0; i < std::int32_t(_state->Prefabs.size()); i++) {
					auto* row = Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
					row->Add<PrefabPreview>(_session, _state, i);
					auto* name = row->Add<UiLabel>(_state->Prefabs[i].Name);
					name->Flex = 1.0f;
					auto* use = row->Add<UiButton>(_("Use"), EditorIcon::Brush, [s, state, i]() {
						if (i >= std::int32_t(state->Prefabs.size())) {
							return;
						}
						TileStamp stamp = state->Prefabs[i].ToStamp(s->GetDocument());
						if (stamp.IsEmpty() || std::find(stamp.Present.begin(), stamp.Present.end(), true) == stamp.Present.end()) {
							s->ShowMessage(_("The level doesn't have the tile sets of the prefab"));
							return;
						}
						s->SetStamp(std::move(stamp));
						s->GetTools().SetActive(EditorToolType::Brush);
					});
					use->Tooltip = _("Paint with it");
					auto* remove = row->Add<UiButton>(StringView(), EditorIcon::Delete, [s, state, i]() {
						if (i >= std::int32_t(state->Prefabs.size())) {
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
			}
		};
	}

	void ShowPrefabsDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto state = std::make_shared<PrefabsState>();
		state->TileSet = session.GetDocument().Properties.DefaultTileSet;
		TilePrefab::Load(state->TileSet, state->Prefabs);

		auto dialog = std::make_unique<UiDialog>(_("Prefabs"));
		UiDialog* dialogPtr = dialog.get();
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
			// One of the same name is replaced
			auto it = std::find_if(state->Prefabs.begin(), state->Prefabs.end(), [&name](const TilePrefab& p) { return p.Name == name; });
			if (it != state->Prefabs.end()) {
				*it = std::move(prefab);
			} else {
				state->Prefabs.push_back(std::move(prefab));
			}
			if (!TilePrefab::Save(state->TileSet, state->Prefabs)) {
				s->ShowMessage(_("The prefabs couldn't be saved"));
				return;
			}
			state->Revision++;
			nameBox->Text = String();
			s->ShowMessage(_f("Prefab \"{}\" saved with the tile set", name));
		});
		save->Tooltip = _("Save the brush as a prefab of the tile set, the rules of the tile set in the user content keep it");

		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
