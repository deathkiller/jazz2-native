#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "Animations.h"

namespace Jazz2::Scripting
{
	class LevelScriptLoader;

	namespace Legacy
	{
		/**
		 * @brief What drives an object every tick
		 *
		 * A small value type that holds exactly one of three things: a built-in @ref BEHAVIOR::Behavior constant, a
		 * script function matching @ref jjVOIDFUNCOBJ, or an instance of a script class implementing @ref jjBEHAVIORINTERFACE.
		 * It is mainly met as @ref jjOBJ::behavior and as an argument of @ref jjOBJ::behave(). Assignment, comparison and
		 * conversion operators accept each form, so the type rarely has to be named explicitly. A held function or
		 * object is kept alive for as long as the behavior holds it.
		 */
		struct jjBEHAVIOR
		{
			/** @brief Creates a behavior holding @ref BEHAVIOR::INACTIVE */
			static jjBEHAVIOR* Create(jjBEHAVIOR* self);
			/** @brief Creates a behavior holding the given built-in behavior */
			static jjBEHAVIOR* CreateFromBehavior(std::uint32_t behavior, jjBEHAVIOR* self);
			/** @brief Destroys the behavior, releasing a held function or object */
			static void Destroy(jjBEHAVIOR* self);

			// Releases the held function/object reference. Runs both when AngelScript destroys a value (via Destroy)
			// and when an embedding object (jjOBJ::behavior) is destroyed.
			~jjBEHAVIOR();

			/** @brief Creates a new instance */
			jjBEHAVIOR() = default;
			// Copy must add a reference to the held function/object (rule of three, since the destructor releases)
			jjBEHAVIOR(const jjBEHAVIOR& other);

			/** @brief Copies another behavior into this one */
			jjBEHAVIOR& operator=(const jjBEHAVIOR& other);
			/** @brief Makes this behavior a built-in one, dropping a held function or object */
			jjBEHAVIOR& operator=(std::uint32_t other);
			/** @brief Makes this behavior a script function, dropping a held built-in behavior or object */
			jjBEHAVIOR& operator=(asIScriptFunction* other);
			/** @brief Makes this behavior a @ref jjBEHAVIORINTERFACE instance, dropping a held built-in behavior or function */
			jjBEHAVIOR& operator=(asIScriptObject* other);
			/** @brief Whether both behaviors hold the same built-in behavior, function or object */
			bool operator==(const jjBEHAVIOR& other) const;
			/** @brief Whether this behavior is the given built-in behavior (never true while it holds a function or object) */
			bool operator==(std::uint32_t other) const;
			/** @brief Whether this behavior holds the given script function */
			bool operator==(const asIScriptFunction* other) const;
			/**
			 * @brief Converts to the held built-in behavior
			 *
			 * Gives `BEHAVIOR::INACTIVE` when the behavior holds a script function or object instead.
			 */
			operator std::uint32_t();
			/**
			 * @brief Casts to the held script function
			 *
			 * Gives null when the behavior holds a built-in behavior or a @ref jjBEHAVIORINTERFACE instance.
			 */
			operator asIScriptFunction* ();
			/**
			 * @brief Casts to the held `jjBEHAVIORINTERFACE` instance
			 *
			 * Gives null when the behavior holds a built-in behavior or a script function.
			 */
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

		// A handle of the script funcdef "void jjVOIDFUNCOBJ(jjOBJ@)"
		using jjVOIDFUNCOBJ = asIScriptFunction*;

