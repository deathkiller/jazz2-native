#if defined(WITH_ANGELSCRIPT)

#include "LevelScriptLoader.h"
#include "Legacy/AnimationTables.h"
#include "Legacy/EventTranslator.h"
#include "Legacy/ObjectHost.h"
#include "Legacy/WorldCanvas.h"
#include "RegisterMath.h"
#include "RegisterRef.h"
#include "RegisterString.h"
#include "RegisterArray.h"
#include "RegisterDictionary.h"
#if defined(WITH_IMGUI)
#	include "RegisterImGuiBindings.h"
#endif
#include "ScriptActorWrapper.h"
#include "ScriptPlayerWrapper.h"

#include "../ContentResolver.h"
#include "../LevelHandler.h"
#include "../PreferencesCache.h"
#include "../Actors/ActorBase.h"
#include "../Actors/Player.h"
#include "../Compatibility/AnimSetMapping.h"
#include "../Compatibility/EventConverter.h"
#include "../Compatibility/JJ2Strings.h"
#include "../Rendering/PlayerViewport.h"
#include "../UI/HUD.h"
#include "../UI/InGameConsole.h"

#include "../../nCine/Application.h"
#include "../../nCine/Base/Random.h"
#include "../../nCine/Graphics/RenderResources.h"
#include "../../nCine/Graphics/Texture.h"
#include "../../nCine/Graphics/Viewport.h"

#if defined(DEATH_TRACE)
#	define AS_LOG_EXCEPTION(ctx)																						\
		do {																											\
			std::int32_t column = 0; const char* sectionName = nullptr;													\
			std::int32_t line = ctx->GetExceptionLineNumber(&column, &sectionName);										\
			LOGE("{}:{}({}) ‡ An exception \"{}\" occurred in \"{}\". Please correct the code and try again.",			\
				sectionName, line, column, ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());	\
		} while (false)
#else
#	define AS_LOG_EXCEPTION(ctx) do {} while (false)
#endif

// Without namespace for shorter log messages
static void asScript(String& msg)
{
	LOGI("{}", msg.data());
}

namespace Jazz2::Scripting
{
	// The pages of the help screen of JJ2+ a script changes in onHelp(), only kept, the game has no such screen
	static constexpr StringView LegacyHelpPages = R"(
namespace HELP {
	enum Position { LEFT, RIGHT, FLOATLEFT, FLOATRIGHT }
}
class jjHELPPARAGRAPH {
	string text;
	STRING::Size textSize = STRING::SMALL;
	STRING::Alignment textAlignment = STRING::LEFT;
	const jjANIMFRAME@ image;
	const jjANIMATION@ animation;
	HELP::Position position = HELP::LEFT;
	jjHELPPARAGRAPH() {}
	jjHELPPARAGRAPH(const string &in text, STRING::Size textSize = STRING::SMALL, STRING::Alignment textAlignment = STRING::LEFT,
		const jjANIMFRAME@ image = null, HELP::Position position = HELP::LEFT) {
		this.text = text;
		this.textSize = textSize;
		this.textAlignment = textAlignment;
		@this.image = image;
		this.position = position;
	}
	jjHELPPARAGRAPH(const string &in text, const jjANIMATION@ image, STRING::Size textSize = STRING::SMALL,
		STRING::Alignment textAlignment = STRING::LEFT, HELP::Position position = HELP::LEFT) {
		this.text = text;
		this.textSize = textSize;
		this.textAlignment = textAlignment;
		@this.animation = image;
		this.position = position;
	}
}
class jjHELPPAGE {
	array<jjHELPPARAGRAPH> paragraphs;
	string tags;
	jjHELPPAGE() {}
	jjHELPPAGE(const array<jjHELPPARAGRAPH> &in paragraphs, const string &in tags = "") {
		this.paragraphs = paragraphs;
		this.tags = tags;
	}
}
)"_s;

	// MLLE::Setup() of the include of weapons of MLLE, in place of it: the weapons as the level has them (see
	// Formats::ScriptDataChunk), those of the script with a hook they share, and then the rest of what it sets up
	static constexpr StringView MlleWeaponsSetup = R"(
namespace MLLE {
	se::WeaponHook@ WeaponHook;

	bool Setup(array<MLLEWeaponApply@> weapons) {
		jjSTREAM data;
		if (_GetWeapons(data)) {
			int16 hookArgument;
			data.pop(hookArgument);
			for (uint slot = 1; slot <= 9; ++slot) {
				bool custom;
				int maximum;
				uint8 birds, gunCrates, gemsLost, gemsLostPowerup, flags, ammoCrate, gun8Style;
				jjSTREAM parameters;
				data.pop(custom);
				data.pop(maximum);
				data.pop(birds);
				data.pop(gunCrates);
				data.pop(gemsLost);
				data.pop(gemsLostPowerup);
				data.pop(flags);
				data.pop(ammoCrate);
				data.pop(gun8Style);
				data.pop(parameters);

				jjWEAPON@ weapon = jjWeapons[slot];
				weapon.maximum = maximum;
				weapon.comesFromBirds = (birds != 0);
				weapon.comesFromBirdsPowerup = (birds == 2);
				weapon.comesFromGunCrates = (gunCrates != 0);
				weapon.gemsLost = gemsLost;
				weapon.gemsLostPowerup = gemsLostPowerup;
				weapon.infinite = ((flags & 1) != 0);
				weapon.replenishes = ((flags & 2) != 0);
				if (custom) {
					if (WeaponHook is null) {
						@WeaponHook = (hookArgument < 0 ? se::DefaultWeaponHook() : se::DefaultWeaponHook(uint8(hookArgument)));
					}
					if (slot <= weapons.length && weapons[slot - 1] !is null) {
						weapons[slot - 1].Apply(slot, WeaponHook, parameters, ammoCrate);
					}
				} else if (gun8Style != 255) {
					weapon.spread = (gun8Style == 0 ? SPREAD::GUN8 : (gun8Style == 1 ? SPREAD::PEPPERSPRAY : SPREAD::NORMAL));
					if (gun8Style == 2) {
						weapon.gradualAim = false;
					}
				}
			}
		}
		return Setup();
	}
}

