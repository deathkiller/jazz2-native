#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Events/EventCatalog.h"
#include "../UI/UiWidgets.h"

namespace Jazz2::Editor
{
	class EditorSession;

	/**
		@brief Events to choose from, as a grid of their pictures and names
	*/
	class EventGrid : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Category shown when all categories are */
		static constexpr std::int32_t AllCategories = -1;

		explicit EventGrid(EditorSession& session);

		/** @brief Shows only events of a category, or of all of them, and those with a text in their name */
		void SetFilter(std::int32_t category, StringView search);
		/** @brief Returns the category shown */
		std::int32_t GetCategory() const {
			return _category;
		}
		/** @brief Returns the text searched for */
		StringView GetSearch() const {
			return _search;
		}

		void Update(float timeMult) override;
		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		StringView GetTooltip(Vector2f pos) const override;

	private:
		EditorSession& _session;
		std::int32_t _category;
		String _search;
		SmallVector<const EventDescriptor*, 0> _items;
		std::uint32_t _catalogRevision;
		float _chipWidth;
		float _chipHeight;
		std::int32_t _columns;
		std::int32_t _pressedIndex;
		float _measuredScale;
		float _measuredNameWidth;
		mutable String _tooltip;

		void UpdateItems();
		std::int32_t GetIndexAt(Vector2f pos) const;
		Rectf GetChipRect(std::int32_t index) const;
	};

	/**
		@brief Panel with the events to choose from, by category
	*/
	class EventPickerPanel : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		explicit EventPickerPanel(EditorSession& session);

	private:
		EditorSession& _session;
		EventGrid* _grid;
	};

	/**
		@brief Properties of the selected events, or of the event to place when none is selected
	*/
	class EventInspectorPanel : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		explicit EventInspectorPanel(EditorSession& session);

		void Update(float timeMult) override;

		/** @brief Returns the event whose properties are shown, `false` if there is none */
		bool GetPrimary(Formats::EventRecord& event) const;
		/**
		 * @brief Changes the selected events, or the event to place
		 *
		 * @param name           Name of the change, marked by @ref N_()
		 * @param change         Changes an event
		 * @param sameTypeOnly   Whether only the events of the type of the shown one are changed
		 */
		void Change(const char* name, Function<void(Formats::EventRecord&)>&& change, bool sameTypeOnly);

	private:
		EditorSession& _session;
		UiScrollView* _scroll;
		std::int32_t _shownType;
		std::int32_t _shownCount;
		bool _shownBrush;
		std::uint32_t _shownRevision;

		void Rebuild();
		void AddCommonRows(UiElement* rows);
		void AddField(UiElement* rows, const EventParamField& field);
	};
}

#endif
