#include "HelpDialogs.h"

#if defined(WITH_EDITOR)

#include "../EditorSession.h"
#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		// Whether all words of a search are in a text, whatever the case of the letters
		bool MatchesSearch(StringView text, StringView search)
		{
			String lowerText = StringUtils::lowercase(text);
			String lowerSearch = StringUtils::lowercase(search);
			StringView rest = lowerSearch;
			while (!rest.empty()) {
				auto parts = rest.partition(' ');
				if (!parts[0].empty() && !lowerText.contains(parts[0])) {
					return false;
				}
				rest = parts[2];
			}
			return true;
		}

		/** Takes no room, only calls a function every frame */
		class UpdateWatcher : public UiElement
		{
		public:
			explicit UpdateWatcher(Function<void()>&& onUpdate)
				: _onUpdate(std::move(onUpdate))
			{
			}

			Vector2f Measure(const UiContext& context, Vector2f available) override
			{
				return Vector2f(0.0f, 0.0f);
			}

			void Update(float timeMult) override
			{
				_onUpdate();
				UiElement::Update(timeMult);
			}

		private:
			Function<void()> _onUpdate;
		};

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

		private:
			UiElement* _widthElements[2];
		};

		/** A shortcut of an action, a click waits for the keys of a new one */
		class ShortcutButton : public UiButton
		{
		public:
			ShortcutButton(EditorSession& session, EditorAction& action)
				: UiButton(StringView()), _session(session), _action(action), _listening(false)
			{
				Style = UiButtonStyle::Flat;
				MinWidth = 80.0f;
				OnClick = [this]() {
					if (UiSurface* surface = GetSurface()) {
						_listening = true;
						surface->SetFocus(this);
					}
				};
				Tooltip = _("Click and press the keys of a new shortcut, Backspace removes it, Esc keeps it");
			}

			void Update(float timeMult) override
			{
				Text = (_listening ? String(_("Press keys...")) : _action.Key.ToString());
				if (Text.empty()) {
					Text = "-"_s;
				}
				UiButton::Update(timeMult);
			}

			bool IsFocusable() const override
			{
				return true;
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
				switch (event.sym) {
					case Keys::LCtrl: case Keys::RCtrl: case Keys::LShift: case Keys::RShift: case Keys::LAlt: case Keys::RAlt:
					case Keys::LSuper: case Keys::RSuper:
						// Waits for the key that goes with them
						return true;
					case Keys::Escape:
						StopListening();
						return true;
					case Keys::Backspace:
						_action.Key = Shortcut();
						Save();
						StopListening();
						return true;
					default:
						break;
				}

				Shortcut shortcut;
				shortcut.Key = event.sym;
				if ((event.mod & KeyMod::Ctrl) != 0) shortcut.Modifiers |= KeyMod::Ctrl;
				if ((event.mod & KeyMod::Shift) != 0) shortcut.Modifiers |= KeyMod::Shift;
				if ((event.mod & KeyMod::Alt) != 0) shortcut.Modifiers |= KeyMod::Alt;
				if (shortcut.ToString().empty()) {
					// A key that has no name can't be shown, it's not used
					return true;
				}
				// The other action that had it is left without it
				if (EditorAction* other = _session.GetActions().FindByShortcut(shortcut, &_action)) {
					if (AreSame(other->Key, shortcut)) {
						other->Key = Shortcut();
					} else {
						other->AlternateKey = Shortcut();
					}
					_session.ShowMessage(_f("{} has no shortcut now", other->Label));
				}
				_action.Key = shortcut;
				Save();
				StopListening();
				return true;
			}

		private:
			EditorSession& _session;
			EditorAction& _action;
			bool _listening;

			static bool AreSame(const Shortcut& a, const Shortcut& b)
			{
				return (a.Key == b.Key && a.Modifiers == b.Modifiers);
			}

			void StopListening()
			{
				_listening = false;
				if (UiSurface* surface = GetSurface()) {
					surface->SetFocus(nullptr);
				}
			}

			void Save()
			{
				if (!_session.GetActions().SaveOverrides(EditorSession::GetHotkeysPath())) {
					_session.ShowMessage(_("The shortcuts couldn't be saved"));
				}
			}
		};
	}

	void ShowCommandPalette(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto panel = std::make_unique<CommandPalettePanel>();
		CommandPalettePanel* panelPtr = panel.get();
		auto* column = panel->Add<UiStack>(UiOrientation::Vertical, UiTheme::Spacing);
		auto* search = column->Add<UiTextBox>();
		search->Placeholder = _("Type what to do...");
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
				rows->Add<UiLabel>(_("Nothing is called like that"), UiTheme::TextDisabled);
			}
		};
		search->OnChanged = [fill](StringView text) { (*fill)(text); };
		search->OnCommit = [s, run](StringView text) {
			// The first one that can be done
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
		surface->PushOverlay(std::move(panel), std::move(options));
		search->BeginEdit();
	}

	void ShowShortcutsDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("Keyboard Shortcuts"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();

		// What isn't an action, so isn't in the list, in rows as the list has them
		struct MouseHint { const char* Action; const char* Input; };
		static const MouseHint MouseHints[] = {
			{ N_("Zoom"), N_("Mouse wheel") },
			{ N_("Move the view"), N_("Middle drag, Space + drag") },
			{ N_("Pick what's under the pointer"), N_("Alt + click") },
			{ N_("Erase or pick"), N_("Right button") },
		};
		auto* hints = content->Add<UiStack>(UiOrientation::Vertical, 1.0f);
		for (const MouseHint& hint : MouseHints) {
			auto* row = hints->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
			row->Add<UiLabel>(_(hint.Action), UiTheme::TextDim)->Flex = 1.0f;
			row->Add<UiLabel>(_(hint.Input), UiTheme::TextDim, Alignment::Right);
		}

		auto* search = content->Add<UiTextBox>();
		search->Placeholder = _("Search...");
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 416.0f;
		scroll->Height = 230.0f;

		auto fill = std::make_shared<Function<void(StringView)>>();
		*fill = [s, scroll](StringView text) {
			auto* rows = scroll->SetContent<UiStack>(UiOrientation::Vertical, 1.0f);
			for (const auto& action : s->GetActions().GetAll()) {
				if (action->Label.empty() || !MatchesSearch(action->Label, text)) {
					continue;
				}
				auto* row = rows->Add<UiStack>(UiOrientation::Horizontal, UiTheme::Spacing);
				row->Add<UiLabel>(action->Label)->Flex = 1.0f;
				row->Add<ShortcutButton>(*s, *action);
			}
		};
		search->OnChanged = [fill](StringView text) { (*fill)(text); };
		(*fill)({});

		dialog->AddButton(_("Reset All"), [s, fill, search]() {
			for (const auto& action : s->GetActions().GetAll()) {
				action->Key = action->DefaultKey;
			}
			s->GetActions().SaveOverrides(EditorSession::GetHotkeysPath());
			(*fill)(search->Text);
			s->ShowMessage(_("All shortcuts are as they were"));
		});
		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}

	void ShowHistoryDialog(EditorSession& session)
	{
		UiSurface* surface = session.GetSurface();
		if (surface == nullptr) {
			return;
		}

		EditorSession* s = &session;
		auto dialog = std::make_unique<UiDialog>(_("History"));
		UiDialog* dialogPtr = dialog.get();
		auto* content = dialog->GetContent();
		content->Add<UiLabel>(_("Click a change to go back or forward to it"), UiTheme::TextDim);
		auto* scroll = content->Add<UiScrollView>();
		scroll->Width = 240.0f;
		scroll->Height = 230.0f;

		// The list follows the history, it's rebuilt when the number of changes or the current one differs
		struct ListState { std::int32_t Count = -1; std::int32_t Done = -1; };
		auto state = std::make_shared<ListState>();
		auto rebuild = std::make_shared<Function<void()>>();
		*rebuild = [s, scroll, state]() {
			CommandHistory& history = s->GetHistory();
			state->Count = history.GetCount();
			state->Done = history.GetDoneCount();
			auto* rows = scroll->SetContent<UiStack>(UiOrientation::Vertical, 1.0f);
			// Back to before all changes
			auto* first = rows->Add<UiButton>(_("Opened"), EditorIcon::None, [s]() {
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
					button->Tooltip = _("Undone, it can be redone");
				}
			}
		};
		(*rebuild)();
		// Checked every frame, the history changes by the editor too while the window is open
		content->Add<UpdateWatcher>([s, state, rebuild]() {
			CommandHistory& history = s->GetHistory();
			if (history.GetCount() != state->Count || history.GetDoneCount() != state->Done) {
				(*rebuild)();
			}
		});

		dialog->AddButton(_("Close"), [dialogPtr]() { dialogPtr->Close(); }, true);

		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface->PushOverlay(std::move(dialog), std::move(options));
	}
}

#endif
