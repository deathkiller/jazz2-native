#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MpGameMode.h"
#include "Reason.h"
#include "ServerInitialization.h"

#if (defined(WITH_ONLINE_MULTIPLAYER) && defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN) && \
		(defined(DEATH_TARGET_WINDOWS) || defined(WITH_CURL))) || defined(DOXYGEN_GENERATING_OUTPUT)
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

#if (defined(WITH_ONLINE_MULTIPLAYER) && defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN) && \
		(defined(DEATH_TARGET_WINDOWS) || defined(WITH_CURL))) || defined(DOXYGEN_GENERATING_OUTPUT)

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
		/**
			@brief Single row of a leaderboard included in a webhook notification

			The name is only read while the event is being serialized, so it can point to a player name owned
			elsewhere. @ref Value is rendered with the unit label passed alongside the whole list.
		*/
		struct LeaderboardEntry
		{
			/** @brief Player name */
			StringView Name;
			/** @brief Score in the unit described by the accompanying label */
			std::uint32_t Value;
		};

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
		/**
		 * @brief Called when a level has been loaded, @p playlistSize is `0` if no playlist is active
		 *
		 * @p targetLabel and @p targetValue describe the win condition of the round (e.g. @cpp "Kills" @ce
		 * and @cpp 10 @ce); pass a zero value to omit it, for example when the target is auto-weighted.
		 */
		void OnLevelChanged(StringView levelDisplayName, MpGameMode gameMode, bool reforgedGameplay, bool elimination,
							StringView targetLabel, std::uint32_t targetValue, std::int32_t playlistIndex, std::int32_t playlistSize);
		/** @brief Called when the round countdown finished and the round is running */
		void OnRoundStarted(StringView levelDisplayName, MpGameMode gameMode);
		/**
		 * @brief Called when the round ended, @p winnerName is empty if the round ended in a draw
		 *
		 * @p standings are the final rankings (best first) rendered as a leaderboard, @p scoreLabel is their
		 * plural unit (e.g. @cpp "kills" @ce). Both can be empty in game modes without meaningful standings.
		 */
		void OnRoundEnded(StringView winnerName, StringView levelDisplayName, MpGameMode gameMode, ArrayView<const LeaderboardEntry> standings = {}, StringView scoreLabel = {});
		/** @brief Called when the round ended with a winning team, @p teamMembers are listed in the notification */
		void OnRoundEndedWithTeamWinner(StringView teamName, StringView levelDisplayName, MpGameMode gameMode, ArrayView<const LeaderboardEntry> teamMembers = {}, StringView scoreLabel = {});
		/** @brief Called when a player won the championship, @p standings are the final championship rankings */
		void OnChampionshipEnded(StringView championName, std::uint32_t points, ArrayView<const LeaderboardEntry> standings = {});
		/** @brief Called when a chat message has been sent */
		void OnChatMessage(StringView playerName, StringView message, bool isAdmin);
		/** @brief Called when a player was roasted, @p attackerName is empty if roasted by the environment */
		void OnPlayerRoasted(StringView playerName, StringView attackerName);
		/** @brief Called when a player finished a lap (Race) */
		void OnPlayerLapFinished(StringView playerName, float lapSecs, std::uint32_t lap, std::uint32_t totalLaps);
		/** @brief Called when a team captured a flag (Capture The Flag) */
		void OnFlagCaptured(StringView playerName, StringView teamName, StringView flagTeamName, std::uint32_t captures, std::uint32_t totalCaptures);

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

	/** @brief Stub implementation for builds without online multiplayer (or thread support), so call sites compile everywhere */
	class WebhookClient
	{
	public:
		struct LeaderboardEntry
		{
			StringView Name;
			std::uint32_t Value;
		};

		WebhookClient(NetworkManager* server) {}

		WebhookClient(const WebhookClient&) = delete;
		WebhookClient& operator=(const WebhookClient&) = delete;

		bool IsEventEnabled(WebhookEventType type) const { return false; }
		void UpdateConfiguration() {}

		void OnServerStarted() {}
		void OnServerStopping() {}
		void OnPlayerConnected(StringView playerName, std::uint32_t playerCount, std::uint32_t maxPlayerCount) {}
		void OnPlayerDisconnected(StringView playerName, Reason reason, std::uint32_t playerCount, std::uint32_t maxPlayerCount) {}
		void OnLevelChanged(StringView levelDisplayName, MpGameMode gameMode, bool reforgedGameplay, bool elimination,
							StringView targetLabel, std::uint32_t targetValue, std::int32_t playlistIndex, std::int32_t playlistSize) {}
		void OnRoundStarted(StringView levelDisplayName, MpGameMode gameMode) {}
		void OnRoundEnded(StringView winnerName, StringView levelDisplayName, MpGameMode gameMode, ArrayView<const LeaderboardEntry> standings = {}, StringView scoreLabel = {}) {}
		void OnRoundEndedWithTeamWinner(StringView teamName, StringView levelDisplayName, MpGameMode gameMode, ArrayView<const LeaderboardEntry> teamMembers = {}, StringView scoreLabel = {}) {}
		void OnChampionshipEnded(StringView championName, std::uint32_t points, ArrayView<const LeaderboardEntry> standings = {}) {}
		void OnChatMessage(StringView playerName, StringView message, bool isAdmin) {}
		void OnPlayerRoasted(StringView playerName, StringView attackerName) {}
		void OnPlayerLapFinished(StringView playerName, float lapSecs, std::uint32_t lap, std::uint32_t totalLaps) {}
		void OnFlagCaptured(StringView playerName, StringView teamName, StringView flagTeamName, std::uint32_t captures, std::uint32_t totalCaptures) {}
	};

#endif
}

#endif
