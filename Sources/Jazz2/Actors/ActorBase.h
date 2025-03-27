#pragma once

#include "../EventType.h"
#include "../LightEmitter.h"
#include "../Resources.h"
#include "../Tiles/TileCollisionParams.h"

#include "../../nCine/Base/Task.h"
#include "../../nCine/Primitives/AABB.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"
#include "../../nCine/Graphics/BaseSprite.h"
#include "../../nCine/Graphics/SceneNode.h"

#include <Base/TypeInfo.h>
#include <Containers/Function.h>
#include <Containers/StringView.h>

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

namespace Jazz2::Actors
{
	class Player;

	/** @brief Flags that modify behaviour of @ref ActorBase, supports a bitwise combination of its member values */
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

		//Reserved = 0x2000,

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
		/** @brief Ignore solid collisions agains similar objects that have this flag */
		ExcludeSimilar = 0x8000000,
	};

	DEATH_ENUM_FLAGS(ActorState);

	/** @brief Description how to initialize an actor */
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

	/** @brief Move type, supports a bitwise combination of its member values */
	enum class MoveType {
		Absolute = 0x00,		/**< Move to absolute position */
		Relative = 0x01,		/**< Move to relative position */
		Force = 0x02			/**< Ignore all collision checks */
	};

	DEATH_ENUM_FLAGS(MoveType);

	/** @brief Actor renderer type */
	enum class ActorRendererType {
		Default,				/**< Default rendering */
		Outline,				/**< Draw outline around the sprite */
		WhiteMask,				/**< Draw all non-transparent pixels as white */
		PartialWhiteMask,		/**< Draw all non-transparent pixels as boosted shades of gray */
		FrozenMask				/**< Apply frozen effect to the sprite */
	};

	/** @brief Base class of an object */
	class ActorBase : public std::enable_shared_from_this<ActorBase>
	{
		DEATH_RUNTIME_OBJECT();

		friend class Jazz2::LevelHandler;
		friend class Jazz2::Rendering::LightingRenderer;
#if defined(WITH_MULTIPLAYER)
		friend class Jazz2::Multiplayer::MpLevelHandler;
		friend class Jazz2::UI::Multiplayer::MpInGameCanvasLayer;
#endif

	public:
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
		virtual bool OnHandleCollision(std::shared_ptr<ActorBase> other);
		/** @brief Called to check whether @p collider can cause damage to the object */
		virtual bool CanCauseDamage(ActorBase* collider);

		/** @brief Returns `true` if the object is invulnerable */
		bool IsInvulnerable();
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
		bool MoveInstantly(Vector2f pos, MoveType type)
		{
			Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, _speed.Y >= 0.0f };
			return MoveInstantly(pos, type, params);
		}

		/** @brief Adds external force */
		void AddExternalForce(float x, float y);

		/** @brief Returns `true` if this object is colliding with a given object */
		bool IsCollidingWith(ActorBase* other);
		/** @brief Returns `true` if this object is colliding with a given AABB */
		bool IsCollidingWith(const AABBf& aabb);
		/** @brief Updates AABB for current position, rotation and animation frame */
		void UpdateAABB();

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

	protected:
		/** @brief Actor renderer */
		class ActorRenderer : public BaseSprite
		{
			friend class ActorBase;

		public:
			ActorRenderer(ActorBase* owner);

			/** @brief Whether the animation is paused */
			bool AnimPaused;
			/** @brief Frame configuration */
			Vector2i FrameConfiguration;
			/** @brief Frame dimensions */
			Vector2i FrameDimensions;
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
			/** @brief Hotspot */
			Vector2f Hotspot;

			/** @brief Initializes the renderer to the specified renderer type */
			void Initialize(ActorRendererType type);

			void OnUpdate(float timeMult) override;
			bool OnDraw(RenderQueue& renderQueue) override;

			/** @brief Returns `true` if animation is running */
			bool IsAnimationRunning();
			/** @brief Returns active renderer type */
			ActorRendererType GetRendererType() const;

		protected:
			void textureHasChanged(Texture* newTexture) override;

		private:
			ActorBase* _owner;
			ActorRendererType _rendererType;
			float _rendererTransition;

			void UpdateVisibleFrames();
			static std::int32_t NormalizeFrame(std::int32_t frame, std::int32_t min, std::int32_t max);
		};

		/** @{ @name Constants */

		/** @brief Alpha transparency threshold */
		static constexpr std::uint8_t AlphaThreshold = 40;
		/** @brief Step for collision checking */
		static constexpr float CollisionCheckStep = 0.5f;
		/** @brief Step for per-pixel collisions */
		static constexpr std::int32_t PerPixelCollisionStep = 3;
		/** @brief Maximum number of animation candidates */
		static constexpr std::int32_t AnimationCandidatesCount = 5;

		/** @} */

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Hide these members from documentation before refactoring
		ILevelHandler* _levelHandler;

		Vector2f _pos;
		Vector2f _speed;
		Vector2f _externalForce;
		float _internalForceY;
		float _elasticity;
		float _friction;
		float _unstuckCooldown;
		float _frozenTimeLeft;
		std::int32_t _maxHealth;
		std::int32_t _health;

		Vector2i _originTile;
		float _spawnFrames;
		Metadata* _metadata;
		ActorRenderer _renderer;
		GraphicResource* _currentAnimation;
		GraphicResource* _currentTransition;
		bool _currentTransitionCancellable;
