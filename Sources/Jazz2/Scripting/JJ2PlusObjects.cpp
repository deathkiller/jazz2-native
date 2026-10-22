#if defined(WITH_ANGELSCRIPT)

#include "JJ2PlusObjects.h"
#include "JJ2PlusAnimations.h"
#include "JJ2PlusEvents.h"
#include "LevelScriptLoader.h"
#include "../ContentResolver.h"
#include "../ILevelHandler.h"
#include "../Actors/Explosion.h"
#include "../Actors/Player.h"
#include "../Actors/Collectibles/CollectibleBase.h"
#include "../Actors/Weapons/ShotBase.h"
#include "../Actors/Weapons/TNT.h"
#include "../Events/EventMap.h"

#include <algorithm>
#include <cstring>

namespace Jazz2::Scripting::Legacy
{
	namespace
	{
		// Value of BEHAVIOR::DEFAULT, the behavior the object has in the game
		constexpr std::uint32_t DefaultBehavior = UINT32_MAX;

		// HANDLING::Player and HANDLING::Bullet of JJ2+
		constexpr std::uint32_t hPARTICLE = 3;
		constexpr std::uint32_t hPICKUP = 5;
		constexpr std::uint32_t hSPECIAL = 8;
		constexpr std::uint32_t hIGNOREBULLET = 1;
		constexpr std::uint32_t hDESTROYBULLET = 2;

		// CREATOR::Type of JJ2+
		constexpr std::uint32_t cPLAYER = 2;

		// BEHAVIOR::EXPLOSION2 of JJ2+, the end of a collected pickup
		constexpr std::uint32_t BehaviorExplosion2 = aCOUNT + 21;

		// The `playerHandling` the template of the objects of an event has before a script changes it, PICKUP for the
		// pickups of the original - ammo, coins, gems, food and the power-ups collected by touching them - and ENEMY (0)
		// for the rest, which the game handles on its own anyway (see ScriptObjectHost::OnCollision())
		std::uint32_t GetDefaultPlayerHandling(std::uint8_t eventId)
		{
			switch (eventId) {
				case aREDGEMTEMP:
				case aGUN3AMMO3: case aGUN2AMMO3: case aGUN4AMMO3: case aGUN5AMMO3:
				case aGUN6AMMO3: case aGUN7AMMO3: case aGUN8AMMO3: case aGUN9AMMO3:
				case aSILVERCOIN: case aGOLDCOIN:
				case aGUN3AMMO15: case aGUN2AMMO15: case aGUN4AMMO15: case aGUN5AMMO15: case aGUN6AMMO15:
				case aGUNFASTFIRE:
				case aREDGEM: case aGREENGEM: case aBLUEGEM: case aPURPLEGEM: case aSUPERREDGEM:
				case aENERGYUP: case aFULLENERGY:
				case aFIRESHIELD: case aWATERSHIELD: case aLIGHTSHIELD: case aLASERSHIELD:
				case aFASTFEET: case aEXTRALIFE: case aINVINCIBILITY: case aEXTRATIME: case aFREEZER: case aSTOPWATCH:
				case aFLYCARROT:
				case aRECTREDGEM: case aRECTGREENGEM: case aRECTBLUEGEM:
					return hPICKUP;
				default:
					// Food, two runs of the events
					return ((eventId >= aAPPLE && eventId <= aSTRAWBERRY) || (eventId >= aLEMON && eventId <= aCHEESE) ? hPICKUP : 0);
			}
		}
	}

	ScriptObjectHost::ScriptObjectHost(LevelScriptLoader* levelScripts, jjOBJ* obj, Actors::ActorBase* actor)
		: _levelScripts(levelScripts), _obj(obj), _actor(actor), _timeMult(1.0f), _nextTick(-1), _drawn(true), _drawFrame(false),
			_ranOwnBehavior(false), _isRunningOwnBehavior(false)
	{
		_obj->AddRef();
		_obj->_host = this;
	}

	ScriptObjectHost::~ScriptObjectHost()
	{
		// The object is gone, or the script is, the view stays as the object left it
		_obj->_host = nullptr;
		_obj->Release();
	}

