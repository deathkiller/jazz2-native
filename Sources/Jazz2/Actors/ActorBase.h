#pragma once

#include "../EventType.h"
#include "../LightEmitter.h"
#include "../Resources.h"
#include "../SfxFlags.h"
#include "../Tiles/TileCollisionParams.h"

#include "../../nCine/Base/Task.h"
#include "../../nCine/Primitives/AABB.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"
#include "../../nCine/Graphics/BaseSprite.h"
#include "../../nCine/Graphics/SceneNode.h"

#include <cstring>

#include <Base/Memory.h>
#include <Base/TypeInfo.h>
#include <Containers/Function.h>
#include <Containers/StringView.h>
#include <IO/MemoryStream.h>

#ifndef DOXYGEN_GENERATING_OUTPUT
// If coroutines are not supported, load resources synchronously
#if defined(WITH_COROUTINES)
#	define async_return co_return
#	define async_await co_await
#else
#	define async_return return
#	define async_await
#endif
#endif

using namespace Death::Containers::Literals;
using namespace Jazz2::Resources;

namespace Jazz2
{
	class ILevelHandler;
	class LevelHandler;
}

namespace Jazz2::Rendering
{
	class LightingRenderer;
	class CombineRenderer;
	class XRayMaskRenderer;
}

#if defined(WITH_MULTIPLAYER)
namespace Jazz2::Multiplayer
{
	class MpLevelHandler;
}
namespace Jazz2::UI::Multiplayer
{
	class MpInGameCanvasLayer;
}
#endif

#if defined(WITH_PHYSICS_PROBE)
namespace Jazz2::Tests
{
	class PhysicsProbe;
}
#endif

#if defined(WITH_EDITOR)
namespace Jazz2::Editor
{
	class PlayTestDebug;
}
#endif

#if defined(WITH_ANGELSCRIPT)
namespace Jazz2::Scripting::Legacy
{
	class ScriptObjectHost;
}
#endif

namespace Jazz2::Actors
{
	class Player;
	class ActorBase;

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)
	/**
		@brief Takes over the per-frame update of an object

		Set by @ref ActorBase::SetUpdateOverride(), lets a level script decide when the object does what it would
		do by itself.
	*/
	class IActorUpdateOverride
	{
	public:
		virtual ~IActorUpdateOverride() {}

		/** @brief Called instead of the per-frame update of the object */
		virtual void OnUpdate(ActorBase* actor, float timeMult) = 0;
		/** @brief Returns `false` if the object isn't drawn in this frame */
		virtual bool IsDrawn() const = 0;
		/**
		 * @brief Called before the object and another one handle their collision
		 *
		 * Returns `true` if handled, neither of them is told about it then.
		 */
		virtual bool OnCollision(ActorBase* actor, ActorBase* other) {
			return false;
		}
		/**
		 * @brief Called before the hitbox is made of the bounding box, see @ref ActorBase::UpdateHitbox()
		 *
		 * Returns `true` if @ref ActorBase::AABBInner is set already.
		 */
		virtual bool OnUpdateHitbox(ActorBase* actor) {
			return false;
		}
		/** @brief Called after the object emitted its own lights, see @ref ActorBase::EmitLights() */
		virtual void OnEmitLights(ActorBase* actor, SmallVectorImpl<LightEmitter>& lights) {
		}
		/** @brief Called when the object runs out of health, returns `true` if it lives on */
		virtual bool OnPerish(ActorBase* actor, ActorBase* collider) {
			return false;
		}
		/** @brief Returns `true` if the override makes the debris instead of the object */
		virtual bool OwnsDebris() const {
			return false;
		}
		/** @brief Returns `true` if the override emits all lights instead of the object, see @ref OnEmitLights() */
		virtual bool OwnsLights() const {
			return false;
		}
		/** @brief Called when the object is out of reach of the players, returns `false` if the override handles it */
		virtual bool OnTileDeactivated(ActorBase* actor) {
			return true;
		}
	};
