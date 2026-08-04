#pragma once

#include "../RhiCapabilitiesBase.h"

namespace nCine::RHI::GL
{
	/**
		@brief Runtime capabilities of the OpenGL family backend

		Queries the active OpenGL, OpenGL ES or WebGL context once at construction and caches the version
		numbers, information strings, integer limits and extension availability flags in
		@ref RhiCapabilitiesBase, so the rest of the renderer can look them up without further driver calls.
	*/
	class GLRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		GLRhiCapabilities();

	private:
		/** @brief Checks availability of the specified OpenGL extensions */
		void CheckGLExtensions(const char* extensionNames[], bool results[], std::uint32_t numExtensionsToCheck) const;
	};
}
