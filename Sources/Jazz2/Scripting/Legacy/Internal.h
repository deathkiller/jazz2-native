#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "Definitions.h"
#include "../LevelScriptLoader.h"
#include "../../ContentResolver.h"
#include "../../LevelHandler.h"

#include <Containers/StringUtils.h>

// What only the sources of the parts of the API of JJ2+ share
#if defined(DEATH_TRACE)
#	define noop() Unimplemented(NCINE_CURRENT_FUNCTION)
#else
#	define noop() do {} while (false)
#endif

namespace Jazz2::Scripting
{
	// Returns the name of the converted tile set of a file name of the original - the tile sets are converted by their names
	// in lower case, without the extension (see Compatibility::JJ2Level) - or an empty string if it can't be one
	inline String GetTileSetName(StringView fileName) {
		String name = StringUtils::lowercase(fileName);
		if (name.hasSuffix(".j2t"_s)) {
			name = String(name.exceptSuffix(".j2t"_s));
		}
		return (Formats::LevelFile::IsSafeTileSetPath(name) ? name : String());
	}

	// Returns true if any pixel in the given AABB is solid in the sprite (collision) layer's mask. Read-only
	// (DestructType::None), so it never destroys tiles. Only the sprite layer carries collision masks in this engine.
	// The edges of the rectangle are pixels of it (see Tiles::TileMap::IsTileEmpty()), the masks of one-way tiles are masks too
	inline bool IsRegionMasked(const AABBf& aabb) {
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = owner->GetLevelHandler()->TileMap();
		if (tileMap == nullptr) {
			return false;
		}
		Tiles::TileCollisionParams params = { Tiles::TileDestructType::None, true };
		return !tileMap->IsTileEmpty(aabb, params);
	}

	// Where a layer of the level is drawn for the view of a player - the layer coordinate at the top left corner of the
	// view, as jjLAYER::getXPosition() and getYPosition() of JJ2+ return it, so without the pixel every layer that moves
	// with the level is drawn higher here (see Tiles::LayerDescription::GetDrawOffsets()). The view is that of the player,
	// smaller in a split screen.
	inline bool GetLayerPositionInView(LevelHandler* levelHandler, Actors::Player* player, std::int32_t layerIndex, Vector2f& result) {
		auto tileMap = levelHandler->TileMap();
		const Tiles::LayerDescription* description = (tileMap != nullptr ? tileMap->GetLayerDescription(layerIndex) : nullptr);
		if (description == nullptr) {
			return false;
		}
		Rectf view = levelHandler->GetCameraView(player);
		Vector2f origin = tileMap->GetGameplayLayerOrigin(layerIndex, view);
		result = Vector2f(view.X - origin.X, view.Y - origin.Y - (description->SpeedModelY != Tiles::LayerSpeedModel::AlwaysOnTop ? 1.0f : 0.0f));
		return true;
	}
}

#endif
