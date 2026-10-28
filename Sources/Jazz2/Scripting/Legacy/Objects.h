#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Animations.h"

namespace Jazz2::Scripting
{
	class LevelScriptLoader;

	namespace Legacy
	{
		/**
		 * @brief Holds the behavior assigned to an object: a built-in id, a script function, or a script object
		 *
		 * Tagged-union-style value assigned to @ref jjOBJ::behavior that selects what drives an object each frame:
		 * a built-in behavior id, a custom script function, or a script object implementing the behavior interface.
		 * Assignment and conversion operators accept each form, and the held function/object reference is reference-
		 * counted so it survives as long as the behavior does.
		 */
		struct jjBEHAVIOR
		{
			/** @brief Constructs an empty behavior in place */
			static jjBEHAVIOR* Create(jjBEHAVIOR* self);
			/** @brief Constructs a behavior from a built-in behavior id in place */
			static jjBEHAVIOR* CreateFromBehavior(std::uint32_t behavior, jjBEHAVIOR* self);
			/** @brief Destroys a behavior in place */
			static void Destroy(jjBEHAVIOR* self);

			// Releases the held function/object reference. Runs both when AngelScript destroys a value (via Destroy)
			// and when an embedding object (jjOBJ::behavior) is destroyed.
			~jjBEHAVIOR();

			/** @brief Creates a new instance */
			jjBEHAVIOR() = default;
			// Copy must add a reference to the held function/object (rule of three, since the destructor releases)
			jjBEHAVIOR(const jjBEHAVIOR& other);

			jjBEHAVIOR& operator=(const jjBEHAVIOR& other);
			jjBEHAVIOR& operator=(std::uint32_t other);
			jjBEHAVIOR& operator=(asIScriptFunction* other);
			jjBEHAVIOR& operator=(asIScriptObject* other);
			bool operator==(const jjBEHAVIOR& other) const;
			bool operator==(std::uint32_t other) const;
			bool operator==(const asIScriptFunction* other) const;
			operator std::uint32_t();
			operator asIScriptFunction* ();
			operator asIScriptObject* ();

			// A behavior is one of: a built-in behavior id (BEHAVIOR::Behavior), a custom script behavior function
			// (jjVOIDFUNCOBJ), or a script object implementing jjBEHAVIORINTERFACE. The function/object hold an
			// AngelScript reference (managed in Create/Destroy/operator=).
			std::uint32_t behaviorId = 0;
			asIScriptFunction* function = nullptr;
			asIScriptObject* object = nullptr;
		};

		class jjOBJ;
		class jjPLAYER;
		// Native actor that hosts a script-controlled jjOBJ (defined in Objects.cpp)
		class ScriptLegacyObject;
		// Drives an object of the game by a behavior of the script (see ObjectHost.h)
		class ScriptObjectHost;

		using jjVOIDFUNCOBJ = void(*)(jjOBJ* obj);

		/**
		 * @brief A level object (actor) controllable from scripts
		 *
		 * Reference-counted script view of an in-level object (enemy, pickup, projectile, etc.), mirroring the original
		 * JJ2+ `jjOBJ`. Exposes position, speed, acceleration, animation, energy, lighting and the handling flags that
		 * govern collisions with players, bullets and blasts, plus its @ref jjBEHAVIOR. Static helpers spawn, delete
		 * and kill objects; live ones are reached through `jjObjects[id]`.
		 */
		class jjOBJ
		{
			friend class ScriptLegacyObject;
			friend class ScriptObjectHost;
			friend class jjPLAYER;
			friend class Scripting::LevelScriptLoader;

		public:
			/** @brief Creates a new instance */
			jjOBJ();
			~jjOBJ();

			/** @brief Increments the reference count */
			void AddRef();

			/** @brief Decrements the reference count */
			void Release();

