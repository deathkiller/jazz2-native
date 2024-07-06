#include "EventConverter.h"
#include "JJ2Level.h"
#include "../Actors/Collectibles/FoodCollectible.h"

using FoodType = Jazz2::Actors::Collectibles::FoodType;

namespace Jazz2::Compatibility
{
	EventConverter::EventConverter()
	{
		AddDefaultConverters();
	}

	ConversionResult EventConverter::TryConvert(JJ2Level* level, JJ2Event old, std::uint32_t eventParams) const
	{
		auto it = _converters.find(old);
		if (it != _converters.end()) {
			return it->second(level, eventParams);
		} else {
			return { EventType::Empty };
		}
	}

	void EventConverter::Add(JJ2Event originalEvent, const ConversionFunction& converter)
	{
		if (_converters.contains(originalEvent)) {
			LOGW("Converter for event %u is already defined", (std::uint32_t)originalEvent);
		}

		_converters[originalEvent] = converter;
	}

	void EventConverter::Override(JJ2Event originalEvent, const ConversionFunction& converter)
	{
		_converters[originalEvent] = converter;
	}

	EventConverter::ConversionFunction EventConverter::NoParamList(EventType ev)
	{
		return [ev](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			return { ev };
		};
	}

	EventConverter::ConversionFunction EventConverter::ConstantParamList(EventType ev, const std::array<std::uint8_t, 16>& eventParams)
	{
		return [ev, eventParams](JJ2Level* level, std::uint32_t jj2Params) mutable -> ConversionResult {
			ConversionResult result;
			result.Type = ev;

			std::int32_t i = 0;
			for (auto& param : eventParams) {
				result.Params[i++] = param;
			}

			return result;
		};
	}

	EventConverter::ConversionFunction EventConverter::ParamIntToParamList(EventType ev, const std::array<Pair<std::int32_t, std::int32_t>, 6>& paramDefs)
	{
		return [ev, paramDefs](JJ2Level* level, std::uint32_t jj2Params) mutable -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, arrayView(paramDefs.data(), paramDefs.size()), eventParams);

			ConversionResult result;
			result.Type = ev;
			std::memcpy(result.Params, eventParams, sizeof(result.Params));

