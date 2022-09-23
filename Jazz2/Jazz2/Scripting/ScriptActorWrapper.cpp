#if defined(WITH_ANGELSCRIPT)

#include "ScriptActorWrapper.h"
#include "LevelScripts.h"
#include "RegisterArray.h"
#include "../ILevelHandler.h"
#include "../Events/EventMap.h"
#include "../Events/EventSpawner.h"
#include "../Tiles/TileMap.h"
#include "../Collisions/DynamicTreeBroadPhase.h"

#include "../../nCine/Primitives/Matrix4x4.h"
#include "../../nCine/Base/Random.h"

using namespace nCine;
using namespace Jazz2::Actors;

#define AsClassName "ActorBase"
#define AsClassNameWrapper "ActorBase_t"

namespace Jazz2::Scripting
{
	ScriptActorWrapper::ScriptActorWrapper(LevelScripts* levelScripts, asIScriptObject* obj)
		:
		_levelScripts(levelScripts),
		_obj(obj),
		_refCount(1)
	{
		_isDead = obj->GetWeakRefFlag();
		_isDead->AddRef();
	}

	ScriptActorWrapper::~ScriptActorWrapper()
	{
		_isDead->Release();
	}

	void ScriptActorWrapper::RegisterFactory(asIScriptEngine* engine, asIScriptModule* module)
	{
		constexpr char AsLibrary[] = R"(
shared abstract class )" AsClassName R"(
{
	// Allow scripts to create instances
	)" AsClassName R"(()
	{
		// Create the C++ side of the proxy
		@_obj = )" AsClassNameWrapper R"(();  
	}

	// The copy constructor performs a deep copy
	)" AsClassName R"((const )" AsClassName R"( &o)
	{
		// Create a new C++ instance and copy content
		@_obj = )" AsClassNameWrapper R"(();
		_obj = o._obj;
	}

	// Do a deep a copy of the C++ object
	)" AsClassName R"( &opAssign(const )" AsClassName R"( &o)
	{
		// copy content of C++ instance
		_obj = o._obj;
		return this;
	}

	
	bool OnActivated(array<uint8> &in eventParams) { return false; }
	bool OnTileDeactivate(int tx1, int ty1, int tx2, int ty2) { return true; }
	void OnHealthChanged() { }
	bool OnPerish() { return true; }
	void OnUpdate(float timeMult) { }
	void OnUpdateHitbox() { }

	void OnHitFloor(float timeMult) { }
	void OnHitCeiling(float timeMult) { }
	void OnHitWall(float timeMult) { }

	float X { get const { return _obj.X; } }
	float Y { get const { return _obj.Y; } }

	bool MoveTo(float x, float y, bool force = false) { return _obj.MoveTo(x, y); }
	bool MoveBy(float x, float y, bool force = false) { return _obj.MoveBy(x, y); }

	void RequestMetadata(const string &in path) { _obj.RequestMetadata(path); }
	void SetAnimation(const string &in identifier) { _obj.SetAnimation(identifier); }
	void SetAnimation(int state) { _obj.SetAnimation(state); }

	// The script class can be implicitly cast to the C++ type through the opImplCast method
	)" AsClassNameWrapper R"( @opImplCast() { return _obj; }

	// Hold a reference to the C++ side of the proxy
	private )" AsClassNameWrapper R"( @_obj;
}
)";

		engine->RegisterObjectType(AsClassNameWrapper, 0, asOBJ_REF);
		engine->RegisterObjectBehaviour(AsClassNameWrapper, asBEHAVE_FACTORY, AsClassNameWrapper " @f()", asFUNCTION(ScriptActorWrapper::Factory), asCALL_CDECL);
		engine->RegisterObjectBehaviour(AsClassNameWrapper, asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptActorWrapper, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour(AsClassNameWrapper, asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptActorWrapper, Release), asCALL_THISCALL);
		engine->RegisterObjectMethod(AsClassNameWrapper, AsClassNameWrapper " &opAssign(const " AsClassNameWrapper " &in)", asMETHOD(ScriptActorWrapper, operator=), asCALL_THISCALL);

		engine->RegisterObjectProperty(AsClassNameWrapper, "float X", asOFFSET(ScriptActorWrapper, _pos.X));
		engine->RegisterObjectProperty(AsClassNameWrapper, "float Y", asOFFSET(ScriptActorWrapper, _pos.Y));

		engine->RegisterObjectMethod(AsClassNameWrapper, "bool MoveTo(float x, float y)", asMETHOD(ScriptActorWrapper, asMoveTo), asCALL_THISCALL);
		engine->RegisterObjectMethod(AsClassNameWrapper, "bool MoveBy(float x, float y)", asMETHOD(ScriptActorWrapper, asMoveBy), asCALL_THISCALL);
		engine->RegisterObjectMethod(AsClassNameWrapper, "void RequestMetadata(const string &in path)", asMETHOD(ScriptActorWrapper, asRequestMetadata), asCALL_THISCALL);
		engine->RegisterObjectMethod(AsClassNameWrapper, "void SetAnimation(const string &in identifier)", asMETHOD(ScriptActorWrapper, asSetAnimation), asCALL_THISCALL);
		engine->RegisterObjectMethod(AsClassNameWrapper, "void SetAnimation(int state)", asMETHOD(ScriptActorWrapper, asSetAnimationState), asCALL_THISCALL);

