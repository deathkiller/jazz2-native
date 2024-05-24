#if defined(WITH_ANGELSCRIPT)

#include "ScriptActorWrapper.h"
#include "ScriptPlayerWrapper.h"
#include "LevelScriptLoader.h"
#include "RegisterArray.h"
#include "RegisterRef.h"
#include "../ILevelHandler.h"
#include "../Events/EventSpawner.h"
#include "../Actors/Explosion.h"
#include "../Actors/Player.h"
#include "../Actors/Weapons/ShotBase.h"
#include "../Actors/Weapons/TNT.h"
#include "../Actors/Enemies/TurtleShell.h"

#include "../../nCine/Base/FrameTimer.h"
#include "../../nCine/Base/Random.h"

using namespace Jazz2::Actors;
using namespace Jazz2::Tiles;
using namespace nCine;

#define AsClassName "ActorBase"
#define AsClassNameInternal "ActorBaseInternal"

namespace Jazz2::Scripting
{
	ScriptActorWrapper::ScriptActorWrapper(LevelScriptLoader* levelScripts, asIScriptObject* obj)
		: _levelScripts(levelScripts), _obj(obj), _refCount(1), _scoreValue(0)
	{
		_isDead = obj->GetWeakRefFlag();
		_isDead->AddRef();

		_onTileDeactivated = _obj->GetObjectType()->GetMethodByDecl("bool OnTileDeactivated()");
		_onHealthChanged = _obj->GetObjectType()->GetMethodByDecl("void OnHealthChanged()");
		_onUpdate = _obj->GetObjectType()->GetMethodByDecl("void OnUpdate(float)");
		_onUpdateHitbox = _obj->GetObjectType()->GetMethodByDecl("void OnUpdateHitbox()");
		_onHandleCollision = _obj->GetObjectType()->GetMethodByDecl("bool OnHandleCollision(ref other)");
		_onHitFloor = _obj->GetObjectType()->GetMethodByDecl("void OnHitFloor(float)");
		_onHitCeiling = _obj->GetObjectType()->GetMethodByDecl("void OnHitCeiling(float)");
		_onHitWall = _obj->GetObjectType()->GetMethodByDecl("void OnHitWall(float)");
		_onAnimationStarted = _obj->GetObjectType()->GetMethodByDecl("void OnAnimationStarted()");
		_onAnimationFinished = _obj->GetObjectType()->GetMethodByDecl("void OnAnimationFinished()");
	}

	ScriptActorWrapper::~ScriptActorWrapper()
	{
		// This had to be added to release the object properly
		_obj->Release();

		_isDead->Release();
	}

