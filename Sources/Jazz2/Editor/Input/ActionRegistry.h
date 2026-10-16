#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../UI/EditorIcons.h"
#include "../../../nCine/Input/InputEvents.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#include <memory>

using namespace nCine;

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::Function;
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/** @brief Key and modifiers that run an action */
	struct Shortcut
	{
		/** @brief Key */
		Keys Key = Keys::Unknown;
		/** @brief Modifiers, a combination of @ref KeyMod::Ctrl, @ref KeyMod::Shift and @ref KeyMod::Alt */
		std::int32_t Modifiers = 0;

		/** @brief Returns `true` if a key event matches */
		bool Matches(const KeyboardEvent& event) const;
		/** @brief Returns the shortcut as text, such as `Ctrl+Z` */
		String ToString() const;
	};

	/**
		@brief Something the user can do in the level editor

		The same action is behind a menu item, a toolbar button and a shortcut, so they all agree on what it does,
		what it's called and whether it can be done now.
	*/
	struct EditorAction
	{
		/** @brief Identifier, it doesn't change between versions */
		String Id;
		/** @brief Name shown to the user */
		String Label;
		/** @brief Icon */
		EditorIcon Icon = EditorIcon::None;
		/** @brief Shortcut, may be none */
		Shortcut Key;
		/** @brief Another shortcut, may be none */
		Shortcut AlternateKey;
		/** @brief Shortcut the action has unless the user changed it, see @ref ActionRegistry::CaptureDefaults() */
		Shortcut DefaultKey;
		/** @brief What it does */
		Function<void()> Execute;
		/** @brief Returns whether it can be done now, always if not set */
		Function<bool()> IsEnabled;
		/** @brief Returns whether it's turned on, for actions that toggle something */
		Function<bool()> IsChecked;

		/** @brief Returns `true` if it can be done now */
		bool CanExecute();
		/** @brief Returns the label with the shortcut, as a tooltip */
		String GetTooltip() const;
	};

	/**
		@brief All actions of the level editor
	*/
	class ActionRegistry
	{
	public:
		/** @brief Adds an action */
		EditorAction& Add(StringView id, StringView label, EditorIcon icon, Function<void()>&& execute);
		/** @brief Returns an action, or `nullptr` */
		EditorAction* Find(StringView id);
		/** @brief Runs an action if it can be done now, returns `false` if it can't */
		bool Execute(StringView id);
		/** @brief Runs the action whose shortcut matches a key, returns `true` if there was one */
		bool HandleKey(const KeyboardEvent& event);
		/** @brief Returns all actions, in the order they were added */
		const SmallVector<std::unique_ptr<EditorAction>, 0>& GetAll() const {
			return _actions;
		}
		/** @brief Returns the action that has a shortcut, other than @p except, or `nullptr` */
		EditorAction* FindByShortcut(const Shortcut& shortcut, const EditorAction* except = nullptr);

		/** @brief Remembers the shortcuts of all actions as their defaults, after they're all added */
		void CaptureDefaults();
		/**
		 * @brief Changes shortcuts as a file of the user says, `Jazz2.EditorHotkeys.json`
		 *
		 * The file has an object of action identifiers, each with `[key, modifiers]`, the key as @ref Keys, or
		 * `null` for no shortcut. Actions not in it keep their default.
		 */
		void LoadOverrides(StringView path);
		/** @brief Saves the shortcuts that differ from the defaults, returns `false` if they can't be saved */
		bool SaveOverrides(StringView path) const;

	private:
		SmallVector<std::unique_ptr<EditorAction>, 0> _actions;
	};
}

#endif
