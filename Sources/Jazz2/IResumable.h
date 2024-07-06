#pragma once

#include "../Common.h"

#include <IO/Stream.h>

using namespace Death::IO;

namespace Jazz2
{
	class IResumable
	{
	public:
		IResumable() {}

		virtual bool SerializeResumableToStream(Stream& dest) = 0;
	};
}