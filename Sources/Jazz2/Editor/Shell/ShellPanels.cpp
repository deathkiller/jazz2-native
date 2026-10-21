#include "ShellPanels.h"
#include "ShellActions.h"
#include "ShellCommon.h"
#include "LevelCheck.h"
#include "PropertyRows.h"
#include "TileEditor.h"
#include "TileSetsDialog.h"

#if defined(WITH_EDITOR)

#include "../Tools/EditorContext.h"
#include "../Common/LayerView.h"
#include "../Common/TileStamp.h"
#include "../Document/CommandHistory.h"
#include "../Document/LevelDocument.h"
#include "../Events/EventPreviews.h"
#include "../Files/EditorSettings.h"
#include "../Files/TileSetDataCache.h"
#include "../Input/ActionRegistry.h"
#include "../Smart/SmartTileModel.h"
#include "../Tools/EditorTool.h"
#include "../Common/TileSource.h"
#include "../Files/AtomicFile.h"
#include "../Imaging/LevelThumbnail.h"
#include "../Imaging/PaletteMatch.h"
#include "../Imaging/PngCodec.h"
#include "../Imaging/TileImages.h"
#include "../Common/ToolMath.h"
#include "../../../nCine/Graphics/Texture.h"
#include "../Document/Commands.h"
#include "../Document/DocumentChanges.h"
#include "../Runtime/TileDrawing.h"
#include "../UI/UiDialogs.h"
#include "../Runtime/EditorCamera.h"
#include "../UI/UiColors.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace Death::Containers::Literals;
using Death::IO::FileAccess;
using Death::IO::fs;

namespace Jazz2::Editor
{
	namespace
	{
		// The tile set of a source of tiles and where its tiles start in the file, so a tile is known in other levels too,
		// returns false for animated tiles, which are of the level only
		bool GetSourceTileSet(const LevelDocument& document, std::uint8_t source, StringView& name, std::int32_t& offset)
		{
			TileSource resolved;
			if (!ResolveTileSource(document, source, resolved)) {
				return false;
			}
			name = resolved.Name;
			offset = resolved.Offset;
			return !name.empty();
		}

		bool IsFavoriteTile(IEditorContext& session, std::uint8_t source, std::uint16_t index)
		{
			StringView name;
			std::int32_t offset;
			if (!GetSourceTileSet(session.GetDocument(), source, name, offset)) {
				return false;
			}
			for (const EditorSettings::TileSetFavorites& favorites : session.GetSettings().TileFavorites) {
				if (favorites.TileSet == name) {
					return std::find(favorites.Favorites.begin(), favorites.Favorites.end(), std::uint16_t(index + offset)) != favorites.Favorites.end();
				}
			}
			return false;
		}

		void ToggleFavoriteTile(IEditorContext& session, std::uint8_t source, std::uint16_t index)
		{
			StringView name;
			std::int32_t offset;
			if (!GetSourceTileSet(session.GetDocument(), source, name, offset)) {
				session.ShowMessage(_x("editor", "Animated tiles are of this level only, they can't be favourites"));
				return;
			}
			EditorSettings& settings = session.GetSettings();
			SmallVector<std::uint16_t, 0>& favorites = settings.GetTileFavorites(name).Favorites;
			std::uint16_t stored = std::uint16_t(index + offset);
			auto it = std::find(favorites.begin(), favorites.end(), stored);
			if (it != favorites.end()) {
				favorites.erase(it);
				session.ShowMessage(_fx("editor", "Tile {} isn't a favourite anymore", index));
			} else if (std::int32_t(favorites.size()) < EditorSettings::MaxFavoriteItems) {
				favorites.push_back(stored);
				session.ShowMessage(_fx("editor", "Tile {} is a favourite now", index));
			}
			settings.Save();
		}

		const DocumentLayer* GetCurrentLayer(IEditorContext& session)
		{
			std::int32_t index = session.GetCurrentLayer();
			LevelDocument& document = session.GetDocument();
			return (index >= 0 && index < std::int32_t(document.Layers.size()) ? &document.Layers[index] : nullptr);
		}

		/** An editable text over a part of the interface, @p onDone gets the text unless the edit is cancelled by Escape */
		class InlineTextEdit : public UiTextBox
		{
		public:
			InlineTextEdit(StringView text, Function<void(StringView)>&& onDone)
				: UiTextBox(text), _onDone(Death::move(onDone)), _done(false)
			{
				OnCommit = [this](StringView text) { Finish(text); };
			}

			void OnFocusChanged(bool focused) override
			{
				UiTextBox::OnFocusChanged(focused);
				if (!focused) {
					// Confirmed by Enter or by clicking elsewhere, which committed it already, or cancelled by Escape
					_done = true;
					if (UiSurface* surface = GetSurface()) {
						surface->CloseOverlay(this);
					}
				}
			}

			// The edit is shown over the interface, a click outside of it closes it, which confirms it too
			void Finish(StringView text)
			{
				if (_done) {
					return;
				}
				_done = true;
				if (_onDone) {
					_onDone(text);
				}
			}

			static void Show(UiSurface& surface, const Rectf& rect, StringView text, StringView placeholder, Function<void(StringView)>&& onDone)
			{
				auto edit = std::make_unique<InlineTextEdit>(text, Death::move(onDone));
				InlineTextEdit* editPtr = edit.get();
				edit->Width = rect.W / surface.GetScale();
				edit->MaxLength = 64;
				edit->Placeholder = placeholder;
				UiOverlayOptions options;
				options.Position = Vector2f(rect.X, rect.Y);
				options.OnClosed = [editPtr]() { editPtr->Finish(editPtr->Text); };
				surface.PushOverlay(Death::move(edit), Death::move(options));
				editPtr->BeginEdit();
			}

		private:
			Function<void(StringView)> _onDone;
			bool _done;
		};
	}

	String GetAutoLayerName(const LevelDocument& document, std::int32_t index)
	{
		const DocumentLayer& layer = document.Layers[index];
		if (layer.IsSpriteLayer()) {
			// TRANSLATORS: Name shown for the unnamed layer the player and the events are in
			return _x("editor", "Sprite layer");
		}
		switch (Tiles::LayerRendererType(layer.Record.RendererType)) {
			case Tiles::LayerRendererType::Sky:
			case Tiles::LayerRendererType::Tunnel:
			case Tiles::LayerRendererType::Wave:
			// TRANSLATORS: Name shown for an unnamed layer drawn as a 3D textured background (sky, tunnel, wave or cylinder)
			case Tiles::LayerRendererType::Cylinder: return _x("editor", "Textured Background");
			// TRANSLATORS: Name shown for an unnamed layer drawn as a reflection, such as of water (noun)
			case Tiles::LayerRendererType::Reflection: return _x("editor", "Reflection");
			// TRANSLATORS: Name shown for an unnamed layer whose tiles blur or distort what is behind them (noun, plural)
			case Tiles::LayerRendererType::BlurBehind: return _x("editor", "Effects");
			// TRANSLATORS: Name shown for an unnamed X-ray layer, shown only through a mask
			case Tiles::LayerRendererType::XRay: return _x("editor", "Masked Layer");
			case Tiles::LayerRendererType::Tinted: return _x("editor", "Tinted Layer");
			case Tiles::LayerRendererType::PaletteShift: return _x("editor", "Palette Shifted Layer");
			// TRANSLATORS: Name shown for an unnamed layer whose tiles are drawn in one solid color
			case Tiles::LayerRendererType::Solid: return _x("editor", "Solid Color");
			default: break;
		}
		// By how it moves compared to the sprite layer
		std::uint8_t modelX = layer.Record.SpeedModels & 0x0F;
		if (modelX == std::uint8_t(Tiles::LayerSpeedModel::AlwaysOnTop)) {
			// TRANSLATORS: Name shown for an unnamed layer that doesn't move with the view
			return _x("editor", "Fixed to Screen");
		}
		if (modelX == std::uint8_t(Tiles::LayerSpeedModel::FitLevel)) {
			// TRANSLATORS: Name shown for an unnamed background layer stretched to fit the level
			return _x("editor", "Fitted Background");
		}
		float speed = std::max(std::abs(layer.Record.SpeedX), std::abs(layer.Record.SpeedY));
		if (speed == 1.0f) {
			// Moves with the sprite layer
			// TRANSLATORS: Name shown for an unnamed layer, {} is its number in the list of layers
			return _fx("editor", "Layer {}", index + 1);
		}
		// In front of the sprite layer or behind it by how it's drawn - a layer in front that moves slower is still in front
		std::int32_t spriteLayer = document.GetSpriteLayerIndex();
		if (spriteLayer >= 0) {
			// TRANSLATORS: Names shown for an unnamed layer in front of the sprite layer or behind it (nouns)
			return (layer.Record.Depth < document.Layers[spriteLayer].Record.Depth ? _x("editor", "Foreground") : _x("editor", "Background"));
		}
		return (speed > 1.0f ? _x("editor", "Foreground") : _x("editor", "Background"));
	}

	String GetLayerTitle(const LevelDocument& document, std::int32_t index)
	{
		const DocumentLayer& layer = document.Layers[index];
		return (!layer.Name.empty() ? layer.Name : GetAutoLayerName(document, index));
	}

	namespace
	{

		// Changes the description of the current layer as an undoable step, the changes of a value while it's dragged
		// are merged, @p final ends the step, a change that isn't dragged is always final
		void ChangeLayer(IEditorContext& session, Function<void(Formats::LayerRecord&, String&)>&& change, bool final = true)
		{
			const DocumentLayer* layer = GetCurrentLayer(session);
			if (layer == nullptr) {
				return;
			}
			Formats::LayerRecord record = layer->Record;
			String name = layer->Name;
			change(record, name);
			bool recordChanged = (std::memcmp(&record, &layer->Record, sizeof(record)) != 0);
			// A locked layer can still be renamed, the name doesn't change how the level looks or plays
			if (recordChanged && !CheckLayerUnlocked(session, session.GetCurrentLayer())) {
				return;
			}
			if (recordChanged || name != layer->Name) {
				// TRANSLATORS: Name of an undo step in the history, a property of a layer changed
				session.Execute(std::make_unique<SetLayerPropertiesCommand>(session.GetDocument(), session.GetCurrentLayer(), record, name, _x("editor", "Layer properties")), true);
			}
			if (final) {
				session.GetHistory().BreakMerge();
			}
		}

		constexpr std::int32_t LayerTextureSize = Formats::TexturedLayerChunk::TextureSize;

		// Replaces the texture of its own of the current layer as an undoable step, empty takes its tiles again
		void SetCurrentLayerTexture(IEditorContext& session, SmallVector<std::uint8_t, 0> texture, String name)
		{
			const DocumentLayer* layer = GetCurrentLayer(session);
			if (layer == nullptr || !CheckLayerUnlocked(session, session.GetCurrentLayer())) {
				return;
			}
			if (layer->Texture.size() == texture.size() && std::equal(texture.begin(), texture.end(), layer->Texture.begin())) {
				return;
			}
			session.Execute(std::make_unique<SetLayerTextureCommand>(session.GetDocument(), session.GetCurrentLayer(), Death::move(texture), Death::move(name)));
			session.GetHistory().BreakMerge();
		}

		// The texture a textured layer without one of its own is drawn with, made of its tiles at the top left as the game
		// takes them (repeated if there are fewer of them). Tiles in true color are what a reflection takes of them, the
		// brightest channel, or the closest entry of the palette for the others.
		SmallVector<std::uint8_t, 0> MakeTextureFromTiles(IEditorContext& session, const DocumentLayer& layer)
		{
			constexpr std::int32_t TileSize = Formats::LevelFile::TileSize;
			constexpr std::int32_t TilesPerSide = LayerTextureSize / TileSize;
			const LevelDocument& document = session.GetDocument();
			const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
			const bool reflection = (layer.Record.RendererType == std::uint8_t(Tiles::LayerRendererType::Reflection));

			SmallVector<std::uint8_t, 0> texture;
			texture.resize(std::size_t(LayerTextureSize) * std::size_t(LayerTextureSize), 0);
			std::int32_t tilesX = std::clamp<std::int32_t>(layer.GetWidth(), 1, TilesPerSide);
			std::int32_t tilesY = std::clamp<std::int32_t>(layer.GetHeight(), 1, TilesPerSide);
			std::unique_ptr<PaletteSearch> search;
			for (std::int32_t ty = 0; ty < TilesPerSide; ty++) {
				for (std::int32_t tx = 0; tx < TilesPerSide; tx++) {
					TileRef tile = layer.GetTile(tx % tilesX, ty % tilesY);
					LevelThumbnail::TilePixels pixels;
					if (!LevelThumbnail::GetTilePixels(session.GetDocument(), session.GetTileSetDataCache(), tile, pixels)) {
						continue;
					}
					// Overridden in the level, as the level shows it
					const Formats::TileDiffuseOverride* diffuse = nullptr;
					std::int32_t tileId = (tile.IsAnimated() ? 0 : document.ToTileId(tile));
					if (tileId > 0 && tileId <= UINT16_MAX) {
						diffuse = SetTileOverrideCommand::FindDiffuse(document, std::uint16_t(tileId));
					}
					bool flipX = (pixels.Flags & Formats::TileRecord::FlipX) != 0;
					bool flipY = (pixels.Flags & Formats::TileRecord::FlipY) != 0;
					for (std::int32_t y = 0; y < TileSize; y++) {
						for (std::int32_t x = 0; x < TileSize; x++) {
							std::int32_t i = (flipY ? TileSize - 1 - y : y) * TileSize + (flipX ? TileSize - 1 - x : x);
							std::uint8_t index;
							if (diffuse != nullptr) {
								index = diffuse->Indices[i];
							} else if (pixels.TrueColor) {
								const std::uint8_t* rgba = &pixels.Pixels[std::size_t(i) * 4];
								if (rgba[3] < 128) {
									index = 0;
								} else if (reflection) {
									index = std::max(rgba[0], std::max(rgba[1], rgba[2]));
								} else {
									if (search == nullptr) {
										search = std::make_unique<PaletteSearch>(palette);
									}
									index = search->FindNearest(OkLab::FromPaletteEntry(std::uint32_t(rgba[0]) | (std::uint32_t(rgba[1]) << 8) | (std::uint32_t(rgba[2]) << 16) | 0xFF000000u));
								}
							} else {
								index = (pixels.Remapping != nullptr ? pixels.Remapping[pixels.Pixels[i]] : pixels.Pixels[i]);
							}
							texture[std::size_t(ty * TileSize + y) * std::size_t(LayerTextureSize) + std::size_t(tx * TileSize + x)] = index;
						}
					}
				}
			}
			return texture;
		}

		void ImportLayerTexture(IEditorContext& session)
		{
			if (session.GetSurface() == nullptr || GetCurrentLayer(session) == nullptr || !CheckLayerUnlocked(session, session.GetCurrentLayer())) {
				return;
			}
			IEditorContext* s = &session;
			// TRANSLATORS: Title of the file dialog that opens an image to texture a layer with
			ShowOpenImageDialog(session, _x("editor", "Import Layer Texture"), [s](StringView path) {
				PngImage image;
				String error;
				if (!PngCodec::Load(path, image, &error)) {
					s->ShowMessage(error);
					return;
				}
				if (image.Width != LayerTextureSize || image.Height != LayerTextureSize) {
					s->ShowMessage(_fx("editor", "The image has to be {} x {} pixels", LayerTextureSize, LayerTextureSize));
					return;
				}
				// The palette indices of an image with the palette of the level, the closest entries of other colors
				SmallVector<std::uint8_t, 0> texture;
				texture.resize_for_overwrite(std::size_t(LayerTextureSize) * std::size_t(LayerTextureSize));
				bool changed = ReadTextureFromImage(image, LayerTextureSize, ContentResolver::Get().GetPalettes().data(), texture.data());
				// TRANSLATORS: Name of an undo step in the history, an image imported as the texture of a layer
				SetCurrentLayerTexture(*s, Death::move(texture), _x("editor", "Import layer texture"));
				if (changed) {
					s->ShowMessage(_x("editor", "Some colors aren't in the palette, the closest ones were taken"));
				}
			});
		}

