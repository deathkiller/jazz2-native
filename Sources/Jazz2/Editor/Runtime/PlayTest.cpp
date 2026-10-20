#include "PlayTest.h"

#if defined(WITH_EDITOR)

#include "../../ContentResolver.h"
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
		Duration = 0.0f;
		_pointCount = 0;
	}

	std::int32_t PlayPath::GetPointCount() const
	{
		std::int32_t count = 0;
		for (const Segment& segment : Segments) {
			count += std::int32_t(segment.Points.size());
		}
		return count;
	}

	std::int32_t PlayPath::GetDeathCount() const
	{
		std::int32_t count = 0;
		for (const Segment& segment : Segments) {
			count += (segment.End == Break::Died ? 1 : 0);
		}
		return count;
	}

	bool PlayPath::HasExited() const
	{
		for (const Segment& segment : Segments) {
			if (segment.End == Break::Exited) {
				return true;
			}
		}
		return false;
	}

	void PlayPath::Record(Vector2f pos, float speed, float timeMult)
	{
		if (Segments.empty()) {
			Segments.emplace_back();
		}
		auto& points = Segments.back().Points;
		_time += timeMult;
		Duration += timeMult;
		if (!points.empty() && (pos - points.back()).Length() < PathMinDistance && _time < PathMaxInterval) {
			return;
		}
		if (!points.empty() && (pos - points.back()).Length() > PathWarpDistance) {
			BreakSegment(Break::Warped);
		}
		Segments.back().Points.push_back(pos);
		Segments.back().Speeds.push_back(speed);
		_time = 0.0f;
		_pointCount++;

		// The oldest points go first when there are too many, a quarter of them at once so it isn't done for every point,
		// also of a single long segment
		if (_pointCount > MaxPoints) {
			constexpr std::int32_t KeptPoints = MaxPoints - MaxPoints / 4;
			_pointCount = GetPointCount();
			while (_pointCount > KeptPoints) {
				Segment& oldest = Segments.front();
				std::int32_t excess = _pointCount - KeptPoints;
				std::int32_t size = std::int32_t(oldest.Points.size());
				if (Segments.size() > 1 && size <= excess) {
					_pointCount -= size;
					Segments.erase(Segments.begin());
					continue;
				}
				// The segment being recorded keeps its last point
				std::int32_t drop = std::min<std::int32_t>(excess, size - 1);
				if (drop <= 0) {
					break;
				}
				oldest.Points.erase(oldest.Points.begin(), oldest.Points.begin() + drop);
				std::int32_t speedDrop = std::min<std::int32_t>(drop, std::int32_t(oldest.Speeds.size()));
				oldest.Speeds.erase(oldest.Speeds.begin(), oldest.Speeds.begin() + speedDrop);
				_pointCount -= drop;
			}
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

	IPlayTest::~IPlayTest()
	{
		// Released by the next loading that doesn't use them, not here
		if (_resourcesRetained) {
			ContentResolver::Get().ReleaseLoadedResources();
		}
	}

	void IPlayTest::RetainResources()
	{
		if (!_resourcesRetained) {
			_resourcesRetained = true;
			ContentResolver::Get().RetainLoadedResources();
		}
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
