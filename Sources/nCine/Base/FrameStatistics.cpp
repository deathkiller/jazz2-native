#include "FrameStatistics.h"
#include "../../Main.h"

#include <algorithm>
#include <cstring>

#if defined(DEATH_TARGET_WINDOWS_RT)
#	include <winrt/Windows.System.h>
#elif defined(DEATH_TARGET_WINDOWS)
#	pragma comment(lib, "psapi")
#
#	include <CommonWindows.h>
#	include <psapi.h>
#elif defined(DEATH_TARGET_APPLE)
#	include <mach/mach.h>
#elif defined(DEATH_TARGET_ANDROID) || (defined(DEATH_TARGET_UNIX) && defined(__linux__))
#	include <cstdio>
#	include <unistd.h>
#elif defined(DEATH_TARGET_N64)
#	include <n64sys.h>
#elif defined(DEATH_TARGET_3DS)
#	include <3ds.h>
#	include <malloc.h>
#elif defined(DEATH_TARGET_DREAMCAST)
#	include "../Backends/Dc/DcPlatform.h"
#endif

namespace nCine
{
	bool FrameStatistics::_enabled = false;
	FrameStatistics::Snapshot FrameStatistics::_snapshot = {};
	TimeStamp FrameStatistics::_intervalStart;
	std::uint32_t FrameStatistics::_frames = 0;
	double FrameStatistics::_frameTimeSum = 0.0;
	float FrameStatistics::_maxFrameTime = 0.0f;
	double FrameStatistics::_phaseTimeSums[(std::size_t)Phase::Count] = {};
	double FrameStatistics::_drawCallSum = 0.0;
	double FrameStatistics::_renderCommandSum = 0.0;
	FrameStatistics::CounterAccumulator FrameStatistics::_counters[MaxCounters] = {};
	std::uint32_t FrameStatistics::_counterCount = 0;

	void FrameStatistics::SetEnabled(bool enabled)
	{
		if (_enabled == enabled) {
			return;
		}

		_enabled = enabled;
		if (enabled) {
			// Whatever an earlier session left behind is stale by now, and a snapshot of it would show up as
			// the first numbers on screen. Emptying it counts as a change, so what was derived from it goes too.
			ResetAccumulators();
			const std::uint32_t sequence = _snapshot.Sequence;
			_snapshot = {};
			_snapshot.Sequence = sequence + 1;
		}
	}

	void FrameStatistics::AddDrawCalls(std::uint32_t drawCalls, std::uint32_t renderCommands)
	{
		if (!_enabled) {
			return;
		}

		// Summed straight into the interval, every frame is counted by EndFrame() anyway
		_drawCallSum += drawCalls;
		_renderCommandSum += renderCommands;
	}

	void FrameStatistics::AddCounter(const char* name, float value, Unit unit, float limit)
	{
		if (!_enabled) {
			return;
		}

		// A handful of entries at most, and compared by content because the same literal can live at a
		// different address in each translation unit that reports it
		CounterAccumulator* counter = nullptr;
		for (std::uint32_t i = 0; i < _counterCount; i++) {
			if (_counters[i].Name == name || std::strcmp(_counters[i].Name, name) == 0) {
				counter = &_counters[i];
				break;
			}
		}
		if (counter == nullptr) {
			if (_counterCount >= MaxCounters) {
				return;
			}
			counter = &_counters[_counterCount++];
			counter->Name = name;
			counter->Sum = 0.0;
			counter->Frames = 0;
			counter->ReportedInFrame = false;
		}

		counter->Sum += value;
		counter->Limit = limit;
		counter->Type = unit;
		counter->ReportedInFrame = true;
	}

	void FrameStatistics::EndFrame(const float* phaseTimes, float frameTime)
	{
		if (!_enabled) {
			return;
		}

		for (std::size_t i = 0; i < (std::size_t)Phase::Count; i++) {
			_phaseTimeSums[i] += phaseTimes[i];
		}
		_frameTimeSum += frameTime;
		_maxFrameTime = std::max(_maxFrameTime, frameTime);
		_frames++;

		for (std::uint32_t i = 0; i < _counterCount; i++) {
			if (_counters[i].ReportedInFrame) {
				_counters[i].ReportedInFrame = false;
				_counters[i].Frames++;
			}
		}

		if (_intervalStart.secondsSince() >= AveragingInterval) {
			PublishSnapshot();
			ResetAccumulators();
		}
	}

