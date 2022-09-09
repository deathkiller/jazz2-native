#pragma once

#include "../ContentResolver.h"
#include "../EventType.h"
#include "../LightEmitter.h"
#include "../../nCine/Base/Task.h"
#include "../../nCine/Primitives/AABB.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"

namespace Jazz2
{
	class ILevelHandler;

	namespace Actors
	{
		class Player;
	}

	enum class ActorState {
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

		// This flag is set automatically after call to OnActivatedAsync()
		Initialized = 0x0100,

		// Actor instance flags
		IsInvulnerable = 0x0200,
		CanJump = 0x0400,
		CanBeFrozen = 0x0800,
		IsFacingLeft = 0x1000,

		// Collision flags
		CollideWithTileset = 0x10000,
		CollideWithOtherActors = 0x20000,
		CollideWithSolidObjects = 0x40000,

		ForceDisableCollisions = 0x80000,

		IsDirty = 0x100000,
		IsDestroyed = 0x200000,

		ApplyGravitation = 0x400000,
		IsSolidObject = 0x800000,
		SkipPerPixelCollisions = 0x1000000,

		CollideWithTilesetReduced = 0x2000000,
		CollideWithSolidObjectsBelow = 0x4000000,
	};

	DEFINE_ENUM_OPERATORS(ActorState);

	struct ActorActivationDetails {
		ILevelHandler* LevelHandler;
		Vector3i Pos;
		ActorState State;
		uint8_t* Params;
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
		WhiteMask
	};

	class ActorBase : public std::enable_shared_from_this<ActorBase>
	{
		friend class LevelHandler;

	public:
		ActorBase();
		~ActorBase();

		AABBf AABB;
		AABBf AABBInner;
		int32_t CollisionProxyID;

		void SetParent(SceneNode* parent);
		Task<bool> OnActivated(const ActorActivationDetails& details);
		virtual bool OnHandleCollision(std::shared_ptr<ActorBase> other);
		virtual void OnEmitLights(SmallVectorImpl<LightEmitter>& lights) { }

		bool IsInvulnerable();
		int GetHealth();
		void SetHealth(int value);
		int GetMaxHealth();
		void DecreaseHealth(int amount = 1, ActorBase* collider = nullptr);

		bool MoveInstantly(const Vector2f& pos, MoveType type, TileCollisionParams& params);
		bool MoveInstantly(const Vector2f& pos, MoveType type)
		{
			TileCollisionParams params = { TileDestructType::None, _speed.Y >= 0.0f };
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
		struct AnimationCandidate {
			const String* Identifier;
			GraphicResource* Resource;
		};

		class ActorRenderer : public BaseSprite
		{
			friend class ActorBase;

		public:
			ActorRenderer(ActorBase* owner)
				:
				BaseSprite(nullptr, nullptr, 0.0f, 0.0f), _owner(owner), _rendererType((ActorRendererType)-1), AnimPaused(false),
				FrameConfiguration(), FrameDimensions(), LoopMode(AnimationLoopMode::Loop),
				FirstFrame(0), FrameCount(0), AnimDuration(0.0f), AnimTime(0.0f),
				CurrentFrame(0), NextFrame(0), CurrentFrameFade(0.0f), Hotspot()
			{
				type_ = ObjectType::SPRITE;
				renderCommand_.setType(RenderCommand::CommandTypes::SPRITE);
				Initialize(ActorRendererType::Default);
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

			void Initialize(ActorRendererType type);

			void OnUpdate(float timeMult) override;
			bool OnDraw(RenderQueue& renderQueue) override;

			bool IsAnimationRunning();

		protected:
			void textureHasChanged(Texture* newTexture) override;

		private:
			ActorBase* _owner;
			ActorRendererType _rendererType;

			void UpdateVisibleFrames();
			static int NormalizeFrame(int frame, int min, int max);
		};

		static constexpr uint8_t AlphaThreshold = 40;
		static constexpr float CollisionCheckStep = 0.5f;
		static constexpr int PerPixelCollisionStep = 3;
		static constexpr int AnimationCandidatesCount = 5;

		ILevelHandler* _levelHandler;

		Vector2f _pos;
		Vector2f _speed;
		Vector2f _externalForce;
		float _internalForceY;
		float _elasticity;
		float _friction;
		float _unstuckCooldown;

		float _frozenTimeLeft;
		int _maxHealth;
		int _health;

		Vector2i _originTile;
		Metadata* _metadata;
		ActorRenderer _renderer;
		GraphicResource* _currentAnimation;
		GraphicResource* _currentTransition;
		AnimState _currentAnimationState;
		AnimState _currentTransitionState;
		bool _currentTransitionCancellable;

		bool IsFacingLeft();
		void SetFacingLeft(bool value);

		virtual Task<bool> OnActivatedAsync(const ActorActivationDetails& details);
		virtual bool OnTileDeactivate(int tx1, int ty1, int tx2, int ty2);

		virtual void OnHealthChanged(ActorBase* collider);
		virtual bool OnPerish(ActorBase* collider);

		virtual void OnUpdate(float timeMult);
		virtual void OnUpdateHitbox();
		virtual bool OnDraw(RenderQueue& renderQueue);
		virtual void OnHitFloor(float timeMult);
		virtual void OnHitCeiling(float timeMult);
		virtual void OnHitWall(float timeMult);

		virtual void OnTriggeredEvent(EventType eventType, uint16_t* eventParams);

		void TryStandardMovement(float timeMult, TileCollisionParams& params);

		void UpdateHitbox(int w, int h);

		void CreateParticleDebris();
		void CreateSpriteDebris(const StringView& identifier, int count);

		std::shared_ptr<AudioBufferPlayer> PlaySfx(const StringView& identifier, float gain = 1.0f, float pitch = 1.0f);
		void SetAnimation(const StringView& identifier);
		bool SetAnimation(AnimState state);
		bool SetTransition(AnimState state, bool cancellable, const std::function<void()>& callback = []() { });
		void CancelTransition();
		void ForceCancelTransition();
		virtual void OnAnimationStarted();
		virtual void OnAnimationFinished();

		int FindAnimationCandidates(AnimState state, AnimationCandidate candidates[AnimationCandidatesCount]);

		static void PreloadMetadataAsync(const StringView& path)
		{
			ContentResolver::Current().PreloadMetadataAsync(path);
		}

		void RequestMetadata(const StringView& path)
		{
			_metadata = ContentResolver::Current().RequestMetadata(path);
		}

		auto RequestMetadataAsync(const StringView& path)
		{
			struct awaitable {
				ActorBase* actor;
				const StringView& path;

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
		/// Deleted copy constructor
		ActorBase(const ActorBase&) = delete;
		/// Deleted assignment operator
		ActorBase& operator=(const ActorBase&) = delete;

		ActorState _state;
		std::function<void()> _currentTransitionCallback;

		bool IsCollidingWithAngled(ActorBase* other);
		bool IsCollidingWithAngled(const AABBf& aabb);

		void RefreshAnimation();

		void HandleAmmoFrozenStateChange(ActorBase* shot);

	};
}