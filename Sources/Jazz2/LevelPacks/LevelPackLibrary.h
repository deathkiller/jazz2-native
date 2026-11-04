#pragma once

#if defined(WITH_LEVEL_PACKS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "LevelPackManifest.h"
#include "../Compatibility/ConversionProgress.h"

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>
#include <Threading/Spinlock.h>

#include <atomic>
#include <memory>

namespace Jazz2::LevelPacks
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using Death::Containers::String;
	using Death::Containers::StringView;
#endif

	/** @brief Outcome of an operation of @ref LevelPackLibrary */
	enum class LevelPackResult : std::uint8_t
	{
		/** It succeeded */
		Success,
		/** Level packs can't be stored on this device */
		Unavailable,
		/** Another pack is being installed or removed */
		Busy,
		/** It was cancelled */
		Cancelled,
		/** The archive can't be read */
		InvalidArchive,
		/** The archive has no levels */
		NoLevels,
		/** The archive has two different files of the same name */
		NameConflict,
		/** The archive or a file in it is too large */
		TooLarge,
		/** A file can't be written */
		CannotWrite,
		/** There's no such pack */
		NotFound
	};

	/** @brief What is known about a pack before it's installed */
	struct LevelPackInfo
	{
		/** @brief Identifier in the level database, empty for a pack from a local file */
		String Id;
		/** @brief Where the pack came from, `digiex` or `local` */
		String Source;
		/** @brief Title of the pack, the name of the archive if empty */
		String Title;
		/** @brief Authors of the pack */
		String Authors;
		/** @brief Description of the pack */
		String Description;
		/** @brief Version of the pack in the level database */
		String Version;
	};

	/**
		@brief Installed level packs

		The original files of all packs are stored once in `Files` of @ref ContentResolver::GetLevelPacksPath(), named
		`<name>~<hash>.<extension>` by their content, so a file that more packs have is there only once. A file that's
		the same as the one of the original game isn't stored at all. A stored file is deleted once no pack has it
		anymore. The levels are converted into @ref ContentResolver::GetLevelPacksCachePath(), they're named
		`+<id>/<level>`, or `+<id>_<episode>/<level>` for a level of an episode of the pack, and they're converted again
		whenever the cache is rebuilt.

		Installing, removing and converting may run on a thread of its own, one at a time.
	*/
	class LevelPackLibrary
	{
	public:
		/** @brief Returns the library */
		static LevelPackLibrary& Get();

		LevelPackLibrary(const LevelPackLibrary&) = delete;
		LevelPackLibrary& operator=(const LevelPackLibrary&) = delete;

		/** @brief Reads the records of the installed packs again */
		void Refresh();
		/** @brief Returns the installed packs, sorted by title */
		SmallVector<std::shared_ptr<const LevelPackManifest>, 0> GetPacks();
		/** @brief Returns an installed pack by its identifier */
		std::shared_ptr<const LevelPackManifest> FindPack(StringView id);
		/** @brief Returns the installed pack a level of the form `+<id>/<level>` belongs to */
		std::shared_ptr<const LevelPackManifest> FindPackOfLevel(StringView levelName);

		/**
		 * @brief Installs a pack from a ZIP archive, or installs it again if it's installed already
		 *
		 * @param archivePath   The archive
		 * @param info          What is known about the pack
		 * @param progress      Reports how far it has got
		 * @param cancel        Cancels it once set, can be `nullptr`
		 * @param installedId   Receives the identifier of the pack, can be `nullptr`
		 */
		LevelPackResult Install(StringView archivePath, const LevelPackInfo& info, Compatibility::ConversionProgress progress = {},
			const std::atomic<bool>* cancel = nullptr, String* installedId = nullptr);
		/** @brief Removes a pack and every file only it had, on the main thread, it changes the preferences */
		LevelPackResult Remove(StringView id);
		/** @brief Converts packs whose levels were converted by another version of the converter or are gone, returns how many */
		std::int32_t ReconvertStale(Compatibility::ConversionProgress progress = {});
		/** @brief Removes what an interrupted operation left behind and files no pack has */
		void CleanUp();

		/**
		 * @brief Returns the full path of an original file of the pack a level belongs to
		 *
		 * For scripts of a pack, which load files by their original name. Returns an empty string if the level isn't
		 * of a pack, or the pack has no such file.
		 */
		String FindOriginalFile(StringView levelName, StringView fileName);
		/**
		 * @brief Returns the name a tile set of the pack a level belongs to was converted to
		 *
		 * For scripts of a pack, which name tile sets by their original name. Returns an empty string if the level
		 * isn't of a pack, or the pack has no such tile set.
		 */
		String FindTileSet(StringView levelName, StringView tileSetName);

		/** @brief Returns the identifier of the pack from a level or an episode name of the form `+<id>...` */
		static StringView GetPackId(StringView name);
		/** @brief Returns a description of a result for messages */
		static StringView GetResultDescription(LevelPackResult result);

	private:
		LevelPackLibrary();

		Death::Threading::Spinlock _lock;
		SmallVector<std::shared_ptr<const LevelPackManifest>, 0> _packs;
		String _storePath;
		std::atomic<bool> _busy;
		bool _loaded;

		void EnsureLoaded();
		LevelPackResult ConvertStaged(StringView stagingPath, LevelPackManifest& manifest, Compatibility::ConversionProgress progress,
			const std::atomic<bool>* cancel);
		void RemoveConvertedFiles(StringView id);
		void CollectGarbage();
	};
}

#endif
