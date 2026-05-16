/*
 *  IXUserAgent.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <string>

namespace ix
{
	/**
		@brief Utility for generating a user agent string for HTTP/WebSocket requests.
		@return String identifying the client library and version.
	*/
	std::string userAgent();
}
