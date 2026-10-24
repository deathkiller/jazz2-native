#include "PaletteDialog.h"
#include "TextPrompt.h"
#include "TileSetApplyDialog.h"
#include "TileSetsDialog.h"
#include "../Imaging/PaletteFile.h"

#if defined(WITH_EDITOR)

#include "../Tools/EditorContext.h"
#include "../Common/TileStamp.h"
#include "../Document/CommandHistory.h"
#include "../Document/LevelDocument.h"
#include "../Common/TileSource.h"
#include "../Document/Commands.h"
#include "../Document/DocumentSnapshot.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Imaging/PaletteMatch.h"
#include "../UI/UiColors.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t ColorCount = Formats::LevelFile::ColorsPerPalette;
		// Width of the column next to the grid in units, its longest labels and rows of buttons fit
		constexpr float PaletteSideWidth = 280.0f;

		struct PaletteSelection
		{
			// The entry the range starts at, which is the one edited, and where it ends
			std::int32_t Anchor = 1;
			std::int32_t End = 1;

			std::int32_t First() const {
				return std::min(Anchor, End);
			}
			std::int32_t Last() const {
				return std::max(Anchor, End);
			}
		};

		bool UsesOwnPalette(IEditorContext& session)
		{
			return (session.GetDocument().Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette;
		}

		// The palette the level is shown in, its own or that of its tile set
		const std::uint32_t* GetShownPalette(IEditorContext& session)
		{
			return (UsesOwnPalette(session) ? session.GetDocument().LevelPalette : ContentResolver::Get().GetPalettes().data());
		}

		// Whether the entries the sprites share with the level are kept as they are, for as long as the editor runs
		bool& GetSpriteLock()
		{
			static bool locked = false;
			return locked;
		}

		// Puts back the entries the sprites share, if they're locked
		void KeepSpriteColors(const std::uint32_t* before, std::uint32_t* palette)
		{
			if (!GetSpriteLock()) {
				return;
			}
			for (std::int32_t i = 1; i < ColorCount; i++) {
				if (IsSpriteColor(i, before[i])) {
					palette[i] = before[i];
				}
			}
		}

		// Changes the colors of the own palette of the level as an undoable step, consecutive changes are merged
		void ChangePalette(IEditorContext& session, StringView name, Function<void(std::uint32_t*)>&& change, bool allowMerge = true)
		{
			if (!UsesOwnPalette(session)) {
				return;
			}
			std::uint32_t palette[ColorCount];
			std::memcpy(palette, session.GetDocument().LevelPalette, sizeof(palette));
			change(palette);
			KeepSpriteColors(session.GetDocument().LevelPalette, palette);
			if (std::memcmp(palette, session.GetDocument().LevelPalette, sizeof(palette)) == 0) {
				return;
			}
			session.Execute(std::make_unique<SetLevelPaletteCommand>(session.GetDocument(), true, palette, name), allowMerge);
		}

		void ToHsv(std::uint32_t entry, float& h, float& s, float& v)
		{
			float r = (entry & 0xFF) / 255.0f, g = ((entry >> 8) & 0xFF) / 255.0f, b = ((entry >> 16) & 0xFF) / 255.0f;
			float max = std::max({ r, g, b }), min = std::min({ r, g, b }), delta = max - min;
			v = max;
			s = (max > 0.0f ? delta / max : 0.0f);
			if (delta <= 0.0f) {
				h = 0.0f;
			} else if (max == r) {
				h = std::fmod((g - b) / delta + 6.0f, 6.0f) * 60.0f;
			} else if (max == g) {
				h = ((b - r) / delta + 2.0f) * 60.0f;
			} else {
				h = ((r - g) / delta + 4.0f) * 60.0f;
			}
		}

		std::uint32_t FromHsv(float h, float s, float v, std::uint32_t alpha)
		{
			float sector = std::fmod(std::max(h, 0.0f), 360.0f) / 60.0f;
			std::int32_t i = std::int32_t(sector);
			float f = sector - float(i);
			float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
			float r, g, b;
			switch (i) {
				case 0: r = v; g = t; b = p; break;
				case 1: r = q; g = v; b = p; break;
				case 2: r = p; g = v; b = t; break;
				case 3: r = p; g = q; b = v; break;
				case 4: r = t; g = p; b = v; break;
				default: r = v; g = p; b = q; break;
			}
			auto toByte = [](float c) { return std::uint32_t(std::clamp<std::int32_t>(std::int32_t(std::round(c * 255.0f)), 0, 255)); };
			return toByte(r) | (toByte(g) << 8) | (toByte(b) << 16) | (alpha & 0xFF000000u);
		}

		String FormatHex(std::uint32_t entry)
		{
			char buffer[16];
			std::int32_t length = std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", entry & 0xFF, (entry >> 8) & 0xFF, (entry >> 16) & 0xFF);
			return String(buffer, std::size_t(std::max<std::int32_t>(length, 0)));
		}

		bool ParseHex(StringView text, std::uint32_t& rgb)
		{
			String value = text.trimmed();
			StringView digits = value;
			if (digits.hasPrefix("#"_s)) {
				digits = digits.exceptPrefix(1);
			}
			if (digits.size() != 6) {
				return false;
			}
			char* end = nullptr;
			String terminated = digits;
			unsigned long parsed = std::strtoul(terminated.data(), &end, 16);
			if (end != terminated.data() + 6) {
				return false;
			}
			// #RRGGBB to red in the lowest byte
			rgb = std::uint32_t(((parsed >> 16) & 0xFF) | (parsed & 0xFF00) | ((parsed & 0xFF) << 16));
			return true;
		}

		// How many pixels of the 8-bit tiles the level shows use each entry, counted again when the level changed
		struct PaletteUsage
		{
			std::uint32_t Pixels[ColorCount];
			std::int32_t Tiles = 0;
			// Tiles the level shows that are true color ones, and those whose tile set can't be read
			std::int32_t TrueColorTiles = 0;
			std::int32_t UnreadTiles = 0;
			std::uint32_t Revision = ~0u;
			bool ShowUnused = false;
			// A move of entries changes the pixels too, so the level looks the same
			bool KeepLook = true;
			// Which entries are crossed out in the grid
			enum class Marks : std::uint8_t { None, Unused, Sprites, TileSetDiff } Mark = Marks::None;
			// How the colors between two entries are made
			enum class Steps : std::uint8_t { OkLab, Rgb, Hue } Gradient = Steps::OkLab;
			// The palette of the default tile set, read again when the tile set is another one
			String TileSetPaletteOf;
			std::uint32_t TileSetPalette[ColorCount];
			bool HasTileSetPalette = false;
		};

		// The palette of the default tile set of the level, `nullptr` if it can't be read
		const std::uint32_t* GetTileSetPalette(IEditorContext& session, PaletteUsage& usage)
		{
			StringView name = session.GetDocument().Properties.DefaultTileSet;
			if (usage.TileSetPaletteOf != name) {
				usage.TileSetPaletteOf = name;
				usage.HasTileSetPalette = session.GetTileSetLibrary().ReadPalette(name, usage.TileSetPalette);
			}
			return (usage.HasTileSetPalette ? usage.TileSetPalette : nullptr);
		}

		// The colors between the first and the last entry of a range, as even steps of how they look, of the red, green
		// and blue, or around the color wheel by the shorter way
		void MakeGradient(PaletteUsage::Steps steps, const std::uint32_t* palette, std::int32_t first, std::int32_t last, std::uint32_t* result)
		{
			for (std::int32_t i = first; i <= last; i++) {
				result[i] = palette[i];
			}
			if (last - first < 2) {
				return;
			}
			std::uint32_t from = palette[first], to = palette[last];
			for (std::int32_t i = first + 1; i < last; i++) {
				float t = float(i - first) / float(last - first);
				std::uint32_t alpha = (palette[i] & 0xFF000000u);
				switch (steps) {
					case PaletteUsage::Steps::Rgb: {
						std::uint32_t color = 0;
						for (std::int32_t ch = 0; ch < 3; ch++) {
							float a = float((from >> (ch * 8)) & 0xFF), b = float((to >> (ch * 8)) & 0xFF);
							color |= std::uint32_t(std::clamp<std::int32_t>(std::int32_t(std::round(a + (b - a) * t)), 0, 255)) << (ch * 8);
						}
						result[i] = color | alpha;
						break;
					}
					case PaletteUsage::Steps::Hue: {
						float h1, s1, v1, h2, s2, v2;
						ToHsv(from, h1, s1, v1);
						ToHsv(to, h2, s2, v2);
						float dh = h2 - h1;
						if (dh > 180.0f) {
							dh -= 360.0f;
						} else if (dh < -180.0f) {
							dh += 360.0f;
						}
						result[i] = FromHsv(std::fmod(h1 + dh * t + 360.0f, 360.0f), s1 + (s2 - s1) * t, v1 + (v2 - v1) * t, alpha);
						break;
					}
					default:
						result[i] = OkLab::Lerp(OkLab::FromPaletteEntry(from), OkLab::FromPaletteEntry(to), t).ToPaletteEntry(std::uint8_t(alpha >> 24));
						break;
				}
			}
		}

		/** The picked entries as they are and as the gradient would make them, one row each */
		class GradientPreview : public UiElement
		{
		public:
			GradientPreview(IEditorContext& session, std::shared_ptr<PaletteSelection> selection, std::shared_ptr<PaletteUsage> usage)
				: _session(session), _selection(Death::move(selection)), _usage(Death::move(usage))
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(context.Px(PaletteSideWidth), context.Px(18.0f));
			}

			void Draw(UiPainter& painter) override
			{
				const std::uint32_t* palette = GetShownPalette(_session);
				std::int32_t first = _selection->First(), last = _selection->Last();
				std::uint32_t result[ColorCount];
				MakeGradient(_usage->Gradient, palette, first, last, result);
				std::int32_t count = last - first + 1;
				float width = Bounds.W / float(count), half = std::floor(Bounds.H * 0.5f);
				for (std::int32_t i = 0; i < count; i++) {
					Colorf before = UiPaletteGrid::ToColor(palette[first + i]);
					Colorf after = UiPaletteGrid::ToColor(result[first + i]);
					before.A = after.A = 1.0f;
					Rectf rect(Bounds.X + i * width, Bounds.Y, std::ceil(width), half);
					painter.FillRect(rect, before, UiLayer::Content);
					painter.FillRect(Rectf(rect.X, rect.Y + half, rect.W, Bounds.H - half), after, UiLayer::Content);
				}
			}

			StringView GetTooltip(Vector2f pos) const override
			{
				return _x("editor", "The picked entries above, as the gradient would make them below");
			}

		private:
			IEditorContext& _session;
			std::shared_ptr<PaletteSelection> _selection;
			std::shared_ptr<PaletteUsage> _usage;
		};

		// The tiles the level shows, each once, without their flags
		void CollectUsedTiles(const LevelDocument& document, SmallVectorImpl<TileRef>& tiles)
		{
			auto add = [&tiles](TileRef tile) {
				if (tile.IsEmpty() || tile.IsAnimated() || tile.Source == TileRef::Raw) {
					return;
				}
				tile.Flags = 0;
				tiles.push_back(tile);
			};
			for (const DocumentLayer& layer : document.Layers) {
				for (const TileRef& tile : layer.Tiles) {
					add(tile);
				}
			}
			for (const DocumentAnimation& animation : document.Animations) {
				for (const TileRef& frame : animation.Frames) {
					add(frame);
				}
			}
			std::sort(tiles.begin(), tiles.end(), [](const TileRef& a, const TileRef& b) {
				return (a.Source != b.Source ? a.Source < b.Source : a.Index < b.Index);
			});
			tiles.erase(std::unique(tiles.begin(), tiles.end()), tiles.end());
		}

		// The palette indices of an 8-bit tile as the level shows it, overridden or not, `false` for a true color one
		bool GetTileIndices(IEditorContext& session, const TileRef& tile, std::uint8_t* indices)
		{
			LevelDocument& document = session.GetDocument();
			std::int32_t tileId = document.ToTileId(tile);
			if (tileId > 0 && tileId <= UINT16_MAX) {
				if (const Formats::TileDiffuseOverride* diffuse = SetTileOverrideCommand::FindDiffuse(document, std::uint16_t(tileId))) {
					std::memcpy(indices, diffuse->Indices, sizeof(diffuse->Indices));
					return true;
				}
			}
			// A tile set with true color tiles isn't matched to the palette, its 8-bit tiles are shown as they are
			TileSource source;
			if (!ResolveTileSource(document, tile.Source, source)) {
				return false;
			}
			std::int32_t index = source.Offset + tile.Index;
			const std::uint8_t* remapping = source.Remapping;
			const Formats::TileSetData* data = session.GetTileSetData(source.Name);
			if (data == nullptr || index < 0 || index >= data->TileCount || data->TrueColor[index] != 0) {
				return false;
			}
			const std::uint8_t* image = &data->Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
			for (std::int32_t i = 0; i < Formats::LevelFile::TileSize * Formats::LevelFile::TileSize; i++) {
				indices[i] = (remapping != nullptr ? remapping[image[i]] : image[i]);
			}
			return true;
		}

		void UpdateUsage(IEditorContext& session, PaletteUsage& usage)
		{
			std::uint32_t revision = session.GetHistory().GetRevision();
			if (usage.Revision == revision) {
				return;
			}
			usage.Revision = revision;
			std::memset(usage.Pixels, 0, sizeof(usage.Pixels));
			usage.Tiles = 0;
			usage.TrueColorTiles = 0;
			usage.UnreadTiles = 0;
			SmallVector<TileRef, 0> tiles;
			CollectUsedTiles(session.GetDocument(), tiles);
			std::uint8_t indices[Formats::LevelFile::TileSize * Formats::LevelFile::TileSize];
			for (const TileRef& tile : tiles) {
				if (!GetTileIndices(session, tile, indices)) {
					// A true color tile doesn't use the palette, any other one couldn't be read
					TileSource source;
					const Formats::TileSetData* data = (ResolveTileSource(session.GetDocument(), tile.Source, source) ? session.GetTileSetData(source.Name) : nullptr);
					std::int32_t index = source.Offset + tile.Index;
					if (data != nullptr && index >= 0 && index < data->TileCount && data->TrueColor[index] != 0) {
						usage.TrueColorTiles++;
					} else {
						usage.UnreadTiles++;
					}
					continue;
				}
				usage.Tiles++;
				for (std::uint8_t index : indices) {
					usage.Pixels[index]++;
				}
			}
		}

		// Whether the entries no tile uses can be told at all: true color tiles don't use the palette, and the pixels of
		// a tile set that can't be read aren't known, crossing out every entry for them would only mislead
		bool CanTellUnused(const PaletteUsage& usage)
		{
			return (usage.UnreadTiles == 0 && (usage.Tiles > 0 || usage.TrueColorTiles == 0));
		}

		/** A move of entries of the palette with the pixels that go with them, it keeps only what it changes */
		class PermutePaletteCommand : public EditCommand
		{
			DEATH_RUNTIME_OBJECT(EditCommand);

		public:
			// The parts of the level the move changes
			struct State
			{
				LevelFlags Flags;
				std::uint32_t Palette[ColorCount];
				SmallVector<Formats::TileDiffuseOverride, 0> Overrides;
				SmallVector<Formats::ExtraTileSetRecord, 0> TileSets;
			};

			PermutePaletteCommand(const LevelDocument& document, StringView name)
				: _name(name), _tileSetsChanged(false), _reloadTileSets(false)
			{
				Capture(document, _old);
				Capture(document, _new);
			}

			// The state after the move, as it's made
			State& GetNew()
			{
				return _new;
			}

			// The extra tile sets change too; a tile set file matched to the palette otherwise is only taken when the
			// level is loaded again, the tiles of the level are shown again as overridden tiles are
			void MarkTileSetsChanged(bool needsReload)
			{
				_tileSetsChanged = true;
				_reloadTileSets |= needsReload;
			}

			void Do(LevelDocument& document, DocumentChanges& changes) override
			{
				Apply(document, _new, _old, changes);
			}

			void Undo(LevelDocument& document, DocumentChanges& changes) override
			{
				Apply(document, _old, _new, changes);
			}

			String GetName() const override
			{
				return _name;
			}

			std::size_t GetMemoryUsage() const override
			{
				std::size_t bytes = sizeof(PermutePaletteCommand) + _name.size() +
					(_old.Overrides.size() + _new.Overrides.size()) * sizeof(Formats::TileDiffuseOverride);
				if (_tileSetsChanged) {
					for (const State* state : { &_old, &_new }) {
						for (const Formats::ExtraTileSetRecord& record : state->TileSets) {
							bytes += sizeof(record) + record.Data.size();
						}
					}
				}
				return bytes;
			}

		private:
			String _name;
			State _old;
			State _new;
			bool _tileSetsChanged;
			bool _reloadTileSets;

			static void Capture(const LevelDocument& document, State& state)
			{
				state.Flags = document.Flags;
				std::memcpy(state.Palette, document.LevelPalette, sizeof(state.Palette));
				state.Overrides = document.DiffuseOverrides;
				state.TileSets = document.ExtraTileSets;
			}

			void Apply(LevelDocument& document, const State& state, const State& other, DocumentChanges& changes)
			{
				document.Flags = state.Flags;
				std::memcpy(document.LevelPalette, state.Palette, sizeof(document.LevelPalette));
				document.DiffuseOverrides = state.Overrides;
				if (_tileSetsChanged) {
					document.ExtraTileSets = state.TileSets;
				}
				changes.Add(DocumentChangeKind::Palette);
				changes.Add(DocumentChangeKind::TileOverrides);
				if ((state.Flags & LevelFlags::UseLevelPalette) != (other.Flags & LevelFlags::UseLevelPalette)) {
					// The level is shown in another palette, which is taken when it's loaded
					changes.Add(DocumentChangeKind::LevelProperties);
				}
				if (_reloadTileSets) {
					changes.Add(DocumentChangeKind::TileSets);
				}
			}
		};

		/**
		 * Moves entries of the palette, @p permutation gives the new place of each entry, as one step
		 *
		 * With @p keepLook the pixels of the 8-bit tiles go with their colors, so the level looks the same: the
		 * colors of extra tile sets are matched to the moved entries, the tiles of the level and the tiles the level
		 * changes are changed, and the tiles of the default tile set the level shows are changed in this level.
		 * Sprites are drawn in the palette too, they can't follow.
		 */
		bool PermutePalette(IEditorContext& session, const std::uint8_t* permutation, bool keepLook, StringView name)
		{
			constexpr std::int32_t PixelCount = Formats::LevelFile::TileSize * Formats::LevelFile::TileSize;
			LevelDocument& document = session.GetDocument();

			// The palette the level is shown in, which becomes its own if it wasn't
			const std::uint32_t* shown = GetShownPalette(session);
			std::uint32_t palette[ColorCount];
			bool movesSpriteColors = false;
			for (std::int32_t i = 0; i < ColorCount; i++) {
				palette[permutation[i]] = shown[i];
				movesSpriteColors |= (permutation[i] != i && IsSpriteColor(i, shown[i]));
			}
			if (movesSpriteColors && GetSpriteLock()) {
				session.ShowMessage(_x("editor", "Sprites use some of these entries, they're kept where they are - turn off keeping the colors of sprites to move them"));
				return false;
			}
			if (!keepLook) {
				session.Execute(std::make_unique<SetLevelPaletteCommand>(document, true, palette, name));
				if (movesSpriteColors) {
					session.ShowMessage(_x("editor", "Sprites use some of the moved entries, they show other colors now"));
				}
				return true;
			}

			// The 8-bit tiles of the default tile set the level shows are changed in this level, and those of extra
			// tile sets that have true color tiles too, which can't be matched to the palette
			struct AddedOverride
			{
				std::uint16_t TileId;
				std::uint8_t Indices[PixelCount];
			};
			SmallVector<AddedOverride, 0> added;
			SmallVector<TileRef, 0> tiles;
			CollectUsedTiles(document, tiles);
			for (const TileRef& tile : tiles) {
				if (tile.Source != TileRef::DefaultTileSet) {
					if (std::size_t(tile.Source) - 1 >= document.ExtraTileSets.size()) {
						continue;
					}
					const Formats::ExtraTileSetRecord& record = document.ExtraTileSets[tile.Source - 1];
					if ((record.Flags & (Formats::ExtraTileSetRecord::Embedded | Formats::ExtraTileSetRecord::TrueColor)) != Formats::ExtraTileSetRecord::TrueColor) {
						// The tiles of the level are changed below, all at once, and an 8-bit tile set is matched
						continue;
					}
				}
				std::int32_t tileId = document.ToTileId(tile);
				if (tileId <= 0 || tileId > UINT16_MAX || SetTileOverrideCommand::FindDiffuse(document, std::uint16_t(tileId)) != nullptr) {
					// A tile the level changes already goes with all the others below
					continue;
				}
				AddedOverride item;
				if (!GetTileIndices(session, tile, item.Indices)) {
					continue;
				}
				bool changed = false;
				for (std::int32_t i = 0; i < PixelCount; i++) {
					std::uint8_t mapped = permutation[item.Indices[i]];
					changed |= (mapped != item.Indices[i]);
					item.Indices[i] = mapped;
				}
				if (changed) {
					item.TileId = std::uint16_t(tileId);
					added.push_back(item);
				}
			}

			// The tile sets the level has in itself, their 8-bit tiles get the indices of the moved colors
			SmallVector<std::pair<std::int32_t, SmallVector<std::uint8_t, 0>>, 1> levelTiles;
			for (std::int32_t slot = 0; slot < std::int32_t(document.ExtraTileSets.size()); slot++) {
				const Formats::ExtraTileSetRecord& record = document.ExtraTileSets[slot];
				if ((record.Flags & Formats::ExtraTileSetRecord::Embedded) == 0) {
					continue;
				}
				const Formats::TileSetData* data = session.GetTileSetData(record.Name);
				if (data == nullptr) {
					continue;
				}
				Formats::TileSetData copy = *data;
				if ((record.Flags & (Formats::ExtraTileSetRecord::Remapped | Formats::ExtraTileSetRecord::TrueColor)) == Formats::ExtraTileSetRecord::Remapped) {
					// As they're shown, they're not matched to the palette anymore then
					BakeRemapping(copy, record.Remapping);
				}
				for (std::int32_t i = 0; i < copy.TileCount; i++) {
					if (copy.TrueColor[i] != 0) {
						continue;
					}
					std::uint8_t* image = &copy.Images[std::size_t(i) * Formats::TileSetData::ImageBytesPerTile];
					for (std::int32_t p = 0; p < PixelCount; p++) {
						image[p] = permutation[image[p]];
					}
				}
				auto& item = levelTiles.emplace_back();
				item.first = slot;
				if (!SerializeTileSet(copy, item.second)) {
					levelTiles.pop_back();
				}
			}

			// All of it as a single change, the level is shown again only once. Only what changes is kept for undo, a
			// snapshot of all tile sets would make the level be loaded again, which the palette and overrides don't need.
			auto command = std::make_unique<PermutePaletteCommand>(document, name);
			PermutePaletteCommand::State& next = command->GetNew();
			next.Flags = (next.Flags | LevelFlags::UseLevelPalette);
			std::memcpy(next.Palette, palette, sizeof(next.Palette));
			std::int32_t changedTiles = std::int32_t(next.Overrides.size() + added.size() + levelTiles.size());
			for (Formats::TileDiffuseOverride& tile : next.Overrides) {
				for (std::int32_t p = 0; p < PixelCount; p++) {
					tile.Indices[p] = permutation[tile.Indices[p]];
				}
			}
			for (const AddedOverride& item : added) {
				auto& list = next.Overrides;
				auto it = std::lower_bound(list.begin(), list.end(), item.TileId, [](const Formats::TileDiffuseOverride& tile, std::uint16_t id) { return tile.TileId < id; });
				Formats::TileDiffuseOverride tile;
				tile.TileId = item.TileId;
				std::memcpy(tile.Indices, item.Indices, sizeof(tile.Indices));
				list.insert(it, tile);
			}
			for (std::size_t slot = 0; slot < next.TileSets.size(); slot++) {
				Formats::ExtraTileSetRecord& record = next.TileSets[slot];
				if ((record.Flags & (Formats::ExtraTileSetRecord::Embedded | Formats::ExtraTileSetRecord::TrueColor)) != 0) {
					continue;
				}
				// Each color of the file is the moved entry it was shown in, all of its tiles follow, not only those
				// the level shows
				if ((record.Flags & Formats::ExtraTileSetRecord::Remapped) == 0) {
					for (std::int32_t i = 0; i < ColorCount; i++) {
						record.Remapping[i] = std::uint8_t(i);
					}
					record.Flags |= Formats::ExtraTileSetRecord::Remapped;
				}
				for (std::int32_t i = 1; i < ColorCount; i++) {
					record.Remapping[i] = permutation[record.Remapping[i]];
				}
				record.Remapping[0] = 0;
				command->MarkTileSetsChanged(true);
				changedTiles++;
			}
			for (auto& [slot, bytes] : levelTiles) {
				Formats::ExtraTileSetRecord& record = next.TileSets[slot];
				record.Data = Death::move(bytes);
				record.Flags &= ~Formats::ExtraTileSetRecord::Remapped;
				command->MarkTileSetsChanged(false);
			}
			session.Execute(Death::move(command));

			String message = _x("editor", "The pixels of the tiles went with their colors");
			if (movesSpriteColors) {
				message += " "_s + _x("editor", "- sprites use some of the moved entries, they show other colors now");
			}
			if (changedTiles > 0 || movesSpriteColors) {
				session.ShowMessage(message);
			}
			return true;
		}

		// A number of the color of the edited entry, its value is a part of the entry
		UiNumberBox* AddChannel(UiStack* row, IEditorContext& session, std::shared_ptr<PaletteSelection> selection, StringView label,
			float max, Function<float(std::uint32_t)>&& get, Function<std::uint32_t(std::uint32_t, float)>&& set)
		{
			IEditorContext* s = &session;
			row->Add<UiLabel>(label, UiTheme::TextDim);
			auto getShared = std::make_shared<Function<float(std::uint32_t)>>(Death::move(get));
			auto setShared = std::make_shared<Function<std::uint32_t(std::uint32_t, float)>>(Death::move(set));
			auto* number = row->Add<UiNumberBox>([s, selection, getShared]() {
				return (*getShared)(GetShownPalette(*s)[selection->Anchor]);
			}, [s, selection, setShared](float value, bool final) {
				std::int32_t index = selection->Anchor;
				// TRANSLATORS: Name of an undo step in the history, a color of the palette of the level was changed
				ChangePalette(*s, _x("editor", "Palette color"), [index, value, setShared](std::uint32_t* palette) {
					palette[index] = (*setShared)(palette[index], value);
				});
				if (final) {
					// A drag of the number is one step, the next one is another
					s->GetHistory().BreakMerge();
				}
			}, 0.0f, max, 1.0f, 0);
			number->Flex = 1.0f;
			number->IsEnabledFunc = [s]() { return UsesOwnPalette(*s); };
			return number;
		}

		// Makes the tiles that use an entry of the palette the brush, those of the default tile set and those of the parts
		// of extra tile sets the level takes, as the level shows them
		void FindTilesUsingEntry(IEditorContext& session, std::int32_t entry)
		{
			constexpr std::int32_t PixelCount = Formats::LevelFile::TileSize * Formats::LevelFile::TileSize;
			// A brush of more tiles than this would be hard to look at
			constexpr std::int32_t MaxTiles = 200;
			constexpr std::int32_t Columns = 10;
			const LevelDocument& document = session.GetDocument();
			SmallVector<TileRef, 0> found;
			std::int32_t total = 0;
			std::uint8_t indices[PixelCount];
			auto check = [&](const TileRef& tile) {
				if (!GetTileIndices(session, tile, indices)) {
					return;
				}
				for (std::int32_t i = 0; i < PixelCount; i++) {
					if (indices[i] == entry) {
						total++;
						if (std::int32_t(found.size()) < MaxTiles) {
							found.push_back(tile);
						}
						return;
					}
				}
			};
			for (std::int32_t i = 1; i < document.DefaultTileCount && i <= UINT16_MAX; i++) {
				check(TileRef{ std::uint16_t(i), TileRef::DefaultTileSet, 0 });
			}
			for (std::size_t slot = 0; slot < document.ExtraTileSets.size() && slot < TileRef::LastExtraTileSet; slot++) {
				for (std::int32_t i = 0; i < document.ExtraTileSets[slot].Count; i++) {
					check(TileRef{ std::uint16_t(i), std::uint8_t(slot + 1), 0 });
				}
			}
			if (found.empty()) {
				// TRANSLATORS: Status message in the Level Palette dialog, {} is the number of an entry of the palette
				session.ShowMessage(_fx("editor", "No tile of the tile sets uses entry {}", entry));
				return;
			}
			TileStamp stamp;
			stamp.Width = std::min<std::int32_t>(std::int32_t(found.size()), Columns);
			stamp.Height = (std::int32_t(found.size()) + Columns - 1) / Columns;
			for (std::int32_t i = 0; i < stamp.Width * stamp.Height; i++) {
				bool present = (i < std::int32_t(found.size()));
				stamp.Tiles.push_back(present ? found[i] : TileRef{});
				stamp.Present.push_back(present);
			}
			session.SetStamp(Death::move(stamp));
			session.ShowMessage(total > MaxTiles
				// TRANSLATORS: Status message in the Level Palette dialog, {} are the count of tiles, the number of an entry of the palette and the count of tiles put into the brush
				? String(_fx("editor", "{} tiles use entry {}, the first {} of them are the brush now", total, entry, MaxTiles))
				// TRANSLATORS: Status message in the Level Palette dialog, {} are the count of tiles and the number of an entry of the palette
				: String(_fnx("editor", "{} tile uses entry {}, it's the brush now", "{} tiles use entry {}, they're the brush now", total, total, entry)));
		}
	}

	bool MovePaletteEntries(IEditorContext& session, std::int32_t first, std::int32_t last, std::int32_t direction, bool keepLook)
	{
		if (first > last) {
			std::swap(first, last);
		}
		// The first entry is transparent, it stays where it is
		if (first < 1 || last >= ColorCount || (direction < 0 && first <= 1) || (direction > 0 && last >= ColorCount - 1) || direction == 0) {
			return false;
		}
		std::uint8_t permutation[ColorCount];
		for (std::int32_t i = 0; i < ColorCount; i++) {
			if (direction < 0) {
				// The entry before the range goes after it
				permutation[i] = std::uint8_t(i >= first && i <= last ? i - 1 : (i == first - 1 ? last : i));
			} else {
				// The entry after the range goes before it
				permutation[i] = std::uint8_t(i >= first && i <= last ? i + 1 : (i == last + 1 ? first : i));
			}
		}
		// TRANSLATORS: Name of an undo step in the history, entries of the palette of the level were moved
		return PermutePalette(session, permutation, keepLook, _x("editor", "Move palette entries"));
	}

	void ShowLevelPaletteDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		IEditorContext* s = &session;
		auto selection = std::make_shared<PaletteSelection>();
		// TRANSLATORS: Title of the dialog that edits the palette of the level
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Level Palette"));
		auto* content = dialog->GetContent();

		content->Add<UiCheckBox>(_x("editor", "The level has its own palette"), [s]() {
			return UsesOwnPalette(*s);
		}, [s](bool value) {
			// It starts as a copy of the palette the level is shown in
			std::uint32_t palette[ColorCount];
			std::memcpy(palette, s->GetDocument().LevelPalette, sizeof(palette));
			if (value && std::all_of(palette, palette + ColorCount, [](std::uint32_t entry) { return entry == 0; })) {
				std::memcpy(palette, ContentResolver::Get().GetPalettes().data(), sizeof(palette));
			}
			s->Execute(std::make_unique<SetLevelPaletteCommand>(s->GetDocument(), value, palette,
				// TRANSLATORS: Names of undo steps in the history, the level got its own palette or uses the palette of its tile set again
				value ? _x("editor", "Use own palette") : _x("editor", "Use tile set palette")));
		})->Tooltip = _x("editor", "Otherwise the level is shown in the palette of its default tile set");

		auto* columns = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing * 2.0f);
		auto* grid = columns->Add<UiPaletteGrid>([s]() {
			return GetShownPalette(*s);
		}, [selection]() {
			return selection->Anchor;
		}, [selection](std::int32_t index, bool extend) {
			if (!extend) {
				selection->Anchor = index;
			}
			selection->End = index;
		});
		grid->CellSize = 12.0f;
		grid->SelectionEndFunc = [selection]() { return selection->End; };
		auto usage = std::make_shared<PaletteUsage>();
		grid->IsCrossedFunc = [s, usage](std::int32_t index) {
			if (index == 0) {
				return false;
			}
			switch (usage->Mark) {
				case PaletteUsage::Marks::Unused:
					UpdateUsage(*s, *usage);
					return (CanTellUnused(*usage) && usage->Pixels[index] == 0);
				case PaletteUsage::Marks::Sprites:
					return IsSpriteColor(index, GetShownPalette(*s)[index]);
				case PaletteUsage::Marks::TileSetDiff: {
					const std::uint32_t* tileSetPalette = GetTileSetPalette(*s, *usage);
					return (tileSetPalette != nullptr && (tileSetPalette[index] & 0x00FFFFFFu) != (GetShownPalette(*s)[index] & 0x00FFFFFFu));
				}
				default:
					return false;
			}
		};

		auto* right = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		right->Width = PaletteSideWidth;
		{
			auto getColor = [s, selection]() {
				Colorf color = UiPaletteGrid::ToColor(GetShownPalette(*s)[selection->Anchor]);
				color.A = 1.0f;
				return color;
			};
			auto* swatch = right->Add<UiColorSwatch>(getColor, [s, selection]() {
				UiSurface* surface = s->GetSurface();
				if (surface == nullptr) {
					return;
				}
				// The entry edited is the one picked when it was opened, even if another one is picked meanwhile
				std::int32_t index = selection->Anchor;
				// The colors picked in the popup are one step, not merged into a change before it
				s->GetHistory().BreakMerge();
				UiColorPicker::ShowPopup(*surface, surface->GetPointerPos(), [s, index]() {
					Colorf color = UiPaletteGrid::ToColor(GetShownPalette(*s)[index]);
					color.A = 1.0f;
					return color;
				}, [s, index](const Colorf& color) {
					std::uint32_t rgb = std::uint32_t(std::round(color.R * 255.0f)) | (std::uint32_t(std::round(color.G * 255.0f)) << 8) |
						(std::uint32_t(std::round(color.B * 255.0f)) << 16);
					// TRANSLATORS: Name of an undo step in the history, a color of the palette of the level was changed
					ChangePalette(*s, _x("editor", "Palette color"), [index, rgb](std::uint32_t* palette) {
						palette[index] = (palette[index] & 0xFF000000u) | rgb;
					});
				});
			});
			swatch->IsEnabledFunc = [s]() { return UsesOwnPalette(*s); };
			swatch->Tooltip = _x("editor", "Pick a color");
			swatch->Height = 32.0f;
			swatch->Width = PaletteSideWidth;
			auto* index = right->Add<UiLabel>(StringView(), UiTheme::TextDim);
			index->TextFunc = [selection]() {
				// The first entry picked is the one edited, the others take part in the gradient
				return (selection->First() == selection->Last()
					// TRANSLATORS: Label in the Level Palette dialog, {} is the number of the picked entry of the palette
					? _fx("editor", "Entry {}", selection->Anchor)
					// TRANSLATORS: Label in the Level Palette dialog, {} are the numbers of the first and the last picked entry of the palette
					: _fx("editor", "Entries {} - {}", selection->First(), selection->Last()));
			};
		}

		// The edited entry as red, green and blue, and as hue, saturation and value
		{
			auto* row = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			for (std::int32_t channel = 0; channel < 3; channel++) {
				static const char* const Names[] = { "R", "G", "B" };
				std::int32_t shift = channel * 8;
				AddChannel(row, session, selection, Names[channel], 255.0f, [shift](std::uint32_t entry) {
					return float((entry >> shift) & 0xFF);
				}, [shift](std::uint32_t entry, float value) {
					std::uint32_t byte = std::uint32_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 255));
					return (entry & ~(0xFFu << shift)) | (byte << shift);
				});
			}
		}
		{
			auto* row = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			// TRANSLATORS: Hue, saturation and value of a color, one letter each
			AddChannel(row, session, selection, _x("editor", "H"), 359.0f, [](std::uint32_t entry) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return std::round(h);
			}, [](std::uint32_t entry, float value) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return FromHsv(value, sat, v, entry);
			});
			AddChannel(row, session, selection, _x("editor", "S"), 100.0f, [](std::uint32_t entry) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return std::round(sat * 100.0f);
			}, [](std::uint32_t entry, float value) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return FromHsv(h, value / 100.0f, v, entry);
			});
			AddChannel(row, session, selection, _x("editor", "V"), 100.0f, [](std::uint32_t entry) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return std::round(v * 100.0f);
			}, [](std::uint32_t entry, float value) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return FromHsv(h, sat, value / 100.0f, entry);
			});
		}
		{
			auto* hex = right->Add<UiTextBox>();
			hex->MaxLength = 7;
			hex->TextFunc = [s, selection]() { return FormatHex(GetShownPalette(*s)[selection->Anchor]); };
			hex->OnCommit = [s, selection](StringView text) {
				std::uint32_t rgb;
				if (!ParseHex(text, rgb)) {
					s->ShowMessage(_x("editor", "A color is written as #RRGGBB"));
					return;
				}
				std::int32_t index = selection->Anchor;
				// TRANSLATORS: Name of an undo step in the history, a color of the palette of the level was changed
				ChangePalette(*s, _x("editor", "Palette color"), [index, rgb](std::uint32_t* palette) {
					palette[index] = (palette[index] & 0xFF000000u) | rgb;
				}, false);
			};
			hex->IsEnabledFunc = [s]() { return UsesOwnPalette(*s); };
		}

		// Which entries the tiles use, and moving entries
		{
			auto* used = right->Add<UiLabel>(StringView(), UiTheme::TextDim);
			used->TextFunc = [s, selection, usage]() {
				UpdateUsage(*s, *usage);
				if (usage->UnreadTiles > 0) {
					return String(_fnx("editor", "{} tile can't be read from its tile set", "{} tiles can't be read from their tile sets", usage->UnreadTiles, usage->UnreadTiles));
				}
				std::uint32_t pixels = usage->Pixels[selection->Anchor];
				String text = (pixels == 0 ? String(_x("editor", "No tile uses it")) : String(_fnx("editor", "{} pixel of tiles uses it", "{} pixels of tiles use it", pixels, pixels)));
				if (usage->TrueColorTiles > 0) {
					text += " - "_s + _fnx("editor", "{} true color tile doesn't use the palette", "{} true color tiles don't use the palette", usage->TrueColorTiles, usage->TrueColorTiles);
				}
				return text;
			};
			auto* unusedRow = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			// Which entries are crossed out: those no tile uses, those of the colors of sprites, those that differ from
			// the palette of the tile set
			struct MarkOption { String Label; PaletteUsage::Marks Mark; };
			static const MarkOption MarkOptions[] = {
				// TRANSLATORS: Options of the dropdown in the Level Palette dialog, which entries of the palette are crossed out (none, unused, shared with sprites, changed from the tile set)
				{ _x("editor", "Mark nothing"), PaletteUsage::Marks::None },
				{ _x("editor", "Mark unused"), PaletteUsage::Marks::Unused },
				{ _x("editor", "Mark sprite colors"), PaletteUsage::Marks::Sprites },
				{ _x("editor", "Mark changed"), PaletteUsage::Marks::TileSetDiff },
			};
			auto* marks = unusedRow->Add<UiDropdown>([usage]() {
				for (const MarkOption& option : MarkOptions) {
					if (option.Mark == usage->Mark) {
						return String(option.Label);
					}
				}
				return String();
			}, [usage](SmallVectorImpl<UiMenuEntry>& entries) {
				for (const MarkOption& option : MarkOptions) {
					PaletteUsage::Marks mark = option.Mark;
					UiMenuEntry entry;
					entry.Label = option.Label;
					entry.IsChecked = [usage, mark]() { return (usage->Mark == mark); };
					entry.Execute = [usage, mark]() { usage->Mark = mark; };
					entries.push_back(Death::move(entry));
				}
			});
			marks->Flex = 1.0f;
			marks->Tooltip = _x("editor", "Cross out the entries no tile of the level uses, those the sprites share, or those that differ from the palette of the tile set");
			// TRANSLATORS: The button that picks the next entry of the palette no tile of the level uses, keep it short
			auto* nextUnused = unusedRow->Add<UiButton>(_x("editor", "Next"), EditorIcon::ChevronRight, [s, selection, usage]() {
				UpdateUsage(*s, *usage);
				if (!CanTellUnused(*usage)) {
					s->ShowMessage(usage->UnreadTiles > 0 ? String(_x("editor", "Some tiles can't be read from their tile sets, so it can't be told which entries are unused"))
						: String(_x("editor", "The tiles of the level are true color ones, they don't use the palette")));
					return;
				}
				for (std::int32_t step = 1; step < ColorCount; step++) {
					std::int32_t index = (selection->Anchor + step) % ColorCount;
					if (index != 0 && usage->Pixels[index] == 0) {
						selection->Anchor = selection->End = index;
						usage->Mark = PaletteUsage::Marks::Unused;
						return;
					}
				}
				s->ShowMessage(_x("editor", "Every entry is used by the tiles of the level"));
			});
			nextUnused->Style = UiButtonStyle::Flat;
			nextUnused->Tooltip = _x("editor", "Pick the next entry no tile of the level uses - sprites may still use it");

			// The tiles that use the entry become the brush, so they can be found and painted
			// TRANSLATORS: Button in the Level Palette dialog, makes the tiles that use the picked entry of the palette the brush
			auto* findTiles = right->Add<UiButton>(_x("editor", "Tiles Using It"), EditorIcon::Search, [s, selection]() {
				FindTilesUsingEntry(*s, selection->Anchor);
			});
			findTiles->Style = UiButtonStyle::Flat;
			findTiles->Tooltip = _x("editor", "Make the tiles of the tile sets that use the entry the brush, the default tile set and the parts of extra ones the level takes");

			auto* lockSprites = right->Add<UiCheckBox>(_x("editor", "Keep the colors of sprites"), []() { return GetSpriteLock(); }, [](bool value) { GetSpriteLock() = value; });
			lockSprites->Tooltip = _x("editor", "The entries the sprites share with the level aren't changed nor moved, the sprites are drawn in the palette too");

			auto canMove = [selection]() { return (selection->First() >= 1); };
			auto* moveRow = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			auto* earlier = moveRow->Add<UiButton>(StringView(), EditorIcon::ChevronLeft, [s, selection, usage]() {
				if (MovePaletteEntries(*s, selection->First(), selection->Last(), -1, usage->KeepLook)) {
					selection->Anchor--;
					selection->End--;
				}
			});
			earlier->Style = UiButtonStyle::Flat;
			earlier->Tooltip = _x("editor", "Move the picked entries an entry earlier");
			earlier->IsEnabledFunc = [canMove, selection]() { return canMove() && selection->First() > 1; };
			auto* later = moveRow->Add<UiButton>(StringView(), EditorIcon::ChevronRight, [s, selection, usage]() {
				if (MovePaletteEntries(*s, selection->First(), selection->Last(), 1, usage->KeepLook)) {
					selection->Anchor++;
					selection->End++;
				}
			});
			later->Style = UiButtonStyle::Flat;
			later->Tooltip = _x("editor", "Move the picked entries an entry later");
			later->IsEnabledFunc = [canMove, selection]() { return canMove() && selection->Last() < ColorCount - 1; };
			// TRANSLATORS: Button in the Level Palette dialog, swaps the first and the last picked entry of the palette (verb)
			auto* swap = moveRow->Add<UiButton>(_x("editor", "Swap"), EditorIcon::None, [s, selection, usage]() {
				std::int32_t first = selection->First(), last = selection->Last();
				std::uint8_t permutation[ColorCount];
				for (std::int32_t i = 0; i < ColorCount; i++) {
					permutation[i] = std::uint8_t(i == first ? last : (i == last ? first : i));
				}
				// TRANSLATORS: Name of an undo step in the history, two entries of the palette of the level were swapped
				PermutePalette(*s, permutation, usage->KeepLook, _x("editor", "Swap palette entries"));
			});
			swap->Style = UiButtonStyle::Flat;
			swap->Flex = 1.0f;
			swap->Tooltip = _x("editor", "Swap the first and the last picked entry, Shift+click picks the other one");
			swap->IsEnabledFunc = [canMove, selection]() { return canMove() && selection->First() != selection->Last(); };
			auto* keepLook = right->Add<UiCheckBox>(_x("editor", "Keep the look of the level"), [usage]() { return usage->KeepLook; }, [usage](bool value) { usage->KeepLook = value; });
			keepLook->Tooltip = _x("editor", "Moved colors take their pixels with them, the tiles of the default tile set the level shows are changed in this level for it - "
				"otherwise the pixels show other colors. Sprites and tiles of the default tile set placed later show the moved colors");
		}

		// What is done with the whole range
		{
			// The colors between the ends of the range, seen before they're made
			auto* preview = right->Add<GradientPreview>(session, selection, usage);
			preview->IsVisibleFunc = [selection]() { return (selection->Last() - selection->First() >= 2); };
			auto* gradientRow = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			struct StepOption { String Label; PaletteUsage::Steps Steps; };
			static const StepOption StepOptions[] = {
				// TRANSLATORS: Options of the dropdown in the Level Palette dialog, how the colors of a gradient are stepped (evenly as they look, by red, green and blue, or around the color wheel)
				{ _x("editor", "As they look"), PaletteUsage::Steps::OkLab },
				{ _x("editor", "Red, green, blue"), PaletteUsage::Steps::Rgb },
				{ _x("editor", "Around the hue"), PaletteUsage::Steps::Hue },
			};
			auto* steps = gradientRow->Add<UiDropdown>([usage]() {
				for (const StepOption& option : StepOptions) {
					if (option.Steps == usage->Gradient) {
						return String(option.Label);
					}
				}
				return String();
			}, [usage](SmallVectorImpl<UiMenuEntry>& entries) {
				for (const StepOption& option : StepOptions) {
					PaletteUsage::Steps value = option.Steps;
					UiMenuEntry entry;
					entry.Label = option.Label;
					entry.IsChecked = [usage, value]() { return (usage->Gradient == value); };
					entry.Execute = [usage, value]() { usage->Gradient = value; };
					entries.push_back(Death::move(entry));
				}
			});
			steps->Flex = 1.0f;
			steps->Tooltip = _x("editor", "Even steps of how the colors look, of their red, green and blue, or around the color wheel");
			// TRANSLATORS: Button in the Level Palette dialog, fills the picked entries of the palette with a gradient
			auto* gradient = gradientRow->Add<UiButton>(_x("editor", "Gradient"), EditorIcon::None, [s, selection, usage]() {
				std::int32_t first = selection->First(), last = selection->Last();
				PaletteUsage::Steps mode = usage->Gradient;
				// TRANSLATORS: Name of an undo step in the history, entries of the palette of the level were filled with a gradient
				ChangePalette(*s, _x("editor", "Palette gradient"), [first, last, mode](std::uint32_t* palette) {
					std::uint32_t result[ColorCount];
					MakeGradient(mode, palette, first, last, result);
					for (std::int32_t i = first; i <= last; i++) {
						palette[i] = result[i];
					}
				}, false);
			});
			gradient->Tooltip = _x("editor", "Fill the picked entries with even steps from the color of the first to the last");
			gradient->IsEnabledFunc = [s, selection]() { return UsesOwnPalette(*s) && selection->Last() - selection->First() >= 2; };

			// TRANSLATORS: Button in the Level Palette dialog, gives the picked entries the colors of the palette of the tile set
			auto* reset = right->Add<UiButton>(_x("editor", "From Tile Set"), EditorIcon::Undo, [s, selection]() {
				std::uint32_t tileSetPalette[ColorCount];
				if (!s->GetTileSetLibrary().ReadPalette(s->GetDocument().Properties.DefaultTileSet, tileSetPalette)) {
					s->ShowMessage(_x("editor", "The palette of the tile set can't be read"));
					return;
				}
				std::int32_t first = selection->First(), last = selection->Last();
				// TRANSLATORS: Name of an undo step in the history, entries of the palette of the level got the colors of the tile set
				ChangePalette(*s, _x("editor", "Palette from tile set"), [first, last, &tileSetPalette](std::uint32_t* palette) {
					for (std::int32_t i = first; i <= last; i++) {
						palette[i] = tileSetPalette[i];
					}
				}, false);
			});
			reset->Tooltip = _x("editor", "Give the picked entries the colors of the palette of the default tile set");
			reset->IsEnabledFunc = [s]() { return UsesOwnPalette(*s); };
			// How the palette of the level differs from that of its tile set, "Mark changed" crosses those entries out
			auto* differences = right->Add<UiLabel>(StringView(), UiTheme::TextDim);
			differences->TextFunc = [s, usage]() {
				const std::uint32_t* tileSetPalette = GetTileSetPalette(*s, *usage);
				if (tileSetPalette == nullptr || !UsesOwnPalette(*s)) {
					return String();
				}
				const std::uint32_t* palette = GetShownPalette(*s);
				std::int32_t count = 0;
				for (std::int32_t i = 1; i < ColorCount; i++) {
					count += ((palette[i] & 0x00FFFFFFu) != (tileSetPalette[i] & 0x00FFFFFFu) ? 1 : 0);
				}
				return (count == 0 ? String(_x("editor", "The same as the palette of the tile set"))
					: String(_fnx("editor", "{} entry differs from the tile set", "{} entries differ from the tile set", count, count)));
			};
			// The other way, the tile set gets the palette of the level, other levels that use it then have it too
			// TRANSLATORS: Button in the Level Palette dialog, saves the palette of the level into its tile set
			auto* save = right->Add<UiButton>(_x("editor", "Into Tile Set"), EditorIcon::Save, [s]() {
				ShowApplyTileChangesDialog(*s, TileRef::DefaultTileSet);
			});
			save->Tooltip = _x("editor", "Save the palette of the level into its default tile set, or a new one");
			save->IsEnabledFunc = [s]() { return UsesOwnPalette(*s); };

			// To and from other programs
			auto* files = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			// TRANSLATORS: Button in the Level Palette dialog, reads a palette from a file (verb)
			auto* import = files->Add<UiButton>(_x("editor", "Import"), EditorIcon::None, [s]() {
				SmallVector<UiFileRoot, 4> roots;
				s->GetImageRoots(false, roots);
				// TRANSLATORS: Title of the file dialog that reads a palette from a file
				UiFileDialog::Show(*s->GetSurface(), _x("editor", "Import Palette"), UiFileDialogMode::Open, Death::move(roots), ".pal;.gpl;.act;.png"_s, {}, {}, [s](StringView path) {
					// The colors the file has replace those of the level, the first entry stays transparent
					std::uint32_t palette[ColorCount];
					std::memcpy(palette, GetShownPalette(*s), sizeof(palette));
					std::uint32_t transparent = palette[0];
					String error;
					std::int32_t count = PaletteFile::Read(path, palette, &error);
					if (count <= 0) {
						s->ShowMessage(_fx("editor", "The palette can't be read: {}", error));
						return;
					}
					palette[0] = transparent;
					KeepSpriteColors(GetShownPalette(*s), palette);
					// TRANSLATORS: Name of an undo step in the history, the palette of the level was read from a file
					s->Execute(std::make_unique<SetLevelPaletteCommand>(s->GetDocument(), true, palette, _x("editor", "Import palette")));
					s->ShowMessage(_fnx("editor", "{} color was imported, the level has its own palette now", "{} colors were imported, the level has its own palette now", count, count));
				});
			});
			import->Flex = 1.0f;
			import->Tooltip = _x("editor", "Take the colors of a palette of another program, JASC (.pal), GIMP (.gpl) or Adobe (.act), or of an image (.png)");
			// TRANSLATORS: Button in the Level Palette dialog, saves the palette into a file of another program (verb)
			auto* exportButton = files->Add<UiButton>(_x("editor", "Export"), EditorIcon::None, [s]() {
				SmallVector<UiFileRoot, 4> roots;
				s->GetImageRoots(true, roots);
				// The name of the level without its episode, a level that wasn't saved yet has none
				StringView levelName = s->GetLevelName();
				std::size_t slash = levelName.size();
				while (slash > 0 && levelName[slash - 1] != '/' && levelName[slash - 1] != '\\') {
					slash--;
				}
				StringView baseName = levelName.exceptPrefix(slash);
				String name = (baseName.empty() ? String("palette"_s) : String(baseName)) + ".pal"_s;
				// TRANSLATORS: Title of the file dialog that saves the palette into a file
				UiFileDialog::Show(*s->GetSurface(), _x("editor", "Export Palette"), UiFileDialogMode::Save, Death::move(roots), ".pal;.gpl;.act;.png"_s, {}, name, [s](StringView path) {
					std::uint32_t palette[ColorCount];
					std::memcpy(palette, GetShownPalette(*s), sizeof(palette));
					if (!PaletteFile::Write(path, palette, s->GetDocument().Properties.DisplayName)) {
						s->ShowMessage(_x("editor", "The palette couldn't be saved"));
						return;
					}
					// TRANSLATORS: Status message after the palette was saved into a file
					s->ShowMessage(_x("editor", "Exported"));
				});
			});
			exportButton->Flex = 1.0f;
			exportButton->Tooltip = _x("editor", "Save the palette the level is shown in for another program, by the extension of its name");
		}

		// The level is seen behind it, in the colors as they change
		ShowModeless(*surface, Death::move(dialog));
	}
}

#endif
