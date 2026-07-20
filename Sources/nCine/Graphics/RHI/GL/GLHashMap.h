#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../../Main.h"

namespace nCine::RhiGL
{
	/** @brief Key type, an OpenGL target enum */
	using key_t = GLenum;
	/** @brief Value type, an OpenGL object id */
	using value_t = GLuint;

	/**
		@brief Naive fixed-size hashmap of OpenGL targets to object ids
		
		Stores one object id per bucket, where `MappingFunc` maps an OpenGL target
		(the key) to a bucket index. Used to cache the currently bound object id for
		each target. The number of buckets `S` must match the range of the mapping
		function.
	*/
	template<std::uint32_t S, class MappingFunc>
	class GLHashMap
	{
	public:
		GLHashMap();
		/** @brief Returns the bucket for a target, mapping the key through the mapping function */
		value_t& operator[](key_t key);

	private:
		value_t buckets_[S];
		MappingFunc mappingFunc;
	};

	template<std::uint32_t S, class MappingFunc>
	GLHashMap<S, MappingFunc>::GLHashMap()
	{
		// Initializing with a null OpenGL object id
		for (std::uint32_t i = 0; i < S; i++)
			buckets_[i] = 0;
	}

	template<std::uint32_t S, class MappingFunc>
	inline value_t& GLHashMap<S, MappingFunc>::operator[](key_t key)
	{
		return buckets_[mappingFunc(key)];
	}

	/**
		@brief Maps OpenGL buffer object targets to bucket indices
		
		Mapping function for @ref GLHashMap that turns buffer object targets
		(`GL_ARRAY_BUFFER`, `GL_ELEMENT_ARRAY_BUFFER`, ...) into contiguous indices.
	*/
	class GLBufferObjectMappingFunc
	{
	public:
		/** @brief Number of distinct buffer object targets, i.e. the required bucket count */
		static const std::uint32_t Size = 6;
		/** @brief Returns the bucket index for a buffer object target */
		inline std::uint32_t operator()(key_t key) const
		{
			std::uint32_t value = 0;

			switch (key) {
				case GL_ARRAY_BUFFER:
					value = 0;
					break;
				case GL_ELEMENT_ARRAY_BUFFER:
					value = 1;
					break;
				case GL_UNIFORM_BUFFER:
					value = 2;
					break;
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0)
				// Pixel buffer objects are OpenGL ES 3.0 (and desktop GL). A real ES 2.0 target such as PS Vita's
				// vitaGL neither declares these enums nor ever binds a PBO, so the cases exist only where available.
				case GL_PIXEL_PACK_BUFFER:
					value = 3;
					break;
				case GL_PIXEL_UNPACK_BUFFER:
					value = 4;
					break;
#endif
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
				case GL_TEXTURE_BUFFER:
					value = 5;
					break;
#endif
				default:
					LOGF("No available case to handle buffer object target: 0x{:x}", key);
					break;
			}

			return value;
		}
	};

	/**
		@brief Maps OpenGL texture targets to bucket indices
		
		Mapping function for @ref GLHashMap that turns texture targets
		(`GL_TEXTURE_2D`, `GL_TEXTURE_3D`, ...) into contiguous indices.
	*/
	class GLTextureMappingFunc
	{
	public:
		/** @brief Number of distinct texture targets, i.e. the required bucket count */
		static const std::uint32_t Size = 4;
		/** @brief Returns the bucket index for a texture target */
		inline std::uint32_t operator()(key_t key) const
		{
			std::uint32_t value = 0;

			switch (key) {
#if !defined(WITH_OPENGLES) // not available in OpenGL ES
				case GL_TEXTURE_1D:
					value = 0;
					break;
#endif
				case GL_TEXTURE_2D:
					value = 1;
					break;
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_0)
				// 3D textures are OpenGL ES 3.0 (and desktop GL), unavailable and never bound on a real ES 2.0
				// target such as PS Vita's vitaGL.
				case GL_TEXTURE_3D:
					value = 2;
					break;
#endif
#if !defined(WITH_OPENGLES) || (defined(WITH_OPENGLES) && GL_ES_VERSION_3_2)
				case GL_TEXTURE_BUFFER:
					value = 3;
					break;
#endif
				default:
					LOGF("No available case to handle texture target: 0x{:x}", key);
					break;
			}

			return value;
		}
	};

}