	void ScriptObjectHost::OnUpdate(Actors::ActorBase* actor, float timeMult)
	{
		// The behaviors of JJ2+ count in ticks, 70 of them in a second whatever the frame rate is - in a frame without a tick
		// the object stays as the last one left it, and drawn so (see LevelScriptLoader::OnLevelUpdate()), a few ticks are
		// caught up after a slow frame
		constexpr std::int32_t MaxTicksPerFrame = 4;
		std::int32_t currentTick = _levelScripts->GetGameTick();
		if (_nextTick < 0 || currentTick - _nextTick >= MaxTicksPerFrame) {
			_nextTick = currentTick;
		}
		if (_nextTick > currentTick) {
			return;
		}
		while (_nextTick <= currentTick) {
			_nextTick++;
			if (!UpdateTick(actor)) {
				return;
			}
		}
		// Drawn as the last tick left it
		FinishTicks();
	}

	bool ScriptObjectHost::UpdateTick(Actors::ActorBase* actor)
	{
		// What the object of the game does in a tick, its time is in units of the 60 frames in a second
		_timeMult = 60.0f / 70.0f;
		_drawn = false;
		_drawFrame = false;
		_ranOwnBehavior = false;

		// The frame changes only when the behavior of the game runs (see RunOwnBehavior())
		PullFromActor(_levelScripts, *_obj, *actor, false);

		// A copy keeps the behavior alive while it runs, it may replace itself
		jjBEHAVIOR behavior = _obj->behavior;
		if (!CallScriptBehavior(_levelScripts, *_obj, behavior)) {
			// The script gave the object a behavior of the game again
			if (behavior.behaviorId == aUNKNOWN) {
				// BEHAVIOR::INACTIVE
				Delete();
				return false;
			}
			if (behavior.behaviorId == aEXPLOSION || behavior.behaviorId == BehaviorExplosion2) {
				// What ends a collected pickup and the like, as the game ends its own
				Actors::Explosion::Create(actor->_levelHandler, Vector3i(std::int32_t(_obj->xPos), std::int32_t(_obj->yPos), actor->_renderer.layer()),
					behavior.behaviorId == aEXPLOSION ? Actors::Explosion::Type::Small : Actors::Explosion::Type::Generator);
				Delete();
				return false;
			}
			RunOwnBehavior(true);
		}

		// The game doesn't count `age` itself, the behaviors do and scripts use it for anything
		UpdateSkin(*_obj);
		PushToActor(*_obj, *actor);
		return true;
	}

	void ScriptObjectHost::FinishTicks()
	{
		if (_drawn || _drawFrame) {
			// A frame of an animation a script gave the object, or one it chose, is drawn as it is, the object of the game
			// draws itself otherwise (its frame is the same then)
			bool ownFrame = (_obj->_skinAnim > 0 || _obj->curFrame != _obj->_synced.CurFrame || _obj->_synced.NativeAnim == 0);
			if (ownFrame && _obj->curFrame != 0) {
				DrawFrame(_levelScripts, *_obj);
				_drawn = false;
			} else {
				_drawn = true;
			}
		}
	}

	bool ScriptObjectHost::IsDrawn() const
	{
		return _drawn;
	}

