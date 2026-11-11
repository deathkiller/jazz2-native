#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#include <type_traits>
#include <utility>

#ifndef DOXYGEN_GENERATING_OUTPUT
using namespace Death::Containers;
#endif

namespace Jazz2::Multiplayer
{
	/**
		@brief Message sent by the server, which the client formats in its own language

		The server sends only the type and the arguments, so every player sees the text in the language of their
		own client, not of the server. Never renumber the existing values, append new ones before @ref Count.
	*/
	enum class ServerMessageType : std::uint16_t
	{
		Unknown,						/**< Unknown */

		Text,							/**< Text shown as it is (e.g., from the server console or an admin command) */

		GameWillBeginShortly,			/**< Pre-game phase of a round started */
		ChampionshipWon,				/**< Player name */
		FirstToReachPointsWins,			/**< Score that wins the overtime */
		TeamsRebalanced,				/**< Players were moved between teams */
		CtfFlagTaken,					/**< Player name, team of the flag */
		CtfFlagReturned,				/**< Team of the flag */
		CtfFlagCaptured,				/**< Team of the carrier, team of the flag */
		CtfFlagDropped,					/**< Team of the flag */
		RaceFinishedFirst,				/**< Player name, seconds left for the others */
		RaceFinished,					/**< Player name, finishing position */
		WinnerIs,						/**< Player name */
		TeamWins,						/**< Team */
		Overtime,						/**< Overtime started */
		RoundEndedInDraw,				/**< Round ended without a winner */

		ControlPointCaptured,			/**< Team, name of the control point */
		ControlPointCapturedUnnamed,	/**< Team, number of the control point */
		FlagRunTookFlag,				/**< Player name */
		FlagRunOutOfTime,				/**< Player name */
		FlagRunPassedFlag,				/**< Name of the carrier, name of the teammate */
		FlagRunScored,					/**< Player name, points */
		FlagRunStoleFlag,				/**< Player name */
		HeadHuntersDelivered,			/**< Player name, gems, points */
		PlayerIsOut,					/**< Player name */
		PlayerPosition,					/**< Player name, tile X, tile Y */
		PestilenceRandomZombie,			/**< No zombie roasted anybody for too long */
		PestilenceLastSurvivor,			/**< Player name */
		PestilenceBecameZombie,			/**< Player name */
		JailbreakPrisonersReleased,		/**< Jails opened */
		JailbreakTeamScored,			/**< Team */
		RoastTagLostRingIdle,			/**< Player name */
		RoastTagHasRing,				/**< Player name */

		IdleSpectate,					/**< The player was moved to spectate mode for inactivity (alert) */
		IdleSpectateHelp,				/**< The player was moved to spectate mode for inactivity (console) */
		RaceAlreadyFinished,			/**< The player can't rejoin the race */
		CannotJoinNow,					/**< The player can't join the game right now */
		CheatsNotAllowed,				/**< Cheats are not allowed */
		TeamJoined,						/**< Team */
		CannotChangeTeam,				/**< The player can't change team right now */
		TeamUsage,						/**< Help for the /team command */
		TeamsOnlyInTeamModes,			/**< Teams are not used in the current game mode */
		TeamsRebalancedReply,			/**< Reply to the /balance command */
		SetTeamUsage,					/**< Help for the /setteam command */
		PlayerMovedToTeam,				/**< Player number, team */
		PlayerNotFound,					/**< No player with the specified number */
		TeamCountOutOfRange,			/**< Maximum number of teams */

		PollStartedRestart,				/**< Poll to restart the playlist started */
		PollStartedResetPoints,			/**< Poll to reset the points started */
		PollStartedSkip,				/**< Poll to skip the current level started */
		PollAlreadyActive,				/**< Seconds left of the active poll */
		PollUnsupportedType,			/**< Requested poll type */
		PollCancelled,					/**< Active poll was cancelled */
		PollVoted,						/**< Vote was counted */
		PollAlreadyVoted,				/**< The player already voted */
		PollNotActive,					/**< No poll is active */
		PollAccepted,					/**< Poll passed */
		PollNotEnoughVotes,				/**< Poll didn't pass */

		Count							/**< Count of supported message types */
	};

	/** @brief Where a @ref ServerMessage is shown */
	enum class ServerMessageDisplay : std::uint8_t
	{
		Console,						/**< Line in the console */
		Alert							/**< Big text in the middle of the screen */
	};

	/** @brief Type of @ref ServerMessageArg */
	enum class ServerMessageArgType : std::uint8_t
	{
		Text,							/**< Text shown as it is (e.g., player name) */
		Number,							/**< Integer number */
		Team							/**< Team index, the client shows its name */
	};

	/** @brief Team index passed to @ref ServerMessage */
	struct ServerMessageTeam
	{
		/** @brief Team index */
		std::uint8_t Value;
	};

	/** @brief Argument of @ref ServerMessage */
	struct ServerMessageArg
	{
		/** @brief Type */
		ServerMessageArgType Type;
		/** @brief Number or team index */
		std::int32_t Number;
		/** @brief Text */
		String Text;

		/** @brief Creates an empty text argument */
		ServerMessageArg()
			: Type(ServerMessageArgType::Text), Number(0) {}
		/** @brief Creates a text argument */
		ServerMessageArg(StringView text)
			: Type(ServerMessageArgType::Text), Number(0), Text(text) {}
		/** @overload */
		ServerMessageArg(const String& text)
			: Type(ServerMessageArgType::Text), Number(0), Text(text) {}
		/** @brief Creates a team argument */
		ServerMessageArg(ServerMessageTeam team)
			: Type(ServerMessageArgType::Team), Number(team.Value) {}
		/** @brief Creates a number argument */
		template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
		ServerMessageArg(T value)
			: Type(ServerMessageArgType::Number), Number((std::int32_t)value) {}
	};

	/** @brief Message of the server with its arguments, see @ref ServerMessageType */
	struct ServerMessage
	{
		/** @brief Maximum number of arguments */
		static constexpr std::uint32_t MaxArgs = 8;

		/** @brief Type */
		ServerMessageType Type;
		/** @brief Arguments */
		SmallVector<ServerMessageArg, 3> Args;

		ServerMessage()
			: Type(ServerMessageType::Unknown) {}

		/** @brief Creates a message with the specified arguments */
		template<class ...TArgs>
		explicit ServerMessage(ServerMessageType type, TArgs&&... args)
			: Type(type)
		{
			static_assert(sizeof...(TArgs) <= MaxArgs, "Too many arguments");
			(Args.emplace_back(std::forward<TArgs>(args)), ...);
		}

		/** @brief Returns the text argument, or an empty string if it's not a text */
		StringView GetText(std::size_t index) const {
			return (index < Args.size() && Args[index].Type == ServerMessageArgType::Text ? StringView(Args[index].Text) : StringView{});
		}

		/** @brief Returns the number argument, or @cpp 0 @ce if it's not a number */
		std::int32_t GetNumber(std::size_t index) const {
			return (index < Args.size() && Args[index].Type == ServerMessageArgType::Number ? Args[index].Number : 0);
		}

		/** @brief Returns the team argument, or @cpp UINT8_MAX @ce if it's not a team */
		std::uint8_t GetTeam(std::size_t index) const {
			return (index < Args.size() && Args[index].Type == ServerMessageArgType::Team ? (std::uint8_t)Args[index].Number : UINT8_MAX);
		}
	};
}

#endif
