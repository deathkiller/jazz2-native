#pragma once

#include "Primitives/Vector2.h"

#include <Containers/Array.h>
#include <Containers/String.h>

using namespace Death::Containers;

namespace nCine
{
	/**
		@brief Initialization settings for an nCine application
		
		Populated in @ref IAppEventHandler::OnPreInitialize() to configure the window, renderer and
		enabled subsystems before the engine starts up.
	*/
	class AppConfiguration
	{
		friend class MainApplication;

	public:
		/** @brief Specifies uninitialized window position coordinate */
		static constexpr std::int32_t WindowPositionIgnore = INT32_MAX;

		AppConfiguration();

		// User configurable compile-time variables
		/** @brief The screen resolution */
		Vector2i Resolution;

		/** @brief Window position coordinates in the virtual screen made of all the connected monitors */
		Vector2i WindowPosition;
		
		/** @brief Maximum number of frames to render per second or 0 for no limit */
		std::uint32_t FrameLimit;

		/** @brief Interval for frame timer accumulation average and log */
		float FrameTimerLogInterval;

		/** @brief Whether the screen is going to be in fullscreen mode */
		bool Fullscreen;
		/** @brief Whether the window is going to be resizable */
		bool Resizable;
		/** @brief Whether the window size is automatically scaled by the display factor */
		bool WindowScaling;

		/** @brief Whether mapping is used to update OpenGL buffers */
		bool UseBufferMapping;
		/** @brief Whether persistently mapped buffer storage is used for streaming buffers, if the device supports it */
		bool UseBufferStorage;
		/**
			@brief Fixed size of render commands to be collected for batching, overriding what the device asks for

			0 lets the rendering backend decide - either from its uniform block budget, or from the
			`IRhiCapabilities::IntValues::MaxBatchSize` it publishes when the budget is not what limits it.
			Set this only to override that from the build (`WITH_FIXED_BATCH_SIZE`) or as a driver workaround.
		*/
		std::uint32_t FixedBatchSize;
		/** @brief Path for the binary shaders cache (or empty to disable binary shader cache) */
		String ShaderCachePath;

		/** @brief Window title */
		String WindowTitle;
		/** @brief Window icon filename */
		String WindowIconFilename;

		/** @brief Maximum size in bytes for each VBO collecting geometry data */
		std::uint32_t VboSize;
		/** @brief Maximum size in bytes for each IBO collecting index data */
		std::uint32_t IboSize;
		/** @brief Maximum size for the pool of VAOs */
		std::uint32_t VaoPoolSize;
		/** @brief Initial size for the pool of render commands */
		std::uint32_t RenderCommandPoolSize;

#if defined(WITH_IMGUI) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Whether the debug overlay is enabled */
		bool WithDebugOverlay;
#endif
		/** @brief Whether the audio subsystem is enabled */
		bool WithAudio;
		/** @brief Whether the graphics subsystem is enabled */
		bool WithGraphics;
		/** @brief Whether the threading subsystem is enabled */
		bool WithThreads;
		/** @brief Whether the scenegraph based rendering is enabled */
		bool WithScenegraph;
		/** @brief Whether the vertical synchronization is enabled */
		bool WithVSync;
		/** @brief Whether the OpenGL debug context is enabled */
		bool WithGlDebugContext;

		/** @brief Returns path for the application to load data from */
		const String& GetDataPath() const;
		/** @overload */
		String& GetDataPath();

		/** @brief Returns `true` if the OpenGL profile is going to be core */
		inline bool IsGlCoreProfile() const {
			return _glCoreProfile;
		}
		/** @brief Returns `true` if the OpenGL context is going to be forward compatible */
		inline bool IsGlForwardCompatible() const {
			return _glForwardCompatible;
		}
		/** @brief Returns major version number of the OpenGL context */
		inline std::uint32_t GetGlMajorVersion() const {
			return _glMajorVersion;
		}
		/** @brief Returns minor version number of the OpenGL context */
		inline std::uint32_t GetGlMinorVersion() const {
			return _glMinorVersion;
		}

		/** @brief Returns number of arguments passed on the command-line */
		inline std::size_t GetArgc() const {
			return _argv.size();
		}
		/** @brief Returns selected argument from the ones passed on the command-line */
		const StringView GetArgv(std::size_t index) const;

	private:
		// Pre-configured compile-time variables
		const bool _glCoreProfile;
		const bool _glForwardCompatible;
		const std::uint32_t _glMajorVersion;
		const std::uint32_t _glMinorVersion;

#if defined(DEATH_TARGET_WINDOWS)
		Array<String> _argv;
#else
		Array<StringView> _argv;
#endif
		String _dataPath;
	};
}