#endif

	/**
		@brief Flags that modify behaviour of @ref ActorBase
		
		Mixes instantiation hints, per-instance state and collision options. Supports a bitwise combination of its
		member values.
	*/
	enum class ActorState {
		None = 0x00,

		/** @brief Actor is created from event map, this flag is used automatically by event system */
		IsCreatedFromEventMap = 0x01,
		/** @brief Actor is created by generator, this flag is used automatically by event system */
		IsFromGenerator = 0x02,

		/** @brief Actor should be illuminated */
		Illuminated = 0x04,

		/** @brief Actor should be created asynchronously, not to block main thread */
		Async = 0x08,

		/** @brief Mask of all instantiation flags that can be used in @ref ActorActivationDetails */
		InstantiationFlags = IsCreatedFromEventMap | IsFromGenerator | Illuminated | Async,

		/** @brief This flag is set automatically after call to @ref ActorBase::OnActivatedAsync() */
		Initialized = 0x0100,

		// Actor instance flags
		/** @brief Actor is invulnerable */
		IsInvulnerable = 0x0200,
		/** @brief Actor is standing on the ground and can jump */
		CanJump = 0x0400,
		/** @brief Actor can be frozen */
		CanBeFrozen = 0x0800,
		/** @brief Actor is facing left */
		IsFacingLeft = 0x1000,

		/** @brief Actor keeps sliding while frozen instead of stopping dead */
		KeepsMomentumWhenFrozen = 0x2000,

		/** @brief Actor should be preserved when state is rolled back to checkpoint */
		PreserveOnRollback = 0x4000,

		// Collision flags
		/** @brief Collide with tiles */
		CollideWithTileset = 0x10000,
		/** @brief Collide with other non-solid actors, @ref ActorBase::OnHandleCollision() will be called for each collision */
		CollideWithOtherActors = 0x20000,
		/** @brief Collide with solid objects */
		CollideWithSolidObjects = 0x40000,
		/** @brief Don't add object to collision tree at all (cannot be changed during object lifetime) */
		ForceDisableCollisions = 0x80000,

		/** @brief Check collisions at the end of current frame, should be used if position or size changed */
		IsDirty = 0x100000,
		/** @brief Remove object at the end of current frame */
		IsDestroyed = 0x200000,

		/** @brief Apply gravitation */
		ApplyGravitation = 0x400000,
		/** @brief Marks object as solid, so other objects with @ref CollideWithSolidObjects will collide with it */
		IsSolidObject = 0x800000,
		/** @brief Check collisions only with hitbox */
		SkipPerPixelCollisions = 0x1000000,

		/** @brief Don't use full hitbox for collisions with tiles, also exclude upper part of hitbox when falling down */
		CollideWithTilesetReduced = 0x2000000,
		/** @brief Collide with other solid object only if it's above center of the other hitbox */
		CollideWithSolidObjectsBelow = 0x4000000,
		/** @brief Ignore solid collisions against similar objects that have this flag */
		ExcludeSimilar = 0x8000000,
		/** @brief Tiles players hang from (vines, hooks) are solid to it as well */
		CollideWithSuspendTiles = 0x10000000,
		/** @brief Original player's tile collision - reduced hitbox, caught by its head, lifted only straight up, only its feet find the floor */
		CollideWithTilesetLegacy = 0x20000000,
	};

	DEATH_ENUM_FLAGS(ActorState);

	/**
		@brief Description how to initialize an actor
		
		Passed to @ref ActorBase::OnActivated() and @ref ActorBase::OnActivatedAsync() when an actor is spawned.
	*/
	struct ActorActivationDetails {
		/** @brief Current level handler */
		ILevelHandler* LevelHandler;
		/** @brief Position */
		Vector3i Pos;
		/** @brief Actor state */
		ActorState State;
		/** @brief Event type */
		EventType Type;
		/** @brief Event activation parameters */
		const std::uint8_t* Params;

		ActorActivationDetails(ILevelHandler* levelHandler, const Vector3i& pos, const std::uint8_t* params = nullptr)
			: LevelHandler(levelHandler), Pos(pos), State(ActorState::None), Type(EventType::Empty), Params(params)
		{
		}
	};

	/**
		@brief Typed accessor over the raw event activation parameter buffer

		Reads unaligned little-endian values from @ref ActorActivationDetails::Params. Prefer it over type-punning
		the raw buffer.
	*/
	struct EventParamsReader {
		explicit EventParamsReader(const ActorActivationDetails& details) noexcept
			: _params(details.Params) {}
		explicit EventParamsReader(const std::uint8_t* params) noexcept
			: _params(params) {}

		/** @brief Reads a boolean at the given byte offset */
		bool GetBool(std::int32_t offset) const noexcept {
			return (_params[offset] != 0);
		}
		/** @brief Reads an unsigned 8-bit value at the given byte offset */
		std::uint8_t GetUint8(std::int32_t offset) const noexcept {
			return _params[offset];
		}
		/** @brief Reads a signed 8-bit value at the given byte offset */
		std::int8_t GetInt8(std::int32_t offset) const noexcept {
			return (std::int8_t)_params[offset];
		}
		/** @brief Reads an unsigned 16-bit value at the given byte offset */
		std::uint16_t GetUint16(std::int32_t offset) const noexcept {
			std::uint16_t value;
			std::memcpy(&value, &_params[offset], sizeof(value));
			return Death::Memory::AsLE(value);
		}
		/** @brief Reads a signed 16-bit value at the given byte offset */
		std::int16_t GetInt16(std::int32_t offset) const noexcept {
			std::int16_t value;
			std::memcpy(&value, &_params[offset], sizeof(value));
			return Death::Memory::AsLE(value);
		}
		/** @brief Reads an unsigned 32-bit value at the given byte offset */
		std::uint32_t GetUint32(std::int32_t offset) const noexcept {
			std::uint32_t value;
			std::memcpy(&value, &_params[offset], sizeof(value));
			return Death::Memory::AsLE(value);
		}
		/** @brief Reads a 32-bit floating-point value at the given byte offset */
		float GetFloat(std::int32_t offset) const noexcept {
			std::uint32_t value;
			std::memcpy(&value, &_params[offset], sizeof(value));
			value = Death::Memory::AsLE(value);
			float result;
			std::memcpy(&result, &value, sizeof(result));
			return result;
		}

	private:
		const std::uint8_t* _params;
	};

	/**
		@brief Writes typed values into a packed event parameter buffer

		Counterpart of @ref EventParamsReader for parameters built at runtime, in the packed little-endian
		layout of the level file.
	*/
	struct EventParamsWriter {
		explicit EventParamsWriter(std::uint8_t* params) noexcept
			: _params(params) {}

		/** @brief Writes an unsigned 8-bit value at the given byte offset */
		void SetUint8(std::int32_t offset, std::uint8_t value) noexcept {
			_params[offset] = value;
		}
		/** @brief Writes an unsigned 16-bit value at the given byte offset */
		void SetUint16(std::int32_t offset, std::uint16_t value) noexcept {
			value = Death::Memory::AsLE(value);
			std::memcpy(&_params[offset], &value, sizeof(value));
		}
		/** @brief Writes a signed 16-bit value at the given byte offset */
		void SetInt16(std::int32_t offset, std::int16_t value) noexcept {
			value = Death::Memory::AsLE(value);
			std::memcpy(&_params[offset], &value, sizeof(value));
		}
		/** @brief Writes an unsigned 32-bit value at the given byte offset */
		void SetUint32(std::int32_t offset, std::uint32_t value) noexcept {
			value = Death::Memory::AsLE(value);
			std::memcpy(&_params[offset], &value, sizeof(value));
		}
		/** @brief Writes a 32-bit floating-point value at the given byte offset */
		void SetFloat(std::int32_t offset, float value) noexcept {
			std::uint32_t raw;
			std::memcpy(&raw, &value, sizeof(raw));
			raw = Death::Memory::AsLE(raw);
			std::memcpy(&_params[offset], &raw, sizeof(raw));
		}

	private:
		std::uint8_t* _params;
	};

	/**
		@brief Move type
		
		How @ref ActorBase::MoveInstantly() interprets the target position. Supports a bitwise combination of its
		member values.
	*/
	enum class MoveType {
		Absolute = 0x00,		/**< Move to absolute position */
		Relative = 0x01,		/**< Move to relative position */
		Force = 0x02			/**< Ignore all collision checks */
	};

	DEATH_ENUM_FLAGS(MoveType);

	/**
		@brief Actor renderer type
		
		Visual mode of @ref ActorBase::ActorRenderer, e.g. an outline, a hit flash or the frozen effect.
	*/
	enum class ActorRendererType {
		Default,				/**< Default rendering */
		Outline,				/**< Draw outline around the sprite */
		WhiteMask,				/**< Draw all non-transparent pixels as white */
		PartialWhiteMask,		/**< Draw all non-transparent pixels as boosted shades of gray */
		FrozenMask				/**< Apply frozen effect to the sprite */
	};

	/**
		@brief Effect type of @ref ActorBase::CreateParticleDebrisOnPerish()
		
		Selects the debris an actor leaves behind when it perishes.
	*/
	enum class ParticleDebrisEffect {
		Unknown,				/**< Unspecified */
		Standard,				/**< Standard */
		StandardInWater,		/**< Standard (in water) */
		Dissolve,				/**< Dissolve (for ghosts) */
		Frozen,					/**< Frozen (for breaking the ice) */
		Fire,					/**< Fire (for toasting) */
		Lightning				/**< Lightning (for electrocuting) */
	};

	/**
		@brief Base class of an object
		
		Shared lifecycle of every object in the game world: activation, update, movement, collisions, rendering,
		lights and health. Derived actors override the virtual callbacks.
	*/
	class ActorBase : public std::enable_shared_from_this<ActorBase>
	{
		DEATH_RUNTIME_OBJECT();

		friend class Player;
		friend class Jazz2::LevelHandler;
		friend class Jazz2::Rendering::LightingRenderer;
		// Software renderer approximates the dynamic lighting in the combine step
		friend class Jazz2::Rendering::CombineRenderer;
		// Draws actor silhouettes behind x-ray tile layers
		friend class Jazz2::Rendering::XRayMaskRenderer;
#if defined(WITH_MULTIPLAYER)
		friend class Jazz2::Multiplayer::MpLevelHandler;
		friend class Jazz2::UI::Multiplayer::MpInGameCanvasLayer;
#endif
#if defined(WITH_PHYSICS_PROBE)
		// Reads position and speed, resets the player between scenarios
		friend class Jazz2::Tests::PhysicsProbe;
#endif
#if defined(WITH_EDITOR)
		// Stops a player moved by hand, destroys objects of events edited during play
		friend class Jazz2::Editor::PlayTestDebug;
#endif
#if defined(WITH_ANGELSCRIPT)
		// Drives objects by level script behaviors
		friend class Jazz2::Scripting::Legacy::ScriptObjectHost;
#endif

	public:
		/** @brief Creates a new instance */
		ActorBase();
		virtual ~ActorBase();

		/** @brief Outer AABB hitbox */
		AABBf AABB;
		/** @brief Inner AABB hitbox */
		AABBf AABBInner;

		/** @brief Returns `true` if the object is currently facing left */
		bool IsFacingLeft() const;

		/** @brief Called after the object is created */
		Task<bool> OnActivated(const ActorActivationDetails& details);
		/** @brief Called when the object collides with another object */
		virtual bool OnHandleCollision(ActorBase* other);
		/** @brief Called to check whether @p collider can cause damage to the object */
		virtual bool CanCauseDamage(ActorBase* collider);

		/** @brief Returns `true` if the object is invulnerable */
		bool IsInvulnerable();
		/** @brief Returns `true` if the object is frozen */
		bool IsFrozen() const;
		/** @brief Returns current health */
		std::int32_t GetHealth();
		/** @brief Sets current health */
		void SetHealth(std::int32_t value);
		/** @brief Returns maximum health */
		std::int32_t GetMaxHealth();
		/** @brief Decreases health by specified amount */
		void DecreaseHealth(std::int32_t amount = 1, ActorBase* collider = nullptr);

		/** @brief Moves the object */
		bool MoveInstantly(Vector2f pos, MoveType type, Tiles::TileCollisionParams& params);
		/** @overload */
		bool MoveInstantly(Vector2f pos, MoveType type) {
			Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, _speed.Y >= 0.0f };
			return MoveInstantly(pos, type, params);
		}

		/** @brief Adds external force */
		void AddExternalForce(float x, float y);

		/** @brief Returns `true` if this object is colliding with a given object */
		bool IsCollidingWith(ActorBase* other);
		/** @brief Returns `true` if this object is colliding with a given AABB */
		bool IsCollidingWith(const AABBf& aabb);
		/**
		 * @brief Returns `true` if this object crossed over a given object during the current frame
		 *
		 * Tests the path taken, so it catches a fast object passing a small one between frames, which
		 * @ref IsCollidingWith() misses.
		 */
		bool HasCrossedOver(const ActorBase* other) const;
		/** @brief Updates AABB for current position, rotation and animation frame */
		void UpdateAABB();
		/**
		 * @brief Re-applies the current position to the renderer
		 *
		 * Called after the object updates itself, call it again if something else moves the object afterwards.
		 * An object drawn elsewhere than it is simulated overrides it.
		 */
		virtual void UpdateRendererPosition();

		/** @brief Returns current position */
		Vector2f GetPos() {
			return _pos;
		}

		/** @brief Returns current speed */
		Vector2f GetSpeed() {
			return _speed;
		}

		/** @brief Returns actor state */
		constexpr ActorState GetState() const noexcept {
			return _state;
		}

		/** @overload */
		constexpr bool GetState(ActorState flag) const noexcept {
			return (_state & flag) == flag;
		}

		/** @brief Returns event type the object was spawned from, or @ref EventType::Empty if it wasn't created from an event */
		EventType GetSpawnEventType() const noexcept {
			return _spawnEventType;
		}

		/** @brief Returns parameters of the event the object was spawned from, see @ref GetSpawnEventType() */
		const std::uint8_t* GetSpawnEventParams() const noexcept {
			return _spawnEventParams;
		}

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Hands the per-frame update over to an override, or takes it back with `nullptr`
		 *
		 * The override then decides when the object updates, see @ref IActorUpdateOverride.
		 */
		void SetUpdateOverride(std::unique_ptr<IActorUpdateOverride> value);
		/** @brief Returns the update override, or `nullptr` */
		IActorUpdateOverride* GetUpdateOverride() const {
			return _updateOverride.get();
		}
		/** @brief Lets the overrides of two colliding objects handle the collision, returns `true` if one did */
		static bool HandleCollisionByOverride(ActorBase* a, ActorBase* b) {
			return (a->_updateOverride != nullptr && a->_updateOverride->OnCollision(a, b)) ||
				(b->_updateOverride != nullptr && b->_updateOverride->OnCollision(b, a));
		}