			/** @brief Returns whether the object is currently active */
			bool get_isActive() const;
			/** @brief Returns the light type emitted by the object */
			std::uint32_t get_lightType() const;
			/** @brief Sets the light type emitted by the object */
			std::uint32_t set_lightType(std::uint32_t value) const;

			/** @brief Handles a collision against the target object and returns the object that was hit */
			jjOBJ* objectHit(jjOBJ* target, std::uint32_t playerHandling);
			/** @brief Destroys nearby objects (and optionally tiles) within the given distance */
			void blast(std::int32_t maxDistance, bool blastObjects);

			/** @brief The behavior driving this object each frame */
			jjBEHAVIOR behavior;

			/** @brief Runs the given built-in behavior id once, optionally drawing the object */
			void behave1(std::uint32_t behavior, bool draw);
			/** @brief Runs the given behavior once, optionally drawing the object */
			void behave2(jjBEHAVIOR behavior, bool draw);
			/** @brief Runs the given behavior function once, optionally drawing the object */
			void behave3(jjVOIDFUNCOBJ behavior, bool draw);
			/** @brief Runs `BEHAVIOR::WALKINGENEMY` once, as measured in JJ2+, optionally drawing the object */
			void RunWalkingEnemy(bool draw);

			/** @brief Spawns a new object with a built-in behavior and returns its object id */
			static std::int32_t jjAddObject(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, std::uint32_t behavior);
			/** @brief Spawns a new object with a custom behavior function and returns its object id */
			static std::int32_t jjAddObjectEx(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, jjVOIDFUNCOBJ behavior);

			/** @brief Deletes the object with the given id */
			static void jjDeleteObject(std::int32_t objectID);
			/** @brief Kills the object with the given id */
			static void jjKillObject(std::int32_t objectID);

			/** @brief Original (spawn) horizontal position in pixels */
			float xOrg = 0;
			/** @brief Original (spawn) vertical position in pixels */
			float yOrg = 0;
			/** @brief Current horizontal position in pixels */
			float xPos = 0;
			/** @brief Current vertical position in pixels */
			float yPos = 0;
			/** @brief Horizontal speed in pixels per frame */
			float xSpeed = 0;
			/** @brief Vertical speed in pixels per frame */
			float ySpeed = 0;
			/** @brief Horizontal acceleration in pixels per frame */
			float xAcc = 0;
			/** @brief Vertical acceleration in pixels per frame */
			float yAcc = 0;
			/** @brief General-purpose counter used by behaviors */
			std::int32_t counter = 0;
			/** @brief Current sprite frame index */
			std::uint32_t curFrame = 0;

			/** @brief Advances and returns the current animation frame, optionally changing it */
			std::uint32_t determineCurFrame(bool change);

			/** @brief Number of frames the object has existed */
			std::int32_t age = 0;
			/** @brief Creator as JJ2+ packs it, its type (`CREATOR::Type`) with its id */
			mutable std::int32_t creator = 0;

			/** @brief Packs a creator type and a 16-bit creator ID as @ref creator keeps them */
			static std::int32_t PackCreator(std::uint32_t type, std::uint16_t id);
			/** @brief Returns the id of the object that created this one */
			std::uint16_t get_creatorID() const;
			/** @brief Sets the id of the object that created this one */
			std::uint16_t set_creatorID(std::uint16_t value) const;
			/** @brief Returns the type of the creator */
			std::uint32_t get_creatorType() const;
			/** @brief Sets the type of the creator */
			std::uint32_t set_creatorType(std::uint32_t value) const;

			/** @brief Current animation index */
			int16_t curAnim = 0;

			/** @brief Selects and returns the current animation, optionally changing it */
			int16_t determineCurAnim(std::uint8_t setID, std::uint8_t animation, bool change);
			/** @brief The same for a set given by `ANIM::Set`, which can be a custom one */
			int16_t determineCurAnimOfSet(std::int32_t setID, std::uint8_t animation, bool change);