		void ExportLayerTexture(IEditorContext& session)
		{
			UiSurface* surface = session.GetSurface();
			const DocumentLayer* layer = GetCurrentLayer(session);
			if (surface == nullptr || layer == nullptr) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			session.GetImageRoots(true, roots);
			if (roots.empty()) {
				session.ShowMessage(_x("editor", "There is no place to save files to"));
				return;
			}
			// The image of its own, or the one its tiles make, so it can be edited and imported as its own
			std::shared_ptr<SmallVector<std::uint8_t, 0>> texture = std::make_shared<SmallVector<std::uint8_t, 0>>();
			if (!layer->Texture.empty()) {
				texture->assign(layer->Texture.begin(), layer->Texture.end());
			} else {
				*texture = MakeTextureFromTiles(session, *layer);
			}
			IEditorContext* s = &session;
			String name = GetLayerTitle(session.GetDocument(), session.GetCurrentLayer()) + ".png"_s;
			UiFileDialog::Show(*surface, _x("editor", "Export Layer Texture"), UiFileDialogMode::Save, Death::move(roots), ".png"_s, {}, name, [s, texture](StringView path) {
				PngImage png = MakeTextureImage(texture->data(), LayerTextureSize, ContentResolver::Get().GetPalettes().data());
				if (!AtomicFile::Write(path, [&png](Stream& stream) { return PngCodec::Encode(stream, png); })) {
					s->ShowMessage(_x("editor", "The file couldn't be written"));
					return;
				}
#if defined(DEATH_TARGET_EMSCRIPTEN)
				fs::SyncToPersistent();
#endif
				// TRANSLATORS: Status message after the texture of a layer was exported, {} is the file name
				s->ShowMessage(_fx("editor", "Saved {}", fs::GetFileName(path)));
			});
		}

		/** The texture of its own of the current layer, drawn through the palette, so it follows the palette as the level does */
		class LayerTexturePreview : public UiElement
		{
		public:
			explicit LayerTexturePreview(IEditorContext& session)
				: _session(session), _shownLayer(-1), _shownRevision(UINT32_MAX), _shownOutsideChanges(UINT32_MAX)
			{
				Height = 88.0f;
				Tooltip = _x("editor", "The image of its own the layer is textured with");
			}

			void Draw(UiPainter& painter) override
			{
				// Made again when the layer or anything of the level changed, by a step or from outside
				CommandHistory& history = _session.GetHistory();
				std::int32_t layerIndex = _session.GetCurrentLayer();
				std::uint32_t revision = history.GetRevision();
				std::uint32_t outsideChanges = history.GetOutsideChangeCount();
				if (layerIndex != _shownLayer || revision != _shownRevision || outsideChanges != _shownOutsideChanges) {
					_shownLayer = layerIndex;
					_shownRevision = revision;
					_shownOutsideChanges = outsideChanges;
					Rebuild(GetCurrentLayer(_session));
				}
				if (_texture == nullptr) {
					return;
				}
				float side = std::floor(std::min(Bounds.W, Bounds.H));
				Rectf rect(std::round(Bounds.X), std::round(Bounds.Y), side, side);
				painter.FillRect(rect, Colorf(0.0f, 0.0f, 0.0f, 0.35f), UiLayer::Fill);
				painter.Image(*_texture, rect, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf::White, 0, UiLayer::Content);
			}

		private:
			IEditorContext& _session;
			std::int32_t _shownLayer;
			std::uint32_t _shownRevision;
			std::uint32_t _shownOutsideChanges;
			std::unique_ptr<nCine::Texture> _texture;

			void Rebuild(const DocumentLayer* layer)
			{
				constexpr std::size_t PixelCount = std::size_t(LayerTextureSize) * std::size_t(LayerTextureSize);
				if (layer == nullptr || layer->Texture.size() != PixelCount) {
					_texture = nullptr;
					return;
				}
				if (_texture == nullptr) {
					_texture = std::make_unique<nCine::Texture>("EditorLayerTexture", nCine::Texture::Format::R8, LayerTextureSize, LayerTextureSize);
					// Palette indices can't be mixed, the image is made smaller pixel by pixel
					_texture->SetMinFiltering(nCine::SamplerFilter::Nearest);
					_texture->SetMagFiltering(nCine::SamplerFilter::Nearest);
					_texture->SetWrap(nCine::SamplerWrapping::ClampToEdge);
				}
				_texture->LoadFromTexels(layer->Texture.data(), 0, 0, LayerTextureSize, LayerTextureSize);
			}
		};

		/** Row of the layer list */
		class LayerRow : public UiElement
		{
		public:
			LayerRow(IEditorContext& session, LayersPanel& panel, std::int32_t index)
				: _session(session), _panel(panel), _index(index), _dragging(false)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(available.X, context.Px(UiTheme::RowHeight + 3.0f));
			}

			void Draw(UiPainter& painter) override
			{
				LevelDocument& document = _session.GetDocument();
				if (_index >= std::int32_t(document.Layers.size())) {
					return;
				}
				const DocumentLayer& layer = document.Layers[_index];
				bool selected = (_session.GetCurrentLayer() == _index);
				bool hidden = (layer.EditorFlags & DocumentLayer::EditorHidden) != 0;
				bool locked = (layer.EditorFlags & DocumentLayer::EditorLocked) != 0;

				if (selected) {
					painter.RoundRect(Bounds, UiTheme::Selection);
					painter.RoundRectBorder(Bounds, UiTheme::AccentDim);
				} else if (_hoverAmount > 0.0f) {
					Colorf hover = UiTheme::Hover;
					hover.A *= _hoverAmount;
					painter.RoundRect(Bounds, hover);
				}

				float iconSize = painter.Px(UiTheme::IconSize);
				Rectf eye = GetEyeRect(painter.GetScale());
				Rectf lock = GetLockRect(painter.GetScale());
				painter.Icon(hidden ? EditorIcon::EyeClosed : EditorIcon::Eye, eye, hidden ? UiTheme::IconDisabled : UiTheme::IconColor);
				painter.Icon(locked ? EditorIcon::Lock : EditorIcon::Unlock, lock, locked ? UiTheme::Accent : Colorf(1.0f, 1.0f, 1.0f, 0.25f));

				// A small picture of the layer, on a dark backdrop so its empty tiles can be told
				Rectf thumbnailRect = GetThumbnailRect(painter.GetScale());
				painter.FillRect(thumbnailRect, Colorf(0.0f, 0.0f, 0.0f, 0.45f), UiLayer::Content);
				if (nCine::Texture* thumbnail = _panel.GetThumbnail(_index)) {
					// The shape of the layer kept, centered in its place
					Vector2i size(thumbnail->GetWidth(), thumbnail->GetHeight());
					float fit = std::min(thumbnailRect.W / float(std::max<std::int32_t>(size.X, 1)), thumbnailRect.H / float(std::max<std::int32_t>(size.Y, 1)));
					float w = std::round(size.X * fit), h = std::round(size.Y * fit);
					Rectf image(std::round(thumbnailRect.X + (thumbnailRect.W - w) * 0.5f), std::round(thumbnailRect.Y + (thumbnailRect.H - h) * 0.5f), w, h);
					painter.Image(*thumbnail, image, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), hidden ? Colorf(1.0f, 1.0f, 1.0f, 0.35f) : Colorf::White, -1, UiLayer::Content);
				}

				// A name the layer only gets by what it is is dimmer than one it was given
				Colorf textColor = (hidden ? UiTheme::TextDisabled
					: (layer.IsSpriteLayer() ? UiTheme::TextHeader : (layer.Name.empty() ? UiTheme::TextDim : UiTheme::Text)));
				painter.PushClip(Bounds);
				painter.TextInRect(GetLayerTitle(document, _index), GetNameRect(painter.GetScale()), textColor);
				painter.PopClip();
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				if (event.Button != MouseButton::Left) {
					return false;
				}
				float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
				if (event.Clicks >= 2 && GetNameRect(scale).Contains(event.Pos)) {
					BeginRename();
					return false;
				}
				_pressPos = event.Pos;
				_dragging = false;
				return true;
			}

			void OnPointerMove(const UiPointerEvent& event) override
			{
				if (!_pressed) {
					return;
				}
				float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
				// Only a real move starts dragging, a click wobbles a little
				if (!_dragging && std::abs(event.Pos.Y - _pressPos.Y) > 4.0f * scale) {
					_dragging = true;
				}
				if (_dragging) {
					_panel.UpdateLayerDrag(_index, event.Pos);
				}
			}

			void OnPointerUp(const UiPointerEvent& event, bool inside) override
			{
				if (_dragging) {
					_dragging = false;
					_panel.EndLayerDrag(true);
					return;
				}
				if (!inside) {
					return;
				}
				float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
				if (GetEyeRect(scale).Contains(event.Pos)) {
					// With Alt, only this layer is shown, or all of them again
					if ((event.Modifiers & KeyMod::Alt) != 0) {
						SoloLayer(_session, _index);
					} else {
						_session.ToggleLayerFlag(_index, DocumentLayer::EditorHidden);
					}
				} else if (GetLockRect(scale).Contains(event.Pos)) {
					_session.ToggleLayerFlag(_index, DocumentLayer::EditorLocked);
				} else {
					_session.SetCurrentLayer(_index);
				}
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				float scale = (GetSurface() != nullptr ? GetSurface()->GetScale() : 1.0f);
				if (GetEyeRect(scale).Contains(pos)) {
					return _x("editor", "Show or hide the layer in the editor, Alt+click shows only this one");
				}
				if (GetLockRect(scale).Contains(pos)) {
					return _x("editor", "Lock the layer, so it can't be changed");
				}
				// TRANSLATORS: Tooltip of a layer in the list of layers, {} is its number
				_tooltip = _fx("editor", "Layer {} - double click to rename it, drag it to move it", _index + 1);
				return _tooltip;
			}

		private:
			IEditorContext& _session;
			LayersPanel& _panel;
			std::int32_t _index;
			Vector2f _pressPos;
			bool _dragging;
			mutable String _tooltip;

			Rectf GetThumbnailRect(float scale) const
			{
				Rectf lock = GetLockRect(scale);
				float inset = std::round(2.0f * scale);
				float height = Bounds.H - inset * 2.0f;
				return Rectf(lock.X + lock.W + std::round(UiTheme::Padding * scale), Bounds.Y + inset, std::round(height * 2.0f), height);
			}

			Rectf GetNameRect(float scale) const
			{
				Rectf thumbnail = GetThumbnailRect(scale);
				float x = thumbnail.X + thumbnail.W + std::round(UiTheme::Padding * scale);
				return Rectf(x, Bounds.Y, Bounds.X + Bounds.W - x, Bounds.H);
			}

			void BeginRename()
			{
				UiSurface* surface = GetSurface();
				LevelDocument& document = _session.GetDocument();
				if (surface == nullptr || _index >= std::int32_t(document.Layers.size())) {
					return;
				}
				const DocumentLayer& layer = document.Layers[_index];
				IEditorContext* s = &_session;
				std::int32_t index = _index;
				InlineTextEdit::Show(*surface, GetNameRect(surface->GetScale()), layer.Name, GetAutoLayerName(document, _index), [s, index](StringView text) {
					LevelDocument& document = s->GetDocument();
					if (index >= std::int32_t(document.Layers.size())) {
						return;
					}
					const DocumentLayer& layer = document.Layers[index];
					String name = text.trimmed();
					if (name != layer.Name) {
						// TRANSLATORS: Name of an undo step in the history, a layer renamed
						s->Execute(std::make_unique<SetLayerPropertiesCommand>(document, index, layer.Record, name, _x("editor", "Rename layer")));
					}
				});
			}

			Rectf GetEyeRect(float scale) const
			{
				float iconSize = std::round(UiTheme::IconSize * scale);
				return Rectf(Bounds.X + std::round(2.0f * scale), Bounds.Y, iconSize, Bounds.H);
			}

