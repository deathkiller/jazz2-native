#include "PlayTestDebug.h"

#if defined(WITH_EDITOR)

#include "../../Actors/Player.h"
#include "../../Actors/SolidObjectBase.h"
#include "../../Actors/Collectibles/CollectibleBase.h"
#include "../../Actors/Enemies/EnemyBase.h"
#include "../../Actors/Enemies/Bosses/BossBase.h"
#include "../../Actors/Weapons/ShotBase.h"
#include "../../Rendering/PlayerViewport.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	namespace
	{
		// How fast the time of the level may run at most and at least, so a step of it is still something
		constexpr float MinTimeScale = 0.05f;
		constexpr float MaxTimeScale = 4.0f;

		// The colour of the zone of a tile event, or transparent for an event that isn't one
		Colorf GetEventZoneColor(EventType type)
		{
			const auto isBetween = [type](EventType first, EventType last) {
				return ((std::uint16_t)type >= (std::uint16_t)first && (std::uint16_t)type <= (std::uint16_t)last);
			};

			if (type == EventType::ModifierHurt || type == EventType::ModifierDeath) {
				return Colorf(1.0f, 0.15f, 0.1f, 0.35f);
			}
			if (isBetween(EventType::SceneryDestruct, EventType::SceneryCollapse)) {
				return Colorf(1.0f, 0.55f, 0.1f, 0.3f);
			}
			if (isBetween(EventType::ModifierVine, EventType::ModifierNoClimb)) {
				return Colorf(0.2f, 0.5f, 1.0f, 0.3f);
			}
			if (type == EventType::AreaEndOfLevel) {
				return Colorf(0.2f, 1.0f, 0.3f, 0.35f);
			}
			if (isBetween(EventType::AreaStopEnemy, EventType::AreaAmbientBubbles)) {
				return Colorf(0.75f, 0.3f, 1.0f, 0.3f);
			}
			if (type == EventType::TriggerArea || type == EventType::TriggerZone) {
				return Colorf(1.0f, 0.9f, 0.1f, 0.3f);
			}
			if (type == EventType::WarpOrigin || type == EventType::WarpCoinBonus) {
				return Colorf(0.1f, 0.95f, 0.95f, 0.3f);
			}
			return Colorf(0.0f, 0.0f, 0.0f, 0.0f);
		}

		// The colour of the hitbox of an object, by what kind of an object it is
		Colorf GetHitboxColor(Actors::ActorBase* actor)
		{
			if (runtime_cast<Actors::Player>(actor) != nullptr) {
				return Colorf(0.3f, 1.0f, 0.35f, 1.0f);
			}
			if (runtime_cast<Actors::Enemies::EnemyBase>(actor) != nullptr) {
				return Colorf(1.0f, 0.25f, 0.2f, 1.0f);
			}
			if (runtime_cast<Actors::Collectibles::CollectibleBase>(actor) != nullptr) {
				return Colorf(1.0f, 0.9f, 0.2f, 1.0f);
			}
			if (runtime_cast<Actors::SolidObjectBase>(actor) != nullptr) {
				return Colorf(0.3f, 0.65f, 1.0f, 1.0f);
			}
			if (runtime_cast<Actors::Weapons::ShotBase>(actor) != nullptr) {
				return Colorf(1.0f, 0.55f, 0.15f, 1.0f);
			}
			return Colorf(0.85f, 0.85f, 0.85f, 1.0f);
		}

		// The part of the level a view of a player shows
		Rectf GetWorldView(const Rendering::PlayerViewport& viewport)
		{
			const Vector2f size = viewport.GetWorldViewSize();
			const Vector2f center = viewport.GetCameraCenter();
			return Rectf(center.X - size.X * 0.5f, center.Y - size.Y * 0.5f, size.X, size.Y);
		}
	}

	PlayTestDebug::PlayTestDebug(LevelHandler& levelHandler)
		: _levelHandler(levelHandler), _timeControlled(false), _frameTimeMult(1.0f), _frameFrozen(false), _stepPending(false)
	{
	}

	PlayTestDebug::~PlayTestDebug()
	{
	}

	void PlayTestDebug::SetOptions(const PlayTestDebugOptions& options)
	{
		_options = options;
		if (!(_options.TimeScale > 0.0f)) {
			// Zero, negative or not a number, the pause is what stops the time
			_options.TimeScale = 1.0f;
		}
		_options.TimeScale = std::clamp(_options.TimeScale, MinTimeScale, MaxTimeScale);
		_timeControlled = (_options.Paused || _options.TimeScale != 1.0f);
		if (!_options.Paused) {
			_stepPending = false;
			_frameFrozen = false;
		}
		if (!_timeControlled) {
			_frameTimeMult = 1.0f;
		}
		UpdateOverlay();
	}

	void PlayTestDebug::Step()
	{
		if (!_options.Paused) {
			_options.Paused = true;
			_timeControlled = true;
		}
		_stepPending = true;
	}

	bool PlayTestDebug::TeleportPlayer(std::int32_t playerIndex, Vector2f pos)
	{
		if (playerIndex < 0 || playerIndex >= (std::int32_t)_levelHandler._players.size()) {
			return false;
		}

		Actors::Player* player = _levelHandler._players[playerIndex];
		Vector2f target;
		if (!FindClearPlayerPosition(pos, 320.0f, target)) {
			return false;
		}

		// It stands still where it's put
		player->_speed = Vector2f::Zero;
		player->_externalForce = Vector2f::Zero;
		player->_internalForceY = 0.0f;
		player->WarpToPosition(target, WarpFlags::Fast);
		return true;
	}

	bool PlayTestDebug::FindClearPlayerPosition(Vector2f pos, float radius, Vector2f& result) const
	{
		Tiles::TileMap* tileMap = _levelHandler._tileMap.get();
		if (tileMap == nullptr) {
			return false;
		}

		const Vector2i bounds = tileMap->GetLevelBounds();
		const auto fits = [tileMap](Vector2f candidate) {
			// The hitbox of a standing player, see LevelHandler::FindClearSpawnPosition()
			AABBf aabb = AABBf(candidate.X - 11.0f, candidate.Y + 8.0f - 26.0f, candidate.X + 11.0f, candidate.Y + 8.0f + 12.0f);
			Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true };
			return tileMap->IsTileEmpty(aabb, params);
		};

		// In growing rings around the position, up first, because the player falls down from there
		constexpr float Step = 8.0f;
		for (float distance = 0.0f; distance <= radius; distance += Step) {
			const std::int32_t count = std::max<std::int32_t>(1, std::int32_t(distance * 6.2832f / Step));
			for (std::int32_t i = 0; i < count; i++) {
				const float angle = -1.5708f + 6.2832f * i / count;
				const Vector2f candidate(pos.X + std::cos(angle) * distance, pos.Y + std::sin(angle) * distance);
				if (candidate.X < 16.0f || candidate.Y < 16.0f || candidate.X > bounds.X - 16.0f || candidate.Y > bounds.Y - 16.0f) {
					continue;
				}
				if (fits(candidate)) {
					result = candidate;
					return true;
				}
			}
		}
		return false;
	}

	void PlayTestDebug::DespawnTileEventActors(ArrayView<const std::uint32_t> sortedCells, std::int32_t width)
	{
		if (sortedCells.empty() || width <= 0) {
			return;
		}
		// As when the tiles go out of range, a generator keeps its own object, its events can't be changed this way
		for (auto& actor : _levelHandler._actors) {
			if ((actor->_state & (Actors::ActorState::IsCreatedFromEventMap | Actors::ActorState::IsFromGenerator)) != Actors::ActorState::IsCreatedFromEventMap ||
				actor->_originTile.X < 0 || actor->_originTile.Y < 0 || actor->_originTile.X >= width) {
				continue;
			}
			std::uint32_t cell = std::uint32_t(actor->_originTile.X) + std::uint32_t(actor->_originTile.Y) * std::uint32_t(width);
			if (std::binary_search(sortedCells.begin(), sortedCells.end(), cell)) {
				actor->_state |= Actors::ActorState::IsDestroyed;
			}
		}
		if (_levelHandler._activeBoss != nullptr && _levelHandler._activeBoss->GetState(Actors::ActorState::IsDestroyed)) {
			_levelHandler._activeBoss->OnDeactivatedBoss();
			_levelHandler._activeBoss = nullptr;
		}
		if (_levelHandler._eventMap != nullptr) {
			for (std::uint32_t cell : sortedCells) {
				_levelHandler._eventMap->Deactivate(std::int32_t(cell % std::uint32_t(width)), std::int32_t(cell / std::uint32_t(width)));
			}
		}
	}

	float PlayTestDebug::BeginFrame(float timeMult)
	{
		if DEATH_LIKELY(!_timeControlled) {
			return timeMult;
		}

		_frameFrozen = false;
		if (_options.Paused) {
			if (_stepPending && _levelHandler._pauseMenu == nullptr) {
				// Exactly one update at the normal rate
				_stepPending = false;
				timeMult = 1.0f;
			} else {
				_frameFrozen = true;
				timeMult = 0.0f;
			}
		} else {
			timeMult *= _options.TimeScale;
		}
		_frameTimeMult = timeMult;
		return timeMult;
	}

	std::unique_ptr<SceneNode> PlayTestDebug::CreateRootNode()
	{
		return std::make_unique<PlayTestRootNode>(*this);
	}

	void PlayTestDebug::UpdateOverlay()
	{
		if (_options.ShowHitboxes || _options.ShowEventZones) {
			if (_overlay == nullptr) {
				_overlay = std::make_unique<PlayTestDebugOverlay>(*this);
			}
			SceneNode* hudParent = _levelHandler.GetHudParentNode();
			if (_overlay->parent() != hudParent) {
				_overlay->setParent(hudParent);
			}
		} else {
			// Not visited at all while nothing of it is shown
			DetachOverlay();
		}
	}

	void PlayTestDebug::DetachOverlay()
	{
		if (_overlay != nullptr) {
			_overlay->setParent(nullptr);
		}
	}

	ArrayView<const std::unique_ptr<Rendering::PlayerViewport>> PlayTestDebug::GetViewports() const
	{
		return _levelHandler._assignedViewports;
	}

	PlayTestRootNode::PlayTestRootNode(PlayTestDebug& debug)
		: _debug(debug)
	{
	}

	void PlayTestRootNode::OnUpdate(float timeMult)
	{
		if DEATH_LIKELY(!_debug._timeControlled) {
			SceneNode::OnUpdate(timeMult);
			return;
		}
		if (_debug._frameFrozen) {
			// Not a single object moves in a frame the time stands still in
			return;
		}
		SceneNode::OnUpdate(_debug._frameTimeMult);
	}

	PlayTestDebugOverlay::PlayTestDebugOverlay(PlayTestDebug& debug)
		: _debug(debug)
	{
	}

	bool PlayTestDebugOverlay::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		const PlayTestDebugOptions& options = _debug._options;
		ViewSize = _debug._levelHandler.GetViewSize();
		for (const auto& viewport : _debug.GetViewports()) {
			// Not across the views of the other players in splitscreen
			const Rectf bounds = viewport->GetBounds();
			ClipRect = bounds;
			if (options.ShowEventZones) {
				DrawEventZones(*viewport, bounds);
			}
			if (options.ShowHitboxes) {
				DrawHitboxes(*viewport, bounds);
			}
		}
		ClipRect = Rectf(0.0f, 0.0f, 0.0f, 0.0f);
		return true;
	}

	void PlayTestDebugOverlay::DrawEventZones(const Rendering::PlayerViewport& viewport, const Rectf& bounds)
	{
		Events::EventMap* eventMap = _debug._levelHandler.EventMap();
		if (eventMap == nullptr) {
			return;
		}

		// Only the tiles of the level, the off-grid events have no zone of their own
		const Vector2i layoutSize = eventMap->GetSize();
		const Rectf worldView = GetWorldView(viewport);
		constexpr float TileSize = (float)Tiles::TileSet::DefaultTileSize;
		const std::int32_t x1 = std::max<std::int32_t>(0, (std::int32_t)std::floor(worldView.X / TileSize));
		const std::int32_t y1 = std::max<std::int32_t>(0, (std::int32_t)std::floor(worldView.Y / TileSize));
		const std::int32_t x2 = std::min<std::int32_t>(layoutSize.X - 1, (std::int32_t)std::floor((worldView.X + worldView.W) / TileSize));
		const std::int32_t y2 = std::min<std::int32_t>(layoutSize.Y - 1, (std::int32_t)std::floor((worldView.Y + worldView.H) / TileSize));

		const float scale = viewport.GetCameraScale();
		const Vector2f origin = Vector2f(bounds.X, bounds.Y);
		for (std::int32_t y = y1; y <= y2; y++) {
			for (std::int32_t x = x1; x <= x2; x++) {
				const Events::EventMap::EventTile& tile = eventMap->GetEventTile(x, y);
				if (tile.Event == EventType::Empty) {
					continue;
				}
				const Colorf color = GetEventZoneColor(tile.Event);
				if (color.A <= 0.0f) {
					continue;
				}
				const Vector2f pos = origin + viewport.WorldToView(Vector2f(x * TileSize, y * TileSize));
				DrawSolid(pos, EventZoneLayer, Vector2f(TileSize * scale, TileSize * scale), color);
			}
		}
	}

	void PlayTestDebugOverlay::DrawHitboxes(const Rendering::PlayerViewport& viewport, const Rectf& bounds)
	{
		const Rectf worldView = GetWorldView(viewport);
		const Vector2f origin = Vector2f(bounds.X, bounds.Y);
		for (const auto& actor : _debug._levelHandler.GetActors()) {
			// The hitbox the collisions are tested with (see ActorBase::IsCollidingWith())
			const AABBf& box = actor->AABBInner;
			if (box.R <= box.L || box.B <= box.T || box.R < worldView.X || box.L > worldView.X + worldView.W ||
				box.B < worldView.Y || box.T > worldView.Y + worldView.H) {
				continue;
			}

			const Vector2f topLeft = origin + viewport.WorldToView(Vector2f(box.L, box.T));
			const Vector2f bottomRight = origin + viewport.WorldToView(Vector2f(box.R, box.B));
			const Rectf rect = Rectf(topLeft.X, topLeft.Y, bottomRight.X - topLeft.X, bottomRight.Y - topLeft.Y);
			const Colorf color = GetHitboxColor(actor.get());
			DrawSolid(Vector2f(rect.X, rect.Y), HitboxLayer, Vector2f(rect.W, rect.H), Colorf(color.R, color.G, color.B, 0.15f));
			DrawOutline(rect, HitboxLayer + 1, color);
		}
	}

	void PlayTestDebugOverlay::DrawOutline(const Rectf& rect, std::uint16_t z, const Colorf& color)
	{
		// One pixel wide, inside the rectangle
		const float w = std::max(rect.W, 1.0f);
		const float h = std::max(rect.H, 1.0f);
		DrawSolid(Vector2f(rect.X, rect.Y), z, Vector2f(w, 1.0f), color);
		DrawSolid(Vector2f(rect.X, rect.Y + h - 1.0f), z, Vector2f(w, 1.0f), color);
		DrawSolid(Vector2f(rect.X, rect.Y + 1.0f), z, Vector2f(1.0f, std::max(h - 2.0f, 0.0f)), color);
		DrawSolid(Vector2f(rect.X + w - 1.0f, rect.Y + 1.0f), z, Vector2f(1.0f, std::max(h - 2.0f, 0.0f)), color);
	}
}

#endif
