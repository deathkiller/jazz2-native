#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	/**
	 * @brief Swinging vine
	 *
	 * Vine that swings back and forth, which the player can grab onto and ride. While holding the
	 * vine the rabbit swings along with it and can let go to be flung across gaps that are too wide
	 * to jump.
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

		float _angle;
		float _phase;
		bool _justTurned;
		Vector2f _chunkPos[ChunkCount];
		std::unique_ptr<RenderCommand> _chunks[ChunkCount];
	};
}