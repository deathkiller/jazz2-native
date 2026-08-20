#include "WebhookClient.h"

#if defined(WITH_MULTIPLAYER) && defined(WITH_ONLINE_MULTIPLAYER) && defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)

#include "NetworkManager.h"
#include "../UI/Font.h"

#include <cstdlib>

#include <Base/Format.h>
#include <Containers/DateTime.h>
#include <IO/WebRequest.h>

using namespace Death;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Multiplayer
{
	namespace
	{
		// Softened embed accent colors (midway between the vivid Discord palette and pastel)
		constexpr std::uint32_t ColorPositive = 0x6FCF97;		// Soft green
		constexpr std::uint32_t ColorNegative = 0xF28B82;		// Soft red
		constexpr std::uint32_t ColorNeutral = 0xA8B4BE;		// Gray-blue
		constexpr std::uint32_t ColorInfo = 0x8FA8EA;			// Soft periwinkle
		constexpr std::uint32_t ColorHighlight = 0xF2CE6E;		// Soft gold
		constexpr std::uint32_t ColorChampionship = 0xF2B27C;	// Soft orange

		// Discord limits the webhook user name to 80 characters
		constexpr std::size_t MaxServerIdentityLength = 80;
		constexpr std::size_t MaxChatMessageLength = 512;

		void AppendRaw(SmallVectorImpl<char>& out, StringView text)
		{
			out.append(text.begin(), text.end());
		}

		void AppendJsonEscaped(SmallVectorImpl<char>& out, StringView text)
		{
			for (char c : text) {
				switch (c) {
					case '"': AppendRaw(out, "\\\""_s); break;
					case '\\': AppendRaw(out, "\\\\"_s); break;
					case '\n': AppendRaw(out, "\\n"_s); break;
					case '\t': AppendRaw(out, "\\t"_s); break;
					case '\r': break;
					default: {
						if ((unsigned char)c < 0x20) {
							char buffer[8];
							std::size_t length = formatInto(buffer, "\\u{:.4x}", (std::uint32_t)(unsigned char)c);
							AppendRaw(out, { buffer, length });
						} else {
							out.push_back(c);
						}
						break;
					}
				}
			}
		}

		// Player-supplied names could otherwise inject Discord markdown into notifications, mentions
		// are already suppressed by "allowed_mentions" in the payload envelope
		void AppendSanitizedName(SmallVectorImpl<char>& out, StringView name)
		{
			String stripped = UI::Font::StripFormatting(name);
			for (char c : stripped.trimmed()) {
				if (c == '\n' || c == '\r') {
					continue;
				}
				if (c == '\\' || c == '*' || c == '_' || c == '~' || c == '`' || c == '|') {
					out.push_back('\\');
				}
				out.push_back(c);
			}
		}

		StringView TruncateUtf8(StringView text, std::size_t maxLength)
		{
			if (text.size() <= maxLength) {
				return text;
			}
			// Don't cut a multi-byte UTF-8 sequence in half
			while (maxLength > 0 && ((unsigned char)text[maxLength] & 0xC0) == 0x80) {
				maxLength--;
			}
			return text.prefix(maxLength);
		}
	}

	WebhookClient::WebhookClient(NetworkManager* server)
		: _server(server), _enabledEvents(0), _shuttingDown(false)
	{
		UpdateConfiguration();

		_thread = Thread(WebhookClient::OnDeliveryThread, this);
		_thread.SetName("Webhook delivery");
	}

	WebhookClient::~WebhookClient()
	{
		// The delivery thread still drains the queue (with reduced retries) before it exits
		_queueMutex.Lock();
		_shuttingDown = true;
		_queueMutex.Unlock();
		_queueRefreshed.Signal();
		_thread.Join();
	}

	bool WebhookClient::IsEventEnabled(WebhookEventType type) const
	{
		return (_enabledEvents.load(std::memory_order_relaxed) & (std::uint32_t)type) == (std::uint32_t)type;
	}

	void WebhookClient::UpdateConfiguration()
	{
		auto& serverConfig = _server->GetServerConfiguration();

		_enabledEvents.store((std::uint32_t)serverConfig.WebhookEvents, std::memory_order_relaxed);

		// The server name doubles as the sender identity, so multiple servers can share one channel
		String serverIdentity = UI::Font::StripFormatting(serverConfig.ServerName);
		String trimmedIdentity = TruncateUtf8(serverIdentity.trimmed(), MaxServerIdentityLength);
		String url = serverConfig.WebhookUrl;

		_queueMutex.Lock();
		_url = std::move(url);
		_serverIdentity = std::move(trimmedIdentity);
		_queueMutex.Unlock();
	}

	void WebhookClient::OnServerStarted()
	{
		if (!IsEventEnabled(WebhookEventType::ServerLifecycle)) {
			return;
		}

		auto& serverConfig = _server->GetServerConfiguration();

		SmallVector<char, 256> description;
		AppendRaw(description, "\xF0\x9F\x9F\xA2" "\xE2\x80\x83" "**"_s /* 🟢 + em space */);
		AppendSanitizedName(description, serverConfig.ServerName);
		AppendRaw(description, "** is now online and accepting players"_s);

		char maxPlayers[16];
		std::size_t maxPlayersLength = formatInto(maxPlayers, "{}", serverConfig.MaxPlayerCount);

		EmbedField fields[] = {
			{ "Game Mode"_s, NetworkManager::GameModeToString(serverConfig.GameMode) },
			{ "Gameplay"_s, serverConfig.ReforgedGameplay ? "Reforged"_s : "Legacy"_s },
			{ "Max Players"_s, { maxPlayers, maxPlayersLength } }
		};
		Enqueue(CreateEmbed({ description.data(), description.size() }, ColorPositive, fields));
	}

	void WebhookClient::OnServerStopping()
	{
		if (!IsEventEnabled(WebhookEventType::ServerLifecycle)) {
			return;
		}

		auto& serverConfig = _server->GetServerConfiguration();

		SmallVector<char, 256> description;
		AppendRaw(description, "\xF0\x9F\x94\xB4" "\xE2\x80\x83" "**"_s /* 🔴 + em space */);
		AppendSanitizedName(description, serverConfig.ServerName);
		AppendRaw(description, "** has been stopped"_s);

		Enqueue(CreateEmbed({ description.data(), description.size() }, ColorNegative));
	}

	void WebhookClient::OnPlayerConnected(StringView playerName, std::uint32_t playerCount, std::uint32_t maxPlayerCount)
	{
		if (!IsEventEnabled(WebhookEventType::PlayerConnected)) {
			return;
		}

		SmallVector<char, 256> description;
		AppendRaw(description, "\xF0\x9F\x93\xA5" "\xE2\x80\x83" "**"_s /* 📥 + em space */);
		AppendSanitizedName(description, playerName);
		AppendRaw(description, "** joined the server"_s);

		char players[24];
		std::size_t playersLength = formatInto(players, " ({}/{})", playerCount, maxPlayerCount);
		AppendRaw(description, { players, playersLength });

		Enqueue(CreateEmbed({ description.data(), description.size() }, ColorPositive));
	}

	void WebhookClient::OnPlayerDisconnected(StringView playerName, Reason reason, std::uint32_t playerCount, std::uint32_t maxPlayerCount)
	{
		WebhookEventType type; StringView prefix, suffix; std::uint32_t color;
		switch (reason) {
			case Reason::Kicked:
				type = WebhookEventType::PlayerKicked; color = ColorNegative;
				prefix = "\xF0\x9F\x91\xA2" "\xE2\x80\x83" "**"_s /* 👢 */; suffix = "** was kicked from the server"_s;
				break;
			case Reason::Banned:
				type = WebhookEventType::PlayerKicked; color = ColorNegative;
				prefix = "\xE2\x9B\x94" "\xE2\x80\x83" "**"_s /* ⛔ */; suffix = "** was banned from the server"_s;
				break;
			case Reason::CheatingDetected:
				type = WebhookEventType::PlayerKicked; color = ColorNegative;
				prefix = "\xF0\x9F\x9A\xA8" "\xE2\x80\x83" "**"_s /* 🚨 */; suffix = "** was kicked (cheating detected)"_s;
				break;
			case Reason::Idle:
				type = WebhookEventType::PlayerKicked; color = ColorNeutral;
				prefix = "\xF0\x9F\x92\xA4" "\xE2\x80\x83" "**"_s /* 💤 */; suffix = "** was kicked (inactivity)"_s;
				break;
			default:
				type = WebhookEventType::PlayerDisconnected; color = ColorNeutral;
				prefix = "\xF0\x9F\x93\xA4" "\xE2\x80\x83" "**"_s /* 📤 */; suffix = "** left the server"_s;
				break;
		}

		if (!IsEventEnabled(type)) {
			return;
		}

		SmallVector<char, 256> description;
		AppendRaw(description, prefix);
		AppendSanitizedName(description, playerName);
		AppendRaw(description, suffix);

		char players[24];
		std::size_t playersLength = formatInto(players, " ({}/{})", playerCount, maxPlayerCount);
		AppendRaw(description, { players, playersLength });

		Enqueue(CreateEmbed({ description.data(), description.size() }, color));
	}

	void WebhookClient::OnLevelChanged(StringView levelDisplayName, MpGameMode gameMode, bool reforgedGameplay, std::int32_t playlistIndex, std::int32_t playlistSize)
	{
		if (!IsEventEnabled(WebhookEventType::LevelChanged)) {
			return;
		}

		SmallVector<char, 256> description;
		AppendRaw(description, "\xF0\x9F\x97\xBA\xEF\xB8\x8F" "\xE2\x80\x83" "Now playing: **"_s /* 🗺️ + em space */);
		AppendSanitizedName(description, levelDisplayName);
		AppendRaw(description, "**"_s);

		char round[32];
		std::size_t roundLength = 0;
		if (playlistSize > 0 && playlistIndex >= 0) {
			roundLength = formatInto(round, "{} of {}", playlistIndex + 1, playlistSize);
		}

		SmallVector<EmbedField, 3> fields;
		fields.push_back({ "Game Mode"_s, NetworkManager::GameModeToString(gameMode) });
		fields.push_back({ "Gameplay"_s, reforgedGameplay ? "Reforged"_s : "Legacy"_s });
		if (roundLength > 0) {
			fields.push_back({ "Playlist"_s, { round, roundLength } });
		}

		Enqueue(CreateEmbed({ description.data(), description.size() }, ColorInfo, { fields.data(), fields.size() }));
	}

	void WebhookClient::OnRoundStarted(StringView levelDisplayName, MpGameMode gameMode)
	{
		if (!IsEventEnabled(WebhookEventType::RoundStarted)) {
			return;
		}

		SmallVector<char, 256> description;
		AppendRaw(description, "\xF0\x9F\x8F\x81" "\xE2\x80\x83" "Round started on **"_s /* 🏁 + em space */);
		AppendSanitizedName(description, levelDisplayName);
		AppendRaw(description, "** ("_s);
		AppendRaw(description, NetworkManager::GameModeToString(gameMode));
		AppendRaw(description, ")"_s);

		Enqueue(CreateEmbed({ description.data(), description.size() }, ColorInfo));
	}

	void WebhookClient::OnRoundEnded(StringView winnerName, StringView levelDisplayName, MpGameMode gameMode)
	{
		if (!IsEventEnabled(WebhookEventType::RoundEnded)) {
			return;
		}

		SmallVector<char, 256> description;
		AppendRaw(description, "\xF0\x9F\x8F\x86" "\xE2\x80\x83"_s /* 🏆 + em space */);
		if (!winnerName.empty()) {
			AppendRaw(description, "**"_s);
			AppendSanitizedName(description, winnerName);
			AppendRaw(description, "** won the round!"_s);
		} else {
			AppendRaw(description, "The round ended in a draw"_s);
		}

		SmallVector<char, 128> level;
		AppendSanitizedName(level, levelDisplayName);

		EmbedField fields[] = {
			{ "Level"_s, { level.data(), level.size() } },
			{ "Game Mode"_s, NetworkManager::GameModeToString(gameMode) }
		};
		Enqueue(CreateEmbed({ description.data(), description.size() }, ColorHighlight, fields));
	}

	void WebhookClient::OnRoundEndedWithTeamWinner(StringView teamName, StringView levelDisplayName, MpGameMode gameMode)
	{
		if (!IsEventEnabled(WebhookEventType::RoundEnded)) {
			return;
		}

		SmallVector<char, 256> description;
		AppendRaw(description, "\xF0\x9F\x8F\x86" "\xE2\x80\x83" "**"_s /* 🏆 + em space */);
		AppendSanitizedName(description, teamName);
		AppendRaw(description, " team** won the round!"_s);

		SmallVector<char, 128> level;
		AppendSanitizedName(level, levelDisplayName);

		EmbedField fields[] = {
			{ "Level"_s, { level.data(), level.size() } },
			{ "Game Mode"_s, NetworkManager::GameModeToString(gameMode) }
		};
		Enqueue(CreateEmbed({ description.data(), description.size() }, ColorHighlight, fields));
	}

	void WebhookClient::OnChampionshipEnded(StringView championName, std::uint32_t points)
	{
		if (!IsEventEnabled(WebhookEventType::ChampionshipEnded)) {
			return;
		}

		SmallVector<char, 256> description;
		AppendRaw(description, "\xF0\x9F\x91\x91" "\xE2\x80\x83" "**"_s /* 👑 + em space */);
		AppendSanitizedName(description, championName);
		AppendRaw(description, "** won the championship with **"_s);

		char pointsBuffer[16];
		std::size_t pointsLength = formatInto(pointsBuffer, "{}", points);
		AppendRaw(description, { pointsBuffer, pointsLength });
		AppendRaw(description, "** points!"_s);

		Enqueue(CreateEmbed({ description.data(), description.size() }, ColorChampionship));
	}

	void WebhookClient::OnChatMessage(StringView playerName, StringView message, bool isAdmin)
	{
		if (!IsEventEnabled(WebhookEventType::ChatMessages)) {
			return;
		}

		String stripped = UI::Font::StripFormatting(message);
		StringView trimmed = stripped.trimmed();
		StringView text = TruncateUtf8(trimmed, MaxChatMessageLength);
		if (text.empty()) {
			return;
		}

		SmallVector<char, 512> description;
		AppendRaw(description, isAdmin
			? "\xF0\x9F\x92\xAC" "\xE2\x80\x83" "\xF0\x9F\x9B\xA1\xEF\xB8\x8F" "\xE2\x80\x83" "**"_s /* 💬 🛡️ */
			: "\xF0\x9F\x92\xAC" "\xE2\x80\x83" "**"_s /* 💬 + em space */);
		AppendSanitizedName(description, playerName);
		AppendRaw(description, ":** "_s);
		AppendRaw(description, text);
		if (text.size() < trimmed.size()) {
			AppendRaw(description, "\xE2\x80\xA6"_s /* … */);
		}

		Enqueue(CreateEmbed({ description.data(), description.size() }, ColorNeutral));
	}

	void WebhookClient::Enqueue(String&& embed)
	{
		_queueMutex.Lock();
		if (_shuttingDown) {
			_queueMutex.Unlock();
			return;
		}
		if (_queue.size() >= MaxQueueLength) {
			LOGW("Webhook event queue is full, dropping the oldest event");
			_queue.erase(_queue.begin());
		}
		_queue.push_back(std::move(embed));
		_queueMutex.Unlock();
		_queueRefreshed.Signal();
	}

	String WebhookClient::CreateEmbed(StringView description, std::uint32_t color, ArrayView<const EmbedField> fields)
	{
		String serverIdentity;
		_queueMutex.Lock();
		serverIdentity = _serverIdentity;
		_queueMutex.Unlock();

		SmallVector<char, 512> out;
		AppendRaw(out, "{"_s);
		if (!description.empty()) {
			AppendRaw(out, "\"description\":\""_s);
			AppendJsonEscaped(out, description);
			AppendRaw(out, "\","_s);
		}
		if (!fields.empty()) {
			AppendRaw(out, "\"fields\":["_s);
			for (std::size_t i = 0; i < fields.size(); i++) {
				if (i > 0) {
					out.push_back(',');
				}
				AppendRaw(out, "{\"name\":\""_s);
				AppendJsonEscaped(out, fields[i].Name);
				AppendRaw(out, "\",\"value\":\""_s);
				AppendJsonEscaped(out, fields[i].Value);
				AppendRaw(out, "\",\"inline\":true}"_s);
			}
			AppendRaw(out, "],"_s);
		}
		if (!serverIdentity.empty()) {
			AppendRaw(out, "\"footer\":{\"text\":\""_s);
			AppendJsonEscaped(out, serverIdentity);
			AppendRaw(out, "\"},"_s);
		}

		auto now = DateTime::UtcNow().Partitioned(DateTime::UTC);
		char buffer[64];
		std::size_t length = formatInto(buffer, "\"timestamp\":\"{:.4}-{:.2}-{:.2}T{:.2}:{:.2}:{:.2}Z\",\"color\":{}}}",
			now.Year, now.Month + 1, now.Day, now.Hour, now.Minute, now.Second, color);
		AppendRaw(out, { buffer, length });

		return String(out.data(), out.size());
	}

	void WebhookClient::SendBatch(ArrayView<const String> embeds)
	{
		String url, serverIdentity;
		_queueMutex.Lock();
		url = _url;
		serverIdentity = _serverIdentity;
		_queueMutex.Unlock();

		if (url.empty()) {
			return;
		}

		SmallVector<char, 0> payload;
		AppendRaw(payload, "{\"username\":\""_s);
		AppendJsonEscaped(payload, serverIdentity);
		// Player-controlled content must never ping anyone in the channel
		AppendRaw(payload, "\",\"allowed_mentions\":{\"parse\":[]},\"embeds\":["_s);
		for (std::size_t i = 0; i < embeds.size(); i++) {
			if (i > 0) {
				payload.push_back(',');
			}
			AppendRaw(payload, embeds[i]);
		}
		AppendRaw(payload, "]}"_s);

		StringView payloadView(payload.data(), payload.size());
		std::int32_t maxAttempts = (_shuttingDown ? 1 : MaxSendAttempts);

		for (std::int32_t attempt = 0; attempt < maxAttempts; attempt++) {
			auto request = WebSession::GetDefault().CreateRequest(url);
			request.SetMethod("POST"_s);
			request.SetData(payloadView, "application/json"_s);

			auto result = request.Execute();
			auto response = request.GetResponse();
			std::int32_t status = (response ? response.GetStatus() : 0);

			if (status == 429) {
				// Rate limited - Discord tells how long to back off
				float retryAfterSecs = 2.0f;
				auto retryAfter = response.GetHeader("Retry-After"_s);
				if (!retryAfter.empty()) {
					retryAfterSecs = strtof(retryAfter.data(), nullptr);
				}
				if (retryAfterSecs < 1.0f) retryAfterSecs = 1.0f;
				if (retryAfterSecs > 30.0f) retryAfterSecs = 30.0f;
				SleepInterruptible((std::uint32_t)(retryAfterSecs * 1000.0f));
				continue;
			}
			if (status >= 200 && status < 300) {
				return;
			}
			if (status >= 400) {
				// The endpoint rejected the payload (or the webhook was deleted), retrying won't help
				LOGW("Failed to deliver {} webhook event(s): Request rejected (HTTP {})", embeds.size(), status);
				return;
			}

			LOGW("Failed to deliver {} webhook event(s): {}", embeds.size(), result.error);
			SleepInterruptible(RetryDelayMs);
		}

		LOGW("Dropped {} webhook event(s) after {} failed attempt(s)", embeds.size(), maxAttempts);
	}

	void WebhookClient::SleepInterruptible(std::uint32_t milliseconds)
	{
		while (milliseconds > 0 && !_shuttingDown) {
			std::uint32_t step = (milliseconds < 100 ? milliseconds : 100);
			Thread::Sleep(step);
			milliseconds -= step;
		}
	}

	void WebhookClient::OnDeliveryThread(void* param)
	{
		WebhookClient* _this = static_cast<WebhookClient*>(param);

		while (true) {
			SmallVector<String, MaxEmbedsPerMessage> batch;
			bool shuttingDown;

			_this->_queueMutex.Lock();
			while (_this->_queue.empty() && !_this->_shuttingDown) {
				_this->_queueRefreshed.Wait(_this->_queueMutex);
			}
			shuttingDown = _this->_shuttingDown;
			std::size_t count = _this->_queue.size();
			if (count > MaxEmbedsPerMessage) {
				count = MaxEmbedsPerMessage;
			}
			for (std::size_t i = 0; i < count; i++) {
				batch.push_back(std::move(_this->_queue[i]));
			}
			_this->_queue.erase(_this->_queue.begin(), _this->_queue.begin() + count);
			bool drained = _this->_queue.empty();
			_this->_queueMutex.Unlock();

			if (!batch.empty()) {
				_this->SendBatch({ batch.data(), batch.size() });
			}

			if (shuttingDown) {
				if (drained) {
					break;
				}
			} else {
				_this->SleepInterruptible(RequestIntervalMs);
			}
		}
	}
}

#endif
