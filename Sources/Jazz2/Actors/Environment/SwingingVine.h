#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
		@brief Swinging vine
		
		Vine that swings back and forth, which the player can grab onto and ride. While holding the
		vine the rabbit swings along with it and can let go to be flung across gaps that are too wide
		to jump.
	*/
	class SwingingVine : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		/** @brief Creates a new instance */
		SwingingVine();
		~SwingingVine();

		bool OnHandleCollision(ActorBase* other) override;

		/** @brief Length of a vine in JJ2, which has no parameter for it */
		static constexpr std::int32_t DefaultLength = 128;

		/**
		 * @brief Returns the swing as `BEHAVIOR::SWINGINGVINE` of JJ2 has it
		 *
		 * @p length is `var[1]`, @p position is `var[2]` (from `length * 256` on the right to the negative of it on the
		 * left), @p speed is `var[3]`.
		 */
		void GetSwing(std::int32_t& length, std::int32_t& position, std::int32_t& speed) const;
		/** @brief Sets the swing, see @ref GetSwing(), it goes on from there */
		void SetSwing(std::int32_t length, std::int32_t position, std::int32_t speed);
		/** @brief Returns the horizontal speed a player letting go of the vine is flung with */
		float GetReleaseSpeedX() const;

		/** @brief Advances the swing by one tick of JJ2 */
		static void AdvanceSwing(std::int32_t& position, std::int32_t& speed);
		/** @brief Returns where the end of the vine is in JJ2 (`xPos` and `yPos` of the object), from its origin */
		static Vector2f GetLegacyTipOffset(std::int32_t length, std::int32_t position);

		/** @brief Preloads all assets required by this actor */
		static void Preload(const ActorActivationDetails& details);

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		static constexpr std::int32_t ChunkCount = 16;
		static constexpr float ChunkSize = 136.0f / ChunkCount;
		/** @brief How much of the swing's phase one chunk is behind the one before it */
		static constexpr float ChunkPhaseStep = 0.64f / ChunkCount;
		/** @brief Horizontal movement in a frame below which the rider's facing is left alone, so the apex of a swing does not flip it on rounding noise */
		static constexpr float TurnDeadzone = 0.01f;

		// The swing of JJ2, see GetSwing()
		std::int32_t _length;
		std::int32_t _position;
		std::int32_t _speed;
		// Ticks of JJ2 since the start of the level the swing is at
		std::int32_t _ticks;
		// The widest angle of the end of the vine, for the length
		float _amplitude;
		Vector2f _chunkPos[ChunkCount];
		std::unique_ptr<RenderCommand> _chunks[ChunkCount];

		float GetTickTime() const;
		std::int32_t GetDirection() const;
		float GetLengthScale() const;
		void UpdateAmplitude();
		float GetCurrentPhase() const;
		void UpdateChunks();
	};
}