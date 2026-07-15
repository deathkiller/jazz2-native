#pragma once

#ifndef DOXYGEN_GENERATING_OUTPUT
#define NCINE_INCLUDE_OPENGL
#include "../../../CommonHeaders.h"
#endif

#include "../../../../Main.h"
#include "../RhiTypes.h"

namespace nCine::RhiGL
{
	/**
		@brief Translates backend-neutral pixel formats into OpenGL formats

		Translation table of the OpenGL backend: maps each logical @ref PixelFormat (plus a BGR/BGRA
		channel-order flag) to the OpenGL internal format, external format and pixel data type consumed
		by the texture upload calls, and verifies device support for the compressed families. This is
		the only place the OpenGL texture-format constants live; pipeline code only ever names a
		@ref PixelFormat.
	*/
	struct GLTextureFormat
	{
		/**
		 * @brief Resolves a pixel format into its OpenGL internal format, external format and data type
		 *
		 * @param format          Backend-neutral pixel format
		 * @param bgr             Whether the external channel order is BGR/BGRA instead of RGB/RGBA
		 * @param internalFormat  Receives the sized OpenGL internal format
		 * @param externalFormat  Receives the external OpenGL format
		 * @param dataType        Receives the OpenGL pixel data type
		 */
		static void Resolve(PixelFormat format, bool bgr, GLint& internalFormat, GLenum& externalFormat, GLenum& dataType);

		/** @brief Verifies that the device supports the given pixel format, aborting if it does not */
		static void CheckSupport(PixelFormat format);
	};
}
