#include "TerrainRulesDialog.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Smart/SmartTileModel.h"
#include "../Smart/TileShapes.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace Jazz2::Editor
{
	namespace
	{
		enum class RulesPage : std::uint8_t { Rules, Slopes, Try };

		struct RulesState
		{
			RulesPage Page = RulesPage::Rules;
			float SlopeLeft = 0.0f;
			float SlopeRight = 16.0f;
		};

		void SaveRules(EditorSession& session)
		{
			if (!session.GetSmartTiles().GetRules().Save(session.GetDocument().Properties.DefaultTileSet)) {
				session.ShowMessage(_("The rules couldn't be saved"));
			}
		}

		// The first tile of the brush that can be in a rule, `false` if there's none
		bool GetBrushTile(EditorSession& session, TileRef& tile)
		{
			const TileStamp& stamp = session.GetStamp();
			for (std::size_t i = 0; i < stamp.Tiles.size(); i++) {
				if (stamp.Present[i] && TerrainRules::IsRuleTile(session.GetDocument(), stamp.Tiles[i])) {
					tile = stamp.Tiles[i];
					return true;
				}
			}
			return false;
		}

		/** The patterns of neighbors, each with its tile */
		class PatternGrid : public UiElement
		{
		public:
			static constexpr std::int32_t Columns = 8;
			static constexpr float CellUnits = 40.0f;

			explicit PatternGrid(EditorSession& session)
				: _session(session)
			{
				TerrainRules::GetPatterns(_patterns);
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float cell = context.Px(CellUnits);
				std::int32_t rows = (std::int32_t(_patterns.size()) + Columns - 1) / Columns;
				return Vector2f(cell * Columns, cell * rows);
			}

			void Arrange(const UiContext& context, const Rectf& bounds) override
			{
				UiElement::Arrange(context, bounds);
				// The cells are as large as measured, the space the parent gives can be wider
				_cell = context.Px(CellUnits);
			}

			void Draw(UiPainter& painter) override
			{
				SmartTileModel& model = _session.GetSmartTiles();
				Tiles::TileMap* tileMap = _session.GetTileMap();
				const LevelDocument& document = _session.GetDocument();
				float cell = _cell;
				std::int32_t hovered = (_hovered && GetSurface() != nullptr ? GetIndexAt(GetSurface()->GetPointerPos()) : -1);

				for (std::int32_t i = 0; i < std::int32_t(_patterns.size()); i++) {
					std::uint8_t pattern = _patterns[i];
					Rectf rect = GetCellRect(i, cell);
					painter.FillRect(Rectf(rect.X + 1.0f, rect.Y + 1.0f, rect.W - 2.0f, rect.H - 2.0f),
						i == hovered ? UiTheme::Hover : Colorf(0.0f, 0.0f, 0.0f, 0.25f), UiLayer::Fill);

					// Which of the cells around are solid, small in the corner
					float dot = std::max(2.0f, std::floor(cell * 0.09f));
					float x0 = rect.X + dot, y0 = rect.Y + dot;
					static const std::int32_t Layout[3][3] = { { 0x01, 0x02, 0x04 }, { 0x08, 0, 0x10 }, { 0x20, 0x40, 0x80 } };
					for (std::int32_t y = 0; y < 3; y++) {
						for (std::int32_t x = 0; x < 3; x++) {
							bool center = (x == 1 && y == 1);
							bool solid = center || (pattern & Layout[y][x]) != 0;
							Colorf color = (center ? UiTheme::Accent : (solid ? Colorf(0.85f, 0.75f, 0.55f, 1.0f) : Colorf(0.15f, 0.12f, 0.25f, 1.0f)));
							painter.FillRect(Rectf(x0 + x * dot, y0 + y * dot, dot - 1.0f, dot - 1.0f), color, UiLayer::Content);
						}
					}

					// The tile it gets, chosen by hand or learned
					const SmallVector<TileRef, 0>* ruled = model.GetRules().Find(pattern);
					TileRef tile;
					bool hasTile = (ruled != nullptr);
					if (hasTile) {
						tile = (*ruled)[0];
					} else {
						hasTile = model.Pick(pattern, true, nullptr, nullptr, nullptr, nullptr, 0, tile);
					}
					float size = std::floor(cell * 0.6f);
					Rectf tileRect(rect.X + rect.W - size - 3.0f, rect.Y + rect.H - size - 3.0f, size, size);
					if (hasTile && tileMap != nullptr) {
						painter.Tile(*tileMap, document.ToTileId(tile), tile.Flags, tileRect, ruled != nullptr ? 1.0f : 0.7f, UiLayer::Content);
					}
					if (ruled != nullptr) {
						float thickness = std::max(1.0f, std::round(painter.GetScale()));
						painter.Frame(Rectf(rect.X + 1.0f, rect.Y + 1.0f, rect.W - 2.0f, rect.H - 2.0f), UiTheme::Accent, thickness, UiLayer::Overlay);
						if (ruled->size() > 1) {
							painter.Text(String(std::to_string(ruled->size()).c_str()), Vector2f(rect.X + dot, rect.Y + rect.H - painter.Px(12.0f)),
								UiTheme::TextAccent, Alignment::TopLeft, UiLayer::OverlayText);
						}
					}
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				std::int32_t index = GetIndexAt(event.Pos);
				if (index < 0) {
					return false;
				}
				SmartTileModel& model = _session.GetSmartTiles();
				TerrainRules& rules = model.GetRules();
				std::uint8_t pattern = _patterns[index];
				if (event.Button == MouseButton::Right) {
					rules.Set(pattern, {});
				} else if (event.Button == MouseButton::Left) {
					// The tiles of the brush, of the default tile set or an extra one
					SmallVector<TileRef, 8> tiles;
					const TileStamp& stamp = _session.GetStamp();
					for (std::size_t i = 0; i < stamp.Tiles.size(); i++) {
						if (stamp.Present[i] && TerrainRules::IsRuleTile(_session.GetDocument(), stamp.Tiles[i])) {
							tiles.push_back(stamp.Tiles[i]);
						}
					}
					if (tiles.empty()) {
						_session.ShowMessage(_("Choose tiles of a tile set as the brush first"));
						return false;
					}
					rules.Set(pattern, tiles);
				} else {
					return false;
				}
				SaveRules(_session);
				return false;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				std::int32_t index = GetIndexAt(pos);
				if (index < 0) {
					return {};
				}
				const SmallVector<TileRef, 0>* ruled = _session.GetSmartTiles().GetRules().Find(_patterns[index]);
				_tooltip = (ruled != nullptr
					? String(_fn("Chosen by hand, {} tile - the right button returns the learned one", "Chosen by hand, {} tiles in turns - the right button returns the learned one",
						std::int32_t(ruled->size()), std::int32_t(ruled->size())))
					: String(_("Learned from the levels - a click gives it the tiles of the brush")));
				return _tooltip;
			}

		private:
			EditorSession& _session;
			SmallVector<std::uint8_t, 0> _patterns;
			mutable String _tooltip;
			float _cell = 0.0f;

			Rectf GetCellRect(std::int32_t index, float cell) const
			{
				return Rectf(Bounds.X + (index % Columns) * cell, Bounds.Y + (index / Columns) * cell, cell, cell);
			}

			std::int32_t GetIndexAt(Vector2f pos) const
			{
				if (!Bounds.Contains(pos) || _cell <= 0.0f || pos.X < Bounds.X || pos.Y < Bounds.Y) {
					return -1;
				}
				std::int32_t x = std::int32_t((pos.X - Bounds.X) / _cell), y = std::int32_t((pos.Y - Bounds.Y) / _cell);
				std::int32_t index = y * Columns + x;
				return (x >= 0 && x < Columns && index >= 0 && index < std::int32_t(_patterns.size()) ? index : -1);
			}
		};

		/** A small area to try the rules on, cells are made solid or empty by clicks, and get tiles as the tools would */
		class TryCanvas : public UiElement
		{
		public:
			static constexpr std::int32_t Columns = 14;
			static constexpr std::int32_t Rows = 7;
			static constexpr float CellUnits = 22.0f;

			explicit TryCanvas(EditorSession& session)
				: _session(session), _painting(false), _paintValue(true)
			{
				// A hill to start with
				for (std::int32_t y = 0; y < Rows; y++) {
					for (std::int32_t x = 0; x < Columns; x++) {
						std::int32_t top = (x < 3 ? 5 : (x < 6 ? 3 : (x < 10 ? 2 : 4)));
						_solid[y * Columns + x] = (y >= top && !(x == 7 && y == 3));
					}
				}
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float cell = context.Px(CellUnits);
				return Vector2f(cell * Columns, cell * Rows);
			}

			void Arrange(const UiContext& context, const Rectf& bounds) override
			{
				UiElement::Arrange(context, bounds);
				_cell = context.Px(CellUnits);
			}

			void Draw(UiPainter& painter) override
			{
				SmartTileModel& model = _session.GetSmartTiles();
				Tiles::TileMap* tileMap = _session.GetTileMap();
				const LevelDocument& document = _session.GetDocument();
				painter.FillRect(Rectf(Bounds.X, Bounds.Y, _cell * Columns, _cell * Rows), Colorf(0.06f, 0.06f, 0.14f, 1.0f), UiLayer::Fill);

				// Row by row as the tools go, each cell knows the tiles left of it and above it
				TileRef picked[Columns * Rows];
				bool hasPicked[Columns * Rows] = {};
				for (std::int32_t y = 0; y < Rows; y++) {
					for (std::int32_t x = 0; x < Columns; x++) {
						std::int32_t i = y * Columns + x;
						if (!_solid[i]) {
							continue;
						}
						std::uint8_t neighbors = 0;
						auto solid = [this](std::int32_t cx, std::int32_t cy) {
							// Below the area is ground, the sides and above are open
							if (cy >= Rows) {
								return (cx >= 0 && cx < Columns);
							}
							return (cx >= 0 && cx < Columns && cy >= 0 && _solid[cy * Columns + cx]);
						};
						using N = SmartTileModel::Neighbor;
						if (solid(x - 1, y - 1)) neighbors |= N::UpLeft;
						if (solid(x, y - 1)) neighbors |= N::Up;
						if (solid(x + 1, y - 1)) neighbors |= N::UpRight;
						if (solid(x - 1, y)) neighbors |= N::Left;
						if (solid(x + 1, y)) neighbors |= N::Right;
						if (solid(x - 1, y + 1)) neighbors |= N::DownLeft;
						if (solid(x, y + 1)) neighbors |= N::Down;
						if (solid(x + 1, y + 1)) neighbors |= N::DownRight;
						const TileRef* left = (x > 0 && hasPicked[i - 1] ? &picked[i - 1] : nullptr);
						const TileRef* up = (y > 0 && hasPicked[i - Columns] ? &picked[i - Columns] : nullptr);
						std::uint32_t hash = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u;
						hasPicked[i] = model.Pick(neighbors, true, left, up, nullptr, nullptr, hash, picked[i]);
						Rectf rect(Bounds.X + x * _cell, Bounds.Y + y * _cell, _cell, _cell);
						if (hasPicked[i] && tileMap != nullptr) {
							painter.Tile(*tileMap, document.ToTileId(picked[i]), picked[i].Flags, rect, 1.0f, UiLayer::Content);
						} else {
							painter.FillRect(rect, Colorf(0.5f, 0.4f, 0.3f, 1.0f), UiLayer::Content);
						}
					}
				}
				float thickness = std::max(1.0f, std::round(painter.GetScale()));
				painter.Frame(Rectf(Bounds.X, Bounds.Y, _cell * Columns, _cell * Rows), UiTheme::InputBorder, thickness, UiLayer::Overlay);
				if (_hovered && GetSurface() != nullptr) {
					std::int32_t index = GetIndexAt(GetSurface()->GetPointerPos());
					if (index >= 0) {
						painter.Frame(Rectf(Bounds.X + (index % Columns) * _cell, Bounds.Y + (index / Columns) * _cell, _cell, _cell), UiTheme::Cursor, thickness, UiLayer::Overlay);
					}
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				std::int32_t index = GetIndexAt(event.Pos);
				if (index < 0) {
					return false;
				}
				// A drag makes the cells what the first one became
				_paintValue = !_solid[index];
				_solid[index] = _paintValue;
				_painting = true;
				return true;
			}

			void OnPointerMove(const UiPointerEvent& event) override
			{
				std::int32_t index = GetIndexAt(event.Pos);
				if (_painting && index >= 0) {
					_solid[index] = _paintValue;
				}
			}

			void OnPointerUp(const UiPointerEvent& event, bool inside) override
			{
				_painting = false;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				return _("Click or drag to make cells solid or empty, they get tiles as the smart tools would choose them");
			}

		private:
			EditorSession& _session;
			bool _solid[Columns * Rows];
			float _cell = 0.0f;
			bool _painting;
			bool _paintValue;

			std::int32_t GetIndexAt(Vector2f pos) const
			{
				if (_cell <= 0.0f || pos.X < Bounds.X || pos.Y < Bounds.Y) {
					return -1;
				}
				std::int32_t x = std::int32_t((pos.X - Bounds.X) / _cell), y = std::int32_t((pos.Y - Bounds.Y) / _cell);
				return (x >= 0 && x < Columns && y >= 0 && y < Rows ? y * Columns + x : -1);
			}
		};

		// The surface the masks give a tile, or `false` if they don't make it ground
		bool FindDetectedSurface(EditorSession& session, const TileRef& tile, float& left, float& right)
		{
			for (const TileShapes::Surface& surface : session.GetTileShapes().GetSurfaces()) {
				if (surface.Tile.Index == tile.Index && surface.Tile.Source == tile.Source && surface.Tile.Flags == 0) {
					left = surface.Left;
					right = surface.Right;
					return true;
				}
			}
			return false;
		}
	}

	void ShowTerrainRulesDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto state = std::make_shared<RulesState>();
		auto dialog = std::make_unique<UiDialog>(_("Terrain Rules"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();

		// Pages
		{
			auto* tabs = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			struct PageOption { const char* Label; RulesPage Page; };
			static const PageOption Pages[] = {
				{ N_("Rules"), RulesPage::Rules }, { N_("Slopes"), RulesPage::Slopes }, { N_("Try It"), RulesPage::Try },
			};
			for (const PageOption& option : Pages) {
				RulesPage page = option.Page;
				auto* button = tabs->Add<UiButton>(_(option.Label), EditorIcon::None, [state, page]() { state->Page = page; });
				button->Style = UiButtonStyle::Flat;
				button->Flex = 1.0f;
				button->IsCheckedFunc = [state, page]() { return (state->Page == page); };
			}
		}

		auto* status = content->Add<UiLabel>(StringView(), UiTheme::TextDim);
		status->TextFunc = [s]() {
			SmartTileModel& model = s->GetSmartTiles();
			if (!model.IsReady()) {
				return String(_f("Learning from the levels of the tile set - {}%", std::int32_t(model.GetProgress() * 100.0f)));
			}
			return String(_f("Learned from {} levels, {} chosen by hand", model.GetLevelCount(), model.GetRules().GetCount()));
		};

		// The rules of a terrain
		{
			auto* page = content->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			page->IsVisibleFunc = [state]() { return (state->Page == RulesPage::Rules); };

			auto* terrainRow = page->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			terrainRow->Add<UiLabel>(_("Terrain"), UiTheme::TextDim);
			auto* terrains = terrainRow->Add<UiDropdown>([s]() {
				TerrainRules& rules = s->GetSmartTiles().GetRules();
				return String(rules.GetTerrainName(rules.GetCurrentTerrain()));
			}, [s](SmallVectorImpl<UiMenuEntry>& entries) {
				TerrainRules& rules = s->GetSmartTiles().GetRules();
				for (std::int32_t i = 0; i < rules.GetTerrainCount(); i++) {
					UiMenuEntry entry;
					entry.Label = rules.GetTerrainName(i);
					entry.Execute = [s, i]() { s->GetSmartTiles().GetRules().SetCurrentTerrain(i); };
					entry.IsChecked = [s, i]() { return (s->GetSmartTiles().GetRules().GetCurrentTerrain() == i); };
					entries.push_back(std::move(entry));
				}
			});
			terrains->Flex = 1.0f;
			terrains->Tooltip = _("The tools use the rules of the terrain chosen here");
			auto* nameBox = terrainRow->Add<UiTextBox>();
			nameBox->Width = 80.0f;
			nameBox->Placeholder = _("Name");
			nameBox->TextFunc = [s]() {
				TerrainRules& rules = s->GetSmartTiles().GetRules();
				return String(rules.GetTerrainName(rules.GetCurrentTerrain()));
			};
			nameBox->OnCommit = [s](StringView text) {
				String name = text.trimmed();
				if (name.empty()) {
					return;
				}
				TerrainRules& rules = s->GetSmartTiles().GetRules();
				rules.RenameTerrain(rules.GetCurrentTerrain(), name);
				SaveRules(*s);
			};
			nameBox->Tooltip = _("Name of the terrain, Enter renames it");
			auto* add = terrainRow->Add<UiButton>(StringView(), EditorIcon::Plus, [s]() {
				TerrainRules& rules = s->GetSmartTiles().GetRules();
				std::int32_t index = rules.AddTerrain(_f("Terrain {}", rules.GetTerrainCount() + 1));
				rules.SetCurrentTerrain(index);
				SaveRules(*s);
			});
			add->Style = UiButtonStyle::Flat;
			add->Tooltip = _("Add a terrain with rules of its own, such as a wall or a cloud");
			auto* remove = terrainRow->Add<UiButton>(StringView(), EditorIcon::Delete, [s]() {
				TerrainRules& rules = s->GetSmartTiles().GetRules();
				rules.RemoveTerrain(rules.GetCurrentTerrain());
				SaveRules(*s);
			});
			remove->Style = UiButtonStyle::Flat;
			remove->Tooltip = _("Remove the terrain with its rules");
			remove->IsEnabledFunc = [s]() { return (s->GetSmartTiles().GetRules().GetTerrainCount() > 1); };

			page->Add<UiLabel>(_("Click: the tiles of the brush - Right: the one learned from the levels"), UiTheme::TextDim);
			auto* scroll = page->Add<UiScrollView>();
			scroll->Height = 230.0f;
			scroll->SetContent<PatternGrid>(session);
		}

		// Slope tags of the tiles of ground, for the ground line tool
		{
			auto* page = content->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			page->IsVisibleFunc = [state]() { return (state->Page == RulesPage::Slopes); };
			page->Add<UiLabel>(_("Slopes are found by the masks of the tiles, a tag of the brush tile goes before that"), UiTheme::TextDim);

			auto* brushInfo = page->Add<UiLabel>(StringView(), UiTheme::Text);
			brushInfo->TextFunc = [s]() {
				TileRef tile;
				if (!GetBrushTile(*s, tile) || tile.Source != TileRef::DefaultTileSet) {
					return String(_("Choose a tile of the default tile set as the brush"));
				}
				TerrainRules& rules = s->GetSmartTiles().GetRules();
				if (const TerrainRules::SlopeTag* tag = rules.FindSlope(tile.Index)) {
					return (tag->Excluded ? String(_f("Tile {} is left out of ground", tile.Index))
						: String(_f("Tile {} is tagged, its surface is {} px high at the left and {} px at the right", tile.Index, std::int32_t(tag->Left), std::int32_t(tag->Right))));
				}
				float left, right;
				if (FindDetectedSurface(*s, tile, left, right)) {
					return String(_f("Tile {} is ground by its mask, {} px high at the left and {} px at the right", tile.Index, std::int32_t(std::round(32.0f - left)), std::int32_t(std::round(32.0f - right))));
				}
				return String(_f("Tile {} isn't ground by its mask", tile.Index));
			};

			auto* heights = page->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			heights->Add<UiLabel>(_("Left"), UiTheme::TextDim);
			heights->Add<UiNumberBox>([state]() { return state->SlopeLeft; }, [state](float value, bool) { state->SlopeLeft = value; }, 0.0f, 32.0f)->Width = 40.0f;
			heights->Add<UiLabel>(_("Right"), UiTheme::TextDim);
			heights->Add<UiNumberBox>([state]() { return state->SlopeRight; }, [state](float value, bool) { state->SlopeRight = value; }, 0.0f, 32.0f)->Width = 40.0f;
			auto* fromMask = heights->Add<UiButton>(_("From Mask"), EditorIcon::Mask, [s, state]() {
				TileRef tile;
				float left, right;
				if (GetBrushTile(*s, tile) && FindDetectedSurface(*s, tile, left, right)) {
					state->SlopeLeft = std::round(32.0f - left);
					state->SlopeRight = std::round(32.0f - right);
				}
			});
			fromMask->Style = UiButtonStyle::Flat;
			fromMask->Tooltip = _("Take the heights the mask of the brush tile has");

			auto* actions = page->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			auto withBrushTile = [s](Function<void(TerrainRules&, std::uint16_t)>&& action) {
				auto shared = std::make_shared<Function<void(TerrainRules&, std::uint16_t)>>(std::move(action));
				return [s, shared]() {
					TileRef tile;
					if (!GetBrushTile(*s, tile) || tile.Source != TileRef::DefaultTileSet) {
						s->ShowMessage(_("Choose a tile of the default tile set as the brush first"));
						return;
					}
					(*shared)(s->GetSmartTiles().GetRules(), tile.Index);
					SaveRules(*s);
				};
			};
			actions->Add<UiButton>(_("Tag as Slope"), EditorIcon::Slope, withBrushTile([state](TerrainRules& rules, std::uint16_t index) {
				TerrainRules::SlopeTag tag;
				tag.Left = state->SlopeLeft;
				tag.Right = state->SlopeRight;
				rules.SetSlope(index, &tag);
			}))->Tooltip = _("The ground line tool takes the brush tile as ground with these heights");
			actions->Add<UiButton>(_("Leave Out"), EditorIcon::Close, withBrushTile([](TerrainRules& rules, std::uint16_t index) {
				TerrainRules::SlopeTag tag;
				tag.Excluded = true;
				rules.SetSlope(index, &tag);
			}))->Tooltip = _("The ground line tool never takes the brush tile as ground");
			actions->Add<UiButton>(_("Untag"), EditorIcon::Undo, withBrushTile([](TerrainRules& rules, std::uint16_t index) {
				rules.SetSlope(index, nullptr);
			}))->Tooltip = _("The mask of the brush tile tells again what it is");

			auto* list = page->Add<UiLabel>(StringView(), UiTheme::TextDim);
			list->TextFunc = [s]() {
				SmallVector<std::uint16_t, 0> tiles;
				s->GetSmartTiles().GetRules().GetSlopeTiles(tiles);
				if (tiles.empty()) {
					return String(_("No tile is tagged"));
				}
				String text = _fn("{} tile tagged:", "{} tiles tagged:", std::int32_t(tiles.size()), std::int32_t(tiles.size()));
				for (std::size_t i = 0; i < tiles.size() && i < 16; i++) {
					text += " "_s + String(std::to_string(tiles[i]).c_str());
				}
				if (tiles.size() > 16) {
					text += " ..."_s;
				}
				return text;
			};
		}

		// A small area to try the rules on
		{
			auto* page = content->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			page->IsVisibleFunc = [state]() { return (state->Page == RulesPage::Try); };
			page->Add<UiLabel>(_("Click or drag to make cells solid or empty"), UiTheme::TextDim);
			page->Add<TryCanvas>(session);
		}

		dialog->AddButton(_("Clear All"), [s]() {
			TerrainRules& rules = s->GetSmartTiles().GetRules();
			if (rules.GetCount() == 0) {
				return;
			}
			rules.Clear();
			SaveRules(*s);
		})->IsVisibleFunc = [state]() { return (state->Page == RulesPage::Rules); };
		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
