#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Events/EventCatalog.h"
#include "../UI/UiWidgets.h"

#include <memory>

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Events to choose from, as a grid of their pictures and names
	*/
	class EventGrid : public UiElement
	{
		DEATH_RUNTIME_OBJECT(UiElement);

	public:
		/** @brief Category shown when all categories are */
		static constexpr std::int32_t AllCategories = -1;

		explicit EventGrid(IEditorContext& session);

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
		IEditorContext& _session;
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
		explicit EventPickerPanel(IEditorContext& session);

	private:
		IEditorContext& _session;
		EventGrid* _grid;
	};

	/**
		@brief Properties of the selected events, or of the event to place when none is selected
	*/
	class EventInspectorPanel : public UiPanel
	{
		DEATH_RUNTIME_OBJECT(UiPanel);

	public:
		explicit EventInspectorPanel(IEditorContext& session);

		void Update(float timeMult) override;

		/** @brief Returns the event whose properties are shown, `false` if there is none */
		bool GetPrimary(Formats::EventRecord& event) const;
		/**
		 * @brief Returns `true` if the selected events are of different kinds
		 *
		 * Their parameters mean something else for each kind, so only what all events have in common is shown then ---
		 * the difficulties and the flags.
		 */
		bool HasMixedKinds() const {
			return (_otherKind != 0);
		}
		/** @brief Returns the kind of a selected event other than the shown one, if they're of different kinds, 0 otherwise */
		std::uint16_t GetOtherKind() const {
			return _otherKind;
		}
		/**
		 * @brief Changes the selected events, or the event to place
		 *
		 * @param name           Name of the change, marked by @ref N_(), changes of the same name are merged while
		 *                       a value is dragged, so it has to be different for each property
		 * @param change         Changes an event
		 * @param sameTypeOnly   Whether only the events of the type of the shown one are changed, nothing is changed
		 *                       then while the selected events are of different kinds, see @ref HasMixedKinds()
		 * @param final          Whether it ends the step, `false` only while a value is dragged
		 */
		void Change(const char* name, Function<void(Formats::EventRecord&)>&& change, bool sameTypeOnly, bool final = true);

	private:
		// What an event chosen in the list is for, see BeginEventPick()
		struct PickTarget
		{
			SmallVector<std::uint32_t, 0> Cells;
			SmallVector<std::int32_t, 0> OffGrid;
			std::uint16_t Type;
			const EventParamField* Field;
			std::uint32_t CatalogRevision;
		};

		IEditorContext& _session;
		UiScrollView* _scroll;
		std::int32_t _shownType;
		std::int32_t _shownCount;
		bool _shownBrush;
		std::uint32_t _shownRevision;
		std::uint32_t _shownContents = 0;
		bool _shownMixedKinds = false;
		// Kept alive by the function waiting in the session, expires when the pick ends in any way
		std::weak_ptr<PickTarget> _pendingPick;
		// Values that differ among the selected events, a bit for each field, then those of all events, see MixedBit,
		// found again only when the selection or the level changes
		std::uint64_t _mixed;
		std::uint32_t _mixedRevision;
		std::uint32_t _mixedSelectionRevision;
		// The first kind of the selected events other than the one shown, 0 if they're all of the same kind, found with _mixed
		std::uint16_t _otherKind = 0;

		// Bits of _mixed after those of the fields
		enum MixedBit : std::int32_t
		{
			MixedEasy = 32, MixedNormal, MixedHard, MixedMultiplayer, MixedIlluminated, MixedGenerator, MixedDelay, MixedDelayFirst, MixedRaw
		};

		void UpdateMixed();
		// Returns `true` if a value committed by a box is the one it shows, of the shown event, which isn't written then
		// into the other selected events, nor clamped to the range of the field only by being shown; a drag still ends
		bool IsShownValue(std::int32_t shown, std::int32_t committed, const EventParamField* field, bool final);
		// Adds a mark shown while a value of @p mask, bits of _mixed, differs among the selected events
		void AddMixedMark(UiElement* parent, std::uint64_t mask);
		void AddMixedMark(UiElement* parent, std::int32_t bit) {
			AddMixedMark(parent, std::uint64_t(1) << bit);
		}

		// Lets the next event chosen in the list go to a parameter of the shown events, only while they're still shown
		void BeginEventPick(const EventParamField* field);
		// Returns `true` if the events are shown still as they were when the pick began
		bool IsPickTargetShown(const PickTarget& target) const;
		void Rebuild();
		void AddCommonRows(UiElement* rows);
		void AddField(UiElement* rows, const EventDescriptor& descriptor, std::int32_t index);
		void AddFieldRows(UiElement* rows, const EventDescriptor& descriptor, std::int32_t index);
		// The fields of the event inside of a field of ParamType::Contents, of the kind it has now
		void AddContentsFields(UiElement* rows, const EventDescriptor& descriptor, std::int32_t index);
		// Returns the kinds of the events inside the event, the fields shown depend on them
		std::uint32_t GetContentsSignature(const Formats::EventRecord& event) const;
	};
}

#endif
