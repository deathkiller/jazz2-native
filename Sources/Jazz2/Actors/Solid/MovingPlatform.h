#pragma once

#include "../SolidObjectBase.h"

#include <cmath>

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Actors::Solid
{
	/**
		@brief Moving platform
		
		A solid platform suspended from a chain that swings or circles along a fixed path; the player can stand
		on it and is carried along as it moves. Comes in several themed variants (Carrotus fruit, ball, lab,
		etc.), including spiked variants that damage the player instead of carrying them.
	*/
	class MovingPlatform : public SolidObjectBase
	{
		DEATH_RUNTIME_OBJECT(SolidObjectBase);

	public:
		/** @brief Creates a new instance */
		MovingPlatform();
		~MovingPlatform();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Returns the point the platform hangs from */
		Vector2f GetOriginPos() const {
			return _originPos;
		}
		/** @brief Moves the point the platform hangs from */
		void SetOriginPos(Vector2f pos) {
			_originPos = pos;
		}
		/** @brief Returns the speed in the units of the parameter of the event */
		std::int32_t GetEventSpeed() const {
			return std::int32_t(std::round(_speed / SpeedUnit));
		}
		/** @brief Sets the speed in the units of the parameter of the event, negative swings the other way */
		void SetEventSpeed(std::int32_t value) {
			_speed = float(value) * SpeedUnit;
		}
		/** @brief Draws only the chain, something else draws the platform itself */
		void SetBodyHidden(bool value) {
			_bodyHidden = value;
		}

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;
		bool OnPerish(ActorBase* collider) override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		enum class PlatformType {
			CarrotusFruit = 1,
			Ball = 2,
			CarrotusGrass = 3,
			Lab = 4,
			Sonic = 5,
			Spike = 6,

			SpikeBall = 7,
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ChainPiece {
			Vector2f Pos;
			std::unique_ptr<RenderCommand> Command;
		};
#endif

		// Radians a frame for a unit of the speed parameter of the event
		static constexpr float SpeedUnit = 0.0072f;
		// How far above the platform its top is, where the feet of a player stand (see OnUpdateHitbox())
		static constexpr float StandingTop = 5.0f;

		PlatformType _type;
		float _speed;
		bool _isSwing;
		bool _bodyHidden = false;
		float _phase;
		Vector2f _originPos, _lastPos;

		SmallVector<ChainPiece, 0> _pieces;

		Vector2f GetPhasePosition(std::int32_t distance);
		// Puts a player standing on the platform to a position if no tile is in the way, returns `false` otherwise
		bool TryCarryPlayer(Player* player, Vector2f pos);
	};
}