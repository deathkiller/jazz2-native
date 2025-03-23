#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Actors/ActorBase.h"
#include "../Actors/Collectibles/CollectibleBase.h"

class asIScriptEngine;
class asIScriptModule;
class asIScriptObject;
class asIScriptFunction;
class asILockableSharedBool;

namespace Jazz2::Actors
{
	class Player;
}

namespace Jazz2::Scripting
{
	class LevelScriptLoader;

	class ScriptActorWrapper : public Actors::ActorBase
	{
	public:
		ScriptActorWrapper(LevelScriptLoader* levelScripts, asIScriptObject* obj);
		~ScriptActorWrapper();

		static void RegisterFactory(asIScriptEngine* engine, asIScriptModule* module);
		static ScriptActorWrapper* Factory(int actorType);

		void AddRef();
		void Release();

		// Assignment operator
		ScriptActorWrapper& operator=(const ScriptActorWrapper& o) {
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		LevelScriptLoader* _levelScripts;
		asIScriptObject* _obj;
		asILockableSharedBool* _isDead;

		std::uint32_t _scoreValue;
#endif

		Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override;
		bool OnTileDeactivated() override;

		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;

		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;
		void OnHitFloor(float timeMult) override;
		void OnHitCeiling(float timeMult) override;
		void OnHitWall(float timeMult) override;

		//void OnTriggeredEvent(EventType eventType, uint8_t* eventParams) override;

		void OnAnimationStarted() override;
		void OnAnimationFinished() override;

		float asGetAlpha() const;
		void asSetAlpha(float value);
		uint16_t asGetLayer() const;
		void asSetLayer(uint16_t value);

		void asDecreaseHealth(int amount);
		bool asMoveTo(float x, float y, bool force);
		bool asMoveBy(float x, float y, bool force);
		void asTryStandardMovement(float timeMult);
		void asRequestMetadata(const String& path);
		void asPlaySfx(const String& identifier, float gain, float pitch);
		void asSetAnimation(const String& name);
		void asSetAnimationState(int state);

	private:
		std::int32_t _refCount;

		asIScriptFunction* _onTileDeactivated;
		asIScriptFunction* _onHealthChanged;
		asIScriptFunction* _onUpdate;
		asIScriptFunction* _onUpdateHitbox;
		asIScriptFunction* _onHandleCollision;
		asIScriptFunction* _onHitFloor;
		asIScriptFunction* _onHitCeiling;
		asIScriptFunction* _onHitWall;
		asIScriptFunction* _onAnimationStarted;
		asIScriptFunction* _onAnimationFinished;
	};

	class ScriptCollectibleWrapper : public ScriptActorWrapper
	{
	public:
		ScriptCollectibleWrapper(LevelScriptLoader* levelScripts, asIScriptObject* obj);

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;

	protected:
		Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override;

		bool OnCollect(Actors::Player* player);

	private:
		bool _untouched;
		float _phase, _timeLeft;
		float _startingY;

		asIScriptFunction* _onCollect;
	};
}

#endif