#pragma once

#include "GsShaderTypes.h"
#include "../RhiUniformNameHash.h"

#include <cstdint>
#include <vector>

#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;

namespace nCine::RHI::GS
{
	/**
		@brief Caches the host-side value of a single uniform (aliased as `RHI::UniformCache`)

		Mirrors the OpenGL backend's uniform cache: it holds a pointer into a shared host data buffer and the
		`Set*` methods that write values into it, plus a dirty flag. For a loose uniform, @ref CommitValue()
		publishes the value pointer to the owning shader program so the generated fixed-function effects can
		read it at draw time (the fixed-function equivalent of a `glUniform*` upload). For a uniform living
		inside a block, committing is a no-op - the whole block is published by @ref GsShaderUniformBlocks.
	*/
	class GsUniformCache
	{
	public:
		GsUniformCache()
			: _uniform(nullptr), _dataPointer(nullptr), _isDirty(false) {}
		explicit GsUniformCache(const GsUniform* uniform)
			: _uniform(uniform), _dataPointer(nullptr), _isDirty(false) {}

		inline const GsUniform* GetUniform() const {
			return _uniform;
		}
		inline std::uint8_t* GetDataPointer() const {
			return _dataPointer;
		}
		inline void SetDataPointer(std::uint8_t* dataPointer) {
			_dataPointer = dataPointer;
		}

		const float* GetFloatVector() const;
		float GetFloatValue(std::uint32_t index) const;
		const std::int32_t* GetIntVector() const;
		std::int32_t GetIntValue(std::uint32_t index) const;

		bool SetFloatVector(const float* vec);
		bool SetFloatValue(float v0);
		bool SetFloatValue(float v0, float v1);
		bool SetFloatValue(float v0, float v1, float v2);
		bool SetFloatValue(float v0, float v1, float v2, float v3);
		bool SetIntVector(const std::int32_t* vec);
		bool SetIntValue(std::int32_t v0);
		bool SetIntValue(std::int32_t v0, std::int32_t v1);
		bool SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2);
		bool SetIntValue(std::int32_t v0, std::int32_t v1, std::int32_t v2, std::int32_t v3);

		inline bool IsDirty() const {
			return _isDirty;
		}
		inline void SetDirty(bool isDirty) {
			_isDirty = isDirty;
		}
		/** @brief Publishes a dirty loose-uniform value to the owning program; a no-op for block members */
		bool CommitValue();

	private:
		const GsUniform* _uniform;
		std::uint8_t* _dataPointer;
		bool _isDirty;

		bool CheckFloat() const;
		bool CheckInt() const;
		bool CheckComponents(std::uint32_t requiredComponents) const;
	};

	/**
		@brief Caches the contents of a uniform block (aliased as `RHI::UniformBlockCache`)

		Holds a pointer into a shared host data buffer mirroring the block's std140 layout and a
		@ref GsUniformCache for each member, each pointing at its member offset within that buffer. The block
		is published as one contiguous range by @ref GsShaderUniformBlocks, which the draw dispatch decodes.
	*/
	class GsUniformBlockCache
	{
	public:
		GsUniformBlockCache()
			: _uniformBlock(nullptr), _dataPointer(nullptr), _usedSize(0) {}
		explicit GsUniformBlockCache(GsUniformBlock* uniformBlock);

		inline const GsUniformBlock* uniformBlock() const {
			return _uniformBlock;
		}
		std::uint32_t GetIndex() const;
		std::int32_t GetBindingIndex() const;
		std::int32_t GetSize() const;
		std::uint8_t GetAlignAmount() const;

		inline std::uint8_t* GetDataPointer() {
			return _dataPointer;
		}
		inline const std::uint8_t* GetDataPointer() const {
			return _dataPointer;
		}
		void SetDataPointer(std::uint8_t* dataPointer);

		inline std::int32_t usedSize() const {
			return _usedSize;
		}
		void SetUsedSize(std::int32_t usedSize);

		bool CopyData(std::uint32_t destIndex, const std::uint8_t* src, std::uint32_t numBytes);
		inline bool CopyData(const std::uint8_t* src) {
			return CopyData(0, src, std::uint32_t(_usedSize));
		}

		/** @brief Returns the member uniform cache with the specified name, or `nullptr` if not found */
		GsUniformCache* GetUniform(StringView name);
		void SetBlockBinding(std::int32_t blockBinding);

	private:
		GsUniformBlock* _uniformBlock;
		std::uint8_t* _dataPointer;
		std::int32_t _usedSize;

		struct NamedCache
		{
			String Name;
			GsUniformCache Cache;
		};
		std::vector<NamedCache> _uniformCaches;
		// Fingerprints of the _uniformCaches names, in the same order and kept in their own packed
		// array so a lookup touches only these few bytes (see @ref HashUniformName)
		std::vector<std::uint32_t> _uniformNameHashes;
	};
}