		/**
		 * @brief An object of the level
		 *
		 * Enemies, pickups, bullets, platforms, light sources and similar things placed in or spawned into the level.
		 * Every object has a slot in @ref jjObjects, and new objects start as a copy of their template in
		 * @ref jjObjectPresets. Besides position, speed and animation, an object carries a @ref jjOBJ::behavior that runs
		 * once per tick, a state machine (@ref jjOBJ::state) and handling settings that decide how it reacts to players
		 * and bullets.
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

			/**
			 * @brief Whether the object currently exists
			 *
			 * False for a free slot of @ref jjObjects or an object that has been deleted, which is the same as its
			 * @ref jjOBJ::behavior being @ref BEHAVIOR::INACTIVE. Deleted objects stop being active immediately, even though the
			 * game removes them only at the end of the frame.
			 */
			bool get_isActive() const;
			/** @brief Returns the light type emitted by the object */
			std::uint32_t get_lightType() const;
			/** @brief Sets the light type emitted by the object */
			std::uint32_t set_lightType(std::uint32_t value) const;

			/**
			 * @brief Makes this object hit another one as a bullet
			 *
			 * The game resolves the collision between the two objects as if they really touched, wherever they are,
			 * and the target's @ref jjOBJ::energy is updated afterward.
			 *
			 * @param target          Object being hit
			 * @param playerHandling  Currently ignored
			 * @return The target object
			 * @partialsupport The `playerHandling` parameter is currently ignored.
			 */
			jjOBJ* objectHit(jjOBJ* target, std::uint32_t playerHandling);
			/**
			 * @brief Hurts and pushes away players near the object, like an explosion
			 *
			 * Every vulnerable player whose squared distance from the object in pixels is less than `maxDistance` loses
			 * one point of health and is knocked away from the object.
			 *
			 * @param maxDistance   Squared radius in pixels
			 * @param blastObjects  Currently ignored, other objects are not affected
			 * @partialsupport The `blastObjects` parameter is currently ignored.
			 */
			void blast(std::int32_t maxDistance, bool blastObjects);

			/**
			 * @brief Behavior that runs for the object once per tick
			 *
			 * Either a @ref BEHAVIOR::Behavior constant, a @ref jjVOIDFUNCOBJ function or a @ref jjBEHAVIORINTERFACE instance. Assigning
			 * to the behavior of a template in @ref jjObjectPresets (typically in @ref onLevelLoad()) changes every object of that
			 * event created afterward.
			 */
			jjBEHAVIOR behavior;

			/**
			 * @brief Runs a built-in behavior for the object once
			 *
			 * Doesn't change @ref jjOBJ::behavior. With @ref BEHAVIOR::DEFAULT, the object's own behavior runs instead. Typically
			 * called from a custom behavior to reuse a built-in one. Only some built-in behaviors are available for
			 * objects controlled entirely by a script.
			 *
			 * @param behavior  Behavior to run
			 * @param draw      Whether the object should be drawn
			 */
			void behave1(std::uint32_t behavior, bool draw);
			/**
			 * @brief Runs the given behavior for the object once
			 *
			 * Accepts any form a @ref jjBEHAVIOR can hold and doesn't change @ref jjOBJ::behavior.
			 *
			 * @param behavior  Behavior to run
			 * @param draw      Whether the object should be drawn
			 */
			void behave2(jjBEHAVIOR behavior, bool draw);
			/**
			 * @brief Runs the given behavior function for the object once
			 *
			 * @param behavior  Function to run
			 * @param draw      Whether the object should be drawn
			 */
			void behave3(jjVOIDFUNCOBJ behavior, bool draw);
			/** @brief Runs `BEHAVIOR::WALKINGENEMY` once, as measured in JJ2+, optionally drawing the object */
			void RunWalkingEnemy(LevelScriptLoader* owner, bool draw);

