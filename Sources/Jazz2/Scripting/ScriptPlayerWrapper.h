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
		ScriptPlayerWrapper(LevelScriptLoader* levelScripts, std::int32_t playerIndex);
		ScriptPlayerWrapper(LevelScriptLoader* levelScripts, Actors::Player* player);
		~ScriptPlayerWrapper();

		static void RegisterFactory(asIScriptEngine* engine);
		static ScriptPlayerWrapper* Factory(std::int32_t playerIndex);

		void AddRef();
		void Release();

		// Assignment operator
		ScriptPlayerWrapper& operator=(const ScriptPlayerWrapper& o) {
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
		std::int32_t asGetIndex() const;
		std::int32_t asGetPlayerType() const;
		float asGetX() const;
		float asGetY() const;
		float asGetSpeedX() const;
		float asGetSpeedY() const;
		std::int32_t asGetHealth() const;
		std::int32_t asGetLives() const;
		std::int32_t asGetFoodEaten() const;
		std::int32_t asGetScore() const;
		void asSetScore(std::int32_t value);
		std::uint16_t asGetLayer() const;
		void asSetLayer(uint16_t value);
		bool asGetWeaponAllowed() const;
		void asSetWeaponAllowed(bool value);
		std::int32_t asGetWeaponAmmo(std::int32_t weaponType) const;
		void asSetWeaponAmmo(std::int32_t weaponType, std::int32_t value);

		void asDecreaseHealth(std::int32_t amount);
		void asMoveTo(float x, float y);
		void asWarpTo(float x, float y);
		void asMoveBy(float x, float y);
		void asPlaySfx(const String& identifier, float gain, float pitch);
		void asSetAnimationState(std::int32_t state);

		void asMorphTo(std::int32_t playerType);
		void asMorphRevert();

	private:
		std::int32_t _refCount;

	};
}

#endif