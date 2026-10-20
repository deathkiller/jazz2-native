#include "Navigator.h"

#if defined(WITH_EDITOR)

#include "ShellCommon.h"
#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../Document/DocumentChanges.h"
#include "../Events/EventPreviews.h"
#include "../UI/UiColors.h"
#include "../../PreferencesCache.h"
#include "../../../nCine/Primitives/Vector3.h"
#include "../Document/Annotations.h"
#include "../Document/Commands.h"
#include "../Imaging/LevelThumbnail.h"
#include "../Runtime/EditorCamera.h"
#include "../Runtime/EditorLevelHandler.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"
#include "../../../nCine/Graphics/Texture.h"

#include "../../../nCine/I18n.h"

#include <Containers/DateTime.h>
#include <Containers/StringConcatenable.h>
#include <Cryptography/xxHash.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

using namespace Death::Containers::Literals;
using namespace nCine;

namespace Jazz2::Editor
{
	namespace
	{
		void ChangeAnnotations(IEditorContext& session, StringView name, Function<void(SmallVectorImpl<Annotation>&)>&& change)
		{
			SmallVector<Annotation, 0> annotations;
			Annotation::Read(session.GetDocument(), annotations);
			change(annotations);
			SmallVector<std::uint8_t, 0> data;
			Annotation::Serialize(annotations, data);
			// An undoable step as any other, so undoing a change of layers can't take them back to how they were then
			const Formats::ChunkRecord* chunk = session.GetDocument().FindChunk(Annotation::Chunk);
			bool same = (chunk != nullptr ? (chunk->Data.size() == data.size() && std::memcmp(chunk->Data.data(), data.data(), data.size()) == 0) : data.empty());
			if (!same) {
				session.Execute(std::make_unique<SetChunkCommand>(session.GetDocument(), Annotation::Chunk, data, String(name)));
			}
		}

		// Grows a rectangle to hold another one too, an empty one is left out
		void UnionArea(Recti& target, const Recti& area)
		{
			if (area.W <= 0 || area.H <= 0) {
				return;
			}
			if (target.W <= 0 || target.H <= 0) {
				target = area;
				return;
			}
			std::int32_t right = std::max(target.X + target.W, area.X + area.W);
			std::int32_t bottom = std::max(target.Y + target.H, area.Y + area.H);
			target.X = std::min(target.X, area.X);
			target.Y = std::min(target.Y, area.Y);
			target.W = right - target.X;
			target.H = bottom - target.Y;
		}

		// Seconds since 1970, as notes and replies keep when they were written
		std::int64_t GetCurrentTime()
		{
			return DateTime::UtcNow().ToUnixMilliseconds() / 1000;
		}

		String FormatTime(std::int64_t seconds)
		{
			if (seconds <= 0) {
				return {};
			}
			DateTime::Tm tm = DateTime::FromUnixMilliseconds(seconds * 1000).Partitioned();
			char buffer[32];
			std::int32_t length = std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d", tm.Year, tm.Month + 1, tm.Day, tm.Hour, tm.Minute);
			return String(buffer, std::size_t(std::max(length, 0)));
		}

		// The author and when, as a note or a reply shows them
		String DescribeAuthor(StringView author, std::int64_t time)
		{
			String when = FormatTime(time);
			if (author.empty()) {
				return when;
			}
			return (when.empty() ? String(author) : String(author + ", "_s + when));
		}

