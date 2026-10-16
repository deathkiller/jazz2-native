#include "TileSetImportDialog.h"

#if defined(WITH_EDITOR)

#include "TileSetsDialog.h"
#include "../EditorSession.h"
#include "../Files/ContentTileSetLibrary.h"
#include "../Imaging/PaletteMatch.h"
#include "../Imaging/TileImages.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"
#include "../../ContentResolver.h"
#include "../../Compatibility/JJ2Anims.Palettes.h"

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

		struct ImportState
		{
			String Path;
			PngImage Image;
			PngImage Masks;
			bool HasMasks = false;
			bool UseMasks = true;
			TileImportColors Colors = TileImportColors::OwnPalette;
			bool Dither = false;
			bool SkipDuplicates = false;
			ImportUse Use = ImportUse::Extra;
		};

		// Lowercase letters, digits, dashes and underscores, as the tile sets of the game are named
		String MakeTileSetName(StringView fileName)
		{
			String name = StringUtils::lowercase(fs::GetFileNameWithoutExtension(fileName));
			String result;
			for (char c : name) {
				if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
					result += StringView(&c, 1);
				} else if (c == ' ' || c == '.') {
					result += "_"_s;
				}
			}
			return result;
		}

		void Import(EditorSession& session, ImportState& state, StringView name)
		{
			if (!ContentTileSetLibrary::IsValidName(name)) {
				session.ShowMessage(_("A tile set is named by lowercase letters, digits, dashes and underscores"));
				return;
			}
			// A new name, a tile set of the game or of another level isn't replaced by mistake
			if (!ContentTileSetLibrary::FindTileSetFile(name).empty()) {
				session.ShowMessage(_("A tile set of this name exists already"));
				return;
			}
			String path = ContentTileSetLibrary::GetWritablePath(name, true);
			if (path.empty()) {
				session.ShowMessage(_("There's no place to save tile sets to"));
				return;
			}

			// The entries the palette of the level shares with the sprites stay, a new palette of the default tile set
			// is the palette of the sprites too. Tile sets often tweak the colors of the sprites a little, so an entry
			// close to the color of the sprites counts too, and it keeps the color the level has, as the sprites look now.
			constexpr float SpriteColorDistance = 0.06f;
			const std::uint32_t* levelPalette = ContentResolver::Get().GetPalettes().data();
			bool locked[256] = {};
			for (std::int32_t i = 1; i < 256; i++) {
				const Color& sprite = SpritePalette[i];
				std::uint32_t spriteColor = std::uint32_t(sprite.R) | (std::uint32_t(sprite.G) << 8) | (std::uint32_t(sprite.B) << 16);
				locked[i] = (OkLab::FromPaletteEntry(levelPalette[i]).DistanceSquared(OkLab::FromPaletteEntry(spriteColor)) <= SpriteColorDistance * SpriteColorDistance);
			}

			TileImportOptions options;
			options.Colors = state.Colors;
			options.Palette = levelPalette;
			options.Locked = locked;
			options.Dither = state.Dither;
			options.SkipDuplicates = state.SkipDuplicates;
			options.Masks = (state.HasMasks && state.UseMasks ? &state.Masks : nullptr);
			Formats::TileSetData tileSet;
			TileImportResult result;
			if (!SliceTileSet(state.Image, options, tileSet, result)) {
				session.ShowMessage(_("The image is smaller than a tile"));
				return;
			}

			{
				auto s = fs::Open(path, FileAccess::Write);
				if (!s->IsValid() || !Formats::TileSetFile::Write(*s, tileSet)) {
					s = nullptr;
					fs::RemoveFile(path);
					session.ShowMessage(_("The tile set couldn't be saved"));
					return;
				}
			}
			session.InvalidateTileSet(name);
			LOGI("Tile set \"{}\" with {} tiles made from \"{}\" ({} colors added, {} entries kept for sprites, {} same tiles left out)", path, result.TileCount, state.Path,
				result.ColorsAdded, std::count(std::begin(locked), std::end(locked), true), result.DuplicateTiles);

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
		}

		void ShowOptions(EditorSession& session, std::shared_ptr<ImportState> state)
		{
			UiSurface* surface = session.GetSurface();
			EditorSession* s = &session;
			auto dialog = std::make_unique<UiDialog>(_("New Tile Set from Image"));
			UiDialog* dialogPtr = dialog.get();
			auto* content = dialog->GetContent()->Add<UiForm>(UiTheme::Spacing);

			std::int32_t tiles = (state->Image.Width / 32) * (state->Image.Height / 32);
			{
				auto* row = content->AddRow(_("Image"));
				row->Add<UiLabel>(_f("{} × {} pixels, {} tiles, {}", state->Image.Width, state->Image.Height, tiles,
					state->Image.Indexed ? _("indexed") : _("true color")), UiTheme::TextDim);
			}
			UiTextBox* nameBox;
			{
				auto* row = content->AddRow(_("Name"));
				nameBox = row->Add<UiTextBox>(MakeTileSetName(fs::GetFileName(state->Path)));
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
				state->Colors = (state->Image.Indexed ? TileImportColors::OwnPalette : TileImportColors::TrueColor);
				auto* row = content->AddRow(_("Colors"));
				auto* dropdown = row->Add<UiDropdown>([state]() {
					for (const ColorOption& option : ColorOptions) {
						if (option.Colors == state->Colors) {
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
						entry.IsChecked = [state, colors]() { return (state->Colors == colors); };
						entry.Execute = [state, colors]() { state->Colors = colors; };
						entries.push_back(std::move(entry));
					}
				});
				dropdown->Width = 170.0f;
				row->Tooltip = _("True color keeps the colors as they are, but they don't change with the palette of a level - a palette made for it keeps the colors of the sprites");
				auto* dither = content->Add<UiCheckBox>(_("Mix colors between two of the palette (dither)"), [state]() { return state->Dither; }, [state](bool value) { state->Dither = value; });
				dither->IsEnabledFunc = [state]() { return (state->Colors == TileImportColors::MatchPalette || state->Colors == TileImportColors::Reduce); };
				content->Add<UiCheckBox>(_("Skip tiles that are the same as another"), [state]() { return state->SkipDuplicates; }, [state](bool value) { state->SkipDuplicates = value; });
			}
			if (state->HasMasks) {
				content->Add<UiCheckBox>(_("Masks from the image of masks next to it"), [state]() { return state->UseMasks; },
					[state](bool value) { state->UseMasks = value; })->Tooltip = _("Otherwise the tiles are solid where they aren't transparent");
			} else {
				content->Add<UiLabel>(_("The tiles are solid where they aren't transparent"), UiTheme::TextDim);
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
						entries.push_back(std::move(entry));
					}
				});
				dropdown->Width = 170.0f;
			}

			dialog->AddButton(_("Make Tile Set"), [s, state, nameBox, dialogPtr]() {
				String name = nameBox->Text.trimmed();
				dialogPtr->Close();
				Import(*s, *state, name);
			}, true);
			dialog->AddButton(_("Cancel"), [dialogPtr]() { dialogPtr->Close(); });

			UiOverlayOptions options;
			options.Centered = true;
			options.Modal = true;
			options.CloseOnOutsideClick = false;
			surface->PushOverlay(std::move(dialog), std::move(options));
		}
	}

	void ImportTileSetImage(EditorSession& session, StringView path, StringView name, bool addToLevel, bool asDefault, StringView colors, bool dither)
	{
		ImportState state;
		state.Path = path;
		auto file = fs::Open(path, FileAccess::Read);
		String error;
		if (!file->IsValid() || !PngCodec::Decode(*file, state.Image, &error)) {
			session.ShowMessage(_f("The image can't be read: {}", error));
			return;
		}
		String maskPath = String(path.exceptSuffix(std::size_t(4))) + ".mask.png"_s;
		if (fs::IsReadableFile(maskPath)) {
			auto maskFile = fs::Open(maskPath, FileAccess::Read);
			state.HasMasks = (maskFile->IsValid() && PngCodec::Decode(*maskFile, state.Masks));
		}
		state.Colors = (state.Image.Indexed ? TileImportColors::OwnPalette : TileImportColors::TrueColor);
		if (colors == "true"_s) {
			state.Colors = TileImportColors::TrueColor;
		} else if (colors == "match"_s) {
			state.Colors = TileImportColors::MatchPalette;
		} else if (colors == "reduce"_s) {
			state.Colors = TileImportColors::Reduce;
		}
		state.Dither = dither;
		state.Use = (!addToLevel ? ImportUse::SaveOnly : (asDefault ? ImportUse::Default : ImportUse::Extra));
		Import(session, state, name);
	}

	void ShowImportTileSetDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}
		EditorSession* s = &session;
		SmallVector<UiFileRoot, 4> roots;
		session.GetImageRoots(false, roots);
		UiFileDialog::Show(*surface, _("New Tile Set from Image"), UiFileDialogMode::Open, std::move(roots), ".png"_s, {}, {}, [s](StringView path) {
			auto state = std::make_shared<ImportState>();
			state->Path = path;
			auto file = fs::Open(path, FileAccess::Read);
			String error;
			if (!file->IsValid() || !PngCodec::Decode(*file, state->Image, &error)) {
				s->ShowMessage(_f("The image can't be read: {}", error));
				return;
			}
			if (state->Image.Width < 32 || state->Image.Height < 32) {
				s->ShowMessage(_("The image is smaller than a tile"));
				return;
			}
			// The masks next to it, as the editor exports them
			String maskPath = String(path.exceptSuffix(std::size_t(4))) + ".mask.png"_s;
			if (fs::IsReadableFile(maskPath)) {
				auto maskFile = fs::Open(maskPath, FileAccess::Read);
				state->HasMasks = (maskFile->IsValid() && PngCodec::Decode(*maskFile, state->Masks));
			}
			ShowOptions(*s, std::move(state));
		});
	}

	bool CreateEmptyTileSet(EditorSession& session, StringView name, std::int32_t tileCount, bool addToLevel, bool asDefault)
	{
		if (!ContentTileSetLibrary::IsValidName(name)) {
			session.ShowMessage(_("A tile set is named by lowercase letters, digits, dashes and underscores"));
			return false;
		}
		if (!ContentTileSetLibrary::FindTileSetFile(name).empty()) {
			session.ShowMessage(_("A tile set of this name exists already"));
			return false;
		}
		String path = ContentTileSetLibrary::GetWritablePath(name, true);
		if (path.empty()) {
			session.ShowMessage(_("There's no place to save tile sets to"));
			return false;
		}

		// Empty tiles, the first one stays empty, in the palette the level is shown in
		Formats::TileSetData tileSet;
		tileSet.Resize(std::clamp(tileCount, 1, std::int32_t(UINT16_MAX)));
		std::memcpy(tileSet.Palette, ContentResolver::Get().GetPalettes().data(), sizeof(tileSet.Palette));
		tileSet.Palette[0] &= 0x00FFFFFFu;
		tileSet.Columns = 0;
		{
			auto s = fs::Open(path, FileAccess::Write);
			if (!s->IsValid() || !Formats::TileSetFile::Write(*s, tileSet)) {
				s = nullptr;
				fs::RemoveFile(path);
				session.ShowMessage(_("The tile set couldn't be saved"));
				return false;
			}
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

	void ShowNewTileSetDialog(EditorSession& session)
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
		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("New Tile Set"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent()->Add<UiForm>(UiTheme::Spacing);
		content->Add<UiLabel>(_("Empty tiles in the palette of the level, painted then by a double click on them."), UiTheme::TextDim);

		UiTextBox* nameBox;
		{
			// A name that isn't taken yet
			String name = "new-tiles"_s;
			for (std::int32_t i = 2; !ContentTileSetLibrary::FindTileSetFile(name).empty() && i < 100; i++) {
				name = "new-tiles-"_s + String(std::to_string(i).c_str());
			}
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
					entries.push_back(std::move(entry));
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

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