			Rectf GetLockRect(float scale) const
			{
				Rectf eye = GetEyeRect(scale);
				return Rectf(eye.X + eye.W, Bounds.Y, eye.W, Bounds.H);
			}
		};

		// Button whose tooltip is made when it's shown, the shortcut in it may have been changed meanwhile
		class LiveTooltipButton : public UiButton
		{
		public:
			using UiButton::UiButton;

			mutable Function<String()> TooltipFunc;

			StringView GetTooltip(Vector2f pos) const override
			{
				if (!TooltipFunc) {
					return UiButton::GetTooltip(pos);
				}
				_liveTooltip = TooltipFunc();
				return _liveTooltip;
			}

		private:
			mutable String _liveTooltip;
		};

		/** Row of the inspector, a name and an editor */
		UiStack* AddPropertyRow(UiElement* parent, StringView label)
		{
			return AddLabeledRow(parent, label, {}, 60.0f);
		}

		std::uint8_t GetRendererType(IEditorContext& session)
		{
			const DocumentLayer* layer = GetCurrentLayer(session);
			return (layer != nullptr ? layer->Record.RendererType : std::uint8_t(Tiles::LayerRendererType::Default));
		}

		Colorf GetLayerColor(IEditorContext& session)
		{
			const DocumentLayer* layer = GetCurrentLayer(session);
			if (layer == nullptr) {
				return Colorf::White;
			}
			const std::uint8_t* color = layer->Record.Color;
			return Colorf(color[0] / 255.0f, color[1] / 255.0f, color[2] / 255.0f, color[3] / 255.0f);
		}

		/** A byte of the color of the current layer, 0 is red, 3 is alpha */
		UiNumberBox* AddLayerColorByte(UiStack* row, IEditorContext& session, std::int32_t i)
		{
			IEditorContext* s = &session;
			auto* number = AddByteBox(row, [s, i]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return std::uint8_t(layer != nullptr ? layer->Record.Color[i] : 0);
			}, [s, i](std::uint8_t byte, bool final) {
				ChangeLayer(*s, [i, byte](Formats::LayerRecord& record, String&) { record.Color[i] = byte; }, final);
			});
			number->Flex = 1.0f;
			return number;
		}

		/** A position of a tunnel or a reflection of the current layer in percent of the view, 0 is the horizontal one, 1 the vertical one */
		UiNumberBox* AddTexturedFadePosition(UiStack* row, IEditorContext& session, std::int32_t axis)
		{
			IEditorContext* s = &session;
			auto* number = row->Add<UiNumberBox>([s, axis]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				if (layer == nullptr) {
					return 50.0f;
				}
				return (axis == 0 ? layer->Record.Textured.FadeX : layer->Record.Textured.FadeY) * 100.0f;
			}, [s, axis](float value, bool final) {
				float position = std::clamp<float>(value, 0.0f, 100.0f) / 100.0f;
				ChangeLayer(*s, [axis, position](Formats::LayerRecord& record, String&) {
					(axis == 0 ? record.Textured.FadeX : record.Textured.FadeY) = position;
				}, final);
			}, 0.0f, 100.0f, 1.0f, 1);
			number->Flex = 1.0f;
			return number;
		}

		/** Swatch of the color of the current layer that opens a color picker, alpha is picked too if @p withAlpha */
		UiColorSwatch* AddLayerColorSwatch(UiStack* row, IEditorContext& session, bool withAlpha)
		{
			IEditorContext* s = &session;
			auto getColor = [s, withAlpha]() {
				Colorf color = GetLayerColor(*s);
				if (!withAlpha) {
					color.A = 1.0f;
				}
				return color;
			};
			auto* swatch = row->Add<UiColorSwatch>(getColor, [s, getColor, withAlpha]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				// All picked while the picker is open is a single step, which starts here
				s->GetHistory().BreakMerge();
				UiColorPicker::ShowPopup(*surface, surface->GetPointerPos(), getColor, [s, withAlpha](const Colorf& color) {
					std::uint8_t bytes[4] = { std::uint8_t(std::round(color.R * 255.0f)), std::uint8_t(std::round(color.G * 255.0f)),
						std::uint8_t(std::round(color.B * 255.0f)), std::uint8_t(std::round(color.A * 255.0f)) };
					ChangeLayer(*s, [&bytes, withAlpha](Formats::LayerRecord& record, String&) {
						// Without alpha, the last byte means something else and stays
						std::memcpy(record.Color, bytes, withAlpha ? 4 : 3);
					}, false);
				}, withAlpha);
			});
			swatch->Tooltip = _x("editor", "Pick a color");
			return swatch;
		}
	}

	void AddPickerPanelTabs(UiElement* header, IEditorContext& session)
	{
		IEditorContext* s = &session;
		struct Tab { const char* Label; EditorIcon Icon; PickerPanel Panel; };
		static const Tab Tabs[] = {
			// TRANSLATORS: Tabs of the picker panel, switch between picking tiles and picking events
			{ NC_("editor", "Tiles"), EditorIcon::Tiles, PickerPanel::Tiles },
			{ NC_("editor", "Events"), EditorIcon::Event, PickerPanel::Events },
		};
		// When either of them is too narrow for its text, both show only their icons
		auto buttons = std::make_shared<SmallVector<UiButton*, 2>>();
		for (const Tab& tab : Tabs) {
			PickerPanel panel = tab.Panel;
			auto* button = header->Add<UiButton>(_x("editor", tab.Label), tab.Icon, [s, panel]() {
				s->SetPickerPanel(panel);
				// Events are placed by their own tool
				if (panel == PickerPanel::Events) {
					s->GetTools().SetActive(EditorToolType::Event);
				} else if (s->GetTools().GetChosenType() == EditorToolType::Event) {
					s->GetTools().SetActive(EditorToolType::Brush);
				}
			});
			button->Style = UiButtonStyle::Flat;
			button->Flex = 1.0f;
			button->IsCheckedFunc = [s, panel]() { return (s->GetPickerPanel() == panel); };
			button->IconOnlyFunc = [buttons]() {
				for (UiButton* b : *buttons) {
					if (b->IsTextCut()) {
						return true;
					}
				}
				return false;
			};
			buttons->push_back(button);
		}
	}

	LayersPanel::LayersPanel(IEditorContext& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session), _list(nullptr), _shownCount(-1),
			_dragFrom(-1), _dropSlot(-1), _revalidateColors(false)
	{
		// The title is in the header of the panel it's in
		_list = Add<UiScrollView>();
		_list->Flex = 1.0f;

		// The pictures of the layers are made again as their tiles change
		_thumbnailColors = std::make_unique<LevelThumbnail>(session.GetDocument(), session.GetTileSetDataCache());
		_changeObserver = std::make_shared<Function<void(const DocumentChanges&)>>([this](const DocumentChanges& changes) {
			bool all = changes.HasAny(DocumentChangeKind::LayerStructure | DocumentChangeKind::TileSets | DocumentChangeKind::TileOverrides |
				DocumentChangeKind::Palette | DocumentChangeKind::AnimationStructure);
			if (all) {
				_revalidateColors = true;
			}
			const LevelDocument& document = _session.GetDocument();
			for (Thumbnail& thumbnail : _thumbnails) {
				if (all) {
					thumbnail.Dirty = true;
					continue;
				}
				for (const DocumentTileArea& area : changes.GetTileAreas()) {
					if (area.Layer >= 0 && area.Layer < std::int32_t(document.Layers.size()) && document.Layers[area.Layer].Id == thumbnail.LayerId) {
						thumbnail.Dirty = true;
					}
				}
			}
		});
		session.AddChangeObserver(_changeObserver);

		auto* buttons = Add<UiStack>(UiOrientation::Horizontal, 1.0f);
		IEditorContext* s = &session;
		for (const char* id : { ActionIds::LayerAdd, ActionIds::LayerDuplicate, ActionIds::LayerRemove, ActionIds::LayerUp, ActionIds::LayerDown }) {
			if (EditorAction* action = session.GetActions().Find(id)) {
				bool isRemove = (StringView(id) == StringView(ActionIds::LayerRemove));
				auto* button = buttons->Add<LiveTooltipButton>(StringView(), action->Icon, [action, isRemove, s]() {
					if (!action->CanExecute()) {
						return;
					}
					// The button is right next to the one that adds a layer, a layer with tiles isn't removed by a slip
					const DocumentLayer* layer = GetCurrentLayer(*s);
					UiSurface* surface = s->GetSurface();
					bool hasTiles = (layer != nullptr && std::any_of(layer->Tiles.begin(), layer->Tiles.end(), [](const TileRef& tile) { return !tile.IsEmpty(); }));
					if (isRemove && hasTiles && surface != nullptr) {
						// Removed only if it's still the layer that was asked about, by its identity, an undo meanwhile
						// could put another one at its place
						std::int32_t index = s->GetCurrentLayer();
						std::uint32_t layerId = layer->Id;
						UiMessageDialog::Show(*surface, _x("editor", "Remove Layer"), _fx("editor", "Remove the layer \"{}\" with all its tiles?", GetLayerTitle(s->GetDocument(), index)),
							// TRANSLATORS: Buttons of the dialog that asks before removing a layer with tiles, "Remove" is a verb
							{ _x("editor", "Remove"), _x("editor", "Cancel") }, [action, s, layerId](std::int32_t button) {
								const DocumentLayer* current = GetCurrentLayer(*s);
								if (button == 0 && current != nullptr && current->Id == layerId && action->CanExecute()) {
									action->Execute();
								}
							});
						return;
					}
					action->Execute();
				});
				button->Style = UiButtonStyle::Flat;
				// With the shortcut it has when it's shown, it can be changed meanwhile
				button->TooltipFunc = [action]() {
					return action->GetTooltip();
				};
				button->IsEnabledFunc = [action]() {
					return action->CanExecute();
				};
			}
		}
		Rebuild();
	}

	LayersPanel::~LayersPanel()
	{
	}

	void LayersPanel::Update(float timeMult)
	{
		if (_shownCount != std::int32_t(_session.GetDocument().Layers.size())) {
			Rebuild();
		}
		UpdateThumbnails(timeMult);
		UiPanel::Update(timeMult);
	}

	nCine::Texture* LayersPanel::GetThumbnail(std::int32_t index) const
	{
		const LevelDocument& document = _session.GetDocument();
		if (index < 0 || index >= std::int32_t(document.Layers.size())) {
			return nullptr;
		}
		std::uint32_t layerId = document.Layers[index].Id;
		for (const Thumbnail& thumbnail : _thumbnails) {
			if (thumbnail.LayerId == layerId) {
				return thumbnail.Image.get();
			}
		}
		return nullptr;
	}

	void LayersPanel::UpdateThumbnails(float timeMult)
	{
		// Painting changes the tiles every frame, a layer is drawn again a few times a second at most, and one a frame
		constexpr float CooldownFrames = 20.0f;
		const LevelDocument& document = _session.GetDocument();
		for (std::size_t i = 0; i < _thumbnails.size(); ) {
			bool exists = std::any_of(document.Layers.begin(), document.Layers.end(), [&](const DocumentLayer& layer) { return layer.Id == _thumbnails[i].LayerId; });
			if (!exists) {
				_thumbnails.erase(_thumbnails.begin() + i);
				continue;
			}
			_thumbnails[i].Cooldown = std::max(_thumbnails[i].Cooldown - timeMult, 0.0f);
			i++;
		}
		for (const DocumentLayer& layer : document.Layers) {
			if (std::none_of(_thumbnails.begin(), _thumbnails.end(), [&layer](const Thumbnail& thumbnail) { return thumbnail.LayerId == layer.Id; })) {
				Thumbnail& thumbnail = _thumbnails.emplace_back();
				thumbnail.LayerId = layer.Id;
				thumbnail.Dirty = true;
				thumbnail.Cooldown = 0.0f;
			}
		}
		if (_revalidateColors) {
			_revalidateColors = false;
			_thumbnailColors->Revalidate();
		}
		for (Thumbnail& thumbnail : _thumbnails) {
			if (!thumbnail.Dirty || thumbnail.Cooldown > 0.0f) {
				continue;
			}
			for (const DocumentLayer& layer : document.Layers) {
				if (layer.Id == thumbnail.LayerId) {
					DrawThumbnail(thumbnail, layer);
					break;
				}
			}
			thumbnail.Dirty = false;
			thumbnail.Cooldown = CooldownFrames;
			break;
		}
	}

	void LayersPanel::DrawThumbnail(Thumbnail& thumbnail, const DocumentLayer& layer)
	{
		// Small, the shape of the layer kept, a texel is a tile there, those between are skipped
		constexpr float MaxWidth = 40.0f, MaxHeight = 20.0f;
		std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
		if (width <= 0 || height <= 0) {
			thumbnail.Image = nullptr;
			return;
		}
		float scale = std::min(MaxWidth / float(width), MaxHeight / float(height));
		Vector2i size(std::clamp<std::int32_t>(std::int32_t(std::round(width * scale)), 1, std::int32_t(MaxWidth)),
			std::clamp<std::int32_t>(std::int32_t(std::round(height * scale)), 1, std::int32_t(MaxHeight)));
		SmallVector<std::uint8_t, 0> texels;
		texels.resize_for_overwrite(std::size_t(size.X) * std::size_t(size.Y) * 4);
		for (std::int32_t y = 0; y < size.Y; y++) {
			std::int32_t ty = std::min<std::int32_t>(std::int32_t((y + 0.5f) * height / size.Y), height - 1);
			for (std::int32_t x = 0; x < size.X; x++) {
				std::int32_t tx = std::min<std::int32_t>(std::int32_t((x + 0.5f) * width / size.X), width - 1);
				std::uint32_t color = _thumbnailColors->GetTileColor(layer.GetTile(tx, ty));
				std::uint8_t* texel = &texels[(std::size_t(y) * size.X + x) * 4];
				texel[0] = std::uint8_t(color);
				texel[1] = std::uint8_t(color >> 8);
				texel[2] = std::uint8_t(color >> 16);
				texel[3] = std::uint8_t(color >> 24);
			}
		}
		if (thumbnail.Image == nullptr || thumbnail.Size != size) {
			thumbnail.Image = std::make_unique<nCine::Texture>("EditorLayerThumbnail", nCine::Texture::Format::RGBA8, size.X, size.Y);
			thumbnail.Image->SetMinFiltering(nCine::SamplerFilter::Linear);
			thumbnail.Image->SetMagFiltering(nCine::SamplerFilter::Nearest);
			thumbnail.Image->SetWrap(nCine::SamplerWrapping::ClampToEdge);
			thumbnail.Size = size;
		}
		thumbnail.Image->LoadFromTexels(texels.data(), 0, 0, size.X, size.Y);
	}

	void LayersPanel::Rebuild()
	{
		_shownCount = std::int32_t(_session.GetDocument().Layers.size());
		_dragFrom = -1;
		_dropSlot = -1;
		auto* rows = _list->SetContent<UiStack>(UiOrientation::Vertical, 1.0f);
		for (std::int32_t i = 0; i < _shownCount; i++) {
			rows->Add<LayerRow>(_session, *this, i);
		}
	}

	void LayersPanel::UpdateLayerDrag(std::int32_t from, Vector2f pos)
	{
		UiElement* rows = _list->GetContent();
		if (rows == nullptr) {
			return;
		}
		// The gap before the first row whose middle is below the pointer
		std::int32_t count = rows->GetChildCount();
		std::int32_t slot = count;
		for (std::int32_t i = 0; i < count; i++) {
			const Rectf& bounds = rows->GetChild(i)->Bounds;
			if (pos.Y < bounds.Y + bounds.H * 0.5f) {
				slot = i;
				break;
			}
		}
		if (_dragFrom != from) {
			// The layer is known by its identifier, an undo meanwhile could put another one at the row
			const LevelDocument& document = _session.GetDocument();
			_dragLayerId = (from >= 0 && from < std::int32_t(document.Layers.size()) ? document.Layers[from].Id : 0);
		}
		_dragFrom = from;
		_dropSlot = slot;
	}

	void LayersPanel::EndLayerDrag(bool drop)
	{
		std::int32_t slot = _dropSlot;
		bool dragging = (_dragFrom >= 0);
		_dragFrom = -1;
		_dropSlot = -1;
		if (!drop || !dragging || slot < 0) {
			return;
		}
		// Where the dragged layer is now
		const LevelDocument& document = _session.GetDocument();
		std::int32_t from = -1;
		for (std::int32_t i = 0; i < std::int32_t(document.Layers.size()); i++) {
			if (document.Layers[i].Id == _dragLayerId) {
				from = i;
				break;
			}
		}
		if (from < 0) {
			return;
		}
		// The gaps right before and after the layer leave it where it is
		std::int32_t to = (slot > from ? slot - 1 : slot);
		if (to != from) {
			_session.MoveLayerTo(from, to);
		}
	}

	Rectf LayersPanel::GetSlotLine(std::int32_t slot) const
	{
		UiElement* rows = _list->GetContent();
		if (rows == nullptr || rows->GetChildCount() == 0) {
			return Rectf();
		}
		std::int32_t count = rows->GetChildCount();
		const Rectf& row = rows->GetChild(std::min(slot, count - 1))->Bounds;
		float y = (slot < count ? row.Y : row.Y + row.H);
		return Rectf(row.X, y, row.W, 0.0f);
	}

	void LayersPanel::Draw(UiPainter& painter)
	{
		UiPanel::Draw(painter);

		// Where the dragged layer would go, unless it would stay where it is
		if (_dragFrom >= 0 && _dropSlot >= 0 && _dropSlot != _dragFrom && _dropSlot != _dragFrom + 1) {
			Rectf line = GetSlotLine(_dropSlot);
			float thickness = std::max(2.0f, std::round(painter.GetScale() * 2.0f));
			painter.PushClip(_list->Bounds);
			painter.FillRect(Rectf(line.X, std::round(line.Y - thickness * 0.5f), line.W, thickness), UiTheme::Accent, UiLayer::Overlay);
			painter.PopClip();
		}
	}

	namespace
	{
		// The sprite layer moves as the game moves it, a locked layer can't be changed at all
		bool IsMovableLayer(IEditorContext& session)
		{
			const DocumentLayer* layer = GetCurrentLayer(session);
			return (layer != nullptr && !layer->IsSpriteLayer() && (layer->EditorFlags & DocumentLayer::EditorLocked) == 0);
		}

		bool IsCurrentLayerUnlocked(IEditorContext& session)
		{
			const DocumentLayer* layer = GetCurrentLayer(session);
			return (layer != nullptr && (layer->EditorFlags & DocumentLayer::EditorLocked) == 0);
		}

		// What the color means depends on the renderer, so it's edited differently for each of them
		bool RendererIs(IEditorContext& session, std::initializer_list<Tiles::LayerRendererType> types)
		{
			std::uint8_t type = GetRendererType(session);
			for (Tiles::LayerRendererType t : types) {
				if (std::uint8_t(t) == type) {
					return true;
				}
			}
			return false;
		}

		// Where a reflection is drawn, the layer wins over the window, as in the game
		std::uint8_t GetReflectionArea(const Formats::LayerRecord& record)
		{
			return std::uint8_t((record.Textured.Flags & Formats::TexturedLayerParams::InnerLayer) != 0
				? Formats::TexturedLayerParams::InnerLayer : (record.Textured.Flags & Formats::TexturedLayerParams::InnerWindow));
		}

		/** The name and the size of the layer */
		void AddLayerNameRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			{
				// TRANSLATORS: Label of a property in the layer properties panel, the name of the layer
				auto* row = AddPropertyRow(rows, _x("editor", "Name"));
				auto* name = row->Add<UiTextBox>();
				name->Flex = 1.0f;
				name->MaxLength = 64;
				// Without a name, the layer is known by what it is
				name->PlaceholderFunc = [s]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? GetAutoLayerName(s->GetDocument(), s->GetCurrentLayer()) : String());
				};
				name->TextFunc = [s]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? layer->Name : String());
				};
				name->OnCommit = [s](StringView text) {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					if (layer != nullptr && layer->Name != text) {
						String newName = text;
						ChangeLayer(*s, [&newName](Formats::LayerRecord& record, String& name) { name = newName; });
					}
				};
			}
			{
				// TRANSLATORS: Label of a property in the layer properties panel, the width and the height of the layer in tiles
				auto* row = AddPropertyRow(rows, _x("editor", "Size"));
				auto* size = row->Add<UiLabel>();
				size->TextFunc = [s]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? String(FormatInteger(layer->GetWidth()) + " x "_s + FormatInteger(layer->GetHeight())) : String());
				};
			}
		}

		/** How the layer moves with the camera, a common way of it, the speeds, the offsets, the depth and the models */
		void AddLayerMovementRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			auto isMovable = [s]() { return IsMovableLayer(*s); };
			{
				// Common ways a layer moves, the speeds and the models are set at once
				struct Preset { const char* Label; float Speed; std::uint8_t Model; bool Repeat; };
				static const Preset Presets[] = {
					// TRANSLATORS: Choices of the Movement property in the layer properties panel, common ways a layer moves with the view
					{ NC_("editor", "Foreground"), 1.5f, std::uint8_t(Tiles::LayerSpeedModel::Default), false },
					{ NC_("editor", "With the sprite layer"), 1.0f, std::uint8_t(Tiles::LayerSpeedModel::Default), false },
					{ NC_("editor", "Near background"), 0.5f, std::uint8_t(Tiles::LayerSpeedModel::Default), true },
					{ NC_("editor", "Background"), 0.25f, std::uint8_t(Tiles::LayerSpeedModel::Default), true },
					{ NC_("editor", "Far background"), 0.125f, std::uint8_t(Tiles::LayerSpeedModel::Default), true },
					{ NC_("editor", "Fixed to screen"), 0.0f, std::uint8_t(Tiles::LayerSpeedModel::AlwaysOnTop), false },
					{ NC_("editor", "Fit to level"), 1.0f, std::uint8_t(Tiles::LayerSpeedModel::FitLevel), false },
				};
				auto matches = [](const Formats::LayerRecord& record, const Preset& preset) {
					std::uint8_t models = std::uint8_t(preset.Model | (preset.Model << 4));
					if (record.SpeedModels != models || record.AutoSpeedX != 0.0f || record.AutoSpeedY != 0.0f) {
						return false;
					}
					// Only the models matter for these, not the speeds
					if (preset.Model != std::uint8_t(Tiles::LayerSpeedModel::Default)) {
						return true;
					}
					return (record.SpeedX == preset.Speed && record.SpeedY == preset.Speed);
				};
				// TRANSLATORS: Label of a property in the layer properties panel, the common way the layer moves with the view
				auto* row = AddPropertyRow(rows, _x("editor", "Movement"));
				auto* dropdown = row->Add<UiDropdown>([s, matches]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					if (layer != nullptr) {
						for (const Preset& preset : Presets) {
							if (matches(layer->Record, preset)) {
								return String(_x("editor", preset.Label));
							}
						}
					}
					// TRANSLATORS: Shown as the Movement of a layer in the layer properties panel when it moves in none of the common ways
					return String(_x("editor", "Custom"));
				}, [s, matches](SmallVectorImpl<UiMenuEntry>& entries) {
					for (const Preset& preset : Presets) {
						const Preset* p = &preset;
						UiMenuEntry entry;
						entry.Label = _x("editor", preset.Label);
						entry.IsChecked = [s, p, matches]() {
							const DocumentLayer* layer = GetCurrentLayer(*s);
							return (layer != nullptr && matches(layer->Record, *p));
						};
						entry.Execute = [s, p]() {
							ChangeLayer(*s, [p](Formats::LayerRecord& record, String&) {
								if (p->Model == std::uint8_t(Tiles::LayerSpeedModel::Default)) {
									record.SpeedX = p->Speed;
									record.SpeedY = p->Speed;
								}
								record.AutoSpeedX = 0.0f;
								record.AutoSpeedY = 0.0f;
								record.SpeedModels = std::uint8_t(p->Model | (p->Model << 4));
								// A background slower than the level has to repeat to cover the view
								if (p->Repeat) {
									record.Flags |= Formats::LayerRecord::RepeatX | Formats::LayerRecord::RepeatY;
								}
							});
						};
						entries.push_back(Death::move(entry));
					}
				});
				dropdown->Flex = 1.0f;
				dropdown->IsEnabledFunc = isMovable;
				row->Tooltip = _x("editor", "Sets the speeds and the models of a common way a layer moves - Custom if they're none of these, as in most converted levels, the speeds and the models below say how it moves then");
			}

			struct FloatProperty {
				const char* Label;
				float Formats::LayerRecord::* Member;
				float Min, Max, Step;
				std::int32_t Decimals;
			};
			static const FloatProperty FloatProperties[] = {
				// TRANSLATORS: Labels of properties in the layer properties panel - speed with the view, speed by itself and offset, X is horizontal and Y vertical
				{ NC_("editor", "Speed X"), &Formats::LayerRecord::SpeedX, -16.0f, 16.0f, 0.01f, 3 },
				{ NC_("editor", "Speed Y"), &Formats::LayerRecord::SpeedY, -16.0f, 16.0f, 0.01f, 3 },
				{ NC_("editor", "Auto X"), &Formats::LayerRecord::AutoSpeedX, -64.0f, 64.0f, 0.05f, 2 },
				{ NC_("editor", "Auto Y"), &Formats::LayerRecord::AutoSpeedY, -64.0f, 64.0f, 0.05f, 2 },
				{ NC_("editor", "Offset X"), &Formats::LayerRecord::OffsetX, -32768.0f, 32767.0f, 1.0f, 0 },
				{ NC_("editor", "Offset Y"), &Formats::LayerRecord::OffsetY, -32768.0f, 32767.0f, 1.0f, 0 },
			};
			for (const FloatProperty& property : FloatProperties) {
				auto* row = AddPropertyRow(rows, _x("editor", property.Label));
				float Formats::LayerRecord::* member = property.Member;
				auto* number = row->Add<UiNumberBox>([s, member]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? layer->Record.*member : 0.0f);
				}, [s, member](float value, bool final) {
					ChangeLayer(*s, [member, value](Formats::LayerRecord& record, String&) { record.*member = value; }, final);
				}, property.Min, property.Max, property.Step, property.Decimals);
				number->Flex = 1.0f;
				number->IsEnabledFunc = isMovable;
			}
			{
				// TRANSLATORS: Label of a property in the layer properties panel, the order the layers are drawn in
				auto* row = AddPropertyRow(rows, _x("editor", "Depth"));
				auto* number = row->Add<UiNumberBox>([s]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? float(layer->Record.Depth) : 0.0f);
				}, [s](float value, bool final) {
					ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) { record.Depth = std::int16_t(value); }, final);
				}, -32768.0f, 32767.0f, 1.0f, 0);
				number->Flex = 1.0f;
				number->IsEnabledFunc = isMovable;
			}

			// How the layer moves with the camera, a model for each axis
			static const PropertyChoice ModelChoices[] = {
				// TRANSLATORS: Choices of the model of how a layer moves with the view along one axis, in the layer properties panel
				{ NC_("editor", "Speed"), std::int32_t(Tiles::LayerSpeedModel::Default) },
				{ NC_("editor", "Speed from level start"), std::int32_t(Tiles::LayerSpeedModel::FromStart) },
				{ NC_("editor", "Fixed to screen"), std::int32_t(Tiles::LayerSpeedModel::AlwaysOnTop) },
				{ NC_("editor", "Fit to level"), std::int32_t(Tiles::LayerSpeedModel::FitLevel) },
				{ NC_("editor", "Speed of view size"), std::int32_t(Tiles::LayerSpeedModel::SpeedMultipliers) },
			};
			for (std::int32_t axis = 0; axis < 2; axis++) {
				// TRANSLATORS: Labels of properties in the layer properties panel, the model of how the layer moves horizontally and vertically
				auto* row = AddPropertyRow(rows, axis == 0 ? _x("editor", "Model X") : _x("editor", "Model Y"));
				std::int32_t shift = axis * 4;
				auto* dropdown = AddChoiceDropdown(row, ModelChoices, [s, shift]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? std::int32_t((layer->Record.SpeedModels >> shift) & 0x0F) : 0);
				}, [s, shift](std::int32_t value) {
					ChangeLayer(*s, [shift, value](Formats::LayerRecord& record, String&) {
						record.SpeedModels = std::uint8_t((record.SpeedModels & ~(0x0F << shift)) | (value << shift));
					});
				}, [](std::int32_t value) { return FormatInteger(value); });
				dropdown->Flex = 1.0f;
				dropdown->IsEnabledFunc = isMovable;
			}
		}

		/** How the tiles of the layer are drawn */
		void AddLayerRendererRow(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			static const PropertyChoice RendererChoices[] = {
				// TRANSLATORS: Choices of the Renderer property in the layer properties panel, the ways the layer can be drawn
				{ NC_("editor", "Tiles"), std::int32_t(Tiles::LayerRendererType::Default) },
				{ NC_("editor", "Solid color"), std::int32_t(Tiles::LayerRendererType::Solid) },
				{ NC_("editor", "Tinted"), std::int32_t(Tiles::LayerRendererType::Tinted) },
				{ NC_("editor", "Palette shift"), std::int32_t(Tiles::LayerRendererType::PaletteShift) },
				{ NC_("editor", "X-ray"), std::int32_t(Tiles::LayerRendererType::XRay) },
				{ NC_("editor", "Blur behind"), std::int32_t(Tiles::LayerRendererType::BlurBehind) },
				{ NC_("editor", "Sky"), std::int32_t(Tiles::LayerRendererType::Sky) },
				{ NC_("editor", "Tunnel"), std::int32_t(Tiles::LayerRendererType::Tunnel) },
				{ NC_("editor", "Wave"), std::int32_t(Tiles::LayerRendererType::Wave) },
				{ NC_("editor", "Cylinder"), std::int32_t(Tiles::LayerRendererType::Cylinder) },
				{ NC_("editor", "Reflection"), std::int32_t(Tiles::LayerRendererType::Reflection) },
			};
			// TRANSLATORS: Label of a property in the layer properties panel, how the tiles of the layer are drawn
			auto* row = AddPropertyRow(rows, _x("editor", "Renderer"));
			auto* dropdown = AddChoiceDropdown(row, RendererChoices, [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return std::int32_t(layer != nullptr ? layer->Record.RendererType : 0);
			}, [s](std::int32_t type) {
				std::uint8_t value = std::uint8_t(type);
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) {
					constexpr std::uint8_t PaletteShift = std::uint8_t(Tiles::LayerRendererType::PaletteShift);
					constexpr std::uint8_t Wave = std::uint8_t(Tiles::LayerRendererType::Wave);
					constexpr std::uint8_t Reflection = std::uint8_t(Tiles::LayerRendererType::Reflection);
					if ((value != Reflection && record.RendererType == Reflection) || (value != Wave && record.RendererType == Wave)) {
						// The parameters of a reflection or a wave aren't a color, the tiles would be lost
						std::memset(record.Color, 255, sizeof(record.Color));
					}
					if (value == PaletteShift && record.RendererType != PaletteShift) {
						// Shifted by nothing at first; green and blue stay full, so a game that doesn't know the renderer
						// shows the layer much as it is (see Formats::LayerRecord::Color)
						bool hadOpacity = (record.RendererType == std::uint8_t(Tiles::LayerRendererType::Default));
						record.Color[0] = 0;
						record.Color[1] = 255;
						record.Color[2] = 255;
						record.Color[3] = (hadOpacity ? record.Color[3] : 255);
					} else if (value != PaletteShift && record.RendererType == PaletteShift) {
						// The shift isn't a color, the tiles would lose their red
						record.Color[0] = 255;
					}
					if (value == Reflection && record.RendererType != Reflection) {
						// Untinted, rippled a little by its tiles and not blurred at first (see Formats::LayerRecord::Color)
						record.Color[0] = 0;
						record.Color[1] = 0;
						record.Color[2] = 16;
						record.Color[3] = 0;
					}
					if (value == Wave && record.RendererType != Wave) {
						// Slow horizontal waves at first, of one of the wavelengths JJ2+ recommends (see Formats::LayerRecord::Color),
						// none of JJ2+ itself would show nothing different from tiles
						record.Color[0] = 63;
						record.Color[1] = 0;
						record.Color[2] = 2;
						record.Color[3] = 0;
					}
					record.RendererType = value;
				});
			}, [](std::int32_t value) {
				return FormatInteger(value);
			}, [s](std::int32_t value) {
				// The sprite layer only by those that draw its tiles where the objects walk on them
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer == nullptr || !layer->IsSpriteLayer() || Tiles::LayerDescription::IsSpriteLayerRenderer(Tiles::LayerRendererType(value)));
			});
			dropdown->Flex = 1.0f;
			// How it's drawn can be changed for the sprite layer too, unlike how it moves
			dropdown->IsEnabledFunc = [s]() { return IsCurrentLayerUnlocked(*s); };
			row->Tooltip = _x("editor", "How the tiles of the layer are drawn - the sprite layer only as tiles, in a solid color, tinted or with a palette shift, the others wouldn't show its tiles where they are");
		}

		/** What makes the layer drawn black, wrong or not at all, right under how it's drawn (see CheckLayerDrawing()) */
		void AddLayerProblemRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			// Found again only when the level or the layer changes, a blur behind layer looks its tiles up in their tile sets
			struct ProblemState {
				std::uint32_t Revision = UINT32_MAX;
				std::int32_t Layer = -2;
				String Warnings;
				String Notes;
			};
			auto state = std::make_shared<ProblemState>();
			auto update = [s, state]() {
				std::uint32_t revision = s->GetHistory().GetRevision();
				std::int32_t layerIndex = s->GetCurrentLayer();
				if (revision == state->Revision && layerIndex == state->Layer) {
					return;
				}
				state->Revision = revision;
				state->Layer = layerIndex;
				state->Warnings = {};
				state->Notes = {};

				SmallVector<LevelProblem, 4> problems;
				CheckLayerDrawing(s->GetDocument(), layerIndex, true, problems);
				if (RendererIs(*s, { Tiles::LayerRendererType::BlurBehind })) {
					std::int32_t indexed = CountPaletteColorTiles(*s, layerIndex);
					if (indexed > 0) {
						LevelProblem& problem = problems.emplace_back();
						problem.Severity = LevelProblemSeverity::Warning;
						problem.Text = _fnx("editor", "{} tile is in palette colors and does nothing - blur behind takes its effect from true color tiles",
							"{} tiles are in palette colors and do nothing - blur behind takes its effect from true color tiles", indexed, indexed);
					}
				}
				for (const LevelProblem& problem : problems) {
					String& text = (problem.Severity == LevelProblemSeverity::Info ? state->Notes : state->Warnings);
					text = (text.empty() ? problem.Text : String(text + "\n"_s + problem.Text));
				}
			};

			auto* warnings = rows->Add<UiWrappedLabel>(StringView(), UiTheme::TextDanger);
			warnings->TextFunc = [update, state]() {
				update();
				return state->Warnings;
			};
			warnings->IsVisibleFunc = [update, state]() {
				update();
				return !state->Warnings.empty();
			};
			auto* notes = rows->Add<UiWrappedLabel>(StringView(), UiTheme::TextDim);
			notes->TextFunc = [update, state]() {
				update();
				return state->Notes;
			};
			notes->IsVisibleFunc = [update, state]() {
				update();
				return !state->Notes.empty();
			};
		}

		/** The texture of a textured layer and the image of its own it may have */
		void AddLayerTextureRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			// A textured layer takes the tiles at its top left, unless it has an image of its own, such as a texture of JJ2+ of
			// a converted level (see DocumentLayer::Texture) - one can be imported, exported to be edited elsewhere, made of
			// the tiles or removed
			// TRANSLATORS: Label of a property in the layer properties panel, what a textured layer is textured with
			auto* row = AddPropertyRow(rows, _x("editor", "Texture"));
			auto* dropdown = row->Add<UiDropdown>([s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				// TRANSLATORS: Value of the Texture property of a layer, textured with an image of its own or with its own tiles
				return String(layer != nullptr && !layer->Texture.empty() ? _x("editor", "Own image") : _x("editor", "Its tiles"));
			}, [s](SmallVectorImpl<UiMenuEntry>& entries) {
				auto hasOwnImage = [s]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr && !layer->Texture.empty());
				};
				UiMenuEntry importImage;
				// TRANSLATORS: Entry in the Texture choice of a layer, opens an image file as its texture (verb)
				importImage.Label = _x("editor", "Import image");
				importImage.Execute = [s]() { ImportLayerTexture(*s); };
				entries.push_back(Death::move(importImage));
				UiMenuEntry exportImage;
				// TRANSLATORS: Entry in the Texture choice of a layer, saves its texture as an image file (verb)
				exportImage.Label = _x("editor", "Export image");
				exportImage.Execute = [s]() { ExportLayerTexture(*s); };
				entries.push_back(Death::move(exportImage));
				UiMenuEntry fromTiles;
				fromTiles.Label = _x("editor", "Make image of its tiles");
				fromTiles.Execute = [s]() {
					if (const DocumentLayer* layer = GetCurrentLayer(*s)) {
						// TRANSLATORS: Name of an undo step in the history, the tiles of a layer made into an image of its own
						SetCurrentLayerTexture(*s, MakeTextureFromTiles(*s, *layer), _x("editor", "Make layer texture of tiles"));
					}
				};
				entries.push_back(Death::move(fromTiles));
				UiMenuEntry useTiles;
				// TRANSLATORS: Entry in the Texture choice of a layer, drops the image of its own so its tiles texture it again
				useTiles.Label = _x("editor", "Use its tiles");
				useTiles.IsEnabled = hasOwnImage;
				// TRANSLATORS: Name of an undo step in the history, the image of its own of a layer removed
				useTiles.Execute = [s]() { SetCurrentLayerTexture(*s, {}, _x("editor", "Remove layer texture")); };
				entries.push_back(Death::move(useTiles));
			});
			dropdown->Flex = 1.0f;
			dropdown->IsEnabledFunc = [s]() { return IsCurrentLayerUnlocked(*s); };
			row->Tooltip = _x("editor", "What the layer is textured with - the 8x8 tiles at its top left, or an image of its own of 256 x 256 pixels in the colors of the palette, such as a texture of JJ2+ a converted level has; it can be exported, edited elsewhere and imported again");
			ShowRowWhen(row, [s]() {
				return RendererIs(*s, { Tiles::LayerRendererType::Sky, Tiles::LayerRendererType::Tunnel, Tiles::LayerRendererType::Wave,
					Tiles::LayerRendererType::Cylinder, Tiles::LayerRendererType::Reflection });
			});

			// The image itself right under it, if the layer has one
			auto* previewRow = AddPropertyRow(rows, StringView());
			auto* preview = previewRow->Add<LayerTexturePreview>(*s);
			preview->Flex = 1.0f;
			ShowRowWhen(previewRow, [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && !layer->Texture.empty() && RendererIs(*s, { Tiles::LayerRendererType::Sky, Tiles::LayerRendererType::Tunnel,
					Tiles::LayerRendererType::Wave, Tiles::LayerRendererType::Cylinder, Tiles::LayerRendererType::Reflection }));
			});
		}

		/** The color of the layer, as each renderer that has one takes it */
		void AddLayerColorRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			auto isUnlocked = [s]() { return IsCurrentLayerUnlocked(*s); };
			{
				// TRANSLATORS: Label of a property in the layer properties panel, the color the tiles of the layer are multiplied by
				auto* row = AddPropertyRow(rows, _x("editor", "Color"));
				AddLayerColorSwatch(row, *s, true)->IsEnabledFunc = isUnlocked;
				for (std::int32_t i = 0; i < 4; i++) {
					AddLayerColorByte(row, *s, i)->IsEnabledFunc = isUnlocked;
				}
				row->Tooltip = _x("editor", "Red, green, blue and alpha, the tiles are multiplied by it - an alpha below 255 makes the layer translucent, 0 invisible but still solid; a solid color layer draws its tiles in it");
				ShowRowWhen(row, [s]() {
					return !RendererIs(*s, { Tiles::LayerRendererType::Tinted, Tiles::LayerRendererType::PaletteShift, Tiles::LayerRendererType::Sky,
						Tiles::LayerRendererType::Tunnel, Tiles::LayerRendererType::Wave, Tiles::LayerRendererType::Cylinder,
						Tiles::LayerRendererType::Reflection });
				});
			}
			{
				// Every color of the tiles is moved through the palette, live as the number is dragged or the wheel turned
				// TRANSLATORS: Label of a property in the layer properties panel, how far the colors of the layer move through the palette (noun, not the key)
				auto* row = AddPropertyRow(rows, _x("editor", "Shift"));
				AddLayerColorByte(row, *s, 0)->IsEnabledFunc = isUnlocked;
				row->Tooltip = _x("editor", "How many entries of the palette each color of the tiles moves by, wrapping around - transparent pixels and tiles in true color stay as they are");
				ShowRowWhen(row, [s]() { return RendererIs(*s, { Tiles::LayerRendererType::PaletteShift }); });

				auto* opacityRow = AddPropertyRow(rows, _x("editor", "Opacity"));
				AddLayerColorByte(opacityRow, *s, 3)->IsEnabledFunc = isUnlocked;
				opacityRow->Tooltip = _x("editor", "How opaque the layer is, 255 is fully");
				ShowRowWhen(opacityRow, [s]() { return RendererIs(*s, { Tiles::LayerRendererType::PaletteShift }); });
			}
			{
				// A tinted layer takes the color from an entry of the palette, so it follows the palette as it changes
				// TRANSLATORS: Label of a property in the layer properties panel, the palette entry the layer is tinted with (noun)
				auto* row = AddPropertyRow(rows, _x("editor", "Tint"));
				row->Add<UiColorSwatch>([s]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					if (layer == nullptr) {
						return Colorf::White;
					}
					Colorf color = UiPaletteGrid::ToColor(ContentResolver::Get().GetPalettes()[layer->Record.Color[0]]);
					color.A *= layer->Record.Color[3] / 255.0f;
					return color;
				}, [s]() {
					UiSurface* surface = s->GetSurface();
					if (surface == nullptr) {
						return;
					}
					UiPaletteGrid::ShowPopup(*surface, surface->GetPointerPos(), []() {
						return ContentResolver::Get().GetPalettes().data();
					}, [s]() {
						const DocumentLayer* layer = GetCurrentLayer(*s);
						return (layer != nullptr ? std::int32_t(layer->Record.Color[0]) : -1);
					}, [s](std::int32_t index) {
						ChangeLayer(*s, [index](Formats::LayerRecord& record, String&) { record.Color[0] = std::uint8_t(index); });
					});
				})->Tooltip = _x("editor", "Pick from the palette");
				AddLayerColorByte(row, *s, 0)->Tooltip = _x("editor", "Index of the palette entry");
				row->Tooltip = _x("editor", "Entry of the palette the layer is tinted with");
				ShowRowWhen(row, [s]() { return RendererIs(*s, { Tiles::LayerRendererType::Tinted }); });

				// TRANSLATORS: Label of a property in the layer properties panel, how strongly the layer is tinted
				auto* strengthRow = AddPropertyRow(rows, _x("editor", "Strength"));
				AddLayerColorByte(strengthRow, *s, 3);
				strengthRow->Tooltip = _x("editor", "How much the layer is tinted, 255 is fully");
				ShowRowWhen(strengthRow, [s]() { return RendererIs(*s, { Tiles::LayerRendererType::Tinted }); });
			}
		}

		/** The horizon of a sky, a tunnel or a cylinder, and the stars over it */
		void AddLayerHorizonRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			// The background blends towards the color of the horizon in the distance
			// TRANSLATORS: Label of a property in the layer properties panel, the color of the horizon of a sky, tunnel or cylinder layer
			auto* row = AddPropertyRow(rows, _x("editor", "Horizon"));
			AddLayerColorSwatch(row, *s, false);
			for (std::int32_t i = 0; i < 3; i++) {
				AddLayerColorByte(row, *s, i);
			}
			row->Tooltip = _x("editor", "Red, green and blue of the horizon the background fades into");
			ShowRowWhen(row, [s]() { return RendererIs(*s, { Tiles::LayerRendererType::Sky, Tiles::LayerRendererType::Tunnel, Tiles::LayerRendererType::Cylinder }); });

			// TRANSLATORS: Checkbox in the layer properties panel, shows stars over the horizon of a sky or tunnel layer
			auto* stars = rows->Add<UiCheckBox>(_x("editor", "Stars"), [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && layer->Record.Color[3] != 0);
			}, [s](bool value) {
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) { record.Color[3] = (value ? 255 : 0); });
			});
			stars->Tooltip = _x("editor", "Shows a field of stars over the horizon");
			stars->IsVisibleFunc = [s]() { return RendererIs(*s, { Tiles::LayerRendererType::Sky, Tiles::LayerRendererType::Tunnel }); };
		}

		/** A tunnel, as the one of JJ2+, kept apart from the color (see Formats::TexturedLayerParams) */
		void AddLayerTunnelRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			auto isTunnel = [s]() { return RendererIs(*s, { Tiles::LayerRendererType::Tunnel }); };
			// TRANSLATORS: Label of a property in the layer properties panel, where the center of a tunnel or a cylinder layer is (noun)
			auto* centerRow = AddPropertyRow(rows, _x("editor", "Center"));
			AddTexturedFadePosition(centerRow, *s, 0);
			AddTexturedFadePosition(centerRow, *s, 1);
			centerRow->Tooltip = _x("editor", "Where the center of the tunnel is, in percent of the width and the height of the view");
			ShowRowWhen(centerRow, isTunnel);

			// TRANSLATORS: Checkbox in the layer properties panel, a tunnel or a cylinder layer fades into the color of the horizon in the distance
			auto* fade = rows->Add<UiCheckBox>(_x("editor", "Fade"), [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && layer->Record.Textured.Fade != 0);
			}, [s](bool value) {
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) {
					record.Textured.Fade = (value ? Formats::TexturedLayerParams::DefaultFade : 0);
				});
			});
			fade->Tooltip = _x("editor", "The circles fade into the color of the horizon in the distance, they go on into the center otherwise");
			fade->IsVisibleFunc = isTunnel;

			// TRANSLATORS: Checkbox in the layer properties panel, the circles of a tunnel layer spiral inwards
			auto* spiral = rows->Add<UiCheckBox>(_x("editor", "Spiral"), [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && (layer->Record.Textured.Flags & Formats::TexturedLayerParams::Spiral) != 0);
			}, [s](bool value) {
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) {
					record.Textured.Flags = std::uint8_t(value ? (record.Textured.Flags | Formats::TexturedLayerParams::Spiral)
						: (record.Textured.Flags & ~Formats::TexturedLayerParams::Spiral));
				});
			});
			spiral->Tooltip = _x("editor", "The circles spiral inwards into the distance");
			spiral->IsVisibleFunc = isTunnel;
		}

		/** A cylinder, as the one of JJ2+, kept apart from the color the same way */
		void AddLayerCylinderRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			auto isCylinder = [s]() { return RendererIs(*s, { Tiles::LayerRendererType::Cylinder }); };
			auto* centerRow = AddPropertyRow(rows, _x("editor", "Center"));
			AddTexturedFadePosition(centerRow, *s, 0);
			AddTexturedFadePosition(centerRow, *s, 1);
			centerRow->Tooltip = _x("editor", "Where the center of the horizon of the cylinder is, in percent of the width and the height of the view - far from the middle, the tube looks bent");
			ShowRowWhen(centerRow, isCylinder);

			auto* fade = rows->Add<UiCheckBox>(_x("editor", "Fade"), [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && layer->Record.Textured.Fade != 0);
			}, [s](bool value) {
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) {
					record.Textured.Fade = (value ? Formats::TexturedLayerParams::DefaultFade : 0);
				});
			});
			fade->Tooltip = _x("editor", "The far wall fades a little into the color of the horizon");
			fade->IsVisibleFunc = isCylinder;

			// TRANSLATORS: Checkbox in the layer properties panel, the texture of a cylinder layer is drawn at half size
			auto* halfSize = rows->Add<UiCheckBox>(_x("editor", "Half size"), [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && (layer->Record.Textured.Flags & Formats::TexturedLayerParams::HalfSize) != 0);
			}, [s](bool value) {
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) {
					record.Textured.Flags = std::uint8_t(value ? (record.Textured.Flags | Formats::TexturedLayerParams::HalfSize)
						: (record.Textured.Flags & ~Formats::TexturedLayerParams::HalfSize));
				});
			});
			halfSize->Tooltip = _x("editor", "The texture goes four times across the view instead of once, and the tube is more curved, as if seen from further away");
			halfSize->IsVisibleFunc = isCylinder;
		}

		/** A wave, as the one of JJ2+, its parameters in the color and apart from it (see Formats::LayerRecord::Color) */
		void AddLayerWaveRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			auto isWave = [s]() { return RendererIs(*s, { Tiles::LayerRendererType::Wave }); };

			auto* wavelengthRow = AddPropertyRow(rows, _x("editor", "Wavelength"));
			AddLayerColorByte(wavelengthRow, *s, 0)->Tooltip = _x("editor", "Of the waves that move the rows left and right");
			AddLayerColorByte(wavelengthRow, *s, 1)->Tooltip = _x("editor", "Of the waves that move the columns up and down");
			wavelengthRow->Tooltip = _x("editor", "How long the horizontal and the vertical waves are, one more pixel than the number, 0 for none - 255, 127, 63 and so on fit the texture best");
			ShowRowWhen(wavelengthRow, isWave);

			// The position of the fade of the other textured layers, JJ2+ keeps the amplitudes there
			auto* amplitudeRow = AddPropertyRow(rows, _x("editor", "Amplitude"));
			for (std::int32_t axis = 0; axis < 2; axis++) {
				auto* number = amplitudeRow->Add<UiNumberBox>([s, axis]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? (axis == 0 ? layer->Record.Textured.FadeX : layer->Record.Textured.FadeY) : 0.5f);
				}, [s, axis](float value, bool final) {
					ChangeLayer(*s, [axis, value](Formats::LayerRecord& record, String&) {
						(axis == 0 ? record.Textured.FadeX : record.Textured.FadeY) = value;
					}, final);
				}, -2.0f, 2.0f, 0.01f, 2);
				number->Flex = 1.0f;
			}
			amplitudeRow->Tooltip = _x("editor", "How far the horizontal and the vertical waves move the texture, 1 is 128 pixels each way");
			ShowRowWhen(amplitudeRow, isWave);

			// The blue of the color as a signed byte, as JJ2+ keeps it
			// TRANSLATORS: Label of a property in the layer properties panel, how fast the waves of a wave layer move (noun)
			auto* speedRow = AddPropertyRow(rows, _x("editor", "Speed"));
			auto* speed = speedRow->Add<UiNumberBox>([s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return float(layer != nullptr ? std::int8_t(layer->Record.Color[2]) : 0);
			}, [s](float value, bool final) {
				std::uint8_t byte = std::uint8_t(std::int8_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), -128, 127)));
				ChangeLayer(*s, [byte](Formats::LayerRecord& record, String&) { record.Color[2] = byte; }, final);
			}, -128.0f, 127.0f, 1.0f, 0);
			speed->Flex = 1.0f;
			speedRow->Tooltip = _x("editor", "How fast the waves move along the texture - a positive one moves the horizontal waves up and the vertical ones left");
			ShowRowWhen(speedRow, isWave);

			// TRANSLATORS: Checkbox in the layer properties panel, the vertical waves of a wave layer move the other way
			auto* angle = rows->Add<UiCheckBox>(_x("editor", "Swap angles"), [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && (layer->Record.Textured.Flags & Formats::TexturedLayerParams::DistortionAngle) != 0);
			}, [s](bool value) {
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) {
					record.Textured.Flags = std::uint8_t(value ? (record.Textured.Flags | Formats::TexturedLayerParams::DistortionAngle)
						: (record.Textured.Flags & ~Formats::TexturedLayerParams::DistortionAngle));
				});
			});
			angle->Tooltip = _x("editor", "The vertical waves move the other way, so both move up and right, or down and left");
			angle->IsVisibleFunc = isWave;
		}

		/** A reflection, as the one of JJ2+, its parameters in the color and apart from it (see Formats::LayerRecord::Color) */
		void AddLayerReflectionRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			auto isReflection = [s]() { return RendererIs(*s, { Tiles::LayerRendererType::Reflection }); };
			{
				static const PropertyChoice AreaChoices[] = {
					// TRANSLATORS: Choices of the Area property of a reflection layer, where it's drawn - over the whole view, the window the layer would fill, or the layer itself
					{ NC_("editor", "Whole view"), 0 },
					{ NC_("editor", "Window of the layer"), Formats::TexturedLayerParams::InnerWindow },
					{ NC_("editor", "The layer"), Formats::TexturedLayerParams::InnerLayer },
				};
				// TRANSLATORS: Label of a property in the layer properties panel, where a reflection layer is drawn
				auto* areaRow = AddPropertyRow(rows, _x("editor", "Area"));
				auto* areaDropdown = AddChoiceDropdown(areaRow, AreaChoices, [s]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return std::int32_t(layer != nullptr ? GetReflectionArea(layer->Record) : 0);
				}, [s](std::int32_t flags) {
					ChangeLayer(*s, [flags](Formats::LayerRecord& record, String&) {
						record.Textured.Flags = std::uint8_t((record.Textured.Flags & ~(Formats::TexturedLayerParams::InnerWindow |
							Formats::TexturedLayerParams::InnerLayer)) | flags);
					});
				});
				areaDropdown->Flex = 1.0f;
				areaRow->Tooltip = _x("editor", "Where the reflection is drawn - over the whole view, or only where the layer is placed as an ordinary layer, its horizon and vanishing point a part of the view or of the layer");
				ShowRowWhen(areaRow, isReflection);
			}

			auto* horizonRow = AddPropertyRow(rows, _x("editor", "Horizon line"));
			AddTexturedFadePosition(horizonRow, *s, 1);
			horizonRow->Tooltip = _x("editor", "How far down the view the horizon is in percent, or down the layer if it's drawn where the layer is - what is above it is mirrored below it");
			ShowRowWhen(horizonRow, isReflection);

			auto* vanishingRow = AddPropertyRow(rows, _x("editor", "Vanishing point"));
			AddTexturedFadePosition(vanishingRow, *s, 0);
			vanishingRow->Tooltip = _x("editor", "Where the ripples meet at the horizon, in percent of the width of the view, or of the layer if it's drawn where the layer is");
			ShowRowWhen(vanishingRow, isReflection);

			// The tint is an entry of the palette, so it follows the palette as it changes
			auto* tintRow = AddPropertyRow(rows, _x("editor", "Tint"));
			tintRow->Add<UiColorSwatch>([s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				if (layer == nullptr) {
					return Colorf::White;
				}
				Colorf color = UiPaletteGrid::ToColor(ContentResolver::Get().GetPalettes()[layer->Record.Textured.Fade]);
				color.A *= layer->Record.Color[0] / 255.0f;
				return color;
			}, [s]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				UiPaletteGrid::ShowPopup(*surface, surface->GetPointerPos(), []() {
					return ContentResolver::Get().GetPalettes().data();
				}, [s]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? std::int32_t(layer->Record.Textured.Fade) : -1);
				}, [s](std::int32_t index) {
					ChangeLayer(*s, [index](Formats::LayerRecord& record, String&) { record.Textured.Fade = std::uint8_t(index); });
				});
			})->Tooltip = _x("editor", "Pick from the palette");
			auto* tintIndex = AddByteBox(tintRow, [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return std::uint8_t(layer != nullptr ? layer->Record.Textured.Fade : 0);
			}, [s](std::uint8_t index, bool final) {
				ChangeLayer(*s, [index](Formats::LayerRecord& record, String&) { record.Textured.Fade = index; }, final);
			});
			tintIndex->Flex = 1.0f;
			tintIndex->Tooltip = _x("editor", "Index of the palette entry");
			tintRow->Tooltip = _x("editor", "Entry of the palette the reflection is tinted with");
			ShowRowWhen(tintRow, isReflection);

			struct ReflectionByte { const char* Label; const char* Tooltip; std::int32_t Index; };
			static const ReflectionByte ReflectionBytes[] = {
				// TRANSLATORS: Labels of properties of a reflection layer in the layer properties panel, each followed by its tooltip
				{ NC_("editor", "Tint opacity"), NC_("editor", "How much the reflection is tinted, 255 is fully"), 0 },
				{ NC_("editor", "Distance"), NC_("editor", "How far away the plane of the reflection is, the farther, the smaller its ripples"), 1 },
				{ NC_("editor", "Distortion"), NC_("editor", "How far the ripples move what is mirrored, by how much each palette index of the texture differs from the one half of the texture away"), 2 },
			};
			for (const ReflectionByte& property : ReflectionBytes) {
				auto* row = AddPropertyRow(rows, _x("editor", property.Label));
				AddLayerColorByte(row, *s, property.Index);
				row->Tooltip = _x("editor", property.Tooltip);
				ShowRowWhen(row, isReflection);
			}

			// Drawn only where the layer is, its speeds place the layer, so the ripples move inside it by speeds of their own, as
			// the speeds of the layer move them over the whole view (see Formats::TexturedLayerParams::InnerSpeedX)
			struct InnerSpeed { const char* Label; const char* Tooltip; float Formats::TexturedLayerParams::* Member; float Min, Max, Step; std::int32_t Decimals; };
			static const InnerSpeed InnerSpeeds[] = {
				// TRANSLATORS: Labels of properties of a reflection layer drawn only where the layer is, speeds of the ripples inside it (X horizontal, Y vertical), each followed by its tooltip
				{ NC_("editor", "Inner speed X"), NC_("editor", "How fast the ripples move to the left inside the layer as the view moves right"), &Formats::TexturedLayerParams::InnerSpeedX, -16.0f, 16.0f, 0.01f, 3 },
				{ NC_("editor", "Inner speed Y"), NC_("editor", "How fast the ripples move towards the horizon inside the layer as the view moves down"), &Formats::TexturedLayerParams::InnerSpeedY, -16.0f, 16.0f, 0.01f, 3 },
				{ NC_("editor", "Inner auto X"), NC_("editor", "How fast the ripples move to the left inside the layer by themselves"), &Formats::TexturedLayerParams::InnerAutoSpeedX, -64.0f, 64.0f, 0.05f, 2 },
				{ NC_("editor", "Inner auto Y"), NC_("editor", "How fast the ripples move towards the horizon inside the layer by themselves"), &Formats::TexturedLayerParams::InnerAutoSpeedY, -64.0f, 64.0f, 0.05f, 2 },
			};
			auto isInnerReflection = [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && RendererIs(*s, { Tiles::LayerRendererType::Reflection }) &&
					(layer->Record.Textured.Flags & (Formats::TexturedLayerParams::InnerWindow | Formats::TexturedLayerParams::InnerLayer)) != 0);
			};
			for (const InnerSpeed& property : InnerSpeeds) {
				auto* row = AddPropertyRow(rows, _x("editor", property.Label));
				float Formats::TexturedLayerParams::* member = property.Member;
				auto* number = row->Add<UiNumberBox>([s, member]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr ? layer->Record.Textured.*member : 0.0f);
				}, [s, member](float value, bool final) {
					ChangeLayer(*s, [member, value](Formats::LayerRecord& record, String&) { record.Textured.*member = value; }, final);
				}, property.Min, property.Max, property.Step, property.Decimals);
				number->Flex = 1.0f;
				row->Tooltip = _x("editor", property.Tooltip);
				ShowRowWhen(row, isInnerReflection);
			}

			// TRANSLATORS: Checkbox in the layer properties panel, blurs what a reflection layer mirrors
			auto* blur = rows->Add<UiCheckBox>(_x("editor", "Blur"), [s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr && layer->Record.Color[3] != 0);
			}, [s](bool value) {
				ChangeLayer(*s, [value](Formats::LayerRecord& record, String&) { record.Color[3] = (value ? 255 : 0); });
			});
			blur->Tooltip = _x("editor", "Blurs what is mirrored horizontally");
			blur->IsVisibleFunc = isReflection;
		}

		/** What only the editor uses, the level isn't changed by it */
		void AddLayerEditorRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			// Only how the editor shows it, the layers in front can be seen through to edit those behind; for any renderer, so
			// it's after the rows of the color of each of them, right below the color itself where it has one
			// TRANSLATORS: Label of a property in the layer properties panel, how opaque the editor shows the layer, not the game
			auto* row = AddPropertyRow(rows, _x("editor", "Editor opacity"));
			auto* number = row->Add<UiNumberBox>([s]() {
				const DocumentLayer* layer = GetCurrentLayer(*s);
				return (layer != nullptr ? std::round(layer->EditorOpacity * 100.0f / 255.0f) : 100.0f);
			}, [s](float value, bool final) {
				std::uint8_t opacity = std::uint8_t(std::clamp<std::int32_t>(std::int32_t(std::round(value * 255.0f / 100.0f)), 0, 255));
				SetLayerEditorOpacity(*s, s->GetCurrentLayer(), opacity);
			}, 0.0f, 100.0f, 5.0f, 0);
			number->Flex = 1.0f;
			row->Tooltip = _x("editor", "How opaque the editor shows the layer in percent, the level isn't changed by it");
		}

		/** How the layer repeats, whether it has the inherent offset and whether it's shown in the game */
		void AddLayerFlagRows(UiForm* rows, IEditorContext& session)
		{
			IEditorContext* s = &session;
			struct FlagProperty {
				const char* Label;
				std::uint16_t Flag;
			};
			static const FlagProperty FlagProperties[] = {
				// TRANSLATORS: Checkboxes in the layer properties panel, the inherent offset moves the layer down in views taller than the original game had
				{ NC_("editor", "Repeat horizontally"), Formats::LayerRecord::RepeatX },
				{ NC_("editor", "Repeat vertically"), Formats::LayerRecord::RepeatY },
				{ NC_("editor", "Inherent offset"), Formats::LayerRecord::UseInherentOffset },
				{ NC_("editor", "Visible in game"), Formats::LayerRecord::Visible },
			};
			for (const FlagProperty& property : FlagProperties) {
				std::uint16_t flag = property.Flag;
				auto* check = rows->Add<UiCheckBox>(_x("editor", property.Label), [s, flag]() {
					const DocumentLayer* layer = GetCurrentLayer(*s);
					return (layer != nullptr && (layer->Record.Flags & flag) != 0);
				}, [s, flag](bool value) {
					ChangeLayer(*s, [flag, value](Formats::LayerRecord& record, String&) {
						record.Flags = (value ? (record.Flags | flag) : (record.Flags & ~flag));
					});
				});
				// Whether it's shown in the game can be changed for the sprite layer too, unlike how it moves
				check->IsEnabledFunc = (flag != Formats::LayerRecord::Visible
					? Function<bool()>([s]() { return IsMovableLayer(*s); })
					: Function<bool()>([s]() { return IsCurrentLayerUnlocked(*s); }));
			}
		}
	}

	InspectorPanel::InspectorPanel(IEditorContext& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session)
	{
		// The edge above, between it and the list of layers, is drawn by the splitter that resizes them
		Add<UiLabel>(_x("editor", "Layer Properties"), UiTheme::TextHeader);

		auto* scroll = Add<UiScrollView>();
		scroll->Flex = 1.0f;
		auto* rows = scroll->SetContent<UiForm>(2.0f, UiThickness(0.0f, 0.0f, 2.0f, 0.0f));

		// How the layer is placed and moves, how it's drawn by each renderer, then what only the editor uses and the flags
		AddLayerNameRows(rows, session);
		AddLayerMovementRows(rows, session);
		AddLayerRendererRow(rows, session);
		AddLayerProblemRows(rows, session);
		AddLayerTextureRows(rows, session);
		AddLayerColorRows(rows, session);
		AddLayerHorizonRows(rows, session);
		AddLayerTunnelRows(rows, session);
		AddLayerWaveRows(rows, session);
		AddLayerCylinderRows(rows, session);
		AddLayerReflectionRows(rows, session);
		AddLayerEditorRows(rows, session);
		AddLayerFlagRows(rows, session);
	}

	// Frames without a change of the tiles before they're counted again, all tiles of all layers are, which takes a while
	// in a large level, so not while painting
	constexpr float TileUsageQuietFrames = 20.0f;

	TileGrid::TileGrid(IEditorContext& session)
		: ShowUsage(false), _usageMax(0), _usageDirty(true), _usageCooldown(0.0f), _usageSource(0xFF), _session(session),
			_source(TileRef::DefaultTileSet), _tileSize(32.0f), _dragging(false)
	{
		_changeObserver = std::make_shared<Function<void(const DocumentChanges&)>>([this](const DocumentChanges& changes) {
			if (changes.HasAny(DocumentChangeKind::Tiles | DocumentChangeKind::LayerStructure | DocumentChangeKind::AnimationStructure |
				DocumentChangeKind::TileSets)) {
				_usageDirty = true;
				// Painting changes the tiles every frame, they're counted again once it stops for a moment
				_usageCooldown = TileUsageQuietFrames;
			}
		});
		session.AddChangeObserver(_changeObserver);
	}

	void TileGrid::Update(float timeMult)
	{
		_usageCooldown = std::max(_usageCooldown - timeMult, 0.0f);
		UiElement::Update(timeMult);
	}

	void TileGrid::UpdateUsage() const
	{
		// Counted again once the tiles stop changing for a while, see TileUsageQuietFrames, or for another tile set at once
		if (_source == _usageSource && (!_usageDirty || _usageCooldown > 0.0f)) {
			return;
		}
		_usageDirty = false;
		_usageSource = _source;
		_usage.clear();
		_usage.resize(std::size_t(std::max<std::int32_t>(GetTileCount(), 0)), 0);
		_usageMax = 0;

		const LevelDocument& document = _session.GetDocument();
		auto count = [this](const TileRef& tile) {
			if (tile.Source == _source && tile.Index < _usage.size() && !tile.IsEmpty()) {
				_usage[tile.Index]++;
			}
		};
		for (const DocumentLayer& layer : document.Layers) {
			for (const TileRef& tile : layer.Tiles) {
				count(tile);
			}
		}
		// A tile only in an animation is used too
		for (const DocumentAnimation& animation : document.Animations) {
			for (const TileRef& frame : animation.Frames) {
				count(frame);
			}
		}
		for (std::int32_t used : _usage) {
			_usageMax = std::max(_usageMax, used);
		}
	}

	void TileGrid::SetSource(std::uint8_t source)
	{
		_source = source;
		_dragging = false;
	}

	std::int32_t TileGrid::GetColumns() const
	{
		return std::max<std::int32_t>(_session.GetSettings().TilePickerColumns, 1);
	}

	void TileGrid::SetColumns(std::int32_t columns)
	{
		EditorSettings& settings = _session.GetSettings();
		columns = std::clamp<std::int32_t>(columns, 1, 32);
		if (settings.TilePickerColumns != columns) {
			settings.TilePickerColumns = columns;
			settings.Save();
		}
		_dragging = false;
	}

	std::int32_t TileGrid::GetTileCount() const
	{
		LevelDocument& document = _session.GetDocument();
		if (_source == TileRef::Animated) {
			return std::int32_t(document.Animations.size());
		}
		std::int32_t count = document.GetSourceTileCount(_source);
		if (count < 0 && _source == TileRef::DefaultTileSet) {
			if (Tiles::TileMap* tileMap = _session.GetTileMap()) {
				count = tileMap->GetTileSetTileCount(0);
			}
		}
		return std::max<std::int32_t>(count, 0);
	}

	float TileGrid::GetTileSize(const UiContext& context, float availableWidth) const
	{
		// As large as the width allows for the number of tiles in a row
		return std::max(std::floor(availableWidth / GetColumns()), 4.0f);
	}

	Vector2f TileGrid::Measure(const UiContext& context, Vector2f available)
	{
		std::int32_t columns = GetColumns();
		_tileSize = GetTileSize(context, available.X);
		std::int32_t rows = (GetTileCount() + columns - 1) / columns;
		return Vector2f(available.X, rows * _tileSize);
	}

	Vector2i TileGrid::GetCellAt(Vector2f pos) const
	{
		return Vector2i(std::int32_t(std::floor((pos.X - Bounds.X) / _tileSize)), std::int32_t(std::floor((pos.Y - Bounds.Y) / _tileSize)));
	}

	void TileGrid::Draw(UiPainter& painter)
	{
		Tiles::TileMap* tileMap = _session.GetTileMap();
		if (tileMap == nullptr) {
			return;
		}

		LevelDocument& document = _session.GetDocument();
		std::int32_t columns = GetColumns();
		std::int32_t count = GetTileCount();
		std::int32_t rows = (count + columns - 1) / columns;
		float width = _tileSize * columns;

		// Only the rows that are seen
		const Rectf& clip = painter.GetClip();
		float clipTop = (clip.H > 0.0f ? clip.Y : Bounds.Y);
		float clipBottom = (clip.H > 0.0f ? clip.Y + clip.H : Bounds.Y + Bounds.H);
		std::int32_t firstRow = std::max<std::int32_t>(0, std::int32_t((clipTop - Bounds.Y) / _tileSize));
		std::int32_t lastRow = std::min(rows - 1, std::int32_t((clipBottom - Bounds.Y) / _tileSize));

		painter.FillRect(Rectf(Bounds.X, Bounds.Y, width, rows * _tileSize), Colorf(0.0f, 0.0f, 0.0f, 0.35f), UiLayer::Fill);
		for (std::int32_t row = firstRow; row <= lastRow; row++) {
			for (std::int32_t column = 0; column < columns; column++) {
				std::int32_t index = row * columns + column;
				if (index >= count) {
					break;
				}
				TileRef tile{ std::uint16_t(index), _source, 0 };
				Rectf rect(Bounds.X + column * _tileSize, Bounds.Y + row * _tileSize, _tileSize, _tileSize);
				// By where it comes from, a tile past the IDs the level can address yet is still drawn as itself
				DrawTile(painter, *tileMap, document, tile, 0, rect);
			}
		}

		if (ShowUsage) {
			// Unused tiles darkened, used ones with a bar as long as how often, on a logarithmic scale
			UpdateUsage();
			float barHeight = std::max(2.0f, std::round(_tileSize / 8.0f));
			float maxLog = std::log(float(std::max<std::int32_t>(_usageMax, 1)) + 1.0f);
			for (std::int32_t row = firstRow; row <= lastRow; row++) {
				for (std::int32_t column = 0; column < columns; column++) {
					std::int32_t index = row * columns + column;
					if (index >= count || index >= std::int32_t(_usage.size())) {
						break;
					}
					Rectf rect(Bounds.X + column * _tileSize, Bounds.Y + row * _tileSize, _tileSize, _tileSize);
					std::int32_t used = _usage[index];
					if (used == 0) {
						painter.FillRect(rect, Colorf(0.0f, 0.0f, 0.0f, 0.65f), UiLayer::Content);
					} else {
						float fraction = std::log(float(used) + 1.0f) / maxLog;
						painter.FillRect(Rectf(rect.X, rect.Y + rect.H - barHeight, std::max(barHeight, std::round(rect.W * fraction)), barHeight),
							Colorf(0.4f + 0.6f * fraction, 0.9f - 0.5f * fraction, 0.3f, 0.95f), UiLayer::Overlay);
					}
				}
			}
		}

		// Favourite tiles have a mark in a corner
		{
			StringView tileSet;
			std::int32_t offset;
			if (GetSourceTileSet(document, _source, tileSet, offset)) {
				for (const EditorSettings::TileSetFavorites& favorites : _session.GetSettings().TileFavorites) {
					if (favorites.TileSet != tileSet) {
						continue;
					}
					float markRadius = std::max(1.5f, std::round(_tileSize / 12.0f));
					for (std::uint16_t stored : favorites.Favorites) {
						std::int32_t index = std::int32_t(stored) - offset;
						if (index < 0 || index >= count || index / columns < firstRow || index / columns > lastRow) {
							continue;
						}
						Vector2f corner(Bounds.X + (index % columns + 1) * _tileSize - markRadius * 1.5f, Bounds.Y + (index / columns) * _tileSize + markRadius * 1.5f);
						painter.Circle(corner, markRadius + 1.0f, Colorf(0.0f, 0.0f, 0.0f, 0.6f), UiLayer::Overlay);
						painter.Circle(corner, markRadius, Colorf(1.0f, 0.85f, 0.3f, 1.0f), UiLayer::Overlay);
					}
					break;
				}
			}
		}

		// The tiles the stamp was picked from, while it's still them
		const TileStamp& stamp = _session.GetStamp();
		if (!stamp.IsEmpty() && stamp.Tiles[0].Source == _source && !_dragging) {
			std::int32_t first = stamp.Tiles[0].Index;
			TileStamp expected = TileStamp::FromTileSet(_source, columns, Recti(first % columns, first / columns, stamp.Width, stamp.Height));
			bool same = (expected.Width == stamp.Width && expected.Height == stamp.Height);
			for (std::size_t i = 0; same && i < stamp.Tiles.size(); i++) {
				same = (stamp.Tiles[i].Index == expected.Tiles[i].Index && stamp.Tiles[i].Source == _source && stamp.Tiles[i].Flags == 0);
			}
			if (same) {
				Rectf rect(Bounds.X + (first % columns) * _tileSize, Bounds.Y + (first / columns) * _tileSize, stamp.Width * _tileSize, stamp.Height * _tileSize);
				painter.FillRect(rect, UiTheme::Selection, UiLayer::Overlay);
				painter.Frame(rect, UiTheme::Accent, std::max(1.0f, std::round(painter.GetScale())), UiLayer::Overlay);
			}
		}

		float thickness = std::max(1.0f, std::round(painter.GetScale()));
		if (_dragging) {
			std::int32_t x1 = std::min(_dragStart.X, _dragEnd.X), y1 = std::min(_dragStart.Y, _dragEnd.Y);
			std::int32_t x2 = std::max(_dragStart.X, _dragEnd.X), y2 = std::max(_dragStart.Y, _dragEnd.Y);
			Rectf rect(Bounds.X + x1 * _tileSize, Bounds.Y + y1 * _tileSize, (x2 - x1 + 1) * _tileSize, (y2 - y1 + 1) * _tileSize);
			painter.Frame(rect, UiTheme::Accent, thickness, UiLayer::Overlay);
		} else if (_hovered) {
			if (UiSurface* surface = GetSurface()) {
				Vector2i cell = GetCellAt(surface->GetPointerPos());
				if (cell.X >= 0 && cell.X < columns && cell.Y >= 0 && cell.Y * columns + cell.X < count) {
					painter.Frame(Rectf(Bounds.X + cell.X * _tileSize, Bounds.Y + cell.Y * _tileSize, _tileSize, _tileSize), UiTheme::Cursor, thickness, UiLayer::Overlay);
				}
			}
		}
	}

	bool TileGrid::AcceptsPointer() const
	{
		return true;
	}

	bool TileGrid::OnPointerDown(const UiPointerEvent& event)
	{
		Vector2i cell = GetCellAt(event.Pos);
		std::int32_t columns = GetColumns();
		if (cell.X < 0 || cell.X >= columns || cell.Y < 0 || cell.Y * columns + cell.X >= GetTileCount()) {
			return false;
		}
		if (event.Button == MouseButton::Right) {
			// A favourite, kept above the tiles
			ToggleFavoriteTile(_session, _source, std::uint16_t(cell.Y * columns + cell.X));
			return false;
		}
		if (event.Button != MouseButton::Left) {
			return false;
		}
		if (event.Clicks >= 2) {
			// The pixels and the mask of the tile, changed only in this level
			ShowTileEditor(_session, TileRef{ std::uint16_t(cell.Y * columns + cell.X), _source, 0 });
			return false;
		}
		_dragging = true;
		_dragStart = _dragEnd = cell;
		return true;
	}

	void TileGrid::OnPointerMove(const UiPointerEvent& event)
	{
		if (_dragging) {
			Vector2i cell = GetCellAt(event.Pos);
			std::int32_t columns = GetColumns();
			std::int32_t rows = (GetTileCount() + columns - 1) / columns;
			_dragEnd = Vector2i(std::clamp<std::int32_t>(cell.X, 0, columns - 1), std::clamp<std::int32_t>(cell.Y, 0, std::max<std::int32_t>(rows - 1, 0)));
		}
	}

	void TileGrid::OnPointerUp(const UiPointerEvent& event, bool inside)
	{
		if (!_dragging) {
			return;
		}
		_dragging = false;

		std::int32_t x1 = std::min(_dragStart.X, _dragEnd.X), y1 = std::min(_dragStart.Y, _dragEnd.Y);
		std::int32_t x2 = std::max(_dragStart.X, _dragEnd.X), y2 = std::max(_dragStart.Y, _dragEnd.Y);
		TileStamp stamp = TileStamp::FromTileSet(_source, GetColumns(), Recti(x1, y1, x2 - x1 + 1, y2 - y1 + 1));
		std::int32_t count = GetTileCount();
		for (std::size_t i = 0; i < stamp.Tiles.size(); i++) {
			if (stamp.Tiles[i].Index >= count) {
				stamp.Present[i] = false;
			}
		}
		// The first tile picked is remembered as a recent one of its tile set
		StringView tileSet;
		std::int32_t offset;
		if (!stamp.IsEmpty() && GetSourceTileSet(_session.GetDocument(), _source, tileSet, offset)) {
			EditorSettings& settings = _session.GetSettings();
			settings.AddRecentTile(tileSet, std::uint16_t(stamp.Tiles[0].Index + offset));
			settings.Save();
		}
		_session.SetStamp(Death::move(stamp));

		// Picking tiles means painting with them
		EditorToolType tool = _session.GetTools().GetChosenType();
		if (tool != EditorToolType::Brush && tool != EditorToolType::Rectangle && tool != EditorToolType::Fill) {
			_session.GetTools().SetActive(EditorToolType::Brush);
		}
	}

	bool TileGrid::OnWheel(const UiPointerEvent& event)
	{
		if ((event.Modifiers & KeyMod::Ctrl) == 0) {
			return false;
		}
		// Up for fewer, so larger tiles
		SetColumns(GetColumns() + (event.Wheel.Y > 0.0f ? -1 : 1));
		return true;
	}

	StringView TileGrid::GetTooltip(Vector2f pos) const
	{
		Vector2i cell = GetCellAt(pos);
		std::int32_t columns = GetColumns();
		std::int32_t index = cell.Y * columns + cell.X;
		if (cell.X < 0 || cell.X >= columns || cell.Y < 0 || index >= GetTileCount()) {
			return {};
		}
		if (ShowUsage) {
			UpdateUsage();
			std::int32_t used = (index < std::int32_t(_usage.size()) ? _usage[index] : 0);
			// TRANSLATORS: Tooltip of a tile in the tile picker while its usage is shown, the first {} is the number of the tile, the second how many times the level uses it
			_tooltip = (used == 0 ? _fx("editor", "Tile {}, not used", index) : _fnx("editor", "Tile {}, used once", "Tile {}, used {} times", used, index, used));
		} else {
			_tooltip = _fx("editor", "Tile {}", index);
		}
		return _tooltip;
	}

	namespace
	{
		/** Tiles the levels have next to the tiles around the cell under the pointer, a click makes one the brush */
		class SuggestionStrip : public UiElement
		{
		public:
			static constexpr std::int32_t MaxTiles = 8;

			explicit SuggestionStrip(IEditorContext& session)
				: _session(session)
			{
				// Always there, so the tiles below don't move when something is suggested
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(available.X, context.Px(UiTheme::ButtonHeight + 4.0f));
			}

			void Update(float timeMult) override
			{
				UiElement::Update(timeMult);
				// While the pointer is over the view, it keeps what it suggested when it moves here to pick one
				ToolPointer pointer;
				if (_session.IsPlaying() || !_session.GetHoverPointer(pointer)) {
					return;
				}
				EditorToolType tool = _session.GetTools().GetActiveType();
				if (tool != EditorToolType::Brush && tool != EditorToolType::Rectangle) {
					_tiles.clear();
					return;
				}
				std::int32_t layerIndex = _session.GetCurrentLayer();
				LayerView view = _session.GetLayerView(layerIndex);
				Vector2i cell;
				LevelDocument& document = _session.GetDocument();
				if (!view.Wrap(pointer.Cell, cell) || layerIndex < 0 || layerIndex >= std::int32_t(document.Layers.size())) {
					return;
				}
				if (cell == _cell && layerIndex == _layer && _session.GetHistory().GetRevision() == _revision) {
					return;
				}
				_cell = cell;
				_layer = layerIndex;
				_revision = _session.GetHistory().GetRevision();
				const DocumentLayer& layer = document.Layers[layerIndex];
				auto at = [&layer](std::int32_t x, std::int32_t y) -> const TileRef* {
					return (layer.Contains(x, y) && !layer.GetTile(x, y).IsEmpty() ? &layer.GetTile(x, y) : nullptr);
				};
				_session.GetSmartTiles().Suggest(at(cell.X - 1, cell.Y), at(cell.X, cell.Y - 1), at(cell.X + 1, cell.Y), at(cell.X, cell.Y + 1), MaxTiles, _tiles);
			}

			void Draw(UiPainter& painter) override
			{
				Tiles::TileMap* tileMap = _session.GetTileMap();
				if (tileMap == nullptr) {
					return;
				}
				float size = Bounds.H - painter.Px(4.0f);
				// TRANSLATORS: Before tiles suggested to place next to the tiles around the cell under the pointer, keep it short
				float label = painter.MeasureText(_x("editor", "Next:")) + painter.Px(UiTheme::Spacing);
				painter.TextInRect(_x("editor", "Next:"), Rectf(Bounds.X, Bounds.Y, label, Bounds.H), _tiles.empty() ? UiTheme::TextDisabled : UiTheme::TextDim);
				if (_tiles.empty()) {
					// TRANSLATORS: In the row of suggested tiles while there's nothing to suggest, keep it short
					String hint = _x("editor", "point at the level");
					if (label + painter.MeasureText(hint) <= Bounds.W) {
						painter.TextInRect(hint, Rectf(Bounds.X + label, Bounds.Y, Bounds.W - label, Bounds.H), UiTheme::TextDisabled);
					}
				}
				std::int32_t hovered = (_hovered && GetSurface() != nullptr ? GetIndexAt(GetSurface()->GetPointerPos()) : -1);
				for (std::int32_t i = 0; i < std::int32_t(_tiles.size()); i++) {
					Rectf rect(Bounds.X + label + i * (size + painter.Px(2.0f)), Bounds.Y + painter.Px(2.0f), size, size);
					if (rect.X + rect.W > Bounds.X + Bounds.W) {
						break;
					}
					DrawTile(painter, *tileMap, _session.GetDocument(), _tiles[i], _tiles[i].Flags, rect, 1.0f, UiLayer::Content);
					if (i == hovered) {
						painter.Frame(rect, UiTheme::Accent, std::max(1.0f, std::round(painter.GetScale())), UiLayer::Overlay);
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
				if (index >= 0) {
					_session.SetStamp(TileStamp::FromTile(_tiles[index]));
				}
				return false;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				return _x("editor", "Tiles the levels have next to the tiles around the cell under the pointer, click one to paint with it");
			}

		private:
			IEditorContext& _session;
			SmallVector<TileRef, 8> _tiles;
			Vector2i _cell{ -1, -1 };
			std::int32_t _layer = -1;
			std::uint32_t _revision = ~0u;

			std::int32_t GetIndexAt(Vector2f pos) const
			{
				UiContext context = (GetSurface() != nullptr ? GetSurface()->GetContext() : UiContext{ 1.0f });
				float size = Bounds.H - context.Px(4.0f);
				float label = context.MeasureText(_x("editor", "Next:")) + context.Px(UiTheme::Spacing);
				if (!Bounds.Contains(pos) || pos.X < Bounds.X + label) {
					return -1;
				}
				std::int32_t index = std::int32_t((pos.X - Bounds.X - label) / (size + context.Px(2.0f)));
				return (index >= 0 && index < std::int32_t(_tiles.size()) ? index : -1);
			}
		};

		/** Favourite tiles of the tile set shown and those picked recently, a click makes one the brush, a right click a favourite or not */
		class TileShortcutStrip : public UiElement
		{
		public:
			TileShortcutStrip(IEditorContext& session, TileGrid*& grid)
				: _session(session), _grid(grid), _favoriteCount(0)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(available.X, context.Px(UiTheme::ButtonHeight + 2.0f));
			}

			void Update(float timeMult) override
			{
				UiElement::Update(timeMult);
				// Favourites first, then the recent ones that aren't, those the tile set as the level has it doesn't have left out
				_tiles.clear();
				_favoriteCount = 0;
				if (_grid == nullptr) {
					return;
				}
				std::uint8_t source = _grid->GetSource();
				StringView tileSet;
				std::int32_t offset;
				if (!GetSourceTileSet(_session.GetDocument(), source, tileSet, offset)) {
					return;
				}
				std::int32_t count = _grid->GetTileCount();
				for (const EditorSettings::TileSetFavorites& favorites : _session.GetSettings().TileFavorites) {
					if (favorites.TileSet != tileSet) {
						continue;
					}
					for (std::uint16_t stored : favorites.Favorites) {
						std::int32_t index = std::int32_t(stored) - offset;
						if (index >= 0 && index < count) {
							_tiles.push_back(TileRef{ std::uint16_t(index), source, 0 });
						}
					}
					_favoriteCount = std::int32_t(_tiles.size());
					for (std::uint16_t stored : favorites.Recent) {
						std::int32_t index = std::int32_t(stored) - offset;
						if (index >= 0 && index < count && std::find(favorites.Favorites.begin(), favorites.Favorites.end(), stored) == favorites.Favorites.end()) {
							_tiles.push_back(TileRef{ std::uint16_t(index), source, 0 });
						}
					}
					break;
				}
			}

			void Draw(UiPainter& painter) override
			{
				Tiles::TileMap* tileMap = _session.GetTileMap();
				if (tileMap == nullptr) {
					return;
				}
				if (_tiles.empty()) {
					painter.TextInRect(_x("editor", "Right-click a tile to keep it here"), Bounds, UiTheme::TextDisabled);
					return;
				}
				std::int32_t hovered = (_hovered && GetSurface() != nullptr ? GetIndexAt(GetSurface()->GetPointerPos()) : -1);
				float size = Bounds.H;
				for (std::int32_t i = 0; i < std::int32_t(_tiles.size()); i++) {
					Rectf rect(Bounds.X + i * (size + 2.0f), Bounds.Y, size, size);
					if (rect.X + rect.W > Bounds.X + Bounds.W) {
						break;
					}
					DrawTile(painter, *tileMap, _session.GetDocument(), _tiles[i], 0, rect, 1.0f, UiLayer::Content);
					if (i < _favoriteCount) {
						float radius = std::max(1.5f, painter.Px(1.5f));
						painter.Circle(Vector2f(rect.X + rect.W - radius * 1.5f, rect.Y + radius * 1.5f), radius, Colorf(1.0f, 0.85f, 0.3f, 1.0f), UiLayer::Overlay);
					}
					if (i == hovered) {
						painter.Frame(rect, UiTheme::Accent, std::max(1.0f, std::round(painter.GetScale())), UiLayer::Overlay);
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
				if (event.Button == MouseButton::Right) {
					ToggleFavoriteTile(_session, _tiles[index].Source, _tiles[index].Index);
				} else if (event.Button == MouseButton::Left) {
					_session.SetStamp(TileStamp::FromTile(_tiles[index]));
					EditorToolType tool = _session.GetTools().GetChosenType();
					if (tool != EditorToolType::Brush && tool != EditorToolType::Rectangle && tool != EditorToolType::Fill) {
						_session.GetTools().SetActive(EditorToolType::Brush);
					}
				}
				return false;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				std::int32_t index = GetIndexAt(pos);
				if (index < 0) {
					return _x("editor", "Favourite tiles of the tile set and those picked recently - right-click a tile to make it a favourite");
				}
				_tooltip = _fx("editor", "Tile {}", _tiles[index].Index) + "\n"_s + (index < _favoriteCount
					? StringView(_x("editor", "Favourite - right-click to take it away")) : StringView(_x("editor", "Picked recently - right-click to make it a favourite")));
				return _tooltip;
			}

		private:
			IEditorContext& _session;
			TileGrid*& _grid;
			SmallVector<TileRef, 0> _tiles;
			std::int32_t _favoriteCount;
			mutable String _tooltip;

			std::int32_t GetIndexAt(Vector2f pos) const
			{
				float size = Bounds.H;
				if (!Bounds.Contains(pos) || size <= 0.0f) {
					return -1;
				}
				std::int32_t index = std::int32_t((pos.X - Bounds.X) / (size + 2.0f));
				return (index >= 0 && index < std::int32_t(_tiles.size()) && pos.X < Bounds.X + index * (size + 2.0f) + size ? index : -1);
			}
		};
	}

	TilePickerPanel::TilePickerPanel(IEditorContext& session)
		: UiPanel(UiPanelStyle::None, UiOrientation::Vertical, UiTheme::Spacing, UiThickness()), _session(session), _grid(nullptr)
	{
		auto* header = Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		auto* previous = header->Add<UiButton>(StringView(), EditorIcon::ChevronLeft, [this]() { NextSource(-1); });
		previous->Style = UiButtonStyle::Flat;
		previous->Tooltip = _x("editor", "Previous tile set");
		auto* name = header->Add<UiLabel>(StringView(), UiTheme::Text, Alignment::Center);
		name->Flex = 1.0f;
		name->TextFunc = [this]() { return GetSourceName(); };
		auto* next = header->Add<UiButton>(StringView(), EditorIcon::ChevronRight, [this]() { NextSource(1); });
		next->Style = UiButtonStyle::Flat;
		next->Tooltip = _x("editor", "Next tile set");
		header->Add<UiSeparator>(UiOrientation::Vertical);
		auto* zoomOut = header->Add<UiButton>(StringView(), EditorIcon::ZoomOut, [this]() { _grid->SetColumns(_grid->GetColumns() + 1); });
		zoomOut->Style = UiButtonStyle::Flat;
		zoomOut->Tooltip = _x("editor", "More tiles in a row, smaller");
		auto* zoomIn = header->Add<UiButton>(StringView(), EditorIcon::ZoomIn, [this]() { _grid->SetColumns(_grid->GetColumns() - 1); });
		zoomIn->Style = UiButtonStyle::Flat;
		zoomIn->Tooltip = _x("editor", "Fewer tiles in a row, larger");
		auto* usage = header->Add<UiButton>(StringView(), EditorIcon::Usage, [this]() { _grid->ShowUsage = !_grid->ShowUsage; });
		usage->Style = UiButtonStyle::Flat;
		usage->IsCheckedFunc = [this]() { return _grid->ShowUsage; };
		usage->Tooltip = _x("editor", "Show how often the level uses each tile, unused ones are dark");

		Add<TileShortcutStrip>(session, _grid);
		Add<SuggestionStrip>(session);

		auto* scroll = Add<UiScrollView>();
		scroll->Flex = 1.0f;
		_grid = scroll->SetContent<TileGrid>(session);
	}

	void TilePickerPanel::Update(float timeMult)
	{
		// An extra tile set that was removed, or animated tiles that are all gone, can't be shown anymore
		LevelDocument& document = _session.GetDocument();
		std::uint8_t source = _grid->GetSource();
		if ((source == TileRef::Animated && document.Animations.empty()) ||
			(source != TileRef::Animated && source != TileRef::DefaultTileSet && source > document.ExtraTileSets.size())) {
			_grid->SetSource(TileRef::DefaultTileSet);
		}
		UiPanel::Update(timeMult);
	}

	String TilePickerPanel::GetSourceName() const
	{
		LevelDocument& document = _session.GetDocument();
		std::uint8_t source = _grid->GetSource();
		if (source == TileRef::Animated) {
			// TRANSLATORS: Shown in the header of the tile picker in place of the name of a tile set, while the animated tiles of the level are picked from
			return _x("editor", "Animated tiles");
		}
		if (source == TileRef::DefaultTileSet) {
			return document.Properties.DefaultTileSet;
		}
		return (source - 1 < std::int32_t(document.ExtraTileSets.size()) ? GetTileSetLabel(document.ExtraTileSets[source - 1].Name) : String());
	}

	void TilePickerPanel::NextSource(std::int32_t direction)
	{
		LevelDocument& document = _session.GetDocument();
		SmallVector<std::uint8_t, 8> sources;
		sources.push_back(TileRef::DefaultTileSet);
		for (std::size_t i = 0; i < document.ExtraTileSets.size() && i < TileRef::LastExtraTileSet; i++) {
			sources.push_back(std::uint8_t(i + 1));
		}
		if (!document.Animations.empty()) {
			sources.push_back(TileRef::Animated);
		}

		std::int32_t current = 0;
		for (std::size_t i = 0; i < sources.size(); i++) {
			if (sources[i] == _grid->GetSource()) {
				current = std::int32_t(i);
			}
		}
		std::int32_t count = std::int32_t(sources.size());
		_grid->SetSource(sources[ToolMath::PositiveModulo(current + direction, count)]);
	}

	StampPreview::StampPreview(IEditorContext& session)
		: _session(session)
	{
	}

	Vector2f StampPreview::Measure(const UiContext& context, Vector2f available)
	{
		return Vector2f(available.X, available.X);
	}

	void StampPreview::Draw(UiPainter& painter)
	{
		painter.RoundRect(Bounds, UiTheme::InputFill);
		const TileStamp& stamp = _session.GetStamp();
		Tiles::TileMap* tileMap = _session.GetTileMap();
		if (stamp.IsEmpty() || tileMap == nullptr) {
			return;
		}

		float inset = painter.Px(2.0f);
		float available = Bounds.W - inset * 2.0f;
		float tileSize = std::floor(std::min(available / stamp.Width, available / stamp.Height));
		if (tileSize < 1.0f) {
			return;
		}
		float x0 = std::round(Bounds.X + (Bounds.W - tileSize * stamp.Width) * 0.5f);
		float y0 = std::round(Bounds.Y + (Bounds.H - tileSize * stamp.Height) * 0.5f);
		LevelDocument& document = _session.GetDocument();
		for (std::int32_t y = 0; y < stamp.Height; y++) {
			for (std::int32_t x = 0; x < stamp.Width; x++) {
				if (stamp.IsPresent(x, y) && !stamp.GetTile(x, y).IsEmpty()) {
					const TileRef& tile = stamp.GetTile(x, y);
					DrawTile(painter, *tileMap, document, tile, tile.Flags, Rectf(x0 + x * tileSize, y0 + y * tileSize, tileSize, tileSize));
				}
			}
		}
	}

	bool StampPreview::AcceptsPointer() const
	{
		return true;
	}

	StringView StampPreview::GetTooltip(Vector2f pos) const
	{
		const TileStamp& stamp = _session.GetStamp();
		// TRANSLATORS: Tooltip of the preview of the brush, the tiles being painted with, {} x {} is its width and height
		_tooltip = _fx("editor", "Brush: {} x {} tiles", stamp.Width, stamp.Height);
		return _tooltip;
	}

	StatusBar::StatusBar(IEditorContext& session)
		: _session(session)
	{
	}

	Vector2f StatusBar::Measure(const UiContext& context, Vector2f available)
	{
		return Vector2f(available.X, context.Px(UiTheme::StatusBarHeight));
	}

	void StatusBar::Draw(UiPainter& painter)
	{
		painter.FillRect(Bounds, UiTheme::PanelHeader);
		float thickness = std::max(1.0f, std::round(painter.GetScale() * 0.5f));
		painter.FillRect(Rectf(Bounds.X, Bounds.Y, Bounds.W, thickness), UiTheme::PanelBorderDim);

		float padding = painter.Px(UiTheme::Padding * 1.5f);
		Rectf inner(Bounds.X + padding, Bounds.Y, Bounds.W - padding * 2.0f, Bounds.H);

		String info;
		LevelDocument& document = _session.GetDocument();
		std::int32_t layerIndex = _session.GetCurrentLayer();
		if (layerIndex >= 0 && layerIndex < std::int32_t(document.Layers.size())) {
			info = GetLayerTitle(document, layerIndex);
		}

		ToolPointer pointer;
		if (_session.GetHoverPointer(pointer)) {
			LayerView view = _session.GetLayerView(layerIndex);
			Vector2i tile;
			if (view.Wrap(pointer.Cell, tile)) {
				const TileRef& ref = document.Layers[layerIndex].GetTile(tile.X, tile.Y);
				info += "   "_s + FormatInteger(tile.X) + ", "_s + FormatInteger(tile.Y) + "   "_s;
				// TRANSLATORS: In the status bar, the tile under the pointer, {} is the number of the animated tile or of the tile
				info += (ref.IsAnimated() ? _fx("editor", "Animation {}", ref.Index) : _fx("editor", "Tile {}", document.ToTileId(ref)));
			}

			// The event under the pointer, whichever layer is edited
			std::int32_t spriteLayer = document.GetSpriteLayerIndex();
			if (spriteLayer >= 0 && (_session.AreEventsShown() || _session.IsEditingEvents())) {
				LayerView spriteView = _session.GetLayerView(spriteLayer);
				Vector2i cell = spriteView.WorldToCell(spriteView.ScreenToWorld(pointer.Screen));
				Vector2i gridSize = document.GetEventGridSize();
				if (cell.X >= 0 && cell.Y >= 0 && cell.X < gridSize.X && cell.Y < gridSize.Y) {
					const Formats::EventRecord& event = document.GetEvent(cell.X, cell.Y);
					if (!LevelDocument::IsEmptyEvent(event)) {
						info += "   "_s + EventPreviews::GetEventName(event.Type);
					}
				}
			}
		}
		if (EditorCamera* camera = _session.GetCamera()) {
			info += "   "_s + FormatInteger(std::int32_t(std::round(camera->GetScale() / camera->GetGameplayScale() * 100.0f))) + "%"_s;
		}
		painter.TextInRect(info, inner, UiTheme::Text, Alignment::Right);

		// The hint gets what the information leaves
		float hintWidth = inner.W - painter.MeasureText(info) - padding * 2.0f;
		if (hintWidth > 0.0f) {
			Rectf hint(inner.X, inner.Y, hintWidth, inner.H);
			painter.PushClip(hint);
			// What the buttons of the gamepad do while it's used, how the tool is used by a mouse otherwise
			// What the buttons of the gamepad or the fingers do while they're used, how the tool is used by a mouse otherwise
			StringView text;
			switch (_session.GetInputModality()) {
				case InputModality::Gamepad:
					// TRANSLATORS: Hint in the status bar while a gamepad is used, keep the names of the buttons as they are
					text = _x("editor", "A: use - B: erase - X: pick - Y: next tool, hold for all - LB/RB: layer - LT/RT: zoom - Right stick: move view - Back: undo - Start: play");
					break;
				case InputModality::Touch:
					text = _x("editor", "One finger: use the tool - Two: move and zoom - Tap with two: undo, three: redo - Hold: tools");
					break;
				default:
					text = _session.GetTools().GetActive()->GetHint();
					break;
			}
			painter.TextInRect(text, hint, UiTheme::TextDim);
			painter.PopClip();
		}
	}

	bool StatusBar::AcceptsPointer() const
	{
		return true;
	}
}

#endif
