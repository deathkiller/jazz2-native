#include "EventGizmos.h"

#if defined(WITH_EDITOR)

#include "EventCatalog.h"
#include "../Document/LevelDocument.h"

#include <algorithm>
#include <cmath>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float TileSize = 32.0f;
		// Actors/Solid/SpikeBall.cpp and MovingPlatform.cpp, a link of the chain
		constexpr float ChainLinkSize = 12.0f;
		// Actors/Solid/Bridge.cpp, the width is in these steps
		constexpr float BridgeStep = 16.0f;

		Vector2f GetCenter(Vector2i cell)
		{
			return Vector2f(cell.X * TileSize + TileSize * 0.5f, cell.Y * TileSize + TileSize * 0.5f);
		}

		const EventParamField* GetField(const EventDescriptor* descriptor, std::uint8_t index)
		{
			return (descriptor != nullptr && index < descriptor->FieldCount ? &descriptor->Fields[index] : nullptr);
		}

		// The left edge and the width of the view a camera limit keeps, in pixels
		void GetCameraSpan(const EventDescriptor* descriptor, const Formats::EventRecord& event, Vector2i cell, std::int32_t levelWidth, float& left, float& width)
		{
			std::int32_t leftTiles = EventCatalog::ReadField(event, *GetField(descriptor, 0));
			std::int32_t widthTiles = EventCatalog::ReadField(event, *GetField(descriptor, 1));
			left = (leftTiles == 0 ? cell.X : leftTiles) * TileSize;
			width = (widthTiles == 0 ? std::max(float(levelWidth) - left, TileSize) : widthTiles * TileSize);
		}

		void Add(SmallVectorImpl<EventGizmo>& gizmos, EventGizmoKind kind, std::uint8_t field, Vector2f world, Vector2f anchor, std::uint8_t secondField = 0xFF)
		{
			EventGizmo& gizmo = gizmos.emplace_back();
			gizmo.Kind = kind;
			gizmo.Field = field;
			gizmo.SecondField = secondField;
			gizmo.World = world;
			gizmo.Anchor = anchor;
		}
	}

	void EventGizmos::Collect(const Formats::EventRecord& event, Vector2i cell, std::int32_t levelWidth, SmallVectorImpl<EventGizmo>& gizmos)
	{
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
		if (descriptor == nullptr) {
			return;
		}
		Vector2f center = GetCenter(cell);

		switch (EventType(event.Type)) {
			case EventType::Bridge:
				if (const EventParamField* field = GetField(descriptor, 0)) {
					// At the right end, the bridge goes from the event to the right
					float width = EventCatalog::ReadField(event, *field) * BridgeStep;
					Add(gizmos, EventGizmoKind::BridgeWidth, 0, Vector2f(center.X + width, center.Y - 6.0f), center);
				}
				return;
			case EventType::Pole:
				if (GetField(descriptor, 2) != nullptr) {
					// Where the pole stands
					Vector2f offset(float(EventCatalog::ReadField(event, descriptor->Fields[1])), float(EventCatalog::ReadField(event, descriptor->Fields[2])));
					Add(gizmos, EventGizmoKind::Offset, 1, center + offset, center, 2);
				}
				return;
			case EventType::ModifierSetWater:
				if (const EventParamField* field = GetField(descriptor, 0)) {
					Add(gizmos, EventGizmoKind::WaterLevel, 0, Vector2f(center.X, float(EventCatalog::ReadField(event, *field))), center);
				}
				return;
			case EventType::ModifierLimitCameraView:
				if (GetField(descriptor, 1) != nullptr) {
					float left, width;
					GetCameraSpan(descriptor, event, cell, levelWidth, left, width);
					Add(gizmos, EventGizmoKind::CameraLeft, 0, Vector2f(left, center.Y), center);
					Add(gizmos, EventGizmoKind::CameraWidth, 1, Vector2f(left + width, center.Y), center);
				}
				return;
			default:
				break;
		}

		// By the parameters, for every event that has them
		std::int32_t radii = 0;
		for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
			const EventParamField& field = descriptor->Fields[i];
			StringView name = field.Name;
			if (field.Unit == ParamUnit::Pixels && name.contains("Radius"_s)) {
				// Each radius in another direction, so they can be told apart even if they're the same
				float angle = radii * 0.6f;
				float radius = float(EventCatalog::ReadField(event, field));
				Add(gizmos, EventGizmoKind::Radius, i, center + Vector2f(std::cos(angle), std::sin(angle)) * radius, center);
				radii++;
			} else if (name == "Length"_s && (EventType(event.Type) == EventType::SpikeBall || EventType(event.Type) == EventType::MovingPlatform)) {
				// The chain hangs down from where it's fixed
				float length = EventCatalog::ReadField(event, field) * ChainLinkSize;
				Add(gizmos, EventGizmoKind::ChainLength, i, center + Vector2f(0.0f, length), center);
			}
		}
	}

	bool EventGizmos::Apply(Formats::EventRecord& event, Vector2i cell, std::int32_t levelWidth, const EventGizmo& gizmo, Vector2f world)
	{
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type));
		const EventParamField* field = GetField(descriptor, gizmo.Field);
		if (field == nullptr) {
			return false;
		}
		Formats::EventRecord before = event;
		Vector2f center = GetCenter(cell);

		switch (gizmo.Kind) {
			case EventGizmoKind::Radius:
				EventCatalog::WriteField(event, *field, std::int32_t(std::round((world - gizmo.Anchor).Length())));
				break;
			case EventGizmoKind::ChainLength:
				EventCatalog::WriteField(event, *field, std::int32_t(std::round((world - gizmo.Anchor).Length() / ChainLinkSize)));
				break;
			case EventGizmoKind::BridgeWidth:
				EventCatalog::WriteField(event, *field, std::max(std::int32_t(std::round((world.X - center.X) / BridgeStep)), 1));
				break;
			case EventGizmoKind::Offset: {
				const EventParamField* second = GetField(descriptor, gizmo.SecondField);
				EventCatalog::WriteField(event, *field, std::int32_t(std::round(world.X - center.X)));
				if (second != nullptr) {
					EventCatalog::WriteField(event, *second, std::int32_t(std::round(world.Y - center.Y)));
				}
				break;
			}
			case EventGizmoKind::WaterLevel:
				EventCatalog::WriteField(event, *field, std::max(std::int32_t(std::round(world.Y)), 0));
				break;
			case EventGizmoKind::CameraLeft:
			case EventGizmoKind::CameraWidth: {
				const EventParamField* leftField = GetField(descriptor, 0);
				const EventParamField* widthField = GetField(descriptor, 1);
				if (leftField == nullptr || widthField == nullptr) {
					return false;
				}
				float left, width;
				GetCameraSpan(descriptor, event, cell, levelWidth, left, width);
				std::int32_t leftColumn = std::int32_t(left / TileSize);
				std::int32_t rightColumn = std::int32_t((left + width) / TileSize);
				bool toLevelEdge = (EventCatalog::ReadField(event, *widthField) == 0);
				std::int32_t levelColumns = std::int32_t(std::ceil(levelWidth / TileSize));
				std::int32_t column = std::clamp(std::int32_t(std::round(world.X / TileSize)), 0, levelColumns);

				if (gizmo.Kind == EventGizmoKind::CameraLeft) {
					// The right edge stays where it is
					leftColumn = std::min(column, rightColumn - 1);
				} else {
					rightColumn = std::max(column, leftColumn + 1);
					toLevelEdge = (rightColumn >= levelColumns);
				}
				// 0 is the column of the event, it moves with the event then, column 0 itself can't be written
				if (leftColumn != cell.X) {
					leftColumn = std::max(leftColumn, 1);
				}
				EventCatalog::WriteField(event, *leftField, leftColumn == cell.X ? 0 : leftColumn);
				EventCatalog::WriteField(event, *widthField, toLevelEdge ? 0 : std::max(rightColumn - leftColumn, 1));
				break;
			}
		}
		return !LevelDocument::AreEventsEqual(before, event);
	}
}

#endif
