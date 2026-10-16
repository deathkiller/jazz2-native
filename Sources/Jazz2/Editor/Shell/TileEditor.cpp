#include "TileEditor.h"

#if defined(WITH_EDITOR)

#include "TileSetApplyDialog.h"
#include "TileSetsDialog.h"
#include "../EditorSession.h"
#include "../Document/Commands.h"
#include "../Imaging/PaletteMatch.h"
#include "../Imaging/TileImages.h"
#include "../Tools/TilePixelTool.h"
#include "../UI/UiColors.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t TileSize = Formats::LevelFile::TileSize;
		constexpr std::int32_t PixelCount = TileSize * TileSize;
		// Tiles in a row of a tile set, the neighbors above and below are this far
		constexpr std::int32_t TileSetColumns = 10;
		// Pixels of the neighboring tiles shown around the tile
		constexpr std::int32_t NeighborPixels = 6;

		enum class EditMode : std::uint8_t { Pixels, Mask };
		enum class EditTool : std::uint8_t { Pencil, Line, Rectangle, Fill, Replace, Eyedropper, Select };

		struct TileEditState
		{
			std::uint16_t TileId = 0;
			// Where the tile comes from, for its neighbors in the tile set
			TileRef Ref{};
			// The tile as its tile set has it, the level overrides it
			std::uint8_t OriginalIndices[PixelCount];
			std::uint8_t OriginalMask[PixelCount];
			// The tile set has it in true color, the palette indices are the closest colors
			bool Reduced = false;
			std::uint8_t Color = 1;
			EditMode Mode = EditMode::Pixels;
			EditTool Tool = EditTool::Pencil;
			bool ShowNeighbors = true;
			// Heights of the slope made in the mask, in pixels from the bottom
			float SlopeLeft = 0.0f;
			float SlopeRight = 32.0f;

			// A tile the level stores in itself is changed right there, in true color if it's a true color one
			bool LevelTile = false;
			std::int32_t Slot = -1;
			std::int32_t Index = 0;
			String SetName;
			bool TrueColor = false;
			std::uint32_t Rgba = 0xFFFFFFFFu;
			// The mask of a tile of the level, a byte a pixel
			std::uint8_t LevelMask[PixelCount];
		};

		// Reads the tile as its tile set has it, as palette indices of the level and a byte per pixel of the mask
		bool LoadOriginalTile(EditorSession& session, const TileRef& tile, TileEditState& state)
		{
			LevelDocument& document = session.GetDocument();
			StringView name;
			std::int32_t index = tile.Index;
			const std::uint8_t* remapping = nullptr;
			if (tile.Source == TileRef::DefaultTileSet) {
				name = document.Properties.DefaultTileSet;
			} else if (tile.Source != TileRef::Animated && tile.Source != TileRef::Raw && tile.Source <= document.ExtraTileSets.size()) {
				const Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets[tile.Source - 1];
				name = tileSet.Name;
				index += tileSet.Offset;
				if ((tileSet.Flags & Formats::ExtraTileSetRecord::Remapped) != 0 && (tileSet.Flags & Formats::ExtraTileSetRecord::TrueColor) == 0) {
					remapping = tileSet.Remapping;
				}
				if ((tileSet.Flags & Formats::ExtraTileSetRecord::Embedded) != 0) {
					state.LevelTile = true;
					state.Slot = tile.Source - 1;
					state.Index = index;
					state.SetName = tileSet.Name;
				}
			} else {
				return false;
			}

			const Formats::TileSetData* data = session.GetTileSetData(name);
			if (data == nullptr || index < 0 || index >= data->TileCount) {
				return false;
			}

			const std::uint8_t* image = &data->Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
			if (data->TrueColor[index] == 0) {
				for (std::int32_t i = 0; i < PixelCount; i++) {
					state.OriginalIndices[i] = (remapping != nullptr ? remapping[image[i]] : image[i]);
				}
			} else if (state.LevelTile) {
				state.TrueColor = true;
				std::memset(state.OriginalIndices, 0, sizeof(state.OriginalIndices));
			} else {
				// The closest entry of the palette the level is shown in, transparent pixels are index 0
				const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
				OkLab colors[256];
				for (std::int32_t i = 1; i < 256; i++) {
					colors[i] = OkLab::FromPaletteEntry(palette[i]);
				}
				for (std::int32_t i = 0; i < PixelCount; i++) {
					const std::uint8_t* pixel = &image[i * 4];
					if (pixel[3] < 128) {
						state.OriginalIndices[i] = 0;
						continue;
					}
					OkLab color = OkLab::FromPaletteEntry(std::uint32_t(pixel[0]) | (std::uint32_t(pixel[1]) << 8) | (std::uint32_t(pixel[2]) << 16));
					std::int32_t best = 1;
					float bestDistance = color.DistanceSquared(colors[1]);
					for (std::int32_t j = 2; j < 256; j++) {
						float distance = color.DistanceSquared(colors[j]);
						if (distance < bestDistance) {
							bestDistance = distance;
							best = j;
						}
					}
					state.OriginalIndices[i] = std::uint8_t(best);
				}
				state.Reduced = true;
			}

			// A bit per pixel, row by row, the lowest bit first
			const std::uint8_t* mask = &data->Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile];
			for (std::int32_t i = 0; i < PixelCount; i++) {
				state.OriginalMask[i] = ((mask[i / 8] >> (i % 8)) & 1);
			}
			return true;
		}

		// The tile set of the level the tile is in, as it is now
		const Formats::TileSetData* GetLevelData(EditorSession& session, const TileEditState& state)
		{
			const Formats::TileSetData* data = session.GetTileSetData(state.SetName);
			return (data != nullptr && state.Index < data->TileCount ? data : nullptr);
		}

		// The pixels and the mask as the level has them now, overridden or not
		const std::uint8_t* GetIndices(EditorSession& session, const TileEditState& state)
		{
			if (state.LevelTile) {
				const Formats::TileSetData* data = GetLevelData(session, state);
				return (data != nullptr ? &data->Images[std::size_t(state.Index) * Formats::TileSetData::ImageBytesPerTile] : state.OriginalIndices);
			}
			const Formats::TileDiffuseOverride* tile = SetTileOverrideCommand::FindDiffuse(session.GetDocument(), state.TileId);
			return (tile != nullptr ? tile->Indices : state.OriginalIndices);
		}

		// The colors of a true color tile of the level, 4 bytes RGBA a pixel
		const std::uint8_t* GetRgba(EditorSession& session, const TileEditState& state)
		{
			const Formats::TileSetData* data = GetLevelData(session, state);
			return (data != nullptr ? &data->Images[std::size_t(state.Index) * Formats::TileSetData::ImageBytesPerTile] : nullptr);
		}

		const std::uint8_t* GetMask(EditorSession& session, TileEditState& state)
		{
			if (state.LevelTile) {
				const Formats::TileSetData* data = GetLevelData(session, state);
				if (data == nullptr) {
					return state.OriginalMask;
				}
				const std::uint8_t* bits = &data->Masks[std::size_t(state.Index) * Formats::TileSetData::MaskBytesPerTile];
				for (std::int32_t i = 0; i < PixelCount; i++) {
					state.LevelMask[i] = ((bits[i / 8] >> (i % 8)) & 1);
				}
				return state.LevelMask;
			}
			const Formats::TileMaskOverride* tile = SetTileOverrideCommand::FindMask(session.GetDocument(), state.TileId);
			return (tile != nullptr ? tile->Mask : state.OriginalMask);
		}

		std::uint32_t ReadRgba(const std::uint8_t* rgba, std::int32_t i)
		{
			return std::uint32_t(rgba[i * 4]) | (std::uint32_t(rgba[i * 4 + 1]) << 8) | (std::uint32_t(rgba[i * 4 + 2]) << 16) | (std::uint32_t(rgba[i * 4 + 3]) << 24);
		}

		void PackMask(const std::uint8_t* values, std::uint8_t* maskBits)
		{
			std::memset(maskBits, 0, Formats::TileSetData::MaskBytesPerTile);
			for (std::int32_t i = 0; i < PixelCount; i++) {
				if (values[i] != 0) {
					maskBits[i / 8] |= std::uint8_t(1 << (i % 8));
				}
			}
		}

		// Changes a tile of the level as one step, merged with the steps of the same kind that follow while allowed
		void ChangeLevelTile(EditorSession& session, const TileEditState& state, Function<void(std::uint8_t* image, std::uint8_t* maskBits)>&& change,
			bool allowMerge, StringView name)
		{
			const Formats::TileSetData* data = GetLevelData(session, state);
			if (data == nullptr) {
				return;
			}
			Formats::TileSetData copy = *data;
			std::uint8_t* image = &copy.Images[std::size_t(state.Index) * Formats::TileSetData::ImageBytesPerTile];
			std::uint8_t* maskBits = &copy.Masks[std::size_t(state.Index) * Formats::TileSetData::MaskBytesPerTile];
			change(image, maskBits);
			if (std::memcmp(image, &data->Images[std::size_t(state.Index) * Formats::TileSetData::ImageBytesPerTile], Formats::TileSetData::ImageBytesPerTile) == 0 &&
				std::memcmp(maskBits, &data->Masks[std::size_t(state.Index) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile) == 0) {
				return;
			}
			SmallVector<std::uint8_t, 0> bytes;
			if (!SerializeTileSet(copy, bytes)) {
				session.ShowMessage(_("The tile set couldn't be saved"));
				return;
			}
			session.Execute(std::make_unique<SetLevelTilesCommand>(session.GetDocument(), state.Slot, std::move(bytes), name), allowMerge);
		}

		// Changes the pixels or the mask, the other one stays overridden or not as it was
		void ChangeTile(EditorSession& session, TileEditState& state, bool mask, Function<void(std::uint8_t*)>&& change, bool allowMerge, StringView name)
		{
			if (state.LevelTile) {
				// The mask a byte a pixel while it's changed, the pixels of an 8-bit tile are the palette indices
				std::uint8_t values[PixelCount];
				std::memcpy(values, mask ? GetMask(session, state) : GetIndices(session, state), sizeof(values));
				change(values);
				ChangeLevelTile(session, state, [mask, &values](std::uint8_t* image, std::uint8_t* maskBits) {
					if (mask) {
						PackMask(values, maskBits);
					} else {
						std::memcpy(image, values, PixelCount);
					}
				}, allowMerge, name);
				return;
			}

			LevelDocument& document = session.GetDocument();
			const Formats::TileDiffuseOverride* diffuse = SetTileOverrideCommand::FindDiffuse(document, state.TileId);
			const Formats::TileMaskOverride* maskOverride = SetTileOverrideCommand::FindMask(document, state.TileId);
			std::uint8_t data[PixelCount];
			std::memcpy(data, mask ? GetMask(session, state) : GetIndices(session, state), sizeof(data));
			change(data);
			const std::uint8_t* current = (mask ? GetMask(session, state) : GetIndices(session, state));
			if (std::memcmp(data, current, sizeof(data)) == 0 && (mask ? maskOverride != nullptr : diffuse != nullptr)) {
				return;
			}
			session.Execute(std::make_unique<SetTileOverrideCommand>(document, state.TileId,
				mask ? (diffuse != nullptr ? diffuse->Indices : nullptr) : data,
				mask ? data : (maskOverride != nullptr ? maskOverride->Mask : nullptr), name), allowMerge);
		}

		// Changes the colors of a true color tile of the level
		void ChangeRgba(EditorSession& session, const TileEditState& state, Function<void(std::uint32_t*)>&& change, bool allowMerge, StringView name)
		{
			ChangeLevelTile(session, state, [&change](std::uint8_t* image, std::uint8_t*) {
				std::uint32_t colors[PixelCount];
				for (std::int32_t i = 0; i < PixelCount; i++) {
					colors[i] = ReadRgba(image, i);
				}
				change(colors);
				for (std::int32_t i = 0; i < PixelCount; i++) {
					image[i * 4] = std::uint8_t(colors[i]);
					image[i * 4 + 1] = std::uint8_t(colors[i] >> 8);
					image[i * 4 + 2] = std::uint8_t(colors[i] >> 16);
					image[i * 4 + 3] = std::uint8_t(colors[i] >> 24);
				}
			}, allowMerge, name);
		}

		// Moves pixels and the mask together, so the collision stays where the picture is. @p source gives the pixel
		// each pixel takes, or -1 for an empty one.
		void TransformTile(EditorSession& session, TileEditState& state, Function<std::int32_t(std::int32_t, std::int32_t)>&& source, StringView name)
		{
			std::uint8_t mask[PixelCount];
			std::uint8_t newMask[PixelCount];
			std::memcpy(mask, GetMask(session, state), sizeof(mask));
			std::int32_t sources[PixelCount];
			for (std::int32_t y = 0; y < TileSize; y++) {
				for (std::int32_t x = 0; x < TileSize; x++) {
					std::int32_t from = source(x, y);
					sources[y * TileSize + x] = from;
					newMask[y * TileSize + x] = (from >= 0 ? mask[from] : 0);
				}
			}

			if (state.LevelTile) {
				std::int32_t bytesPerPixel = (state.TrueColor ? 4 : 1);
				ChangeLevelTile(session, state, [&sources, &newMask, bytesPerPixel](std::uint8_t* image, std::uint8_t* maskBits) {
					std::uint8_t copy[PixelCount * 4];
					std::memcpy(copy, image, std::size_t(PixelCount * bytesPerPixel));
					for (std::int32_t i = 0; i < PixelCount; i++) {
						for (std::int32_t b = 0; b < bytesPerPixel; b++) {
							image[i * bytesPerPixel + b] = (sources[i] >= 0 ? copy[sources[i] * bytesPerPixel + b] : 0);
						}
					}
					PackMask(newMask, maskBits);
				}, false, name);
				return;
			}

			const std::uint8_t* indices = GetIndices(session, state);
			std::uint8_t newIndices[PixelCount];
			for (std::int32_t i = 0; i < PixelCount; i++) {
				newIndices[i] = (sources[i] >= 0 ? indices[sources[i]] : 0);
			}
			session.Execute(std::make_unique<SetTileOverrideCommand>(session.GetDocument(), state.TileId, newIndices, newMask, name));
		}

		Colorf RgbaToColor(std::uint32_t rgba)
		{
			return Colorf((rgba & 0xFF) / 255.0f, ((rgba >> 8) & 0xFF) / 255.0f, ((rgba >> 16) & 0xFF) / 255.0f, (rgba >> 24) / 255.0f);
		}

		std::uint32_t ColorToRgba(const Colorf& color)
		{
			auto channel = [](float value) { return std::uint32_t(std::clamp<std::int32_t>(std::int32_t(std::round(value * 255.0f)), 0, 255)); };
			return channel(color.R) | (channel(color.G) << 8) | (channel(color.B) << 16) | (channel(color.A) << 24);
		}

		template<typename T>
		void PaintLine(T* data, Vector2i from, Vector2i to, T value)
		{
			std::int32_t steps = std::max(std::abs(to.X - from.X), std::abs(to.Y - from.Y));
			for (std::int32_t i = 0; i <= steps; i++) {
				float t = (steps > 0 ? float(i) / float(steps) : 0.0f);
				std::int32_t x = std::int32_t(std::round(from.X + (to.X - from.X) * t));
				std::int32_t y = std::int32_t(std::round(from.Y + (to.Y - from.Y) * t));
				data[y * TileSize + x] = value;
			}
		}

		template<typename T>
		void PaintRectangle(T* data, Vector2i a, Vector2i b, T value, bool filled)
		{
			std::int32_t x0 = std::min(a.X, b.X), x1 = std::max(a.X, b.X), y0 = std::min(a.Y, b.Y), y1 = std::max(a.Y, b.Y);
			for (std::int32_t y = y0; y <= y1; y++) {
				for (std::int32_t x = x0; x <= x1; x++) {
					if (filled || x == x0 || x == x1 || y == y0 || y == y1) {
						data[y * TileSize + x] = value;
					}
				}
			}
		}

		template<typename T>
		void FloodFill(T* data, Vector2i start, T value)
		{
			T target = data[start.Y * TileSize + start.X];
			if (target == value) {
				return;
			}
			SmallVector<Vector2i, 64> stack;
			stack.push_back(start);
			while (!stack.empty()) {
				Vector2i pixel = stack.back();
				stack.pop_back();
				if (pixel.X < 0 || pixel.X >= TileSize || pixel.Y < 0 || pixel.Y >= TileSize || data[pixel.Y * TileSize + pixel.X] != target) {
					continue;
				}
				data[pixel.Y * TileSize + pixel.X] = value;
				stack.push_back(Vector2i(pixel.X + 1, pixel.Y));
				stack.push_back(Vector2i(pixel.X - 1, pixel.Y));
				stack.push_back(Vector2i(pixel.X, pixel.Y + 1));
				stack.push_back(Vector2i(pixel.X, pixel.Y - 1));
			}
		}

		// Every pixel of one color gets another, connected or not
		template<typename T>
		void ReplaceValue(T* data, T target, T value)
		{
			for (std::int32_t i = 0; i < PixelCount; i++) {
				if (data[i] == target) {
					data[i] = value;
				}
			}
		}

		// The pixels of a rectangle move by an offset, the place they leave is empty
		template<typename T>
		void MoveArea(T* data, const Recti& area, Vector2i offset)
		{
			T copy[PixelCount];
			std::memcpy(copy, data, sizeof(copy));
			for (std::int32_t y = area.Y; y < area.Y + area.H; y++) {
				for (std::int32_t x = area.X; x < area.X + area.W; x++) {
					data[y * TileSize + x] = T(0);
				}
			}
			for (std::int32_t y = area.Y; y < area.Y + area.H; y++) {
				for (std::int32_t x = area.X; x < area.X + area.W; x++) {
					std::int32_t tx = x + offset.X, ty = y + offset.Y;
					if (tx >= 0 && tx < TileSize && ty >= 0 && ty < TileSize) {
						data[ty * TileSize + tx] = copy[y * TileSize + x];
					}
				}
			}
		}

		/** The pixels of the tile, large, to paint on, with the edges of its neighbors in the tile set around */
		class TilePixelCanvas : public UiElement
		{
		public:
			static constexpr float PixelUnits = 5.0f;

			TilePixelCanvas(EditorSession& session, std::shared_ptr<TileEditState> state)
				: _session(session), _state(std::move(state)), _lastPixel(-1, -1), _startPixel(-1, -1), _button(MouseButton::Left),
					_dragging(false), _moving(false), _shape(false)
			{
			}

			/** Pixels of a stroke, given by the tile pixel tool in the level too */
			void PaintPixel(Vector2i pixel, MouseButton button, bool continued)
			{
				_button = button;
				Paint(pixel, pixel, continued);
			}

			void ClearSelection()
			{
				_selection = Recti();
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float size = context.Px(PixelUnits) * (TileSize + NeighborPixels * 2);
				return Vector2f(size, size);
			}

			void Draw(UiPainter& painter) override
			{
				float cell = GetCell();
				Vector2f origin = GetOrigin();
				bool trueColor = IsTrueColor();
				const std::uint8_t* indices = (trueColor ? nullptr : GetIndices(_session, *_state));
				const std::uint8_t* rgba = (trueColor ? GetRgba(_session, *_state) : nullptr);
				const std::uint8_t* mask = GetMask(_session, *_state);
				const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
				bool maskMode = (_state->Mode == EditMode::Mask);

				if (_state->ShowNeighbors) {
					DrawNeighbors(painter, origin, cell);
				}

				for (std::int32_t y = 0; y < TileSize; y++) {
					for (std::int32_t x = 0; x < TileSize; x++) {
						std::int32_t i = y * TileSize + x;
						Rectf rect(origin.X + x * cell, origin.Y + y * cell, cell, cell);
						Colorf color;
						if (trueColor) {
							color = RgbaToColor(rgba != nullptr ? ReadRgba(rgba, i) : 0);
						} else if (indices[i] != 0) {
							color = UiPaletteGrid::ToColor(palette[indices[i]]);
							color.A = 1.0f;
						} else {
							color = Colorf(0.0f, 0.0f, 0.0f, 0.0f);
						}
						if (color.A < 1.0f) {
							// Transparent, a checkerboard of two pixels in a cell
							bool even = (((x + y) & 1) == 0);
							painter.FillRect(rect, even ? Colorf(0.42f, 0.42f, 0.46f, 1.0f) : Colorf(0.32f, 0.32f, 0.36f, 1.0f), UiLayer::Fill);
						}
						if (color.A > 0.0f) {
							if (maskMode) {
								// The colors only hint the tile under the mask
								color = Colorf(color.R * 0.45f, color.G * 0.45f, color.B * 0.45f, color.A);
							}
							painter.FillRect(rect, color, UiLayer::Fill);
						}
						if (maskMode && mask[i] != 0) {
							painter.FillRect(rect, Colorf(1.0f, 1.0f, 1.0f, 0.55f), UiLayer::Content);
						}
					}
				}

				// The grid when the pixels are large enough to tell
				if (cell >= 5.0f) {
					Colorf line(0.0f, 0.0f, 0.0f, 0.25f);
					for (std::int32_t i = 1; i < TileSize; i++) {
						painter.FillRect(Rectf(origin.X + i * cell, origin.Y, 1.0f, cell * TileSize), line, UiLayer::Overlay);
						painter.FillRect(Rectf(origin.X, origin.Y + i * cell, cell * TileSize, 1.0f), line, UiLayer::Overlay);
					}
				}
				float thickness = std::max(1.0f, std::round(painter.GetScale()));
				painter.Frame(Rectf(origin.X - thickness, origin.Y - thickness, cell * TileSize + thickness * 2.0f, cell * TileSize + thickness * 2.0f),
					UiTheme::InputBorder, thickness, UiLayer::Overlay);

				// What a line or a rectangle will be, while it's dragged
				if (_dragging && _shape && _lastPixel.X >= 0) {
					std::uint8_t preview[PixelCount] = {};
					if (_state->Tool == EditTool::Line) {
						PaintLine<std::uint8_t>(preview, _startPixel, _lastPixel, 1);
					} else {
						PaintRectangle<std::uint8_t>(preview, _startPixel, _lastPixel, 1, IsFilled());
					}
					Colorf color = UiTheme::Accent;
					color.A = 0.75f;
					for (std::int32_t i = 0; i < PixelCount; i++) {
						if (preview[i] != 0) {
							painter.FillRect(Rectf(origin.X + (i % TileSize) * cell, origin.Y + (i / TileSize) * cell, cell, cell), color, UiLayer::Overlay);
						}
					}
				}

				// The selection, where it's being moved to
				if (_selection.W > 0 && _selection.H > 0) {
					Vector2i offset = (_moving ? _moveOffset : Vector2i());
					Rectf rect(origin.X + (_selection.X + offset.X) * cell, origin.Y + (_selection.Y + offset.Y) * cell, _selection.W * cell, _selection.H * cell);
					painter.Frame(rect, Colorf(0.0f, 0.0f, 0.0f, 0.8f), thickness * 2.0f, UiLayer::Overlay);
					painter.Frame(rect, UiTheme::Accent, thickness, UiLayer::Overlay);
				}

				if (_hovered && GetSurface() != nullptr) {
					Vector2i pixel = GetPixelAt(GetSurface()->GetPointerPos());
					if (pixel.X >= 0) {
						painter.Frame(Rectf(origin.X + pixel.X * cell, origin.Y + pixel.Y * cell, cell, cell), UiTheme::Cursor, thickness, UiLayer::Overlay);
					}
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				if (event.Button != MouseButton::Left && event.Button != MouseButton::Right) {
					return false;
				}
				Vector2i pixel = GetPixelAt(event.Pos);
				if (pixel.X < 0) {
					return false;
				}
				_button = event.Button;
				_lastPixel = pixel;
				_startPixel = pixel;
				_modifiers = event.Modifiers;
				switch (_state->Tool) {
					case EditTool::Pencil:
						Paint(pixel, pixel, false);
						_dragging = true;
						return true;
					case EditTool::Line:
					case EditTool::Rectangle:
						_dragging = true;
						_shape = true;
						return true;
					case EditTool::Fill:
						Modify(false, _state->Mode == EditMode::Mask ? _("Fill tile mask") : _("Fill tile"), [pixel](auto* data, auto value) {
							FloodFill(data, pixel, value);
						});
						return false;
					case EditTool::Replace:
						Modify(false, _("Replace color"), [pixel](auto* data, auto value) {
							ReplaceValue(data, data[pixel.Y * TileSize + pixel.X], value);
						});
						return false;
					case EditTool::Select:
						if (event.Button == MouseButton::Right) {
							// Empties the selection, or the pixel without one
							Recti area = (_selection.W > 0 ? _selection : Recti(pixel.X, pixel.Y, 1, 1));
							_button = MouseButton::Right;
							Modify(false, _("Clear pixels"), [area](auto* data, auto value) {
								for (std::int32_t y = area.Y; y < area.Y + area.H; y++) {
									for (std::int32_t x = area.X; x < area.X + area.W; x++) {
										data[y * TileSize + x] = value;
									}
								}
							});
							return false;
						}
						_dragging = true;
						_moving = (_selection.W > 0 && _selection.Contains(pixel));
						_moveOffset = Vector2i();
						if (!_moving) {
							_selection = Recti(pixel.X, pixel.Y, 1, 1);
						}
						return true;
					default:
						if (_state->Mode == EditMode::Pixels) {
							std::int32_t i = pixel.Y * TileSize + pixel.X;
							if (IsTrueColor()) {
								if (const std::uint8_t* rgba = GetRgba(_session, *_state)) {
									_state->Rgba = ReadRgba(rgba, i);
								}
							} else {
								_state->Color = GetIndices(_session, *_state)[i];
							}
						}
						return false;
				}
			}

			void OnPointerMove(const UiPointerEvent& event) override
			{
				if (!_pressed || !_dragging) {
					return;
				}
				Vector2i pixel = GetPixelAt(event.Pos, true);
				_modifiers = event.Modifiers;
				if (pixel == _lastPixel) {
					return;
				}
				switch (_state->Tool) {
					case EditTool::Pencil:
						Paint(_lastPixel, pixel, true);
						break;
					case EditTool::Select:
						if (_moving) {
							_moveOffset = pixel - _startPixel;
						} else {
							_selection = Recti(std::min(_startPixel.X, pixel.X), std::min(_startPixel.Y, pixel.Y),
								std::abs(pixel.X - _startPixel.X) + 1, std::abs(pixel.Y - _startPixel.Y) + 1);
						}
						break;
					default:
						break;
				}
				_lastPixel = pixel;
			}

			void OnPointerUp(const UiPointerEvent& event, bool inside) override
			{
				if (!_dragging) {
					return;
				}
				_dragging = false;
				if (_shape) {
					_shape = false;
					Vector2i from = _startPixel, to = _lastPixel;
					if (_state->Tool == EditTool::Line) {
						Modify(false, _state->Mode == EditMode::Mask ? _("Line in tile mask") : _("Line in tile"), [from, to](auto* data, auto value) {
							PaintLine(data, from, to, value);
						});
					} else {
						bool filled = IsFilled();
						Modify(false, _state->Mode == EditMode::Mask ? _("Rectangle in tile mask") : _("Rectangle in tile"), [from, to, filled](auto* data, auto value) {
							PaintRectangle(data, from, to, value, filled);
						});
					}
				} else if (_moving) {
					_moving = false;
					if (_moveOffset != Vector2i()) {
						Recti area = _selection;
						Vector2i offset = _moveOffset;
						Modify(false, _("Move pixels"), [area, offset](auto* data, auto) {
							MoveArea(data, area, offset);
						});
						_selection.X += offset.X;
						_selection.Y += offset.Y;
						// What went past the edge of the tile is gone, the selection is what is left of it
						Recti clipped = _selection;
						clipped.Intersect(Recti(0, 0, TileSize, TileSize));
						_selection = (clipped.W > 0 && clipped.H > 0 ? clipped : Recti());
					}
				}
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				Vector2i pixel = GetPixelAt(pos);
				if (pixel.X < 0) {
					return {};
				}
				std::int32_t i = pixel.Y * TileSize + pixel.X;
				if (IsTrueColor()) {
					const std::uint8_t* rgba = GetRgba(_session, *_state);
					std::uint32_t color = (rgba != nullptr ? ReadRgba(rgba, i) : 0);
					char hex[16];
					std::snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF, color >> 24);
					_tooltip = _f("{}, {}: {}", pixel.X, pixel.Y, hex);
				} else {
					_tooltip = _f("{}, {}: color {}", pixel.X, pixel.Y, GetIndices(_session, *_state)[i]);
				}
				return _tooltip;
			}

		private:
			EditorSession& _session;
			std::shared_ptr<TileEditState> _state;
			Vector2i _lastPixel;
			Vector2i _startPixel;
			MouseButton _button;
			std::int32_t _modifiers = 0;
			bool _dragging;
			bool _moving;
			bool _shape;
			Recti _selection;
			Vector2i _moveOffset;
			mutable String _tooltip;

			bool IsTrueColor() const
			{
				return (_state->LevelTile && _state->TrueColor);
			}

			// A rectangle is filled with Shift held
			bool IsFilled() const
			{
				return ((_modifiers & KeyMod::Shift) != 0);
			}

			float GetCell() const
			{
				return std::floor(Bounds.W / (TileSize + NeighborPixels * 2));
			}

			Vector2f GetOrigin() const
			{
				float cell = GetCell();
				return Vector2f(Bounds.X + cell * NeighborPixels, Bounds.Y + cell * NeighborPixels);
			}

			Vector2i GetPixelAt(Vector2f pos, bool clamp = false) const
			{
				float cell = GetCell();
				if (cell <= 0.0f) {
					return Vector2i(-1, -1);
				}
				Vector2f origin = GetOrigin();
				std::int32_t x = std::int32_t(std::floor((pos.X - origin.X) / cell));
				std::int32_t y = std::int32_t(std::floor((pos.Y - origin.Y) / cell));
				if (clamp) {
					return Vector2i(std::clamp(x, 0, TileSize - 1), std::clamp(y, 0, TileSize - 1));
				}
				return (x >= 0 && x < TileSize && y >= 0 && y < TileSize ? Vector2i(x, y) : Vector2i(-1, -1));
			}

			// The tiles next to this one in its tile set, as large, dimmed, cut off at the edge of the canvas
			void DrawNeighbors(UiPainter& painter, Vector2f origin, float cell)
			{
				Tiles::TileMap* tileMap = _session.GetTileMap();
				const TileRef& ref = _state->Ref;
				if (tileMap == nullptr || ref.IsAnimated() || ref.Source == TileRef::Raw) {
					return;
				}
				LevelDocument& document = _session.GetDocument();
				std::int32_t tileCount = document.GetSourceTileCount(ref.Source);
				std::int32_t column = ref.Index % TileSetColumns;
				float size = cell * TileSize;
				painter.PushClip(Rectf(Bounds.X, Bounds.Y, cell * (TileSize + NeighborPixels * 2), cell * (TileSize + NeighborPixels * 2)));
				for (std::int32_t dy = -1; dy <= 1; dy++) {
					for (std::int32_t dx = -1; dx <= 1; dx++) {
						if ((dx == 0 && dy == 0) || column + dx < 0 || column + dx >= TileSetColumns) {
							continue;
						}
						std::int32_t index = std::int32_t(ref.Index) + dy * TileSetColumns + dx;
						if (index <= 0 || (tileCount >= 0 && index >= tileCount)) {
							continue;
						}
						std::int32_t tileId = document.ToTileId(TileRef{ std::uint16_t(index), ref.Source, 0 });
						// Over the background of the window, under the pixels of the tile drawn after it
						painter.Tile(*tileMap, tileId, 0, Rectf(origin.X + dx * size, origin.Y + dy * size, size, size), 0.5f, UiLayer::Fill);
					}
				}
				painter.PopClip();
			}

			// The left button paints the color or a solid pixel, the right one a transparent or an empty one
			std::uint8_t GetValue() const
			{
				bool primary = (_button == MouseButton::Left);
				return (_state->Mode == EditMode::Mask ? std::uint8_t(primary ? 1 : 0) : (primary ? _state->Color : std::uint8_t(0)));
			}

			std::uint32_t GetRgbaValue() const
			{
				return (_button == MouseButton::Left ? _state->Rgba : 0u);
			}

			// Changes what the mode edits, the colors of a true color tile as 32-bit values, anything else as bytes,
			// with the value the button paints
			template<class Operation>
			void Modify(bool continued, StringView name, Operation operation)
			{
				bool mask = (_state->Mode == EditMode::Mask);
				if (!mask && IsTrueColor()) {
					std::uint32_t value = GetRgbaValue();
					ChangeRgba(_session, *_state, [&operation, value](std::uint32_t* data) { operation(data, value); }, continued, name);
				} else {
					std::uint8_t value = GetValue();
					ChangeTile(_session, *_state, mask, [&operation, value](std::uint8_t* data) { operation(data, value); }, continued, name);
				}
			}

			// A line, so a fast stroke has no gaps, the whole stroke is a single step
			void Paint(Vector2i from, Vector2i to, bool continued)
			{
				Modify(continued, _state->Mode == EditMode::Mask ? _("Paint tile mask") : _("Paint tile"), [from, to](auto* data, auto value) {
					PaintLine(data, from, to, value);
				});
			}
		};

		// The mask of a tile as the level has it now, overridden or not
		bool ReadTileMask(EditorSession& session, const TileRef& tile, std::uint8_t* mask)
		{
			TileEditState other;
			std::int32_t tileId = session.GetDocument().ToTileId(tile);
			if (tile.IsAnimated() || tileId <= 0 || tileId > UINT16_MAX || !LoadOriginalTile(session, tile, other)) {
				return false;
			}
			other.TileId = std::uint16_t(tileId);
			std::memcpy(mask, GetMask(session, other), PixelCount);
			return true;
		}
	}

	void ShowTileEditor(EditorSession& session, const TileRef& tile)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}
		if (tile.IsAnimated()) {
			session.ShowMessage(_("An animated tile is changed by changing the tiles of its frames"));
			return;
		}

		auto state = std::make_shared<TileEditState>();
		std::int32_t tileId = session.GetDocument().ToTileId(tile);
		if (tileId <= 0 || tileId > UINT16_MAX || !LoadOriginalTile(session, tile, *state)) {
			session.ShowMessage(_("The tile can't be read from its tile set"));
			return;
		}
		state->TileId = std::uint16_t(tileId);
		state->Ref = TileRef{ tile.Index, tile.Source, 0 };

		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_f("Tile {} in This Level", tileId));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();

		{
			auto* info = content->Add<UiLabel>(StringView(), UiTheme::TextDim);
			info->TextFunc = [s, state]() {
				if (state->LevelTile) {
					return String(state->TrueColor ? _("A tile of this level in true color, only this level has it") : _("A tile of this level, only this level has it"));
				}
				bool overridden = (SetTileOverrideCommand::FindDiffuse(s->GetDocument(), state->TileId) != nullptr ||
					SetTileOverrideCommand::FindMask(s->GetDocument(), state->TileId) != nullptr);
				return String(overridden ? _("Changed in this level, its tile set stays as it is") : _("As its tile set has it"));
			};
			if (state->Reduced) {
				content->Add<UiLabel>(_("A true color tile, its colors are the closest of the palette"), UiTheme::TextAccent);
			}
		}

		auto canvasHolder = std::make_shared<TilePixelCanvas*>(nullptr);

		// What is painted, and with what
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			struct ModeOption { const char* Label; EditMode Mode; };
			static const ModeOption Modes[] = { { N_("Pixels"), EditMode::Pixels }, { N_("Mask"), EditMode::Mask } };
			for (const ModeOption& option : Modes) {
				EditMode mode = option.Mode;
				auto* button = row->Add<UiButton>(_(option.Label), mode == EditMode::Mask ? EditorIcon::Mask : EditorIcon::Tiles, [state, mode]() { state->Mode = mode; });
				button->Style = UiButtonStyle::Flat;
				button->IsCheckedFunc = [state, mode]() { return (state->Mode == mode); };
			}
			row->Add<UiSeparator>(UiOrientation::Vertical);
			struct ToolOption { EditorIcon Icon; EditTool Tool; const char* Tooltip; };
			static const ToolOption Tools[] = {
				{ EditorIcon::Brush, EditTool::Pencil, N_("Pencil - the right button erases") },
				{ EditorIcon::Path, EditTool::Line, N_("Line - the right button erases") },
				{ EditorIcon::Rectangle, EditTool::Rectangle, N_("Rectangle, filled with Shift - the right button erases") },
				{ EditorIcon::Fill, EditTool::Fill, N_("Fill - the right button erases") },
				{ EditorIcon::Wand, EditTool::Replace, N_("Replace a color everywhere in the tile - the right button erases it") },
				{ EditorIcon::Select, EditTool::Select, N_("Select pixels and drag them to move them - the right button clears them") },
				{ EditorIcon::Eyedropper, EditTool::Eyedropper, N_("Pick a color") },
			};
			for (const ToolOption& option : Tools) {
				EditTool tool = option.Tool;
				auto* button = row->Add<UiButton>(StringView(), option.Icon, [state, tool, canvasHolder]() {
					state->Tool = tool;
					if (tool != EditTool::Select && *canvasHolder != nullptr) {
						(*canvasHolder)->ClearSelection();
					}
				});
				button->Style = UiButtonStyle::Flat;
				button->Tooltip = _(option.Tooltip);
				button->IsCheckedFunc = [state, tool]() { return (state->Tool == tool); };
			}
		}

		// Moving the whole tile, the pixels and the mask together, so the collision stays where the picture is
		{
			auto* row = content->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			struct TransformOption { EditorIcon Icon; const char* Text; const char* Tooltip; std::int32_t Kind; };
			static const TransformOption Transforms[] = {
				{ EditorIcon::FlipX, nullptr, N_("Flip horizontally"), 0 },
				{ EditorIcon::FlipY, nullptr, N_("Flip vertically"), 1 },
				{ EditorIcon::Redo, nullptr, N_("Rotate clockwise"), 2 },
				{ EditorIcon::ChevronLeft, nullptr, N_("Shift a pixel to the left, what goes out comes back on the other side"), 3 },
				{ EditorIcon::ChevronRight, nullptr, N_("Shift a pixel to the right, what goes out comes back on the other side"), 4 },
				{ EditorIcon::ArrowUp, nullptr, N_("Shift a pixel up, what goes out comes back on the other side"), 5 },
				{ EditorIcon::ArrowDown, nullptr, N_("Shift a pixel down, what goes out comes back on the other side"), 6 },
			};
			for (const TransformOption& option : Transforms) {
				std::int32_t kind = option.Kind;
				auto* button = row->Add<UiButton>(StringView(), option.Icon, [s, state, kind]() {
					switch (kind) {
						case 0: TransformTile(*s, *state, [](std::int32_t x, std::int32_t y) { return y * TileSize + (TileSize - 1 - x); }, _("Flip tile")); break;
						case 1: TransformTile(*s, *state, [](std::int32_t x, std::int32_t y) { return (TileSize - 1 - y) * TileSize + x; }, _("Flip tile")); break;
						case 2: TransformTile(*s, *state, [](std::int32_t x, std::int32_t y) { return (TileSize - 1 - x) * TileSize + y; }, _("Rotate tile")); break;
						case 3: TransformTile(*s, *state, [](std::int32_t x, std::int32_t y) { return y * TileSize + (x + 1) % TileSize; }, _("Shift tile")); break;
						case 4: TransformTile(*s, *state, [](std::int32_t x, std::int32_t y) { return y * TileSize + (x + TileSize - 1) % TileSize; }, _("Shift tile")); break;
						case 5: TransformTile(*s, *state, [](std::int32_t x, std::int32_t y) { return ((y + 1) % TileSize) * TileSize + x; }, _("Shift tile")); break;
						default: TransformTile(*s, *state, [](std::int32_t x, std::int32_t y) { return ((y + TileSize - 1) % TileSize) * TileSize + x; }, _("Shift tile")); break;
					}
				});
				button->Style = UiButtonStyle::Flat;
				button->Tooltip = _(option.Tooltip);
			}
			row->Add<UiSeparator>(UiOrientation::Vertical);
			auto* neighbors = row->Add<UiButton>(StringView(), EditorIcon::Grid, [state]() { state->ShowNeighbors = !state->ShowNeighbors; });
			neighbors->Style = UiButtonStyle::Flat;
			neighbors->Tooltip = _("Show the edges of the tiles around it in its tile set");
			neighbors->IsCheckedFunc = [state]() { return state->ShowNeighbors; };

			// Painted right in the level, wherever the tile is seen
			auto* inLevel = row->Add<UiButton>(_("Paint in Level"), EditorIcon::Brush, [s, state, canvasHolder]() {
				auto* tool = static_cast<TilePixelTool*>(s->GetTools().Get(EditorToolType::TilePixels));
				if (s->GetTools().GetChosenType() == EditorToolType::TilePixels && tool->GetTargetTile() == state->TileId) {
					tool->SetTarget(-1, nullptr);
					s->GetTools().SetActive(EditorToolType::Brush);
					return;
				}
				tool->SetTarget(state->TileId, [canvasHolder](Vector2i pixel, MouseButton button, bool continued) {
					if (*canvasHolder != nullptr) {
						(*canvasHolder)->PaintPixel(pixel, button, continued);
					}
				});
				s->GetTools().SetActive(EditorToolType::TilePixels);
			});
			inLevel->Style = UiButtonStyle::Flat;
			inLevel->Tooltip = _("Paint the tile right in the level with the tool chosen here, all the places it's seen change");
			inLevel->IsCheckedFunc = [s, state]() {
				auto* tool = static_cast<TilePixelTool*>(s->GetTools().Get(EditorToolType::TilePixels));
				return (s->GetTools().GetChosenType() == EditorToolType::TilePixels && tool->GetTargetTile() == state->TileId);
			};
		}

		auto* columns = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing * 2.0f);
		*canvasHolder = columns->Add<TilePixelCanvas>(session, state);
		{
			auto* right = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			bool trueColor = (state->LevelTile && state->TrueColor);
			auto* swatch = right->Add<UiColorSwatch>([state, trueColor]() {
				if (trueColor) {
					return RgbaToColor(state->Rgba);
				}
				if (state->Color == 0) {
					return Colorf(0.0f, 0.0f, 0.0f, 0.0f);
				}
				Colorf color = UiPaletteGrid::ToColor(ContentResolver::Get().GetPalettes()[state->Color]);
				color.A = 1.0f;
				return color;
			});
			swatch->Width = 6.0f * 16.0f;
			swatch->Height = 16.0f;
			swatch->IsVisibleFunc = [state]() { return (state->Mode == EditMode::Pixels); };
			if (trueColor) {
				// Any color and how see-through it is
				UiColorSwatch* swatchPtr = swatch;
				swatch->OnClick = [s, state, swatchPtr]() {
					UiColorPicker::ShowPopup(*s->GetSurface(), Vector2f(swatchPtr->Bounds.X, swatchPtr->Bounds.Y + swatchPtr->Bounds.H), [state]() {
						return RgbaToColor(state->Rgba);
					}, [state](const Colorf& color) {
						state->Rgba = ColorToRgba(color);
					}, true);
				};
				swatch->Tooltip = _("Color to paint with, click to choose any");
			}
			auto* grid = right->Add<UiPaletteGrid>([]() {
				return ContentResolver::Get().GetPalettes().data();
			}, [state, trueColor]() {
				return (trueColor ? -1 : std::int32_t(state->Color));
			}, [state, trueColor](std::int32_t index, bool) {
				if (trueColor) {
					state->Rgba = (index == 0 ? 0u : (ContentResolver::Get().GetPalettes()[index] | 0xFF000000u));
				} else {
					state->Color = std::uint8_t(index);
				}
			});
			grid->CellSize = 6.0f;
			grid->IsVisibleFunc = [state]() { return (state->Mode == EditMode::Pixels); };

			// What the mask can be made from
			auto* maskTools = right->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			maskTools->IsVisibleFunc = [state]() { return (state->Mode == EditMode::Mask); };
			{
				auto* fromPixels = maskTools->Add<UiButton>(_("Mask from Pixels"), EditorIcon::Mask, [s, state]() {
					// Solid where it isn't transparent, a true color pixel at least half opaque
					std::uint8_t solid[PixelCount];
					if (state->LevelTile && state->TrueColor) {
						const std::uint8_t* rgba = GetRgba(*s, *state);
						for (std::int32_t i = 0; i < PixelCount; i++) {
							solid[i] = (rgba != nullptr && rgba[i * 4 + 3] >= 128 ? 1 : 0);
						}
					} else {
						const std::uint8_t* indices = GetIndices(*s, *state);
						for (std::int32_t i = 0; i < PixelCount; i++) {
							solid[i] = (indices[i] != 0 ? 1 : 0);
						}
					}
					ChangeTile(*s, *state, true, [&solid](std::uint8_t* mask) {
						std::memcpy(mask, solid, PixelCount);
					}, false, _("Mask from pixels"));
				});
				fromPixels->Tooltip = _("Make every pixel that isn't transparent solid, and the others not");

				auto* fillBelow = maskTools->Add<UiButton>(_("Fill Below Surface"), EditorIcon::Fill, [s, state]() {
					ChangeTile(*s, *state, true, [](std::uint8_t* mask) {
						for (std::int32_t x = 0; x < TileSize; x++) {
							std::int32_t top = 0;
							while (top < TileSize && mask[top * TileSize + x] == 0) {
								top++;
							}
							for (std::int32_t y = top; y < TileSize; y++) {
								mask[y * TileSize + x] = 1;
							}
						}
					}, false, _("Fill mask below surface"));
				});
				fillBelow->Tooltip = _("Draw the surface with the pencil, this makes everything below it in each column solid");

				auto* fromBrush = maskTools->Add<UiButton>(_("Mask of Brush Tile"), EditorIcon::Copy, [s, state]() {
					const TileStamp& stamp = s->GetStamp();
					for (std::size_t i = 0; i < stamp.Tiles.size(); i++) {
						if (!stamp.Present[i] || stamp.Tiles[i].IsEmpty()) {
							continue;
						}
						std::uint8_t mask[PixelCount];
						if (!ReadTileMask(*s, stamp.Tiles[i], mask)) {
							break;
						}
						ChangeTile(*s, *state, true, [&mask](std::uint8_t* data) {
							std::memcpy(data, mask, PixelCount);
						}, false, _("Mask of another tile"));
						return;
					}
					s->ShowMessage(_("Choose a tile with the mask as the brush first"));
				});
				fromBrush->Tooltip = _("Take the mask of the first tile of the brush");

				// A straight surface from one height at the left edge to another at the right edge
				auto* slopeRow = maskTools->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
				auto* left = slopeRow->Add<UiNumberBox>([state]() { return state->SlopeLeft; }, [state](float value, bool) { state->SlopeLeft = value; }, 0.0f, 32.0f);
				left->Width = 34.0f;
				left->Tooltip = _("Height of the surface at the left edge in pixels");
				auto* right2 = slopeRow->Add<UiNumberBox>([state]() { return state->SlopeRight; }, [state](float value, bool) { state->SlopeRight = value; }, 0.0f, 32.0f);
				right2->Width = 34.0f;
				right2->Tooltip = _("Height of the surface at the right edge in pixels");
				auto* slope = slopeRow->Add<UiButton>(_("Slope"), EditorIcon::Slope, [s, state]() {
					float leftHeight = state->SlopeLeft, rightHeight = state->SlopeRight;
					ChangeTile(*s, *state, true, [leftHeight, rightHeight](std::uint8_t* mask) {
						for (std::int32_t x = 0; x < TileSize; x++) {
							// At the middle of the column
							float t = (float(x) + 0.5f) / float(TileSize);
							std::int32_t height = std::int32_t(std::round(leftHeight + (rightHeight - leftHeight) * t));
							for (std::int32_t y = 0; y < TileSize; y++) {
								mask[y * TileSize + x] = (y >= TileSize - height ? 1 : 0);
							}
						}
					}, false, _("Slope mask"));
				});
				slope->Tooltip = _("Make the mask a slope between the two heights");

				auto* clearRow = maskTools->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
				clearRow->Add<UiButton>(_("Empty"), EditorIcon::Delete, [s, state]() {
					ChangeTile(*s, *state, true, [](std::uint8_t* mask) { std::memset(mask, 0, PixelCount); }, false, _("Empty mask"));
				})->Style = UiButtonStyle::Flat;
				clearRow->Add<UiButton>(_("Solid"), EditorIcon::Rectangle, [s, state]() {
					ChangeTile(*s, *state, true, [](std::uint8_t* mask) { std::memset(mask, 1, PixelCount); }, false, _("Solid mask"));
				})->Style = UiButtonStyle::Flat;
			}

			if (state->LevelTile) {
				right->Add<UiLabel>(_("Changes are saved with the level"), UiTheme::TextDim);
			} else {
				auto* revert = right->Add<UiButton>(_("As in Tile Set"), EditorIcon::Undo, [s, state]() {
					LevelDocument& document = s->GetDocument();
					if (SetTileOverrideCommand::FindDiffuse(document, state->TileId) == nullptr && SetTileOverrideCommand::FindMask(document, state->TileId) == nullptr) {
						return;
					}
					s->Execute(std::make_unique<SetTileOverrideCommand>(document, state->TileId, nullptr, nullptr, _("Tile as in tile set")));
				});
				revert->Tooltip = _("The level doesn't change the tile anymore");
				auto* apply = right->Add<UiButton>(_("Save into Tile Set..."), EditorIcon::Save, [s, state]() {
					ShowApplyTileChangesDialog(*s, s->GetDocument().FromTileId(state->TileId, 0).Source);
				});
				apply->Tooltip = _("Make the tiles changed in this level tiles of a tile set");
				apply->IsEnabledFunc = [s, state]() {
					return (CountChangedTiles(*s, s->GetDocument().FromTileId(state->TileId, 0).Source) > 0);
				};
				// True color, and anything else only this level has, in a copy of the tile the level shows instead
				auto* copy = right->Add<UiButton>(_("Copy into This Level"), EditorIcon::Copy, [s, state, dialogPtr]() {
					TileRef original = s->GetDocument().FromTileId(state->TileId, 0);
					TileRef copied;
					if (CopyTileIntoLevel(*s, original, copied)) {
						dialogPtr->Close();
						ShowTileEditor(*s, copied);
						s->ShowMessage(_("The level shows a copy of the tile everywhere now, only this level has it"));
					}
				});
				copy->Tooltip = _("The tiles of this level can be changed in true color, the copy is shown wherever the tile was");
			}

			// To change the tile in another program and bring it back
			auto* importImage = right->Add<UiButton>(_("Import PNG..."), EditorIcon::Folder, [s, state]() {
				SmallVector<UiFileRoot, 4> roots;
				s->GetImageRoots(false, roots);
				UiFileDialog::Show(*s->GetSurface(), _("Import Tile"), UiFileDialogMode::Open, std::move(roots), ".png"_s, {}, {}, [s, state](StringView path) {
					auto file = fs::Open(path, FileAccess::Read);
					PngImage image;
					String error;
					if (!file->IsValid() || !PngCodec::Decode(*file, image, &error)) {
						s->ShowMessage(_f("The image can't be read: {}", error));
						return;
					}
					bool reduced = false;
					if (state->LevelTile && state->TrueColor) {
						// The colors as they are
						ChangeRgba(*s, *state, [&image](std::uint32_t* colors) {
							for (std::int32_t y = 0; y < TileSize; y++) {
								for (std::int32_t x = 0; x < TileSize; x++) {
									colors[y * TileSize + x] = (x < image.Width && y < image.Height ? image.GetColor(x, y) : 0u);
								}
							}
						}, false, _("Import tile"));
					} else {
						const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
						ChangeTile(*s, *state, false, [&image, palette, &reduced](std::uint8_t* indices) {
							reduced = ReadTileFromImage(image, 0, 0, palette, indices);
						}, false, _("Import tile"));
					}
					if (image.Width > TileSize || image.Height > TileSize) {
						s->ShowMessage(_("Only the top left 32 × 32 pixels of the image were taken"));
					} else if (reduced) {
						s->ShowMessage(_("Some colors aren't in the palette, the closest ones were taken"));
					}
				});
			});
			importImage->Tooltip = _("Take the pixels of the tile from an image");
			auto* exportImage = right->Add<UiButton>(_("Export PNG..."), EditorIcon::Export, [s, state]() {
				SmallVector<UiFileRoot, 4> roots;
				s->GetImageRoots(true, roots);
				String name = _f("tile{}.png", state->TileId);
				UiFileDialog::Show(*s->GetSurface(), _("Export Tile"), UiFileDialogMode::Save, std::move(roots), ".png"_s, {}, name, [s, state](StringView path) {
					PngImage image;
					if (state->LevelTile && state->TrueColor) {
						image.Create(TileSize, TileSize, false);
						if (const std::uint8_t* rgba = GetRgba(*s, *state)) {
							std::memcpy(image.Pixels.data(), rgba, PixelCount * 4);
						}
					} else {
						image = MakeTileImage(GetIndices(*s, *state), ContentResolver::Get().GetPalettes().data());
					}
					auto file = fs::Open(path, FileAccess::Write);
					if (!file->IsValid() || !PngCodec::Encode(*file, image)) {
						s->ShowMessage(_("The image couldn't be saved"));
					}
				});
			});
			exportImage->Tooltip = _("Save the tile as an image");
		}

		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		// The level is seen behind it, with the tile changing as it's painted
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		options.OnClosed = [s, state, canvasHolder]() {
			// Painting in the level ends with the window
			*canvasHolder = nullptr;
			auto* tool = static_cast<TilePixelTool*>(s->GetTools().Get(EditorToolType::TilePixels));
			if (tool->GetTargetTile() == state->TileId) {
				tool->SetTarget(-1, nullptr);
				if (s->GetTools().GetChosenType() == EditorToolType::TilePixels) {
					s->GetTools().SetActive(EditorToolType::Brush);
				}
			}
		};
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
