#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ActionIds.h"
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
	using Death::Containers::SmallVectorImpl;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/**
		@brief Key and modifiers that run an action

		On Apple platforms, <kbd>Cmd</kbd> is taken as <kbd>Ctrl</kbd> and shown as `Cmd`, elsewhere a key pressed with
		the system key (<kbd>Win</kbd>) matches no shortcut. The side buttons of a mouse (@ref MouseButton::Fourth and
		@ref MouseButton::Fifth) have no use of their own in the editor, so they can be shortcuts too. They stand for
		the keys @ref Mouse4 and @ref Mouse5, which no keyboard has, a press of them is passed on as a press of that key.
	*/
	struct Shortcut
	{
		/** @brief Key that stands for @ref MouseButton::Fourth, written as `Mouse 4` */
		static constexpr Keys Mouse4 = Keys(std::int32_t(Keys::Count) + 1);
		/** @brief Key that stands for @ref MouseButton::Fifth, written as `Mouse 5` */
		static constexpr Keys Mouse5 = Keys(std::int32_t(Keys::Count) + 2);

		/** @brief Key, @ref Keys::Unknown for none */
		Keys Key = Keys::Unknown;
		/** @brief Modifiers, a combination of @ref KeyMod::Ctrl, @ref KeyMod::Shift and @ref KeyMod::Alt */
		std::int32_t Modifiers = 0;

		/** @brief Returns the shortcut a key event would be, its key and modifiers */
		static Shortcut FromEvent(const KeyboardEvent& event);
		/** @brief Returns the key that stands for a side button of the mouse, @ref Keys::Unknown for a button with a use of its own */
		static Keys GetMouseButtonKey(MouseButton button);
		/** @brief Reads a shortcut from text as @ref ToString() writes it, returns `false` if it isn't one */
		static bool Parse(StringView text, Shortcut& result);

		/** @brief Returns `true` if a key event matches */
		bool Matches(const KeyboardEvent& event) const;
		/** @brief Returns `true` if it's the same shortcut, plus, minus and Enter of the numeric keypad are the same keys as the others, its digits aren't */
		bool IsSameAs(const Shortcut& other) const;
		/** @brief Returns the shortcut as text, such as `Ctrl+Z` or `Shift+Mouse 4`, empty if the key has no name */
		String ToString() const;
	};

	/** @brief When the shortcut of an action works, see @ref EditorAction::Scope */
	enum class ActionScope : std::uint8_t
	{
		Edit,			/**< While the level is edited */
		Play,			/**< While the level is play-tested, keys no action of it has go to the game */
		Both			/**< Always */
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
		/** @brief Other shortcut the action has unless the user changed it, see @ref ActionRegistry::CaptureDefaults() */
		Shortcut DefaultAlternateKey;
		/** @brief What it does */
		Function<void()> Execute;
		/**
		 * @brief What ends it once the key that ran it is released, for an action that lasts while its key is held, or empty
		 *
		 * Called only after it was run by its key, see @ref EditorInputRouter.
		 */
		Function<void()> Release;
		/** @brief Returns whether it can be done now, always if not set */
		Function<bool()> IsEnabled;
		/** @brief Returns whether it's turned on, for actions that toggle something */
		Function<bool()> IsChecked;
		/** @brief Whether its shortcut works while a text is typed, as saving does, the text is confirmed first */
		bool WorksWhileTyping = false;
		/** @brief Whether its shortcut runs it only once while it's held, not again as the key repeats, as marking a corner shouldn't */
		bool IgnoresRepeat = false;
		/**
		 * @brief When its shortcut works
		 *
		 * An action that works only while the level is played may have the same key as one that works only while it's
		 * edited, they don't conflict. A menu item or a button runs it whenever it can be done.
		 */
		ActionScope Scope = ActionScope::Edit;

		/** @brief Returns `true` if it can be done now */
		bool CanExecute();
		/** @brief Returns the label with the shortcut, as a tooltip */
		String GetTooltip() const;
	};

	/** @brief Set of shortcuts like those of another level editor, see @ref ActionRegistry::ApplyPreset() */
	enum class KeymapPreset : std::int32_t
	{
		Default,		/**< The shortcuts of this editor, B grabs a block and Ctrl+E / Shift+E take and place events as in the editors of the original game */
		Jcs,			/**< Like Jazz Creation Station: F and I flip, the number keys choose layers as by default */
		Mlle			/**< Like the Multi-Layer Level Editor: as JCS, and Ctrl+C / Ctrl+X into the brush, Ctrl+V events, Ctrl+R plays, the comma picks */
	};

	/**
		@brief All actions of the level editor
	*/
	class ActionRegistry
	{
	public:
		/** @brief Adds an action */
		EditorAction& Add(StringView id, StringView label, EditorIcon icon, Function<void()>&& execute);
		/**
		 * @brief Returns an action, or `nullptr`
		 *
		 * An identifier no action has is logged as a warning, once for each, so a typo in one shows in the log.
		 * Identifiers are in @ref ActionIds.
		 */
		EditorAction* Find(StringView id);
		/** @brief Runs an action if it can be done now, returns `false` if it can't, an identifier no action has is logged as in @ref Find() */
		bool Execute(StringView id);
		/**
		 * @brief Runs the action whose shortcut matches a key and works in @p scope, returns `true` if there was one
		 *
		 * With @p repeat the key is held and repeats, an action that ignores it (see @ref EditorAction::IgnoresRepeat)
		 * takes the key without running. @p ran, if given, gets the action that ran, or `nullptr`.
		 */
		bool HandleKey(const KeyboardEvent& event, ActionScope scope = ActionScope::Edit, bool repeat = false, EditorAction** ran = nullptr);
		/** @brief Returns all actions, in the order they were added */
		const SmallVector<std::unique_ptr<EditorAction>, 0>& GetAll() const {
			return _actions;
		}
		/**
		 * @brief Returns the action that has a shortcut, or `nullptr`
		 *
		 * If @p except is set, it's another action whose shortcut works when the one of @p except does.
		 */
		EditorAction* FindByShortcut(const Shortcut& shortcut, const EditorAction* except = nullptr);
		/** @brief Returns `true` if another action has the same shortcut as one of an action and they work at the same time, the first one of them runs then */
		bool HasConflict(const EditorAction& action) const;
		/** @brief Returns `true` if the shortcuts of actions of two scopes work at the same time */
		static bool ScopesOverlap(ActionScope a, ActionScope b) {
			return (a == b || a == ActionScope::Both || b == ActionScope::Both);
		}

		/** @brief Remembers the shortcuts of all actions as their defaults, after they're all added */
		void CaptureDefaults();
		/** @brief Gives all actions their default shortcuts back */
		void ResetToDefaults();
		/**
		 * @brief Changes shortcuts as a file of the user says, `Jazz2.EditorHotkeys.json`
		 *
		 * The file has an object of action identifiers. Each has the shortcut as text, such as `"Ctrl+Shift+Z"` or `"Mouse 4"`, or
		 * `null` for no shortcut, or an object of `"Key"` and `"Alternate"` if the other shortcut differs too.
		 * The form of older versions, `[key, modifiers]` with the key as a number of @ref Keys, is still read.
		 * Actions not in it keep their defaults, a shortcut that can't be read is left out with a warning.
		 */
		bool LoadOverrides(StringView path);
		/** @brief Saves the shortcuts that differ from the defaults, returns `false` if they can't be saved */
		bool SaveOverrides(StringView path) const;
		/** @brief Saves all shortcuts of all actions, to be read by @ref ImportKeymap() elsewhere */
		bool ExportKeymap(StringView path) const;
		/** @brief Gives all actions the shortcuts of a file written by @ref ExportKeymap(), or of an override file, the rest their defaults */
		bool ImportKeymap(StringView path);
		/**
		 * @brief Gives all actions the shortcuts of a preset
		 *
		 * Starts from the defaults, the actions that had a key the preset gives to another one are left without it,
		 * their names are added to @p cleared. Actions the preset names that don't exist are left out.
		 */
		void ApplyPreset(KeymapPreset preset, SmallVectorImpl<String>* cleared = nullptr);
		/** @brief Returns `true` if a key runs an action that works while a text is typed, which is in the editor */
		bool MatchesWhileTyping(const KeyboardEvent& event) const;

		/** @brief Tests reading and writing shortcuts and the presets, returns the number of problems found */
		static std::int32_t SelfTest();

	private:
		SmallVector<std::unique_ptr<EditorAction>, 0> _actions;
		// Identifiers looked up that no action has, each is logged only once
		SmallVector<String, 0> _missingIds;

		// Writes the shortcuts that differ from the defaults, or all of them
		bool WriteKeymap(StringView path, bool all) const;
	};
}

#endif
