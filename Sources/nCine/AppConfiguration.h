#pragma once

#include "Primitives/Vector2.h"

#include <Containers/Array.h>
#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine
{
	/// Stores initialization settings for an nCine application
	class AppConfiguration
	{
	public:
		static constexpr std::int32_t WindowPositionIgnore = INT32_MAX;

		AppConfiguration();

		// User configurable compile-time variables
		/// Interval for frame timer accumulation average and log
		float frameTimerLogInterval;

		/// The screen resolution
		/*! \note If either `x` or `y` are zero then the screen resolution will not be changed. */
		Vector2i resolution;

		/// Window position coordinates in the virtual screen made of all the connected monitors
		/*! \note It can also be used to go full screen on a monitor that is not the primary one of the system. */
		/*! \note The `WindowPositionIgnore` value can be used in either or both dimensions when a specific position is not needed. */
		Vector2i windowPosition;

		/// Whether the screen is going to be in fullscreen mode
		bool fullscreen;
		/// Whether the window is going to be resizable
		bool resizable;
		/// Whether the window size is automatically scaled by the display factor
		bool windowScaling;
		/// Maximum number of frames to render per second or 0 for no limit
		unsigned int frameLimit;

		/// Window title
		String windowTitle;
		/// Window icon filename
		String windowIconFilename;

		/// Whether mapping is used to update OpenGL buffers
		bool useBufferMapping;
		/// Fixed size of render commands to be collected for batching on Emscripten and ANGLE
		/*! \note Increasing this value too much might negatively affect batching shaders compilation time.
		A value of zero restores the default behavior of non fixed size for batches. */
		unsigned int fixedBatchSize;
		/// Path for the binary shaders cache (or empty to disable binary shader cache)
		/*! \note Even if the path is set the functionality might still not be supported by the OpenGL context */
		String shaderCachePath;

		/// Maximum size in bytes for each VBO collecting geometry data
		unsigned long vboSize;
		/// Maximum size in bytes for each IBO collecting index data
		unsigned long iboSize;
		/// Maximum size for the pool of VAOs
		unsigned int vaoPoolSize;
		/// Initial size for the pool of render commands
		unsigned int renderCommandPoolSize;

#if defined(WITH_IMGUI) || defined(DOXYGEN_GENERATING_OUTPUT)
		/// Whether the debug overlay is enabled
		bool withDebugOverlay;
#endif
		/// Whether the audio subsystem is enabled
		bool withAudio;
		/// Whether the threading subsystem is enabled
		bool withThreads;
		/// Whether the scenegraph based rendering is enabled
		bool withScenegraph;
		/// Whether the vertical synchronization is enabled
		bool withVSync;
		/// Whether the OpenGL debug context is enabled
		bool withGlDebugContext;

		/// Returns path for the application to load data from
		const String& dataPath() const;
		/// @overload
		String& dataPath();

		/// Returns `true` if the OpenGL profile is going to be core
		inline bool glCoreProfile() const {
			return glCoreProfile_;
		}
		/// Returns `true` if the OpenGL context is going to be forward compatible
		inline bool glForwardCompatible() const {
			return glForwardCompatible_;
		}
		/// Returns major version number of the OpenGL context
		inline unsigned int glMajorVersion() const {
			return glMajorVersion_;
		}
		/// Returns minor version number of the OpenGL context
		inline unsigned int glMinorVersion() const {
			return glMinorVersion_;
		}

		/// Returns number of arguments passed on the command-line
		inline std::size_t argc() const {
			return argv_.size();
		}
		/// Returns selected argument from the ones passed on the command-line
		const StringView argv(int index) const;

	private:
		// Pre-configured compile-time variables
		const bool glCoreProfile_;
		const bool glForwardCompatible_;
		const unsigned int glMajorVersion_;
		const unsigned int glMinorVersion_;

#if defined(DEATH_TARGET_WINDOWS)
		Array<String> argv_;
#else
		Array<StringView> argv_;
#endif
		String dataPath_;

		friend class MainApplication;
	};
}