		// Asks for the text of a reply to a note, by its index among the annotations
		void ShowReplyDialog(IEditorContext& session, std::int32_t index)
		{
			UiSurface* surface = session.GetSurface();
			if (surface == nullptr) {
				return;
			}
			IEditorContext* s = &session;
			auto dialog = std::make_unique<UiDialog>(_("Reply to the Note"));
			UiDialog* dialogPtr = dialog.get();
			auto* text = dialog->GetContent()->Add<UiTextBox>();
			text->Width = 240.0f;
			text->MaxLength = 200;
			text->Placeholder = _("Reply");
			dialog->AddButton(_("Reply"), [s, index, text, dialogPtr]() {
				String reply = text->Text.trimmed();
				if (!reply.empty()) {
					ChangeAnnotations(*s, _("Reply to note"), [index, &reply](SmallVectorImpl<Annotation>& list) {
						if (index < std::int32_t(list.size())) {
							Annotation::Reply& added = list[index].Replies.emplace_back();
							added.Author = PreferencesCache::GetEffectivePlayerName();
							added.Time = GetCurrentTime();
							added.Text = reply;
						}
					});
				}
				dialogPtr->Close();
			}, true);
			dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });
			ShowModal(*surface, Death::move(dialog));
			text->BeginEdit();
		}

		/** The sprite layer small, a pixel for a tile, and the part of it the view shows */
		class MinimapView : public UiElement
		{
		public:
			explicit MinimapView(IEditorContext& session)
				: _session(session), _thumbnail(session.GetDocument(), session.GetTileSetDataCache()), _step(1), _rebuild(true), _clearColors(true), _paletteHash(0), _cooldown(0.0f), _dragging(false)
			{
				// Only changes of the sprite layer and of the colors of tiles are drawn again, as far as they reach
				_changeObserver = std::make_shared<Function<void(const DocumentChanges&)>>([this](const DocumentChanges& changes) {
					// Events of a known area are drawn again only there, others all
					if (changes.HasAny(DocumentChangeKind::LayerStructure) || (changes.HasAny(DocumentChangeKind::Events) && !changes.IsEventAreaComplete())) {
						_eventsDirty = true;
					} else if (changes.HasAny(DocumentChangeKind::Events)) {
						UnionArea(_eventsDirtyArea, changes.GetEventArea());
					}
					if (changes.HasAny(DocumentChangeKind::TileOverrides)) {
						_rebuild = true;
						_clearColors = true;
					} else if (changes.HasAny(DocumentChangeKind::TileSets | DocumentChangeKind::Palette | DocumentChangeKind::AnimationStructure |
						DocumentChangeKind::LayerStructure)) {
						// The colors of the tiles are kept if they're still the same, see LevelThumbnail::Revalidate()
						_rebuild = true;
					} else if (changes.HasAny(DocumentChangeKind::Tiles)) {
						std::int32_t spriteLayer = _session.GetDocument().GetSpriteLayerIndex();
						for (const DocumentTileArea& area : changes.GetTileAreas()) {
							if (area.Layer == spriteLayer) {
								UnionArea(_dirty, area.Area);
							}
						}
					}
				});
				session.AddChangeObserver(_changeObserver);
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				Vector2f fixedSize = GetFixedSize(context);
				return Vector2f(fixedSize.X >= 0.0f ? fixedSize.X : context.Px(220.0f), fixedSize.Y >= 0.0f ? fixedSize.Y : context.Px(110.0f));
			}

			void Update(float timeMult) override
			{
				// Painting changes the tiles every frame, they're drawn again a few times a second at most
				constexpr float CooldownFrames = 8.0f;
				// An animated palette changes in every frame, the whole map is made again about once a second then
				constexpr float PaletteCooldownFrames = 60.0f;
				_cooldown = std::max(_cooldown - timeMult, 0.0f);
				_paletteCooldown = std::max(_paletteCooldown - timeMult, 0.0f);
				_eventsCooldown = std::max(_eventsCooldown - timeMult, 0.0f);
				// The palette can change in the game without a change of the level
				std::uint32_t paletteHash = HashPalette();
				if (paletteHash != _paletteHash) {
					_paletteHash = paletteHash;
					_paletteDirty = true;
				}
				if (_paletteDirty && _paletteCooldown <= 0.0f) {
					_paletteDirty = false;
					_paletteCooldown = PaletteCooldownFrames;
					_rebuild = true;
				}
				bool dirty = (_dirty.W > 0 && _dirty.H > 0);
				if ((_rebuild || dirty) && _cooldown <= 0.0f) {
					if (_clearColors) {
						_thumbnail.Clear();
						_clearColors = false;
					}
					// Painting only places tiles, otherwise the colors of the tiles are forgotten if what they're made of changed
					if (_rebuild) {
						_thumbnail.Revalidate();
					}
					if (_rebuild || _texture == nullptr) {
						Build();
					} else {
						UpdateTiles(_dirty);
					}
					_rebuild = false;
					_dirty = Recti();
					_cooldown = CooldownFrames;
				}
				// Dragging a value of events changes them in every frame, as painting does the tiles, but only where they
				// are, so only all of them wait for the cooldown
				if (ShowEvents && _texture != nullptr) {
					if (_eventsDirty || _eventsTexture == nullptr) {
						if (_eventsCooldown <= 0.0f) {
							BuildEvents();
							_eventsCooldown = CooldownFrames;
						}
					} else if (_eventsDirtyArea.W > 0 && _eventsDirtyArea.H > 0) {
						UpdateEvents(_eventsDirtyArea);
						_eventsDirtyArea = Recti();
					}
				}
				UiElement::Update(timeMult);
			}

			/** @brief Whether the events and where the camera is limited are shown over the map */
			bool ShowEvents = true;

			void Draw(UiPainter& painter) override
			{
				painter.FillRect(Bounds, Colorf(0.0f, 0.0f, 0.0f, 0.5f), UiLayer::Fill);
				Rectf rect = GetMapRect();
				if (_texture != nullptr) {
					painter.Image(*_texture, rect, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf::White, -1, UiLayer::Content);
				}
				painter.Frame(rect, UiTheme::PanelBorderDim, 1.0f, UiLayer::Content);
				if (ShowEvents && _eventsTexture != nullptr && _size.X > 0) {
					// The events in the colors of their categories, the spans the camera is kept in as bars at their rows
					painter.Image(*_eventsTexture, rect, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf::White, -1, UiLayer::Overlay);
					float thickness = std::max(1.0f, std::round(painter.GetScale()));
					for (const Vector3i& span : _cameraSpans) {
						Vector2f from = WorldToMap(rect, Vector2f(span.X * 32.0f, span.Z * 32.0f + 16.0f));
						Vector2f to = WorldToMap(rect, Vector2f(span.Y * 32.0f, span.Z * 32.0f + 16.0f));
						Colorf color(1.0f, 0.55f, 0.85f, 0.9f);
						painter.FillRect(Rectf(from.X, from.Y - thickness * 0.5f, std::max(to.X - from.X, thickness), thickness), color, UiLayer::Overlay);
						painter.FillRect(Rectf(from.X, from.Y - thickness * 2.0f, thickness, thickness * 4.0f), color, UiLayer::Overlay);
						painter.FillRect(Rectf(to.X - thickness, to.Y - thickness * 2.0f, thickness, thickness * 4.0f), color, UiLayer::Overlay);
					}
				}

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

				// Bookmarks, as their numbers would be too small, in their own colors
				for (const Annotation& annotation : _annotations.Get(_session.GetDocument())) {
					Vector2f pos = WorldToMap(rect, Vector2f(float(annotation.X), float(annotation.Y)));
					float size = std::max(2.0f, std::round(painter.Px(2.0f)));
					Colorf color = (annotation.Color != 0 ? AnnotationColor(annotation.Color)
						: (annotation.IsBookmark() ? Colorf(0.4f, 0.8f, 1.0f, 1.0f) : Colorf(1.0f, 0.9f, 0.4f, 1.0f)));
					if (annotation.Resolved) {
						color.A *= 0.4f;
					}
					painter.FillRect(Rectf(pos.X - size, pos.Y - size, size * 2.0f, size * 2.0f), color, UiLayer::OverlayText);
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
			// Larger textures aren't available everywhere, a larger level gets a texel for a square of tiles
			static constexpr std::int32_t MaxTextureSize = 2048;

			IEditorContext& _session;
			LevelThumbnail _thumbnail;
			std::unique_ptr<Texture> _texture;
			// Size of the sprite layer in tiles, of the texture in texels, and tiles in a texel each way
			Vector2i _size;
			Vector2i _textureSize;
			std::int32_t _step;
			// The texels as RGBA bytes, so only the changed ones are made again
			SmallVector<std::uint8_t, 0> _texels;
			std::shared_ptr<Function<void(const DocumentChanges&)>> _changeObserver;
			// Tiles of the sprite layer changed since it was drawn, all of it, or also the colors of the tiles
			Recti _dirty;
			bool _rebuild;
			bool _clearColors;
			std::uint32_t _paletteHash;
			float _cooldown;
			bool _dragging;
			AnnotationCache _annotations;
			// The events over the map as large as its texture, and the spans the camera is limited to as left, right and
			// row in tiles, found again when the events change
			std::unique_ptr<Texture> _eventsTexture;
			SmallVector<Vector3i, 0> _cameraSpans;
			bool _eventsDirty = true;
			// Tiles whose events changed since they were drawn, if not all of them did, see UpdateEvents()
			Recti _eventsDirtyArea;
			float _eventsCooldown = 0.0f;
			// The texels of the events, kept so they aren't allocated again each time
			SmallVector<std::uint8_t, 0> _eventTexels;
			// The palette changed since the map was made, it's made again only after a while, see Update()
			bool _paletteDirty = false;
			float _paletteCooldown = 0.0f;

			void BuildEvents()
			{
				_eventsDirty = false;
				_eventsDirtyArea = Recti();
				if (_textureSize.X <= 0 || _textureSize.Y <= 0) {
					_cameraSpans.clear();
					_eventsTexture = nullptr;
					return;
				}
				const LevelDocument& document = _session.GetDocument();
				Vector2i gridSize = document.GetEventGridSize();
				// All texels are made again, into the same memory
				SmallVector<std::uint8_t, 0>& texels = _eventTexels;
				std::size_t texelBytes = std::size_t(_textureSize.X) * std::size_t(_textureSize.Y) * 4;
				if (texels.size() != texelBytes) {
					texels.clear();
					texels.resize(texelBytes, 0);
				} else {
					std::memset(texels.data(), 0, texelBytes);
				}
				if (gridSize.X > 0) {
					for (const DocumentEvent& event : document.Events) {
						PlotEvent(event.Record, std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X)));
					}
				}
				for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
					PlotEvent(event.Event, event.X / 32, event.Y / 32);
				}
				CollectCameraSpans();
				if (_eventsTexture == nullptr || _eventsTexture->GetWidth() != _textureSize.X || _eventsTexture->GetHeight() != _textureSize.Y) {
					_eventsTexture = std::make_unique<Texture>("EditorMinimapEvents", Texture::Format::RGBA8, _textureSize.X, _textureSize.Y);
					_eventsTexture->SetMinFiltering(SamplerFilter::Nearest);
					_eventsTexture->SetMagFiltering(SamplerFilter::Nearest);
					_eventsTexture->SetWrap(SamplerWrapping::ClampToEdge);
				}
				_eventsTexture->LoadFromTexels(texels.data(), 0, 0, _textureSize.X, _textureSize.Y);
			}

			// Draws again the texels of the events of changed tiles, only they are sent to the texture
			void UpdateEvents(const Recti& tiles)
			{
				std::size_t texelBytes = std::size_t(_textureSize.X) * std::size_t(_textureSize.Y) * 4;
				if (_eventsTexture == nullptr || _eventTexels.size() != texelBytes || _eventsTexture->GetWidth() != _textureSize.X ||
					_eventsTexture->GetHeight() != _textureSize.Y) {
					BuildEvents();
					return;
				}
				std::int32_t x0 = std::max<std::int32_t>(tiles.X, 0) / _step, y0 = std::max<std::int32_t>(tiles.Y, 0) / _step;
				std::int32_t x1 = std::min<std::int32_t>((tiles.X + tiles.W - 1) / _step, _textureSize.X - 1);
				std::int32_t y1 = std::min<std::int32_t>((tiles.Y + tiles.H - 1) / _step, _textureSize.Y - 1);
				if (x1 < x0 || y1 < y0) {
					return;
				}
				Recti region(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
				for (std::int32_t y = region.Y; y < region.Y + region.H; y++) {
					std::memset(&_eventTexels[(std::size_t(y) * _textureSize.X + region.X) * 4], 0, std::size_t(region.W) * 4);
				}

				// The events of all tiles of the texels, in the same order as all of them are drawn, so the same one is on top
				// of a texel of more tiles
				const LevelDocument& document = _session.GetDocument();
				Vector2i gridSize = document.GetEventGridSize();
				auto isInRegion = [&](std::int32_t x, std::int32_t y) {
					std::int32_t tx = x / _step, ty = y / _step;
					return (tx >= region.X && ty >= region.Y && tx < region.X + region.W && ty < region.Y + region.H);
				};
				if (gridSize.X > 0) {
					std::int32_t fromX = region.X * _step, toX = std::min<std::int32_t>((region.X + region.W) * _step, gridSize.X);
					std::int32_t toY = std::min<std::int32_t>((region.Y + region.H) * _step, gridSize.Y);
					for (std::int32_t y = region.Y * _step; y < toY && fromX < toX; y++) {
						std::uint32_t first = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(fromX);
						std::uint32_t last = std::uint32_t(y) * std::uint32_t(gridSize.X) + std::uint32_t(toX);
						auto it = std::lower_bound(document.Events.begin(), document.Events.end(), first, [](const DocumentEvent& event, std::uint32_t cell) {
							return event.Cell < cell;
						});
						for (; it != document.Events.end() && it->Cell < last; ++it) {
							PlotEvent(it->Record, std::int32_t(it->Cell % std::uint32_t(gridSize.X)), y);
						}
					}
				}
				for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
					if (isInRegion(std::int32_t(event.X / 32), std::int32_t(event.Y / 32))) {
						PlotEvent(event.Event, event.X / 32, event.Y / 32);
					}
				}
				CollectCameraSpans();

				SmallVector<std::uint8_t, 0> packed;
				packed.resize_for_overwrite(std::size_t(region.W) * std::size_t(region.H) * 4);
				for (std::int32_t y = 0; y < region.H; y++) {
					std::memcpy(&packed[std::size_t(y) * region.W * 4], &_eventTexels[(std::size_t(region.Y + y) * _textureSize.X + region.X) * 4], std::size_t(region.W) * 4);
				}
				_eventsTexture->LoadFromTexels(packed.data(), region.X, region.Y, region.W, region.H);
			}

			// The texel of the tile of an event in the color of its category
			void PlotEvent(const Formats::EventRecord& event, std::int32_t x, std::int32_t y)
			{
				const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
				std::int32_t tx = x / _step, ty = y / _step;
				if (descriptor == nullptr || tx < 0 || ty < 0 || tx >= _textureSize.X || ty >= _textureSize.Y) {
					return;
				}
				Colorf color = EventPreviews::GetCategoryColor(descriptor->Category);
				std::uint8_t* texel = &_eventTexels[(std::size_t(ty) * _textureSize.X + tx) * 4];
				texel[0] = std::uint8_t(std::clamp<float>(color.R, 0.0f, 1.0f) * 255.0f);
				texel[1] = std::uint8_t(std::clamp<float>(color.G, 0.0f, 1.0f) * 255.0f);
				texel[2] = std::uint8_t(std::clamp<float>(color.B, 0.0f, 1.0f) * 255.0f);
				texel[3] = 255;
			}

			// Where the camera is kept from the row of each event that limits it on, see EditorShell::DrawSelectedEventDetails()
			void CollectCameraSpans()
			{
				_cameraSpans.clear();
				const LevelDocument& document = _session.GetDocument();
				Vector2i gridSize = document.GetEventGridSize();
				auto collect = [&](const Formats::EventRecord& event, std::int32_t x, std::int32_t y) {
					if (EventType(event.Type) != EventType::ModifierLimitCameraView) {
						return;
					}
					const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
					std::int32_t tx = x / _step, ty = y / _step;
					if (descriptor == nullptr || descriptor->FieldCount <= 1 || tx < 0 || ty < 0 || tx >= _textureSize.X || ty >= _textureSize.Y) {
						return;
					}
					std::int32_t leftTiles = EventCatalog::ReadField(event, descriptor->Fields[0]);
					std::int32_t widthTiles = EventCatalog::ReadField(event, descriptor->Fields[1]);
					std::int32_t left = (leftTiles == 0 ? x : leftTiles);
					std::int32_t right = (widthTiles == 0 ? gridSize.X : left + widthTiles);
					_cameraSpans.push_back(Vector3i(left, right, y));
				};
				if (gridSize.X > 0) {
					for (const DocumentEvent& event : document.Events) {
						collect(event.Record, std::int32_t(event.Cell % std::uint32_t(gridSize.X)), std::int32_t(event.Cell / std::uint32_t(gridSize.X)));
					}
				}
				for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
					collect(event.Event, event.X / 32, event.Y / 32);
				}
			}

			static Colorf AnnotationColor(std::uint32_t color)
			{
				return Colorf((color & 0xFF) / 255.0f, ((color >> 8) & 0xFF) / 255.0f, ((color >> 16) & 0xFF) / 255.0f, 1.0f);
			}

			static std::uint32_t HashPalette()
			{
				const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
				std::uint64_t hash = Death::Cryptography::xxHash3(palette, 256 * sizeof(std::uint32_t));
				return std::uint32_t(hash ^ (hash >> 32));
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

			const DocumentLayer* GetSpriteLayer() const
			{
				const LevelDocument& document = _session.GetDocument();
				std::int32_t spriteLayer = document.GetSpriteLayerIndex();
				return (spriteLayer >= 0 && spriteLayer < std::int32_t(document.Layers.size()) ? &document.Layers[spriteLayer] : nullptr);
			}

			void Build()
			{
				const DocumentLayer* layer = GetSpriteLayer();
				std::int32_t width = (layer != nullptr ? layer->GetWidth() : 0), height = (layer != nullptr ? layer->GetHeight() : 0);
				if (width <= 0 || height <= 0) {
					_texture = nullptr;
					_size = Vector2i();
					_textureSize = Vector2i();
					return;
				}

				_step = std::max<std::int32_t>({ (width + MaxTextureSize - 1) / MaxTextureSize, (height + MaxTextureSize - 1) / MaxTextureSize, 1 });
				Vector2i textureSize((width + _step - 1) / _step, (height + _step - 1) / _step);
				if (_texture == nullptr || textureSize != _textureSize) {
					_texture = std::make_unique<Texture>("EditorMinimap", Texture::Format::RGBA8, textureSize.X, textureSize.Y);
					_texture->SetMinFiltering(SamplerFilter::Linear);
					_texture->SetMagFiltering(SamplerFilter::Nearest);
					_texture->SetWrap(SamplerWrapping::ClampToEdge);
				}
				_size = Vector2i(width, height);
				_textureSize = textureSize;
				_texels.resize_for_overwrite(std::size_t(textureSize.X) * std::size_t(textureSize.Y) * 4);
				FillTexels(*layer, Recti(0, 0, textureSize.X, textureSize.Y));
				_texture->LoadFromTexels(_texels.data(), 0, 0, textureSize.X, textureSize.Y);
				// The events go over it texel for texel
				_eventsDirty = true;
			}

			// Draws again the texels of changed tiles, only they are sent to the texture
			void UpdateTiles(const Recti& tiles)
			{
				const DocumentLayer* layer = GetSpriteLayer();
				if (layer == nullptr || layer->GetWidth() != _size.X || layer->GetHeight() != _size.Y) {
					Build();
					return;
				}
				std::int32_t x0 = std::max<std::int32_t>(tiles.X, 0) / _step, y0 = std::max<std::int32_t>(tiles.Y, 0) / _step;
				std::int32_t x1 = std::min<std::int32_t>((tiles.X + tiles.W - 1) / _step, _textureSize.X - 1);
				std::int32_t y1 = std::min<std::int32_t>((tiles.Y + tiles.H - 1) / _step, _textureSize.Y - 1);
				if (x1 < x0 || y1 < y0) {
					return;
				}
				Recti region(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
				FillTexels(*layer, region);
				SmallVector<std::uint8_t, 0> packed;
				packed.resize_for_overwrite(std::size_t(region.W) * std::size_t(region.H) * 4);
				for (std::int32_t y = 0; y < region.H; y++) {
					std::memcpy(&packed[std::size_t(y) * region.W * 4], &_texels[(std::size_t(region.Y + y) * _textureSize.X + region.X) * 4], std::size_t(region.W) * 4);
				}
				_texture->LoadFromTexels(packed.data(), region.X, region.Y, region.W, region.H);
			}

			// Each texel is the average color of its tiles, their colors weighted by how much of them is covered
			void FillTexels(const DocumentLayer& layer, const Recti& region)
			{
				for (std::int32_t ty = region.Y; ty < region.Y + region.H; ty++) {
					for (std::int32_t tx = region.X; tx < region.X + region.W; tx++) {
						std::uint32_t color;
						if (_step == 1) {
							color = _thumbnail.GetTileColor(layer.GetTile(tx, ty));
						} else {
							std::uint32_t r = 0, g = 0, b = 0, a = 0, count = 0;
							for (std::int32_t y = ty * _step; y < std::min((ty + 1) * _step, _size.Y); y++) {
								for (std::int32_t x = tx * _step; x < std::min((tx + 1) * _step, _size.X); x++) {
									std::uint32_t c = _thumbnail.GetTileColor(layer.GetTile(x, y));
									std::uint32_t alpha = (c >> 24);
									r += (c & 0xFF) * alpha;
									g += ((c >> 8) & 0xFF) * alpha;
									b += ((c >> 16) & 0xFF) * alpha;
									a += alpha;
									count++;
								}
							}
							color = (a > 0 ? ((r / a) | ((g / a) << 8) | ((b / a) << 16) | ((a / std::max<std::uint32_t>(count, 1)) << 24)) : 0);
						}
						std::uint8_t* texel = &_texels[(std::size_t(ty) * _textureSize.X + tx) * 4];
						texel[0] = std::uint8_t(color);
						texel[1] = std::uint8_t(color >> 8);
						texel[2] = std::uint8_t(color >> 16);
						texel[3] = std::uint8_t(color >> 24);
					}
				}
			}
		};
	}

	void SetBookmark(IEditorContext& session, std::int32_t slot)
	{
		EditorLevelHandler* editHandler = session.GetEditHandler();
		if (editHandler == nullptr || slot < 1 || slot > 9) {
			return;
		}
		Vector2f center = editHandler->GetCamera().GetCenter();
		ChangeAnnotations(session, _("Set bookmark"), [slot, center](SmallVectorImpl<Annotation>& annotations) {
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

	bool GoToBookmark(IEditorContext& session, std::int32_t slot)
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

	void AddNote(IEditorContext& session, Vector2f worldPos, StringView text)
	{
		ChangeAnnotations(session, _("Add note"), [worldPos, text](SmallVectorImpl<Annotation>& annotations) {
			Annotation& note = annotations.emplace_back();
			note.X = std::int32_t(worldPos.X);
			note.Y = std::int32_t(worldPos.Y);
			note.Text = (text.empty() ? String(_("Note")) : String(text));
			// Who wrote it and when, a level made by more people has notes of all of them
			note.Author = PreferencesCache::GetEffectivePlayerName();
			note.Time = GetCurrentTime();
		});
	}

	void ShowNavigator(IEditorContext& session, bool toggle)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr || ReuseSingleDialog(session, "navigator", toggle)) {
			return;
		}

		IEditorContext* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("Navigator"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();
		auto* minimap = content->Add<MinimapView>(session);
		{
			auto* options = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			options->Add<UiCheckBox>(_("Events"), [minimap]() { return minimap->ShowEvents; }, [minimap](bool value) { minimap->ShowEvents = value; })
				->Tooltip = _("Shows the events in the colors of their categories, and where the camera is limited");
		}

		// The list follows the annotations, rebuilt when their chunk changes or the search does
		struct ListState { AnnotationCache Cache; std::uint32_t Version = 0; bool Built = false; String Search; };
		auto state = std::make_shared<ListState>();
		auto* search = content->Add<UiTextBox>();
		search->Placeholder = _("Search the notes");
		search->OnChanged = [state](StringView text) {
			state->Search = text;
			state->Built = false;
		};

		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 260.0f;
		// 4 bookmarks at a time, as high as a button each with the spacing between the rows, the rest scrolls
		constexpr std::int32_t ShownRows = 4;
		scroll->Height = UiTheme::ButtonHeight * ShownRows + 2.0f * (ShownRows - 1) + 1.0f;

		auto rebuild = [s, scroll, state]() {
			SmallVector<Annotation, 0> annotations;
			Annotation::Read(s->GetDocument(), annotations);
			auto* rows = scroll->SetContent<UiStack>(UiOrientation::Vertical, 2.0f);
			for (std::int32_t slot = 1; slot <= 9; slot++) {
				auto it = std::find_if(annotations.begin(), annotations.end(), [slot](const Annotation& a) { return a.Slot == slot; });
				bool isSet = (it != annotations.end());
				auto* row = rows->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				row->Add<UiLabel>(_f("{}", slot), isSet ? UiTheme::Text : UiTheme::TextDisabled)->Width = 10.0f;
				if (isSet) {
					// Its name and color, a bookmark is known by them in the view and on the map
					auto* name = row->Add<UiTextBox>(it->Text);
					name->Flex = 1.0f;
					name->MaxLength = 64;
					name->Placeholder = _f("Bookmark {}", slot);
					name->OnCommit = [s, slot](StringView value) {
						String text = value.trimmed();
						ChangeAnnotations(*s, _("Rename bookmark"), [slot, &text](SmallVectorImpl<Annotation>& list) {
							for (Annotation& annotation : list) {
								if (annotation.Slot == slot) {
									annotation.Text = text;
								}
							}
						});
					};
					std::uint32_t storedColor = it->Color;
					auto getColor = [storedColor]() {
						return (storedColor != 0 ? Colorf((storedColor & 0xFF) / 255.0f, ((storedColor >> 8) & 0xFF) / 255.0f, ((storedColor >> 16) & 0xFF) / 255.0f, 1.0f)
							: Colorf(0.4f, 0.8f, 1.0f, 1.0f));
					};
					row->Add<UiColorSwatch>(getColor, [s, slot, getColor]() {
						UiSurface* surface = s->GetSurface();
						if (surface == nullptr) {
							return;
						}
						UiColorPicker::ShowPopup(*surface, surface->GetPointerPos(), getColor, [s, slot](const Colorf& color) {
							std::uint32_t rgb = std::uint32_t(std::round(color.R * 255.0f)) | (std::uint32_t(std::round(color.G * 255.0f)) << 8) |
								(std::uint32_t(std::round(color.B * 255.0f)) << 16) | 0xFF000000u;
							ChangeAnnotations(*s, _("Bookmark color"), [slot, rgb](SmallVectorImpl<Annotation>& list) {
								for (Annotation& annotation : list) {
									if (annotation.Slot == slot) {
										annotation.Color = rgb;
									}
								}
							});
						}, false);
					})->Tooltip = _("Color of the bookmark");
					row->Add<UiButton>(_("Go"), EditorIcon::None, [s, slot]() { GoToBookmark(*s, slot); })->Style = UiButtonStyle::Flat;
				} else {
					row->Add<UiLabel>(_f("Bookmark {}", slot), UiTheme::TextDisabled)->Flex = 1.0f;
				}
				auto* set = row->Add<UiButton>(_("Set"), EditorIcon::None, [s, slot]() { SetBookmark(*s, slot); });
				set->Style = UiButtonStyle::Flat;
				set->Tooltip = _f("The middle of the view, Ctrl+Shift+{}", slot);
				if (isSet) {
					row->Add<UiButton>(StringView(), EditorIcon::Delete, [s, slot]() {
						ChangeAnnotations(*s, _("Remove bookmark"), [slot](SmallVectorImpl<Annotation>& list) {
							list.erase(std::remove_if(list.begin(), list.end(), [slot](const Annotation& a) { return a.Slot == slot; }), list.end());
						});
					})->Style = UiButtonStyle::Flat;
				}
			}

			// Notes, those the search finds in their text, author or replies
			std::int32_t shownNotes = 0, notes = 0;
			for (std::size_t i = 0; i < annotations.size(); i++) {
				const Annotation& note = annotations[i];
				if (note.IsBookmark()) {
					continue;
				}
				notes++;
				if (!state->Search.empty()) {
					String haystack = note.Text + " "_s + note.Author;
					for (const Annotation::Reply& reply : note.Replies) {
						haystack += " "_s + reply.Text + " "_s + reply.Author;
					}
					if (!MatchesSearch(haystack, state->Search)) {
						continue;
					}
				}
				shownNotes++;
				std::int32_t index = std::int32_t(i);
				Vector2f pos(float(note.X), float(note.Y));
				rows->Add<UiSeparator>();
				auto* row = rows->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				auto* text = row->Add<UiTextBox>(note.Text);
				text->Flex = 1.0f;
				text->MaxLength = 200;
				text->OnCommit = [s, index](StringView value) {
					String copy = value;
					ChangeAnnotations(*s, _("Edit note"), [index, &copy](SmallVectorImpl<Annotation>& list) {
						if (index < std::int32_t(list.size())) {
							list[index].Text = copy;
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
					ChangeAnnotations(*s, _("Remove note"), [index](SmallVectorImpl<Annotation>& list) {
						if (index < std::int32_t(list.size())) {
							list.erase(list.begin() + index);
						}
					});
				})->Style = UiButtonStyle::Flat;

				// Who wrote it, whether it was done, and the replies to it
				auto* details = rows->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				String author = DescribeAuthor(note.Author, note.Time);
				details->Add<UiLabel>(author.empty() ? String(_("Unknown author")) : author, UiTheme::TextDim)->Flex = 1.0f;
				std::weak_ptr<ListState> weakState = state;
				details->Add<UiCheckBox>(_("Done"), [s, weakState, index]() {
					std::shared_ptr<ListState> state = weakState.lock();
					if (state == nullptr) {
						return false;
					}
					const SmallVectorImpl<Annotation>& list = state->Cache.Get(s->GetDocument());
					return (index < std::int32_t(list.size()) && list[index].Resolved);
				}, [s, index](bool value) {
					ChangeAnnotations(*s, value ? _("Resolve note") : _("Reopen note"), [index, value](SmallVectorImpl<Annotation>& list) {
						if (index < std::int32_t(list.size())) {
							list[index].Resolved = value;
						}
					});
				})->Tooltip = _("What the note is about was done, it's shown dimmer");
				details->Add<UiButton>(_("Reply"), EditorIcon::None, [s, index]() { ShowReplyDialog(*s, index); })->Style = UiButtonStyle::Flat;
				for (const Annotation::Reply& reply : note.Replies) {
					auto* replyRow = rows->Add<UiStack>(UiOrientation::Vertical, 0.0f, UiThickness(10.0f, 0.0f, 0.0f, 0.0f));
					replyRow->Add<UiLabel>(reply.Text, UiTheme::Text);
					replyRow->Add<UiLabel>(DescribeAuthor(reply.Author, reply.Time), UiTheme::TextDim);
				}
			}
			if (notes > 0 && shownNotes == 0) {
				rows->Add<UiLabel>(_("No note matches"), UiTheme::TextDisabled);
			}
		};
		// Checked every frame, a note added in the view or a bookmark set by its keys shows at once
		content->Add<UiUpdateWatcher>([s, state, rebuild](float) {
			state->Cache.Get(s->GetDocument());
			if (!state->Built || state->Cache.GetVersion() != state->Version) {
				state->Version = state->Cache.GetVersion();
				state->Built = true;
				rebuild();
			}
		});

		dialog->AddButton(_("Add Note Here"), [s]() {
			if (EditorLevelHandler* editHandler = s->GetEditHandler()) {
				AddNote(*s, editHandler->GetCamera().GetCenter());
			}
		});
		// Closed by its cross or Escape, adding a note isn't what they do
		dialog->OnCancel = [dialogPtr]() { dialogPtr->Close(); };

		// In the bottom right corner, out of the way of the view, the position is moved to fit the screen
		UiOverlayOptions options;
		options.Position = Vector2f(1.0e6f, 1.0e6f);
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		ShowSingleDialog(session, "navigator", Death::move(dialog), Death::move(options));
	}
}

#endif
