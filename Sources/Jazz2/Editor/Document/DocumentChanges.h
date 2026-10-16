#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../../Main.h"
#include "../../../nCine/Primitives/Rect.h"

#include <Containers/ArrayView.h>
#include <Containers/SmallVector.h>

using namespace nCine;

namespace Jazz2::Editor
{
	using Death::Containers::ArrayView;
	using Death::Containers::SmallVector;

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

		/** @brief Adds changes of the given kinds */
		void Add(DocumentChangeKind kinds);
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
	};
}

#endif
