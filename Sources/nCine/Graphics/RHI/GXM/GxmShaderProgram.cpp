#include "GxmShaderProgram.h"
#include "GxmShaderUniforms.h"
#include "GxmBufferObject.h"
#include "GxmDevice.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"
#include "../../../../Shaders/Generated/CgGeneratedShaders.h"
#include "../../Material.h"

#include <cstdlib>
#include <cstring>

#include <Containers/StringConcatenable.h>

#include <vitashark.h>

namespace nCine::RHI::GXM
{
	namespace
	{
		/** @brief Maps an engine blending factor onto its sceGxm counterpart */
		SceGxmBlendFactor TranslateBlendFactor(nCine::BlendingFactor factor)
		{
			switch (factor) {
				case nCine::BlendingFactor::Zero: return SCE_GXM_BLEND_FACTOR_ZERO;
				case nCine::BlendingFactor::One: return SCE_GXM_BLEND_FACTOR_ONE;
				case nCine::BlendingFactor::SrcColor: return SCE_GXM_BLEND_FACTOR_SRC_COLOR;
				case nCine::BlendingFactor::OneMinusSrcColor: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
				case nCine::BlendingFactor::SrcAlpha: return SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
				case nCine::BlendingFactor::OneMinusSrcAlpha: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				case nCine::BlendingFactor::DstAlpha: return SCE_GXM_BLEND_FACTOR_DST_ALPHA;
				case nCine::BlendingFactor::OneMinusDstAlpha: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				case nCine::BlendingFactor::DstColor: return SCE_GXM_BLEND_FACTOR_DST_COLOR;
				case nCine::BlendingFactor::OneMinusDstColor: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
				case nCine::BlendingFactor::SrcAlphaSaturate: return SCE_GXM_BLEND_FACTOR_SRC_ALPHA_SATURATE;
				default:
					// The constant-colour factors have no sceGxm counterpart (there is no per-context blend
					// colour), and the renderer never selects them
					return SCE_GXM_BLEND_FACTOR_ONE;
			}
		}

		/**
			@brief Rewrites the batch size baked into a generated batched Cg source

			The emitter bakes the fallback the shader's own `#ifndef BATCH_SIZE` declares (the 64 KB uniform
			block budget divided by the instance stride) into a plain `#define`, because a Cg source is
			compiled as one string with no place to inject a define ahead of it. When the runtime settles on a
			different batch size - a smaller uniform-block budget, a `WITH_FIXED_BATCH_SIZE` build, the retry
			after a failed compile - the number in that line is rewritten here so the shader's instance array
			matches what the pipeline will actually bind.
		*/
		String PatchBatchSize(const char* source, std::uint32_t batchSize)
		{
			static const char Marker[] = "#define BATCH_SIZE ";
			const char* found = std::strstr(source, Marker);
			if (found == nullptr) {
				return String{source};
			}

			const char* valueBegin = found + (sizeof(Marker) - 1);
			const char* valueEnd = valueBegin;
			while (*valueEnd != '\0' && *valueEnd != '\n' && *valueEnd != '\r') {
				valueEnd++;
			}

			char number[16];
			const std::int32_t numberLength = std::snprintf(number, sizeof(number), "%u", batchSize);
			return StringView{source, std::size_t(valueBegin - source)}
				+ StringView{number, std::size_t(numberLength > 0 ? numberLength : 0)}
				+ StringView{valueEnd};
		}

		/** @brief Forwards SceShaccCg diagnostics into the engine log */
		void SharkLogCallback(const char* message, shark_log_level level, int line)
		{
			if (message == nullptr) {
				return;
			}
			switch (level) {
				case SHARK_LOG_ERROR: LOGE("Cg compiler (line {}): {}", line, message); break;
				case SHARK_LOG_WARNING: LOGW("Cg compiler (line {}): {}", line, message); break;
				default: LOGD("Cg compiler (line {}): {}", line, message); break;
			}
		}
	}

	std::uint32_t GxmShaderProgram::_nextHandle = 1;

	GxmShaderProgram::GxmShaderProgram()
		: GxmShaderProgram(QueryPhase::Immediate)
	{
	}

	GxmShaderProgram::GxmShaderProgram(QueryPhase queryPhase)
		: _handle(_nextHandle++), _status(Status::NotLinked), _introspection(Introspection::Enabled), _queryPhase(queryPhase),
			_batchSize(std::uint32_t(DefaultBatchSize)), _shouldLogOnErrors(true), _uniformsSize(0), _uniformBlocksSize(0),
			_reflection(nullptr), _programName(nullptr), _variantName(nullptr),
			_boundVbo(nullptr), _boundIbo(nullptr), _vboOffset(0), _usesCornerStream(false), _usesBatchedStream(false),
			_usesGeometryStream(false), _geometryStride(0),
			_vertexStage(nullptr), _fragmentStage(nullptr), _vertexStageId(nullptr), _fragmentStageId(nullptr),
			_vertexUniformBufferSize(0), _fragmentUniformBufferSize(0)
	{
	}

	GxmShaderProgram::GxmShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: GxmShaderProgram(queryPhase)
	{
		static_cast<void>(vertexFile);
		static_cast<void>(fragmentFile);
		_introspection = introspection;
	}

