/*
 *  IXUrlParser.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <string>

namespace ix
{
	/**
		@brief URL parser utility for extracting components from a URL string.
	*/
	class UrlParser
	{
	public:
		/**
		 * @brief Parse a URL into its components.
		 * @param url Input URL string.
		 * @param protocol Output protocol.
		 * @param host Output host.
		 * @param path Output path.
		 * @param query Output query string.
		 * @param port Output port number.
		 * @return True on success.
		 */
		static bool parse(const std::string& url,
						  std::string& protocol,
						  std::string& host,
						  std::string& path,
						  std::string& query,
						  int& port);

		/**
		 * @brief Parse a URL into its components, with protocol default port detection.
		 * @param url Input URL string.
		 * @param protocol Output protocol.
		 * @param host Output host.
		 * @param path Output path.
		 * @param query Output query string.
		 * @param port Output port number.
		 * @param isProtocolDefaultPort Output flag for default port.
		 * @return True on success.
		 */
		static bool parse(const std::string& url,
						  std::string& protocol,
						  std::string& host,
						  std::string& path,
						  std::string& query,
						  int& port,
						  bool& isProtocolDefaultPort);
	};
}
