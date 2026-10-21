#include "HelpDialogs.h"

#if defined(WITH_EDITOR)

#include "ShellCommon.h"
#include "TextPrompt.h"
#include "../Tools/EditorContext.h"
#include "../Document/CommandHistory.h"
#include "../Input/ActionRegistry.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiMenu.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		/** Panel of the command palette, as wide as the window allows up to a limit */
		class CommandPalettePanel : public UiPanel
		{
		public:
			// Width of the content in units, at most and at least, and the part of the window it can take
			static constexpr float MaxUnits = 480.0f;
			static constexpr float MinUnits = 200.0f;
			static constexpr float WindowFraction = 0.8f;

			CommandPalettePanel()
				: UiPanel(UiPanelStyle::Floating), _widthElements{}
			{
			}

			void SetWidthElements(UiElement* search, UiElement* list)
			{
				_widthElements[0] = search;
				_widthElements[1] = list;
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				float units = available.X * WindowFraction / context.Scale - Padding.Left - Padding.Right;
				units = std::floor(std::clamp(units, MinUnits, MaxUnits));
				for (UiElement* element : _widthElements) {
					if (element != nullptr) {
						element->Width = units;
					}
				}
				return UiPanel::Measure(context, available);
			}

			// Enter is passed on from the search once it's confirmed, leaving the search confirms it too, but only Enter runs
			bool OnKeyPressed(const KeyboardEvent& event) override
			{
				if ((event.sym == Keys::Return || event.sym == Keys::NumPadEnter) && OnEnter) {
					OnEnter();
					return true;
				}
				return UiPanel::OnKeyPressed(event);
			}

			/** Called when Enter is pressed */
			Function<void()> OnEnter;

		private:
			UiElement* _widthElements[2];
		};

		// Shortcuts are rare enough to be read as a word, unless it's only modifier keys being held
		bool IsModifierKey(Keys key)
		{
			switch (key) {
				case Keys::LCtrl: case Keys::RCtrl: case Keys::LShift: case Keys::RShift: case Keys::LAlt: case Keys::RAlt:
				case Keys::LSuper: case Keys::RSuper:
					return true;
				default:
					return false;
			}
		}

		/**
			All actions with their shortcuts, only the rows seen are drawn. A click on a shortcut, a double click or Enter
			waits for the keys of a new one, Backspace removes it then, Esc keeps it.
		*/
		class ShortcutList : public UiVirtualList
		{
		public:
			// Width of the column of shortcuts in units at least, and the part of the list it takes when that's more
			static constexpr float ShortcutColumn = 130.0f;
			static constexpr float ShortcutColumnFraction = 0.4f;

			explicit ShortcutList(IEditorContext& session)
				: _session(session), _listening(nullptr)
			{
				CountFunc = [this]() { return std::int32_t(_shown.size()); };
				RowText = [this](std::int32_t index) { return _shown[index]->Label; };
				RowTooltip = [this](std::int32_t index) {
					// A shortcut of playing may be the same as one of editing, they don't conflict
					if (_session.GetActions().HasConflict(*_shown[index])) {
						return String(_x("editor", "Another action has the same shortcut, only the first one in the list runs by it - click and press other keys"));
					}
					return String(_shown[index]->Scope == ActionScope::Play
						? _x("editor", "The shortcut works while the level is played - click and press the keys of a new one, Backspace removes it, Esc keeps it")
						: _x("editor", "Click and press the keys of a new shortcut, or a side button of the mouse - Backspace removes it, Esc keeps it"));
				};
				DrawRow = [this](UiPainter& painter, std::int32_t index, const Rectf& rect, bool selected, float hover) {
					DrawShortcutRow(painter, index, rect, selected, hover);
				};
				OnRowPressed = [this](std::int32_t index, Vector2f pos, const UiPointerEvent& event) {
					// A click on the shortcut waits for a new one
					if (pos.X >= Bounds.W - GetShortcutColumn(Bounds.W, painter_Px(ShortcutColumn))) {
						Listen(index);
					}
				};
				OnActivated = [this](std::int32_t index) { Listen(index); };
			}

			/**
				Shows the actions called like a search, or those with a shortcut if the search is one with a modifier, such
				as "Ctrl+S" or a function key, or if @p byKey says it's keys that were pressed
			*/
			void Filter(StringView text, bool byKey = false)
			{
				_shown.clear();
				_listening = nullptr;
				Shortcut shortcut;
				// A single letter is a word to look for too, it's taken as a key only when it was pressed, a side button of
				// the mouse is no word of any action
				StringView trimmed = text.trimmed();
				bool looksLikeKeys = (byKey || trimmed.contains('+') || (trimmed.size() >= 2 && trimmed.size() <= 3 &&
					(trimmed[0] == 'F' || trimmed[0] == 'f') && trimmed[1] >= '0' && trimmed[1] <= '9'));
				bool searchKey = (Shortcut::Parse(text, shortcut) &&
					(looksLikeKeys || shortcut.Key == Shortcut::Mouse4 || shortcut.Key == Shortcut::Mouse5));
				for (const auto& action : _session.GetActions().GetAll()) {
					if (action->Label.empty()) {
						continue;
					}
					if (searchKey ? (action->Key.IsSameAs(shortcut) || action->AlternateKey.IsSameAs(shortcut)) : MatchesSearch(action->Label, text)) {
						_shown.push_back(action.get());
					}
				}
				SetSelected(-1, false);
			}

			bool OnKeyPressed(const KeyboardEvent& event) override
			{
				if (_listening == nullptr) {
					return UiVirtualList::OnKeyPressed(event);
				}
				if (IsModifierKey(event.sym)) {
					// Waits for the key that goes with them
					return true;
				}
				if (event.sym == Keys::Escape) {
					_listening = nullptr;
					return true;
				}
				EditorAction& action = *_listening;
				_listening = nullptr;
				if (event.sym == Keys::Backspace) {
					action.Key = Shortcut();
					Save();
					return true;
				}
				// Cmd counts as Ctrl on Apple platforms, as it does when the shortcut is pressed
				Shortcut shortcut = Shortcut::FromEvent(event);
				if (shortcut.ToString().empty()) {
					// A key that has no name can't be shown or saved, it's not used
					return true;
				}
				// The other actions that had it are left without it, both shortcuts of theirs are saved
				while (EditorAction* other = _session.GetActions().FindByShortcut(shortcut, &action)) {
					if (other->Key.IsSameAs(shortcut)) {
						other->Key = Shortcut();
					}
					if (other->AlternateKey.IsSameAs(shortcut)) {
						other->AlternateKey = Shortcut();
					}
					// TRANSLATORS: Status message in the Keyboard Shortcuts dialog, {} is the name of an action whose shortcut was given to another one
					_session.ShowMessage(_fx("editor", "{} has no shortcut now", other->Label));
				}
				action.Key = shortcut;
				if (action.AlternateKey.IsSameAs(shortcut)) {
					action.AlternateKey = Shortcut();
				}
				Save();
				return true;
			}

			void OnFocusChanged(bool focused) override
			{
				if (!focused) {
					_listening = nullptr;
				}
				UiVirtualList::OnFocusChanged(focused);
			}

		private:
			IEditorContext& _session;
			SmallVector<EditorAction*, 0> _shown;
			EditorAction* _listening;

			float painter_Px(float units) const
			{
				UiSurface* surface = GetSurface();
				return std::round(units * (surface != nullptr ? surface->GetScale() : 1.0f));
			}

			// The column of shortcuts grows with the list, the names keep the rest
			static float GetShortcutColumn(float width, float minWidth)
			{
				return std::max(minWidth, std::round(width * ShortcutColumnFraction));
			}

			void Listen(std::int32_t index)
			{
				if (index < 0 || index >= std::int32_t(_shown.size())) {
					return;
				}
				SetSelected(index, true);
				_listening = _shown[index];
				if (UiSurface* surface = GetSurface()) {
					surface->SetFocus(this);
				}
			}

			void Save()
			{
				if (!_session.GetActions().SaveOverrides(IEditorContext::GetHotkeysPath())) {
					_session.ShowMessage(_x("editor", "The shortcuts couldn't be saved"));
				}
			}

			void DrawShortcutRow(UiPainter& painter, std::int32_t index, const Rectf& rect, bool selected, float hover)
			{
				EditorAction& action = *_shown[index];
				if (selected) {
					painter.RoundRect(rect, UiTheme::Selection);
				} else if (hover > 0.0f) {
					Colorf color = UiTheme::Hover;
					color.A *= hover;
					painter.RoundRect(rect, color);
				}
				float padding = painter.Px(UiTheme::Padding);
				float column = GetShortcutColumn(rect.W, painter.Px(ShortcutColumn));
				painter.PushClip(Rectf(rect.X, rect.Y, std::max(rect.W - column, 1.0f), rect.H));
				painter.TextInRect(action.Label, Rectf(rect.X + padding, rect.Y, rect.W - column - padding, rect.H), UiTheme::Text);
				painter.PopClip();

				// The shortcut and the other one, red if another action has one of them, the first of them runs by it
				String text;
				if (&action == _listening) {
					// TRANSLATORS: In the list of the Keyboard Shortcuts dialog instead of the shortcut, while waiting for the keys of a new one
					text = _x("editor", "Press keys...");
				} else {
					text = action.Key.ToString();
					String alternate = action.AlternateKey.ToString();
					if (!alternate.empty()) {
						text = (text.empty() ? alternate : String(text + ", "_s + alternate));
					}
					if (text.empty()) {
						text = "-"_s;
					}
				}
				bool conflict = (&action != _listening && _session.GetActions().HasConflict(action));
				Rectf shortcutRect(rect.X + rect.W - column, rect.Y, column - padding, rect.H);
				painter.PushClip(shortcutRect);
				painter.TextInRect(text, shortcutRect, conflict ? UiTheme::TextDanger : (&action == _listening ? UiTheme::TextAccent : UiTheme::TextDim),
					Alignment::Right);
				painter.PopClip();
			}
		};

		/** Button that waits for keys and puts them into the search, to find the actions with a shortcut */
		class KeySearchButton : public UiButton
		{
		public:
			KeySearchButton(UiTextBox* search, Function<void(StringView)>&& onFound)
				// TRANSLATORS: Button next to the search in the Keyboard Shortcuts dialog, waits for keys to find the action they run
				: UiButton(_x("editor", "Press Keys")), _search(search), _onFound(Death::move(onFound)), _listening(false)
			{
				Style = UiButtonStyle::Flat;
				Tooltip = _x("editor", "Press the keys of a shortcut, or a side button of the mouse, to find what it does");
				IsCheckedFunc = [this]() { return _listening; };
				OnClick = [this]() {
					_listening = true;
					if (UiSurface* surface = GetSurface()) {
						surface->SetFocus(this);
					}
				};
			}

			bool IsFocusable() const override
			{
				return _listening;
			}

			void OnFocusChanged(bool focused) override
			{
				if (!focused) {
					_listening = false;
				}
				UiButton::OnFocusChanged(focused);
			}

			bool OnKeyPressed(const KeyboardEvent& event) override
			{
				if (!_listening) {
					return UiButton::OnKeyPressed(event);
				}
				if (IsModifierKey(event.sym)) {
					return true;
				}
				_listening = false;
				if (event.sym != Keys::Escape) {
					String text = Shortcut::FromEvent(event).ToString();
					if (!text.empty()) {
						_search->Text = text;
						if (_onFound) {
							_onFound(text);
						}
					}
				}
				if (UiSurface* surface = GetSurface()) {
					surface->SetFocus(nullptr);
				}
				return true;
			}

		private:
			UiTextBox* _search;
			Function<void(StringView)> _onFound;
			bool _listening;
		};

		// Where keymaps are imported from and exported to, the places of levels and the directory of the settings
		void GetKeymapRoots(IEditorContext& session, SmallVectorImpl<UiFileRoot>& roots)
		{
			// TRANSLATORS: Name of a place in the file dialog, the folder of the settings of the game
			roots.push_back(UiFileRoot{ String(_x("editor", "Settings")), String(fs::GetDirectoryName(IEditorContext::GetHotkeysPath())), true });
			session.GetFileRoots(true, roots);
		}
	}

	void ShowCommandPalette(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		// Its key again hides it
		if (surface == nullptr || ReuseSingleDialog(session, "commandPalette", true)) {
			return;
		}

		IEditorContext* s = &session;
		auto panel = std::make_unique<CommandPalettePanel>();
		CommandPalettePanel* panelPtr = panel.get();
		auto* column = panel->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		auto* search = column->Add<UiTextBox>();
		// TRANSLATORS: Placeholder of the search box of the command palette, which finds actions of the editor by name
		search->Placeholder = _x("editor", "Type what to do...");
		auto* scroll = column->Add<UiScrollView>();
		scroll->Height = 180.0f;
		// The widths are set when it's measured, from the size of the window
		panel->SetWidthElements(search, scroll);

		// Runs an action once the palette is closed, it may open a window of its own
		auto run = [s, surface, panelPtr](EditorAction* action) {
			surface->CloseOverlay(panelPtr);
			if (action->CanExecute()) {
				action->Execute();
			}
		};
		auto fill = std::make_shared<Function<void(StringView)>>();
		*fill = [s, scroll, run](StringView text) {
			auto* rows = scroll->SetContent<UiStack>(UiOrientation::Vertical, 1.0f);
			std::int32_t shown = 0;
			for (const auto& action : s->GetActions().GetAll()) {
				if (action->Label.empty() || !action->Execute || !MatchesSearch(action->Label, text)) {
					continue;
				}
				EditorAction* actionPtr = action.get();
				auto* row = rows->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				auto* button = row->Add<UiButton>(action->Label, action->Icon, [run, actionPtr]() { run(actionPtr); });
				button->Style = UiButtonStyle::Flat;
				button->TextAlign = Alignment::Left;
				button->Flex = 1.0f;
				button->IsEnabledFunc = [actionPtr]() { return actionPtr->CanExecute(); };
				String shortcut = action->Key.ToString();
				if (!shortcut.empty()) {
					row->Add<UiLabel>(shortcut, UiTheme::TextDim);
				}
				if (++shown >= 50) {
					break;
				}
			}
			if (shown == 0) {
				rows->Add<UiLabel>(_x("editor", "Nothing is called like that"), UiTheme::TextDisabled);
			}
		};
		search->OnChanged = [fill](StringView text) { (*fill)(text); };
		panel->OnEnter = [s, run, search]() {
			// The first one that can be done, nothing without a search, the first action of all would be a guess
			StringView text = search->Text;
			if (text.trimmed().empty()) {
				return;
			}
			for (const auto& action : s->GetActions().GetAll()) {
				if (!action->Label.empty() && action->Execute && action->CanExecute() && MatchesSearch(action->Label, text)) {
					run(action.get());
					return;
				}
			}
		};
		(*fill)({});

		UiOverlayOptions options;
		options.Centered = true;
		ShowSingleDialog(session, "commandPalette", Death::move(panel), Death::move(options));
		search->BeginEdit();
	}

	void ShowShortcutsDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		IEditorContext* s = &session;
		// TRANSLATORS: Title of the dialog that lists and changes the shortcuts of the editor
		auto dialog = std::make_unique<UiDialog>(_x("editor", "Keyboard Shortcuts"));
		UiDialog* dialogPtr = dialog.get();
		// Wide enough for long names next to two shortcuts, the list grows with it
		dialog->MinScreenWidthFraction = 0.58f;
		auto* content = dialog->GetContent();

		// What isn't an action, so isn't in the list, in rows as the list has them
		struct MouseHint { const char* Action; const char* Input; };
		static const MouseHint MouseHints[] = {
			// TRANSLATORS: Rows above the list of the Keyboard Shortcuts dialog, what is done (verbs) and by which mouse input
			{ NC_("editor", "Zoom"), NC_("editor", "Mouse wheel") },
			{ NC_("editor", "Move the view"), NC_("editor", "Middle drag, Space + drag") },
			{ NC_("editor", "Pick what's under the pointer"), NC_("editor", "Alt + click") },
			{ NC_("editor", "Erase or pick"), NC_("editor", "Right button") },
		};
		auto* hints = content->Add<UiStack>(UiOrientation::Vertical, 1.0f);
		for (const MouseHint& hint : MouseHints) {
			auto* row = hints->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<UiLabel>(_x("editor", hint.Action), UiTheme::TextDim)->Flex = 1.0f;
			row->Add<UiLabel>(_x("editor", hint.Input), UiTheme::TextDim, Alignment::Right);
		}

		// A name to look for, or the keys of a shortcut ("Ctrl+S"), typed or pressed
		auto* searchRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		auto* search = searchRow->Add<UiTextBox>();
		// TRANSLATORS: Placeholder of the search box in the Keyboard Shortcuts dialog, "Ctrl+S" is an example of a shortcut as the list shows it
		search->Placeholder = _x("editor", "Search, or keys such as Ctrl+S...");
		search->Flex = 1.0f;
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 416.0f;
		scroll->Height = 230.0f;
		ShortcutList* list = scroll->SetContent<ShortcutList>(session);

		auto fill = std::make_shared<Function<void(StringView)>>();
		*fill = [list](StringView text) { list->Filter(text); };
		search->OnChanged = [fill](StringView text) { (*fill)(text); };
		searchRow->Add<KeySearchButton>(search, [list](StringView text) { list->Filter(text, true); });
		(*fill)({});

		// Sets of shortcuts like those of other level editors, and keymaps of other computers
		auto* keymapRow = content->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
		// TRANSLATORS: Label in the Keyboard Shortcuts dialog before the choice of shortcuts like those of another level editor
		keymapRow->Add<UiLabel>(_x("editor", "Shortcuts like"), UiTheme::TextDim);
		// TRANSLATORS: Text of the choice of shortcuts like those of another level editor in the Keyboard Shortcuts dialog (verb)
		auto* presets = keymapRow->Add<UiDropdown>([]() { return String(_x("editor", "Choose")); }, [s, fill, search](SmallVectorImpl<UiMenuEntry>& entries) {
			struct PresetChoice { const char* Label; KeymapPreset Preset; };
			static const PresetChoice Choices[] = {
				// TRANSLATORS: Level editors whose shortcuts can be chosen in the Keyboard Shortcuts dialog, the first one is this editor, the others are names of programs
				{ NC_("editor", "This Editor"), KeymapPreset::Default },
				{ NC_("editor", "Jazz Creation Station"), KeymapPreset::Jcs },
				{ NC_("editor", "MLLE"), KeymapPreset::Mlle },
			};
			for (const PresetChoice& choice : Choices) {
				UiMenuEntry entry;
				entry.Label = _x("editor", choice.Label);
				KeymapPreset preset = choice.Preset;
				entry.Execute = [s, fill, search, preset]() {
					UiSurface* surface = s->GetSurface();
					if (surface == nullptr) {
						return;
					}
					// The shortcuts changed by hand would be lost
					UiMessageDialog::Show(*surface, _x("editor", "Keyboard Shortcuts"), _x("editor", "Replace all shortcuts by the chosen ones? The ones you changed are lost."),
						// TRANSLATORS: Buttons of the message box asking before replacing all shortcuts by those of another level editor
						{ _x("editor", "Replace"), _x("editor", "Cancel") }, [s, fill, search, preset](std::int32_t button) {
							if (button != 0) {
								return;
							}
							SmallVector<String, 8> cleared;
							s->GetActions().ApplyPreset(preset, &cleared);
							s->GetActions().SaveOverrides(IEditorContext::GetHotkeysPath());
							(*fill)(search->Text);
							s->ShowMessage(cleared.empty() ? String(_x("editor", "The shortcuts were replaced"))
								: _fx("editor", "The shortcuts were replaced, {} actions gave their key away", std::int32_t(cleared.size())));
						});
				};
				entries.push_back(Death::move(entry));
			}
		});
		presets->Width = 120.0f;
		keymapRow->Add<UiElement>()->Flex = 1.0f;
		// TRANSLATORS: Button in the Keyboard Shortcuts dialog, reads shortcuts from a file (verb)
		keymapRow->Add<UiButton>(_x("editor", "Import"), EditorIcon::Folder, [s, fill, search]() {
			UiSurface* surface = s->GetSurface();
			if (surface == nullptr) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			GetKeymapRoots(*s, roots);
			// TRANSLATORS: Title of the file dialog that reads shortcuts from a file
			UiFileDialog::Show(*surface, _x("editor", "Import Shortcuts"), UiFileDialogMode::Open, Death::move(roots), ".json"_s, {}, {}, [s, fill, search](StringView path) {
				if (!s->GetActions().ImportKeymap(path)) {
					s->ShowMessage(_x("editor", "The shortcuts couldn't be read"));
					return;
				}
				s->GetActions().SaveOverrides(IEditorContext::GetHotkeysPath());
				(*fill)(search->Text);
				s->ShowMessage(_x("editor", "The shortcuts were imported"));
			});
		})->Tooltip = _x("editor", "Take the shortcuts of a file exported elsewhere");
		// TRANSLATORS: Button in the Keyboard Shortcuts dialog, saves all shortcuts into a file (verb)
		keymapRow->Add<UiButton>(_x("editor", "Export"), EditorIcon::Export, [s]() {
			UiSurface* surface = s->GetSurface();
			if (surface == nullptr) {
				return;
			}
			SmallVector<UiFileRoot, 4> roots;
			GetKeymapRoots(*s, roots);
			// TRANSLATORS: Title of the file dialog that saves all shortcuts into a file
			UiFileDialog::Show(*surface, _x("editor", "Export Shortcuts"), UiFileDialogMode::Save, Death::move(roots), ".json"_s, {}, "Shortcuts.json"_s, [s](StringView path) {
				s->ShowMessage(s->GetActions().ExportKeymap(path) ? String(_x("editor", "The shortcuts were exported")) : String(_x("editor", "The shortcuts couldn't be saved")));
			});
		})->Tooltip = _x("editor", "Save all shortcuts into a file, to take them to another computer");

		// TRANSLATORS: Button in the Keyboard Shortcuts dialog, sets all shortcuts back to how they were
		dialog->AddButton(_x("editor", "Reset All"), [s, fill, search]() {
			UiSurface* surface = s->GetSurface();
			if (surface == nullptr) {
				return;
			}
			// Every shortcut changed would be lost, it can't be undone
			UiMessageDialog::Show(*surface, _x("editor", "Reset All"), _x("editor", "Set all shortcuts back to how they were? The ones you changed are lost."),
				// TRANSLATORS: Buttons of the message box asking before setting all shortcuts back to how they were
				{ _x("editor", "Reset All"), _x("editor", "Cancel") }, [s, fill, search](std::int32_t button) {
					if (button != 0) {
						return;
					}
					// The other shortcuts too, a conflict may have taken them away
					s->GetActions().ResetToDefaults();
					s->GetActions().SaveOverrides(IEditorContext::GetHotkeysPath());
					(*fill)(search->Text);
					s->ShowMessage(_x("editor", "All shortcuts are as they were"));
				});
		});
		// Closed by its cross or Escape, Reset All isn't what they do
		dialog->OnCancel = [dialogPtr]() { dialogPtr->Close(); };

		ShowModal(*surface, Death::move(dialog));
	}

	void ShowHistoryDialog(IEditorContext& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr || ReuseSingleDialog(session, "history")) {
			return;
		}

		IEditorContext* s = &session;
		// TRANSLATORS: Title of the dialog that lists the changes that can be undone and redone
		auto dialog = std::make_unique<UiDialog>(_x("editor", "History"));
		// Narrow by default, as the names of the changes are short, and resized as needed; the size stays for the session
		static Vector2f lastSize(180.0f, 300.0f);
		dialog->Resizable = true;
		dialog->MinResizeSize = Vector2f(140.0f, 140.0f);
		dialog->Width = lastSize.X;
		dialog->Height = lastSize.Y;
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();
		auto* scroll = content->Add<UiScrollView>();
		scroll->Flex = 1.0f;
		// The newest changes at the end, shown first, and followed as more are made, unless it's scrolled back
		scroll->FollowEnd = true;

		// The list follows the history, it's rebuilt when the number of changes or the current one differs
		struct ListState { std::int32_t Count = -1; std::int32_t Done = -1; const CommandHistory* History = nullptr; };
		auto state = std::make_shared<ListState>();
		state->History = &session.GetHistory();
		auto rebuild = std::make_shared<Function<void()>>();
		*rebuild = [s, scroll, state]() {
			CommandHistory& history = s->GetHistory();
			state->Count = history.GetCount();
			state->Done = history.GetDoneCount();
			auto* rows = scroll->SetContent<UiStack>(UiOrientation::Vertical, 1.0f);
			// Back to before all changes
			// TRANSLATORS: First entry of the History dialog, the level as it was when it was opened, before all changes
			auto* first = rows->Add<UiButton>(_x("editor", "Opened"), EditorIcon::None, [s]() {
				while (s->GetHistory().GetDoneCount() > 0 && s->Undo()) {
				}
			});
			first->Style = UiButtonStyle::Flat;
			first->TextAlign = Alignment::Left;
			first->TextColor = UiTheme::TextDim;
			first->IsCheckedFunc = [s]() { return (s->GetHistory().GetDoneCount() == 0); };
			for (std::int32_t i = 0; i < state->Count; i++) {
				std::int32_t target = i + 1;
				auto* button = rows->Add<UiButton>(history.GetName(i), EditorIcon::None, [s, target]() {
					while (s->GetHistory().GetDoneCount() > target && s->Undo()) {
					}
					while (s->GetHistory().GetDoneCount() < target && s->Redo()) {
					}
				});
				button->Style = UiButtonStyle::Flat;
				button->TextAlign = Alignment::Left;
				button->IsCheckedFunc = [s, target]() { return (s->GetHistory().GetDoneCount() == target); };
				// Undone changes are dimmed, they're gone once something else is changed
				if (target > state->Done) {
					button->TextColor = UiTheme::TextDisabled;
					button->Tooltip = _x("editor", "Undone, it can be redone");
				}
			}
		};
		(*rebuild)();
		// Checked every frame, the history changes by the editor too while the window is open, and another level has its own
		content->Add<UiUpdateWatcher>([s, state, rebuild, dialogPtr](float) {
			lastSize = Vector2f(dialogPtr->Width, dialogPtr->Height);
			CommandHistory& history = s->GetHistory();
			if (history.GetCount() != state->Count || history.GetDoneCount() != state->Done || &history != state->History) {
				state->History = &history;
				(*rebuild)();
			}
		});

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		ShowSingleDialog(session, "history", Death::move(dialog), Death::move(options));
	}
}

#endif
