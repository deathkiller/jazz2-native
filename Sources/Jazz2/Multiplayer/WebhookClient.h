#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
#include "Reason.h"
#include "ServerInitialization.h"

#if (defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)) || defined(DOXYGEN_GENERATING_OUTPUT)
#	include "../../nCine/Threading/Thread.h"
#	include "../../nCine/Threading/ThreadSync.h"

#	include <atomic>
#endif

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace nCine;

namespace Jazz2::Multiplayer
{
	class NetworkManager;

#if (defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)) || defined(DOXYGEN_GENERATING_OUTPUT)

	/**
		@brief Posts selected server events to a configured webhook

		Serializes server events into rich embed notifications and delivers them to the webhook URL from
		@ref ServerConfiguration::WebhookUrl on a background thread, so game and network threads never block
		on HTTP requests. The payload format targets Discord webhooks --- pending events are batched into
		a single message (up to 10 embeds), the per-webhook rate limit is respected (including `Retry-After`
		on HTTP 429) and every notification carries the server name, so multiple servers can be aggregated
		into a single channel. Which events are posted is selected by @ref ServerConfiguration::WebhookEvents.

		@experimental
	*/
	class WebhookClient
	{
	public:
		/** @brief Creates an instance for a running server and starts the delivery thread */
		WebhookClient(NetworkManager* server);
		/** @brief Flushes still pending events and stops the delivery thread */
		~WebhookClient();

		WebhookClient(const WebhookClient&) = delete;
		WebhookClient& operator=(const WebhookClient&) = delete;

		/** @brief Returns `true` if the specified event should be posted to the webhook */
		bool IsEventEnabled(WebhookEventType type) const;
		/** @brief Applies webhook-related changes after the server configuration has been reloaded */
		void UpdateConfiguration();

		/** @brief Called when the server has been started */
		void OnServerStarted();
		/** @brief Called when the server is about to be stopped */
		void OnServerStopping();
		/** @brief Called when a player successfully joined the server */
		void OnPlayerConnected(StringView playerName, std::uint32_t playerCount, std::uint32_t maxPlayerCount);
		/** @brief Called when a player left the server, including moderation actions described by @p reason */
		void OnPlayerDisconnected(StringView playerName, Reason reason, std::uint32_t playerCount, std::uint32_t maxPlayerCount);
		/** @brief Called when a level has been loaded, @p playlistSize is `0` if no playlist is active */
		void OnLevelChanged(StringView levelDisplayName, MpGameMode gameMode, bool reforgedGameplay, std::int32_t playlistIndex, std::int32_t playlistSize);
		/** @brief Called when the round countdown finished and the round is running */
		void OnRoundStarted(StringView levelDisplayName, MpGameMode gameMode);
		/** @brief Called when the round ended, @p winnerName is empty if the round ended in a draw */
		void OnRoundEnded(StringView winnerName, StringView levelDisplayName, MpGameMode gameMode);
		/** @brief Called when the round ended with a winning team */
		void OnRoundEndedWithTeamWinner(StringView teamName, StringView levelDisplayName, MpGameMode gameMode);
		/** @brief Called when a player won the championship */
		void OnChampionshipEnded(StringView championName, std::uint32_t points);
		/** @brief Called when a chat message has been sent */
		void OnChatMessage(StringView playerName, StringView message, bool isAdmin);

	private:
		// Discord allows up to 10 embeds in a single webhook message
		static constexpr std::size_t MaxEmbedsPerMessage = 10;
		// Bounds memory if the endpoint is unreachable or can't keep up, the oldest events are dropped
		static constexpr std::size_t MaxQueueLength = 100;
		// Keeps a safe distance from the Discord per-webhook rate limit (~30 requests/min)
		static constexpr std::uint32_t RequestIntervalMs = 2000;
		static constexpr std::uint32_t RetryDelayMs = 5000;
		static constexpr std::int32_t MaxSendAttempts = 4;

		struct EmbedField {
			StringView Name;
			StringView Value;
		};

		NetworkManager* _server;
		std::atomic<std::uint32_t> _enabledEvents;
		std::atomic<bool> _shuttingDown;
		Mutex _queueMutex;
		CondVariable _queueRefreshed;
		SmallVector<String, 0> _queue;	// Guarded by _queueMutex
		String _url;					// Guarded by _queueMutex
		String _serverIdentity;			// Guarded by _queueMutex
		Thread _thread;

		void Enqueue(String&& embed);
		String CreateEmbed(StringView description, std::uint32_t color, ArrayView<const EmbedField> fields = {});
		void SendBatch(ArrayView<const String> embeds);
		void SleepInterruptible(std::uint32_t milliseconds);

		static void OnDeliveryThread(void* param);
	};

#else

	/** @brief Stub implementation for platforms without thread support, so call sites compile everywhere */
	class WebhookClient
	{
	public:
		WebhookClient(NetworkManager* server) {}

		WebhookClient(const WebhookClient&) = delete;
		WebhookClient& operator=(const WebhookClient&) = delete;

		bool IsEventEnabled(WebhookEventType type) const { return false; }
		void UpdateConfiguration() {}

		void OnServerStarted() {}
		void OnServerStopping() {}
		void OnPlayerConnected(StringView playerName, std::uint32_t playerCount, std::uint32_t maxPlayerCount) {}
		void OnPlayerDisconnected(StringView playerName, Reason reason, std::uint32_t playerCount, std::uint32_t maxPlayerCount) {}
		void OnLevelChanged(StringView levelDisplayName, MpGameMode gameMode, bool reforgedGameplay, std::int32_t playlistIndex, std::int32_t playlistSize) {}
		void OnRoundStarted(StringView levelDisplayName, MpGameMode gameMode) {}
		void OnRoundEnded(StringView winnerName, StringView levelDisplayName, MpGameMode gameMode) {}
		void OnRoundEndedWithTeamWinner(StringView teamName, StringView levelDisplayName, MpGameMode gameMode) {}
		void OnChampionshipEnded(StringView championName, std::uint32_t points) {}
		void OnChatMessage(StringView playerName, StringView message, bool isAdmin) {}
	};

#endif
}

#endif
