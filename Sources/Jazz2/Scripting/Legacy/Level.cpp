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
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_waterLayer;
	}
	std::int32_t LevelScriptLoader::set_waterLayer(std::int32_t value) {
		// A number of no layer hides the water as in JJ2+, the actors are still in it then
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_waterLayer = value;
		std::int32_t index = _this->GetLayerIndex(value);
		_this->_levelHandler->SetWaterLayer(index >= 0 ? index : Formats::LevelSettingsChunk::WaterHidden);
		return value;
	}
	void LevelScriptLoader::setWaterGradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2) {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->SetWaterColors(Vector3f(red1, green1, blue1) / 255.0f, Vector3f(red2, green2, blue2) / 255.0f);
	}
	void LevelScriptLoader::setWaterGradientFromColors(jjPALCOLOR color1, jjPALCOLOR color2) {
		setWaterGradient(color1.red, color1.green, color1.blue, color2.red, color2.green, color2.blue);
	}
	void LevelScriptLoader::setWaterGradientToTBG() {
		// The colors of the palette the textured background of JJ2 fades through
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		const jjPALCOLOR& top = _this->jjPalette.getColor(176);
		const jjPALCOLOR& bottom = _this->jjPalette.getColor(207);
		setWaterGradient(top.red, top.green, top.blue, bottom.red, bottom.green, bottom.blue);
	}
	void LevelScriptLoader::resetWaterGradient() {
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->SetWaterColors(LevelHandler::DefaultWaterColor, LevelHandler::DefaultWaterColor);
	}

	void LevelScriptLoader::triggerRock(std::uint8_t id) {

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint8_t eventParams[] = { id };
		_this->_levelHandler->BroadcastTriggeredEvent(nullptr, EventType::RollingRockTrigger, eventParams);
	}

	void LevelScriptLoader::cycleTo(const String& filename, bool warp, bool fast) {
		ExitType exitType = (warp ? ExitType::Warp : ExitType::Normal);
		if (fast) {
			exitType |= ExitType::FastTransition;
		}

		// As the converter names levels, without the extension and in lower case
		StringView name = filename;
		if (name.size() > 4) {
			StringView extension = name.exceptPrefix(name.size() - 4);
			if (StringUtils::equalsIgnoreCase(extension, ".j2l"_s) || StringUtils::equalsIgnoreCase(extension, ".lev"_s)) {
				name = name.exceptSuffix(4);
			}
		}
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->BeginLevelChange(nullptr, exitType, StringUtils::lowercase(name));
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
		/** @brief Kind of weather effect */
		engine->RegisterEnum("Type");
		engine->RegisterEnumValue("Type", "SNOW", 0);		///< Snowflakes
		engine->RegisterEnumValue("Type", "FLOWER", 1);		///< Flower petals
		engine->RegisterEnumValue("Type", "RAIN", 2);		///< Raindrops
		engine->RegisterEnumValue("Type", "LEAF", 3);		///< Falling leaves
		engine->SetDefaultNamespace("");
		/**
		 * @brief Whether a weather effect is active
		 *
		 * Reflects the weather of the level, including changes made by weather events. Assigning it turns the effect
		 * on or off once the current script callback returns; @ref jjSnowingType selects the kind of weather.
		 */
		engine->RegisterGlobalProperty("bool jjIsSnowing", &snowing);
		/** @brief Whether the weather effect is limited to outdoor areas of the level */
		engine->RegisterGlobalProperty("bool jjIsSnowingOutdoorsOnly", &snowingOutdoors);
		/**
		 * @brief Intensity of the weather effect
		 *
		 * Only matters while @ref jjIsSnowing is `true`.
		 */
		engine->RegisterGlobalProperty("uint8 jjSnowingIntensity", &snowingIntensity);
		/**
		 * @brief Kind of the weather effect
		 *
		 * Only matters while @ref jjIsSnowing is `true`.
		 */
		engine->RegisterGlobalProperty("SNOWING::Type jjSnowingType", &snowingType);

		/**
		 * @brief State of the level triggers, indexed by trigger ID
		 *
		 * The same triggers that Trigger Zone and Trigger Crate events turn on and off, so tiles tied to a trigger
		 * react to assignments too.
		 */
		engine->RegisterGlobalFunction("bool get_jjTriggers(uint8)", asFUNCTION(get_jjTriggers), asCALL_CDECL);
		/** @brief Sets the state of a level trigger */
		engine->RegisterGlobalFunction("bool set_jjTriggers(uint8, bool)", asFUNCTION(set_jjTriggers), asCALL_CDECL);
		/**
		 * @brief Flips the state of a level trigger
		 *
		 * Behaves like the switch option of Trigger Zone and Trigger Crate events.
		 *
		 * @return New state of the trigger
		 */
		engine->RegisterGlobalFunction("bool jjSwitchTrigger(uint8 id)", asFUNCTION(jjSwitchTrigger), asCALL_CDECL);

		/**
		 * @brief Whether each numbered `onFunction#` callback may be called by Text events
		 *
		 * All entries start as `true`. When a Text event with the vanish option calls `onFunction#`, the entry with
		 * the same number becomes `false` and further Text events skip the callback until it is enabled again.
		 */
		engine->RegisterGlobalFunction("bool get_jjEnabledASFunctions(uint8)", asFUNCTION(isNumberedASFunctionEnabled), asCALL_CDECL);
		/** @brief Sets whether a numbered `onFunction#` callback may be called by Text events */
		engine->RegisterGlobalFunction("bool set_jjEnabledASFunctions(uint8, bool)", asFUNCTION(setNumberedASFunctionEnabled), asCALL_CDECL);
		/** @brief Sets every entry of @ref jjEnabledASFunctions back to `true` */
		engine->RegisterGlobalFunction("void jjEnableEachASFunction()", asFUNCTION(reenableAllNumberedASFunctions), asCALL_CDECL);

		engine->SetDefaultNamespace("WATERLIGHT");
		/** @brief How ambient lighting behaves when the level has water */
		engine->RegisterEnum("wl");
		engine->RegisterEnumValue("wl", "NONE", LegacyWaterLightingNone);			///< Full brightness everywhere while water is active, ignoring ambient lighting
		engine->RegisterEnumValue("wl", "GLOBAL", LegacyWaterLightingGlobal);		///< Ambient lighting applies equally above and below the water surface
		engine->RegisterEnumValue("wl", "LAGUNICUS", LegacyWaterLightingLagunicus);	///< Full brightness above water, darker the deeper below the surface the player is
		engine->SetDefaultNamespace("WATERINTERACTION");
		/** @brief How local players move underwater */
		engine->RegisterEnum("WaterInteraction");
		engine->RegisterEnumValue("WaterInteraction", "POSITIONBASED", waterInteraction_POSITIONBASED);	///< Swimming or low gravity, chosen by how deep the water level is
		engine->RegisterEnumValue("WaterInteraction", "SWIM", waterInteraction_SWIM);						///< Players swim
		engine->RegisterEnumValue("WaterInteraction", "LOWGRAVITY", waterInteraction_LOWGRAVITY);			///< Players move normally but fall more slowly
		engine->SetDefaultNamespace("");
		/**
		 * @brief How ambient lighting behaves when the level has water
		 *
		 * The level starts with what its MLLE settings say.
		 * @partialsupport @ref WATERLIGHT::NONE and @ref WATERLIGHT::GLOBAL both light the level as the game always does, above
		 * and below the water. @ref WATERLIGHT::LAGUNICUS is approximated: full light above the water, and below it the light
		 * of the level is reached 15 tiles deep.
		 */
		engine->RegisterGlobalProperty("WATERLIGHT::wl jjWaterLighting", &waterLightMode);
		/**
		 * @brief How local players move underwater
		 *
		 * With @ref WATERINTERACTION::LOWGRAVITY players don't swim, they walk and jump as on land and fall with the gravity
		 * of the water. The level starts with what its MLLE settings say.
		 * @partialsupport @ref WATERINTERACTION::POSITIONBASED always swims, the gravity of the water isn't measured in JJ2+.
		 */
		engine->RegisterGlobalProperty("WATERINTERACTION::WaterInteraction jjWaterInteraction", &waterInteraction);
		/**
		 * @brief Current height of the water surface in pixels
		 *
		 * Measured from the top of the level. Read-only, use @ref jjSetWaterLevel() to change it.
		 */
		engine->RegisterGlobalFunction("float get_jjWaterLevel()", asFUNCTION(getWaterLevel), asCALL_CDECL);
		/**
		 * @brief Height in pixels the water surface is moving towards
		 *
		 * Differs from @ref jjWaterLevel only while the water is still moving after a non-instant @ref jjSetWaterLevel().
		 */
		engine->RegisterGlobalFunction("float get_jjWaterTarget()", asFUNCTION(getWaterLevel2), asCALL_CDECL);
		/**
		 * @brief Moves the water surface to a new height
		 *
		 * The height is in pixels, unlike the Water Level event, which uses tiles (32 pixels each). If `instant` is
		 * `true`, the surface jumps there immediately. Otherwise @ref jjWaterTarget is set and the surface moves by
		 * @ref jjWaterChangeSpeed pixels every tick until it gets there; a negative target is treated as 0.
		 *
		 * @return The requested height
		 */
		engine->RegisterGlobalFunction("float jjSetWaterLevel(float yPixel, bool instant)", asFUNCTION(setWaterLevel), asCALL_CDECL);
		/**
		 * @brief Speed of the water surface after a non-instant @ref jjSetWaterLevel()
		 *
		 * In pixels per tick, 1 by default.
		 */
		engine->RegisterGlobalFunction("float get_jjWaterChangeSpeed()", asFUNCTION(get_waterChangeSpeed), asCALL_CDECL);
		/** @brief Sets the speed of the water surface */
		engine->RegisterGlobalFunction("float set_jjWaterChangeSpeed(float)", asFUNCTION(set_waterChangeSpeed), asCALL_CDECL);
		/**
		 * @brief Layer the water is drawn in front of
		 *
		 * 1 by default. Any value outside 1 to 8 hides the water. This only affects drawing, players still swim in
		 * hidden water.
		 *
		 * @partialsupport Visible water is always drawn in front of all layers, the exact layer number currently
		 * doesn't matter.
		 */
		engine->RegisterGlobalFunction("int get_jjWaterLayer()", asFUNCTION(get_waterLayer), asCALL_CDECL);
		/** @brief Sets the layer the water is drawn in front of */
		engine->RegisterGlobalFunction("int set_jjWaterLayer(int)", asFUNCTION(set_waterLayer), asCALL_CDECL);
		/**
		 * @brief Sets the colors of the water from two RGB colors
		 *
		 * The first color tints the water at its surface, the second one at the bottom of the view.
		 * @partialsupport The colors tint the water effect of the game, which isn't the gradient of JJ2+. The software renderer
		 * and the consoles without shaders keep the default color.
		 */
		engine->RegisterGlobalFunction("void jjSetWaterGradient(uint8 red1, uint8 green1, uint8 blue1, uint8 red2, uint8 green2, uint8 blue2)", asFUNCTION(setWaterGradient), asCALL_CDECL);
		/**
		 * @brief Sets the colors of the water from two palette colors
		 *
		 * See the other overload.
		 */
		engine->RegisterGlobalFunction("void jjSetWaterGradient(jjPALCOLOR color1, jjPALCOLOR color2)", asFUNCTION(setWaterGradientFromColors), asCALL_CDECL);
		/**
		 * @brief Sets the colors of the water from the textured background colors of the palette
		 *
		 * The colors of palette indices 176 and 207, the gradient the textured background of the original game fades through.
		 */
		engine->RegisterGlobalFunction("void jjSetWaterGradient()", asFUNCTION(setWaterGradientToTBG), asCALL_CDECL);
		/** @brief Restores the default colors of the water */
		engine->RegisterGlobalFunction("void jjResetWaterGradient()", asFUNCTION(resetWaterGradient), asCALL_CDECL);

		/**
		 * @brief Starts every rolling rock with the given rock ID
		 *
		 * Same as touching a Trigger Rock event.
		 */
		engine->RegisterGlobalFunction("void jjTriggerRock(uint8 id)", asFUNCTION(triggerRock), asCALL_CDECL);

		/**
		 * @brief Ends the level and continues with the given level
		 *
		 * The file name may include the `.j2l` extension. With `warp`, the level is left as through a warp exit. With
		 * `fast`, the transition to the next level is shortened.
		 * @partialsupport A name without an episode is looked for in the current episode only.
		 */
		engine->RegisterGlobalFunction("void jjNxt(const string &in filename, bool warp = false, bool fast = false)", asFUNCTION(cycleTo), asCALL_CDECL);
		/**
		 * @brief Ends the level and continues with the next one
		 *
		 * With `warp`, the level is left as through a warp exit. With `fast`, the transition to the next level is
		 * shortened.
		 */
		engine->RegisterGlobalFunction("void jjNxt(bool warp = false, bool fast = false)", asFUNCTION(jjNxt), asCALL_CDECL);

		engine->RegisterGlobalFunction("bool get_jjEnabledTeams(uint8)", asFUNCTION(getEnabledTeam), asCALL_CDECL);

		/**
		 * @brief Virtual-key code of the key that opens the chat prompt
		 *
		 * @partialsupport Currently has no effect.
		 */
		engine->RegisterGlobalProperty("uint8 jjKeyChat", &ChatKey);
		engine->RegisterGlobalFunction("bool get_jjKey(uint8)", asFUNCTION(getKeyDown), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjMouseX()", asFUNCTION(getCursorX), asCALL_CDECL);
		engine->RegisterGlobalFunction("int get_jjMouseY()", asFUNCTION(getCursorY), asCALL_CDECL);

		/**
		 * @brief Whether a coin warp in single player turns the remaining coins into gems
		 *
		 * `true` by default. Each coin gives a red gem and 100 points.
		 */
		engine->RegisterGlobalProperty("bool jjWarpsTransmuteCoins", &warpsTransmuteCoins);
		/**
		 * @brief Whether crates made by a generator take their parameters from the tile where they land
		 *
		 * Instead of the tile of the generator.
		 * @partialsupport Currently has no effect.
		 */
		engine->RegisterGlobalProperty("bool jjDelayGeneratedCrateOrigins", &delayGeneratedCrateOrigins);
		/**
		 * @brief Whether the background layer moves by its own speeds
		 *
		 * A shortcut for the speed models of `jjLayers[8]`: `false` keeps the layer fixed in the original way of
		 * layer 8, `true` makes it scroll by its own speeds like any other layer. Only a background layer that the level
		 * itself left fixed is affected.
		 */
		engine->RegisterGlobalFunction("bool get_jjUseLayer8Speeds()", asFUNCTION(getUseLayer8Speeds), asCALL_CDECL);
		/** @brief Sets whether the background layer moves by its own speeds */
		engine->RegisterGlobalFunction("bool set_jjUseLayer8Speeds(bool)", asFUNCTION(setUseLayer8Speeds), asCALL_CDECL);

		/**
		 * @brief Whether players may get a sugar rush in this level
		 *
		 * Used by multiplayer servers to detect cheating.
		 * @partialsupport Currently has no effect.
		 */
		engine->RegisterGlobalProperty("bool jjSugarRushAllowed", &g_levelHasFood);
		/** @brief Alternative name of @ref jjSugarRushAllowed */
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
