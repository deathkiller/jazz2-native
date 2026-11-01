#if defined(WITH_ANGELSCRIPT)

#include "Game.h"
#include "Internal.h"

#include "../../Actors/Player.h"
#include "../../Compatibility/JJ2Strings.h"
#include "../../UI/HUD.h"
#include "../../UI/InGameConsole.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Input/IInputManager.h"
#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>
#include <IO/FileSystem.h>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	std::int32_t LevelScriptLoader::GetDifficulty() {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		switch (_this->_levelHandler->_difficulty) {
			case GameDifficulty::Easy: return 0;
			default:
			case GameDifficulty::Normal: return 1;
			case GameDifficulty::Hard: return 2;
		}
	}
	std::int32_t LevelScriptLoader::SetDifficulty(std::int32_t value) {

		if (value >= 0 && value <= 2) {
			// As GetDifficulty() gives it, the difficulties of the game start after the default one
			auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			_this->_levelHandler->_difficulty = (GameDifficulty)(std::int32_t(GameDifficulty::Easy) + value);
		}
		return value;
	}

	String getLevelFileName() {
		// The name of the file of the level as JJ2+ has it, which scripts take layers of (jjLayersFromLevel()), not the
		// episode and the level the game knows it by
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return String(fs::GetFileName(owner->GetLevelHandler()->GetLevelName())) + ".j2l"_s;
	}
	String getCurrLevelName() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetLevelHandler()->GetLevelDisplayName();
	}
	void setCurrLevelName(const String& in) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		owner->GetLevelHandler()->SetLevelDisplayName(in);
	}
	String LevelScriptLoader::get_jjMusicFileName() {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_musicCurrentPath;
	}
	String get_jjTilesetFileName() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = owner->GetLevelHandler()->TileMap();
		if (tileMap == nullptr) {
			return "";
		}
		auto paths = tileMap->GetUsedTileSetPaths();
		return (paths.empty() ? String{} : String(paths[0]));
	}

	String LevelScriptLoader::get_jjHelpStrings(std::uint32_t index) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->GetScriptText(index);
	}
	void LevelScriptLoader::set_jjHelpStrings(std::uint32_t index, const String& text) {

		// Read back as it is, shown in the formatting of the original game as the texts of the level
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->OverrideScriptText(index, text);
		_this->_levelHandler->OverrideLevelText(index, Compatibility::JJ2Strings::RecodeString(text));
	}

	std::int32_t get_gameState() {
		noop(); return 0;
	}

	// TODO

	void LevelScriptLoader::jjAlert(const String& text, bool sendToAll, std::uint32_t size)
	{
		// In the colors of a message (see Compatibility::JJ2Strings::RecodeScriptString()), a small one is written to the
		// messages at the bottom of the screen as in JJ2+, the others are shown big
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		String recodedText = Compatibility::JJ2Strings::RecodeScriptString(text, true);
		constexpr std::uint32_t SizeSmall = 1;	// STRING::SMALL
		if (size == SizeSmall) {
			_this->_levelHandler->_console->WriteLine(UI::MessageLevel::Info, recodedText);
		} else {
			_this->ShowLevelText(recodedText, size);
		}
	}
	void LevelScriptLoader::jjPrint(const String& text, bool timestamp) {
		LOGW("{}", text);
	}
	void LevelScriptLoader::jjDebug(const String& text, bool timestamp) {
		LOGD("{}", text);
	}
	void LevelScriptLoader::jjChat(const String& text, bool teamchat) {
		noop();
		LOGW("{}", text);

		if (text.hasPrefix('/')) {
			// TODO: Process command
			return;
		}

		// TODO: teamchat
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->_console->WriteLine(UI::MessageLevel::Info, Compatibility::JJ2Strings::RecodeScriptString(text, true));
	}
	void LevelScriptLoader::jjConsole(const String& text, bool sendToAll) {
		noop();
		LOGW("{}", text);

		// TODO: sendToAll
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->_console->WriteLine(UI::MessageLevel::Important, Compatibility::JJ2Strings::RecodeScriptString(text, true));
	}
	void LevelScriptLoader::jjSpy(const String& text) {
		LOGD("{}", text);
	}

	// TODO

	std::int32_t getBorderWidth() {
		noop(); return 0;
	}
	std::int32_t getBorderHeight() {
		noop(); return 0;
	}
	bool getSplitscreenType() {
		return ScriptLoader::FromActiveContext<LevelScriptLoader>()->_verticalSplitscreen;
	}
	bool setSplitscreenType(bool value) {
		// Kept for the scripts, the split screen is laid out as the player prefers it
		ScriptLoader::FromActiveContext<LevelScriptLoader>()->_verticalSplitscreen = value;
		return value;
	}

	// TODO

	std::int32_t get_teamScore(std::int32_t color) {
		noop(); return 0;
	}
	std::int32_t GetMaxHealth() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto players = owner->GetPlayers();
		return (players.size() == 0 ? 5 : players[0]->GetMaxHealth());
	}
	std::int32_t GetStartHealth() {
		// Players spawn at full health, so the starting health matches the maximum health
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto players = owner->GetPlayers();
		return (players.size() == 0 ? 5 : players[0]->GetMaxHealth());
	}

	bool getEnabledTeam(std::uint8_t team) {
		noop();
		return false;
	}

	// The key of the game a virtual-key code of Windows is, as `jjKey` takes them, `Keys::Unknown` for none
	static Keys VirtualKeyToKey(std::uint8_t key) {
		if (key >= 0x30 && key <= 0x39) {
			return Keys(std::int32_t(Keys::D0) + (key - 0x30));
		}
		if (key >= 0x41 && key <= 0x5A) {
			return Keys(std::int32_t(Keys::A) + (key - 0x41));
		}
		if (key >= 0x60 && key <= 0x69) {
			return Keys(std::int32_t(Keys::NumPad0) + (key - 0x60));
		}
		if (key >= 0x70 && key <= 0x7E) {
			return Keys(std::int32_t(Keys::F1) + (key - 0x70));
		}
		switch (key) {
			case 0x08: return Keys::Backspace;
			case 0x09: return Keys::Tab;
			case 0x0D: return Keys::Return;
			case 0x13: return Keys::Pause;
			case 0x14: return Keys::CapsLock;
			case 0x1B: return Keys::Escape;
			case 0x20: return Keys::Space;
			case 0x21: return Keys::PageUp;
			case 0x22: return Keys::PageDown;
			case 0x23: return Keys::End;
			case 0x24: return Keys::Home;
			case 0x25: return Keys::Left;
			case 0x26: return Keys::Up;
			case 0x27: return Keys::Right;
			case 0x28: return Keys::Down;
			case 0x2C: return Keys::PrintScreen;
			case 0x2D: return Keys::Insert;
			case 0x2E: return Keys::Delete;
			case 0x5B: return Keys::LSuper;
			case 0x5C: return Keys::RSuper;
			case 0x5D: return Keys::Menu;
			case 0x6A: return Keys::NumPadMultiply;
			case 0x6B: return Keys::NumPadPlus;
			case 0x6D: return Keys::NumPadMinus;
			case 0x6E: return Keys::NumPadPeriod;
			case 0x6F: return Keys::NumPadDivide;
			case 0x90: return Keys::NumLock;
			case 0x91: return Keys::ScrollLock;
			case 0xA0: return Keys::LShift;
			case 0xA1: return Keys::RShift;
			case 0xA2: return Keys::LCtrl;
			case 0xA3: return Keys::RCtrl;
			case 0xA4: return Keys::LAlt;
			case 0xA5: return Keys::RAlt;
			case 0xBA: return Keys::Semicolon;
			case 0xBB: return Keys::Plus;
			case 0xBC: return Keys::Comma;
			case 0xBD: return Keys::Minus;
			case 0xBE: return Keys::Period;
			case 0xBF: return Keys::Slash;
			case 0xC0: return Keys::Backquote;
			case 0xDB: return Keys::LeftBracket;
			case 0xDC: return Keys::Backslash;
			case 0xDD: return Keys::RightBracket;
			case 0xDE: return Keys::Quote;
			default: return Keys::Unknown;
		}
	}

	bool getKeyDown(std::uint8_t key) {
		// Virtual-key codes of Windows, as JJ2+ has them - mouse buttons, either of two sides of a modifier, or a key
		auto& input = theApplication().GetInputManager();
		switch (key) {
			case 0x01: return input.mouseState().isButtonDown(MouseButton::Left);
			case 0x02: return input.mouseState().isButtonDown(MouseButton::Right);
			case 0x04: return input.mouseState().isButtonDown(MouseButton::Middle);
			case 0x05: return input.mouseState().isButtonDown(MouseButton::Fourth);
			case 0x06: return input.mouseState().isButtonDown(MouseButton::Fifth);
			case 0x10: return input.keyboardState().isKeyDown(Keys::LShift) || input.keyboardState().isKeyDown(Keys::RShift);
			case 0x11: return input.keyboardState().isKeyDown(Keys::LCtrl) || input.keyboardState().isKeyDown(Keys::RCtrl);
			case 0x12: return input.keyboardState().isKeyDown(Keys::LAlt) || input.keyboardState().isKeyDown(Keys::RAlt);
		}
		Keys mapped = VirtualKeyToKey(key);
		return (mapped != Keys::Unknown && input.keyboardState().isKeyDown(mapped));
	}

	// The position of the mouse in the view of the game, which the window shows scaled
	static Vector2i GetCursorInView() {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto& mouse = theApplication().GetInputManager().mouseState();
		Vector2i windowSize = theApplication().GetResolution();
		Vector2i viewSize = owner->GetLevelHandler()->GetViewSize();
		if (windowSize.X <= 0 || windowSize.Y <= 0 || viewSize.X <= 0 || viewSize.Y <= 0) {
			return Vector2i(mouse.x, mouse.y);
		}
		return Vector2i(mouse.x * viewSize.X / windowSize.X, mouse.y * viewSize.Y / windowSize.Y);
	}

	std::int32_t getCursorX() {
		return GetCursorInView().X;
	}
	std::int32_t getCursorY() {
		return GetCursorInView().Y;
	}

	void LevelScriptLoader::ShowLevelText(StringView text, std::uint32_t size)
	{
		// STRING::Size of JJ2+
		constexpr std::uint32_t SizeSmall = 1;
		constexpr std::uint32_t SizeLarge = 2;

		_levelHandler->ShowLevelText(text);
		if (_levelHandler->_hud != nullptr) {
			_levelHandler->_hud->SetLevelTextScale(size == SizeSmall ? 0.8f : (size == SizeLarge ? 1.25f : 1.0f));
		}
	}

	void LevelScriptLoader::RegisterLegacyGame(asIScriptEngine* engine)
	{
		engine->SetDefaultNamespace("");
		/**
		 * @brief Returns the sine of an angle from a 1024-step table
		 *
		 * A full turn is 1024 units and larger values wrap around, so any `uint` angle is valid. The game uses the same
		 * table for rotating platforms and similar motion; the native @ref sin() may be more convenient elsewhere.
		 */
		engine->RegisterGlobalFunction("float jjSin(uint angle)", asFUNCTION(get_sinTable), asCALL_CDECL);
		/**
		 * @brief Returns the cosine of an angle from a 1024-step table
		 *
		 * A full turn is 1024 units and larger values wrap around. See also @ref jjSin().
		 */
		engine->RegisterGlobalFunction("float jjCos(uint angle)", asFUNCTION(get_cosTable), asCALL_CDECL);
		/** @brief Returns a random 32-bit number */
		engine->RegisterGlobalFunction("uint jjRandom()", asFUNCTION(RandWord32), asCALL_CDECL);
		/** @brief Returns the current UTC time as whole seconds since 1 January 1970 */
		engine->RegisterGlobalFunction("uint64 jjUnixTimeSec()", asFUNCTION(unixTimeSec), asCALL_CDECL);
		/** @brief Returns the current UTC time as milliseconds since 1 January 1970 */
		engine->RegisterGlobalFunction("uint64 jjUnixTimeMs()", asFUNCTION(unixTimeMs), asCALL_CDECL);

		//engine->RegisterGlobalFunction("bool jjIsValidCheat(const string &in text)", asFUNCTION(stringIsCheat), asCALL_CDECL);
		//engine->RegisterGlobalProperty("const bool jjDebugF10", &F10Debug);

		/**
		 * @brief Checks whether a string is a valid regular expression
		 *
		 * Expressions follow the ECMAScript grammar. Unlike the other regex functions, an invalid expression is not
		 * reported as an error here, so call this first whenever the expression comes from user input.
		 */
		engine->RegisterGlobalFunction("bool jjRegexIsValid(const string &in expression)", asFUNCTION(jjRegexIsValid), asCALL_CDECL);
		/**
		 * @brief Checks whether a regular expression matches the whole text
		 *
		 * A match of only part of the text is not enough, use @ref jjRegexSearch() for that. If `ignoreCase` is `true`,
		 * letter case is ignored. An invalid expression logs an error and returns `false`.
		 */
		engine->RegisterGlobalFunction("bool jjRegexMatch(const string &in text, const string &in expression, bool ignoreCase = false)", asFUNCTION(jjRegexMatch), asCALL_CDECL);
		/**
		 * @brief Checks whether a regular expression matches the whole text, with captured groups
		 *
		 * @note `results` is currently left unchanged, only the return value is set.
		 */
		engine->RegisterGlobalFunction("bool jjRegexMatch(const string &in text, const string &in expression, array<string> &out results, bool ignoreCase = false)", asFUNCTION(jjRegexMatchWithResults), asCALL_CDECL);
		/**
		 * @brief Checks whether a regular expression matches any part of the text
		 *
		 * If `ignoreCase` is `true`, letter case is ignored. An invalid expression logs an error and returns `false`.
		 */
		engine->RegisterGlobalFunction("bool jjRegexSearch(const string &in text, const string &in expression, bool ignoreCase = false)", asFUNCTION(jjRegexSearch), asCALL_CDECL);
		/**
		 * @brief Checks whether a regular expression matches any part of the text, with captured groups
		 *
		 * @note `results` is currently left unchanged, only the return value is set.
		 */
		engine->RegisterGlobalFunction("bool jjRegexSearch(const string &in text, const string &in expression, array<string> &out results, bool ignoreCase = false)", asFUNCTION(jjRegexSearchWithResults), asCALL_CDECL);
		/**
		 * @brief Replaces every match of a regular expression in the text
		 *
		 * The replacement may refer to captured groups as `$1`, `$2` and so on, `$0` being the whole match. If
		 * `ignoreCase` is `true`, letter case is ignored. An invalid expression logs an error and returns the text
		 * unchanged.
		 */
		engine->RegisterGlobalFunction("string jjRegexReplace(const string &in text, const string &in expression, const string &in replacement, bool ignoreCase= false)", asFUNCTION(jjRegexReplace), asCALL_CDECL);

		/**
		 * @brief Whether picking up ammo of a new weapon switches to it automatically
		 *
		 * Always `true`.
		 */
		engine->RegisterGlobalProperty("const bool jjAutoWeaponChange", &jjAutoWeaponChange);
		/**
		 * @brief Identifier of the script module that reads it
		 *
		 * Meant to be passed to @ref jjSendPacket() and @ref jjPLAYER::hasPrivilege(). Always 0, there is only the level
		 * script.
		 */
		engine->RegisterGlobalProperty("const int jjScriptModuleID", &jjScriptModuleID);
		/**
		 * @brief Number of game ticks elapsed in the level
		 *
		 * The game runs at 70 ticks per second. The counter never goes back, not even when a player dies and the level
		 * returns to a checkpoint.
		 */
		engine->RegisterGlobalProperty("const int jjGameTicks", &_onLevelUpdateLastFrame);
		/**
		 * @brief Number of game ticks elapsed while the game was actually running
		 *
		 * Meant to exclude time spent paused or waiting for a multiplayer game to start.
		 * @partialsupport Currently always 0.
		 */
		engine->RegisterGlobalProperty("const uint jjActiveGameTicks", &gameTicksSpentWhileActive);
		/**
		 * @brief Frame counter used to time drawing and automatic layer movement
		 *
		 * Follows the same 70-per-second clock as @ref jjGameTicks.
		 */
		engine->RegisterGlobalProperty("const int jjRenderFrame", &renderFrame);
		/** @brief Average number of frames drawn per second */
		engine->RegisterGlobalFunction("int get_jjFPS()", asFUNCTION(GetFPS), asCALL_CDECL);
		/**
		 * @brief Whether content of the later game version with Lori is available
		 *
		 * `true` if Lori's sprites are installed. It also decides how @ref ANIM::Set values are numbered.
		 */
		engine->RegisterGlobalProperty("const bool jjIsTSF", &versionTSF);
		/**
		 * @brief Whether the local player is logged in as an administrator of the server
		 *
		 * Always `false`.
		 */
		engine->RegisterGlobalFunction("bool get_jjIsAdmin()", asFUNCTION(isAdmin), asCALL_CDECL);
		/**
		 * @brief Whether this instance of the game hosts a multiplayer server
		 *
		 * Always `false`.
		 */
		engine->RegisterGlobalProperty("const bool jjIsServer", &isServer);
		/**
		 * @brief Current difficulty
		 *
		 * 0 for Easy, 1 for Normal and 2 for Hard.
		 * @partialsupport Assigning a value outside this range is ignored, and assigning doesn't change
		 * @ref jjDifficultyNext.
		 */
		engine->RegisterGlobalFunction("int get_jjDifficulty()", asFUNCTION(GetDifficulty), asCALL_CDECL);
		/** @brief Sets the current difficulty */
		engine->RegisterGlobalFunction("int set_jjDifficulty(int)", asFUNCTION(SetDifficulty), asCALL_CDECL);
		/**
		 * @brief Difficulty to use for the next level
		 *
		 * @partialsupport Currently has no effect, the next level keeps the current difficulty.
		 */
		engine->RegisterGlobalProperty("int jjDifficultyNext", &DifficultyForNextLevel);
		/**
		 * @brief Difficulty the level was started with
		 *
		 * Unlike @ref jjDifficulty, it doesn't change when a script assigns a new difficulty.
		 */
		engine->RegisterGlobalProperty("const int jjDifficultyOrig", &DifficultyAtLevelStart);

		engine->RegisterGlobalFunction("string get_jjLevelFileName()", asFUNCTION(getLevelFileName), asCALL_CDECL);
		engine->RegisterGlobalFunction("string get_jjLevelName()", asFUNCTION(getCurrLevelName), asCALL_CDECL);
		engine->RegisterGlobalFunction("void set_jjLevelName(const string &in)", asFUNCTION(setCurrLevelName), asCALL_CDECL);
		/**
		 * @brief File name of the music currently playing
		 *
		 * To play a different track, use @ref jjMusicLoad().
		 */
		engine->RegisterGlobalFunction("string get_jjMusicFileName()", asFUNCTION(get_jjMusicFileName), asCALL_CDECL);
		engine->RegisterGlobalFunction("string get_jjTilesetFileName()", asFUNCTION(get_jjTilesetFileName), asCALL_CDECL);
		/**
		 * @brief Number of static tiles currently available in the level
		 *
		 * Doesn't count animated or flipped tiles. Grows when @ref jjTilesFromTileset() adds tiles.
		 */
		engine->RegisterGlobalProperty("const uint jjTileCount", &numberOfTiles);

		/**
		 * @brief Text strings of the level, indexed from 0
		 *
		 * Used by Text events and by bosses that display a message. Scripts may replace them; a new string is shown
		 * with the same formatting as the texts stored in the level.
		 */
		engine->RegisterGlobalFunction("string get_jjHelpStrings(uint)", asFUNCTION(get_jjHelpStrings), asCALL_CDECL);
		/** @brief Sets a text string of the level */
		engine->RegisterGlobalFunction("void set_jjHelpStrings(uint, const string &in)", asFUNCTION(set_jjHelpStrings), asCALL_CDECL);

		engine->SetDefaultNamespace("GAME");
		/** @brief State of a multiplayer game */
		engine->RegisterEnum("State");
		engine->RegisterEnumValue("State", "STOPPED", gameSTOPPED);		///< Game is stopped
		engine->RegisterEnumValue("State", "STARTED", gameSTARTED);		///< Game is running
		engine->RegisterEnumValue("State", "PAUSED", gamePAUSED);		///< Game is paused, only possible with a time limit
		engine->RegisterEnumValue("State", "PREGAME", gamePREGAME);		///< Waiting for the game to start
		engine->RegisterEnumValue("State", "OVERTIME", gameOVERTIME);	///< Time limit ran out with no winner yet
		/** @brief Base game mode, regardless of any custom mode built on top of it */
		engine->RegisterEnum("Mode");
		engine->RegisterEnumValue("Mode", "SP", GM_SP);					///< Single player
		engine->RegisterEnumValue("Mode", "COOP", GM_COOP);				///< Cooperative
		engine->RegisterEnumValue("Mode", "BATTLE", GM_BATTLE);			///< Battle
		engine->RegisterEnumValue("Mode", "CTF", GM_CTF);				///< Capture the flag
		engine->RegisterEnumValue("Mode", "TREASURE", GM_TREASURE);		///< Treasure hunt
		engine->RegisterEnumValue("Mode", "RACE", GM_RACE);				///< Race
		/** @brief Custom game mode built on top of a base mode */
		engine->RegisterEnum("Custom");
		engine->RegisterEnumValue("Custom", "NOCUSTOM", 0);				///< No custom mode, only the base mode applies
		engine->RegisterEnumValue("Custom", "RT", 1);					///< Roast tag
		engine->RegisterEnumValue("Custom", "LRS", 2);					///< Last rabbit standing
		engine->RegisterEnumValue("Custom", "XLRS", 3);					///< Extended last rabbit standing
		engine->RegisterEnumValue("Custom", "PEST", 4);					///< Pestilence
		engine->RegisterEnumValue("Custom", "TB", 11);					///< Team battle
		engine->RegisterEnumValue("Custom", "JB", 12);					///< Jailbreak
		engine->RegisterEnumValue("Custom", "DCTF", 13);				///< Death capture the flag
		engine->RegisterEnumValue("Custom", "FR", 14);					///< Flag run
		engine->RegisterEnumValue("Custom", "TLRS", 15);				///< Team last rabbit standing
		engine->RegisterEnumValue("Custom", "DOM", 16);					///< Domination
		engine->RegisterEnumValue("Custom", "HEAD", 17);				///< Head hunters
		/** @brief Who can join the game */
		engine->RegisterEnum("Connection");
		engine->RegisterEnumValue("Connection", "LOCAL", gameLOCAL);		///< Local game only
		engine->RegisterEnumValue("Connection", "ONLINE", gameINTERNET);	///< Joinable over the internet
		engine->RegisterEnumValue("Connection", "LAN", gameLAN_TCP);		///< Joinable over the local network
		engine->SetDefaultNamespace("");
		engine->RegisterGlobalFunction("GAME::State get_jjGameState()", asFUNCTION(get_gameState), asCALL_CDECL);
		/**
		 * @brief Base mode of the current game
		 *
		 * Doesn't reflect a custom mode, see @ref jjGameCustom for that. Always @ref GAME::SP.
		 */
		engine->RegisterGlobalProperty("const GAME::Mode jjGameMode", &gameMode);
		/**
		 * @brief Custom mode of the current game, if any
		 *
		 * Always @ref GAME::NOCUSTOM.
		 */
		engine->RegisterGlobalProperty("const GAME::Custom jjGameCustom", &customMode);
		/**
		 * @brief Whether players from other computers can join, and over which network
		 *
		 * Always @ref GAME::LOCAL.
		 */
		engine->RegisterGlobalProperty("const GAME::Connection jjGameConnection", &partyMode);
	}

	void LevelScriptLoader::RegisterLegacyScreen(asIScriptEngine* engine)
	{
		/**
		 * @brief Whether the low detail video option is enabled
		 *
		 * Always `false`.
		 */
		engine->RegisterGlobalProperty("const bool jjLowDetail", &parLowDetail);
		/**
		 * @brief Color depth of the display in bits per pixel
		 *
		 * Always 16, so scripts use their full-color code paths.
		 */
		engine->RegisterGlobalProperty("const int jjColorDepth", &colorDepth);
		/**
		 * @brief Largest width a subscreen may have in the current level
		 *
		 * Currently the same as @ref jjSubscreenWidth.
		 */
		engine->RegisterGlobalProperty("const int jjResolutionMaxWidth", &checkedMaxSubVideoWidth);
		/**
		 * @brief Largest height a subscreen may have in the current level
		 *
		 * Currently the same as @ref jjSubscreenHeight.
		 */
		engine->RegisterGlobalProperty("const int jjResolutionMaxHeight", &checkedMaxSubVideoHeight);
		/**
		 * @brief Width of the game view in pixels
		 *
		 * The size the game renders at, which may differ from the size of the window.
		 */
		engine->RegisterGlobalProperty("const int jjResolutionWidth", &realVideoW);
		/**
		 * @brief Height of the game view in pixels
		 *
		 * The size the game renders at, which may differ from the size of the window.
		 */
		engine->RegisterGlobalProperty("const int jjResolutionHeight", &realVideoH);
		/**
		 * @brief Width of a player's subscreen in pixels
		 *
		 * Equal to @ref jjResolutionWidth with a single local player, smaller when the screen is split.
		 */
		engine->RegisterGlobalProperty("const int jjSubscreenWidth", &subVideoW);
		/**
		 * @brief Height of a player's subscreen in pixels
		 *
		 * Equal to @ref jjResolutionHeight with a single local player, smaller when the screen is split.
		 */
		engine->RegisterGlobalProperty("const int jjSubscreenHeight", &subVideoH);
		engine->RegisterGlobalFunction("int get_jjBorderWidth()", asFUNCTION(getBorderWidth), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjBorderHeight()", asFUNCTION(getBorderHeight), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjVerticalSplitscreen()", asFUNCTION(getSplitscreenType), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjVerticalSplitscreen(bool)", asFUNCTION(setSplitscreenType), asCALL_CDECL);

		// TODO
		/*engine->RegisterGlobalProperty("const bool jjAllowsMouseAim", &checkedAllowMouseAim);
		engine->RegisterGlobalProperty("const bool jjAllowsReady", &checkedAllowReady);
		engine->RegisterGlobalProperty("const bool jjAllowsWalljump", &checkedAllowWalljump);
		engine->RegisterGlobalProperty("const bool jjAlwaysRunning", &alwaysRunning);
		engine->RegisterGlobalProperty("const bool jjAutoWeaponChange", &weaponChange);
		engine->RegisterGlobalProperty("const bool jjFriendlyFire", &checkedFriendlyFire);
		engine->RegisterGlobalProperty("const bool jjMouseAim", &mouseAim);
		engine->RegisterGlobalProperty("const bool jjNoBlink", &checkedNoBlink);
		engine->RegisterGlobalProperty("const bool jjNoMovement", &checkedNoMovement);
		engine->RegisterGlobalProperty("const bool jjQuirks", &testQuirksMode);
		engine->RegisterGlobalProperty("const bool jjShowMaxHealth", &showEmptyHearts);*/

		// Options of a server of JJ2+, off as they're without one
		static bool allowsFireball = false, strongPowerups = false;
		/**
		 * @brief Whether weapon 8 is the fireball instead of the pepper spray
		 *
		 * Always `false`.
		 */
		engine->RegisterGlobalProperty("const bool jjAllowsFireball", &allowsFireball);
		/**
		 * @brief Whether the server enabled stronger powered-up weapons
		 *
		 * Always `false`.
		 */
		engine->RegisterGlobalProperty("const bool jjStrongPowerups", &strongPowerups);

		/**
		 * @brief Score needed to win a competitive game
		 *
		 * @partialsupport Always 0.
		 */
		engine->RegisterGlobalProperty("const int jjMaxScore", &maxScore);
		engine->RegisterGlobalFunction("int get_jjTeamScore(TEAM::Color)", asFUNCTION(get_teamScore), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjMaxHealth()", asFUNCTION(GetMaxHealth), asCALL_CDECL);
		/**
		 * @brief Whether missing health is shown as empty hearts in the HUD
		 *
		 * Always `false`.
		 */
		engine->RegisterGlobalProperty("const bool jjShowMaxHealth", &jjShowMaxHealth);
		engine->RegisterGlobalFunction("int get_jjStartHealth()", asFUNCTION(GetStartHealth), asCALL_CDECL);

		/*engine->RegisterGlobalProperty("const bool jjDoZombiesAlreadyExist", &doZombiesAlreadyExist);
		engine->RegisterGlobalFunction("jjPLAYER@ get_jjBottomFeeder()", asFUNCTION(get_bottomFeeder), asCALL_CDECL);
		engine->RegisterGlobalFunction("jjPLAYER@ get_jjTokenOwner()", asFUNCTION(get_tokenOwner), asCALL_CDECL);*/
	}
}

#endif
