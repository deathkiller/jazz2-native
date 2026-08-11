#pragma once

#include "IRhiCapabilities.h"

namespace nCine::RHI
{
	/**
		@brief Backend-independent part of the RHI capabilities

		Owns the cached version numbers, information strings, integer limits and extension flags and answers
		every @ref IRhiCapabilities query out of them - everything that does not depend on how the values were
		obtained. A backend derives from this and fills the storage in its constructor: the OpenGL family
		backend queries the live context (see @ref GL::GLRhiCapabilities), every other backend publishes the
		limits it already knows through @ref SetDeviceCapabilities().
	*/
	class RhiCapabilitiesBase : public IRhiCapabilities
	{
	public:
		std::int32_t GetApiVersion(ApiVersion version) const override;
		inline const InfoStrings& GetInfoStrings() const override {
			return _infoStrings;
		}
		std::int32_t GetValue(IntValues valueName) const override;
		std::int32_t GetArrayValue(ArrayIntValues valueName, std::uint32_t index) const override;
		bool HasExtension(Extensions extensionName) const override;

	protected:
		/** @brief Largest number of program binary formats that can be cached */
		static constexpr std::int32_t MaxProgramBinaryFormats = 4;

		std::int32_t _majorVersion;
		std::int32_t _minorVersion;
		/** @brief Release version number (not available in OpenGL ES) */
		std::int32_t _releaseVersion;

		InfoStrings _infoStrings;

		/** @brief Cached values of the queryable integer limits */
		std::int32_t _intValues[std::int32_t(IRhiCapabilities::IntValues::Count)];
		/** @brief Cached availability flags of the queryable OpenGL extensions */
		bool _extensions[std::int32_t(IRhiCapabilities::Extensions::Count)];
		/** @brief Cached program binary formats, the first @ref IntValues::NumProgramBinaryFormats are valid */
		std::int32_t _programBinaryFormats[MaxProgramBinaryFormats];

		RhiCapabilitiesBase();

		/**
		 * @brief Publishes the capabilities of a backend that has no queryable API context
		 *
		 * The published values keep the GL-era names the render pipeline reads, but they describe the backend's
		 * own device: the window backend creates it before the capabilities are constructed, so its real limits
		 * are already known here. The version strings describe the pipeline's OpenGL 3.3 compatibility level
		 * (the feature set the backend replays), not a live GL context. The vendor is left empty, because
		 * there is no driver vendor behind these backends to name.
		 *
		 * Beside the arguments this publishes the values every such backend shares: the pipeline's
		 * uniform-binding budget (8, matching the backends' `MaxUniformBindings`), the 2048-byte vertex stride
		 * every target guarantees (the Direct3D 11 input-layout limit, the Vulkan spec minimum for
		 * `maxVertexInputBindingStride`, unconstrained on the CPU rasterizer) and no program-binary formats
		 * (@ref BinaryShaderCache is disabled outside the OpenGL family backend).
		 *
		 * @param renderer						Renderer name reported in the information strings
		 * @param maxTextureSize				Largest supported 2D texture dimension
		 * @param maxTextureImageUnits			Per-draw texture-unit budget the backend's bind tracking supports
		 * @param maxUniformBlockSize			Largest uniform block the backend accepts, normalized as well
		 * @param uniformBufferOffsetAlignment	Alignment the engine has to suballocate uniform ranges at
		 * @param maxColorAttachments			Color attachments a render target of this backend can hold
		 * @param maxBatchSize					Hard ceiling on the instance batch, or 0 where only the block size limits it
		 */
		void SetDeviceCapabilities(const char* renderer, std::int32_t maxTextureSize, std::int32_t maxTextureImageUnits,
			std::int32_t maxUniformBlockSize, std::int32_t uniformBufferOffsetAlignment, std::int32_t maxColorAttachments,
			std::int32_t maxBatchSize = 0);

		/**
		 * @brief Derives @ref IntValues::MaxUniformBlockSizeNormalized from the raw block size
		 *
		 * The raw limit is sometimes not reported correctly (and is unbounded on backends that decode uniforms
		 * on the host), so the pipeline sizes its buffers from the value clamped into the [16 KB, 64 KB] window
		 * this publishes instead.
		 */
		void NormalizeUniformBlockSize();

		/**
		 * @brief Writes the published device information, limits and capability tier to the log
		 *
		 * Called at the end of each backend's constructor, so every backend reports what it ended up
		 * publishing instead of only the OpenGL family one (which is all the log used to carry). The values
		 * keep their GL-era names in @ref IntValues, but they are spelled out neutrally here and lines that
		 * do not apply to the backend are left out - an ES2 or fixed-function backend has no uniform buffer
		 * bindings to report, and only the OpenGL family backend has program binary formats and extensions
		 * (@ref GL::GLRhiCapabilities appends those itself). Compiles away without `DEATH_TRACE`.
		 */
		void LogCapabilities() const;
	};
}
