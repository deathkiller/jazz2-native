#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"
#include "../../../nCine/Primitives/Rect.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>

using namespace nCine;

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::ArrayView;
	using Death::Containers::SmallVector;
#endif

	/** @brief Kind of a change of a level being edited, supports a bitwise combination of its member values */
	enum class DocumentChangeKind : std::uint32_t
	{
		None = 0,						/**< Nothing changed */
		Tiles = 0x01,					/**< Tiles of layers, see @ref DocumentChanges::GetTileAreas() */
		LayerProperties = 0x02,			/**< Descriptions or names of layers */
		LayerStructure = 0x04,			/**< Layers were added, removed, moved or resized */
		AnimationTiming = 0x08,			/**< Timing of animated tiles */
		AnimationStructure = 0x10,		/**< Animated tiles were added or removed, or their frames changed */
		Events = 0x20,					/**< Events, see @ref DocumentChanges::GetEventArea() */
		Palette = 0x40,					/**< Palettes */
		TileSets = 0x80,				/**< Tile sets */
		TileOverrides = 0x100,			/**< Overridden tile diffuse or masks */
		Texts = 0x200,					/**< Level texts */
		LevelProperties = 0x400,		/**< Level properties or flags */
		EditorState = 0x800,			/**< State saved with the level only for the editor, such as hidden layers */
		Chunks = 0x1000,				/**< Other chunks */

		All = 0x1FFF					/**< Everything */
	};

	DEATH_ENUM_FLAGS(DocumentChangeKind);

	/**
		@brief Area of a layer whose tiles changed
	*/
	struct DocumentTileArea
	{
		/** @brief Index of the layer */
		std::int32_t Layer;
		/** @brief Changed tiles */
		Recti Area;
	};

	/**
		@brief Collects what changed in a level being edited

		Edit commands report what they change, the editor then brings everything that shows the level up to date
		at once. Changed tiles are tracked as one area for each layer, changed events as one area of the grid.
	*/
	class DocumentChanges
	{
	public:
		DocumentChanges();

		/** @brief Returns `true` if nothing changed */
		bool IsEmpty() const {
			return (_kinds == DocumentChangeKind::None);
		}
		/** @brief Returns all kinds of changes */
		DocumentChangeKind GetKinds() const {
			return _kinds;
		}
		/** @brief Returns `true` if any of the given kinds changed */
		bool HasAny(DocumentChangeKind kinds) const {
			return (_kinds & kinds) != DocumentChangeKind::None;
		}
		/** @brief Returns the changed areas of layers */
		ArrayView<const DocumentTileArea> GetTileAreas() const {
			return _tileAreas;
		}
		/** @brief Returns the changed area of the event grid, or an empty rectangle */
		const Recti& GetEventArea() const {
			return _eventArea;
		}
		/**
		 * @brief Returns `false` if tiles changed that may be outside of @ref GetTileAreas()
		 *
		 * A change reported only by its kind, as a snapshot of layers restored, doesn't tell which tiles it changed, all
		 * of them have to be taken as changed then.
		 */
		bool AreTileAreasComplete() const {
			return !_tilesOutsideAreas;
		}
		/**
		 * @brief Returns `false` if events changed that may be outside of @ref GetEventArea()
		 *
		 * An event off the grid is in the area of the tile its position is in.
		 */
		bool IsEventAreaComplete() const {
			return !_eventsOutsideArea;
		}
		/**
		 * @brief Returns the index each layer had before, for each index it has now, if the layers were only moved
		 *
		 * Empty if the layers weren't moved, or if they changed otherwise too --- added, removed, resized or replaced
		 * by a snapshot (see @ref DocumentChangeKind::LayerStructure).
		 */
		ArrayView<const std::int32_t> GetLayerOrder() const {
			return _layerOrder;
		}

		/**
		 * @brief Adds changes of the given kinds
		 *
		 * Tiles and events added this way are of no known area, see @ref AreTileAreasComplete() and
		 * @ref IsEventAreaComplete(), those of a known one are added by @ref AddTiles() and @ref AddEvents().
		 */
		void Add(DocumentChangeKind kinds);
		/** @brief Adds a move of a layer of @p count layers, from an index to another one, see @ref GetLayerOrder() */
		void AddLayerMove(std::int32_t from, std::int32_t to, std::int32_t count);
		/** @brief Adds changed tiles of a layer */
		void AddTiles(std::int32_t layer, const Recti& area);
		/** @brief Adds changed events */
		void AddEvents(const Recti& area);
		/** @brief Adds all changes of another collection */
		void Add(const DocumentChanges& other);
		/** @brief Clears all changes */
		void Clear();

	private:
		DocumentChangeKind _kinds;
		SmallVector<DocumentTileArea, 4> _tileAreas;
		Recti _eventArea;
		// See AreTileAreasComplete() and IsEventAreaComplete()
		bool _tilesOutsideAreas;
		bool _eventsOutsideArea;
		// See GetLayerOrder(), whether the layers changed only by moves is told by it being non-empty
		SmallVector<std::int32_t, 0> _layerOrder;

		// Adds moves of layers, or forgets them if the layers changed otherwise too
		void AddLayerOrder(ArrayView<const std::int32_t> order, bool onlyMoved);
	};
}

#endif
