/*
 *  IXGetFreePort.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone. All rights reserved.
 */

#pragma once

namespace ix
{
	/**
		@brief Get a free TCP port on the local machine.

		Returns an available port number for use in network tests or dynamic server startup.
	*/
	int getFreePort();
}
