#include "UiKeyboard.h"

#if defined(WITH_EDITOR)

#include "UiSurface.h"

#include "../../../nCine/I18n.h"

#include <Containers/StringUtils.h>

#include <cstring>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		/** A key, which leaves the focus in the box it types into */
		class KeyButton : public UiButton
		{
		public:
			KeyButton(StringView text, EditorIcon icon, Function<void()>&& onClick)
				: UiButton(text, icon, std::move(onClick))
			{
			}

			bool KeepsFocus() const override
			{
				return true;
			}
		};

		const char* const Rows[] = { "1234567890-", "qwertyuiop/", "asdfghjkl'.", "zxcvbnm,!?_" };
	}

	UiOnScreenKeyboard* UiOnScreenKeyboard::_instance = nullptr;

	UiOnScreenKeyboard::UiOnScreenKeyboard()
		: UiPanel(UiPanelStyle::Floating, UiOrientation::Vertical, 2.0f, UiThickness(UiTheme::Padding)), _shift(false)
	{
		_instance = this;
		for (const char* row : Rows) {
			auto* line = Add<UiStack>(UiOrientation::Horizontal, 2.0f);
			for (const char* c = row; *c != '\0'; c++) {
				char text[2] = { *c, '\0' };
				String typed(text);
				auto* key = line->Add<KeyButton>(StringView(text, 1), EditorIcon::None, [this, typed]() {
					Type(_shift ? StringUtils::uppercase(typed) : typed);
				});
				key->Width = 18.0f;
				key->Height = 18.0f;
				if (*c >= 'a' && *c <= 'z') {
					_letterKeys.push_back(key);
				}
			}
		}
		auto* line = Add<UiStack>(UiOrientation::Horizontal, 2.0f);
		// TRANSLATORS: Keys of the keyboard on the screen, as a real keyboard names them
		auto* shift = line->Add<KeyButton>(_("Shift"), EditorIcon::ArrowUp, [this]() {
			_shift = !_shift;
			UpdateLetters();
		});
		shift->IsCheckedFunc = [this]() { return _shift; };
		auto* space = line->Add<KeyButton>(_("Space"), EditorIcon::None, [this]() { Type(" "_s); });
		space->Flex = 1.0f;
		line->Add<KeyButton>(StringView(), EditorIcon::ChevronLeft, [this]() { Press(Keys::Left); })->Tooltip = _("Move left");
		line->Add<KeyButton>(StringView(), EditorIcon::ChevronRight, [this]() { Press(Keys::Right); })->Tooltip = _("Move right");
		line->Add<KeyButton>(StringView(), EditorIcon::Delete, [this]() { Press(Keys::Backspace); })->Tooltip = _("Delete the character before");
		auto* done = line->Add<KeyButton>(_("Done"), EditorIcon::Check, [this]() {
			Press(Keys::Return);
			if (UiSurface* surface = GetSurface()) {
				surface->SetFocus(nullptr);
				surface->CloseOverlay(this);
			}
		});
		done->Style = UiButtonStyle::Accent;
	}

	UiOnScreenKeyboard::~UiOnScreenKeyboard()
	{
		if (_instance == this) {
			_instance = nullptr;
		}
	}

	void UiOnScreenKeyboard::Show(UiSurface& surface)
	{
		if (_instance != nullptr) {
			return;
		}
		UiOverlayOptions options;
		// At the bottom in the middle, it's moved to fit the screen
		options.Position = Vector2f(surface.GetScreenSize().X * 0.5f - surface.GetScale() * 120.0f, 1.0e6f);
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		surface.PushOverlay(std::make_unique<UiOnScreenKeyboard>(), std::move(options));
	}

	bool UiOnScreenKeyboard::IsShown(const UiSurface& surface)
	{
		return (_instance != nullptr);
	}

	void UiOnScreenKeyboard::Update(float timeMult)
	{
		UiPanel::Update(timeMult);
		// Gone with the box it typed into
		UiSurface* surface = GetSurface();
		if (surface != nullptr && (surface->GetFocus() == nullptr || runtime_cast<UiTextBox>(surface->GetFocus()) == nullptr)) {
			surface->CloseOverlay(this);
		}
	}

	bool UiOnScreenKeyboard::KeepsFocus() const
	{
		return true;
	}

	void UiOnScreenKeyboard::Type(StringView text)
	{
		UiSurface* surface = GetSurface();
		if (surface == nullptr) {
			return;
		}
		TextInputEvent event;
		std::size_t length = std::min(text.size(), sizeof(event.text) - 1);
		std::memcpy(event.text, text.data(), length);
		event.text[length] = '\0';
		event.length = std::int32_t(length);
		surface->OnTextInput(event);
		// A capital letter, then the small ones again
		if (_shift) {
			_shift = false;
			UpdateLetters();
		}
	}

	void UiOnScreenKeyboard::Press(Keys key)
	{
		if (UiSurface* surface = GetSurface()) {
			KeyboardEvent event;
			event.sym = key;
			event.mod = 0;
			surface->OnKeyPressed(event);
		}
	}

	void UiOnScreenKeyboard::UpdateLetters()
	{
		for (UiButton* key : _letterKeys) {
			key->Text = (_shift ? StringUtils::uppercase(key->Text) : StringUtils::lowercase(key->Text));
		}
	}
}

#endif
