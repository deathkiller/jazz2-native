/*
 *  IXUuid.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2017 Machine Zone. All rights reserved.
 */
#pragma once

#include <string>

namespace ix
{
	/**
		@brief Generate a random UUID (version 4).
		@return Random UUID string.
	*/
	std::string uuid4();
}
