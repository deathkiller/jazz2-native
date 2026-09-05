#pragma once

#include "../Main.h"

#include <IO/WebRequest.h>

#if defined(WITH_CURL) && (defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA))
#	include "ContentResolver.h"

#	include <Containers/String.h>
#	include <IO/FileSystem.h>

#	include <curl/curl.h>
#endif

namespace Jazz2
{
	/**
		@brief Applies whatever the platform needs on a freshly created web request before it is executed

		Nothing anywhere else, and nothing at all on a platform whose TLS stack can find its own trust store.
		The PS Vita cannot: VitaSDK's libcurl verifies against the firmware's own store under `vs0:`, a mount
		point an application's sandbox does not contain - opening it fails with `ENOENT`, not a permission
		error - and the TLS stack linked into the executable is not the system one that iTLS-Enso and friends
		patch, so nothing installed on the console supplies it either. Without this every HTTPS request fails
		to verify, which takes the server list, the update check and script web requests with it. The PSP is
		in the same position for a simpler reason: its libcurl is built on mbedTLS with a Unix default path
		(`/etc/ssl/certs/ca-certificates.crt`) that exists nowhere on a memory stick. The Nintendo 3DS is the
		PSP's case again: devkitPro's libcurl is built on mbedTLS with the same default path, and the console's
		own trust store is not something a homebrew title can read.

		The bundle therefore travels with the content (the CMake packaging fetches it at configure time) and
		is named here rather than inside @relativeref{Death::IO,WebRequest}: where a game keeps its files is
		not something that layer can know, and the handle it already exposes is enough to say so from outside.
	*/
	inline void ApplyPlatformWebRequestOptions(Death::IO::WebRequest& request)
	{
#if defined(WITH_CURL) && (defined(DEATH_TARGET_3DS) || defined(DEATH_TARGET_PSP) || defined(DEATH_TARGET_VITA))
		using namespace Death::Containers::Literals;

		// Resolved once - the path cannot change while the game is running, and a missing bundle leaves the
		// TLS stack's own default alone instead of pointing it at something that is not there either
		static const Death::Containers::String caBundlePath = []() -> Death::Containers::String {
			Death::Containers::String path = Death::IO::FileSystem::CombinePath(
				ContentResolver::Get().GetContentPath(), "cacert.pem"_s);
			if (!Death::IO::FileSystem::FileExists(path)) {
				LOGW("Certificate bundle \"{}\" is missing, HTTPS requests will fail to verify", path);
				return {};
			}
			return path;
		}();

		if (!caBundlePath.empty()) {
			if (CURL* handle = (CURL*)request.GetNativeHandle()) {
				curl_easy_setopt(handle, CURLOPT_CAINFO, caBundlePath.data());
				// The build's default CA *directory* has to go too: mbedTLS reads the whole directory before it
				// looks at the file, and on the PSP that is a Unix path that does not exist ("Error reading ca
				// cert path /etc/ssl/certs"), which fails the verification before the bundle is ever opened
				curl_easy_setopt(handle, CURLOPT_CAPATH, nullptr);
			}
		}
#else
		static_cast<void>(request);
#endif
	}
}
