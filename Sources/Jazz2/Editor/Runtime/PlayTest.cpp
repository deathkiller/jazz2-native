#include "PlayTest.h"

#if defined(WITH_EDITOR)

#include "../../Tiles/TileMap.h"

#include <IO/MemoryStream.h>

#include <cmath>

using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// The path is sampled when the player moves this far, or after this many frames
		constexpr float PathMinDistance = 2.0f;
		constexpr float PathMaxInterval = 15.0f;
		// A move longer than this between two frames is a warp, which starts a new segment
		constexpr float PathWarpDistance = 200.0f;
	}

	void PlayPath::Clear()
	{
		Segments.clear();
	}

	std::int32_t PlayPath::GetPointCount() const
	{
		std::int32_t count = 0;
		for (const Segment& segment : Segments) {
			count += std::int32_t(segment.Points.size());
		}
		return count;
	}

	void PlayPath::Record(Vector2f pos, float speed, float timeMult)
	{
		if (Segments.empty()) {
			Segments.emplace_back();
		}
		auto& points = Segments.back().Points;
		_time += timeMult;
		if (!points.empty() && (pos - points.back()).Length() < PathMinDistance && _time < PathMaxInterval) {
			return;
		}
		if (!points.empty() && (pos - points.back()).Length() > PathWarpDistance) {
			BreakSegment(Break::Warped);
		}
		Segments.back().Points.push_back(pos);
		Segments.back().Speeds.push_back(speed);
		_time = 0.0f;

		// The oldest points go first when there are too many
		while (GetPointCount() > MaxPoints && Segments.size() > 1) {
			Segments.erase(Segments.begin());
		}
	}

	void PlayPath::BreakSegment(Break reason)
	{
		if (Segments.empty() || Segments.back().Points.empty()) {
			return;
		}
		Segments.back().End = reason;
		Segments.emplace_back();
	}

	bool IPlayTest::FindSpawnPositionNear(Tiles::TileMap* tileMap, Vector2f pos, float radius, Vector2f& result)
	{
		if (tileMap == nullptr) {
			return false;
		}

		Vector2i bounds = tileMap->GetLevelBounds();
		auto fits = [tileMap](Vector2f candidate) {
			// The hitbox of the player, see LevelHandler::FindClearSpawnPosition()
			AABBf aabb = AABBf(candidate.X - 11.0f, candidate.Y + 8.0f - 26.0f, candidate.X + 11.0f, candidate.Y + 8.0f + 12.0f);
			Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true };
			return tileMap->IsTileEmpty(aabb, params);
		};

		// In growing rings around the position, up first, because the player falls down from there
		constexpr float Step = 8.0f;
		for (float distance = 0.0f; distance <= radius; distance += Step) {
			std::int32_t count = std::max(1, std::int32_t(distance * 6.2832f / Step));
			for (std::int32_t i = 0; i < count; i++) {
				float angle = -1.5708f + 6.2832f * i / count;
				Vector2f candidate(pos.X + std::cos(angle) * distance, pos.Y + std::sin(angle) * distance);
				if (candidate.X < 16.0f || candidate.Y < 16.0f || candidate.X > bounds.X - 16.0f || candidate.Y > bounds.Y - 16.0f) {
					continue;
				}
				if (fits(candidate)) {
					result = candidate;
					return true;
				}
			}
		}
		return false;
	}

	bool IPlayTest::SaveLevel(const LevelDocument& document, StringView levelName, SmallVectorImpl<std::uint8_t>& data)
	{
		MemoryStream s;
		if (!document.Save(s, true, 0)) {
			LOGE("Level \"{}\" cannot be prepared for playing", levelName);
			return false;
		}
		data.assign(s.GetBuffer(), s.GetBuffer() + s.GetSize());
		return true;
	}
}

#endif
