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

	/**
		@brief Wraps a scripted actor, forwarding engine callbacks to the **AngelScript** object
		
		Engine-side @ref Actors::ActorBase subclass that backs a custom actor type implemented in script. Engine
		lifecycle and event callbacks (activation, update, draw, collision, hit, animation) are dispatched to the
		matching methods on the backing script object, and a set of helper methods expose the actor's state back to
		script. Instances are reference-counted and created through the registered AngelScript factory.
		
		@experimental
	*/
	class ScriptActorWrapper : public Actors::ActorBase
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param levelScripts  Owning level script loader
		 * @param obj           Backing **AngelScript** object
		 */
		ScriptActorWrapper(LevelScriptLoader* levelScripts, asIScriptObject* obj);
		~ScriptActorWrapper();

		/** @brief Registers the actor factory to the specified **AngelScript** engine and module */
		static void RegisterFactory(asIScriptEngine* engine, asIScriptModule* module);
		/** @brief Creates a new wrapper instance for the specified actor type */
		static ScriptActorWrapper* Factory(std::int32_t actorType);

		/** @brief Increases the reference count */
		void AddRef();
		/** @brief Decreases the reference count, destroying the instance when it reaches zero */
		void Release();

		// Assignment operator
		ScriptActorWrapper& operator=(const ScriptActorWrapper& o) {
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		bool OnHandleCollision(ActorBase* other) override;

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

		/** @brief Returns the alpha (opacity) of the actor */
		float asGetAlpha() const;
		/** @brief Sets the alpha (opacity) of the actor */
		void asSetAlpha(float value);
		/** @brief Returns the rendering layer of the actor */
		uint16_t asGetLayer() const;
		/** @brief Sets the rendering layer of the actor */
		void asSetLayer(std::uint16_t value);

		/** @brief Decreases the health of the actor by the specified amount */
		void asDecreaseHealth(std::int32_t amount);
		/** @brief Moves the actor to the specified position, returns `true` on success */
		bool asMoveTo(float x, float y, bool force);
		/** @brief Moves the actor by the specified offset, returns `true` on success */
		bool asMoveBy(float x, float y, bool force);
		/** @brief Applies standard movement integration for the current frame */
		void asTryStandardMovement(float timeMult);
		/** @brief Requests metadata to be loaded from the specified path */
		void asRequestMetadata(const String& path);
		/** @brief Plays a sound effect by its identifier */
		void asPlaySfx(const String& identifier, float gain, float pitch);
		/** @brief Sets the current animation state */
		void asSetAnimationState(std::int32_t state);

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

	/**
		@brief Wraps a scripted collectible actor, forwarding engine callbacks to the **AngelScript** object
		
		Specialization of @ref ScriptActorWrapper for collectible items, adding the bobbing/floating behavior and an
		`OnCollect` callback dispatched to the backing script object when a player picks the item up.
		
		@experimental
	*/
	class ScriptCollectibleWrapper : public ScriptActorWrapper
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param levelScripts  Owning level script loader
		 * @param obj           Backing **AngelScript** object
		 */
		ScriptCollectibleWrapper(LevelScriptLoader* levelScripts, asIScriptObject* obj);

		bool OnHandleCollision(ActorBase* other) override;

	protected:
		Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override;

		/** @brief Called when the collectible is collected by the specified player */
		bool OnCollect(Actors::Player* player);

	private:
		bool _untouched;
		float _phase, _timeLeft;
		float _startingY;

		asIScriptFunction* _onCollect;
	};
}

#endif