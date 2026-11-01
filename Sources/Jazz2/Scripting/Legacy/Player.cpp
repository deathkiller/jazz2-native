#if defined(WITH_ANGELSCRIPT)

#include "Player.h"
#include "AnimationTables.h"
#include "Internal.h"
#include "ObjectHost.h"

#include "../../Actors/ActorBase.h"
#include "../../Actors/Player.h"
#include "../../Actors/Collectibles/CollectibleBase.h"
#include "../../Actors/Environment/Spring.h"
#include "../../Actors/Weapons/BlasterShot.h"
#include "../../Actors/Weapons/BouncerShot.h"
#include "../../Actors/Weapons/ElectroShot.h"
#include "../../Actors/Weapons/FreezerShot.h"
#include "../../Actors/Weapons/PepperShot.h"
#include "../../Actors/Weapons/RFShot.h"
#include "../../Actors/Weapons/SeekerShot.h"
#include "../../Actors/Weapons/ToasterShot.h"
#include "../../Compatibility/JJ2Strings.h"
#include "../../Rendering/PlayerViewport.h"

#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		namespace
		{
			// JJ2+ names a character by its animation set (CHAR::Char), not by a player type - CHAR::BIRD is the set's
			// own and CHAR::BIRD2 borrows Chuck's. Returns PlayerType::None for a set that isn't a character.
			static PlayerType CharToPlayerType(std::uint32_t character) {
				// The set IDs depend on the version of the game (see AnimationTables)
				bool isTSF = ScriptLoader::FromActiveContext<LevelScriptLoader>()->IsTSF();
				std::int32_t setID = std::int32_t(character);
				if (setID == AnimationTables::GetSetID(mJAZZ, isTSF)) return PlayerType::Jazz;
				if (setID == AnimationTables::GetSetID(mSPAZ, isTSF)) return PlayerType::Spaz;
				if (setID == AnimationTables::GetSetID(mLORI, isTSF)) return PlayerType::Lori;
				if (setID == AnimationTables::GetSetID(mFROG, isTSF)) return PlayerType::Frog;
				if (setID == AnimationTables::GetSetID(mBIRD, isTSF)) return PlayerType::Bird;
				if (setID == AnimationTables::GetSetID(mCHUCK, isTSF)) return PlayerType::Bird2;
				return PlayerType::None;
			}

			// The other direction, for what a script reads back
			static std::uint32_t PlayerTypeToChar(PlayerType playerType, bool isTSF) {
				anim set;
				switch (playerType) {
					default:
					case PlayerType::Jazz: set = mJAZZ; break;
					case PlayerType::Spaz: set = mSPAZ; break;
					case PlayerType::Lori: set = mLORI; break;
					case PlayerType::Frog: set = mFROG; break;
					case PlayerType::Bird: set = mBIRD; break;
					case PlayerType::Bird2: set = mCHUCK; break;
				}
				return std::uint32_t(AnimationTables::GetSetID(set, isTSF));
			}
			static std::uint32_t PlayerTypeToChar(PlayerType playerType) {
				return PlayerTypeToChar(playerType, ScriptLoader::FromActiveContext<LevelScriptLoader>()->IsTSF());
			}
		}

		jjPLAYER::jjPLAYER(LevelScriptLoader* levelScripts, Actors::Player* player)
			: _levelScriptLoader(levelScripts), _player(player), _timerCallback(nullptr),
				_timerState(0), _timerLeft(0.0f), _timerPersists(false) {
			// Without a player, it stands for the players that aren't in the game, see LevelScriptLoader::GetPlayerBackingStore()
			isLocal = (player != nullptr);
			isActive = (player != nullptr);
			// JJ2+ starts the blaster's count at 99, it never spends it
			_infiniteAmmo[std::int32_t(WeaponType::Blaster)] = 99;
		}
		jjPLAYER::~jjPLAYER() {
		}

		jjPLAYER& jjPLAYER::operator=(const jjPLAYER& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		std::int32_t jjPLAYER::get_score() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_score;
		}
		std::int32_t jjPLAYER::set_score(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_score = value;
			return _player->_score;
		}
		std::int32_t jjPLAYER::get_scoreDisplayed() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_score;
		}
		std::int32_t jjPLAYER::set_scoreDisplayed(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_score = value;
			return _player->_score;
		}

		std::int32_t jjPLAYER::setScore(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_score = value;
			return _player->_score;
		}

		float jjPLAYER::get_xPos() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_pos.X;
		}
		float jjPLAYER::set_xPos(float value) {
			if (_player == nullptr) {
				return {};
			}
			// The hitbox goes along, a move of the player from a stale one would end up inside what it's next to
			float delta = value - _player->_pos.X;
			_player->_pos.X = value;
			_player->AABBInner.L += delta;
			_player->AABBInner.R += delta;
			// A scripted move is a teleport, not a path the player walked, so nothing between the two positions
			// may be collided with or triggered (see Actors::ActorBase::ResetPathTracking())
			_player->ResetPathTracking();
			return _player->_pos.X;
		}
		// A player standing on the ground is 0.6 to 1.9 px lower in JJ2+ (measured), scripts test the ground by `yPos + 21`
		static constexpr float PlayerOffsetY = 1.0f;

		float jjPLAYER::get_yPos() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_pos.Y + PlayerOffsetY;
		}
		float jjPLAYER::set_yPos(float value) {
			if (_player == nullptr) {
				return {};
			}
			float delta = value - PlayerOffsetY - _player->_pos.Y;
			_player->_pos.Y = value - PlayerOffsetY;
			_player->AABBInner.T += delta;
			_player->AABBInner.B += delta;
			_player->ResetPathTracking();
			return value;
		}
		float jjPLAYER::get_xAcc() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_externalForce.X;
		}
		float jjPLAYER::set_xAcc(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_externalForce.X = value;
			return _player->_externalForce.X;
		}
		float jjPLAYER::get_yAcc() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_externalForce.Y;
		}
		float jjPLAYER::set_yAcc(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_externalForce.Y = value;
			return _player->_externalForce.Y;
		}
		float jjPLAYER::get_xOrg() const {
			if (_player == nullptr) {
				return {};
			}
			// The "origin" is the position the player respawns at, i.e. the last checkpoint
			return _player->_checkpointPos.X;
		}
		float jjPLAYER::set_xOrg(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_checkpointPos.X = value;
			return _player->_checkpointPos.X;
		}
		float jjPLAYER::get_yOrg() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_checkpointPos.Y;
		}
		float jjPLAYER::set_yOrg(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_checkpointPos.Y = value;
			return _player->_checkpointPos.Y;
		}
		// Speeds are in pixels per tick of JJ2+, 70 in a second, the player moves by frames, 60 in a second (a burger booth
		// lifts the player by 1 a tick, 100 pixels in its 100 ticks)
		static constexpr float PlayerSpeedToTicks = 60.0f / 70.0f;

		float jjPLAYER::get_xSpeed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_speed.X * PlayerSpeedToTicks;
		}
		float jjPLAYER::set_xSpeed(float value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_speed.X = value / PlayerSpeedToTicks;
			return value;
		}
		float jjPLAYER::get_ySpeed() {
			if (_player == nullptr) {
				return {};
			}
			if (_hasPendingYSpeed) {
				return _pendingYSpeed;
			}
			return _player->_speed.Y * PlayerSpeedToTicks;
		}
		float jjPLAYER::set_ySpeed(float value) {
			if (_player == nullptr) {
				return {};
			}
			if (_levelScriptLoader->_objectBehaviorDepth > 0) {
				// JJ2+ moves the players before the objects, what a behavior sets moves the player from the next tick (a sword
				// stabbing up, measured), the frames of the game until then would move it by it already
				_pendingYSpeed = value;
				_pendingYSpeedTick = _levelScriptLoader->GetGameTick();
				_hasPendingYSpeed = true;
				return value;
			}
			_hasPendingYSpeed = false;
			ApplyYSpeed(value);
			return value;
		}
		void jjPLAYER::ApplyYSpeed(float value) {
			if (_player->_currentSpecialMove == Actors::Player::SpecialMoveType::Buttstomp) {
				// A buttstomp sets the speed itself in every tick, as measured in JJ2+ (a pole holding the player, a jump off it)
				return;
			}
			_player->_speed.Y = value / PlayerSpeedToTicks;
			if (value < 0.0f) {
				// An assigned rise slows by the live Jump key, as a float-up column's (a sword stabbing up)
				_player->_riseFromFloatUp = true;
			}
		}
		void jjPLAYER::ApplyPendingSpeed(std::int32_t currentTick) {
			if (_hasPendingYSpeed && _player != nullptr && currentTick > _pendingYSpeedTick) {
				_hasPendingYSpeed = false;
				ApplyYSpeed(_pendingYSpeed);
			}
		}

		void jjPLAYER::freeze(bool frozen) {
			if (_player == nullptr) {
				return;
			}
			// As an ice bullet freezes a player (see Actors::Multiplayer::PlayerOnServer), who can't move or shoot then - also
			// when frozen again while still blinking from a hurt, which used to give the controls back
			if (frozen) {
				_player->Freeze(3.0f * FrameTimer::FramesPerSecond);
			} else if (_player->_frozenTimeLeft > 0.0f) {
				_player->Freeze(0.0f);
			}
		}
		std::int32_t jjPLAYER::get_currTile() {
			if (_player == nullptr) {
				return {};
			}
			auto pos = _player->_pos;
			return ((std::int32_t)pos.X / 32) + ((std::int32_t)pos.Y / 32 * 65536);
		}
		bool jjPLAYER::startSugarRush(std::int32_t time) {
			if (_player == nullptr) {
				return {};
			}
			noop();
			// TODO: if boss active, return false
			_player->ActivateSugarRush((float)time * 60.0f / 70.0f);
			return true;
		}
		std::int8_t jjPLAYER::get_health() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int8_t)_player->_health;
		}
		std::int8_t jjPLAYER::set_health(std::int8_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->SetHealth(value);
			return value;
		}

		// `fastfire` of JJ2 is the delay between the shots of the blaster, 35 ticks without any fast fire - the game counts the
		// fast fires, each takes 2.76 frames off the 30 frames of the blaster (see Player::FireCurrentWeapon())
		constexpr float FastFireDefaultTicks = 35.0f;
		constexpr float TicksPerFastFire = 2.76f * 70.0f / 60.0f;
		constexpr std::int32_t MaxFastFires = 9;

		std::int32_t jjPLAYER::get_fastfire() const {
			if (_player == nullptr) {
				return {};
			}
			std::int32_t count = (_player->_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
			return std::int32_t(std::round(FastFireDefaultTicks - count * TicksPerFastFire));
		}
		std::int32_t jjPLAYER::set_fastfire(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			std::int32_t count = std::clamp(std::int32_t(std::round((FastFireDefaultTicks - value) / TicksPerFastFire)), 0, MaxFastFires);
			_player->_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] = (std::uint8_t)((_player->_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] & 0x1) | (count << 1));
			return value;
		}

		// The weapons of JJ2+ are numbered from 1 (`WEAPON::BLASTER`), ours from 0, returns -1 if there's no such weapon
		static std::int32_t ToWeaponIndex(std::int32_t number) {
			return (number >= 1 && number <= std::int32_t(WeaponType::Count) ? number - 1 : -1);
		}
		// As ToWeaponIndex(), `WEAPON::CURRENT` is the selected weapon (measured in JJ2+ on `ammo` and `powerup`)
		static std::int32_t ToWeaponIndex(const Actors::Player* player, std::int32_t number) {
			return (number == 0 && player != nullptr ? std::int32_t(player->GetCurrentWeapon()) : ToWeaponIndex(number));
		}

		std::int8_t jjPLAYER::get_currWeapon() const {
			if (_player == nullptr) {
				return {};
			}
			return std::int8_t(std::int32_t(_player->_currentWeapon) + 1);
		}
		std::int8_t jjPLAYER::set_currWeapon(std::int8_t value) {
			if (_player == nullptr) {
				return {};
			}
			std::int32_t index = ToWeaponIndex(value);
			if (index < 0) {
				return std::int8_t(std::int32_t(_player->_currentWeapon) + 1);
			}
			_player->_currentWeapon = (WeaponType)index;
			return value;
		}

		/*std::int32_t jjPLAYER::get_lives() const {
			return _player->_lives;
		}
		std::int32_t jjPLAYER::set_lives(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_lives = value;
			return _player->_lives;
		}*/

		std::int32_t jjPLAYER::get_invincibility() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)(_player->_invulnerableTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::uint32_t jjPLAYER::get_lightType() const {
			// LIGHT::NONE and LIGHT::PLAYER of JJ2+
			return (_player != nullptr && _player->_lightHiddenByScript ? 0 : 10);
		}
		std::uint32_t jjPLAYER::set_lightType(std::uint32_t value) {
			// Only whether the player has its light, the light of the game is the light of a player of any type
			if (_player != nullptr) {
				_player->_lightHiddenByScript = (value == 0);
			}
			return value;
		}
		std::int32_t jjPLAYER::set_invincibility(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			if (value > 0) {
				// As JJ2+ shows it, in the bubble of the invincibility carrot
				_player->SetInvulnerability(value * FrameTimer::FramesPerSecond / 70.0f, Actors::Player::InvulnerableType::Shielded);
			} else {
				_player->SetInvulnerability(0.0f, Actors::Player::InvulnerableType::Transient);
			}
			return value;
		}

		// The ticks left of the blinking invulnerability, as after a hurt
		std::int32_t jjPLAYER::get_blink() const {
			if (_player == nullptr || _player->_invulnerableTime <= 0.0f || _player->GetInvulnerableType() != Actors::Player::InvulnerableType::Blinking) {
				return 0;
			}
			return (std::int32_t)std::ceil(_player->_invulnerableTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_blink(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			if (value > 0) {
				_player->SetInvulnerability(value * FrameTimer::FramesPerSecond / 70.0f, Actors::Player::InvulnerableType::Blinking);
			} else if (get_blink() > 0) {
				// Only the blinking ends, not a shield
				_player->SetInvulnerability(0.0f, Actors::Player::InvulnerableType::Blinking);
			}
			return value;
		}

		std::int32_t jjPLAYER::extendInvincibility(std::int32_t duration) {
			if (_player == nullptr) {
				return {};
			}
			// Negative values are clamped so invincibility is never reduced below zero
			float timeLeft = std::max(_player->_invulnerableTime + duration * FrameTimer::FramesPerSecond / 70.0f, 0.0f);
			_player->SetInvulnerability(timeLeft, Actors::Player::InvulnerableType::Shielded);
			return (std::int32_t)(_player->_invulnerableTime * 70.0f / FrameTimer::FramesPerSecond);
		}

		std::int32_t jjPLAYER::get_food() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_inventory.FoodEaten;
		}
		std::int32_t jjPLAYER::set_food(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_inventory.FoodEaten = value;
			return _player->_inventory.FoodEaten;
		}
		std::int32_t jjPLAYER::get_coins() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_inventory.Coins;
		}
		std::int32_t jjPLAYER::set_coins(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_inventory.Coins = value;
			return _player->_inventory.Coins;
		}

		bool jjPLAYER::testForCoins(std::int32_t numberOfCoins) {
			if (_player == nullptr) {
				return {};
			}
			if (numberOfCoins > _player->_inventory.Coins) {
				return false;
			}
			_player->AddCoins(-numberOfCoins);
			return true;
		}
		std::int32_t* jjPLAYER::GetGemCount(std::uint32_t type) const {
			// GEM::Color counts from 1 (RED), the gems of the player from 0
			return (_player != nullptr && type >= 1 && type <= arraySize(_player->_inventory.Gems) ? &_player->_inventory.Gems[type - 1] : nullptr);
		}
		std::int32_t jjPLAYER::get_gems(std::uint32_t type) const {
			std::int32_t* count = GetGemCount(type);
			return (count != nullptr ? *count : 0);
		}
		std::int32_t jjPLAYER::set_gems(std::uint32_t type, std::int32_t value) {
			std::int32_t* count = GetGemCount(type);
			if (count == nullptr) {
				return 0;
			}
			*count = value;
			return value;
		}
		bool jjPLAYER::testForGems(std::int32_t numberOfGems, std::uint32_t type) {
			noop();
			std::int32_t* count = GetGemCount(type);
			if (count != nullptr && numberOfGems <= *count) {
				if (numberOfGems > 0) {
					*count -= numberOfGems;
				}
				return true;
			}
			// TODO: Show warning message
			return false;
		}

		std::int32_t jjPLAYER::get_shieldType() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)_player->_activeShield;
		}
		std::int32_t jjPLAYER::set_shieldType(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			if (value >= 0 && value < (std::int32_t)ShieldType::Count) {
				_player->_activeShield = (ShieldType)value;
			}
			return value;
		}
		std::int32_t jjPLAYER::get_shieldTime() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)(_player->_activeShieldTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_shieldTime(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_activeShieldTime = value * FrameTimer::FramesPerSecond / 70.0f;
			return value;
		}

		std::int8_t jjPLAYER::get_frozen() const {
			if (_player == nullptr) {
				return {};
			}
			return std::int8_t(std::min(std::ceil(_player->_frozenTimeLeft * 70.0f / FrameTimer::FramesPerSecond), 127.0f));
		}
		std::int8_t jjPLAYER::set_frozen(std::int8_t value) {
			if (_player == nullptr) {
				return {};
			}
			if (value > 0) {
				_player->Freeze(value * FrameTimer::FramesPerSecond / 70.0f);
			} else if (_player->_frozenTimeLeft > 0.0f) {
				_player->Freeze(0.0f);
			}
			return value;
		}
		std::int32_t jjPLAYER::get_lives() const {
			return (_player != nullptr ? _player->_lives : 0);
		}
		std::int32_t jjPLAYER::set_lives(std::int32_t value) {
			if (_player != nullptr) {
				_player->_lives = value;
			}
			return value;
		}
		std::int8_t jjPLAYER::get_direction() const {
			if (_player == nullptr) {
				return {};
			}
			return std::int8_t(_player->IsFacingLeft() ? -1 : 1);
		}
		std::int8_t jjPLAYER::set_direction(std::int8_t value) {
			if (_player != nullptr && value != 0) {
				_player->SetFacingLeft(value < 0);
			}
			return value;
		}
		std::int32_t jjPLAYER::get_doubleJumpCount() const {
			return (_player != nullptr && !_player->_canDoubleJump ? 1 : 0);
		}
		std::int32_t jjPLAYER::set_doubleJumpCount(std::int32_t value) {
			if (_player != nullptr) {
				_player->_canDoubleJump = (value == 0);
			}
			return value;
		}
		bool jjPLAYER::get_alreadyDoubleJumped() const {
			return (get_doubleJumpCount() != 0);
		}
		bool jjPLAYER::set_alreadyDoubleJumped(bool value) {
			set_doubleJumpCount(value ? 1 : 0);
			return value;
		}
		std::int32_t jjPLAYER::get_helicopter() const {
			if (_player == nullptr) {
				return {};
			}
			return std::int32_t(std::ceil(std::max(_player->_copterFramesLeft, 0.0f) * 70.0f / FrameTimer::FramesPerSecond));
		}
		std::int32_t jjPLAYER::set_helicopter(std::int32_t value) {
			if (_player != nullptr) {
				_player->_copterFramesLeft = std::max(value, 0) * FrameTimer::FramesPerSecond / 70.0f;
			}
			return value;
		}
		std::int32_t jjPLAYER::get_idle() const {
			if (_player == nullptr) {
				return {};
			}
			return std::int32_t(_player->_idleTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_idle(std::int32_t value) {
			if (_player != nullptr) {
				_player->_idleTime = std::max(value, 0) * FrameTimer::FramesPerSecond / 70.0f;
			}
			return value;
		}

		std::int32_t jjPLAYER::get_rolling() const {
			if (_player == nullptr) {
				return {};
			}
			if (_player->_inTubeTime > 0.0f) {
				return (std::int32_t)(_player->_inTubeTime * 70.0f / FrameTimer::FramesPerSecond);
			}
			return std::max(_ballTime - (_levelScriptLoader->GetGameTick() - _ballTimeTick), 0);
		}
		std::int32_t jjPLAYER::set_rolling(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}

			if (value > 0) {
				// Measured in JJ2+, the player keeps falling and running in a ball (a sword spinning), it isn't a sucker tube
				_ballTime = value;
				_ballTimeTick = _levelScriptLoader->GetGameTick();
				// Curled up as after a sucker tube for as long, the pose only
				_player->_tubeBallPoseLeft = std::max(_player->_tubeBallPoseLeft, value * FrameTimer::FramesPerSecond / 70.0f);
				// ...which can't shoot meanwhile (measured), as on a pinball paddle
				_player->_onPinballPaddleTime = std::max(_player->_onPinballPaddleTime, value * FrameTimer::FramesPerSecond / 70.0f);
			} else {
				_ballTime = 0;
				if (_player->_inTubeTime > 0) {
					_player->_controllable = true;
					_player->SetState(Actors::ActorState::ApplyGravitation | Actors::ActorState::CollideWithTileset, true);
					_player->_inTubeTime = 0.0f;
				}
			}
		
			return value;
		}

		std::int8_t jjPLAYER::get_playerID() const {
			if (_player == nullptr) {
				return {};
			}
			return (std::int8_t)_player->_playerIndex;
		}
		std::int32_t jjPLAYER::get_localPlayerID() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->_playerIndex;
		}

		bool jjPLAYER::get_running() const {
			if (_player == nullptr) {
				return {};
			}
			noop(); return _player->_isRunPressed;
		}
		bool jjPLAYER::set_running(bool value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_isRunPressed = value;
			return _player->_isRunPressed;
		}

		std::int32_t jjPLAYER::get_stoned() {
			if (_player == nullptr) {
				return {};
			}
			return (std::int32_t)(_player->_dizzyTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_stoned(std::int32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->SetDizzy(value * FrameTimer::FramesPerSecond / 70.0f);
			return value;
		}

		void jjPLAYER::suckerTube(std::int32_t xSpeed, std::int32_t ySpeed, bool center, bool noclip, bool trigSample) {
			if (_player == nullptr) {
				return;
			}
			noop(); // TODO: trigSample

			// As touching a tube event of these speeds (see Player::HandleAreaEventAt())
			using Actors::Player;
			Player* player = _player;
			if (player->_currentTransition != nullptr && (player->_currentTransition->State == AnimState::TransitionWarpOut ||
				player->_currentTransition->State == AnimState::TransitionWarpOutFreefall)) {
				player->_currentTransitionCancellable = true;
				player->CancelTransition();
			}
			player->EndDamagingMove();
			player->SetAnimation(AnimState::Dash | AnimState::Jump);
			player->_tubeBallPoseLeft = Player::TubeBallPoseTime;
			player->_controllable = false;
			player->SetState(Actors::ActorState::CanJump, false);

			bool isReforged = player->_levelHandler->IsReforged();
			player->_speed = Vector2f(float(xSpeed) * Player::LegacyFrameRateScale, float(ySpeed) * Player::LegacyFrameRateScale);
			if (center) {
				Vector2f pos = player->_pos;
				if (xSpeed == 0 || ySpeed != 0) {
					pos.X = std::floor(pos.X / 32.0f) * 32.0f + 16.0f;
				}
				if (ySpeed == 0 || xSpeed != 0) {
					pos.Y = std::floor(pos.Y / 32.0f) * 32.0f + Player::TubeSnapY;
				}
				player->MoveInstantly(pos, Actors::MoveType::Absolute | Actors::MoveType::Force);
				player->ResetPathTracking();
			}

			player->SetState(Actors::ActorState::CollideWithTileset, !noclip);
			if (noclip || isReforged) {
				player->SetState(Actors::ActorState::ApplyGravitation, false);
			} else {
				player->SetState(Actors::ActorState::ApplyGravitation, true);
			}
			player->_inTubeTime = (noclip ? 600.0f : Player::TubeControlTime);
			// The rise decays at the held rate whatever the jump key did or does, as a spring's (measured)
			player->_jumpReleased = false;
			player->_riseFromFloatUp = false;
			player->_riseOwnedByJump = false;
			player->_isSpring = (ySpeed < 0);
			player->_tubeHeldSpeedY = std::min(player->_speed.Y, 0.0f);
			player->_tubeHeldTimeLeft = 140.0f / Player::LegacyFrameRateScale;
		}
		void jjPLAYER::poleSpin(float xSpeed, float ySpeed, std::uint32_t delay) {
			noop();
		}
		void jjPLAYER::spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample) {
			if (_player == nullptr || (xSpeed == 0.0f && ySpeed == 0.0f)) {
				return;
			}
			// As touching a spring of the speeds of the original, the axis of a zero speed keeps the speed if asked
			auto* levelHandler = _player->_levelHandler;
			Vector2f force(
				xSpeed != 0.0f ? std::copysign(Actors::Environment::Spring::GetStrengthOfLegacySpeed(levelHandler, std::abs(xSpeed), true), xSpeed) : 0.0f,
				ySpeed != 0.0f ? std::copysign(Actors::Environment::Spring::GetStrengthOfLegacySpeed(levelHandler, std::abs(ySpeed), false), ySpeed) : 0.0f);
			bool removeSpecialMove = false;
			_player->OnHitSpring(_player->_pos, force, keepZeroSpeeds && xSpeed == 0.0f, keepZeroSpeeds && ySpeed == 0.0f, removeSpecialMove);
			if (removeSpecialMove) {
				_player->_controllable = true;
				_player->EndDamagingMove();
			}
			if (sample) {
				_levelScriptLoader->PlaySample(_player->_pos, sSPRING_SPRING1, 63, 0, false);
			}
		}

		bool jjPLAYER::get_isConnecting() const {
			noop();
			// TODO Whether the player is a client who has not finished joining the current online server yet.
			return false;
		}
		bool jjPLAYER::get_isIdle() const {
			noop();
			// TODO: Whether the player is idle and does not appear in the level or player list. Currently this can only ever be true of the server.
			return false;
		}
		bool jjPLAYER::get_isOut() const {
			noop();
			// TODO: Equals true if the player has lost all their lives (or joined too late) in an LRS-based gamemode.
			return false;
		}
		bool jjPLAYER::get_isSpectating() const {
			noop();
			// TODO: Equals true if the player is spectating normally, i.e. not forced into spectating by being out or an idle server.
			return false;
		}
		bool jjPLAYER::get_isInGame() const {
			// TODO: Equals true if isActive is true but isConnecting, isIdle, isOut, and isSpectating are all false.
			return (_player != nullptr);
		}

		String jjPLAYER::get_name() const {
			noop();
			return {};
		}
		String jjPLAYER::get_nameUnformatted() const {
			noop();
			return {};
		}
		bool jjPLAYER::setName(const String& name) {
			noop();
			return false;
		}
		std::int8_t jjPLAYER::get_light() const {
			noop();
			return 0;
		}
		std::int8_t jjPLAYER::set_light(std::int8_t value) {
			noop();
			return 0;
		}
		std::uint32_t jjPLAYER::get_fur() const {
			if (_player == nullptr) {
				return {};
			}
			// The engine packs one fur-section start index per byte, matching the JJ2+ 4-byte fur model
			return _player->_furColor;
		}
		std::uint32_t jjPLAYER::set_fur(std::uint32_t value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_furColor = value;
			_player->RefreshColorPalette();
			return value;
		}
		void jjPLAYER::getFur(std::uint8_t& a, std::uint8_t& b, std::uint8_t& c, std::uint8_t& d) const {
			if (_player == nullptr) {
				return;
			}
			std::uint32_t fur = _player->_furColor;
			a = (std::uint8_t)(fur & 0xFF);
			b = (std::uint8_t)((fur >> 8) & 0xFF);
			c = (std::uint8_t)((fur >> 16) & 0xFF);
			d = (std::uint8_t)((fur >> 24) & 0xFF);
		}
		void jjPLAYER::setFur(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
			if (_player == nullptr) {
				return;
			}
			_player->_furColor = (std::uint32_t)a | ((std::uint32_t)b << 8) | ((std::uint32_t)c << 16) | ((std::uint32_t)d << 24);
			_player->RefreshColorPalette();
		}

		bool jjPLAYER::get_noFire() const {
			if (_player == nullptr) {
				return {};
			}
			return !_player->_weaponAllowed;
		}
		bool jjPLAYER::set_noFire(bool value) {
			if (_player == nullptr) {
				return {};
			}
			_player->_weaponAllowed = !value;
			return value;
		}
		bool jjPLAYER::get_antiGrav() const {
			if (_player == nullptr) {
				return {};
			}
			return !_player->GetState(Actors::ActorState::ApplyGravitation);
		}
		bool jjPLAYER::set_antiGrav(bool value) {
			if (_player == nullptr) {
				return {};
			}
			_player->SetState(Actors::ActorState::ApplyGravitation, !value);
			return value;
		}
		bool jjPLAYER::get_invisibility() const {
			if (_player == nullptr) {
				return {};
			}
			return _player->GetState(Actors::ActorState::IsInvulnerable);
		}
		bool jjPLAYER::set_invisibility(bool value) {
			noop();
			return false;
		}
		bool jjPLAYER::get_noclipMode() const {
			if (_player == nullptr) {
				return {};
			}
			return !_player->GetState(Actors::ActorState::CollideWithTileset);
		}
		bool jjPLAYER::set_noclipMode(bool value) {
			if (_player == nullptr) {
				return {};
			}
			// Toggles tileset collision so the player can pass through level geometry, and gravity, a player in noclip mode keeps
			// the speed it has (a burger booth makes the player rise slowly by it)
			_player->SetState(Actors::ActorState::CollideWithTileset | Actors::ActorState::ApplyGravitation, !value);
			return value;
		}
		std::uint8_t jjPLAYER::get_lighting() const {
			if (_player == nullptr) {
				return {};
			}
			// The ambient light of the view of the player, 100 is full light as in JJ2 (see the converter of LIGHT_SET)
			return (std::uint8_t)std::clamp(_levelScriptLoader->_levelHandler->GetAmbientLight(_player) * 100.0f + 0.5f, 0.0f, 255.0f);
		}
		std::uint8_t jjPLAYER::set_lighting(std::uint8_t value) {
			if (_player == nullptr) {
				return {};
			}
			_levelScriptLoader->_levelHandler->SetAmbientLight(_player, value / 100.0f);
			return value;
		}
		std::uint8_t jjPLAYER::resetLight() {
			noop();
			return 0;
		}

		bool jjPLAYER::get_playerKeyLeftPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Left);
		}
		bool jjPLAYER::get_playerKeyRightPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Right);
		}
		bool jjPLAYER::get_playerKeyUpPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Up);
		}
		bool jjPLAYER::get_playerKeyDownPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Down);
		}
		bool jjPLAYER::get_playerKeyFirePressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Fire);
		}
		bool jjPLAYER::get_playerKeySelectPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::ChangeWeapon);
		}
		bool jjPLAYER::get_playerKeyJumpPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Jump);
		}
		bool jjPLAYER::get_playerKeyRunPressed() {
			if (_player == nullptr) {
				return {};
			}
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Run);
		}
		bool jjPLAYER::set_playerKeyLeftPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Left, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyRightPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Right, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyUpPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Up, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyDownPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			// JJ2+ has one key for both, it buttstomps in the air
			auto* levelScripts = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			levelScripts->OverridePlayerInput(_player, PlayerAction::Down, value);
			levelScripts->OverridePlayerInput(_player, PlayerAction::Buttstomp, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyFirePressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Fire, value);
			return value;
		}
		bool jjPLAYER::set_playerKeySelectPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::ChangeWeapon, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyJumpPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Jump, value);
			return value;
		}
		bool jjPLAYER::set_playerKeyRunPressed(bool value) {
			if (_player == nullptr) {
				return {};
			}
			ScriptLoader::FromActiveContext<LevelScriptLoader>()->OverridePlayerInput(_player, PlayerAction::Run, value);
			return value;
		}

		bool jjPLAYER::get_powerup(std::uint8_t number) {
			std::int32_t index = ToWeaponIndex(_player, number);
			if (_player == nullptr || index < 0) {
				return false;
			}
			return (_player->_inventory.WeaponUpgrades[index] & 0x01) == 0x01;
		}
		bool jjPLAYER::set_powerup(std::uint8_t number, bool value) {
			std::int32_t index = ToWeaponIndex(_player, number);
			if (_player == nullptr || index < 0) {
				return false;
			}
			// The rest of the bits are the fast fire of the blaster (see get_fastfire())
			std::uint8_t& upgrades = _player->_inventory.WeaponUpgrades[index];
			upgrades = std::uint8_t((upgrades & ~0x01) | (value ? 0x01 : 0x00));
			return value;
		}
		std::int32_t jjPLAYER::get_ammo(std::uint8_t number) const {
			std::int32_t index = ToWeaponIndex(_player, number);
			if (_player == nullptr || index < 0) {
				return 0;
			}
			// In 1/256 units, UINT16_MAX is unlimited: JJ2+ doesn't count the blaster's, the others keep their count
			std::uint16_t ammo = _player->_inventory.WeaponAmmo[index];
			if (ammo == UINT16_MAX) {
				return _infiniteAmmo[index];
			}
			return ammo / 256;
		}
		std::int32_t jjPLAYER::set_ammo(std::uint8_t number, std::int32_t value) {
			std::int32_t index = ToWeaponIndex(_player, number);
			if (_player == nullptr || index < 0) {
				return value;
			}
			// A weapon of `jjWEAPON::infinite` that the player has gets the unlimited ammo the HUD shows as such
			std::uint16_t& ammo = _player->_inventory.WeaponAmmo[index];
			std::int32_t count = std::clamp<std::int32_t>(value, 0, UINT16_MAX / 256 - 1);
			if (_levelScriptLoader->GetWeapon(std::uint8_t(index + 1))->Infinite) {
				if (count > 0) {
					ammo = UINT16_MAX;
					_infiniteAmmo[index] = std::uint16_t(count);
				} else if (index != std::int32_t(WeaponType::Blaster)) {
					ammo = 0;
				} else {
					_infiniteAmmo[index] = 0;
				}
			} else {
				ammo = std::uint16_t(count * 256);
			}
			return value;
		}

		bool jjPLAYER::offsetPosition(std::int32_t xPixels, std::int32_t yPixels) {
			if (_player == nullptr) {
				return {};
			}

			Vector2f pos = _player->GetPos();
			_player->WarpToPosition(Vector2f(pos.X + xPixels, pos.Y + yPixels), WarpFlags::Fast);
			return true;
		}
		bool jjPLAYER::warpToTile(std::int32_t xTile, std::int32_t yTile, bool fast) {
			if (_player == nullptr) {
				return {};
			}

			// As measured in JJ2+, at the top left corner of the tile, half a pixel above it
			_player->WarpToPosition(Vector2f(float(xTile * TileSet::DefaultTileSize), float(yTile * TileSet::DefaultTileSize) - 0.5f - PlayerOffsetY),
				fast ? WarpFlags::Fast : WarpFlags::Default);
			return true;
		}
		bool jjPLAYER::warpToID(std::uint8_t warpID, bool fast) {
			if (_player == nullptr) {
				return {};
			}

			auto events = _levelScriptLoader->_levelHandler->EventMap();
			Vector2f c = events->GetWarpTarget(warpID);
			if (c.X >= 0.0f && c.Y >= 0.0f) {
				_player->WarpToPosition(c, fast ? WarpFlags::Fast : WarpFlags::Default);
				return true;
			}
			return false;
		}

		std::uint32_t jjPLAYER::morph(bool rabbitsOnly, bool morphEffect) {
			noop();
			return 0;
		}
		std::uint32_t jjPLAYER::morphTo(std::uint32_t charNew, bool morphEffect) {
			if (_player == nullptr) {
				return {};
			}
			noop();
			// TODO: morphEffect
			PlayerType playerType = CharToPlayerType(charNew);
			// MorphTo() leaves the player as it is if the character's sprites aren't available (the bird in the shareware data)
			if (playerType != PlayerType::None) {
				_player->MorphTo(playerType);
			}
			return PlayerTypeToChar(_player->_playerType);
		}
		std::uint32_t jjPLAYER::revertMorph(bool morphEffect) {
			if (_player == nullptr) {
				return {};
			}
			noop();
			// TODO: morphEffect
			// JJ2+ reverts to charOrig, the same as a Revert Morph area does
			_player->MorphTo(_player->_playerTypeOriginal);
			return PlayerTypeToChar(_player->_playerType);
		}
		std::uint32_t jjPLAYER::get_charCurr() const {
			if (_player == nullptr) {
				return {};
			}
			return PlayerTypeToChar(_player->_playerType, _levelScriptLoader->IsTSF());
		}
		std::uint32_t jjPLAYER::get_charOrig() const {
			if (_player == nullptr) {
				return {};
			}
			return PlayerTypeToChar(_player->_playerTypeOriginal);
		}
		std::uint32_t jjPLAYER::set_charOrig(std::uint32_t value) {
			if (_player == nullptr) {
				return {};
			}
			PlayerType playerType = CharToPlayerType(value);
			// Only a rabbit can be what the player reverts to, a temporary form is never anyone's original character
			if (playerType == PlayerType::Jazz || playerType == PlayerType::Spaz || playerType == PlayerType::Lori) {
				_player->_playerTypeOriginal = playerType;
			}
			return value;
		}

		void jjPLAYER::kill() {
			if (_player == nullptr) {
				return;
			}
			_player->DecreaseHealth(INT32_MAX);
		}
		bool jjPLAYER::hurt(std::int8_t damage, bool forceHurt, jjPLAYER* attacker) {
			if (_player == nullptr) {
				return {};
			}
			// Whether the player was hurt, scripts act on it (bombs explode then) - one blinking after
			// a hit is hurt only when forced
			if (forceHurt && _player->_invulnerableTime > 0.0f) {
				_player->_invulnerableTime = 0.0f;
				_player->SetState(Actors::ActorState::IsInvulnerable, false);
			}
			std::int32_t health = _player->GetHealth();
			_player->TakeDamage(damage);
			return (_player->GetHealth() < health);
		}

		std::uint32_t jjPLAYER::get_timerState() const {
			return _timerState;
		}
		bool jjPLAYER::get_timerPersists() const {
			return _timerPersists;
		}
		bool jjPLAYER::set_timerPersists(bool value) {
			_timerPersists = value;
			return value;
		}
		std::uint32_t jjPLAYER::timerStart(std::int32_t ticks, bool startPaused) {
			_timerLeft = (float)ticks * FrameTimer::FramesPerSecond / 70;
			_timerState = (startPaused ? 2 : 1);
			return _timerState;
		}
		std::uint32_t jjPLAYER::timerPause() {
			// Only a started timer pauses, a stopped one stays stopped
			if (_timerState == 1) { // STARTED
				_timerState = 2; // PAUSED
			}
			return _timerState;
		}
		std::uint32_t jjPLAYER::timerResume() {
			if (_timerState == 2) { // PAUSED
				_timerState = 1; // STARTED
			}
			return _timerState;
		}
		std::uint32_t jjPLAYER::timerStop() {
			_timerLeft = 0.0f;
			_timerState = 0; // STOPPED
			return _timerState;
		}
		std::int32_t jjPLAYER::get_timerTime() const {
			std::int32_t jjTicksLeft = (std::int32_t)(_timerLeft * 70 / FrameTimer::FramesPerSecond);
			return jjTicksLeft;
		}
		std::int32_t jjPLAYER::set_timerTime(std::int32_t value) {
			_timerLeft = (float)value * FrameTimer::FramesPerSecond / 70;
			return value;
		}
		void jjPLAYER::timerFunction(const String& functionName) {
			asIScriptFunction* func = _levelScriptLoader->GetMainModule()->GetFunctionByName(String::nullTerminatedView(functionName).data());
			_timerCallback = func;
		}
		void jjPLAYER::timerFunctionPtr(void* function) {
			_timerCallback = function;
		}
		void jjPLAYER::timerFunctionFuncPtr(void* function) {
			_timerCallback = function;
		}

		bool jjPLAYER::activateBoss(bool activate) {
			if (_player == nullptr) {
				return {};
			}
			noop();

			// TODO: activate
			_levelScriptLoader->_levelHandler->BroadcastTriggeredEvent(_player, EventType::AreaActivateBoss, nullptr);
			return true;
		}
		bool jjPLAYER::limitXScroll(std::uint16_t left, std::uint16_t width) {
			if (_player == nullptr) {
				return {};
			}

			_levelScriptLoader->_levelHandler->LimitCameraView(_player, _player->_pos, left * Tiles::TileSet::DefaultTileSize, width * Tiles::TileSet::DefaultTileSize);
			return true;
		}
		// An axis given by a bool is let go of if it's true, and left as it is otherwise
		void jjPLAYER::cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, CameraOverride::Fix, xPixel, CameraOverride::Fix, yPixel, centered, instant);
		}
		void jjPLAYER::cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, xUnfreeze ? CameraOverride::Follow : CameraOverride::Keep, 0.0f,
				CameraOverride::Fix, yPixel, centered, instant);
		}
		void jjPLAYER::cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, CameraOverride::Fix, xPixel,
				yUnfreeze ? CameraOverride::Follow : CameraOverride::Keep, 0.0f, centered, instant);
		}
		void jjPLAYER::cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, xUnfreeze ? CameraOverride::Follow : CameraOverride::Keep, 0.0f,
				yUnfreeze ? CameraOverride::Follow : CameraOverride::Keep, 0.0f, centered, instant);
		}
		void jjPLAYER::cameraUnfreeze(bool instant) {
			if (_player == nullptr) {
				return;
			}
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, CameraOverride::Follow, 0.0f, CameraOverride::Follow, 0.0f, false, instant);
		}
		void jjPLAYER::showText(const String& text, std::uint32_t size) {
			// In the colors and the encoding of the texts of the level, as JJ2+ shows them
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			_levelScriptLoader->ShowLevelText(recodedText, size);
		}
		void jjPLAYER::showTextByID(std::uint32_t textID, std::uint32_t offset, std::uint32_t size) {
			auto text = _levelScriptLoader->_levelHandler->GetLevelText(textID, offset, '|');
			_levelScriptLoader->ShowLevelText(text, size);
		}

		// FLIGHT::NONE is 0, FLYCARROT 1 (any copter) and AIRBOARD -1
		std::uint32_t jjPLAYER::get_fly() const {
			if (_player == nullptr) {
				return 0;
			}
			switch (_player->GetModifier()) {
				case Actors::Player::Modifier::Airboard: return UINT32_MAX;
				case Actors::Player::Modifier::Copter:
				case Actors::Player::Modifier::LizardCopter: return 1;
				default: return 0;
			}
		}
		std::uint32_t jjPLAYER::set_fly(std::uint32_t value) {
			if (_player == nullptr) {
				return value;
			}
			_player->SetModifier(value == UINT32_MAX ? Actors::Player::Modifier::Airboard
				: (value == 1 ? Actors::Player::Modifier::Copter : Actors::Player::Modifier::None));
			return value;
		}

		std::int32_t jjPLAYER::fireBulletDirection(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, std::uint32_t direction) {
			// As measured in JJ2+: right and left turn the player that way, the current one is up while looking up
			if (_player == nullptr) {
				return 0;
			}
			constexpr float HalfPi = 1.5707963f;
			float angle;
			switch (direction) {
				case dirRIGHT: _player->SetFacingLeft(false); angle = HalfPi; break;
				case dirLEFT: _player->SetFacingLeft(true); angle = 3.0f * HalfPi; break;
				case dirUP: angle = 0.0f; break;
				default: {
					bool lookingUp = ((_player->_currentAnimation->State & AnimState::Lookup) == AnimState::Lookup);
					angle = (lookingUp ? 0.0f : (_player->IsFacingLeft() ? 3.0f * HalfPi : HalfPi));
					break;
				}
			}
			return fireBulletAngle(gun, depleteAmmo, requireAmmo, angle);
		}

		template<typename T>
		Actors::ActorBase* jjPLAYER::FireAimedNativeShot(WeaponType weaponType, float aimAngle) {
			// The shot of the game turned to the angle (0 up, clockwise), the side it goes to is the side it faces
			Vector3i initialPos;
			Vector2f gunspotPos;
			float angle;
			_player->GetFirePointAndAngle(initialPos, gunspotPos, angle);
			float x = std::sin(aimAngle), y = -std::cos(aimAngle);
			bool left = (x < -0.0001f || (std::abs(x) <= 0.0001f && _player->IsFacingLeft()));
			float shotAngle = (left ? std::atan2(-y, -x) : std::atan2(y, x));

			std::uint8_t shotParams[1] = { _player->_inventory.WeaponUpgrades[(std::int32_t)weaponType] };
			std::shared_ptr<T> shot = std::make_shared<T>();
			shot->OnActivated(Actors::ActorActivationDetails(_player->_levelHandler, initialPos, shotParams));
			shot->OnFire(_player->shared_from_this(), gunspotPos, _player->_speed, shotAngle, left);
			_player->_levelHandler->AddActor(shot);
			return shot.get();
		}

		std::int32_t jjPLAYER::fireBulletAngle(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, float angle) {
			// At the angle as measured in JJ2+ (0 up, clockwise)
			if (_player == nullptr) {
				return 0;
			}
			std::int32_t index = (gun == 0 ? std::int32_t(_player->_currentWeapon) : ToWeaponIndex(gun));
			if (index < 0 || index >= std::int32_t(WeaponType::Count)) {
				return 0;
			}
			WeaponType weaponType = WeaponType(index);
			auto& ammo = _player->_inventory.WeaponAmmo[index];
			bool infinite = (weaponType == WeaponType::Blaster || ammo == UINT16_MAX);
			// The blaster's count is never spent, but JJ2+ still refuses it at 0
			if (requireAmmo && (weaponType == WeaponType::Blaster ? get_ammo(std::uint8_t(index + 1)) <= 0 : (!infinite && ammo < 256))) {
				return 0;
			}
			constexpr float TwoPi = 6.2831853f;
			angle = std::fmod(angle, TwoPi);
			if (angle < 0.0f) {
				angle += TwoPi;
			}
			Actors::ActorBase* shot = nullptr;
			if (!FireScriptedWeapon(weaponType, angle, &shot)) {
				switch (weaponType) {
					case WeaponType::Blaster: shot = FireAimedNativeShot<Actors::Weapons::BlasterShot>(weaponType, angle); break;
					case WeaponType::Bouncer: shot = FireAimedNativeShot<Actors::Weapons::BouncerShot>(weaponType, angle); break;
					case WeaponType::Freezer: shot = FireAimedNativeShot<Actors::Weapons::FreezerShot>(weaponType, angle); break;
					case WeaponType::Seeker: shot = FireAimedNativeShot<Actors::Weapons::SeekerShot>(weaponType, angle); break;
					case WeaponType::RF: shot = FireAimedNativeShot<Actors::Weapons::RFShot>(weaponType, angle); break;
					case WeaponType::Toaster: shot = FireAimedNativeShot<Actors::Weapons::ToasterShot>(weaponType, angle); break;
					case WeaponType::Pepper: shot = FireAimedNativeShot<Actors::Weapons::PepperShot>(weaponType, angle); break;
					case WeaponType::Electro: shot = FireAimedNativeShot<Actors::Weapons::ElectroShot>(weaponType, angle); break;
					default: noop(); break;
				}
			}
			if (shot == nullptr) {
				return 0;
			}
			if (depleteAmmo && !infinite) {
				ammo = std::uint16_t(ammo - 256);
			}
			// The weapon waits for the next shot as after one the player fired (see Player::FireCurrentWeapon())
			std::int32_t fastFire = (_player->_inventory.WeaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
			_player->_weaponCooldown = std::max(_player->_weaponCooldown, 30.0f - (fastFire * 2.76f));
			// A script ends a buttstomp by `buttstomp = 121` before it fires down (a sword), as measured in JJ2+
			if (buttstomp >= 41 && _player->_currentSpecialMove == Actors::Player::SpecialMoveType::Buttstomp) {
				_player->EndDamagingMove();
			}
			jjOBJ* obj = _levelScriptLoader->GetObjectView(shot);
			return (obj != nullptr ? obj->objectID : 0);
		}

		// The top left corner of the view, as JJ2+ has it - the layer coordinate of the sprite layer there (see GetLayerPositionInView())
		float jjPLAYER::get_cameraX() const {
			if (_player == nullptr) {
				return {};
			}
			return _levelScriptLoader->_levelHandler->GetCameraView(_player).X;
		}
		float jjPLAYER::get_cameraY() const {
			if (_player == nullptr) {
				return {};
			}
			return _levelScriptLoader->_levelHandler->GetCameraView(_player).Y;
		}
		std::int32_t jjPLAYER::get_deaths() const {
			noop();
			return 0;
		}

		bool jjPLAYER::get_isJailed() const {
			noop();
			return false;
		}
		bool jjPLAYER::get_isZombie() const {
			noop();
			return false;
		}
		std::int32_t jjPLAYER::get_lrsLives() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_roasts() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_laps() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_lapTimeCurrent() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_lapTimes(std::uint32_t index) const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_lapTimeBest() const {
			noop();
			return 0;
		}
		bool jjPLAYER::get_isAdmin() const {
			noop();
			return false;
		}
		bool jjPLAYER::hasPrivilege(const String& privilege, std::uint32_t moduleID) const {
			noop();
			return false;
		}

		bool jjPLAYER::doesCollide(const jjOBJ* object, bool always) const {
			// The pixels of the frame the object shows against those of the player, as jjOBJ::doesCollidePlayer()
			bool result = (object != nullptr && _player != nullptr && DoesObjectTouchPlayer(_levelScriptLoader, *object, *_player));
			if (object != nullptr) {
				// A handle passed to a native function is the function's to release
				const_cast<jjOBJ*>(object)->Release();
			}
			return result;
		}
		int jjPLAYER::getObjectHitForce(const jjOBJ& target) const {
			noop();
			return 0;
		}
		bool jjPLAYER::objectHit(jjOBJ* target, std::int32_t force, std::uint32_t playerHandling) {
			// The player collides with the target as the game would have it, even where they aren't, the script sees the
			// energy the target has then
			std::shared_ptr<Actors::ActorBase> victim = (target != nullptr ? target->_actor.lock() : nullptr);
			bool result = false;
			if (_player != nullptr && victim != nullptr) {
				if (target->_scriptedCollisions) {
					// The behavior decides, as in JJ2+ - the game doesn't collect a pickup then
					result = ScriptObjectHost::CallObjectHit(_levelScriptLoader, *target, nullptr, this, force);
				} else {
					result = (_player->OnHandleCollision(victim.get()) || victim->OnHandleCollision(_player));
					target->energy = std::int8_t(std::clamp<std::int32_t>(victim->GetHealth(), INT8_MIN, INT8_MAX));
					target->_synced.Energy = target->energy;
					if ((victim->GetHealth() <= 0 || victim->GetState(Actors::ActorState::IsDestroyed)) && runtime_cast<Actors::Collectibles::CollectibleBase>(victim.get()) != nullptr) {
						// A collected pickup is a sparkle at once, as measured in JJ2+ - scripts tell the ones that are gone by their
						// behavior (series of gems keep drawing theirs otherwise)
						constexpr std::uint32_t BehaviorExplosion2 = aCOUNT + 21;
						constexpr std::uint32_t HandlingExplosion = 4;
						target->behavior = BehaviorExplosion2;
						target->_playerHandling = HandlingExplosion;
					}
				}
			}
			if (target != nullptr) {
				// A handle passed to a native function is the function's to release
				target->Release();
			}
			return result;
		}
		bool jjPLAYER::isEnemy(const jjPLAYER* victim) const {
			noop();
			return false;
		}

		std::uint32_t jjPLAYER::get_spriteMode() const {
			return _spriteMode;
		}
		class jjPLAYER::InvisibleOverride : public Actors::IActorUpdateOverride
		{
		public:
			void OnUpdate(Actors::ActorBase* actor, float timeMult) override {
				RunPlayerUpdate(static_cast<Actors::Player*>(actor), timeMult);
			}
			bool IsDrawn() const override {
				return false;
			}
		};

		void jjPLAYER::UpdateDrawOverride() {
			if (_player == nullptr) {
				return;
			}
			bool hidden = (_spriteMode == spriteType_INVISIBLE || _drawnByScript);
			// Nothing else hands over the update of a player
			bool overridden = (_player->GetUpdateOverride() != nullptr);
			if (hidden != overridden) {
				_player->SetUpdateOverride(hidden ? std::make_unique<InvisibleOverride>() : nullptr);
			}
		}
		void jjPLAYER::SetDrawnByScript(bool value) {
			if (_drawnByScript != value) {
				_drawnByScript = value;
				UpdateDrawOverride();
			}
		}

		void jjPLAYER::RunPlayerUpdate(Actors::Player* player, float timeMult) {
			player->OnUpdate(timeMult);
		}

		std::uint32_t jjPLAYER::set_spriteMode(std::uint32_t value) {
			// Only SPRITE::INVISIBLE is taken, the player decides how it's drawn every frame itself (blinking)
			_spriteMode = value;
			UpdateDrawOverride();
			return value;
		}
		std::uint8_t jjPLAYER::get_spriteParam() const {
			return _spriteParam;
		}
		std::uint8_t jjPLAYER::set_spriteParam(std::uint8_t value) {
			_spriteParam = value;
			return value;
		}

		std::int32_t jjPLAYER::get_subscreenX() const {
			return (_player != nullptr ? _levelScriptLoader->GetPlayerSubscreenPosition(_player).X : 0);
		}
		std::int32_t jjPLAYER::get_subscreenY() const {
			return (_player != nullptr ? _levelScriptLoader->GetPlayerSubscreenPosition(_player).Y : 0);
		}

		bool jjPLAYER::FindShownFrame(std::uint32_t& anim, std::uint32_t& position, std::uint32_t& firstFrame) const {
			if (_player == nullptr) {
				return false;
			}
			auto& tables = _levelScriptLoader->GetAnimationTables();
			anim = tables.FindAnimationOfGraphics(_player->_renderer.FrameSource);
			if (anim == 0 && _player->_currentTransition != nullptr && _player->_currentAnimation != nullptr) {
				// A transition the original has no animation of (`jump_shoot_end` of the remastered content after a shot in
				// the air), the animation it leads to then - scripts that draw the player themselves would draw nothing
				anim = tables.FindAnimationOfGraphics(_player->_currentAnimation->Base);
			}
			std::uint32_t frameCount;
			if (anim == 0 || !tables.GetAnimationFrames(anim, frameCount, firstFrame) || frameCount == 0) {
				return false;
			}
			position = std::uint32_t(std::max<std::int32_t>(_player->_renderer.CurrentFrame, 0)) % frameCount;
			return true;
		}
		std::uint16_t jjPLAYER::get_curAnim() const {
			std::uint32_t anim, position, firstFrame;
			return (FindShownFrame(anim, position, firstFrame) ? std::uint16_t(anim) : 0);
		}
		std::uint32_t jjPLAYER::get_curFrame() const {
			std::uint32_t anim, position, firstFrame;
			return (FindShownFrame(anim, position, firstFrame) ? firstFrame + position : 0);
		}
		std::uint8_t jjPLAYER::get_frameID() const {
			std::uint32_t anim, position, firstFrame;
			return (FindShownFrame(anim, position, firstFrame) ? std::uint8_t(position) : 0);
		}

		void jjPLAYER::SyncPropertiesToBackingStore() {
			if (_player == nullptr) {
				return;
			}

			// What only the game decides, as JJ2+ has it - a buttstomp counts down from below 41, 50 is none
			buttstomp = (_player->_currentSpecialMove == Actors::Player::SpecialMoveType::Buttstomp ? 1 : 50);
			specialMove = std::int32_t(_player->_currentSpecialMove);
			// A boss of the game is the one, a boss of a script is told by the script (`boss`)
			// - and what a script set stays as it set it (a boss of a script ends it so when it's slain)
			std::int32_t nativeBoss = _levelScriptLoader->GetActiveBossObjectID();
			if (nativeBoss != 0) {
				bossNumber = nativeBoss;
			}
			bool nativeBossActive = (nativeBoss != 0 || _levelScriptLoader->IsBossAreaActivated());
			if (!_syncedOnce || bossActive == _syncedBossActive) {
				bossActive = nativeBossActive;
			}
			_syncedBossActive = nativeBossActive;
			_syncedOnce = true;
			// The object the player stands on, if it's an object of the scripts (trapezes are told by it)
			// - none while it dies (a player dying on a trapeze would be drawn still hanging there)
			jjOBJ* standingOn = (_player->_carryingObject != nullptr && _player->GetHealth() > 0
				? _levelScriptLoader->GetObjectView(_player->_carryingObject) : nullptr);
			platform = (standingOn != nullptr ? standingOn->objectID : 0);
			if (platform == 0 && _pushedTick >= _levelScriptLoader->GetGameTick() - 1) {
				platform = _pushedObject;
			}
		}
		bool jjPLAYER::FireScriptedWeapon(WeaponType weaponType, float aimAngle, Actors::ActorBase** lastShot) {
			// The bullet of the weapon must have a behavior of the script, the behavior moves the bullets then
			std::uint8_t bullet;
			switch (weaponType) {
				case WeaponType::Pepper: bullet = aPLAYERBULLET8; break;
				case WeaponType::Electro: bullet = aPLAYERBULLET9; break;
				default: bullet = (weaponType <= WeaponType::Toaster ? std::uint8_t(aPLAYERBULLET1 + std::int32_t(weaponType)) : std::uint8_t(0)); break;
			}
			const jjOBJ* preset = (bullet != 0 ? _levelScriptLoader->FindObjectPreset(bullet) : nullptr);
			if (preset == nullptr || (preset->behavior.function == nullptr && preset->behavior.object == nullptr)) {
				return false;
			}

			// As JJ2+ fires by `SPREAD::Spread` (measured): RFNORMAL two bullets angled up and down, RFPU those and a straight
			// one, RF two or three when powered up, every other spread one
			WeaponSpread spread = _levelScriptLoader->GetWeapon(std::int32_t(weaponType) + 1)->Spread;
			bool upgraded = ((_player->_inventory.WeaponUpgrades[(std::int32_t)weaponType] & 0x01) != 0);
			bool angled = (spread == WeaponSpread::Double || spread == WeaponSpread::Triple || spread == WeaponSpread::DoubleOrTriple) && aimAngle < 0.0f;
			bool straight = (!angled || spread == WeaponSpread::Triple || (spread == WeaponSpread::DoubleOrTriple && upgraded));
			for (std::int8_t slot = -1; slot <= 1; slot++) {
				if (slot == 0 ? !straight : !angled) {
					continue;
				}
				Actors::ActorBase* shot = nullptr;
				switch (weaponType) {
					case WeaponType::Blaster: shot = FireSpreadShot<Actors::Weapons::BlasterShot>(weaponType, slot, aimAngle); break;
					case WeaponType::Bouncer: shot = FireSpreadShot<Actors::Weapons::BouncerShot>(weaponType, slot, aimAngle); break;
					case WeaponType::Freezer: shot = FireSpreadShot<Actors::Weapons::FreezerShot>(weaponType, slot, aimAngle); break;
					case WeaponType::Seeker: shot = FireSpreadShot<Actors::Weapons::SeekerShot>(weaponType, slot, aimAngle); break;
					case WeaponType::RF: shot = FireSpreadShot<Actors::Weapons::RFShot>(weaponType, slot, aimAngle); break;
					case WeaponType::Toaster: shot = FireSpreadShot<Actors::Weapons::ToasterShot>(weaponType, slot, aimAngle); break;
					case WeaponType::Pepper: shot = FireSpreadShot<Actors::Weapons::PepperShot>(weaponType, slot, aimAngle); break;
					case WeaponType::Electro: shot = FireSpreadShot<Actors::Weapons::ElectroShot>(weaponType, slot, aimAngle); break;
					default: break;
				}
				if (lastShot != nullptr && shot != nullptr) {
					*lastShot = shot;
				}
			}
			return true;
		}

		template<typename T>
		Actors::ActorBase* jjPLAYER::FireSpreadShot(WeaponType weaponType, std::int8_t slot, float aimAngle) {
			Vector3i initialPos;
			Vector2f gunspotPos;
			float angle;
			_player->GetFirePointAndAngle(initialPos, gunspotPos, angle);
			float angleOffset = slot * (_player->IsFacingLeft() ? -0.2f : 0.2f);

			std::uint8_t shotParams[1] = { _player->_inventory.WeaponUpgrades[(std::int32_t)weaponType] };
			std::shared_ptr<T> shot = std::make_shared<T>();
			shot->OnActivated(Actors::ActorActivationDetails(_player->_levelHandler, initialPos, shotParams));
			shot->OnFire(_player->shared_from_this(), gunspotPos, _player->_speed, angle + angleOffset, _player->IsFacingLeft());
			// Its view is made as JJ2+ fires it when the level adds it (see LevelScriptLoader::EnsureObjectView())
			bool upward = (aimAngle < 0.0f && !_player->_inWater && std::abs(std::sin(angle)) > 0.5f);
			_levelScriptLoader->_firingBullet = { shot.get(), slot, gunspotPos.Y - _player->_pos.Y, upward, aimAngle };
			_player->_levelHandler->AddActor(shot);
			_levelScriptLoader->_firingBullet = {};
			return shot.get();
		}

		bool jjPLAYERDRAW::get_shield(std::int32_t shield) const {
			noop();
			return false;
		}
		bool jjPLAYERDRAW::set_shield(std::int32_t shield, bool enable) {
			noop();
			return false;
		}
		jjPLAYER* jjPLAYERDRAW::get_player() const {
			return _player;
		}
	}

	std::int32_t get_jjPlayerCount() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayers().size();
	}
	std::int32_t get_jjLocalPlayerCount() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayers().size();
	}

	jjPLAYER* get_jjP() {

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayerBackingStore(0);
	}
	jjPLAYER* get_jjPlayers(std::uint8_t index) {

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayerBackingStore(index);
	}
	jjPLAYER* get_jjLocalPlayers(std::uint8_t index) {

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayerBackingStore(index);
	}

	bool LevelScriptLoader::FirePlayerWeapon(Actors::Player* player, WeaponType weaponType)
	{
		jjPLAYER* p = GetPlayerBackingStore(player);
		return (p != nullptr && p->FireScriptedWeapon(weaponType));
	}

	Legacy::jjWEAPON* LevelScriptLoader::GetWeapon(std::int32_t number)
	{
		// The options of the level itself, so the game plays by what a script changes; the entry 0 of JJ2+ is no
		// weapon, it's a scratch one
		if (number >= 1 && number <= std::int32_t(WeaponType::Count)) {
			if (WeaponDefinition* definition = _levelHandler->GetWritableWeaponDefinition(WeaponType(number - 1))) {
				return &definition->Options;
			}
		}
		return &_scratchWeapon;
	}

	Vector2i LevelScriptLoader::GetPlayerSubscreenPosition(Actors::Player* player) const
	{
		for (const auto& viewport : _levelHandler->_assignedViewports) {
			if (viewport->GetTargetActor() == player) {
				Rectf bounds = viewport->GetBounds();
				return Vector2i(std::int32_t(bounds.X), std::int32_t(bounds.Y));
			}
		}
		return {};
	}

	jjWEAPON* get_jjWEAPON(int index) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetWeapon(index);
	}

	static bool get_jjWEAPON_infinite(jjWEAPON* self) {
		return self->Infinite;
	}
	static bool set_jjWEAPON_infinite(jjWEAPON* self, bool value) {
		if (self->Infinite == value) {
			return value;
		}
		// The ammo the players have is read as it was and written as it is now, set_ammo() converts it
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		for (std::int32_t number = 1; number <= std::int32_t(WeaponType::Count); number++) {
			if (owner->GetWeapon(number) != self) {
				continue;
			}
			for (Actors::Player* player : owner->GetPlayers()) {
				if (jjPLAYER* p = owner->GetPlayerBackingStore(player)) {
					std::int32_t ammo = p->get_ammo(std::uint8_t(number));
					self->Infinite = value;
					p->set_ammo(std::uint8_t(number), ammo);
					self->Infinite = !value;
				}
			}
			break;
		}
		self->Infinite = value;
		return value;
	}

	jjCHARACTER* get_jjCHARACTER(int index) {
		static jjCHARACTER dummy;
		return &dummy;
	}

	void LevelScriptLoader::RegisterLegacyPlayer(asIScriptEngine* engine)
	{
		// TODO
		engine->RegisterObjectType("jjPLAYER", sizeof(jjPLAYER), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("const int get_jjPlayerCount()", asFUNCTION(get_jjPlayerCount), asCALL_CDECL);
		engine->RegisterGlobalFunction("const int get_jjLocalPlayerCount()", asFUNCTION(get_jjLocalPlayerCount), asCALL_CDECL);
		/** @deprecated Use @ref jjLocalPlayers or @ref jjPlayers instead, or the player a hook gets as its parameter. */
		engine->RegisterGlobalFunction("jjPLAYER@ get_jjP()", asFUNCTION(get_jjP), asCALL_CDECL); // Deprecated
		/**
		 * @brief Alias of @ref jjP, the only global without the `jj` prefix
		 * @deprecated Use @ref jjLocalPlayers or @ref jjPlayers instead, or the player a hook gets as its parameter.
		 */
		engine->RegisterGlobalFunction("jjPLAYER@ get_p()", asFUNCTION(get_jjP), asCALL_CDECL); // Deprecated
		engine->RegisterGlobalFunction("jjPLAYER@ get_jjPlayers(uint8)", asFUNCTION(get_jjPlayers), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPLAYER@ get_jjLocalPlayers(uint8)", asFUNCTION(get_jjLocalPlayers), asCALL_CDECL);

		engine->SetDefaultNamespace("WEAPON");
		/**
		 * @brief Weapon a player can use
		 *
		 * Numbered from 1, so plain numbers 1 to 9 work in their place, e.g. as an index of @ref jjPLAYER::ammo or @ref jjWeapons.
		 */
		engine->RegisterEnum("Weapon");
		engine->RegisterEnumValue("Weapon", "BLASTER", 1);
		engine->RegisterEnumValue("Weapon", "BOUNCER", 2);
		engine->RegisterEnumValue("Weapon", "ICE", 3);
		engine->RegisterEnumValue("Weapon", "SEEKER", 4);
		engine->RegisterEnumValue("Weapon", "RF", 5);
		engine->RegisterEnumValue("Weapon", "TOASTER", 6);
		engine->RegisterEnumValue("Weapon", "TNT", 7);
		engine->RegisterEnumValue("Weapon", "GUN8", 8);	///< Pepper spray, or the fireball gun when it is enabled
		engine->RegisterEnumValue("Weapon", "GUN9", 9);	///< Electro blaster, or the blade gun when it is enabled
		engine->RegisterEnumValue("Weapon", "CURRENT", 0);	///< The weapon the player has selected
		/**
		 * @brief How a weapon fires while the fire key is held
		 *
		 * Used by @ref jjWEAPON::style.
		 */
		engine->RegisterEnum("Style");
		engine->RegisterEnumValue("Style", "NORMAL", wsNORMAL);	///< Fires repeatedly, as fast as @ref jjPLAYER::fastfire allows
		engine->RegisterEnumValue("Style", "MISSILE", wsMISSILE);	///< Fires once per press of the fire key
		engine->RegisterEnumValue("Style", "POPCORN", wsPOPCORN);	///< Fires repeatedly by @ref jjPLAYER::fastfire, but never faster than a fixed limit
		engine->RegisterEnumValue("Style", "CAPPED", wsCAPPED);	///< Like NORMAL, but pressing fire faster doesn't beat @ref jjPLAYER::fastfire
		/**
		 * @brief Older name of CAPPED
		 * @deprecated Use @ref WEAPON::CAPPED instead, this is only its older name.
		 */
		engine->RegisterEnumValue("Style", "TUNA", wsCAPPED); // Deprecated
		engine->SetDefaultNamespace("SPREAD");
		/**
		 * @brief How many bullets a weapon fires at once and where
		 *
		 * Used by @ref jjWEAPON::spread.
		 */
		engine->RegisterEnum("Spread");
		engine->RegisterEnumValue("Spread", "NORMAL", wspNORMAL);	///< One bullet
		engine->RegisterEnumValue("Spread", "ICE", wspNORMALORDIRECTIONANDAIM);	///< NORMAL without a powerup, ICEPU with it
		engine->RegisterEnumValue("Spread", "ICEPU", wspDIRECTIONANDAIM);	///< Two bullets, one where the player faces and one where it aims
		engine->RegisterEnumValue("Spread", "RF", wspDOUBLEORTRIPLE);	///< RFNORMAL without a powerup, RFPU with it
		engine->RegisterEnumValue("Spread", "RFNORMAL", wspDOUBLE);	///< Two bullets, angled up and down
		engine->RegisterEnumValue("Spread", "RFPU", wspTRIPLE);	///< Three bullets, angled up, straight and angled down
		engine->RegisterEnumValue("Spread", "TOASTER", wspREFLECTSFASTFIRE);	///< One bullet whose speed depends partly on @ref jjPLAYER::fastfire
		engine->RegisterEnumValue("Spread", "GUN8", wspNORMALORBBGUN);	///< NORMAL when the fireball gun is enabled, PEPPERSPRAY otherwise
		engine->RegisterEnumValue("Spread", "PEPPERSPRAY", wspBBGUN);	///< Two bullets of pepper spray
		engine->SetDefaultNamespace("GEM");
		/** @brief Color of a gem */
		engine->RegisterEnum("Color");
		engine->RegisterEnumValue("Color", "RED", 1);
		engine->RegisterEnumValue("Color", "GREEN", 2);
		engine->RegisterEnumValue("Color", "BLUE", 3);
		engine->RegisterEnumValue("Color", "PURPLE", 4);
		engine->SetDefaultNamespace("SHIELD");
		/** @brief Shield a player can have */
		engine->RegisterEnum("Shield");
		engine->RegisterEnumValue("Shield", "NONE", 0);	///< No shield
		engine->RegisterEnumValue("Shield", "FIRE", 1);
		engine->RegisterEnumValue("Shield", "BUBBLE", 2);	///< Water shield, same as WATER
		engine->RegisterEnumValue("Shield", "WATER", 2);	///< Same as BUBBLE
		engine->RegisterEnumValue("Shield", "LIGHTNING", 3);	///< Same as PLASMA
		engine->RegisterEnumValue("Shield", "PLASMA", 3);	///< Same as LIGHTNING
		engine->RegisterEnumValue("Shield", "LASER", 4);

		engine->SetDefaultNamespace("");
		engine->RegisterObjectMethod("jjPLAYER", "int get_score() const", asMETHOD(jjPLAYER, get_score), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_score(int)", asMETHOD(jjPLAYER, set_score), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_scoreDisplayed() const", asMETHOD(jjPLAYER, get_scoreDisplayed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_scoreDisplayed(int)", asMETHOD(jjPLAYER, set_scoreDisplayed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int setScore(int score)", asMETHOD(jjPLAYER, setScore), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_xPos() const", asMETHOD(jjPLAYER, get_xPos), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float set_xPos(float)", asMETHOD(jjPLAYER, set_xPos), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_yPos() const", asMETHOD(jjPLAYER, get_yPos), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float set_yPos(float)", asMETHOD(jjPLAYER, set_yPos), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_xAcc() const", asMETHOD(jjPLAYER, get_xAcc), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float set_xAcc(float)", asMETHOD(jjPLAYER, set_xAcc), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_yAcc() const", asMETHOD(jjPLAYER, get_yAcc), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float set_yAcc(float)", asMETHOD(jjPLAYER, set_yAcc), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_xOrg() const", asMETHOD(jjPLAYER, get_xOrg), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float set_xOrg(float)", asMETHOD(jjPLAYER, set_xOrg), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_yOrg() const", asMETHOD(jjPLAYER, get_yOrg), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float set_yOrg(float)", asMETHOD(jjPLAYER, set_yOrg), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_xSpeed() const", asMETHOD(jjPLAYER, get_xSpeed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float set_xSpeed(float)", asMETHOD(jjPLAYER, set_xSpeed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_ySpeed() const", asMETHOD(jjPLAYER, get_ySpeed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float set_ySpeed(float)", asMETHOD(jjPLAYER, set_ySpeed), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPLAYER", "float jumpStrength", asOFFSET(jjPLAYER, jumpStrength));
		engine->RegisterObjectMethod("jjPLAYER", "int8 get_frozen() const", asMETHOD(jjPLAYER, get_frozen), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int8 set_frozen(int8)", asMETHOD(jjPLAYER, set_frozen), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void freeze(bool frozen = true)", asMETHOD(jjPLAYER, freeze), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_currTile() const", asMETHOD(jjPLAYER, get_currTile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool startSugarRush(int time = 1400)", asMETHOD(jjPLAYER, startSugarRush), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int8 get_health() const", asMETHOD(jjPLAYER, get_health), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int8 set_health(int8)", asMETHOD(jjPLAYER, set_health), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPLAYER", "const int warpID", asOFFSET(jjPLAYER, warpID));
		engine->RegisterObjectMethod("jjPLAYER", "int get_fastfire() const", asMETHOD(jjPLAYER, get_fastfire), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_fastfire(int)", asMETHOD(jjPLAYER, set_fastfire), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 get_currWeapon() const", asMETHOD(jjPLAYER, get_currWeapon), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 set_currWeapon(uint8)", asMETHOD(jjPLAYER, set_currWeapon), asCALL_THISCALL);
		
		//engine->RegisterObjectMethod("jjPLAYER", "int get_lives() const", asMETHOD(jjPLAYER, get_lives), asCALL_THISCALL);
		//engine->RegisterObjectMethod("jjPLAYER", "int set_lives(int)", asMETHOD(jjPLAYER, set_lives), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_lives() const", asMETHOD(jjPLAYER, get_lives), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_lives(int)", asMETHOD(jjPLAYER, set_lives), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "int get_invincibility() const", asMETHOD(jjPLAYER, get_invincibility), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_invincibility(int)", asMETHOD(jjPLAYER, set_invincibility), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_blink() const", asMETHOD(jjPLAYER, get_blink), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_blink(int)", asMETHOD(jjPLAYER, set_blink), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int extendInvincibility(int duration)", asMETHOD(jjPLAYER, extendInvincibility), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_food() const", asMETHOD(jjPLAYER, get_food), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_food(int)", asMETHOD(jjPLAYER, set_food), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_coins() const", asMETHOD(jjPLAYER, get_coins), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_coins(int)", asMETHOD(jjPLAYER, set_coins), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool testForCoins(int numberOfCoins)", asMETHOD(jjPLAYER, testForCoins), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_gems(GEM::Color) const", asMETHOD(jjPLAYER, get_gems), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_gems(GEM::Color, int)", asMETHOD(jjPLAYER, set_gems), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool testForGems(int numberOfGems, GEM::Color type)", asMETHOD(jjPLAYER, testForGems), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_shieldType() const", asMETHOD(jjPLAYER, get_shieldType), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_shieldType(int)", asMETHOD(jjPLAYER, set_shieldType), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_shieldTime() const", asMETHOD(jjPLAYER, get_shieldTime), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_shieldTime(int)", asMETHOD(jjPLAYER, set_shieldTime), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_ballTime() const", asMETHOD(jjPLAYER, get_rolling), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_ballTime(int)", asMETHOD(jjPLAYER, set_rolling), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPLAYER", "int boss", asOFFSET(jjPLAYER, bossNumber));
		engine->RegisterObjectProperty("jjPLAYER", "bool bossActivated", asOFFSET(jjPLAYER, bossActive));
		engine->RegisterObjectMethod("jjPLAYER", "int8 get_direction() const", asMETHOD(jjPLAYER, get_direction), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int8 set_direction(int8)", asMETHOD(jjPLAYER, set_direction), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPLAYER", "int platform", asOFFSET(jjPLAYER, platform));
		engine->RegisterObjectProperty("jjPLAYER", "const int flag", asOFFSET(jjPLAYER, flag));
		engine->RegisterObjectProperty("jjPLAYER", "const int clientID", asOFFSET(jjPLAYER, clientID));
		engine->RegisterObjectMethod("jjPLAYER", "int8 get_playerID() const", asMETHOD(jjPLAYER, get_playerID), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_localPlayerID() const", asMETHOD(jjPLAYER, get_localPlayerID), asCALL_THISCALL);
		/**
		 * @brief Whether the player isn't on the blue team
		 *
		 * Also true for the green and yellow teams, use team instead.
		 * @partialsupport Teams aren't tracked for scripts yet, so it's currently always false.
		 */
		engine->RegisterObjectProperty("jjPLAYER", "const bool teamRed", asOFFSET(jjPLAYER, team));
		engine->RegisterObjectMethod("jjPLAYER", "bool get_running() const", asMETHOD(jjPLAYER, get_running), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_running(bool)", asMETHOD(jjPLAYER, set_running), asCALL_THISCALL);
		/** @deprecated Use @ref jjPLAYER::doubleJumpCount instead, which also supports more jumps in the air. */
		engine->RegisterObjectMethod("jjPLAYER", "bool get_alreadyDoubleJumped() const", asMETHOD(jjPLAYER, get_alreadyDoubleJumped), asCALL_THISCALL); // Deprecated
		engine->RegisterObjectMethod("jjPLAYER", "bool set_alreadyDoubleJumped(bool)", asMETHOD(jjPLAYER, set_alreadyDoubleJumped), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_doubleJumpCount() const", asMETHOD(jjPLAYER, get_doubleJumpCount), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_doubleJumpCount(int)", asMETHOD(jjPLAYER, set_doubleJumpCount), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_stoned() const", asMETHOD(jjPLAYER, get_stoned), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_stoned(int)", asMETHOD(jjPLAYER, set_stoned), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPLAYER", "int buttstomp", asOFFSET(jjPLAYER, buttstomp));
		engine->RegisterObjectMethod("jjPLAYER", "int get_helicopter() const", asMETHOD(jjPLAYER, get_helicopter), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_helicopter(int)", asMETHOD(jjPLAYER, set_helicopter), asCALL_THISCALL);
		engine->RegisterObjectProperty("jjPLAYER", "int helicopterElapsed", asOFFSET(jjPLAYER, helicopterElapsed));
		engine->RegisterObjectProperty("jjPLAYER", "int specialMove", asOFFSET(jjPLAYER, specialMove));
		engine->RegisterObjectMethod("jjPLAYER", "int get_idle() const", asMETHOD(jjPLAYER, get_idle), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_idle(int)", asMETHOD(jjPLAYER, set_idle), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void suckerTube(int xSpeed, int ySpeed, bool center, bool noclip = false, bool trigSample = false)", asMETHOD(jjPLAYER, suckerTube), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void poleSpin(float xSpeed, float ySpeed, uint delay = 70)", asMETHOD(jjPLAYER, poleSpin), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample)", asMETHOD(jjPLAYER, spring), asCALL_THISCALL);

		engine->RegisterObjectProperty("jjPLAYER", "const bool isLocal", asOFFSET(jjPLAYER, isLocal));
		engine->RegisterObjectProperty("jjPLAYER", "const bool isActive", asOFFSET(jjPLAYER, isActive));
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isConnecting() const", asMETHOD(jjPLAYER, get_isConnecting), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isIdle() const", asMETHOD(jjPLAYER, get_isIdle), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isOut() const", asMETHOD(jjPLAYER, get_isOut), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isSpectating() const", asMETHOD(jjPLAYER, get_isSpectating), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isInGame() const", asMETHOD(jjPLAYER, get_isInGame), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "string get_name() const", asMETHOD(jjPLAYER, get_name), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "string get_nameUnformatted() const", asMETHOD(jjPLAYER, get_nameUnformatted), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool setName(const string &in name)", asMETHOD(jjPLAYER, setName), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int8 get_light() const", asMETHOD(jjPLAYER, get_light), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int8 set_light(int8)", asMETHOD(jjPLAYER, set_light), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint32 get_fur() const", asMETHOD(jjPLAYER, get_fur), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint32 set_fur(uint32)", asMETHOD(jjPLAYER, set_fur), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void furGet(uint8 &out a, uint8 &out b, uint8 &out c, uint8 &out d) const", asMETHOD(jjPLAYER, getFur), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void furSet(uint8 a, uint8 b, uint8 c, uint8 d)", asMETHOD(jjPLAYER, setFur), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "bool get_noFire() const", asMETHOD(jjPLAYER, get_noFire), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_noFire(bool)", asMETHOD(jjPLAYER, set_noFire), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_antiGrav() const", asMETHOD(jjPLAYER, get_antiGrav), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_antiGrav(bool)", asMETHOD(jjPLAYER, set_antiGrav), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_invisibility() const", asMETHOD(jjPLAYER, get_invisibility), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_invisibility(bool)", asMETHOD(jjPLAYER, set_invisibility), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_noclipMode() const", asMETHOD(jjPLAYER, get_noclipMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_noclipMode(bool)", asMETHOD(jjPLAYER, set_noclipMode), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 get_lighting() const", asMETHOD(jjPLAYER, get_lighting), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 set_lighting(uint8)", asMETHOD(jjPLAYER, set_lighting), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "uint8 resetLight()", asMETHOD(jjPLAYER, resetLight), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyLeft() const", asMETHOD(jjPLAYER, get_playerKeyLeftPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyRight() const", asMETHOD(jjPLAYER, get_playerKeyRightPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyUp() const", asMETHOD(jjPLAYER, get_playerKeyUpPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyDown() const", asMETHOD(jjPLAYER, get_playerKeyDownPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyFire() const", asMETHOD(jjPLAYER, get_playerKeyFirePressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keySelect() const", asMETHOD(jjPLAYER, get_playerKeySelectPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyJump() const", asMETHOD(jjPLAYER, get_playerKeyJumpPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_keyRun() const", asMETHOD(jjPLAYER, get_playerKeyRunPressed), asCALL_THISCALL);
		// The value a setter returns is the value of the assignment, so `a.keyLeft = a.keyRight = false` needs it
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyLeft(bool)", asMETHOD(jjPLAYER, set_playerKeyLeftPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyRight(bool)", asMETHOD(jjPLAYER, set_playerKeyRightPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyUp(bool)", asMETHOD(jjPLAYER, set_playerKeyUpPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyDown(bool)", asMETHOD(jjPLAYER, set_playerKeyDownPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyFire(bool)", asMETHOD(jjPLAYER, set_playerKeyFirePressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keySelect(bool)", asMETHOD(jjPLAYER, set_playerKeySelectPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyJump(bool)", asMETHOD(jjPLAYER, set_playerKeyJumpPressed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_keyRun(bool)", asMETHOD(jjPLAYER, set_playerKeyRunPressed), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "bool get_powerup(uint8) const", asMETHOD(jjPLAYER, get_powerup), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_powerup(uint8, bool)", asMETHOD(jjPLAYER, set_powerup), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_ammo(uint8) const", asMETHOD(jjPLAYER, get_ammo), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_ammo(uint8, int)", asMETHOD(jjPLAYER, set_ammo), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "bool offsetPosition(int xPixels, int yPixels)", asMETHOD(jjPLAYER, offsetPosition), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool warpToTile(int xTile, int yTile, bool fast = false)", asMETHOD(jjPLAYER, warpToTile), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool warpToID(uint8 warpID, bool fast = false)", asMETHOD(jjPLAYER, warpToID), asCALL_THISCALL);

		engine->SetDefaultNamespace("CHAR");
		/**
		 * @brief Character a player can be
		 *
		 * Each value is the index of the animation set the character uses.
		 */
		engine->RegisterEnum("Char");
		engine->RegisterEnumValue("Char", "JAZZ", Legacy::AnimationTables::GetSetID(mJAZZ, versionTSF));
		engine->RegisterEnumValue("Char", "SPAZ", Legacy::AnimationTables::GetSetID(mSPAZ, versionTSF));
		engine->RegisterEnumValue("Char", "LORI", Legacy::AnimationTables::GetSetID(mLORI, versionTSF));

		engine->RegisterEnumValue("Char", "BIRD", Legacy::AnimationTables::GetSetID(mBIRD, versionTSF));
		engine->RegisterEnumValue("Char", "BIRD2", Legacy::AnimationTables::GetSetID(mCHUCK, versionTSF));	///< Bird using the sprites of Chuck
		engine->RegisterEnumValue("Char", "FROG", Legacy::AnimationTables::GetSetID(mFROG, versionTSF));
		engine->SetDefaultNamespace("");

		engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char morph(bool rabbitsOnly = false, bool morphEffect = true)", asMETHOD(jjPLAYER, morph), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char morphTo(CHAR::Char charNew, bool morphEffect = true)", asMETHOD(jjPLAYER, morphTo), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char revertMorph(bool morphEffect = true)", asMETHOD(jjPLAYER, revertMorph), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char get_charCurr() const", asMETHOD(jjPLAYER, get_charCurr), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char get_charOrig() const", asMETHOD(jjPLAYER, get_charOrig), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "CHAR::Char set_charOrig(CHAR::Char)", asMETHOD(jjPLAYER, set_charOrig), asCALL_THISCALL);

		engine->SetDefaultNamespace("TEAM");
		/** @brief Team in team-based game modes */
		engine->RegisterEnum("Color");
		engine->RegisterEnumValue("Color", "NEUTRAL", -1);	///< No team
		engine->RegisterEnumValue("Color", "BLUE", 0);
		engine->RegisterEnumValue("Color", "RED", 1);
		engine->RegisterEnumValue("Color", "GREEN", 2);
		engine->RegisterEnumValue("Color", "YELLOW", 3);
		engine->SetDefaultNamespace("");

		engine->SetDefaultNamespace("CHAT");
		/**
		 * @brief Kind of a chat message
		 *
		 * @partialsupport Currently not used, chat hooks aren't called.
		 */
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "NORMAL", 0);	///< Message to everyone
		engine->RegisterEnumValue("Type", "TEAMCHAT", 1);	///< Message to the sender's team
		engine->RegisterEnumValue("Type", "WHISPER", 2);	///< Private message to one player
		engine->RegisterEnumValue("Type", "ME", 3);	///< Action message, as sent by the `/me` command
		engine->SetDefaultNamespace("");
		/**
		 * @brief Team the player belongs to
		 *
		 * A @ref TEAM::Color value. Teams aren't tracked for scripts yet, so it can't be relied on.
		 */
		engine->RegisterObjectProperty("jjPLAYER", "const TEAM::Color team", asOFFSET(jjPLAYER, team));

		engine->RegisterObjectMethod("jjPLAYER", "void kill()", asMETHOD(jjPLAYER, kill), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool hurt(int8 damage = 1, bool forceHurt = false, jjPLAYER@ attacker = null)", asMETHOD(jjPLAYER, hurt), asCALL_THISCALL);

		// TODO
		//engine->RegisterGlobalFunction("array<jjPLAYER@>@ jjPlayersWithClientID(int clientID)", asFUNCTION(getPlayersWithClientID), asCALL_CDECL);*/

		engine->SetDefaultNamespace("TIMER");
		/** @brief State of a player's timer */
		engine->RegisterEnum("State");
		engine->RegisterEnumValue("State", "STOPPED", 0);
		engine->RegisterEnumValue("State", "STARTED", 1);
		engine->RegisterEnumValue("State", "PAUSED", 2);
		engine->SetDefaultNamespace("STRING");
		/** @brief How drawn text is styled or animated */
		engine->RegisterEnum("Mode");
		engine->RegisterEnumValue("Mode", "NORMAL", 0);
		engine->RegisterEnumValue("Mode", "DARK", 1);
		engine->RegisterEnumValue("Mode", "RIGHTALIGN", 2);	///< Text ends at the given position instead of starting there
		engine->RegisterEnumValue("Mode", "BOUNCE", 3);
		engine->RegisterEnumValue("Mode", "SPIN", 4);
		engine->RegisterEnumValue("Mode", "PALSHIFT", 5);
		/** @brief Size of text */
		engine->RegisterEnum("Size");
		engine->RegisterEnumValue("Size", "SMALL", 1);
		engine->RegisterEnumValue("Size", "MEDIUM", 0);
		engine->RegisterEnumValue("Size", "LARGE", 2);
		engine->SetDefaultNamespace("");

		/**
		 * @brief Shows a message to the local player
		 *
		 * Small text is added to the messages at the bottom of the screen, other sizes are shown in big letters the
		 * way a Text event shows text.
		 * @param text Text of the message
		 * @param sendToAll Currently ignored
		 * @param size @ref STRING::Size of the text
		 * @partialsupport The `sendToAll` parameter is currently ignored.
		 */
		engine->RegisterGlobalFunction("void jjAlert(const ::string &in text, bool sendToAll = false, STRING::Size size = STRING::SMALL)", asFUNCTION(jjAlert), asCALL_CDECL);
		/**
		 * @brief Writes text to the log
		 *
		 * @param text Text to write
		 * @param timestamp Currently ignored
		 * @partialsupport The text isn't shown in the game. The `timestamp` parameter is currently ignored.
		 */
		engine->RegisterGlobalFunction("void jjPrint(const ::string &in text, bool timestamp = false)", asFUNCTION(jjPrint), asCALL_CDECL);
		/**
		 * @brief Writes debugging text to the log
		 *
		 * Written as a debug message, so it only appears when the log includes those.
		 * @param text Text to write
		 * @param timestamp Currently ignored
		 * @partialsupport The `timestamp` parameter is currently ignored.
		 */
		engine->RegisterGlobalFunction("void jjDebug(const ::string &in text, bool timestamp = false)", asFUNCTION(jjDebug), asCALL_CDECL);
		/**
		 * @brief Sends a line of chat
		 *
		 * Currently the text is only added to the messages at the bottom of the screen and to the log. Commands,
		 * text starting with `/`, are ignored.
		 * @param text Text of the message
		 * @param teamchat Currently ignored
		 */
		engine->RegisterGlobalFunction("void jjChat(const ::string &in text, bool teamchat = false)", asFUNCTION(jjChat), asCALL_CDECL);
		/**
		 * @brief Writes a console message
		 *
		 * The text is added to the messages at the bottom of the screen as an important message, and to the log.
		 * @param text Text of the message
		 * @param sendToAll Currently ignored
		 * @partialsupport The `sendToAll` parameter is currently ignored.
		 */
		engine->RegisterGlobalFunction("void jjConsole(const ::string &in text, bool sendToAll = false)", asFUNCTION(jjConsole), asCALL_CDECL);
		/**
		 * @brief Writes text to the log
		 *
		 * Written as a debug message, so it only appears when the log includes those. The text isn't shown in the
		 * game.
		 */
		engine->RegisterGlobalFunction("void jjSpy(const ::string &in text)", asFUNCTION(jjSpy), asCALL_CDECL);

		engine->RegisterObjectMethod("jjPLAYER", "TIMER::State get_timerState() const", asMETHOD(jjPLAYER, get_timerState), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_timerPersists() const", asMETHOD(jjPLAYER, get_timerPersists), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool set_timerPersists(bool)", asMETHOD(jjPLAYER, set_timerPersists), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "TIMER::State timerStart(int ticks, bool startPaused = false)", asMETHOD(jjPLAYER, timerStart), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "TIMER::State timerPause()", asMETHOD(jjPLAYER, timerPause), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "TIMER::State timerResume()", asMETHOD(jjPLAYER, timerResume), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "TIMER::State timerStop()", asMETHOD(jjPLAYER, timerStop), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_timerTime() const", asMETHOD(jjPLAYER, get_timerTime), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int set_timerTime(int)", asMETHOD(jjPLAYER, set_timerTime), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void timerFunction(const string functionName)", asMETHOD(jjPLAYER, timerFunction), asCALL_THISCALL);
		/** @brief Callback that takes no arguments */
		engine->RegisterFuncdef("void jjVOIDFUNC()");
		engine->RegisterObjectMethod("jjPLAYER", "void timerFunction(jjVOIDFUNC@ function)", asMETHOD(jjPLAYER, timerFunctionPtr), asCALL_THISCALL);
		/** @brief Callback that takes a player */
		engine->RegisterFuncdef("void jjVOIDFUNCPLAYER(jjPLAYER@)");
		engine->RegisterObjectMethod("jjPLAYER", "void timerFunction(jjVOIDFUNCPLAYER@ function)", asMETHOD(jjPLAYER, timerFunctionFuncPtr), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "bool activateBoss(bool activate = true)", asMETHOD(jjPLAYER, activateBoss), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool limitXScroll(uint16 left, uint16 width)", asMETHOD(jjPLAYER, limitXScroll), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraFreeze(float xPixel, float yPixel, bool centered, bool instant)", asMETHOD(jjPLAYER, cameraFreezeFF), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraFreeze(bool xUnfreeze, float yPixel, bool centered, bool instant)", asMETHOD(jjPLAYER, cameraFreezeBF), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraFreeze(float xPixel, bool yUnfreeze, bool centered, bool instant)", asMETHOD(jjPLAYER, cameraFreezeFB), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraFreeze(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant)", asMETHOD(jjPLAYER, cameraFreezeBB), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void cameraUnfreeze(bool instant = true)", asMETHOD(jjPLAYER, cameraUnfreeze), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void showText(string &in text, STRING::Size size = STRING::SMALL)", asMETHOD(jjPLAYER, showText), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "void showText(uint textID, uint offset, STRING::Size size = STRING::SMALL)", asMETHOD(jjPLAYER, showTextByID), asCALL_THISCALL);

		engine->SetDefaultNamespace("FLIGHT");
		/**
		 * @brief How a player is flying
		 *
		 * Used by @ref jjPLAYER::fly.
		 */
		engine->RegisterEnum("Mode");
		engine->RegisterEnumValue("Mode", "NONE", 0);	///< Not flying
		engine->RegisterEnumValue("Mode", "FLYCARROT", 1);	///< Flying with a copter, as from a fly carrot
		engine->RegisterEnumValue("Mode", "AIRBOARD", -1);	///< Riding an airboard
		engine->SetDefaultNamespace("");
		engine->RegisterObjectMethod("jjPLAYER", "FLIGHT::Mode get_fly() const", asMETHOD(jjPLAYER, get_fly), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "FLIGHT::Mode set_fly(FLIGHT::Mode)", asMETHOD(jjPLAYER, set_fly), asCALL_THISCALL);

		engine->SetDefaultNamespace("DIRECTION");
		/** @brief Direction to fire in */
		engine->RegisterEnum("Dir");
		engine->RegisterEnumValue("Dir", "RIGHT", dirRIGHT);
		engine->RegisterEnumValue("Dir", "LEFT", dirLEFT);
		engine->RegisterEnumValue("Dir", "UP", dirUP);
		engine->RegisterEnumValue("Dir", "CURRENT", dirCURRENT);	///< Where the player currently aims
		engine->SetDefaultNamespace("");

		engine->RegisterObjectMethod("jjPLAYER", "int fireBullet(uint8 gun = 0, bool depleteAmmo = true, bool requireAmmo = true, DIRECTION::Dir direction = DIRECTION::CURRENT)", asMETHOD(jjPLAYER, fireBulletDirection), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int fireBullet(uint8 gun, bool depleteAmmo, bool requireAmmo, float angle)", asMETHOD(jjPLAYER, fireBulletAngle), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "int get_subscreenX() const", asMETHOD(jjPLAYER, get_subscreenX), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_subscreenY() const", asMETHOD(jjPLAYER, get_subscreenY), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_cameraX() const", asMETHOD(jjPLAYER, get_cameraX), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "float get_cameraY() const", asMETHOD(jjPLAYER, get_cameraY), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "int get_deaths() const", asMETHOD(jjPLAYER, get_deaths), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isJailed() const", asMETHOD(jjPLAYER, get_isJailed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool get_isZombie() const", asMETHOD(jjPLAYER, get_isZombie), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_lrsLives() const", asMETHOD(jjPLAYER, get_lrsLives), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_roasts() const", asMETHOD(jjPLAYER, get_roasts), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_laps() const", asMETHOD(jjPLAYER, get_laps), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_lapTimeCurrent() const", asMETHOD(jjPLAYER, get_lapTimeCurrent), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_lapTimes(uint) const", asMETHOD(jjPLAYER, get_lapTimes), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "int get_lapTimeBest() const", asMETHOD(jjPLAYER, get_lapTimeBest), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjPLAYER", "bool get_isAdmin() const", asMETHOD(jjPLAYER, get_isAdmin), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYER", "bool hasPrivilege(const string &in privilege, uint moduleID = ::jjScriptModuleID) const", asMETHOD(jjPLAYER, hasPrivilege), asCALL_THISCALL);
	}

	void LevelScriptLoader::RegisterLegacyCharacters(asIScriptEngine* engine)
	{
		/**
		 * @brief Options of one weapon
		 *
		 * Reached through @ref jjWeapons, indexed by @ref WEAPON::Weapon. These are the options the game itself plays the level
		 * by, so changing them changes how the weapon behaves for every player.
		 */
		engine->RegisterObjectType("jjWEAPON", sizeof(jjWEAPON), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjWEAPON@ get_jjWeapons(int)", asFUNCTION(get_jjWEAPON), asCALL_CDECL);
		/**
		 * @brief Whether the ammo of the weapon never runs out
		 *
		 * The HUD shows such ammo as unlimited. True by default for the blaster only. Changing it converts the ammo
		 * every player has of the weapon accordingly.
		 */
		engine->RegisterObjectMethod("jjWEAPON", "bool get_infinite() const", asFUNCTION(get_jjWEAPON_infinite), asCALL_CDECL_OBJFIRST);
		engine->RegisterObjectMethod("jjWEAPON", "bool set_infinite(bool)", asFUNCTION(set_jjWEAPON_infinite), asCALL_CDECL_OBJFIRST);
		/**
		 * @brief Whether the ammo is refilled to the maximum when the level starts
		 *
		 * False by default.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "bool replenishes", asOFFSET(jjWEAPON, Replenishes));
		/**
		 * @brief Whether an active shield fires in place of the weapon
		 *
		 * True by default for the blaster only.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "bool replacedByShield", asOFFSET(jjWEAPON, ReplacedByShield));
		/**
		 * @brief Whether the weapon fires air bubbles under water
		 *
		 * False by default.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "bool replacedByBubbles", asOFFSET(jjWEAPON, ReplacedByBubbles));
		/**
		 * @brief Whether gun crates and gun barrels can drop ammo of the weapon
		 *
		 * True by default for every weapon but the blaster.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "bool comesFromGunCrates", asOFFSET(jjWEAPON, ComesFromGunCrates));
		/**
		 * @brief Whether bullets turn towards where the player aims over a few shots
		 *
		 * Kept for scripts, the game's own weapons don't use it.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "bool gradualAim", asOFFSET(jjWEAPON, GradualAim));
		/**
		 * @brief How many times as much ammo pickups give
		 *
		 * The ammo shown on the HUD is divided by it. 1 by default.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "int multiplier", asOFFSET(jjWEAPON, Multiplier));
		/**
		 * @brief Most ammo of the weapon a player can carry
		 *
		 * 0 or less means the default of 99, higher values are capped at 255. -1 by default.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "int maximum", asOFFSET(jjWEAPON, Maximum));
		/**
		 * @brief Gems a player hit by the weapon loses in Treasure Hunt
		 *
		 * 3 by default.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "int gemsLost", asOFFSET(jjWEAPON, GemsLost));
		/**
		 * @brief Gems a player hit by the powered-up weapon loses in Treasure Hunt
		 *
		 * 3 by default.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "int gemsLostPowerup", asOFFSET(jjWEAPON, GemsLostPowerup));
		/**
		 * @brief How the weapon fires while the fire key is held
		 *
		 * A @ref WEAPON::Style value.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "int8 style", asOFFSET(jjWEAPON, Style));
		/**
		 * @brief How many bullets the weapon fires at once
		 *
		 * @partialsupport Currently only used by weapons whose bullets a script behavior controls.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "SPREAD::Spread spread", asOFFSET(jjWEAPON, Spread));
		/** @brief Whether the sound of firing the weapon is played */
		engine->RegisterObjectProperty("jjWEAPON", "bool defaultSample", asOFFSET(jjWEAPON, DefaultSample));
		/**
		 * @brief Whether the weapon can be used in the level
		 *
		 * When false, players can't fire it and lose their ammo of it when the level starts. True by default.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "bool allowed", asOFFSET(jjWEAPON, Allowed));
		/**
		 * @brief Whether the weapon can be powered up in the level
		 *
		 * When false, players don't get or keep the powerup of it. True by default.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "bool allowedPowerup", asOFFSET(jjWEAPON, AllowedPowerup));
		/**
		 * @brief Whether a companion bird fires the weapon
		 *
		 * Otherwise the bird fires blaster bullets, whatever weapon the player has selected.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "bool comesFromBirds", asOFFSET(jjWEAPON, ComesFromBirds));
		/**
		 * @brief Whether a companion bird fires the weapon powered up
		 *
		 * Only when comesFromBirds is true and the player has the powerup of the weapon.
		 */
		engine->RegisterObjectProperty("jjWEAPON", "bool comesFromBirdsPowerup", asOFFSET(jjWEAPON, ComesFromBirdsPowerup));

		engine->SetDefaultNamespace("AIR");
		/**
		 * @brief What a character does when jump is pressed in the air
		 *
		 * Used by @ref jjCHARACTER::airJump.
		 */
		engine->RegisterEnum("Jump");
		engine->RegisterEnumValue("Jump", "NONE", airjumpNONE);	///< Nothing
		engine->RegisterEnumValue("Jump", "HELICOPTER", airjumpHELICOPTER);	///< Helicopter ears
		engine->RegisterEnumValue("Jump", "DOUBLEJUMP", airjumpSPAZ);	///< Another jump in the air
		engine->SetDefaultNamespace("GROUND");
		/**
		 * @brief What a character does when jump is pressed while crouching
		 *
		 * Used by @ref jjCHARACTER::groundJump.
		 */
		engine->RegisterEnum("Jump");
		engine->RegisterEnumValue("Jump", "CROUCH", groundjumpNONE);	///< Keeps crouching without jumping
		engine->RegisterEnumValue("Jump", "JUMP", groundjumpREGULARJUMP);	///< A plain jump that doesn't turn into a buttstomp
		engine->RegisterEnumValue("Jump", "JAZZ", groundjumpJAZZ);	///< The move of Jazz
		engine->RegisterEnumValue("Jump", "SPAZ", groundjumpSPAZ);	///< The move of Spaz
		engine->RegisterEnumValue("Jump", "LORI", groundjumpLORI);	///< The move of Lori
		engine->SetDefaultNamespace("");
		engine->RegisterObjectType("jjCHARACTER", sizeof(jjCHARACTER), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterGlobalFunction("jjCHARACTER@ get_jjCharacters(CHAR::Char)", asFUNCTION(get_jjCHARACTER), asCALL_CDECL);
		engine->RegisterObjectProperty("jjCHARACTER", "AIR::Jump airJump", asOFFSET(jjCHARACTER, airJump));
		engine->RegisterObjectProperty("jjCHARACTER", "GROUND::Jump groundJump", asOFFSET(jjCHARACTER, groundJump));
		engine->RegisterObjectProperty("jjCHARACTER", "int doubleJumpCountMax", asOFFSET(jjCHARACTER, doubleJumpCountMax));
		engine->RegisterObjectProperty("jjCHARACTER", "float doubleJumpXSpeed", asOFFSET(jjCHARACTER, doubleJumpXSpeed));
		engine->RegisterObjectProperty("jjCHARACTER", "float doubleJumpYSpeed", asOFFSET(jjCHARACTER, doubleJumpYSpeed));
		engine->RegisterObjectProperty("jjCHARACTER", "int helicopterDurationMax", asOFFSET(jjCHARACTER, helicopterDurationMax));
		engine->RegisterObjectProperty("jjCHARACTER", "float helicopterXSpeed", asOFFSET(jjCHARACTER, helicopterXSpeed));
		engine->RegisterObjectProperty("jjCHARACTER", "float helicopterYSpeed", asOFFSET(jjCHARACTER, helicopterYSpeed));
		engine->RegisterObjectProperty("jjCHARACTER", "bool canHurt", asOFFSET(jjCHARACTER, canHurt));
		engine->RegisterObjectProperty("jjCHARACTER", "bool canRun", asOFFSET(jjCHARACTER, canRun));
		engine->RegisterObjectProperty("jjCHARACTER", "bool morphBoxCycle", asOFFSET(jjCHARACTER, morphBoxCycle));
	}

	void LevelScriptLoader::RegisterLegacyPlayerDraw(asIScriptEngine* engine)
	{
		// TODO
		engine->RegisterObjectType("jjPLAYERDRAW", sizeof(jjPLAYERDRAW), asOBJ_REF | asOBJ_NOCOUNT);
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool name", asOFFSET(jjPLAYERDRAW, name));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool sprite", asOFFSET(jjPLAYERDRAW, sprite));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool sugarRush", asOFFSET(jjPLAYERDRAW, sugarRush));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool gunFlash", asOFFSET(jjPLAYERDRAW, gunFlash));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool invincibility", asOFFSET(jjPLAYERDRAW,invincibility));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool trail", asOFFSET(jjPLAYERDRAW, trail));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool morphingExplosions", asOFFSET(jjPLAYERDRAW, morphingExplosions));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool airboardBouncingMotion", asOFFSET(jjPLAYERDRAW, airboardBouncingMotion));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "bool airboardPuff", asOFFSET(jjPLAYERDRAW, airboardPuff));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "SPRITE::Mode spriteMode", asOFFSET(jjPLAYERDRAW, spriteMode));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "uint8 spriteParam", asOFFSET(jjPLAYERDRAW, spriteParam));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "LIGHT::Type lightType", asOFFSET(jjPLAYERDRAW, lightType));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "int8 light", asOFFSET(jjPLAYERDRAW, light));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "int layer", asOFFSET(jjPLAYERDRAW, layer));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "uint curFrame", asOFFSET(jjPLAYERDRAW, curFrame));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "int angle", asOFFSET(jjPLAYERDRAW, angle));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "float xOffset", asOFFSET(jjPLAYERDRAW, xOffset));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "float yOffset", asOFFSET(jjPLAYERDRAW, yOffset));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "float xScale", asOFFSET(jjPLAYERDRAW, xScale));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "float yScale", asOFFSET(jjPLAYERDRAW, yScale));
		engine->RegisterObjectProperty("jjPLAYERDRAW", "TEAM::Color flag", asOFFSET(jjPLAYERDRAW, flag));
		engine->RegisterObjectMethod("jjPLAYERDRAW", "bool get_shield(SHIELD::Shield) const", asMETHOD(jjPLAYERDRAW, get_shield), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYERDRAW", "bool set_shield(SHIELD::Shield, bool)", asMETHOD(jjPLAYERDRAW, set_shield), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjPLAYERDRAW", "jjPLAYER@ get_player() const", asMETHOD(jjPLAYERDRAW, get_player), asCALL_THISCALL);
	}
}

#endif
