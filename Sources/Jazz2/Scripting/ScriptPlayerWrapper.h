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

	/** @brief Wraps a player actor for access from **AngelScript** */
	class ScriptPlayerWrapper
	{
	public:
		/**
		 * @brief Creates a new instance from the specified player index
		 *
		 * @param levelScripts  Owning level script loader
		 * @param playerIndex   Index of the wrapped player
		 */
		ScriptPlayerWrapper(LevelScriptLoader* levelScripts, std::int32_t playerIndex);
		/**
		 * @brief Creates a new instance from the specified player
		 *
		 * @param levelScripts  Owning level script loader
		 * @param player        Wrapped player actor
		 */
		ScriptPlayerWrapper(LevelScriptLoader* levelScripts, Actors::Player* player);
		~ScriptPlayerWrapper();

		/** @brief Registers the player factory to the specified **AngelScript** engine */
		static void RegisterFactory(asIScriptEngine* engine);
		/** @brief Creates a new wrapper instance for the player with the specified index */
		static ScriptPlayerWrapper* Factory(std::int32_t playerIndex);

		/** @brief Increases the reference count */
		void AddRef();
		/** @brief Decreases the reference count, destroying the instance when it reaches zero */
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

		/** @brief Returns `true` if the player is currently in game */
		bool asIsInGame() const;
		/** @brief Returns the index of the player */
		std::int32_t asGetIndex() const;
		/** @brief Returns the type of the player */
		std::int32_t asGetPlayerType() const;
		/** @brief Returns the horizontal position of the player */
		float asGetX() const;
		/** @brief Returns the vertical position of the player */
		float asGetY() const;
		/** @brief Returns the horizontal speed of the player */
		float asGetSpeedX() const;
		/** @brief Returns the vertical speed of the player */
		float asGetSpeedY() const;
		/** @brief Returns the current health of the player */
		std::int32_t asGetHealth() const;
		/** @brief Returns the number of remaining lives of the player */
		std::int32_t asGetLives() const;
		/** @brief Returns the amount of food eaten by the player */
		std::int32_t asGetFoodEaten() const;
		/** @brief Returns the current score of the player */
		std::int32_t asGetScore() const;
		/** @brief Sets the current score of the player */
		void asSetScore(std::int32_t value);
		/** @brief Returns the rendering layer of the player */
		std::uint16_t asGetLayer() const;
		/** @brief Sets the rendering layer of the player */
		void asSetLayer(uint16_t value);
		/** @brief Returns `true` if weapons are currently allowed for the player */
		bool asGetWeaponAllowed() const;
		/** @brief Sets whether weapons are allowed for the player */
		void asSetWeaponAllowed(bool value);
		/** @brief Returns the ammo count for the specified weapon type */
		std::int32_t asGetWeaponAmmo(std::int32_t weaponType) const;
		/** @brief Sets the ammo count for the specified weapon type */
		void asSetWeaponAmmo(std::int32_t weaponType, std::int32_t value);

		/** @brief Decreases the health of the player by the specified amount */
		void asDecreaseHealth(std::int32_t amount);
		/** @brief Moves the player to the specified position */
		void asMoveTo(float x, float y);
		/** @brief Warps the player to the specified position */
		void asWarpTo(float x, float y);
		/** @brief Moves the player by the specified offset */
		void asMoveBy(float x, float y);
		/** @brief Plays a sound effect by its identifier */
		void asPlaySfx(const String& identifier, float gain, float pitch);
		/** @brief Sets the current animation state */
		void asSetAnimationState(std::int32_t state);

		/** @brief Morphs the player into the specified player type */
		void asMorphTo(std::int32_t playerType);
		/** @brief Reverts a previous morph back to the original player type */
		void asMorphRevert();

	private:
		std::int32_t _refCount;

	};
}

#endif