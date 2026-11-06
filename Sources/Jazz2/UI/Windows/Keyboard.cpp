#include "Keyboard.h"

#if defined(WITH_UI_WINDOWS)

#include "Surface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringUtils.h>

#include <cstring>

using namespace Death::Containers::Literals;

namespace Jazz2::UI::Windows
{
	namespace
	{
		/** A key, which leaves the focus in the box it types into */
		class KeyButton : public Button
		{
		public:
			KeyButton(StringView text, IconId icon, Function<void()>&& onClick)
				: Button(text, icon, Death::move(onClick))
			{
			}

			bool KeepsFocus() const override
			{
				return true;
			}
		};

		const char* const Rows[] = { "1234567890-", "qwertyuiop/", "asdfghjkl'.", "zxcvbnm,!?_" };
	}

	OnScreenKeyboard* OnScreenKeyboard::_instance = nullptr;

	OnScreenKeyboard::OnScreenKeyboard()
		: Panel(PanelStyle::Floating, Orientation::Vertical, 2.0f, Thickness(Theme::Padding)), _shift(false)
	{
		_instance = this;
		for (const char* row : Rows) {
			auto* line = Add<Stack>(Orientation::Horizontal, 2.0f);
			for (const char* c = row; *c != '\0'; c++) {
				char text[2] = { *c, '\0' };
				String typed(text);
				auto* key = line->Add<KeyButton>(StringView(text, 1), StockIcon::None, [this, typed]() {
					Type(_shift ? StringUtils::uppercase(typed) : typed);
				});
				key->Width = 18.0f;
				key->Height = 18.0f;
				if (*c >= 'a' && *c <= 'z') {
					_letterKeys.push_back(key);
				}
			}
		}
		auto* line = Add<Stack>(Orientation::Horizontal, 2.0f);
		// TRANSLATORS: Keys of the keyboard on the screen, as a real keyboard names them
		auto* shift = line->Add<KeyButton>(_x("editor", "Shift"), StockIcon::ArrowUp, [this]() {
			_shift = !_shift;
			UpdateLetters();
		});
		shift->IsCheckedFunc = [this]() { return _shift; };
		// TRANSLATORS: Key of the keyboard on the screen, the space bar, as a real keyboard names it
		auto* space = line->Add<KeyButton>(_x("editor", "Space"), StockIcon::None, [this]() { Type(" "_s); });
		space->Flex = 1.0f;
		// TRANSLATORS: Tooltip of a key of the keyboard on the screen, moves the text cursor one character to the left
		line->Add<KeyButton>(StringView(), StockIcon::ChevronLeft, [this]() { Press(Keys::Left); })->Tooltip = _x("editor", "Move left");
		// TRANSLATORS: Tooltip of a key of the keyboard on the screen, moves the text cursor one character to the right
		line->Add<KeyButton>(StringView(), StockIcon::ChevronRight, [this]() { Press(Keys::Right); })->Tooltip = _x("editor", "Move right");
		line->Add<KeyButton>(StringView(), StockIcon::Delete, [this]() { Press(Keys::Backspace); })->Tooltip = _x("editor", "Delete the character before");
		// TRANSLATORS: Key of the keyboard on the screen, confirms the text and hides the keyboard
		auto* done = line->Add<KeyButton>(_x("editor", "Done"), StockIcon::Check, [this]() {
			// Taking the focus away confirms the text, Enter would break a line in a box of more lines first
			if (Surface* surface = GetSurface()) {
				surface->SetFocus(nullptr);
				surface->CloseOverlay(this);
			}
		});
		done->Style = ButtonStyle::Accent;
	}

	OnScreenKeyboard::~OnScreenKeyboard()
	{
		if (_instance == this) {
			_instance = nullptr;
		}
	}

	void OnScreenKeyboard::Show(Surface& surface)
	{
		if (_instance != nullptr) {
			return;
		}
		OverlayOptions options;
		// At the bottom in the middle, it's moved to fit the screen
		Rectf safe = surface.GetSafeBounds();
		options.Position = Vector2f(safe.X + safe.W * 0.5f - surface.GetScale() * 120.0f, 1.0e6f);
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface.PushOverlay(std::make_unique<OnScreenKeyboard>(), Death::move(options));
	}

	bool OnScreenKeyboard::IsShown(const Surface& surface)
	{
		return (_instance != nullptr);
	}

	void OnScreenKeyboard::Update(float timeMult)
	{
		Panel::Update(timeMult);
		// Gone with the box it typed into
		Surface* surface = GetSurface();
		if (surface != nullptr && (surface->GetFocus() == nullptr || runtime_cast<TextBox>(surface->GetFocus()) == nullptr)) {
			surface->CloseOverlay(this);
		}
	}

	bool OnScreenKeyboard::KeepsFocus() const
	{
		return true;
	}

	void OnScreenKeyboard::Type(StringView text)
	{
		Surface* surface = GetSurface();
		if (surface == nullptr) {
			return;
		}
		TextInputEvent event;
		std::size_t length = std::min(text.size(), sizeof(event.Text) - 1);
		std::memcpy(event.Text, text.data(), length);
		event.Text[length] = '\0';
		event.Length = std::int32_t(length);
		surface->OnTextInput(event);
		// A capital letter, then the small ones again
		if (_shift) {
			_shift = false;
			UpdateLetters();
		}
	}

	void OnScreenKeyboard::Press(Keys key)
	{
		if (Surface* surface = GetSurface()) {
			KeyboardEvent event;
			event.Sym = key;
			event.Mod = 0;
			surface->OnKeyPressed(event);
		}
	}

	void OnScreenKeyboard::UpdateLetters()
	{
		for (Button* key : _letterKeys) {
			key->Text = (_shift ? StringUtils::uppercase(key->Text) : StringUtils::lowercase(key->Text));
		}
	}
}

#endif