#endif

		/** @brief Adds the lights of the object and of its update override */
		void EmitLights(SmallVectorImpl<LightEmitter>& lights);

	protected:
		/** @brief Actor renderer */
		class ActorRenderer : public BaseSprite
		{
			friend class ActorBase;

		public:
			/**
			 * @brief Creates a new instance
			 *
			 * @param owner  Actor that owns this renderer
			 */
			ActorRenderer(ActorBase* owner);

			/** @brief Sheet the frames come from, or `nullptr` when the frames form a regular grid */
			const Resources::GenericGraphicResource* FrameSource;
			/** @brief Frame configuration */
			Vector2i FrameConfiguration;
			/** @brief Frame dimensions */
			Vector2i FrameDimensions;
			/** @brief Hotspot of the current animation, never mirrored */
			Vector2f Hotspot;
			/** @brief Animation loop mode */
			AnimationLoopMode LoopMode;
			/** @brief Frame offset */
			std::int32_t FirstFrame;
			/** @brief Frame count */
			std::int32_t FrameCount;
			/** @brief Animation duration (in normalized frames) */
			float AnimDuration;
			/** @brief Current animation progress */
			float AnimTime;
			/** @brief Current animation frame */
			std::int32_t CurrentFrame;
			/** @brief Whether the animation is paused */
			bool AnimPaused;

			/** @brief Initializes the renderer to the specified renderer type */
			void Initialize(ActorRendererType type);

			/**
			 * @brief Selects a recolor palette override
			 *
			 * Flat offset into the shared palette texture, or -1 for none. Takes precedence over @ref SetIndexed.
			 */
			void SetPalette(std::int32_t paletteOffset);

			/**
			 * @brief Marks whether the current graphic is indexed and which palette region it samples
			 *
			 * Set automatically by @ref ActorBase::RefreshAnimation, a @ref SetPalette override wins.
			 */
			void SetIndexed(bool indexed, std::int32_t basePaletteOffset);

			void OnUpdate(float timeMult) override;
			bool OnDraw(RenderQueue& renderQueue) override;

			/** @brief Returns `true` if animation is running */
			bool IsAnimationRunning();
			/** @brief Returns active renderer type */
			ActorRendererType GetRendererType() const;

			/**
			 * @brief Points the sprite's anchor at the hotspot of the frame being shown
			 *
			 * A flipped hotspot is mirrored within the frame's own area, not the animation's cell.
			 */
			void UpdateAnchor();

		protected:
			void TextureHasChanged(Texture* newTexture) override;

		private:
			// Current graphic is indexed, so it uses the palette shader even without an override
			bool _baseIndexed;
			ActorRendererType _rendererType;
			ActorBase* _owner;
			float _rendererTransition;
			// Recolor override offset into the shared palette texture, or -1
			std::int32_t _paletteOffset;
			// Palette offset of the current indexed graphic
			std::int32_t _basePaletteOffset;

			void UpdateVisibleFrames();
			// Re-applies the renderer type so a palette change swaps the shader
			void ReinitializeCurrentType();
			static std::int32_t NormalizeFrame(std::int32_t frame, std::int32_t min, std::int32_t max);
		};

		/** @{ @name Constants */

		/** @brief Alpha transparency threshold, same as the collision masks */
		static constexpr std::uint8_t AlphaThreshold = MaskAlphaThreshold;
		/** @brief Step for collision checking */
		static constexpr float CollisionCheckStep = 0.5f;
		// Horizontal travel per original tick from which `_airFloorSnapReach` applies, tested after the tick's brake
		static constexpr float AirFloorSnapMinStepX = 1.0f;
		// How far a tick's fall may end in the floor and still be stepped onto it, plus the sideways speed in px/tick
		static constexpr float LandingStepHeight = 4.0f;
		/** @brief Longest single collision sub-step of @ref TryStandardMovement() */
		static constexpr float MaxMovementStep = 8.0f;
		/** @brief Most collision sub-steps of @ref TryStandardMovement() per frame */
		static constexpr std::int32_t MaxMovementSubsteps = 24;
		/** @brief Farthest @ref TryUnstuck() moves an actor out of solid geometry */
		static constexpr float MaxUnstuckDistance = 24.0f;
		/** @brief Frames before @ref TryUnstuck() probes again for an actor that wasn't stuck */
		static constexpr float UnstuckRetryNotStuck = 8.0f;
		/** @brief Frames before @ref TryUnstuck() searches again for a trapped actor */
		static constexpr float UnstuckRetryTrapped = 20.0f;
		/** @brief Frames before @ref TryUnstuck() runs again for an actor it successfully freed */
		static constexpr float UnstuckCooldown = 60.0f;
		/** @brief Step for per-pixel collisions */
		static constexpr std::int32_t PerPixelCollisionStep = 3;
		/** @brief Maximum number of animation candidates */
		static constexpr std::int32_t AnimationCandidatesCount = 5;
		/** @brief Size of spawn event parameters, same as @ref Events::EventSpawner::SpawnParamsSize */
		static constexpr std::int32_t SpawnParamsSize = 16;

		/** @} */

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		// Ordered by alignment to avoid padding
		ILevelHandler* _levelHandler;
		Metadata* _metadata;
		GraphicResource* _currentAnimation;
		GraphicResource* _currentTransition;
