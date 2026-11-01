#if defined(WITH_ANGELSCRIPT)

#include "Objects.h"
#include "AnimationTables.h"
#include "EventTranslator.h"
#include "ObjectHost.h"
#include "Internal.h"
#include "Layers.h"
#include "PixelMaps.h"
#include "WorldCanvas.h"

#include "../../Actors/ActorBase.h"
#include "../../Actors/Player.h"
#include "../../Actors/Collectibles/CollectibleBase.h"
#include "../../Actors/SolidObjectBase.h"
#include "../../Actors/Enemies/EnemyBase.h"
#include "../../Actors/Weapons/ShotBase.h"
#include "../../PreferencesCache.h"
#include "../../Rendering/PlayerViewport.h"
#include "../../Tiles/TileMap.h"
#include "../../UI/Font.h"

#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		// Stands in for a bullet a script controls when it hits an object of the game, which handles only shots of the game
		// (crates, barrels, monitors) - never in the level, the bullet stops if it lost its health, turns if it ricocheted
		class ScriptShotProxy : public Actors::Weapons::ShotBase
		{
			DEATH_RUNTIME_OBJECT(Actors::Weapons::ShotBase);

		public:
			ScriptShotProxy(ILevelHandler* levelHandler, std::shared_ptr<Actors::ActorBase> owner, WeaponType weaponType,
				bool upgraded, std::int32_t strength, Vector2f pos)
				: _weaponType(weaponType)
			{
				_levelHandler = levelHandler;
				_owner = std::move(owner);
				_upgrades = (upgraded ? 0x01 : 0x00);
				_strength = strength;
				_health = 1;
				_pos = pos;
			}

			WeaponType GetWeaponType() override {
				return _weaponType;
			}

			bool Ricocheted = false;

		protected:
			void OnRicochet() override {
				Ricocheted = true;
			}
			bool OnPerish(ActorBase* collider) override {
				return true;
			}

		private:
			WeaponType _weaponType;
		};

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
			// Makes the object solid by its frame for this tick, as jjOBJ::beSolid() (see RunTick())
			void BeSolid() {
				_solidTick = _levelScripts->GetGameTick();
				SetState(Actors::ActorState::IsSolidObject | Actors::ActorState::SkipPerPixelCollisions | Actors::ActorState::IsDirty, true);
			}
			bool GetSolidBox(AABBf& box) const {
				return GetSolidObjectBox(_levelScripts, *_obj, box);
			}

		protected:
			void OnUpdateHitbox() override
			{
				AABBf box;
				if (GetState(Actors::ActorState::IsSolidObject) && GetSolidBox(box)) {
					AABBInner = box;
				}
			}

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
				OnUpdateHitbox();
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
			/** @brief Runs the behavior of the object for one tick of the scripts, the one it's made in isn't told to deactivate */
			void RunTick(bool firstTick = false)
			{
				// JJ2 counts it down for every object, the behaviors only set it
				if (_obj->justHit > 0) {
					_obj->justHit--;
				}
				if (!firstTick && ScriptObjectHost::IsOutOfActivationRange(_levelScripts, *_obj)) {
					_obj->state = sDEACTIVATE;
				}
				// The players collide with it before its behavior runs, as with the other objects (see ScriptObjectHost::TouchPlayersByBox())
				if (!firstTick) {
					ScriptObjectHost::TouchPlayers(_levelScripts, *_obj);
					if (GetState(Actors::ActorState::IsDestroyed)) {
						return;
					}
				}
				std::int8_t previousState = _obj->state;
				// A copy keeps the behavior alive while it runs, it may replace itself
				jjBEHAVIOR behavior = _obj->behavior;
				_drawnByBehavior = ScriptObjectHost::CallScriptBehavior(_levelScripts, *_obj, behavior);
				if (_solidTick != INT32_MIN && _solidTick < _levelScripts->GetGameTick()) {
					// The behavior doesn't make it solid any more
					_solidTick = INT32_MIN;
					SetState(Actors::ActorState::IsSolidObject, false);
				}
				// Moved up since the last tick by anything, the players on it go along
				AABBf solidBox;
				bool isSolid = (_solidTick != INT32_MIN && GetSolidBox(solidBox));
				if (isSolid && _liftValid && _obj->yPos < _liftY) {
					ScriptObjectHost::LiftStandingPlayers(_levelScripts, _liftBox, _obj->yPos - _liftY);
				}
				_liftValid = isSolid;
				if (isSolid) {
					_liftBox = solidBox;
					_liftY = _obj->yPos;
				}
				if (!_drawnByBehavior && _obj->state == sDEACTIVATE) {
					// Every behavior of the game deactivates an object told to
					_obj->deactivate();
					return;
				}
				if (!_drawnByBehavior) {
					// BEHAVIOR::DEFAULT, the behavior of the event, has the ID of the event in JJ2 (a shard flies off as a shard)
					constexpr std::uint32_t BehaviorDefault = UINT32_MAX;
					std::uint32_t behaviorId = behavior.behaviorId;
					if (behaviorId == BehaviorDefault && _obj->eventID == aBULLET) {
						// As measured in JJ2+, a bullet nobody gave a behavior is gone (see jjOBJ::jjAddObject())
						Remove();
						return;
					}
					if (behaviorId == BehaviorDefault && (_obj->eventID == aSHARD || _obj->eventID == aEXPLOSION)) {
						behaviorId = _obj->eventID;
					} else if (behaviorId == BehaviorDefault && _obj->eventID >= aPLAYERBULLET1 && _obj->eventID <= aPLAYERBULLETC3) {
						// A bullet of a weapon flies as a bullet, a flame of the toaster as one - also in the tick it's made in, so a
						// script that gives it another behavior next has it flying already (as measured in JJ2+, a dragon
						// breathes fire by speeds of its own)
						behaviorId = (_obj->eventID == aPLAYERBULLET6 || _obj->eventID == aPLAYERBULLETP6 ? aPLAYERBULLET6 :
							(_obj->eventID == aPLAYERBULLET2 || _obj->eventID == aPLAYERBULLETP2 ? aPLAYERBULLET2 : aBULLET));
					}
					switch (behaviorId) {
						case BehaviorPepperBullet:
						case aBULLET:
							RunBullet(true, behaviorId == BehaviorPepperBullet);
							_drawnByBehavior = true;
							break;
						case BehaviorBilsyBullet:
							RunBilsyBullet(true);
							_drawnByBehavior = true;
							break;
						case aPLAYERBULLET2:
							RunBouncerBullet(true);
							_drawnByBehavior = true;
							break;
						case aPLAYERBULLET6:
							RunToasterBullet(true);
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
						case aEXPLOSION:
							RunExplosion(true);
							_drawnByBehavior = true;
							break;
						case aCOUNT + 1:	// BEHAVIOR::WALKINGENEMY
							_obj->RunWalkingEnemy(_levelScripts, true);
							_drawnByBehavior = true;
							break;
						case aUNKNOWN:		// BEHAVIOR::INACTIVE
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
				HitEnemies();
				HitScenery(previousState);
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
						shardObj->creator = jjOBJ::PackCreator(0, std::uint16_t(obj.objectID));
					}
				}

				obj.state = sKILL;
				obj.behavior = BehaviorExplosion2;
				obj._bulletHandling = HandlingIgnoreBullet;
			}

			/**
			 * @brief Runs `BEHAVIOR::EXPLOSION` once, its `curAnim` plays once where it is and it's gone then
			 *
			 * A frame in 4 ticks and its state stays `STATE::START`, as measured in JJ2+.
			 */
			void RunExplosion(bool draw)
			{
				constexpr std::int32_t TicksPerFrame = 4;

				jjOBJ& obj = *_obj;
				auto& tables = _levelScripts->GetAnimationTables();
				std::uint32_t frameCount, firstFrame;
				if (obj.curAnim <= 0 || !tables.GetAnimationFrames(std::uint32_t(obj.curAnim), frameCount, firstFrame) || frameCount == 0) {
					// A new one from jjAddObject() runs before its script gives it the animation, it gets one more tick
					if (obj.state == sSTART && _explosionAnim == 0) {
						_explosionAnim = -1;
						return;
					}
					Remove();
					return;
				}
				if (obj.curAnim != _explosionAnim) {
					// From the start of the animation a script gave it, also after the first tick it ran in
					obj.counter = 0;
					_explosionAnim = obj.curAnim;
				}
				std::int32_t frame = obj.counter++ / TicksPerFrame;
				if (frame >= std::int32_t(frameCount)) {
					Remove();
					return;
				}
				obj.frameID = std::int8_t(frame);
				obj.curFrame = firstFrame + std::uint32_t(frame);
				if (draw) {
					ScriptObjectHost::DrawFrame(_levelScripts, obj);
				}
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
			/**
			 * @brief Runs `BEHAVIOR::BILSYBULLET` once, as measured in JJ2+
			 *
			 * It heads for the nearest player in every tick, `var[0]` and `var[1]` are the way in 1024 steps of a circle. The
			 * speed is in eighths of a pixel, from 0 it goes half the way to 31 in a tick (rounded up), 16 first - and starts lower
			 * again when it turns (5 after a turn of 58 degrees, taken as the speed along the new way, cubed, nothing else is
			 * measured). A frame in 4 ticks, it explodes in a wall and on a player (see HitPlayers()).
			 */
			void RunBilsyBullet(bool draw)
			{
				constexpr float MaxSpeed = 31.0f;
				constexpr float StartSpeed = 0.0f;

				jjOBJ& obj = *_obj;
				if (obj.state == sKILL || obj.state == sEXPLODE) {
					RunBullet(draw);
					return;
				}

				Vector2f target = Vector2f(obj.xPos, obj.yPos);
				float nearest = FLT_MAX;
				for (Actors::Player* player : _levelScripts->GetPlayers()) {
					float distance = (player->GetPos() - Vector2f(obj.xPos, obj.yPos)).SqrLength();
					if (distance < nearest) {
						nearest = distance;
						target = player->GetPos();
					}
				}
				float radians = std::atan2(target.Y - obj.yPos, target.X - obj.xPos);
				std::int32_t angle = std::int32_t(std::round(radians * 512.0f / fPi)) & 1023;

				float speed;
				if (obj.state == sSTART || (obj.get_var(0) == 0 && obj.get_var(1) == 0)) {
					speed = StartSpeed;
					obj.state = sFLY;
				} else {
					speed = std::sqrt(obj.xSpeed * obj.xSpeed + obj.ySpeed * obj.ySpeed) * 8.0f;
					if (angle != obj.get_var(1)) {
						float turn = std::cos((angle - obj.get_var(1)) * fPi / 512.0f);
						speed *= std::max(turn, 0.0f) * std::max(turn, 0.0f) * std::max(turn, 0.0f);
					}
				}
				speed = std::round(speed);
				speed = std::min(speed + std::ceil((MaxSpeed - speed) * 0.5f), MaxSpeed);
				obj.set_var(0, angle);
				obj.set_var(1, angle);
				obj.xSpeed = std::cos(radians) * speed / 8.0f;
				obj.ySpeed = std::sin(radians) * speed / 8.0f;

				if (IsMasked(obj.xPos + obj.xSpeed, obj.yPos + obj.ySpeed)) {
					obj.state = sEXPLODE;
					obj.counter = 1;
				} else {
					obj.xPos += obj.xSpeed;
					obj.yPos += obj.ySpeed;
					obj.counter = (obj.counter + 1) & 3;
					auto& tables = _levelScripts->GetAnimationTables();
					std::uint32_t frameCount, firstFrame;
					if (obj.curAnim > 0 && tables.GetAnimationFrames(std::uint32_t(obj.curAnim), frameCount, firstFrame) && frameCount > 0) {
						if (obj.counter == 0) {
							obj.frameID = std::int8_t((obj.frameID + 1) % std::int32_t(frameCount));
						}
						obj.curFrame = firstFrame + std::uint32_t(obj.frameID % std::int32_t(frameCount));
					}
				}

				if (draw && obj.curFrame != 0) {
					ScriptObjectHost::DrawFrame(_levelScripts, obj);
				}
			}

			/**
			 * @brief Runs `BEHAVIOR::TOASTERBULLET` once, as measured in JJ2+
			 *
			 * It turns the way a bullet does (see RunBullet()), but stays in that tick. It goes 6 pixels a tick at most, slowing
			 * down by 1/64 before it moves, and rises by 1/64 more in a tick while it flies left (to the right it doesn't). A
			 * frame in 8 ticks, the first one after it starts, it's gone when its animation ends (57 ticks, whatever its
			 * `counterEnd` is), and explodes in a wall.
			 */
			void RunToasterBullet(bool draw)
			{
				constexpr float MaxSpeed = 6.0f;
				constexpr float Slowdown = 1.0f / 64.0f;
				constexpr std::int32_t TicksPerFrame = 8;
				// Of the animation of the game, if it has none
				constexpr std::uint32_t DefaultFrameCount = 8;

				jjOBJ& obj = *_obj;
				if (obj.state == sKILL || obj.state == sEXPLODE) {
					// The kill and the explosion are those of a bullet
					RunBullet(draw);
					return;
				}
				if (obj.state == sSTART) {
					std::int8_t direction;
					float creatorSpeed;
					GetCreatorMotion(direction, creatorSpeed);
					obj.direction = (direction != 0 ? direction : 1);
					obj.xSpeed = obj.direction * obj.xSpeed + creatorSpeed;
					obj.state = sFLY;
					// It moves from the next tick on
					if (draw && obj.curFrame != 0) {
						ScriptObjectHost::DrawFrame(_levelScripts, obj);
					}
					return;
				}

				auto& tables = _levelScripts->GetAnimationTables();
				std::uint32_t frameCount = 0, firstFrame = 0;
				bool hasFrames = (obj.curAnim > 0 && tables.GetAnimationFrames(std::uint32_t(obj.curAnim), frameCount, firstFrame) && frameCount > 0);
				std::int32_t frame = obj.counter / TicksPerFrame + 1;
				if (frame >= std::int32_t(hasFrames ? frameCount : DefaultFrameCount)) {
					Remove();
					return;
				}
				if (IsMasked(obj.xPos, obj.yPos)) {
					obj.state = sEXPLODE;
					obj.counter = 1;
					RunBullet(draw);
					return;
				}
				obj.counter++;
				obj.xSpeed = (obj.xSpeed > 0.0f ? std::max(obj.xSpeed - Slowdown, 0.0f) : std::min(obj.xSpeed + Slowdown, 0.0f));
				obj.xSpeed = std::clamp(obj.xSpeed, -MaxSpeed, MaxSpeed);
				if (obj.xSpeed < 0.0f) {
					obj.ySpeed -= Slowdown;
				}
				obj.xPos += obj.xSpeed;
				obj.yPos += obj.ySpeed;

				if (hasFrames) {
					obj.frameID = std::int8_t(frame);
					obj.curFrame = firstFrame + std::uint32_t(frame);
				}
				if (draw && obj.curFrame != 0) {
					ScriptObjectHost::DrawFrame(_levelScripts, obj);
				}
			}

			/**
			 * @brief Runs `BEHAVIOR::BOUNCERBULLET` once
			 *
			 * As a bullet does (see RunBullet()), but it bounces off the walls and the ground instead of exploding in them, until
			 * its `counterEnd`.
			 */
			void RunBouncerBullet(bool draw)
			{
				jjOBJ& obj = *_obj;
				if (obj.state == sSTART || obj.state == sKILL || obj.state == sEXPLODE || obj.counter > obj.counterEnd) {
					RunBullet(draw);
					return;
				}

				obj.counter++;
				if (IsMasked(obj.xPos + obj.xSpeed, obj.yPos)) {
					obj.xSpeed = -obj.xSpeed;
					obj.xAcc = -obj.xAcc;
				} else {
					obj.xPos += obj.xSpeed;
				}
				obj.ySpeed += obj.yAcc;
				if (IsMasked(obj.xPos, obj.yPos + obj.ySpeed)) {
					obj.ySpeed = -obj.ySpeed;
				} else {
					obj.yPos += obj.ySpeed;
				}

				auto& tables = _levelScripts->GetAnimationTables();
				std::uint32_t frameCount, firstFrame;
				if (obj.curAnim > 0 && tables.GetAnimationFrames(std::uint32_t(obj.curAnim), frameCount, firstFrame) && frameCount > 0) {
					obj.frameID = std::int8_t((obj.counter / 4) % std::int32_t(frameCount));
					obj.curFrame = firstFrame + std::uint32_t(obj.frameID);
				}
				if (draw && obj.curFrame != 0) {
					ScriptObjectHost::DrawFrame(_levelScripts, obj);
				}
			}
			void RunBullet(bool draw, bool translucent = false)
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

				if (obj.state == sKILL) {
					// Killed by an attack of a player, as an enemy a script made of it (bees)
					Remove();
					return;
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
					// As measured in JJ2+, the pepper spray (FIREBALLBULLET) is drawn translucent
					translucent |= (obj.eventID == aPLAYERBULLET8 || obj.eventID == aPLAYERBULLETP8);
					ScriptObjectHost::DrawFrame(_levelScripts, obj, 0.0f, translucent);
				}
			}

		private:
			// BEHAVIOR::CRATE, BEHAVIOR::EXPLOSION2, BEHAVIOR::PEPPERBULLET, HANDLING::PARTICLE and HANDLING::IGNOREBULLET of JJ2+
			static constexpr std::uint32_t BehaviorCrate = aCOUNT + 9;
			static constexpr std::uint32_t BehaviorExplosion2 = aCOUNT + 21;
		public:
			static constexpr std::uint32_t BehaviorPepperBullet = aCOUNT + 29;
			static constexpr std::uint32_t BehaviorBilsyBullet = aCOUNT + 14;
		private:
			static constexpr std::uint32_t HandlingParticle = 3;
			static constexpr std::uint32_t HandlingIgnoreBullet = 1;

			LevelScriptLoader* _levelScripts;
			jjOBJ* _obj;
			std::uint32_t _lastAnimation = UINT32_MAX;
			// The next tick of the scripts the behavior runs in, -1 before the first one
			std::int32_t _nextTick = -1;
			// Ticks a piece of a broken crate is still there for (see RunShard())
			std::int32_t _ticksLeft = 0;
			// The animation an explosion plays (see RunExplosion())
			std::int16_t _explosionAnim = 0;
			// A behavior draws the object, its animation isn't drawn by itself
			bool _drawnByBehavior = false;
			// The tick the behavior made the object solid in (see BeSolid()), and its box then
			std::int32_t _solidTick = INT32_MIN;
			AABBf _liftBox;
			float _liftY = 0.0f;
			bool _liftValid = false;
			// What the bullet ricocheted off last, it stops if it does again (see HitEnemies())
			Actors::ActorBase* _lastRicochet = nullptr;
			// The tiles of scenery a bullet going through them broke already (see HitScenery())
			SmallVector<std::int32_t, 2> _hitSceneryTiles;

			// Returns the direction and the speed of who made the object, none if there's no such object or player
			void GetCreatorMotion(std::int8_t& direction, float& speed)
			{
				// CREATOR::Type of JJ2+
				constexpr std::uint32_t CreatorObject = 0;
				constexpr std::uint32_t CreatorPlayer = 0x8000;

				direction = 0;
				speed = 0.0f;
				if (_obj->get_creatorType() == CreatorPlayer) {
					for (Actors::Player* player : _levelScripts->GetPlayers()) {
						if (player->GetPlayerIndex() == std::int32_t(_obj->get_creatorID())) {
							direction = (player->IsFacingLeft() ? -1 : 1);
							speed = player->GetSpeed().X;
							break;
						}
					}
				} else if (_obj->get_creatorType() == CreatorObject && _obj->get_creatorID() > 0) {
					jjOBJ* creator = _levelScripts->GetObjectById(_obj->get_creatorID());
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
				// The pixel the position is in (a bullet at x=3807.3 isn't in a wall starting at 3808, as measured)
				x = float(std::int32_t(x));
				y = float(std::int32_t(y));
				Tiles::TileCollisionParams params = { Tiles::TileDestructType::SuspendTiles, true };
				return !tileMap->IsTileEmpty(AABBf(x, y, x, y), params);
			}

			// A bullet of an enemy (HANDLING::ENEMYBULLET) explodes on a player it touches and hurts the player by its
			// `animSpeed`, as measured in JJ2+ - unless the player can't be hurt then. It goes through a frozen player, which
			// it doesn't hurt either, it's after the behavior: ice bullets may freeze the player they touch and push it
			void HitPlayers()
			{
				constexpr std::uint32_t HandlingEnemy = 0;
				constexpr std::uint32_t HandlingEnemyBullet = 2;

				jjOBJ& obj = *_obj;
				bool isEnemy = (obj._playerHandlingSet && obj._playerHandling == HandlingEnemy);
				if ((obj._playerHandling != HandlingEnemyBullet && !isEnemy) || obj.state == sEXPLODE || obj.state == sKILL ||
					GetState(Actors::ActorState::IsDestroyed)) {
					return;
				}
				for (Actors::Player* player : _levelScripts->GetPlayers()) {
					if (player->GetHealth() <= 0 || player->IsFrozen() || !DoesObjectTouch(_levelScripts, obj, player->AABBInner)) {
						continue;
					}
					if (isEnemy) {
						// An enemy a script made of another object hurts a player who touches it, an attack hurts it instead, as
						// measured in JJ2+ (bees made of bullets), its behavior isn't told of it
						if (player->HasSugarRush() || player->GetSpecialMove() != Actors::Player::SpecialMoveType::None) {
							obj.energy = std::int8_t(std::max(obj.energy - 4, 0));
							if (obj.energy <= 0) {
								obj.state = sKILL;
							}
						} else if (!player->IsInvulnerable()) {
							player->TakeDamage(1, 4.0f * (player->GetPos().X > obj.xPos ? 1.0f : -1.0f));
						}
						break;
					}
					if (!player->IsInvulnerable()) {
						player->TakeDamage(std::max(obj.animSpeed, 1), 4.0f * (player->GetPos().X > obj.xPos ? 1.0f : -1.0f));
					}
					obj.state = sEXPLODE;
					break;
				}
			}

			// A bullet of a player (HANDLING::PLAYERBULLET) a script made hits what a shot of the game hits, by its `animSpeed`
			// (the copies a weapon makes of its bullets) - an enemy, a crate, a barrel or a monitor as by the shot of its
			// weapon, an object a script gave a `bulletHandling` or `scriptedCollisions` as the script says
			void HitEnemies()
			{
				constexpr std::uint32_t HandlingPlayerBullet = 1;
				constexpr std::uint32_t HandlingSpecial = 8;
				constexpr std::uint32_t HandlingDestroyBullet = 2;
				constexpr std::uint32_t CreatorPlayer = 0x8000;

				jjOBJ& obj = *_obj;
				if (obj._playerHandling != HandlingPlayerBullet || obj.get_creatorType() != CreatorPlayer || obj.state == sEXPLODE ||
					obj.state == sKILL || GetState(Actors::ActorState::IsDestroyed)) {
					return;
				}
				constexpr float Reach = 32.0f;
				AABBf box(obj.xPos - Reach, obj.yPos - Reach, obj.xPos + Reach, obj.yPos + Reach);
				SmallVector<Actors::ActorBase*, 4> touched;
				_levelHandler->FindCollisionActorsByAABB(this, box, [this, &obj, &touched](Actors::ActorBase* other) {
					if (other->GetHealth() > 0 && runtime_cast<Actors::Player>(other) == nullptr &&
						runtime_cast<Actors::Weapons::ShotBase>(other) == nullptr && runtime_cast<ScriptLegacyObject>(other) == nullptr &&
						DoesObjectTouch(_levelScripts, obj, other->AABBInner)) {
						touched.push_back(other);
					}
					return true;
				});

				std::int32_t strength = std::max<std::int32_t>(obj.animSpeed, 1);
				Actors::Player* owner = nullptr;
				for (Actors::Player* player : _levelScripts->GetPlayers()) {
					if (player->GetPlayerIndex() == std::int32_t(obj.get_creatorID())) {
						owner = player;
						break;
					}
				}

				for (Actors::ActorBase* other : touched) {
					if (other->GetState(Actors::ActorState::IsDestroyed) || other->GetHealth() <= 0) {
						continue;
					}
					jjOBJ* target = _levelScripts->GetObjectView(other);
					if (target != nullptr && target->_bulletHandlingSet) {
						if (target->_bulletHandling == HandlingIgnoreBullet) {
							continue;
						}
						if (target->_bulletHandling == HandlingDestroyBullet) {
							obj.state = sEXPLODE;
							break;
						}
					}
					if (target != nullptr && target->_scriptedCollisions && (target->_playerHandlingSet
						? target->_playerHandling == HandlingSpecial : runtime_cast<Actors::Collectibles::CollectibleBase>(other) == nullptr)) {
						jjPLAYER* p = (owner != nullptr ? _levelScripts->GetPlayerBackingStore(owner) : nullptr);
						ScriptObjectHost::CallObjectHit(_levelScripts, *target, &obj, p, strength);
						if (obj.state == sEXPLODE) {
							break;
						}
						continue;
					}
					if (auto* enemy = runtime_cast<Actors::Enemies::EnemyBase>(other)) {
						if (enemy->CanCauseDamage(this)) {
							enemy->DecreaseHealth(strength, this);
							obj.state = sEXPLODE;
							break;
						}
						continue;
					}

					auto proxy = std::make_shared<ScriptShotProxy>(_levelHandler, owner != nullptr ? owner->shared_from_this() : nullptr,
						GetBulletWeaponType(obj.eventID), IsUpgradedBullet(obj.eventID), strength, _pos);
					if (!Actors::ActorBase::HandleCollisionByOverride(other, proxy.get())) {
						other->OnHandleCollision(proxy.get());
					}
					if (proxy->GetHealth() <= 0 || (proxy->Ricocheted && _lastRicochet == other)) {
						obj.state = sEXPLODE;
						break;
					}
					if (proxy->Ricocheted) {
						_lastRicochet = other;
						obj.xSpeed = -obj.xSpeed;
						obj.xAcc = -obj.xAcc;
						obj.direction = std::int8_t(-obj.direction);
						break;
					}
				}
			}

			// A bullet of a player a script made breaks scenery as the shot of its weapon, in the tick it was still flying in
			// (see ScriptObjectHost::BreakScenery()) - a bullet with `var[6] & 16` goes through it (a shockwave of a sword)
			void HitScenery(std::int8_t previousState)
			{
				constexpr std::uint32_t HandlingPlayerBullet = 1;
				constexpr std::uint32_t CreatorPlayer = 0x8000;

				jjOBJ& obj = *_obj;
				if (obj._playerHandling != HandlingPlayerBullet || obj.get_creatorType() != CreatorPlayer || previousState == sEXPLODE ||
					previousState == sKILL || GetState(Actors::ActorState::IsDestroyed)) {
					return;
				}
				bool goesThrough = ((obj.get_var(6) & 16) != 0);
				if (ScriptObjectHost::BreakScenery(_levelScripts, *this, obj, GetBulletWeaponType(obj.eventID), std::max<std::int32_t>(obj.animSpeed, 1),
						goesThrough ? &_hitSceneryTiles : nullptr) && !goesThrough && obj.state != sEXPLODE) {
					obj.state = sEXPLODE;
					obj.counter = 1;
				}
			}

			// The weapon a bullet of a player is of by its event, the one its shot of the game would be
			static WeaponType GetBulletWeaponType(std::uint8_t eventID)
			{
				std::uint8_t index = (eventID >= aPLAYERBULLETP1 && eventID <= aPLAYERBULLETP9 ? std::uint8_t(eventID - aPLAYERBULLETP1 + aPLAYERBULLET1) : eventID);
				switch (index) {
					case aPLAYERBULLET2: return WeaponType::Bouncer;
					case aPLAYERBULLET3: return WeaponType::Freezer;
					case aPLAYERBULLET4: return WeaponType::Seeker;
					case aPLAYERBULLET5: return WeaponType::RF;
					case aPLAYERBULLET6: return WeaponType::Toaster;
					case aPLAYERBULLET8: return WeaponType::Pepper;
					case aPLAYERBULLET9: return WeaponType::Electro;
					default: return WeaponType::Blaster;
				}
			}

			static bool IsUpgradedBullet(std::uint8_t eventID)
			{
				return (eventID >= aPLAYERBULLETP1 && eventID <= aPLAYERBULLETP9);
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
			// Active while its object exists, the one a script controls or the one of the game - a deleted one is gone at once, as
			// in JJ2+, the object of the game is removed only after the frame
			if (_wrapper != nullptr) {
				return true;
			}
			std::shared_ptr<Actors::ActorBase> actor = _actor.lock();
			return (actor != nullptr && !actor->GetState(Actors::ActorState::IsDestroyed));
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
			// Hurts the players nearer than the square root of `maxDistance` (bombs pass 100*100), pushed
			// away from it - what it does to objects (`blastObjects`) isn't done
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			for (Actors::Player* player : owner->GetPlayers()) {
				Vector2f diff = player->GetPos() - Vector2f(xPos, yPos);
				if (player->GetHealth() > 0 && diff.SqrLength() < float(maxDistance) && !player->IsInvulnerable()) {
					player->TakeDamage(1, 4.0f * (diff.X >= 0.0f ? 1.0f : -1.0f));
				}
			}
		}

		void jjOBJ::RunWalkingEnemy(LevelScriptLoader* owner, bool draw) {
			// BEHAVIOR::WALKINGENEMY, as measured in JJ2+: the row of the cold spot of its frame stands on the ground, without
			// it the object falls (0.125 a tick); it goes by xSpeed and faces the way it goes, it turns round on the spot if
			// at the front edge of its frame (width + hot spot X beyond where it'd go) the feet row isn't masked or the row
			// 16 px above it is (it turns every tick in the air); its frames go every 8 ticks of the game
			constexpr float Gravity = 0.125f;
			constexpr float MaxFallSpeed = 8.0f;
			constexpr std::int32_t WallHeight = 16;

			// Also run outside of the scripts, as the object's own behavior
			auto* tileMap = owner->GetLevelHandler()->TileMap();
			auto isMasked = [tileMap](std::int32_t x, std::int32_t y) {
				Tiles::TileCollisionParams params = { Tiles::TileDestructType::SuspendTiles, true };
				return (tileMap != nullptr && !tileMap->IsTileEmpty(AABBf(float(x), float(y), float(x), float(y)), params));
			};
			auto& tables = owner->GetAnimationTables();
			std::int32_t feet = 0, frontEdge = 0;
			Recti frameRect; Vector2i coldSpot;
			if (curFrame != 0 && tables.GetFrameSpots(curFrame, frameRect, coldSpot)) {
				feet = frameRect.Y - coldSpot.Y;
				frontEdge = frameRect.W + frameRect.X;
			}

			if (state == sDEACTIVATE) {
				deactivate();
				return;
			}
			if (state == sKILL) {
				// Killed, it bursts into the pixels of its frame and is gone, as the enemies of JJ2 die
				if (curFrame != 0) {
					AddPixelExplosion(owner, xPos, yPos, curFrame, direction);
				}
				if (_wrapper != nullptr) {
					_wrapper->Remove();
				} else if (_host != nullptr) {
					_host->Delete();
				}
				return;
			}
			if (state == sSTART) {
				state = sWALK;
				yAcc = (isMasked(std::int32_t(xPos), std::int32_t(yPos) + feet) ? 0.0f : Gravity);
				// Placed in the ground, it starts on it
				for (std::int32_t i = 0; yAcc == 0.0f && i < 8 && isMasked(std::int32_t(xPos), std::int32_t(yPos) + feet - 1); i++) {
					yPos -= 1.0f;
				}
			} else if (state == sFREEZE) {
				if (freeze > 0) {
					freeze--;
				}
				if (freeze == 0) {
					state = oldState;
				}
			} else if (state == sWALK) {
				yPos += ySpeed;
				if (isMasked(std::int32_t(xPos), std::int32_t(yPos) + feet)) {
					// A fall ends where it got to, also deeper in the ground (measured: a clown landing 5 px deep in a thin floor)
					ySpeed = 0.0f;
					yAcc = 0.0f;
				} else {
					// The acceleration it had, gravity from the next tick on (measured: walking off a ledge it falls a tick later)
					ySpeed = std::min(ySpeed + yAcc, MaxFallSpeed);
					yAcc = Gravity;
				}

				float targetX = xPos + xSpeed;
				std::int32_t way = (xSpeed < 0.0f ? -1 : 1);
				std::int32_t feetRow = std::int32_t(yPos) + feet;
				std::int32_t frontX = std::int32_t(targetX) + way * frontEdge;
				if (!isMasked(frontX, feetRow) || isMasked(frontX, feetRow - WallHeight)) {
					xSpeed = -xSpeed;
				} else {
					xPos = targetX;
				}
				if (xSpeed != 0.0f) {
					direction = std::int8_t(xSpeed < 0.0f ? -1 : 1);
				}
			}

			if ((owner->GetGameTick() & 7) == 0) {
				frameID++;
			}
			std::uint32_t frameCount, firstFrame;
			if (curAnim > 0 && tables.GetAnimationFrames(std::uint32_t(curAnim), frameCount, firstFrame) && frameCount > 0) {
				frameID = std::int8_t(std::uint32_t(frameID) % frameCount);
				curFrame = firstFrame + std::uint32_t(frameID);
			}

			if (_host != nullptr) {
				_host->FollowScriptMovement();
			}
			if (draw && curFrame != 0) {
				ScriptObjectHost::DrawFrame(owner, *this);
			}
		}

		void jjOBJ::behave1(std::uint32_t behavior, bool draw) {
			// BEHAVIOR::DEFAULT is the behavior the object has (`obj.behave()` of JJ2+ runs a behavior of the script given to
			// it, puppets start so) - but not again from within itself, the one of the game runs then
			constexpr std::uint32_t BehaviorDefault = UINT32_MAX;
			if (behavior == BehaviorDefault) {
				const void* own = (this->behavior.object != nullptr ? static_cast<const void*>(this->behavior.object) : static_cast<const void*>(this->behavior.function));
				if (own != nullptr && own != _runningBehavior) {
					jjBEHAVIOR ownBehavior = this->behavior;
					ScriptObjectHost::CallScriptBehavior(ScriptLoader::FromActiveContext<LevelScriptLoader>(), *this, ownBehavior, draw);
					return;
				}
				if (own == nullptr) {
					behavior = this->behavior.behaviorId;
				}
			}
			if (_host == nullptr && _wrapper == nullptr && behavior != aUNKNOWN) {
				ScriptLoader::FromActiveContext<LevelScriptLoader>()->OnGameBehavior(*this, behavior);
			}
			// Runs a behavior once, it doesn't become the behavior of the object. A behavior of the game is the object's own
			// update, the object has no other; an object a script controls entirely can be a bullet.
			// BEHAVIOR::WALKINGENEMY of JJ2+, which the game has only in the objects of its own enemies
			constexpr std::uint32_t BehaviorWalkingEnemy = aCOUNT + 1;
			if (behavior == BehaviorWalkingEnemy && (_host != nullptr || _wrapper != nullptr)) {
				RunWalkingEnemy(ScriptLoader::FromActiveContext<LevelScriptLoader>(), draw);
				return;
			}
			if (_host != nullptr) {
				// BEHAVIOR::MONITOR of JJ2+
				constexpr std::uint32_t BehaviorMonitor = aCOUNT + 8;
				if (behavior == aUNKNOWN) {
					// BEHAVIOR::INACTIVE
					_host->Delete();
				} else if (behavior == aBULLET && _host->IsScriptedBullet()) {
					_host->RunBullet(draw);
				} else if (behavior == BehaviorMonitor) {
					_host->RunMonitor(draw);
				} else {
					_host->RunGameBehavior(behavior, draw);
				}
			} else if (_wrapper != nullptr) {
				if (state == sDEACTIVATE) {
					// Every behavior of the game deactivates an object told to
					deactivate();
					return;
				}
				// BEHAVIOR::CRATE of JJ2+
				constexpr std::uint32_t BehaviorCrate = aCOUNT + 9;
				switch (behavior) {
					case aUNKNOWN: _wrapper->Remove(); break;
					case ScriptLegacyObject::BehaviorPepperBullet: _wrapper->RunBullet(draw, true); break;
					case aBULLET:
					// The other bullets of the weapons go as a bullet does, a bouncer bounces (snowballs)
					case aPLAYERBULLET1:
					case aPLAYERBULLET3:
					case aPLAYERBULLET4:
					case aPLAYERBULLET5:
					case aPLAYERBULLET9:
					case aPLAYERBULLETC1:
					case aPLAYERBULLETC2:
					case aPLAYERBULLETC3: _wrapper->RunBullet(draw); break;
					case aPLAYERBULLET2: _wrapper->RunBouncerBullet(draw); break;
					case ScriptLegacyObject::BehaviorBilsyBullet: _wrapper->RunBilsyBullet(draw); break;
					case aPLAYERBULLET6: _wrapper->RunToasterBullet(draw); break;
					case aSHARD: _wrapper->RunShard(draw); break;
					case aEXPLOSION: _wrapper->RunExplosion(draw); break;
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
			// The funcdef handle comes with a reference that is this function's to release - the temporary takes it over
			jjBEHAVIOR function;
			function.function = behavior;
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			ScriptObjectHost::CallScriptBehavior(owner, *this, function, draw);
		}

		std::int32_t jjOBJ::jjAddObject(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, std::uint32_t behavior) {
			// The object of the game the event has, with its creator, which its behavior may look at (an object that spawns
			// one of its own kind tells the copy by it, or the copies spawn more forever)
			constexpr std::uint32_t BehaviorDefault = UINT32_MAX;
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			std::int32_t id = owner->AddObjectFromEvent(eventID, xPixel, yPixel);
			if (id > 0) {
				jjOBJ* obj = owner->GetObjectById(id);
				obj->creator = PackCreator(creatorType, creatorID);
				if (behavior != BehaviorDefault) {
					obj->behavior = behavior;
				}
				obj->Release();
				owner->RunNewObjectBehavior(id);
				return id;
			}

			// The event has no object in the game (OBJECT::SHARD, OBJECT::EXPLOSION...), a script-controlled one stands in, as
			// JJ2+ makes every object a script asks for
			id = owner->AddScriptControlledObject(eventID, xPixel, yPixel, nullptr);
			if (auto* wrapper = runtime_cast<ScriptLegacyObject>(owner->GetScriptObject(id))) {
				jjOBJ* obj = wrapper->GetObject();
				obj->creator = PackCreator(creatorType, creatorID);
				if (behavior != BehaviorDefault) {
					obj->behavior = behavior;
				}
				// BEHAVIOR::INACTIVE keeps the object as it is until the script gives it a behavior (pulses of
				// trigger crates), it would be gone at once - so does a bullet, as measured in JJ2+, its behavior does
				// nothing then (fire given the homing behavior next)
				if (behavior != aUNKNOWN && !(behavior == BehaviorDefault && eventID == aBULLET)) {
					wrapper->RunTick(true);
				}
			}
			return id;
		}
		std::int32_t jjOBJ::jjAddObjectEx(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, jjVOIDFUNCOBJ behavior) {
			// The funcdef handle comes with a reference that is this function's to release - the temporary takes it over
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			jjBEHAVIOR function;
			function.function = behavior;

			// The object of the game the event has, driven by the behavior, so the behavior can run the object's own one
			std::int32_t id = owner->AddObjectFromEvent(eventID, xPixel, yPixel);
			if (id > 0) {
				jjOBJ* obj = owner->GetObjectById(id);
				obj->behavior = function;
				obj->creator = PackCreator(creatorType, creatorID);
				obj->Release();
				owner->RunNewObjectBehavior(id);
				return id;
			}

			// The event has no object in the game, a script-controlled one stands in
			id = owner->AddScriptControlledObject(eventID, xPixel, yPixel, function.function);
			// Record the creator on the persistent jjOBJ so scripts can read it back
			if (auto* wrapper = runtime_cast<ScriptLegacyObject>(owner->GetScriptObject(id))) {
				jjOBJ* obj = wrapper->GetObject();
				obj->creator = PackCreator(creatorType, creatorID);
				// The behavior runs once right away, as JJ2+ runs it for a new object - a new bullet starts to fly in it
				wrapper->RunTick(true);
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

		// CREATOR::LEVEL 0x4000 and CREATOR::PLAYER 0x8000 over the lower 14 bits of the ID, its 2 upper bits above them -
		// scripts keep any 16-bit value there (puppet shards keep frames)
		std::int32_t jjOBJ::PackCreator(std::uint32_t type, std::uint16_t id) {
			return std::int32_t((type & 0xC000u) | (id & 0x3FFFu) | (std::uint32_t(id & 0xC000u) << 2));
		}
		std::uint16_t jjOBJ::get_creatorID() const {
			return std::uint16_t((std::uint32_t(creator) & 0x3FFFu) | ((std::uint32_t(creator) >> 2) & 0xC000u));
		}
		std::uint16_t jjOBJ::set_creatorID(std::uint16_t value) const {
			creator = PackCreator(get_creatorType(), value);
			return value;
		}
		std::uint32_t jjOBJ::get_creatorType() const {
			return std::uint32_t(creator) & 0xC000u;
		}
		std::uint32_t jjOBJ::set_creatorType(std::uint32_t value) const {
			creator = PackCreator(value, get_creatorID());
			return get_creatorType();
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
			} else if (curFrame != 0) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				owner->OnFrameDrawn(xPos, yPos, curFrame, spriteType_NORMAL);
			}
			return 0;
		}
		std::int32_t jjOBJ::beSolid(bool shouldCheckForStompingLocalPlayers) {
			// As measured in JJ2+: -1/1 while a player holds a direction against it, `platform` names it while pushed or stood on
			std::shared_ptr<Actors::ActorBase> hosted;
			Actors::ActorBase* actor = nullptr;
			if (_wrapper != nullptr) {
				_wrapper->BeSolid();
				actor = _wrapper;
			} else if (_host != nullptr && (hosted = _actor.lock()) != nullptr) {
				_host->BeSolid();
				actor = hosted.get();
			}
			std::int32_t pushDir = 0;
			if (actor == nullptr || !actor->GetState(Actors::ActorState::IsSolidObject) || actor->GetState(Actors::ActorState::IsDestroyed)) {
				return pushDir;
			}
			if (auto* solidObject = runtime_cast<Actors::SolidObjectBase>(actor)) {
				solidObject->Movable = false;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			LevelHandler* levelHandler = owner->GetLevelHandler();
			constexpr float Reach = 2.0f;
			std::int32_t tick = owner->GetGameTick();
			const AABBf box = actor->AABBInner;
			for (Actors::Player* player : owner->GetPlayers()) {
				const AABBf& p = player->AABBInner;
				if (player->GetHealth() <= 0) {
					continue;
				}
				// Where the player is now, a script may have moved it since its hitbox was made
				float centerX = player->GetPos().X;
				float halfWidth = (p.R - p.L) * 0.5f;
				if (centerX + halfWidth > box.L && centerX - halfWidth < box.R && p.B > box.T + Reach && p.T < box.B) {
					// A player inside its side is put out of it, as measured in JJ2+ (the script moves a pusher into it), not touching
					// it, or it couldn't fall along it
					constexpr float Gap = 0.1f;
					float deltaX = (centerX < (box.L + box.R) * 0.5f ? box.L - Gap - (centerX + halfWidth) : box.R + Gap - (centerX - halfWidth));
					AABBf moved(centerX - halfWidth + deltaX, p.T, centerX + halfWidth + deltaX, p.B);
					auto* tiles = levelHandler->TileMap();
					Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, false };
					if (tiles == nullptr || tiles->IsTileEmpty(moved, params)) {
						player->MoveInstantly(Vector2f(centerX + deltaX, player->GetPos().Y), Actors::MoveType::Absolute | Actors::MoveType::Force);
						centerX += deltaX;
					}
				}
				float left = centerX - halfWidth, right = centerX + halfWidth;
				bool overX = (right > box.L && left < box.R);
				std::int32_t dir = 0;
				bool standing = false;
				if (p.B > box.T + Reach && p.T < box.B) {
					if (std::abs(left - box.R) <= Reach && levelHandler->PlayerActionPressed(player, PlayerAction::Left)) {
						dir = -1;
					} else if (std::abs(right - box.L) <= Reach && levelHandler->PlayerActionPressed(player, PlayerAction::Right)) {
						dir = 1;
					}
				} else if (overX && p.B >= box.T - Reach && player->GetSpeed().Y >= 0.0f) {
					standing = true;
				}
				// Standing on it, or landing on it in this tick
				bool landing = (overX && p.B <= box.T + Reach && player->GetSpeed().Y > 0.0f && p.B + player->GetSpeed().Y + Reach >= box.T);
				jjPLAYER* view = (dir != 0 || standing || landing ? owner->GetPlayerBackingStore(player) : nullptr);
				if (view != nullptr && (dir != 0 || standing)) {
					view->_pushedObject = std::int32_t(objectID);
					view->_pushedTick = tick;
					view->platform = view->_pushedObject;
				}
				if (dir != 0 && pushDir == 0) {
					pushDir = dir;
				}
				if (standing || landing) {
					// As measured in JJ2+, it's touched in every tick a player stands on it, an attack while landing by a buttstomp
					ScriptObjectHost::TouchStandingPlayer(owner, *this, *player);
				}
				// A buttstomp onto it is one more hit (force 1) for a behavior that collides itself, in the tick it touches it
				if (shouldCheckForStompingLocalPlayers && _scriptedCollisions && overX && _stompTouchTick == tick &&
					player->GetSpecialMove() == Actors::Player::SpecialMoveType::Buttstomp && _stompHitTick < tick - 35) {
					_stompHitTick = tick;
					ScriptObjectHost::CallObjectHit(owner, *this, nullptr, (view != nullptr ? view : owner->GetPlayerBackingStore(player)), 1);
				}
			}
			return pushDir;
		}
		void jjOBJ::bePlatform(float xOld, float yOld, std::int32_t width, std::int32_t height) {
			// Only an object of the game can carry a player, one the script controls entirely has none
			if (_host != nullptr) {
				_host->BePlatform(xOld, yOld, width);
			}
		}
		void jjOBJ::clearPlatform() {
			if (_host != nullptr) {
				_host->ClearPlatform();
			}
		}
		void jjOBJ::putOnGround(bool precise) {
			// As measured in JJ2+, in the column of the middle of the frame: the precise mode rests the row of the coldspot (at
			// most the last row of the frame) on the topmost masked pixel under it - up out of the ground by 15 px at most, down
			// to it if it's less than 50 px below - and lowers the frame in 4 px steps until the pixel below it is masked then,
			// the coarse mode lowers the last row in 4 px steps until it's masked and doesn't lift an object out of the ground
			constexpr std::int32_t MaxSearch = 512;
			constexpr std::int32_t MaxPreciseRise = 15;
			constexpr std::int32_t MaxPreciseDrop = 50;

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			Recti frameRect; Vector2i coldSpot;
			if (curFrame == 0 || !owner->GetAnimationTables().GetFrameSpots(curFrame, frameRect, coldSpot)) {
				frameRect = Recti(0, 0, 1, 1);
				coldSpot = Vector2i::Zero;
			}
			std::int32_t x = std::int32_t(xPos) + frameRect.X + frameRect.W / 2;
			std::int32_t lastY = frameRect.Y + std::max(frameRect.H, 1) - 1;

			std::int32_t y = std::int32_t(yPos);
			if (precise) {
				std::int32_t offset = std::min(frameRect.Y - coldSpot.Y, lastY);
				if (jjMaskedPixel(x, y + offset)) {
					for (std::int32_t i = 0; i < MaxPreciseRise && jjMaskedPixel(x, y + offset - 1); i++) {
						y--;
					}
				} else {
					std::int32_t distance = jjMaskedTopVLine(x, y + offset, MaxPreciseDrop) - 1;
					if (distance < MaxPreciseDrop) {
						y += distance;
					}
				}
				for (std::int32_t searched = 0; searched < MaxSearch && !jjMaskedPixel(x, y + lastY + 1); searched += 4) {
					y += 4;
				}
			} else {
				for (std::int32_t searched = 0; searched < MaxSearch && !jjMaskedPixel(x, y + lastY); searched += 4) {
					y += 4;
				}
			}
			yPos += float(y - std::int32_t(yPos));
			ySpeed = 0.0f;
		}
		bool jjOBJ::ricochet() {
			// A shot of the game turns around as off a wall, an object of a script isn't one
			std::shared_ptr<Actors::ActorBase> actor = _actor.lock();
			auto* shot = runtime_cast<Actors::Weapons::ShotBase>(actor.get());
			if (shot == nullptr) {
				return false;
			}
			shot->TriggerRicochet(nullptr);
			return true;
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
				// Only an object of the level comes back, as in JJ2+
				constexpr std::uint32_t CreatorLevel = 0x4000;
				if (get_creatorType() == CreatorLevel) {
					ScriptObjectHost::DeactivateActor(*actor);
				} else {
					ScriptObjectHost::DeleteActor(*actor);
				}
			}
			behavior = aUNKNOWN;
		}
		void jjOBJ::pathMovement() {
			noop();
		}
		int32_t jjOBJ::fireBullet(std::uint8_t eventID) {
			// As measured in JJ2+, an object of the event at the gun spot of the frame this one shows (hot spot less gun spot, a
			// dragon breathes fire from its mouth), made by it - the speed of its template goes the way this one faces (see
			// ScriptLegacyObject::RunBullet()), its handling is the one of the template
			constexpr std::uint32_t CreatorObject = 0;
			constexpr std::uint32_t BehaviorDefault = UINT32_MAX;
			float x = xPos, y = yPos;
			if (curFrame != 0) {
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				if (jjANIMFRAME* frame = owner->GetAnimationTables().GetFrame(curFrame)) {
					float offsetX = float(frame->hotSpotX - frame->gunSpotX);
					x += (direction < 0 ? -offsetX : offsetX);
					y += float(frame->hotSpotY - frame->gunSpotY);
					frame->Release();
				}
			}
			return jjAddObject(eventID, x, y, std::uint16_t(objectID), CreatorObject, BehaviorDefault);
		}
		void jjOBJ::particlePixelExplosion(std::int32_t style) {
			AddPixelExplosion(ScriptLoader::FromActiveContext<LevelScriptLoader>(), xPos, yPos, curFrame, direction);
		}
		void jjOBJ::AddPixelExplosion(LevelScriptLoader* owner, float xPixel, float yPixel, std::uint32_t frame, std::int32_t direction) {
			// As measured in JJ2+: a pixel particle for every 16 pixels of the frame, at a random pixel of it (none where that one
			// is transparent), 1 to 3 px big (3 px half of the time) with the colors of the frame there, flying up - the styles of
			// JJ2+ (colored or burning ones) aren't told apart
			jjANIMFRAME* animFrame = (frame != 0 ? owner->GetAnimationTables().GetFrame(frame) : nullptr);
			if (animFrame == nullptr) {
				return;
			}
			std::int32_t width = animFrame->width, height = animFrame->height;
			float left = (direction < 0 ? xPixel - float(animFrame->hotSpotX) - float(width) : xPixel + float(animFrame->hotSpotX));
			float top = yPixel + float(animFrame->hotSpotY);
			jjPIXELMAP* image = jjPIXELMAP::CreateFromFrame(animFrame, nullptr);
			if (image == nullptr) {
				return;
			}
			auto colorAt = [&](std::int32_t x, std::int32_t y) -> std::uint8_t {
				if (x < 0 || y < 0 || x >= width || y >= height) {
					return 0;
				}
				std::uint8_t* pixel = image->GetPixel(std::uint32_t(direction < 0 ? width - 1 - x : x), std::uint32_t(y));
				return (pixel != nullptr ? *pixel : 0);
			};
			std::int32_t count = (width * height) / 16;
			for (std::int32_t i = 0; i < count; i++) {
				std::int32_t x = Random().Next(0, std::max(width, 1));
				std::int32_t y = Random().Next(0, std::max(height, 1));
				if (colorAt(x, y) == 0) {
					continue;
				}
				jjPARTICLE* particle = owner->AddScriptParticle(particlePIXEL);
				if (particle == nullptr) {
					break;
				}
				std::int32_t size = std::min<std::int32_t>(std::int32_t(Random().Next(0, 4)), 2);
				particle->xPos = left + float(x);
				particle->yPos = top + float(y);
				particle->xSpeed = Random().FastFloat(-2.0f, 2.0f);
				particle->ySpeed = Random().FastFloat(-6.0f, -2.0f);
				std::uint8_t* data = particle->GENERIC.raw;
				data[0] = std::uint8_t(size);
				for (std::int32_t py = 0; py <= size; py++) {
					for (std::int32_t px = 0; px <= size; px++) {
						data[1 + py * (size + 1) + px] = colorAt(x + px, y + py);
					}
				}
			}
			image->Release();
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
			// By the pixels of the frames the two show, as JJ2 has bullets hit objects - a single pixel is enough for a bullet
			constexpr std::uint32_t HandlingPlayerBullet = 1;
			constexpr std::uint32_t HandlingEnemyBullet = 2;
			bool result = false;
			if (object != nullptr && curFrame != 0 && object->curFrame != 0) {
				auto& tables = ScriptLoader::FromActiveContext<LevelScriptLoader>()->GetAnimationTables();
				jjANIMFRAME* frame = tables.GetFrame(curFrame);
				jjANIMFRAME* otherFrame = (frame != nullptr ? tables.GetFrame(object->curFrame) : nullptr);
				if (otherFrame != nullptr) {
					bool bullet = (_playerHandling == HandlingPlayerBullet || _playerHandling == HandlingEnemyBullet ||
						object->_playerHandling == HandlingPlayerBullet || object->_playerHandling == HandlingEnemyBullet);
					result = tables.DoFramesCollide(*frame, std::int32_t(xPos), std::int32_t(yPos), direction, *otherFrame,
						std::int32_t(object->xPos), std::int32_t(object->yPos), object->direction, always || bullet);
					otherFrame->Release();
				}
				if (frame != nullptr) {
					frame->Release();
				}
			}
			if (object != nullptr) {
				// A handle passed to a native function is the function's to release
				const_cast<jjOBJ*>(object)->Release();
			}
			return result;
		}
		bool jjOBJ::doesCollidePlayer(const jjPLAYER* object, bool always) const {
			// The pixels of the frame the object shows against those of the player
			if (object == nullptr || object->_player == nullptr) {
				return false;
			}
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return DoesObjectTouchPlayer(owner, *this, *object->_player);
		}

		// The data of a particle of a type is in its 9 bytes, at the offsets RegisterLegacyParticles() gives the fields - the
		// colors of a pixel follow its size, the text of a string fills them
		constexpr std::size_t ParticleDataSize = 9;

		std::uint8_t jjPARTICLEPIXEL::get_color(std::int32_t i) const {
			const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(this);
			return (i >= 0 && i < std::int32_t(ParticleDataSize - 1) ? data[1 + i] : 0);
		}
		std::uint8_t jjPARTICLEPIXEL::set_color(std::int32_t i, std::uint8_t value) {
			std::uint8_t* data = reinterpret_cast<std::uint8_t*>(this);
			if (i >= 0 && i < std::int32_t(ParticleDataSize - 1)) {
				data[1 + i] = value;
			}
			return value;
		}

		String jjPARTICLESTRING::get_text() const {
			const char* data = reinterpret_cast<const char*>(this);
			std::size_t length = 0;
			while (length < ParticleDataSize && data[length] != '\0') {
				length++;
			}
			return String(data, length);
		}
		void jjPARTICLESTRING::set_text(String text) {
			char* data = reinterpret_cast<char*>(this);
			std::size_t length = std::min(text.size(), ParticleDataSize);
			std::memcpy(data, text.data(), length);
			std::memset(data + length, 0, ParticleDataSize - length);
		}

		jjPARTICLE::jjPARTICLE()
			: xPos(0.0f), yPos(0.0f), xSpeed(0.0f), ySpeed(0.0f), particleType(particleNONE), active(false), GENERIC{},
				_refCount(1), _age(0), _scale(0.0f), _weather(false), _snowFrameBase(0) {
		}
		jjPARTICLE::~jjPARTICLE() {
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
		jjOBJ::AddPixelExplosion(ScriptLoader::FromActiveContext<LevelScriptLoader>(), xPixel, yPixel, std::uint32_t(curFrame), direction);
	}

	jjPARTICLE* GetParticle(std::int32_t index) {
		return ScriptLoader::FromActiveContext<LevelScriptLoader>()->GetScriptParticle(index);
	}

	jjPARTICLE* AddParticle(std::int32_t particleType) {
		return ScriptLoader::FromActiveContext<LevelScriptLoader>()->AddScriptParticle(std::uint32_t(particleType));
	}

	// As many as JJ2+ has (`jjParticles`)
	static constexpr std::int32_t MaxParticles = 1024;

	Legacy::jjPARTICLE* LevelScriptLoader::GetScriptParticle(std::int32_t index)
	{
		if (index < 0 || index >= MaxParticles) {
			if (asIScriptContext* ctx = asGetActiveContext()) {
				ctx->SetException("Index out of bounds");
			}
			return nullptr;
		}
		if (_particles == nullptr) {
			_particles = std::make_unique<Legacy::jjPARTICLE[]>(MaxParticles);
			_particlesTick = GetGameTick();
		}
		return &_particles[index];
	}

	Legacy::jjPARTICLE* LevelScriptLoader::AddScriptParticle(std::uint32_t type)
	{
		if (type == Legacy::particleNONE || (type > Legacy::particleSTAR && type != Legacy::particleTILE)) {
			return nullptr;
		}
		if (_particles == nullptr) {
			_particles = std::make_unique<Legacy::jjPARTICLE[]>(MaxParticles);
			_particlesTick = GetGameTick();
		}

		Legacy::jjPARTICLE* particle = nullptr;
		for (std::int32_t i = 0; i < MaxParticles; i++) {
			std::int32_t index = (_nextParticle + i) % MaxParticles;
			if (_particles[index].particleType == Legacy::particleNONE) {
				particle = &_particles[index];
				_nextParticle = (index + 1) % MaxParticles;
				break;
			}
		}
		if (particle == nullptr) {
			return nullptr;
		}

		// What JJ2+ gives a new particle of the type, as measured
		particle->xPos = particle->yPos = particle->xSpeed = particle->ySpeed = 0.0f;
		particle->particleType = std::uint8_t(type);
		particle->active = true;
		particle->_age = 0;
		particle->_scale = 0.0f;
		particle->_weather = false;
		particle->_snowFrameBase = 0;
		std::uint8_t* data = particle->GENERIC.raw;
		std::memset(data, 0, sizeof(particle->GENERIC.raw));
		auto setU16 = [data](std::size_t offset, std::uint16_t value) {
			std::memcpy(&data[offset], &value, sizeof(value));
		};
		switch (type) {
			case Legacy::particleFIRE: data[0] = 3; data[1] = 40; data[2] = 48; data[3] = 1; break;
			case Legacy::particleSMOKE: data[0] = 71; break;
			case Legacy::particleICETRAIL: data[0] = 32; data[1] = 40; data[2] = 1; break;
			case Legacy::particleSPARK: data[0] = 40; data[1] = 46; data[2] = 1; break;
			case Legacy::particleSNOW: data[2] = 35; break;
			case Legacy::particleRAIN: {
				// The drops are the third animation of the common set (bubbles of a garbage can in ThreedRealms)
				particle->ySpeed = 6.0f;
				auto& tables = GetAnimationTables();
				std::uint32_t firstAnim = tables.GetFirstAnim(Legacy::AnimationTables::GetSetID(Legacy::mCOMMON, versionTSF));
				std::uint32_t frameCount, firstFrame;
				if (firstAnim != 0 && tables.GetAnimationFrames(firstAnim + 2, frameCount, firstFrame) && frameCount > 0) {
					setU16(1, std::uint16_t(std::min<std::uint32_t>(firstFrame, UINT16_MAX)));
				}
				break;
			}
			case Legacy::particleFLOWER: particle->ySpeed = 0.5f; data[0] = 64; data[1] = 16; data[4] = 5; break;
			case Legacy::particleSTAR: data[0] = 16; data[1] = 40; particle->_scale = 16.0f; break;
			case Legacy::particleLEAF: {
				// The leaves of JJ2+ are the first animation of its scenery set (17 frames, the one MLLE recolors)
				particle->ySpeed = 1.0f;
				data[3] = 2;
				auto& tables = GetAnimationTables();
				std::uint32_t firstAnim = tables.GetFirstAnim(Legacy::AnimationTables::GetSetID(Legacy::mZZSCENERY, versionTSF));
				std::uint32_t frameCount, firstFrame;
				if (firstAnim != 0 && tables.GetAnimationFrames(firstAnim, frameCount, firstFrame)) {
					setU16(4, std::uint16_t(std::min<std::uint32_t>(firstFrame, UINT16_MAX)));
				}
				break;
			}
		}
		return particle;
	}

	void LevelScriptLoader::UpdateParticles(std::int32_t currentTick)
	{
		// As measured in JJ2+ - most fall by their speeds, the colored ones change their color now and then and are gone at the
		// last one, a string speeds up and is gone above the view, a leaf drifts down by itself and lies on the ground a while
		constexpr std::int32_t LeafFrames = 17;
		Tiles::TileMap* tileMap = _levelHandler->TileMap();
		auto isMasked = [tileMap](float x, float y) {
			Tiles::TileCollisionParams params = { Tiles::TileDestructType::SuspendTiles, true };
			float px = std::floor(x), py = std::floor(y);
			return (tileMap != nullptr && !tileMap->IsTileEmpty(AABBf(px, py, px, py), params));
		};
		if (_particles == nullptr) {
			_particles = std::make_unique<Legacy::jjPARTICLE[]>(MaxParticles);
			_particlesTick = currentTick - 1;
		}
		if (_particlesTick >= currentTick) {
			// Drawn already in this tick, the level keeps what was drawn until the next one
			return;
		}
		while (_particlesTick < currentTick) {
			_particlesTick++;
			SpawnWeatherParticles();
			for (std::int32_t i = 0; i < MaxParticles; i++) {
				Legacy::jjPARTICLE& p = _particles[i];
				if (p.particleType == Legacy::particleNONE) {
					continue;
				}
				std::uint8_t* data = p.GENERIC.raw;
				bool remove = false;
				auto advanceColor = [&](std::uint8_t colorIndex, std::int32_t chance) {
					std::uint8_t& color = data[colorIndex];
					std::uint8_t stop = data[colorIndex + 1];
					std::int8_t delta = std::int8_t(data[colorIndex + 2]);
					if (Random().Next(0, chance) == 0) {
						color = std::uint8_t(color + delta);
					}
					if (delta == 0 || (delta > 0 ? color >= stop : color <= stop)) {
						remove = true;
					}
				};
				p._age = std::uint16_t(std::min<std::int32_t>(p._age + 1, UINT16_MAX));

				switch (p.particleType) {
					case Legacy::particlePIXEL:
						p.xPos += p.xSpeed;
						p.yPos += p.ySpeed;
						p.ySpeed += 1.0f / 16.0f;
						p.xSpeed = (p.xSpeed > 0.0f ? std::max(p.xSpeed - 1.0f / 256.0f, 0.0f) : std::min(p.xSpeed + 1.0f / 256.0f, 0.0f));
						if (p.ySpeed > 0.0f && isMasked(p.xPos, p.yPos)) {
							p.yPos -= p.ySpeed;
							p.ySpeed *= -0.5f;
							p.xSpeed *= -0.6f;
						}
						remove = (p._age >= 190);
						break;
					case Legacy::particleFIRE:
						p.xPos += p.xSpeed;
						p.yPos += p.ySpeed;
						p.ySpeed += 1.0f / 64.0f;
						advanceColor(1, 6);
						break;
					case Legacy::particleICETRAIL:
						p.xPos += p.xSpeed;
						p.yPos += p.ySpeed;
						p.ySpeed += 1.0f / 32.0f;
						advanceColor(0, 6);
						break;
					case Legacy::particleSPARK:
						p.xPos += p.xSpeed;
						p.yPos += p.ySpeed;
						p.ySpeed += 1.0f / 64.0f;
						advanceColor(0, 3);
						break;
					case Legacy::particleSMOKE:
						p.xPos += p.xSpeed + Random().FastFloat(-0.5f, 0.5f);
						p.yPos += p.ySpeed + Random().FastFloat(-0.9f, 0.0f);
						if (p._age % 25 == 0 && data[0] > 0) {
							data[0]--;
						}
						remove = (p._age >= 102);
						break;
					case Legacy::particleSCORE:
						p.xPos += p.xSpeed;
						p.yPos += p.ySpeed;
						p.xSpeed *= 1.0625f;
						p.ySpeed *= 1.0625f;
						remove = (p._age >= 43);
						break;
					case Legacy::particleSNOW: {
						std::uint16_t frameBase;
						std::memcpy(&frameBase, &data[3], sizeof(frameBase));
						p.xPos += p.xSpeed;
						p.yPos += p.ySpeed;
						remove = (frameBase == 0 || data[2] == 0 || --data[2] == 0);
						break;
					}
					case Legacy::particleRAIN:
						p.xPos += p.xSpeed;
						p.yPos += p.ySpeed;
						data[0] = std::uint8_t(Random().Next(0, 8));
						remove = (p._age >= 122);
						break;
					case Legacy::particleFLOWER:
						// Stops for good in a wall, still turning and shrinking
						if (!isMasked(p.xPos, p.yPos)) {
							p.xPos += p.xSpeed;
							p.yPos += p.ySpeed;
						}
						data[2] = std::uint8_t(data[2] + data[3]);
						remove = (data[0] == 0 || --data[0] == 0);
						break;
					case Legacy::particleSTAR:
						p.xPos += p.xSpeed;
						p.yPos += p.ySpeed;
						p.ySpeed += 1.0f / 64.0f;
						data[2] = std::uint8_t(data[2] + data[3]);
						p._scale *= 0.97f;
						data[0] = std::uint8_t(p._scale);
						remove = (p._scale < 2.0f);
						break;
					case Legacy::particleLEAF: {
						// `countup` counts the ticks it lies on the ground
						if (data[1] > 0) {
							remove = (++data[1] >= 140);
							break;
						}
						p.xPos -= Random().FastFloat(0.0f, 2.3f);
						p.yPos += Random().FastFloat(-0.25f, 2.05f);
						if ((p._age & 1) == 0) {
							data[0] = std::uint8_t((data[0] + 1) % (LeafFrames * 2));
						}
						if (data[2] == 0 && isMasked(p.xPos, p.yPos + 1.0f)) {
							data[1] = 1;
						}
						remove = (p._age >= 320);
						break;
					}
					default:
						remove = true;
						break;
				}

				// Weather of the outdoors only is gone where it falls into a wall
				if (p._weather && snowingOutdoors && p.particleType != Legacy::particleLEAF && isMasked(p.xPos, p.yPos)) {
					remove = true;
				}
				if (remove) {
					p.particleType = Legacy::particleNONE;
					p.active = false;
				}
			}
		}

		if (ContentResolver::Get().IsHeadless()) {
			return;
		}
		// Drawn in the sprite layer, as JJ2+ draws them, until the next tick
		auto& canvas = GetWorldCanvas();
		auto& tables = GetAnimationTables();
		std::uint16_t z = GetLayerDrawZ(4);
		std::int32_t paletteOffset = GetSpritePaletteOffset(Legacy::spriteType_NORMAL, 0);
		auto drawSolid = [&](float x, float y, float size, std::uint8_t color, std::uint32_t mode) {
			if (color != 0 && size > 0.0f) {
				canvas.AddSolid({ x - size * 0.5f, y - size * 0.5f, size, size, color, mode, 0, z });
			}
		};
		for (std::int32_t i = 0; i < MaxParticles; i++) {
			const Legacy::jjPARTICLE& p = _particles[i];
			const std::uint8_t* data = p.GENERIC.raw;
			switch (p.particleType) {
				case Legacy::particlePIXEL: {
					if (data[0] > 2) {
						drawSolid(p.xPos, p.yPos, float(data[0] + 1), data[1], Legacy::spriteType_NORMAL);
						break;
					}
					// A block of up to 3x3 pixels of its colors row by row, 0 is transparent, as JJ2+ breaks frames into them
					std::int32_t side = data[0] + 1;
					float left = p.xPos - float(side) * 0.5f, top = p.yPos - float(side) * 0.5f;
					for (std::int32_t py = 0; py < side; py++) {
						for (std::int32_t px = 0; px < side; px++) {
							std::uint8_t color = data[1 + py * side + px];
							if (color != 0) {
								canvas.AddSolid({ left + float(px), top + float(py), 1.0f, 1.0f, color, Legacy::spriteType_NORMAL, 0, z });
							}
						}
					}
					break;
				}
				case Legacy::particleFIRE: drawSolid(p.xPos, p.yPos, float(data[0]), data[1], Legacy::spriteType_NORMAL); break;
				case Legacy::particleICETRAIL:
				case Legacy::particleSPARK: drawSolid(p.xPos, p.yPos, 2.0f, data[0], Legacy::spriteType_NORMAL); break;
				case Legacy::particleSMOKE: drawSolid(p.xPos, p.yPos, 3.0f, 72, Legacy::spriteType_TRANSLUCENT); break;
				case Legacy::particleFLOWER: {
					// Thin petals from the middle, size / 8 long, turned by its angle (a whole turn is 256), the tips faded
					float length = float(data[0]) / 8.0f;
					std::int32_t petals = std::max<std::int32_t>(data[4], 1);
					if (data[1] == 0 || length < 1.0f) {
						break;
					}
					for (std::int32_t j = 0; j < petals; j++) {
						float angle = float(data[2] + j * 256 / petals) * fTwoPi / 256.0f;
						float dx = cosf(angle), dy = sinf(angle);
						auto drawPetal = [&](float petalLength, float width, std::uint32_t mode) {
							canvas.AddSolid({ p.xPos + dx * petalLength * 0.5f - petalLength * 0.5f, p.yPos + dy * petalLength * 0.5f - width * 0.5f,
								petalLength, width, data[1], mode, 0, z, angle });
						};
						drawPetal(length, 2.0f, Legacy::spriteType_TRANSLUCENT);
						drawPetal(length * 0.65f, 1.0f, Legacy::spriteType_NORMAL);
					}
					break;
				}
				case Legacy::particleSTAR: drawSolid(p.xPos, p.yPos, std::max(float(data[0]) / 2.0f, 1.0f), data[1], Legacy::spriteType_NORMAL); break;
				case Legacy::particleSCORE: {
					const Legacy::jjPARTICLESTRING& text = p.GENERIC.string;
					AddWorldText(p.xPos, p.yPos, text.get_text(), 1, UI::Font::DefaultColor, 4);
					break;
				}
				case Legacy::particleSNOW:
				case Legacy::particleRAIN:
				case Legacy::particleLEAF: {
					std::uint16_t frameBase;
					std::uint32_t frame;
					if (p.particleType == Legacy::particleLEAF) {
						std::memcpy(&frameBase, &data[4], sizeof(frameBase));
						frame = (frameBase != 0 ? frameBase + (data[0] / 2) % LeafFrames : 0);
					} else if (p.particleType == Legacy::particleSNOW) {
						std::memcpy(&frameBase, &data[3], sizeof(frameBase));
						// The full first frame of the weather, unless a script gave the snowflake another one
						std::uint32_t base = (p._snowFrameBase != 0 && frameBase == std::min<std::uint32_t>(p._snowFrameBase, UINT16_MAX)
							? p._snowFrameBase : frameBase);
						frame = (base != 0 ? base + data[0] : 0);
					} else {
						std::memcpy(&frameBase, &data[1], sizeof(frameBase));
						frame = (frameBase != 0 ? frameBase + data[0] : 0);
					}
					std::int32_t cell = 0;
					if (auto* graphics = (frame != 0 ? tables.ResolveFrame(frame, cell) : nullptr)) {
						canvas.AddSprite({ graphics, cell, p.xPos, p.yPos, 1, 1.0f, 1.0f, 0, Legacy::spriteType_NORMAL, 0, z, paletteOffset });
					} else if (p.particleType == Legacy::particleRAIN) {
						// No frames of rain, a streak of the color of the drops
						canvas.AddSolid({ p.xPos, p.yPos - 3.0f, 1.0f, 6.0f, 34, Legacy::spriteType_TRANSLUCENT, 0, z });
					}
					break;
				}
			}
		}
	}

	void LevelScriptLoader::SpawnWeatherParticles()
	{
		// JJ2+ makes the weather of particles scripts see (`jjParticles`), which they steer or remove - as many in a tick
		// as the intensity, across the view of each player, rain from above it
		if (!snowing || snowingType < 0 || snowingType > 3 || PreferencesCache::Particles == ParticleQuality::Off ||
			ContentResolver::Get().IsHeadless()) {
			return;
		}
		static constexpr std::uint32_t Types[] = { Legacy::particleSNOW, Legacy::particleFLOWER, Legacy::particleRAIN, Legacy::particleLEAF };
		std::uint32_t type = Types[snowingType];
		float rate = float(std::max<std::int32_t>(snowingIntensity, 1)) * (PreferencesCache::Particles == ParticleQuality::Low ? 0.5f : 1.0f);

		// The frames of a snowflake, the first animation of the set of snow
		std::uint32_t snowFirstFrame = 0, snowFrameCount = 0;
		if (type == Legacy::particleSNOW) {
			auto& tables = GetAnimationTables();
			std::uint32_t firstAnim = tables.GetFirstAnim(Legacy::AnimationTables::GetSetID(Legacy::mSNOW, versionTSF));
			if (firstAnim == 0 || !tables.GetAnimationFrames(firstAnim, snowFrameCount, snowFirstFrame) || snowFrameCount == 0) {
				return;
			}
		}

		// Flowers are a steady 64 per view, a new one in a tick while any is missing, as measured in JJ2+
		std::int32_t missingFlowers = 0;
		if (type == Legacy::particleFLOWER) {
			std::int32_t flowersPerView = (PreferencesCache::Particles == ParticleQuality::Low ? 32 : 64);
			missingFlowers = flowersPerView * std::int32_t(_levelHandler->_assignedViewports.size());
			for (std::int32_t i = 0; i < MaxParticles; i++) {
				if (_particles[i]._weather && _particles[i].particleType == Legacy::particleFLOWER) {
					missingFlowers--;
				}
			}
		}

		for (const auto& viewport : _levelHandler->_assignedViewports) {
			auto* player = runtime_cast<Actors::Player>(viewport->GetTargetActor());
			if (player == nullptr) {
				continue;
			}
			Rectf view = _levelHandler->GetCameraView(player);
			std::int32_t count;
			if (type == Legacy::particleFLOWER) {
				count = (missingFlowers > 0 ? 1 : 0);
				missingFlowers -= count;
			} else {
				count = std::int32_t(rate);
				if (Random().NextFloat() < rate - float(count)) {
					count++;
				}
			}
			for (std::int32_t i = 0; i < count; i++) {
				Legacy::jjPARTICLE* p = AddScriptParticle(type);
				if (p == nullptr) {
					return;
				}
				p->_weather = true;
				// Further to the right too, snow drifts left
				p->xPos = view.X + Random().FastFloat(-view.W * 0.1f, view.W * 1.25f);
				switch (type) {
					case Legacy::particleSNOW: {
						p->yPos = view.Y + Random().FastFloat(-16.0f, view.H);
						p->xSpeed = Random().FastFloat(-1.0f, -0.25f);
						p->ySpeed = Random().FastFloat(0.75f, 2.0f);
						std::uint8_t* data = p->GENERIC.raw;
						data[0] = std::uint8_t(Random().Next(0, std::int32_t(std::min<std::uint32_t>(snowFrameCount, 255))));
						std::uint16_t frameBase = std::uint16_t(std::min<std::uint32_t>(snowFirstFrame, UINT16_MAX));
						std::memcpy(&data[3], &frameBase, sizeof(frameBase));
						p->_snowFrameBase = snowFirstFrame;
						break;
					}
					case Legacy::particleRAIN:
						p->xPos -= view.W * 0.25f;
						p->yPos = view.Y + Random().FastFloat(-view.H * 0.5f, view.H * 0.5f);
						p->xSpeed = Random().FastFloat(1.5f, 2.0f);
						p->ySpeed = Random().FastFloat(6.0f, 7.0f);
						break;
					case Legacy::particleFLOWER: {
						// Anywhere in the view, white, 64 ticks long (now and then 128), drifting left and down
						p->xPos = view.X + Random().FastFloat(-32.0f, view.W + 32.0f);
						p->yPos = view.Y + Random().FastFloat(-32.0f, view.H);
						p->xSpeed = Random().FastFloat(-0.75f, -0.5f);
						p->ySpeed = Random().FastFloat(0.0f, 2.0f);
						std::uint8_t* data = p->GENERIC.raw;
						data[0] = (Random().Next(0, 8) == 0 ? 127 : 63);
						data[1] = 72;
						data[2] = std::uint8_t(Random().Next(0, 256));
						data[3] = std::uint8_t(std::int8_t(Random().Next(-8, 8)));
						data[4] = (Random().Next(0, 5) == 0 ? 7 : 6);
						break;
					}
					default:
						p->yPos = view.Y + Random().FastFloat(-32.0f, view.H * 0.5f);
						break;
				}
			}
		}
	}

	std::int32_t LevelScriptLoader::AddObjectFromEvent(std::uint8_t eventId, float xPixel, float yPixel)
	{
		// Objects below the first ammo are bullets and particles (OBJECT::SHARD, OBJECT::EXPLOSION...), the same IDs
		// as events of a level are areas (AREA::DIMLIGHT...), which aren't objects
		if (eventId < Legacy::aGUN3AMMO3) {
			return 0;
		}

		// An object of JJ2 reads its parameters of the tile it's made at, a script may make a platform at the tile of
		// another (the converters that read the level get an empty one, see Legacy::EventTranslator)
		std::uint32_t tileWord = GetOriginalEvent(std::int32_t(xPixel) >> 5, std::int32_t(yPixel) >> 5);
		std::uint32_t params = ((tileWord & 0xFF) == eventId ? tileWord >> Legacy::EventTranslator::ParamsShift : 0);
		auto result = GetEventTranslator().FromOriginal(eventId, params);
		if (result.Type == EventType::Empty) {
			return 0;
		}
		Compatibility::EventConverter::KeepOriginalParams(result.Type, result.Params, params);

		// The object of the game is a bit further than the object of JJ2+ would be, see Legacy::ScriptObjectHost::PositionOffset
		auto actor = _levelHandler->EventSpawner()->SpawnEvent(result.Type, result.Params, Actors::ActorState::None,
			Vector3i(std::int32_t(xPixel + Legacy::ScriptObjectHost::PositionOffset), std::int32_t(yPixel + Legacy::ScriptObjectHost::PositionOffset),
				ILevelHandler::SpritePlaneZ));
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

	std::int32_t LevelScriptLoader::GetObjectSpawnTick(std::int32_t objectId) const
	{
		return (objectId > 0 && objectId < std::int32_t(_objectSlots.size()) ? _objectSlots[objectId].SpawnTick : 0);
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

	bool LevelScriptLoader::GetScriptBossHealth(std::int32_t& health, std::int32_t& maxHealth)
	{
		for (Actors::Player* player : _levelHandler->_players) {
			auto it = _playerBackingStore.find(std::uint8_t(player->GetPlayerIndex()));
			if (it == _playerBackingStore.end()) {
				continue;
			}
			const Legacy::jjPLAYER& p = *it->second;
			if (!p.bossActive || p.bossNumber <= 0 || p.bossNumber >= std::int32_t(_objectSlots.size())) {
				continue;
			}
			Legacy::jjOBJ* obj = _objectSlots[p.bossNumber].Object;
			if (obj == nullptr || !obj->get_isActive()) {
				continue;
			}
			Legacy::jjOBJ* preset = _objectPresets[obj->eventID];
			maxHealth = (preset != nullptr && preset->energy > 0 ? preset->energy : 100);
			health = obj->energy;
			return true;
		}
		return false;
	}

	bool LevelScriptLoader::OnBossAreaActivated()
	{
		if (GetContextType() != ScriptContextType::Legacy) {
			return false;
		}
		_bossAreaActivated = true;
		return true;
	}

	void LevelScriptLoader::SwapActor(std::int32_t objectId, std::uint8_t eventId)
	{
		_actorSwaps.push_back({ objectId, eventId });
	}

	void LevelScriptLoader::ProcessActorSwaps()
	{
		for (const ActorSwap& swap : _actorSwaps) {
			if (swap.ObjectID <= 0 || swap.ObjectID >= std::int32_t(_objectSlots.size())) {
				continue;
			}
			ObjectSlot& slot = _objectSlots[swap.ObjectID];
			Legacy::jjOBJ* obj = slot.Object;
			std::shared_ptr<Actors::ActorBase> previous = slot.Actor.lock();
			if (obj == nullptr || previous == nullptr || previous->GetState(Actors::ActorState::IsDestroyed)) {
				continue;
			}
			std::shared_ptr<Actors::ActorBase> actor = Legacy::ScriptObjectHost::SpawnInPlace(this, *previous, swap.EventID);
			if (actor == nullptr) {
				continue;
			}

			// The new object keeps the object ID and the view, the behavior drives it from now on
			_swappingActor = actor.get();
			_levelHandler->AddActor(actor);
			_swappingActor = nullptr;
			SetSlotActor(swap.ObjectID, actor);
			obj->_actor = actor;
			if (obj->state == Legacy::sFLOAT || obj->state == Legacy::sFLOATFALL) {
				// Floating was the pickup's, the behavior of the new object starts it as JJ2+ does (zombie dogs made of food walk then)
				obj->state = Legacy::sSTART;
				obj->_synced.State = obj->state;
			}
			previous->SetUpdateOverride(nullptr);
			Legacy::ScriptObjectHost::DeleteActor(*previous);
			HostObject(obj, actor.get());
		}
		_actorSwaps.clear();
	}

	void LevelScriptLoader::RegisterLegacyObjects(asIScriptEngine* engine)
	{
		engine->SetDefaultNamespace("CREATOR");
		/** @brief What created an object, see @ref jjOBJ::creatorType */
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "OBJECT", 0);	///< Created by another object
		// As measured in JJ2+
		engine->RegisterEnumValue("Type", "LEVEL", 0x4000);	///< Placed in the level
		engine->RegisterEnumValue("Type", "PLAYER", 0x8000);	///< Created by a player, for example a fired bullet
		engine->SetDefaultNamespace("AREA");
		/** @brief Events that only mark an area of the level and have no object of their own */
		engine->RegisterEnum("Area");
		engine->SetDefaultNamespace("OBJECT");
		/** @brief Events that create objects, as used by @ref jjOBJ::eventID */
		engine->RegisterEnum("Object");
		engine->SetDefaultNamespace("ANIM");
		/** @brief Animation sets, as used by @ref jjOBJ::determineCurAnim() and @ref jjAnimSets */
		engine->RegisterEnum("Set");
		engine->SetDefaultNamespace("");

		engine->SetDefaultNamespace("LIGHT");
		/** @brief How much of the lighting players are allowed to turn off, see @ref jjEnforceLighting */
		engine->RegisterEnum("Enforce");
		engine->RegisterEnumValue("Enforce", "OPTIONAL", ambientLighting_OPTIONAL);	///< Players may turn off lighting completely
		engine->RegisterEnumValue("Enforce", "BASIC", ambientLighting_BASIC);	///< Players may turn off ambient lighting, but lights of objects and players stay
		engine->RegisterEnumValue("Enforce", "COMPLETE", ambientLighting_COMPLETE);	///< All lighting is always shown

		engine->SetDefaultNamespace("");
		/**
		 * @brief How much of the lighting players are allowed to turn off
		 *
		 * Defaults to @ref LIGHT::OPTIONAL.
		 * @partialsupport The value is stored, but the game currently doesn't act on it.
		 */
		engine->RegisterGlobalProperty("LIGHT::Enforce jjEnforceLighting", &enforceAmbientLighting);

		engine->SetDefaultNamespace("STATE");
		/**
		 * @brief States of the state machine of an object, see @ref jjOBJ::state
		 *
		 * The most important are @ref START for a newly created object, @ref KILL for a destroyed one, @ref DEACTIVATE for one that
		 * is being unloaded, @ref EXPLODE for a bullet that is ending, @ref FREEZE for a frozen one and @ref ACTION for collisions
		 * with some complex objects. The meaning of the others depends on the behavior.
		 */
		engine->RegisterEnum("State");
		engine->SetDefaultNamespace("BEHAVIOR");
		/**
		 * @brief Built-in behaviors of objects, see @ref jjOBJ::behavior
		 *
		 * Most values share their names with the @ref OBJECT::Object events that use them. `INACTIVE` marks a free or deleted
		 * object, and `DEFAULT` stands for an object's own behavior in @ref jjAddObject() and @ref jjOBJ::behave().
		 */
		engine->RegisterEnum("Behavior");

		engine->SetDefaultNamespace("LIGHT");
		/** @brief Kinds of light objects and players can emit, see @ref jjOBJ::lightType */
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "NONE", 0);	///< No light
		engine->RegisterEnumValue("Type", "NORMAL", 3);	///< Round light whose size is given by @ref jjOBJ::light
		engine->RegisterEnumValue("Type", "POINT", 1);	///< Small dim point of a fixed size
		engine->RegisterEnumValue("Type", "POINT2", 2);	///< Small brighter point of a fixed size that slowly pulses
		engine->RegisterEnumValue("Type", "FLICKER", 4);	///< Round light that randomly flickers
		engine->RegisterEnumValue("Type", "BRIGHT", 5);	///< Round light that also brightens what it lights
		engine->RegisterEnumValue("Type", "LASERBEAM", 6);	///< Beam of light, currently not shown @partialsupport
		engine->RegisterEnumValue("Type", "LASER", 7);	///< Cloud of soft spots swirling around the object
		engine->RegisterEnumValue("Type", "RING", 8);	///< Ring of light whose radius is given by @ref jjOBJ::light
		engine->RegisterEnumValue("Type", "RING2", 9);	///< Wider ring of light around a dim disk
		engine->RegisterEnumValue("Type", "PLAYER", 10);	///< Light around a player

		engine->SetDefaultNamespace("HANDLING");
		/** @brief How an object reacts when a bullet touches it, see @ref jjOBJ::bulletHandling */
		engine->RegisterEnum("Bullet");
		engine->RegisterEnumValue("Bullet", "HURTBYBULLET", 0);	///< The object loses energy, the bullet is usually destroyed
		engine->RegisterEnumValue("Bullet", "IGNOREBULLET", 1);	///< Neither the object nor the bullet is affected
		engine->RegisterEnumValue("Bullet", "DESTROYBULLET", 2);	///< The bullet is destroyed, the object is unaffected
		engine->RegisterEnumValue("Bullet", "DETECTBULLET", 3);	///< The object is unaffected, the bullet is usually destroyed
		/** @brief How an object reacts when a player touches it, see @ref jjOBJ::playerHandling */
		engine->RegisterEnum("Player");
		engine->RegisterEnumValue("Player", "ENEMY", 0);	///< Hurts the player, unless the player attacks it
		engine->RegisterEnumValue("Player", "PLAYERBULLET", 1);	///< Bullet fired by a player, hits enemies and other players
		engine->RegisterEnumValue("Player", "ENEMYBULLET", 2);	///< Bullet fired by an enemy, hurts players
		engine->RegisterEnumValue("Player", "PARTICLE", 3);	///< No special handling
		engine->RegisterEnumValue("Player", "EXPLOSION", 4);	///< No special handling
		engine->RegisterEnumValue("Player", "PICKUP", 5);	///< Collected by the player, with an effect depending on @ref jjOBJ::eventID
		engine->RegisterEnumValue("Player", "DELAYEDPICKUP", 6);	///< No special handling
		engine->RegisterEnumValue("Player", "HURT", 7);	///< No special handling
		engine->RegisterEnumValue("Player", "SPECIAL", 8);	///< Reacts to players and bullets in a way specific to the object
		engine->RegisterEnumValue("Player", "DYING", 9);	///< No special handling
		engine->RegisterEnumValue("Player", "SPECIALDONE", 10);	///< No special handling
		engine->RegisterEnumValue("Player", "SELFCOLLISION", 11);	///< No special handling
		engine->SetDefaultNamespace("");

		// TODO
		engine->RegisterObjectType("jjOBJ", sizeof(jjOBJ), asOBJ_REF /*| asOBJ_NOCOUNT*/);
		engine->RegisterObjectBehaviour("jjOBJ", asBEHAVE_ADDREF, "void f()", asMETHOD(jjOBJ, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjOBJ", asBEHAVE_RELEASE, "void f()", asMETHOD(jjOBJ, Release), asCALL_THISCALL);
		engine->RegisterGlobalFunction("jjOBJ @get_jjObjects(int)", asFUNCTION(get_jjObjects), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjOBJ @get_jjObjectPresets(uint8)", asFUNCTION(get_jjObjectPresets), asCALL_CDECL);
		/**
		 * @brief One past the highest object ID in use
		 *
		 * The end point for loops over @ref jjObjects. Not the number of existing objects, since slots below it can be free.
		 */
		engine->RegisterGlobalProperty("const int jjObjectCount", &jjObjectCount);
		/** @brief Maximum number of objects that can exist at once, currently 2048 */
		engine->RegisterGlobalProperty("const int jjObjectMax", &jjObjectMax);
		engine->RegisterObjectMethod("jjOBJ", "bool get_isActive() const", asMETHOD(jjOBJ, get_isActive), asCALL_THISCALL);

		// Registered here, after LIGHT::Type
		/**
		 * @brief Returns the kind of light the player emits
		 *
		 * Only whether the player has a light at all is supported, so this is either @ref LIGHT::NONE or @ref LIGHT::PLAYER.
		 */
		engine->RegisterObjectMethod("jjPLAYER", "LIGHT::Type get_lightType() const", asMETHOD(jjPLAYER, get_lightType), asCALL_THISCALL);
		/** @brief Sets the kind of light the player emits, @ref LIGHT::NONE turns it off and any other value turns it on */
		engine->RegisterObjectMethod("jjPLAYER", "LIGHT::Type set_lightType(LIGHT::Type)", asMETHOD(jjPLAYER, set_lightType), asCALL_THISCALL);

		/**
		 * @brief Checks whether an object's sprite overlaps the player
		 *
		 * Compares the pixels of the object's current frame with the hitbox of the player. Handling settings aren't
		 * taken into account.
		 *
		 * @param object  Object to test against
		 * @param always  Currently ignored
		 * @partialsupport The `always` parameter is currently ignored.
		 */
		engine->RegisterObjectMethod("jjPLAYER", "bool doesCollide(const jjOBJ@ object, bool always = false) const", asMETHOD(jjPLAYER, doesCollide), asCALL_THISCALL);
		/**
		 * @brief Tells whether the player is currently able to harm objects
		 *
		 * The value has the same meaning as the `force` argument of @ref jjPLAYER::objectHit().
		 * @partialsupport Currently always returns 0.
		 */
		engine->RegisterObjectMethod("jjPLAYER", "int getObjectHitForce(const jjOBJ@ target = null) const", asMETHOD(jjPLAYER, getObjectHitForce), asCALL_THISCALL);
		/**
		 * @brief Makes the player collide with an object
		 *
		 * The collision is resolved as if the two really touched, wherever they are, and the object's @ref jjOBJ::energy
		 * is updated afterward. For an object with @ref jjOBJ::scriptedCollisions set, its @ref onObjectHit() decides the
		 * result instead of the game.
		 *
		 * @param target          Object to collide with
		 * @param force           How hard the player hits: 0 for a plain touch, 1 for a buttstomp or sugar rush, -1 for a
		 *                        special move, -101 for running fast into a frozen object
		 * @param playerHandling  Currently ignored
		 * @return Whether the object was affected
		 * @partialsupport The `playerHandling` parameter is currently ignored.
		 */
		engine->RegisterObjectMethod("jjPLAYER", "bool objectHit(jjOBJ@ target, int force, HANDLING::Player playerHandling)", asMETHOD(jjPLAYER, objectHit), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "jjOBJ@ objectHit(jjOBJ@ target, HANDLING::Player playerHandling)", asMETHOD(jjOBJ, objectHit), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjOBJ", "void blast(int maxDistance, bool blastObjects)", asMETHOD(jjOBJ, blast), asCALL_THISCALL);

		/**
		 * @brief Whether the player's attacks can hurt another player under the rules of the current game mode
		 *
		 * @partialsupport Currently always returns false.
		 */
		engine->RegisterObjectMethod("jjPLAYER", "bool isEnemy(const jjPLAYER &in victim) const", asMETHOD(jjPLAYER, isEnemy), asCALL_THISCALL);

		/** @brief Returns the animation set the player takes animations from */
		engine->RegisterObjectMethod("jjPLAYER", "ANIM::Set get_setID() const", asMETHOD(jjPLAYER, get_charCurr), asCALL_THISCALL);
		/** @brief Returns the animation currently shown for the player, an index of @ref jjAnimations */
		engine->RegisterObjectMethod("jjPLAYER", "uint16 get_curAnim() const", asMETHOD(jjPLAYER, get_curAnim), asCALL_THISCALL);
		/**
		 * @brief Returns the frame currently shown for the player, an index of @ref jjAnimFrames
		 *
		 * Equals the first frame of @ref jjPLAYER::curAnim plus @ref jjPLAYER::frameID.
		 */
		engine->RegisterObjectMethod("jjPLAYER", "uint get_curFrame() const", asMETHOD(jjPLAYER, get_curFrame), asCALL_THISCALL);
		/** @brief Returns the index of the frame currently shown for the player within @ref jjPLAYER::curAnim */
		engine->RegisterObjectMethod("jjPLAYER", "uint8 get_frameID() const", asMETHOD(jjPLAYER, get_frameID), asCALL_THISCALL);

		/**
		 * @brief Signature of a behavior function, see @ref jjOBJ::behavior
		 *
		 * A function taking the object it drives, called once per tick. A delegate of a class method with the same
		 * signature works too.
		 */
		engine->RegisterFuncdef("void jjVOIDFUNCOBJ(jjOBJ@)");
		engine->RegisterObjectType("jjBEHAVIOR", sizeof(jjBEHAVIOR), asOBJ_VALUE | asOBJ_APP_CLASS_CDA);
		engine->RegisterObjectBehaviour("jjBEHAVIOR", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(jjBEHAVIOR::Create), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjBEHAVIOR", asBEHAVE_CONSTRUCT, "void f(const BEHAVIOR::Behavior &in behavior)", asFUNCTION(jjBEHAVIOR::CreateFromBehavior), asCALL_CDECL_OBJLAST);
		engine->RegisterObjectBehaviour("jjBEHAVIOR", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(jjBEHAVIOR::Destroy), asCALL_CDECL_OBJLAST);

		/**
		 * @brief Base for script classes that act as object behaviors
		 *
		 * Assign an instance of an implementing class to @ref jjOBJ::behavior (or to a template in @ref jjObjectPresets) to
		 * drive objects by it. Unlike a plain @ref jjVOIDFUNCOBJ function, the class can keep its own data and define more
		 * methods the game calls when they are present:
		 * - `void onDraw(jjOBJ@ obj)` --- draws the object, called after @ref onBehave() unless drawing is skipped
		 * - `bool onObjectHit(jjOBJ@ obj, jjOBJ@ bullet, jjPLAYER@ player, int force)` --- decides what happens when a
		 *   player or bullet hits an object with @ref jjOBJ::scriptedCollisions set, returning false falls back to the global
		 *   @ref onObjectHit() function
		 */
		engine->RegisterInterface("jjBEHAVIORINTERFACE");
		/**
		 * @brief Runs the behavior for an object, once per tick
		 *
		 * Called for every object whose @ref jjOBJ::behavior is this instance. It is the only method an implementing class
		 * must define.
		 *
		 * @param obj  Object being driven
		 */
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
		/** @deprecated Use @ref jjOBJ::creatorID and @ref jjOBJ::creatorType instead, the combined number is hard to tell apart. */
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
		/** @deprecated Use @ref jjOBJ::bulletHandling, @ref jjOBJ::causesRicochet, @ref jjOBJ::isFreezable and @ref jjOBJ::isBlastable instead. */
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
		/** @deprecated Use @ref jjOBJ::playerHandling, @ref jjOBJ::isTarget, @ref jjOBJ::triggersTNT, @ref jjOBJ::deactivates and @ref jjOBJ::scriptedCollisions instead. */
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
		/**
		 * @brief Whether objects are being unloaded because the player died
		 *
		 * Meant to tell apart the two reasons for @ref STATE::DEACTIVATE: the player dying or the object being too far
		 * away.
		 * @partialsupport Currently always false.
		 */
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
		/** @brief Types of particles, see @ref jjPARTICLE::type and @ref jjAddParticle() */
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "INACTIVE", particleNONE);	///< Free particle slot
		engine->RegisterEnumValue("Type", "PIXEL", particlePIXEL);	///< Small square of debris that bounces off the ground, see @ref jjPARTICLE::pixel
		engine->RegisterEnumValue("Type", "FIRE", particleFIRE);	///< Small flame that changes color until it burns out, see @ref jjPARTICLE::fire
		engine->RegisterEnumValue("Type", "SMOKE", particleSMOKE);	///< Puff of smoke drifting upward, see @ref jjPARTICLE::smoke
		engine->RegisterEnumValue("Type", "ICETRAIL", particleICETRAIL);	///< Single dot left behind by ice bullets, see @ref jjPARTICLE::icetrail
		engine->RegisterEnumValue("Type", "SPARK", particleSPARK);	///< Short-lived spark, for example from a bouncing bullet, see @ref jjPARTICLE::spark
		engine->RegisterEnumValue("Type", "STRING", particleSCORE);	///< Floating text such as gained points, see @ref jjPARTICLE::string
		engine->RegisterEnumValue("Type", "SNOW", particleSNOW);	///< Snowflake, see @ref jjPARTICLE::snow
		engine->RegisterEnumValue("Type", "RAIN", particleRAIN);	///< Raindrop, see @ref jjPARTICLE::rain
		engine->RegisterEnumValue("Type", "FLOWER", particleFLOWER);	///< Spinning flower that shrinks over time, see @ref jjPARTICLE::flower
		engine->RegisterEnumValue("Type", "LEAF", particleLEAF);	///< Drifting leaf that settles on the ground, see @ref jjPARTICLE::leaf
		engine->RegisterEnumValue("Type", "STAR", particleSTAR);	///< Shrinking star, as shown during a sugar rush, see @ref jjPARTICLE::star
		engine->RegisterEnumValue("Type", "TILE", particleTILE);	///< Falling piece of a tile, currently disappears right away without being shown
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
		
		/**
		 * @brief Extra properties of a @ref PARTICLE::PIXEL particle, see @ref jjPARTICLE::pixel
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::pixel.
		 */
		engine->RegisterObjectType("jjPARTICLEPIXEL", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		/** @brief Size of the square, 0 for 1x1, 1 for 2x2 and 2 for 3x3 pixels, defaults to 0 */
		engine->RegisterObjectProperty("jjPARTICLEPIXEL", "uint8 size", 0);
		engine->RegisterObjectMethod("jjPARTICLEPIXEL", "uint8 get_color(int) const", asMETHOD(jjPARTICLEPIXEL, get_color), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPARTICLEPIXEL", "uint8 set_color(int, uint8)", asMETHOD(jjPARTICLEPIXEL, set_color), asCALL_THISCALL);
		/**
		 * @brief Properties of a @ref PARTICLE::PIXEL particle
		 *
		 * A small square of debris that falls, slows down and bounces off the ground, like the pieces of a destroyed enemy.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEPIXEL pixel", asOFFSET(jjPARTICLE, GENERIC));
		/**
		 * @brief Extra properties of a @ref PARTICLE::FIRE particle, see @ref jjPARTICLE::fire
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::fire.
		 */
		engine->RegisterObjectType("jjPARTICLEFIRE", 9, asOBJ_VALUE | asOBJ_POD);  // Private/deprecated
		/** @brief Size of the flame in pixels, defaults to 3 */
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "uint8 size", 0);
		/** @brief Current palette color, defaults to 40 */
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "uint8 color", 1);
		/** @brief Palette color at which the particle disappears, defaults to 48 */
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "uint8 colorStop", 2);
		/** @brief Step added to `color` each time it changes, defaults to 1 */
		engine->RegisterObjectProperty("jjPARTICLEFIRE", "int8 colorDelta", 3);
		/**
		 * @brief Properties of a @ref PARTICLE::FIRE particle
		 *
		 * A small flame that slowly falls while its color changes now and then, until the color reaches `colorStop`.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEFIRE fire", asOFFSET(jjPARTICLE, GENERIC));
		/**
		 * @brief Extra properties of a @ref PARTICLE::SMOKE particle, see @ref jjPARTICLE::smoke
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::smoke.
		 */
		engine->RegisterObjectType("jjPARTICLESMOKE", 9, asOBJ_VALUE | asOBJ_POD);  // Private/deprecated
		/** @brief Decreases slowly over the particle's life, defaults to 71 */
		engine->RegisterObjectProperty("jjPARTICLESMOKE", "uint8 countdown", 0);
		/**
		 * @brief Properties of a @ref PARTICLE::SMOKE particle
		 *
		 * A small gray puff that wobbles upward and disappears after a while.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESMOKE smoke", asOFFSET(jjPARTICLE, GENERIC));
		/** @brief Extra properties of a @ref PARTICLE::ICETRAIL particle, see @ref jjPARTICLE::icetrail */
		engine->RegisterObjectType("jjPARTICLEICETRAIL", 9, asOBJ_VALUE | asOBJ_POD);
		/** @brief Current palette color, defaults to 32 */
		engine->RegisterObjectProperty("jjPARTICLEICETRAIL", "uint8 color", 0);
		/** @brief Palette color at which the particle disappears, defaults to 40 */
		engine->RegisterObjectProperty("jjPARTICLEICETRAIL", "uint8 colorStop", 1);
		/** @brief Step added to `color` each time it changes, defaults to 1 */
		engine->RegisterObjectProperty("jjPARTICLEICETRAIL", "int8 colorDelta", 2);
		/**
		 * @brief Properties of a @ref PARTICLE::ICETRAIL particle
		 *
		 * A tiny dot that falls while its color changes now and then, until the color reaches `colorStop`.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEICETRAIL icetrail", asOFFSET(jjPARTICLE, GENERIC));
		/**
		 * @brief Extra properties of a @ref PARTICLE::SPARK particle, see @ref jjPARTICLE::spark
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::spark.
		 */
		engine->RegisterObjectType("jjPARTICLESPARK", 9, asOBJ_VALUE | asOBJ_POD);  // Private/deprecated
		/** @brief Current palette color, defaults to 40 */
		engine->RegisterObjectProperty("jjPARTICLESPARK", "uint8 color", 0);
		/** @brief Palette color at which the particle disappears, defaults to 46 */
		engine->RegisterObjectProperty("jjPARTICLESPARK", "uint8 colorStop", 1);
		/** @brief Step added to `color` each time it changes, defaults to 1 */
		engine->RegisterObjectProperty("jjPARTICLESPARK", "int8 colorDelta", 2);
		/**
		 * @brief Properties of a @ref PARTICLE::SPARK particle
		 *
		 * A tiny dot that falls while its color changes quickly, until the color reaches `colorStop`.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESPARK spark", asOFFSET(jjPARTICLE, GENERIC));
		/**
		 * @brief Extra properties of a @ref PARTICLE::STRING particle, see @ref jjPARTICLE::string
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::string.
		 */
		engine->RegisterObjectType("jjPARTICLESTRING", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		engine->RegisterObjectMethod("jjPARTICLESTRING", "::string get_text() const", asMETHOD(jjPARTICLESTRING, get_text), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPARTICLESTRING", "void set_text(::string)", asMETHOD(jjPARTICLESTRING, set_text), asCALL_THISCALL);
		/**
		 * @brief Properties of a @ref PARTICLE::STRING particle
		 *
		 * A short text, such as the points gained for destroying something, that moves faster and faster and disappears
		 * after a moment.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESTRING string", asOFFSET(jjPARTICLE, GENERIC));
		/**
		 * @brief Extra properties of a @ref PARTICLE::SNOW particle, see @ref jjPARTICLE::snow
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::snow.
		 */
		engine->RegisterObjectType("jjPARTICLESNOW", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		/** @brief Frame shown, counted from `frameBase`, defaults to 0 */
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint8 frame", 0);
		/** @brief Currently not used by the game @partialsupport */
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint8 countup", 1);
		/** @brief Remaining lifetime in ticks, defaults to 35 */
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint8 countdown", 2);
		/** @brief First frame of the snowflake animation, an index of @ref jjAnimFrames, the particle disappears while it is 0 */
		engine->RegisterObjectProperty("jjPARTICLESNOW", "uint16 frameBase", 3);
		/**
		 * @brief Properties of a @ref PARTICLE::SNOW particle
		 *
		 * A snowflake sprite that moves by its speed until its lifetime runs out. A new snow particle has no
		 * `frameBase`, so set one or it disappears right away.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLESNOW snow", asOFFSET(jjPARTICLE, GENERIC));
		/**
		 * @brief Extra properties of a @ref PARTICLE::RAIN particle, see @ref jjPARTICLE::rain
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::rain.
		 */
		engine->RegisterObjectType("jjPARTICLERAIN", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		/** @brief Frame shown, counted from `frameBase`, picked at random every tick */
		engine->RegisterObjectProperty("jjPARTICLERAIN", "uint8 frame", 0);
		/** @brief First frame of the raindrop animation, an index of @ref jjAnimFrames */
		engine->RegisterObjectProperty("jjPARTICLERAIN", "uint16 frameBase", 1);
		/**
		 * @brief Properties of a @ref PARTICLE::RAIN particle
		 *
		 * A raindrop sprite that falls by its speed, 6 pixels per tick by default, and disappears after a while.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLERAIN rain", asOFFSET(jjPARTICLE, GENERIC));
		/**
		 * @brief Extra properties of a @ref PARTICLE::LEAF particle, see @ref jjPARTICLE::leaf
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::leaf.
		 */
		engine->RegisterObjectType("jjPARTICLELEAF", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		/** @brief Animation step of the drifting leaf, two steps per frame shown */
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint8 frame", 0);
		/** @brief 0 while the leaf drifts, then counts the ticks it lies on the ground until it disappears at 140 */
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint8 countup", 1);
		/** @brief Whether the leaf passes through the ground instead of settling on it */
		engine->RegisterObjectProperty("jjPARTICLELEAF", "bool noclip", 2);
		/** @brief Height above the ground at which the leaf settles, defaults to 2, currently not used by the game @partialsupport */
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint8 height", 3);
		/** @brief First frame of the leaf animation, an index of @ref jjAnimFrames */
		engine->RegisterObjectProperty("jjPARTICLELEAF", "uint16 frameBase", 4);
		/**
		 * @brief Properties of a @ref PARTICLE::LEAF particle
		 *
		 * A leaf sprite that drifts down and to the left with some random jiggle, ignoring its speed, then lies on the
		 * ground for a while.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLELEAF leaf", asOFFSET(jjPARTICLE, GENERIC));
		/**
		 * @brief Extra properties of a @ref PARTICLE::FLOWER particle, see @ref jjPARTICLE::flower
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::flower.
		 */
		engine->RegisterObjectType("jjPARTICLEFLOWER", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		/** @brief Size of the flower, decreases by 1 every tick until the particle disappears at 0, defaults to 64 */
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 size", 0);
		/** @brief Palette color of the flower, defaults to 16 */
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 color", 1);
		/** @brief Current rotation, 256 is a full turn */
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 angle", 2);
		/** @brief Change of `angle` every tick, defaults to 0 */
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "int8 angularSpeed", 3);
		/** @brief Number of petals, defaults to 5 */
		engine->RegisterObjectProperty("jjPARTICLEFLOWER", "uint8 petals", 4);
		/**
		 * @brief Properties of a @ref PARTICLE::FLOWER particle
		 *
		 * A translucent flower of one color that drifts and shrinks until it vanishes. It stops moving inside walls.
		 */
		engine->RegisterObjectProperty("jjPARTICLE", "jjPARTICLEFLOWER flower", asOFFSET(jjPARTICLE, GENERIC));
		/**
		 * @brief Extra properties of a @ref PARTICLE::STAR particle, see @ref jjPARTICLE::star
		 * @deprecated Not meant to be used by its name, it's reached through @ref jjPARTICLE::star.
		 */
		engine->RegisterObjectType("jjPARTICLESTAR", 9, asOBJ_VALUE | asOBJ_POD); // Private/deprecated
		/** @brief Size of the star, shrinks over time, defaults to 16 */
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 size", 0);
		/** @brief Palette color, defaults to 40 */
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 color", 1);
		/** @brief Current rotation, 256 is a full turn */
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 angle", 2);
		/** @brief Change of `angle` every tick, defaults to 0 */
		engine->RegisterObjectProperty("jjPARTICLESTAR", "int8 angularSpeed", 3);
		/** @brief 0 for a filled star, 1 for an outline, currently not used by the game @partialsupport */
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 frame", 4);
		/** @brief Ticks left until the next color change, currently not used by the game @partialsupport */
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 colorChangeCounter", 5);
		/** @brief Ticks between color changes, 0 for never, currently not used by the game @partialsupport */
		engine->RegisterObjectProperty("jjPARTICLESTAR", "uint8 colorChangeInterval", 6);
		/**
		 * @brief Properties of a @ref PARTICLE::STAR particle
		 *
		 * A star that falls slowly and shrinks until it disappears, like those shown during a sugar rush.
		 */
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
