#include "../UI/EditorUi.h"
#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <Containers/Function.h>
#include <Containers/StringView.h>

namespace Jazz2::UI::Windows
{
	class FileDialog;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::StringView;
#endif

	class IEditorContext;

	/**
	 * @brief Makes a file dialog show levels as the editor knows them
	 *
	 * Their icons tell a level from a level with a script and from a file that isn't a level of this version of the
	 * game, the selected one is shown with its preview, name, author, music, tile set and the levels that follow it.
	 */
	void SetupLevelFileDialog(FileDialog& dialog);
	/** @brief Makes a file dialog show tile sets with their icons, the first tiles and how many there are */
	void SetupTileSetFileDialog(FileDialog& dialog);
	/** @brief Makes a file dialog show music with its icons, the format and the title of the song */
	void SetupMusicFileDialog(FileDialog& dialog);

	/**
	 * @brief Lets a level be picked, @p onPicked gets how a level refers to it
	 *
	 * A level of the same episode as the edited one without the episode, as the original levels have it, `episode/level`
	 * otherwise. The dialog opens at @p current, the reference that is there now.
	 */
	void ShowLevelPicker(IEditorContext& session, StringView title, StringView current, Function<void(StringView)>&& onPicked);
	/** @brief Lets a tile set be picked, @p onPicked gets its path relative to `Tilesets` without the extension */
	void ShowTileSetPicker(IEditorContext& session, StringView title, StringView current, Function<void(StringView)>&& onPicked);
	/** @brief Lets music be picked, @p onPicked gets its path relative to the place it's in, as the game looks for it */
	void ShowMusicPicker(IEditorContext& session, StringView title, StringView current, Function<void(StringView)>&& onPicked);
}

#endif
