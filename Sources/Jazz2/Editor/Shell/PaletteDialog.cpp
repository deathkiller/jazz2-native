#include "PaletteDialog.h"
#include "TileSetApplyDialog.h"
#include "TileSetsDialog.h"
#include "../Imaging/PaletteFile.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../Document/Commands.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Imaging/PaletteMatch.h"
#include "../UI/UiColors.h"
#include "../UI/UiDialogs.h"
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

		bool UsesOwnPalette(EditorSession& session)
		{
			return (session.GetDocument().Flags & LevelFlags::UseLevelPalette) == LevelFlags::UseLevelPalette;
		}

		// The palette the level is shown in, its own or that of its tile set
		const std::uint32_t* GetShownPalette(EditorSession& session)
		{
			return (UsesOwnPalette(session) ? session.GetDocument().LevelPalette : ContentResolver::Get().GetPalettes().data());
		}

		// Changes the colors of the own palette of the level as an undoable step, consecutive changes are merged
		void ChangePalette(EditorSession& session, StringView name, Function<void(std::uint32_t*)>&& change, bool allowMerge = true)
		{
			if (!UsesOwnPalette(session)) {
				return;
			}
			std::uint32_t palette[ColorCount];
			std::memcpy(palette, session.GetDocument().LevelPalette, sizeof(palette));
			change(palette);
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
			auto toByte = [](float c) { return std::uint32_t(std::clamp(std::int32_t(std::round(c * 255.0f)), 0, 255)); };
			return toByte(r) | (toByte(g) << 8) | (toByte(b) << 16) | (alpha & 0xFF000000u);
		}

		String FormatHex(std::uint32_t entry)
		{
			char buffer[16];
			std::int32_t length = std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", entry & 0xFF, (entry >> 8) & 0xFF, (entry >> 16) & 0xFF);
			return String(buffer, std::size_t(std::max(length, 0)));
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
			std::uint32_t Revision = ~0u;
			bool ShowUnused = false;
			// A move of entries changes the pixels too, so the level looks the same
			bool KeepLook = true;
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
		bool GetTileIndices(EditorSession& session, const TileRef& tile, std::uint8_t* indices)
		{
			LevelDocument& document = session.GetDocument();
			std::int32_t tileId = document.ToTileId(tile);
			if (tileId > 0 && tileId <= UINT16_MAX) {
				if (const Formats::TileDiffuseOverride* diffuse = SetTileOverrideCommand::FindDiffuse(document, std::uint16_t(tileId))) {
					std::memcpy(indices, diffuse->Indices, sizeof(diffuse->Indices));
					return true;
				}
			}
			StringView name;
			std::int32_t index = tile.Index;
			const std::uint8_t* remapping = nullptr;
			if (tile.Source == TileRef::DefaultTileSet) {
				name = document.Properties.DefaultTileSet;
			} else if (tile.Source <= document.ExtraTileSets.size()) {
				const Formats::ExtraTileSetRecord& tileSet = document.ExtraTileSets[tile.Source - 1];
				if ((tileSet.Flags & Formats::ExtraTileSetRecord::TrueColor) != 0) {
					return false;
				}
				name = tileSet.Name;
				index += tileSet.Offset;
				if ((tileSet.Flags & Formats::ExtraTileSetRecord::Remapped) != 0) {
					remapping = tileSet.Remapping;
				}
			} else {
				return false;
			}
			const Formats::TileSetData* data = session.GetTileSetData(name);
			if (data == nullptr || index < 0 || index >= data->TileCount || data->TrueColor[index] != 0) {
				return false;
			}
			const std::uint8_t* image = &data->Images[std::size_t(index) * Formats::TileSetData::ImageBytesPerTile];
			for (std::int32_t i = 0; i < Formats::LevelFile::TileSize * Formats::LevelFile::TileSize; i++) {
				indices[i] = (remapping != nullptr ? remapping[image[i]] : image[i]);
			}
			return true;
		}

		void UpdateUsage(EditorSession& session, PaletteUsage& usage)
		{
			std::uint32_t revision = session.GetHistory().GetRevision();
			if (usage.Revision == revision) {
				return;
			}
			usage.Revision = revision;
			std::memset(usage.Pixels, 0, sizeof(usage.Pixels));
			usage.Tiles = 0;
			SmallVector<TileRef, 0> tiles;
			CollectUsedTiles(session.GetDocument(), tiles);
			std::uint8_t indices[Formats::LevelFile::TileSize * Formats::LevelFile::TileSize];
			for (const TileRef& tile : tiles) {
				if (!GetTileIndices(session, tile, indices)) {
					continue;
				}
				usage.Tiles++;
				for (std::uint8_t index : indices) {
					usage.Pixels[index]++;
				}
			}
		}

		/**
		 * Moves entries of the palette, @p permutation gives the new place of each entry, as one step
		 *
		 * With @p keepLook the pixels of every 8-bit tile the level shows go with their colors, so the level looks the
		 * same, the tiles of tile sets are overridden for it, the tiles of the level are changed.
		 */
		void PermutePalette(EditorSession& session, const std::uint8_t* permutation, bool keepLook, StringView name)
		{
			LevelDocument& document = session.GetDocument();
			CommandHistory& history = session.GetHistory();
			history.BeginTransaction(String(name));

			// The palette the level is shown in, which becomes its own if it wasn't
			const std::uint32_t* shown = GetShownPalette(session);
			std::uint32_t palette[ColorCount];
			for (std::int32_t i = 0; i < ColorCount; i++) {
				palette[permutation[i]] = shown[i];
			}
			session.Execute(std::make_unique<SetLevelPaletteCommand>(document, true, palette, name));

			std::int32_t changedTiles = 0;
			if (keepLook) {
				constexpr std::int32_t PixelCount = Formats::LevelFile::TileSize * Formats::LevelFile::TileSize;
				SmallVector<TileRef, 0> tiles;
				CollectUsedTiles(document, tiles);
				for (const TileRef& tile : tiles) {
					if (tile.Source != TileRef::DefaultTileSet && tile.Source <= document.ExtraTileSets.size() &&
						(document.ExtraTileSets[tile.Source - 1].Flags & Formats::ExtraTileSetRecord::Embedded) != 0) {
						// The tiles of the level are changed below, all at once
						continue;
					}
					std::uint8_t indices[PixelCount];
					if (!GetTileIndices(session, tile, indices)) {
						continue;
					}
					std::uint8_t mapped[PixelCount];
					bool changed = false;
					for (std::int32_t i = 0; i < PixelCount; i++) {
						mapped[i] = permutation[indices[i]];
						changed |= (mapped[i] != indices[i]);
					}
					if (!changed) {
						continue;
					}
					std::uint16_t tileId = std::uint16_t(document.ToTileId(tile));
					const Formats::TileMaskOverride* mask = SetTileOverrideCommand::FindMask(document, tileId);
					session.Execute(std::make_unique<SetTileOverrideCommand>(document, tileId, mapped, mask != nullptr ? mask->Mask : nullptr, name));
					changedTiles++;
				}
				// The tile sets the level has in itself, their 8-bit tiles
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
					for (std::int32_t i = 0; i < copy.TileCount; i++) {
						if (copy.TrueColor[i] != 0) {
							continue;
						}
						std::uint8_t* image = &copy.Images[std::size_t(i) * Formats::TileSetData::ImageBytesPerTile];
						for (std::int32_t p = 0; p < PixelCount; p++) {
							image[p] = permutation[image[p]];
						}
					}
					SmallVector<std::uint8_t, 0> bytes;
					if (SerializeTileSet(copy, bytes)) {
						session.Execute(std::make_unique<SetLevelTilesCommand>(document, slot, std::move(bytes), name));
						changedTiles++;
					}
				}
			}
			history.EndTransaction();
			if (keepLook && changedTiles > 0) {
				session.ShowMessage(_fn("The pixels of {} tile went with their colors", "The pixels of {} tiles went with their colors", changedTiles, changedTiles));
			}
		}

		// A number of the color of the edited entry, its value is a part of the entry
		UiNumberBox* AddChannel(UiStack* row, EditorSession& session, std::shared_ptr<PaletteSelection> selection, StringView label,
			float max, Function<float(std::uint32_t)>&& get, Function<std::uint32_t(std::uint32_t, float)>&& set)
		{
			EditorSession* s = &session;
			row->Add<UiLabel>(label, UiTheme::TextDim);
			auto getShared = std::make_shared<Function<float(std::uint32_t)>>(std::move(get));
			auto setShared = std::make_shared<Function<std::uint32_t(std::uint32_t, float)>>(std::move(set));
			auto* number = row->Add<UiNumberBox>([s, selection, getShared]() {
				return (*getShared)(GetShownPalette(*s)[selection->Anchor]);
			}, [s, selection, setShared](float value, bool final) {
				std::int32_t index = selection->Anchor;
				ChangePalette(*s, _("Palette color"), [index, value, setShared](std::uint32_t* palette) {
					palette[index] = (*setShared)(palette[index], value);
				});
			}, 0.0f, max, 1.0f, 0);
			number->Flex = 1.0f;
			number->IsEnabledFunc = [s]() { return UsesOwnPalette(*s); };
			return number;
		}
	}

	void ShowLevelPaletteDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto selection = std::make_shared<PaletteSelection>();
		auto dialog = std::make_unique<UiDialog>(_("Level Palette"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();

		content->Add<UiCheckBox>(_("The level has its own palette"), [s]() {
			return UsesOwnPalette(*s);
		}, [s](bool value) {
			// It starts as a copy of the palette the level is shown in
			std::uint32_t palette[ColorCount];
			std::memcpy(palette, s->GetDocument().LevelPalette, sizeof(palette));
			if (value && std::all_of(palette, palette + ColorCount, [](std::uint32_t entry) { return entry == 0; })) {
				std::memcpy(palette, ContentResolver::Get().GetPalettes().data(), sizeof(palette));
			}
			s->Execute(std::make_unique<SetLevelPaletteCommand>(s->GetDocument(), value, palette,
				value ? _("Use own palette") : _("Use tile set palette")));
		})->Tooltip = _("Otherwise the level is shown in the palette of its default tile set");

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
			if (!usage->ShowUnused) {
				return false;
			}
			UpdateUsage(*s, *usage);
			return (index != 0 && usage->Pixels[index] == 0);
		};

		auto* right = columns->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		right->Width = 190.0f;
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
				UiColorPicker::ShowPopup(*surface, surface->GetPointerPos(), [s, index]() {
					Colorf color = UiPaletteGrid::ToColor(GetShownPalette(*s)[index]);
					color.A = 1.0f;
					return color;
				}, [s, index](const Colorf& color) {
					std::uint32_t rgb = std::uint32_t(std::round(color.R * 255.0f)) | (std::uint32_t(std::round(color.G * 255.0f)) << 8) |
						(std::uint32_t(std::round(color.B * 255.0f)) << 16);
					ChangePalette(*s, _("Palette color"), [index, rgb](std::uint32_t* palette) {
						palette[index] = (palette[index] & 0xFF000000u) | rgb;
					});
				});
			});
			swatch->IsEnabledFunc = [s]() { return UsesOwnPalette(*s); };
			swatch->Tooltip = _("Pick a color");
			swatch->Height = 32.0f;
			swatch->Width = 190.0f;
			auto* index = right->Add<UiLabel>(StringView(), UiTheme::TextDim);
			index->TextFunc = [selection]() {
				// The first entry picked is the one edited, the others take part in the gradient
				return (selection->First() == selection->Last()
					? _f("Entry {}", selection->Anchor)
					: _f("Entries {}–{}", selection->First(), selection->Last()));
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
					std::uint32_t byte = std::uint32_t(std::clamp(std::int32_t(std::round(value)), 0, 255));
					return (entry & ~(0xFFu << shift)) | (byte << shift);
				});
			}
		}
		{
			auto* row = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			// TRANSLATORS: Hue, saturation and value of a color, one letter each
			AddChannel(row, session, selection, _("H"), 359.0f, [](std::uint32_t entry) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return std::round(h);
			}, [](std::uint32_t entry, float value) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return FromHsv(value, sat, v, entry);
			});
			AddChannel(row, session, selection, _("S"), 100.0f, [](std::uint32_t entry) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return std::round(sat * 100.0f);
			}, [](std::uint32_t entry, float value) {
				float h, sat, v;
				ToHsv(entry, h, sat, v);
				return FromHsv(h, value / 100.0f, v, entry);
			});
			AddChannel(row, session, selection, _("V"), 100.0f, [](std::uint32_t entry) {
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
					s->ShowMessage(_("A color is written as #RRGGBB"));
					return;
				}
				std::int32_t index = selection->Anchor;
				ChangePalette(*s, _("Palette color"), [index, rgb](std::uint32_t* palette) {
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
				std::uint32_t pixels = usage->Pixels[selection->Anchor];
				return (pixels == 0 ? String(_("No tile uses it")) : String(_fn("{} pixel of tiles uses it", "{} pixels of tiles use it", pixels, pixels)));
			};
			auto* unusedRow = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			// TRANSLATORS: Entries of the palette no tile of the level uses, and the button that picks the next of them
			// is labeled "Next", keep both short
			auto* showUnused = unusedRow->Add<UiButton>(_("Unused"), EditorIcon::Search, [usage]() { usage->ShowUnused = !usage->ShowUnused; });
			showUnused->Style = UiButtonStyle::Flat;
			showUnused->Flex = 1.0f;
			showUnused->Tooltip = _("Cross out the entries no tile of the level uses - sprites may still use some of them");
			showUnused->IsCheckedFunc = [usage]() { return usage->ShowUnused; };
			auto* nextUnused = unusedRow->Add<UiButton>(_("Next"), EditorIcon::ChevronRight, [s, selection, usage]() {
				UpdateUsage(*s, *usage);
				for (std::int32_t step = 1; step < ColorCount; step++) {
					std::int32_t index = (selection->Anchor + step) % ColorCount;
					if (index != 0 && usage->Pixels[index] == 0) {
						selection->Anchor = selection->End = index;
						usage->ShowUnused = true;
						return;
					}
				}
				s->ShowMessage(_("Every entry is used by the tiles of the level"));
			});
			nextUnused->Style = UiButtonStyle::Flat;
			nextUnused->Flex = 1.0f;

			auto canMove = [selection]() { return (selection->First() >= 1); };
			auto* moveRow = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			auto* earlier = moveRow->Add<UiButton>(StringView(), EditorIcon::ChevronLeft, [s, selection, usage]() {
				std::int32_t first = selection->First(), last = selection->Last();
				if (first <= 1) {
					return;
				}
				// The entry before the range goes after it
				std::uint8_t permutation[ColorCount];
				for (std::int32_t i = 0; i < ColorCount; i++) {
					permutation[i] = std::uint8_t(i >= first && i <= last ? i - 1 : (i == first - 1 ? last : i));
				}
				PermutePalette(*s, permutation, usage->KeepLook, _("Move palette entries"));
				selection->Anchor--;
				selection->End--;
			});
			earlier->Style = UiButtonStyle::Flat;
			earlier->Tooltip = _("Move the picked entries an entry earlier");
			earlier->IsEnabledFunc = [canMove, selection]() { return canMove() && selection->First() > 1; };
			auto* later = moveRow->Add<UiButton>(StringView(), EditorIcon::ChevronRight, [s, selection, usage]() {
				std::int32_t first = selection->First(), last = selection->Last();
				if (last >= ColorCount - 1) {
					return;
				}
				// The entry after the range goes before it
				std::uint8_t permutation[ColorCount];
				for (std::int32_t i = 0; i < ColorCount; i++) {
					permutation[i] = std::uint8_t(i >= first && i <= last ? i + 1 : (i == last + 1 ? first : i));
				}
				PermutePalette(*s, permutation, usage->KeepLook, _("Move palette entries"));
				selection->Anchor++;
				selection->End++;
			});
			later->Style = UiButtonStyle::Flat;
			later->Tooltip = _("Move the picked entries an entry later");
			later->IsEnabledFunc = [canMove, selection]() { return canMove() && selection->Last() < ColorCount - 1; };
			auto* swap = moveRow->Add<UiButton>(_("Swap"), EditorIcon::None, [s, selection, usage]() {
				std::int32_t first = selection->First(), last = selection->Last();
				std::uint8_t permutation[ColorCount];
				for (std::int32_t i = 0; i < ColorCount; i++) {
					permutation[i] = std::uint8_t(i == first ? last : (i == last ? first : i));
				}
				PermutePalette(*s, permutation, usage->KeepLook, _("Swap palette entries"));
			});
			swap->Style = UiButtonStyle::Flat;
			swap->Flex = 1.0f;
			swap->Tooltip = _("Swap the first and the last picked entry, Shift+click picks the other one");
			swap->IsEnabledFunc = [canMove, selection]() { return canMove() && selection->First() != selection->Last(); };
			auto* keepLook = right->Add<UiCheckBox>(_("Keep the look of the level"), [usage]() { return usage->KeepLook; }, [usage](bool value) { usage->KeepLook = value; });
			keepLook->Tooltip = _("Moved colors take their pixels with them, the tiles of tile sets are changed in this level for it - otherwise the pixels show other colors");
		}

		// What is done with the whole range
		{
			auto* gradient = right->Add<UiButton>(_("Gradient"), EditorIcon::None, [s, selection]() {
				std::int32_t first = selection->First(), last = selection->Last();
				ChangePalette(*s, _("Palette gradient"), [first, last](std::uint32_t* palette) {
					// Even steps of how the colors look, from one end to the other
					OkLab from = OkLab::FromPaletteEntry(palette[first]);
					OkLab to = OkLab::FromPaletteEntry(palette[last]);
					for (std::int32_t i = first + 1; i < last; i++) {
						float t = float(i - first) / float(last - first);
						palette[i] = OkLab::Lerp(from, to, t).ToPaletteEntry(std::uint8_t(palette[i] >> 24));
					}
				}, false);
			});
			gradient->Tooltip = _("Fill the picked entries with even steps from the color of the first to the last");
			gradient->IsEnabledFunc = [s, selection]() { return UsesOwnPalette(*s) && selection->Last() - selection->First() >= 2; };

			auto* reset = right->Add<UiButton>(_("From Tile Set"), EditorIcon::Undo, [s, selection]() {
				std::uint32_t tileSetPalette[ColorCount];
				if (!s->GetTileSetLibrary().ReadPalette(s->GetDocument().Properties.DefaultTileSet, tileSetPalette)) {
					s->ShowMessage(_("The palette of the tile set can't be read"));
					return;
				}
				std::int32_t first = selection->First(), last = selection->Last();
				ChangePalette(*s, _("Palette from tile set"), [first, last, &tileSetPalette](std::uint32_t* palette) {
					for (std::int32_t i = first; i <= last; i++) {
						palette[i] = tileSetPalette[i];
					}
				}, false);
			});
			reset->Tooltip = _("Give the picked entries the colors of the palette of the default tile set");
			reset->IsEnabledFunc = [s]() { return UsesOwnPalette(*s); };
			// The other way, the tile set gets the palette of the level, other levels that use it then have it too
			auto* save = right->Add<UiButton>(_("Into Tile Set..."), EditorIcon::Save, [s]() {
				ShowApplyTileChangesDialog(*s, TileRef::DefaultTileSet);
			});
			save->Tooltip = _("Save the palette of the level into its default tile set, or a new one");
			save->IsEnabledFunc = [s]() { return UsesOwnPalette(*s); };

			// To and from other programs
			auto* files = right->Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			auto* import = files->Add<UiButton>(_("Import..."), EditorIcon::None, [s]() {
				SmallVector<UiFileRoot, 4> roots;
				s->GetImageRoots(false, roots);
				UiFileDialog::Show(*s->GetSurface(), _("Import Palette"), UiFileDialogMode::Open, std::move(roots), ".pal;.gpl;.act;.png"_s, {}, {}, [s](StringView path) {
					// The colors the file has replace those of the level, the first entry stays transparent
					std::uint32_t palette[ColorCount];
					std::memcpy(palette, GetShownPalette(*s), sizeof(palette));
					std::uint32_t transparent = palette[0];
					String error;
					std::int32_t count = PaletteFile::Read(path, palette, &error);
					if (count <= 0) {
						s->ShowMessage(_f("The palette can't be read: {}", error));
						return;
					}
					palette[0] = transparent;
					s->Execute(std::make_unique<SetLevelPaletteCommand>(s->GetDocument(), true, palette, _("Import palette")));
					s->ShowMessage(_fn("{} color was imported, the level has its own palette now", "{} colors were imported, the level has its own palette now", count, count));
				});
			});
			import->Flex = 1.0f;
			import->Tooltip = _("Take the colors of a palette of another program, JASC (.pal), GIMP (.gpl) or Adobe (.act), or of an image (.png)");
			auto* exportButton = files->Add<UiButton>(_("Export..."), EditorIcon::None, [s]() {
				SmallVector<UiFileRoot, 4> roots;
				s->GetImageRoots(true, roots);
				String name = String(s->GetLevelName().partition('/')[2]) + ".pal"_s;
				UiFileDialog::Show(*s->GetSurface(), _("Export Palette"), UiFileDialogMode::Save, std::move(roots), ".pal;.gpl;.act;.png"_s, {}, name, [s](StringView path) {
					std::uint32_t palette[ColorCount];
					std::memcpy(palette, GetShownPalette(*s), sizeof(palette));
					if (!PaletteFile::Write(path, palette, s->GetDocument().Properties.DisplayName)) {
						s->ShowMessage(_("The palette couldn't be saved"));
						return;
					}
					s->ShowMessage(_("Exported"));
				});
			});
			exportButton->Flex = 1.0f;
			exportButton->Tooltip = _("Save the palette the level is shown in for another program, by the extension of its name");
		}

		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		// The level is seen behind it, in the colors as they change
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
