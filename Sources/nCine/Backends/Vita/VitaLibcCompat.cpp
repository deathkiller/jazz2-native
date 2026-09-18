#include "../../../Main.h"

#if defined(DEATH_TARGET_VITA)

#include <cstring>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

// VitaSDK's inet_pton() reports "this is not an address" the way POSIX reserves for "I do not know this
// address family": its AF_INET arm forwards to sceNetInetPton() and collapses any negative result to -1
// (`orr r0, r4, r4, asr #31`), so a host name comes back as -1 instead of 0. Both callers in this
// application test the result for truth rather than for a positive value, which is what the libraries
// themselves do, so -1 reads as "yes, that was an address":
//
//  - libcurl's OpenSSL back end asks inet_pton() whether the host it is about to verify is a literal
//    address. Told that it is, it matches the certificate's iPAddress entries against four bytes of
//    nothing and rejects every certificate with "no alternative certificate subject name matches target
//    ipv4 address '<host>'" - which takes down the update check and the public server list together.
//  - ENet's enet_address_set_host_ip() is written as `if (!inet_pton(...)) return -1;`, so it accepts
//    whatever it was given and carries on with an address that was never parsed.
//
// Replacing the function is enough to fix both: this definition is in an object file, so the linker
// resolves inet_pton() here and never reaches libc's member (which exports nothing else, so nothing is
// lost by leaving it out). The parse below is the strict form POSIX describes - four decimal octets, no
// leading zeros, nothing trailing - and returns 1, 0 and -1 as it should.

extern "C" int inet_pton(int af, const char* src, void* dst)
{
	if (af == AF_INET) {
		if (src == nullptr || dst == nullptr) {
			errno = EINVAL;
			return -1;
		}

		std::uint8_t octets[4];
		const char* p = src;

		for (std::int32_t i = 0; i < 4; i++) {
			if (*p < '0' || *p > '9') {
				return 0;
			}

			// A leading zero is not allowed to be followed by more digits - "010" is rejected rather than
			// read as octal, which is the difference between inet_pton() and inet_aton()
			std::int32_t value = 0, digits = 0;
			const bool leadingZero = (*p == '0');
			while (*p >= '0' && *p <= '9') {
				value = value * 10 + (*p - '0');
				if (++digits > 3 || value > 255 || (leadingZero && digits > 1)) {
					return 0;
				}
				p++;
			}

			octets[i] = (std::uint8_t)value;

			if (i < 3) {
				if (*p != '.') {
					return 0;
				}
				p++;
			}
		}

		if (*p != '\0') {
			return 0;
		}

		std::memcpy(dst, octets, sizeof(octets));
		return 1;
	}

	// The console's stack is IPv4-only - there is no address family for IPv6 anywhere in sceNet - so no
	// address of another family can be represented, let alone used. That is reported as 0 ("src does not
	// contain a valid address in this family") rather than -1/EAFNOSUPPORT on purpose: -1 would walk
	// straight back into the truthiness tests described above, with libcurl deciding that a host name is
	// an IPv6 literal instead of an IPv4 one.
	return 0;
}

#endif