	GxmShaderProgram::GxmShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: GxmShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	GxmShaderProgram::GxmShaderProgram(StringView vertexFile, StringView fragmentFile)
		: GxmShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	GxmShaderProgram::~GxmShaderProgram()
	{
		GxmDevice::OnProgramDestroyed(this);
		ReleaseGpu();
	}

	void GxmShaderProgram::ReleaseGpu()
	{
		SceGxmShaderPatcher* patcher = GxmDevice::GetShaderPatcher();
		if (patcher != nullptr) {
			// A patched program may still be referenced by a scene the GPU has not finished, and releasing it
			// frees its USSE code
			GxmDevice::FinishScene();
			if (SceGxmContext* context = GxmDevice::GetContext()) {
				sceGxmFinish(context);
			}
			for (CachedVertexProgram& cached : _vertexPrograms) {
				sceGxmShaderPatcherReleaseVertexProgram(patcher, cached.Program);
			}
			for (CachedFragmentProgram& cached : _fragmentPrograms) {
				sceGxmShaderPatcherReleaseFragmentProgram(patcher, cached.Program);
			}
			if (_vertexStageId != nullptr) {
				sceGxmShaderPatcherUnregisterProgram(patcher, _vertexStageId);
			}
			if (_fragmentStageId != nullptr) {
				sceGxmShaderPatcherUnregisterProgram(patcher, _fragmentStageId);
			}
		}
		_vertexPrograms.clear();
		_fragmentPrograms.clear();
		_vertexStageId = nullptr;
		_fragmentStageId = nullptr;

		// The GXP binaries are our own copies of the compiler's output (see CompileCgStage), and the patcher
		// only borrowed them
		std::free(_vertexStage);
		std::free(_fragmentStage);
		_vertexStage = nullptr;
		_fragmentStage = nullptr;
	}

	bool GxmShaderProgram::IsLinked() const
	{
		return (_status == Status::Linked || _status == Status::LinkedWithDeferredQueries || _status == Status::LinkedWithIntrospection);
	}

