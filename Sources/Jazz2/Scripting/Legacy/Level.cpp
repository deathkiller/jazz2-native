#if defined(WITH_ANGELSCRIPT)

#include "Level.h"
#include "EventTranslator.h"
#include "Internal.h"

#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	bool LevelScriptLoader::get_jjTriggers(std::uint8_t id) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->GetTrigger(id);
	}
	bool LevelScriptLoader::set_jjTriggers(std::uint8_t id, bool value) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->SetTrigger(id, value);
		return value;
	}
	bool LevelScriptLoader::jjSwitchTrigger(std::uint8_t id) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->SetTrigger(id, !_this->_levelHandler->GetTrigger(id));
		return _this->_levelHandler->GetTrigger(id);
	}

	bool LevelScriptLoader::isNumberedASFunctionEnabled(std::uint8_t id) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_enabledCallbacks[id];
	}
	bool LevelScriptLoader::setNumberedASFunctionEnabled(std::uint8_t id, bool value) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_enabledCallbacks.set(id, value);
		return value;
	}
	void LevelScriptLoader::reenableAllNumberedASFunctions() {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_enabledCallbacks.setAll();
	}

	float LevelScriptLoader::getWaterLevel() {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_waterLevel;
	}
	float LevelScriptLoader::getWaterLevel2() {
		// Where the water goes, see UpdateWaterLevel()
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return (_this->_waterTarget >= 0.0f ? _this->_waterTarget : _this->_levelHandler->_waterLevel);
	}
	float LevelScriptLoader::setWaterLevel(float value, bool instant) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (instant) {
			_this->_levelHandler->_waterLevel = value;
			_this->_waterTarget = -1.0f;
		} else {
			_this->_waterTarget = std::max(value, 0.0f);
		}
		return value;
	}
	float LevelScriptLoader::get_waterChangeSpeed() {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_waterChangeSpeed;
	}
	float LevelScriptLoader::set_waterChangeSpeed(float value) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_waterChangeSpeed = value;
		return value;
	}

	void LevelScriptLoader::UpdateWaterLevel(std::int32_t currentTick)
	{
		// As JJ2+ moves the water to the level `jjSetWaterLevel()` asked for, by jjWaterChangeSpeed pixels each tick
		std::int32_t ticks = (_waterTick >= 0 ? currentTick - _waterTick : 0);
		_waterTick = currentTick;
		if (_waterTarget < 0.0f || ticks <= 0) {
			return;
		}

		float& waterLevel = _levelHandler->_waterLevel;
		float step = std::abs(_waterChangeSpeed) * float(std::min(ticks, 70));
		if (std::abs(_waterTarget - waterLevel) <= step) {
			waterLevel = _waterTarget;
			_waterTarget = -1.0f;
		} else {
			waterLevel += (_waterTarget > waterLevel ? step : -step);
		}
	}
	std::int32_t LevelScriptLoader::get_waterLayer() {
		noop(); return 0;
	}
	std::int32_t LevelScriptLoader::set_waterLayer(std::int32_t value) {
		noop(); return 0;
	}
	void LevelScriptLoader::setWaterGradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2) {
		noop();
	}
	void LevelScriptLoader::setWaterGradientFromColors(jjPALCOLOR color1, jjPALCOLOR color2) {
		noop();
	}
	void LevelScriptLoader::setWaterGradientToTBG() {
		noop();
	}
	void LevelScriptLoader::resetWaterGradient() {
		noop();
	}

	void LevelScriptLoader::triggerRock(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint8_t eventParams[] = { id };
		_this->_levelHandler->BroadcastTriggeredEvent(nullptr, EventType::RollingRockTrigger, eventParams);
	}

	void LevelScriptLoader::cycleTo(const String& filename, bool warp, bool fast) {
		noop();
	}
	void LevelScriptLoader::jjNxt(bool warp, bool fast)
	{
		ExitType exitType = (warp ? ExitType::Warp : ExitType::Normal);
		if (fast) {
			exitType |= ExitType::FastTransition;
		}

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->BeginLevelChange(nullptr, exitType, {});
	}

	// Returns the bits of an event word a parameter of JJ2+ means, by its offset from the first bit of the parameters and
	// its length, negative for a signed one, as JCS.ini writes them; `false` if they aren't in the word
	static bool GetEventParamBits(std::int32_t offset, std::int32_t length, std::int32_t& shift, std::uint32_t& mask) {
		std::int32_t bits = (length < 0 ? -length : length);
		shift = Legacy::EventTranslator::ParamsShift + offset;
		if (bits <= 0 || shift < 0 || shift + bits > 32) {
			return false;
		}
		mask = (bits >= 32 ? UINT32_MAX : (1u << bits) - 1);
		return true;
	}

	std::int32_t GetEvent(std::uint16_t tx, std::uint16_t ty) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return std::int32_t(owner->GetOriginalEvent(tx, ty) & 0xFF);
	}
	std::int32_t GetEventParamWrapper(std::uint16_t tx, std::uint16_t ty, std::int32_t offset, std::int32_t length) {
		std::int32_t shift; std::uint32_t mask;
		if (!GetEventParamBits(offset, length, shift, mask)) {
			return 0;
		}

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint32_t value = (owner->GetOriginalEvent(tx, ty) >> shift) & mask;
		if (length < 0 && mask != UINT32_MAX && (value & ((mask >> 1) + 1)) != 0) {
			// A signed parameter, two's complement in its own length
			return std::int32_t(std::int64_t(value) - std::int64_t(mask) - 1);
		}
		return std::int32_t(value);
	}
	void SetEventByte(std::uint16_t tx, std::uint16_t ty, std::uint8_t newEventId) {
		// Only the event changes, the parameters stay as they are
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint32_t word = owner->GetOriginalEvent(tx, ty);
		owner->SetOriginalEvent(tx, ty, (word & ~0xFFu) | newEventId);
	}
	void SetEventParam(std::uint16_t tx, std::uint16_t ty, std::int8_t offset, std::int8_t length, std::int32_t newValue) {
		std::int32_t shift; std::uint32_t mask;
		if (!GetEventParamBits(offset, length, shift, mask)) {
			return;
		}

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint32_t word = owner->GetOriginalEvent(tx, ty);
		word = (word & ~(mask << shift)) | ((std::uint32_t(newValue) & mask) << shift);
		owner->SetOriginalEvent(tx, ty, word);
	}
	std::int8_t GetTileType(std::uint16_t tile) {
		return std::int8_t(ScriptLoader::FromActiveContext<LevelScriptLoader>()->GetOriginalTileType(tile));
	}
	std::int8_t SetTileType(std::uint16_t tile, std::uint16_t value) {
		ScriptLoader::FromActiveContext<LevelScriptLoader>()->SetOriginalTileType(tile, std::uint8_t(value));
		return std::int8_t(value);
	}

	std::uint8_t LevelScriptLoader::GetOriginalTileType(std::int32_t tileId)
	{
		auto tileMap = _levelHandler->TileMap();
		if (tileMap == nullptr || tileId < 0 || tileId >= tileMap->GetStaticTileCount()) {
			return 0;
		}
		if (_tileTypes.empty()) {
			// The converter keeps the type of a tile on each placed tile of it (see Formats::TileRecord::ModifierMask)
			_tileTypes.resize(std::size_t(tileMap->GetStaticTileCount()), 0);
			SmallVector<std::uint8_t, 0> seen;
			seen.resize(std::size_t(tileMap->GetStaticTileCount()), 0);
			for (const TileMapLayer& layer : tileMap->_layers) {
				for (std::size_t i = 0; i < layer.Layout.size(); i++) {
					std::int32_t id = layer.Layout.TileIDAt(i);
					if (id <= 0 || id >= std::int32_t(_tileTypes.size()) || seen[id]) {
						continue;
					}
					seen[id] = 1;
					const LayerTile& tile = layer.Layout[i];
					_tileTypes[id] = std::uint8_t((tile.Flags & LayerTileFlags::Heat) == LayerTileFlags::Heat ? 5
						: (tile.Alpha == 0 ? 3 : (tile.Alpha == Tiles::TileMap::TranslucentTileAlpha ? 1 : 0)));
				}
			}
		} else if (std::size_t(tileId) >= _tileTypes.size()) {
			// A script added tiles since (see TileMap::AppendTileSet()), they have no type yet
			_tileTypes.resize(std::size_t(tileMap->GetStaticTileCount()), 0);
		}
		return _tileTypes[tileId];
	}

	void LevelScriptLoader::SetOriginalTileType(std::int32_t tileId, std::uint8_t type)
	{
		auto tileMap = _levelHandler->TileMap();
		if (tileMap == nullptr || tileId <= 0 || tileId >= tileMap->GetStaticTileCount() || GetOriginalTileType(tileId) == type) {
			return;
		}
		_tileTypes[tileId] = type;
		// The types the game draws, the others (as the caption) are drawn as ordinary tiles
		tileMap->SetTileModifier(tileId, type == 3 ? 0 : (type == 1 ? Tiles::TileMap::TranslucentTileAlpha : 255), type == 5);
	}
	bool GetTileIs32Bit(std::uint16_t tile) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->IsTileTrueColor(tile);
	}

	void LevelScriptLoader::RegisterLegacyLevel(asIScriptEngine* engine)
	{
		engine->SetDefaultNamespace("SNOWING");
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "SNOW", 0);
		engine->RegisterEnumValue("Type", "FLOWER", 1);
		engine->RegisterEnumValue("Type", "RAIN", 2);
		engine->RegisterEnumValue("Type", "LEAF", 3);
		engine->SetDefaultNamespace("");
		engine->RegisterGlobalProperty("bool jjIsSnowing", &snowing);
		engine->RegisterGlobalProperty("bool jjIsSnowingOutdoorsOnly", &snowingOutdoors);
		engine->RegisterGlobalProperty("uint8 jjSnowingIntensity", &snowingIntensity);
		engine->RegisterGlobalProperty("SNOWING::Type jjSnowingType", &snowingType);

		engine->RegisterGlobalFunction("bool get_jjTriggers(uint8)", asFUNCTION(get_jjTriggers), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjTriggers(uint8, bool)", asFUNCTION(set_jjTriggers), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjSwitchTrigger(uint8 id)", asFUNCTION(jjSwitchTrigger), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool get_jjEnabledASFunctions(uint8)", asFUNCTION(isNumberedASFunctionEnabled), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjEnabledASFunctions(uint8, bool)", asFUNCTION(setNumberedASFunctionEnabled), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjEnableEachASFunction()", asFUNCTION(reenableAllNumberedASFunctions), asCALL_CDECL);

		engine->SetDefaultNamespace("WATERLIGHT");
		engine->RegisterEnum("wl");
		engine->RegisterEnumValue("wl", "NONE", 0);
		engine->RegisterEnumValue("wl", "GLOBAL", 1);
		engine->RegisterEnumValue("wl", "LAGUNICUS", 3);
		engine->SetDefaultNamespace("WATERINTERACTION");
		engine->RegisterEnum("WaterInteraction");
		engine->RegisterEnumValue("WaterInteraction", "POSITIONBASED", waterInteraction_POSITIONBASED);
		engine->RegisterEnumValue("WaterInteraction", "SWIM", waterInteraction_SWIM);
		engine->RegisterEnumValue("WaterInteraction", "LOWGRAVITY", waterInteraction_LOWGRAVITY);
		engine->SetDefaultNamespace("");
		engine->RegisterGlobalProperty("WATERLIGHT::wl jjWaterLighting", &waterLightMode);
		engine->RegisterGlobalProperty("WATERINTERACTION::WaterInteraction jjWaterInteraction", &waterInteraction);
		engine->RegisterGlobalFunction("float get_jjWaterLevel()", asFUNCTION(getWaterLevel), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjWaterTarget()", asFUNCTION(getWaterLevel2), asCALL_CDECL);
		engine->RegisterGlobalFunction("float jjSetWaterLevel(float yPixel, bool instant)", asFUNCTION(setWaterLevel), asCALL_CDECL);
		engine->RegisterGlobalFunction("float get_jjWaterChangeSpeed()", asFUNCTION(get_waterChangeSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("float set_jjWaterChangeSpeed(float)", asFUNCTION(set_waterChangeSpeed), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjWaterLayer()", asFUNCTION(get_waterLayer), asCALL_CDECL);
		engine->RegisterGlobalFunction("int set_jjWaterLayer(int)", asFUNCTION(set_waterLayer), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetWaterGradient(uint8 red1, uint8 green1, uint8 blue1, uint8 red2, uint8 green2, uint8 blue2)", asFUNCTION(setWaterGradient), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetWaterGradient(jjPALCOLOR color1, jjPALCOLOR color2)", asFUNCTION(setWaterGradientFromColors), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjSetWaterGradient()", asFUNCTION(setWaterGradientToTBG), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjResetWaterGradient()", asFUNCTION(resetWaterGradient), asCALL_CDECL);

		engine->RegisterGlobalFunction("void jjTriggerRock(uint8 id)", asFUNCTION(triggerRock), asCALL_CDECL);

		engine->RegisterGlobalFunction("void jjNxt(const string &in filename, bool warp = false, bool fast = false)", asFUNCTION(cycleTo), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjNxt(bool warp = false, bool fast = false)", asFUNCTION(jjNxt), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool get_jjEnabledTeams(uint8)", asFUNCTION(getEnabledTeam), asCALL_CDECL);

		engine->RegisterGlobalProperty("uint8 jjKeyChat", &ChatKey);
		engine->RegisterGlobalFunction("bool get_jjKey(uint8)", asFUNCTION(getKeyDown), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjMouseX()", asFUNCTION(getCursorX), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjMouseY()", asFUNCTION(getCursorY), asCALL_CDECL);

		engine->RegisterGlobalProperty("bool jjWarpsTransmuteCoins", &warpsTransmuteCoins);
		engine->RegisterGlobalProperty("bool jjDelayGeneratedCrateOrigins", &delayGeneratedCrateOrigins);
		engine->RegisterGlobalFunction("bool get_jjUseLayer8Speeds()", asFUNCTION(getUseLayer8Speeds), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool set_jjUseLayer8Speeds(bool)", asFUNCTION(setUseLayer8Speeds), asCALL_CDECL);

		engine->RegisterGlobalProperty("bool jjSugarRushAllowed", &g_levelHasFood);
		engine->RegisterGlobalProperty("bool jjSugarRushesAllowed", &g_levelHasFood);

		engine->RegisterGlobalFunction("int jjEventGet(uint16 xTile, uint16 yTile)", asFUNCTION(GetEvent), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjParameterGet(uint16 xTile, uint16 yTile, int offset, int length)", asFUNCTION(GetEventParamWrapper), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjEventSet(uint16 xTile, uint16 yTile, uint8 newEventID)", asFUNCTION(SetEventByte), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjParameterSet(uint16 xTile, uint16 yTile, int8 offset, int8 length, int newValue)", asFUNCTION(SetEventParam), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint8 get_jjTileType(uint16)", asFUNCTION(GetTileType), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint8 set_jjTileType(uint16,uint8)", asFUNCTION(SetTileType), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool get_jjTileIs32Bit(uint16)", asFUNCTION(GetTileIs32Bit), asCALL_CDECL);
	}
}

#endif
