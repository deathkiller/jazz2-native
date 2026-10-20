#include "TileSetApplyDialog.h"
#include "TileSetsDialog.h"

#if defined(WITH_EDITOR)

#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../Common/TileSource.h"
#include "../Document/Commands.h"
#include "../Files/AtomicFile.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Imaging/PaletteMatch.h"
#include "../Imaging/TileMasks.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Formats/TileSetFile.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cstring>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		struct ApplyState
		{
			std::int32_t Source = -1;
			bool IntoItself = false;
			bool WithPalette = true;
		};

		// The tile set of a source, and where its tiles start in the file
		bool GetSourceTileSet(const LevelDocument& document, std::uint8_t source, String& name, std::int32_t& offset,
			const Formats::ExtraTileSetRecord*& extra)
		{
			TileSource resolved;
			if (!ResolveTileSource(document, source, resolved)) {
				extra = nullptr;
				return false;
			}
			name = resolved.Name;
			offset = resolved.Offset;
			extra = resolved.Extra;
			// The default tile set has to have a name, an extra one has its record
			return (extra != nullptr || !name.empty());
		}

		// A name for the new tile set that isn't taken yet
		String MakeNewName(const LevelDocument& document, std::uint8_t source)
		{
			String name;
			std::int32_t offset;
			const Formats::ExtraTileSetRecord* extra;
			if (IsLevelTileSet(document, source) || !GetSourceTileSet(document, source, name, offset, extra)) {
				name = "level"_s;
			}
			return SuggestTileSetName(name, "-edited"_s);
		}

		// Indices of the level palette as they're stored in the file of an extra tile set remapped to it: the first
		// index of the file that becomes it, otherwise the one of the closest color
		void MakeInverseRemapping(const Formats::ExtraTileSetRecord& extra, const std::uint32_t* palette, std::uint8_t (&inverse)[256])
		{
			bool found[256] = {};
			for (std::int32_t f = 255; f >= 1; f--) {
				inverse[extra.Remapping[f]] = std::uint8_t(f);
				found[extra.Remapping[f]] = true;
			}
			inverse[0] = 0;
			// The colors the indices of the file show, the lowest one of equally close ones is taken
			std::uint32_t filePalette[256];
			for (std::int32_t f = 0; f < 256; f++) {
				filePalette[f] = palette[extra.Remapping[f]];
			}
			PaletteSearch search(filePalette);
			for (std::int32_t i = 1; i < 256; i++) {
				if (!found[i]) {
					inverse[i] = search.FindNearest(OkLab::FromPaletteEntry(palette[i]));
				}
			}
		}
	}

	std::int32_t CountChangedTiles(IEditorContext& session, std::uint8_t source)
	{
		const LevelDocument& document = session.GetDocument();
		SmallVector<std::uint16_t, 0> tiles;
		for (const Formats::TileDiffuseOverride& tile : document.DiffuseOverrides) {
			if (document.FromTileId(tile.TileId, 0).Source == source) {
				tiles.push_back(tile.TileId);
			}
		}
		for (const Formats::TileMaskOverride& tile : document.MaskOverrides) {
			if (document.FromTileId(tile.TileId, 0).Source == source) {
				tiles.push_back(tile.TileId);
			}
		}
		// A tile with both its pixels and its mask changed counts once
		std::sort(tiles.begin(), tiles.end());
		return std::int32_t(std::unique(tiles.begin(), tiles.end()) - tiles.begin());
	}

	static bool HasLevelPalette(IEditorContext& session)
	{
		return ((session.GetDocument().Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette);
	}

	bool ApplyTileChanges(IEditorContext& session, std::uint8_t source, StringView newName, bool withPalette)
	{
		LevelDocument& document = session.GetDocument();
		String name;
		std::int32_t offset;
		const Formats::ExtraTileSetRecord* extra;
		if (!GetSourceTileSet(document, source, name, offset, extra)) {
			return false;
		}
		bool intoItself = newName.empty();
		String targetName = (intoItself ? name : String(newName));
		if (!intoItself) {
			if (!ContentTileSetLibrary::IsValidName(targetName)) {
				session.ShowMessage(_("A tile set is named by lowercase letters, digits, dashes and underscores"));
				return false;
			}
			if (!ContentTileSetLibrary::FindTileSetFile(targetName).empty()) {
				session.ShowMessage(_("A tile set of this name exists already"));
				return false;
			}
		}

		Formats::TileSetData tileSet;
		if (!ReadTileSetOf(session, source, tileSet)) {
			session.ShowMessage(_("The tile set can't be read"));
			return false;
		}
		// The tiles the level stores in itself are changed there, a new tile set gets a file of its own
		bool embedded = IsLevelTileSet(document, source);
		bool intoLevel = (embedded && intoItself);

		// The level shows its changed tiles in the colors of the palette in use. The default tile set is drawn in them,
		// and so is an extra one that isn't remapped, its indices stay as they are. A remapped one gets the indices of
		// its own file back.
		const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
		std::uint8_t toFile[256];
		for (std::int32_t i = 0; i < 256; i++) {
			toFile[i] = std::uint8_t(i);
		}
		bool remapped = (extra != nullptr && (extra->Flags & Formats::ExtraTileSetRecord::Remapped) != 0);
		if (remapped && (extra->Flags & Formats::ExtraTileSetRecord::TrueColor) == 0) {
			MakeInverseRemapping(*extra, palette, toFile);
		} else if (remapped) {
			// An alternate palette, the closest colors of the palette of the file
			std::uint32_t inUse[256];
			std::memcpy(inUse, palette, sizeof(inUse));
			MatchPalette(inUse, tileSet.Palette, toFile);
		}

		SmallVector<std::uint16_t, 0> applied;
		for (const Formats::TileDiffuseOverride& tile : document.DiffuseOverrides) {
			TileRef ref = document.FromTileId(tile.TileId, 0);
			std::int32_t fileTile = offset + ref.Index;
			if (ref.Source != source || fileTile >= tileSet.TileCount) {
				continue;
			}
			std::uint8_t* image = &tileSet.Images[std::size_t(fileTile) * Formats::TileSetData::ImageBytesPerTile];
			if (tileSet.TrueColor[fileTile] != 0) {
				// A true color tile keeps being one, in the colors the level showed
				for (std::int32_t p = 0; p < 32 * 32; p++) {
					std::uint32_t color = (tile.Indices[p] == 0 ? 0u : (palette[tile.Indices[p]] | 0xFF000000u));
					image[p * 4] = std::uint8_t(color);
					image[p * 4 + 1] = std::uint8_t(color >> 8);
					image[p * 4 + 2] = std::uint8_t(color >> 16);
					image[p * 4 + 3] = std::uint8_t(color >> 24);
				}
			} else {
				for (std::int32_t p = 0; p < 32 * 32; p++) {
					image[p] = toFile[tile.Indices[p]];
				}
			}
			applied.push_back(tile.TileId);
		}
		for (const Formats::TileMaskOverride& tile : document.MaskOverrides) {
			TileRef ref = document.FromTileId(tile.TileId, 0);
			std::int32_t fileTile = offset + ref.Index;
			if (ref.Source != source || fileTile >= tileSet.TileCount) {
				continue;
			}
			// One byte a pixel in the level, one bit in the tile set
			TileMasks::Pack(tile.Mask, &tileSet.Masks[std::size_t(fileTile) * Formats::TileSetData::MaskBytesPerTile]);
			applied.push_back(tile.TileId);
		}

		// The palette of the level becomes the palette of its default tile set, the tiles of which it colors
		bool paletteApplied = (withPalette && source == TileRef::DefaultTileSet && HasLevelPalette(session));
		if (paletteApplied) {
			std::memcpy(tileSet.Palette, document.LevelPalette, sizeof(tileSet.Palette));
			tileSet.Palette[0] &= 0x00FFFFFFu;
		}
		if (applied.empty() && !paletteApplied) {
			session.ShowMessage(_("This level doesn't change any tile of the tile set"));
			return false;
		}

		SmallVector<std::uint8_t, 0> bytes;
		String path;
		if (intoLevel) {
			if (!SerializeTileSet(tileSet, bytes)) {
				session.ShowMessage(_("The tile set couldn't be saved"));
				return false;
			}
		} else {
			// The name of the tile set comes from the level, it must not lead out of where tile sets are saved to
			if (!Formats::LevelFile::IsSafeTileSetPath(targetName)) {
				session.ShowMessage(_("The tile set has a name that can't be saved"));
				return false;
			}
			path = ContentTileSetLibrary::GetWritablePath(targetName, true);
			if (path.empty()) {
				session.ShowMessage(_("There's no place to save tile sets to"));
				return false;
			}
			// Written next to it first, so a tile set that can't be saved whole isn't lost, the one written over is kept
			// as a backup, undo doesn't change the file back
			if (!AtomicFile::Write(path, [&tileSet](Stream& s) { return Formats::TileSetFile::Write(s, tileSet); }, intoItself)) {
				session.ShowMessage(_("The tile set couldn't be saved"));
				return false;
			}
			session.InvalidateTileSet(targetName);
		}
		// A tile with both its pixels and its mask changed is there twice
		std::sort(applied.begin(), applied.end());
		applied.erase(std::unique(applied.begin(), applied.end()), applied.end());
		std::int32_t tiles = std::int32_t(applied.size());
		LOGI("Tiles changed in the level saved into \"{}\" ({} tiles)", intoLevel ? StringView(name) : StringView(path), tiles);

		// The level uses the new tile set instead, the same tiles at the same places, and doesn't change them anymore
		SnapshotParts parts = SnapshotParts::TileSets;
		if ((!intoItself && source == TileRef::DefaultTileSet) || paletteApplied) {
			parts |= SnapshotParts::Properties;
		}
		session.RecordSnapshotStep(_("Save tile changes into tile set"), parts, [&](LevelDocument& document) {
			if (intoLevel) {
				document.ExtraTileSets[std::size_t(source) - 1].Data = Death::move(bytes);
			} else if (!intoItself) {
				if (source == TileRef::DefaultTileSet) {
					document.Properties.DefaultTileSet = targetName;
				} else {
					// The tiles the level had in itself are in the file now
					Formats::ExtraTileSetRecord& record = document.ExtraTileSets[std::size_t(source) - 1];
					record.Name = targetName;
					record.Flags &= ~Formats::ExtraTileSetRecord::Embedded;
					record.Data.clear();
				}
			}
			if (paletteApplied) {
				// The tile set has it now
				document.Flags = (document.Flags & ~LevelFlags::UseLevelPalette);
			}
			auto isApplied = [&applied](std::uint16_t tileId) {
				return std::binary_search(applied.begin(), applied.end(), tileId);
			};
			auto& diffuse = document.DiffuseOverrides;
			diffuse.erase(std::remove_if(diffuse.begin(), diffuse.end(), [&](const Formats::TileDiffuseOverride& tile) { return isApplied(tile.TileId); }), diffuse.end());
			auto& masks = document.MaskOverrides;
			masks.erase(std::remove_if(masks.begin(), masks.end(), [&](const Formats::TileMaskOverride& tile) { return isApplied(tile.TileId); }), masks.end());
			return true;
		});

		if (tiles == 0) {
			session.ShowMessage(_f("The palette of the level was saved into tile set \"{}\"", targetName));
		} else {
			String message = _fn("{} changed tile was saved into tile set \"{}\"", "{} changed tiles were saved into tile set \"{}\"", tiles, tiles, targetName);
			if (paletteApplied) {
				message += " "_s + _("- and the palette of the level");
			}
			session.ShowMessage(message);
		}
		return true;
	}

	void ShowApplyTileChangesDialog(IEditorContext& session, std::int32_t source)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		LevelDocument& document = session.GetDocument();
		auto state = std::make_shared<ApplyState>();
		std::int32_t sourceCount = 1 + std::min<std::int32_t>(std::int32_t(document.ExtraTileSets.size()), TileRef::LastExtraTileSet);
		// The default tile set can take the palette of the level too, even with no tile changed
		bool levelPalette = HasLevelPalette(session);
		auto hasChanges = [&session, levelPalette](std::int32_t source) {
			return (CountChangedTiles(session, std::uint8_t(source)) > 0 || (source == TileRef::DefaultTileSet && levelPalette));
		};
		if (source >= 0 && source < sourceCount && hasChanges(source)) {
			state->Source = source;
		} else {
			for (std::int32_t i = 0; i < sourceCount && state->Source < 0; i++) {
				if (hasChanges(i)) {
					state->Source = i;
				}
			}
		}
		if (state->Source < 0) {
			session.ShowMessage(_("This level doesn't change any tile or have a palette of its own"));
			return;
		}

		IEditorContext* s = &session;
		auto sourceName = [s](std::int32_t source) {
			String name;
			std::int32_t offset;
			const Formats::ExtraTileSetRecord* extra;
			GetSourceTileSet(s->GetDocument(), std::uint8_t(source), name, offset, extra);
			return GetTileSetLabel(name);
		};

		auto dialog = std::make_unique<UiDialog>(_("Save Tile Changes into Tile Set"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent()->Add<UiForm>(UiTheme::Spacing);
		content->Add<UiLabel>(_("The tiles changed in this level become tiles of a tile set,"), UiTheme::TextDim);
		content->Add<UiLabel>(_("the level doesn't change them anymore."), UiTheme::TextDim);
		// The name of the new tile set follows the tile set chosen
		auto nameHolder = std::make_shared<UiTextBox*>(nullptr);
		{
			auto* row = content->AddRow(_("Tiles of"));
			auto* dropdown = row->Add<UiDropdown>([state, sourceName]() {
				return sourceName(state->Source);
			}, [s, state, sourceName, sourceCount, nameHolder](SmallVectorImpl<UiMenuEntry>& entries) {
				for (std::int32_t i = 0; i < sourceCount; i++) {
					std::int32_t changed = CountChangedTiles(*s, std::uint8_t(i));
					if (changed <= 0 && !(i == TileRef::DefaultTileSet && HasLevelPalette(*s))) {
						continue;
					}
					UiMenuEntry entry;
					entry.Label = sourceName(i) + " ("_s + _fn("{} changed tile", "{} changed tiles", changed, changed) + ")"_s;
					entry.IsChecked = [state, i]() { return (state->Source == i); };
					entry.Execute = [s, state, i, nameHolder]() {
						if (state->Source != i && *nameHolder != nullptr) {
							(*nameHolder)->Text = MakeNewName(s->GetDocument(), std::uint8_t(i));
						}
						state->Source = i;
					};
					entries.push_back(Death::move(entry));
				}
			});
			dropdown->Width = 160.0f;
			auto* count = row->Add<UiLabel>(StringView(), UiTheme::TextDim);
			// Counted again only when the level or the tile set chosen changes, not every frame
			struct CountCache
			{
				std::uint32_t Revision = ~0u;
				std::int32_t Source = -1;
				String Text;
			};
			auto cache = std::make_shared<CountCache>();
			count->TextFunc = [s, state, cache]() {
				std::uint32_t revision = s->GetHistory().GetRevision();
				if (cache->Revision != revision || cache->Source != state->Source) {
					cache->Revision = revision;
					cache->Source = state->Source;
					std::int32_t changed = CountChangedTiles(*s, std::uint8_t(state->Source));
					cache->Text = _fn("{} changed tile", "{} changed tiles", changed, changed);
				}
				return cache->Text;
			};
		}
		{
			auto* withPalette = content->Add<UiCheckBox>(_("With the palette of the level"), [state]() { return state->WithPalette; },
				[state](bool value) { state->WithPalette = value; });
			withPalette->Tooltip = _("It becomes the palette of the tile set, the level then doesn't have its own");
			withPalette->IsVisibleFunc = [s, state]() { return (state->Source == TileRef::DefaultTileSet && HasLevelPalette(*s)); };
		}
		UiTextBox* nameBox;
		{
			content->Add<UiCheckBox>(_("Into a new tile set, this level then uses it instead"), [state]() { return !state->IntoItself; },
				[state](bool value) { state->IntoItself = !value; });
			auto* row = content->AddRow(_("Name"));
			nameBox = row->Add<UiTextBox>(MakeNewName(document, std::uint8_t(state->Source)));
			*nameHolder = nameBox;
			nameBox->Width = 160.0f;
			nameBox->MaxLength = 32;
			nameBox->Tooltip = _("The tile set is saved in the user content, Tilesets/<name>.j2t");
			nameBox->IsEnabledFunc = [state]() { return !state->IntoItself; };
		}
		{
			auto* warning = content->Add<UiLabel>(StringView(), UiTheme::TextAccent);
			warning->TextFunc = [s, state]() {
				return (state->IntoItself && !IsLevelTileSet(s->GetDocument(), std::uint8_t(state->Source))
					? String(_("Every level that uses the tile set shows the changes")) : String());
			};
			// Its own copy in the user content is written over, any other one is covered by a copy there
			auto* copy = content->Add<UiLabel>(StringView(), UiTheme::TextAccent);
			copy->TextFunc = [s, state, sourceName]() {
				if (IsLevelTileSet(s->GetDocument(), std::uint8_t(state->Source))) {
					return String();
				}
				String name = sourceName(state->Source);
				return (state->IntoItself && !IsOwnTileSetCopy(name) ? String(_("- a copy in the user content is used instead of it")) : String());
			};
		}

		dialog->AddButton(_("Save"), [s, state, nameBox, dialogPtr]() {
			String newName = (state->IntoItself ? String() : String(nameBox->Text.trimmed()));
			if (ApplyTileChanges(*s, std::uint8_t(state->Source), newName, state->WithPalette)) {
				dialogPtr->Close();
			}
		}, true);
		dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });

		ShowModal(*surface, Death::move(dialog));
	}
}

#endif