			/**
			 * @brief Creates a new object
			 *
			 * The object is built from the template in @ref jjObjectPresets for `eventID` and placed at the given position,
			 * which also becomes its @ref jjOBJ::xOrg and @ref jjOBJ::yOrg. Its behavior runs once right away while the object
			 * is still in @ref STATE::START, so passing `behavior` here is not the same as assigning @ref jjOBJ::behavior
			 * afterward, when the template's behavior has already initialized the object. The same applies to the creator.
			 *
			 * @param eventID      Event of the object, usually an @ref OBJECT::Object constant
			 * @param xPixel       Horizontal position in pixels
			 * @param yPixel       Vertical position in pixels
			 * @param creatorID    Object or player that created the object, see @ref jjOBJ::creatorID
			 * @param creatorType  Kind of the creator, see @ref jjOBJ::creatorType
			 * @param behavior     Behavior to use instead of the template's, @ref BEHAVIOR::DEFAULT keeps the template's
			 * @return Object ID of the new object (an index of @ref jjObjects), or 0 on failure
			 */
			static std::int32_t jjAddObject(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, std::uint32_t behavior);
			/**
			 * @brief Creates a new object driven by a behavior function
			 *
			 * Works like the other overload, but the new object gets the given function as its @ref jjOBJ::behavior, and the
			 * function runs once right away.
			 *
			 * @param eventID      Event of the object, usually an @ref OBJECT::Object constant
			 * @param xPixel       Horizontal position in pixels
			 * @param yPixel       Vertical position in pixels
			 * @param creatorID    Object or player that created the object, see @ref jjOBJ::creatorID
			 * @param creatorType  Kind of the creator, see @ref jjOBJ::creatorType
			 * @param behavior     Behavior function of the new object
			 * @return Object ID of the new object (an index of @ref jjObjects), or 0 on failure
			 */
			static std::int32_t jjAddObjectEx(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, jjVOIDFUNCOBJ behavior);

			/**
			 * @brief Removes an object from the level
			 *
			 * The same as calling @ref jjOBJ::delete() on `jjObjects[objectID]`.
			 */
			static void jjDeleteObject(std::int32_t objectID);
			/**
			 * @brief Kills an object
			 *
			 * Unlike @ref jjDeleteObject(), the object dies the regular way first, so whatever it does on death still
			 * happens.
			 */
			static void jjKillObject(std::int32_t objectID);

			/** @brief Horizontal position the object was created at, in pixels */
			float xOrg = 0;
			/** @brief Vertical position the object was created at, in pixels */
			float yOrg = 0;
			/** @brief Current horizontal position in pixels */
			float xPos = 0;
			/** @brief Current vertical position in pixels */
			float yPos = 0;
			/** @brief Horizontal speed in pixels per tick, negative to the left */
			float xSpeed = 0;
			/** @brief Vertical speed in pixels per tick, negative upward */
			float ySpeed = 0;
			/** @brief Horizontal acceleration in pixels per tick squared */
			float xAcc = 0;
			/** @brief Vertical acceleration in pixels per tick squared */
			float yAcc = 0;
			/** @brief General-purpose counter, mostly used by behaviors to time upcoming events */
			std::int32_t counter = 0;
			/**
			 * @brief Frame currently shown for the object
			 *
			 * An index of @ref jjAnimFrames that combines @ref jjOBJ::curAnim and @ref jjOBJ::frameID. Use
			 * @ref jjOBJ::determineCurFrame() to compute it.
			 */
			std::uint32_t curFrame = 0;

			/**
			 * @brief Computes the frame for the current animation and frame index
			 *
			 * The result is the first frame of @ref jjOBJ::curAnim plus @ref jjOBJ::frameID wrapped to the length of the
			 * animation, or 0 if the animation has no frames.
			 *
			 * @param change  Whether to also store the result in @ref jjOBJ::curFrame
			 * @return The computed frame, an index of @ref jjAnimFrames
			 */
			std::uint32_t determineCurFrame(bool change);

			/** @brief General-purpose value, the game doesn't change it by itself */
			std::int32_t age = 0;
			/**
			 * @brief Creator ID and creator type combined into one number
			 *
			 * Use @ref jjOBJ::creatorID and @ref jjOBJ::creatorType instead.
			 */
			mutable std::int32_t creator = 0;

