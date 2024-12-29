#pragma once

#include "../../Main.h"

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)

#include "../../nCine/Threading/Thread.h"

#include <atomic>

#include <CommonWindows.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::UI
{
	/** @brief Allows interactions with running Discord client */
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

		bool Connect(const StringView clientId);
		void Disconnect();
		bool IsSupported() const;
		std::uint64_t GetUserId() const;
		StringView GetUserDisplayName() const;
		bool SetRichPresence(const RichPresence& richPresence);

		static DiscordRpcClient& Get();

	private:
		DiscordRpcClient(const DiscordRpcClient&) = delete;
		DiscordRpcClient& operator=(const DiscordRpcClient&) = delete;

		enum class Opcodes : std::uint32_t {
			Handshake,
			Frame,
			Close,
			Ping,
			Pong
		};

#	if defined(DEATH_TARGET_WINDOWS)
		std::atomic<HANDLE> _hPipe;
		HANDLE _hEventRead;
		HANDLE _hEventWrite;
		String _pendingFrame;
#	else
		std::atomic_int32_t _sockFd;
#	endif
		Thread _thread;
		std::int32_t _nonce;
		String _clientId;
		std::uint64_t _userId;
		String _userDisplayName;

		void ProcessInboundFrame(const char* json, std::size_t length, std::size_t allocated);
		bool WriteFrame(Opcodes opcode, const char* buffer, std::uint32_t bufferSize);

		static void OnBackgroundThread(void* args);
	};
}

#endif