	void ScriptActorWrapper::RegisterFactory(asIScriptEngine* engine, asIScriptModule* module)
	{
		static const char AsLibrary[] = R"(
shared abstract class )" AsClassName R"(
{
	// Allow scripts to create instances
	)" AsClassName R"(()
	{
		// Create the C++ side of the proxy
		@_obj = )" AsClassNameInternal R"((GetActorType());  
	}

	// The copy constructor performs a deep copy
	)" AsClassName R"((const )" AsClassName R"( &o)
	{
		// Create a new C++ instance and copy content
		@_obj = )" AsClassNameInternal R"((GetActorType());
		_obj = o._obj;
	}

	// Do a deep a copy of the C++ object
	)" AsClassName R"( &opAssign(const )" AsClassName R"( &o)
	{
		// copy content of C++ instance
		_obj = o._obj;
		return this;
	}

	// The script class can be implicitly cast to the C++ type through the opImplCast method
	)" AsClassNameInternal R"( @opImplCast() { return _obj; }

	// Hold a reference to the C++ side of the proxy
	protected )" AsClassNameInternal R"( @_obj;

	protected int GetActorType() { return 0; }

	// Overridable events
	//bool OnActivated(array<uint8> &in eventParams) { return false; }
	//bool OnTileDeactivate(int tx1, int ty1, int tx2, int ty2) { return true; }
	//void OnHealthChanged() { }
	//bool OnPerish() { return true; }
	//void OnUpdate(float timeMult) { }
	//void OnUpdateHitbox() { }

	//void OnHitFloor(float timeMult) { }
	//void OnHitCeiling(float timeMult) { }
	//void OnHitWall(float timeMult) { }

	//void OnAnimationStarted() { }
	//void OnAnimationFinished() { }

	// Properties
	float X { get const { return _obj.X; } }
	float Y { get const { return _obj.Y; } }
	float SpeedX { get const { return _obj.SpeedX; } set { _obj.SpeedX = value; } }
	float SpeedY { get const { return _obj.SpeedY; } set { _obj.SpeedY = value; } }
	float ExternalForceX { get const { return _obj.ExternalForceX; } set { _obj.ExternalForceX = value; } }
	float ExternalForceY { get const { return _obj.ExternalForceY; } set { _obj.ExternalForceY = value; } }
	float Elasticity { get const { return _obj.Elasticity; } set { _obj.Elasticity = value; } }
	float Friction { get const { return _obj.Friction; } set { _obj.Friction = value; } }
	int Health { get const { return _obj.Health; } set { _obj.Health = value; } }
	float Alpha { get const { return _obj.Alpha; } set { _obj.Alpha = value; } }
	uint16 Layer { get const { return _obj.Layer; } set { _obj.Layer = value; } }

	// Methods
	void DecreaseHealth(int amount) { _obj.DecreaseHealth(amount); }
	bool MoveTo(float x, float y, bool force = false) { return _obj.MoveTo(x, y, force); }
	bool MoveBy(float x, float y, bool force = false) { return _obj.MoveBy(x, y, force); }
	void TryStandardMovement(float timeMult) { _obj.TryStandardMovement(timeMult); }

	void RequestMetadata(const string &in path) { _obj.RequestMetadata(path); }
	void PlaySfx(const string &in identifier, float gain = 1.0, float pitch = 1.0) { _obj.PlaySfx(identifier, gain, pitch); }
	void SetAnimation(int state) { _obj.SetAnimation(state); }
}

shared abstract class CollectibleBase : )" AsClassName R"(
{
	protected int GetActorType() final { return 1; }

	// Overridable events
	//bool OnCollect(Player@ player) { return true; }

	// Properties
	int ScoreValue { get const { return _obj.ScoreValue; } set { _obj.ScoreValue = value; } }
}
)";
		int r;
		r = engine->RegisterObjectType(AsClassNameInternal, 0, asOBJ_REF); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour(AsClassNameInternal, asBEHAVE_FACTORY, AsClassNameInternal " @f(int)", asFUNCTION(ScriptActorWrapper::Factory), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour(AsClassNameInternal, asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptActorWrapper, AddRef), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour(AsClassNameInternal, asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptActorWrapper, Release), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, AsClassNameInternal " &opAssign(const " AsClassNameInternal " &in)", asMETHOD(ScriptActorWrapper, operator=), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectProperty(AsClassNameInternal, "float X", asOFFSET(ScriptActorWrapper, _pos.X)); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectProperty(AsClassNameInternal, "float Y", asOFFSET(ScriptActorWrapper, _pos.Y)); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectProperty(AsClassNameInternal, "float SpeedX", asOFFSET(ScriptActorWrapper, _speed.X)); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectProperty(AsClassNameInternal, "float SpeedY", asOFFSET(ScriptActorWrapper, _speed.Y)); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectProperty(AsClassNameInternal, "float ExternalForceX", asOFFSET(ScriptActorWrapper, _externalForce.X)); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectProperty(AsClassNameInternal, "float ExternalForceY", asOFFSET(ScriptActorWrapper, _externalForce.Y)); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectProperty(AsClassNameInternal, "float Elasticity", asOFFSET(ScriptActorWrapper, _elasticity)); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectProperty(AsClassNameInternal, "float Friction", asOFFSET(ScriptActorWrapper, _friction)); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectProperty(AsClassNameInternal, "int Health", asOFFSET(ScriptActorWrapper, _health)); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectProperty(AsClassNameInternal, "int ScoreValue", asOFFSET(ScriptActorWrapper, _scoreValue)); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod(AsClassNameInternal, "float get_Alpha() const property", asMETHOD(ScriptActorWrapper, asGetAlpha), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, "void set_Alpha(float) property", asMETHOD(ScriptActorWrapper, asSetAlpha), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, "uint16 get_Layer() const property", asMETHOD(ScriptActorWrapper, asGetLayer), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, "void set_Layer(uint16) property", asMETHOD(ScriptActorWrapper, asSetLayer), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod(AsClassNameInternal, "void DecreaseHealth(int)", asMETHOD(ScriptActorWrapper, asDecreaseHealth), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, "bool MoveTo(float, float, bool)", asMETHOD(ScriptActorWrapper, asMoveTo), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, "bool MoveBy(float, float, bool)", asMETHOD(ScriptActorWrapper, asMoveBy), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, "void TryStandardMovement(float)", asMETHOD(ScriptActorWrapper, asTryStandardMovement), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, "void RequestMetadata(const string &in)", asMETHOD(ScriptActorWrapper, asRequestMetadata), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, "void PlaySfx(const string &in, float, float)", asMETHOD(ScriptActorWrapper, asPlaySfx), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassNameInternal, "void SetAnimation(int)", asMETHOD(ScriptActorWrapper, asSetAnimationState), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = module->AddScriptSection("__" AsClassName, AsLibrary, arraySize(AsLibrary) - 1, 0); RETURN_ASSERT(r >= 0);
	}

	ScriptActorWrapper* ScriptActorWrapper::Factory(int actorType)
	{
		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		// Get the function that is calling the factory, so we can be certain it is the our internal script class
		asIScriptFunction* func = ctx->GetFunction(0);
		if (func->GetObjectType() == nullptr || StringView(func->GetObjectType()->GetName()) != StringView(AsClassName)) {
			ctx->SetException("Cannot manually instantiate " AsClassNameInternal);
			return nullptr;
		}

		asIScriptObject* obj = static_cast<asIScriptObject*>(ctx->GetThisPointer());
		switch (actorType) {
			default:
			case 0: {
				void* mem = asAllocMem(sizeof(ScriptActorWrapper));
				return new(mem) ScriptActorWrapper(owner, obj);
			}
			case 1: {
				void* mem = asAllocMem(sizeof(ScriptCollectibleWrapper));
				return new(mem) ScriptCollectibleWrapper(owner, obj);
			}
		}
	}

	void ScriptActorWrapper::AddRef()
	{
		_refCount++;

		// Increment also the reference counter to the script side, so it isn't accidentally destroyed before the C++ side
		if (!_isDead->Get()) {
			_obj->AddRef();
		}
	}

	void ScriptActorWrapper::Release()
	{
		// Release the script instance too
		if (!_isDead->Get()) {
			_obj->Release();
		}
		if (--_refCount == 0) {
			this->~ScriptActorWrapper();
			asFreeMem(this);
		}
	}

	Task<bool> ScriptActorWrapper::OnActivatedAsync(const Actors::ActorActivationDetails& details)
	{
		if (_isDead->Get()) {
			async_return false;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptFunction* func = _obj->GetObjectType()->GetMethodByDecl("bool OnActivated(array<uint8> &in)");
		if (func == nullptr) {
			async_return false;
		}

		SetState(ActorState::CollideWithOtherActors, _onHandleCollision != nullptr);

		CScriptArray* eventParams = CScriptArray::Create(engine->GetTypeInfoByDecl("array<uint8>"), Events::EventSpawner::SpawnParamsSize);
		std::memcpy(eventParams->At(0), details.Params, Events::EventSpawner::SpawnParamsSize);

		asIScriptContext* ctx = engine->RequestContext();
		ctx->Prepare(func);
		ctx->SetObject(_obj);
		ctx->SetArgObject(0, eventParams);
		int r = ctx->Execute();
		bool result;
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			result = true;
		} else {
			result = (ctx->GetReturnByte() != 0);
		}

		eventParams->Release();
		engine->ReturnContext(ctx);

		async_return result;
	}

	bool ScriptActorWrapper::OnTileDeactivated()
	{
		if (_onTileDeactivated == nullptr || _isDead->Get()) {
			return true;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_onTileDeactivated);
		ctx->SetObject(_obj);
		int r = ctx->Execute();
		bool result;
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			result = true;
		} else {
			result = (ctx->GetReturnByte() != 0);
		}

		engine->ReturnContext(ctx);

		return result;
	}

	void ScriptActorWrapper::OnHealthChanged(ActorBase* collider)
	{
		if (_onHealthChanged == nullptr || _isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_onHealthChanged);
		ctx->SetObject(_obj);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	bool ScriptActorWrapper::OnPerish(ActorBase* collider)
	{
		if (_isDead->Get()) {
			return ActorBase::OnPerish(collider);
		}

		asIScriptFunction* func = _obj->GetObjectType()->GetMethodByDecl("bool OnPerish()");
		if (func == nullptr) {
			return ActorBase::OnPerish(collider);
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(func);
		ctx->SetObject(_obj);
		int r = ctx->Execute();
		bool result;
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			result = true;
		} else {
			result = (ctx->GetReturnByte() != 0);
		}

		engine->ReturnContext(ctx);

		return (result && ActorBase::OnPerish(collider));
	}

	void ScriptActorWrapper::OnUpdate(float timeMult)
	{
		if (_onUpdate == nullptr || _isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_onUpdate);
		ctx->SetObject(_obj);
		ctx->SetArgFloat(0, timeMult);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	void ScriptActorWrapper::OnUpdateHitbox()
	{
		if (_onUpdateHitbox == nullptr || _isDead->Get()) {
			// Call base implementation if not overriden
			ActorBase::OnUpdateHitbox();
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_onUpdateHitbox);
		ctx->SetObject(_obj);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	bool ScriptActorWrapper::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (_onHandleCollision != nullptr) {
			if (auto* otherWrapper = runtime_cast<ScriptActorWrapper*>(other)) {
				asIScriptEngine* engine = _obj->GetEngine();
				asITypeInfo* typeInfo = _levelScripts->GetMainModule()->GetTypeInfoByName(AsClassName);
				if (typeInfo != nullptr) {
					asIScriptContext* ctx = engine->RequestContext();

					CScriptHandle handle(otherWrapper->_obj, typeInfo);
					ctx->Prepare(_onHandleCollision);
					ctx->SetObject(_obj);
					int p = ctx->SetArgObject(0, &handle);
					int r = ctx->Execute();
					bool result;
					if (r == asEXECUTION_EXCEPTION) {
						LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
						result = true;
					} else {
						result = (ctx->GetReturnByte() != 0);
					}

					engine->ReturnContext(ctx);

					if (result) {
						return true;
					}
				}
			} else if (auto* player = runtime_cast<Player*>(other)) {
				asIScriptEngine* engine = _obj->GetEngine();
				asITypeInfo* typeInfo = engine->GetTypeInfoByName("Player");
				if (typeInfo != nullptr) {
					asIScriptContext* ctx = engine->RequestContext();

					void* mem = asAllocMem(sizeof(ScriptPlayerWrapper));
					ScriptPlayerWrapper* playerWrapper = new(mem) ScriptPlayerWrapper(_levelScripts, player);

					CScriptHandle handle(playerWrapper, typeInfo);
					ctx->Prepare(_onHandleCollision);
					ctx->SetObject(_obj);
					int p = ctx->SetArgObject(0, &handle);
					int r = ctx->Execute();
					bool result;
					if (r == asEXECUTION_EXCEPTION) {
						LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
						result = true;
					} else {
						result = (ctx->GetReturnByte() != 0);
					}

					engine->ReturnContext(ctx);

					playerWrapper->Release();

					if (result) {
						return true;
					}
				}
			}
		}

		return ActorBase::OnHandleCollision(other);
	}

	bool ScriptActorWrapper::OnDraw(RenderQueue& renderQueue)
	{
		// TODO
		return ActorBase::OnDraw(renderQueue);
	}

	void ScriptActorWrapper::OnHitFloor(float timeMult)
	{
		if (_onHitFloor == nullptr || _isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_onHitFloor);
		ctx->SetObject(_obj);
		ctx->SetArgFloat(0, timeMult);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	void ScriptActorWrapper::OnHitCeiling(float timeMult)
	{
		if (_onHitCeiling == nullptr || _isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_onHitCeiling);
		ctx->SetObject(_obj);
		ctx->SetArgFloat(0, timeMult);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	void ScriptActorWrapper::OnHitWall(float timeMult)
	{
		if (_onHitWall == nullptr || _isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_onHitWall);
		ctx->SetObject(_obj);
		ctx->SetArgFloat(0, timeMult);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	void ScriptActorWrapper::OnAnimationStarted()
	{
		if (_onAnimationStarted == nullptr || _isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_onAnimationStarted);
		ctx->SetObject(_obj);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	void ScriptActorWrapper::OnAnimationFinished()
	{
		// Always call base implementation
		ActorBase::OnAnimationFinished();

		if (_onAnimationFinished == nullptr || _isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_onAnimationFinished);
		ctx->SetObject(_obj);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	float ScriptActorWrapper::asGetAlpha() const
	{
		return _renderer.alpha();
	}

	void ScriptActorWrapper::asSetAlpha(float value)
	{
		_renderer.setAlphaF(value);
	}

	uint16_t ScriptActorWrapper::asGetLayer() const
	{
		return _renderer.layer();
	}

	void ScriptActorWrapper::asSetLayer(uint16_t value)
	{
		_renderer.setLayer(value);
	}

	void ScriptActorWrapper::asDecreaseHealth(int amount)
	{
		DecreaseHealth(amount);
	}

	bool ScriptActorWrapper::asMoveTo(float x, float y, bool force)
	{
		return MoveInstantly(Vector2f(x, y), (force ? MoveType::Absolute | MoveType::Force : MoveType::Absolute));
	}

	bool ScriptActorWrapper::asMoveBy(float x, float y, bool force)
	{
		return MoveInstantly(Vector2f(x, y), (force ? MoveType::Relative | MoveType::Force : MoveType::Relative));
	}

	void ScriptActorWrapper::asTryStandardMovement(float timeMult)
	{
		TileCollisionParams params = { TileDestructType::None, _speed.Y >= 0.0f };
		TryStandardMovement(timeMult, params);
	}

	void ScriptActorWrapper::asRequestMetadata(const String& path)
	{
		RequestMetadata(path);
	}

	void ScriptActorWrapper::asPlaySfx(const String& identifier, float gain, float pitch)
	{
		PlaySfx(identifier, gain, pitch);
	}

	void ScriptActorWrapper::asSetAnimationState(int state)
	{
		SetAnimation((AnimState)state);
	}

	ScriptCollectibleWrapper::ScriptCollectibleWrapper(LevelScriptLoader* levelScripts, asIScriptObject* obj)
		:
		ScriptActorWrapper(levelScripts, obj),
		_untouched(true),
		_phase(0.0f),
		_timeLeft(0.0f),
		_startingY(0.0f)
	{
		_onCollect = _obj->GetObjectType()->GetMethodByDecl("bool OnCollect(Player@)");
	}

	Task<bool> ScriptCollectibleWrapper::OnActivatedAsync(const ActorActivationDetails& details)
	{
		_elasticity = 0.6f;

		Vector2f pos = _pos;
		_phase = ((pos.X / 32) + (pos.Y / 32)) * 2.0f;

		if ((GetState() & (ActorState::IsCreatedFromEventMap | ActorState::IsFromGenerator)) != ActorState::None) {
			_untouched = true;
			SetState(ActorState::ApplyGravitation, false);

			_startingY = pos.Y;
		} else {
			_untouched = false;
			SetState(ActorState::ApplyGravitation, true);

			_timeLeft = 90.0f * FrameTimer::FramesPerSecond;
		}

		bool success = async_await ScriptActorWrapper::OnActivatedAsync(details);

		SetState(ActorState::CollideWithOtherActors | ActorState::SkipPerPixelCollisions, true);

		async_return success;
	}

	bool ScriptCollectibleWrapper::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		if (auto* player = runtime_cast<Player*>(other)) {
			if (OnCollect(player)) {
				return true;
			}
		} else {
			bool shouldDrop = _untouched && (runtime_cast<Weapons::ShotBase*>(other) || runtime_cast<Weapons::TNT*>(other) || runtime_cast<Enemies::TurtleShell*>(other));
			if (shouldDrop) {
				Vector2f speed = other->GetSpeed();
				_externalForce.X += speed.X / 2.0f * (0.9f + Random().NextFloat(0.0f, 0.2f));
				_externalForce.Y += -speed.Y / 4.0f * (0.9f + Random().NextFloat(0.0f, 0.2f));

				_untouched = false;
				SetState(ActorState::ApplyGravitation, true);
			}
		}

		return ScriptActorWrapper::OnHandleCollision(other);
	}

	bool ScriptCollectibleWrapper::OnCollect(Player* player)
	{
		if (_onCollect == nullptr || _isDead->Get()) {
			return false;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		void* mem = asAllocMem(sizeof(ScriptPlayerWrapper));
		ScriptPlayerWrapper* playerWrapper = new(mem) ScriptPlayerWrapper(_levelScripts, player);

		ctx->Prepare(_onCollect);
		ctx->SetObject(_obj);
		ctx->SetArgObject(0, playerWrapper);
		int r = ctx->Execute();
		bool result;
		if (r == asEXECUTION_EXCEPTION) {
			LOGE("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			result = true;
		} else {
			result = (ctx->GetReturnByte() != 0);
		}

		engine->ReturnContext(ctx);

		playerWrapper->Release();

		if (result) {
			player->AddScore(_scoreValue);
			Explosion::Create(_levelHandler, Vector3i((int)_pos.X, (int)_pos.Y, _renderer.layer()), Explosion::Type::Generator);
			DecreaseHealth(INT32_MAX);
			return true;
		} else {
			return false;
		}
	}
}

#endif