#pragma once

#include "../ActorBase.h"

namespace Jazz2::Actors::Environment
{
	class SwingingVine : public ActorBase
	{
		DEATH_RUNTIME_OBJECT(ActorBase);

	public:
		SwingingVine();
		~SwingingVine();

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

		static void Preload(const ActorActivationDetails& details)
		{
			PreloadMetadataAsync("Object/SwingingVine"_s);
		}

	protected:
		Task<bool> OnActivatedAsync(const ActorActivationDetails& details) override;
		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;

	private:
		static constexpr int ChunkCount = 16;
		static constexpr float ChunkSize = 136.0f / ChunkCount;

		float _angle;
		float _phase;
		bool _justTurned;
		Vector2f _chunkPos[ChunkCount];
		std::unique_ptr<RenderCommand> _chunks[ChunkCount];
	};
}