#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Update override, see @ref SetUpdateOverride() */
		std::unique_ptr<IActorUpdateOverride> _updateOverride;
#endif

		Vector2f _pos;
		/** @brief Position at the start of the current frame, where the path taken begins */
		Vector2f _frameStartPos;
		Vector2f _speed;
		Vector2f _externalForce;
		float _internalForceY;
		float _elasticity;
		float _friction;
		/** @brief Cap on applied upward speed per frame (0 = none), for the original's constant-speed rise */
		float _maxRiseSpeed = 0.0f;
		/** @brief Per-frame clamp on @ref _speed Y, raised for forced transport (e.g. sucker tubes) */
		float _verticalSpeedLimit = 16.0f;
		/** @brief Per-frame clamp on @ref _speed X, raised for the non-Reforged dash */
		float _horizontalSpeedLimit = 16.0f;
		/**
		 * @brief Cap on applied horizontal movement per frame (0 = none)
		 *
		 * Like @ref _maxRiseSpeed, @ref _speed X keeps its momentum, as in the original's dash.
		 */
		float _maxAppliedSpeedX = 0.0f;
		/**
		 * @brief How far below a ledge's top a descending actor may land on it (0 = sub-step reach only)
		 *
		 * Only the player sets it, see @ref Player::LegacyLandingTolerance.
		 */
		float _landingTolerance = 0.0f;
		/**
		 * @brief How far the floor still holds the actor up past either side of its hitbox (0 = the hitbox only)
		 *
		 * Only the player sets it, see @ref Player::LegacyFootingExtraLeft.
		 */
		float _footingExtraLeft = 0.0f;
		/** @brief See @ref _footingExtraLeft */
		float _footingExtraRight = 0.0f;
		/** @brief Whether footing found at the start of a tick holds for that tick, as the original's player */
		bool _footingGrace = false;
		/** @brief Whether each tick's gravity is applied before that tick's move, as the original's player */
		bool _gravityBeforeMove = false;
		/** @brief Whether mostly-horizontal movement in the air climbs a gentle upslope instead of hitting it */
		bool _climbsSlopesInAir = true;
		/** @brief Whether a landing made by the air floor snap can lose the next tick's move, as the original's player */
		bool _landingStall = false;
		/** @brief Frames left during which the actor doesn't move, see @ref _landingStall */
		float _moveStallLeft = 0.0f;
		/** @brief Fastest applied descent (0 = no cap), only the player sets it */
		float _maxFallSpeed = 0.0f;
		/** @brief How much the horizontal speed changed this frame before the move, consumed by the move */
		float _frameSpeedChangeX = 0.0f;
		/**
		 * @brief How far below its move a falling actor moving sideways still lands on a floor (0 = off)
		 *
		 * Only the player sets it, see @ref Player::LegacyAirFloorSnap.
		 */
		float _airFloorSnapReach = 0.0f;
		float _unstuckCooldown;
		float _frozenTimeLeft;
		std::int32_t _maxHealth;
		std::int32_t _health;

		Vector2i _originTile;
		float _spawnFrames;
		/** @brief Position and layer the object was activated at, to recreate it from a snapshot */
		Vector3i _spawnPos;
		/** @brief Spawn event parameters, valid only if @ref _spawnEventType is set */
		std::uint8_t _spawnEventParams[SpawnParamsSize];
		/** @brief Event type the object was spawned from, or @ref EventType::Empty */
		EventType _spawnEventType;
		bool _thawPending = false;
		bool _currentTransitionCancellable;

		ActorRenderer _renderer;
