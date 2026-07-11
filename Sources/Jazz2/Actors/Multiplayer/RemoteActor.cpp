#include "RemoteActor.h"

#if defined(WITH_MULTIPLAYER)

#include "../../ContentResolver.h"
#include "../../ILevelHandler.h"
#include "../Player.h"
#include "../../../nCine/Graphics/RenderQueue.h"

namespace Jazz2::Actors::Multiplayer
{
	namespace
	{
		// Recoloring uses a per-character palette scheme; a remote player's character is identified by the metadata
		// path the server sent (the only signal a RemoteActor has). Only the character matters here, not the morph.
		PlayerType GetPlayerTypeFromMetadataPath(StringView path)
		{
			// _metadata->Path uses native separators (backslashes on Windows), so pull out the file name with the
			// separator-aware helper instead of matching a hard-coded "Interactive/PlayerX" literal - that never
			// matched on Windows and made every remote player fall back to Jazz's recolor scheme (wrong/default fur).
			StringView fileName = fs::GetFileName(path);
			if (fileName == "PlayerLori"_s) {
				return PlayerType::Lori;
			} else if (fileName == "PlayerSpaz"_s) {
				return PlayerType::Spaz;
			} else if (fileName == "PlayerFrog"_s) {
				return PlayerType::Frog;
			} else {
				return PlayerType::Jazz;
			}
		}
	}

	RemoteActor::RemoteActor()
		: _lastAnim(AnimState::Idle), _isAttachedLocally(false), _furColor(0), _paletteOffset(-1),
			_activeShield(ShieldType::None), _activeShieldTime(0.0f)
	{
	}

	RemoteActor::~RemoteActor()
	{
		// Release the shared palette offset back to the pool when the remote player disconnects/despawns
		if (_paletteOffset >= 0) {
			ContentResolver::Get().ReleasePaletteOffset(_paletteOffset);
			_paletteOffset = -1;
		}
	}

	void RemoteActor::RefreshColorPalette()
	{
		auto& resolver = ContentResolver::Get();
		if (resolver.IsHeadless()) {
			return;
		}

		// Acquire the new (shared, reference-counted) palette before releasing the old one, so an unchanged fur color
		// keeps its slot; players with the same color and character share a single palette
		PlayerType playerType = (_metadata != nullptr ? GetPlayerTypeFromMetadataPath(_metadata->Path) : PlayerType::Jazz);
		std::int32_t newOffset = (_furColor != 0 ? resolver.AcquirePaletteOffset(_furColor, playerType) : -1);
		if (_paletteOffset >= 0) {
			resolver.ReleasePaletteOffset(_paletteOffset);
		}
		_paletteOffset = newOffset;
		_renderer.SetPalette(_paletteOffset);
	}

	Task<bool> RemoteActor::OnActivatedAsync(const ActorActivationDetails& details)
	{
		SetState(ActorState::PreserveOnRollback, true);
		SetState(ActorState::CanBeFrozen | ActorState::CollideWithTileset | ActorState::ApplyGravitation, false);

		_stateBuffer.Reset(Vector2f(details.Pos.X, details.Pos.Y), StateInterpolationBuffer::Now());

		async_return true;
	}

	void RemoteActor::OnUpdate(float timeMult)
	{
		if (!_isAttachedLocally) {
			std::int64_t renderTime = StateInterpolationBuffer::Now() - StateInterpolationBuffer::ServerDelay;

			Vector2f pos;
			if (_stateBuffer.Sample(renderTime, pos)) {
				MoveInstantly(pos, MoveType::Absolute | MoveType::Force);
			}
		}

		// Shield time decays locally (the server sends only state changes, not per-frame expiry), so the decoration
		// fades out on remote players just like on the owning player
		if (_activeShieldTime > 0.0f) {
			_activeShieldTime -= timeMult;
			if (_activeShieldTime <= 0.0f) {
				_activeShield = ShieldType::None;
				_activeShieldTime = 0.0f;
			}
		}

		ActorBase::OnUpdate(timeMult);
	}

