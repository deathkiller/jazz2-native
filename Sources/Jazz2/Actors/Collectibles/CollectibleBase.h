#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Collectibles
{
	/**
		@brief Base class of a collectible object
		
		Base for pickups that grant the player a bonus when collected on contact. It provides the behavior shared by
		all collectibles: the floating/bobbing animation, optional light emission when illuminated, and dispatching
		the collection logic to the deriving type.
	*/
	class CollectibleBase : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/**
			@brief Swarm of lights orbiting an illuminated collectible

			The effect is decoration, seeded at random and animated on its own, which is why it's reusable: an
			illuminated collectible in an online session is drawn by a stand-in that runs none of the object's
			logic, and reproducing the swarm there costs nothing --- whereas describing two dozen constantly
			moving lights to every client on every update would be by far the most expensive object in the level.
		*/
		class IlluminateLights
		{
		public:
			/** @brief Seeds the swarm */
			void Create();
			/** @brief Returns `true` if the swarm hasn't been seeded yet */
			bool IsEmpty() const {
				return _lights.empty();
			}
			/** @brief Advances the swarm */
			void OnUpdate(float timeMult);
			/** @brief Emits the swarm around the specified position */
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights, Vector2f pos) const;

		private:
			// Number of lights the swarm consists of
			static constexpr std::int32_t LightCount = 20;

			struct Light {
				float Intensity;
				float Distance;
				float Phase;
				float Speed;
			};

			SmallVector<Light, 0> _lights;
		};

		/** @brief Creates a new instance */
		CollectibleBase();

		bool OnHandleCollision(ActorBase* other) override;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		bool _untouched;
		std::int32_t _scoreValue;
		float _timeLeft;
#endif

		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;
		// Every observer reproduces the swarm from the remoted ActorState::Illuminated instead
		void OnEmitRemotedLights(SmallVectorImpl<LightEmitter>& lights) override { }
		bool IsIlluminatedStateRemoted() const override {
			return true;
		}

		/** @brief Called when the collectible is collected */
		virtual void OnCollect(Player* player);

		/** @brief Sets facing direction */
		void SetFacingDirection(bool inverse = false);

	private:
		float _phase;
		float _startingY;
		IlluminateLights _illuminateLights;
	};
}