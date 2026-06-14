#pragma once

#include "../Main.h"

#include <IO/Stream.h>

using namespace Death::IO;

namespace Jazz2
{
	/**
		@brief Base interface of a resumable object
		
		Implemented by objects that can serialize their state to a stream so the session can be saved and later
		restored (e.g. the running level), enabling the "resume on start" feature.
	*/
	class IResumable
	{
	public:
		/** @brief Creates a new instance */
		IResumable() {}

		/** @brief Serializes object state to a stream */
		virtual bool SerializeResumableToStream(Stream& dest) = 0;
	};
}