#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../Actors/Player.h"

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

	class ScriptPlayerWrapper
	{
	public:
		ScriptPlayerWrapper(LevelScriptLoader* levelScripts, int playerIndex);
		ScriptPlayerWrapper(LevelScriptLoader* levelScripts, Actors::Player* player);
		~ScriptPlayerWrapper();

		static void RegisterFactory(asIScriptEngine* engine);
		static ScriptPlayerWrapper* Factory(int playerIndex);

		void AddRef();
		void Release();

		// Assignment operator
		ScriptPlayerWrapper& operator=(const ScriptPlayerWrapper& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		LevelScriptLoader* _levelScripts;
		Actors::Player* _player;
#endif

		bool asIsInGame() const;
		int asGetIndex() const;
		int asGetPlayerType() const;
		float asGetX() const;
		float asGetY() const;
		float asGetSpeedX() const;
		float asGetSpeedY() const;
		int asGetHealth() const;
		int asGetLives() const;
		int asGetFoodEaten() const;
		int asGetScore() const;
		void asSetScore(int value);
		uint16_t asGetLayer() const;
		void asSetLayer(uint16_t value);
		bool asGetWeaponAllowed() const;
		void asSetWeaponAllowed(bool value);
		int asGetWeaponAmmo(int weaponType) const;
		void asSetWeaponAmmo(int weaponType, int value);

		void asDecreaseHealth(int amount);
		void asMoveTo(float x, float y);
		void asWarpTo(float x, float y);
		void asMoveBy(float x, float y);
		void asPlaySfx(const String& identifier, float gain, float pitch);
		void asSetAnimation(const String& name);
		void asSetAnimationState(int state);

		void asMorphTo(int playerType);
		void asMorphRevert();

	private:
		int _refCount;

	};
}

#endif