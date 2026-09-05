#pragma once

#include "../../../../Main.h"

#if defined(WITH_RHI_GXM) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <cstdint>

#include <Containers/Array.h>
#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

#include <psp2/gxm.h>

using namespace Death::Containers;

namespace nCine::RHI::GXM
{
	/**
		@brief On-disk cache of the GXP binaries SceShaccCg produced for the PS Vita's Cg stage sources

		sceGxm consumes compiled GXP binaries and the SDK ships no offline compiler for them, so every stage
		of every program is compiled on the console at startup by the firmware's own Cg compiler (see
		@relativeref{nCine::RHI::GXM,GxmShaderProgram::CompileCgStage()}). That is around a hundred SceShaccCg
		invocations before the first frame, and it is what the console spends its startup on. What comes back
		is self-contained: the blob `sceShaccCgCompileProgram()` hands over is exactly what
		`sceGxmShaderPatcherRegisterProgram()` takes later, so keeping it means the compiler never has to run
		for that stage again - and, when nothing is missing, never has to be *loaded* either (`libshacccg.suprx`
		is only brought up on the first miss, see @ref GxmDevice::EnsureShaderCompiler()).

		**How an entry is invalidated.** Every entry is keyed by a 64-bit hash of the exact Cg source string it
		was compiled from. Those sources are generated artifacts (`Shaders/Generated/CgGeneratedShaders.h`), so
		editing a ".shader" file and regenerating changes the hash of every stage it produced and leaves the
		rest of the cache alone: the changed stages miss and are recompiled, everything else is still a hit.
		There is no version number for anyone to remember to bump - a stale entry cannot be *found*, only
		orphaned, and an orphan is dropped the next time the pack is written because the pack is rewritten from
		what this run actually used. The file header adds the two invalidations a per-entry source hash cannot
		express: @ref FileVersion, for a change in this format or in how the engine consumes a GXP, and a
		fingerprint of the `libshacccg.suprx` the entries were produced by, so a different compiler build does
		not have its output handed to a different driver. Either mismatch rejects the whole pack, as does a
		truncated or corrupt one - in every case the console recompiles and writes a fresh pack rather than
		failing, so a cache is never something the game needs to have.

		**Two packs.** A read-only one shipped in the VPK next to the content (@ref PrebakedPath) is loaded
		first, then the writable one under the cache path is loaded over it. Writes only ever go to the
		writable one. That is what makes
		"precompiled offline" work without an offline compiler: run once on a console, pull the written pack
		back, and ship it - and because entries are keyed by source hash, a shipped pack that was forgotten at
		the last shader change is not wrong, only incomplete.
	*/
	class GxmShaderCache
	{
	public:
		GxmShaderCache() = delete;
		~GxmShaderCache() = delete;

		/** @brief Signature every pack file starts with */
		static constexpr std::uint8_t Signature[] = { 0xEF, 0xBB, 0xBF, 0xF0, 0x9F, 0x8C, 0xAA, 0x21 };
		/** @brief Format revision; bump when the layout below changes or a GXP is consumed differently */
		static constexpr std::uint16_t FileVersion = 1 | 0x1000;
		/** @brief Name both packs carry */
		static constexpr const char* FileName = "ShadersGxm.bin";
		/**
			@brief Where a pack shipped inside the VPK is looked for, read-only

			The application's own directory is mounted at "app0:" and the content travels inside the VPK, so
			this is the content path on this platform. It is spelled here rather than plumbed down from the
			application because only this backend has a shipped shader cache at all - the path is as much a
			property of the console as "ur0:/data/" is.
		*/
		static constexpr const char* PrebakedPath = "app0:/Content/Shaders/";

		/**
			@brief Loads the prebaked pack and then the writable one over it

			@param writablePath  Directory the pack is written back to (empty disables the cache entirely)
			@param compilerFingerprint  Identifies the on-console Cg compiler the entries were produced by

			Neither pack has to exist. A pack whose header does not match is reported and ignored.
		*/
		static void Initialize(StringView writablePath, std::uint32_t compilerFingerprint);
		/** @brief Writes the pack back if this run changed it, then releases everything */
		static void Shutdown();

		/** @brief Whether a cache directory was configured (the lookups below are no-ops when it was not) */
		static bool IsAvailable();
		/** @brief Number of entries currently held, from either pack plus whatever this run compiled */
		static std::uint32_t GetEntryCount();

		/**
			@brief Returns the cached GXP for @p source, or `nullptr` when there is none

			The returned block is allocated with `std::malloc()` and owned by the caller, which is the contract
			@relativeref{nCine::RHI::GXM,GxmShaderProgram::CompileCgStage()} already has with its callers.
		*/
		static SceGxmProgram* Lookup(const char* source, bool vertexStage, std::uint32_t& sizeInBytes);
		/** @brief Records the GXP a compile produced for @p source, replacing any entry already under its key */
		static void Store(const char* source, bool vertexStage, const void* binary, std::uint32_t sizeInBytes);

		/** @brief Writes the pack back if this run changed it (also done by @ref Shutdown()) */
		static void Flush();

	private:
		struct Entry
		{
			std::uint64_t Key;
			Array<std::uint8_t> Binary;
			/** Whether this run looked the entry up; an entry no build of the game asks for is not written back */
			bool Used;
		};

		/** @brief Hash identifying one stage source: its bytes plus which stage it is compiled as */
		static std::uint64_t KeyOf(const char* source, bool vertexStage);
		/** @brief Reads one pack into @ref _entries, overwriting entries that are already there; returns how many entries it read (`0` when there is none) */
		static std::int32_t LoadPack(StringView path, bool writable);
		static Entry* Find(std::uint64_t key);

		static String _writablePath;
		static std::uint32_t _compilerFingerprint;
		static SmallVector<Entry, 0> _entries;
		/** An entry was added or replaced, or a loaded one went unused - either way the pack on disk is not this one */
		static bool _dirty;
		/** A write failed once (a full or read-only card), so no further one is attempted or reported */
		static bool _writeFailed;
		static bool _initialized;
	};
}

#endif
