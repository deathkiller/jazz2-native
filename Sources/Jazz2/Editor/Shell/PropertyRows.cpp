#include "PropertyRows.h"

#if defined(WITH_EDITOR)

#include "ShellCommon.h"
#include "../../UI/Windows/Menu.h"
#include "../UI/EditorUi.h"

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

	Dropdown* AddChoiceDropdown(Element* row, ArrayView<const PropertyChoice> choices, Function<std::int32_t()>&& getter,
		Function<void(std::int32_t)>&& setter, Function<String(std::int32_t)>&& otherLabel, Function<bool(std::int32_t)>&& isEnabled)
	{
		auto binding = std::make_shared<ChoiceBinding>();
		binding->Choices = choices;
		binding->Getter = Death::move(getter);
		binding->Setter = Death::move(setter);
		binding->OtherLabel = Death::move(otherLabel);
		binding->IsEnabled = Death::move(isEnabled);

		return row->Add<Dropdown>([binding]() {
			std::int32_t value = binding->Getter();
			for (const PropertyChoice& choice : binding->Choices) {
				if (choice.Value == value) {
					return String(choice.Label);
				}
			}
			return (binding->OtherLabel ? binding->OtherLabel(value) : String());
		}, [binding](SmallVectorImpl<MenuEntry>& entries) {
			for (const PropertyChoice& choice : binding->Choices) {
				std::int32_t value = choice.Value;
				MenuEntry entry;
				entry.Label = choice.Label;
				entry.IsChecked = [binding, value]() { return (binding->Getter() == value); };
				if (binding->IsEnabled) {
					entry.IsEnabled = [binding, value]() { return binding->IsEnabled(value); };
				}
				entry.Execute = [binding, value]() { binding->Setter(value); };
				entries.push_back(Death::move(entry));
			}
		});
	}

	NumberBox* AddByteBox(Element* row, Function<std::uint8_t()>&& getter, Function<void(std::uint8_t, bool)>&& setter)
	{
		return row->Add<NumberBox>([getter = Death::move(getter)]() mutable {
			return float(getter());
		}, [setter = Death::move(setter)](float value, bool final) mutable {
			setter(std::uint8_t(std::clamp<std::int32_t>(std::int32_t(std::round(value)), 0, 255)), final);
		}, 0.0f, 255.0f, 1.0f, 0);
	}

	void ShowRowWhen(Stack* row, Function<bool()>&& isVisible)
	{
		Element* parent = row->GetParent();
		Element* target = (parent != nullptr && runtime_cast<FormRow>(parent) != nullptr ? parent : row);
		target->IsVisibleFunc = Death::move(isVisible);
	}
}

#endif
