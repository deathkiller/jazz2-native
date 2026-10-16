#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Shows a search of all actions of the editor, the one picked is done right away

		What's written is looked for in their names, Enter does the first one found.
	*/
	void ShowCommandPalette(EditorSession& session);

	/**
		@brief Shows all actions with their shortcuts, which can be changed

		A click on a shortcut waits for the keys of a new one, <kbd>Backspace</kbd> removes it and <kbd>Esc</kbd>
		keeps it. A shortcut another action had moves to the new one. Changed shortcuts are saved to
		`Jazz2.EditorHotkeys.json` in the configuration directory.
	*/
	void ShowShortcutsDialog(EditorSession& session);

	/**
		@brief Shows the changes that can be undone and redone, a click goes back or forward to one of them
	*/
	void ShowHistoryDialog(EditorSession& session);
}

#endif