			/** @brief Packs a creator type and a 16-bit creator ID as @ref creator keeps them */
			static std::int32_t PackCreator(std::uint32_t type, std::uint16_t id);
			/**
			 * @brief Returns the ID of the object or player that created this object
			 *
			 * An index of @ref jjObjects or @ref jjPlayers, depending on @ref jjOBJ::creatorType, or 0 if no creator is known.
			 */
			std::uint16_t get_creatorID() const;
			/** @brief Sets the ID of the object or player that created this object */
			std::uint16_t set_creatorID(std::uint16_t value) const;
			/**
			 * @brief Returns what kind of thing created this object
			 *
			 * @ref CREATOR::OBJECT for an object made by another object, @ref CREATOR::PLAYER for one made by a player, or
			 * @ref CREATOR::LEVEL for one placed in the level.
			 */
			std::uint32_t get_creatorType() const;
			/** @brief Sets what kind of thing created this object */
			std::uint32_t set_creatorType(std::uint32_t value) const;

			/**
			 * @brief Animation the object takes its frames from
			 *
			 * An index of @ref jjAnimations. Use @ref jjOBJ::determineCurAnim() to get the value for an animation of a set.
			 */
			int16_t curAnim = 0;

			/**
			 * @brief Computes the animation index for an animation of a set
			 *
			 * The result is the first animation of the set plus `animation`, or 0 if the set isn't available.
			 *
			 * @param setID      Animation set, an @ref ANIM::Set constant is recommended
			 * @param animation  Zero-based index of the animation within the set
			 * @param change     Whether to also store the result in @ref jjOBJ::curAnim
			 * @return The computed animation, an index of @ref jjAnimations
			 */
			int16_t determineCurAnim(std::uint8_t setID, std::uint8_t animation, bool change);
			/**
			 * @brief Computes the animation index for an animation of a set
			 *
			 * The result is the first animation of the set plus `animation`, or 0 if the set isn't available. The set
			 * can also be a custom one.
			 *
			 * @param setID      Animation set
			 * @param animation  Zero-based index of the animation within the set
			 * @param change     Whether to also store the result in @ref jjOBJ::curAnim
			 * @return The computed animation, an index of @ref jjAnimations
			 */
			int16_t determineCurAnimOfSet(std::int32_t setID, std::uint8_t animation, bool change);

			/** @brief Animation used while the object is being destroyed, in the same form as @ref jjOBJ::curAnim */
			std::uint16_t killAnim = 0;
			/**
			 * @brief Remaining time the object stays frozen, in ticks
			 *
			 * 0 when the object isn't frozen. Built-in behaviors count it down, custom behaviors have to do so themselves.
			 */
			std::uint8_t freeze = 0;
			/**
			 * @brief Kind of light the object emits
			 *
			 * One of the @ref LIGHT::Type constants, its size is given by @ref jjOBJ::light.
			 */
			std::uint8_t lightType = 0;
			/** @brief Index of the current frame within @ref jjOBJ::curAnim */
			std::int8_t frameID = 0;
			/**
			 * @brief Legacy hit flags
			 *
			 * Use @ref jjOBJ::bulletHandling, @ref jjOBJ::causesRicochet, @ref jjOBJ::isFreezable and @ref jjOBJ::isBlastable instead.
			 */
			std::int8_t noHit = 0;

