#include "Navigator.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Document/Annotations.h"
#include "../Imaging/LevelThumbnail.h"
#include "../Runtime/EditorCamera.h"
#include "../Runtime/EditorLevelHandler.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"
#include "../../../nCine/Graphics/Texture.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>

using namespace Death::Containers::Literals;
using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		void ChangeAnnotations(EditorSession& session, Function<void(SmallVectorImpl<Annotation>&)>&& change)
		{
			SmallVector<Annotation, 0> annotations;
			Annotation::Read(session.GetDocument(), annotations);
			change(annotations);
			Annotation::Write(session.GetDocument(), annotations);
			// Not a change of the level that's undone, but it's saved with it
			session.GetHistory().MarkDirty();
		}

		/** The sprite layer small, a pixel for a tile, and the part of it the view shows */
		class MinimapView : public UiElement
		{
		public:
			explicit MinimapView(EditorSession& session)
				: _session(session), _thumbnail(session), _revision(UINT32_MAX), _paletteHash(0), _dragging(false)
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				Vector2f fixedSize = GetFixedSize(context);
				return Vector2f(fixedSize.X >= 0.0f ? fixedSize.X : context.Px(220.0f), fixedSize.Y >= 0.0f ? fixedSize.Y : context.Px(110.0f));
			}

			void Update(float timeMult) override
			{
				// Built again when the level or its colors change
				std::uint32_t revision = _session.GetHistory().GetRevision();
				std::uint32_t paletteHash = HashPalette();
				if (revision != _revision || paletteHash != _paletteHash || _texture == nullptr) {
					// The colors of the tiles are kept while the palette and the tile sets stay
					if (paletteHash != _paletteHash || HashTileSets() != _tileSetsHash) {
						_thumbnail.Clear();
						_tileSetsHash = HashTileSets();
					}
					_revision = revision;
					_paletteHash = paletteHash;
					Build();
				}
				UiElement::Update(timeMult);
			}

			void Draw(UiPainter& painter) override
			{
				painter.FillRect(Bounds, Colorf(0.0f, 0.0f, 0.0f, 0.5f), UiLayer::Fill);
				Rectf rect = GetMapRect();
				if (_texture != nullptr) {
					painter.Image(*_texture, rect, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf::White, -1, UiLayer::Content);
				}
				painter.Frame(rect, UiTheme::PanelBorderDim, 1.0f, UiLayer::Content);

				// What the view shows
				EditorLevelHandler* editHandler = _session.GetEditHandler();
				if (editHandler == nullptr || _size.X <= 0) {
					return;
				}
				EditorCamera& camera = editHandler->GetCamera();
				Vector2f half = Vector2f(float(camera.GetViewSize().X), float(camera.GetViewSize().Y)) * (0.5f / camera.GetScale());
				Vector2f topLeft = WorldToMap(rect, camera.GetCenter() - half);
				Vector2f bottomRight = WorldToMap(rect, camera.GetCenter() + half);
				Rectf view(topLeft.X, topLeft.Y, bottomRight.X - topLeft.X, bottomRight.Y - topLeft.Y);
				painter.PushClip(rect);
				painter.Frame(view, UiTheme::Accent, std::max(1.0f, std::round(painter.GetScale())), UiLayer::Overlay);
				painter.PopClip();

				// Bookmarks, as their numbers would be too small
				SmallVector<Annotation, 0> annotations;
				Annotation::Read(_session.GetDocument(), annotations);
				for (const Annotation& annotation : annotations) {
					Vector2f pos = WorldToMap(rect, Vector2f(float(annotation.X), float(annotation.Y)));
					float size = std::max(2.0f, std::round(painter.Px(2.0f)));
					painter.FillRect(Rectf(pos.X - size, pos.Y - size, size * 2.0f, size * 2.0f),
						annotation.IsBookmark() ? Colorf(0.4f, 0.8f, 1.0f, 1.0f) : Colorf(1.0f, 0.9f, 0.4f, 1.0f), UiLayer::Overlay);
				}
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
				_dragging = true;
				MoveViewTo(event.Pos);
				return true;
			}

			void OnPointerMove(const UiPointerEvent& event) override
			{
				if (_dragging) {
					MoveViewTo(event.Pos);
				}
			}

			void OnPointerUp(const UiPointerEvent& event, bool inside) override
			{
				_dragging = false;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				return _("Click or drag to move the view there");
			}

		private:
			EditorSession& _session;
			LevelThumbnail _thumbnail;
			std::unique_ptr<Texture> _texture;
			Vector2i _size;
			std::uint32_t _revision;
			std::uint32_t _paletteHash;
			std::uint32_t _tileSetsHash = 0;
			bool _dragging;

			std::uint32_t HashTileSets() const
			{
				const LevelDocument& document = _session.GetDocument();
				std::uint32_t hash = 2166136261u;
				auto add = [&hash](StringView text) {
					for (char c : text) {
						hash = (hash ^ std::uint8_t(c)) * 16777619u;
					}
					hash = (hash ^ 0xFFu) * 16777619u;
				};
				add(document.Properties.DefaultTileSet);
				for (const Formats::ExtraTileSetRecord& extra : document.ExtraTileSets) {
					add(extra.Name);
					hash = (hash ^ (std::uint32_t(extra.Offset) | (std::uint32_t(extra.Flags) << 16))) * 16777619u;
					hash = (hash ^ std::uint32_t(extra.Data.size())) * 16777619u;
				}
				return hash;
			}

			static std::uint32_t HashPalette()
			{
				const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
				std::uint32_t hash = 2166136261u;
				for (std::int32_t i = 0; i < 256; i++) {
					hash = (hash ^ palette[i]) * 16777619u;
				}
				return hash;
			}

			// The map keeps the shape of the level, centered
			Rectf GetMapRect() const
			{
				if (_size.X <= 0 || _size.Y <= 0) {
					return Bounds;
				}
				float scale = std::min(Bounds.W / float(_size.X), Bounds.H / float(_size.Y));
				float w = std::round(_size.X * scale), h = std::round(_size.Y * scale);
				return Rectf(std::round(Bounds.X + (Bounds.W - w) * 0.5f), std::round(Bounds.Y + (Bounds.H - h) * 0.5f), w, h);
			}

			Vector2f WorldToMap(const Rectf& rect, Vector2f world) const
			{
				return Vector2f(rect.X + world.X / (_size.X * 32.0f) * rect.W, rect.Y + world.Y / (_size.Y * 32.0f) * rect.H);
			}

			void MoveViewTo(Vector2f screen)
			{
				EditorLevelHandler* editHandler = _session.GetEditHandler();
				if (editHandler == nullptr || _size.X <= 0) {
					return;
				}
				Rectf rect = GetMapRect();
				Vector2f world((screen.X - rect.X) / rect.W * _size.X * 32.0f, (screen.Y - rect.Y) / rect.H * _size.Y * 32.0f);
				EditorCamera& camera = editHandler->GetCamera();
				camera.JumpTo(world, camera.GetScale());
			}

			void Build()
			{
				const LevelDocument& document = _session.GetDocument();
				std::int32_t spriteLayer = document.GetSpriteLayerIndex();
				if (spriteLayer < 0) {
					_texture = nullptr;
					_size = Vector2i();
					return;
				}
				const DocumentLayer& layer = document.Layers[spriteLayer];
				std::int32_t width = layer.GetWidth(), height = layer.GetHeight();
				if (width <= 0 || height <= 0) {
					_texture = nullptr;
					_size = Vector2i();
					return;
				}

				SmallVector<std::uint32_t, 0> colors;
				_thumbnail.DrawLayer(spriteLayer, colors);
				SmallVector<std::uint8_t, 0> texels;
				texels.resize_for_overwrite(colors.size() * 4);
				for (std::size_t i = 0; i < colors.size(); i++) {
					texels[i * 4] = std::uint8_t(colors[i]);
					texels[i * 4 + 1] = std::uint8_t(colors[i] >> 8);
					texels[i * 4 + 2] = std::uint8_t(colors[i] >> 16);
					texels[i * 4 + 3] = std::uint8_t(colors[i] >> 24);
				}
				if (_texture == nullptr || _size.X != width || _size.Y != height) {
					_texture = std::make_unique<Texture>("EditorMinimap", Texture::Format::RGBA8, width, height);
					_texture->SetMinFiltering(SamplerFilter::Linear);
					_texture->SetMagFiltering(SamplerFilter::Nearest);
					_texture->SetWrap(SamplerWrapping::ClampToEdge);
				}
				_texture->LoadFromTexels(texels.data(), 0, 0, width, height);
				_size = Vector2i(width, height);
			}
		};
	}

	void SetBookmark(EditorSession& session, std::int32_t slot)
	{
		EditorLevelHandler* editHandler = session.GetEditHandler();
		if (editHandler == nullptr || slot < 1 || slot > 9) {
			return;
		}
		Vector2f center = editHandler->GetCamera().GetCenter();
		ChangeAnnotations(session, [slot, center](SmallVectorImpl<Annotation>& annotations) {
			for (std::size_t i = 0; i < annotations.size(); i++) {
				if (annotations[i].Slot == slot) {
					annotations.erase(annotations.begin() + i);
					break;
				}
			}
			Annotation& bookmark = annotations.emplace_back();
			bookmark.X = std::int32_t(center.X);
			bookmark.Y = std::int32_t(center.Y);
			bookmark.Slot = std::uint8_t(slot);
		});
		session.ShowMessage(_f("Bookmark {} set, Ctrl+{} goes back to it", slot, slot));
	}

	bool GoToBookmark(EditorSession& session, std::int32_t slot)
	{
		SmallVector<Annotation, 0> annotations;
		Annotation::Read(session.GetDocument(), annotations);
		EditorLevelHandler* editHandler = session.GetEditHandler();
		for (const Annotation& annotation : annotations) {
			if (annotation.Slot == slot && editHandler != nullptr) {
				EditorCamera& camera = editHandler->GetCamera();
				camera.FlyTo(Vector2f(float(annotation.X), float(annotation.Y)), camera.GetScale());
				return true;
			}
		}
		session.ShowMessage(_f("Bookmark {} isn't set, Ctrl+Shift+{} sets it", slot, slot));
		return false;
	}

	void AddNote(EditorSession& session, Vector2f worldPos, StringView text)
	{
		ChangeAnnotations(session, [worldPos, text](SmallVectorImpl<Annotation>& annotations) {
			Annotation& note = annotations.emplace_back();
			note.X = std::int32_t(worldPos.X);
			note.Y = std::int32_t(worldPos.Y);
			note.Text = (text.empty() ? String(_("Note")) : String(text));
		});
	}

	void ShowNavigator(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("Navigator"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();
		content->Add<MinimapView>(session);

		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 220.0f;
		scroll->Height = 120.0f;

		// The list follows the annotations, rebuilt when their chunk changes
		struct ListState { SmallVector<std::uint8_t, 0> Data; bool Built = false; };
		auto state = std::make_shared<ListState>();
		auto rebuild = [s, scroll]() {
			SmallVector<Annotation, 0> annotations;
			Annotation::Read(s->GetDocument(), annotations);
			auto* rows = scroll->SetContent<UiStack>(UiOrientation::Vertical, 2.0f);
			for (std::int32_t slot = 1; slot <= 9; slot++) {
				auto it = std::find_if(annotations.begin(), annotations.end(), [slot](const Annotation& a) { return a.Slot == slot; });
				auto* row = rows->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				auto* label = row->Add<UiLabel>(_f("Bookmark {}", slot), it != annotations.end() ? UiTheme::Text : UiTheme::TextDisabled);
				label->Flex = 1.0f;
				if (it != annotations.end()) {
					row->Add<UiButton>(_("Go"), EditorIcon::None, [s, slot]() { GoToBookmark(*s, slot); })->Style = UiButtonStyle::Flat;
				}
				auto* set = row->Add<UiButton>(_("Set"), EditorIcon::None, [s, slot]() { SetBookmark(*s, slot); });
				set->Style = UiButtonStyle::Flat;
				set->Tooltip = _f("The middle of the view, Ctrl+Shift+{}", slot);
				if (it != annotations.end()) {
					row->Add<UiButton>(StringView(), EditorIcon::Delete, [s, slot]() {
						ChangeAnnotations(*s, [slot](SmallVectorImpl<Annotation>& list) {
							list.erase(std::remove_if(list.begin(), list.end(), [slot](const Annotation& a) { return a.Slot == slot; }), list.end());
						});
					})->Style = UiButtonStyle::Flat;
				}
			}

			for (std::size_t i = 0; i < annotations.size(); i++) {
				if (annotations[i].IsBookmark()) {
					continue;
				}
				std::int32_t index = std::int32_t(i);
				Vector2f pos(float(annotations[i].X), float(annotations[i].Y));
				auto* row = rows->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				auto* text = row->Add<UiTextBox>(annotations[i].Text);
				text->Flex = 1.0f;
				text->MaxLength = 200;
				text->OnCommit = [s, index](StringView value) {
					ChangeAnnotations(*s, [index, value](SmallVectorImpl<Annotation>& list) {
						if (index < std::int32_t(list.size())) {
							list[index].Text = value;
						}
					});
				};
				row->Add<UiButton>(_("Go"), EditorIcon::None, [s, pos]() {
					if (EditorLevelHandler* editHandler = s->GetEditHandler()) {
						EditorCamera& camera = editHandler->GetCamera();
						camera.FlyTo(pos, camera.GetScale());
					}
				})->Style = UiButtonStyle::Flat;
				row->Add<UiButton>(StringView(), EditorIcon::Delete, [s, index]() {
					ChangeAnnotations(*s, [index](SmallVectorImpl<Annotation>& list) {
						if (index < std::int32_t(list.size())) {
							list.erase(list.begin() + index);
						}
					});
				})->Style = UiButtonStyle::Flat;
			}
		};
		// Checked every frame, a note added in the view or a bookmark set by its keys shows at once
		class Watcher : public UiElement
		{
		public:
			Watcher(Function<void()>&& onUpdate) : _onUpdate(std::move(onUpdate)) {}
			Vector2f Measure(const UiContext& context, Vector2f available) override { return Vector2f(0.0f, 0.0f); }
			void Update(float timeMult) override { _onUpdate(); UiElement::Update(timeMult); }
		private:
			Function<void()> _onUpdate;
		};
		content->Add<Watcher>([s, state, rebuild]() {
			const Formats::ChunkRecord* chunk = s->GetDocument().FindChunk(Annotation::Chunk);
			std::size_t size = (chunk != nullptr ? chunk->Data.size() : 0);
			bool same = (state->Built && size == state->Data.size() && (size == 0 || std::memcmp(chunk->Data.data(), state->Data.data(), size) == 0));
			if (!same) {
				state->Data.clear();
				if (chunk != nullptr) {
					for (std::uint8_t byte : chunk->Data) {
						state->Data.push_back(byte);
					}
				}
				state->Built = true;
				rebuild();
			}
		});

		dialog->AddButton(_("Add Note Here"), [s]() {
			if (EditorLevelHandler* editHandler = s->GetEditHandler()) {
				AddNote(*s, editHandler->GetCamera().GetCenter());
			}
		});
		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		// In the bottom right corner, out of the way of the view, the position is moved to fit the screen
		UiOverlayOptions options;
		options.Position = Vector2f(1.0e6f, 1.0e6f);
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