#endif

		/** @brief Sets internal parent node */
		void SetParent(SceneNode* parent);

		/** @brief Sets whether the object is facing left */
		void SetFacingLeft(bool value);

		/** @brief Called when the object is created and activated */
		virtual Task<bool> OnActivatedAsync(const ActorActivationDetails& details);
		/** @brief Called when corresponding tile should be deactivated */
		virtual bool OnTileDeactivated();

		/**
		 * @brief Returns `true` if the live state of the object can be stored in a level state snapshot
		 *
		 * Only for objects spawned from an event registered in @ref Events::EventSpawner. Short-lived objects
		 * can opt out by overriding it.
		 */
		virtual bool IsSerializable() const;
		/**
		 * @brief Serializes the live state of the object to a level state snapshot
		 *
		 * State set from the event parameters needn't be stored, the object is spawned again with them. Derived
		 * classes call the base first and read the data back in the same order.
		 */
		virtual void OnSerializeState(Stream& dest);
		/**
		 * @brief Restores the live state of the object from a level state snapshot
		 *
		 * Called after @ref OnActivatedAsync(), before the object is added to the level. Running transitions
		 * aren't restored, so the restored state must not wait for a transition callback.
		 */
		virtual void OnDeserializeState(Stream& src);

		/** @brief Called when the object is attached to an another object */
		virtual void OnAttach(ActorBase* parent);
		/** @brief Called when the object is detached from the previously attached object */
		virtual void OnDetach(ActorBase* parent);

		/** @brief Called when health of the object changed */
		virtual void OnHealthChanged(ActorBase* collider);
		/** @brief Called when the object has no health left and should perish */
		virtual bool OnPerish(ActorBase* collider);

		/** @brief Called every frame to update the object state */
		virtual void OnUpdate(float timeMult);
		/** @brief Called when the hitbox needs to be updated */
		virtual void OnUpdateHitbox();
		/** @brief Called when the object needs to be drawn */
		virtual bool OnDraw(RenderQueue& renderQueue);
		/** @brief Called when emitting lights */
		virtual void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) { }
		/**
		 * @brief Called when emitting lights that have to be replicated to remote peers
		 *
		 * Defaults to @ref OnEmitLights(). Override it to leave out lights that clients produce on their own.
		 */
		virtual void OnEmitRemotedLights(SmallVectorImpl<LightEmitter>& lights) {
			OnEmitLights(lights);
		}
		/**
		 * @brief Whether an observer reproduces this object's @ref ActorState::Illuminated decoration itself
		 *
		 * Only for objects that turn the flag into lights. They leave those lights out of
		 * @ref OnEmitRemotedLights() and every observer spawns its own.
		 */
		virtual bool IsIlluminatedStateRemoted() const {
			return false;
		}
		/** @brief Returns how fast the object slides while frozen, see @ref ActorState::KeepsMomentumWhenFrozen */
		float GetFrozenMomentumX() const;
		/** @brief Called when the object hits a floor */
		virtual void OnHitFloor(float timeMult);
		/** @brief Called when the object hits a ceiling */
		virtual void OnHitCeiling(float timeMult);
		/** @brief Called when the object hits a wall */
		virtual void OnHitWall(float timeMult);
		/** @brief Returns the gravity applied this frame, may depend on direction (e.g., the player's jump arc) */
		virtual float GetGravityModifier(float baseGravity, bool isRising) const {
			return baseGravity;
		}
		/** @brief Sets how long the animation or transition just started lasts, in ticks of the original game (70 a second) */
		void SetAnimationTicks(float ticks) {
			_renderer.AnimDuration = ticks / 70.0f;
		}
		/** @brief Returns @p baseGravity with the gravity of the level replaced by the given one, for an override of @ref GetGravityModifier() */
		float ReplaceLevelGravity(float gravity, float baseGravity) const;

		/** @brief Called when an event is triggered */
		virtual void OnTriggeredEvent(EventType eventType, std::uint8_t* eventParams);

		/** @brief Performs standard movement behavior */
		void TryStandardMovement(float timeMult, Tiles::TileCollisionParams& params);
		/**
		 * @brief Discards the path the object travelled so far during the current frame
		 *
		 * Call it after assigning the position directly for a warp, respawn or re-sync, otherwise
		 * @ref HasCrossedOver() hits everything on the way. @ref MoveInstantly() does it for absolute moves.
		 */
		void ResetPathTracking() {
			_frameStartPos = _pos;
		}
		/**
		 * @brief Tries to push the actor out of solid geometry it ended up inside of
		 *
		 * Probes are non-destructive, so they never destroy a tile or count as a weapon hit.
		 *
		 * @return `true` if the actor was moved to a free position
		 */
		bool TryUnstuck();
		/** @brief Updates hitbox to a given size */
		void UpdateHitbox(std::int32_t w, std::int32_t h);
		/** @brief Updates frozen state of the object, returns `true` if it thawed in this frame */
		bool UpdateFrozenState(float timeMult, bool frictionApplied = false);
		/** @brief Slows a frozen object that keeps its momentum by one frame's friction */
		void ApplyFrozenFriction(float timeMult);
		/** @brief Ends the freeze at once, the ice breaks in the next @ref UpdateFrozenState() */
		void ThawOnNextUpdate();
		/** @brief Handles change of frozen state after collision with other object */
		void HandleFrozenStateChange(ActorBase* shot);

		/** @brief Creates a particle debris from a sprite when the object is going to perish */
		void CreateParticleDebrisOnPerish(ActorBase* collider);
		/** @overload */
		void CreateParticleDebrisOnPerish(ParticleDebrisEffect effect, Vector2f speed);
		/** @brief Creates a sprite debris, its pieces keep @p elasticity of their speed on bounce */
		void CreateSpriteDebris(AnimState state, std::int32_t count, float elasticity = 0.8f);
		/** @brief Returns scale of ice shrapnels */
		virtual float GetIceShrapnelScale() const;

		/** @brief Plays a sound effect for the object */
		std::shared_ptr<AudioBufferPlayer> PlaySfx(StringView identifier, float gain = 1.0f, float pitch = 1.0f, SfxFlags flags = SfxFlags::None);
		/** @brief Sets an animation of the object */
		bool SetAnimation(AnimState state, bool skipAnimation = false);
		/** @brief Pauses the current animation on the specified frame, for objects that count their frames themselves */
		void SetAnimationFrame(std::int32_t frame);
		/** @brief Sets a transition animation of the object */
		bool SetTransition(AnimState state, bool cancellable, Function<void()>&& callback = {});
		/** @brief Cancels a cancellable transition */
		void CancelTransition();
		/** @brief Cancels any transition */
		void ForceCancelTransition();
		/** @brief Called when an animation started */
		virtual void OnAnimationStarted();
		/** @brief Called when an animation finished */
		virtual void OnAnimationFinished();

		/** @brief Called when the object receives a network packet */
		virtual void OnPacketReceived(MemoryStream& packet);
		/** @brief Sends a packet to the other side of a non-local session */
		void SendPacket(ArrayView<const std::uint8_t> data);

		/** @brief Preloads specified metadata and its linked assets to cache */
		static void PreloadMetadataAsync(StringView path);
		/**
		 * @brief Loads specified metadata and its linked assets
		 *
		 * @param path          Relative path to the metadata asset
		 * @param forceIndexed  Load linked graphics as indexed for recoloring (e.g., the player)
		 */
		void RequestMetadata(StringView path, bool forceIndexed = false);

		/** @brief Loads specified metadata and its linked assets asynchronously if supported */
