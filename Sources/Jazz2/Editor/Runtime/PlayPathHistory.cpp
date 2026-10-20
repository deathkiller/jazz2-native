#include "PlayPathHistory.h"

#if defined(WITH_EDITOR)

#include <IO/MemoryStream.h>

#include <cmath>

using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr std::int32_t MaxStoredSegments = 4096;
		// Bit of the flags of a path, the level was finished in it
		constexpr std::uint8_t PathExited = 0x01;
		// The speed at a point is kept in sixteenths of a pixel a frame, up to a speed no player gets to
		constexpr float SpeedScale = 16.0f;
		constexpr float MaxStoredSpeed = 1024.0f;
	}

	void PlayPathHistory::Clear()
	{
		_paths.clear();
		_lastPlayTest = 0;
		_revision++;
	}

	void PlayPathHistory::Update(const PlayPath& path, std::uint32_t playTest)
	{
		if (path.GetPointCount() == 0) {
			return;
		}
		_revision++;
		if (playTest != 0 && playTest == _lastPlayTest && !_paths.empty()) {
			_paths.back() = path;
			return;
		}
		if (std::int32_t(_paths.size()) >= MaxPaths) {
			_paths.erase(_paths.begin());
		}
		_paths.push_back(path);
		_lastPlayTest = playTest;
	}

	void PlayPathHistory::CollectDeaths(SmallVectorImpl<Vector2f>& positions) const
	{
		for (const PlayPath& path : _paths) {
			for (const PlayPath::Segment& segment : path.Segments) {
				if (segment.End == PlayPath::Break::Died && !segment.Points.empty()) {
					positions.push_back(segment.Points.back());
				}
			}
		}
	}

	float PlayPathHistory::GetBestExitTime() const
	{
		float best = -1.0f;
		for (const PlayPath& path : _paths) {
			if (path.HasExited() && (best < 0.0f || path.Duration < best)) {
				best = path.Duration;
			}
		}
		return best;
	}

	std::int32_t PlayPathHistory::GetTotalDeaths() const
	{
		std::int32_t count = 0;
		for (const PlayPath& path : _paths) {
			count += path.GetDeathCount();
		}
		return count;
	}

	bool PlayPathHistory::Read(const LevelDocument& document)
	{
		_paths.clear();
		_lastPlayTest = 0;
		_revision++;
		const Formats::ChunkRecord* chunk = document.FindChunk(Chunk);
		if (chunk == nullptr || chunk->Data.empty()) {
			return false;
		}

		MemoryStream s(chunk->Data.data(), std::int64_t(chunk->Data.size()));
		// A newer version is left alone, it's written over only by new paths
		if (s.ReadValue<std::uint8_t>() != Version) {
			return false;
		}
		std::uint32_t pathCount = s.ReadVariableUint32();
		if (pathCount > std::uint32_t(MaxPaths)) {
			return false;
		}
		for (std::uint32_t p = 0; p < pathCount; p++) {
			PlayPath& path = _paths.emplace_back();
			path.Duration = float(s.ReadVariableUint32());
			std::uint8_t flags = s.ReadValue<std::uint8_t>();
			std::uint32_t segmentCount = s.ReadVariableUint32();
			if (segmentCount > std::uint32_t(MaxStoredSegments)) {
				_paths.clear();
				return false;
			}
			std::int32_t x = 0, y = 0, points = 0;
			for (std::uint32_t i = 0; i < segmentCount; i++) {
				PlayPath::Segment& segment = path.Segments.emplace_back();
				std::uint8_t end = s.ReadValue<std::uint8_t>();
				segment.End = (end <= std::uint8_t(PlayPath::Break::Exited) ? PlayPath::Break(end) : PlayPath::Break::None);
				std::uint32_t pointCount = s.ReadVariableUint32();
				points += std::int32_t(pointCount);
				if (pointCount > std::uint32_t(MaxStoredPoints) || points > MaxStoredPoints || s.GetPosition() >= s.GetSize()) {
					_paths.clear();
					return false;
				}
				for (std::uint32_t j = 0; j < pointCount; j++) {
					x += s.ReadVariableInt32();
					y += s.ReadVariableInt32();
					// The path is drawn thicker where the player was fast
					float speed = float(s.ReadVariableUint32()) / SpeedScale;
					segment.Points.push_back(Vector2f(float(x), float(y)));
					segment.Speeds.push_back(speed);
				}
			}
			if ((flags & PathExited) != 0 && !path.Segments.empty() && !path.HasExited()) {
				path.Segments.back().End = PlayPath::Break::Exited;
			}
			if (s.GetPosition() > s.GetSize()) {
				_paths.clear();
				return false;
			}
		}
		return true;
	}

	void PlayPathHistory::Write(LevelDocument& document) const
	{
		if (_paths.empty()) {
			document.SetChunk(Chunk, {});
			return;
		}

		MemoryStream s(1024);
		s.WriteValue<std::uint8_t>(Version);
		s.WriteVariableUint32(std::uint32_t(_paths.size()));
		for (const PlayPath& path : _paths) {
			// Thinned out evenly, the ends of the segments are always kept, the deaths are shown there
			std::int32_t total = path.GetPointCount();
			std::int32_t stride = std::max<std::int32_t>((total + MaxStoredPoints - 1) / MaxStoredPoints, 1);
			std::int32_t segmentCount = std::min<std::int32_t>(std::int32_t(path.Segments.size()), MaxStoredSegments);
			s.WriteVariableUint32(std::uint32_t(std::max(path.Duration, 0.0f)));
			s.WriteValue<std::uint8_t>(path.HasExited() ? PathExited : 0);
			s.WriteVariableUint32(std::uint32_t(segmentCount));
			std::int32_t x = 0, y = 0, written = 0;
			for (std::int32_t i = 0; i < segmentCount; i++) {
				const PlayPath::Segment& segment = path.Segments[i];
				SmallVector<std::int32_t, 0> kept;
				std::int32_t count = std::int32_t(segment.Points.size());
				for (std::int32_t j = 0; j < count; j++) {
					if ((j % stride) == 0 || j == count - 1) {
						kept.push_back(j);
					}
				}
				// What doesn't fit anymore is left out, the oldest points are kept, they came first
				if (written + std::int32_t(kept.size()) > MaxStoredPoints) {
					kept.resize(std::size_t(std::max<std::int32_t>(MaxStoredPoints - written, 0)));
				}
				written += std::int32_t(kept.size());
				s.WriteValue<std::uint8_t>(std::uint8_t(segment.End));
				s.WriteVariableUint32(std::uint32_t(kept.size()));
				for (std::int32_t index : kept) {
					std::int32_t px = std::int32_t(std::round(segment.Points[index].X));
					std::int32_t py = std::int32_t(std::round(segment.Points[index].Y));
					float speed = (index < std::int32_t(segment.Speeds.size()) ? segment.Speeds[index] : 0.0f);
					s.WriteVariableInt32(px - x);
					s.WriteVariableInt32(py - y);
					s.WriteVariableUint32(std::uint32_t(std::round((speed > 0.0f ? std::min(speed, MaxStoredSpeed) : 0.0f) * SpeedScale)));
					x = px;
					y = py;
				}
			}
		}
		document.SetChunk(Chunk, ArrayView<const std::uint8_t>(s.GetBuffer(), std::size_t(s.GetSize())));
	}

	std::int32_t PlayPathHistory::SelfTest()
	{
		std::int32_t problems = 0;

		// A run with a death, then a finished one of another play-test, and the first one again replaced by a longer run
		PlayPath died;
		for (std::int32_t i = 0; i < 40; i++) {
			died.Record(Vector2f(100.0f + float(i) * 5.0f, 200.0f), 5.0f, 1.0f);
		}
		died.BreakSegment(PlayPath::Break::Died);
		for (std::int32_t i = 0; i < 20; i++) {
			died.Record(Vector2f(100.0f, 200.0f - float(i) * 5.0f), 5.0f, 1.0f);
		}
		PlayPath finished;
		for (std::int32_t i = 0; i < 30; i++) {
			finished.Record(Vector2f(64.0f + float(i) * 10.0f, 96.0f), 10.0f, 2.0f);
		}
		finished.Segments.back().End = PlayPath::Break::Exited;

		PlayPathHistory history;
		history.Update(died, 1);
		history.Update(finished, 2);
		history.Update(finished, 2);
		if (history.GetPaths().size() != 2 || history.GetTotalDeaths() != 1 || history.GetBestExitTime() != finished.Duration) {
			LOGE("Play-test paths aren't kept as they should be");
			problems++;
		}

		LevelDocument document;
		history.Write(document);
		PlayPathHistory read;
		if (!read.Read(document) || read.GetPaths().size() != 2 || read.GetTotalDeaths() != 1 || read.GetPaths()[1].Duration != finished.Duration ||
			read.GetPaths()[0].GetPointCount() != died.GetPointCount() || !read.GetPaths()[1].HasExited()) {
			LOGE("Play-test paths aren't read back as they were written");
			problems++;
		} else {
			SmallVector<Vector2f, 4> deaths, readDeaths;
			history.CollectDeaths(deaths);
			read.CollectDeaths(readDeaths);
			if (deaths.size() != readDeaths.size() || deaths.empty() || (deaths[0] - readDeaths[0]).Length() > 0.75f) {
				LOGE("Deaths in play-test paths aren't read back where they were");
				problems++;
			}
			const PlayPath::Segment& segment = read.GetPaths()[1].Segments[0];
			if (segment.Speeds.size() != segment.Points.size() || segment.Speeds.empty() || std::abs(segment.Speeds.back() - 10.0f) > 0.1f) {
				LOGE("Speeds in play-test paths aren't read back as they were");
				problems++;
			}
		}

		// Nothing is written over a chunk that couldn't be read, a newer version
		std::uint8_t newer[] = { Version + 1, 0 };
		document.SetChunk(Chunk, newer);
		if (read.Read(document) || !read.IsEmpty()) {
			LOGE("Play-test paths of a newer version are read");
			problems++;
		}
		return problems;
	}
}

#endif