			/**
			 * @brief Returns how the object reacts when a bullet touches it
			 *
			 * - @ref HANDLING::HURTBYBULLET --- the object loses energy and the bullet is usually destroyed
			 * - @ref HANDLING::IGNOREBULLET --- neither the object nor the bullet is affected
			 * - @ref HANDLING::DESTROYBULLET --- the bullet is destroyed, the object is unaffected
			 * - @ref HANDLING::DETECTBULLET --- the object is unaffected, the bullet is usually destroyed
			 */
			std::uint32_t get_bulletHandling();
			/** @brief Sets how the object reacts when a bullet touches it */
			std::uint32_t set_bulletHandling(std::uint32_t value);
			/**
			 * @brief Returns whether bullets bounce off the object
			 *
			 * @partialsupport The value is stored, but the game currently doesn't act on it.
			 */
			bool get_ricochet();
			/** @brief Sets whether bullets bounce off the object */
			bool set_ricochet(bool value);
			/**
			 * @brief Returns whether ice bullets freeze the object
			 *
			 * When false, ice bullets hit the object like any other bullet.
			 */
			bool get_freezable();
			/** @brief Sets whether ice bullets freeze the object */
			bool set_freezable(bool value);
			/**
			 * @brief Returns whether explosions affect the object
			 *
			 * @partialsupport The value is stored, but the game currently doesn't act on it.
			 */
			bool get_blastable();
			/** @brief Sets whether explosions affect the object */
			bool set_blastable(bool value);

			/**
			 * @brief Remaining health of the object
			 *
			 * For enemies, the number of hits the object can still take.
			 */
			std::int8_t energy = 0;
			/** @brief Size of the light emitted by the object, see @ref jjOBJ::lightType */
			std::int8_t light = 0;
			/**
			 * @brief Legacy object category
			 *
			 * Use @ref jjOBJ::playerHandling, @ref jjOBJ::isTarget, @ref jjOBJ::triggersTNT, @ref jjOBJ::deactivates and
			 * @ref jjOBJ::scriptedCollisions instead.
			 */
			std::uint8_t objType = 0;

			/**
			 * @brief Returns how the object reacts when a player touches it
			 *
			 * For example, @ref HANDLING::ENEMY hurts the player, @ref HANDLING::PICKUP is collected and @ref HANDLING::SPECIAL runs
			 * code specific to the object. Bullets use @ref HANDLING::PLAYERBULLET or @ref HANDLING::ENEMYBULLET.
			 */
			std::uint32_t get_playerHandling();
			/** @brief Sets how the object reacts when a player touches it */
			std::uint32_t set_playerHandling(std::uint32_t value);
			/**
			 * @brief Returns whether homing attacks go after the object
			 *
			 * @partialsupport The value is stored, but the game currently doesn't act on it.
			 */
			bool get_isTarget();
			/** @brief Sets whether homing attacks go after the object */
			bool set_isTarget(bool value);
			/**
			 * @brief Returns whether the object sets off nearby TNT
			 *
			 * @partialsupport The value is stored, but the game currently doesn't act on it.
			 */
			bool get_triggersTNT();
			/** @brief Sets whether the object sets off nearby TNT */
			bool set_triggersTNT(bool value);
			/**
			 * @brief Returns whether the object is unloaded when far away from players
			 *
			 * When true, the object enters @ref STATE::DEACTIVATE once it gets too far from players. An object placed in the
			 * level then comes back, reset, when a player gets near again.
			 */
			bool get_deactivates();
			/** @brief Sets whether the object is unloaded when far away from players */
			bool set_deactivates(bool value);
			/**
			 * @brief Returns whether the script decides what happens when the object is hit
			 *
			 * When true and @ref jjOBJ::playerHandling is @ref HANDLING::PICKUP or @ref HANDLING::SPECIAL, collisions with players
			 * and bullets call @ref onObjectHit() of the object's behavior class, or the global @ref onObjectHit() function,
			 * instead of the game's own handling.
			 */
			bool get_scriptedCollisions();
			/** @brief Sets whether the script decides what happens when the object is hit */
			bool set_scriptedCollisions(bool value);

