#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "PlayTestTypes.h"
#include "../Document/LevelDocument.h"

namespace Jazz2::Editor
{
	/**
		@brief Paths the player took in the last play-tests of a level

		Kept in the editor-only chunk `ppth` of the level (see @ref Formats::LevelFile::IsEditorOnlyChunk()), so the
		paths are still there when the level is opened again, and the deaths of all of them can be shown together. The
		chunk starts with a version byte, then the number of paths, each with its time in frames, whether the level was
		finished, and its segments of points as differences from the previous point with the speed of the player there,
		a path thinned out to a few thousand points.
	*/
	class PlayPathHistory
	{
	public:
		/** @brief Tag of the chunk the paths are kept in */
		static constexpr std::uint32_t Chunk = Formats::LevelFile::MakeChunkTag('p', 'p', 't', 'h');
		/** @brief Version of the chunk, a chunk of another one is left alone until new paths are written over it */
		static constexpr std::uint8_t Version = 2;
		/** @brief Most paths kept, the oldest go first */
		static constexpr std::int32_t MaxPaths = 8;
		/** @brief Most points of a path written into the level */
		static constexpr std::int32_t MaxStoredPoints = 4096;

		/** @brief Forgets all paths */
		void Clear();
		/** @brief Returns `true` if there is no path */
		bool IsEmpty() const {
			return _paths.empty();
		}
		/**
		 * @brief Adds the path of a play-test
		 *
		 * A play-test that goes on after it was left keeps its path, so the path of the same @p playTest replaces the
		 * last one instead.
		 */
		void Update(const PlayPath& path, std::uint32_t playTest);
		/** @brief Returns the paths, the oldest first */
		ArrayView<const PlayPath> GetPaths() const {
			return _paths;
		}
		/** @brief Returns a number that changes whenever the paths change, so what is made of them can be kept until then */
		std::uint32_t GetRevision() const {
			return _revision;
		}
		/** @brief Returns the places where the player died in all paths */
		void CollectDeaths(SmallVectorImpl<Vector2f>& positions) const;
		/** @brief Returns the shortest time of the paths in which the level was finished, in frames, or -1 */
		float GetBestExitTime() const;
		/** @brief Returns how many times the player died in all paths */
		std::int32_t GetTotalDeaths() const;

		/** @brief Reads the paths from the level, returns `false` if it has none or they can't be read */
		bool Read(const LevelDocument& document);
		/** @brief Writes the paths into the level */
		void Write(LevelDocument& document) const;

		/** @brief Writes paths into a level and reads them back, returns the number of problems found, see @ref EditorSelfTest */
		static std::int32_t SelfTest();

	private:
		SmallVector<PlayPath, 0> _paths;
		// The play-test whose path is the last one, see Update()
		std::uint32_t _lastPlayTest = 0;
		// See GetRevision()
		std::uint32_t _revision = 0;
	};
}

#endif
