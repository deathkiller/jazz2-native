#include "Sha256.h"

#include <cstring>

namespace Death { namespace Cryptography {
//###==##====#=====--==~--~=~- --- -- -  -  -   -

	namespace
	{
		constexpr std::uint32_t RoundConstants[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
		};

		inline std::uint32_t RotateRight(std::uint32_t value, std::uint32_t count)
		{
			return (value >> count) | (value << (32 - count));
		}
	}

	Sha256::Sha256()
	{
		Reset();
	}

	void Sha256::Reset()
	{
		_state[0] = 0x6a09e667;
		_state[1] = 0xbb67ae85;
		_state[2] = 0x3c6ef372;
		_state[3] = 0xa54ff53a;
		_state[4] = 0x510e527f;
		_state[5] = 0x9b05688c;
		_state[6] = 0x1f83d9ab;
		_state[7] = 0x5be0cd19;
		_totalLength = 0;
		_bufferLength = 0;
	}

	void Sha256::Update(const void* data, std::size_t length)
	{
		const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
		_totalLength += length;

		if (_bufferLength > 0) {
			std::size_t toCopy = sizeof(_buffer) - _bufferLength;
			if (toCopy > length) {
				toCopy = length;
			}
			std::memcpy(_buffer + _bufferLength, bytes, toCopy);
			_bufferLength += toCopy;
			bytes += toCopy;
			length -= toCopy;
			if (_bufferLength < sizeof(_buffer)) {
				return;
			}
			ProcessBlock(_buffer);
			_bufferLength = 0;
		}

		while (length >= sizeof(_buffer)) {
			ProcessBlock(bytes);
			bytes += sizeof(_buffer);
			length -= sizeof(_buffer);
		}

		if (length > 0) {
			std::memcpy(_buffer, bytes, length);
			_bufferLength = length;
		}
	}

	void Sha256::Finish(std::uint8_t (&digest)[DigestLength])
	{
		std::uint64_t bitLength = _totalLength * 8;

		_buffer[_bufferLength++] = 0x80;
		if (_bufferLength > sizeof(_buffer) - 8) {
			std::memset(_buffer + _bufferLength, 0, sizeof(_buffer) - _bufferLength);
			ProcessBlock(_buffer);
			_bufferLength = 0;
		}
		std::memset(_buffer + _bufferLength, 0, sizeof(_buffer) - 8 - _bufferLength);
		for (std::int32_t i = 0; i < 8; i++) {
			_buffer[sizeof(_buffer) - 1 - i] = std::uint8_t(bitLength >> (i * 8));
		}
		ProcessBlock(_buffer);
		_bufferLength = 0;

		for (std::int32_t i = 0; i < 8; i++) {
			digest[i * 4 + 0] = std::uint8_t(_state[i] >> 24);
			digest[i * 4 + 1] = std::uint8_t(_state[i] >> 16);
			digest[i * 4 + 2] = std::uint8_t(_state[i] >> 8);
			digest[i * 4 + 3] = std::uint8_t(_state[i]);
		}
	}

	void Sha256::Compute(const void* data, std::size_t length, std::uint8_t (&digest)[DigestLength])
	{
		Sha256 sha;
		sha.Update(data, length);
		sha.Finish(digest);
	}

	void Sha256::ProcessBlock(const std::uint8_t* block)
	{
		std::uint32_t w[64];
		for (std::int32_t i = 0; i < 16; i++) {
			w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
				(std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
		}
		for (std::int32_t i = 16; i < 64; i++) {
			std::uint32_t s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
			std::uint32_t s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}

		std::uint32_t a = _state[0], b = _state[1], c = _state[2], d = _state[3];
		std::uint32_t e = _state[4], f = _state[5], g = _state[6], h = _state[7];

		for (std::int32_t i = 0; i < 64; i++) {
			std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
			std::uint32_t ch = (e & f) ^ (~e & g);
			std::uint32_t t1 = h + s1 + ch + RoundConstants[i] + w[i];
			std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
			std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			std::uint32_t t2 = s0 + maj;
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}

		_state[0] += a;
		_state[1] += b;
		_state[2] += c;
		_state[3] += d;
		_state[4] += e;
		_state[5] += f;
		_state[6] += g;
		_state[7] += h;
	}

}}
