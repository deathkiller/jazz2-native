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
#include <Containers/StringView.h>

// If coroutines are not supported, load resources synchronously
#if defined(WITH_COROUTINES)
#	define async_return co_return
#	define async_await co_await
#else
#	define async_return return
#	define async_await
#endif

using namespace Death::Containers::Literals;

namespace Jazz2
{
	class ILevelHandler;
	class LevelHandler;
	class LightingRenderer;
}

#if defined(WITH_MULTIPLAYER)
namespace Jazz2::Multiplayer
{
	class MultiLevelHandler;
}
#endif

namespace Jazz2::Actors
{
	class Player;

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
		/** @brief Actor triggers TNT explosion */
		TriggersTNT = 0x2000,
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

	DEFINE_ENUM_OPERATORS(ActorState);

	struct ActorActivationDetails {
		ILevelHandler* LevelHandler;
		Vector3i Pos;
		ActorState State;
		EventType Type;
		const std::uint8_t* Params;

		ActorActivationDetails(ILevelHandler* levelHandler, const Vector3i& pos, const std::uint8_t* params = nullptr)
			: LevelHandler(levelHandler), Pos(pos), State(ActorState::None), Type(EventType::Empty), Params(params)
		{
		}
	};

	enum class MoveType {
		Absolute = 0x00,
		Relative = 0x01,
		Force = 0x02
	};

	DEFINE_ENUM_OPERATORS(MoveType);

	enum class ActorRendererType {
		Default,
		Outline,
		WhiteMask,
		PartialWhiteMask,
		FrozenMask
	};

	class ActorBase : public std::enable_shared_from_this<ActorBase>
	{
		DEATH_RUNTIME_OBJECT();

		friend class Jazz2::LevelHandler;
		friend class Jazz2::LightingRenderer;
#if defined(WITH_MULTIPLAYER)
		friend class Jazz2::Multiplayer::MultiLevelHandler;
#endif

	public:
		ActorBase();
		virtual ~ActorBase();

		AABBf AABB;
		AABBf AABBInner;
		std::int32_t CollisionProxyID;

		bool IsFacingLeft();

		void SetParent(SceneNode* parent);
		Task<bool> OnActivated(const ActorActivationDetails& details);
		virtual bool OnHandleCollision(std::shared_ptr<ActorBase> other);

		bool IsInvulnerable();
		std::int32_t GetHealth();
		void SetHealth(std::int32_t value);
		std::int32_t GetMaxHealth();
		void DecreaseHealth(std::int32_t amount = 1, ActorBase* collider = nullptr);

		bool MoveInstantly(const Vector2f& pos, MoveType type, Tiles::TileCollisionParams& params);
		bool MoveInstantly(const Vector2f& pos, MoveType type)
		{
			Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, _speed.Y >= 0.0f };
			return MoveInstantly(pos, type, params);
		}

		void AddExternalForce(float x, float y);

		bool IsCollidingWith(ActorBase* other);
		bool IsCollidingWith(const AABBf& aabb);
		void UpdateAABB();

		const Vector2f& GetPos() {
			return _pos;
		}

		const Vector2f& GetSpeed() {
			return _speed;
		}

		constexpr ActorState GetState() const noexcept
		{
			return _state;
		}

		constexpr bool GetState(ActorState flag) const noexcept
		{
			return (_state & flag) == flag;
		}

	protected:
		class ActorRenderer : public BaseSprite
		{
			friend class ActorBase;

		public:
			ActorRenderer(ActorBase* owner);

			bool AnimPaused;
			Vector2i FrameConfiguration;
			Vector2i FrameDimensions;
			AnimationLoopMode LoopMode;
			std::int32_t FirstFrame;
			std::int32_t FrameCount;
			float AnimDuration;
			float AnimTime;
			std::int32_t CurrentFrame;
			Vector2f Hotspot;

			void Initialize(ActorRendererType type);

			void OnUpdate(float timeMult) override;
			bool OnDraw(RenderQueue& renderQueue) override;

			bool IsAnimationRunning();
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

		static constexpr std::uint8_t AlphaThreshold = 40;
		static constexpr float CollisionCheckStep = 0.5f;
		static constexpr std::int32_t PerPixelCollisionStep = 3;
		static constexpr std::int32_t AnimationCandidatesCount = 5;

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

		void SetFacingLeft(bool value);

		virtual Task<bool> OnActivatedAsync(const ActorActivationDetails& details);
		virtual bool OnTileDeactivated();

		virtual void OnHealthChanged(ActorBase* collider);
		virtual bool OnPerish(ActorBase* collider);

		virtual void OnUpdate(float timeMult);
		virtual void OnUpdateHitbox();
		virtual bool OnDraw(RenderQueue& renderQueue);
		virtual void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) { }
		virtual void OnHitFloor(float timeMult);
		virtual void OnHitCeiling(float timeMult);
		virtual void OnHitWall(float timeMult);

		virtual void OnTriggeredEvent(EventType eventType, uint8_t* eventParams);

		void TryStandardMovement(float timeMult, Tiles::TileCollisionParams& params);
		void UpdateHitbox(std::int32_t w, std::int32_t h);
		void UpdateFrozenState(float timeMult);
		void HandleFrozenStateChange(ActorBase* shot);

		void CreateParticleDebris();
		void CreateSpriteDebris(AnimState state, std::int32_t count);
		virtual float GetIceShrapnelScale() const;

		std::shared_ptr<AudioBufferPlayer> PlaySfx(const StringView identifier, float gain = 1.0f, float pitch = 1.0f);
		bool SetAnimation(AnimState state, bool skipAnimation = false);
		bool SetTransition(AnimState state, bool cancellable, const std::function<void()>& callback = nullptr);
		bool SetTransition(AnimState state, bool cancellable, std::function<void()>&& callback);
		void CancelTransition();
		void ForceCancelTransition();
		virtual void OnAnimationStarted();
		virtual void OnAnimationFinished();

		static void PreloadMetadataAsync(const StringView path);
		void RequestMetadata(const StringView path);

#if defined(WITH_COROUTINES)
		auto RequestMetadataAsync(const StringView path)
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
		void RequestMetadataAsync(const StringView path);
#endif

		constexpr void SetState(ActorState flags) noexcept
		{
			_state = flags;
		}

		constexpr void SetState(ActorState flag, bool value) noexcept
		{
			if (value) {
				_state = _state | flag;
			} else {
				_state = _state & (~flag);
			}
		}

	private:
		ActorBase(const ActorBase&) = delete;
		ActorBase& operator=(const ActorBase&) = delete;

		ActorState _state;
		std::function<void()> _currentTransitionCallback;

		bool IsCollidingWithAngled(ActorBase* other);
		bool IsCollidingWithAngled(const AABBf& aabb);

		void RefreshAnimation(bool skipAnimation = false);
	};
}