#pragma once

#include "ContentResolver.h"
#include "EventType.h"
#include "LightEmitter.h"
#include "../nCine/Base/Task.h"
#include "../nCine/Primitives/AABB.h"

namespace Jazz2
{
	class ILevelHandler;

	namespace Actors
	{
		class Player;
	}

	enum class ActorFlags {
		None = 0,

		// Actor is created from event map 
		IsCreatedFromEventMap = 0x01,
		// Actor is created by generator
		IsFromGenerator = 0x02,

		// Actor should be illuminated
		Illuminated = 0x04,

		// Actor should be created asynchronously
		Async = 0x08,

		// Mask of all instantiation flags
		InstantiationFlags = IsCreatedFromEventMap | IsFromGenerator | Illuminated | Async,

		// Actor instance flags
		Initializing = 0x0100,
		Initialized = 0x0200,

		IsInvulnerable = 0x1000,
		CanJump = 0x2000,
		CanBeFrozen = 0x4000,
		IsFacingLeft = 0x8000
	};

	DEFINE_ENUM_OPERATORS(ActorFlags);

	struct ActorActivationDetails {
		ILevelHandler* LevelHandler;
		Vector3i Pos;
		ActorFlags Flags;
		uint8_t* Params;
	};

	enum class CollisionFlags : uint16_t {
		None = 0,

		CollideWithTileset = 0x01,
		CollideWithOtherActors = 0x02,
		CollideWithSolidObjects = 0x04,

		ForceDisableCollisions = 0x08,

		IsDirty = 0x10,
		IsDestroyed = 0x20,

		ApplyGravitation = 0x40,
		IsSolidObject = 0x80,
		SkipPerPixelCollisions = 0x100
	};

	DEFINE_ENUM_OPERATORS(CollisionFlags);

	enum class MoveType {
		Absolute,
		Relative
	};

	class ActorBase : public std::enable_shared_from_this<ActorBase>
	{
		friend class LevelHandler;

	public:
		ActorBase();
		~ActorBase();

		CollisionFlags CollisionFlags = CollisionFlags::CollideWithTileset | CollisionFlags::CollideWithOtherActors | CollisionFlags::ApplyGravitation;

		AABBf AABB;
		AABBf AABBInner;
		int32_t CollisionProxyID;

		void SetParent(SceneNode* parent);
		Task<bool> OnActivated(const ActorActivationDetails& details);
		virtual bool OnHandleCollision(ActorBase* other);
		virtual void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) { }

		bool IsInvulnerable();
		int GetHealth();
		void SetHealth(int value);
		int GetMaxHealth();
		void DecreaseHealth(int amount = 1, ActorBase* collider = nullptr);

		bool MoveInstantly(const Vector2f& pos, MoveType type, bool force = false);
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

		constexpr bool GetState(ActorFlags flag) const noexcept
		{
			return (_flags & flag) == flag;
		}

	protected:
		struct AnimationCandidate {
			const std::string* Identifier;
			GraphicResource* Resource;
		};

		class SpriteRenderer : public Sprite
		{
			friend class ActorBase;

		public:
			SpriteRenderer(ActorBase* owner)
				:
				_owner(owner), AnimPaused(false),
				FrameConfiguration(), FrameDimensions(), LoopMode(AnimationLoopMode::Loop),
				FirstFrame(0), FrameCount(0), AnimDuration(0.0f), AnimTime(0.0f),
				CurrentFrame(0), NextFrame(0), CurrentFrameFade(0.0f), Hotspot()
			{
			}

			bool AnimPaused;

			Vector2i FrameConfiguration;
			Vector2i FrameDimensions;
			AnimationLoopMode LoopMode;
			int FirstFrame;
			int FrameCount;
			float AnimDuration;
			float AnimTime;
			int CurrentFrame, NextFrame;
			float CurrentFrameFade;
			Vector2i Hotspot;

			void OnUpdate(float timeMult) override;