#if defined(WITH_COROUTINES)
		auto RequestMetadataAsync(StringView path, bool forceIndexed = false)
		{
			struct awaitable {
				ActorBase* actor;
				const StringView& path;
				bool forceIndexed;

				bool await_ready() {
					return false;
				}
				void await_suspend(std::coroutine_handle<> handle) {
					// TODO: implement async
					auto metadata = ContentResolver::Get().RequestMetadata(path, forceIndexed);
					actor->_metadata = metadata;
					handle();
				}
				void await_resume() { }
			};
			return awaitable{this, path, forceIndexed};
		}
#else
		void RequestMetadataAsync(StringView path, bool forceIndexed = false);
#endif

		/** @brief Sets actor state */
		constexpr void SetState(ActorState flags) noexcept {
			_state = flags;
		}

		/** @overload */
		constexpr void SetState(ActorState flag, bool value) noexcept {
			if (value) {
				_state = _state | flag;
			} else {
				_state = _state & (~flag);
			}
		}

	private:
		ActorBase(const ActorBase&) = delete;
		ActorBase& operator=(const ActorBase&) = delete;

		// What the sub-steps of a single frame ran into, so the callbacks fire at most once per frame
		struct MovementResult {
			bool HitWall;
			bool HitCeiling;
			bool HitFloor;
		};

		std::int32_t _collisionProxyID;
		ActorState _state;
		Function<void()> _currentTransitionCallback;

		// Returns `true` if the actor would still have ground under it after a given relative move
		bool IsSupportedAfterMove(float stepX, float stepY);
		// Returns `true` if the floor past the sides of the feet holds the actor up, see `_footingExtraLeft`
		bool HasExtendedFooting(const AABBf& feet);
		// Resolves one bounded sub-step of the standard movement, returns `false` if the actor couldn't move at all
		bool TryMoveSubstep(float stepX, float stepY, float currentGravity, float currentElasticity, Tiles::TileCollisionParams& params, MovementResult& result);

		bool IsCollidingWithAngled(ActorBase* other);
		bool IsCollidingWithAngled(const AABBf& aabb);

		void RefreshAnimation(bool skipAnimation = false);
		// Puts back an actor whose position became unusable
		DEATH_NEVER_INLINE void RestoreFromInvalidPosition();
	};
}