			/** @brief Animation played when the object is killed */
			std::uint16_t killAnim = 0;
			/** @brief Number of frames the object remains frozen */
			std::uint8_t freeze = 0;
			/** @brief Light type emitted by the object */
			std::uint8_t lightType = 0;
			/** @brief Current frame id within the animation */
			std::int8_t frameID = 0;
			/** @brief Number of frames during which the object cannot be hit */
			std::int8_t noHit = 0;

			/** @brief Returns how the object responds to bullets */
			std::uint32_t get_bulletHandling();
			/** @brief Sets how the object responds to bullets */
			std::uint32_t set_bulletHandling(std::uint32_t value);
			/** @brief Returns whether bullets ricochet off the object */
			bool get_ricochet();
			/** @brief Sets whether bullets ricochet off the object */
			bool set_ricochet(bool value);
			/** @brief Returns whether the object can be frozen */
			bool get_freezable();
			/** @brief Sets whether the object can be frozen */
			bool set_freezable(bool value);
			/** @brief Returns whether the object can be destroyed by a blast */
			bool get_blastable();
			/** @brief Sets whether the object can be destroyed by a blast */
			bool set_blastable(bool value);

			/** @brief Remaining health of the object */
			std::int8_t energy = 0;
			/** @brief Brightness of the light emitted by the object */
			std::int8_t light = 0;
			/** @brief Object category/type */
			std::uint8_t objType = 0;

			/** @brief Returns how the object responds to player contact */
			std::uint32_t get_playerHandling();
			/** @brief Sets how the object responds to player contact */
			std::uint32_t set_playerHandling(std::uint32_t value);
			/** @brief Returns whether the object can be targeted */
			bool get_isTarget();
			/** @brief Sets whether the object can be targeted */
			bool set_isTarget(bool value);
			/** @brief Returns whether the object triggers nearby TNT */
			bool get_triggersTNT();
			/** @brief Sets whether the object triggers nearby TNT */
			bool set_triggersTNT(bool value);
			/** @brief Returns whether the object deactivates when off screen */
			bool get_deactivates();
			/** @brief Sets whether the object deactivates when off screen */
			bool set_deactivates(bool value);
			/** @brief Returns whether collisions are handled by the script */
			bool get_scriptedCollisions();
			/** @brief Sets whether collisions are handled by the script */
			bool set_scriptedCollisions(bool value);

			/** @brief Current behavior state */
			std::int8_t state = 0;
			/** @brief Score points awarded when the object is destroyed */
			std::uint16_t points = 0;
			/** @brief Event id the object was spawned from */
			std::uint8_t eventID = 0;
			/** @brief Facing direction of the object */
			std::int8_t direction = 0;
			/** @brief Number of frames since the object was last hit */
			std::uint8_t justHit = 0;
			/** @brief Previous behavior state */
			std::int8_t oldState = 0;
			/** @brief Animation playback speed */
			std::int32_t animSpeed = 0;
			/** @brief General-purpose value used by behaviors */
			std::int32_t special = 0;

			/** @brief Returns the value of a custom per-object variable */
			std::int32_t get_var(std::uint8_t x);
			/** @brief Sets the value of a custom per-object variable */
			std::int32_t set_var(std::uint8_t x, std::int32_t value);

			/** @brief Amount of damage the object deals on contact */
			std::uint8_t doesHurt = 0;
			/** @brief Counter value at which the object's counter is considered complete */
			std::uint8_t counterEnd = 0;
			/** @brief Unique id of the object */
			std::int16_t objectID = 0;

