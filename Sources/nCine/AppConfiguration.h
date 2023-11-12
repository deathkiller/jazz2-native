#pragma once

#include "Primitives/Vector2.h"

#include <Containers/Array.h>
#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine
{
	/// The class storing initialization settings for an nCine application
	class AppConfiguration
	{
	public:
		/// Default constructor setting the defaults
		AppConfiguration();

		// User configurable compile-time variables
		/// The interval for frame timer accumulation average and log
		float frameTimerLogInterval;

		/// The screen resolution
		/*! \note If either `x` or `y` are zero then the screen resolution will not be changed. */
		Vector2i resolution;
		/// The flag is `true` if the screen is going to be in fullscreen mode
		bool fullscreen;
		/// The flag is `true` if the window is going to be resizable
		bool resizable;
		/// The flag is `true` if the window size is automatically scaled by the display factor
		bool windowScaling;
		/// The maximum number of frames to render per second or 0 for no limit
		unsigned int frameLimit;

		/// The window title
		String windowTitle;
		/// The window icon filename
		String windowIconFilename;

		/// The flag is `true` if mapping is used to update OpenGL buffers
		bool useBufferMapping;
		/// Fixed size of render commands to be collected for batching on Emscripten and ANGLE
		/*! \note Increasing this value too much might negatively affect batching shaders compilation time.
		A value of zero restores the default behavior of non fixed size for batches. */
		unsigned int fixedBatchSize;
		/// The path for the binary shaders cache (or empty to disable binary shader cache)
		/*! \note Even if the path is set the functionality might still not be supported by the OpenGL context */
		String shaderCachePath;

		/// The maximum size in bytes for each VBO collecting geometry data
		unsigned long vboSize;
		/// The maximum size in bytes for each IBO collecting index data
		unsigned long iboSize;
		/// The maximum size for the pool of VAOs
		unsigned int vaoPoolSize;
		/// The initial size for the pool of render commands
		unsigned int renderCommandPoolSize;

#if defined(WITH_IMGUI)
		/// The flag is `true` if the debug overlay is enabled
		bool withDebugOverlay;
#endif
		/// The flag is `true` if the audio subsystem is enabled
		bool withAudio;
		/// The flag is `true` if the threading subsystem is enabled
		bool withThreads;
		/// The flag is `true` if the scenegraph based rendering is enabled
		bool withScenegraph;
		/// The flag is `true` if the vertical synchronization is enabled
		bool withVSync;
		/// The flag is `true` if the OpenGL debug context is enabled
		bool withGlDebugContext;

		/// \returns The path for the application to load data from
		const String& dataPath() const;
		/// \returns The path for the application to load data from
		String& dataPath();

		/// \returns True if the OpenGL profile is going to be core
		inline bool glCoreProfile() const {
			return glCoreProfile_;
		}
		/// \returns True if the OpenGL context is going to be forward compatible
		inline bool glForwardCompatible() const {
			return glForwardCompatible_;
		}
		/// \returns The major version number of the OpenGL context
		inline unsigned int glMajorVersion() const {
			return glMajorVersion_;
		}
		/// \returns The minor version number of the OpenGL context
		inline unsigned int glMinorVersion() const {
			return glMinorVersion_;
		}

		/// \returns The number of arguments passed on the command-line
		inline std::size_t argc() const {
			return argv_.size();
		}
		/// \returns The selected argument from the ones passed on the command-line
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