			return result;
		};
	}

	void EventConverter::ConvertParamInt(uint32_t paramInt, const ArrayView<const Pair<std::int32_t, std::int32_t>>& paramTypes, std::uint8_t eventParams[16])
	{
		std::int32_t i = 0;
		for (auto& param : paramTypes) {
			if (param.second() == 0) {
				continue;
			}

			switch (param.first()) {
				case JJ2ParamBool: {
					eventParams[i++] = (std::uint8_t)(paramInt & 1);
					paramInt = paramInt >> 1;
					break;
				}
				case JJ2ParamUInt: {
					std::uint32_t value = (paramInt & ((1 << param.second()) - 1));
					if (param.second() > 8) {
						*(std::uint16_t*)&eventParams[i] = (std::uint16_t)value;
						i += 2;
					} else {
						eventParams[i++] = (std::uint8_t)value;
					}
					paramInt = paramInt >> param.second();
					break;
				}
				case JJ2ParamInt: {
					std::uint32_t value = (paramInt & ((1 << param.second()) - 1));

					// Complement of two, with variable bit length
					std::int32_t highestBitValue = (1 << (param.second() - 1));
					if (value >= highestBitValue) {
						value = (uint32_t)(-highestBitValue + (value - highestBitValue));
					}
					if (param.second() > 8) {
						*(std::uint16_t*)&eventParams[i] = (std::uint16_t)value;
						i += 2;
					} else {
						eventParams[i++] = (std::uint8_t)value;
					}
					paramInt = paramInt >> param.second();
					break;
				}

				default:
					ASSERT_MSG(false, "Unknown JJ2Param type specified");
					break;
			}
		}
	}

	void EventConverter::AddDefaultConverters()
	{
		Add(JJ2Event::EMPTY, NoParamList(EventType::Empty));

		// Basic events
		Add(JJ2Event::JAZZ_LEVEL_START, ConstantParamList(EventType::LevelStart, { 0x01 /*Jazz*/ }));
		Add(JJ2Event::SPAZ_LEVEL_START, ConstantParamList(EventType::LevelStart, { 0x02 /*Spaz*/ }));
		Add(JJ2Event::LORI_LEVEL_START, ConstantParamList(EventType::LevelStart, { 0x04 /*Lori*/ }));

		Add(JJ2Event::MP_LEVEL_START, ParamIntToParamList(EventType::LevelStartMultiplayer, {{
			{ JJ2ParamUInt, 2 }		// Team (JJ2+)
		}}));

		Add(JJ2Event::SAVE_POINT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			// Green xmas-themed checkpoints for some levels
			bool isXmas = (level->Tileset.find("xmas"_s) != nullptr);
			std::uint8_t theme = (isXmas ? 1 : 0);
			return { EventType::Checkpoint, { theme } };
		});

		// Scenery events
		Add(JJ2Event::SCENERY_DESTRUCT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 10 },	// Empty
				{ JJ2ParamUInt, 5 },	// Speed
				{ JJ2ParamUInt, 4 }		// Weapon
			}, eventParams);

			if (eventParams[2] > 0) {
				return { EventType::SceneryDestructSpeed, { (std::uint8_t)(eventParams[2] + 4) } };
			} else {
				std::uint16_t weaponMask;
				if (eventParams[3] == 0) {
					// Allow all weapons except Freezer
					weaponMask = UINT16_MAX & ~(1 << (std::uint16_t)WeaponType::Freezer);
				} else {
					weaponMask = (1 << ((std::uint16_t)eventParams[3] - 1));

					// Fixed TNT blocks in `xmas3.j2l` (https://github.com/deathkiller/jazz2/issues/117)
					if (eventParams[3] == 9 && level->LevelName == "xmas3"_s) {
						weaponMask |= (1 << (std::uint16_t)WeaponType::TNT);
					}
				}

				return { EventType::SceneryDestruct, { (std::uint8_t)(weaponMask & 0xff), (std::uint8_t)((weaponMask >> 8) & 0xff) } };
			}
		});
		Add(JJ2Event::SCENERY_DESTR_BOMB, ConstantParamList(EventType::SceneryDestruct, { (std::uint8_t)(1 << (std::int32_t)WeaponType::TNT), 0 }));
		Add(JJ2Event::SCENERY_BUTTSTOMP, NoParamList(EventType::SceneryDestructButtstomp));
		Add(JJ2Event::SCENERY_COLLAPSE, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 10},	// Wait Time
				{ JJ2ParamUInt, 5 }		// FPS
			}, eventParams);

			std::uint16_t waitTime = *(std::uint16_t*)&eventParams[0] * 25;
			return { EventType::SceneryCollapse, { (std::uint8_t)(waitTime & 0xff), (std::uint8_t)((waitTime >> 8) & 0xff), eventParams[1] } };
		});

		// Modifier events
		Add(JJ2Event::MODIFIER_HOOK, NoParamList(EventType::ModifierHook));
		Add(JJ2Event::MODIFIER_ONE_WAY, NoParamList(EventType::ModifierOneWay));
		Add(JJ2Event::MODIFIER_VINE, NoParamList(EventType::ModifierVine));
		Add(JJ2Event::MODIFIER_HURT, ParamIntToParamList(EventType::ModifierHurt, {{
			{ JJ2ParamBool, 1 },	// Up (JJ2+)
			{ JJ2ParamBool, 1 },	// Down (JJ2+)
			{ JJ2ParamBool, 1 },	// Left (JJ2+)
			{ JJ2ParamBool, 1 }		// Right (JJ2+)
		}}));
		Add(JJ2Event::MODIFIER_RICOCHET, NoParamList(EventType::ModifierRicochet));
		Add(JJ2Event::MODIFIER_H_POLE, NoParamList(EventType::ModifierHPole));
		Add(JJ2Event::MODIFIER_V_POLE, NoParamList(EventType::ModifierVPole));
		Add(JJ2Event::MODIFIER_TUBE, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamInt, 7 },   // X Speed
				{ JJ2ParamInt, 7 },   // Y Speed
				{ JJ2ParamUInt, 1 },  // Trig Sample
				{ JJ2ParamBool, 1 },  // BecomeNoclip (JJ2+)
				{ JJ2ParamBool, 1 },  // Noclip Only (JJ2+)
				{ JJ2ParamUInt, 3 }   // Wait Time (JJ2+)
			}, eventParams);

			return { EventType::ModifierTube, { eventParams[0], eventParams[1], eventParams[5], eventParams[2], eventParams[3], eventParams[4] } };
		});

		Add(JJ2Event::MODIFIER_SLIDE, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 2 }
			}, eventParams); // Strength

			return { EventType::ModifierSlide, { eventParams[0] } };
		});

		Add(JJ2Event::MODIFIER_BELT_LEFT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			// TODO: Use single variable
			std::uint8_t left, right;
			if (jj2Params == 0) {
				left = 3;
				right = 0;
			} else if (jj2Params > 127) {
				left = 0;
				right = (std::uint8_t)(256 - jj2Params);
			} else {
				left = (std::uint8_t)jj2Params;
				right = 0;
			}

			return { EventType::AreaHForce, { left, right } };
		});
		Add(JJ2Event::MODIFIER_BELT_RIGHT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			// TODO: Use single variable
			std::uint8_t left, right;
			if (jj2Params == 0) {
				left = 0;
				right = 3;
			} else if (jj2Params > 127) {
				left = (std::uint8_t)(256 - jj2Params);
				right = 0;
			} else {
				left = 0;
				right = (std::uint8_t)jj2Params;
			}

			return { EventType::AreaHForce, { left, right } };
		});
		Add(JJ2Event::MODIFIER_ACC_BELT_LEFT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			if (jj2Params == 0) {
				jj2Params = 3;
			}

			return { EventType::AreaHForce, { 0, 0, (std::uint8_t)jj2Params } };
		});
		Add(JJ2Event::MODIFIER_ACC_BELT_RIGHT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			if (jj2Params == 0) {
				jj2Params = 3;
			}

			return { EventType::AreaHForce, { 0, 0, 0, (std::uint8_t)jj2Params } };
		});

		Add(JJ2Event::MODIFIER_WIND_LEFT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			// TODO: Use single variable
			std::uint8_t left, right;
			if (jj2Params > 127) {
				left = (std::uint8_t)(256 - jj2Params);
				right = 0;
			} else {
				left = 0;
				right = (std::uint8_t)jj2Params;
			}

			return { EventType::AreaHForce, { 0, 0, 0, 0, left, right } };
		});
		Add(JJ2Event::MODIFIER_WIND_RIGHT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			return { EventType::AreaHForce, { 0, 0, 0, 0, 0, (uint8_t)jj2Params } };
		});

		Add(JJ2Event::MODIFIER_SET_WATER, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 8 },	// Height (Tiles)
				{ JJ2ParamBool, 1 },	// Instant [TODO]
				{ JJ2ParamUInt, 2 }		// Lighting [TODO]
			}, eventParams);

			uint16_t waterLevel = eventParams[0] * 32;
			return { EventType::ModifierSetWater, { (std::uint8_t)(waterLevel & 0xff), (std::uint8_t)((waterLevel >> 8) & 0xff), eventParams[1], eventParams[2] } };
		});

		Add(JJ2Event::AREA_LIMIT_X_SCROLL, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 10 },	// Left (Tiles)
				{ JJ2ParamUInt, 10 }	// Width (Tiles)
			}, eventParams);

			std::uint16_t left = *(std::uint16_t*)&eventParams[0];
			std::uint16_t width = *(std::uint16_t*)&eventParams[2];
			return { EventType::ModifierLimitCameraView, { (std::uint8_t)(left & 0xff), (std::uint8_t)((left >> 8) & 0xff), (std::uint8_t)(width & 0xff), (std::uint8_t)((width >> 8) & 0xff) } };
		});

		// Area events
		Add(JJ2Event::AREA_STOP_ENEMY, NoParamList(EventType::AreaStopEnemy));
		Add(JJ2Event::AREA_FLOAT_UP, NoParamList(EventType::AreaFloatUp));
		Add(JJ2Event::AREA_ACTIVATE_BOSS, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 1 }		// Music
			}, eventParams);

			return { EventType::AreaActivateBoss, { 'b', 'o', 's', 's', (std::uint8_t)('1' + eventParams[0]), '.', 'j', '2', 'b', '\0' } };
		});

		Add(JJ2Event::AREA_EOL, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamBool, 1 },	// Secret
				{ JJ2ParamBool, 1 },	// Fast (JJ2+)
				{ JJ2ParamUInt, 4 },	// TextID (JJ2+)
				{ JJ2ParamUInt, 4 }		// Offset (JJ2+)
			}, eventParams);

			if (eventParams[2] != 0) {
				level->AddLevelTokenTextID(eventParams[2]);
			}

			return { EventType::AreaEndOfLevel, { (std::uint8_t)(eventParams[0] == 1 ? 4 : 1), eventParams[1], eventParams[2], eventParams[3] } };
		});
		Add(JJ2Event::AREA_EOL_WARP, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamBool, 1 },	// Empty (JJ2+)
				{ JJ2ParamBool, 1 },	// Fast (JJ2+)
				{ JJ2ParamUInt, 4 },	// TextID (JJ2+)
				{ JJ2ParamUInt, 4 }		// Offset (JJ2+)
			}, eventParams);

			if (eventParams[2] != 0) {
				level->AddLevelTokenTextID(eventParams[2]);
			}

			return { EventType::AreaEndOfLevel, { 2, eventParams[1], eventParams[2], eventParams[3] } };
		});
		Add(JJ2Event::AREA_SECRET_WARP, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 10 },	// Coins
				{ JJ2ParamUInt, 4 },	// TextID (JJ2+)
				{ JJ2ParamUInt, 4 }		// Offset (JJ2+)
			}, eventParams);

			if (eventParams[1] != 0) {
				level->AddLevelTokenTextID(eventParams[2]);
			}

			std::uint16_t coins = *(std::uint16_t*)&eventParams[0];
			return { EventType::AreaEndOfLevel, { 3, 0, eventParams[2], eventParams[3], (std::uint8_t)(coins & 0xff), (std::uint8_t)((coins >> 8) & 0xff) } };
		});

		Add(JJ2Event::EOL_SIGN, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamBool, 1 }	// Secret
			}, eventParams);

			return { EventType::SignEOL, { (std::uint8_t)(eventParams[0] == 1 ? 4 : 1) } };
		});

		Add(JJ2Event::BONUS_SIGN, ConstantParamList(EventType::AreaEndOfLevel, { 3, 0, 0, 0, 0 }));

		Add(JJ2Event::AREA_TEXT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 8 },	// Text
				{ JJ2ParamBool, 1 },	// Vanish
				{ JJ2ParamBool, 1 },	// AngelScript (JJ2+)
				{ JJ2ParamUInt, 8 }		// Offset (JJ2+)
			}, eventParams);

			if (eventParams[2] != 0) {
				return { EventType::AreaCallback, { eventParams[0], eventParams[3], eventParams[1] } };
			} else {
				return { EventType::AreaText, { eventParams[0], eventParams[3], eventParams[1] } };
			}
		});

		Add(JJ2Event::AREA_FLY_OFF, NoParamList(EventType::AreaFlyOff));
		Add(JJ2Event::AREA_REVERT_MORPH, NoParamList(EventType::AreaRevertMorph));
		Add(JJ2Event::AREA_MORPH_FROG, NoParamList(EventType::AreaMorphToFrog));

		Add(JJ2Event::AREA_NO_FIRE, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 2 },	// Set To (JJ2+)
				{ JJ2ParamUInt, 2 }		// Var (JJ2+)
			}, eventParams);

			// TODO: Gravity (1) and Invisibility (2) is not supported yet
			if (eventParams[1] != 0) {
				return { EventType::Empty };
			}
			// TODO: Toggle is not supported yet
			if (eventParams[0] == 3) {
				return { EventType::Empty };
			}

			return { EventType::AreaNoFire, { eventParams[0] } };
		});

		Add(JJ2Event::WATER_BLOCK, ParamIntToParamList(EventType::AreaWaterBlock, {{
			{ JJ2ParamInt, 8 }	// Adjust Y
		}}));

		Add(JJ2Event::SNOW, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 2 },	// Intensity
				{ JJ2ParamBool, 1 },	// Outdoors
				{ JJ2ParamBool, 1 },	// Off
				{ JJ2ParamUInt, 2 }		// Type
			}, eventParams);

			WeatherType weatherType = (eventParams[2] == 0 ? (WeatherType)(eventParams[3] + 1) : WeatherType::None);
			if (weatherType != WeatherType::None && eventParams[1] != 0) {
				weatherType |= WeatherType::OutdoorsOnly;
			}
			std::uint8_t weatherIntensity = (weatherType != WeatherType::None ? (std::uint8_t)((eventParams[0] + 1) * 5 / 3) : 0);

			return { EventType::AreaWeather, { (std::uint8_t)weatherType, weatherIntensity } };
		});

		Add(JJ2Event::AMBIENT_SOUND, ParamIntToParamList(EventType::AreaAmbientSound, {{
			{ JJ2ParamUInt, 8 },		// Sample
			{ JJ2ParamUInt, 8 },		// Amplify
			{ JJ2ParamBool, 1 },		// Fade [TODO]
			{ JJ2ParamBool, 1 }			// Sine [TODO]
		}}));

		Add(JJ2Event::SCENERY_BUBBLER, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 4 }		// Speed
			}, eventParams);

			return { EventType::AreaAmbientBubbles, { (std::uint8_t)((eventParams[0] + 1) * 5 / 3) } };
		});

		// Triggers events
		Add(JJ2Event::TRIGGER_CRATE, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 5 },	// Trigger ID
				{ JJ2ParamBool, 1 },	// Set to (0: on, 1: off)
				{ JJ2ParamBool, 1 }		// Switch
			}, eventParams);

			return { EventType::TriggerCrate, { eventParams[0], (std::uint8_t)(eventParams[1] == 0 ? 1 : 0), eventParams[2] } };
		});
		Add(JJ2Event::TRIGGER_AREA, ParamIntToParamList(EventType::TriggerArea, {{
			{ JJ2ParamUInt, 5 }  // Trigger ID
		}}));
		Add(JJ2Event::TRIGGER_ZONE, ParamIntToParamList(EventType::TriggerZone, {{
			{ JJ2ParamUInt, 5 }, // Trigger ID
			{ JJ2ParamBool, 1 }, // Set to (0: off, 1: on)
			{ JJ2ParamBool, 1 }  // Switch
		}}));

		// Warp events
		Add(JJ2Event::WARP_ORIGIN, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 8 },	// Warp ID
				{ JJ2ParamUInt, 8 },	// Coins
				{ JJ2ParamBool, 1 },	// Set Lap
				{ JJ2ParamBool, 1 },	// Show Anim
				{ JJ2ParamBool, 1 }		// Fast (JJ2+)
			}, eventParams);

			if (eventParams[1] > 0 || eventParams[3] != 0) {
				return { EventType::WarpCoinBonus, { eventParams[0], eventParams[4], eventParams[2], eventParams[1], eventParams[3] } };
			} else {
				return { EventType::WarpOrigin, { eventParams[0], eventParams[4], eventParams[2] } };
			}
		});
		Add(JJ2Event::WARP_TARGET, ParamIntToParamList(EventType::WarpTarget, {{
			{ JJ2ParamUInt, 8 } // Warp ID
		}}));

		// Lights events
		Add(JJ2Event::LIGHT_SET, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 7 },	// Intensity
				{ JJ2ParamUInt, 4 },	// Red
				{ JJ2ParamUInt, 4 },	// Green
				{ JJ2ParamUInt, 4 },	// Blue
				{ JJ2ParamBool, 1 }		// Flicker
			}, eventParams);

			return { EventType::LightAmbient, { (std::uint8_t)std::min(eventParams[0] * 255 / 100, 255), eventParams[1], eventParams[2], eventParams[3], eventParams[4] } };
		});
		Add(JJ2Event::LIGHT_RESET, [](JJ2Level* level, uint32_t jj2Params) -> ConversionResult {
			return { EventType::LightAmbient, { (std::uint8_t)std::min(level->LightingStart * 255 / 64, 255), 255, 255, 255, 0 } };
		});
		Add(JJ2Event::LIGHT_DIM, ConstantParamList(EventType::LightSteady, { 127, 60, 100, 0, 0, 0, 0, 0 }));
		Add(JJ2Event::LIGHT_STEADY, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 3 },	// Type
				{ JJ2ParamUInt, 7 }		// Size
			}, eventParams);

			switch (eventParams[0]) {
				default:
				case 0: { // Normal
					std::uint16_t radiusNear = (std::uint16_t)(eventParams[1] == 0 ? 60 : eventParams[1] * 6);
					std::uint16_t radiusFar = (std::uint16_t)(radiusNear * 1.666f);
					return { EventType::LightSteady, { 255, 10, (std::uint8_t)(radiusNear & 0xff), (std::uint8_t)((radiusNear >> 8) & 0xff), (std::uint8_t)(radiusFar & 0xff), (std::uint8_t)((radiusFar >> 8) & 0xff) } };
				}

				case 1: // Single point (ignores the "Size" parameter)
					return { EventType::LightSteady, { 127, 10, 0, 0, 16, 0 } };

				case 2: // Single point (brighter) (ignores the "Size" parameter)
					return { EventType::LightSteady, { 255, 200, 0, 0, 16, 0 } };

				case 3: { // Flicker light
					std::uint16_t radiusNear = (std::uint16_t)(eventParams[1] == 0 ? 60 : eventParams[1] * 6);
					std::uint16_t radiusFar = (std::uint16_t)(radiusNear * 1.666f);
					return { EventType::LightFlicker, { std::min((std::uint8_t)(110 + eventParams[1] * 2), (std::uint8_t)255), 40, (std::uint8_t)(radiusNear & 0xff), (std::uint8_t)((radiusNear >> 8) & 0xff), (std::uint8_t)(radiusFar & 0xff), (std::uint8_t)((radiusFar >> 8) & 0xff) } };
				}

				case 4: { // Bright normal light
					std::uint16_t radiusNear = (std::uint16_t)(eventParams[1] == 0 ? 80 : eventParams[1] * 7);
					std::uint16_t radiusFar = (std::uint16_t)(radiusNear * 1.25f);
					return { EventType::LightSteady, { 255, 200, (std::uint8_t)(radiusNear & 0xff), (std::uint8_t)((radiusNear >> 8) & 0xff), (std::uint8_t)(radiusFar & 0xff), (std::uint8_t)((radiusFar >> 8) & 0xff) } };
				}

				case 5: { // Laser shield/Illuminate Surroundings
					return { EventType::LightIlluminate, { (std::uint8_t)(eventParams[1] < 1 ? 1 : eventParams[1]) } };
				}

				case 6: // Ring of light
					// TODO: JJ2+ Extension
					return { EventType::Empty };

				case 7: // Ring of light 2
					// TODO: JJ2+ Extension
					return { EventType::Empty };
			}
		});
		Add(JJ2Event::LIGHT_PULSE, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 8 },	// Speed
				{ JJ2ParamUInt, 4 },	// Sync
				{ JJ2ParamUInt, 3 },	// Type
				{ JJ2ParamUInt, 5 }		// Size
			}, eventParams);

			std::uint16_t radiusNear1 = (std::uint16_t)(eventParams[3] == 0 ? 20 : eventParams[3] * 4.8f);
			std::uint16_t radiusNear2 = (std::uint16_t)(radiusNear1 * 2);
			std::uint16_t radiusFar = (std::uint16_t)(radiusNear1 * 2.4f);

			std::uint8_t speed = (std::uint8_t)(eventParams[0] == 0 ? 6 : eventParams[0]); // Quickfix for Tube2.j2l to look better
			std::uint8_t sync = eventParams[1];

			switch (eventParams[2]) {
				default:
				case 0: { // Normal
					return { EventType::LightPulse, { 255, 10, (std::uint8_t)(radiusNear1 & 0xff), (std::uint8_t)((radiusNear1 >> 8) & 0xff), (std::uint8_t)(radiusNear2 & 0xff), (std::uint8_t)((radiusNear2 >> 8) & 0xff), (std::uint8_t)(radiusFar & 0xff), (std::uint8_t)((radiusFar >> 8) & 0xff), speed, sync } };
				}
				case 4: { // Bright normal light
					return { EventType::LightPulse, { 255, 200, (std::uint8_t)(radiusNear1 & 0xff), (std::uint8_t)((radiusNear1 >> 8) & 0xff), (std::uint8_t)(radiusNear2 & 0xff), (std::uint8_t)((radiusNear2 >> 8) & 0xff), (std::uint8_t)(radiusFar & 0xff), (std::uint8_t)((radiusFar >> 8) & 0xff), speed, sync } };
				}
				case 5: { // Laser shield/Illuminate Surroundings
					// TODO: Not pulsating yet
					return { EventType::LightIlluminate, { (std::uint8_t)(eventParams[1] < 1 ? 1 : eventParams[1]) } };
				}
			}
		});
		Add(JJ2Event::LIGHT_FLICKER, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 8 }		// Sample (not used)
			}, eventParams);

			return { EventType::LightFlicker, { 110, 40, 60, 0, 110, 0 } };
		});

		// Environment events
		Add(JJ2Event::PUSHABLE_ROCK, ConstantParamList(EventType::PushableBox, { 0 }));
		Add(JJ2Event::PUSHABLE_BOX, ConstantParamList(EventType::PushableBox, { 1 }));

		Add(JJ2Event::PLATFORM_FRUIT, GetPlatformConverter(1));
		Add(JJ2Event::PLATFORM_BOLL, GetPlatformConverter(2));
		Add(JJ2Event::PLATFORM_GRASS, GetPlatformConverter(3));
		Add(JJ2Event::PLATFORM_PINK, GetPlatformConverter(4));
		Add(JJ2Event::PLATFORM_SONIC, GetPlatformConverter(5));
		Add(JJ2Event::PLATFORM_SPIKE, GetPlatformConverter(6));
		Add(JJ2Event::BOLL_SPIKE, GetPlatformConverter(7));

		Add(JJ2Event::BOLL_SPIKE_3D, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 2 },	// Sync
				{ JJ2ParamInt, 6 },		// Speed
				{ JJ2ParamUInt, 4 },	// Length
				{ JJ2ParamBool, 1 },	// Swing
				{ JJ2ParamBool, 1 }		// Shade
			}, eventParams);

			return { EventType::SpikeBall, { eventParams[0], eventParams[1], eventParams[2], eventParams[3], eventParams[4] } };
		});

		Add(JJ2Event::SPRING_RED, GetSpringConverter(0 /*Red*/, false, false));
		Add(JJ2Event::SPRING_GREEN, GetSpringConverter(1 /*Green*/, false, false));
		Add(JJ2Event::SPRING_BLUE, GetSpringConverter(2 /*Blue*/, false, false));
		Add(JJ2Event::SPRING_RED_HOR, GetSpringConverter(0 /*Red*/, true, false));
		Add(JJ2Event::SPRING_GREEN_HOR, GetSpringConverter(1 /*Green*/, true, false));
		Add(JJ2Event::SPRING_BLUE_HOR, GetSpringConverter(2 /*Blue*/, true, false));
		Add(JJ2Event::SPRING_GREEN_FROZEN, GetSpringConverter(1 /*Green*/, false, true));

		Add(JJ2Event::BRIDGE, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 4 },	// Width
				{ JJ2ParamUInt, 3 },	// Type
				{ JJ2ParamUInt, 4 }		// Toughness
			}, eventParams);

			std::uint16_t width = (eventParams[0] * 2);
			return { EventType::Bridge, { (std::uint8_t)(width & 0xff), (std::uint8_t)((width >> 8) & 0xff), eventParams[1], eventParams[2] } };
		});

		Add(JJ2Event::POLE_CARROTUS, GetPoleConverter(0));
		Add(JJ2Event::POLE_DIAMONDUS, GetPoleConverter(1));
		Add(JJ2Event::SMALL_TREE, GetPoleConverter(2));
		Add(JJ2Event::POLE_JUNGLE, GetPoleConverter(3));
		Add(JJ2Event::POLE_PSYCH, GetPoleConverter(4));

		Add(JJ2Event::ROTATING_ROCK, ParamIntToParamList(EventType::RollingRock, {{
			{ JJ2ParamUInt, 8 },	// ID
			{ JJ2ParamInt, 4 },		// X-Speed
			{ JJ2ParamInt, 4 }		// Y-Speed
		}}));

		Add(JJ2Event::TRIGGER_ROCK, ParamIntToParamList(EventType::RollingRockTrigger, {{
			{ JJ2ParamUInt, 8 }	// ID
		}}));

		Add(JJ2Event::SWINGING_VINE, NoParamList(EventType::SwingingVine));

		// Enemies
		Add(JJ2Event::ENEMY_TURTLE_NORMAL, ConstantParamList(EventType::EnemyTurtle, { 0 }));
		Add(JJ2Event::ENEMY_NORMAL_TURTLE_XMAS, ConstantParamList(EventType::EnemyTurtle, { 1 }));
		Add(JJ2Event::ENEMY_LIZARD, ConstantParamList(EventType::EnemyLizard, { 0 }));
		Add(JJ2Event::ENEMY_LIZARD_XMAS, ConstantParamList(EventType::EnemyLizard, { 1 }));
		Add(JJ2Event::ENEMY_LIZARD_FLOAT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 8 },	// Duration
				{ JJ2ParamBool, 1 }		// Fly Away
			}, eventParams);

			return { EventType::EnemyLizardFloat, { 0, eventParams[0], eventParams[1] } };
		});
		Add(JJ2Event::ENEMY_LIZARD_FLOAT_XMAS, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 8 },	// Duration
				{ JJ2ParamBool, 1 }		// Fly Away
			}, eventParams);

			return { EventType::EnemyLizardFloat, { 1, eventParams[0], eventParams[1] } };
		});
		Add(JJ2Event::ENEMY_DRAGON, NoParamList(EventType::EnemyDragon));
		Add(JJ2Event::ENEMY_LAB_RAT, NoParamList(EventType::EnemyLabRat));
		Add(JJ2Event::ENEMY_SUCKER_FLOAT, NoParamList(EventType::EnemySuckerFloat));
		Add(JJ2Event::ENEMY_SUCKER, NoParamList(EventType::EnemySucker));
		Add(JJ2Event::ENEMY_HELMUT, NoParamList(EventType::EnemyHelmut));
		Add(JJ2Event::ENEMY_BAT, NoParamList(EventType::EnemyBat));
		Add(JJ2Event::ENEMY_FAT_CHICK, NoParamList(EventType::EnemyFatChick));
		Add(JJ2Event::ENEMY_FENCER, NoParamList(EventType::EnemyFencer));
		Add(JJ2Event::ENEMY_RAPIER, NoParamList(EventType::EnemyRapier));
		Add(JJ2Event::ENEMY_SPARKS, NoParamList(EventType::EnemySparks));
		Add(JJ2Event::ENEMY_MONKEY, ConstantParamList(EventType::EnemyMonkey, { 1 }));
		Add(JJ2Event::ENEMY_MONKEY_STAND, ConstantParamList(EventType::EnemyMonkey, { 0 }));
		Add(JJ2Event::ENEMY_DEMON, NoParamList(EventType::EnemyDemon));
		Add(JJ2Event::ENEMY_BEE, NoParamList(EventType::EnemyBee));
		Add(JJ2Event::ENEMY_BEE_SWARM, NoParamList(EventType::EnemyBeeSwarm));
		Add(JJ2Event::ENEMY_CATERPILLAR, NoParamList(EventType::EnemyCaterpillar));
		Add(JJ2Event::ENEMY_CRAB, NoParamList(EventType::EnemyCrab));
		Add(JJ2Event::ENEMY_DOGGY_DOGG, ConstantParamList(EventType::EnemyDoggy, { 0 }));
		Add(JJ2Event::EMPTY_TSF_DOG, ConstantParamList(EventType::EnemyDoggy, { 1 }));
		Add(JJ2Event::ENEMY_DRAGONFLY, NoParamList(EventType::EnemyDragonfly));
		Add(JJ2Event::ENEMY_FISH, NoParamList(EventType::EnemyFish));
		Add(JJ2Event::ENEMY_MADDER_HATTER, NoParamList(EventType::EnemyMadderHatter));
		Add(JJ2Event::ENEMY_RAVEN, NoParamList(EventType::EnemyRaven));
		Add(JJ2Event::ENEMY_SKELETON, NoParamList(EventType::EnemySkeleton));
		Add(JJ2Event::ENEMY_TUF_TURT, NoParamList(EventType::EnemyTurtleTough));
		Add(JJ2Event::ENEMY_TURTLE_TUBE, NoParamList(EventType::EnemyTurtleTube));
		Add(JJ2Event::ENEMY_WITCH, NoParamList(EventType::EnemyWitch));

		Add(JJ2Event::TURTLE_SHELL, NoParamList(EventType::TurtleShell));

		// Bosses
		Add(JJ2Event::BOSS_TWEEDLE, GetBossConverter(EventType::BossTweedle));
		Add(JJ2Event::BOSS_BILSY, GetBossConverter(EventType::BossBilsy, { 0 }));
		Add(JJ2Event::EMPTY_BOSS_BILSY_XMAS, GetBossConverter(EventType::BossBilsy, { 1 }));
		Add(JJ2Event::BOSS_DEVAN_DEVIL, GetBossConverter(EventType::BossDevan));
		Add(JJ2Event::BOSS_ROBOT, NoParamList(EventType::BossRobot));
		Add(JJ2Event::BOSS_QUEEN, GetBossConverter(EventType::BossQueen));
		Add(JJ2Event::BOSS_UTERUS, GetBossConverter(EventType::BossUterus));
		Add(JJ2Event::BOSS_BUBBA, GetBossConverter(EventType::BossBubba));
		Add(JJ2Event::BOSS_TUF_TURT, GetBossConverter(EventType::BossTurtleTough));
		Add(JJ2Event::BOSS_DEVAN_ROBOT, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 4 },	// IntroText
				{ JJ2ParamUInt, 4 }		// EndText
			}, eventParams);

			return { EventType::BossDevanRemote, { 0, eventParams[0], eventParams[1] } };
		});
		Add(JJ2Event::BOSS_BOLLY, GetBossConverter(EventType::BossBolly));

		// Collectibles
		Add(JJ2Event::COIN_SILVER, ConstantParamList(EventType::Coin, { 0 }));
		Add(JJ2Event::COIN_GOLD, ConstantParamList(EventType::Coin, { 1 }));

		Add(JJ2Event::GEM_RED, ConstantParamList(EventType::Gem, { 0 }));
		Add(JJ2Event::GEM_GREEN, ConstantParamList(EventType::Gem, { 1 }));
		Add(JJ2Event::GEM_BLUE, ConstantParamList(EventType::Gem, { 2 }));
		Add(JJ2Event::GEM_PURPLE, ConstantParamList(EventType::Gem, { 3 }));

		Add(JJ2Event::GEM_RED_RECT, ConstantParamList(EventType::Gem, { 0 }));
		Add(JJ2Event::GEM_GREEN_RECT, ConstantParamList(EventType::Gem, { 1 }));
		Add(JJ2Event::GEM_BLUE_RECT, ConstantParamList(EventType::Gem, { 2 }));

		Add(JJ2Event::GEM_SUPER, NoParamList(EventType::GemGiant));
		Add(JJ2Event::GEM_RING, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 5 },	// Length
				{ JJ2ParamUInt, 5 },	// Speed
				{ JJ2ParamBool, 8 }		// Event
			}, eventParams);

			return { EventType::GemRing, { eventParams[0], eventParams[1] } };
		});

		Add(JJ2Event::SCENERY_GEMSTOMP, NoParamList(EventType::GemStomp));

		Add(JJ2Event::CARROT, ConstantParamList(EventType::Carrot, { 0 }));
		Add(JJ2Event::CARROT_FULL, ConstantParamList(EventType::Carrot, { 1 }));
		Add(JJ2Event::CARROT_FLY, NoParamList(EventType::CarrotFly));
		Add(JJ2Event::CARROT_INVINCIBLE, NoParamList(EventType::CarrotInvincible));
		Add(JJ2Event::ONEUP, NoParamList(EventType::OneUp));

		Add(JJ2Event::AMMO_BOUNCER, ConstantParamList(EventType::Ammo, { (std::uint8_t)WeaponType::Bouncer }));
		Add(JJ2Event::AMMO_FREEZER, ConstantParamList(EventType::Ammo, { (std::uint8_t)WeaponType::Freezer }));
		Add(JJ2Event::AMMO_SEEKER, ConstantParamList(EventType::Ammo, { (std::uint8_t)WeaponType::Seeker }));
		Add(JJ2Event::AMMO_RF, ConstantParamList(EventType::Ammo, { (std::uint8_t)WeaponType::RF }));
		Add(JJ2Event::AMMO_TOASTER, ConstantParamList(EventType::Ammo, { (std::uint8_t)WeaponType::Toaster }));
		Add(JJ2Event::AMMO_TNT, ConstantParamList(EventType::Ammo, { (std::uint8_t)WeaponType::TNT }));
		Add(JJ2Event::AMMO_PEPPER, ConstantParamList(EventType::Ammo, { (std::uint8_t)WeaponType::Pepper }));
		Add(JJ2Event::AMMO_ELECTRO, ConstantParamList(EventType::Ammo, { (std::uint8_t)WeaponType::Electro }));

		Add(JJ2Event::FAST_FIRE, NoParamList(EventType::FastFire));
		Add(JJ2Event::POWERUP_BLASTER, ConstantParamList(EventType::PowerUpWeapon, { (std::uint8_t)WeaponType::Blaster }));
		Add(JJ2Event::POWERUP_BOUNCER, ConstantParamList(EventType::PowerUpWeapon, { (std::uint8_t)WeaponType::Bouncer }));
		Add(JJ2Event::POWERUP_FREEZER, ConstantParamList(EventType::PowerUpWeapon, { (std::uint8_t)WeaponType::Freezer }));
		Add(JJ2Event::POWERUP_SEEKER, ConstantParamList(EventType::PowerUpWeapon, { (std::uint8_t)WeaponType::Seeker }));
		Add(JJ2Event::POWERUP_RF, ConstantParamList(EventType::PowerUpWeapon, { (std::uint8_t)WeaponType::RF }));
		Add(JJ2Event::POWERUP_TOASTER, ConstantParamList(EventType::PowerUpWeapon, { (std::uint8_t)WeaponType::Toaster }));
		Add(JJ2Event::POWERUP_TNT, ConstantParamList(EventType::PowerUpWeapon, { (std::uint8_t)WeaponType::TNT }));
		Add(JJ2Event::POWERUP_PEPPER, ConstantParamList(EventType::PowerUpWeapon, { (std::uint8_t)WeaponType::Pepper }));
		Add(JJ2Event::POWERUP_ELECTRO, ConstantParamList(EventType::PowerUpWeapon, { (std::uint8_t)WeaponType::Electro }));

		Add(JJ2Event::FOOD_APPLE, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Apple }));
		Add(JJ2Event::FOOD_BANANA, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Banana }));
		Add(JJ2Event::FOOD_CHERRY, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Cherry }));
		Add(JJ2Event::FOOD_ORANGE, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Orange }));
		Add(JJ2Event::FOOD_PEAR, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Pear }));
		Add(JJ2Event::FOOD_PRETZEL, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Pretzel }));
		Add(JJ2Event::FOOD_STRAWBERRY, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Strawberry }));
		Add(JJ2Event::FOOD_LEMON, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Lemon }));
		Add(JJ2Event::FOOD_LIME, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Lime }));
		Add(JJ2Event::FOOD_THING, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Thing }));
		Add(JJ2Event::FOOD_WATERMELON, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::WaterMelon }));
		Add(JJ2Event::FOOD_PEACH, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Peach }));
		Add(JJ2Event::FOOD_GRAPES, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Grapes }));
		Add(JJ2Event::FOOD_LETTUCE, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Lettuce }));
		Add(JJ2Event::FOOD_EGGPLANT, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Eggplant }));
		Add(JJ2Event::FOOD_CUCUMBER, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Cucumber }));
		Add(JJ2Event::FOOD_PEPSI, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Pepsi }));
		Add(JJ2Event::FOOD_COKE, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Coke }));
		Add(JJ2Event::FOOD_MILK, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Milk }));
		Add(JJ2Event::FOOD_PIE, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Pie }));
		Add(JJ2Event::FOOD_CAKE, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Cake }));
		Add(JJ2Event::FOOD_DONUT, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Donut }));
		Add(JJ2Event::FOOD_CUPCAKE, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Cupcake }));
		Add(JJ2Event::FOOD_CHIPS, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Chips }));
		Add(JJ2Event::FOOD_CANDY, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Candy }));
		Add(JJ2Event::FOOD_CHOCOLATE, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Chocolate }));
		Add(JJ2Event::FOOD_ICE_CREAM, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::IceCream }));
		Add(JJ2Event::FOOD_BURGER, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Burger }));
		Add(JJ2Event::FOOD_PIZZA, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Pizza }));
		Add(JJ2Event::FOOD_FRIES, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Fries }));
		Add(JJ2Event::FOOD_CHICKEN_LEG, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::ChickenLeg }));
		Add(JJ2Event::FOOD_SANDWICH, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Sandwich }));
		Add(JJ2Event::FOOD_TACO, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Taco }));
		Add(JJ2Event::FOOD_HOT_DOG, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::HotDog }));
		Add(JJ2Event::FOOD_HAM, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Ham }));
		Add(JJ2Event::FOOD_CHEESE, ConstantParamList(EventType::Food, { (std::uint8_t)FoodType::Cheese }));

		Add(JJ2Event::CRATE_AMMO, GetAmmoCrateConverter(0));
		Add(JJ2Event::CRATE_AMMO_BOUNCER, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 3 }		// Weapon
			}, eventParams);

			std::uint8_t type = eventParams[0] + 1;
			if (type < 1 || type > 8) {
				type = 1;	// Fallback to Bouncer if out of range
			}

			return { EventType::CrateAmmo, { type } };
		});
		Add(JJ2Event::CRATE_AMMO_FREEZER, GetAmmoCrateConverter(2));
		Add(JJ2Event::CRATE_AMMO_SEEKER, GetAmmoCrateConverter(3));
		Add(JJ2Event::CRATE_AMMO_RF, GetAmmoCrateConverter(4));
		Add(JJ2Event::CRATE_AMMO_TOASTER, GetAmmoCrateConverter(5));
		Add(JJ2Event::CRATE_CARROT, ConstantParamList(EventType::Crate, { (std::uint8_t)EventType::Carrot, (std::uint8_t)((std::int32_t)EventType::Carrot >> 8), 1 }));
		Add(JJ2Event::CRATE_SPRING, ConstantParamList(EventType::Crate, { (std::uint8_t)EventType::Spring, (std::uint8_t)((std::int32_t)EventType::Spring >> 8), 1, 1 }));
		Add(JJ2Event::CRATE_ONEUP, ConstantParamList(EventType::Crate, { (std::uint8_t)EventType::OneUp, (std::uint8_t)((std::int32_t)EventType::OneUp >> 8), 1 }));
		Add(JJ2Event::CRATE_BOMB, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 8 },	// ExtraEvent
				{ JJ2ParamUInt, 4 },	// NumEvent
				{ JJ2ParamBool, 1 },	// RandomFly
				{ JJ2ParamBool, 1 }		// NoBomb
			}, eventParams);

			// TODO: Implement RandomFly parameter

			if (eventParams[0] > 0 && eventParams[1] > 0) {
				// TODO: Convert ExtraEvent
				//return { EventType::Crate, { (uint8_t)(eventParams[0] & 0xff), (uint8_t)((eventParams[0] >> 8) & 0xff), eventParams[1] } };
				return { EventType::Crate };
			} else if (eventParams[3] == 0) {
				return { EventType::Crate, { (std::uint8_t)EventType::Bomb, (std::uint8_t)((std::int32_t)EventType::Bomb >> 8), 1 } };
			} else {
				return { EventType::Crate };
			}
		});
		Add(JJ2Event::BARREL_AMMO, NoParamList(EventType::BarrelAmmo));
		Add(JJ2Event::BARREL_CARROT, ConstantParamList(EventType::Barrel, { (std::uint8_t)EventType::Carrot, (std::uint8_t)((std::int32_t)EventType::Carrot >> 8), 1 }));
		Add(JJ2Event::BARREL_ONEUP, ConstantParamList(EventType::Barrel, { (std::uint8_t)EventType::OneUp, (std::uint8_t)((std::int32_t)EventType::OneUp >> 8), 1 }));
		Add(JJ2Event::CRATE_GEM, ParamIntToParamList(EventType::CrateGem, {{
			{ JJ2ParamUInt, 4 },		// Red
			{ JJ2ParamUInt, 4 },		// Green
			{ JJ2ParamUInt, 4 },		// Blue
			{ JJ2ParamUInt, 4 }			// Purple
		}}));
		Add(JJ2Event::BARREL_GEM, ParamIntToParamList(EventType::BarrelGem, {{
			{ JJ2ParamUInt, 4 },		// Red
			{ JJ2ParamUInt, 4 },		// Green
			{ JJ2ParamUInt, 4 },		// Blue
			{ JJ2ParamUInt, 4 }			// Purple
		}}));

		Add(JJ2Event::POWERUP_SWAP, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			if (level->GetVersion() == JJ2Version::TSF || level->GetVersion() == JJ2Version::CC) {
				return { EventType::PowerUpMorph, { 1 } };
			} else {
				return { EventType::PowerUpMorph, { 0 } };
			}
		});

		Add(JJ2Event::POWERUP_BIRD, ConstantParamList(EventType::PowerUpMorph, { 2 }));

		Add(JJ2Event::BIRDY, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamBool, 1 }		// Chuck (Yellow)
			}, eventParams);

			return { EventType::BirdCage, { eventParams[0] } };
		});

		// Misc events
		Add(JJ2Event::EVA, NoParamList(EventType::Eva));
		Add(JJ2Event::MOTH, ParamIntToParamList(EventType::Moth, {{
			{ JJ2ParamUInt, 3 }
		}}));
		Add(JJ2Event::STEAM, NoParamList(EventType::SteamNote));
		Add(JJ2Event::SCENERY_BOMB, NoParamList(EventType::Bomb));
		Add(JJ2Event::PINBALL_BUMP_500, ConstantParamList(EventType::PinballBumper, { 0 }));
		Add(JJ2Event::PINBALL_BUMP_CARROT, ConstantParamList(EventType::PinballBumper, { 1 }));
		Add(JJ2Event::PINBALL_PADDLE_L, ConstantParamList(EventType::PinballPaddle, { 0 }));
		Add(JJ2Event::PINBALL_PADDLE_R, ConstantParamList(EventType::PinballPaddle, { 1 }));

		Add(JJ2Event::AIRBOARD, [](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 5 }		// Delay (Secs.) - Default: 30
			}, eventParams); 

			return { EventType::AirboardGenerator, { (std::uint8_t)(eventParams[0] == 0 ? 30 : eventParams[0]) } };
		});

		Add(JJ2Event::COPTER, NoParamList(EventType::Copter));

		Add(JJ2Event::CTF_BASE, ParamIntToParamList(EventType::CtfBase, {{
			{ JJ2ParamUInt, 1 },		// Team
			{ JJ2ParamUInt, 1 }			// Direction
		}}));

		Add(JJ2Event::SHIELD_FIRE, ConstantParamList(EventType::PowerUpShield, { 1 }));
		Add(JJ2Event::SHIELD_WATER, ConstantParamList(EventType::PowerUpShield, { 2 }));
		Add(JJ2Event::SHIELD_LIGHTNING, ConstantParamList(EventType::PowerUpShield, { 3 }));
		Add(JJ2Event::SHIELD_LASER, ConstantParamList(EventType::PowerUpShield, { 4 }));
		Add(JJ2Event::STOPWATCH, NoParamList(EventType::Stopwatch));
	}

	EventConverter::ConversionFunction EventConverter::GetSpringConverter(std::uint8_t type, bool horizontal, bool frozen)
	{
		return [type, horizontal, frozen](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamBool, 1 },	// Orientation (vertical only)
				{ JJ2ParamBool, 1 },	// Keep X Speed (vertical only)
				{ JJ2ParamBool, 1 },	// Keep Y Speed
				{ JJ2ParamUInt, 4 },	// Delay
				{ JJ2ParamBool, 1 }		// Reverse (horzontal only, JJ2+)
			}, eventParams);

			return { EventType::Spring, { type, (std::uint8_t)(horizontal ? (eventParams[4] != 0 ? 5 : 4) : eventParams[0] * 2), eventParams[1], eventParams[2], eventParams[3], (std::uint8_t)(frozen ? 1 : 0) } };
		};
	}

	EventConverter::ConversionFunction EventConverter::GetPlatformConverter(std::uint8_t type)
	{
		return [type](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 2 },	// Sync
				{ JJ2ParamInt, 6 },		// Speed
				{ JJ2ParamUInt, 4 },	// Length
				{ JJ2ParamBool, 1 }		// Swing
			}, eventParams);

			return { EventType::MovingPlatform, { type, eventParams[0], eventParams[1], eventParams[2], eventParams[3] } };
		};	
	}

	EventConverter::ConversionFunction EventConverter::GetPoleConverter(std::uint8_t theme)
	{
		return [theme](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 5 },	// Adjust Y
				{ JJ2ParamInt, 6 }		// Adjust X
			}, eventParams);

			constexpr std::int32_t AdjustX = 2;
			constexpr std::int32_t AdjustY = 2;

			std::uint16_t x = (std::uint16_t)((std::int8_t)eventParams[1] + 16 - AdjustX);
			std::uint16_t y = (std::uint16_t)((eventParams[0] == 0 ? 24 : eventParams[0]) - AdjustY);
			return { EventType::Pole, { theme, (std::uint8_t)(x & 0xff), (std::uint8_t)((x >> 8) & 0xff), (std::uint8_t)(y & 0xff), (std::uint8_t)((y >> 8) & 0xff) } };
		};
	}

	EventConverter::ConversionFunction EventConverter::GetAmmoCrateConverter(std::uint8_t type)
	{
		return [type](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			return { EventType::CrateAmmo, { type } };
		};
	}

	EventConverter::ConversionFunction EventConverter::GetBossConverter(EventType ev, std::uint8_t customParam)
	{
		return [ev, customParam](JJ2Level* level, std::uint32_t jj2Params) -> ConversionResult {
			std::uint8_t eventParams[16];
			ConvertParamInt(jj2Params, {
				{ JJ2ParamUInt, 4 }		// EndText
			}, eventParams);

			return { ev, { customParam, eventParams[0] } };
		};
	}
}