	bool ScriptObjectHost::OnCollision(Actors::ActorBase* actor, Actors::ActorBase* other)
	{
		jjOBJ& obj = *_obj;
		auto* player = runtime_cast<Actors::Player>(other);
		auto* shot = (player == nullptr ? runtime_cast<Actors::Weapons::ShotBase>(other) : nullptr);
		if (player == nullptr && shot == nullptr) {
			return false;
		}

		// What the object is to players, the game handles a pickup as one, anything else as special
		std::uint32_t playerHandling = (obj._playerHandlingSet ? obj._playerHandling
			: (runtime_cast<Actors::Collectibles::CollectibleBase>(actor) != nullptr ? hPICKUP : hSPECIAL));

		if (player != nullptr) {
			if (obj._playerHandlingSet && playerHandling == hPARTICLE) {
				// Players go through it
				return true;
			}
			if (!obj._scriptedCollisions || (playerHandling != hPICKUP && playerHandling != hSPECIAL)) {
				return false;
			}

			jjPLAYER* p = _levelScripts->GetPlayerBackingStore(player);
			if (playerHandling == hPICKUP) {
				// Whatever the script does, the game doesn't collect it
				CallObjectHit(_levelScripts, obj, nullptr, p, 0);
				return true;
			}
			// Only an attack hits a special object, a sugar rush (0) or a special move (1), as JJ2+ tells them apart
			std::int32_t force;
			if (player->HasSugarRush()) {
				force = 0;
			} else if (player->GetSpecialMove() != Actors::Player::SpecialMoveType::None) {
				force = 1;
			} else {
				return false;
			}
			return CallObjectHit(_levelScripts, obj, nullptr, p, force);
		}

		if (obj._bulletHandlingSet) {
			if (obj._bulletHandling == hIGNOREBULLET) {
				return true;
			}
			if (obj._bulletHandling == hDESTROYBULLET) {
				// The bullet stops without harm
				shot->DecreaseHealth(INT32_MAX);
				return true;
			}
		}
		if (!obj._scriptedCollisions || playerHandling != hSPECIAL) {
			return false;
		}

		// The force of a bullet is its damage, as the `animSpeed` of a bullet in JJ2+
		jjOBJ* bullet = _levelScripts->GetObjectView(shot);
		Actors::Player* owner = shot->GetOwner();
		jjPLAYER* p = (owner != nullptr ? _levelScripts->GetPlayerBackingStore(owner) : nullptr);
		return CallObjectHit(_levelScripts, obj, bullet, p, shot->GetStrength());
	}

	void ScriptObjectHost::RunOwnBehavior(bool draw)
	{
		if (_isRunningOwnBehavior) {
			return;
		}

		_isRunningOwnBehavior = true;
		// What the behavior changed so far goes to the object first, what the object did comes back
		UpdateSkin(*_obj);
		PushToActor(*_obj, *_actor);
		_actor->OnUpdate(_timeMult);
		PullFromActor(_levelScripts, *_obj, *_actor);
		// The behaviors of the game leave the start state on their first update
		if (_obj->state == sSTART) {
			_obj->state = sSTILL;
			_obj->_synced.State = sSTILL;
		}
		_isRunningOwnBehavior = false;

		_ranOwnBehavior = true;
		if (draw) {
			_drawn = true;
		}
	}

	void ScriptObjectHost::Draw()
	{
		_drawFrame = true;
	}

	bool ScriptObjectHost::NeedsHost(const jjOBJ& obj)
	{
		// Also a behavior of the game that ends the object, which the object doesn't know itself
		std::uint32_t behaviorId = obj.behavior.behaviorId;
		bool ending = (obj.behavior.function == nullptr && obj.behavior.object == nullptr &&
			(behaviorId == aUNKNOWN || behaviorId == aEXPLOSION || behaviorId == BehaviorExplosion2));
		return (obj.behavior.function != nullptr || obj.behavior.object != nullptr || ending || obj._skinAnim > 0 ||
			obj._scriptedCollisions || obj._playerHandlingSet || obj._bulletHandlingSet);
	}

	void ScriptObjectHost::UpdateSkin(jjOBJ& obj)
	{
		if (obj.curAnim != obj._synced.CurAnim) {
			// The animation of the object of the game shows its own frames again, any other one is shown in their place
			obj._skinAnim = (obj.curAnim > 0 && obj.curAnim != obj._synced.NativeAnim ? obj.curAnim : 0);
			obj._synced.CurAnim = obj.curAnim;
		}
	}

	void ScriptObjectHost::DrawFrame(LevelScriptLoader* levelScripts, const jjOBJ& obj)
	{
		if (ContentResolver::Get().IsHeadless()) {
			return;
		}
		std::int32_t cell = 0;
		auto* graphics = levelScripts->GetAnimationTables().ResolveFrame(obj.curFrame, cell);
		if (graphics == nullptr) {
			return;
		}
		// Frozen or just hit as the game draws the objects, in the sprite layer
		std::uint32_t mode = (obj.freeze != 0 ? spriteType_FROZEN : (obj.justHit != 0 ? spriteType_SINGLECOLOR : spriteType_NORMAL));
		ScriptWorldCanvas::SpriteDraw draw = { graphics, cell, obj.xPos, obj.yPos, obj.direction, 1.0f, 1.0f, 0, mode, 15,
			levelScripts->GetLayerDrawZ(4) };
		levelScripts->GetWorldCanvas().AddSprite(draw);
	}

