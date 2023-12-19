#if defined(WITH_ANGELSCRIPT)

#include "ScriptPlayerWrapper.h"
#include "LevelScriptLoader.h"
#include "RegisterArray.h"
#include "../ILevelHandler.h"

using namespace Jazz2::Actors;

#define AsClassName "Player"

namespace Jazz2::Scripting
{
	ScriptPlayerWrapper::ScriptPlayerWrapper(LevelScriptLoader* levelScripts, int playerIndex)
		:
		_levelScripts(levelScripts),
		_refCount(1)
	{
		auto& players = levelScripts->GetPlayers();
		_player = (playerIndex < players.size() ? players[playerIndex] : nullptr);
	}

	ScriptPlayerWrapper::ScriptPlayerWrapper(LevelScriptLoader* levelScripts, Player* player)
		:
		_levelScripts(levelScripts),
		_refCount(1),
		_player(player)
	{
	}

	ScriptPlayerWrapper::~ScriptPlayerWrapper()
	{
	}

	void ScriptPlayerWrapper::RegisterFactory(asIScriptEngine* engine)
	{
		int r;
		r = engine->RegisterObjectType(AsClassName, 0, asOBJ_REF); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour(AsClassName, asBEHAVE_FACTORY, AsClassName " @f(int = 0)", asFUNCTION(ScriptPlayerWrapper::Factory), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour(AsClassName, asBEHAVE_ADDREF, "void f()", asMETHOD(ScriptPlayerWrapper, AddRef), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour(AsClassName, asBEHAVE_RELEASE, "void f()", asMETHOD(ScriptPlayerWrapper, Release), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, AsClassName " &opAssign(const " AsClassName " &in)", asMETHOD(ScriptPlayerWrapper, operator=), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod(AsClassName, "bool get_IsInGame() const property", asMETHOD(ScriptPlayerWrapper, asIsInGame), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "int get_GetIndex() const property", asMETHOD(ScriptPlayerWrapper, asGetIndex), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "int get_GetPlayerType() const property", asMETHOD(ScriptPlayerWrapper, asGetPlayerType), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "float get_X() const property", asMETHOD(ScriptPlayerWrapper, asGetX), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "float get_Y() const property", asMETHOD(ScriptPlayerWrapper, asGetY), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "float get_SpeedX() const property", asMETHOD(ScriptPlayerWrapper, asGetSpeedX), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "float get_SpeedY() const property", asMETHOD(ScriptPlayerWrapper, asGetSpeedY), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "int get_Health() const property", asMETHOD(ScriptPlayerWrapper, asGetHealth), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "int get_Lives() const property", asMETHOD(ScriptPlayerWrapper, asGetLives), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "int get_FoodEaten() const property", asMETHOD(ScriptPlayerWrapper, asGetFoodEaten), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "int get_Score() const property", asMETHOD(ScriptPlayerWrapper, asGetScore), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void set_Score(int) property", asMETHOD(ScriptPlayerWrapper, asSetScore), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "uint16 get_Layer() const property", asMETHOD(ScriptPlayerWrapper, asGetLayer), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void set_Layer(uint16) property", asMETHOD(ScriptPlayerWrapper, asSetLayer), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "bool get_WeaponAllowed() const property", asMETHOD(ScriptPlayerWrapper, asGetWeaponAllowed), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void set_WeaponAllowed(bool) property", asMETHOD(ScriptPlayerWrapper, asSetWeaponAllowed), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "int get_WeaponAmmo(int) const property", asMETHOD(ScriptPlayerWrapper, asGetWeaponAmmo), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void set_WeaponAmmo(int, int) property", asMETHOD(ScriptPlayerWrapper, asSetWeaponAmmo), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod(AsClassName, "void DecreaseHealth(int)", asMETHOD(ScriptPlayerWrapper, asDecreaseHealth), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void MoveTo(float, float)", asMETHOD(ScriptPlayerWrapper, asMoveTo), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void WarpTo(float, float)", asMETHOD(ScriptPlayerWrapper, asWarpTo), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void MoveBy(float, float)", asMETHOD(ScriptPlayerWrapper, asMoveBy), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void PlaySfx(const string &in, float = 1.0, float = 1.0)", asMETHOD(ScriptPlayerWrapper, asPlaySfx), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void SetAnimation(int)", asMETHOD(ScriptPlayerWrapper, asSetAnimationState), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod(AsClassName, "void MorphTo(int)", asMETHOD(ScriptPlayerWrapper, asMorphTo), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod(AsClassName, "void MorphRevert()", asMETHOD(ScriptPlayerWrapper, asMorphRevert), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
	}

	ScriptPlayerWrapper* ScriptPlayerWrapper::Factory(int playerIndex)
	{
		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(ScriptPlayerWrapper));
		return new(mem) ScriptPlayerWrapper(owner, playerIndex);
	}

	void ScriptPlayerWrapper::AddRef()
	{
		_refCount++;
	}

	void ScriptPlayerWrapper::Release()
	{
		if (--_refCount == 0) {
			this->~ScriptPlayerWrapper();
			asFreeMem(this);
		}
	}

	bool ScriptPlayerWrapper::asIsInGame() const
	{
		return (_player != nullptr);
	}

	int ScriptPlayerWrapper::asGetIndex() const
	{
		return (_player != nullptr ? _player->_playerIndex : -1);
	}

	int ScriptPlayerWrapper::asGetPlayerType() const
	{
		return (_player != nullptr ? (int)_player->_playerType : -1);
	}

	float ScriptPlayerWrapper::asGetX() const
	{
		return (_player != nullptr ? _player->_pos.X : -1);
	}

	float ScriptPlayerWrapper::asGetY() const
	{
		return (_player != nullptr ? _player->_pos.Y : -1);
	}

	float ScriptPlayerWrapper::asGetSpeedX() const
	{
		return (_player != nullptr ? _player->_speed.X : -1);
	}

	float ScriptPlayerWrapper::asGetSpeedY() const
	{
		return (_player != nullptr ? _player->_speed.Y : -1);
	}

	int ScriptPlayerWrapper::asGetHealth() const
	{
		return (_player != nullptr ? _player->_health : -1);
	}

	int ScriptPlayerWrapper::asGetLives() const
	{
		return (_player != nullptr ? _player->_lives : -1);
	}

	int ScriptPlayerWrapper::asGetFoodEaten() const
	{
		return (_player != nullptr ? _player->_foodEaten : -1);
	}

	int ScriptPlayerWrapper::asGetScore() const
	{
		return (_player != nullptr ? _player->_score : -1);
	}

	void ScriptPlayerWrapper::asSetScore(int value)
	{
		if (_player != nullptr) {
			_player->_score = value;
		}
	}

	uint16_t ScriptPlayerWrapper::asGetLayer() const
	{
		return (_player != nullptr ? _player->_renderer.layer() : 0);
	}

	void ScriptPlayerWrapper::asSetLayer(uint16_t value)
	{
		if (_player != nullptr) {
			_player->_renderer.setLayer(value);
		}
	}

	bool ScriptPlayerWrapper::asGetWeaponAllowed() const
	{
		return (_player != nullptr && _player->_weaponAllowed);
	}

	void ScriptPlayerWrapper::asSetWeaponAllowed(bool value)
	{
		if (_player != nullptr) {
			_player->_weaponAllowed = value;
		}
	}

	int ScriptPlayerWrapper::asGetWeaponAmmo(int weaponType) const
	{
		return (_player != nullptr && weaponType >= 0 && weaponType < (int)WeaponType::Count ? _player->_weaponAmmo[weaponType] : -1);
	}

	void ScriptPlayerWrapper::asSetWeaponAmmo(int weaponType, int value)
	{
		if (_player != nullptr && weaponType >= 0 && weaponType < (int)WeaponType::Count) {
			_player->_weaponAmmo[weaponType] = value;
		}
	}

	void ScriptPlayerWrapper::asDecreaseHealth(int amount)
	{
		if (_player != nullptr) {
			_player->DecreaseHealth(amount);
		}
	}

	void ScriptPlayerWrapper::asMoveTo(float x, float y)
	{
		if (_player != nullptr) {
			_player->WarpToPosition(Vector2f(x, y), WarpFlags::Fast);
		}
	}

	void ScriptPlayerWrapper::asWarpTo(float x, float y)
	{
		if (_player != nullptr) {
			_player->WarpToPosition(Vector2f(x, y), WarpFlags::Default);
		}
	}

	void ScriptPlayerWrapper::asMoveBy(float x, float y)
	{
		if (_player != nullptr) {
			_player->WarpToPosition(Vector2f(_player->_pos.X + x, _player->_pos.Y + y), WarpFlags::Fast);
		}
	}

	void ScriptPlayerWrapper::asPlaySfx(const String& identifier, float gain, float pitch)
	{
		if (_player != nullptr) {
			_player->PlayPlayerSfx(identifier, gain, pitch);
		}
	}

	void ScriptPlayerWrapper::asSetAnimationState(int state)
	{
		if (_player != nullptr) {
			_player->SetAnimation((AnimState)state);
		}
	}

	void ScriptPlayerWrapper::asMorphTo(int playerType)
	{
		if (_player != nullptr) {
			_player->MorphTo((PlayerType)playerType);
		}
	}

	void ScriptPlayerWrapper::asMorphRevert()
	{
		if (_player != nullptr) {
			_player->MorphRevert();
		}
	}
}

#endif