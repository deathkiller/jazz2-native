/*
 *  IXWebSocketPerMessageDeflateOptions.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <cstdint>
#include <string>

namespace ix
{
	/**
		@brief Options for per-message deflate compression (RFC 7692).
	
		Used by @ref WebSocket and @ref WebSocketHandshake to negotiate and configure compression.
	*/
	class WebSocketPerMessageDeflateOptions
	{
	public:
		/**
		 * @brief Construct options for per-message deflate.
		 * @param enabled Enable per-message deflate.
		 * @param clientNoContextTakeover Client no context takeover flag.
		 * @param serverNoContextTakeover Server no context takeover flag.
		 * @param clientMaxWindowBits Max window bits for client.
		 * @param serverMaxWindowBits Max window bits for server.
		 */
		WebSocketPerMessageDeflateOptions(
			bool enabled = false,
			bool clientNoContextTakeover = false,
			bool serverNoContextTakeover = false,
			uint8_t clientMaxWindowBits = kDefaultClientMaxWindowBits,
			uint8_t serverMaxWindowBits = kDefaultServerMaxWindowBits);

		/**
		 * @brief Construct options from extension string.
		 * @param extension The extension string from HTTP header.
		 */
		WebSocketPerMessageDeflateOptions(std::string extension);

		/**
		 * @brief Generate the per-message deflate HTTP header value.
		 * @return The header string.
		 */
		std::string generateHeader();

		/** @brief Check if per-message deflate is enabled. */
		bool enabled() const;

		/** @brief Get client no context takeover flag. */
		bool getClientNoContextTakeover() const;

		/** @brief Get server no context takeover flag. */
		bool getServerNoContextTakeover() const;

		/** @brief Get server max window bits. */
		uint8_t getServerMaxWindowBits() const;

		/** @brief Get client max window bits. */
		uint8_t getClientMaxWindowBits() const;

		/**
		 * @brief Check if a string starts with a prefix.
		 * @param str The string to check.
		 * @param start The prefix.
		 * @return True if str starts with start.
		 */
		static bool startsWith(const std::string& str, const std::string& start);

		/**
		 * @brief Remove spaces from a string.
		 * @param str The string to process.
		 * @return String with spaces removed.
		 */
		static std::string removeSpaces(const std::string& str);

		static uint8_t const kDefaultClientMaxWindowBits; /**< Default client max window bits. */
		static uint8_t const kDefaultServerMaxWindowBits; /**< Default server max window bits. */

	private:
		bool _enabled;
		bool _clientNoContextTakeover;
		bool _serverNoContextTakeover;
		uint8_t _clientMaxWindowBits;
		uint8_t _serverMaxWindowBits;

		/**
		 * @brief Sanitize the client max window bits value.
		 */
		void sanitizeClientMaxWindowBits();
	};
}
