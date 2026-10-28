#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "ScriptLoader.h"
#include "Legacy/Definitions.h"
#include "../ILevelHandler.h"
#include "../Formats/LevelFile.h"
#include "../Input/ControlScheme.h"
#include "../Formats/TileSetFile.h"
#include "../../nCine/Base/BitArray.h"

namespace Jazz2::UI
{
	class Canvas;
	class HUD;
}

namespace nCine
{
	class AudioBuffer;
	class Texture;
}

namespace Jazz2::Compatibility
{
	class AnimSetMapping;
}

namespace Jazz2::Resources
{
	struct GenericGraphicResource;
}

namespace Jazz2::Tiles
{
	struct LayerDescription;
	struct TileMapLayer;
}

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		class jjPLAYER;
		class EventTranslator;
		class ScriptWorldCanvas;
		class ScriptLayerCanvas;
	}

	/**
		@brief Specifies a part to be drawn in @ref LevelScriptLoader::OnDraw()
		
		Identifies an individual HUD element a script may override or augment while drawing, such as the
		weapon ammo, health, lives, player timer, score or the game mode specific overlay. It is passed
		to the draw callback so the script can render or customize that particular part.
	*/
	enum class DrawType
	{
		WeaponAmmo,
		Health,
		Lives,
		PlayerTimer,
		Score,
		GameModeHUD
	};

	/**
		@brief Binds an **AngelScript** script file to @ref LevelHandler and executes the script
		
		@experimental
	*/
	class LevelScriptLoader : public ScriptLoader
	{
		friend class Legacy::jjPLAYER;
		friend class Legacy::jjPAL;
		friend class Legacy::jjLAYER;
		friend class Legacy::jjTILE;
		friend class Legacy::jjPIXELMAP;
		friend class Legacy::jjCOLORMAP;

	public:
		/**
		 * @brief Creates a new instance and binds the specified script to the level handler
		 *
		 * @param levelHandler  Level handler the script is bound to
		 * @param scriptPath    Path to the script file to load
		 */
		LevelScriptLoader(LevelHandler* levelHandler, StringView scriptPath);
		~LevelScriptLoader();

		/** @brief Returns the level handler this script is bound to */
		LevelHandler* GetLevelHandler() const {
			return _levelHandler;
		}

		/**
		 * @brief Plays a sound sample by its original sample index (see `SOUND::Sample`)
		 *
		 * Loads and caches the sample on first use. Returns the player, or `nullptr` if the sample couldn't be played.
		 */
		std::shared_ptr<AudioBufferPlayer> PlaySample(const Vector2f& pos, std::int32_t sampleId, std::int32_t volume, std::int32_t frequency, bool sourceRelative);
		/**
		 * @brief Keeps a looped sample playing on a channel for this tick, as `jjSampleLooped()`
		 *
		 * As JJ2+ has it, channel 0 starts it on a new channel, which is returned - the script passes it again in every tick
		 * it wants to hear it (where the object is now), a loop no script played in a tick stops.
		 */
		std::int32_t PlayLoopedSample(const Vector2f& pos, std::int32_t sampleId, std::int32_t channel, std::int32_t volume, std::int32_t frequency);
		/** @brief Returns `true` if the given built-in sample is available (loading it on first query) */
		bool IsSampleLoaded(std::int32_t sampleId);
		/** @brief Loads a custom sample from the given content path, overriding the sample at the given index */
		bool LoadSample(std::int32_t sampleId, StringView path);

		/** @brief Returns `true` if the game is converted from version 1.24 (The Secret Files), which has more animation sets than 1.23 */
		bool IsTSF() const {
			return versionTSF;
		}

		/**
		 * @brief Returns the animation tables of the script, `jjAnimSets`, `jjAnimations` and `jjAnimFrames`
		 *
		 * Made on first use. The sprites scripts draw come from there, see @ref Legacy::AnimationTables.
		 */
		Legacy::AnimationTables& GetAnimationTables();

		/**
		 * @brief Returns the event of a tile as the original stores it, see @ref Legacy::EventTranslator
		 *
		 * The event ID in the low 8 bits, the parameters from bit 12. A word a script wrote by @ref SetOriginalEvent()
		 * stays as it was written, as long as the event of the tile isn't changed otherwise.
		 */
		std::uint32_t GetOriginalEvent(std::int32_t x, std::int32_t y);
		/** @brief Changes the event of a tile to one of the original, as `jjEventSet()` and `jjParameterSet()` do */
		void SetOriginalEvent(std::int32_t x, std::int32_t y, std::uint32_t eventWord);

		/**
		 * @brief Spawns an object from an original JJ2 event ID at the given pixel position
		 *
		 * Converts the event to a native type, registers it, and returns a script object ID (or 0 on failure). Used by
		 * `jjAddObject`.
		 */
		std::int32_t AddObjectFromEvent(std::uint8_t eventId, float xPixel, float yPixel);
		/**
		 * @brief Returns the live actor for a script object ID previously returned by @ref AddObjectFromEvent
		 *
		 * Returns `nullptr` if it no longer exists. Used by `jjObjects`.
		 */
		Actors::ActorBase* GetScriptObject(std::int32_t objectId);
		/** @brief Returns the tick of the scripts the object got its object ID in, 0 if the ID isn't given */
		std::int32_t GetObjectSpawnTick(std::int32_t objectId) const;
		/** @brief Returns the translator between the events of the game and those of the original, made on first use */
		Legacy::EventTranslator& GetEventTranslator();

		/**
		 * @brief Called when an object is added to the level
		 *
		 * An object of an event gets an object ID (`jjObjects`), the event of the original it comes from is worked out
		 * by @ref Legacy::EventTranslator. If a script gave the template of that event (`jjObjectPresets`) a behavior of
		 * its own, the behavior drives the object (see @ref Legacy::ScriptObjectHost).
		 */
		void OnActorAdded(const std::shared_ptr<Actors::ActorBase>& actor);
		/** @brief Returns the template of the objects of an event of the original (`jjObjectPresets`), AddRef'd */
		Legacy::jjOBJ* GetObjectPreset(std::uint8_t eventId);
		/**
		 * @brief Fires the bullets of a weapon whose bullets a script controls as JJ2+ does, `false` if a script doesn't control them
		 *
		 * The bullets go by `SPREAD::Spread` of the weapon, the behavior of the script moves them then (see
		 * @ref Legacy::ScriptObjectHost::RunBullet()).
		 */
		bool FirePlayerWeapon(Actors::Player* player, WeaponType weaponType);
		/**
		 * @brief Returns `true` if the scripts make the weather of the level, the level doesn't draw it then
		 *
		 * The weather of JJ2+ is of particles scripts see (`jjParticles`), they steer or remove them.
		 */
		bool OwnsWeather() const;
		/** @brief Returns `true` if tapping fire doesn't fire a weapon faster (`jjWEAPON::style` of `WEAPON::CAPPED`) */
		bool IsWeaponFireCapped(WeaponType weaponType);
		/** @brief Returns `true` if a weapon fires without its sound (`jjWEAPON::defaultSample` off, MLLE weapons play their own) */
		bool IsWeaponSoundMuted(WeaponType weaponType);
		/** @brief Returns the template of an event if a script asked for it, `nullptr` otherwise, not AddRef'd */
		Legacy::jjOBJ* FindObjectPreset(std::uint8_t eventId) const;
		/** @brief How a new object of an event looks, see @ref GetStartingLook() */
		struct StartingLook
		{
			std::int16_t CurAnim;
			std::uint32_t CurFrame;
			/** @brief Frame its behavior drew where the object is, 0 if none */
			std::uint32_t DrawnFrame;
			/** @brief Whether it keeps showing that frame, by a behavior that runs none of the game */
			bool Steady;
		};

		/**
		 * @brief Returns how a new object of an event at a tile starts, `false` if no script asked for its template
		 *
		 * The behavior of a script the template has runs once on a copy of it in `STATE::START`, which is in no level,
		 * and for a few ticks more to see whether it changes the frame. The copy is at the tile, its behavior reads the
		 * parameters of the event there.
		 */
		bool GetStartingLook(std::uint8_t eventId, std::int32_t tileX, std::int32_t tileY, StartingLook& look);
		/** @brief Called by what a script draws a frame by, see @ref GetStartingLook() */
		void OnFrameDrawn(float x, float y, std::uint32_t frame, std::uint32_t mode);
		/** @brief Called when an object that isn't in the level runs a behavior of the game, see @ref GetStartingLook() */
		void OnGameBehavior(const Legacy::jjOBJ& obj, std::uint32_t behavior);
		/** @brief Stops a context running for @ref GetStartingLook() after too many lines, returns `true` if it's watched */
		bool WatchLookContext(asIScriptContext* ctx);
		/** @brief Returns the animation the template of an event has before a script changes it, 0 if it has none */
		std::uint32_t GetDefaultPresetAnimation(std::uint8_t eventId);

		/** @brief Returns what the script draws in the level (`jjDrawSprite()` and the like), attached to the level on first use */
		Legacy::ScriptWorldCanvas& GetWorldCanvas();
		/** @brief Calls `onDrawLayer1()` to `onDrawLayer8()` of the script for the view being drawn, see @ref Legacy::ScriptLayerCanvas */
		void OnDrawLayers(UI::Canvas* canvas);
		/**
		 * @brief Returns the palette offset a palette-indexed sprite is drawn with in a sprite mode (`SPRITE::Mode`)
		 *
		 * `PALSHIFT` and `SINGLEHUE` draw the sprite in the sprite palette moved by @p param or in the gradient from
		 * @p param, a row of the shared palette texture each, which the scripts keep until they go. Otherwise it's the
		 * sprite palette, 0.
		 */
		std::int32_t GetSpritePaletteOffset(std::uint32_t mode, std::uint8_t param);
		/**
		 * @brief Returns how `SPRITE::BLEND_DISSOLVE` draws a color, as many random pixels as @p density says
		 *
		 * Returns a repeating texture of noise and sets @p paletteOffset to the palette it's drawn through, or `nullptr`
		 * where it can't be drawn so.
		 */
		Texture* GetDissolveNoise(std::uint8_t color, std::uint8_t density, std::int32_t& paletteOffset);
		/** @brief Returns the depth something drawn on a layer of the original, 1 to 8, is drawn at */
		std::uint16_t GetLayerDrawZ(std::int32_t layerNumber) const;
		/** @brief Forgets the depths @ref GetLayerDrawZ() resolved, once the order or the depths of the layers change */
		void InvalidateLayerDrawZ() {
			_layerDrawZFrame = UINT32_MAX;
		}
		/**
		 * @brief Returns the script view of the object of an object ID (`jjObjects`), AddRef'd
		 *
		 * An object ID nothing has, or had, gets an inactive object.
		 */
		Legacy::jjOBJ* GetObjectById(std::int32_t objectId);
		/** @brief Returns the script view of an object of the level, not AddRef'd, `nullptr` if it has no object ID (see @ref OnActorAdded()) */
		Legacy::jjOBJ* GetObjectView(Actors::ActorBase* actor);
		/** @brief Returns the top left corner of the view of a player on the screen (`jjPLAYER::subscreenX/Y`) */
		Vector2i GetPlayerSubscreenPosition(Actors::Player* player) const;
		/** @brief Returns the object ID of the boss that's active (`jjPLAYER::boss`), 0 for none */
		std::int32_t GetActiveBossObjectID();
		/**
		 * @brief Called when a player activates the boss of a level without a boss of the game
		 *
		 * A script of JJ2+ drives a boss of its own, it learns the boss is activated by
		 * `jjPLAYER::bossActivated`. Returns `false` if the context isn't of JJ2+, the level has no boss then.
		 */
		bool OnBossAreaActivated();
		/** @brief Recolors the frames of the sprites the level recolors (see @ref Formats::ScriptDataChunk), as `MLLE::Setup()` does */
		void ApplySpriteRecolors();
		/** @brief Draws text in the level until the next tick, as `jjDrawString()` does */
		void AddWorldText(float xPixel, float yPixel, StringView text, std::uint32_t size, const Colorf& color, std::int8_t layerZ);
		/** @brief Returns a particle of `jjParticles`, `nullptr` out of range */
		Legacy::jjPARTICLE* GetScriptParticle(std::int32_t index);
		/** @brief Makes a particle of a type (`PARTICLE::Type`) as `jjAddParticle()` does, `nullptr` if all are in use */
		Legacy::jjPARTICLE* AddScriptParticle(std::uint32_t type);
		/**
		 * @brief Returns the health of the boss a script activated for a player (`jjPLAYER::boss`), `false` if there's none
		 *
		 * The energy of the object against the one of its template, as JJ2+ shows the health of a boss.
		 */
		bool GetScriptBossHealth(std::int32_t& health, std::int32_t& maxHealth);
		/** @brief Returns `true` once a boss of a script was activated, see @ref OnBossAreaActivated() */
		bool IsBossAreaActivated() const {
			return _bossAreaActivated;
		}

		/** @brief Methods of a class of a script that implements `jjBEHAVIORINTERFACE`, `nullptr` for the ones it doesn't have */
		struct BehaviorMethods {
			asIScriptFunction* OnBehave;
			asIScriptFunction* OnDraw;
			asIScriptFunction* OnObjectHit;
		};
		/** @brief Returns the methods of a class of a script that implements `jjBEHAVIORINTERFACE`, looked up once per class */
		BehaviorMethods GetBehaviorMethods(asITypeInfo* type);
		/** @brief Returns the global `onObjectHit()` of the script, `nullptr` if it has none */
		asIScriptFunction* GetGlobalObjectHit() const {
			return _onObjectHit;
		}
		/**
		 * @brief Shows a text of the level in a size of `STRING::Size`
		 *
		 * MEDIUM is how the game shows the texts of the level, SMALL is smaller and LARGE bigger.
		 */
		void ShowLevelText(StringView text, std::uint32_t size);

		/**
		 * @brief Spawns a script-controlled object whose behavior function drives it each frame
		 *
		 * A host actor applies its velocity, calls the behavior over the jjOBJ, and syncs the position back. Returns a
		 * script object ID. Used by `jjAddObject` when a custom behavior function is supplied.
		 */
		std::int32_t AddScriptControlledObject(std::uint8_t eventId, float xPixel, float yPixel, asIScriptFunction* behaviorFunc);
		/**
		 * @brief Lets the object of the script of a crate that broke in the game go on without it
		 *
		 * It's done in the next update of the scripts, see @ref Legacy::ScriptObjectHost::OnPerish().
		 */
		void BreakObject(std::int32_t objectId);
		/**
		 * @brief Makes the object of the game of an object ID the object of another event, which keeps its object ID and view
		 *
		 * As JJ2+ has an object behave as another one (food that behaves as a dog), the object of the game is that
		 * object then. It's done in the next update of the scripts, see @ref Legacy::ScriptObjectHost::RunGameBehavior().
		 */
		void SwapActor(std::int32_t objectId, std::uint8_t eventId);

		/**
		 * @brief Returns the persistent `jjLAYER` proxy bound to the given level layer index
		 *
		 * Creates and populates it from the engine layer on first access (or returns `nullptr` if the index is out of
		 * range). The returned handle is AddRef'd for the caller. Used by `jjLayers`.
		 */
		Legacy::jjLAYER* GetLayerProxy(std::int32_t index);

		/**
		 * @brief Returns the index of the level layer a script means by a layer number of the original, 1 to 8
		 *
		 * The layers of a converted level know their numbers (see @ref Formats::LayerPropertiesChunk), wherever they are
		 * drawn. In a level without them the number is counted from the sprite layer, which is always layer 4. Returns -1
		 * if the level has no such layer.
		 */
		std::int32_t GetLayerIndex(std::int32_t layerNumber) const;
		/** @brief Returns the type of a tile of the original game (`jjTileType[]`), 1 translucent, 3 invisible, 5 heat, 0 ordinary */
		std::uint8_t GetOriginalTileType(std::int32_t tileId);
		/** @brief Changes the type of a tile of the original game, every placed tile of it is drawn so */
		void SetOriginalTileType(std::int32_t tileId, std::uint8_t type);
		/** @brief Returns whether layer 8 moves by its own speeds (`jjUseLayer8Speeds`) instead of staying fixed */
		bool GetUseLayer8Speeds() const {
			return _useLayer8Speeds;
		}
		/**
		 * @brief Sets whether layer 8 moves by its own speeds (`jjUseLayer8Speeds`) instead of staying fixed
		 *
		 * Only a layer 8 the level left fixed is changed. A value set while the layers are drawn takes effect after
		 * `onDrawLayer8()`, where JJ2+ reads it.
		 */
		void SetUseLayer8Speeds(bool value);
		/** @brief Returns the index of the first level layer of a given name (see @ref Formats::LayerPropertiesChunk), or -1 */
		std::int32_t FindLayerByName(StringView name) const;
		/**
		 * @brief Returns the indices of the level layers in the order they are drawn, from the front, as `jjLayerOrderGet()`
		 *
		 * A layer left out of the order by @ref SetLayerOrder() isn't there.
		 */
		void GetLayerOrder(SmallVectorImpl<std::int32_t>& order) const;
		/**
		 * @brief Changes the order the level layers are drawn in, from the front, as `jjLayerOrderSet()`
		 *
		 * The layers left out aren't drawn. Returns `false` and changes nothing if the order doesn't have the sprite
		 * layer, or has an index that isn't a level layer.
		 */
		bool SetLayerOrder(ArrayView<const std::int32_t> order);
		/**
		 * @brief Makes a layer of no tiles, as `jjLAYER(uint, uint)`, and returns its `jjLAYER` proxy, AddRef'd for the caller
		 *
		 * The layer is one of the level, which isn't drawn until it's in the order of the layers (see @ref SetLayerOrder()).
		 * The layer of one no script refers to any more, and which isn't drawn, is used again. Returns `nullptr` if the
		 * size is out of range or the memory isn't there.
		 */
		Legacy::jjLAYER* CreateLayer(std::int32_t width, std::int32_t height);
		/** @brief Makes a layer as a copy of a layer of the level, with its tiles and its properties, as `jjLAYER(const jjLAYER &in)`, see @ref CreateLayer() */
		Legacy::jjLAYER* CopyLayer(const Legacy::jjLAYER& source);
		/**
		 * @brief Makes layers of the layers of a level file, as `jjLayersFromLevel()`, see @ref CreateLayer()
		 *
		 * @p layerNumbers are 1 to 8. The layers of the level being played are copied as they are now, those of another
		 * level are read from its original file. @p tileIdAdjustment is added to every tile ID that isn't empty or animated,
		 * an animated tile is the animated tile of this level of the same index. Returns `false` and makes no layers if
		 * the level can't be read or doesn't have one of the layers.
		 */
		bool CreateLayersFromLevel(StringView fileName, ArrayView<const std::uint32_t> layerNumbers, std::int32_t tileIdAdjustment,
			SmallVectorImpl<Legacy::jjLAYER*>& layers);
		/**
		 * @brief Adds tiles of a tile set after the tiles of the level, as `jjTilesFromTileset()`
		 *
		 * @p paletteRemapping is 256 palette indices the pixels are remapped by, or `nullptr`. `jjTileCount` grows by
		 * @p tileCount. Returns `false` if the tile set can't be loaded or the tiles don't fit below the animated tiles.
		 */
		bool AddTilesFromTileSet(StringView fileName, std::uint32_t firstTileId, std::uint32_t tileCount, const std::uint8_t* paletteRemapping);
		/**
		 * @brief Adds tiles of a tile set as true color tiles, as `jjTilesFromTileset32()`
		 *
		 * The 8-bit tiles are in the colors of @p palette, or of the tile set's own palette without it, and don't follow
		 * the palette of the level. Otherwise the same as @ref AddTilesFromTileSet().
		 */
		bool AddTrueColorTilesFromTileSet(StringView fileName, std::uint32_t firstTileId, std::uint32_t tileCount, const Legacy::jjPAL* palette);
		/**
		 * @brief Fills the 256 × 256 palette indices of a texture of JJ2+ by its number (`TEXTURE::Texture`)
		 *
		 * `Plus.j2d` of JJ2+ in the directory of the original game has them, it's read on first use. Returns `false` if
		 * there's no such texture.
		 */
		bool GetPlusTexture(std::int32_t index, std::uint8_t* pixels);
		/**
		 * @brief Returns the description of the level layer a script means by a layer number of the original, or `nullptr`
		 *
		 * See @ref GetLayerIndex(). A change takes effect on the next frame, unless it's one the `jjLAYER` proxy of the
		 * layer pushes back every frame (its speeds, offsets and repeating).
		 */
		Tiles::LayerDescription* GetLayerDescription(std::int32_t layerNumber);

		/**
		 * @brief Reads the 32 × 32 palette indices of a tile that isn't animated, as scripts see them
		 *
		 * The pixels come from the file of the tile set, read on first use, and include what a script saved into the
		 * tile since (see @ref SetTilePixels()). A true color tile is mapped to the nearest colors of @p palette, or of
		 * `jjPalette` without it. Returns `false` and leaves @p pixels untouched if the tile has none, e.g. one of a
		 * tile set stored in the level.
		 */
		bool GetTilePixels(std::int32_t tileId, std::uint8_t* pixels, const Legacy::jjPAL* palette = nullptr);
		/** @brief Overwrites the pixels of a tile that isn't animated by 32 × 32 palette indices, everywhere it's drawn */
		bool SetTilePixels(std::int32_t tileId, const std::uint8_t* pixels);
		/**
		 * @brief Reads the 32 × 32 colors of a tile that isn't animated, as `jjCOLORMAP` of JJ2+ has them
		 *
		 * A true color tile has its own colors, an 8-bit one those of @p palette, index 0 transparent. Returns `false`
		 * as @ref GetTilePixels() does.
		 */
		bool GetTileColors(std::int32_t tileId, Legacy::jjPALCOLOR* colors, const Legacy::jjPAL& palette);
		/**
		 * @brief Overwrites the pixels of a tile that isn't animated by 32 × 32 colors with alpha, a true color tile
		 *
		 * A tile of a tile set drawn palette-indexed can't have colors of its own, it gets the nearest colors of
		 * `jjPalette` instead.
		 */
		bool SetTileColors(std::int32_t tileId, const Legacy::jjPALCOLOR* colors);
		/** @brief Returns whether a tile that isn't animated is a true color tile, `jjTileIs32Bit[]` of JJ2+ */
		bool IsTileTrueColor(std::int32_t tileId);

		/**
		 * @brief Returns the properties of a weapon by its number of JJ2+ (`WEAPON::BLASTER` is 1), `jjWeapons[]`
		 *
		 * Scripts read and write them, the game itself doesn't take most of them. A number out of range gives a scratch
		 * entry.
		 */
		Legacy::jjWEAPON* GetWeapon(std::int32_t number);

		/** @brief Returns list of players */
		ArrayView<Actors::Player* const> GetPlayers() const;

		/**
		 * @brief Overrides what the specified player believes one of its controls is doing until the next tick of the scripts
		 *
		 * As a tick of JJ2+ is what the player does in it, the frames without a tick (the display is faster than the 70
		 * ticks of a second) keep it too.
		 */
		void OverridePlayerInput(Actors::Player* player, PlayerAction action, bool value);
		/** @brief Applies what the scripts set the controls of a local player to in the last tick to the input of this frame */
		void ApplyInputOverrides(std::int32_t playerIndex);

		/** @brief Returns script backing store for specified player */
		jjPLAYER* GetPlayerBackingStore(Actors::Player* player);
		/** @overload */
		jjPLAYER* GetPlayerBackingStore(std::int32_t playerIndex);

		/** @brief Called when a level is loaded */
		void OnLevelLoad();
		/** @brief Called when a level begins */
		void OnLevelBegin();
		/** @brief Called when a level reloads */
		void OnLevelReload();
		/**
		 * @brief Called when another handler is about to be made in place of the level, see @ref IStateHandler::OnReplacing()
		 *
		 * The sprites of the game the script redrew are given back as they were, the next level would otherwise load them
		 * changed, and they may be freed before the level goes.
		 */
		void OnReplacing();
		/** @brief Called when a level updates (on the beginning of each frame) */
		void OnLevelUpdate(float timeMult);
		/** @brief Returns the number of the current tick of the scripts of JJ2+, 70 of them in a second (`jjGameTicks`) */
		std::int32_t GetGameTick() const;
		/** @brief Called when a level callback is triggered */
		void OnLevelCallback(Actors::ActorBase* initiator, std::uint8_t* eventParams);
		/** @brief Called when a part of viewport or HUD is drawn */
		bool OnDraw(UI::HUD* hud, Actors::Player* player, const Rectf& view, DrawType type);
		/** @brief Called when a player dies */
		void OnPlayerDied(Actors::Player* player, Actors::ActorBase* collider);

	protected:
		String OnProcessInclude(StringView includePath, StringView scriptPath) override;
		void OnProcessPragma(StringView content, ScriptContextType& contextType) override;

		/** @brief Called before a script function is called */
		void OnBeforeScriptCall();
		/** @brief Called after a script function is called */
		void OnAfterScriptCall();

	private:
		LevelHandler* _levelHandler;
		asIScriptFunction* _onLevelUpdate;
		std::int32_t _onLevelUpdateLastFrame;
		// The elapsed frames of the level as they were last, and the frames the level went back by since (it goes back to
		// a checkpoint when a player dies), so the ticks of the scripts go on (see GetGameTick())
		mutable float _lastElapsedFrames = 0.0f;
		mutable float _rolledBackFrames = 0.0f;
		// The water level `jjSetWaterLevel()` asked for without `instant`, -1 for none, how fast the water gets there in
		// pixels per tick and the tick it moved in last (see UpdateWaterLevel())
		float _waterTarget = -1.0f;
		float _waterChangeSpeed = 1.0f;
		std::int32_t _waterTick = -1;
		// The layer `jjWaterLayer` puts the water in front of, the water isn't drawn for one that isn't 1 to 8
		std::int32_t _waterLayer = 1;
		asIScriptFunction* _onDrawAmmo;
		asIScriptFunction* _onDrawHealth;
		asIScriptFunction* _onDrawLives;
		asIScriptFunction* _onDrawPlayerTimer;
		asIScriptFunction* _onDrawScore;
		asIScriptFunction* _onDrawGameModeHUD;
		// `onDrawLayer1()` to `onDrawLayer8()`, and where they draw, made in the first update if the script has any of them
		asIScriptFunction* _onDrawLayer[8] = {};
		std::unique_ptr<Legacy::ScriptLayerCanvas> _layerCanvas;
		bool _drawingLayers = false;
		// `jjTileType[]` of every tile, read from the placed tiles on first use
		SmallVector<std::uint8_t, 0> _tileTypes;
		// `jjUseLayer8Speeds`, and whether the level left layer 8 fixed, -1 until it's looked at
		bool _useLayer8Speeds = false;
		std::int8_t _layer8Fixed = -1;

		void ApplyUseLayer8Speeds();
		// The global callbacks of JJ2+ looked up once, a declaration is parsed on every lookup
		asIScriptFunction* _onPlayer = nullptr;
		asIScriptFunction* _onPlayerInput = nullptr;
		asIScriptFunction* _onPlayerDraw = nullptr;
		asIScriptFunction* _onObjectHit = nullptr;
		// The same for the classes of behaviors, see GetBehaviorMethods()
		HashMap<asITypeInfo*, BehaviorMethods> _behaviorMethods;
		// The draws of the canvases of the HUD drawn last and the frame of it, see jjCANVAS::NextLayer()
		std::uint32_t _canvasFrame = UINT32_MAX;
		UI::HUD* _canvasHud = nullptr;
		std::uint16_t _canvasDrawCount = 0;
		HashMap<std::int32_t, asITypeInfo*> _eventTypeToTypeInfo;
		BitArray _enabledCallbacks;
		HashMap<std::uint8_t, std::unique_ptr<jjPLAYER>> _playerBackingStore;
		// The controls of each local player the scripts set in the last tick and what to, see OverridePlayerInput()
		std::uint64_t _inputOverrideMask[Input::ControlScheme::MaxSupportedPlayers] = {};
		std::uint64_t _inputOverrideValue[Input::ControlScheme::MaxSupportedPlayers] = {};
		// What `jjPlayers` gives for the players that aren't in the game, it's inactive and its methods do nothing
		std::unique_ptr<jjPLAYER> _absentPlayer;
		// `jjWeapons[]` by the numbers of JJ2+, entry 0 is the scratch one (see GetWeapon())
		Legacy::jjWEAPON _weapons[(std::int32_t)WeaponType::Count + 1];
		bool _weaponsInitialized = false;
		// Whether `MLLE::Setup()` of the include of weapons is in the module already, see OnProcessInclude()
		bool _mlleWeaponsAdded = false;

		// Lazily-built mapping of original JJ2 sample indices to converted asset paths (only allocated once a script
		// actually plays a sample, so non-scripting and silent sessions pay nothing)
		std::unique_ptr<Compatibility::AnimSetMapping> _sampleMapping;
		// Cache of loaded sample buffers keyed by sample index (a null entry marks a sample that failed to load, to
		// avoid repeatedly trying to open a missing file)
		HashMap<std::int32_t, std::unique_ptr<nCine::AudioBuffer>> _scriptSamples;
		// Looped samples playing by their channels, and the tick a script played each in last (see PlayLoopedSample())
		struct LoopedSample {
			std::shared_ptr<AudioBufferPlayer> Player;
			std::int32_t Tick;
		};
		HashMap<std::int32_t, LoopedSample> _loopedSamples;
		std::int32_t _lastLoopChannel = 0;
		// Stops the looped samples no script played in the last tick
		void StopUnusedLoopedSamples();

		// The animation tables, made once a script uses an animation (see GetAnimationTables()), they're destroyed first as
		// they give the graphics of the game back as they were
		std::unique_ptr<Legacy::AnimationTables> _animTables;

		// Translates between the events of the game and those of the original, made on first use (see GetEventTranslator())
		std::unique_ptr<Legacy::EventTranslator> _eventTranslator;
		// Event words scripts wrote by the index of the tile, with what the tile was made into, a word is kept only while
		// the tile stays so - translating the event back could lose what the script wrote
		struct WrittenEvent {
			std::uint32_t Word;
			EventType Type;
			std::uint8_t Params[16];
		};
		HashMap<std::int32_t, WrittenEvent> _writtenEvents;
		// Every event a script wrote to a tile, by its position, which stays when the level goes back to a checkpoint as
		// in JJ2+ - a script clears the event of a pickup so it isn't there again after a death (see ReapplyEventWrites())
		struct EventWrite {
			EventType Type;
			Actors::ActorState Flags;
			std::uint8_t Params[16];
		};
		HashMap<std::int32_t, EventWrite> _eventWrites;
		// Writes the events a script wrote again after the level went back to a checkpoint, the objects that came back on
		// those tiles go
		void ReapplyEventWrites();

		// The objects of the level scripts reach by their object ID (jjObjects), the first entry is never used. The level
		// owns the objects, an entry only follows its object.
		struct ObjectSlot {
			std::weak_ptr<Actors::ActorBase> Actor;
			// The same object as a key of _objectIdsByActor, never dereferenced (it may be gone)
			Actors::ActorBase* ActorKey;
			// The script view of the object, made on first use, holds a reference
			Legacy::jjOBJ* Object;
			std::uint8_t EventID;
			// Who made the object (`CREATOR::Type`), a player shooting a bullet, for its view
			std::uint32_t CreatorType;
			std::uint16_t CreatorID;
			// Tick of the scripts the object got its object ID in
			std::int32_t SpawnTick;
			// Where a bullet a player fired is in the spread of a weapon whose bullets a script controls, -1 angled up, 0
			// straight, 1 angled down, NotSpread for any other object - and how far below the player the gunspot was
			std::int8_t SpreadSlot;
			float GunspotOffsetY;
		};
		static constexpr std::int8_t NotSpread = INT8_MIN;
		SmallVector<ObjectSlot, 0> _objectSlots;
		// The bullet being fired by FirePlayerWeapon(), RegisterObject() gives its object ID the place in the spread
		struct FiringBullet {
			const Actors::ActorBase* Shot;
			std::int8_t SpreadSlot;
			float GunspotOffsetY;
		};
		FiringBullet _firingBullet = {};
		// The object ID of each object of the level that has one, an entry of an object that's gone stays until its ID is
		// given to another one (see GetObjectView())
		HashMap<Actors::ActorBase*, std::int32_t> _objectIdsByActor;
		// Object IDs of the crates that broke, see BreakObject()
		SmallVector<std::int32_t, 0> _brokenObjects;
		// The object ID OnActorAdded() gave the last object, for the code that spawns one and returns its ID
		std::int32_t _lastObjectId = 0;
		// Templates of the objects of each event of the original (jjObjectPresets), made on first use, each holds a reference
		Legacy::jjOBJ* _objectPresets[256] = {};
		// The copy GetStartingLook() runs the behavior of, and the frame drawn where it is
		Legacy::jjOBJ* _lookObject = nullptr;
		std::uint32_t _lookDrawnFrame = 0;
		bool _lookGameBehavior = false;
		std::uint32_t _lookLines = 0;
		static constexpr std::uint32_t MaxLookLines = 200000;

		void OnLookLine(asIScriptContext* ctx);
		// What the script draws in the level, made on first use, and the tick it was cleared in last
		std::unique_ptr<Legacy::ScriptWorldCanvas> _worldCanvas;
		std::int32_t _canvasClearTick = -1;
		// The particles of the script (`jjParticles`), made on first use, the tick they moved in last and where the search for
		// a free one starts (see UpdateParticles())
		std::unique_ptr<Legacy::jjPARTICLE[]> _particles;
		std::int32_t _particlesTick = -1;
		std::int32_t _nextParticle = 0;

		// Moves the particles by the ticks since the last update and draws them in the level
		void UpdateParticles(std::int32_t currentTick);
		// Makes the particles of the weather of a tick, see OwnsWeather()
		void SpawnWeatherParticles();
		// Takes the weather the level has now, changed by an area of weather, into the variables of the scripts
		void PullWeather(bool force = false);
		// Palette offsets of the sprite modes by the mode (0 for PALSHIFT, 1 for SINGLEHUE) and the parameter, -1 if no row
		// was free, each holds a reference (see GetSpritePaletteOffset())
		HashMap<std::uint32_t, std::int32_t> _spritePaletteOffsets;
		// Random indices that SPRITE::BLEND_DISSOLVE is drawn through, made on first use
		std::unique_ptr<Texture> _noiseTexture;
		// The scenery events whose templates need an object, a bit each (see RefreshSceneryObjects())
		std::uint8_t _sceneryObjectsMask = 0;
		// Objects of the game to be made the objects of other events, by their object IDs (see SwapActor()), and the one being
		// added in place of another, which keeps its object ID
		struct ActorSwap {
			std::int32_t ObjectID;
			std::uint8_t EventID;
		};
		SmallVector<ActorSwap, 0> _actorSwaps;
		Actors::ActorBase* _swappingActor = nullptr;
		// A player activated the boss of the level, which is one of the script, see OnBossAreaActivated()
		bool _bossAreaActivated = false;

		// Gives an object of the level an object ID, `object` is its script view if it has one already (taken over)
		std::int32_t RegisterObject(const std::shared_ptr<Actors::ActorBase>& actor, Legacy::jjOBJ* object, std::uint8_t eventId);
		// Makes an object the one of an object ID, see _objectIdsByActor
		void SetSlotActor(std::int32_t objectId, const std::shared_ptr<Actors::ActorBase>& actor);
		// Returns the script view of a registered object, made from its template on first use, or nullptr if it's gone
		Legacy::jjOBJ* EnsureObjectView(std::int32_t objectId);
		// Hands the update of an object of the game over to the behavior of the script its view has
		void HostObject(Legacy::jjOBJ* obj, Actors::ActorBase* actor);
		// Gives the script views of the objects of the game what their objects are now, and the objects what scripts changed
		void SyncObjectsFromActors();
		void SyncObjectsToActors();
		// Gives the view of a projectile of an enemy what JJ2+ has for it, also what never changes if it's just `created`
		void UpdateEnemyProjectileView(const ObjectSlot& slot, Legacy::jjOBJ& obj, Actors::ActorBase& actor, bool created);
		// Makes the objects of the script of the crates that broke since objects a script controls entirely (see BreakObject())
		void BreakObjects();
		// Moves the water towards the level a script asked for, for the ticks since the last update
		void UpdateWaterLevel(std::int32_t currentTick);
		// Releases the script views of the objects and the templates, the objects of the game stop running behaviors of the script
		void ReleaseObjects();
		// Deletes the objects scripts made (`jjAddObject()`), those of the event map a behavior of a script drives start again
		// (see OnLevelReload())
		void DeleteObjectsOfScripts();

		// Persistent jjLAYER proxies keyed by engine layer index, created lazily when a script first accesses jjLayers[i].
		// Each holds one AddRef'd reference (released in the destructor); SyncLayerProperties pushes their writable fields
		// into the engine layer descriptions every frame so direct property writes (e.g., layer.xSpeed = N) take effect.
		HashMap<std::int32_t, Legacy::jjLAYER*> _layerProxies;

		// Pushes every cached jjLAYER proxy's writable properties (speeds, offsets, repeat, speed models) into the engine
		// tile-map layer descriptions; called once per frame from OnLevelUpdate. No proxies = no cost.
		void SyncLayerProperties();
		// Sets jjResolutionWidth/Height and jjSubscreenWidth/Height to the size of the view, every frame
		void UpdateViewSize();

		// Returns the engine layer a script means by a layer number of the original (see GetLayerIndex()), or nullptr, and
		// in `proxy` its jjLAYER proxy if a script asked for one (not AddRef'd) - the global layer properties are the
		// same as those of jjLayers[n], so they have to be written to it too, as the proxy pushes its values every frame
		Tiles::TileMapLayer* ResolveLayerNumber(std::int32_t layerNumber, Legacy::jjLAYER** proxy);

		// Pixels of the tile sets of the level as scripts read and write them (see jjPIXELMAP), one per part of the tile
		// map, read from the file on first use (Data stays null if it can't be)
		struct ScriptTileSetPixels {
			std::unique_ptr<Formats::TileSetData> Data;
			// The colors the 8-bit tiles of a part added as true color tiles are in (see AddTrueColorTilesFromTileSet()),
			// they're made true color tiles of `Data` once it's read
			std::unique_ptr<std::uint32_t[]> TrueColorPalette;
			bool Loaded = false;
		};
		SmallVector<ScriptTileSetPixels, 0> _tileSetPixels;
		// Whether a true color image was saved into a tile drawn palette-indexed already, see SetTileColors()
		bool _warnedIndexedTileColors = false;

		// Returns the cached pixels of the tile set part a tile is in, and the index of the tile in its file, or nullptr
		Formats::TileSetData* ResolveTileSetPixels(std::int32_t tileId, std::int32_t& fileTileId);

		// The index of the level layer of each number of the original, 1 to 8, worked out on first use (the layers of a level
		// don't change while it's played, only their order and their properties do)
		mutable std::int32_t _layerIndexByNumber[Formats::LayerPropertiesChunk::OriginalLayerCount + 1] = {};
		mutable bool _layerIndicesResolved = false;
		// Layers left out of the order by jjLayerOrderSet(), one per level layer, aren't drawn whatever their hasTiles says
		SmallVector<bool, 0> _layersOutOfOrder;
		// GetLayerDrawZ() of each layer number in the frame it's of, 0 if not resolved yet (see InvalidateLayerDrawZ())
		mutable std::uint16_t _layerDrawZCache[256] = {};
		mutable std::uint32_t _layerDrawZFrame = UINT32_MAX;

		std::uint16_t ResolveLayerDrawZ(std::int32_t layerNumber) const;
		// The index of the first layer a script made (see CreateLayer()), the layers from it on are made by scripts, or -1
		std::int32_t _firstScriptLayer = -1;
		// The textures of JJ2+ read from Plus.j2d (see GetPlusTexture()), by their numbers
		struct PlusTexture {
			std::int32_t Index;
			std::unique_ptr<std::uint8_t[]> Pixels;
		};
		SmallVector<PlusTexture, 0> _plusTextures;
		bool _plusTexturesLoaded = false;
		// The mappings of SPRITE::MAPPING of JJ2+ (see jjSpriteModeSetMapping()), only kept for the scripts, 256 of them made
		// on first use; one without a palette of its own maps to the colors of the palette in effect
		struct SpriteModeMapping {
			bool Used = false;
			bool HasPalette = false;
			std::uint8_t Indices[256];
			jjPALCOLOR Colors[256];
		};
		std::unique_ptr<SpriteModeMapping[]> _spriteModeMappings;
		SpriteModeMapping& GetSpriteModeMapping(std::uint8_t index);

		// Makes a layer for a script, or uses one again, and returns its index, or -1 (see CreateLayer())
		std::int32_t AddScriptLayer(Vector2i size, const Tiles::LayerDescription& description);
		// Returns the proxy of a layer a script made, which is drawn as soon as it's in the order of the layers
		Legacy::jjLAYER* MakeScriptLayerProxy(std::int32_t index, bool hasTiles);
		// Copies the tiles of a layer of the level into a layer of a script of the same size, as they're drawn, with the
		// adjustment of jjLayersFromLevel()
		void CopyLayerTiles(std::int32_t sourceIndex, std::int32_t targetIndex, std::int32_t tileIdAdjustment);
		// Returns whether a file name a script gives is the file of the level being played
		bool IsCurrentLevelFile(StringView fileName) const;

		// MLLE::Palette - the level palette as the level was made, which MLLE applies, a script may change it
		jjPAL _mllePalette;

		// One jjTILE for each tile ID scripts asked for (with TILE::ANIMATED for an animated tile), each holds a reference
		// released in the destructor, as JJ2+ there's only one instance for a tile
		HashMap<std::uint16_t, Legacy::jjTILE*> _tileProxies;

		// Returns the index of an animated tile in the tile map for a tile ID of a script, or -1 if it isn't one
		std::int32_t ResolveAnimatedTile(std::uint16_t tileID) const;
		// Returns the jjTILE of a tile ID of a script (flips are ignored), AddRef'd, or nullptr if the level has no such tile
		Legacy::jjTILE* GetTileProxy(std::uint16_t tileID);
		// What jjTILE reads and changes of the animated tiles of the tile map, see there
		void GetTileFrames(std::uint16_t tileID, SmallVectorImpl<std::uint16_t>& frames) const;
		bool SetTileFrames(std::uint16_t tileID, ArrayView<const std::uint16_t> frames, bool pingPong, std::uint16_t wait, std::uint16_t randomWait, std::uint16_t pingPongWait);
		std::uint8_t GetTileFps(std::uint16_t tileID) const;
		void SetTileFps(std::uint16_t tileID, std::uint8_t fps);

		// Resolves (loading and caching on first use) the sample buffer for the given index, or `nullptr` if unavailable
		nCine::AudioBuffer* ResolveSampleBuffer(std::int32_t sampleId);

		// Captures the level's loaded palette into jjPalette/jjBackupPalette (called once at construction, before any
		// script runs, so scripts see the level palette and can restore it via jjPAL::reset())
		void CaptureLevelPalette();

		// Global scripting variables
		static constexpr std::int32_t FLAG_HFLIPPED_TILE = 0x1000;
		static constexpr std::int32_t FLAG_VFLIPPED_TILE = 0x2000;
		static constexpr std::int32_t FLAG_ANIMATED_TILE = 0x4000;

		jjPAL jjPalette;
		jjPAL jjBackupPalette;

		// Object 0 is never used in JJ2+, the count is at least 1 (scripts count down to it by `--i != 0`)
		std::int32_t jjObjectCount = 1;
		// As in a local game of JJ2+
		std::int32_t jjObjectMax = 2048;

		std::int32_t gameMode = 0;
		std::int32_t customMode = 0;
		std::int32_t partyMode = 0;

		bool jjAutoWeaponChange = true;
		// The option of the Plus menu of JJ2+ to show the hearts of the health that's missing, which this game doesn't have
		bool jjShowMaxHealth = false;
		std::uint32_t jjScriptModuleID = 0;
		std::uint32_t gameTicksSpentWhileActive = 0;
		std::int32_t renderFrame = 0;

		bool versionTSF = true;
		bool isServer = false;
		bool jjDeactivatingBecauseOfDeath = false;

		std::int32_t DifficultyForNextLevel = 0;
		std::int32_t DifficultyAtLevelStart = 0;

		std::uint32_t numberOfTiles = 0;

		bool parLowDetail = false;
		// JJ2+ runs in 8 or 16 bits, scripts show their effects in 16 (fog)
		std::int32_t colorDepth = 16;
		std::int32_t checkedMaxSubVideoWidth = 0;
		std::int32_t checkedMaxSubVideoHeight = 0;
		std::int32_t realVideoW = 0;
		std::int32_t realVideoH = 0;
		std::int32_t subVideoW = 0;
		std::int32_t subVideoH = 0;

		bool snowing = false;
		bool snowingOutdoors = false;
		std::uint8_t snowingIntensity = 0;
		std::int32_t snowingType = 0;
		// The weather the level handler has, what the variables above were set to last
		WeatherType _appliedWeather = WeatherType::None;
		std::uint8_t _appliedWeatherIntensity = 0;

		std::int32_t maxScore = 0;

		std::int32_t waterLightMode = 0;
		std::int32_t waterInteraction = 0;

		std::uint8_t ChatKey = 0;

		bool soundEnabled = false;
		bool soundFXActive = false;
		bool musicActive = false;
		std::int32_t soundFXVolume = false;
		std::int32_t musicVolume = false;
		std::int32_t levelEcho = 0;

		bool warpsTransmuteCoins = false;
		bool delayGeneratedCrateOrigins = false;

		bool g_levelHasFood = false;
		std::int32_t enforceAmbientLighting = 0;

		LevelScriptLoader(const LevelScriptLoader&) = delete;
		LevelScriptLoader& operator=(const LevelScriptLoader&) = delete;

		Actors::ActorBase* CreateActorInstance(StringView typeName);

		static void RegisterBuiltInFunctions(asIScriptEngine* engine);
		void RegisterLegacyFunctions(asIScriptEngine* engine);
		void RegisterStandardFunctions(asIScriptEngine* engine, asIScriptModule* module);

		// The parts of the API of JJ2+, each next to what it registers in Legacy/ - see RegisterLegacyFunctions() for the order
		void RegisterLegacyGame(asIScriptEngine* engine);
		void RegisterLegacyPlayer(asIScriptEngine* engine);
		void RegisterLegacyScreen(asIScriptEngine* engine);
		void RegisterLegacyLayerGlobals(asIScriptEngine* engine);
		void RegisterLegacyPalette(asIScriptEngine* engine);
		void RegisterLegacyCanvas(asIScriptEngine* engine);
		void RegisterLegacyTexturedBackground(asIScriptEngine* engine);
		void RegisterLegacyLevel(asIScriptEngine* engine);
		void RegisterLegacySound(asIScriptEngine* engine);
		void RegisterLegacyCharacters(asIScriptEngine* engine);
		void RegisterLegacyObjects(asIScriptEngine* engine);
		void RegisterLegacyParticles(asIScriptEngine* engine);
		void RegisterLegacyStream(asIScriptEngine* engine);
		void RegisterLegacyAnimations(asIScriptEngine* engine);
		void RegisterLegacyPixelMaps(asIScriptEngine* engine);
		void RegisterLegacyLayers(asIScriptEngine* engine);
		void RegisterLegacyPlayerDraw(asIScriptEngine* engine);
		void RegisterLegacyEnumValues(asIScriptEngine* engine);
		void RegisterLegacyMlle(asIScriptEngine* engine);

		static std::uint8_t asGetDifficulty();
		static bool asIsReforged();
		static std::int32_t asGetLevelWidth();
		static std::int32_t asGetLevelHeight();
		static float asGetElapsedFrames();
		static float asGetAmbientLight();
		static void asSetAmbientLight(float value);
		static float asGetWaterLevel();
		static void asSetWaterLevel(float value);

		static void asPreloadMetadata(const String& path);
		static void asRegisterSpawnable(std::int32_t eventType, const String& typeName);
		static std::shared_ptr<Actors::ActorBase> asRegisterSpawnableCallback(const Actors::ActorActivationDetails& details);
		// Spawns the object of scenery where the template of its event needs one, see Legacy::ScriptSceneryObject
		static std::shared_ptr<Actors::ActorBase> SpawnSceneryObject(const Actors::ActorActivationDetails& details);
		// Spawns the object of an event the game has nothing of where its template needs one, see Legacy::ScriptEventObject
		static std::shared_ptr<Actors::ActorBase> SpawnPassthroughObject(const Actors::ActorActivationDetails& details);
		// Makes the objects of the game the objects of the events SwapActor() asked for
		void ProcessActorSwaps();
		// Returns the scenery events whose templates need an object, a bit each, see Legacy::ScriptSceneryObject
		std::uint8_t GetSceneryObjectsMask() const;
		// Activates the scenery again whose template came to need an object since, so it gets one
		void RefreshSceneryObjects();
		static void asSpawnEvent(std::int32_t eventType, std::int32_t x, std::int32_t y);
		static void asSpawnEventParams(std::int32_t eventType, std::int32_t x, std::int32_t y, const CScriptArray& eventParams);
		static void asSpawnType(const String& typeName, std::int32_t x, std::int32_t y);
		static void asSpawnTypeParams(const String& typeName, std::int32_t x, std::int32_t y, const CScriptArray& eventParams);

		static void asChangeLevel(std::int32_t exitType, const String& path);
		static void asShowLevelText(const String& text);
		static void asSetWeather(std::uint8_t weatherType, std::uint8_t intensity);

		static std::int32_t GetDifficulty();
		static std::int32_t SetDifficulty(std::int32_t value);

		static String get_jjMusicFileName();

		static String get_jjHelpStrings(std::uint32_t index);
		static void set_jjHelpStrings(std::uint32_t index, const String& text);

		static void jjAlert(const String& text, bool sendToAll, std::uint32_t size);
		static void jjPrint(const String& text, bool timestamp);
		static void jjDebug(const String& text, bool timestamp);
		static void jjChat(const String& text, bool teamchat);
		static void jjConsole(const String& text, bool sendToAll);
		static void jjSpy(const String& text);

		static float get_layerXOffset(std::uint8_t id);
		static float set_layerXOffset(std::uint8_t id, float value);
		static float get_layerYOffset(std::uint8_t id);
		static float set_layerYOffset(std::uint8_t id, float value);
		static std::int32_t get_layerWidth(std::uint8_t id);
		static std::int32_t get_layerRealWidth(std::uint8_t id);
		static std::int32_t get_layerRoundedWidth(std::uint8_t id);
		static std::int32_t get_layerHeight(std::uint8_t id);
		static float get_layerXSpeed(std::uint8_t id);
		static float set_layerXSpeed(std::uint8_t id, float value);
		static float get_layerYSpeed(std::uint8_t id);
		static float set_layerYSpeed(std::uint8_t id, float value);
		static float get_layerXAutoSpeed(std::uint8_t id);
		static float set_layerXAutoSpeed(std::uint8_t id, float value);
		static float get_layerYAutoSpeed(std::uint8_t id);
		static float set_layerYAutoSpeed(std::uint8_t id, float value);
		static bool get_layerHasTiles(std::uint8_t id);
		static bool set_layerHasTiles(std::uint8_t id, bool value);
		static bool get_layerTileHeight(std::uint8_t id);
		static bool set_layerTileHeight(std::uint8_t id, bool value);
		static bool get_layerTileWidth(std::uint8_t id);
		static bool set_layerTileWidth(std::uint8_t id, bool value);
		static bool get_layerLimitVisibleRegion(std::uint8_t id);
		static bool set_layerLimitVisibleRegion(std::uint8_t id, bool value);

		static void setLayerXSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed);
		static void setLayerYSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed);

		static void jjSpriteModeSetMapping(std::uint8_t index, const CScriptArray& indexMapping, const jjPAL& rgbMapping);
		static void jjSpriteModeSetIndexMapping(std::uint8_t index, const CScriptArray& indexMapping);
		static bool jjSpriteModeIsMappingUsed(std::uint8_t index);
		static std::int32_t jjSpriteModeFirstFreeMapping();
		static CScriptArray* jjSpriteModeGetIndexMapping(std::uint8_t index);
		static jjPAL* jjSpriteModeGetColorMapping(std::uint8_t index);

		static void jjDrawPixel(float xPixel, float yPixel, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawRectangle(float xPixel, float yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawResizedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawRotatedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawTile(float xPixel, float yPixel, std::uint16_t tile, std::uint32_t tileQuadrant, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawString(float xPixel, float yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static void jjDrawStringEx(float xPixel, float yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, spriteType spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID);
		static std::int32_t jjGetStringWidth(const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& style);

		static bool get_jjTriggers(std::uint8_t id);
		static bool set_jjTriggers(std::uint8_t id, bool value);
		static bool jjSwitchTrigger(std::uint8_t id);

		static bool isNumberedASFunctionEnabled(std::uint8_t id);
		static bool setNumberedASFunctionEnabled(std::uint8_t id, bool value);
		static void reenableAllNumberedASFunctions();

		static float getWaterLevel();
		static float getWaterLevel2();
		static float setWaterLevel(float value, bool instant);
		static float get_waterChangeSpeed();
		static float set_waterChangeSpeed(float value);
		static std::int32_t get_waterLayer();
		static std::int32_t set_waterLayer(std::int32_t value);
		static void setWaterGradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2);
		static void setWaterGradientFromColors(jjPALCOLOR color1, jjPALCOLOR color2);
		static void setWaterGradientToTBG();
		static void resetWaterGradient();
		static void triggerRock(std::uint8_t id);
		static void cycleTo(const String& filename, bool warp, bool fast);
		static void jjNxt(bool warp, bool fast);
		static bool jjMusicLoad(const String& filename, bool forceReload, bool temporary);
		static void jjMusicStop();
		static void jjMusicPlay();
		static void jjMusicPause();
		static void jjMusicResume();

		static bool jjSendPacket(const jjSTREAM& packet, std::int32_t toClientID, std::uint32_t toScriptModuleID);

	};
}

#endif