#pragma once

#include "../EnemyBase.h"

namespace Jazz2::Actors::Bosses
{
	class BossBase : public Enemies::EnemyBase
	{
		DEATH_RUNTIME_OBJECT(Enemies::EnemyBase);

	public:
		virtual bool OnActivatedBoss() = 0;
		virtual void OnDeactivatedBoss() { };

		virtual bool OnPlayerDied();

	protected:
		bool OnTileDeactivated() override;
	};
}