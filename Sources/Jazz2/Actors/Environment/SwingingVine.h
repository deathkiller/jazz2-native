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
		/** @brief Transparent margin around an imported frame (mirrors `JJ2Anims::AddBorder`), excluded so the artwork tiles seamlessly */
		static constexpr float SpriteBorder = 2.0f;

		float _angle;
		float _phase;
		Vector2f _chunkPos[ChunkCount];
		std::unique_ptr<RenderCommand> _chunks[ChunkCount];
	};
}