#include "TextPrompt.h"

#if defined(WITH_EDITOR)

#include "../UI/UiDialogs.h"
#include "../UI/UiSurface.h"

#include "../../../nCine/I18n.h"

#include <memory>

namespace Jazz2::Editor
{
	UiElement* ShowModal(UiSurface& surface, std::unique_ptr<UiElement> dialog)
	{
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		return surface.PushOverlay(Death::move(dialog), Death::move(options));
	}

	UiElement* ShowModeless(UiSurface& surface, std::unique_ptr<UiElement> dialog, Function<void()>&& onClosed)
	{
		UiOverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		options.OnClosed = Death::move(onClosed);
		return surface.PushOverlay(Death::move(dialog), Death::move(options));
	}

	void ShowTextPrompt(UiSurface& surface, StringView title, StringView label, StringView initialText, std::int32_t maxLength,
		Function<void(StringView)>&& onConfirmed)
	{
		auto dialog = std::make_unique<UiDialog>(title);
		UiDialog* dialogPtr = dialog.get();
		UiStack* content = dialog->GetContent();
		if (!label.empty()) {
			content->Add<UiLabel>(label, UiTheme::TextDim);
		}
		auto* box = content->Add<UiTextBox>(initialText);
		box->Width = 220.0f;
		box->MaxLength = maxLength;

		auto sharedConfirmed = std::make_shared<Function<void(StringView)>>(Death::move(onConfirmed));
		// TRANSLATORS: Button in a dialog that asks for a text, such as a name, confirms it
		dialog->AddButton(_x("editor", "OK"), [dialogPtr, box, sharedConfirmed]() {
			String text = box->Text.trimmed();
			dialogPtr->Close();
			if (!text.empty() && *sharedConfirmed) {
				(*sharedConfirmed)(text);
			}
		}, true);
		// TRANSLATORS: Button in a dialog that asks for a text, closes it without a change
		dialog->AddButton(_x("editor", "Cancel"), [dialogPtr]() { dialogPtr->Close(); });

		ShowModal(surface, Death::move(dialog));
		// Typed right away
		surface.SetFocus(box);
		box->BeginEdit();
	}
}

#endif
