#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Solid
{
	/**
		@brief Pole
		
		An upright tree trunk or vine (Diamondus, Carrotus, Jungle, etc.) that the player can shoot or blow up
		with TNT to make it topple over, where it falls in the hit direction and bounces a few times before
		coming to rest, often forming a bridge or clearing a path.
	*/
	class Pole : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Direction the pole is falling in */
		enum class FallDirection {
			None,		/**< Standing upright */
			Right,		/**< Falling to the right */
			Left,		/**< Falling to the left */
			Fallen		/**< Finished falling */
		};

		/** @brief Creates a new instance */
		Pole();

		bool OnHandleCollision(ActorBase* other) override;
		bool CanCauseDamage(ActorBase* collider) override;

		/** @brief Returns the direction the pole is falling in */
		FallDirection GetFallDirection() const {
			return _fall;
		}

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnPacketReceived(MemoryStream& packet) override;

	private:
		static constexpr std::int32_t BouncesMax = 3;

		FallDirection _fall;
		float _angleVel;
		float _angleVelLast;
		float _fallTime;
		std::int32_t _bouncesLeft = BouncesMax;

		void Fall(FallDirection dir);
		bool IsPositionBlocked();
	};
}