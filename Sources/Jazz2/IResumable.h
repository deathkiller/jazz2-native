﻿#pragma once

#include "../Main.h"

#include <IO/Stream.h>

using namespace Death::IO;

namespace Jazz2
{
	/** @brief Base interface of a resumable object */
	class IResumable
	{
	public:
		IResumable() {}

		/** @brief Serializes object state to a stream */
		virtual bool SerializeResumableToStream(Stream& dest) = 0;
	};
}