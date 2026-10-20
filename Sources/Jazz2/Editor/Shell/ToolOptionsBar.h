#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Tools/EditorTool.h"
#include "../UI/UiWidgets.h"

#include <memory>

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Options of the tool chosen, in the toolbar, see @ref EditorTool::BuildOptions()

		They're made again whenever another tool is chosen, a tool without options leaves the bar empty. Where the
		row hasn't room for all of them, choices and groups of toggles become dropdowns first, then those that still
		don't fit are left out of the row and shown in a list by a button at its end. Every option shows its value as
		it is whenever it's drawn, a value changed by a shortcut or in a menu is seen right away.
	*/
	class ToolOptionsBar : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Creates a new instance */
		explicit ToolOptionsBar(IEditorContext& session);
		~ToolOptionsBar() override;

		/** @brief Returns `true` if the chosen tool has options */
		bool HasOptions() const {
			return !_items.empty();
		}

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Arrange(const UiContext& context, const Rectf& bounds) override;
		void Update(float timeMult) override;

	private:
		// How an option is made, in the row as it is, in the row made smaller, or in the list of those that don't fit
		enum class Form : std::uint8_t
		{
			Full,
			Compact,
			List
		};

		struct Item
		{
			// Makes the option in a form, into a parent, and returns it
			Function<UiElement*(UiElement*, Form)> Make;
			UiElement* Full = nullptr;
			// nullptr if it's as large as the full one
			UiElement* Compact = nullptr;
			bool IsSeparator = false;
		};

		class Builder;
		class MoreButton;

		IEditorContext& _session;
		SmallVector<Item, 0> _items;
		MoreButton* _more;
		// The first option that doesn't fit the row, all of them if they fit
		std::size_t _firstHidden;
		bool _built;
		EditorToolType _shownTool;
		bool _shownExpert;
		// The list of the options that don't fit while it's shown, shared with it, cleared when it's closed
		std::shared_ptr<UiElement*> _list;
		// Set when the list is closed, until the next frame, so the click on the button that closed it doesn't show it again
		std::shared_ptr<bool> _listJustClosed;

		void Rebuild();
		void ShowList();
		void CloseList();
	};
}

#endif
