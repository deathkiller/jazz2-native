#pragma once

#include "RHI/Rhi.h"
#include "../Base/Object.h"

#include <memory>
#include <string>

namespace ShaderCompiler
{
	class RuntimeProgram;
	struct ProgramVariant;
}

namespace nCine
{
	/**
		@brief GPU shader program usable by materials and drawable nodes

		Wraps an OpenGL shader program built from a vertex and a fragment shader. The sources can
		come from memory, a file or the binary cache, and a matching variant can be registered for
		rendering batched draw commands.
	*/
	class Shader : public Object
	{
		friend class ShaderState;
		friend class Material;

	public:
		/** @brief Load mode */
		enum class LoadMode {
			String,
			File
		};

		/** @brief Introspection mode */
		enum class Introspection {
			Enabled,
			NoUniformsInBlocks,
			Disabled
		};

		Shader();

		Shader(const char* shaderName, LoadMode loadMode, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize);
		Shader(const char* shaderName, LoadMode loadMode, const char* vertex, const char* fragment, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize);
		Shader(LoadMode loadMode, const char* vertex, const char* fragment, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize);

		~Shader() override;

		Shader(const Shader&) = delete;
		Shader& operator=(const Shader&) = delete;

		bool LoadFromMemory(const char* shaderName, Introspection introspection, const char* vertex, const char* fragment, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromMemory(const char* shaderName, const char* vertex, const char* fragment, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize);
		bool LoadFromMemory(const char* vertex, const char* fragment, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize);
		/**
		 * @brief Loads a ShaderCompiler program variant, using its reflection instead of GL introspection
		 *
		 * The variant's sources are compiled like with the plain-string overloads, but uniforms, uniform
		 * blocks and attributes come from the offline reflection data, with targeted location queries only.
		 */
		bool LoadFromMemory(const char* shaderName, Introspection introspection, const ShaderCompiler::ProgramVariant& variant, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize);

		bool LoadFromFile(const char* shaderName, Introspection introspection, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize, ArrayView<const StringView> defines = {});
		bool LoadFromFile(const char* shaderName, StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize);
		bool LoadFromFile(StringView vertexPath, StringView fragmentPath, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize);

		/**
		 * @brief Loads one variant of an annotated ".shader" program file (ShaderCompiler format)
		 *
		 * The file is compiled with the same pipeline the offline ShaderCompiler uses, including
		 * `#include` directives resolved relative to the file, so externally supplied
		 * shaders behave exactly like the precompiled ones. Pass `nullptr` as @p variantName
		 * for the unnamed base variant (`Variants[0]`).
		 */
		bool LoadFromShaderFile(const char* shaderName, Introspection introspection, StringView path, const char* variantName = nullptr, std::int32_t batchSize = RHI::ShaderProgram::DefaultBatchSize);

		/**
		 * @brief Compiles an annotated ".shader" program file (ShaderCompiler format) into @p program without creating any GPU objects
		 *
		 * Exposes the lowered sources and reflection of every variant, so the caller can
		 * compute a reflected batch size before loading a variant with LoadFromMemory().
		 */
		static bool CompileShaderFile(StringView path, ShaderCompiler::RuntimeProgram& program);

		bool LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, Introspection introspection);
		/** @brief Loads a binary-cached program, importing uniforms, blocks and attributes from ShaderCompiler reflection instead of GL introspection */
		bool LoadFromCache(const char* shaderName, std::uint64_t shaderVersion, Introspection introspection, const ShaderCompiler::ProgramVariant& variant);

		bool SaveToCache(const char* shaderName, std::uint64_t shaderVersion) const;

		/** @brief Sets the VBO stride and pointer for the specified vertex attribute */
		bool SetAttribute(const char* name, std::int32_t stride, void* pointer);

		/** @brief Returns `true` if the shader is linked and can therefore be used */
		bool IsLinked() const;

		/** @brief Returns the length of the information log, including the null terminator */
		unsigned int RetrieveInfoLogLength() const;
		/** @brief Retrieves the information log and copies it into the provided string object */
		void RetrieveInfoLog(std::string& infoLog) const;

		/** @brief Returns the automatic log on errors flag */
		bool GetLogOnErrors() const;
		/**
		 * @brief Sets the automatic log on errors flag
		 *
		 * When the flag is `true` the shader automatically logs compilation and linking errors.
		 */
		void SetLogOnErrors(bool shouldLogOnErrors);

		/** @brief Sets the OpenGL object label for the shader program */
		void SetGLShaderProgramLabel(const char* label);

		/** @brief Registers a shader to be used when rendering batches of render commands */
		void RegisterBatchedShader(Shader& batchedShader);

		/** @brief Returns the `render_mode` flags of the shader (a bitmask of @ref ShaderCompiler::RenderMode, 0 when none were declared) */
		inline std::uint32_t GetRenderModes() const {
			return renderModes_;
		}
		/**
		 * @brief Sets the `render_mode` flags of the shader
		 *
		 * Loaders that see the program-level reflection (e.g. ContentResolver) call this so that
		 * @ref Material::SetShader() can apply the declared blending preset automatically.
		 */
		inline void SetRenderModes(std::uint32_t renderModes) {
			renderModes_ = renderModes;
		}

		RHI::ShaderProgram* GetHandle() {
			return glShaderProgram_.get();
		}

		inline static ObjectType sType() {
			return ObjectType::Shader;
		}

	private:
		/** @brief Underlying OpenGL shader program */
		std::unique_ptr<RHI::ShaderProgram> glShaderProgram_;
		// "render_mode" bitmask (ShaderCompiler::RenderMode), 0 when none were declared
		std::uint32_t renderModes_;
	};
}