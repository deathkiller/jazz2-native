#include "PlayOverlays.h"

#if defined(WITH_EDITOR)

#include "../Common/LayerView.h"
#include "../Document/DocumentChanges.h"
#include "../Document/LevelDocument.h"
#include "../Files/EditorSettings.h"
#include "../Runtime/EditorLevelHandler.h"
#include "../Runtime/PlayPathHistory.h"
#include "../Tools/EditorContext.h"
#include "../UI/UiPainter.h"
#include "../UI/UiTheme.h"
#include "../../EventType.h"
#include "../../PlayerType.h"
#include "../../Events/EventMap.h"
#if defined(WITH_MULTIPLAYER)
#	include "../../Multiplayer/RaceRouteGenerator.h"
#endif

#include "../../../nCine/I18n.h"

#include <algorithm>
#include <cmath>

namespace Jazz2::Editor
{
	PlayOverlays::PlayOverlays(IEditorContext& context)
		: _context(context)
	{
	}

	bool PlayOverlays::ComputeReachability(SmallVectorImpl<std::int32_t>& distances, std::int32_t character, bool fromMultiplayerStarts)
	{
		distances.clear();
#if defined(WITH_MULTIPLAYER)
		EditorLevelHandler* editHandler = _context.GetEditHandler();
		if (editHandler == nullptr || editHandler->TileMap() == nullptr || editHandler->EventMap() == nullptr) {
			return false;
		}
		const LevelDocument& document = _context.GetDocument();
		Events::EventMap* eventMap = editHandler->EventMap();
		std::int32_t startCharacter = (character > 0 ? character : _context.GetSettings().PlayCharacter);
		Vector2f start = eventMap->GetSpawnPosition(PlayerType(std::clamp<std::int32_t>(startCharacter, 1, 3)));
		if (start.X < 0.0f || start.Y < 0.0f) {
			start = eventMap->GetSpawnPosition(PlayerType::Jazz);
		}
		SmallVector<Vector2f, 4> starts;
		if (start.X >= 0.0f && start.Y >= 0.0f) {
			starts.push_back(start);
		}
		Vector2i gridSize = document.GetEventGridSize();
		if (fromMultiplayerStarts && gridSize.X > 0) {
			for (const DocumentEvent& event : document.Events) {
				if (EventType(event.Record.Type) == EventType::LevelStartMultiplayer) {
					starts.push_back(Vector2f(float(event.Cell % std::uint32_t(gridSize.X)) * 32.0f + 16.0f, float(event.Cell / std::uint32_t(gridSize.X)) * 32.0f + 16.0f));
				}
			}
		}
		if (starts.empty()) {
			return false;
		}

		// Only the moves of the character asked for, with none asked for, of any of them
		using Multiplayer::TrackCharacterMoves;
		TrackCharacterMoves moves = (character <= 0 ? TrackCharacterMoves::All
			: (character == std::int32_t(PlayerType::Spaz) ? TrackCharacterMoves::DoubleJump : TrackCharacterMoves::Glide));
		// As the multiplayer starts the tracer is given otherwise, it finds the nearest tile the player can be in, from
		// several starts each tile is as far as from the nearest of them
		float waterLevel = float(document.Properties.WaterLevel);
		SmallVector<std::int32_t, 0> fromStart;
		for (Vector2f pos : starts) {
			if (!Multiplayer::ComputeReachableTiles(editHandler->TileMap(), eventMap, waterLevel, pos, fromStart, moves)) {
				continue;
			}
			if (distances.empty()) {
				distances.assign(fromStart.begin(), fromStart.end());
				continue;
			}
			for (std::size_t i = 0; i < distances.size() && i < fromStart.size(); i++) {
				if (fromStart[i] >= 0 && (distances[i] < 0 || fromStart[i] < distances[i])) {
					distances[i] = fromStart[i];
				}
			}
		}
		// The tracer works on the sprite layer, so its tiles are those of the event grid
		return (!distances.empty() && std::int32_t(distances.size()) == gridSize.X * gridSize.Y);
#else
		static_cast<void>(character);
		static_cast<void>(fromMultiplayerStarts);
		return false;
#endif
	}

	void PlayOverlays::RecomputeReachability(bool reload)
	{
		if (reload) {
			// The events of the level shown are as it was loaded, so it's loaded again for them
			_context.ReloadRuntime();
		}
		const EditorSettings::PlayTestSettings& settings = _context.GetSettings().PlayTest;
		if (!ComputeReachability(_reachability, settings.ReachCharacter, settings.ReachFromMpStarts)) {
			_reachability.clear();
			return;
		}
		// Found once, it's drawn by how far each tile is out of the farthest one every frame
		_reachabilityMaxDistance = 1;
		for (std::int32_t distance : _reachability) {
			_reachabilityMaxDistance = std::max(_reachabilityMaxDistance, distance);
		}
	}

