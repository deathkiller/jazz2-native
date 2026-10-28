#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief Spring
		
		Bouncy spring that launches the player when stepped on or run into. Vertical springs come in
		three strengths---red, green, and blue---that fling the rabbit progressively higher, while
		horizontal springs shove the player sideways; a frozen spring must be thawed before it works.
	*/
	class Spring : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		Spring();

		/** @brief Whether player horizontal speed should be kept */
		bool KeepSpeedX;
		/** @brief Whether player vertical speed should be kept */
		bool KeepSpeedY;

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

		/** @brief Activates the spring */
		Vector2f Activate();

		/** @brief Returns the launch speed of its color as the original has it, in pixels per tick at 70 Hz, the way it faces */
		Vector2f GetLegacyLaunchSpeed() const;
		/** @brief Launches by a speed as the original has it, which also decides the way it faces */
		void SetLegacyLaunchSpeed(Vector2f speed);
		/** @brief Thaws a frozen spring */
		void Thaw();

		/** @brief Returns the strength a spring launches by for a speed of the original (pixels per tick at 70 Hz) */
		static float GetStrengthOfLegacySpeed(ILevelHandler* levelHandler, float speed, bool horizontal);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		void OnSerializeState(Stream& dest) override;
		void OnDeserializeState(Stream& src) override;

	private:
		enum class State {
			Default,
			Frozen,
			Heated
		};

		enum class Orientation : uint8_t {
			Bottom,
			Right,
			Top,
			Left
		};

		uint8_t _type;
		Orientation _orientation;
		uint8_t _delay;
		State _state;
		float _strength;
		float _cooldown;

		void ApplyOrientation();
		void UpdateStrength();
	};
}