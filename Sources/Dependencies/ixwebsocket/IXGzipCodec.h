/*
 *  IXGzipCodec.h
 *  Author: Benjamin Sergeant
 *  Copyright (c) 2020 Machine Zone, Inc. All rights reserved.
 */

#pragma once

#include <string>

namespace ix
{
	/**
		@brief Compress a string using gzip.

		@param str Input string to compress.
		@return Compressed string.
	*/
	std::string gzipCompress(const std::string& str);

	/**
		@brief Decompress a gzip-compressed string.

		@param in Input compressed string.
		@param out Output decompressed string.
		@return True on success.
	*/
	bool gzipDecompress(const std::string& in, std::string& out);
}
