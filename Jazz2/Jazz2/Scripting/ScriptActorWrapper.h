#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "../Actors/ActorBase.h"

class asIScriptEngine;
class asIScriptModule;
class asIScriptObject;
class asILockableSharedBool;

namespace Jazz2::Scripting
{
	class LevelScripts;

	class ScriptActorWrapper : public Actors::ActorBase
	{
	public:
		ScriptActorWrapper(LevelScripts* levelScripts, asIScriptObject* obj);
		~ScriptActorWrapper();

		static void RegisterFactory(asIScriptEngine* engine, asIScriptModule* module);
		static ScriptActorWrapper* Factory();

		void AddRef();
		void Release();

		// Assignment operator
		ScriptActorWrapper& operator=(const ScriptActorWrapper& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		bool OnHandleCollision(std::shared_ptr<ActorBase> other) override;
		void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override;

	protected:
		Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override;
		bool OnTileDeactivate(int tx1, int ty1, int tx2, int ty2) override;

		void OnHealthChanged(ActorBase* collider) override;
		bool OnPerish(ActorBase* collider) override;

		void OnUpdate(float timeMult) override;
		void OnUpdateHitbox() override;
		bool OnDraw(RenderQueue& renderQueue) override;
		void OnHitFloor(float timeMult) override;
		void OnHitCeiling(float timeMult) override;
		void OnHitWall(float timeMult) override;

		//void OnTriggeredEvent(EventType eventType, uint16_t* eventParams) override;

		//void OnAnimationStarted() override;
		//void OnAnimationFinished() override;

	private:
		LevelScripts* _levelScripts;
		asILockableSharedBool* _isDead;
		asIScriptObject* _obj;
		int _refCount;

		bool asMoveTo(float x, float y, bool force);
		bool asMoveBy(float x, float y, bool force);
		void asRequestMetadata(String& path);
		void asSetAnimation(String& name);
		void asSetAnimationState(int state);
	};
}

#endif