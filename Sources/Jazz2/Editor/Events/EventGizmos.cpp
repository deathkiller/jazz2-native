#include "EventGizmos.h"

#if defined(WITH_EDITOR)

#include "EventCatalog.h"
#include "EventPreviews.h"
#include "../Document/LevelDocument.h"

#include <algorithm>
#include <cmath>

using namespace Death::Containers::Literals;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float TileSize = 32.0f;

		Vector2f GetCenter(Vector2i cell)
		{
			return Vector2f(cell.X * TileSize + TileSize * 0.5f, cell.Y * TileSize + TileSize * 0.5f);
		}

		const EventParamField* GetField(const EventDescriptor* descriptor, std::uint8_t index)
		{
			return (descriptor != nullptr && index < descriptor->FieldCount ? &descriptor->Fields[index] : nullptr);
		}

		// The handle of a field; a radius in pixels without one gets it too, as event packs described them before
		FieldGizmo GetGizmo(const EventParamField& field)
		{
			if (field.Gizmo != FieldGizmo::None) {
				return field.Gizmo;
			}
			if (field.Unit == ParamUnit::Pixels && field.Name != nullptr && StringView(field.Name).contains("Radius"_s)) {
				return FieldGizmo::Radius;
			}
			return FieldGizmo::None;
		}

		// The vertical offset paired with a horizontal one, 0xFF if there's none
		std::uint8_t FindOffsetY(const EventDescriptor* descriptor)
		{
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				if (descriptor->Fields[i].Gizmo == FieldGizmo::OffsetY) {
					return i;
				}
			}
			return 0xFF;
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

		// The fields of a group of directions along each axis and way, 0xFF where there's none
		struct DirectionFields
		{
			std::uint8_t First = 0xFF;
			std::uint8_t PositiveX = 0xFF;
			std::uint8_t NegativeX = 0xFF;
			std::uint8_t PositiveY = 0xFF;
			std::uint8_t NegativeY = 0xFF;
		};

		// The group as EventPreviews::CollectDirections() takes it
		std::uint8_t GetDirectionGroup(const EventParamField& field)
		{
			return std::min<std::uint8_t>(field.DirectionGroup, 3);
		}

		bool GetDirectionFields(const EventDescriptor* descriptor, std::uint8_t group, DirectionFields& fields)
		{
			fields = DirectionFields();
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				const EventParamField& field = descriptor->Fields[i];
				if (field.Direction == DirectionAxis::None || GetDirectionGroup(field) != group) {
					continue;
				}
				if (fields.First == 0xFF) {
					fields.First = i;
				}
				// The first one of each, a group has one or two fields along an axis
				std::uint8_t& slot = (field.Direction == DirectionAxis::X
					? (field.DirectionNegative ? fields.NegativeX : fields.PositiveX)
					: (field.DirectionNegative ? fields.NegativeY : fields.PositiveY));
				if (slot == 0xFF) {
					slot = i;
				}
			}
			return (fields.First != 0xFF);
		}

		// Writes a value along an axis, split into the field of each way if there are two, as the left and right belt
		void WriteDirection(Formats::EventRecord& event, const EventDescriptor* descriptor, std::uint8_t positive, std::uint8_t negative, std::int32_t value)
		{
			if (positive != 0xFF && negative != 0xFF) {
				EventCatalog::WriteField(event, descriptor->Fields[positive], std::max<std::int32_t>(value, 0));
				EventCatalog::WriteField(event, descriptor->Fields[negative], std::max<std::int32_t>(-value, 0));
			} else if (positive != 0xFF) {
				EventCatalog::WriteField(event, descriptor->Fields[positive], value);
			} else if (negative != 0xFF) {
				EventCatalog::WriteField(event, descriptor->Fields[negative], -value);
			}
		}
	}

	void EventGizmos::Collect(const Formats::EventRecord& event, Vector2i cell, std::int32_t levelWidth, SmallVectorImpl<EventGizmo>& gizmos)
	{
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
		if (descriptor == nullptr) {
			return;
		}
		Vector2f center = GetCenter(cell);
		// The handles of the game's own events know which of their parameters is what, an event the game knows that a
		// level or an event pack describes in its own way gets only the handles its description gives it
		bool builtIn = !EventCatalog::IsExtension(descriptor);

		// Handles of more than one parameter, measured from the level instead of from the event
		switch (builtIn ? EventType(event.Type) : EventType::Empty) {
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

		// By the handles the parameters have, see EventParamField::Gizmo, of the game's own events and of others alike
		std::int32_t radii = 0;
		for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
			const EventParamField& field = descriptor->Fields[i];
			float step = float(std::max<std::int16_t>(field.GizmoStep, 1));
			float value = float(EventCatalog::ReadField(event, field)) * step;
			switch (GetGizmo(field)) {
				case FieldGizmo::Radius: {
					// Each radius in another direction, so they can be told apart even if they're the same
					float angle = radii * 0.6f;
					Add(gizmos, EventGizmoKind::Radius, i, center + Vector2f(std::cos(angle), std::sin(angle)) * value, center);
					radii++;
					break;
				}
				case FieldGizmo::LengthDown:
					// A chain hangs down from where it's fixed
					Add(gizmos, EventGizmoKind::ChainLength, i, center + Vector2f(0.0f, value), center);
					break;
				case FieldGizmo::WidthRight:
					// At the right end, a bridge goes from the event to the right, a little above the middle where its pieces are
					Add(gizmos, EventGizmoKind::BridgeWidth, i, Vector2f(center.X + value, center.Y - 6.0f), center);
					break;
				case FieldGizmo::OffsetX: {
					// Where the object stands, with the vertical offset paired with it
					std::uint8_t second = FindOffsetY(descriptor);
					float y = 0.0f;
					if (second != 0xFF) {
						const EventParamField& secondField = descriptor->Fields[second];
						y = float(EventCatalog::ReadField(event, secondField)) * float(std::max<std::int16_t>(secondField.GizmoStep, 1));
					}
					Add(gizmos, EventGizmoKind::Offset, i, center + Vector2f(value, y), center, second);
					break;
				}
				default:
					break;
			}
		}

		// At the tip of each arrow of the directions, found as EditorShell draws them, from the position of the event, see
		// EventPreviews::GetDirectionTip(); with no arrow at all, the first group gets a handle where the shortest arrow would
		// end, so one can be dragged out
		EventDirection directions[4];
		std::int32_t directionCount = EventPreviews::CollectDirections(event, directions);
		DirectionFields fields;
		for (std::int32_t d = 0; d < directionCount; d++) {
			if (GetDirectionFields(descriptor, directions[d].Group, fields)) {
				Add(gizmos, EventGizmoKind::Direction, fields.First, EventPreviews::GetDirectionTip(center, directions[d].Vector), center);
			}
		}
		if (directionCount == 0) {
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				if (descriptor->Fields[i].Direction != DirectionAxis::None && GetDirectionFields(descriptor, GetDirectionGroup(descriptor->Fields[i]), fields)) {
					Vector2f axis = (fields.PositiveX != 0xFF || fields.NegativeX != 0xFF ? Vector2f(1.0f, 0.0f) : Vector2f(0.0f, 1.0f));
					Add(gizmos, EventGizmoKind::Direction, fields.First, center + axis * EventPreviews::GetDirectionLength(0.0f), center);
					break;
				}
			}
		}
	}

	bool EventGizmos::Apply(Formats::EventRecord& event, Vector2i cell, std::int32_t levelWidth, const EventGizmo& gizmo, Vector2f world, bool snap)
	{
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
		const EventParamField* field = GetField(descriptor, gizmo.Field);
		if (field == nullptr) {
			return false;
		}
		// The descriptions can change between collecting the handles and dragging one, the handle has to still be the
		// one of the parameter; the handles measured from the level are only of the game's own events
		bool builtIn = !EventCatalog::IsExtension(descriptor);
		switch (gizmo.Kind) {
			case EventGizmoKind::Radius: if (GetGizmo(*field) != FieldGizmo::Radius) return false; break;
			case EventGizmoKind::ChainLength: if (GetGizmo(*field) != FieldGizmo::LengthDown) return false; break;
			case EventGizmoKind::BridgeWidth: if (GetGizmo(*field) != FieldGizmo::WidthRight) return false; break;
			case EventGizmoKind::Offset: if (GetGizmo(*field) != FieldGizmo::OffsetX) return false; break;
			case EventGizmoKind::Direction: if (field->Direction == DirectionAxis::None) return false; break;
			default: if (!builtIn) return false; break;
		}
		Formats::EventRecord before = event;
		Vector2f center = GetCenter(cell);
		float step = float(std::max<std::int16_t>(field->GizmoStep, 1));

		switch (gizmo.Kind) {
			case EventGizmoKind::Radius:
				EventCatalog::WriteField(event, *field, std::int32_t(std::round((world - gizmo.Anchor).Length() / step)));
				break;
			case EventGizmoKind::ChainLength:
				EventCatalog::WriteField(event, *field, std::int32_t(std::round((world - gizmo.Anchor).Length() / step)));
				break;
			case EventGizmoKind::BridgeWidth:
				// At least a step, a handle in the middle of the event couldn't be grabbed again
				EventCatalog::WriteField(event, *field, std::max<std::int32_t>(std::int32_t(std::round((world.X - center.X) / step)), 1));
				break;
			case EventGizmoKind::Offset: {
				const EventParamField* second = GetField(descriptor, gizmo.SecondField);
				EventCatalog::WriteField(event, *field, std::int32_t(std::round((world.X - center.X) / step)));
				if (second != nullptr && GetGizmo(*second) == FieldGizmo::OffsetY) {
					float secondStep = float(std::max<std::int16_t>(second->GizmoStep, 1));
					EventCatalog::WriteField(event, *second, std::int32_t(std::round((world.Y - center.Y) / secondStep)));
				}
				break;
			}
			case EventGizmoKind::WaterLevel:
				EventCatalog::WriteField(event, *field, std::max<std::int32_t>(std::int32_t(std::round(world.Y)), 0));
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
				std::int32_t column = std::clamp<std::int32_t>(std::int32_t(std::round(world.X / TileSize)), 0, levelColumns);

				if (gizmo.Kind == EventGizmoKind::CameraLeft) {
					// The right edge stays where it is
					leftColumn = std::min<std::int32_t>(column, rightColumn - 1);
				} else {
					rightColumn = std::max<std::int32_t>(column, leftColumn + 1);
					toLevelEdge = (rightColumn >= levelColumns);
				}
				// 0 is the column of the event, it moves with the event then, column 0 itself can't be written
				if (leftColumn != cell.X) {
					leftColumn = std::max<std::int32_t>(leftColumn, 1);
				}
				EventCatalog::WriteField(event, *leftField, leftColumn == cell.X ? 0 : leftColumn);
				EventCatalog::WriteField(event, *widthField, toLevelEdge ? 0 : std::max<std::int32_t>(rightColumn - leftColumn, 1));
				break;
			}
			case EventGizmoKind::Direction: {
				DirectionFields fields;
				if (!GetDirectionFields(descriptor, GetDirectionGroup(*field), fields)) {
					return false;
				}
				bool hasX = (fields.PositiveX != 0xFF || fields.NegativeX != 0xFF);
				bool hasY = (fields.PositiveY != 0xFF || fields.NegativeY != 0xFF);
				// Only along the axes the group has, a belt doesn't get weaker as it's dragged up
				Vector2f offset = world - gizmo.Anchor;
				if (!hasX) {
					offset.X = 0.0f;
				}
				if (!hasY) {
					offset.Y = 0.0f;
				}
				float length = offset.Length();
				Vector2f unit = (length > 0.0f ? offset / length : Vector2f::Zero);
				if (snap && length > 0.0f) {
					// The nearest of the 8 directions, each takes 45 degrees around it, 22.5 to either side of it, at the same
					// length; a diagonal gets the same value on both axes
					constexpr float Sin22_5 = 0.38268343f;
					float x = (std::abs(unit.X) >= Sin22_5 ? (unit.X < 0.0f ? -1.0f : 1.0f) : 0.0f);
					float y = (std::abs(unit.Y) >= Sin22_5 ? (unit.Y < 0.0f ? -1.0f : 1.0f) : 0.0f);
					unit = Vector2f(x, y).Normalized();
					offset = unit * length;
				}
				Vector2f value = unit * EventPreviews::GetDirectionStrength(length);
				// In steps of the field, see EventParamField::GizmoStep
				std::int32_t x = std::int32_t(std::round(value.X / step)) * std::int32_t(step);
				std::int32_t y = std::int32_t(std::round(value.Y / step)) * std::int32_t(step);
				if (x == 0 && y == 0) {
					// At least a step, an arrow of no strength isn't drawn and its handle would be lost in the event, 0 is set in
					// the inspector; it goes the way it's dragged, so dragging across the event turns it around
					if (hasX && (!hasY || std::abs(offset.X) >= std::abs(offset.Y))) {
						x = (offset.X < 0.0f ? -std::int32_t(step) : std::int32_t(step));
					} else {
						y = (offset.Y < 0.0f ? -std::int32_t(step) : std::int32_t(step));
					}
				}
				WriteDirection(event, descriptor, fields.PositiveX, fields.NegativeX, x);
				WriteDirection(event, descriptor, fields.PositiveY, fields.NegativeY, y);
				break;
			}
		}
		return !LevelDocument::AreEventsEqual(before, event);
	}

	Vector2f EventGizmos::GetDragDirection(const Formats::EventRecord& event, const EventGizmo& gizmo)
	{
		switch (gizmo.Kind) {
			case EventGizmoKind::Radius:
			case EventGizmoKind::ChainLength: {
				// Away from what it's measured from
				Vector2f offset = gizmo.World - gizmo.Anchor;
				float length = offset.Length();
				return (length > 0.0f ? offset / length : (gizmo.Kind == EventGizmoKind::Radius ? Vector2f(1.0f, 0.0f) : Vector2f(0.0f, 1.0f)));
			}
			case EventGizmoKind::BridgeWidth:
			case EventGizmoKind::CameraLeft:
			case EventGizmoKind::CameraWidth:
				return Vector2f(1.0f, 0.0f);
			case EventGizmoKind::WaterLevel:
				return Vector2f(0.0f, 1.0f);
			case EventGizmoKind::Direction: {
				// Along the only axis of the group, a group of both goes any way
				const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
				const EventParamField* field = GetField(descriptor, gizmo.Field);
				DirectionFields fields;
				if (field == nullptr || !GetDirectionFields(descriptor, GetDirectionGroup(*field), fields)) {
					return Vector2f::Zero;
				}
				bool hasX = (fields.PositiveX != 0xFF || fields.NegativeX != 0xFF);
				bool hasY = (fields.PositiveY != 0xFF || fields.NegativeY != 0xFF);
				return (hasX && !hasY ? Vector2f(1.0f, 0.0f) : (hasY && !hasX ? Vector2f(0.0f, 1.0f) : Vector2f::Zero));
			}
			default:
				return Vector2f::Zero;
		}
	}
}

#endif
