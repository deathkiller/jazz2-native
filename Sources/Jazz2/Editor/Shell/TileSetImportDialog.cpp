#include "TileSetImportDialog.h"

#if defined(WITH_EDITOR)

#include "ProgressDialog.h"
#include "ShellCommon.h"
#include "TextPrompt.h"
#include "TileSetsDialog.h"
#include "../Tools/EditorContext.h"
#include "../Files/AtomicFile.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Imaging/PaletteMatch.h"
#include "../Imaging/PngCodec.h"
#include "../Imaging/TileImages.h"
#include "../UI/UiColors.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		enum class ImportUse : std::uint8_t { Extra, Default, SaveOnly };
		enum class DitherMode : std::uint8_t { None, Pattern, ErrorDiffusion };

		// What is chosen for the import, compared to tell whether a preview is still what would be made
		struct ImportSettings
		{
			TileImportColors Colors = TileImportColors::OwnPalette;
			DitherMode Dither = DitherMode::None;
			bool SkipDuplicates = false;
			bool UseMasks = true;
			std::int32_t OffsetX = 0;
			std::int32_t OffsetY = 0;
			std::int32_t SpacingX = 0;
			std::int32_t SpacingY = 0;
			bool UseColorKey = false;
			std::uint32_t ColorKey = 0x00FF00FFu;
			std::int32_t AlphaThreshold = 128;

			bool operator==(const ImportSettings& other) const
			{
				return (Colors == other.Colors && Dither == other.Dither && SkipDuplicates == other.SkipDuplicates && UseMasks == other.UseMasks &&
					OffsetX == other.OffsetX && OffsetY == other.OffsetY && SpacingX == other.SpacingX && SpacingY == other.SpacingY &&
					UseColorKey == other.UseColorKey && ColorKey == other.ColorKey && AlphaThreshold == other.AlphaThreshold);
			}
		};

		// A tile set made of the image, with what it's made with, so it can be made off the user interface
		struct SlicedTileSet
		{
			ImportSettings Settings;
			std::uint32_t Palette[256];
			bool Locked[256];
			bool HasMasks = false;
			Formats::TileSetData TileSet;
			TileImportResult Result;
			bool Success = false;
		};

		struct ImportState
		{
			String Path;
			PngImage Image;
			PngImage Masks;
			bool HasMasks = false;
			ImportSettings Settings;
			ImportUse Use = ImportUse::Extra;
			// The tile set made last to see what it would be, it's saved as it is if nothing was changed since
			std::shared_ptr<SlicedTileSet> Preview;
		};

		void ApplySettings(const ImportSettings& settings, TileImportOptions& options)
		{
			options.Colors = settings.Colors;
			options.Dither = (settings.Dither == DitherMode::Pattern);
			options.ErrorDiffusion = (settings.Dither == DitherMode::ErrorDiffusion);
			options.SkipDuplicates = settings.SkipDuplicates;
			options.OffsetX = settings.OffsetX;
			options.OffsetY = settings.OffsetY;
			options.SpacingX = settings.SpacingX;
			options.SpacingY = settings.SpacingY;
			options.UseColorKey = settings.UseColorKey;
			options.ColorKey = settings.ColorKey;
			options.AlphaThreshold = std::uint8_t(std::clamp<std::int32_t>(settings.AlphaThreshold, 1, 255));
		}

		// What making the tile set needs from the editor, taken on the main thread
		std::shared_ptr<SlicedTileSet> PrepareSlice(const ImportState& state)
		{
			auto job = std::make_shared<SlicedTileSet>();
			job->Settings = state.Settings;
			job->HasMasks = (state.HasMasks && state.Settings.UseMasks);
			// The entries the palette of the level shares with the sprites stay, a new palette of the default tile set
			// is the palette of the sprites too. Tile sets often tweak the colors of the sprites a little, so an entry
			// close to the color of the sprites counts too, and it keeps the color the level has, as the sprites look now.
			std::memcpy(job->Palette, ContentResolver::Get().GetPalettes().data(), sizeof(job->Palette));
			for (std::int32_t i = 0; i < 256; i++) {
				job->Locked[i] = IsSpriteColor(i, job->Palette[i]);
			}
			return job;
		}

		// Makes the tile set, on any thread, it only reads the image
		void SliceInto(const ImportState& state, SlicedTileSet& job, JobProgress* progress)
		{
			TileImportOptions options;
			ApplySettings(job.Settings, options);
			options.Palette = job.Palette;
			options.Locked = job.Locked;
			options.Masks = (job.HasMasks ? &state.Masks : nullptr);
			if (progress != nullptr) {
				options.Progress = [progress](float value) {
					return progress->Report(value);
				};
			}
			job.Success = SliceTileSet(state.Image, options, job.TileSet, job.Result);
		}

		// Tells why the tile set couldn't be made, returns `false` then
		bool CheckSliced(IEditorContext& session, const SlicedTileSet& job)
		{
			if (job.Success) {
				return true;
			}
			switch (job.Result.Error) {
				case TileImportError::TooManyTiles:
					session.ShowMessage(job.Settings.SkipDuplicates
						? String(_f("The image has more than {} different tiles, a tile set can't hold them", MaxImportedTiles))
						: String(_f("The image has more than {} tiles, a tile set can't hold them - skipping the same tiles may help", MaxImportedTiles)));
					break;
				case TileImportError::Cancelled:
					session.ShowMessage(_("Making the tile set was stopped"));
					break;
				default:
					session.ShowMessage(_("The image has no tile in it, it's smaller than a tile with the margin and gutters"));
					break;
			}
			return false;
		}

		// Makes the tile set off the user interface, with how far it is shown, then passes it on
		void SliceAsync(IEditorContext& session, std::shared_ptr<ImportState> state, Function<void(std::shared_ptr<SlicedTileSet>)>&& onSliced)
		{
			std::shared_ptr<SlicedTileSet> job = PrepareSlice(*state);
			IEditorContext* s = &session;
			auto callback = std::make_shared<Function<void(std::shared_ptr<SlicedTileSet>)>>(Death::move(onSliced));
			RunWithProgress(session, _("Making Tile Set"), [state, job](JobProgress& progress) {
				SliceInto(*state, *job, &progress);
			}, [s, job, callback](bool cancelled) {
				if (cancelled && job->Success) {
					// Stopped right when it was done, it's taken anyway
					cancelled = false;
				}
				if (cancelled) {
					s->ShowMessage(_("Making the tile set was stopped"));
					return;
				}
				if (CheckSliced(*s, *job)) {
					(*callback)(job);
				}
			});
		}

		// Whether a name can be given to a new tile set, shows why not otherwise
		bool CheckNewName(IEditorContext& session, StringView name)
		{
			if (!ContentTileSetLibrary::IsValidName(name)) {
				session.ShowMessage(_("A tile set is named by lowercase letters, digits, dashes and underscores"));
				return false;
			}
			// A new name, a tile set of the game or of another level isn't replaced by mistake
			if (!ContentTileSetLibrary::FindTileSetFile(name).empty()) {
				session.ShowMessage(_("A tile set of this name exists already"));
				return false;
			}
			return true;
		}

		// Saves a tile set made of the image and uses it in the level as chosen
		bool FinishImport(IEditorContext& session, const ImportState& state, StringView name, const SlicedTileSet& job)
		{
			if (!CheckNewName(session, name)) {
				return false;
			}
			String path = ContentTileSetLibrary::GetWritablePath(name, true);
			if (path.empty()) {
				session.ShowMessage(_("There's no place to save tile sets to"));
				return false;
			}
			const Formats::TileSetData& tileSet = job.TileSet;
			const TileImportResult& result = job.Result;

			// A file that isn't written whole isn't left there, it would take the name
			if (!AtomicFile::Write(path, [&tileSet](Stream& s) { return Formats::TileSetFile::Write(s, tileSet); })) {
				session.ShowMessage(_("The tile set couldn't be saved"));
				return false;
			}
			session.InvalidateTileSet(name);
			LOGI("Tile set \"{}\" with {} tiles made from \"{}\" ({} colors added, {} entries kept for sprites, {} same tiles left out)", path, result.TileCount, state.Path,
				result.ColorsAdded, std::count(std::begin(job.Locked), std::end(job.Locked), true), result.DuplicateTiles);

			switch (state.Use) {
				case ImportUse::Extra: AddExtraTileSet(session, name); break;
				case ImportUse::Default: SetDefaultTileSet(session, name); break;
				default: break;
			}

			String message = _fn("Tile set \"{}\" with {} tile was made", "Tile set \"{}\" with {} tiles was made", result.TileCount, name, result.TileCount);
			if (result.AddedEmptyTile) {
				message += " "_s + _("- the image didn't start with an empty tile, so one was put first");
			}
			if (result.ColorsAdded > 0) {
				message += " "_s + _fn("- {} color was made for it", "- {} colors were made for it", result.ColorsAdded, result.ColorsAdded);
			} else if (result.ColorsChanged) {
				message += " "_s + _("- some colors became the closest ones of the palette");
			}
			if (result.DuplicateTiles > 0) {
				message += " "_s + _fn("- {} tile was the same as another one", "- {} tiles were the same as others", result.DuplicateTiles, result.DuplicateTiles);
			}
			session.ShowMessage(message);
			return true;
		}

		// Makes and saves the tile set right away, waiting for it, as the console command does
		bool Import(IEditorContext& session, const ImportState& state, StringView name)
		{
			if (!CheckNewName(session, name)) {
				return false;
			}
			std::shared_ptr<SlicedTileSet> job = PrepareSlice(state);
			SliceInto(state, *job, nullptr);
			return (CheckSliced(session, *job) && FinishImport(session, state, name, *job));
		}

		/** The palette of the tile set as the preview made it, with what it found */
		String DescribePreview(const ImportState& state)
		{
			const SlicedTileSet* preview = state.Preview.get();
			if (preview == nullptr) {
				return _("Preview shows the palette and the tiles the tile set would have before it's saved");
			}
			String text = _fn("{} tile", "{} tiles", preview->Result.TileCount, preview->Result.TileCount);
			if (preview->Result.ColorsAdded > 0) {
				text += ", "_s + _fn("{} color made for it", "{} colors made for it", preview->Result.ColorsAdded, preview->Result.ColorsAdded);
			} else if (preview->Result.ColorsChanged) {
				text += ", "_s + _("some colors became the closest ones");
			}
			if (preview->Result.DuplicateTiles > 0) {
				text += ", "_s + _fn("{} same tile left out", "{} same tiles left out", preview->Result.DuplicateTiles, preview->Result.DuplicateTiles);
			}
			if (!(preview->Settings == state.Settings)) {
				text += " - "_s + _("changed since, it's made again when it's saved");
			}
			return text;
		}

		void ShowOptions(IEditorContext& session, std::shared_ptr<ImportState> state)
		{
			UiSurface* surface = session.GetSurface();
			IEditorContext* s = &session;
			auto dialog = std::make_unique<UiDialog>(_("New Tile Set from Image"));
			UiDialog* dialogPtr = dialog.get();
			auto* content = dialog->GetContent()->Add<UiForm>(UiTheme::Spacing);

			{
				auto* row = content->AddRow(_("Image"));
				auto* info = row->Add<UiLabel>(StringView(), UiTheme::TextDim);
				info->TextFunc = [state]() {
					// The tiles as the margin and the gutters lay them out
					TileImportOptions options;
					ApplySettings(state->Settings, options);
					Vector2i grid = GetTileGrid(state->Image, options);
					return String(_f("{} x {} pixels, {} x {} tiles, {}", state->Image.Width, state->Image.Height, grid.X, grid.Y,
						state->Image.Indexed ? _("indexed") : _("true color")));
				};
			}
			UiTextBox* nameBox;
			{
				auto* row = content->AddRow(_("Name"));
				nameBox = row->Add<UiTextBox>(SuggestTileSetName(fs::GetFileNameWithoutExtension(state->Path), {}));
				nameBox->Width = 140.0f;
				nameBox->MaxLength = 32;
				nameBox->Tooltip = _("The tile set is saved in the user content, Tilesets/<name>.j2t");
			}
			{
				struct ColorOption { const char* Label; TileImportColors Colors; bool IndexedOnly; };
				static const ColorOption ColorOptions[] = {
					{ N_("Its own palette"), TileImportColors::OwnPalette, true },
					{ N_("True color"), TileImportColors::TrueColor, false },
					{ N_("Closest colors of the level palette"), TileImportColors::MatchPalette, false },
					{ N_("A palette made for it"), TileImportColors::Reduce, false },
				};
				state->Settings.Colors = (state->Image.Indexed ? TileImportColors::OwnPalette : TileImportColors::TrueColor);
				auto* row = content->AddRow(_("Colors"));
				auto* dropdown = row->Add<UiDropdown>([state]() {
					for (const ColorOption& option : ColorOptions) {
						if (option.Colors == state->Settings.Colors) {
							return String(_(option.Label));
						}
					}
					return String();
				}, [state](SmallVectorImpl<UiMenuEntry>& entries) {
					for (const ColorOption& option : ColorOptions) {
						if (option.IndexedOnly && !state->Image.Indexed) {
							continue;
						}
						TileImportColors colors = option.Colors;
						UiMenuEntry entry;
						entry.Label = _(option.Label);
						entry.IsChecked = [state, colors]() { return (state->Settings.Colors == colors); };
						entry.Execute = [state, colors]() { state->Settings.Colors = colors; };
						entries.push_back(Death::move(entry));
					}
				});
				dropdown->Width = 170.0f;
				row->Tooltip = _("True color keeps the colors as they are, but they don't change with the palette of a level - a palette made for it keeps the colors of the sprites");

				struct DitherOption { const char* Label; DitherMode Mode; };
				static const DitherOption DitherOptions[] = {
					{ N_("None"), DitherMode::None },
					{ N_("Pattern"), DitherMode::Pattern },
					{ N_("Error diffusion"), DitherMode::ErrorDiffusion },
				};
				auto* ditherRow = content->AddRow(_("Dither"));
				auto* dither = ditherRow->Add<UiDropdown>([state]() {
					for (const DitherOption& option : DitherOptions) {
						if (option.Mode == state->Settings.Dither) {
							return String(_(option.Label));
						}
					}
					return String();
				}, [state](SmallVectorImpl<UiMenuEntry>& entries) {
					for (const DitherOption& option : DitherOptions) {
						DitherMode mode = option.Mode;
						UiMenuEntry entry;
						entry.Label = _(option.Label);
						entry.IsChecked = [state, mode]() { return (state->Settings.Dither == mode); };
						entry.Execute = [state, mode]() { state->Settings.Dither = mode; };
						entries.push_back(Death::move(entry));
					}
				});
				dither->Width = 170.0f;
				dither->Tooltip = _("Colors between two of the palette are mixed - by a regular pattern, or by spreading what each pixel misses to the next ones (Floyd-Steinberg)");
				dither->IsEnabledFunc = [state]() { return (state->Settings.Colors == TileImportColors::MatchPalette || state->Settings.Colors == TileImportColors::Reduce); };
				content->Add<UiCheckBox>(_("Skip tiles that are the same as another"), [state]() { return state->Settings.SkipDuplicates; },
					[state](bool value) { state->Settings.SkipDuplicates = value; });
			}
			if (state->HasMasks) {
				content->Add<UiCheckBox>(_("Masks from the image of masks next to it"), [state]() { return state->Settings.UseMasks; },
					[state](bool value) { state->Settings.UseMasks = value; })->Tooltip = _("Otherwise the tiles are solid where they aren't transparent");
			} else {
				content->Add<UiLabel>(_("The tiles are solid where they aren't transparent"), UiTheme::TextDim);
			}

			// Sprite sheets have a margin around the tiles and gutters between them
			auto addNumber = [](UiStack* row, Function<std::int32_t&()>&& value, float max, StringView tooltip) {
				auto shared = std::make_shared<Function<std::int32_t&()>>(Death::move(value));
				auto* number = row->Add<UiNumberBox>([shared]() { return float((*shared)()); }, [shared](float v, bool) {
					(*shared)() = std::int32_t(std::round(v));
				}, 0.0f, max, 1.0f, 0);
				number->Width = 48.0f;
				number->Tooltip = tooltip;
			};
			{
				auto* row = content->AddRow(_("Margin"));
				addNumber(row, [state]() -> std::int32_t& { return state->Settings.OffsetX; }, 1024.0f, _("Pixels left of the first column of tiles"));
				addNumber(row, [state]() -> std::int32_t& { return state->Settings.OffsetY; }, 1024.0f, _("Pixels above the first row of tiles"));
				auto* gutterRow = content->AddRow(_("Gutters"));
				addNumber(gutterRow, [state]() -> std::int32_t& { return state->Settings.SpacingX; }, 256.0f, _("Pixels between two tiles next to each other"));
				addNumber(gutterRow, [state]() -> std::int32_t& { return state->Settings.SpacingY; }, 256.0f, _("Pixels between two rows of tiles"));
			}

			// What is transparent, a color that stands for it in images without alpha, and how opaque a pixel has to be
			{
				auto* row = content->AddRow(_("Transparent"));
				row->Add<UiCheckBox>(_("Color"), [state]() { return state->Settings.UseColorKey; }, [state](bool value) { state->Settings.UseColorKey = value; })
					->Tooltip = _("Pixels of this color are transparent, magenta in many sprite sheets");
				UiColorSwatch* swatch = row->Add<UiColorSwatch>([state]() {
					std::uint32_t key = state->Settings.ColorKey;
					return Colorf((key & 0xFF) / 255.0f, ((key >> 8) & 0xFF) / 255.0f, ((key >> 16) & 0xFF) / 255.0f, 1.0f);
				});
				swatch->Width = 24.0f;
				swatch->Height = 16.0f;
				swatch->OnClick = [s, state, swatch]() {
					UiColorPicker::ShowPopup(*s->GetSurface(), Vector2f(swatch->Bounds.X, swatch->Bounds.Y + swatch->Bounds.H), [state]() {
						std::uint32_t key = state->Settings.ColorKey;
						return Colorf((key & 0xFF) / 255.0f, ((key >> 8) & 0xFF) / 255.0f, ((key >> 16) & 0xFF) / 255.0f, 1.0f);
					}, [state](const Colorf& color) {
						auto channel = [](float value) { return std::uint32_t(std::clamp<std::int32_t>(std::int32_t(std::round(value * 255.0f)), 0, 255)); };
						state->Settings.ColorKey = channel(color.R) | (channel(color.G) << 8) | (channel(color.B) << 16);
						state->Settings.UseColorKey = true;
					});
				};
				swatch->IsEnabledFunc = [state]() { return state->Settings.UseColorKey; };
				row->Add<UiLabel>(_("Alpha"), UiTheme::TextDim);
				addNumber(row, [state]() -> std::int32_t& { return state->Settings.AlphaThreshold; }, 255.0f,
					_("How opaque a pixel has to be to be seen, and a pixel of the masks to be solid, from 1 to 255"));
			}
			{
				struct UseOption { const char* Label; ImportUse Use; };
				static const UseOption UseOptions[] = {
					{ N_("Extra tile set of this level"), ImportUse::Extra },
					{ N_("Default tile set of this level"), ImportUse::Default },
					{ N_("Only save it"), ImportUse::SaveOnly },
				};
				auto* row = content->AddRow(_("Then"));
				auto* dropdown = row->Add<UiDropdown>([state]() {
					for (const UseOption& option : UseOptions) {
						if (option.Use == state->Use) {
							return String(_(option.Label));
						}
					}
					return String();
				}, [state](SmallVectorImpl<UiMenuEntry>& entries) {
					for (const UseOption& option : UseOptions) {
						ImportUse use = option.Use;
						UiMenuEntry entry;
						entry.Label = _(option.Label);
						entry.IsChecked = [state, use]() { return (state->Use == use); };
						entry.Execute = [state, use]() { state->Use = use; };
						entries.push_back(Death::move(entry));
					}
				});
				dropdown->Width = 170.0f;
			}

			// What the tile set would be, before it's saved
			{
				auto* previewRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				auto* previewButton = previewRow->Add<UiButton>(_("Preview"), EditorIcon::Search, [s, state]() {
					SliceAsync(*s, state, [state](std::shared_ptr<SlicedTileSet> job) {
						state->Preview = Death::move(job);
					});
				});
				previewButton->Tooltip = _("Make the tile set to see its palette and how many tiles it has, without saving it");
				auto* previewInfo = previewRow->Add<UiLabel>(StringView(), UiTheme::TextDim);
				previewInfo->Flex = 1.0f;
				previewInfo->TextFunc = [state]() { return DescribePreview(*state); };
				auto* palette = content->Add<UiPaletteGrid>([state]() -> const std::uint32_t* {
					return (state->Preview != nullptr ? state->Preview->TileSet.Palette : ContentResolver::Get().GetPalettes().data());
				}, []() { return -1; }, [](std::int32_t, bool) {});
				palette->CellSize = 8.0f;
				palette->Tooltip = _("The palette the tile set would have");
				palette->IsVisibleFunc = [state]() {
					return (state->Preview != nullptr && state->Preview->Settings.Colors != TileImportColors::TrueColor);
				};
			}

			dialog->AddButton(_("Make Tile Set"), [s, state, nameBox, dialogPtr]() {
				// The options stay as they were chosen if the name has to be changed
				String name = nameBox->Text.trimmed();
				if (!CheckNewName(*s, name)) {
					return;
				}
				// What was previewed is saved as it is if nothing was changed since
				if (state->Preview != nullptr && state->Preview->Settings == state->Settings) {
					if (FinishImport(*s, *state, name, *state->Preview)) {
						dialogPtr->Close();
					}
					return;
				}
				SliceAsync(*s, state, [s, state, name, dialogPtr](std::shared_ptr<SlicedTileSet> job) {
					state->Preview = job;
					if (FinishImport(*s, *state, name, *job)) {
						dialogPtr->Close();
					}
				});
			}, true);
			dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });

			ShowModal(*surface, Death::move(dialog));
		}
	}

	void ImportTileSetImage(IEditorContext& session, StringView path, StringView name, bool addToLevel, bool asDefault, StringView colors, bool dither)
	{
		ImportState state;
		state.Path = path;
		String error;
		if (!PngCodec::LoadWithMasks(path, state.Image, state.Masks, state.HasMasks, &error)) {
			session.ShowMessage(_f("The image can't be read: {}", error));
			return;
		}
		state.Settings.Colors = (state.Image.Indexed ? TileImportColors::OwnPalette : TileImportColors::TrueColor);
		if (colors == "true"_s) {
			state.Settings.Colors = TileImportColors::TrueColor;
		} else if (colors == "match"_s) {
			state.Settings.Colors = TileImportColors::MatchPalette;
		} else if (colors == "reduce"_s) {
			state.Settings.Colors = TileImportColors::Reduce;
		}
		state.Settings.Dither = (dither ? DitherMode::Pattern : DitherMode::None);
		state.Use = (!addToLevel ? ImportUse::SaveOnly : (asDefault ? ImportUse::Default : ImportUse::Extra));
		Import(session, state, name);
	}

	void ShowImportTileSetDialog(IEditorContext& session)
	{
		IEditorContext* s = &session;
		ShowOpenImageDialog(session, _("New Tile Set from Image"), [s](StringView path) {
			auto state = std::make_shared<ImportState>();
			state->Path = path;
			// The masks next to it, as the editor exports them
			String error;
			if (!PngCodec::LoadWithMasks(path, state->Image, state->Masks, state->HasMasks, &error)) {
				s->ShowMessage(_f("The image can't be read: {}", error));
				return;
			}
			if (state->Image.Width < 32 || state->Image.Height < 32) {
				s->ShowMessage(_("The image is smaller than a tile"));
				return;
			}
			ShowOptions(*s, Death::move(state));
		});
	}

	bool CreateEmptyTileSet(IEditorContext& session, StringView name, std::int32_t tileCount, bool addToLevel, bool asDefault)
	{
		if (!CheckNewName(session, name)) {
			return false;
		}
		String path = ContentTileSetLibrary::GetWritablePath(name, true);
		if (path.empty()) {
			session.ShowMessage(_("There's no place to save tile sets to"));
			return false;
		}

		// Empty tiles, the first one stays empty, in the palette the level is shown in
		Formats::TileSetData tileSet;
		tileSet.Resize(std::clamp<std::int32_t>(tileCount, 1, UINT16_MAX));
		GetLevelPalette(session, tileSet.Palette);
		tileSet.Columns = 0;
		// A file that isn't written whole isn't left there, it would take the name
		if (!AtomicFile::Write(path, [&tileSet](Stream& s) { return Formats::TileSetFile::Write(s, tileSet); })) {
			session.ShowMessage(_("The tile set couldn't be saved"));
			return false;
		}
		session.InvalidateTileSet(name);
		LOGI("Empty tile set \"{}\" with {} tiles made", path, tileSet.TileCount);

		if (addToLevel) {
			if (asDefault) {
				SetDefaultTileSet(session, name);
			} else {
				AddExtraTileSet(session, name);
			}
		}
		session.ShowMessage(_fn("Tile set \"{}\" with {} empty tile was made", "Tile set \"{}\" with {} empty tiles was made", tileSet.TileCount, name, tileSet.TileCount));
		return true;
	}

	void ShowNewTileSetDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		struct NewTileSetState
		{
			float TileCount = 100.0f;
			ImportUse Use = ImportUse::Extra;
		};
		auto state = std::make_shared<NewTileSetState>();
		IEditorContext* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("New Tile Set"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent()->Add<UiForm>(UiTheme::Spacing);
		content->Add<UiLabel>(_("Empty tiles in the palette of the level, painted then by a double click on them."), UiTheme::TextDim);

		UiTextBox* nameBox;
		{
			// A name that isn't taken yet
			String name = SuggestTileSetName("new"_s, "-tiles"_s);
			auto* row = content->AddRow(_("Name"));
			nameBox = row->Add<UiTextBox>(name);
			nameBox->Width = 140.0f;
			nameBox->MaxLength = 32;
			nameBox->Tooltip = _("The tile set is saved in the user content, Tilesets/<name>.j2t");
		}
		{
			auto* row = content->AddRow(_("Tiles"));
			auto* count = row->Add<UiNumberBox>([state]() { return state->TileCount; }, [state](float value, bool final) {
				state->TileCount = std::round(value);
			}, 10.0f, 4000.0f, 10.0f, 0);
			count->Width = 80.0f;
			count->Tooltip = _("The original tile sets have 10 tiles in a row");
		}
		{
			struct UseOption { const char* Label; ImportUse Use; };
			static const UseOption UseOptions[] = {
				{ N_("Extra tile set of this level"), ImportUse::Extra },
				{ N_("Default tile set of this level"), ImportUse::Default },
				{ N_("Only save it"), ImportUse::SaveOnly },
			};
			auto* row = content->AddRow(_("Then"));
			auto* dropdown = row->Add<UiDropdown>([state]() {
				for (const UseOption& option : UseOptions) {
					if (option.Use == state->Use) {
						return String(_(option.Label));
					}
				}
				return String();
			}, [state](SmallVectorImpl<UiMenuEntry>& entries) {
				for (const UseOption& option : UseOptions) {
					ImportUse use = option.Use;
					UiMenuEntry entry;
					entry.Label = _(option.Label);
					entry.IsChecked = [state, use]() { return (state->Use == use); };
					entry.Execute = [state, use]() { state->Use = use; };
					entries.push_back(Death::move(entry));
				}
			});
			dropdown->Width = 170.0f;
		}

		dialog->AddButton(_("Make Tile Set"), [s, state, nameBox, dialogPtr]() {
			String name = nameBox->Text.trimmed();
			if (CreateEmptyTileSet(*s, name, std::int32_t(state->TileCount), state->Use != ImportUse::SaveOnly, state->Use == ImportUse::Default)) {
				dialogPtr->Close();
			}
		}, true);
		dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });

		ShowModal(*surface, Death::move(dialog));
	}
}

#endif
