#include "TileEditor.h"

#if defined(WITH_EDITOR)

#include "ShellCommon.h"
#include "TextPrompt.h"
#include "TileSetApplyDialog.h"
#include "TileSetsDialog.h"
#include "../Tools/EditorContext.h"
#include "../Common/TileSource.h"
#include "../Document/Commands.h"
#include "../Imaging/PaletteMatch.h"
#include "../Imaging/TileImages.h"
#include "../Imaging/TileMasks.h"
#include "../Runtime/TileDrawing.h"
#include "../Tools/TilePixelTool.h"
#include "../UI/UiColors.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Cryptography/xxHash.h>
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
			// The 8-bit pixels of a tile of the level as they're shown, when its tile set is matched to the palette
			std::uint8_t LevelIndices[PixelCount];

			// Where the tiles of the tile sets were when the window was opened, the tile is another one after they move
			std::uint64_t Layout = 0;

			// A stroke on a tile of the level is kept here while it's painted, so the whole tile set isn't written into
			// the level again for every pixel, it's saved into the level as one step when the stroke ends
			bool Pending = false;
			std::uint8_t PendingImage[Formats::TileSetData::ImageBytesPerTile];
			std::uint8_t PendingMask[PixelCount];
			String PendingName;
			// The stroke made an 8-bit tile of the level a true color one
			bool PendingConverted = false;

			// The tile is drawn around itself instead of its neighbors, to see whether it tiles without seams
			bool ShowSeams = false;
			// What is painted is mirrored across the middle of the tile, horizontally (1), vertically (2) or both
			std::uint8_t Mirror = 0;
			// Colors are painted in true color, a tile of a tile set is copied into the level for it on the first stroke
			bool PaintTrueColor = false;
			// The animated tile edited frame by frame, -1 if it's a single tile, and the frame shown
			std::int32_t Animation = -1;
			std::int32_t Frame = 0;
			// Called when the tile becomes another one, a copy of it in the level, so painting in the level follows it
			Function<void()> OnRetarget;
		};

		// Pixels copied from a tile, both as palette indices and in true color, to be pasted into another one
		struct PixelClipboard
		{
			bool Valid = false;
			Recti Area;
			std::uint8_t Indices[PixelCount];
			std::uint32_t Rgba[PixelCount];
			std::uint8_t Mask[PixelCount];
		};

		PixelClipboard& GetPixelClipboard()
		{
			static PixelClipboard clipboard;
			return clipboard;
		}

		// Where the tiles of the tile sets of a level are, any change of it gives other tile IDs to tiles
		std::uint64_t GetTileSetsLayout(const LevelDocument& document)
		{
			std::uint64_t hash = 0;
			auto add = [&hash](const void* data, std::size_t size) {
				hash = Death::Cryptography::xxHash3(data, size, hash);
			};
			auto addName = [&add](StringView name) {
				add(name.data(), name.size());
				add("|", 1);
			};
			addName(document.Properties.DefaultTileSet);
			add(&document.DefaultTileCount, sizeof(document.DefaultTileCount));
			for (const Formats::ExtraTileSetRecord& record : document.ExtraTileSets) {
				addName(record.Name);
				add(&record.Offset, sizeof(record.Offset));
				add(&record.Count, sizeof(record.Count));
				// Matching the colors to the palette doesn't move any tile
				std::uint8_t embedded = std::uint8_t(record.Flags & Formats::ExtraTileSetRecord::Embedded);
				add(&embedded, 1);
			}
			return hash;
		}

		// Whether the tile isn't where it was anymore, as after another tile set was added before it, or after an undo
		bool IsStale(IEditorContext& session, const TileEditState& state)
		{
			return (GetTileSetsLayout(session.GetDocument()) != state.Layout);
		}

		// The colors the tile set of a tile of the level is matched to, nullptr if it isn't
		const std::uint8_t* GetLevelRemapping(IEditorContext& session, const TileEditState& state)
		{
			const LevelDocument& document = session.GetDocument();
			if (!state.LevelTile || state.TrueColor || state.Slot < 0 || state.Slot >= std::int32_t(document.ExtraTileSets.size())) {
				return nullptr;
			}
			const Formats::ExtraTileSetRecord& record = document.ExtraTileSets[state.Slot];
			return ((record.Flags & (Formats::ExtraTileSetRecord::Remapped | Formats::ExtraTileSetRecord::TrueColor)) == Formats::ExtraTileSetRecord::Remapped
				? record.Remapping : nullptr);
		}

		// Reads the tile as its tile set has it, as palette indices of the level and a byte per pixel of the mask
		bool LoadOriginalTile(IEditorContext& session, const TileRef& tile, TileEditState& state)
		{
			TileSource source;
			if (!ResolveTileSource(session.GetDocument(), tile.Source, source)) {
				return false;
			}
			std::int32_t index = source.Offset + tile.Index;
			const std::uint8_t* remapping = source.Remapping;
			if (source.IsEmbedded()) {
				state.LevelTile = true;
				state.Slot = tile.Source - 1;
				state.Index = index;
				state.SetName = source.Name;
			}

			const Formats::TileSetData* data = session.GetTileSetData(source.Name);
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
				PaletteSearch search(ContentResolver::Get().GetPalettes().data());
				for (std::int32_t i = 0; i < PixelCount; i++) {
					const std::uint8_t* pixel = &image[i * 4];
					if (pixel[3] < 128) {
						state.OriginalIndices[i] = 0;
						continue;
					}
					OkLab color = OkLab::FromPaletteEntry(std::uint32_t(pixel[0]) | (std::uint32_t(pixel[1]) << 8) | (std::uint32_t(pixel[2]) << 16));
					state.OriginalIndices[i] = search.FindNearest(color);
				}
				state.Reduced = true;
			}

			TileMasks::Unpack(&data->Masks[std::size_t(index) * Formats::TileSetData::MaskBytesPerTile], state.OriginalMask);
			return true;
		}

		// The tile set of the level the tile is in, as it is now
		const Formats::TileSetData* GetLevelData(IEditorContext& session, const TileEditState& state)
		{
			const Formats::TileSetData* data = session.GetTileSetData(state.SetName);
			return (data != nullptr && state.Index < data->TileCount ? data : nullptr);
		}

		// Whether a tile of the level is a true color one now, an undo can change it back to an 8-bit one
		void RefreshTrueColor(IEditorContext& session, TileEditState& state)
		{
			if (!state.LevelTile || state.Pending) {
				return;
			}
			if (const Formats::TileSetData* data = GetLevelData(session, state)) {
				state.TrueColor = (data->TrueColor[state.Index] != 0);
			}
		}

		// The colors the tile set of a tile of the level is matched to whatever the tile is, nullptr if it isn't
		const std::uint8_t* GetRecordRemapping(IEditorContext& session, const TileEditState& state)
		{
			const LevelDocument& document = session.GetDocument();
			if (!state.LevelTile || state.Slot < 0 || state.Slot >= std::int32_t(document.ExtraTileSets.size())) {
				return nullptr;
			}
			const Formats::ExtraTileSetRecord& record = document.ExtraTileSets[state.Slot];
			return ((record.Flags & (Formats::ExtraTileSetRecord::Remapped | Formats::ExtraTileSetRecord::TrueColor)) == Formats::ExtraTileSetRecord::Remapped
				? record.Remapping : nullptr);
		}

		// The pixels and the mask as the level has them now, overridden or not
		const std::uint8_t* GetIndices(IEditorContext& session, TileEditState& state)
		{
			if (state.LevelTile) {
				if (state.Pending) {
					return state.PendingImage;
				}
				const Formats::TileSetData* data = GetLevelData(session, state);
				if (data == nullptr) {
					return state.OriginalIndices;
				}
				const std::uint8_t* image = &data->Images[std::size_t(state.Index) * Formats::TileSetData::ImageBytesPerTile];
				if (const std::uint8_t* remapping = GetLevelRemapping(session, state)) {
					// As the game shows them, which is what is painted on
					for (std::int32_t i = 0; i < PixelCount; i++) {
						state.LevelIndices[i] = (image[i] == 0 ? 0 : remapping[image[i]]);
					}
					return state.LevelIndices;
				}
				return image;
			}
			const Formats::TileDiffuseOverride* tile = SetTileOverrideCommand::FindDiffuse(session.GetDocument(), state.TileId);
			return (tile != nullptr ? tile->Indices : state.OriginalIndices);
		}

		// The colors of a true color tile of the level, 4 bytes RGBA a pixel
		const std::uint8_t* GetRgba(IEditorContext& session, const TileEditState& state)
		{
			if (state.Pending) {
				return state.PendingImage;
			}
			const Formats::TileSetData* data = GetLevelData(session, state);
			return (data != nullptr ? &data->Images[std::size_t(state.Index) * Formats::TileSetData::ImageBytesPerTile] : nullptr);
		}

		const std::uint8_t* GetMask(IEditorContext& session, TileEditState& state)
		{
			if (state.LevelTile) {
				if (state.Pending) {
					return state.PendingMask;
				}
				const Formats::TileSetData* data = GetLevelData(session, state);
				if (data == nullptr) {
					return state.OriginalMask;
				}
				TileMasks::Unpack(&data->Masks[std::size_t(state.Index) * Formats::TileSetData::MaskBytesPerTile], state.LevelMask);
				return state.LevelMask;
			}
			const Formats::TileMaskOverride* tile = SetTileOverrideCommand::FindMask(session.GetDocument(), state.TileId);
			return (tile != nullptr ? tile->Mask : state.OriginalMask);
		}

		std::uint32_t ReadRgba(const std::uint8_t* rgba, std::int32_t i)
		{
			return std::uint32_t(rgba[i * 4]) | (std::uint32_t(rgba[i * 4 + 1]) << 8) | (std::uint32_t(rgba[i * 4 + 2]) << 16) | (std::uint32_t(rgba[i * 4 + 3]) << 24);
		}

		// Shows the pixels and the mask of a tile of the level in the level, before they're saved into it
		void PreviewLevelTile(IEditorContext& session, const TileEditState& state, const std::uint8_t* image, const std::uint8_t* mask)
		{
			Tiles::TileMap* tileMap = session.GetTileMap();
			if (tileMap == nullptr) {
				return;
			}
			if (state.TrueColor) {
				ContentResolver::Get().ApplyTileDiffuseOverrideRgba(*tileMap, state.TileId, image);
			} else {
				ContentResolver::Get().ApplyTileDiffuseOverride(*tileMap, state.TileId, image);
			}
			std::uint8_t maskValues[PixelCount];
			std::memcpy(maskValues, mask, sizeof(maskValues));
			tileMap->OverrideTileMask(state.TileId, maskValues);
		}

		// Starts keeping a stroke on a tile of the level, from the tile as the level has it now
		bool BeginPending(IEditorContext& session, TileEditState& state)
		{
			if (state.Pending) {
				return true;
			}
			const Formats::TileSetData* data = GetLevelData(session, state);
			if (data == nullptr) {
				return false;
			}
			std::memcpy(state.PendingImage, &data->Images[std::size_t(state.Index) * Formats::TileSetData::ImageBytesPerTile], Formats::TileSetData::ImageBytesPerTile);
			if (const std::uint8_t* remapping = GetLevelRemapping(session, state)) {
				for (std::int32_t i = 0; i < PixelCount; i++) {
					state.PendingImage[i] = (state.PendingImage[i] == 0 ? 0 : remapping[state.PendingImage[i]]);
				}
			}
			TileMasks::Unpack(&data->Masks[std::size_t(state.Index) * Formats::TileSetData::MaskBytesPerTile], state.PendingMask);
			state.Pending = true;
			return true;
		}

		// Saves the stroke kept on a tile of the level into the level as one step
		void CommitPending(IEditorContext& session, TileEditState& state)
		{
			if (!state.Pending) {
				return;
			}
			state.Pending = false;
			const Formats::TileSetData* data = GetLevelData(session, state);
			if (data == nullptr || IsStale(session, state)) {
				// The tiles moved meanwhile, the level is loaded again anyway, it's never changed at another tile
				state.PendingConverted = false;
				return;
			}
			Formats::TileSetData copy = *data;
			std::int32_t flags = -1;
			std::uint8_t recordFlags = session.GetDocument().ExtraTileSets[state.Slot].Flags;
			if (const std::uint8_t* remapping = GetRecordRemapping(session, state)) {
				// The pixels were painted as they're shown, the tiles of the level are in the palette of the level from
				// now on, so they don't have to be matched to it anymore
				BakeRemapping(copy, remapping);
				recordFlags &= ~Formats::ExtraTileSetRecord::Remapped;
				flags = recordFlags;
			}
			std::uint8_t* image = &copy.Images[std::size_t(state.Index) * Formats::TileSetData::ImageBytesPerTile];
			std::uint8_t* maskBits = &copy.Masks[std::size_t(state.Index) * Formats::TileSetData::MaskBytesPerTile];
			std::memcpy(image, state.PendingImage, Formats::TileSetData::ImageBytesPerTile);
			TileMasks::Pack(state.PendingMask, maskBits);
			if (state.PendingConverted) {
				// Painted in true color, the tile is one now, and so the tile set has a true color tile
				copy.TrueColor[state.Index] = 1;
				recordFlags |= Formats::ExtraTileSetRecord::TrueColor;
				flags = recordFlags;
				state.PendingConverted = false;
			}
			if (flags < 0 && std::memcmp(image, &data->Images[std::size_t(state.Index) * Formats::TileSetData::ImageBytesPerTile], Formats::TileSetData::ImageBytesPerTile) == 0 &&
				std::memcmp(maskBits, &data->Masks[std::size_t(state.Index) * Formats::TileSetData::MaskBytesPerTile], Formats::TileSetData::MaskBytesPerTile) == 0) {
				return;
			}
			SmallVector<std::uint8_t, 0> bytes;
			if (!SerializeTileSet(copy, bytes)) {
				session.ShowMessage(_("The tile set couldn't be saved"));
				// The level shows the tile as it has it again, in the colors it's matched to as the session shows it
				std::uint8_t mask[PixelCount];
				TileMasks::Unpack(&data->Masks[std::size_t(state.Index) * Formats::TileSetData::MaskBytesPerTile], mask);
				state.Pending = false;
				state.PendingConverted = false;
				RefreshTrueColor(session, state);
				PreviewLevelTile(session, state, state.TrueColor ? GetRgba(session, state) : GetIndices(session, state), mask);
				return;
			}
			session.Execute(std::make_unique<SetLevelTilesCommand>(session.GetDocument(), state.Slot, Death::move(bytes), state.PendingName, flags));
		}

		/**
		 * Changes a tile of the level. A @p stroke is kept while it's painted and saved as one step when it ends (see
		 * CommitPending()), anything else is saved as a step right away.
		 */
		void ChangeLevelTile(IEditorContext& session, TileEditState& state, Function<void(std::uint8_t* image, std::uint8_t* maskBits)>&& change,
			bool stroke, StringView name)
		{
			if (IsStale(session, state)) {
				return;
			}
			if (state.Pending && (!stroke || state.PendingName != name)) {
				CommitPending(session, state);
			}
			RefreshTrueColor(session, state);
			if (!BeginPending(session, state)) {
				return;
			}
			std::uint8_t maskBits[Formats::TileSetData::MaskBytesPerTile];
			TileMasks::Pack(state.PendingMask, maskBits);
			change(state.PendingImage, maskBits);
			TileMasks::Unpack(maskBits, state.PendingMask);
			state.PendingName = name;
			PreviewLevelTile(session, state, state.PendingImage, state.PendingMask);
			if (!stroke) {
				CommitPending(session, state);
			}
		}

		// Changes the pixels or the mask, the other one stays overridden or not as it was
		void ChangeTile(IEditorContext& session, TileEditState& state, bool mask, Function<void(std::uint8_t*)>&& change, bool allowMerge, StringView name,
			bool stroke = false)
		{
			if (IsStale(session, state)) {
				return;
			}
			if (state.LevelTile) {
				// The mask a byte a pixel while it's changed, the pixels of an 8-bit tile are the palette indices
				std::uint8_t values[PixelCount];
				std::memcpy(values, mask ? GetMask(session, state) : GetIndices(session, state), sizeof(values));
				change(values);
				ChangeLevelTile(session, state, [mask, &values](std::uint8_t* image, std::uint8_t* maskBits) {
					if (mask) {
						TileMasks::Pack(values, maskBits);
					} else {
						std::memcpy(image, values, PixelCount);
					}
				}, stroke, name);
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
		void ChangeRgba(IEditorContext& session, TileEditState& state, Function<void(std::uint32_t*)>&& change, bool stroke, StringView name)
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
			}, stroke, name);
		}

		/**
		 * Makes the tile one that can be painted in true color: a tile of a tile set is copied into the level first, and
		 * an 8-bit tile of the level becomes a true color one with the stroke @p name. Returns `false` if it can't be.
		 */
		bool EnsureTrueColorTile(IEditorContext& session, TileEditState& state, StringView name)
		{
			if (IsStale(session, state)) {
				return false;
			}
			if (!state.LevelTile) {
				// By where it comes from, the ID of a tile the level can't address yet would be read as an animated tile
				TileRef original = state.Ref;
				TileRef copied;
				if (!CopyTileIntoLevel(session, original, copied)) {
					return false;
				}
				// The window goes on with the copy, which the level shows wherever it showed the tile
				std::int32_t tileId = session.GetDocument().ToTileId(copied);
				if (tileId <= 0 || tileId > UINT16_MAX || !LoadOriginalTile(session, copied, state)) {
					return false;
				}
				state.TileId = std::uint16_t(tileId);
				state.Ref = TileRef{ copied.Index, copied.Source, 0 };
				state.Layout = GetTileSetsLayout(session.GetDocument());
				state.Reduced = false;
				if (state.OnRetarget) {
					state.OnRetarget();
				}
				session.ShowMessage(_("The tile was copied into this level to be painted in true color, the level shows the copy everywhere"));
			}
			RefreshTrueColor(session, state);
			if (state.TrueColor) {
				return true;
			}
			if (state.Pending && state.PendingName != name) {
				CommitPending(session, state);
			}
			if (!BeginPending(session, state)) {
				return false;
			}
			// The pixels keep the colors they're shown in, transparent ones stay transparent
			const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
			std::uint8_t rgba[Formats::TileSetData::ImageBytesPerTile];
			for (std::int32_t i = 0; i < PixelCount; i++) {
				std::uint8_t index = state.PendingImage[i];
				std::uint32_t color = (index == 0 ? 0u : (palette[index] | 0xFF000000u));
				rgba[i * 4] = std::uint8_t(color);
				rgba[i * 4 + 1] = std::uint8_t(color >> 8);
				rgba[i * 4 + 2] = std::uint8_t(color >> 16);
				rgba[i * 4 + 3] = std::uint8_t(color >> 24);
			}
			std::memcpy(state.PendingImage, rgba, sizeof(rgba));
			state.PendingName = name;
			state.TrueColor = true;
			state.PendingConverted = true;
			return true;
		}

		// Mirrors pixels of a tile in place, horizontally, vertically or both
		template<typename T>
		void MirrorPixels(T* data, bool horizontal, bool vertical)
		{
			T copy[PixelCount];
			std::memcpy(copy, data, sizeof(copy));
			for (std::int32_t y = 0; y < TileSize; y++) {
				for (std::int32_t x = 0; x < TileSize; x++) {
					std::int32_t sx = (horizontal ? TileSize - 1 - x : x);
					std::int32_t sy = (vertical ? TileSize - 1 - y : y);
					data[y * TileSize + x] = copy[sy * TileSize + sx];
				}
			}
		}

		// Moves pixels and the mask together, so the collision stays where the picture is. @p source gives the pixel
		// each pixel takes, or -1 for an empty one.
		void TransformTile(IEditorContext& session, TileEditState& state, Function<std::int32_t(std::int32_t, std::int32_t)>&& source, StringView name)
		{
			if (IsStale(session, state)) {
				return;
			}
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
					TileMasks::Pack(newMask, maskBits);
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

			TilePixelCanvas(IEditorContext& session, std::shared_ptr<TileEditState> state, Function<void()>&& onStale)
				: _session(session), _state(Death::move(state)), _lastPixel(-1, -1), _startPixel(-1, -1), _button(MouseButton::Left),
					_dragging(false), _moving(false), _shape(false), _onStale(Death::move(onStale))
			{
			}

			void Update(float timeMult) override
			{
				UiElement::Update(timeMult);
				// An undo can make a tile of the level 8-bit again
				RefreshTrueColor(_session, *_state);
				// A stroke painted right in the level ends with the tool, it doesn't tell this window
				auto* tool = static_cast<TilePixelTool*>(_session.GetTools().Get(EditorToolType::TilePixels));
				bool toolBusy = (tool != nullptr && tool->IsBusy());
				if (_state->Pending && !_dragging && !toolBusy) {
					CommitPending(_session, *_state);
				}
				if (_toolWasBusy && !toolBusy) {
					_session.GetHistory().BreakMerge();
				}
				_toolWasBusy = toolBusy;
				if (!_staleHandled && IsStale(_session, *_state)) {
					// Its tile ID is another tile's now, it would be changed instead
					_staleHandled = true;
					_state->Pending = false;
					if (_onStale) {
						_onStale();
					}
				}
			}

			/** Pixels of a stroke, a line from one to another, given by the tile pixel tool in the level too */
			void PaintPixel(Vector2i from, Vector2i to, MouseButton button, bool continued)
			{
				if (!continued) {
					// A stroke in the level is a step of its own
					_session.GetHistory().BreakMerge();
				}
				_button = button;
				Paint(from, to, continued);
			}

			void ClearSelection()
			{
				_selection = Recti();
			}

			/** Returns the selected pixels, empty if none are */
			Recti GetSelection() const
			{
				return _selection;
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

				if (_state->ShowNeighbors || _state->ShowSeams) {
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

				// The middle of the tile that painting is mirrored across
				if (_state->Mirror != 0) {
					Colorf axis = UiTheme::Accent;
					axis.A = 0.6f;
					float half = cell * TileSize * 0.5f;
					if ((_state->Mirror & 1) != 0) {
						painter.FillRect(Rectf(origin.X + half - thickness * 0.5f, origin.Y, thickness, cell * TileSize), axis, UiLayer::Overlay);
					}
					if ((_state->Mirror & 2) != 0) {
						painter.FillRect(Rectf(origin.X, origin.Y + half - thickness * 0.5f, cell * TileSize, thickness), axis, UiLayer::Overlay);
					}
				}

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
				// Each stroke is a step of its own, not merged into the one before
				_session.GetHistory().BreakMerge();
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
						}, false, true);
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
								// The color it's shown in, for painting in true color
								_state->Rgba = (_state->Color == 0 ? 0u : (ContentResolver::Get().GetPalettes()[_state->Color] | 0xFF000000u));
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
						}, false, true);
					} else {
						bool filled = IsFilled();
						Modify(false, _state->Mode == EditMode::Mask ? _("Rectangle in tile mask") : _("Rectangle in tile"), [from, to, filled](auto* data, auto value) {
							PaintRectangle(data, from, to, value, filled);
						}, false, true);
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
				// A stroke of the pencil on a tile of the level is saved into it now, as one step
				CommitPending(_session, *_state);
				_session.GetHistory().BreakMerge();
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
			IEditorContext& _session;
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
			Function<void()> _onStale;
			bool _staleHandled = false;
			bool _toolWasBusy = false;

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
					return Vector2i(std::clamp<std::int32_t>(x, 0, TileSize - 1), std::clamp<std::int32_t>(y, 0, TileSize - 1));
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
				if (_state->ShowSeams) {
					// The tile itself all around, as the level shows it with the changes being painted, so the edges show
					// whether it tiles without a seam
					for (std::int32_t dy = -1; dy <= 1; dy++) {
						for (std::int32_t dx = -1; dx <= 1; dx++) {
							if (dx != 0 || dy != 0) {
								DrawTile(painter, *tileMap, document, ref, 0, Rectf(origin.X + dx * size, origin.Y + dy * size, size, size), 0.85f, UiLayer::Fill);
							}
						}
					}
					painter.PopClip();
					return;
				}
				for (std::int32_t dy = -1; dy <= 1; dy++) {
					for (std::int32_t dx = -1; dx <= 1; dx++) {
						if ((dx == 0 && dy == 0) || column + dx < 0 || column + dx >= TileSetColumns) {
							continue;
						}
						std::int32_t index = std::int32_t(ref.Index) + dy * TileSetColumns + dx;
						if (index <= 0 || (tileCount >= 0 && index >= tileCount)) {
							continue;
						}
						// Over the background of the window, under the pixels of the tile drawn after it
						DrawTile(painter, *tileMap, document, TileRef{ std::uint16_t(index), ref.Source, 0 }, 0,
							Rectf(origin.X + dx * size, origin.Y + dy * size, size, size), 0.5f, UiLayer::Fill);
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
			// With @p symmetric, what is painted is mirrored as chosen too
			template<class Operation>
			void Modify(bool continued, StringView name, Operation operation, bool stroke = false, bool symmetric = false)
			{
				bool mask = (_state->Mode == EditMode::Mask);
				std::uint8_t mirror = (symmetric ? _state->Mirror : std::uint8_t(0));
				// The operation, then again on the pixels mirrored, which paints it at the mirrored places
				auto apply = [&operation, mirror](auto* data, auto value) {
					operation(data, value);
					for (std::int32_t m = 1; m <= 3; m++) {
						if ((m & mirror) == m) {
							MirrorPixels(data, (m & 1) != 0, (m & 2) != 0);
							operation(data, value);
							MirrorPixels(data, (m & 1) != 0, (m & 2) != 0);
						}
					}
				};
				if (!mask && (IsTrueColor() || _state->PaintTrueColor)) {
					// A tile of a tile set is copied into the level for it, an 8-bit tile of the level becomes a true color one
					if (!IsTrueColor() && !EnsureTrueColorTile(_session, *_state, name)) {
						return;
					}
					std::uint32_t value = GetRgbaValue();
					ChangeRgba(_session, *_state, [&apply, value](std::uint32_t* data) { apply(data, value); }, stroke, name);
				} else {
					std::uint8_t value = GetValue();
					ChangeTile(_session, *_state, mask, [&apply, value](std::uint8_t* data) { apply(data, value); }, continued, name, stroke);
				}
			}

			// A line, so a fast stroke has no gaps, the whole stroke is a single step
			void Paint(Vector2i from, Vector2i to, bool continued)
			{
				Modify(continued, _state->Mode == EditMode::Mask ? _("Paint tile mask") : _("Paint tile"), [from, to](auto* data, auto value) {
					PaintLine(data, from, to, value);
				}, true, true);
			}
		};

		// The mask of a tile as the level has it now, overridden or not
		bool ReadTileMask(IEditorContext& session, const TileRef& tile, std::uint8_t* mask)
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

	namespace
	{
		void ShowTileEditorFor(IEditorContext& session, const TileRef& tile, std::int32_t animation, std::int32_t frame);

		/** The frames of an animated tile side by side, a click edits another one */
		class FrameStrip : public UiElement
		{
		public:
			static constexpr float CellUnits = 28.0f;

			FrameStrip(IEditorContext& session, std::int32_t animation, std::int32_t frame, Function<void(std::int32_t)>&& onPick)
				: _session(session), _animation(animation), _frame(frame), _onPick(Death::move(onPick))
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float cell = context.Px(CellUnits);
				std::int32_t count = GetFrameCount();
				return Vector2f(std::min(cell * std::max<std::int32_t>(count, 1), available.X), cell);
			}

			void Draw(UiPainter& painter) override
			{
				Tiles::TileMap* tileMap = _session.GetTileMap();
				const LevelDocument& document = _session.GetDocument();
				if (tileMap == nullptr || _animation >= std::int32_t(document.Animations.size())) {
					return;
				}
				const DocumentAnimation& animation = document.Animations[_animation];
				float cell = Bounds.H;
				float thickness = std::max(1.0f, std::round(painter.GetScale()));
				for (std::int32_t i = 0; i < std::int32_t(animation.Frames.size()); i++) {
					Rectf rect(Bounds.X + i * cell, Bounds.Y, cell, cell);
					if (rect.X + rect.W > Bounds.X + Bounds.W + 0.5f) {
						break;
					}
					painter.FillRect(Rectf(rect.X + 1.0f, rect.Y + 1.0f, rect.W - 2.0f, rect.H - 2.0f), Colorf(0.0f, 0.0f, 0.0f, 0.3f), UiLayer::Fill);
					const TileRef& tile = animation.Frames[i];
					if (!tile.IsEmpty()) {
						DrawTile(painter, *tileMap, document, tile, tile.Flags, Rectf(rect.X + 2.0f, rect.Y + 2.0f, rect.W - 4.0f, rect.H - 4.0f),
							CanEdit(tile) ? 1.0f : 0.4f, UiLayer::Content);
					}
					if (i == _frame) {
						painter.Frame(rect, UiTheme::Accent, thickness * 2.0f, UiLayer::Overlay);
					}
				}
			}

			bool AcceptsPointer() const override
			{
				return true;
			}

			bool OnPointerDown(const UiPointerEvent& event) override
			{
				const LevelDocument& document = _session.GetDocument();
				if (event.Button != MouseButton::Left || Bounds.H <= 0.0f || _animation >= std::int32_t(document.Animations.size())) {
					return false;
				}
				std::int32_t index = std::int32_t((event.Pos.X - Bounds.X) / Bounds.H);
				const DocumentAnimation& animation = document.Animations[_animation];
				if (index >= 0 && index < std::int32_t(animation.Frames.size()) && index != _frame && CanEdit(animation.Frames[index])) {
					_onPick(index);
				}
				return false;
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				std::int32_t index = (Bounds.H > 0.0f ? std::int32_t((pos.X - Bounds.X) / Bounds.H) : -1);
				_tooltip = _f("Frame {} - a click changes it", index + 1);
				return _tooltip;
			}

		private:
			IEditorContext& _session;
			std::int32_t _animation;
			std::int32_t _frame;
			Function<void(std::int32_t)> _onPick;
			mutable String _tooltip;

			std::int32_t GetFrameCount() const
			{
				const LevelDocument& document = _session.GetDocument();
				return (_animation < std::int32_t(document.Animations.size()) ? std::int32_t(document.Animations[_animation].Frames.size()) : 0);
			}

			static bool CanEdit(const TileRef& tile)
			{
				// A frame that shows another animated tile is changed in that one
				return (!tile.IsEmpty() && !tile.IsAnimated() && tile.Source != TileRef::Raw);
			}
		};
	}

	void ShowTileEditor(IEditorContext& session, const TileRef& tile)
	{
		if (!tile.IsAnimated()) {
			ShowTileEditorFor(session, tile, -1, 0);
			return;
		}
		// Its frames side by side, the first one that can be changed is edited first
		const LevelDocument& document = session.GetDocument();
		if (tile.Index >= document.Animations.size()) {
			return;
		}
		const DocumentAnimation& animation = document.Animations[tile.Index];
		for (std::int32_t i = 0; i < std::int32_t(animation.Frames.size()); i++) {
			const TileRef& frame = animation.Frames[i];
			if (!frame.IsEmpty() && !frame.IsAnimated() && frame.Source != TileRef::Raw) {
				ShowTileEditorFor(session, frame, std::int32_t(tile.Index), i);
				return;
			}
		}
		session.ShowMessage(_("The animated tile has no frame of a tile set to change"));
	}

	namespace
	{
	void ShowTileEditorFor(IEditorContext& session, const TileRef& tile, std::int32_t animation, std::int32_t frame)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
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
		state->Layout = GetTileSetsLayout(session.GetDocument());
		state->Animation = animation;
		state->Frame = frame;

		IEditorContext* s = &session;
		auto dialog = std::make_unique<UiDialog>(animation >= 0
			? String(_f("Frame {} of Animated Tile {}", frame + 1, animation + 1))
			: String(_f("Tile {} in This Level", tileId)));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();

		if (animation >= 0) {
			// The other frames, a click edits another one in this window's place
			content->Add<FrameStrip>(session, animation, frame, [s, animation, dialogPtr](std::int32_t index) {
				const LevelDocument& document = s->GetDocument();
				if (animation >= std::int32_t(document.Animations.size()) || index >= std::int32_t(document.Animations[animation].Frames.size())) {
					return;
				}
				TileRef next = document.Animations[animation].Frames[index];
				dialogPtr->Close();
				ShowTileEditorFor(*s, next, animation, index);
			});
		}

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
			auto* seams = row->Add<UiButton>(_("Seams"), EditorIcon::Tiles, [state]() { state->ShowSeams = !state->ShowSeams; });
			seams->Style = UiButtonStyle::Flat;
			seams->Tooltip = _("Show the tile all around itself, as it is changed, to see whether it tiles without a seam");
			seams->IsCheckedFunc = [state]() { return state->ShowSeams; };

			// What the pencil, line, rectangle and fill paint is mirrored across the middle of the tile
			row->Add<UiSeparator>(UiOrientation::Vertical);
			for (std::uint8_t axis : { std::uint8_t(1), std::uint8_t(2) }) {
				auto* mirror = row->Add<UiButton>(StringView(), axis == 1 ? EditorIcon::FlipX : EditorIcon::FlipY, [state, axis]() {
					state->Mirror ^= axis;
				});
				mirror->Style = UiButtonStyle::Flat;
				mirror->Tooltip = (axis == 1 ? _("Mirror painting from left to right") : _("Mirror painting from top to bottom"));
				mirror->IsCheckedFunc = [state, axis]() { return ((state->Mirror & axis) != 0); };
			}

			// Pixels of one tile into another, the selection or all of them, the mask in the mask mode
			row->Add<UiSeparator>(UiOrientation::Vertical);
			auto* copyPixels = row->Add<UiButton>(StringView(), EditorIcon::Copy, [s, state, canvasHolder]() {
				PixelClipboard& clipboard = GetPixelClipboard();
				Recti area = (*canvasHolder != nullptr ? (*canvasHolder)->GetSelection() : Recti());
				clipboard.Area = (area.W > 0 && area.H > 0 ? area : Recti(0, 0, TileSize, TileSize));
				const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
				std::memcpy(clipboard.Mask, GetMask(*s, *state), PixelCount);
				if (state->LevelTile && state->TrueColor) {
					const std::uint8_t* rgba = GetRgba(*s, *state);
					PaletteSearch search(palette);
					for (std::int32_t i = 0; i < PixelCount; i++) {
						std::uint32_t color = (rgba != nullptr ? ReadRgba(rgba, i) : 0u);
						clipboard.Rgba[i] = color;
						clipboard.Indices[i] = ((color >> 24) < 128 ? 0 : search.FindNearest(OkLab::FromPaletteEntry(color)));
					}
				} else {
					const std::uint8_t* indices = GetIndices(*s, *state);
					for (std::int32_t i = 0; i < PixelCount; i++) {
						clipboard.Indices[i] = indices[i];
						clipboard.Rgba[i] = (indices[i] == 0 ? 0u : (palette[indices[i]] | 0xFF000000u));
					}
				}
				clipboard.Valid = true;
				s->ShowMessage(_("The pixels were copied, paste them into another tile"));
			});
			copyPixels->Style = UiButtonStyle::Flat;
			copyPixels->Tooltip = _("Copy the selected pixels, or all of them, and the mask, to paste them into another tile");
			auto* pastePixels = row->Add<UiButton>(StringView(), EditorIcon::Paste, [s, state]() {
				const PixelClipboard& clipboard = GetPixelClipboard();
				if (!clipboard.Valid) {
					return;
				}
				Recti area = clipboard.Area;
				auto inArea = [area](std::int32_t i) {
					std::int32_t x = i % TileSize, y = i / TileSize;
					return (x >= area.X && x < area.X + area.W && y >= area.Y && y < area.Y + area.H);
				};
				if (state->Mode == EditMode::Mask) {
					ChangeTile(*s, *state, true, [&clipboard, &inArea](std::uint8_t* mask) {
						for (std::int32_t i = 0; i < PixelCount; i++) {
							if (inArea(i)) {
								mask[i] = clipboard.Mask[i];
							}
						}
					}, false, _("Paste mask"));
				} else if (state->LevelTile && state->TrueColor) {
					ChangeRgba(*s, *state, [&clipboard, &inArea](std::uint32_t* colors) {
						for (std::int32_t i = 0; i < PixelCount; i++) {
							if (inArea(i)) {
								colors[i] = clipboard.Rgba[i];
							}
						}
					}, false, _("Paste pixels"));
				} else {
					ChangeTile(*s, *state, false, [&clipboard, &inArea](std::uint8_t* indices) {
						for (std::int32_t i = 0; i < PixelCount; i++) {
							if (inArea(i)) {
								indices[i] = clipboard.Indices[i];
							}
						}
					}, false, _("Paste pixels"));
				}
			});
			pastePixels->Style = UiButtonStyle::Flat;
			pastePixels->Tooltip = _("Paste the copied pixels where they were in their tile - the mask in the mask mode");
			pastePixels->IsEnabledFunc = []() { return GetPixelClipboard().Valid; };

			// Painted right in the level, wherever the tile is seen
			auto makePaint = [canvasHolder]() {
				return TilePixelTool::PaintFunction([canvasHolder](Vector2i from, Vector2i to, MouseButton button, bool continued) {
					if (*canvasHolder != nullptr) {
						(*canvasHolder)->PaintPixel(from, to, button, continued);
					}
				});
			};
			// The tile copied into the level for true color is painted in the level instead of the tile it was
			auto lastTarget = std::make_shared<std::int32_t>(state->TileId);
			// Not the state itself, which keeps this function
			std::weak_ptr<TileEditState> weakState = state;
			state->OnRetarget = [s, weakState, lastTarget, makePaint]() {
				std::shared_ptr<TileEditState> current = weakState.lock();
				if (current == nullptr) {
					return;
				}
				auto* tool = static_cast<TilePixelTool*>(s->GetTools().Get(EditorToolType::TilePixels));
				if (tool != nullptr && tool->GetTargetTile() == *lastTarget) {
					tool->SetTarget(current->TileId, makePaint());
				}
				*lastTarget = current->TileId;
			};
			auto* inLevel = row->Add<UiButton>(_("Paint in Level"), EditorIcon::Brush, [s, state, makePaint]() {
				auto* tool = static_cast<TilePixelTool*>(s->GetTools().Get(EditorToolType::TilePixels));
				if (s->GetTools().GetChosenType() == EditorToolType::TilePixels && tool->GetTargetTile() == state->TileId) {
					tool->SetTarget(-1, nullptr);
					s->GetTools().SetActive(EditorToolType::Brush);
					return;
				}
				tool->SetTarget(state->TileId, makePaint());
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
		*canvasHolder = columns->Add<TilePixelCanvas>(session, state, [s, dialogPtr]() {
			// The tile sets changed, the window would change another tile now
			s->ShowMessage(_("The tile sets of the level changed, open the tile again to change it"));
			dialogPtr->Close();
		});
		{
			auto* right = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
			// Any color is painted on a true color tile, and with true color painting, otherwise an entry of the palette
			auto paintsRgba = [state]() { return ((state->LevelTile && state->TrueColor) || state->PaintTrueColor); };
			auto* swatch = right->Add<UiColorSwatch>([state, paintsRgba]() {
				if (paintsRgba()) {
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
			{
				// Any color and how see-through it is
				UiColorSwatch* swatchPtr = swatch;
				swatch->OnClick = [s, state, swatchPtr, paintsRgba]() {
					if (!paintsRgba()) {
						return;
					}
					UiColorPicker::ShowPopup(*s->GetSurface(), Vector2f(swatchPtr->Bounds.X, swatchPtr->Bounds.Y + swatchPtr->Bounds.H), [state]() {
						return RgbaToColor(state->Rgba);
					}, [state](const Colorf& color) {
						state->Rgba = ColorToRgba(color);
					}, true);
				};
				swatch->Tooltip = _("Color to paint with - in true color, a click chooses any");
			}
			auto* trueColorButton = right->Add<UiButton>(_("True Color"), EditorIcon::None, [state]() {
				state->PaintTrueColor = !state->PaintTrueColor;
				if (state->PaintTrueColor && state->Rgba == 0xFFFFFFFFu && state->Color != 0) {
					state->Rgba = (ContentResolver::Get().GetPalettes()[state->Color] | 0xFF000000u);
				}
			});
			trueColorButton->Style = UiButtonStyle::Flat;
			trueColorButton->Tooltip = _("Paint any color, a tile of a tile set is copied into this level for it on the first stroke, a tile of the level becomes a true color one");
			trueColorButton->IsCheckedFunc = [state]() { return (state->PaintTrueColor || (state->LevelTile && state->TrueColor)); };
			trueColorButton->IsVisibleFunc = [state]() { return (state->Mode == EditMode::Pixels); };
			auto* grid = right->Add<UiPaletteGrid>([]() {
				return ContentResolver::Get().GetPalettes().data();
			}, [state, paintsRgba]() {
				return (paintsRgba() ? -1 : std::int32_t(state->Color));
			}, [state, paintsRgba](std::int32_t index, bool) {
				state->Color = std::uint8_t(index);
				if (paintsRgba()) {
					state->Rgba = (index == 0 ? 0u : (ContentResolver::Get().GetPalettes()[index] | 0xFF000000u));
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

			// A tile of a tile set can become a tile of the level while the window is open, painted in true color
			right->Add<UiLabel>(_("Changes are saved with the level"), UiTheme::TextDim)->IsVisibleFunc = [state]() { return state->LevelTile; };
			{
				auto* revert = right->Add<UiButton>(_("As in Tile Set"), EditorIcon::Undo, [s, state]() {
					LevelDocument& document = s->GetDocument();
					if (IsStale(*s, *state)) {
						return;
					}
					if (SetTileOverrideCommand::FindDiffuse(document, state->TileId) == nullptr && SetTileOverrideCommand::FindMask(document, state->TileId) == nullptr) {
						return;
					}
					s->Execute(std::make_unique<SetTileOverrideCommand>(document, state->TileId, nullptr, nullptr, _("Tile as in tile set")));
				});
				revert->Tooltip = _("The level doesn't change the tile anymore");
				revert->IsVisibleFunc = [state]() { return !state->LevelTile; };
				auto* apply = right->Add<UiButton>(_("Save into Tile Set"), EditorIcon::Save, [s, state]() {
					ShowApplyTileChangesDialog(*s, state->Ref.Source);
				});
				apply->Tooltip = _("Make the tiles changed in this level tiles of a tile set");
				apply->IsEnabledFunc = [s, state]() {
					return (CountChangedTiles(*s, state->Ref.Source) > 0);
				};
				apply->IsVisibleFunc = [state]() { return !state->LevelTile; };
				// True color, and anything else only this level has, in a copy of the tile the level shows instead
				auto* copy = right->Add<UiButton>(_("Copy into This Level"), EditorIcon::Copy, [s, state, dialogPtr]() {
					if (IsStale(*s, *state)) {
						return;
					}
					TileRef original = state->Ref;
					TileRef copied;
					if (CopyTileIntoLevel(*s, original, copied)) {
						std::int32_t animation = state->Animation, frame = state->Frame;
						dialogPtr->Close();
						ShowTileEditorFor(*s, copied, animation, frame);
						s->ShowMessage(_("The level shows a copy of the tile everywhere now, only this level has it"));
					}
				});
				copy->Tooltip = _("The tiles of this level can be changed in true color, the copy is shown wherever the tile was");
				copy->IsVisibleFunc = [state]() { return !state->LevelTile; };
			}

			// To change the tile in another program and bring it back
			auto* importImage = right->Add<UiButton>(_("Import PNG"), EditorIcon::Folder, [s, state]() {
				ShowOpenImageDialog(*s, _("Import Tile"), [s, state](StringView path) {
					PngImage image;
					String error;
					if (!PngCodec::Load(path, image, &error)) {
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
						s->ShowMessage(_("Only the top left 32 x 32 pixels of the image were taken"));
					} else if (reduced) {
						s->ShowMessage(_("Some colors aren't in the palette, the closest ones were taken"));
					}
				});
			});
			importImage->Tooltip = _("Take the pixels of the tile from an image");
			auto* exportImage = right->Add<UiButton>(_("Export PNG"), EditorIcon::Export, [s, state]() {
				SmallVector<UiFileRoot, 4> roots;
				s->GetImageRoots(true, roots);
				String name = _f("tile{}.png", state->TileId);
				UiFileDialog::Show(*s->GetSurface(), _("Export Tile"), UiFileDialogMode::Save, Death::move(roots), ".png"_s, {}, name, [s, state](StringView path) {
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

		// The level is seen behind it, with the tile changing as it's painted
		ShowModeless(*surface, Death::move(dialog), [s, state, canvasHolder]() {
			// Painting in the level ends with the window, a stroke not saved yet is saved now
			CommitPending(*s, *state);
			*canvasHolder = nullptr;
			auto* tool = static_cast<TilePixelTool*>(s->GetTools().Get(EditorToolType::TilePixels));
			if (tool->GetTargetTile() == state->TileId) {
				tool->SetTarget(-1, nullptr);
				if (s->GetTools().GetChosenType() == EditorToolType::TilePixels) {
					s->GetTools().SetActive(EditorToolType::Brush);
				}
			}
		});
	}
	}
}

#endif
