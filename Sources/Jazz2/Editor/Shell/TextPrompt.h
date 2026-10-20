#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"

#include <Containers/Function.h>
#include <Containers/StringView.h>

#include <memory>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::StringView;
#endif

	class UiElement;
	class UiSurface;

	/**
	 * @brief Shows a dialog in the middle of the screen, the rest of the interface can't be used until it's closed
	 *
	 * A click outside of it doesn't close it, it's closed by its buttons, its cross or Escape. Returns the dialog.
	 */
	UiElement* ShowModal(UiSurface& surface, std::unique_ptr<UiElement> dialog);
	/**
	 * @brief Shows a window in the middle of the screen, the rest of the interface and the level can still be used
	 *
	 * A click outside of it doesn't close it, it's closed by its buttons, its cross or Escape. @p onClosed is called
	 * when it's closed. Returns the window.
	 */
	UiElement* ShowModeless(UiSurface& surface, std::unique_ptr<UiElement> dialog, Function<void()>&& onClosed = {});

	/**
	 * @brief Asks for a line of text in a small dialog
	 *
	 * @p onConfirmed gets the text, trimmed, when it's confirmed by Enter or the button, not if the dialog is cancelled
	 * or the text is empty.
	 */
	void ShowTextPrompt(UiSurface& surface, StringView title, StringView label, StringView initialText, std::int32_t maxLength,
		Function<void(StringView)>&& onConfirmed);
}

#endif