			/**
			 * @brief Current state of the object
			 *
			 * One of the @ref STATE::State constants, for example @ref STATE::START for a new object or @ref STATE::KILL for one
			 * that has been destroyed.
			 */
			std::int8_t state = 0;
			/** @brief Score the object is worth when a player destroys it */
			std::uint16_t points = 0;
			/**
			 * @brief Event the object was created from
			 *
			 * Usually compared with @ref OBJECT::Object constants. It is the only reliable way to tell what kind of object
			 * this is, so changing it may make the object behave unexpectedly.
			 */
			std::uint8_t eventID = 0;
			/**
			 * @brief Direction the object faces
			 *
			 * Zero or positive for right, negative for left.
			 */
			std::int8_t direction = 0;
			/**
			 * @brief Ticks left of the white flash shown after a hit
			 *
			 * While non-zero, the object is drawn in a single color. The game counts it down by itself.
			 */
			std::uint8_t justHit = 0;
			/** @brief State the object was in before it got frozen */
			std::int8_t oldState = 0;
			/**
			 * @brief Animation speed, or damage of a bullet
			 *
			 * For bullets, how much damage they deal.
			 */
			std::int32_t animSpeed = 0;
			/**
			 * @brief General-purpose value with a meaning specific to each object
			 *
			 * For bullets, the animation used when they are fired upward.
			 */
			std::int32_t special = 0;

			/**
			 * @brief Returns a general-purpose value of the object
			 *
			 * Each object has a small array of integers that behaviors use for anything they need. Indices 0 to 15 are
			 * available, reading other ones gives 0 and writing them has no effect.
			 */
			std::int32_t get_var(std::uint8_t x);
			/** @brief Sets a general-purpose value of the object */
			std::int32_t set_var(std::uint8_t x, std::int32_t value);

			/** @brief General-purpose value, the game doesn't use it by itself */
			std::uint8_t doesHurt = 0;
			/**
			 * @brief Smaller general-purpose counter
			 *
			 * For bullets, how long they fly before they explode.
			 */
			std::uint8_t counterEnd = 0;
			/** @brief Index of the object in @ref jjObjects */
			std::int16_t objectID = 0;

			/**
			 * @brief Draws the object
			 *
			 * Uses the position, direction and @ref jjOBJ::curFrame of the object, which is enough for most objects.
			 *
			 * @return Always 0
			 * @partialsupport
			 */
			std::int32_t draw();
			/**
			 * @brief Makes the object solid for players
			 *
			 * Solid by its current frame for this tick, where it is. A player standing on it is lifted when it moves up.
			 *
			 * @param shouldCheckForStompingLocalPlayers  A player buttstomping onto it is one more hit (force 1) for
			 *                                            a behavior with @ref scriptedCollisions
			 * @return -1 or 1 while a player pushes it left or right, 0 otherwise
			 */
			std::int32_t beSolid(bool shouldCheckForStompingLocalPlayers);
			/**
			 * @brief Lets players stand on top of the object
			 *
			 * Call it every tick while the object should carry players. Only affects objects of the game driven by a
			 * script behavior.
			 *
			 * @param xOld    Horizontal position of the object in the previous tick
			 * @param yOld    Vertical position of the object in the previous tick
			 * @param width   Width of the platform in pixels, 0 to derive it from the current frame
			 * @param height  Currently ignored
			 * @partialsupport The `height` parameter is currently ignored.
			 */
			void bePlatform(float xOld, float yOld, std::int32_t width, std::int32_t height);
			/**
			 * @brief Releases players standing on the object
			 *
			 * Call it before removing an object that used @ref jjOBJ::bePlatform().
			 */
			void clearPlatform();
			/**
			 * @brief Moves the object down onto the ground below it
			 *
			 * Without `precise`, the result can be a few pixels off, which is fine for most objects. Also resets
			 * @ref jjOBJ::ySpeed.
			 *
			 * @param precise  Whether to place the object exactly
			 */
			void putOnGround(bool precise);
			/**
			 * @brief Makes a bullet bounce off
			 *
			 * @return True if the object is a bullet of the game and bounced, false otherwise
			 */
			bool ricochet();
			/**
			 * @brief Unfreezes the object
			 *
			 * Sets @ref jjOBJ::freeze to 0.
			 *
			 * @param style  Currently ignored
			 * @return Value @ref jjOBJ::freeze had before
			 * @partialsupport The shattering ice effect isn't shown currently. The `style` parameter is currently
			 * ignored.
			 */
			std::int32_t unfreeze(std::int32_t style);
			/**
			 * @brief Removes the object from the level permanently
			 *
			 * The object stops being active immediately.
			 */
			void deleteObject();
			/**
			 * @brief Unloads the object, usually in response to @ref STATE::DEACTIVATE
			 *
			 * An object placed in the level is restored to the event map, so it comes back when a player gets near again.
			 * Any other object is removed permanently.
			 */
			void deactivate();
			/**
			 * @brief Moves the object along a path of waypoints
			 *
			 * @partialsupport Currently has no effect.
			 */
			void pathMovement();
			/**
			 * @brief Fires a bullet from the object
			 *
			 * Creates an object of the given event at the gun spot of the current frame, facing the same way as this object
			 * and with this object as its creator.
			 *
			 * @param eventID  Event of the bullet, usually an @ref OBJECT::Object constant
			 * @return Object ID of the bullet, or 0 on failure
			 */
			std::int32_t fireBullet(std::uint8_t eventID);
			/**
			 * @brief Bursts the current frame of the object into pixel particles
			 *
			 * Same as @ref jjAddParticlePixelExplosion() with the position, frame and direction of the object.
			 *
			 * @param style  Currently ignored, every style looks the same
			 * @partialsupport The `style` parameter is currently ignored.
			 */
			void particlePixelExplosion(std::int32_t style);
			/** @brief Bursts a frame of the animation table into debris where it would be drawn, see @ref particlePixelExplosion() */
			static void AddPixelExplosion(LevelScriptLoader* owner, float xPixel, float yPixel, std::uint32_t frame, std::int32_t direction);
			/**
			 * @brief Possibly drops a random pickup, as destroyed enemies and crates do
			 *
			 * @partialsupport Currently has no effect.
			 */
			void grantPickup(jjPLAYER* player, std::int32_t frequency);