	void FrameStatistics::ResetAccumulators()
	{
		_intervalStart = TimeStamp::now();
		_frames = 0;
		_frameTimeSum = 0.0;
		_maxFrameTime = 0.0f;
		for (std::size_t i = 0; i < (std::size_t)Phase::Count; i++) {
			_phaseTimeSums[i] = 0.0;
		}
		_drawCallSum = 0.0;
		_renderCommandSum = 0.0;
		// The counters are forgotten as well, so a backend that stops reporting one does not leave it frozen on
		// screen with its last value
		_counterCount = 0;
	}

	void FrameStatistics::PublishSnapshot()
	{
		if (_frames == 0) {
			return;
		}

		const double framesInv = 1.0 / double(_frames);
		_snapshot.Sequence++;
		_snapshot.FrameCount = _frames;
		_snapshot.FrameTime = float(_frameTimeSum * framesInv * 1000.0);
		_snapshot.MaxFrameTime = _maxFrameTime * 1000.0f;
		for (std::size_t i = 0; i < (std::size_t)Phase::Count; i++) {
			_snapshot.PhaseTimes[i] = float(_phaseTimeSums[i] * framesInv * 1000.0);
		}
		_snapshot.DrawCalls = float(_drawCallSum * framesInv);
		_snapshot.RenderCommands = float(_renderCommandSum * framesInv);

		_snapshot.CounterCount = 0;
		for (std::uint32_t i = 0; i < _counterCount; i++) {
			const CounterAccumulator& counter = _counters[i];
			if (counter.Frames == 0) {
				// Reported only in the frame the interval ended with, it is picked up by the next one
				continue;
			}
			Counter& dst = _snapshot.Counters[_snapshot.CounterCount++];
			dst.Name = counter.Name;
			dst.Value = float(counter.Sum / double(counter.Frames));
			dst.Limit = counter.Limit;
			dst.Type = counter.Type;
		}

		// Twice a second rather than every frame: on some of these a query walks the heap or reads a file
		QueryMemoryUsage(_snapshot.MemoryUsed, _snapshot.MemoryTotal);
	}

	void FrameStatistics::QueryMemoryUsage(std::uint64_t& used, std::uint64_t& total)
	{
		used = 0;
		total = 0;

#if defined(DEATH_TARGET_WINDOWS_RT)
		// The Xbox caps what a game may use, which is the number worth comparing against
		used = winrt::Windows::System::MemoryManager::AppMemoryUsage();
		total = winrt::Windows::System::MemoryManager::AppMemoryUsageLimit();
#elif defined(DEATH_TARGET_WINDOWS)
		// Private bytes, what the process has committed for itself, rather than the working set, which also
		// counts the shared pages of every loaded library and drops whenever the system trims it
		PROCESS_MEMORY_COUNTERS_EX counters = {};
		counters.cb = sizeof(counters);
		if (::GetProcessMemoryInfo(::GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
			used = counters.PrivateUsage;
		}
#elif defined(DEATH_TARGET_APPLE)
		// The physical footprint is the number the system itself holds against the app (and what Xcode shows)
		task_vm_info_data_t info;
		mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
		if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
			used = info.phys_footprint;
		}
#elif defined(DEATH_TARGET_ANDROID) || (defined(DEATH_TARGET_UNIX) && defined(__linux__))
		// The resident set, the second field of statm, in pages
		const long pageSize = ::sysconf(_SC_PAGESIZE);
		if (FILE* f = (pageSize > 0 ? std::fopen("/proc/self/statm", "r") : nullptr)) {
			unsigned long size = 0, resident = 0;
			if (std::fscanf(f, "%lu %lu", &size, &resident) == 2) {
				used = std::uint64_t(resident) * std::uint64_t(pageSize);
			}
			std::fclose(f);
		}
#elif defined(DEATH_TARGET_N64)
		// Out of the 4 or 8 MB of RDRAM, less the code, the framebuffers and the audio buffers
		heap_stats_t stats;
		sys_get_heap_stats(&stats);
		used = std::uint64_t(stats.used);
		total = std::uint64_t(stats.total);
#elif defined(DEATH_TARGET_3DS)
		// The regular heap only - the linear heap every texture and vertex buffer comes out of is reported
		// by the graphics backend, next to the video memory
		const struct ::mallinfo info = ::mallinfo();
		used = std::uint64_t(info.uordblks);
		total = std::uint64_t(envGetHeapSize());
#elif defined(DEATH_TARGET_DREAMCAST)
		std::size_t heapUsed, heapTotal;
		Backends::DcPlatform::GetHeapUsage(heapUsed, heapTotal);
		used = heapUsed;
		total = heapTotal;
#endif
	}
}
