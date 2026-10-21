#include "PropertyRows.h"

#if defined(WITH_EDITOR)

#include "ShellCommon.h"
#include "../UI/UiMenu.h"

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace Jazz2::Editor
{
	namespace
	{
		// What a dropdown of choices is bound to, shared by the text it shows and the entries of each menu it opens
		struct ChoiceBinding
		{
			ArrayView<const PropertyChoice> Choices;
			Function<std::int32_t()> Getter;
			Function<void(std::int32_t)> Setter;
			Function<String(std::int32_t)> OtherLabel;
			Function<bool(std::int32_t)> IsEnabled;
		};
	}

	UiDropdown* AddChoiceDropdown(UiElement* row, ArrayView<const PropertyChoice> choices, Function<std::int32_t()>&& getter,
		Function<void(std::int32_t)>&& setter, Function<String(std::int32_t)>&& otherLabel, Function<bool(std::int32_t)>&& isEnabled)
	{
		auto binding = std::make_shared<ChoiceBinding>();
		binding->Choices = choices;
		binding->Getter = Death::move(getter);
		binding->Setter = Death::move(setter);
		binding->OtherLabel = Death::move(otherLabel);
		binding->IsEnabled = Death::move(isEnabled);

		return row->Add<UiDropdown>([binding]() {
			std::int32_t value = binding->Getter();
			for (const PropertyChoice& choice : binding->Choices) {
				if (choice.Value == value) {
					return String(_x("editor", choice.Label));
				}
			}
			return (binding->OtherLabel ? binding->OtherLabel(value) : String());
		}, [binding](SmallVectorImpl<UiMenuEntry>& entries) {
			for (const PropertyChoice& choice : binding->Choices) {
				std::int32_t value = choice.Value;
				UiMenuEntry entry;
				entry.Label = _x("editor", choice.Label);
				entry.IsChecked = [binding, value]() { return (binding->Getter() == value); };
				if (binding->IsEnabled) {
					entry.IsEnabled = [binding, value]() { return binding->IsEnabled(value); };
				}
				entry.Execute = [binding, value]() { binding->Setter(value); };
				entries.push_back(Death::move(entry));
			}
		});
	}

	UiNumberBox* AddByteBox(UiElement* row, Function<std::uint8_t()>&& getter, Function<void(std::uint8_t, bool)>&& setter)
	{
		return row->Add<UiNumberBox>([getter = Death::move(getter)]() mutable {
			return float(getter());
		}, [setter = Death::move(setter)](float value, bool final) mutable {
			setter(std::uint8_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 255)), final);
		}, 0.0f, 255.0f, 1.0f, 0);
	}

	void ShowRowWhen(UiStack* row, Function<bool()>&& isVisible)
	{
		UiElement* parent = row->GetParent();
		UiElement* target = (parent != nullptr && runtime_cast<UiFormRow>(parent) != nullptr ? parent : row);
		target->IsVisibleFunc = Death::move(isVisible);
	}
}

#endif