			/**
			 * @brief Finds the nearest player
			 *
			 * @param maxDistance  Squared search radius in pixels
			 * @return Index of the nearest player within the radius, or a negative number if there is none
			 */
			std::int32_t findNearestPlayer(std::int32_t maxDistance) const;
			/**
			 * @brief Finds the nearest player and its distance
			 *
			 * @param maxDistance    Squared search radius in pixels
			 * @param foundDistance  Receives the squared distance of the found player in pixels
			 * @return Index of the nearest player within the radius, or a negative number if there is none
			 */
			std::int32_t findNearestPlayerEx(std::int32_t maxDistance, std::int32_t& foundDistance) const;

			/**
			 * @brief Checks whether the sprites of this object and another object overlap
			 *
			 * Compares the pixels of the current frames at the objects' positions and directions. Handling settings, scaling
			 * and rotation aren't taken into account.
			 *
			 * @param object  Object to test against
			 * @param always  Whether a single shared pixel is enough, otherwise a few are needed (bullets always need just one)
			 */
			bool doesCollide(const jjOBJ* object, bool always) const;
			/**
			 * @brief Checks whether the sprite of this object overlaps a player
			 *
			 * Compares the pixels of the current frame of the object with the hitbox of the player.
			 *
			 * @param object  Player to test against
			 * @param always  Currently ignored
			 * @partialsupport The `always` parameter is currently ignored.
			 */
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
			// The tick a buttstomp onto the solid object was told to its behavior in (see beSolid())
			std::int32_t _stompHitTick = INT32_MIN;
			// The tick a player touching the object was told to its behavior in (see ScriptObjectHost::TouchPlayers())
			std::int32_t _touchTick = INT32_MIN;
			// The last tick a buttstomping player touched it in, the first touch of a buttstomp is the attack
			std::int32_t _stompTouchTick = INT32_MIN;
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
				// Ticks the object is frozen for, of a bullet how long it freezes what it hits (see ScriptObjectHost::PushToActor())
				std::uint8_t Freeze;
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

