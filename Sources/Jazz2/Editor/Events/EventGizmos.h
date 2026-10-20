#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Formats/LevelFile.h"
#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/SmallVector.h>

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVectorImpl;
	using nCine::Vector2f;
	using nCine::Vector2i;
#endif

	/** @brief What a handle of an event changes */
	enum class EventGizmoKind : std::uint8_t
	{
		/** A distance from the event in pixels, as the radius of a light */
		Radius,
		/** A distance from the event in links of a chain, 12 px each */
		ChainLength,
		/** The width of a bridge to the right of the event, in steps of 16 px */
		BridgeWidth,
		/** Both offsets of the event from the center of its tile, in pixels */
		Offset,
		/** A height from the top of the level in pixels, as of the water */
		WaterLevel,
		/** The left edge of the view in tiles, 0 is the column of the event */
		CameraLeft,
		/** The width of the view in tiles from its left edge, 0 reaches to the right edge of the level */
		CameraWidth,
		/** The values of a group of directions, the tip of its arrow, as of the speed of a tube or a belt, see
			@ref EventParamField::Direction and @ref EventPreviews::GetDirectionTip() */
		Direction
	};

	/**
		@brief A handle in the view that changes a parameter of an event by dragging it

		Handles are where the parameter puts something in the level --- the edge of the light, the end of a bridge,
		the water line --- so dragging one there shows what the event does as it's changed.
	*/
	struct EventGizmo
	{
		/** @brief What it changes */
		EventGizmoKind Kind;
		/** @brief The parameter, an index to the fields of the event in the catalog, the first field of the group of
			@ref EventGizmoKind::Direction */
		std::uint8_t Field;
		/** @brief The second parameter of @ref EventGizmoKind::Offset, the vertical one */
		std::uint8_t SecondField;
		/** @brief Where the handle is in the level */
		Vector2f World;
		/** @brief Where the handle is measured from in the level, as the center of a light */
		Vector2f Anchor;
	};

	/**
		@brief Finds the handles of events and changes their parameters by them
	*/
	class EventGizmos
	{
	public:
		EventGizmos() = delete;

		/**
		 * @brief Adds the handles of an event at a tile of the event grid
		 *
		 * @p levelWidth is the width of the level in pixels, where a camera limit of no width ends.
		 */
		static void Collect(const Formats::EventRecord& event, Vector2i cell, std::int32_t levelWidth, SmallVectorImpl<EventGizmo>& gizmos);

		/**
		 * @brief Changes the parameter of a handle of an event so the handle is at a position in the level
		 *
		 * With @p snap, a direction (see @ref EventGizmoKind::Direction) goes only horizontally, vertically or
		 * diagonally, as long as it's dragged. Returns `false` if the event doesn't change.
		 */
		static bool Apply(Formats::EventRecord& event, Vector2i cell, std::int32_t levelWidth, const EventGizmo& gizmo, Vector2f world, bool snap = false);

		/**
		 * @brief Returns which way a handle of an event is dragged, as arrows by the pointer show it
		 *
		 * A unit vector along the line the handle moves on, X to the right and Y down, or a zero vector for a handle
		 * dragged any way, as an offset or the arrow of a tube.
		 */
		static Vector2f GetDragDirection(const Formats::EventRecord& event, const EventGizmo& gizmo);
	};
}

#endif