#endif

		/** @brief Sets internal parent node */
		void SetParent(SceneNode* parent);

		/** @brief Sets whether the object is facing left */
		void SetFacingLeft(bool value);

		/** @brief Called when the object is created and activated */
		virtual Task<bool> OnActivatedAsync(const ActorActivationDetails& details);
		/** @brief Called when corresponding tile should be deactivated */
		virtual bool OnTileDeactivated();

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
		/** @brief Called when the object hits a floor */
		virtual void OnHitFloor(float timeMult);
		/** @brief Called when the object hits a ceiling */
		virtual void OnHitCeiling(float timeMult);
		/** @brief Called when the object hits a wall */
		virtual void OnHitWall(float timeMult);

		/** @brief Called when an event is triggered */
		virtual void OnTriggeredEvent(EventType eventType, std::uint8_t* eventParams);

		/** @brief Performs standard movement behavior */
		void TryStandardMovement(float timeMult, Tiles::TileCollisionParams& params);
		/** @brief Updates hitbox to a given size */
		void UpdateHitbox(std::int32_t w, std::int32_t h);
		/** @brief Updates frozen state of the object */
		void UpdateFrozenState(float timeMult);
		/** @brief Handles change of frozen state after collision with other object */
		void HandleFrozenStateChange(ActorBase* shot);

		/** @brief Creates a particle debris from a sprite */
		void CreateParticleDebris();
		/** @brief Creates a sprite debris */
		void CreateSpriteDebris(AnimState state, std::int32_t count);
		/** @brief Returns scale of ice shrapnels */
		virtual float GetIceShrapnelScale() const;

		/** @brief Plays a sound effect for the object */
		std::shared_ptr<AudioBufferPlayer> PlaySfx(StringView identifier, float gain = 1.0f, float pitch = 1.0f);
		/** @brief Sets an animation of the object */
		bool SetAnimation(AnimState state, bool skipAnimation = false);
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

		/** @brief Preloads specified metadata and its linked assets to cache */
		static void PreloadMetadataAsync(StringView path);
		/** @brief Loads specified metadata and its linked assets */
		void RequestMetadata(StringView path);

		/** @brief Loads specified metadata and its linked assets asynchronously if supported */
#if defined(WITH_COROUTINES)
		auto RequestMetadataAsync(StringView path)
		{
			struct awaitable {
				ActorBase* actor;
				const StringView& path;

				bool await_ready() {
					return false;
				}
				void await_suspend(std::coroutine_handle<> handle) {
					// TODO: implement async
					auto metadata = ContentResolver::Get().RequestMetadata(path);
					actor->_metadata = metadata;
					handle();
				}
				void await_resume() { }
			};
			return awaitable{this, path};
		}
#else
		void RequestMetadataAsync(StringView path);
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

		std::int32_t _collisionProxyID;
		ActorState _state;
		Function<void()> _currentTransitionCallback;

		bool IsCollidingWithAngled(ActorBase* other);
		bool IsCollidingWithAngled(const AABBf& aabb);

		void RefreshAnimation(bool skipAnimation = false);
	};
}