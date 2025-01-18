#pragma once

#include "Primitives/Vector2.h"

#include <Containers/Array.h>
#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine
{
	/** @brief Stores initialization settings for an nCine application */
	class AppConfiguration
	{
	public:
		/** @brief Specifies uninitialized window position coordinate */
		static constexpr std::int32_t WindowPositionIgnore = INT32_MAX;

		AppConfiguration();

		// User configurable compile-time variables
		/** @brief Interval for frame timer accumulation average and log */
		float frameTimerLogInterval;

		/** @brief The screen resolution */
		Vector2i resolution;

		/** @brief Window position coordinates in the virtual screen made of all the connected monitors */
		Vector2i windowPosition;

		/** @brief Whether the screen is going to be in fullscreen mode */
		bool fullscreen;
		/** @brief Whether the window is going to be resizable */
		bool resizable;
		/** @brief Whether the window size is automatically scaled by the display factor */
		bool windowScaling;
		/** @brief Maximum number of frames to render per second or 0 for no limit */
		unsigned int frameLimit;

		/** @brief Window title */
		String windowTitle;
		/** @brief Window icon filename */
		String windowIconFilename;

		/** @brief Whether mapping is used to update OpenGL buffers */
		bool useBufferMapping;
		/** @brief Fixed size of render commands to be collected for batching on Emscripten and ANGLE */
		unsigned int fixedBatchSize;
		/** @brief Path for the binary shaders cache (or empty to disable binary shader cache) */
		String shaderCachePath;

		/** @brief Maximum size in bytes for each VBO collecting geometry data */
		unsigned long vboSize;
		/** @brief Maximum size in bytes for each IBO collecting index data */
		unsigned long iboSize;
		/** @brief Maximum size for the pool of VAOs */
		unsigned int vaoPoolSize;
		/** @brief Initial size for the pool of render commands */
		unsigned int renderCommandPoolSize;

#if defined(WITH_IMGUI) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Whether the debug overlay is enabled */
		bool withDebugOverlay;
#endif
		/** @brief Whether the audio subsystem is enabled */
		bool withAudio;
		/** @brief Whether the threading subsystem is enabled */
		bool withThreads;
		/** @brief Whether the scenegraph based rendering is enabled */
		bool withScenegraph;
		/** @brief Whether the vertical synchronization is enabled */
		bool withVSync;
		/** @brief Whether the OpenGL debug context is enabled */
		bool withGlDebugContext;

		/** @brief Returns path for the application to load data from */
		const String& dataPath() const;
		/** @overload */
		String& dataPath();

		/** @brief Returns `true` if the OpenGL profile is going to be core */
		inline bool glCoreProfile() const {
			return glCoreProfile_;
		}
		/** @brief Returns `true` if the OpenGL context is going to be forward compatible */
		inline bool glForwardCompatible() const {
			return glForwardCompatible_;
		}
		/** @brief Returns major version number of the OpenGL context */
		inline unsigned int glMajorVersion() const {
			return glMajorVersion_;
		}
		/** @brief Returns minor version number of the OpenGL context */
		inline unsigned int glMinorVersion() const {
			return glMinorVersion_;
		}

		/** @brief Returns number of arguments passed on the command-line */
		inline std::size_t argc() const {
			return argv_.size();
		}
		/** @brief Returns selected argument from the ones passed on the command-line */
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