			/** @brief Draws the object this frame */
			std::int32_t draw();
			/** @brief Makes the object behave as a solid obstacle */
			std::int32_t beSolid(bool shouldCheckForStompingLocalPlayers);
			/** @brief Makes the object behave as a moving platform */
			void bePlatform(float xOld, float yOld, std::int32_t width, std::int32_t height);
			/** @brief Stops the object from acting as a platform */
			void clearPlatform();
			/** @brief Snaps the object onto the ground below it */
			void putOnGround(bool precise);
			/** @brief Bounces the object as a ricocheting bullet */
			bool ricochet();
			/** @brief Unfreezes the object with the given shatter style */
			std::int32_t unfreeze(std::int32_t style);
			/** @brief Removes the object from the level */
			void deleteObject();
			/** @brief Deactivates the object */
			void deactivate();
			/** @brief Advances the object along its predefined path */
			void pathMovement();
			/** @brief Fires a bullet from the object and returns the new bullet's id */
			std::int32_t fireBullet(std::uint8_t eventID);
			/** @brief Spawns a pixel-explosion particle effect for the object */
			void particlePixelExplosion(std::int32_t style);
			/** @brief Bursts a frame of the animation table into debris where it would be drawn, see @ref particlePixelExplosion() */
			static void AddPixelExplosion(LevelScriptLoader* owner, float xPixel, float yPixel, std::uint32_t frame, std::int32_t direction);
			/** @brief Grants the object as a pickup to the given player */
			void grantPickup(jjPLAYER* player, std::int32_t frequency);

			/** @brief Returns the id of the nearest player within the given distance */
			std::int32_t findNearestPlayer(std::int32_t maxDistance) const;
			/** @brief Returns the id of the nearest player within the given distance and outputs the distance */
			std::int32_t findNearestPlayerEx(std::int32_t maxDistance, std::int32_t& foundDistance) const;

			/** @brief Returns whether this object collides with another object */
			bool doesCollide(const jjOBJ* object, bool always) const;
			/** @brief Returns whether this object collides with the given player */
			bool doesCollidePlayer(const jjPLAYER* object, bool always) const;

		private:
			std::int32_t _refCount;
			// The hosting actor while this is a live script-controlled object (null once despawned); set by the wrapper
			ScriptLegacyObject* _wrapper = nullptr;

			// Script-facing per-object state. The native engine doesn't act on most of these for script-spawned objects,
			// but scripts read/write them constantly (state machines lean on var[]), so they're stored for round-trip use.
			static constexpr std::int32_t MaxVars = 16;
			std::int32_t _vars[MaxVars] = {};
			std::uint32_t _bulletHandling = 0;
			std::uint32_t _playerHandling = 0;
			bool _ricochetProp = false;
			bool _freezable = true;
			bool _blastable = true;
			bool _isTarget = false;
			bool _triggersTNT = false;
			bool _deactivates = true;
			bool _scriptedCollisions = false;

			// Whether a script set how the object is handled, the game handles it itself otherwise (see ScriptObjectHost::OnCollision())
			bool _bulletHandlingSet = false;
			bool _playerHandlingSet = false;

			// The object of the game this is the script view of (see LevelScriptLoader::GetObjectById()), none for a template
			// or an object a script controls entirely (see _wrapper)
			std::weak_ptr<Actors::ActorBase> _actor;
			// While a behavior of the script drives the object of the game
			ScriptObjectHost* _host = nullptr;
			// The behavior of the script running for the object (its function or object), see behave1()
			const void* _runningBehavior = nullptr;
			// A behavior moves the object itself, the speeds are its (see ScriptObjectHost::FollowScriptMovement())
			bool _movedByBehavior = false;
			// Made in STATE::START, shown floating as a pickup of the game until a behavior of the script runs it (see
			// ScriptObjectHost::CallScriptBehavior())
			bool _startPending = false;
			// A bullet a player fired of a weapon whose bullets a script controls, see ScriptObjectHost::InitializeScriptedBullet()
			bool _scriptedBullet = false;
			// The animation of its own a script gave the object of the game, which shows its frames in place of the ones of
			// its own animation, 0 for none (see ScriptObjectHost::PullFromActor())
			std::int32_t _skinAnim = 0;
			// How many frames ahead a behavior set `frameID` of an animation counted by the age (see ScriptObjectHost::PullFromActor())
			std::int8_t _framePhase = 0;
			// The properties as the object of the game had them last, the ones a script writes are told apart by them
			struct SyncedState {
				float XPos, YPos, XSpeed, YSpeed;
				std::int8_t Direction, Energy, State;
				// The animation of the animation table the object shows by itself, 0 for none of the original
				std::int32_t NativeAnim;
				std::int16_t CurAnim;
				std::uint32_t CurFrame;
				// The light the object has in the game, of the template of its event (the object of the game lights itself)
				std::uint8_t LightType;
				// How much lower `yPos` is than the object of the game, so the frame of its animation stands where it stands
				// (see ScriptObjectHost::GetSkinOffsetY())
				float SkinOffsetY;
				// The animation has more frames than the graphics of the game, the frame is drawn as it is (see PullFromActor())
				bool LongerAnim;
			};
			SyncedState _synced = {};

