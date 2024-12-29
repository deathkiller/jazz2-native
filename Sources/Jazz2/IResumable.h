#pragma once

#include "../Main.h"

#include <IO/Stream.h>

using namespace Death::IO;

namespace Jazz2
{
	/** @brief Base interface of a resumable objects */
	class IResumable
	{
	public:
		IResumable() {}

		virtual bool SerializeResumableToStream(Stream& dest) = 0;
	};
}