	void PlayOverlays::ToggleReachability()
	{
		if (!_reachability.empty()) {
			_reachability.clear();
			return;
		}
		// The level is loaded again first, so the events are as they are now
		RecomputeReachability(true);
		if (_reachability.empty()) {
			_context.ShowMessage(_x("editor", "It can't be found out, the level needs a start of the character it's played with"));
			return;
		}
		std::int32_t reached = 0;
		for (std::int32_t distance : _reachability) {
			reached += (distance >= 0 ? 1 : 0);
		}
		_context.ShowMessage(_fx("editor", "The player can get to {} tiles, shown green, found again after each change", reached));
	}

	void PlayOverlays::HideReachability()
	{
		_reachability.clear();
		_reachabilityMaxDistance = 1;
	}

	void PlayOverlays::NoteDocumentChanges(const DocumentChanges& changes)
	{
		if (!_reachability.empty() && changes.HasAny(DocumentChangeKind::Tiles | DocumentChangeKind::Events | DocumentChangeKind::LayerStructure |
			DocumentChangeKind::TileOverrides | DocumentChangeKind::TileSets | DocumentChangeKind::LevelProperties)) {
			// Found again once the changes stop for a moment, the events of the level shown are only those it was loaded with
			_reachabilityDirty = true;
			_reachabilityWait = 30.0f;
			_reachabilityNeedsReload |= changes.HasAny(DocumentChangeKind::Events | DocumentChangeKind::LayerStructure |
				DocumentChangeKind::TileSets | DocumentChangeKind::LevelProperties);
		}
	}

	void PlayOverlays::Update(float timeMult, bool strokeInProgress)
	{
		// Found again after the level was changed, once the changes stop for a moment, and not while a tool is used
		if (!_reachabilityDirty || _reachability.empty() || strokeInProgress || _context.IsPlaying()) {
			return;
		}
		_reachabilityWait -= timeMult;
		if (_reachabilityWait > 0.0f) {
			return;
		}
		bool reload = _reachabilityNeedsReload;
		_reachabilityDirty = false;
		_reachabilityNeedsReload = false;
		RecomputeReachability(reload);
	}

	void PlayOverlays::DrawDeaths(UiPainter& painter, const Rectf& viewRect)
	{
		if (!_context.GetSettings().PlayTest.ShowDeaths) {
			return;
		}
		std::int32_t spriteLayer = _context.GetDocument().GetSpriteLayerIndex();
		LayerView view = _context.GetLayerView(spriteLayer >= 0 ? spriteLayer : 0);
		float zoom = std::clamp(std::sqrt(view.Scale), 0.4f, 1.5f);
		float unit = std::max(1.0f, painter.GetScale()) * zoom;

		// Deaths close to each other are a single place, larger and stronger the more often the player died there, found
		// again only once the paths changed
		const PlayPathHistory& history = _context.GetPlayPathHistory();
		if (_deathSpotsRevision != history.GetRevision()) {
			_deathSpotsRevision = history.GetRevision();
			_deathSpots.clear();
			_deathSpotsMaxCount = 1;

			SmallVector<Vector2f, 0> deaths;
			history.CollectDeaths(deaths);
			constexpr float CellSize = 48.0f;
			struct DeathCell
			{
				Vector2i Cell;
				Vector2f Pos;
			};
			SmallVector<DeathCell, 0> sorted;
			sorted.reserve(deaths.size());
			for (Vector2f pos : deaths) {
				sorted.push_back(DeathCell{ Vector2i(std::int32_t(std::floor(pos.X / CellSize)), std::int32_t(std::floor(pos.Y / CellSize))), pos });
			}
			// By the cell, so the deaths of a cell follow each other
			std::sort(sorted.begin(), sorted.end(), [](const DeathCell& a, const DeathCell& b) {
				return (a.Cell.Y < b.Cell.Y || (a.Cell.Y == b.Cell.Y && a.Cell.X < b.Cell.X));
			});
			for (std::size_t i = 0; i < sorted.size(); ) {
				Vector2f sum;
				std::int32_t count = 0;
				std::size_t j = i;
				for (; j < sorted.size() && sorted[j].Cell == sorted[i].Cell; j++) {
					sum += sorted[j].Pos;
					count++;
				}
				_deathSpots.push_back(DeathSpot{ sum / float(count), count });
				_deathSpotsMaxCount = std::max(_deathSpotsMaxCount, count);
				i = j;
			}
		}

		std::int32_t maxCount = _deathSpotsMaxCount;
		for (const DeathSpot& spot : _deathSpots) {
			Vector2f center = view.WorldToScreen(spot.Center);
			float radius = (5.0f + 4.0f * std::sqrt(float(spot.Count))) * unit;
			if (center.X + radius < viewRect.X || center.Y + radius < viewRect.Y || center.X - radius > viewRect.X + viewRect.W ||
				center.Y - radius > viewRect.Y + viewRect.H) {
				continue;
			}
			Colorf color = UiTheme::OverlayBad;
			color.A = 0.3f + 0.45f * float(spot.Count) / float(maxCount);
			painter.Circle(center, radius, color, UiLayer::Content);
			Colorf core = UiTheme::OverlayBad;
			painter.Circle(center, std::max(2.0f, radius * 0.3f), core, UiLayer::Content);
		}
	}
}

#endif
