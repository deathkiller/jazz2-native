#pragma once

#include "../../Common.h"

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)

#include "../../nCine/Threading/Thread.h"

#include <CommonWindows.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::UI
{
	class DiscordRpcClient
	{
	public:
		struct RichPresence
		{
			String State;
			String Details;
			String LargeImage;
			String LargeImageTooltip;
			String SmallImage;
			String SmallImageTooltip;
		};

		DiscordRpcClient();
		~DiscordRpcClient();

		bool Connect(const StringView& clientId);
		void Disconnect();
		bool IsSupported() const;
		bool SetRichPresence(const RichPresence& richPresence);

		static DiscordRpcClient& Get();

	private:
		/// Deleted copy constructor
		DiscordRpcClient(const DiscordRpcClient&) = delete;
		/// Deleted assignment operator
		DiscordRpcClient& operator=(const DiscordRpcClient&) = delete;

		enum class Opcodes : std::uint32_t {
			Handshake,
			Frame,
			Close,
			Ping,
			Pong
		};

#if defined(DEATH_TARGET_WINDOWS)
		HANDLE _hPipe;
		HANDLE _hEventRead;
		HANDLE _hEventWrite;
		String _pendingFrame;
#else
		int _sockFd;
#endif
		Thread _thread;
		std::int32_t _nonce;
		String _clientId;

		bool WriteFrame(Opcodes opcode, const char* buffer, std::uint32_t bufferSize);

		static void OnBackgroundThread(void* args);
	};
}

#endif