	void ScriptObjectHost::Delete()
	{
		DeleteActor(*_actor);
	}

	void ScriptObjectHost::Deactivate()
	{
		DeactivateActor(*_actor);
	}

	void ScriptObjectHost::PullFromActor(LevelScriptLoader* levelScripts, jjOBJ& obj, Actors::ActorBase& actor, bool withAnimation)
	{
		obj.xPos = actor._pos.X;
		obj.yPos = actor._pos.Y;
		obj.xSpeed = actor._speed.X;
		obj.ySpeed = actor._speed.Y;
		obj.direction = (actor.IsFacingLeft() ? -1 : 1);
		obj.energy = std::int8_t(std::clamp<std::int32_t>(actor._health, INT8_MIN, INT8_MAX));
		if (actor._health <= 0 || actor.GetState(Actors::ActorState::IsDestroyed)) {
			obj.state = sKILL;
		}

		obj._synced.XPos = obj.xPos;
		obj._synced.YPos = obj.yPos;
		obj._synced.XSpeed = obj.xSpeed;
		obj._synced.YSpeed = obj.ySpeed;
		obj._synced.Direction = obj.direction;
		obj._synced.Energy = obj.energy;
		obj._synced.State = obj.state;

		if (withAnimation) {
			// The frame of the animation the object shows, or of the one a script gave it in its place, of the same position
			auto& tables = levelScripts->GetAnimationTables();
			std::uint32_t nativeAnim = tables.FindAnimationOfGraphics(actor._renderer.FrameSource);
			std::uint32_t shownAnim = (obj._skinAnim > 0 ? std::uint32_t(obj._skinAnim) : nativeAnim);
			std::uint32_t frameCount, firstFrame;
			if (shownAnim != 0 && tables.GetAnimationFrames(shownAnim, frameCount, firstFrame) && frameCount > 0) {
				std::uint32_t position = std::uint32_t(std::max<std::int32_t>(actor._renderer.CurrentFrame, 0)) % frameCount;
				obj.curAnim = std::int16_t(shownAnim);
				obj.frameID = std::int8_t(position);
				obj.curFrame = firstFrame + position;
			}
			obj._synced.NativeAnim = std::int32_t(nativeAnim);
			obj._synced.CurAnim = obj.curAnim;
			obj._synced.CurFrame = obj.curFrame;
		}
	}

	void ScriptObjectHost::PushToActor(jjOBJ& obj, Actors::ActorBase& actor)
	{
		jjOBJ::SyncedState& synced = obj._synced;

		if (obj.xPos != synced.XPos || obj.yPos != synced.YPos) {
			actor.MoveInstantly(Vector2f(obj.xPos, obj.yPos), Actors::MoveType::Absolute | Actors::MoveType::Force);
		}
		if (obj.xSpeed != synced.XSpeed || obj.ySpeed != synced.YSpeed) {
			actor._speed = Vector2f(obj.xSpeed, obj.ySpeed);
		}
		if (obj.direction != synced.Direction && obj.direction != 0) {
			actor.SetFacingLeft(obj.direction < 0);
		}
		if (obj.energy != synced.Energy) {
			if (obj.energy <= 0) {
				// No energy kills the object as the game does
				if (actor._health > 0) {
					actor.DecreaseHealth(INT32_MAX);
				}
			} else {
				actor.SetHealth(obj.energy);
			}
		}
		if (obj.state != synced.State) {
			if (obj.state == sKILL) {
				if (actor._health > 0) {
					actor.DecreaseHealth(INT32_MAX);
				}
			} else if (obj.state == sDEACTIVATE) {
				DeactivateActor(actor);
			}
		}

		synced.XPos = obj.xPos;
		synced.YPos = obj.yPos;
		synced.XSpeed = obj.xSpeed;
		synced.YSpeed = obj.ySpeed;
		synced.Direction = obj.direction;
		synced.Energy = obj.energy;
		synced.State = obj.state;
	}

	Vector2f ScriptObjectHost::GetSpawnPos(const Actors::ActorBase& actor)
	{
		return Vector2f(float(actor._spawnPos.X), float(actor._spawnPos.Y));
	}

