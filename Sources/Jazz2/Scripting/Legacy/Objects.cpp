#if defined(WITH_ANGELSCRIPT)

#include "Objects.h"
#include "AnimationTables.h"
#include "EventTranslator.h"
#include "ObjectHost.h"
#include "Internal.h"

#include "../../Actors/ActorBase.h"
#include "../../Actors/Player.h"

#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		// Native actor that hosts a script-controlled jjOBJ: each frame it applies the object's own velocity, runs the
		// script behavior (a jjVOIDFUNCOBJ function or a jjBEHAVIORINTERFACE object) over the jjOBJ, then syncs the
		// resulting position back to the actor. This is what makes jjAddObject objects script-controllable.
		// NOTE: in-world sprite drawing isn't implemented yet (the object is logic/position-only for now).
		class ScriptLegacyObject : public Actors::ActorBase
		{
			DEATH_RUNTIME_OBJECT(Actors::ActorBase);

		public:
			ScriptLegacyObject(LevelScriptLoader* levelScripts, jjOBJ* obj)
				: _levelScripts(levelScripts), _obj(obj)
			{
				// Takes ownership of the single reference created by AddScriptControlledObject
				_obj->_wrapper = this;
			}
			~ScriptLegacyObject()
			{
				if (_obj != nullptr) {
					// Mark the script-facing object inactive, then drop the hosted reference
					_obj->_wrapper = nullptr;
					_obj->Release();
				}
			}

			jjOBJ* GetObject() const {
				return _obj;
			}

			// Configures the actor renderer to display an animation of jjAnimations, mirroring ActorBase::RefreshAnimation
			// but from a raw graphic resource. The renderer then auto-advances and draws the animation in-world at the
			// actor's position.
			void ApplyAnimation(std::uint32_t animIndex)
			{
				// Behaviors often call determineCurAnim every frame; only rebuild the renderer when it actually changes
				if (animIndex == _lastAnimation) {
					return;
				}

				std::int32_t cellCount = 0;
				auto* base = _levelScripts->GetAnimationTables().ResolveAnimation(animIndex, cellCount);
				if (base == nullptr || base->TextureDiffuse == nullptr || base->FrameConfiguration.X <= 0 || cellCount <= 0) {
					return;
				}
				_lastAnimation = animIndex;

				// No GL renderer to configure on a dedicated/headless server
				if (ContentResolver::Get().IsHeadless()) {
					return;
				}

				_renderer.FrameConfiguration = base->FrameConfiguration;
				_renderer.FrameSource = base;
				_renderer.FrameDimensions = base->FrameDimensions;
				_renderer.FirstFrame = 0;
				_renderer.FrameCount = cellCount;
				_renderer.AnimDuration = base->AnimDuration;
				_renderer.LoopMode = AnimationLoopMode::Loop;
				_renderer.AnimTime = 0.0f;
				_renderer.Hotspot.X = (float)base->Hotspot.X;
				_renderer.Hotspot.Y = (float)base->Hotspot.Y;
				_renderer.setTexture(base->TextureDiffuse.get());
				_renderer.SetIndexed((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed, 0);
				_renderer.UpdateAnchor();
				_renderer.Initialize(Actors::ActorRendererType::Default);
				// The renderer recomputes the visible frame each frame in its own OnUpdate (UpdateVisibleFrames is private)
			}

			// Marks the object for removal at the end of the frame (SetState is protected, so the jjOBJ proxy routes
			// deleteObject/deactivate through here)
			void Remove() {
				SetState(Actors::ActorState::IsDestroyed, true);
			}
			// Toggles whether other actors/players collide with this object as a solid
			void SetSolid(bool solid) {
				SetState(Actors::ActorState::IsSolidObject, solid);
			}

		protected:
			void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) override
			{
				EmitObjectLight(*_obj, _pos, _levelScripts->GetGameTick(), lights);
			}

			Task<bool> OnActivatedAsync(const Actors::ActorActivationDetails& details) override
			{
				_obj->xPos = _obj->xOrg = _pos.X;
				_obj->yPos = _obj->yOrg = _pos.Y;
				// The script behavior drives the object; don't apply engine gravity by default
				SetState(Actors::ActorState::ApplyGravitation, false);
				async_return true;
			}

			void OnUpdate(float timeMult) override
			{
				// A behavior of the script moves the object itself (and counts its `age`), a behavior of the game that
				// this object can't run at least moves it by its speed
				// The behaviors of JJ2+ count in ticks, 70 of them in a second whatever the frame rate is, a few are caught up
				// after a slow frame (see ScriptObjectHost::OnUpdate())
				constexpr std::int32_t MaxTicksPerFrame = 4;
				std::int32_t currentTick = _levelScripts->GetGameTick();
				if (_nextTick < 0 || currentTick - _nextTick >= MaxTicksPerFrame) {
					_nextTick = currentTick;
				}
				while (_nextTick <= currentTick && !GetState(Actors::ActorState::IsDestroyed)) {
					_nextTick++;
					RunTick();
				}

				// Apply the (possibly script-adjusted) position/facing to the actor
				MoveInstantly(Vector2f(_obj->xPos, _obj->yPos), Actors::MoveType::Absolute | Actors::MoveType::Force);
				SetFacingLeft(_obj->direction < 0);
				// A behavior draws the frame it chooses (see jjOBJ::draw()), without one the animation plays by itself and
				// the current frame is the one it shows
				std::uint32_t frameCount, firstFrame;
				if (!_drawnByBehavior && _lastAnimation != UINT32_MAX &&
					_levelScripts->GetAnimationTables().GetAnimationFrames(_lastAnimation, frameCount, firstFrame) && frameCount > 0) {
					_obj->curFrame = firstFrame + std::uint32_t(_renderer.CurrentFrame) % frameCount;
				}
			}

			bool OnDraw(RenderQueue& renderQueue) override
			{
				return _drawnByBehavior;
			}

		public:
			/** @brief Runs the behavior of the object for one tick of the scripts */
			void RunTick()
			{
				// A copy keeps the behavior alive while it runs, it may replace itself
				jjBEHAVIOR behavior = _obj->behavior;
				_drawnByBehavior = ScriptObjectHost::CallScriptBehavior(_levelScripts, *_obj, behavior);
				if (!_drawnByBehavior) {
					switch (behavior.behaviorId) {
						case aBULLET:
							RunBullet(true);
							_drawnByBehavior = true;
							break;
						case aSHARD:
							RunShard(true);
							_drawnByBehavior = true;
							break;
						case BehaviorCrate:
							// A broken crate, as JJ2+ has it, it explodes after it breaks
							if (_obj->state == sACTION) {
								BreakCrate();
							} else {
								Remove();
							}
							break;
						case aUNKNOWN:		// BEHAVIOR::INACTIVE
						case aEXPLOSION:
						case BehaviorExplosion2:
							// What's left of a broken crate (see BreakCrate()), unless a behavior of the script keeps it
							Remove();
							break;
						default: {
							// In units of the 60 frames in a second, as a tick is
							constexpr float TickTimeMult = 60.0f / 70.0f;
							_obj->xSpeed += _obj->xAcc * TickTimeMult;
							_obj->ySpeed += _obj->yAcc * TickTimeMult;
							_obj->xPos += _obj->xSpeed * TickTimeMult;
							_obj->yPos += _obj->ySpeed * TickTimeMult;
							break;
						}
					}
				}
				HitPlayers();
			}

			/**
			 * @brief Runs `BEHAVIOR::CRATE` once on a crate that was hit (in `STATE::ACTION`), as measured in JJ2+
			 *
			 * The crate breaks into 11 pieces (`OBJECT::SHARD`) where it is, and it's an explosion then - which a behavior
			 * of a script can keep from it, to keep what remains of the crate.
			 */
			void BreakCrate()
			{
				constexpr std::int32_t ShardCount = 11;
				// The pieces of a crate in the set PICKUPS
				constexpr std::uint32_t ShardAnimation = 93;

				jjOBJ& obj = *_obj;
				if (obj.state != sACTION) {
					return;
				}

				std::int32_t setID = AnimationTables::GetSetID(mPICKUPS, _levelScripts->IsTSF());
				std::uint32_t firstAnim = (setID >= 0 ? _levelScripts->GetAnimationTables().GetFirstAnim(setID) : 0);
				for (std::int32_t i = 0; i < ShardCount; i++) {
					std::int32_t id = _levelScripts->AddScriptControlledObject(aSHARD, obj.xPos, obj.yPos, nullptr);
					if (auto* shard = runtime_cast<ScriptLegacyObject>(_levelScripts->GetScriptObject(id))) {
						jjOBJ* shardObj = shard->_obj;
						shardObj->behavior = aSHARD;
						shardObj->curAnim = std::int16_t(firstAnim != 0 ? firstAnim + ShardAnimation : 0);
						shardObj->_playerHandling = HandlingParticle;
						shardObj->_creatorType = 0;
						shardObj->_creatorID = std::uint16_t(obj.objectID);
					}
				}

				obj.state = sKILL;
				obj.behavior = BehaviorExplosion2;
				obj._bulletHandling = HandlingIgnoreBullet;
			}

			/**
			 * @brief Runs `BEHAVIOR::SHARD` once, a piece of a broken crate
			 *
			 * It flies off upwards at random, falls (0.125 px faster each tick, as measured in JJ2+) and bounces off the
			 * ground, and it's gone after a while.
			 */
			void RunShard(bool draw)
			{
				constexpr float Gravity = 0.125f;

				jjOBJ& obj = *_obj;
				auto& tables = _levelScripts->GetAnimationTables();
				std::uint32_t frameCount = 0, firstFrame = 0;
				bool hasFrames = (obj.curAnim > 0 && tables.GetAnimationFrames(std::uint32_t(obj.curAnim), frameCount, firstFrame) && frameCount > 0);
				if (obj.state == sSTART) {
					obj.xSpeed = Random().FastFloat(-2.5f, 2.5f);
					obj.ySpeed = Random().FastFloat(-3.5f, -1.0f);
					obj.frameID = std::int8_t(hasFrames ? Random().Next(0, frameCount) : 0);
					obj.state = sACTION;
					_ticksLeft = std::int32_t(Random().Next(70, 210));
				}
				if (--_ticksLeft <= 0) {
					Remove();
					return;
				}

				if (IsMasked(obj.xPos + obj.xSpeed, obj.yPos)) {
					obj.xSpeed = -0.5f * obj.xSpeed;
				} else {
					obj.xPos += obj.xSpeed;
				}
				if (obj.ySpeed > 0.0f && IsMasked(obj.xPos, obj.yPos + obj.ySpeed)) {
					obj.ySpeed = -0.5f * obj.ySpeed;
					obj.xSpeed *= 0.7f;
				} else {
					obj.yPos += obj.ySpeed;
				}
				obj.ySpeed += Gravity;

				if (hasFrames) {
					obj.curFrame = firstFrame + std::uint32_t(obj.frameID) % frameCount;
				}
				if (draw && obj.curFrame != 0) {
					ScriptObjectHost::DrawFrame(_levelScripts, obj);
				}
			}

			/**
			 * @brief Runs `BEHAVIOR::BULLET` once, as measured in JJ2+
			 *
			 * A new bullet flies the way its creator faces, faster by the speed of the creator. It flies until its counter
			 * passes `counterEnd` or it's in a wall and explodes then, its `killAnim` plays once, a frame in 8 ticks, and
			 * the bullet is gone after it.
			 */
			void RunBullet(bool draw)
			{
				// Measured in JJ2+, the acceleration doesn't make a bullet faster than this
				constexpr float MaxSpeed = 10.0f;

				jjOBJ& obj = *_obj;
				if (obj.state == sSTART) {
					std::int8_t direction;
					float creatorSpeed;
					GetCreatorMotion(direction, creatorSpeed);
					obj.direction = (direction != 0 ? direction : 1);
					obj.xSpeed = obj.direction * obj.xSpeed + creatorSpeed;
					obj.xAcc = obj.direction * obj.xAcc;
					obj.state = sFLY;
				}

				auto& tables = _levelScripts->GetAnimationTables();
				std::uint32_t frameCount, firstFrame;
				if (obj.state == sEXPLODE) {
					if (obj.curAnim != std::int16_t(obj.killAnim)) {
						obj.curAnim = std::int16_t(obj.killAnim);
						obj.frameID = 0;
					} else if ((_levelScripts->GetGameTick() & 7) == 0) {
						obj.frameID++;
					}
					if (obj.curAnim <= 0 || !tables.GetAnimationFrames(std::uint32_t(obj.curAnim), frameCount, firstFrame) ||
						obj.frameID < 0 || std::uint32_t(obj.frameID) >= frameCount) {
						Remove();
						return;
					}
					obj.curFrame = firstFrame + std::uint32_t(obj.frameID);
				} else {
					if (obj.counter > obj.counterEnd || IsMasked(obj.xPos, obj.yPos)) {
						obj.state = sEXPLODE;
						obj.counter = 1;
					} else {
						obj.counter++;
						obj.xPos += obj.xSpeed;
						obj.yPos += obj.ySpeed;
						if (obj.xAcc != 0.0f && std::abs(obj.xSpeed) < MaxSpeed) {
							obj.xSpeed = std::clamp(obj.xSpeed + obj.xAcc, -MaxSpeed, MaxSpeed);
						}
						obj.ySpeed += obj.yAcc;
					}
					// A frame in 4 ticks
					if (obj.curAnim > 0 && tables.GetAnimationFrames(std::uint32_t(obj.curAnim), frameCount, firstFrame) && frameCount > 0) {
						obj.frameID = std::int8_t((obj.counter / 4) % std::int32_t(frameCount));
						obj.curFrame = firstFrame + std::uint32_t(obj.frameID);
					}
				}

				if (draw && obj.curFrame != 0) {
					ScriptObjectHost::DrawFrame(_levelScripts, obj);
				}
			}

		private:
			// BEHAVIOR::CRATE, BEHAVIOR::EXPLOSION2, HANDLING::PARTICLE and HANDLING::IGNOREBULLET of JJ2+
			static constexpr std::uint32_t BehaviorCrate = aCOUNT + 9;
			static constexpr std::uint32_t BehaviorExplosion2 = aCOUNT + 21;
			static constexpr std::uint32_t HandlingParticle = 3;
			static constexpr std::uint32_t HandlingIgnoreBullet = 1;

			LevelScriptLoader* _levelScripts;
			jjOBJ* _obj;
			std::uint32_t _lastAnimation = UINT32_MAX;
			// The next tick of the scripts the behavior runs in, -1 before the first one
			std::int32_t _nextTick = -1;
			// Ticks a piece of a broken crate is still there for (see RunShard())
			std::int32_t _ticksLeft = 0;
			// A behavior draws the object, its animation isn't drawn by itself
			bool _drawnByBehavior = false;

			// Returns the direction and the speed of who made the object, none if there's no such object or player
			void GetCreatorMotion(std::int8_t& direction, float& speed)
			{
				// CREATOR::Type of JJ2+
				constexpr std::uint32_t CreatorObject = 0;
				constexpr std::uint32_t CreatorPlayer = 2;

				direction = 0;
				speed = 0.0f;
				if (_obj->_creatorType == CreatorPlayer) {
					for (Actors::Player* player : _levelScripts->GetPlayers()) {
						if (player->GetPlayerIndex() == std::int32_t(_obj->_creatorID)) {
							direction = (player->IsFacingLeft() ? -1 : 1);
							speed = player->GetSpeed().X;
							break;
						}
					}
				} else if (_obj->_creatorType == CreatorObject && _obj->_creatorID > 0) {
					jjOBJ* creator = _levelScripts->GetObjectById(_obj->_creatorID);
					direction = creator->direction;
					speed = creator->xSpeed;
					creator->Release();
				}
			}

			// Returns `true` if the pixel is in a wall, as jjMaskedPixel() tells
			bool IsMasked(float x, float y)
			{
				auto* tileMap = _levelHandler->TileMap();
				if (tileMap == nullptr) {
					return false;
				}
				Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true };
				return !tileMap->IsTileEmpty(AABBf(x, y, x, y), params);
			}

			// A bullet of an enemy (HANDLING::ENEMYBULLET) explodes on a player it touches and hurts the player by its
			// `animSpeed`, as measured in JJ2+ - unless the player can't be hurt then
			void HitPlayers()
			{
				constexpr std::uint32_t HandlingEnemyBullet = 2;

				jjOBJ& obj = *_obj;
				if (obj._playerHandling != HandlingEnemyBullet || obj.state == sEXPLODE || obj.state == sKILL ||
					GetState(Actors::ActorState::IsDestroyed)) {
					return;
				}
				for (Actors::Player* player : _levelScripts->GetPlayers()) {
					if (player->GetHealth() <= 0 || !DoesObjectTouch(_levelScripts, obj, player->AABBInner)) {
						continue;
					}
					if (!player->IsInvulnerable()) {
						player->TakeDamage(std::max(obj.animSpeed, 1), 4.0f * (player->GetPos().X > obj.xPos ? 1.0f : -1.0f));
					}
					obj.state = sEXPLODE;
					break;
				}
			}
		};

		jjBEHAVIOR* jjBEHAVIOR::Create(jjBEHAVIOR* self) {
			return new(self) jjBEHAVIOR{};
		}
		jjBEHAVIOR* jjBEHAVIOR::CreateFromBehavior(std::uint32_t behavior, jjBEHAVIOR* self) {
			auto* result = new(self) jjBEHAVIOR{};
			result->behaviorId = behavior;
			return result;
		}
		void jjBEHAVIOR::Destroy(jjBEHAVIOR* self) {
			self->~jjBEHAVIOR();
		}

		jjBEHAVIOR::~jjBEHAVIOR() {
			if (function != nullptr) {
				function->Release();
			}
			if (object != nullptr) {
				object->Release();
			}
		}

		jjBEHAVIOR::jjBEHAVIOR(const jjBEHAVIOR& other)
			: behaviorId(other.behaviorId), function(other.function), object(other.object) {
			if (function != nullptr) {
				function->AddRef();
			}
			if (object != nullptr) {
				object->AddRef();
			}
		}

		jjBEHAVIOR& jjBEHAVIOR::operator=(const jjBEHAVIOR& other) {
			if (this != &other) {
				if (other.function != nullptr) {
					other.function->AddRef();
				}
				if (other.object != nullptr) {
					other.object->AddRef();
				}
				if (function != nullptr) {
					function->Release();
				}
				if (object != nullptr) {
					object->Release();
				}
				behaviorId = other.behaviorId;
				function = other.function;
				object = other.object;
			}
			return *this;
		}
		jjBEHAVIOR& jjBEHAVIOR::operator=(std::uint32_t other) {
			if (function != nullptr) {
				function->Release();
				function = nullptr;
			}
			if (object != nullptr) {
				object->Release();
				object = nullptr;
			}
			behaviorId = other;
			return *this;
		}
		jjBEHAVIOR& jjBEHAVIOR::operator=(asIScriptFunction* other) {
			if (other != nullptr) {
				other->AddRef();
			}
			if (function != nullptr) {
				function->Release();
			}
			if (object != nullptr) {
				object->Release();
				object = nullptr;
			}
			function = other;
			behaviorId = 0;
			return *this;
		}
		jjBEHAVIOR& jjBEHAVIOR::operator=(asIScriptObject* other) {
			if (other != nullptr) {
				other->AddRef();
			}
			if (object != nullptr) {
				object->Release();
			}
			if (function != nullptr) {
				function->Release();
				function = nullptr;
			}
			object = other;
			behaviorId = 0;
			return *this;
		}
		bool jjBEHAVIOR::operator==(const jjBEHAVIOR& other) const {
			return (behaviorId == other.behaviorId && function == other.function && object == other.object);
		}
		bool jjBEHAVIOR::operator==(std::uint32_t other) const {
			return (function == nullptr && object == nullptr && behaviorId == other);
		}
		bool jjBEHAVIOR::operator==(const asIScriptFunction* other) const {
			return (function == other);
		}
		jjBEHAVIOR::operator std::uint32_t() {
			return behaviorId;
		}
		jjBEHAVIOR::operator asIScriptFunction* () {
			return function;
		}
		jjBEHAVIOR::operator asIScriptObject* () {
			return object;
		}

		jjOBJ::jjOBJ() : _refCount(1) {
		}
		jjOBJ::~jjOBJ() {
		}

		void jjOBJ::AddRef()
		{
			_refCount++;
		}

		void jjOBJ::Release()
		{
			if (--_refCount == 0) {
				this->~jjOBJ();
				asFreeMem(this);
			}
		}

		bool jjOBJ::get_isActive() const {
			// Active while its object exists, the one a script controls or the one of the game
			return (_wrapper != nullptr || !_actor.expired());
		}
		std::uint32_t jjOBJ::get_lightType() const {
			noop();
			return 0;
		}
		std::uint32_t jjOBJ::set_lightType(std::uint32_t value) const {
			noop();
			return 0;
		}

		jjOBJ* jjOBJ::objectHit(jjOBJ* target, std::uint32_t playerHandling) {
			// This is a bullet that hits the target as the game would have it, even where they aren't - the game handles the
			// collision of the two objects of the game, the script sees the energy the target has then
			std::shared_ptr<Actors::ActorBase> bullet = _actor.lock();
			std::shared_ptr<Actors::ActorBase> victim = (target != nullptr ? target->_actor.lock() : nullptr);
			if (bullet != nullptr && victim != nullptr) {
				if (!victim->OnHandleCollision(bullet.get())) {
					bullet->OnHandleCollision(victim.get());
				}
				target->energy = std::int8_t(std::clamp<std::int32_t>(victim->GetHealth(), INT8_MIN, INT8_MAX));
				target->_synced.Energy = target->energy;
			}
			// The handle is the function's to release, the result is the target
			return target;
		}
		void jjOBJ::blast(std::int32_t maxDistance, bool blastObjects) {
			noop();
		}

		void jjOBJ::behave1(std::uint32_t behavior, bool draw) {
			// Runs a behavior once, it doesn't become the behavior of the object. A behavior of the game is the object's own
			// update, the object has no other; an object a script controls entirely can be a bullet.
			if (_host != nullptr) {
				if (behavior == aUNKNOWN) {
					// BEHAVIOR::INACTIVE
					_host->Delete();
				} else {
					_host->RunOwnBehavior(draw);
				}
			} else if (_wrapper != nullptr) {
				// BEHAVIOR::CRATE of JJ2+
				constexpr std::uint32_t BehaviorCrate = aCOUNT + 9;
				switch (behavior) {
					case aUNKNOWN: _wrapper->Remove(); break;
					case aBULLET: _wrapper->RunBullet(draw); break;
					case aSHARD: _wrapper->RunShard(draw); break;
					case BehaviorCrate: _wrapper->BreakCrate(); break;
				}
			}
		}
		void jjOBJ::behave2(jjBEHAVIOR behavior, bool draw) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			if (!ScriptObjectHost::CallScriptBehavior(owner, *this, behavior, draw)) {
				behave1(behavior.behaviorId, draw);
			}
		}
		void jjOBJ::behave3(jjVOIDFUNCOBJ behavior, bool draw) {
			// The funcdef handle arrives as an AngelScript function pointer (see jjAddObjectEx), with a reference that is
			// this function's to release - the temporary takes it over
			jjBEHAVIOR function;
			function.function = reinterpret_cast<asIScriptFunction*>(behavior);
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			ScriptObjectHost::CallScriptBehavior(owner, *this, function, draw);
		}

		std::int32_t jjOBJ::jjAddObject(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, std::uint32_t behavior) {
			// The spawned actor runs its own native behavior; the custom `behavior`/creator arguments aren't applied
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->AddObjectFromEvent(eventID, xPixel, yPixel);
		}
		std::int32_t jjOBJ::jjAddObjectEx(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, jjVOIDFUNCOBJ behavior) {
			// The funcdef handle is passed by AngelScript as an asIScriptFunction*, with a reference that is this function's
			// to release - the temporary takes it over
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			jjBEHAVIOR function;
			function.function = reinterpret_cast<asIScriptFunction*>(behavior);

			// The object of the game the event has, driven by the behavior, so the behavior can run the object's own one
			std::int32_t id = owner->AddObjectFromEvent(eventID, xPixel, yPixel);
			if (id > 0) {
				jjOBJ* obj = owner->GetObjectById(id);
				obj->behavior = function;
				obj->_creatorID = creatorID;
				obj->_creatorType = creatorType;
				obj->Release();
				return id;
			}

			// The event has no object in the game, a script-controlled one stands in
			id = owner->AddScriptControlledObject(eventID, xPixel, yPixel, function.function);
			// Record the creator on the persistent jjOBJ so scripts can read it back
			if (auto* wrapper = runtime_cast<ScriptLegacyObject>(owner->GetScriptObject(id))) {
				jjOBJ* obj = wrapper->GetObject();
				obj->_creatorID = creatorID;
				obj->_creatorType = creatorType;
				// The behavior runs once right away, as JJ2+ runs it for a new object - a new bullet starts to fly in it
				wrapper->RunTick();
			}
			return id;
		}

		void jjOBJ::jjDeleteObject(std::int32_t objectID) {
			// Silent removal, as by jjOBJ::delete()
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			jjOBJ* obj = owner->GetObjectById(objectID);
			obj->deleteObject();
			obj->Release();
		}
		void jjOBJ::jjKillObject(std::int32_t objectID) {
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto* actor = owner->GetScriptObject(objectID);
			if (actor != nullptr) {
				actor->DecreaseHealth(INT32_MAX);	// kill (triggers perish)
			}
		}

		std::uint32_t jjOBJ::determineCurFrame(bool change) {
			// As JJ2+ documents it, the frame of curAnim that frameID says
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::uint32_t frameCount, firstFrame, result = 0;
			if (curAnim >= 0 && owner->GetAnimationTables().GetAnimationFrames(std::uint32_t(curAnim), frameCount, firstFrame) && frameCount > 0) {
				result = firstFrame + std::uint32_t(frameID < 0 ? -frameID : frameID) % frameCount;
			}
			if (change) {
				curFrame = result;
			}
			return result;
		}

		std::uint16_t jjOBJ::get_creatorID() const {
			return _creatorID;
		}
		std::uint16_t jjOBJ::set_creatorID(std::uint16_t value) const {
			_creatorID = value;
			return _creatorID;
		}
		std::uint32_t jjOBJ::get_creatorType() const {
			return _creatorType;
		}
		std::uint32_t jjOBJ::set_creatorType(std::uint32_t value) const {
			_creatorType = value;
			return _creatorType;
		}

		int16_t jjOBJ::determineCurAnim(std::uint8_t setID, std::uint8_t animation, bool change) {
			return determineCurAnimOfSet(setID, animation, change);
		}
		int16_t jjOBJ::determineCurAnimOfSet(std::int32_t setID, std::uint8_t animation, bool change) {
			// As JJ2+ documents it, the animation of the set in jjAnimations (a set of the original is always loaded), and
			// the hosted actor shows it
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::uint32_t firstAnim = owner->GetAnimationTables().GetFirstAnim(setID);
			int16_t result = int16_t(firstAnim != 0 ? firstAnim + animation : 0);
			if (change) {
				curAnim = result;
				if (_wrapper != nullptr && result > 0) {
					_wrapper->ApplyAnimation(std::uint32_t(result));
				}
			}
			return result;
		}

		std::uint32_t jjOBJ::get_bulletHandling() {
			return _bulletHandling;
		}
		std::uint32_t jjOBJ::set_bulletHandling(std::uint32_t value) {
			_bulletHandling = value;
			_bulletHandlingSet = true;
			return _bulletHandling;
		}
		bool jjOBJ::get_ricochet() {
			return _ricochetProp;
		}
		bool jjOBJ::set_ricochet(bool value) {
			_ricochetProp = value;
			return _ricochetProp;
		}
		bool jjOBJ::get_freezable() {
			return _freezable;
		}
		bool jjOBJ::set_freezable(bool value) {
			_freezable = value;
			return _freezable;
		}
		bool jjOBJ::get_blastable() {
			return _blastable;
		}
		bool jjOBJ::set_blastable(bool value) {
			_blastable = value;
			return _blastable;
		}

		std::uint32_t jjOBJ::get_playerHandling() {
			return _playerHandling;
		}
		std::uint32_t jjOBJ::set_playerHandling(std::uint32_t value) {
			_playerHandling = value;
			_playerHandlingSet = true;
			return _playerHandling;
		}
		bool jjOBJ::get_isTarget() {
			return _isTarget;
		}
		bool jjOBJ::set_isTarget(bool value) {
			_isTarget = value;
			return _isTarget;
		}
		bool jjOBJ::get_triggersTNT() {
			return _triggersTNT;
		}
		bool jjOBJ::set_triggersTNT(bool value) {
			_triggersTNT = value;
			return _triggersTNT;
		}
		bool jjOBJ::get_deactivates() {
			return _deactivates;
		}
		bool jjOBJ::set_deactivates(bool value) {
			_deactivates = value;
			return _deactivates;
		}
		bool jjOBJ::get_scriptedCollisions() {
			return _scriptedCollisions;
		}
		bool jjOBJ::set_scriptedCollisions(bool value) {
			_scriptedCollisions = value;
			return _scriptedCollisions;
		}

		std::int32_t jjOBJ::get_var(std::uint8_t x) {
			return (x < MaxVars ? _vars[x] : 0);
		}
		std::int32_t jjOBJ::set_var(std::uint8_t x, std::int32_t value) {
			if (x < MaxVars) {
				_vars[x] = value;
			}
			return value;
		}

		std::int32_t jjOBJ::draw() {
			// An object of the game a behavior of the script drives is drawn only when the behavior asks, one a script controls
			// entirely draws its current frame
			if (_host != nullptr) {
				_host->Draw();
			} else if (_wrapper != nullptr && curFrame != 0) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				ScriptObjectHost::DrawFrame(owner, *this);
			}
			return 0;
		}
		std::int32_t jjOBJ::beSolid(bool shouldCheckForStompingLocalPlayers) {
			if (_wrapper != nullptr) {
				_wrapper->SetSolid(true);
			}
			return 0;
		}
		void jjOBJ::bePlatform(float xOld, float yOld, std::int32_t width, std::int32_t height) {
			noop();
		}
		void jjOBJ::clearPlatform() {
			noop();
		}
		void jjOBJ::putOnGround(bool precise) {
			// Snap the object down onto (or up out of) the nearest solid ground below its reference point.
			// `precise` scans pixel-by-pixel; the coarse mode steps in 4px increments (faster, tile-ish accuracy).
			// Masks live only on the sprite layer here, so this works regardless of the object's own layer.
			const float x = xPos;
			float y = yPos;
			const float step = precise ? 1.0f : 4.0f;
			const float maxSearch = 256.0f;	// guard against an off-map object scanning the whole level
			float searched = 0.0f;
			if (IsRegionMasked(AABBf(x, y, x, y))) {
				// Embedded in ground: rise until the reference pixel is clear
				while (searched < maxSearch && IsRegionMasked(AABBf(x, y, x, y))) {
					y -= step;
					searched += step;
				}
			} else {
				// Floating: descend until the pixel directly below becomes solid
				while (searched < maxSearch && !IsRegionMasked(AABBf(x, y + 1.0f, x, y + 1.0f))) {
					y += step;
					searched += step;
				}
			}
			yPos = y;
			ySpeed = 0.0f;
		}
		bool jjOBJ::ricochet() {
			noop();
			return false;
		}
		std::int32_t jjOBJ::unfreeze(std::int32_t style) {
			// Thaw a frozen object; the shatter `style` effect isn't emulated for script-controlled objects
			std::int32_t wasFrozen = freeze;
			freeze = 0;
			return wasFrozen;
		}
		void jjOBJ::deleteObject() {
			if (_wrapper != nullptr) {
				_wrapper->Remove();
			} else if (std::shared_ptr<Actors::ActorBase> actor = _actor.lock()) {
				ScriptObjectHost::DeleteActor(*actor);
			}
			// Gone for the scripts at once (BEHAVIOR::INACTIVE), as in JJ2+ - the object of the game is removed only after
			// the frame, which may have more ticks of the scripts
			behavior = aUNKNOWN;
		}
		void jjOBJ::deactivate() {
			if (_wrapper != nullptr) {
				// Script-controlled objects don't return to the event map, so deactivation despawns them
				_wrapper->Remove();
			} else if (std::shared_ptr<Actors::ActorBase> actor = _actor.lock()) {
				ScriptObjectHost::DeactivateActor(*actor);
			}
			behavior = aUNKNOWN;
		}
		void jjOBJ::pathMovement() {
			noop();
		}
		int32_t jjOBJ::fireBullet(std::uint8_t eventID) {
			noop();
			return 0;
		}
		void jjOBJ::particlePixelExplosion(std::int32_t style) {
			noop();
		}
		void jjOBJ::grantPickup(jjPLAYER* player, std::int32_t frequency) {
			noop();
		}

		std::int32_t jjOBJ::findNearestPlayerEx(std::int32_t maxDistance, std::int32_t& foundDistance) const {
			// Both distances are squares of the distance in pixels, as JJ2+ documents them
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			auto players = owner->GetPlayers();
			std::int32_t nearest = -1;
			float bestSq = (float)maxDistance;
			for (std::int32_t i = 0; i < (std::int32_t)players.size(); i++) {
				Vector2f p = players[i]->GetPos();
				float dx = p.X - xPos;
				float dy = p.Y - yPos;
				float distSq = dx * dx + dy * dy;
				if (distSq <= bestSq) {
					bestSq = distSq;
					nearest = i;
				}
			}
			foundDistance = (nearest >= 0 ? (std::int32_t)std::min(bestSq, float(INT32_MAX)) : maxDistance);
			return nearest;
		}
		std::int32_t jjOBJ::findNearestPlayer(std::int32_t maxDistance) const {
			std::int32_t foundDistance;
			return findNearestPlayerEx(maxDistance, foundDistance);
		}

		bool jjOBJ::doesCollide(const jjOBJ* object, bool always) const {
			noop();
			return false;
		}
		bool jjOBJ::doesCollidePlayer(const jjPLAYER* object, bool always) const {
			// The pixels of the frame the object shows against the hitbox of the player
			if (object == nullptr || object->_player == nullptr) {
				return false;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return DoesObjectTouch(owner, *this, object->_player->AABBInner);
		}

		std::uint8_t jjPARTICLEPIXEL::get_color(std::int32_t i) const {
			noop(); return 0;
		}
		std::uint8_t jjPARTICLEPIXEL::set_color(std::int32_t i, std::uint8_t value) {
			noop(); return 0;
		}

		String jjPARTICLESTRING::get_text() const {
			noop(); return {};
		}
		void jjPARTICLESTRING::set_text(String text) {
			noop();
		}

		jjPARTICLE::jjPARTICLE() : _refCount(1) {
			noop();
		}
		jjPARTICLE::~jjPARTICLE() {
			noop();
		}

		void jjPARTICLE::AddRef() {
			_refCount++;
		}

		void jjPARTICLE::Release() {
			if (--_refCount == 0) {
				this->~jjPARTICLE();
				asFreeMem(this);
			}
		}
	}

	jjOBJ* get_jjObjects(std::int32_t index)
	{
		// One persistent view of each object, what a script writes goes to the object (see LevelScriptLoader)
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetObjectById(index);
	}

	jjOBJ* get_jjObjectPresets(std::int8_t id)
	{
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetObjectPreset(std::uint8_t(id));
	}

	void jjAddParticleTileExplosion(std::uint16_t xTile, std::uint16_t yTile, std::uint16_t tile, bool collapseSceneryStyle) {
		noop();
	}
	void jjAddParticlePixelExplosion(float xPixel, float yPixel, int curFrame, int direction, int mode) {
		noop();
	}

	jjPARTICLE* GetParticle(std::int32_t index) {
		noop();
		// TODO
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		void* mem = asAllocMem(sizeof(jjPARTICLE));
		return new(mem) jjPARTICLE();
	}

	jjPARTICLE* AddParticle(std::int32_t particleType) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		void* mem = asAllocMem(sizeof(jjPARTICLE));
		return new(mem) jjPARTICLE();
	}

	std::int32_t LevelScriptLoader::AddObjectFromEvent(std::uint8_t eventId, float xPixel, float yPixel)
	{
		// The converters that read the level get an empty one, see Legacy::EventTranslator
		auto result = GetEventTranslator().FromOriginal(eventId, 0);
		if (result.Type == EventType::Empty) {
			return 0;
		}

		// The object of the game is a bit further than the object of JJ2+ would be, see Legacy::ScriptObjectHost::PositionOffset
		auto actor = _levelHandler->EventSpawner()->SpawnEvent(result.Type, result.Params, Actors::ActorState::None,
			Vector3i(std::int32_t(xPixel + Legacy::ScriptObjectHost::PositionOffset), std::int32_t(yPixel + Legacy::ScriptObjectHost::PositionOffset),
				ILevelHandler::MainPlaneZ));
		if (actor == nullptr) {
			return 0;
		}

		// The object gets its object ID as every object of an event, see OnActorAdded()
		_levelHandler->AddActor(actor);
		return _lastObjectId;
	}

	Actors::ActorBase* LevelScriptLoader::GetScriptObject(std::int32_t objectId)
	{
		if (objectId <= 0 || objectId >= std::int32_t(_objectSlots.size())) {
			return nullptr;
		}
		// Returns nullptr if the actor has since been destroyed (the registry holds only weak references)
		return _objectSlots[objectId].Actor.lock().get();
	}

	std::int32_t LevelScriptLoader::AddScriptControlledObject(std::uint8_t eventId, float xPixel, float yPixel, asIScriptFunction* behaviorFunc)
	{
		// The script-facing object, built from its template as in JJ2+; one reference is created here and handed to the
		// hosting actor
		void* mem = asAllocMem(sizeof(Legacy::jjOBJ));
		auto* obj = new(mem) Legacy::jjOBJ();
		Legacy::jjOBJ* preset = GetObjectPreset(eventId);
		obj->CopyTemplate(*preset);
		preset->Release();
		obj->eventID = eventId;
		obj->xPos = obj->xOrg = xPixel;
		obj->yPos = obj->yOrg = yPixel;
		if (behaviorFunc != nullptr) {
			obj->behavior = behaviorFunc;
		}

		auto wrapper = std::make_shared<Legacy::ScriptLegacyObject>(this, obj);
		wrapper->OnActivated(Actors::ActorActivationDetails(_levelHandler,
			Vector3i((std::int32_t)xPixel, (std::int32_t)yPixel, ILevelHandler::MainPlaneZ)));
		// Where it was asked for, not in whole pixels as the object of the game is placed
		obj->xPos = obj->xOrg = xPixel;
		obj->yPos = obj->yOrg = yPixel;
		_levelHandler->AddActor(wrapper);

		// The object ID keeps a reference of its own
		obj->AddRef();
		std::int32_t id = RegisterObject(wrapper, obj, eventId);
		if (id == 0) {
			obj->Release();
		}
		return id;
	}

	std::int32_t LevelScriptLoader::GetActiveBossObjectID()
	{
		if (_levelHandler->_activeBoss == nullptr) {
			return 0;
		}
		Legacy::jjOBJ* boss = GetObjectView(_levelHandler->_activeBoss.get());
		return (boss != nullptr ? boss->objectID : 0);
	}

	void LevelScriptLoader::BreakObject(std::int32_t objectId)
	{
		_brokenObjects.push_back(objectId);
	}

	void LevelScriptLoader::BreakObjects()
	{
		for (std::int32_t objectId : _brokenObjects) {
			if (objectId <= 0 || objectId >= std::int32_t(_objectSlots.size())) {
				continue;
			}
			ObjectSlot& slot = _objectSlots[objectId];
			Legacy::jjOBJ* obj = slot.Object;
			if (obj == nullptr || obj->_wrapper != nullptr) {
				continue;
			}

			// The object of the game is done, the behavior doesn't drive it anymore
			if (std::shared_ptr<Actors::ActorBase> actor = slot.Actor.lock()) {
				if (actor->GetUpdateOverride() == static_cast<Actors::IActorUpdateOverride*>(obj->_host)) {
					actor->SetUpdateOverride(nullptr);
				}
				Legacy::ScriptObjectHost::DeleteActor(*actor);
			}

			// The object of the script goes on where it is, as a crate JJ2+ has that was hit, under its object ID
			float xPos = obj->xPos, yPos = obj->yPos, xOrg = obj->xOrg, yOrg = obj->yOrg;
			obj->AddRef();
			auto wrapper = std::make_shared<Legacy::ScriptLegacyObject>(this, obj);
			wrapper->OnActivated(Actors::ActorActivationDetails(_levelHandler,
				Vector3i(std::int32_t(xPos), std::int32_t(yPos), ILevelHandler::MainPlaneZ)));
			obj->xPos = xPos;
			obj->yPos = yPos;
			obj->xOrg = xOrg;
			obj->yOrg = yOrg;
			obj->_actor.reset();
			obj->state = Legacy::sACTION;
			_levelHandler->AddActor(wrapper);
			SetSlotActor(objectId, wrapper);
		}
		_brokenObjects.clear();
	}

	void LevelScriptLoader::RegisterLegacyObjects(asIScriptEngine* engine)
	{
		engine->SetDefaultNamespace("CREATOR");
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "OBJECT", 0);
		engine->RegisterEnumValue("Type", "LEVEL", 1);
		engine->RegisterEnumValue("Type", "PLAYER", 2);
		engine->SetDefaultNamespace("AREA");
		engine->RegisterEnum("Area");
		engine->SetDefaultNamespace("OBJECT");
		engine->RegisterEnum("Object");
		engine->SetDefaultNamespace("ANIM");
		engine->RegisterEnum("Set");
		engine->SetDefaultNamespace("");

		engine->SetDefaultNamespace("LIGHT");
		engine->RegisterEnum("Enforce");
		engine->RegisterEnumValue("Enforce", "OPTIONAL", ambientLighting_OPTIONAL);
		engine->RegisterEnumValue("Enforce", "BASIC", ambientLighting_BASIC);
		engine->RegisterEnumValue("Enforce", "COMPLETE", ambientLighting_COMPLETE);

		engine->SetDefaultNamespace("");
		engine->RegisterGlobalProperty("LIGHT::Enforce jjEnforceLighting", &enforceAmbientLighting);

		engine->SetDefaultNamespace("STATE");
		engine->RegisterEnum("State");
		engine->SetDefaultNamespace("BEHAVIOR");
		engine->RegisterEnum("Behavior");

		engine->SetDefaultNamespace("LIGHT");
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "NONE", 0);
		engine->RegisterEnumValue("Type", "NORMAL", 3);
		engine->RegisterEnumValue("Type", "POINT", 1);
		engine->RegisterEnumValue("Type", "POINT2", 2);
		engine->RegisterEnumValue("Type", "FLICKER", 4);
		engine->RegisterEnumValue("Type", "BRIGHT", 5);
		engine->RegisterEnumValue("Type", "LASERBEAM", 6);
		engine->RegisterEnumValue("Type", "LASER", 7);
		engine->RegisterEnumValue("Type", "RING", 8);
		engine->RegisterEnumValue("Type", "RING2", 9);
		engine->RegisterEnumValue("Type", "PLAYER", 10);

		engine->SetDefaultNamespace("HANDLING");
		engine->RegisterEnum("Bullet");
		engine->RegisterEnumValue("Bullet", "HURTBYBULLET", 0);
		engine->RegisterEnumValue("Bullet", "IGNOREBULLET", 1);
		engine->RegisterEnumValue("Bullet", "DESTROYBULLET", 2);
		engine->RegisterEnumValue("Bullet", "DETECTBULLET", 3);
		engine->RegisterEnum("Player");
		engine->RegisterEnumValue("Player", "ENEMY", 0);
		engine->RegisterEnumValue("Player", "PLAYERBULLET", 1);
		engine->RegisterEnumValue("Player", "ENEMYBULLET", 2);
		engine->RegisterEnumValue("Player", "PARTICLE", 3);
		engine->RegisterEnumValue("Player", "EXPLOSION", 4);
		engine->RegisterEnumValue("Player", "PICKUP", 5);
		engine->RegisterEnumValue("Player", "DELAYEDPICKUP", 6);
		engine->RegisterEnumValue("Player", "HURT", 7);
		engine->RegisterEnumValue("Player", "SPECIAL", 8);
		engine->RegisterEnumValue("Player", "DYING", 9);
		engine->RegisterEnumValue("Player", "SPECIALDONE", 10);
		engine->RegisterEnumValue("Player", "SELFCOLLISION", 11);
		engine->SetDefaultNamespace("");

		// TODO
		engine->RegisterObjectType("jjOBJ", sizeof(jjOBJ), asOBJ_REF /*| asOBJ_NOCOUNT*/);
		engine->RegisterObjectBehaviour("jjOBJ", asBEHAVE_ADDREF, "void f()", asMETHOD(jjOBJ, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjOBJ", asBEHAVE_RELEASE, "void f()", asMETHOD(jjOBJ, Release), asCALL_THISCALL);
		engine->RegisterGlobalFunction("jjOBJ @get_jjObjects(int)", asFUNCTION(get_jjObjects), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjOBJ @get_jjObjectPresets(uint8)", asFUNCTION(get_jjObjectPresets), asCALL_CDECL);
		engine->RegisterGlobalProperty("const int jjObjectCount", &jjObjectCount);
		engine->RegisterGlobalProperty("const int jjObjectMax", &jjObjectMax);
		engine->RegisterObjectMethod("jjOBJ", "bool get_isActive() const", asMETHOD(jjOBJ, get_isActive), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "LIGHT::Type get_lightType() const", asMETHOD(jjOBJ, get_lightType), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "LIGHT::Type set_lightType(LIGHT::Type)", asMETHOD(jjOBJ, set_lightType), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "bool doesCollide(const jjOBJ@ object, bool always = false) const", asMETHOD(jjPLAYER, doesCollide), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int getObjectHitForce(const jjOBJ@ target = null) const", asMETHOD(jjPLAYER, getObjectHitForce), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool objectHit(jjOBJ@ target, int force, HANDLING::Player playerHandling)", asMETHOD(jjPLAYER, objectHit), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "jjOBJ@ objectHit(jjOBJ@ target, HANDLING::Player playerHandling)", asMETHOD(jjOBJ, objectHit), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void blast(int maxDistance, bool blastObjects)", asMETHOD(jjOBJ, blast), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "bool isEnemy(const jjPLAYER &in victim) const", asMETHOD(jjPLAYER, isEnemy), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "ANIM::Set get_setID() const", asMETHOD(jjPLAYER, get_charCurr), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint16 get_curAnim() const", asMETHOD(jjPLAYER, get_curAnim), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint get_curFrame() const", asMETHOD(jjPLAYER, get_curFrame), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 get_frameID() const", asMETHOD(jjPLAYER, get_frameID), asCALL_THISCALL);

		engine->RegisterFuncdef("void jjVOIDFUNCOBJ(jjOBJ@)");
		engine->RegisterObjectType("jjBEHAVIOR", sizeof(jjBEHAVIOR), asOBJ_VALUE | asOBJ_APP_CLASS_CDA);
		engine->RegisterObjectBehaviour("jjBEHAVIOR", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(jjBEHAVIOR::Create), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjBEHAVIOR", asBEHAVE_CONSTRUCT, "void f(const BEHAVIOR::Behavior &in behavior)", asFUNCTION(jjBEHAVIOR::CreateFromBehavior), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjBEHAVIOR", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(jjBEHAVIOR::Destroy), asCALL_CDECL_OBJLAST);

		engine->RegisterInterface("jjBEHAVIORINTERFACE");
		engine->RegisterInterfaceMethod("jjBEHAVIORINTERFACE", "void onBehave(jjOBJ@ obj)");

		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIOR& opAssign(const jjBEHAVIOR &in)", asMETHODPR(jjBEHAVIOR, operator=, (const jjBEHAVIOR&), jjBEHAVIOR&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIOR& opAssign(BEHAVIOR::Behavior)", asMETHODPR(jjBEHAVIOR, operator=, (uint32_t), jjBEHAVIOR&), asCALL_THISCALL);
		// The handles are auto handles (@+), AngelScript keeps their references - the behavior adds its own for what it holds,
		// and what it gives back isn't a reference of the caller's
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIOR& opAssign(jjVOIDFUNCOBJ@+)", asMETHODPR(jjBEHAVIOR, operator=, (asIScriptFunction*), jjBEHAVIOR&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIOR& opAssign(jjBEHAVIORINTERFACE@+)", asMETHODPR(jjBEHAVIOR, operator=, (asIScriptObject*), jjBEHAVIOR&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "bool opEquals(const jjBEHAVIOR &in) const", asMETHODPR(jjBEHAVIOR, operator==, (const jjBEHAVIOR&) const, bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "bool opEquals(BEHAVIOR::Behavior) const", asMETHODPR(jjBEHAVIOR, operator==, (uint32_t) const, bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "bool opEquals(const jjVOIDFUNCOBJ@+) const", asMETHODPR(jjBEHAVIOR, operator==, (const asIScriptFunction*) const, bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "BEHAVIOR::Behavior opConv() const", asMETHOD(jjBEHAVIOR, operator uint32_t), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjVOIDFUNCOBJ@+ opCast() const", asMETHOD(jjBEHAVIOR, operator asIScriptFunction*), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjBEHAVIOR", "jjBEHAVIORINTERFACE@+ opCast() const", asMETHOD(jjBEHAVIOR, operator asIScriptObject*), asCALL_THISCALL);

		engine->RegisterObjectProperty("jjOBJ", "jjBEHAVIOR behavior", asOFFSET(jjOBJ, behavior));

		engine->RegisterObjectMethod("jjOBJ", "void behave(BEHAVIOR::Behavior behavior = BEHAVIOR::DEFAULT, bool draw = true)", asMETHOD(jjOBJ, behave1), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void behave(jjBEHAVIOR behavior, bool draw = true)", asMETHOD(jjOBJ, behave2), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void behave(jjVOIDFUNCOBJ@ behavior, bool draw = true)", asMETHOD(jjOBJ, behave3), asCALL_THISCALL);

		engine->RegisterGlobalFunction("int jjAddObject(uint8 eventID, float xPixel, float yPixel, uint16 creatorID = 0, CREATOR::Type creatorType = CREATOR::OBJECT, BEHAVIOR::Behavior behavior = BEHAVIOR::DEFAULT)", asFUNCTION(jjOBJ::jjAddObject), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjAddObject(uint8 eventID, float xPixel, float xPixel, uint16 creatorID, CREATOR::Type creatorType, jjVOIDFUNCOBJ@ behavior)", asFUNCTION(jjOBJ::jjAddObjectEx), asCALL_CDECL);

		engine->RegisterObjectProperty("jjOBJ", "float xOrg", asOFFSET(jjOBJ, xOrg));
		engine->RegisterObjectProperty("jjOBJ", "float yOrg", asOFFSET(jjOBJ, yOrg));
		engine->RegisterObjectProperty("jjOBJ", "float xPos", asOFFSET(jjOBJ, xPos));
		engine->RegisterObjectProperty("jjOBJ", "float yPos", asOFFSET(jjOBJ, yPos));
		engine->RegisterObjectProperty("jjOBJ", "float xSpeed", asOFFSET(jjOBJ, xSpeed));
		engine->RegisterObjectProperty("jjOBJ", "float ySpeed", asOFFSET(jjOBJ, ySpeed));
		engine->RegisterObjectProperty("jjOBJ", "float xAcc", asOFFSET(jjOBJ, xAcc));
		engine->RegisterObjectProperty("jjOBJ", "float yAcc", asOFFSET(jjOBJ, yAcc));
		engine->RegisterObjectProperty("jjOBJ", "int counter", asOFFSET(jjOBJ, counter));
		engine->RegisterObjectProperty("jjOBJ", "uint curFrame", asOFFSET(jjOBJ, curFrame));
		engine->RegisterObjectMethod("jjOBJ", "uint determineCurFrame(bool change = true)", asMETHOD(jjOBJ, determineCurFrame), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjOBJ", "int age", asOFFSET(jjOBJ, age));
		engine->RegisterObjectProperty("jjOBJ", "int creator", asOFFSET(jjOBJ, creator)); // Deprecated
		engine->RegisterObjectMethod("jjOBJ", "uint16 get_creatorID() const", asMETHOD(jjOBJ, get_creatorID), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "uint16 set_creatorID(uint16)", asMETHOD(jjOBJ, set_creatorID), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "CREATOR::Type get_creatorType() const", asMETHOD(jjOBJ, get_creatorType), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "CREATOR::Type set_creatorType(CREATOR::Type)", asMETHOD(jjOBJ, set_creatorType), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjOBJ", "int16 curAnim", asOFFSET(jjOBJ, curAnim));
		engine->RegisterObjectMethod("jjOBJ", "int16 determineCurAnim(uint8 setID, uint8 animation, bool change = true)", asMETHOD(jjOBJ, determineCurAnim), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "int16 determineCurAnim(ANIM::Set setID, uint8 animation, bool change = true)", asMETHOD(jjOBJ, determineCurAnimOfSet), asCALL_THISCALL);

		engine->RegisterObjectProperty("jjOBJ", "int16 killAnim", asOFFSET(jjOBJ, killAnim));
		engine->RegisterObjectProperty("jjOBJ", "uint8 freeze", asOFFSET(jjOBJ, freeze));
		engine->RegisterObjectProperty("jjOBJ", "uint8 lightType", asOFFSET(jjOBJ, lightType));
		engine->RegisterObjectProperty("jjOBJ", "int8 frameID", asOFFSET(jjOBJ, frameID));
		engine->RegisterObjectProperty("jjOBJ", "int8 noHit", asOFFSET(jjOBJ, noHit)); // Deprecated
		engine->RegisterObjectMethod("jjOBJ", "HANDLING::Bullet get_bulletHandling() const", asMETHOD(jjOBJ, get_bulletHandling), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "HANDLING::Bullet set_bulletHandling(HANDLING::Bullet)", asMETHOD(jjOBJ, set_bulletHandling), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool get_causesRicochet() const", asMETHOD(jjOBJ, get_ricochet), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool set_causesRicochet(bool)", asMETHOD(jjOBJ, set_ricochet), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool get_isFreezable() const", asMETHOD(jjOBJ, get_freezable), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool set_isFreezable(bool)", asMETHOD(jjOBJ, set_freezable), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool get_isBlastable() const", asMETHOD(jjOBJ, get_blastable), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool set_isBlastable(bool)", asMETHOD(jjOBJ, set_blastable), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjOBJ", "int8 energy", asOFFSET(jjOBJ, energy));
		engine->RegisterObjectProperty("jjOBJ", "int8 light", asOFFSET(jjOBJ, light));
		engine->RegisterObjectProperty("jjOBJ", "uint8 objType", asOFFSET(jjOBJ, objType)); // Deprecated
		engine->RegisterObjectMethod("jjOBJ", "HANDLING::Player get_playerHandling() const", asMETHOD(jjOBJ, get_playerHandling), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "HANDLING::Player set_playerHandling(HANDLING::Player)", asMETHOD(jjOBJ, set_playerHandling), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool get_isTarget() const", asMETHOD(jjOBJ, get_isTarget), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool set_isTarget(bool)", asMETHOD(jjOBJ, set_isTarget), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool get_triggersTNT() const", asMETHOD(jjOBJ, get_triggersTNT), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool set_triggersTNT(bool)", asMETHOD(jjOBJ, set_triggersTNT), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool get_deactivates() const", asMETHOD(jjOBJ, get_deactivates), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool set_deactivates(bool)", asMETHOD(jjOBJ, set_deactivates), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool get_scriptedCollisions() const", asMETHOD(jjOBJ, get_scriptedCollisions), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool set_scriptedCollisions(bool)", asMETHOD(jjOBJ, set_scriptedCollisions), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjOBJ", "int8 state", asOFFSET(jjOBJ, state));
		engine->RegisterObjectProperty("jjOBJ", "uint16 points", asOFFSET(jjOBJ, points));
		engine->RegisterObjectProperty("jjOBJ", "uint8 eventID", asOFFSET(jjOBJ, eventID));
		engine->RegisterObjectProperty("jjOBJ", "int8 direction", asOFFSET(jjOBJ, direction));
		engine->RegisterObjectProperty("jjOBJ", "uint8 justHit", asOFFSET(jjOBJ, justHit));
		engine->RegisterObjectProperty("jjOBJ", "int8 oldState", asOFFSET(jjOBJ, oldState));
		engine->RegisterObjectProperty("jjOBJ", "int animSpeed", asOFFSET(jjOBJ, animSpeed));
		engine->RegisterObjectProperty("jjOBJ", "int special", asOFFSET(jjOBJ, special));
		engine->RegisterObjectMethod("jjOBJ", "int get_var(uint8) const", asMETHOD(jjOBJ, get_var), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "int set_var(uint8, int)", asMETHOD(jjOBJ, set_var), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjOBJ", "uint8 doesHurt", asOFFSET(jjOBJ, doesHurt));
		engine->RegisterObjectProperty("jjOBJ", "uint8 counterEnd", asOFFSET(jjOBJ, counterEnd));
		engine->RegisterObjectProperty("jjOBJ", "const int16 objectID", asOFFSET(jjOBJ, objectID));

		engine->RegisterGlobalFunction("void jjDeleteObject(int objectID)", asFUNCTION(jjOBJ::jjDeleteObject), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjKillObject(int objectID)", asFUNCTION(jjOBJ::jjKillObject), asCALL_CDECL);
		engine->RegisterGlobalProperty("const bool jjDeactivatingBecauseOfDeath", &jjDeactivatingBecauseOfDeath);

		engine->RegisterObjectMethod("jjOBJ", "int draw()", asMETHOD(jjOBJ, draw), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "int beSolid(bool shouldCheckForStompingLocalPlayers = false)", asMETHOD(jjOBJ, beSolid), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void bePlatform(float xOld, float yOld, int width = 0, int height = 0)", asMETHOD(jjOBJ, bePlatform), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void clearPlatform()", asMETHOD(jjOBJ, clearPlatform), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void putOnGround(bool precise = false)", asMETHOD(jjOBJ, putOnGround), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool ricochet()", asMETHOD(jjOBJ, ricochet), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "int unfreeze(int style)", asMETHOD(jjOBJ, unfreeze), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void delete()", asMETHOD(jjOBJ, deleteObject), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void deactivate()", asMETHOD(jjOBJ, deactivate), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void pathMovement()", asMETHOD(jjOBJ, pathMovement), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "int fireBullet(uint8 eventID) const", asMETHOD(jjOBJ, fireBullet), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void particlePixelExplosion(int style) const", asMETHOD(jjOBJ, particlePixelExplosion), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void grantPickup(jjPLAYER@ player, int frequency) const", asMETHOD(jjOBJ, grantPickup), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjOBJ", "int findNearestPlayer(int maxDistance) const", asMETHOD(jjOBJ, findNearestPlayer), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "int findNearestPlayer(int maxDistance, int &out foundDistance) const", asMETHOD(jjOBJ, findNearestPlayerEx), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjOBJ", "bool doesCollide(const jjOBJ@ object, bool always = false) const", asMETHOD(jjOBJ, doesCollide), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "bool doesCollide(const jjPLAYER@ player, bool always = false) const", asMETHOD(jjOBJ, doesCollidePlayer), asCALL_THISCALL);

		engine->RegisterGlobalFunction("void jjAddParticleTileExplosion(uint16 xTile, uint16 yTile, uint16 tile, bool collapseSceneryStyle)", asFUNCTION(jjAddParticleTileExplosion), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjAddParticlePixelExplosion(float xPixel, float yPixel, int curFrame, int direction, int mode)", asFUNCTION(jjAddParticlePixelExplosion), asCALL_CDECL);

		engine->SetDefaultNamespace("PARTICLE");
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "INACTIVE", particleNONE);
		engine->RegisterEnumValue("Type", "PIXEL", particlePIXEL);
		engine->RegisterEnumValue("Type", "FIRE", particleFIRE);
		engine->RegisterEnumValue("Type", "SMOKE", particleSMOKE);
		engine->RegisterEnumValue("Type", "ICETRAIL", particleICETRAIL);
		engine->RegisterEnumValue("Type", "SPARK", particleSPARK);
		engine->RegisterEnumValue("Type", "STRING", particleSCORE);
		engine->RegisterEnumValue("Type", "SNOW", particleSNOW);
		engine->RegisterEnumValue("Type", "RAIN", particleRAIN);
		engine->RegisterEnumValue("Type", "FLOWER", particleFLOWER);
		engine->RegisterEnumValue("Type", "LEAF", particleLEAF);
		engine->RegisterEnumValue("Type", "STAR", particleSTAR);
		engine->RegisterEnumValue("Type", "TILE", particleTILE);
		engine->SetDefaultNamespace("");
	}

	void LevelScriptLoader::RegisterLegacyParticles(asIScriptEngine* engine)
	{
		engine->RegisterObjectType("jjPARTICLE", sizeof(jjPARTICLE), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjPARTICLE @get_jjParticles(int)", asFUNCTION(GetParticle), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPARTICLE @jjAddParticle(PARTICLE::Type type)", asFUNCTION(AddParticle), asCALL_CDECL);
		engine->RegisterObjectProperty("jjPARTICLE", "float xPos", asOFFSET(jjPARTICLE, xPos));
		engine->RegisterObjectProperty("jjPARTICLE", "float yPos", asOFFSET(jjPARTICLE, yPos));
		engine->RegisterObjectProperty("jjPARTICLE", "float xSpeed", asOFFSET(jjPARTICLE, xSpeed));
		engine->RegisterObjectProperty("jjPARTICLE", "float ySpeed", asOFFSET(jjPARTICLE, ySpeed));
		engine->RegisterObjectProperty("jjPARTICLE", "uint8 type", asOFFSET(jjPARTICLE, particleType));
		engine->RegisterObjectProperty("jjPARTICLE", "bool isActive", asOFFSET(jjPARTICLE, active));
		
		engine->RegisterObjectType("jjPARTICLEPIXEL", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLEPIXEL", "uint8 size", 0);
		engine->RegisterObjectMethod("jjPARTICLEPIXEL", "uint8 get_color(int) const", asMETHOD(jjPARTICLEPIXEL, get_color), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPARTICLEPIXEL", "uint8 set_color(int, uint8)", asMETHOD(jjPARTICLEPIXEL, set_color), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEPIXEL pixel", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLEFIRE", 9, asOBJ_VALUE | asOBJ_POD);  // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "uint8 size", 0);
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "uint8 color", 1);
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "uint8 colorStop", 2);
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "int8 colorDelta", 3);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEFIRE fire", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLESMOKE", 9, asOBJ_VALUE | asOBJ_POD);  // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLESMOKE", "uint8 countdown", 0);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESMOKE smoke", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLEICETRAIL", 9, asOBJ_VALUE | asOBJ_POD);
		engine->RegisterObjectProperty("jjPARTICLEICETRAIL", "uint8 color", 0);
		engine->RegisterObjectProperty("jjPARTICLEICETRAIL", "uint8 colorStop", 1);
		engine->RegisterObjectProperty("jjPARTICLEICETRAIL", "int8 colorDelta", 2);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEICETRAIL icetrail", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLESPARK", 9, asOBJ_VALUE | asOBJ_POD);  // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLESPARK", "uint8 color", 0);
		engine->RegisterObjectProperty("jjPARTICLESPARK", "uint8 colorStop", 1);
		engine->RegisterObjectProperty("jjPARTICLESPARK", "int8 colorDelta", 2);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESPARK spark", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLESTRING", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectMethod("jjPARTICLESTRING", "::string get_text() const", asMETHOD(jjPARTICLESTRING, get_text), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPARTICLESTRING", "void set_text(::string)", asMETHOD(jjPARTICLESTRING, set_text), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESTRING string", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLESNOW", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint8 frame", 0);
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint8 countup", 1);
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint8 countdown", 2);
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint16 frameBase", 3);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESNOW snow", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLERAIN", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLERAIN", "uint8 frame", 0);
		engine->RegisterObjectProperty("jjPARTICLERAIN", "uint16 frameBase", 1);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLERAIN rain", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLELEAF", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint8 frame", 0);
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint8 countup", 1);
		engine->RegisterObjectProperty("jjPARTICLELEAF", "bool noclip", 2);
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint8 height", 3);
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint16 frameBase", 4);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLELEAF leaf", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLEFLOWER", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 size", 0);
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 color", 1);
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 angle", 2);
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "int8 angularSpeed", 3);
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 petals", 4);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEFLOWER flower", asOFFSET(jjPARTICLE, GENERIC));
		engine->RegisterObjectType("jjPARTICLESTAR", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 size", 0);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 color", 1);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 angle", 2);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "int8 angularSpeed", 3);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 frame", 4);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 colorChangeCounter", 5);
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 colorChangeInterval", 6);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESTAR star", asOFFSET(jjPARTICLE, GENERIC));
		// TODO
		/*engine->RegisterObjectType("jjPARTICLETILE", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectProperty("jjPARTICLETILE", "uint8 quadrant", 0);
		engine->RegisterObjectMethod("jjPARTICLETILE", "uint16 get_tileID() const", asMETHOD(TparticleTILE, get_AStile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPARTICLETILE", "uint16 set_tileID(uint16)", asMETHOD(TparticleTILE, set_AStile), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLETILE tile", asOFFSET(jjPARTICLE, GENERIC));

		engine->RegisterObjectType("jjCONTROLPOINT", sizeof(_controlPoint), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("const jjCONTROLPOINT@ get_jjControlPoints(int)", asFUNCTION(getControlPoint), asCALL_CDECL);
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const string name", asOFFSET(_controlPoint, name));
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const int xTile", asOFFSET(_controlPoint, xTile));
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const int yTile", asOFFSET(_controlPoint, yTile));
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const int direction", asOFFSET(_controlPoint, direction));
		engine->RegisterObjectProperty("jjCONTROLPOINT", "const TEAM::Color controlTeam", asOFFSET(_controlPoint, controlTeam));
		engine->RegisterObjectMethod("jjCONTROLPOINT", "float get_xPos() const", AS_OBJ_FLOAT_GETTER(_controlPoint, pos.x), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectMethod("jjCONTROLPOINT", "float get_yPos() const", AS_OBJ_FLOAT_GETTER(_controlPoint, pos.y), asCALL_CDECL_OBJLAST);*/
	}
}

#endif