	bool GxmShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(filename);
		// The stage sources come from the generated Cg table, resolved by SetProgramIdentity()
		return true;
	}

	bool GxmShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		static_cast<void>(stage);
		static_cast<void>(string);
		return true;
	}

	bool GxmShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		return true;
	}

	bool GxmShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		static_cast<void>(filename);
		return true;
	}

	bool GxmShaderProgram::CompileStages()
	{
		SceGxmShaderPatcher* patcher = GxmDevice::GetShaderPatcher();
		if (patcher == nullptr) {
			LOGE("Cannot compile shaders: the sceGxm shader patcher is not created yet");
			return false;
		}

		const GeneratedCgShader* sources = FindGeneratedCgShader(_programName, _variantName);
		if (sources == nullptr) {
			if (_shouldLogOnErrors) {
				LOGE("No generated Cg source for shader \"{}\" (variant \"{}\"): either its Cg transform was "
					"declined by the emitter, or the shader is compiled at runtime, which this backend cannot do",
					_programName != nullptr ? _programName : "<unnamed>", _variantName != nullptr ? _variantName : "");
			}
			return false;
		}

		// Only a batched program carries the baked define, and only then is there anything to rewrite
		String vertexSource, fragmentSource;
		const char* vertexText = sources->VertexSource;
		const char* fragmentText = sources->FragmentSource;
		if (_batchSize != std::uint32_t(DefaultBatchSize) && _batchSize > 0) {
			vertexSource = PatchBatchSize(vertexText, _batchSize);
			fragmentSource = PatchBatchSize(fragmentText, _batchSize);
			vertexText = vertexSource.data();
			fragmentText = fragmentSource.data();
		}

		std::uint32_t vertexSize = 0;
		_vertexStage = CompileCgStage(vertexText, true, vertexSize);
		if (_vertexStage == nullptr) {
			LOGE("Failed to compile the vertex stage of shader \"{}\"", _programName != nullptr ? _programName : "<unnamed>");
			return false;
		}

		std::uint32_t fragmentSize = 0;
		_fragmentStage = CompileCgStage(fragmentText, false, fragmentSize);
		if (_fragmentStage == nullptr) {
			LOGE("Failed to compile the fragment stage of shader \"{}\"", _programName != nullptr ? _programName : "<unnamed>");
			std::free(_vertexStage);
			_vertexStage = nullptr;
			return false;
		}

		std::int32_t result = sceGxmShaderPatcherRegisterProgram(patcher, _vertexStage, &_vertexStageId);
		if (result < 0) {
			LOGE("sceGxmShaderPatcherRegisterProgram(vertex) failed with 0x{:.8x}", std::uint32_t(result));
			return false;
		}
		result = sceGxmShaderPatcherRegisterProgram(patcher, _fragmentStage, &_fragmentStageId);
		if (result < 0) {
			LOGE("sceGxmShaderPatcherRegisterProgram(fragment) failed with 0x{:.8x}", std::uint32_t(result));
			return false;
		}

		_vertexUniformBufferSize = sceGxmProgramGetDefaultUniformBufferSize(_vertexStage);
		_fragmentUniformBufferSize = sceGxmProgramGetDefaultUniformBufferSize(_fragmentStage);
		return true;
	}

	void GxmShaderProgram::ReflectStage(const SceGxmProgram* program, SmallVector<GxmUniformSlot, 0>& uniformSlots,
		SmallVector<GxmBlockUpload, 0>& blockUploads, SmallVector<GxmSamplerSlot, 0>& samplerSlots)
	{
		if (program == nullptr || _reflection == nullptr) {
			return;
		}

		// Loose uniforms: the byte size comes from the engine-side reflection rather than the compiled
		// parameter, because that is the layout the uniform cache wrote and the whole value moves as one copy
		for (const GxmUniform& uniform : _uniforms) {
			if (uniform.GetBlockIndex() != -1) {
				continue;
			}
			const SceGxmProgramParameter* parameter = sceGxmProgramFindParameterByName(program, uniform.GetName());
			if (parameter == nullptr) {
				continue;
			}
			// Samplers are resolved separately below, out of the reflection list that carries their unit
			if (sceGxmProgramParameterGetCategory(parameter) == SCE_GXM_PARAMETER_CATEGORY_UNIFORM) {
				uniformSlots.push_back({uniform.GetName(), sceGxmProgramParameterGetResourceIndex(parameter),
					uniform.GetMemorySize()});
			}
		}

		// Uniform blocks: the Cg emitter hoisted each member to a top-level uniform, so a block is uploaded
		// member by member from the range the pipeline bound. The binding index the pipeline uses is the
		// block's own index (see GxmShaderUniformBlocks::Bind()).
		for (std::size_t blockIndex = 0; blockIndex < _reflection->BlockCount; blockIndex++) {
			const ShaderCompiler::UniformBlock& block = _reflection->Blocks[blockIndex];
			// A batched instance array has to move as one contiguous run, because the reflection describes one
			// element and the shader's array is addressed by index - there is no per-element parameter to resolve.
			// Every other block is a set of independent uniforms that the Cg emitter hoisted out of it, and each
			// one is written where the *compiler* put it, at its own size (see below).
			const bool blockIsInstanceArray = (block.InstanceStride > 0);
			std::int64_t blockBase = INT64_MIN;
			for (std::size_t memberIndex = 0; memberIndex < block.MemberCount; memberIndex++) {
				const ShaderCompiler::BlockMember& member = block.Members[memberIndex];
				// A member a stage does not read simply is not there, which is normal - only the vertex stage
				// reads the transform, only the fragment stage reads the palette offset - so an unresolved
				// member is not itself worth reporting. What matters is a *block* that reaches neither stage,
				// which is checked once after both have been reflected.
				GxmBlockUpload::Destination destination = GxmBlockUpload::Destination::DefaultUniformBuffer;
				std::uint32_t index = 0;
				if (!FindUniformBase(program, member.Name, destination, index)) {
					continue;
				}
				std::uint32_t memberBytes;
				std::uint32_t sourceStride = 0;
				std::uint32_t compiledStride = 0;
				if (blockIsInstanceArray) {
					// How far apart the compiler put two elements of the array, which is not necessarily the
					// std140 stride the pipeline writes them at: the widest Instance struct carries a palOffset
					// and the narrowest (BatchedLighting) does not, so the compiler packs a shorter element and
					// only element zero of a single contiguous copy ends up where the shader looks for it
					char elementName[128];
					std::snprintf(elementName, sizeof(elementName), "%s[1]", member.Name);
					GxmBlockUpload::Destination elementDestination = destination;
					std::uint32_t elementIndex = 0;
					if (FindUniformBase(program, elementName, elementDestination, elementIndex)
						&& elementDestination == destination && elementIndex > index) {
						sourceStride = block.InstanceStride;
						compiledStride = (elementIndex - index) * 4u;
						if (compiledStride != sourceStride) {
							LOGD("Shader \"{}\" packs \"{}\" {} bytes apart, not the {} its std140 stride implies",
								_programName != nullptr ? _programName : "<unnamed>", member.Name,
								compiledStride, sourceStride);
						}
					}
					// The bound range from this member to the end of the block, so a batched draw uploads every
					// instance it filled rather than one - clamping to a single element is exactly the kind of
					// silent truncation that renders "almost right". This does assume the compiled array element
					// is laid out like the engine's std140 one, which it is (verified on the console).
					const std::uint32_t blockBytes = (blockIndex < _uniformBlocks.size()
						? std::uint32_t(_uniformBlocks[blockIndex].GetSize()) : 0u);
					memberBytes = (blockBytes > member.Offset ? blockBytes - member.Offset : 0u);
				} else {
					// Just this member. Copying the rest of the block along with it - which is what the batched
					// case above has to do - would only land correctly if the compiler had placed every following
					// uniform exactly where std140 places it, and nothing says it does: it packs what a stage
					// actually reads, in its own order. Where that differs, the members after the first get each
					// other's bytes, and a `spriteSize` holding some other member's data is a quad of the wrong
					// size - a light covering the whole screen instead of its own small area.
					const std::uint32_t elements = (member.ArraySize > 0
						&& member.ArraySize != ShaderCompiler::SymbolicArraySize ? std::uint32_t(member.ArraySize) : 1u);
					memberBytes = UniformTypeInfo::ComponentCount(member.Type) * 4u * elements;
					if (memberBytes == 0) {
						// A type with no component count of its own (a struct aggregate); the old bulk copy is
						// the only thing that can move it, so keep doing that rather than nothing
						const std::uint32_t blockBytes = (blockIndex < _uniformBlocks.size()
							? std::uint32_t(_uniformBlocks[blockIndex].GetSize()) : 0u);
						memberBytes = (blockBytes > member.Offset ? blockBytes - member.Offset : 0u);
					}

					// Whether that assumption would have held is worth knowing, so the difference between where
					// the compiler put this member and where std140 puts it is compared against the first
					// member's - a block whose members disagree is one the old bulk copy corrupted
					const std::int64_t base = std::int64_t(index) * 4 - std::int64_t(member.Offset);
					if (blockBase == INT64_MIN) {
						blockBase = base;
					} else if (base != blockBase) {
						LOGD("Shader \"{}\" lays out \"{}.{}\" at byte {}, not the {} its std140 offset implies",
							_programName != nullptr ? _programName : "<unnamed>", block.Name, member.Name,
							index * 4u, blockBase + std::int64_t(member.Offset));
					}
				}
				blockUploads.push_back({destination, std::uint32_t(blockIndex), member.Offset, index, memberBytes,
					member.Name, sourceStride, compiledStride});
			}
		}

		// Samplers: the reflection assigns each one the texture unit the material binds to, while the compiled
		// shader decides which sceGxm texture slot it reads - both are needed to route a bound texture
		for (std::size_t i = 0; i < _reflection->TextureCount; i++) {
			const ShaderCompiler::TextureBinding& texture = _reflection->Textures[i];
			const SceGxmProgramParameter* parameter = sceGxmProgramFindParameterByName(program, texture.Name);
			if (parameter == nullptr || sceGxmProgramParameterGetCategory(parameter) != SCE_GXM_PARAMETER_CATEGORY_SAMPLER) {
				continue;
			}
			const std::uint32_t engineUnit = (texture.Unit >= 0 ? std::uint32_t(texture.Unit) : std::uint32_t(i));
			const std::uint32_t textureIndex = sceGxmProgramParameterGetResourceIndex(parameter);
			samplerSlots.push_back({texture.Name, engineUnit, textureIndex});
		}
	}

	bool GxmShaderProgram::Link(Introspection introspection)
	{
		_introspection = introspection;

		if (_reflection == nullptr) {
			if (_shouldLogOnErrors) {
				LOGE("Cannot link a shader program without offline reflection on this backend");
			}
			_status = Status::CompilationFailed;
			return false;
		}

		if (!CompileStages()) {
			_status = Status::CompilationFailed;
			return false;
		}

		return FinalizeAfterLinking(introspection);
	}

	bool GxmShaderProgram::FinalizeAfterLinking(Introspection introspection)
	{
		_introspection = introspection;
		_status = Status::Linked;
		PerformIntrospection();
		return true;
	}

	void GxmShaderProgram::PerformIntrospection()
	{
		if (_introspection == Introspection::Disabled || _reflection == nullptr) {
			return;
		}

		_uniforms.clear();
		_uniformBlocks.clear();
		_attributes.clear();
		_uniformsSize = 0;
		_uniformBlocksSize = 0;
		_vertexUniformSlots.clear();
		_fragmentUniformSlots.clear();
		_vertexBlockUploads.clear();
		_fragmentBlockUploads.clear();
		_vertexSamplerSlots.clear();
		_fragmentSamplerSlots.clear();
		_stageAttributes.clear();

		ImportReflection();
		ReflectStageAttributes();
		ReflectStage(_vertexStage, _vertexUniformSlots, _vertexBlockUploads, _vertexSamplerSlots);
		ReflectStage(_fragmentStage, _fragmentUniformSlots, _fragmentBlockUploads, _fragmentSamplerSlots);

		// A uniform block that resolved in neither stage means every draw of this program reads undefined
		// instance data - the failure mode that looks like garbled sprites rather than like an error
		for (std::size_t blockIndex = 0; blockIndex < _reflection->BlockCount; blockIndex++) {
			bool reached = false;
			for (const GxmBlockUpload& upload : _vertexBlockUploads) {
				reached |= (upload.BindingIndex == blockIndex);
			}
			for (const GxmBlockUpload& upload : _fragmentBlockUploads) {
				reached |= (upload.BindingIndex == blockIndex);
			}
			if (!reached) {
				LOGW("Shader \"{}\" uniform block \"{}\" reaches neither stage; its draws will read undefined uniforms",
					_programName != nullptr ? _programName : "<unnamed>", _reflection->Blocks[blockIndex].Name);
			}
		}

		_status = Status::LinkedWithIntrospection;
	}

	void GxmShaderProgram::ReflectStageAttributes()
	{
		if (_vertexStage == nullptr) {
			return;
		}

		const std::uint32_t count = sceGxmProgramGetParameterCount(_vertexStage);
		for (std::uint32_t i = 0; i < count; i++) {
			const SceGxmProgramParameter* parameter = sceGxmProgramGetParameter(_vertexStage, i);
			if (parameter == nullptr || sceGxmProgramParameterGetCategory(parameter) != SCE_GXM_PARAMETER_CATEGORY_ATTRIBUTE) {
				continue;
			}
			const char* name = sceGxmProgramParameterGetName(parameter);
			if (name == nullptr) {
				continue;
			}
			// An attribute the entry point receives through an input struct is named "<struct>.<member>";
			// the engine knows it by the member alone
			if (const char* lastDot = std::strrchr(name, '.')) {
				name = lastDot + 1;
			}
			_stageAttributes.push_back({name,
				std::uint16_t(sceGxmProgramParameterGetResourceIndex(parameter)),
				std::uint8_t(sceGxmProgramParameterGetComponentCount(parameter))});
		}
	}

	void GxmShaderProgram::ImportReflection()
	{
		const ShaderCompiler::ProgramVariant& reflection = *_reflection;
		std::int32_t nextLocation = 0;

		// Loose uniforms - samplers are kept in a separate reflection list but treated as loose uniforms here
		for (std::size_t i = 0; i < reflection.UniformCount; i++) {
			const ShaderCompiler::Uniform& u = reflection.Uniforms[i];
			_uniforms.emplace_back(this, u.Name, u.Type, std::int32_t(u.ArraySize), nextLocation++);
			_uniformsSize += _uniforms.back().GetMemorySize();
		}
		for (std::size_t i = 0; i < reflection.TextureCount; i++) {
			const ShaderCompiler::TextureBinding& t = reflection.Textures[i];
			_uniforms.emplace_back(this, t.Name, ShaderCompiler::UniformType::Sampler2D, 1, nextLocation++);
			_uniformsSize += _uniforms.back().GetMemorySize();
		}

		_uniformBlocks.reserve(reflection.BlockCount);
		for (std::size_t i = 0; i < reflection.BlockCount; i++) {
			const ShaderCompiler::UniformBlock& b = reflection.Blocks[i];

			// A BATCH_SIZE-sized instance array uses the explicitly set batch size, or the same 64 KB-based
			// fallback the in-shader "#ifndef BATCH_SIZE" defaults assume when no size is injected
			std::uint32_t effectiveBatchSize = 0;
			std::uint32_t dataSize = b.BaseSize;
			if (b.InstanceStride > 0) {
				effectiveBatchSize = (_batchSize != std::uint32_t(DefaultBatchSize) && _batchSize > 0)
					? _batchSize : (64u * 1024u) / b.InstanceStride;
				dataSize += b.InstanceStride * effectiveBatchSize;
			}

			const std::uint32_t blockIndex = std::uint32_t(_uniformBlocks.size());
			_uniformBlocks.emplace_back(blockIndex, b.Name, std::int32_t(dataSize));
			GxmUniformBlock& block = _uniformBlocks.back();
			_uniformBlocksSize += block.GetSize();

			if (_introspection != Introspection::NoUniformsInBlocks) {
				block._members.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					GxmUniform member;
					member.SetName(m.Name);
					member._type = m.Type;
					member._size = (m.ArraySize == ShaderCompiler::SymbolicArraySize)
						? std::int32_t(effectiveBatchSize)
						: (m.ArraySize > 0 ? std::int32_t(m.ArraySize) : 1);
					member._blockIndex = std::int32_t(blockIndex);
					member._offset = std::int32_t(m.Offset);
					member._owner = this;
					block._members.push_back(member);
				}
			}
		}

		for (std::size_t i = 0; i < reflection.AttributeCount; i++) {
			const ShaderCompiler::Attribute& a = reflection.Attributes[i];
			const std::int32_t location = (a.Location >= 0 ? a.Location : std::int32_t(i));
			_attributes.emplace_back(a.Name, a.Type, location);
			_vertexFormat[std::uint32_t(location)].Init(std::uint32_t(location), std::int32_t(UniformTypeInfo::ComponentCount(a.Type)), 0);
		}
	}

	void GxmShaderProgram::Use()
	{
		GxmDevice::BindProgram(this);
	}

	bool GxmShaderProgram::HasAttribute(const char* name) const
	{
		for (const GxmAttribute& attribute : _attributes) {
			if (std::strcmp(attribute.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	bool GxmShaderProgram::IsIntegerAttribute(const char* name) const
	{
		for (const GxmAttribute& attribute : _attributes) {
			if (std::strcmp(attribute.GetName(), name) == 0) {
				const ShaderCompiler::UniformType type = attribute.GetType();
				return (type == ShaderCompiler::UniformType::Int || type == ShaderCompiler::UniformType::UInt);
			}
		}
		return false;
	}

	GxmVertexFormat::Attribute* GxmShaderProgram::GetAttribute(const char* name)
	{
		for (const GxmAttribute& attribute : _attributes) {
			if (std::strcmp(attribute.GetName(), name) == 0 && attribute.GetLocation() >= 0) {
				return &_vertexFormat[std::uint32_t(attribute.GetLocation())];
			}
		}
		return nullptr;
	}

	void GxmShaderProgram::DefineVertexFormat(const GxmBufferObject* vbo, const GxmBufferObject* ibo, std::uint32_t vboOffset)
	{
		_boundVbo = vbo;
		_boundIbo = ibo;
		_vboOffset = vboOffset;
		_vertexFormat.SetIbo(ibo);

		// The corner and instance-index attributes are fed by the device's static streams, not by the
		// pipeline's geometry buffer - the shaders read them instead of gl_VertexID (see VertexIdRewrite).
		// Everything else comes from the bound vertex buffer, exactly like the OpenGL|ES 2.0 profile.
		// The compiled stage decides which streams exist: a sprite shader's corner and instance index come
		// from the device's static streams and appear in no reflection, while a mesh shader's attributes come
		// out of the pipeline's vertex buffer and do
		_usesCornerStream = false;
		_usesBatchedStream = false;
		_usesGeometryStream = false;
		_geometryStride = 0;
		for (const GxmStageAttribute& stageAttribute : _stageAttributes) {
			const bool isCorner = (std::strcmp(stageAttribute.Name, Material::QuadCornerAttributeName) == 0);
			const bool isInstance = (std::strcmp(stageAttribute.Name, "aInstanceIndex") == 0);
			if (isCorner || isInstance) {
				_usesCornerStream = true;
				_usesBatchedStream |= isInstance;
				continue;
			}
			if (vbo == nullptr) {
				continue;
			}
			if (GxmVertexFormat::Attribute* formatAttribute = GetAttribute(stageAttribute.Name)) {
				formatAttribute->setVbo(vbo);
				formatAttribute->SetBaseOffset(vboOffset);
				_usesGeometryStream = true;
				if (formatAttribute->GetStride() > 0) {
					_geometryStride = std::uint32_t(formatAttribute->GetStride());
				}
			}
		}
	}

	void GxmShaderProgram::Reset()
	{
		if (_status != Status::NotLinked && _status != Status::CompilationFailed) {
			_uniforms.clear();
			_uniformBlocks.clear();
			_attributes.clear();
			_uniformsSize = 0;
			_uniformBlocksSize = 0;
			_resolvedUniforms.clear();
			_vertexUniformSlots.clear();
			_fragmentUniformSlots.clear();
			_vertexBlockUploads.clear();
			_fragmentBlockUploads.clear();
			_vertexSamplerSlots.clear();
			_fragmentSamplerSlots.clear();
			_vertexFormat.Reset();
			ReleaseGpu();
		}
		_status = Status::NotLinked;
	}

	void GxmShaderProgram::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}

	void GxmShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		for (ResolvedUniform& resolved : _resolvedUniforms) {
			if (resolved.Name == name) {
				resolved.Data = data;
				return;
			}
		}
		_resolvedUniforms.push_back({String{name}, data});
	}

	const std::uint8_t* GxmShaderProgram::ResolveUniform(const char* name) const
	{
		for (const ResolvedUniform& resolved : _resolvedUniforms) {
			if (resolved.Name == name) {
				return resolved.Data;
			}
		}
		return nullptr;
	}

	void GxmShaderProgram::InstallCompilerLogCallback()
	{
		shark_install_log_cb(SharkLogCallback);
	}

	SceGxmProgram* GxmShaderProgram::CompileCgStage(const char* source, bool vertexStage, std::uint32_t& sizeInBytes)
	{
		// The length goes IN (vitaShaRK reads it as the source length), the compiled size comes back OUT
		sizeInBytes = std::uint32_t(std::strlen(source));
		const SceGxmProgram* compiled = shark_compile_shader(source, &sizeInBytes,
			vertexStage ? SHARK_VERTEX_SHADER : SHARK_FRAGMENT_SHADER);

		SceGxmProgram* owned = nullptr;
		if (compiled != nullptr && sizeInBytes > 0) {
			owned = static_cast<SceGxmProgram*>(std::malloc(sizeInBytes));
			if (owned != nullptr) {
				std::memcpy(owned, compiled, sizeInBytes);
			} else {
				LOGE("Out of memory copying a {} byte GXP binary", sizeInBytes);
			}
		}

		// Only now, with our own copy in hand, is the compiler's output released
		shark_clear_output();
		return owned;
	}

	bool GxmShaderProgram::FindUniformBase(const SceGxmProgram* program, const char* name,
		GxmBlockUpload::Destination& destination, std::uint32_t& index)
	{
		if (program == nullptr || name == nullptr) {
			return false;
		}

		// A member is matched by its own name, or - for an array of structs, which the compiler may only name
		// through its leaves - by the lowest-indexed parameter whose name begins with it
		const std::size_t nameLength = std::strlen(name);
		const std::uint32_t count = sceGxmProgramGetParameterCount(program);
		bool found = false;
		bool foundBuffer = false;
		std::uint32_t lowest = 0;
		for (std::uint32_t i = 0; i < count; i++) {
			const SceGxmProgramParameter* candidate = sceGxmProgramGetParameter(program, i);
			if (candidate == nullptr) {
				continue;
			}
			const SceGxmParameterCategory category = sceGxmProgramParameterGetCategory(candidate);
			if (category != SCE_GXM_PARAMETER_CATEGORY_UNIFORM && category != SCE_GXM_PARAMETER_CATEGORY_UNIFORM_BUFFER) {
				continue;
			}
			const char* candidateName = sceGxmProgramParameterGetName(candidate);
			if (candidateName == nullptr || std::strncmp(candidateName, name, nameLength) != 0) {
				continue;
			}
			// Only a whole-name match counts, so "instances" does not swallow "instancesFoo"
			const char separator = candidateName[nameLength];
			if (separator != '\0' && separator != '[' && separator != '.') {
				continue;
			}

			if (category == SCE_GXM_PARAMETER_CATEGORY_UNIFORM_BUFFER) {
				// The container wins outright: a member that lives in one is not in the default buffer at all,
				// and its buffer index is what sceGxmSet{Vertex,Fragment}UniformBuffer() takes
				destination = GxmBlockUpload::Destination::UniformBufferContainer;
				index = sceGxmProgramParameterGetContainerIndex(candidate);
				LOGI("Uniform \"{}\" lives in uniform buffer container {} (resource index {})",
					candidateName, index, sceGxmProgramParameterGetResourceIndex(candidate));
				foundBuffer = true;
				return true;
			}

			const std::uint32_t resourceIndex = sceGxmProgramParameterGetResourceIndex(candidate);
			if (!found || resourceIndex < lowest) {
				lowest = resourceIndex;
				found = true;
			}
		}

		if (found && !foundBuffer) {
			destination = GxmBlockUpload::Destination::DefaultUniformBuffer;
			index = lowest;
		}
		return found;
	}

	const SceGxmProgramParameter* GxmShaderProgram::FindAttribute(const SceGxmProgram* program, const char* name)
	{
		if (program == nullptr || name == nullptr) {
			return nullptr;
		}

		const std::uint32_t count = sceGxmProgramGetParameterCount(program);
		const SceGxmProgramParameter* parameter = (count > 0 ? sceGxmProgramFindParameterByName(program, name) : nullptr);
		if (parameter != nullptr && sceGxmProgramParameterGetCategory(parameter) == SCE_GXM_PARAMETER_CATEGORY_ATTRIBUTE) {
			return parameter;
		}

		// Fall back to the last dot-separated component, which is what a `<struct>.<member>` reflected name
		// reduces to, and report the real names if that fails too
		for (std::uint32_t i = 0; i < count; i++) {
			const SceGxmProgramParameter* candidate = sceGxmProgramGetParameter(program, i);
			if (candidate == nullptr || sceGxmProgramParameterGetCategory(candidate) != SCE_GXM_PARAMETER_CATEGORY_ATTRIBUTE) {
				continue;
			}
			const char* candidateName = sceGxmProgramParameterGetName(candidate);
			if (candidateName == nullptr) {
				continue;
			}
			const char* lastDot = std::strrchr(candidateName, '.');
			if (std::strcmp(lastDot != nullptr ? lastDot + 1 : candidateName, name) == 0) {
				return candidate;
			}
		}

		// Every parameter is listed, not just the attributes: "no parameters at all" and "parameters but no
		// attributes" are different problems with different fixes, and the whole point of getting here is to
		// tell them apart
		LOGW("Vertex attribute \"{}\" was not resolved; the stage reflects {} parameters:", name, count);
		for (std::uint32_t i = 0; i < count; i++) {
			const SceGxmProgramParameter* candidate = sceGxmProgramGetParameter(program, i);
			if (candidate == nullptr) {
				LOGW("  [{}] <null parameter>", i);
				continue;
			}
			const char* candidateName = sceGxmProgramParameterGetName(candidate);
			LOGW("  [{}] \"{}\" category {} type {} components {} array {} resource {}", i,
				candidateName != nullptr ? candidateName : "<unnamed>",
				std::uint32_t(sceGxmProgramParameterGetCategory(candidate)),
				std::uint32_t(sceGxmProgramParameterGetType(candidate)),
				sceGxmProgramParameterGetComponentCount(candidate),
				sceGxmProgramParameterGetArraySize(candidate),
				sceGxmProgramParameterGetResourceIndex(candidate));
		}
		return nullptr;
	}

	std::uint32_t GxmShaderProgram::PackBlendKey(bool enabled, nCine::BlendingFactor srcRgb, nCine::BlendingFactor dstRgb,
		nCine::BlendingFactor srcAlpha, nCine::BlendingFactor dstAlpha)
	{
		if (!enabled) {
			return 0;
		}
		// The four factors compress to a nibble each through their sceGxm counterparts (12 values), leaving the
		// top bit to mark "blending enabled" so an opaque key stays 0
		return 0x80000000u
			| (std::uint32_t(TranslateBlendFactor(srcRgb)) << 0)
			| (std::uint32_t(TranslateBlendFactor(dstRgb)) << 4)
			| (std::uint32_t(TranslateBlendFactor(srcAlpha)) << 8)
			| (std::uint32_t(TranslateBlendFactor(dstAlpha)) << 12);
	}

	SceGxmVertexProgram* GxmShaderProgram::GetVertexProgram()
	{
		SceGxmShaderPatcher* patcher = GxmDevice::GetShaderPatcher();
		if (patcher == nullptr || _vertexStageId == nullptr) {
			return nullptr;
		}

		const std::uint64_t fingerprint = _vertexFormat.CalculateFingerprint();
		for (const CachedVertexProgram& cached : _vertexPrograms) {
			if (cached.Fingerprint == fingerprint) {
				return cached.Program;
			}
		}

		// Stream 0 is the pipeline's geometry buffer, stream 1 the device's static corner/instance stream.
		// Their strides are the ones the vertex format recorded, or the fixed layout of the static streams.
		SceGxmVertexAttribute gxmAttributes[SCE_GXM_MAX_VERTEX_ATTRIBUTES] = {};
		SceGxmVertexStream gxmStreams[2] = {};
		std::uint32_t attributeCount = 0;
		const std::uint32_t staticStreamIndex = GetStaticStreamIndex();

		// Built from what the compiled stage declares - the reflection does not list a sprite shader's
		// synthesized corner and instance-index inputs at all (see GxmStageAttribute)
		for (const GxmStageAttribute& stageAttribute : _stageAttributes) {
			if (attributeCount >= SCE_GXM_MAX_VERTEX_ATTRIBUTES) {
				break;
			}
			const bool isCorner = (std::strcmp(stageAttribute.Name, Material::QuadCornerAttributeName) == 0);
			const bool isInstance = (std::strcmp(stageAttribute.Name, "aInstanceIndex") == 0);

			SceGxmVertexAttribute& out = gxmAttributes[attributeCount];
			out.regIndex = stageAttribute.RegIndex;
			if (isCorner || isInstance) {
				// The device's static stream: two floats of corner, then the batched instance index
				out.streamIndex = std::uint16_t(staticStreamIndex);
				out.offset = std::uint16_t(isInstance ? 8 : 0);
				out.format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
				out.componentCount = std::uint8_t(isInstance ? 1 : 2);
				attributeCount++;
				continue;
			}

			const GxmVertexFormat::Attribute* formatAttribute = GetAttribute(stageAttribute.Name);
			if (formatAttribute == nullptr || !_usesGeometryStream) {
				// A geometry attribute the pipeline never described has no stream to read from; leaving it
				// undeclared is better than pointing it at nothing
				LOGW("Vertex attribute \"{}\" of shader \"{}\" has no vertex buffer bound and was skipped",
					stageAttribute.Name, _programName != nullptr ? _programName : "<unnamed>");
				continue;
			}

			out.streamIndex = 0;
			out.offset = std::uint16_t(reinterpret_cast<std::uintptr_t>(formatAttribute->GetPointer()));
			out.componentCount = std::uint8_t(formatAttribute->GetSize() > 0 ? formatAttribute->GetSize() : 1);
			// A vertex format may override the component type to unsigned byte (the ImGui vertex colour is
			// 4 x u8 read as a float4), which is honoured here like glVertexAttribPointer does
			if (formatAttribute->GetType() == std::uint32_t(VertexAttribType::UnsignedByte)) {
				out.format = (formatAttribute->IsNormalized() ? SCE_GXM_ATTRIBUTE_FORMAT_U8N : SCE_GXM_ATTRIBUTE_FORMAT_U8);
			} else if (IsIntegerAttribute(stageAttribute.Name)) {
				// The batch element index of a batched mesh shader, which the batcher writes as an `int32_t`
				// because that is what an integer attribute takes in OpenGL. sceGxm has no 32-bit integer vertex
				// format (only 8- and 16-bit ones, F16 and F32), so the low half of the value is read as a U16
				// instead - an integer format, so the shader still receives the index as the number it is, where
				// reading the same bytes as F32 would deliver a denormal and truncate to element 0. Little-endian
				// puts that half first, and no batch comes close to 65535 elements.
				out.format = SCE_GXM_ATTRIBUTE_FORMAT_U16;
				out.componentCount = 1;
			} else {
				out.format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
			}
			attributeCount++;
		}

		if (attributeCount == 0) {
			// A shader with no attribute inputs at all still needs a vertex program; sceGxm accepts an empty
			// attribute set as long as no stream is declared either
			SceGxmVertexProgram* program = nullptr;
			const std::int32_t result = sceGxmShaderPatcherCreateVertexProgram(patcher, _vertexStageId, nullptr, 0, nullptr, 0, &program);
			if (result < 0) {
				LOGE("sceGxmShaderPatcherCreateVertexProgram(no attributes) failed with 0x{:.8x}", std::uint32_t(result));
				return nullptr;
			}
			_vertexPrograms.push_back({fingerprint, program});
			return program;
		}

		// Only the streams the layout actually reads are declared: sceGxm expects an address for every one of
		// them, and a layout with no geometry attributes gives the static stream slot 0 instead
		std::uint32_t streamCount = 0;
		if (_usesGeometryStream) {
			gxmStreams[streamCount].stride = std::uint16_t(_geometryStride);
			gxmStreams[streamCount].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
			streamCount++;
		}
		if (_usesCornerStream) {
			gxmStreams[staticStreamIndex].stride = std::uint16_t(_usesBatchedStream ? 12 : 8);
			gxmStreams[staticStreamIndex].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
			streamCount = staticStreamIndex + 1;
		}

		SceGxmVertexProgram* program = nullptr;
		const std::int32_t result = sceGxmShaderPatcherCreateVertexProgram(patcher, _vertexStageId,
			gxmAttributes, attributeCount, gxmStreams, streamCount, &program);
		if (result < 0) {
			LOGE("sceGxmShaderPatcherCreateVertexProgram({} attributes, {} streams) failed with 0x{:.8x}",
				attributeCount, streamCount, std::uint32_t(result));
			return nullptr;
		}

		_vertexPrograms.push_back({fingerprint, program});
		return program;
	}

	SceGxmFragmentProgram* GxmShaderProgram::GetFragmentProgram(std::uint32_t blendKey, const SceGxmBlendInfo* blendInfo)
	{
		SceGxmShaderPatcher* patcher = GxmDevice::GetShaderPatcher();
		if (patcher == nullptr || _fragmentStageId == nullptr) {
			return nullptr;
		}

		for (const CachedFragmentProgram& cached : _fragmentPrograms) {
			if (cached.BlendKey == blendKey) {
				return cached.Program;
			}
		}

		SceGxmFragmentProgram* program = nullptr;
		const std::int32_t result = sceGxmShaderPatcherCreateFragmentProgram(patcher, _fragmentStageId,
			SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, blendInfo, _vertexStage, &program);
		if (result < 0) {
			LOGE("sceGxmShaderPatcherCreateFragmentProgram(blend 0x{:.8x}) failed with 0x{:.8x}", blendKey, std::uint32_t(result));
			return nullptr;
		}

		_fragmentPrograms.push_back({blendKey, program});
		return program;
	}
}