	void ScriptObjectHost::DeleteActor(Actors::ActorBase& actor)
	{
		// The event stays as it is, its object isn't spawned again
		actor.SetState(Actors::ActorState::IsDestroyed, true);
	}

	void ScriptObjectHost::DeactivateActor(Actors::ActorBase& actor)
	{
		// The event spawns its object again when it's seen again
		if (actor.GetState(Actors::ActorState::IsCreatedFromEventMap)) {
			if (auto* events = actor._levelHandler->EventMap()) {
				events->Deactivate(actor._originTile.X, actor._originTile.Y);
			}
		}
		actor.SetState(Actors::ActorState::IsDestroyed, true);
	}

	namespace
	{
		void LogBehaviorException(asIScriptContext* ctx)
		{
			std::int32_t column = 0; const char* sectionName = nullptr;
			std::int32_t line = ctx->GetExceptionLineNumber(&column, &sectionName);
			LOGE("{}:{}({}) ‡ An exception \"{}\" occurred in \"{}\". Please correct the code and try again.",
				sectionName, line, column, ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}
	}

	bool ScriptObjectHost::CallScriptBehavior(LevelScriptLoader* levelScripts, jjOBJ& obj, const jjBEHAVIOR& behavior, bool draw)
	{
		if (behavior.function == nullptr && behavior.object == nullptr) {
			return false;
		}

		asIScriptEngine* engine = levelScripts->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();
		std::int32_t r = asSUCCESS;
		if (behavior.object != nullptr) {
			// The object may lose its behavior while it runs, which would free the instance
			asIScriptObject* instance = behavior.object;
			instance->AddRef();
			asITypeInfo* type = instance->GetObjectType();
			asIScriptFunction* onBehave = type->GetMethodByDecl("void onBehave(jjOBJ@ obj)");
			if (onBehave != nullptr) {
				ctx->Prepare(onBehave);
				ctx->SetObject(instance);
				ctx->SetArgObject(0, &obj);
				r = ctx->Execute();
			}
			// What relates to drawing the object, called every time the behavior is unless it's told not to draw
			if (draw && r != asEXECUTION_EXCEPTION) {
				if (asIScriptFunction* onDraw = type->GetMethodByDecl("void onDraw(jjOBJ@ obj)")) {
					ctx->Prepare(onDraw);
					ctx->SetObject(instance);
					ctx->SetArgObject(0, &obj);
					r = ctx->Execute();
				}
			}
			instance->Release();
		} else {
			ctx->Prepare(behavior.function);
			ctx->SetArgObject(0, &obj);
			r = ctx->Execute();
		}
		if (r == asEXECUTION_EXCEPTION) {
			LogBehaviorException(ctx);
		}
		engine->ReturnContext(ctx);
		return true;
	}

	bool ScriptObjectHost::CallObjectHit(LevelScriptLoader* levelScripts, jjOBJ& obj, jjOBJ* bullet, jjPLAYER* player, std::int32_t force)
	{
		asIScriptEngine* engine = levelScripts->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		// The method of the behavior, if it has one, decides first
		if (asIScriptObject* instance = obj.behavior.object) {
			instance->AddRef();
			if (asIScriptFunction* onObjectHit = instance->GetObjectType()->GetMethodByDecl("bool onObjectHit(jjOBJ@, jjOBJ@, jjPLAYER@, int)")) {
				ctx->Prepare(onObjectHit);
				ctx->SetObject(instance);
				ctx->SetArgObject(0, &obj);
				ctx->SetArgObject(1, bullet);
				ctx->SetArgObject(2, player);
				ctx->SetArgDWord(3, std::uint32_t(force));
				std::int32_t r = ctx->Execute();
				bool handled = (r == asEXECUTION_FINISHED && ctx->GetReturnByte() != 0);
				if (r == asEXECUTION_EXCEPTION) {
					LogBehaviorException(ctx);
				}
				if (handled) {
					instance->Release();
					engine->ReturnContext(ctx);
					return true;
				}
			}
			instance->Release();
		}

		// Then the global ones, kept by JJ2+ for older scripts
		bool called = false;
		if (asIScriptFunction* onObjectHit = levelScripts->GetMainModule()->GetFunctionByDecl("void onObjectHit(jjOBJ@, jjOBJ@, jjPLAYER@, int)")) {
			ctx->Prepare(onObjectHit);
			ctx->SetArgObject(0, &obj);
			ctx->SetArgObject(1, bullet);
			ctx->SetArgObject(2, player);
			ctx->SetArgDWord(3, std::uint32_t(force));
			if (ctx->Execute() == asEXECUTION_EXCEPTION) {
				LogBehaviorException(ctx);
			}
			called = true;
		}
		engine->ReturnContext(ctx);
		return called;
	}