		module->AddScriptSection("__" AsClassName, AsLibrary, _countof(AsLibrary) - 1, 0);
	}

	ScriptActorWrapper* ScriptActorWrapper::Factory()
	{
		auto ctx = asGetActiveContext();
		auto controller = reinterpret_cast<LevelScripts*>(ctx->GetUserData(LevelScripts::ContextToController));

		// Get the function that is calling the factory, so we can be certain it is the our internal script class
		asIScriptFunction* func = ctx->GetFunction(0);
		if (func->GetObjectType() == 0 || StringView(func->GetObjectType()->GetName()) != StringView(AsClassName)) {
			ctx->SetException("Invalid attempt to manually instantiate FooScript_t");
			return nullptr;
		}

		asIScriptObject* obj = reinterpret_cast<asIScriptObject*>(ctx->GetThisPointer(0));
		return new ScriptActorWrapper(controller, obj);
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
			delete this;
		}
	}

	Task<bool> ScriptActorWrapper::OnActivatedAsync(const Actors::ActorActivationDetails& details)
	{
		if (_isDead->Get()) {
			co_return false;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		CScriptArray* eventParams = CScriptArray::Create(engine->GetTypeInfoByDecl("array<uint8>"), Events::EventSpawner::SpawnParamsSize);
		std::memcpy(eventParams->At(0), details.Params, Events::EventSpawner::SpawnParamsSize);

		ctx->Prepare(_obj->GetObjectType()->GetMethodByDecl("bool OnActivated(array<uint8> &in eventParams)"));
		ctx->SetObject(_obj);
		ctx->SetArgObject(0, eventParams);
		int r = ctx->Execute();
		bool result;
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			result = true;
		} else {
			result = (ctx->GetReturnByte() != 0);
		}

		eventParams->Release();

		engine->ReturnContext(ctx);
		co_return result;
	}

	bool ScriptActorWrapper::OnTileDeactivate(int tx1, int ty1, int tx2, int ty2)
	{
		if (_isDead->Get()) {
			return true;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_obj->GetObjectType()->GetMethodByDecl("bool OnTileDeactivate(int tx1, int ty1, int tx2, int ty2)"));
		ctx->SetObject(_obj);
		ctx->SetArgDWord(0, tx1);
		ctx->SetArgDWord(1, ty1);
		ctx->SetArgDWord(2, tx2);
		ctx->SetArgDWord(3, ty2);
		int r = ctx->Execute();
		bool result;
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			result = true;
		} else {
			result = (ctx->GetReturnByte() != 0);
		}

		engine->ReturnContext(ctx);
		return result;
	}

	void ScriptActorWrapper::OnHealthChanged(ActorBase* collider)
	{
		if (_isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_obj->GetObjectType()->GetMethodByDecl("void OnHealthChanged()"));
		ctx->SetObject(_obj);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	bool ScriptActorWrapper::OnPerish(ActorBase* collider)
	{
		if (_isDead->Get()) {
			return true;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_obj->GetObjectType()->GetMethodByDecl("bool OnPerish()"));
		ctx->SetObject(_obj);
		int r = ctx->Execute();
		bool result;
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			result = true;
		} else {
			result = (ctx->GetReturnByte() != 0);
		}

		engine->ReturnContext(ctx);
		return result;
	}

	void ScriptActorWrapper::OnUpdate(float timeMult)
	{
		if (_isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_obj->GetObjectType()->GetMethodByDecl("void OnUpdate(float timeMult)"));
		ctx->SetObject(_obj);
		ctx->SetArgFloat(0, timeMult);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	void ScriptActorWrapper::OnUpdateHitbox()
	{
		if (_isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_obj->GetObjectType()->GetMethodByDecl("void OnUpdateHitbox()"));
		ctx->SetObject(_obj);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	bool ScriptActorWrapper::OnHandleCollision(std::shared_ptr<ActorBase> other)
	{
		return ActorBase::OnHandleCollision(other);
	}

	bool ScriptActorWrapper::OnDraw(RenderQueue& renderQueue)
	{
		return ActorBase::OnDraw(renderQueue);
	}

	void ScriptActorWrapper::OnEmitLights(SmallVectorImpl<LightEmitter>& lights)
	{
		// TODO
	}

	void ScriptActorWrapper::OnHitFloor(float timeMult)
	{
		if (_isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_obj->GetObjectType()->GetMethodByDecl("void OnHitFloor(float timeMult)"));
		ctx->SetObject(_obj);
		ctx->SetArgFloat(0, timeMult);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	void ScriptActorWrapper::OnHitCeiling(float timeMult)
	{
		if (_isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_obj->GetObjectType()->GetMethodByDecl("void OnHitCeiling(float timeMult)"));
		ctx->SetObject(_obj);
		ctx->SetArgFloat(0, timeMult);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	void ScriptActorWrapper::OnHitWall(float timeMult)
	{
		if (_isDead->Get()) {
			return;
		}

		asIScriptEngine* engine = _obj->GetEngine();
		asIScriptContext* ctx = engine->RequestContext();

		ctx->Prepare(_obj->GetObjectType()->GetMethodByDecl("void OnHitWall(float timeMult)"));
		ctx->SetObject(_obj);
		ctx->SetArgFloat(0, timeMult);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		engine->ReturnContext(ctx);
	}

	bool ScriptActorWrapper::asMoveTo(float x, float y, bool force)
	{
		return MoveInstantly(Vector2f(x, y), (force ? MoveType::Absolute | MoveType::Force : MoveType::Absolute));
	}

	bool ScriptActorWrapper::asMoveBy(float x, float y, bool force)
	{
		return MoveInstantly(Vector2f(x, y), (force ? MoveType::Relative | MoveType::Force : MoveType::Relative));
	}

	void ScriptActorWrapper::asRequestMetadata(String& path)
	{
		RequestMetadata(path);
	}

	void ScriptActorWrapper::asSetAnimation(String& identifier)
	{
		SetAnimation(identifier);
	}

	void ScriptActorWrapper::asSetAnimationState(int state)
	{
		SetAnimation((AnimState)state);
	}
}

#endif