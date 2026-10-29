#include "EventPreviews.h"

#if defined(WITH_EDITOR)

#include "../Document/LevelDocument.h"
#include "../../ExitType.h"
#include "../../UI/Canvas.h"

#include "../../../nCine/Base/Algorithms.h"
#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace Jazz2::Editor
{
	namespace
	{
		constexpr float TileSize = 32.0f;
		// An arrow of a strength of 1 reaches the edge of the tile of its event
		constexpr float DirectionBaseLength = 10.0f;
		constexpr float DirectionLengthScale = 6.0f;

		// An event with a field of a link channel
		struct LinkEnd
		{
			const char* Channel;
			std::int32_t Id;
			EventLinkRole Role;
			// Tile of the event grid, or the position of an off-grid event in pixels
			std::uint32_t Cell;
			bool OffGrid;
			Vector2f Position;
			// Index of the end the event is part of, events on tiles next to each other are one
			std::int32_t Group;
		};

		bool IsSameLink(const LinkEnd& a, const LinkEnd& b)
		{
			return (a.Id == b.Id && a.Role == b.Role && std::strcmp(a.Channel, b.Channel) == 0);
		}

		// Adds the ends of the links of an event, one for each of its link fields
		void AddLinkEnds(const Formats::EventRecord& event, std::uint32_t cell, bool offGrid, Vector2f position, SmallVectorImpl<LinkEnd>& ends)
		{
			const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
			if (descriptor == nullptr) {
				return;
			}
			for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
				const EventParamField& field = descriptor->Fields[i];
				if (field.LinkChannel == nullptr || field.LinkRole == EventLinkRole::None) {
					continue;
				}
				LinkEnd& end = ends.emplace_back();
				end.Channel = field.LinkChannel;
				end.Id = EventCatalog::ReadField(event, field);
				end.Role = field.LinkRole;
				end.Cell = cell;
				end.OffGrid = offGrid;
				end.Position = position;
				end.Group = -1;
			}
		}
	}

	void EventPreviews::CollectLinks(const LevelDocument& document, SmallVectorImpl<EventLink>& links, ArrayView<const std::uint32_t> onlyCells, std::size_t maxLinks)
	{
		links.clear();
		Vector2i gridSize = document.GetEventGridSize();
		if (gridSize.X <= 0 || gridSize.Y <= 0) {
			return;
		}

		SmallVector<LinkEnd, 0> ends;
		for (const DocumentEvent& event : document.Events) {
			AddLinkEnds(event.Record, event.Cell, false, Vector2f(), ends);
		}
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			AddLinkEnds(event.Event, 0, true, Vector2f(float(event.X), float(event.Y)), ends);
		}
		if (ends.empty()) {
			return;
		}

		// Events of the same link on tiles next to each other are one end; an off-grid event is an end of its own
		std::unordered_multimap<std::uint32_t, std::int32_t> byCell;
		for (std::int32_t i = 0; i < std::int32_t(ends.size()); i++) {
			if (!ends[i].OffGrid) {
				byCell.emplace(ends[i].Cell, i);
			}
		}
		struct Group
		{
			Rectf Area;
			std::int32_t First;
			bool Selected;
		};
		SmallVector<Group, 0> groups;
		SmallVector<std::int32_t, 0> stack;
		auto findAt = [&](std::uint32_t cell, const LinkEnd& like) -> std::int32_t {
			// An event has an end for each of its link fields, a tile has a few at most
			auto range = byCell.equal_range(cell);
			for (auto it = range.first; it != range.second; ++it) {
				if (IsSameLink(ends[it->second], like)) {
					return it->second;
				}
			}
			return -1;
		};
		auto isSelected = [&onlyCells](std::uint32_t cell) {
			return std::binary_search(onlyCells.begin(), onlyCells.end(), cell);
		};
		for (std::int32_t i = 0; i < std::int32_t(ends.size()); i++) {
			if (ends[i].Group >= 0) {
				continue;
			}
			std::int32_t groupIndex = std::int32_t(groups.size());
			Group& group = groups.emplace_back();
			group.First = i;
			group.Selected = false;
			ends[i].Group = groupIndex;
			if (ends[i].OffGrid) {
				group.Area = Rectf(ends[i].Position.X - 4.0f, ends[i].Position.Y - 4.0f, 8.0f, 8.0f);
				continue;
			}
			std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
			stack.clear();
			stack.push_back(i);
			while (!stack.empty()) {
				std::int32_t current = stack.back();
				stack.pop_back();
				std::uint32_t cell = ends[current].Cell;
				std::int32_t x = std::int32_t(cell % std::uint32_t(gridSize.X)), y = std::int32_t(cell / std::uint32_t(gridSize.X));
				minX = std::min<std::int32_t>(minX, x);
				minY = std::min<std::int32_t>(minY, y);
				maxX = std::max<std::int32_t>(maxX, x);
				maxY = std::max<std::int32_t>(maxY, y);
				group.Selected |= (!onlyCells.empty() && isSelected(cell));
				const std::int32_t neighbors[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
				for (const auto& offset : neighbors) {
					std::int32_t nx = x + offset[0], ny = y + offset[1];
					if (nx < 0 || ny < 0 || nx >= gridSize.X || ny >= gridSize.Y) {
						continue;
					}
					std::int32_t next = findAt(std::uint32_t(nx + ny * gridSize.X), ends[i]);
					if (next >= 0 && ends[next].Group < 0) {
						ends[next].Group = groupIndex;
						stack.push_back(next);
					}
				}
			}
			group.Area = Rectf(minX * TileSize, minY * TileSize, (maxX - minX + 1) * TileSize, (maxY - minY + 1) * TileSize);
		}

		// The ends by the channel and the value, then a source to every target, peers in a chain
		SmallVector<std::int32_t, 0> order;
		order.reserve(groups.size());
		for (std::int32_t g = 0; g < std::int32_t(groups.size()); g++) {
			order.push_back(g);
		}
		std::sort(order.begin(), order.end(), [&](std::int32_t a, std::int32_t b) {
			const LinkEnd& endA = ends[groups[a].First];
			const LinkEnd& endB = ends[groups[b].First];
			std::int32_t channel = std::strcmp(endA.Channel, endB.Channel);
			if (channel != 0) {
				return (channel < 0);
			}
			if (endA.Id != endB.Id) {
				return (endA.Id < endB.Id);
			}
			return (a < b);
		});
		for (std::size_t first = 0; first < order.size(); ) {
			const LinkEnd& like = ends[groups[order[first]].First];
			std::size_t last = first;
			while (last < order.size()) {
				const LinkEnd& other = ends[groups[order[last]].First];
				if (other.Id != like.Id || std::strcmp(other.Channel, like.Channel) != 0) {
					break;
				}
				last++;
			}

			bool hasTarget = false;
			for (std::size_t k = first; k < last; k++) {
				hasTarget |= (ends[groups[order[k]].First].Role == EventLinkRole::Target);
			}
			std::int32_t previousPeer = -1;
			for (std::size_t k = first; k < last && links.size() < maxLinks; k++) {
				const Group& from = groups[order[k]];
				EventLinkRole role = ends[from.First].Role;
				if (role == EventLinkRole::Source) {
					if (!hasTarget) {
						if (onlyCells.empty() || from.Selected) {
							EventLink& link = links.emplace_back();
							link.From = from.Area;
							link.To = Rectf();
							link.Channel = like.Channel;
							link.Id = like.Id;
							link.Directed = true;
							link.Missing = true;
						}
						continue;
					}
					for (std::size_t t = first; t < last && links.size() < maxLinks; t++) {
						const Group& to = groups[order[t]];
						if (ends[to.First].Role != EventLinkRole::Target || (!onlyCells.empty() && !from.Selected && !to.Selected)) {
							continue;
						}
						EventLink& link = links.emplace_back();
						link.From = from.Area;
						link.To = to.Area;
						link.Channel = like.Channel;
						link.Id = like.Id;
						link.Directed = true;
						link.Missing = false;
					}
				} else if (role == EventLinkRole::Peer) {
					if (previousPeer >= 0) {
						const Group& to = groups[previousPeer];
						if (onlyCells.empty() || from.Selected || to.Selected) {
							EventLink& link = links.emplace_back();
							link.From = to.Area;
							link.To = from.Area;
							link.Channel = like.Channel;
							link.Id = like.Id;
							link.Directed = false;
							link.Missing = false;
						}
					}
					previousPeer = order[k];
				}
			}
			first = last;
		}
	}

	void EventPreviews::CollectRaceTrack(const LevelDocument& document, SmallVectorImpl<EventLink>& links, std::size_t maxLinks)
	{
		links.clear();
		Vector2i gridSize = document.GetEventGridSize();
		if (gridSize.X <= 0 || gridSize.Y <= 0) {
			return;
		}

		// By their bytes, as MpLevelHandler::BuildTrackCheckpoints() reads them whatever describes them
		struct Waypoint
		{
			std::uint8_t Group;
			std::uint8_t Order;
			Rectf Area;
		};
		SmallVector<Waypoint, 0> waypoints;
		// A warp counts a lap with Set Lap, see Player::HandleAreaEventAt(), as a coin warp does, see BonusWarp::Activate(),
		// and a level exit always does in a race, it warps the player back to the start, see MpLevelHandler::BeginLevelChange()
		auto endsLap = [](const Formats::EventRecord& event) {
			EventType type = EventType(event.Type);
			return ((type == EventType::WarpOrigin || type == EventType::WarpCoinBonus) && event.Params[2] != 0) || type == EventType::AreaEndOfLevel;
		};
		SmallVector<std::uint32_t, 0> finishCells;
		SmallVector<Rectf, 0> finishes;
		for (const DocumentEvent& event : document.Events) {
			if (EventType(event.Record.Type) == EventType::RaceCheckpoint) {
				Rectf area(float(event.Cell % std::uint32_t(gridSize.X)) * TileSize, float(event.Cell / std::uint32_t(gridSize.X)) * TileSize, TileSize, TileSize);
				waypoints.push_back({ event.Record.Params[1], event.Record.Params[0], area });
			} else if (endsLap(event.Record)) {
				finishCells.push_back(event.Cell);
			}
		}
		for (const Formats::OffGridEventRecord& event : document.OffGridEvents) {
			// As CollectLinks() takes an off-grid end
			Rectf area(float(event.X) - 4.0f, float(event.Y) - 4.0f, 8.0f, 8.0f);
			if (EventType(event.Event.Type) == EventType::RaceCheckpoint) {
				waypoints.push_back({ event.Event.Params[1], event.Event.Params[0], area });
			} else if (endsLap(event.Event)) {
				finishes.push_back(area);
			}
		}
		if (waypoints.empty()) {
			return;
		}

		// Tiles of the finish next to each other are one end, as the tiles of a warp; the events are sorted by the tile,
		// so the cells are too
		SmallVector<bool, 0> visited;
		visited.resize(finishCells.size(), false);
		SmallVector<std::size_t, 0> stack;
		for (std::size_t i = 0; i < finishCells.size(); i++) {
			if (visited[i]) {
				continue;
			}
			visited[i] = true;
			stack.push_back(i);
			std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = INT32_MIN, maxY = INT32_MIN;
			while (!stack.empty()) {
				std::uint32_t cell = finishCells[stack.back()];
				stack.pop_back();
				std::int32_t x = std::int32_t(cell % std::uint32_t(gridSize.X)), y = std::int32_t(cell / std::uint32_t(gridSize.X));
				minX = std::min<std::int32_t>(minX, x);
				minY = std::min<std::int32_t>(minY, y);
				maxX = std::max<std::int32_t>(maxX, x);
				maxY = std::max<std::int32_t>(maxY, y);
				const std::int32_t neighbors[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
				for (const auto& offset : neighbors) {
					std::int32_t nx = x + offset[0], ny = y + offset[1];
					if (nx < 0 || ny < 0 || nx >= gridSize.X || ny >= gridSize.Y) {
						continue;
					}
					std::uint32_t neighbor = std::uint32_t(nx + ny * gridSize.X);
					auto it = std::lower_bound(finishCells.begin(), finishCells.end(), neighbor);
					std::size_t next = std::size_t(it - finishCells.begin());
					if (it != finishCells.end() && *it == neighbor && !visited[next]) {
						visited[next] = true;
						stack.push_back(next);
					}
				}
			}
			finishes.push_back(Rectf(minX * TileSize, minY * TileSize, (maxX - minX + 1) * TileSize, (maxY - minY + 1) * TileSize));
		}

		// By the group and the order, those of the same order and group are one end around all of them
		std::stable_sort(waypoints.begin(), waypoints.end(), [](const Waypoint& a, const Waypoint& b) {
			return (a.Group != b.Group ? a.Group < b.Group : a.Order < b.Order);
		});
		std::size_t count = 0;
		for (std::size_t i = 0; i < waypoints.size(); i++) {
			if (count > 0 && waypoints[count - 1].Group == waypoints[i].Group && waypoints[count - 1].Order == waypoints[i].Order) {
				waypoints[count - 1].Area.Union(waypoints[i].Area);
			} else {
				waypoints[count++] = waypoints[i];
			}
		}

		// The next order of the group follows, the last waypoint of the group leads to every finish
		for (std::size_t i = 0; i < count && links.size() < maxLinks; i++) {
			const Waypoint& from = waypoints[i];
			bool isLast = (i + 1 == count || waypoints[i + 1].Group != from.Group);
			if (!isLast) {
				if (waypoints[i + 1].Order == from.Order + 1) {
					EventLink& link = links.emplace_back();
					link.From = from.Area;
					link.To = waypoints[i + 1].Area;
					link.Channel = "Race";
					link.Id = from.Group;
					link.Directed = true;
					link.Missing = false;
				}
				continue;
			}
			for (std::size_t f = 0; f < finishes.size() && links.size() < maxLinks; f++) {
				EventLink& link = links.emplace_back();
				link.From = from.Area;
				link.To = finishes[f];
				link.Channel = "Race";
				link.Id = from.Group;
				link.Directed = true;
				link.Missing = false;
			}
		}
	}

	std::int32_t EventPreviews::CollectDirections(const Formats::EventRecord& event, EventDirection (&directions)[4])
	{
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
		if (descriptor == nullptr) {
			return 0;
		}
		Vector2f sums[4];
		bool used[4] = {};
		for (std::uint8_t i = 0; i < descriptor->FieldCount; i++) {
			const EventParamField& field = descriptor->Fields[i];
			if (field.Direction == DirectionAxis::None) {
				continue;
			}
			std::uint8_t group = std::min<std::uint8_t>(field.DirectionGroup, 3);
			float value = float(EventCatalog::ReadField(event, field)) * (field.DirectionNegative ? -1.0f : 1.0f);
			if (field.Direction == DirectionAxis::X) {
				sums[group].X += value;
			} else {
				sums[group].Y += value;
			}
			used[group] = true;
		}
		std::int32_t count = 0;
		for (std::uint8_t g = 0; g < 4; g++) {
			if (used[g] && (sums[g].X != 0.0f || sums[g].Y != 0.0f)) {
				directions[count].Vector = sums[g];
				directions[count].Group = g;
				count++;
			}
		}
		return count;
	}

	bool EventPreviews::GetLight(const Formats::EventRecord& event, EventLightShape& light)
	{
		light = EventLightShape();
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
		// The parameters as the lights of the game read them, an event pack may mean something else by them
		if (descriptor == nullptr || EventCatalog::IsExtension(descriptor)) {
			return false;
		}
		auto byte = [&event](std::int32_t offset) {
			return float(event.Params[offset]);
		};
		auto word = [&event](std::int32_t offset) {
			return float(std::uint16_t(event.Params[offset] | (event.Params[offset + 1] << 8)));
		};
		switch (EventType(event.Type)) {
			case EventType::LightSteady:
				// StaticRadialLight::OnActivatedAsync()
				light.Intensity = byte(0) / 255.0f;
				light.Brightness = byte(1) / 255.0f;
				light.RadiusNear = word(2);
				light.RadiusFar = word(4);
				return true;
			case EventType::LightPulse:
				// PulsatingRadialLight::OnEmitLights(), the near radius pulses and the far one follows it at the same distance,
				// at the speed and from the phase PulsatingRadialLight::OnActivatedAsync() reads, in quarters of a pulse
				light.Intensity = byte(0) / 255.0f;
				light.Brightness = byte(1) / 255.0f;
				light.RadiusNear = word(2);
				light.Pulse = word(4);
				light.RadiusFar = light.RadiusNear + word(6);
				light.PulseSpeed = byte(8) * 0.0072f;
				light.PulsePhase = byte(9) * 1.5707963f;
				return true;
			case EventType::LightFlicker:
				// FlickerLight::OnEmitLights(), the far radius goes between the near one and the far one, weaker overall
				light.Intensity = byte(0) / 255.0f * 0.2f;
				light.Brightness = byte(1) / 255.0f * 0.4f;
				light.RadiusNear = word(2);
				light.RadiusFar = word(4);
				light.Flickers = true;
				return true;
			default:
				return false;
		}
	}

	float EventPreviews::GetLightStrength(float distance, float radiusNear, float radiusFar)
	{
		// lightBlend(clamp(1 - (d - near) / (1 - near))) of distances relative to the far radius, which is (far - d) / (far - near)
		if (radiusFar <= 0.0f || distance >= radiusFar) {
			return 0.0f;
		}
		if (distance <= radiusNear || radiusFar <= radiusNear) {
			return 1.0f;
		}
		float t = std::clamp((radiusFar - distance) / (radiusFar - radiusNear), 0.0f, 1.0f);
		return t * t * t;
	}

	float EventPreviews::GetLightPulse(const EventLightShape& light, float animTime)
	{
		if (light.Pulse == 0.0f || light.PulseSpeed <= 0.0f) {
			return 0.0f;
		}
		// PulsatingRadialLight::OnUpdate() moves the phase on by the speed every frame, all lights from the start of the
		// level, so those of the same speed and phase pulse together
		float frames = animTime / UI::Canvas::AnimTimeMultiplier;
		return std::sin(light.PulsePhase + light.PulseSpeed * frames) * light.Pulse;
	}

	bool EventPreviews::GetAmbientLight(const Formats::EventRecord& event, std::uint8_t& intensity)
	{
		// Player::HandleAreaEventAt(), an event pack may mean something else by the parameter
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
		if (EventType(event.Type) != EventType::LightAmbient || descriptor == nullptr || EventCatalog::IsExtension(descriptor)) {
			return false;
		}
		intensity = event.Params[0];
		return true;
	}

	bool EventPreviews::CollectAmbientLight(const LevelDocument& document, std::int32_t step, Vector2i& size, SmallVectorImpl<std::uint8_t>& intensities)
	{
		intensities.clear();
		size = Vector2i();
		Vector2i gridSize = document.GetEventGridSize();
		if (gridSize.X <= 0 || gridSize.Y <= 0 || step <= 0) {
			return false;
		}

		// Positions in blocks, the middle of the first block is at 0.5
		struct Source
		{
			Vector2f Position;
			std::uint8_t Intensity;
		};
		SmallVector<Source, 0> sources;
		for (const DocumentEvent& event : document.Events) {
			std::uint8_t intensity;
			if (GetAmbientLight(event.Record, intensity)) {
				float x = float(event.Cell % std::uint32_t(gridSize.X)) + 0.5f, y = float(event.Cell / std::uint32_t(gridSize.X)) + 0.5f;
				sources.push_back({ Vector2f(x / float(step), y / float(step)), intensity });
			}
		}
		if (sources.empty()) {
			return false;
		}

		// The nearest event and the nearest one of another intensity of each block, passed on from block to block
		size = Vector2i((gridSize.X + step - 1) / step, (gridSize.Y + step - 1) / step);
		std::size_t count = std::size_t(size.X) * std::size_t(size.Y);
		SmallVector<std::int32_t, 0> nearest, other;
		nearest.resize(count, -1);
		other.resize(count, -1);
		auto offer = [&](std::int32_t x, std::int32_t y, std::int32_t candidate) {
			std::size_t i = std::size_t(y) * std::size_t(size.X) + std::size_t(x);
			std::int32_t& a = nearest[i];
			std::int32_t& b = other[i];
			if (candidate < 0 || candidate == a || candidate == b) {
				return;
			}
			if (a < 0) {
				a = candidate;
				return;
			}
			Vector2f center(float(x) + 0.5f, float(y) + 0.5f);
			float distance = (sources[candidate].Position - center).SqrLength();
			float distanceA = (sources[a].Position - center).SqrLength();
			if (sources[candidate].Intensity == sources[a].Intensity) {
				if (distance < distanceA) {
					a = candidate;
				}
			} else if (distance < distanceA) {
				b = a;
				a = candidate;
			} else if (b < 0 || distance < (sources[b].Position - center).SqrLength()) {
				b = candidate;
			}
		};
		auto pull = [&](std::int32_t x, std::int32_t y, std::int32_t fromX, std::int32_t fromY) {
			if (fromX < 0 || fromY < 0 || fromX >= size.X || fromY >= size.Y) {
				return;
			}
			std::size_t from = std::size_t(fromY) * std::size_t(size.X) + std::size_t(fromX);
			std::int32_t a = nearest[from], b = other[from];
			offer(x, y, a);
			offer(x, y, b);
		};
		for (std::int32_t s = 0; s < std::int32_t(sources.size()); s++) {
			offer(std::min<std::int32_t>(std::int32_t(sources[s].Position.X), size.X - 1), std::min<std::int32_t>(std::int32_t(sources[s].Position.Y), size.Y - 1), s);
		}
		// Down and back up, each row both ways, as a distance transform sweeps
		for (std::int32_t y = 0; y < size.Y; y++) {
			for (std::int32_t x = 0; x < size.X; x++) {
				pull(x, y, x - 1, y);
				pull(x, y, x - 1, y - 1);
				pull(x, y, x, y - 1);
				pull(x, y, x + 1, y - 1);
			}
			for (std::int32_t x = size.X - 1; x >= 0; x--) {
				pull(x, y, x + 1, y);
			}
		}
		for (std::int32_t y = size.Y - 1; y >= 0; y--) {
			for (std::int32_t x = size.X - 1; x >= 0; x--) {
				pull(x, y, x + 1, y);
				pull(x, y, x + 1, y + 1);
				pull(x, y, x, y + 1);
				pull(x, y, x - 1, y + 1);
			}
			for (std::int32_t x = 0; x < size.X; x++) {
				pull(x, y, x - 1, y);
			}
		}

		// From the nearest intensity towards the other one, by how much nearer the block is to the other event than the
		// events are to each other, half-way where both are as near
		intensities.resize(count);
		for (std::int32_t y = 0; y < size.Y; y++) {
			for (std::int32_t x = 0; x < size.X; x++) {
				std::size_t i = std::size_t(y) * std::size_t(size.X) + std::size_t(x);
				std::int32_t a = nearest[i], b = other[i];
				float value = (a >= 0 ? float(sources[a].Intensity) : 255.0f);
				if (a >= 0 && b >= 0) {
					Vector2f center(float(x) + 0.5f, float(y) + 0.5f);
					float gap = (sources[b].Position - sources[a].Position).Length();
					if (gap > 0.0f) {
						float distanceA = (sources[a].Position - center).Length();
						float distanceB = (sources[b].Position - center).Length();
						float t = std::clamp(0.5f + 0.5f * (distanceA - distanceB) / gap, 0.0f, 0.5f);
						value += (float(sources[b].Intensity) - value) * t;
					}
				}
				intensities[i] = std::uint8_t(std::round(value));
			}
		}
		return true;
	}

	float EventPreviews::GetDirectionLength(float strength)
	{
		// By the square root of the strength, a tube of 8 reaches into the next tile, the strongest one a few tiles away
		return DirectionBaseLength + DirectionLengthScale * std::sqrt(std::max(strength, 0.0f));
	}

	Vector2f EventPreviews::GetDirectionTip(Vector2f center, Vector2f direction)
	{
		float strength = direction.Length();
		return (strength > 0.0f ? center + direction / strength * GetDirectionLength(strength) : center);
	}

	float EventPreviews::GetDirectionStrength(float length)
	{
		float root = std::max(length - DirectionBaseLength, 0.0f) / DirectionLengthScale;
		return root * root;
	}

	bool EventPreviews::DescribeDefaultLevel(const LevelDocument& document, const Formats::EventRecord& event, const EventParamField& field, String& text, bool brief)
	{
		const EventDescriptor* descriptor = EventCatalog::Find(EventType(event.Type), event.Params);
		// The name of the next level as Player::HandleAreaEventAt() reads it, only a text that isn't 0; an event pack may
		// mean something else by the parameter
		if (descriptor == nullptr || EventCatalog::IsExtension(descriptor) || EventType(event.Type) != EventType::AreaEndOfLevel ||
			field.Type != ParamType::TextId || field.Offset != 2 || EventCatalog::ReadField(event, field) != 0) {
			return false;
		}
		// LevelHandler::ResolveNextLevelName(), no level at all goes back to the main menu
		bool bonus = ((event.Params[0] & std::uint8_t(ExitType::TypeMask)) == std::uint8_t(ExitType::Bonus));
		const String& level = (bonus ? document.Properties.SecretLevel : document.Properties.NextLevel);
		if (brief) {
			// Under the field of the next level, the narrow inspector shows only that much
			// TRANSLATORS: In place of the level name under the next level of an exit in the event properties, the level properties set none, keep it short
			String name = (level.empty() ? String(_x("editor", "none - main menu")) : level);
			// TRANSLATORS: Under the next level of an exit in the event properties, {} is the next (or secret) level from the level properties, keep it short
			text = (bonus ? _fx("editor", "Default (secret): {}", name) : _fx("editor", "Default: {}", name));
		} else {
			// TRANSLATORS: In place of the level name above an exit in the level, the level properties set no next level
			String name = (level.empty() ? String(_x("editor", "none - back to the main menu")) : level);
			// TRANSLATORS: Above an exit in the level, {} is the next (or secret) level from the level properties the exit goes to
			text = (bonus ? _fx("editor", "Secret level (default): {}", name) : _fx("editor", "Next level (default): {}", name));
		}
		return true;
	}

	bool EventPreviews::GetUsedText(const LevelDocument& document, const Formats::EventRecord& event, const EventParamField& field, StringView& text)
	{
		std::int32_t index = EventCatalog::ReadField(event, field);
		if (field.Type != ParamType::TextId || index < 0 || index >= std::int32_t(document.Texts.size()) || document.Texts[index].empty()) {
			return false;
		}
		text = document.Texts[index];

		// Player::HandleAreaEventAt(), an event pack may mean something else by the parameters
		std::int32_t part = -1;
		if (!EventCatalog::IsExtension(EventCatalog::Find(EventType(event.Type), event.Params))) {
			if (EventType(event.Type) == EventType::AreaEndOfLevel && field.Offset == 2) {
				part = event.Params[3];
			} else if (EventType(event.Type) == EventType::AreaText && field.Offset == 0) {
				part = (event.Params[1] != 0 ? std::int32_t(event.Params[1]) : -1);
			}
		}
		if (part < 0) {
			return true;
		}

		// LevelHandler::GetLevelText()
		std::int32_t count = 0;
		std::size_t start = 0;
		for (std::size_t i = 0; i < text.size(); i++) {
			if (text[i] == '|') {
				if (count == part) {
					text = text.slice(start, i);
					return true;
				}
				count++;
				start = i + 1;
			}
		}
		text = (count == part ? text.exceptPrefix(start) : StringView());
		return true;
	}

	std::int32_t EventPreviews::FindTextPartField(const EventDescriptor& descriptor, std::int32_t textField)
	{
		if (textField < 0 || textField >= std::int32_t(descriptor.FieldCount) || EventCatalog::IsExtension(&descriptor)) {
			return -1;
		}
		// As GetUsedText() reads it
		std::uint8_t textOffset = descriptor.Fields[textField].Offset;
		std::int32_t partOffset;
		if (EventType(descriptor.Type) == EventType::AreaEndOfLevel && textOffset == 2) {
			partOffset = 3;
		} else if (EventType(descriptor.Type) == EventType::AreaText && textOffset == 0) {
			partOffset = 1;
		} else {
			return -1;
		}
		for (std::int32_t i = 0; i < std::int32_t(descriptor.FieldCount); i++) {
			const EventParamField& field = descriptor.Fields[i];
			if (i != textField && field.Offset == partOffset && field.Type != ParamType::TextId) {
				return i;
			}
		}
		return -1;
	}
}

#endif
