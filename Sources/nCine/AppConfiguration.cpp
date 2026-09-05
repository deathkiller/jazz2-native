#include "AppConfiguration.h"
#include "../Main.h"

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <Utf8.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine
{
	AppConfiguration::AppConfiguration()
		:
		resolution(0, 0),
		windowPosition(WindowPositionIgnore, WindowPositionIgnore),
		frameLimit(0),
		frameTimerLogInterval(5.0f),
		fullscreen(false),
		resizable(true),
		windowScaling(true),
		useBufferMapping(false),
		useBufferStorage(true),
		// Only the build-time override lives here. A batch size that follows from the rendering device is the
		// device's own business and is published as IntValues::MaxBatchSize by the backend that knows it
		// (GLRhiCapabilities for the ANGLE/Emscripten/WinRT targets, GxmRhiCapabilities, RsxRhiCapabilities);
		// everything that sizes a batch consults that, so leaving this 0 is what lets the device speak.
#if defined(WITH_FIXED_BATCH_SIZE) && WITH_FIXED_BATCH_SIZE > 0
		fixedBatchSize(WITH_FIXED_BATCH_SIZE),
#else
		fixedBatchSize(0),
#endif
#if defined(WITH_IMGUI)
		vboSize(512 * 1024),
		iboSize(128 * 1024),
#else
		vboSize(64 * 1024),
		iboSize(8 * 1024),
#endif
		vaoPoolSize(16),
		renderCommandPoolSize(32),
#if defined(WITH_IMGUI)
		withDebugOverlay(false),
#endif
		withAudio(true),
		withGraphics(true),
		withScenegraph(true),
		withThreads(false),
		withVSync(true),
		withGlDebugContext(false),

		// Compile-time variables
#if defined(WITH_RHI_LEGACYGL)
		// The fixed-function GL backend needs the fixed-function pipeline, which only a compatibility
		// context has - a core profile has neither glBegin/glInterleavedArrays nor the texture
		// environment the effect tables are expressed in. 1.3 is what it actually uses (the texture
		// combiners); asking for exactly that also keeps a driver from handing out a context that
		// deprecates what the backend calls.
		_glCoreProfile(false),
		_glForwardCompatible(false),
		_glMajorVersion(1),
		_glMinorVersion(3),
#else
		_glCoreProfile(true),
		_glForwardCompatible(true),
#	if defined(RHI_GL_PROFILE_ES2)
		// Real OpenGL|ES 2.0 profile (PS Vita target): ESSL 100, no UBOs, no gl_VertexID
		_glMajorVersion(2),
		_glMinorVersion(0),
#	elif defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
		_glMajorVersion(3),
		_glMinorVersion(0),
#	else
		_glMajorVersion(3),
		_glMinorVersion(3),
#	endif
#endif
		_argv(nullptr)
	{
#if defined(DEATH_TARGET_ANDROID)
		dataPath() = "assets:/"_s;
#elif defined(DEATH_TARGET_EMSCRIPTEN)
		dataPath() = fs::PathSeparator;
		// Always disable mapping on Emscripten as it is not supported by WebGL 2
		useBufferMapping = false;
#else
		dataPath() = "Content"_s + fs::PathSeparator;
#endif

#if defined(RHI_GL_PROFILE_ES2)
		// glMapBufferRange()/glUnmapBuffer() are ES 3.0 (OES_mapbuffer is write-only and not assumed), so the
		// streaming buffers must use the glBufferSubData() host-copy path on the ES2 profile
		useBufferMapping = false;
#endif

#if defined(WITH_RHI_RDP) || defined(WITH_RHI_GU) || defined(WITH_RHI_PVR) || defined(WITH_RHI_GX) || \
		defined(WITH_RHI_GS) || defined(WITH_RHI_PICA) || defined(WITH_RHI_SOFTWARE)
		// These tiers have no device memory at all: a buffer object is a host allocation, and the draw
		// dispatch reads the vertices and indices out of it with the CPU. Mapping it is therefore the
		// identity, and it is the unmapped path that costs something - RenderBuffersManager::FlushUnmap()
		// copies every byte the frame streamed from its own staging buffer into the buffer object, which is
		// one host-to-host copy of the whole tile mesh per frame for nothing. Mapping also drops the
		// staging buffers themselves, 136 KB of the 8 MB. Keyed on the BACKEND rather than the console,
		// because the property belongs to how the buffer object stores data, not to the machine it runs on.
		//
		// Every other fixed-function buffer class is the same story and was missed when the RDP got this.
		// GuBuffer, SwBuffer, PvrBuffer, GxBuffer and GsBuffer are character for character alike in the
		// three methods that matter: `MapBufferRange()` returns `_storage.data() + offset`,
		// `FlushMappedBufferRange()` and `Unmap()` are empty, and `BufferSubData()` is a plain memcpy into
		// that same storage. So mapping is the identity on all of them, while the unmapped path pays one
		// BufferSubData() of everything the frame streamed - vertices AND uniform blocks.
		//
		// Measured on the PSP: this took the commit phase from 3.5 ms to 2.0 ms of a ~17 ms frame, and the
		// staging buffers it drops also stopped competing for memory bandwidth with the audio mixer thread
		// (updatePlayers() fell from about 2.1 ms to well under 1). Only the GU number is measured; the
		// other four are enabled on the strength of their buffer classes being identical.
		useBufferMapping = true;
#endif

#if defined(DEATH_TARGET_UNIX) && (defined(WITH_SDL2) || defined(WITH_SDL3))
		// DPI queries do not seem to work reliably on X11 with SDL2
		windowScaling = false;
#endif
	}

	const String& AppConfiguration::dataPath() const
	{
		return _dataPath;
	}

	String& AppConfiguration::dataPath()
	{
		return _dataPath;
	}

	const StringView AppConfiguration::argv(std::size_t index) const
	{
		return _argv[index];
	}
}
