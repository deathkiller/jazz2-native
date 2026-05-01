#pragma once

#if defined(WITH_RHI_GL)

#include "UniformCache.h"
#include "../../../Base/StaticHashMap.h"

#include <string>

namespace nCine::RHI
{
	class ShaderProgram;

	/// Handles all the uniforms of a shader program
	class ShaderUniforms
	{
	public:
		static constexpr std::uint32_t UniformCachesHashSize = 16;
		// TODO: Using FNV1aHashFunc because other hash functions are causing graphics glitches for some reason
		using UniformHashMapType = StaticHashMap<String, UniformCache, UniformCachesHashSize, FNV1aHashFunc<String>>;

		ShaderUniforms();
		explicit ShaderUniforms(ShaderProgram* shaderProgram);
		ShaderUniforms(ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);

		inline void SetProgram(ShaderProgram* shaderProgram) {
			SetProgram(shaderProgram, nullptr, nullptr);
		}
		void SetProgram(ShaderProgram* shaderProgram, const char* includeOnly, const char* exclude);
		void SetUniformsDataPointer(GLubyte* dataPointer);
		void SetDirty(bool isDirty);

		inline std::uint32_t GetUniformCount() const {
			return uniformCaches_.size();
		}
		inline bool HasUniform(const char* name) const {
			return (uniformCaches_.find(String::nullTerminatedView(name)) != nullptr);
		}
		UniformCache* GetUniform(const char* name);
		inline const UniformHashMapType GetAllUniforms() const {
			return uniformCaches_;
		}
		void CommitUniforms();

	private:
		ShaderProgram* shaderProgram_;
		UniformHashMapType uniformCaches_;

		/// Imports the uniforms with the option of including only some or excluding others
		void ImportUniforms(const char* includeOnly, const char* exclude);
	};
}

#endif // defined(WITH_RHI_GL)