shared interface MLLEWeaponApply { bool Apply(uint, se::WeaponHook@ = null, jjSTREAM@ = null, uint8 = 0); }
)"_s;

	static float asFractionf(float v)
	{
		float intPart;
		return modff(v, &intPart);
	}

	static std::int32_t asRandom()
	{
		return Random().Next();
	}

	static std::int32_t asRandom(std::int32_t max)
	{
		return Random().Fast(0, max);
	}

	static float asRandom(float min, float max)
	{
		return Random().FastFloat(min, max);
	}

	// The rest of the math add-on of AngelScript, which JJ2+ ships whole
	static bool asCloseTo(float a, float b, float epsilon)
	{
		// The same relative test as the add-on, so a script compares exactly as it does in JJ2+
		if (a == b) {
			return true;
		}
		float diff = fabsf(a - b);
		if ((a == 0.0f || b == 0.0f) && (diff < epsilon)) {
			return true;
		}
		// The add-on divides in double precision (fabs of floats)
		return (diff / (fabs(a) + fabs(b)) < epsilon);
	}

	static bool asCloseToDouble(double a, double b, double epsilon)
	{
		if (a == b) {
			return true;
		}
		double diff = fabs(a - b);
		if ((a == 0.0 || b == 0.0) && (diff < epsilon)) {
			return true;
		}
		return (diff / (fabs(a) + fabs(b)) < epsilon);
	}

	static float asFloatFromIEEE(std::uint32_t raw)
	{
		float value;
		std::memcpy(&value, &raw, sizeof(value));
		return value;
	}

	static std::uint32_t asFloatToIEEE(float value)
	{
		std::uint32_t raw;
		std::memcpy(&raw, &value, sizeof(raw));
		return raw;
	}

	static double asDoubleFromIEEE(std::uint64_t raw)
	{
		double value;
		std::memcpy(&value, &raw, sizeof(value));
		return value;
	}

	static std::uint64_t asDoubleToIEEE(double value)
	{
		std::uint64_t raw;
		std::memcpy(&raw, &value, sizeof(raw));
		return raw;
	}

	LevelScriptLoader::LevelScriptLoader(LevelHandler* levelHandler, StringView scriptPath)
		: _levelHandler(levelHandler), _onLevelUpdate(nullptr), _onLevelUpdateLastFrame(-1), _onDrawAmmo(nullptr),
			_onDrawHealth(nullptr), _onDrawLives(nullptr), _onDrawPlayerTimer(nullptr), _onDrawScore(nullptr), _onDrawGameModeHUD(nullptr),
			_enabledCallbacks(NoInit, 256)
	{
		// jjDifficultyOrig, as jjDifficulty is when the level starts (scripts limit the speed of platforms by it)
		switch (levelHandler->_difficulty) {
			case GameDifficulty::Easy: DifficultyAtLevelStart = 0; break;
			default: DifficultyAtLevelStart = 1; break;
			case GameDifficulty::Hard: DifficultyAtLevelStart = 2; break;
		}

		// Try to load the script
		HashMap<String, bool> DefinedSymbols = {
#if defined(DEATH_TARGET_ANDROID)
			{ "TARGET_ANDROID"_s, true },
#elif defined(DEATH_TARGET_APPLE)
			{ "TARGET_APPLE"_s, true },
#	if defined(DEATH_TARGET_IOS)
			{ "TARGET_IOS"_s, true },
#	endif
#elif defined(DEATH_TARGET_EMSCRIPTEN)
			{ "TARGET_EMSCRIPTEN"_s, true },
#elif defined(DEATH_TARGET_WINDOWS)
			{ "TARGET_WINDOWS"_s, true },
#	if defined(DEATH_TARGET_WINDOWS_RT)
			{ "TARGET_WINDOWS_RT"_s, true },
#	endif
#elif defined(DEATH_TARGET_SWITCH)
			{ "TARGET_SWITCH"_s, true },
#elif defined(DEATH_TARGET_UNIX)
			{ "TARGET_UNIX"_s, true },
#endif

#if defined(DEATH_TARGET_BIG_ENDIAN)
			{ "TARGET_BIG_ENDIAN"_s, true },
#endif

#if defined(RHI_GL_PROFILE_ES)
			{ "RHI_GL_PROFILE_ES"_s, true },
#endif
#if defined(WITH_AUDIO)
			{ "WITH_AUDIO"_s, true },
#endif
#if defined(WITH_VORBIS)
			{ "WITH_VORBIS"_s, true },
#endif
#if defined(WITH_OPENMPT)
			{ "WITH_OPENMPT"_s, true },
#endif
#if defined(WITH_XMP)
			{ "WITH_XMP"_s, true },
#endif
#if defined(WITH_IMGUI)
			{ "WITH_IMGUI"_s, true },
#endif
#if defined(WITH_THREADS)
			{ "WITH_THREADS"_s, true },
#endif
			{ "Resurrection"_s, true }
		};

		// The IDs of the animation sets differ between the versions of the game (see Legacy::AnimationTables), and only
		// 1.24 has Lori, so whether her sprites were converted tells which one it is
		{
			ContentResolver::SpriteSheet sheet;
			versionTSF = ContentResolver::Get().ReadSpriteSheet("Lori/airboard.aura"_s, sheet, false);
		}

		auto contextType = AddScriptFromFile(scriptPath, DefinedSymbols);
		if (contextType == ScriptContextType::Unknown) {
			LOGE("Cannot compile the script. Please correct the code and try again.");
#if defined(DEATH_DEBUG)
			levelHandler->_console->WriteLine(UI::MessageLevel::Error, "Cannot compile the script. Please correct the code and try again."_s);
#endif
			return;
		}

		SetContextType(contextType);
		RegisterBuiltInFunctions(GetEngine());
		switch (GetContextType()) {
			case ScriptContextType::Legacy:
				LOGD("Compiling script with \"Legacy\" context");
#if defined(DEATH_DEBUG)
				levelHandler->_console->WriteLine(UI::MessageLevel::Debug, "Compiling script with \"Legacy\" context"_s);
#endif
				RegisterLegacyFunctions(GetEngine());
				// Strings of JJ2+ are bytes of Windows-1252 as they're written, `§1` of a text is 0xA7 there (the default
				// UTF-8 scanner made it 0xC2 0xA7, drawn as `Â§`) - the text is recoded when it's drawn
				GetEngine()->SetEngineProperty(asEP_SCRIPT_SCANNER, 0);
				// The language as the older AngelScript of JJ2+ has it
				GetEngine()->SetEngineProperty(asEP_ALLOW_NONCONST_IMPLICIT_THIS_CALL, true);
				GetEngine()->SetEngineProperty(asEP_MEMBER_INIT_MODE, 0);
				GetEngine()->SetEngineProperty(asEP_FOREACH_SUPPORT, false);
				break;
			case ScriptContextType::Standard:
				LOGD("Compiling script with \"Standard\" context");
#if defined(DEATH_DEBUG)
				levelHandler->_console->WriteLine(UI::MessageLevel::Debug, "Compiling script with \"Standard\" context"_s);
#endif
				RegisterStandardFunctions(GetEngine(), GetMainModule());
				break;
		}

		// The global variables are initialized in OnLevelLoad(), once the players exist - scripts of JJ2+ take them in the
		// initializers of their variables (`int oldScore = jjLocalPlayers[0].score;`), and the script is built before
		GetEngine()->SetEngineProperty(asEP_INIT_GLOBAL_VARS_AFTER_BUILD, false);

		ScriptBuildResult r = Build();
		if (r != ScriptBuildResult::Success) {
			LOGE("Cannot compile the script. Please correct the code and try again.");
#if defined(DEATH_DEBUG)
			levelHandler->_console->WriteLine(UI::MessageLevel::Error, "Cannot compile the script. Please correct the code and try again."_s);
#endif
			return;
		}

		LOGD("Script compiled successfully.");

		// Enable all callbacks by default
		_enabledCallbacks.setAll();

		switch (GetContextType()) {
			case ScriptContextType::Legacy:
				_onLevelUpdate = GetMainModule()->GetFunctionByDecl("void onMain()");
				_onDrawAmmo = GetMainModule()->GetFunctionByDecl("bool onDrawAmmo(jjPLAYER@ player, jjCANVAS@ canvas)");
				_onDrawHealth = GetMainModule()->GetFunctionByDecl("bool onDrawHealth(jjPLAYER@ player, jjCANVAS@ canvas)");
				_onDrawLives = GetMainModule()->GetFunctionByDecl("bool onDrawLives(jjPLAYER@ player, jjCANVAS@ canvas)");
				_onDrawPlayerTimer = GetMainModule()->GetFunctionByDecl("bool onDrawPlayerTimer(jjPLAYER@ player, jjCANVAS@ canvas)");
				_onDrawScore = GetMainModule()->GetFunctionByDecl("bool onDrawScore(jjPLAYER@ player, jjCANVAS@ canvas)");
				_onDrawGameModeHUD = GetMainModule()->GetFunctionByDecl("bool onDrawGameModeHUD(jjPLAYER@ player, jjCANVAS@ canvas)");
				_onPlayer = GetMainModule()->GetFunctionByDecl("void onPlayer(jjPLAYER@)");
				// Runs before onPlayer() within the same tick, which is where a script belongs that wants to change what
				// the player believes its controls are doing (see jjPLAYER's key properties)
				_onPlayerInput = GetMainModule()->GetFunctionByDecl("void onPlayerInput(jjPLAYER@)");
				_onPlayerDraw = GetMainModule()->GetFunctionByDecl("void onPlayerDraw(jjPLAYERDRAW&)");
				_onObjectHit = GetMainModule()->GetFunctionByDecl("void onObjectHit(jjOBJ@, jjOBJ@, jjPLAYER@, int)");
				for (std::int32_t i = 0; i < 8; i++) {
					char decl[64];
					std::size_t length = formatInto(decl, "void onDrawLayer{}(jjPLAYER@, jjCANVAS@)", i + 1);
					decl[length] = '\0';
					_onDrawLayer[i] = GetMainModule()->GetFunctionByDecl(decl);
				}
				break;
			case ScriptContextType::Standard:
				_onLevelUpdate = GetMainModule()->GetFunctionByDecl("void onLevelUpdate(float)");
				// TODO: Add draw callbacks
				break;
		}

		// Snapshot the level's palette so scripts see it via jjPalette and can restore it via jjPAL::reset()
		CaptureLevelPalette();
	}

	LevelScriptLoader::~LevelScriptLoader()
	{
		// Release the reference each cached jjLAYER proxy holds (the script side may still hold its own references)
		for (auto& pair : _layerProxies) {
			if (pair.second != nullptr) {
				pair.second->Release();
			}
		}
		for (auto& pair : _tileProxies) {
			pair.second->Release();
		}
		// The objects of the game a script still drives stop now, while the script can still be released
		ReleaseObjects();
		// What the script drew refers to the graphics of the animation tables
		_worldCanvas = nullptr;
		_layerCanvas = nullptr;
		for (auto& pair : _spritePaletteOffsets) {
			if (pair.second >= 0) {
				ContentResolver::Get().ReleasePaletteOffset(pair.second);
			}
		}
		// Gives the graphics of the game back as they were, before anything else of the level goes
		_animTables = nullptr;
	}

	void LevelScriptLoader::CaptureLevelPalette()
	{
		auto palettes = ContentResolver::Get().GetPalettes();
		for (std::int32_t i = 0; i < ContentResolver::ColorsPerPalette; i++) {
			std::uint32_t c = palettes[i];
			jjPALCOLOR col;
			col.red = (std::uint8_t)(c & 0xFF);
			col.green = (std::uint8_t)((c >> 8) & 0xFF);
			col.blue = (std::uint8_t)((c >> 16) & 0xFF);
			jjPalette.setColorEntry((std::uint8_t)i, col);
			jjBackupPalette.setColorEntry((std::uint8_t)i, col);
			// The converter applies the palette MLLE would, so it's the one the level starts with
			_mllePalette.setColorEntry((std::uint8_t)i, col);
		}
	}

	String LevelScriptLoader::OnProcessInclude(StringView includePath, StringView scriptPath)
	{
		// Skip MLLE files, because it's handled natively
		if (includePath.hasPrefix("MLLE-Include-"_s) && includePath.hasSuffix(".asc"_s)) {
			// The include of weapons (`MLLE-Include-1.5w.asc` and later) has MLLE::Setup() take the weapons of the script,
			// set up as the level has them (see Formats::ScriptDataChunk) with the hook of SEweapon.asc it includes
			if (includePath.hasSuffix("w.asc"_s) && !_mlleWeaponsAdded) {
				_mlleWeaponsAdded = true;
				AddScriptSection("MLLE-Weapons", MlleWeaponsSetup);
				return fs::FindPathCaseInsensitive(fs::CombinePath(ContentResolver::Get().GetSourcePath(), "SEweapon.asc"_s));
			}
			return {};
		}

		// TODO: Allow multiple search paths
		//return ConstructPath(includePath, path);

		auto sourcePath = ContentResolver::Get().GetSourcePath();
		return fs::FindPathCaseInsensitive(fs::CombinePath(sourcePath, includePath));
	}

	void LevelScriptLoader::OnProcessPragma(StringView content, ScriptContextType& contextType)
	{
		// #pragma target Jazz² Resurrection - Changes script context type to Standard
		if (content == "target Jazz² Resurrection"_s || content == "target Jazz2 Resurrection"_s) {
			contextType = ScriptContextType::Standard;
		}
	}

	void LevelScriptLoader::OnBeforeScriptCall()
	{
		// The script sees the objects as they are now
		SyncObjectsFromActors();
		PullWeather();
	}

	void LevelScriptLoader::PullWeather(bool force)
	{
		if (!force && _levelHandler->_weatherType == _appliedWeather && _levelHandler->_weatherIntensity == _appliedWeatherIntensity) {
			return;
		}
		_appliedWeather = _levelHandler->_weatherType;
		_appliedWeatherIntensity = _levelHandler->_weatherIntensity;
		WeatherType weatherKind = (_appliedWeather & ~WeatherType::OutdoorsOnly);
		snowing = (weatherKind != WeatherType::None);
		snowingOutdoors = ((_appliedWeather & WeatherType::OutdoorsOnly) == WeatherType::OutdoorsOnly);
		snowingIntensity = _appliedWeatherIntensity;
		snowingType = (snowing ? std::int32_t(weatherKind) - 1 : 0);
	}

	bool LevelScriptLoader::OwnsWeather() const
	{
		return (GetContextType() == ScriptContextType::Legacy);
	}

	void LevelScriptLoader::OnAfterScriptCall()
	{
		// The objects get what the script changed of them
		SyncObjectsToActors();
		// The sprites of the game show what the script changed of the animations
		if (_animTables != nullptr) {
			_animTables->Flush();
		}

		// The weather as the script set it, SNOWING::Type is one less than WeatherType
		WeatherType weather = WeatherType::None;
		if (snowing && snowingType >= 0 && snowingType <= 3) {
			weather = WeatherType(snowingType + 1);
			if (snowingOutdoors) {
				weather |= WeatherType::OutdoorsOnly;
			}
		}
		if (weather != _appliedWeather || snowingIntensity != _appliedWeatherIntensity) {
			_appliedWeather = weather;
			_appliedWeatherIntensity = snowingIntensity;
			_levelHandler->SetWeather(weather, snowingIntensity);
		}
	}

	void LevelScriptLoader::OnLevelLoad()
	{
		// jjTileCount, which initializers of global variables may read already, grows by jjTilesFromTileset()
		if (auto tileMap = _levelHandler->TileMap()) {
			numberOfTiles = std::uint32_t(tileMap->GetLoadedTileCount());
		}
		UpdateViewSize();

		// The weather of the level, scripts start and stop it by these
		PullWeather(true);

		// Scenery is a part of its tile, it has an object only where a script drives it (see Legacy::ScriptSceneryObject),
		// a script can still register a spawner of its own over this one
		for (EventType type : { EventType::SceneryDestruct, EventType::SceneryDestructButtstomp, EventType::SceneryDestructSpeed, EventType::SceneryCollapse,
			EventType::TriggerArea }) {
			_levelHandler->EventSpawner()->RegisterSpawnable(type, SpawnSceneryObject);
		}
		_levelHandler->EventSpawner()->RegisterSpawnable(EventType::PassthroughEvent, SpawnPassthroughObject);

		// See the constructor, a variable whose initializer fails is left at its default, as AngelScript leaves it
		{
			OnBeforeScriptCall();
			asIScriptContext* ctx = GetEngine()->RequestContext();
			if (GetMainModule()->ResetGlobalVars(ctx) < 0) {
				LOGE("Cannot initialize all global variables of the script. Please correct the code and try again.");
			}
			GetEngine()->ReturnContext(ctx);
			OnAfterScriptCall();
		}

		asIScriptFunction* func = GetMainModule()->GetFunctionByDecl("void onLevelLoad()");
		if (func != nullptr) {
			OnBeforeScriptCall();
			asIScriptContext* ctx = GetEngine()->RequestContext();

			ctx->Prepare(func);
			std::int32_t r = ctx->Execute();
			if (r == asEXECUTION_EXCEPTION) {
				AS_LOG_EXCEPTION(ctx);
			}

			GetEngine()->ReturnContext(ctx);
			OnAfterScriptCall();
		}

		// The scenery is activated by the templates as they are now, see RefreshSceneryObjects()
		_sceneryObjectsMask = GetSceneryObjectsMask();
	}

	void LevelScriptLoader::OnLevelBegin()
	{
		asIScriptFunction* func = GetMainModule()->GetFunctionByDecl("void onLevelBegin()");
		if (func == nullptr) {
			return;
		}

		OnBeforeScriptCall();
		asIScriptContext* ctx = GetEngine()->RequestContext();

		ctx->Prepare(func);
		std::int32_t r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			AS_LOG_EXCEPTION(ctx);
		}

		GetEngine()->ReturnContext(ctx);
		OnAfterScriptCall();
	}

	void LevelScriptLoader::OnReplacing()
	{
		if (_animTables != nullptr) {
			_animTables->RestoreNativeGraphics();
		}
	}

	void LevelScriptLoader::OnLevelReload()
	{
		// The level went back to a checkpoint, but not what scripts wrote to it - the boss is to be activated again
		ReapplyEventWrites();
		_bossAreaActivated = false;
		// JJ2+ starts the objects again, those scripts made are gone (scripts make them again then)
		DeleteObjectsOfScripts();

		asIScriptFunction* func = GetMainModule()->GetFunctionByDecl("void onLevelReload()");
		if (func == nullptr) {
			return;
		}

		OnBeforeScriptCall();
		asIScriptContext* ctx = GetEngine()->RequestContext();

		ctx->Prepare(func);
		std::int32_t r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			AS_LOG_EXCEPTION(ctx);
		}

		GetEngine()->ReturnContext(ctx);
		OnAfterScriptCall();
	}

	void LevelScriptLoader::UpdateViewSize()
	{
		// The HUD callbacks set them for the view they draw, a script without any reads them too; before the views are
		// made, they're of the size the game is made for
		Vector2i viewSize = _levelHandler->GetViewSize();
		if (viewSize.X <= 0 || viewSize.Y <= 0) {
			viewSize = Vector2i(LevelHandler::DefaultWidth, LevelHandler::DefaultHeight);
		}
		Vector2i subscreenSize = viewSize;
		if (!_levelHandler->_assignedViewports.empty()) {
			// The view of the first player, there's no other one without splitscreen
			Rectf bounds = _levelHandler->_assignedViewports[0]->GetBounds();
			if (bounds.W >= 1.0f && bounds.H >= 1.0f) {
				subscreenSize = Vector2i(std::int32_t(bounds.W), std::int32_t(bounds.H));
			}
		}

		realVideoW = viewSize.X;
		realVideoH = viewSize.Y;
		subVideoW = subscreenSize.X;
		subVideoH = subscreenSize.Y;
		checkedMaxSubVideoWidth = subscreenSize.X;
		checkedMaxSubVideoHeight = subscreenSize.Y;
	}

	std::int32_t LevelScriptLoader::GetGameTick() const
	{
		// The level goes back in time to a checkpoint when a player dies, the ticks of JJ2+ never do - the scripts would
		// stop until the time caught up again
		float elapsedFrames = _levelHandler->_elapsedFrames;
		if (elapsedFrames < _lastElapsedFrames) {
			_rolledBackFrames += _lastElapsedFrames - elapsedFrames;
		}
		_lastElapsedFrames = elapsedFrames;

		// It should update at 70 FPS instead of 60 FPS
		return (std::int32_t)((elapsedFrames + _rolledBackFrames) * (70.0f / 60.0f));
	}

	void LevelScriptLoader::OnLevelUpdate(float timeMult)
	{
		// What the script drew in the level lasts until it updates again, in the next tick - a frame without one draws it
		// again, the objects draw only in ticks too (see Legacy::ScriptObjectHost::OnUpdate())
		std::int32_t currentTick = GetGameTick();
		// Frames of JJ2+ are its ticks, scripts time what they draw by it (blinking, animations)
		renderFrame = currentTick;
		if (_worldCanvas != nullptr && _canvasClearTick != currentTick) {
			_worldCanvas->Clear();
			_canvasClearTick = currentTick;
		}
		if (_particles != nullptr || (snowing && OwnsWeather())) {
			UpdateParticles(currentTick);
		}
		if (!_loopedSamples.empty()) {
			StopUnusedLoopedSamples();
		}
		UpdateViewSize();
		RefreshSceneryObjects();
		UpdateWaterLevel(currentTick);

		if (_layerCanvas == nullptr && !ContentResolver::Get().IsHeadless() &&
			std::any_of(std::begin(_onDrawLayer), std::end(_onDrawLayer), [](asIScriptFunction* f) { return f != nullptr; })) {
			_layerCanvas = std::make_unique<Legacy::ScriptLayerCanvas>(this);
			_layerCanvas->OrderedDraws = true;
			_layerCanvas->setParent(_levelHandler->_rootNode.get());
		}

		switch (GetContextType()) {
			case ScriptContextType::Legacy: {
				asIScriptFunction* onPlayer = _onPlayer;
				asIScriptFunction* onPlayerInput = _onPlayerInput;
				asIScriptFunction* onPlayerDraw = _onPlayerDraw;

				if (_onLevelUpdate == nullptr && onPlayer == nullptr && onPlayerInput == nullptr && onPlayerDraw == nullptr) {
					// jjGameTicks, as the ticks would have run
					_onLevelUpdateLastFrame = currentTick + 1;
					break;
				}
				if (_onLevelUpdateLastFrame > currentTick) {
					// No tick in this frame, the objects don't need to be synced either, the controls stay as the last
					// tick left them
					for (std::int32_t i = 0; i < ControlScheme::MaxSupportedPlayers; i++) {
						ApplyInputOverrides(i);
					}
					break;
				}

				// Legacy context requires fixed frame count per second
				OnBeforeScriptCall();
				asIScriptContext* ctx = GetEngine()->RequestContext();

				std::int32_t currentFrame = currentTick;
				while (_onLevelUpdateLastFrame <= currentFrame) {
					// The scripts decide again in each tick
					std::fill(std::begin(_inputOverrideMask), std::end(_inputOverrideMask), 0);
					if (onPlayerInput != nullptr) {
						for (auto* player : _levelHandler->_players) {
							ctx->Prepare(onPlayerInput);

							jjPLAYER* p = GetPlayerBackingStore(player);
							ctx->SetArgObject(0, p);

							std::int32_t r = ctx->Execute();
							if (r == asEXECUTION_EXCEPTION) {
								AS_LOG_EXCEPTION(ctx);
							}
						}
					}
					if (_onLevelUpdate != nullptr) {
						ctx->Prepare(_onLevelUpdate);
						std::int32_t r = ctx->Execute();
						if (r == asEXECUTION_EXCEPTION) {
							AS_LOG_EXCEPTION(ctx);
							// Don't call the method again if an exception occurs
							//_onLevelUpdate = nullptr;
						}
					}
					if (onPlayer != nullptr) {
						for (auto* player : _levelHandler->_players) {
							ctx->Prepare(onPlayer);

							jjPLAYER* p = GetPlayerBackingStore(player);
							ctx->SetArgObject(0, p);

							std::int32_t r = ctx->Execute();
							if (r == asEXECUTION_EXCEPTION) {
								AS_LOG_EXCEPTION(ctx);
								// Don't call the method again if an exception occurs
								//_onLevelUpdate = nullptr;
							}
						}
						// The players swim by the water as onPlayer() left it, the objects of the tick come after them in JJ2+
						_levelHandler->_playerWaterLevel = _levelHandler->_waterLevel;
						_levelHandler->_playerWaterLevelSet = true;
					}
					if (onPlayerDraw != nullptr) {
						// What the script draws with a player (a shadow), the player is drawn as
						// it is whatever the script changes of it
						for (auto* player : _levelHandler->_players) {
							jjPLAYER* p = GetPlayerBackingStore(player);
							Legacy::jjPLAYERDRAW draw = {};
							draw.name = draw.sprite = draw.sugarRush = draw.gunFlash = draw.invincibility = draw.trail = true;
							draw.morphingExplosions = true;
							// Only on an airboard, scripts tell one by it
							draw.airboardBouncingMotion = draw.airboardPuff = (player->GetModifier() == Actors::Player::Modifier::Airboard);
							draw.spriteMode = std::int32_t(p->get_spriteMode());
							draw.spriteParam = p->get_spriteParam();
							draw.layer = 4;
							draw.curFrame = p->get_curFrame();
							// The sign is the side the player faces, as in JJ2+ (scripts set 1 for "direction")
							draw.xScale = (player->IsFacingLeft() ? -1.0f : 1.0f);
							draw.yScale = 1.0f;
							draw._player = p;
							const Legacy::jjPLAYERDRAW original = draw;

							ctx->Prepare(onPlayerDraw);
							ctx->SetArgAddress(0, &draw);
							std::int32_t r = ctx->Execute();
							if (r == asEXECUTION_EXCEPTION) {
								AS_LOG_EXCEPTION(ctx);
							}

							// Another frame, offset, scale, angle or mode the script gave the player is drawn here in place of the
							// player until the next tick (trapezes and burgers), the player isn't drawn without a sprite
							bool replaced = (draw.curFrame != original.curFrame || draw.xOffset != original.xOffset ||
								draw.yOffset != original.yOffset || draw.xScale != original.xScale || draw.yScale != original.yScale ||
								draw.angle != original.angle || draw.spriteMode != original.spriteMode || draw.spriteParam != original.spriteParam);
							p->SetDrawnByScript(!draw.sprite || replaced);
							if (draw.sprite && replaced && !ContentResolver::Get().IsHeadless()) {
								std::int32_t cell = 0;
								if (auto* graphics = GetAnimationTables().ResolveFrame(draw.curFrame, cell)) {
									// In the colors of the fur of the player, as it draws itself
									std::uint32_t mode = std::uint32_t(draw.spriteMode);
									std::int32_t paletteOffset = (mode == Legacy::spriteType_NORMAL
										? GetSpritePaletteOffset(Legacy::spriteType_PLAYER, std::uint8_t(player->GetPlayerIndex()))
										: GetSpritePaletteOffset(mode, draw.spriteParam));
									Legacy::ScriptWorldCanvas::SpriteDraw sprite = { graphics, cell, draw.xOffset, draw.yOffset, 1, draw.xScale, draw.yScale,
										draw.angle, mode, draw.spriteParam, GetLayerDrawZ(std::int8_t(draw.layer)), paletteOffset, player };
									GetWorldCanvas().AddSprite(sprite);
								}
							}
						}
					}
					_onLevelUpdateLastFrame++;
				}

				GetEngine()->ReturnContext(ctx);
				OnAfterScriptCall();
				break;
			}
			case ScriptContextType::Standard: {
				_onLevelUpdateLastFrame = (int32_t)_levelHandler->_elapsedFrames;
				if (_onLevelUpdate == nullptr) {
					break;
				}

				// Standard context supports floating frame rate
				OnBeforeScriptCall();
				asIScriptContext* ctx = GetEngine()->RequestContext();

				ctx->Prepare(_onLevelUpdate);
				ctx->SetArgFloat(0, timeMult);
				std::int32_t r = ctx->Execute();
				if (r == asEXECUTION_EXCEPTION) {
					AS_LOG_EXCEPTION(ctx);
					// Don't call the method again if an exception occurs
					_onLevelUpdate = nullptr;
				}

				GetEngine()->ReturnContext(ctx);
				OnAfterScriptCall();
				break;
			}
		}

		for (auto* player : _levelHandler->_players) {
			auto it = _playerBackingStore.find(player->GetPlayerIndex());
			if (it != _playerBackingStore.end()) {
				if (it->second->_timerState == 1) { // STARTED
					it->second->_timerLeft -= timeMult;
					if (it->second->_timerLeft <= 0.0f) {
						it->second->_timerState = 0; // STOPPED
						asIScriptFunction* func = (asIScriptFunction*)it->second->_timerCallback;
						if (func == nullptr) {
							func = GetMainModule()->GetFunctionByName("onPlayerTimerEnd");
							if (func == nullptr) {
								continue;
							}
						}

						OnBeforeScriptCall();
						asIScriptContext* ctx = GetEngine()->RequestContext();

						ctx->Prepare(func);

						std::int32_t typeId = 0;
						if (func->GetParam(0, &typeId) >= 0) {
							if ((typeId & (asTYPEID_OBJHANDLE | asTYPEID_APPOBJECT)) == (asTYPEID_OBJHANDLE | asTYPEID_APPOBJECT)) {
								asITypeInfo* typeInfo = GetEngine()->GetTypeInfoById(typeId);
								if (typeInfo->GetName() == "jjPLAYER"_s) {
									jjPLAYER* p = GetPlayerBackingStore(player);
									ctx->SetArgObject(0, p);
								}
							}
						}

						std::int32_t r = ctx->Execute();
						if (r == asEXECUTION_EXCEPTION) {
							AS_LOG_EXCEPTION(ctx);
						}

						GetEngine()->ReturnContext(ctx);
						OnAfterScriptCall();
					}
				}
			}
		}

		// Push any script-modified jjLAYER properties into the engine layer descriptions (no-op if no jjLayers accessed)
		SyncLayerProperties();
	}

	void LevelScriptLoader::OnLevelCallback(Actors::ActorBase* initiator, uint8_t* eventParams)
	{
		std::uint32_t callbackId = eventParams[0];
		if (!_enabledCallbacks[callbackId]) {
			return;
		}

		if (eventParams[2]) { // Vanish
			_enabledCallbacks.reset(callbackId);
		}

		char funcName[32];
		std::size_t length = formatInto(funcName, "onFunction{}", callbackId);
		funcName[length] = '\0';

		asIScriptFunction* func = GetMainModule()->GetFunctionByName(funcName);
		if (func != nullptr) {
			OnBeforeScriptCall();
			asIScriptContext* ctx = GetEngine()->RequestContext();

			ctx->Prepare(func);

			std::int32_t paramIdx = 0;
			std::int32_t typeId = 0;
			if (func->GetParam(paramIdx, &typeId) >= 0) {
				if ((typeId & (asTYPEID_OBJHANDLE | asTYPEID_APPOBJECT)) == (asTYPEID_OBJHANDLE | asTYPEID_APPOBJECT)) {
					asITypeInfo* typeInfo = GetEngine()->GetTypeInfoById(typeId);
					if (typeInfo->GetName() == "jjPLAYER"_s) {
						if (auto* player = runtime_cast<Actors::Player>(initiator)) {
							jjPLAYER* p = GetPlayerBackingStore(player);
							ctx->SetArgObject(0, p);
						} else {
							// Player is required but wasn't provided
							return;
						}
					}
					paramIdx++;
				}
			}
			if (func->GetParam(paramIdx, &typeId) >= 0) {
				if (typeId == asTYPEID_BOOL || typeId == asTYPEID_INT8 || typeId == asTYPEID_UINT8) {
					ctx->SetArgByte(1, eventParams[1]);
					paramIdx++;
				}
			}

			std::int32_t r = ctx->Execute();
			if (r == asEXECUTION_EXCEPTION) {
				AS_LOG_EXCEPTION(ctx);
			}

			GetEngine()->ReturnContext(ctx);
			OnAfterScriptCall();
			return;
		}

		LOGW("Callback function \"{}\" was not found in the script. Please correct the code and try again.", funcName);
	}

	bool LevelScriptLoader::OnDraw(UI::HUD* hud, Actors::Player* player, const Rectf& view, DrawType type)
	{
		asIScriptFunction* func;
		switch (type) {
			case DrawType::WeaponAmmo: func = _onDrawAmmo; break;
			case DrawType::Health: func = _onDrawHealth; break;
			case DrawType::Lives: func = _onDrawLives; break;
			case DrawType::PlayerTimer: func = _onDrawPlayerTimer; break;
			case DrawType::Score: func = _onDrawScore; break;
			case DrawType::GameModeHUD: func = _onDrawGameModeHUD; break;
			default: func = nullptr; break;
		}

		bool overrideDraw = false;
		if (func != nullptr) {
			// TODO: Move this somewhere
			checkedMaxSubVideoWidth = (std::int32_t)view.W;
			checkedMaxSubVideoHeight = (std::int32_t)view.H;
			realVideoW = (std::int32_t)view.W;
			realVideoH = (std::int32_t)view.H;
			subVideoW = (std::int32_t)view.W;
			subVideoH = (std::int32_t)view.H;

			// The draws of every hook of the HUD in a frame, so each is drawn over the ones before it
			std::uint32_t frame = theApplication().GetFrameCount();
			bool firstInFrame = (_canvasFrame != frame);
			if (firstInFrame || _canvasHud != hud) {
				_canvasFrame = frame;
				_canvasHud = hud;
				_canvasDrawCount = 0;
			}

			// The objects don't change between the hooks of the HUD in a frame, only by what the scripts write, which their
			// views keep - so they're pulled only before the first one
			if (firstInFrame) {
				OnBeforeScriptCall();
			}
			asIScriptContext* ctx = GetEngine()->RequestContext();

			ctx->Prepare(func);

			jjPLAYER* p = GetPlayerBackingStore(player);
			ctx->SetArgObject(0, p);
			jjCANVAS* canvasWrapper = new(asAllocMem(sizeof(jjCANVAS))) jjCANVAS(hud, Vector2f(view.X, view.Y), view.W, &_canvasDrawCount,
				UI::HUD::MainLayer, UI::HUD::FontLayer - 1); // TODO: Cache this object ???
			ctx->SetArgObject(1, canvasWrapper);

			std::int32_t r = ctx->Execute();
			if (r == asEXECUTION_FINISHED) {
				overrideDraw = (ctx->GetReturnByte() != 0);
			} else if (r == asEXECUTION_EXCEPTION) {
				AS_LOG_EXCEPTION(ctx);
			}

			GetEngine()->ReturnContext(ctx);
			OnAfterScriptCall();

			// TODO
			asFreeMem(canvasWrapper);
		}

		return overrideDraw;
	}

	void LevelScriptLoader::OnDrawLayers(UI::Canvas* canvas)
	{
		auto tileMap = _levelHandler->TileMap();
		const Viewport* viewport = RenderResources::GetCurrentViewport();
		if (tileMap == nullptr || viewport == nullptr) {
			return;
		}
		Actors::Player* player = nullptr;
		for (auto& playerViewport : _levelHandler->_assignedViewports) {
			if (playerViewport->_view.get() == viewport) {
				player = runtime_cast<Actors::Player>(playerViewport->_targetActor);
				break;
			}
		}
		if (player == nullptr) {
			return;
		}

		// As JJ2+, from the back to the front, the layers placed for the view as they're drawn (see Tiles::TileMap::OnDraw())
		Rectf view = viewport->GetCullingRect();
		asIScriptContext* ctx = nullptr;
		for (std::int32_t number = 8; number >= 1; number--) {
			asIScriptFunction* func = _onDrawLayer[number - 1];
			std::int32_t index = GetLayerIndex(number);
			if (func == nullptr || index < 0 || index >= std::int32_t(tileMap->_layers.size())) {
				continue;
			}

			// In front of the tiles of the layer, behind what's drawn on the layer (see GetLayerDrawZ()) - the sprite layer
			// has the objects in front
			std::uint16_t firstLayer = std::uint16_t(tileMap->_layers[index].Description.Depth + 1);
			std::uint16_t lastLayer = std::uint16_t(number == 4 ? ILevelHandler::SpritePlaneZ - 1 : std::max<std::int32_t>(GetLayerDrawZ(number) - 1, firstLayer));
			std::uint16_t drawCount = 0;
			jjCANVAS layerCanvas(canvas, tileMap->GetGameplayLayerOrigin(index, view), view.W, &drawCount, firstLayer, lastLayer);

			if (ctx == nullptr) {
				OnBeforeScriptCall();
				ctx = GetEngine()->RequestContext();
			}
			ctx->Prepare(func);
			ctx->SetArgObject(0, GetPlayerBackingStore(player));
			ctx->SetArgObject(1, &layerCanvas);
			_drawingLayers = true;
			if (ctx->Execute() == asEXECUTION_EXCEPTION) {
				AS_LOG_EXCEPTION(ctx);
			}
			_drawingLayers = false;
			if (number == 8) {
				ApplyUseLayer8Speeds();
			}
		}
		if (ctx != nullptr) {
			GetEngine()->ReturnContext(ctx);
			OnAfterScriptCall();
		}
	}

	void LevelScriptLoader::SetUseLayer8Speeds(bool value)
	{
		_useLayer8Speeds = value;
		if (!_drawingLayers) {
			ApplyUseLayer8Speeds();
		}
	}

	void LevelScriptLoader::ApplyUseLayer8Speeds()
	{
		auto tileMap = _levelHandler->TileMap();
		std::int32_t index = GetLayerIndex(8);
		if (tileMap == nullptr || index < 0 || index >= std::int32_t(tileMap->_layers.size())) {
			return;
		}
		auto& description = tileMap->_layers[index].Description;
		if (_layer8Fixed < 0) {
			_layer8Fixed = (description.SpeedModelX == Tiles::LayerSpeedModel::AlwaysOnTop &&
				description.SpeedModelY == Tiles::LayerSpeedModel::AlwaysOnTop ? 1 : 0);
		}
		if (_layer8Fixed != 0) {
			Tiles::LayerSpeedModel model = (_useLayer8Speeds ? Tiles::LayerSpeedModel::Default : Tiles::LayerSpeedModel::AlwaysOnTop);
			description.SpeedModelX = model;
			description.SpeedModelY = model;
		}
	}

	void LevelScriptLoader::OnPlayerDied(Actors::Player* player, Actors::ActorBase* collider)
	{
		auto it = _playerBackingStore.find(player->GetPlayerIndex());
		if (it != _playerBackingStore.end()) {
			if (!it->second->_timerPersists) {
				it->second->_timerState = 0; // STOPPED
			}
		}

		asIScriptFunction* func = GetMainModule()->GetFunctionByName("onRoast");
		if (func != nullptr) {
			OnBeforeScriptCall();
			asIScriptContext* ctx = GetEngine()->RequestContext();

			ctx->Prepare(func);

			std::int32_t typeId = 0;
			if (func->GetParam(0, &typeId) >= 0) {
				if ((typeId & (asTYPEID_OBJHANDLE | asTYPEID_APPOBJECT)) == (asTYPEID_OBJHANDLE | asTYPEID_APPOBJECT)) {
					asITypeInfo* typeInfo = GetEngine()->GetTypeInfoById(typeId);
					if (typeInfo->GetName() == "jjPLAYER"_s) {
						jjPLAYER* p = GetPlayerBackingStore(player);
						ctx->SetArgObject(0, p);
					}
				}
			}
			if (func->GetParam(1, &typeId) >= 0) {
				if ((typeId & (asTYPEID_OBJHANDLE | asTYPEID_APPOBJECT)) == (asTYPEID_OBJHANDLE | asTYPEID_APPOBJECT)) {
					asITypeInfo* typeInfo = GetEngine()->GetTypeInfoById(typeId);
					if (typeInfo->GetName() == "jjPLAYER"_s) {
						// TODO: Detect weapons properly
						if (auto* killer = runtime_cast<Actors::Player>(collider)) {
							jjPLAYER* p = GetPlayerBackingStore(killer);
							ctx->SetArgObject(0, p);
						} else {
							ctx->SetArgObject(0, nullptr);
						}
					}
				}
			}

			std::int32_t r = ctx->Execute();
			if (r == asEXECUTION_EXCEPTION) {
				AS_LOG_EXCEPTION(ctx);
			}

			GetEngine()->ReturnContext(ctx);
			OnAfterScriptCall();
		}
	}

	void LevelScriptLoader::RegisterBuiltInFunctions(asIScriptEngine* engine)
	{
		RegisterMath(engine);
		RegisterRef(engine);
		RegisterString(engine);
		RegisterArray(engine);
		RegisterDictionary(engine);
#if defined(WITH_IMGUI)
		RegisterImGuiBindings(engine);
#endif

		// Math functions
		int r;
		r = engine->RegisterGlobalFunction("float cos(float)", asFUNCTIONPR(cosf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float sin(float)", asFUNCTIONPR(sinf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float tan(float)", asFUNCTIONPR(tanf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float acos(float)", asFUNCTIONPR(acosf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float asin(float)", asFUNCTIONPR(asinf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float atan(float)", asFUNCTIONPR(atanf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float atan2(float, float)", asFUNCTIONPR(atan2f, (float, float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float cosh(float)", asFUNCTIONPR(coshf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float sinh(float)", asFUNCTIONPR(sinhf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float tanh(float)", asFUNCTIONPR(tanhf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float log(float)", asFUNCTIONPR(logf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float log10(float)", asFUNCTIONPR(log10f, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float pow(float, float)", asFUNCTIONPR(powf, (float, float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float sqrt(float)", asFUNCTIONPR(sqrtf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("float ceil(float)", asFUNCTIONPR(ceilf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float abs(float)", asFUNCTIONPR(fabsf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float floor(float)", asFUNCTIONPR(floorf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float fraction(float)", asFUNCTIONPR(asFractionf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("bool closeTo(float, float, float = 0.00001f)", asFUNCTION(asCloseTo), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("bool closeTo(double, double, double = 0.0000000001)", asFUNCTION(asCloseToDouble), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float fpFromIEEE(uint)", asFUNCTION(asFloatFromIEEE), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("uint fpToIEEE(float)", asFUNCTION(asFloatToIEEE), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("double fpFromIEEE(uint64)", asFUNCTION(asDoubleFromIEEE), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("uint64 fpToIEEE(double)", asFUNCTION(asDoubleToIEEE), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		// Needs the array type, so it can't be part of RegisterString()
		RegisterStringUtils(engine);
	}

	void LevelScriptLoader::RegisterLegacyFunctions(asIScriptEngine* engine)
	{
		// JJ2+ Declarations (provided by JJ2+ team on 2023/01/06), registered by the parts of the API in Legacy/ - in this
		// order, a type has to be registered before what uses it
		RegisterLegacyGame(engine);
		RegisterLegacyPlayer(engine);
		RegisterLegacyScreen(engine);
		RegisterLegacyLayerGlobals(engine);
		RegisterLegacyPalette(engine);
		RegisterLegacyCanvas(engine);
		RegisterLegacyTexturedBackground(engine);
		RegisterLegacyLevel(engine);
		RegisterLegacySound(engine);
		RegisterLegacyCharacters(engine);
		RegisterLegacyObjects(engine);
		RegisterLegacyParticles(engine);
		RegisterLegacyStream(engine);
		RegisterLegacyAnimations(engine);
		RegisterLegacyPixelMaps(engine);
		RegisterLegacyLayers(engine);
		RegisterLegacyPlayerDraw(engine);
		RegisterLegacyEnumValues(engine);
		RegisterLegacyMlle(engine);
		// Declared by a script, not by the engine
		AddScriptSection("JJ2+ Help", LegacyHelpPages);
	}

	void LevelScriptLoader::RegisterStandardFunctions(asIScriptEngine* engine, asIScriptModule* module)
	{
		int r;
		r = engine->RegisterGlobalFunction("int Random()", asFUNCTIONPR(asRandom, (), int), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("int Random(int)", asFUNCTIONPR(asRandom, (int), int), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float Random(float, float)", asFUNCTIONPR(asRandom, (float, float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("void Print(const string &in)", asFUNCTION(asScript), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("uint8 get_Difficulty()", asFUNCTION(asGetDifficulty), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("bool get_IsReforged()", asFUNCTION(asIsReforged), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("int get_LevelWidth()", asFUNCTION(asGetLevelWidth), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("int get_LevelHeight()", asFUNCTION(asGetLevelHeight), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float get_ElapsedFrames()", asFUNCTION(asGetElapsedFrames), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float get_AmbientLight()", asFUNCTION(asGetAmbientLight), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void set_AmbientLight(float)", asFUNCTION(asSetAmbientLight), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("float get_WaterLevel()", asFUNCTION(asGetWaterLevel), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void set_WaterLevel(float)", asFUNCTION(asSetWaterLevel), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("void PreloadMetadata(const string &in)", asFUNCTION(asPreloadMetadata), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void RegisterSpawnable(int, const string &in)", asFUNCTION(asRegisterSpawnable), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void Spawn(int, int, int)", asFUNCTION(asSpawnEvent), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void Spawn(int, int, int, const array<uint8> &in)", asFUNCTION(asSpawnEventParams), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void Spawn(const string &in, int, int)", asFUNCTION(asSpawnType), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void Spawn(const string &in, int, int, const array<uint8> &in)", asFUNCTION(asSpawnTypeParams), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("void ChangeLevel(int, const string &in = string())", asFUNCTION(asChangeLevel), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		//r = engine->RegisterGlobalFunction("void MusicPlay(const string &in)", asFUNCTION(asMusicPlay), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void ShowLevelText(const string &in)", asFUNCTION(asShowLevelText), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("void SetWeather(uint8, uint8)", asFUNCTION(asSetWeather), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		// Game-specific classes
		ScriptActorWrapper::RegisterFactory(engine, module);
		ScriptPlayerWrapper::RegisterFactory(engine);
	}

	void LevelScriptLoader::OverridePlayerInput(Actors::Player* player, PlayerAction action, bool value)
	{
		if (player == nullptr) {
			return;
		}

		// `_playerInputs` is only as wide as the local control schemes, but a multiplayer player index comes
		// from MpLevelHandler::FindFreePlayerId() and is not bounded by that at all - writing at such an index
		// would run off the end of the array. Remote players have no local input state to override anyway.
		std::int32_t playerIndex = player->GetPlayerIndex();
		if (playerIndex < 0 || playerIndex >= ControlScheme::MaxSupportedPlayers) {
			return;
		}

		// Kept until the next tick, the frames without one are applied it again (see ApplyInputOverrides())
		std::uint64_t bit = (1ull << (std::int32_t)action);
		_inputOverrideMask[playerIndex] |= bit;
		if (value) {
			_inputOverrideValue[playerIndex] |= bit;
		} else {
			_inputOverrideValue[playerIndex] &= ~bit;
		}
		ApplyInputOverrides(playerIndex);
	}

	void LevelScriptLoader::ApplyInputOverrides(std::int32_t playerIndex)
	{
		std::uint64_t mask = _inputOverrideMask[playerIndex];
		if (mask == 0) {
			return;
		}

		// Written straight into the state the input was collected into, rather than kept as a separate
		// override: the collection happens once at the start of the frame and everything downstream
		// (PlayerActionPressed(), PlayerActionHit(), PlayerHorizontalMovement()) reads it from here, so a
		// script tick that runs afterwards is simply the last word on what was pressed. It also means the
		// press/release edges keep working, because next frame's PressedActionsLast comes from this.
		auto& input = _levelHandler->_playerInputs[playerIndex];
		input.PressedActions = (input.PressedActions & ~mask) | (_inputOverrideValue[playerIndex] & mask);

		// The directions are also read as an axis, which is what the movement code actually uses, so the two
		// representations have to be kept in step. Both directions are re-read from the bitmask: a script that
		// releases one direction while the other is still held ("p.keyRight = true; p.keyLeft = false;") would
		// otherwise zero the whole axis and stop the player dead, and the outcome would depend on the order the
		// two properties were assigned.
		auto isPressed = [&input](PlayerAction action) {
			return ((input.PressedActions & (1ull << (std::int32_t)action)) != 0);
		};
		auto isOverridden = [mask](PlayerAction action) {
			return ((mask & (1ull << (std::int32_t)action)) != 0);
		};
		if (isOverridden(PlayerAction::Left) || isOverridden(PlayerAction::Right)) {
			input.RequiredMovement.X = (isPressed(PlayerAction::Right) ? 1.0f : (isPressed(PlayerAction::Left) ? -1.0f : 0.0f));
		}
		if (isOverridden(PlayerAction::Up) || isOverridden(PlayerAction::Down)) {
			input.RequiredMovement.Y = (isPressed(PlayerAction::Down) ? 1.0f : (isPressed(PlayerAction::Up) ? -1.0f : 0.0f));
		}
	}

	jjPLAYER* LevelScriptLoader::GetPlayerBackingStore(Actors::Player* player)
	{
		auto it = _playerBackingStore.find(player->GetPlayerIndex());
		if (it != _playerBackingStore.end()) {
			it->second->SyncPropertiesToBackingStore();
			return it->second.get();
		}

		auto [result, success] = _playerBackingStore.emplace(player->GetPlayerIndex(), std::make_unique<jjPLAYER>(this, player));
		DEATH_DEBUG_ASSERT(success);
		result->second->SyncPropertiesToBackingStore();
		return result->second.get();
	}

	jjPLAYER* LevelScriptLoader::GetPlayerBackingStore(std::int32_t playerIndex)
	{
		std::uint8_t playerIndex8 = (std::uint8_t)playerIndex;
		auto it = _playerBackingStore.find(playerIndex8);
		if (it != _playerBackingStore.end()) {
			it->second->SyncPropertiesToBackingStore();
			return it->second.get();
		}

		auto& players = _levelHandler->_players;
		auto** pp = std::find_if(players.begin(), players.end(), [playerIndex8](Actors::Player* p) {
			return (p->GetPlayerIndex() == playerIndex8);
		});
		if (pp == players.end()) {
			// Scripts go through all the players in `jjPlayers` and check `isActive`, so a missing one can't be null
			if (_absentPlayer == nullptr) {
				_absentPlayer = std::make_unique<jjPLAYER>(this, nullptr);
			}
			return _absentPlayer.get();
		}

		auto [result, success] = _playerBackingStore.emplace(playerIndex8, std::make_unique<jjPLAYER>(this, *pp));
		DEATH_DEBUG_ASSERT(success);
		result->second->SyncPropertiesToBackingStore();
		return result->second.get();
	}

	Actors::ActorBase* LevelScriptLoader::CreateActorInstance(StringView typeName)
	{
		auto nullTerminatedTypeName = String::nullTerminatedView(typeName);

		// Create an instance of the ActorBase script class that inherits from the ScriptActorWrapper C++ class
		asITypeInfo* typeInfo = GetMainModule()->GetTypeInfoByName(nullTerminatedTypeName.data());
		if (typeInfo == nullptr) {
			return nullptr;
		}

		asIScriptObject* obj = static_cast<asIScriptObject*>(GetEngine()->CreateScriptObject(typeInfo));

		// Get the pointer to the C++ side of the ActorBase class
		ScriptActorWrapper* obj2 = *static_cast<ScriptActorWrapper**>(obj->GetAddressOfProperty(0));

		// Increase the reference count to the C++ object, as this is what will be used to control the life time of the object from the application side 
		obj2->AddRef();

		// Release the reference to the script side
		obj->Release();

		return obj2;
	}

	ArrayView<Actors::Player* const> LevelScriptLoader::GetPlayers() const
	{
		return _levelHandler->_players;
	}

	uint8_t LevelScriptLoader::asGetDifficulty()
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return (uint8_t)_this->_levelHandler->_difficulty;
	}

	bool LevelScriptLoader::asIsReforged()
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return (uint8_t)_this->_levelHandler->_isReforged;
	}

	int LevelScriptLoader::asGetLevelWidth()
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_tileMap->GetLevelBounds().X;
	}

	int LevelScriptLoader::asGetLevelHeight()
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_tileMap->GetLevelBounds().Y;
	}

	float LevelScriptLoader::asGetElapsedFrames()
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_elapsedFrames;
	}

	float LevelScriptLoader::asGetAmbientLight()
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		// TODO: Viewports
		return _this->_levelHandler->_defaultAmbientLight.W;
	}

	void LevelScriptLoader::asSetAmbientLight(float value)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		// TODO: Viewports
		_this->_levelHandler->_defaultAmbientLight.W = value;
	}

	float LevelScriptLoader::asGetWaterLevel()
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_waterLevel;
	}

	void LevelScriptLoader::asSetWaterLevel(float value)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->_waterLevel = value;
	}

	void LevelScriptLoader::asPreloadMetadata(const String& path)
	{
		ContentResolver::Get().PreloadMetadataAsync(path);
	}

	void LevelScriptLoader::asRegisterSpawnable(std::int32_t eventType, const String& typeName)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		asITypeInfo* typeInfo = _this->GetMainModule()->GetTypeInfoByName(typeName.data());
		if (typeInfo == nullptr) {
			return;
		}

		bool added = _this->_eventTypeToTypeInfo.emplace(eventType, typeInfo).second;
		if (added) {
			_this->_levelHandler->EventSpawner()->RegisterSpawnable((EventType)eventType, asRegisterSpawnableCallback);
		}
	}

	std::shared_ptr<Actors::ActorBase> LevelScriptLoader::asRegisterSpawnableCallback(const Actors::ActorActivationDetails& details)
	{
		if (auto* levelHandler = runtime_cast<LevelHandler>(details.LevelHandler)) {
			auto _this = levelHandler->_scripts.get();
			// Spawn() function with custom event cannot be used in OnLevelLoad(), because _scripts is not assigned yet
			if (_this != nullptr) {
				auto it = _this->_eventTypeToTypeInfo.find((int)details.Type);
				if (it != _this->_eventTypeToTypeInfo.end()) {
					asIScriptObject* obj = static_cast<asIScriptObject*>(_this->GetEngine()->CreateScriptObject(it->second));
					ScriptActorWrapper* obj2 = *static_cast<ScriptActorWrapper**>(obj->GetAddressOfProperty(0));
					obj2->AddRef();
					obj->Release();
					obj2->OnActivated(details);
					return std::shared_ptr<Actors::ActorBase>(obj2);
				}
			}
		}
		return nullptr;
	}

	void LevelScriptLoader::asSpawnEvent(std::int32_t eventType, std::int32_t x, std::int32_t y)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		auto actor = _this->_levelHandler->EventSpawner()->SpawnEvent((EventType)eventType, spawnParams, Actors::ActorState::None, Vector3i(x, y, ILevelHandler::MainPlaneZ));
		if (actor != nullptr) {
			_this->_levelHandler->AddActor(actor);
		}
	}

	void LevelScriptLoader::asSpawnEventParams(std::int32_t eventType, std::int32_t x, std::int32_t y, const CScriptArray& eventParams)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		int size = eventParams.GetSize();
		std::memcpy(spawnParams, eventParams.At(0), size);

		auto actor = _this->_levelHandler->EventSpawner()->SpawnEvent((EventType)eventType, spawnParams, Actors::ActorState::None, Vector3i(x, y, ILevelHandler::MainPlaneZ));
		if (actor != nullptr) {
			_this->_levelHandler->AddActor(actor);
		}
	}

	void LevelScriptLoader::asSpawnType(const String& typeName, std::int32_t x, std::int32_t y)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		auto actor = _this->CreateActorInstance(typeName);
		if (actor == nullptr) {
			return;
		}

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		actor->OnActivated(Actors::ActorActivationDetails(
			_this->_levelHandler,
			Vector3i(x, y, ILevelHandler::MainPlaneZ),
			spawnParams
		));
		_this->_levelHandler->AddActor(std::shared_ptr<Actors::ActorBase>(actor));
	}

	void LevelScriptLoader::asSpawnTypeParams(const String& typeName, std::int32_t x, std::int32_t y, const CScriptArray& eventParams)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		auto actor = _this->CreateActorInstance(typeName);
		if (actor == nullptr) {
			return;
		}

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		int size = eventParams.GetSize();
		std::memcpy(spawnParams, eventParams.At(0), size);

		actor->OnActivated(Actors::ActorActivationDetails(
			_this->_levelHandler,
			Vector3i(x, y, ILevelHandler::MainPlaneZ),
			spawnParams
		));
		_this->_levelHandler->AddActor(std::shared_ptr<Actors::ActorBase>(actor));
	}

	void LevelScriptLoader::asChangeLevel(std::int32_t exitType, const String& path)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->BeginLevelChange(nullptr, (ExitType)exitType, path);
	}

	void LevelScriptLoader::asShowLevelText(const String& text)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->ShowLevelText(text);
	}

	void LevelScriptLoader::asSetWeather(std::uint8_t weatherType, std::uint8_t intensity)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->SetWeather((WeatherType)weatherType, intensity);
	}
}

#endif