	void jjOBJ::CopyTemplate(const jjOBJ& preset)
	{
		xOrg = preset.xOrg;
		yOrg = preset.yOrg;
		xPos = preset.xPos;
		yPos = preset.yPos;
		xSpeed = preset.xSpeed;
		ySpeed = preset.ySpeed;
		xAcc = preset.xAcc;
		yAcc = preset.yAcc;
		counter = preset.counter;
		curFrame = preset.curFrame;
		age = preset.age;
		creator = preset.creator;
		curAnim = preset.curAnim;
		killAnim = preset.killAnim;
		freeze = preset.freeze;
		lightType = preset.lightType;
		frameID = preset.frameID;
		noHit = preset.noHit;
		energy = preset.energy;
		light = preset.light;
		objType = preset.objType;
		state = preset.state;
		points = preset.points;
		eventID = preset.eventID;
		direction = preset.direction;
		justHit = preset.justHit;
		oldState = preset.oldState;
		animSpeed = preset.animSpeed;
		special = preset.special;
		doesHurt = preset.doesHurt;
		counterEnd = preset.counterEnd;
		behavior = preset.behavior;

		std::memcpy(_vars, preset._vars, sizeof(_vars));
		_creatorID = preset._creatorID;
		_creatorType = preset._creatorType;
		_bulletHandling = preset._bulletHandling;
		_playerHandling = preset._playerHandling;
		_ricochetProp = preset._ricochetProp;
		_freezable = preset._freezable;
		_blastable = preset._blastable;
		_isTarget = preset._isTarget;
		_triggersTNT = preset._triggersTNT;
		_deactivates = preset._deactivates;
		_scriptedCollisions = preset._scriptedCollisions;
		_bulletHandlingSet = preset._bulletHandlingSet;
		_playerHandlingSet = preset._playerHandlingSet;

		// An animation a script gave the template is the one the object shows (a template has none of its own, see
		// LevelScriptLoader::GetObjectPreset())
		_skinAnim = (preset.curAnim > 0 ? preset.curAnim : 0);
	}
}

namespace Jazz2::Scripting
{
	void LevelScriptLoader::OnActorAdded(const std::shared_ptr<Actors::ActorBase>& actor)
	{
		_lastObjectId = 0;

		// The bullets of the players are the objects of the events of their weapons, the powered-up ones 9 further, as in
		// JJ2+ - the players and effects are no objects of events
		std::uint8_t eventId;
		std::int32_t creatorId = -1;
		if (auto* shot = runtime_cast<Actors::Weapons::ShotBase>(actor.get())) {
			std::int32_t weapon = std::int32_t(shot->GetWeaponType());
			if (weapon < 0 || weapon >= std::int32_t(WeaponType::Count) || weapon == std::int32_t(WeaponType::TNT)) {
				return;
			}
			eventId = std::uint8_t(Legacy::aPLAYERBULLET1 + weapon + (shot->IsUpgraded() ? 9 : 0));
			if (Actors::Player* owner = shot->GetOwner()) {
				creatorId = owner->GetPlayerIndex();
			}
		} else if (runtime_cast<Actors::Weapons::TNT>(actor.get()) != nullptr) {
			eventId = Legacy::aTNT;
		} else {
			EventType type = actor->GetSpawnEventType();
			if (type == EventType::Empty) {
				return;
			}
			eventId = std::uint8_t(GetEventTranslator().ToOriginal(type, actor->GetSpawnEventParams()) & 0xFF);
		}

		std::int32_t objectId = RegisterObject(actor, nullptr, eventId);
		_lastObjectId = objectId;
		if (objectId <= 0) {
			return;
		}
		if (creatorId >= 0) {
			_objectSlots[objectId].CreatorType = Legacy::cPLAYER;
			_objectSlots[objectId].CreatorID = std::uint16_t(creatorId);
		}

		// A template no script asked for is as the game has it
		Legacy::jjOBJ* preset = _objectPresets[eventId];
		if (preset == nullptr) {
			return;
		}

		// An animation of the template is what the objects show (see jjOBJ::CopyTemplate())
		if (Legacy::ScriptObjectHost::NeedsHost(*preset) || preset->curAnim > 0) {
			if (Legacy::jjOBJ* obj = EnsureObjectView(objectId)) {
				HostObject(obj, actor.get());
			}
		} else if (preset->energy != preset->_synced.Energy) {
			// The energy a script gave the template
			actor->SetHealth(std::max<std::int32_t>(preset->energy, 1));
		}
	}

