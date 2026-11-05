#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../UI/Windows/Widgets.h"
#include "../UI/EditorUi.h"

#include <Containers/ArrayView.h>

namespace Jazz2::UI::Windows
{
	class Dropdown;
}

namespace Jazz2::Editor
{
	/**
		@brief A value of a dropdown of fixed choices and the text it's shown with, see @ref AddChoiceDropdown()
	*/
	struct PropertyChoice
	{
		/** @brief Text as it's shown, translated by `_x()` where the choice is made */
		String Label;
		/** @brief Value */
		std::int32_t Value;
	};

	/**
	 * @brief Adds a dropdown of fixed choices bound to a value to a row of a form
	 *
	 * It shows the label of the choice the value is, a value that is none of them is shown by @p otherLabel, or as
	 * nothing without it. A choice is checked in the menu while the value is it, @p isEnabled can leave some of them
	 * disabled. @p choices have to stay as long as the dropdown, a static table of them does.
	 */
	Dropdown* AddChoiceDropdown(Element* row, ArrayView<const PropertyChoice> choices, Function<std::int32_t()>&& getter,
		Function<void(std::int32_t)>&& setter, Function<String(std::int32_t)>&& otherLabel = {}, Function<bool(std::int32_t)>&& isEnabled = {});

	/**
	 * @brief Adds a number box of a byte bound to a value to a row of a form
	 *
	 * The value typed or dragged is rounded and kept from 0 to 255, @p setter gets whether it's final or only shown
	 * while it's dragged, as @ref UI::Windows::NumberBox::Setter does.
	 */
	NumberBox* AddByteBox(Element* row, Function<std::uint8_t()>&& getter, Function<void(std::uint8_t, bool)>&& setter);

	/**
	 * @brief Shows a row of a form only while it applies
	 *
	 * The label of a row of a @ref UI::Windows::Form is shown and hidden with it, a row elsewhere is shown and hidden itself.
	 */
	void ShowRowWhen(Stack* row, Function<bool()>&& isVisible);
}

#endif