			// Takes over what an object gets of its template (jjObjectPresets), all but what makes it this object
			void CopyTemplate(const jjOBJ& preset);
		};

		/** @brief Per-particle data for a pixel particle */
		struct jjPARTICLEPIXEL
		{
			/** @brief Size of the pixel cluster */
			std::uint8_t size;

			/** @brief Returns the color of the pixel at the given index */
			std::uint8_t get_color(std::int32_t i) const;
			/** @brief Sets the color of the pixel at the given index */
			std::uint8_t set_color(std::int32_t i, std::uint8_t value);
		};

		/** @brief Per-particle data for a fire particle */
		struct jjPARTICLEFIRE
		{
			/** @brief Size of the particle */
			std::uint8_t size;
			/** @brief Current color index */
			std::uint8_t color;
			/** @brief Final color index the particle fades toward */
			std::uint8_t colorStop;
			/** @brief Per-frame change applied to the color */
			std::int8_t colorDelta;
		};

		/** @brief Per-particle data for a smoke particle */
		struct jjPARTICLESMOKE
		{
			/** @brief Frames remaining before the particle expires */
			std::uint8_t countdown;
		};

		/** @brief Per-particle data for an ice trail particle */
		struct jjPARTICLEICETRAIL
		{
			/** @brief Current color index */
			std::uint8_t color;
			/** @brief Final color index the particle fades toward */
			std::uint8_t colorStop;
			/** @brief Per-frame change applied to the color */
			std::int8_t colorDelta;
		};

		/** @brief Per-particle data for a spark particle */
		struct jjPARTICLESPARK
		{
			/** @brief Current color index */
			std::uint8_t color;
			/** @brief Final color index the particle fades toward */
			std::uint8_t colorStop;
			/** @brief Per-frame change applied to the color */
			std::int8_t colorDelta;
		};

		/** @brief Per-particle data for a text (score) particle */
		struct jjPARTICLESTRING
		{
			/** @brief Returns the displayed text */
			String get_text() const;
			/** @brief Sets the displayed text */
			void set_text(String text);
		};

		/** @brief Per-particle data for a snow particle */
		struct jjPARTICLESNOW
		{
			/** @brief Current animation frame */
			std::uint8_t frame;
			/** @brief Counter advancing the animation */
			std::uint8_t countup;
			/** @brief Frames remaining before the particle expires */
			std::uint8_t countdown;
			/** @brief Base animation frame index */
			std::uint16_t frameBase;
		};

		/** @brief Per-particle data for a rain particle */
		struct jjPARTICLERAIN
		{
			/** @brief Current animation frame */
			std::uint8_t frame;
			/** @brief Base animation frame index */
			std::uint16_t frameBase;
		};

		/** @brief Per-particle data for a flower particle */
		struct jjPARTICLEFLOWER
		{
			/** @brief Size of the particle */
			std::uint8_t size;
			/** @brief Color index */
			std::uint8_t color;
			/** @brief Current rotation angle */
			std::uint8_t angle;
			/** @brief Rotation speed */
			std::int8_t angularSpeed;
			/** @brief Number of petals */
			std::uint8_t petals;
		};