			bool IsAnimationRunning()
			{
				if (FrameCount <= 0) {
					return false;
				}

				switch (LoopMode) {
					case AnimationLoopMode::FixedSingle:
						return false;
					case AnimationLoopMode::Loop:
						return !AnimPaused;
					case AnimationLoopMode::Once:
						return !AnimPaused && AnimTime < AnimDuration;
					default:
						return false;
				}
			}

		private:
			ActorBase* _owner;

			void UpdateVisibleFrames();
			static int NormalizeFrame(int frame, int min, int max);
		};

		static constexpr uint8_t AlphaThreshold = 40;
		static constexpr float CollisionCheckStep = 0.5f;
		static constexpr int PerPixelCollisionStep = 3;
		static constexpr int AnimationCandidatesCount = 5;

		ActorFlags _flags;
		ILevelHandler* _levelHandler;

		Vector2f _pos;
		Vector2f _speed;
		Vector2f _externalForce;
		float _internalForceY;
		float _elasticity;
		float _friction;

		float _frozenTimeLeft;
		int _maxHealth;
		int _health;

		SuspendType _suspendType;
		Vector2i _originTile;

		SpriteRenderer _renderer;
		GraphicResource* _currentAnimation;
		GraphicResource* _currentTransition;
		AnimState _currentAnimationState = AnimState::Uninitialized;
		AnimState _currentTransitionState;
		bool _currentTransitionCancellable;

		bool IsFacingLeft();
		void SetFacingLeft(bool value);

		virtual Task<bool> OnActivatedAsync(const ActorActivationDetails& details);
		virtual void OnDestroyed();
		virtual bool OnTileDeactivate(int tx1, int ty1, int tx2, int ty2);

		virtual void OnHealthChanged(ActorBase* collider);
		virtual bool OnPerish(ActorBase* collider);

		virtual void OnUpdate(float timeMult);
		virtual void OnUpdateHitbox();
		virtual void OnHitFloor();
		virtual void OnHitCeiling();
		virtual void OnHitWall();

		virtual void OnTriggeredEvent(EventType eventType, uint16_t* eventParams);

		void TryStandardMovement(float timeMult);

		void UpdateHitbox(int w, int h);

		void CreateParticleDebris();
		void CreateSpriteDebris(const std::string& identifier, int count);

		void PlaySfx(const std::string& identifier, float gain = 1.0f, float pitch = 1.0f);
		void SetAnimation(const std::string& identifier);
		bool SetAnimation(AnimState state);
		bool SetTransition(AnimState state, bool cancellable, const std::function<void()>& callback = []() { });
		void CancelTransition();
		void ForceCancelTransition();
		virtual void OnAnimationStarted();
		virtual void OnAnimationFinished();

		int FindAnimationCandidates(AnimState state, AnimationCandidate candidates[AnimationCandidatesCount]);

		static void PreloadMetadataAsync(const std::string& path)
		{
			ContentResolver::Current().PreloadMetadataAsync(path);
		}

		void RequestMetadata(const std::string& path)
		{
			_metadata = ContentResolver::Current().RequestMetadata(path);
		}

		auto RequestMetadataAsync(const std::string& path)
		{
			struct awaitable {
				ActorBase* actor;
				std::string path;

				bool await_ready() {
					return false;
				}
				void await_suspend(std::coroutine_handle<> handle) {
					// TODO: implement async
					auto metadata = ContentResolver::Current().RequestMetadata(path);
					actor->_metadata = metadata;
					handle();
				}
				void await_resume() { }
			};
			return awaitable { this, path };
		}

		constexpr void SetState(ActorFlags flag, bool value) noexcept
		{
			if (value) {
				_flags |= flag;
			} else {
				_flags &= ~flag;
			}
		}

	private:
		/// Deleted copy constructor
		ActorBase(const ActorBase&) = delete;
		/// Deleted assignment operator
		ActorBase& operator=(const ActorBase&) = delete;

		Metadata* _metadata;

#if SERVER
		const std::string* _currentAnimationKey;
#endif

		std::function<void()> _currentTransitionCallback;

		bool IsCollidingWithAngled(ActorBase* other);
		bool IsCollidingWithAngled(const AABBf& aabb);

		void RefreshAnimation();

	};
}