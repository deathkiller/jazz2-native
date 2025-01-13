#pragma once

#include "../../Main.h"

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX) || defined(DOXYGEN_GENERATING_OUTPUT)

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
		/** @brief Rich presence description */
		struct RichPresence
		{
			/** @brief State text */
			String State;
			/** @brief Details description text */
			String Details;
			/** @brief Large image ID */
			String LargeImage;
			/** @brief Large image tooltip text */
			String LargeImageTooltip;
			/** @brief Small image ID */
			String SmallImage;
			/** @brief Small image tooltip text */
			String SmallImageTooltip;
		};

		DiscordRpcClient();
		~DiscordRpcClient();

		/** @brief Connects to a local Discord client */
		bool Connect(StringView clientId);
		/** @brief Disconnects from a local Discord client */
		void Disconnect();
		/** @brief Returns `true` if Discord is running and connection is active */
		bool IsSupported() const;
		/** @brief Returns a user ID of the logged-in user */
		std::uint64_t GetUserId() const;
		/** @brief Returns a display name of the logged-in user */
		StringView GetUserDisplayName() const;
		/** @brief Sets rich presence */
		bool SetRichPresence(const RichPresence& richPresence);

		/** @brief Returns static instance of @ref DiscordRpcClient */
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