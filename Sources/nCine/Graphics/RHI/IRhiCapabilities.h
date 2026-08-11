#pragma once

#include "../../../Main.h"

namespace nCine::RHI
{
	/**
		@brief Interface to query the runtime capabilities of the selected RHI backend

		Abstracts access to the version numbers, information strings, integer limits and extension availability
		flags of the active rendering device, so renderer code can adapt to it without issuing backend queries
		directly. The values and extensions are the OpenGL ones the render pipeline has always read, named
		after their `GL_` constants (@ref IntValues::MaxTextureSize is `GL_MAX_TEXTURE_SIZE`); a backend that
		has no OpenGL context publishes the equivalent limit of its own API under the same name.
		@ref RhiCapabilitiesBase is the shared implementation every backend derives from, aliased as
		@ref RHI::Capabilities for the backend selected at compile time.
	*/
	class IRhiCapabilities
	{
	public:
		/** @brief API version component */
		enum class ApiVersion
		{
			Major,
			Minor,
			Release
		};

		/** @brief Device information strings */
		struct InfoStrings
		{
			const char* vendor = nullptr;
			const char* renderer = nullptr;
			const char* apiVersion = nullptr;
			const char* shadingLanguageVersion = nullptr;
		};

		/** @brief Queryable runtime integer value */
		enum class IntValues
		{
			MaxTextureSize = 0,
			MaxTextureImageUnits,
			MaxUniformBlockSize,
			MaxUniformBlockSizeNormalized,
			MaxUniformBufferBindings,
			MaxVertexUniformBlocks,
			MaxFragmentUniformBlocks,
			UniformBufferOffsetAlignment,
			MaxVertexAttribStride,
			MaxColorAttachments,
			NumProgramBinaryFormats,
			/**
				@brief Instance batch size this backend requires, or 0 to let the uniform block budget decide

				The block budget is normally what limits a batch, so most backends leave this at 0 and the
				batcher sizes itself as "block size / instance stride". Where that derivation does not hold the
				backend states the count here, and it is applied everywhere a batch is sized - both shader
				compilation paths and @ref RenderBatcher, which pins its minimum and maximum to it.

				Two kinds of backend publish one. On the RSX it is a hard ceiling: the instance array reaches
				the vertex program through its constant registers rather than a bindable buffer, so every
				batched variant is compiled offline for one fixed count and cannot address more however much
				block space is published - exceeding it overruns the instance array. Elsewhere it is a
				deliberate choice: a backend that copies the whole array into the draw rather than binding it
				would otherwise be handed tens of KB of per-draw uniform data, which is not a shape the
				hardware is meant to be fed.
			*/
			MaxBatchSize,

			Count
		};

		/** @brief Queryable runtime integer array value */
		enum class ArrayIntValues
		{
			ProgramBinaryFormats = 0,

			Count
		};

		/** @brief Queryable OpenGL extension, only ever available with the OpenGL family backend */
		enum class Extensions
		{
			KhrDebug = 0,
			ArbTextureStorage,
			ArbBufferStorage,
			ArbGetProgramBinary,
#if defined(RHI_GL_PROFILE_ES) && !defined(DEATH_TARGET_EMSCRIPTEN) && !defined(DEATH_TARGET_SWITCH) && !defined(DEATH_TARGET_UNIX)
			OesGetProgramBinary,
#endif
			ExtTextureCompressionS3tc,
			AmdCompressedAtcTexture,
			ImgTextureCompressionPvrtc,
			KhrTextureCompressionAstcLdr,
#if defined(RHI_GL_PROFILE_ES) || defined(DEATH_TARGET_EMSCRIPTEN)
			OesCompressedEtc1Rgb8Texture,
#endif
			Count
		};

		virtual ~IRhiCapabilities() = 0;

		/** @brief Returns the specified API version component */
		virtual std::int32_t GetApiVersion(ApiVersion version) const = 0;
		/** @brief Returns the device information strings */
		virtual const InfoStrings& GetInfoStrings() const = 0;
		/** @brief Returns a runtime integer value */
		virtual std::int32_t GetValue(IntValues valueName) const = 0;
		/** @brief Returns an element of a runtime integer array value */
		virtual std::int32_t GetArrayValue(ArrayIntValues arrayValueName, std::uint32_t index) const = 0;
		/** @brief Returns `true` if the specified OpenGL extension is available */
		virtual bool HasExtension(Extensions extensionName) const = 0;
	};

	inline IRhiCapabilities::~IRhiCapabilities() {}

#ifndef DOXYGEN_GENERATING_OUTPUT
	/**
		@brief Fake capabilities that report no available capabilities

		Null implementation of @ref IRhiCapabilities used when no rendering device is present (for example in
		headless or server builds); every query returns zero, an empty set of strings or no extension support.
	*/
	class NullRhiCapabilities : public IRhiCapabilities
	{
	public:
		inline std::int32_t GetApiVersion(ApiVersion version) const override {
			return 0;
		}
		inline const InfoStrings& GetInfoStrings() const override {
			return _infoStrings;
		}
		inline std::int32_t GetValue(IntValues valueName) const override {
			return 0;
		}
		inline std::int32_t GetArrayValue(ArrayIntValues arrayValueName, std::uint32_t index) const override {
			return 0;
		}
		inline bool HasExtension(Extensions extensionName) const override {
			return false;
		}

	private:
		InfoStrings _infoStrings;
	};
#endif
}
