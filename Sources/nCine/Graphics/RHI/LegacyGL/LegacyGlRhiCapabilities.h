#pragma once

#include "../RhiCapabilitiesBase.h"
#include "LegacyGlDevice.h"
#include "LegacyGlRenderTarget.h"
#include "LegacyGlTexture.h"

namespace nCine::RHI::LegacyGL
{
	/**
		@brief Runtime capabilities of the legacy OpenGL backend

		The limits are the ones the backend itself imposes rather than the ones the driver reports: what
		bounds this backend is its own fixed-function pipeline (one texture unit's environment, host-memory
		uniforms decoded by the draw dispatch), not the numbers a modern compatibility context would answer
		with. The exception is the texture dimension, which is a real driver limit and is where
		@ref LegacyGlDevice::GetMaxTextureDimension() gets it from.

		The identification strings, on the other hand, ARE the driver's - a GL here is a real
		implementation with a name worth logging, unlike the consoles' fixed hardware.
	*/
	class LegacyGlRhiCapabilities : public RhiCapabilitiesBase
	{
	public:
		LegacyGlRhiCapabilities()
		{
			// Drives the tileset texture chunking in ContentResolver; prebaked content that is larger anyway
			// is paged internally by LegacyGlTexture rather than rejected (see the note on
			// LegacyGlDevice::GetMaxTextureDimension())
			SetDeviceCapabilities("Legacy OpenGL", LegacyGlDevice::GetMaxTextureDimension(),
				std::int32_t(LegacyGlTexture::MaxTextureUnits), 64 * 1024, 16,
				std::int32_t(LegacyGlRenderTarget::MaxColorAttachments));

			LegacyGlDevice::DescribeContext(_majorVersion, _minorVersion, _infoStrings.vendor,
				_infoStrings.renderer, _infoStrings.apiVersion);

			LogCapabilities();
		}
	};
}
