#if defined(WITH_OGC) && defined(DEATH_TARGET_GAMECUBE)

#include "OgcStorage.h"
#include "../../../Main.h"

#include <cstdio>
#include <cstring>
#include <fat.h>
#include <sys/iosupport.h>

#include <ogc/video.h>

#include <Containers/SmallVector.h>
#include <Containers/String.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace nCine::Backends
{
	namespace
	{
		/** @brief Every volume libfat can mount on the GameCube, in the order they are preferred */
		constexpr StringView FatDevices[] = { "sd:/"_s, "carda:/"_s, "cardb:/"_s };

		SmallVector<StringView, 3> _mountedDevices;
		String _bootDirectory;
	}

	namespace OgcStorage
	{
		void Initialize(const char* bootPath)
		{
			// Mounting nothing is not an error any more: a console that booted the disc usually has no SD card at all
			fatInitDefault();

			_mountedDevices.clear();
			for (StringView device : FatDevices) {
				// FindDevice() wants the colon, and nothing after it
				char name[8];
				std::size_t length = device.size() - 1;
				std::memcpy(name, device.data(), length);
				name[length] = '\0';
				if (FindDevice(name) >= 0) {
					_mountedDevices.push_back(device);
				}
			}

			_bootDirectory = {};
			if (bootPath != nullptr) {
				StringView path = bootPath;
				StringView separator = path.findLast('/');
				for (StringView device : _mountedDevices) {
					if (path.hasPrefix(device) && !separator.empty()) {
						_bootDirectory = path.prefix(separator.end());
						break;
					}
				}
			}
		}

		ArrayView<const StringView> GetMountedDevices()
		{
			return _mountedDevices;
		}

		StringView GetBootDirectory()
		{
			return _bootDirectory;
		}

		StringView GetGameId()
		{
			return NCINE_GAMECUBE_GAME_ID ""_s;
		}

		void HaltWithMessage(const char* message)
		{
			std::printf("\n%s\n", message);
			while (true) {
				VIDEO_WaitVSync();
			}
		}
	}
}

#endif