			/**
			 * @brief Returns the palette color of one pixel of the particle
			 *
			 * @partialsupport Indices 0 to 7 are available, reading other ones gives 0 and writing them has no effect.
			 * Only the first color is currently drawn, as a square of the particle's size.
			 */
			std::uint8_t get_color(std::int32_t i) const;
			/** @brief Sets the palette color of one pixel of the particle */
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
			/**
			 * @brief Returns the text the particle shows
			 *
			 * Longer text is cut to 9 characters.
			 */
			String get_text() const;
			/** @brief Sets the text the particle shows */
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

		/**
		 * @brief A simple visual effect in the level
		 *
		 * Particles are lightweight effects such as sparks, smoke, debris, weather or floating score text. They don't
		 * interact with objects or players and disappear on their own after a while. Up to 1024 particles exist at once, all listed in @ref jjParticles. Create new ones with @ref jjAddParticle(). Each type
		 * keeps its own extra properties in the member of the same name, for example @ref jjPARTICLE::fire for
		 * @ref PARTICLE::FIRE.
		 */
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
			/** @brief Horizontal speed in pixels per tick */
			float xSpeed;
			/** @brief Vertical speed in pixels per tick */
			float ySpeed;

			/**
			 * @brief Type of the particle
			 *
			 * One of the @ref PARTICLE::Type constants, @ref PARTICLE::INACTIVE for a free slot. Decides which of the
			 * type-specific members applies.
			 */
			std::uint8_t particleType;
			/**
			 * @brief Whether the particle is in use
			 *
			 * Set when a particle is created and cleared when it disappears. Checking @ref jjPARTICLE::type against
			 * @ref PARTICLE::INACTIVE is the more common way to tell.
			 */
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

	/**
	 * @brief Returns all objects of the level, indexed by object ID
	 *
	 * Slots up to @ref jjObjectCount may hold objects, check @ref jjOBJ::isActive to skip the free ones.
	 */
	jjOBJ* get_jjObjects(std::int32_t index);
	/**
	 * @brief Returns the templates of new objects, indexed by event ID
	 *
	 * Every object created from an event starts as a copy of its template, so changing a template (best done in
	 * @ref onLevelLoad()) changes all objects of that event created afterward, including their @ref jjOBJ::behavior.
	 */
	jjOBJ* get_jjObjectPresets(std::int8_t id);

	/**
	 * @brief Breaks a tile into falling pieces, like destroyed scenery
	 *
	 * @partialsupport Currently has no effect.
	 */
	void jjAddParticleTileExplosion(std::uint16_t xTile, std::uint16_t yTile, std::uint16_t tile, bool collapseSceneryStyle);
	/**
	 * @brief Bursts a sprite frame into pixel particles
	 *
	 * The pieces take the shape of the frame, drawn as it would be at the given position.
	 *
	 * @param xPixel     Horizontal position in pixels
	 * @param yPixel     Vertical position in pixels
	 * @param curFrame   Frame to burst, an index of @ref jjAnimFrames
	 * @param direction  Negative to flip the frame horizontally
	 * @param mode       Currently ignored, every mode looks the same
	 * @partialsupport The `mode` parameter is currently ignored.
	 */
	void jjAddParticlePixelExplosion(float xPixel, float yPixel, int curFrame, int direction, int mode);

	/** @brief Returns all particles of the level, indexed from 0 to 1023 */
	jjPARTICLE* GetParticle(std::int32_t index);

	/**
	 * @brief Creates a new particle
	 *
	 * The particle starts at position 0, 0 with default values for its type, so at least set its position afterward.
	 *
	 * @param particleType  Type of the particle, @ref PARTICLE::INACTIVE is not allowed
	 * @return The new particle, or null if all particle slots are in use or the type is invalid
	 */
	jjPARTICLE* AddParticle(std::int32_t particleType);
}

#endif