	Legacy::jjOBJ* LevelScriptLoader::GetObjectPreset(std::uint8_t eventId)
	{
		Legacy::jjOBJ*& preset = _objectPresets[eventId];
		if (preset == nullptr) {
			void* mem = asAllocMem(sizeof(Legacy::jjOBJ));
			preset = new(mem) Legacy::jjOBJ();
			preset->eventID = eventId;
			preset->behavior = Legacy::DefaultBehavior;
			preset->state = Legacy::sSTART;
			// Not as set by a script, it doesn't change how the objects are handled
			preset->_playerHandling = Legacy::GetDefaultPlayerHandling(eventId);
			// What a script changes of the template is told apart by this, see OnActorAdded()
			preset->_synced.Energy = preset->energy;
		}
		preset->AddRef();
		return preset;
	}

	Legacy::jjOBJ* LevelScriptLoader::GetObjectById(std::int32_t objectId)
	{
		Legacy::jjOBJ* obj = (objectId > 0 && objectId < std::int32_t(_objectSlots.size()) ? EnsureObjectView(objectId) : nullptr);
		if (obj == nullptr) {
			// An object ID nothing has gets an inactive object
			void* mem = asAllocMem(sizeof(Legacy::jjOBJ));
			obj = new(mem) Legacy::jjOBJ();
			obj->objectID = std::int16_t(objectId);
			return obj;
		}
		obj->AddRef();
		return obj;
	}

	std::int32_t LevelScriptLoader::RegisterObject(const std::shared_ptr<Actors::ActorBase>& actor, Legacy::jjOBJ* object, std::uint8_t eventId)
	{
		if (_objectSlots.empty()) {
			// Object ID 0 is never used
			_objectSlots.push_back({});
		}

		// The lowest object ID that's free, as JJ2+ reuses them
		std::int32_t objectId = 0;
		for (std::int32_t i = 1; i < std::int32_t(_objectSlots.size()); i++) {
			if (_objectSlots[i].Actor.expired()) {
				objectId = i;
				break;
			}
		}
		if (objectId == 0) {
			if (std::int32_t(_objectSlots.size()) >= jjObjectMax) {
				return 0;
			}
			objectId = std::int32_t(_objectSlots.size());
			_objectSlots.push_back({});
		}

		ObjectSlot& slot = _objectSlots[objectId];
		if (slot.Object != nullptr) {
			// The view of the object that had the ID before stays inactive for whoever holds it
			slot.Object->Release();
		}
		slot.Actor = actor;
		slot.Object = object;
		slot.EventID = eventId;
		// An object of an event is made by the level
		slot.CreatorType = 1;
		slot.CreatorID = 0;
		if (object != nullptr) {
			object->objectID = std::int16_t(objectId);
		}

		jjObjectCount = std::max<std::int32_t>(jjObjectCount, objectId + 1);
		return objectId;
	}

