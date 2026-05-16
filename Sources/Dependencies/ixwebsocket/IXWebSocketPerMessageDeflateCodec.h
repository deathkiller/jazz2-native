/*
 *  IXWebSocketPerMessageDeflateCodec.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2018-2019 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#ifdef IXWEBSOCKET_USE_ZLIB
#include "zlib.h"
#endif
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "IXWebSocketSendData.h"

namespace ix
{
	/**
		@brief Compressor for per-message deflate (RFC 7692) in WebSocket frames.
	
		Used by @ref ix::WebSocketPerMessageDeflate to compress outgoing WebSocket messages.
	*/
	class WebSocketPerMessageDeflateCompressor
	{
	public:
		/** @brief Constructor */
		WebSocketPerMessageDeflateCompressor();
		/** @brief Destructor */
		~WebSocketPerMessageDeflateCompressor();

		/**
		 * @brief Initialize compressor state.
		 * @param deflateBits Window bits for deflate.
		 * @param clientNoContextTakeOver No context takeover flag.
		 * @return True on success.
		 */
		bool init(uint8_t deflateBits, bool clientNoContextTakeOver);

		/**
		 * @brief Compress input data to string.
		 * @param in Input data.
		 * @param out Output string.
		 * @return True on success.
		 */
		bool compress(const IXWebSocketSendData& in, std::string& out);
		bool compress(const std::string& in, std::string& out);
		bool compress(const std::string& in, std::vector<uint8_t>& out);
		bool compress(const std::vector<uint8_t>& in, std::string& out);
		bool compress(const std::vector<uint8_t>& in, std::vector<uint8_t>& out);

	private:
		/** @brief Compress data (internal template). */
		template<typename T, typename S>
		bool compressData(const T& in, S& out);
		/** @brief Check for empty uncompressed block (internal). */
		template<typename T>
		bool endsWithEmptyUnCompressedBlock(const T& value);

		int _flush; /**< Flush mode for zlib. */
		std::array<unsigned char, 1 << 14> _compressBuffer; /**< Compression buffer. */

#ifdef IXWEBSOCKET_USE_ZLIB
		z_stream _deflateState; /**< zlib deflate state. */
#endif
	};

	/**
		@brief Decompressor for per-message deflate (RFC 7692) in WebSocket frames.
	
		Used by @ref ix::WebSocketPerMessageDeflate to decompress incoming WebSocket messages.
	*/
	class WebSocketPerMessageDeflateDecompressor
	{
	public:
		/** @brief Constructor */
		WebSocketPerMessageDeflateDecompressor();
		/** @brief Destructor */
		~WebSocketPerMessageDeflateDecompressor();

		/**
		 * @brief Initialize decompressor state.
		 * @param inflateBits Window bits for inflate.
		 * @param clientNoContextTakeOver No context takeover flag.
		 * @return True on success.
		 */
		bool init(uint8_t inflateBits, bool clientNoContextTakeOver);

		/**
		 * @brief Decompress input string to output string.
		 * @param in Input compressed string.
		 * @param out Output decompressed string.
		 * @return True on success.
		 */
		bool decompress(const std::string& in, std::string& out);

	private:
		int _flush; /**< Flush mode for zlib. */
		std::array<unsigned char, 1 << 14> _compressBuffer; /**< Decompression buffer. */

#ifdef IXWEBSOCKET_USE_ZLIB
		z_stream _inflateState; /**< zlib inflate state. */
#endif
	};

}