	bool RemoteActor::OnDraw(RenderQueue& renderQueue)
	{
		// Remote players carry the same shield decoration as the local player; reuse the shared renderer so it looks
		// identical. Skipped for non-player remote actors, which never receive a shield (and lack the shield anims).
		if (_activeShield != ShieldType::None && _metadata != nullptr) {
			Player::DrawShield(renderQueue, _activeShield, _activeShieldTime, _metadata,
				_levelHandler->GetElapsedFrames(), _pos, _renderer.layer(), _shieldRenderCommands);
		}

		return ActorBase::OnDraw(renderQueue);
	}

	void RemoteActor::OnAttach(ActorBase* parent)
	{
		_isAttachedLocally = true;
	}

	void RemoteActor::OnDetach(ActorBase* parent)
	{
		_isAttachedLocally = false;
	}

	void RemoteActor::AssignMetadata(std::uint8_t flags, ActorState state, StringView path, AnimState anim, float rotation, float scaleX, float scaleY, ActorRendererType rendererType)
	{
		constexpr ActorState RemotedFlags = ActorState::Illuminated | ActorState::IsInvulnerable |
			ActorState::CollideWithOtherActors | ActorState::CollideWithSolidObjects | ActorState::IsSolidObject |
			ActorState::CollideWithTilesetReduced | ActorState::CollideWithSolidObjectsBelow | ActorState::ExcludeSimilar;

		// Load indexed sprites when this remote player has a custom color, so they can be recolored at draw time
		RequestMetadata(path, _furColor != 0);
		SetAnimation(anim);
		// Track the assigned animation so the first SyncAnimationWithServer() for the same state isn't skipped as
		// "unchanged", which would otherwise leave the actor stuck in its spawn animation
		_lastAnim = anim;
		SetState((GetState() & ~RemotedFlags) | (state & RemotedFlags));

		_renderer.Initialize(rendererType);
		_renderer.setRotation(rotation);

		RefreshColorPalette();

		SyncMiscWithServer(flags);
	}

	void RemoteActor::SetPlayerColor(std::uint32_t furColor)
	{
		if (_furColor == furColor) {
			return;
		}
		_furColor = furColor;

		// Not assigned yet - AssignMetadata will pick up _furColor when it runs
		if (_metadata == nullptr) {
			return;
		}

		// Reload the current metadata with matching indexed-ness and re-resolve the animation (the metadata pointer
		// changes), then (re)apply or clear the palette
		String path = _metadata->Path;
		AnimState currentState = (_currentAnimation != nullptr ? _currentAnimation->State : _lastAnim);
		RequestMetadata(path, furColor != 0);
		_currentAnimation = nullptr;
		SetAnimation(currentState);

		RefreshColorPalette();
	}

	void RemoteActor::ChangeMetadata(StringView path)
	{
		// Keep the recolor: load indexed when colored, and (re)apply or clear the palette
		RequestMetadata(path, _furColor != 0);
		RefreshColorPalette();
	}

	void RemoteActor::SyncPositionWithServer(Vector2f pos)
	{
		_stateBuffer.Push(pos, StateInterpolationBuffer::Now(), _renderer.isDrawEnabled());
	}

	void RemoteActor::SyncAnimationWithServer(AnimState anim, float rotation, float scaleX, float scaleY, Actors::ActorRendererType rendererType)
	{
		if (_lastAnim != anim) {
			_lastAnim = anim;
			SetAnimation(anim);
		}

		_renderer.setRotation(rotation);
		_renderer.setScale(scaleX, scaleY);
		_renderer.Initialize(rendererType);
	}

	void RemoteActor::SyncMiscWithServer(std::uint8_t flags)
	{
		_renderer.setDrawEnabled((flags & 0x04) != 0);
		_renderer.AnimPaused = (flags & 0x08) != 0;
		SetFacingLeft((flags & 0x10) != 0);
		_renderer.setFlippedY((flags & 0x20) != 0);

		bool justWarped = (flags & 0x40) != 0;
		if (justWarped) {
			// Collapse the buffer to the most recent position, so the actor teleports instead of interpolating
			_stateBuffer.Reset(_stateBuffer.GetLatest(), StateInterpolationBuffer::Now());
		}
	}

	void RemoteActor::SetShield(ShieldType shieldType, float timeLeft)
	{
		_activeShield = shieldType;
		_activeShieldTime = (shieldType != ShieldType::None ? timeLeft : 0.0f);
	}
}

#endif