	Legacy::jjOBJ* LevelScriptLoader::EnsureObjectView(std::int32_t objectId)
	{
		ObjectSlot& slot = _objectSlots[objectId];
		if (slot.Object != nullptr) {
			return slot.Object;
		}

		std::shared_ptr<Actors::ActorBase> actor = slot.Actor.lock();
		if (actor == nullptr) {
			return nullptr;
		}

		// An object is built from its template, as in JJ2+
		void* mem = asAllocMem(sizeof(Legacy::jjOBJ));
		Legacy::jjOBJ* obj = new(mem) Legacy::jjOBJ();
		if (Legacy::jjOBJ* preset = _objectPresets[slot.EventID]) {
			obj->CopyTemplate(*preset);
		} else {
			obj->behavior = Legacy::DefaultBehavior;
			obj->state = Legacy::sSTART;
			obj->_playerHandling = Legacy::GetDefaultPlayerHandling(slot.EventID);
		}
		obj->eventID = slot.EventID;
		obj->objectID = std::int16_t(objectId);
		obj->_actor = actor;
		obj->_creatorType = slot.CreatorType;
		obj->_creatorID = slot.CreatorID;
		Vector2f spawnPos = Legacy::ScriptObjectHost::GetSpawnPos(*actor);
		obj->xOrg = spawnPos.X;
		obj->yOrg = spawnPos.Y;
		Legacy::ScriptObjectHost::PullFromActor(this, *obj, *actor);

		slot.Object = obj;
		return obj;
	}

	Legacy::jjOBJ* LevelScriptLoader::GetObjectView(Actors::ActorBase* actor)
	{
		for (std::int32_t i = 1; i < std::int32_t(_objectSlots.size()); i++) {
			if (!_objectSlots[i].Actor.expired() && _objectSlots[i].Actor.lock().get() == actor) {
				return EnsureObjectView(i);
			}
		}
		return nullptr;
	}

	void LevelScriptLoader::HostObject(Legacy::jjOBJ* obj, Actors::ActorBase* actor)
	{
		if (obj->_host == nullptr) {
			actor->SetUpdateOverride(std::make_unique<Legacy::ScriptObjectHost>(this, obj, actor));
		}
	}

	void LevelScriptLoader::SyncObjectsFromActors()
	{
		for (std::size_t i = 1; i < _objectSlots.size(); i++) {
			ObjectSlot& slot = _objectSlots[i];
			// A driven object and one a script controls entirely keep their views themselves
			if (slot.Object == nullptr || slot.Object->_host != nullptr || slot.Object->_wrapper != nullptr) {
				continue;
			}
			if (std::shared_ptr<Actors::ActorBase> actor = slot.Actor.lock()) {
				Legacy::ScriptObjectHost::PullFromActor(this, *slot.Object, *actor);
			}
		}
	}

	void LevelScriptLoader::SyncObjectsToActors()
	{
		for (std::size_t i = 1; i < _objectSlots.size(); i++) {
			ObjectSlot& slot = _objectSlots[i];
			if (slot.Object == nullptr || slot.Object->_wrapper != nullptr) {
				continue;
			}
			std::shared_ptr<Actors::ActorBase> actor = slot.Actor.lock();
			if (actor == nullptr) {
				continue;
			}

			Legacy::ScriptObjectHost::UpdateSkin(*slot.Object);
			Legacy::ScriptObjectHost::PushToActor(*slot.Object, *actor);
			// A script gave a running object a behavior or an animation of its own, or handles its collisions
			if (slot.Object->_host == nullptr && Legacy::ScriptObjectHost::NeedsHost(*slot.Object)) {
				HostObject(slot.Object, actor.get());
			}
		}
	}

	void LevelScriptLoader::ReleaseObjects()
	{
		for (std::size_t i = 1; i < _objectSlots.size(); i++) {
			ObjectSlot& slot = _objectSlots[i];
			if (slot.Object == nullptr) {
				continue;
			}
			// An object the script drives stops, the script goes away
			if (slot.Object->_host != nullptr) {
				if (std::shared_ptr<Actors::ActorBase> actor = slot.Actor.lock()) {
					if (actor->GetUpdateOverride() == static_cast<Actors::IActorUpdateOverride*>(slot.Object->_host)) {
						actor->SetUpdateOverride(nullptr);
					}
				}
			}
			slot.Object->Release();
			slot.Object = nullptr;
		}
		_objectSlots.clear();

		for (Legacy::jjOBJ*& preset : _objectPresets) {
			if (preset != nullptr) {
				preset->Release();
				preset = nullptr;
			}
		}
	}
}

#endif