		/** @brief Per-particle data for a star particle */
		struct jjPARTICLESTAR
		{
			/** @brief Size of the particle */
			std::uint8_t size;
			/** @brief Color index */
			std::uint8_t color;
			/** @brief Current rotation angle */
			std::uint8_t angle;
			/** @brief Rotation speed */
			std::int8_t angularSpeed;
			/** @brief Current animation frame */
			std::uint8_t frame;
			/** @brief Counter toward the next color change */
			std::uint8_t colorChangeCounter;
			/** @brief Number of frames between color changes */
			std::uint8_t colorChangeInterval;
		};

		/** @brief Per-particle data for a leaf particle */
		struct jjPARTICLELEAF
		{
			/** @brief Current animation frame */
			std::uint8_t frame;
			/** @brief Counter advancing the animation */
			std::uint8_t countup;
			/** @brief Whether the particle ignores collision */
			bool noclip;
			/** @brief Height at which the particle settles */
			std::uint8_t height;
			/** @brief Base animation frame index */
			std::uint16_t frameBase;
		};

		/** @brief A visual particle in the level */
		class jjPARTICLE
		{
		public:
			/** @brief Creates a new instance */
			jjPARTICLE();
			~jjPARTICLE();

			/** @brief Increments the reference count */
			void AddRef();

			/** @brief Decrements the reference count */
			void Release();

			/** @brief Horizontal position in pixels */
			float xPos;
			/** @brief Vertical position in pixels */
			float yPos;
			/** @brief Horizontal speed in pixels per frame */
			float xSpeed;
			/** @brief Vertical speed in pixels per frame */
			float ySpeed;

			/** @brief Type of the particle */
			std::uint8_t particleType;
			/** @brief Whether the particle is currently active */
			bool active;

			/** @brief Type-specific particle data, interpreted according to particleType */
			union {
				jjPARTICLEPIXEL pixel;
				jjPARTICLEFIRE fire;
				jjPARTICLESMOKE smoke;
				jjPARTICLEICETRAIL trail;
				jjPARTICLESPARK spark;
				jjPARTICLESTRING string;
				jjPARTICLESNOW snow;
				jjPARTICLERAIN rain;
				jjPARTICLEFLOWER flower;
				jjPARTICLESTAR star;
				jjPARTICLELEAF leaf;
				// The fields of the types are at the offsets JJ2+ has them (see RegisterLegacyParticles()), 9 bytes
				std::uint8_t raw[10];
			} GENERIC;

		private:
			friend class Scripting::LevelScriptLoader;

			std::int32_t _refCount;
			// Ticks since the particle was made, or since a leaf landed
			std::uint16_t _age;
			// What the size of a star shrinks from
			float _scale;
			// Made by the weather of the level, see LevelScriptLoader::SpawnWeatherParticles()
			bool _weather;
			// The first frame of a snowflake of the weather, `snow.frameBase` holds only 16 bits of it
			std::uint32_t _snowFrameBase;
		};
	}
}

namespace Jazz2::Scripting
{
	using namespace Legacy;

	/** @brief Returns the level object at the given index */
	jjOBJ* get_jjObjects(std::int32_t index);
	/** @brief Returns the object preset for the given event id */
	jjOBJ* get_jjObjectPresets(std::int8_t id);

	/** @brief Spawns a tile-explosion particle effect at the given tile */
	void jjAddParticleTileExplosion(std::uint16_t xTile, std::uint16_t yTile, std::uint16_t tile, bool collapseSceneryStyle);
	/** @brief Spawns a pixel-explosion particle effect at the given position */
	void jjAddParticlePixelExplosion(float xPixel, float yPixel, int curFrame, int direction, int mode);

	/** @brief Returns the particle at the given index */
	jjPARTICLE* GetParticle(std::int32_t index);

	/** @brief Spawns and returns a new particle of the given type */
	jjPARTICLE* AddParticle(std::int32_t particleType);
}

#endif
