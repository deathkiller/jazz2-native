#include "TextPrompt.h"

#if defined(WITH_EDITOR)

#include "../../UI/Windows/Dialogs.h"
#include "../../UI/Windows/Surface.h"
#include "../UI/EditorUi.h"

#include "../../../nCine/I18n.h"

#include <memory>

namespace Jazz2::Editor
{
	Element* ShowModal(Surface& surface, std::unique_ptr<Element> dialog)
	{
		OverlayOptions options;
		options.Centered = true;
		options.Modal = true;
		options.CloseOnOutsideClick = false;
		return surface.PushOverlay(Death::move(dialog), Death::move(options));
	}

	Element* ShowModeless(Surface& surface, std::unique_ptr<Element> dialog, Function<void()>&& onClosed)
	{
		OverlayOptions options;
		options.Centered = true;
		options.Modal = false;
		options.CloseOnOutsideClick = false;
		options.OnClosed = Death::move(onClosed);
		return surface.PushOverlay(Death::move(dialog), Death::move(options));
	}

	void ShowTextPrompt(Surface& surface, StringView title, StringView label, StringView initialText, std::int32_t maxLength,
		Function<void(StringView)>&& onConfirmed)
	{
		auto dialog = std::make_unique<Dialog>(title);
		Dialog* dialogPtr = dialog.get();
		Stack* content = dialog->GetContent();
		if (!label.empty()) {
			content->Add<Label>(label, Theme::TextDim);
		}
		auto* box = content->Add<TextBox>(initialText);
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
