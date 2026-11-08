#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../ActorBase.h"

namespace Jazz2::Actors::Multiplayer
{
	/**
		@brief Object of a game mode in the level

		Purely visual actor that shows a @ref Jazz2::Multiplayer::GameModes::GameModeObject --- a control point in
		Domination, a jail in Jailbreak or the flag in Flag Run. Both the server and clients create it locally and
		drive it from the synchronized state, it's never remoted. All the logic lives in the game mode.
	*/
	class GameModeObjectActor : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Kind of the visual */
		enum class Visual : std::uint8_t {
			ControlPoint,	/**< Giant gem in the color of the owning team, translucent while nobody owns it */
			Jail,			/**< Base in the color of the team */
			Flag			/**< Flag in the color of the carrier's team */
		};

		GameModeObjectActor();

		/** @brief Changes what the actor shows, `team` is `0xFF` for no team */
		void SetVisual(Visual visual, std::uint8_t team, float progress, bool facingLeft = false);

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;

	private:
		float _progress;
		float _phase;
		Visual _visual;
		std::uint8_t _team;
		bool _isLoaded;
		bool _isDirty;

		void ApplyVisual();
	};
}

#endif
