#if defined(WITH_RHI_GX2)

#include "Gx2ShaderProgram.h"
#include "Gx2ShaderUniforms.h"
#include "Gx2BufferObject.h"
#include "Gx2Device.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"
#include "../../../../Shaders/Generated/Gx2GeneratedShaders.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <gx2/mem.h>
#include <gx2/utils.h>

namespace nCine::RHI::GX2
{
	namespace
	{
		/** @brief Copies a generated stage's program into GPU-visible memory and points @p program at it */
		bool UploadProgram(const GeneratedGx2Stage& stage, Gx2Memory::Block& block)
		{
			block = Gx2Memory::Alloc(stage.ProgramSize, GX2_SHADER_PROGRAM_ALIGNMENT);
			if (!block.IsValid()) {
				return false;
			}
			std::memcpy(block.Base, stage.Program, stage.ProgramSize);
			Gx2Memory::FlushCpuWrites(block.Base, stage.ProgramSize, GX2_INVALIDATE_MODE_CPU_SHADER);
			return true;
		}

		/** @brief Fills the parts the vertex and pixel shader structures share */
		template<typename T>
		void FillCommon(T& shader, const GeneratedGx2Stage& stage, void* program)
		{
			static_assert(sizeof(shader.regs) % 4 == 0);
			std::memcpy(&shader.regs, stage.Registers, std::min(sizeof(shader.regs), std::size_t(stage.RegisterCount) * 4));
			shader.size = stage.ProgramSize;
			shader.program = program;
			shader.mode = GX2_SHADER_MODE_UNIFORM_BLOCK;
			// The tables are read-only, GX2 only reads them through these non-const pointers
			shader.uniformBlockCount = stage.UniformBlockCount;
			shader.uniformBlocks = const_cast<GX2UniformBlock*>(stage.UniformBlocks);
			shader.uniformVarCount = stage.UniformVarCount;
			shader.uniformVars = const_cast<GX2UniformVar*>(stage.UniformVars);
			shader.loopVarCount = stage.LoopVarCount;
			shader.loopVars = const_cast<GX2LoopVar*>(stage.LoopVars);
			shader.samplerVarCount = stage.SamplerVarCount;
			shader.samplerVars = const_cast<GX2SamplerVar*>(stage.SamplerVars);
		}

		/** @brief Returns `true` for an attribute type the fetch shader has to hand over as integers */
		bool IsIntegerType(ShaderCompiler::UniformType type)
		{
			switch (type) {
				case ShaderCompiler::UniformType::Int:
				case ShaderCompiler::UniformType::IVec2:
				case ShaderCompiler::UniformType::IVec3:
				case ShaderCompiler::UniformType::IVec4:
				case ShaderCompiler::UniformType::UInt:
				case ShaderCompiler::UniformType::UVec2:
				case ShaderCompiler::UniformType::UVec3:
				case ShaderCompiler::UniformType::UVec4:
					return true;
				default:
					return false;
			}
		}
	}

	std::uint32_t Gx2ShaderProgram::_nextHandle = 1;

	Gx2ShaderProgram::Gx2ShaderProgram()
		: Gx2ShaderProgram(QueryPhase::Immediate)
	{
	}

	Gx2ShaderProgram::Gx2ShaderProgram(QueryPhase queryPhase)
		: _handle(_nextHandle++), _status(Status::NotLinked), _introspection(Introspection::Enabled),
			_queryPhase(queryPhase), _batchSize(std::uint32_t(DefaultBatchSize)), _shouldLogOnErrors(true),
			_reflection(nullptr), _programName(nullptr), _variantName(nullptr),
			_uniformsSize(0), _uniformBlocksSize(0), _hasBinaries(false)
	{
		std::memset(&_vertexShader, 0, sizeof(_vertexShader));
		std::memset(&_pixelShader, 0, sizeof(_pixelShader));
	}

	Gx2ShaderProgram::Gx2ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: Gx2ShaderProgram(queryPhase)
	{
		static_cast<void>(vertexFile);
		static_cast<void>(fragmentFile);
		_introspection = introspection;
	}

	Gx2ShaderProgram::Gx2ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: Gx2ShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	Gx2ShaderProgram::Gx2ShaderProgram(StringView vertexFile, StringView fragmentFile)
		: Gx2ShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	Gx2ShaderProgram::~Gx2ShaderProgram()
	{
		// The device tracks the bound program by pointer, so it has to forget this one before it goes
		Gx2Device::OnProgramDestroyed(this);
		ReleaseBinaries();
	}

	bool Gx2ShaderProgram::IsLinked() const
	{
		return (_status == Status::Linked || _status == Status::LinkedWithDeferredQueries ||
			_status == Status::LinkedWithIntrospection);
	}

	bool Gx2ShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		// Stage sources play no part on this backend: the binaries come from the generated table, keyed by
		// the program identity. Accepted so the shared loading code needs no Wii U arm.
		static_cast<void>(stage);
		static_cast<void>(filename);
		return true;
	}

	bool Gx2ShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		static_cast<void>(stage);
		static_cast<void>(string);
		return true;
	}

	bool Gx2ShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		return true;
	}

	bool Gx2ShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		static_cast<void>(filename);
		return true;
	}

	bool Gx2ShaderProgram::Link(Introspection introspection)
	{
		_introspection = introspection;

		if (_reflection == nullptr) {
			if (_shouldLogOnErrors) {
				LOGE("Cannot link a shader program without offline reflection on this backend");
			}
			_status = Status::CompilationFailed;
			return false;
		}

		if (!LoadBinaries()) {
			// Linked from the reflection alone, so the engine's uniform and batching plumbing still runs; the
			// device skips every draw with this program
			LOGW("Shader program \"{}\" ({}) has no GX2 shaders in the generated table, its draws are skipped",
				_programName != nullptr ? _programName : "<unnamed>", _variantName != nullptr ? _variantName : "");
		}

		if (!FinalizeAfterLinking(introspection)) {
			return false;
		}
		BuildBindings();
		return true;
	}

	bool Gx2ShaderProgram::LoadBinaries()
	{
		ReleaseBinaries();

		const GeneratedGx2Shader* generated = FindGeneratedGx2Shader(_programName, _variantName);
		if (generated == nullptr) {
			return false;
		}
		if (generated->BatchSize > 0) {
			// The instance array is compiled into the shader at this size
			_batchSize = std::uint32_t(generated->BatchSize);
		}

		if (!UploadProgram(generated->Vertex, _vertexProgram) || !UploadProgram(generated->Pixel, _pixelProgram)) {
			LOGE("Cannot allocate GPU memory for the shaders of \"{}\"", _programName);
			ReleaseBinaries();
			return false;
		}

		std::memset(&_vertexShader, 0, sizeof(_vertexShader));
		FillCommon(_vertexShader, generated->Vertex, _vertexProgram.Base);
		_vertexShader.attribVarCount = generated->Vertex.AttribVarCount;
		_vertexShader.attribVars = const_cast<GX2AttribVar*>(generated->Vertex.AttribVars);
		_vertexShader.ringItemsize = generated->Vertex.RingItemSize;

		std::memset(&_pixelShader, 0, sizeof(_pixelShader));
		FillCommon(_pixelShader, generated->Pixel, _pixelProgram.Base);

		_hasBinaries = true;
		return true;
	}

	void Gx2ShaderProgram::ReleaseBinaries()
	{
		// The device skips the shader binds while this program stays the last one used, so it has to forget
		// it before the binaries move; a relinked program would otherwise keep drawing with the old addresses
		Gx2Device::OnProgramBinariesReleased(this);
		_hasBinaries = false;
		Gx2Device::RetireBlock(_vertexProgram);
		Gx2Device::RetireBlock(_pixelProgram);
		for (FetchShaderEntry& entry : _fetchShaders) {
			Gx2Device::RetireBlock(entry.Program);
		}
		_fetchShaders.clear();
		_vertexBindings = StageBindings{};
		_pixelBindings = StageBindings{};
		_samplers.clear();
	}

	void Gx2ShaderProgram::BuildBindings()
	{
		_vertexBindings = StageBindings{};
		_pixelBindings = StageBindings{};
		_samplers.clear();
		if (!_hasBinaries) {
			return;
		}

		const auto buildStage = [this](StageBindings& bindings, const GX2UniformBlock* blocks, std::uint32_t blockCount,
			const GX2UniformVar* vars, std::uint32_t varCount) {
			std::int32_t looseIndex = -1;
			for (std::uint32_t i = 0; i < blockCount; i++) {
				const GX2UniformBlock& block = blocks[i];
				if (std::strcmp(block.name, "__cafe_loose_uniforms") == 0) {
					looseIndex = std::int32_t(i);
					bindings.LooseLocation = std::int32_t(block.offset);
					bindings.LooseSize = (block.size + 15u) & ~15u;
					continue;
				}
				// An explicit block is fed from the engine block of the same name, whose binding point is its
				// index in the program (Gx2ShaderUniformBlocks::Bind())
				bool found = false;
				for (std::uint32_t j = 0; j < _uniformBlocks.size(); j++) {
					if (std::strcmp(_uniformBlocks[j].GetName(), block.name) == 0) {
						bindings.Blocks.push_back({block.offset, j, block.size});
						found = true;
						break;
					}
				}
				if (!found) {
					LOGW("Shader \"{}\": uniform block \"{}\" is not in the reflection and stays unbound", _programName, block.name);
				}
			}

			for (std::uint32_t i = 0; i < varCount; i++) {
				const GX2UniformVar& var = vars[i];
				if (var.block != looseIndex) {
					continue;
				}
				const Gx2Uniform* uniform = nullptr;
				for (const Gx2Uniform& u : _uniforms) {
					if (std::strcmp(u.GetName(), var.name) == 0) {
						uniform = &u;
						break;
					}
				}
				if (uniform == nullptr) {
					LOGW("Shader \"{}\": uniform \"{}\" is not in the reflection and stays zero", _programName, var.name);
					continue;
				}
				LooseUniform loose;
				loose.Name = uniform->GetName();
				loose.Offset = var.offset * 4;	// GX2 counts uniform offsets in 32-bit components
				loose.Count = std::uint32_t(uniform->GetSize());
				loose.ElementSize = uniform->GetComponentCount() * 4;
				// Array elements each start on a register (16 bytes) boundary; a single value or a matrix is
				// packed the same way the engine packs it
				loose.Stride = (loose.Count > 1 ? std::max(loose.ElementSize, 16u) : loose.ElementSize);
				bindings.LooseUniforms.push_back(loose);
			}
		};

		buildStage(_vertexBindings, _vertexShader.uniformBlocks, _vertexShader.uniformBlockCount,
			_vertexShader.uniformVars, _vertexShader.uniformVarCount);
		buildStage(_pixelBindings, _pixelShader.uniformBlocks, _pixelShader.uniformBlockCount,
			_pixelShader.uniformVars, _pixelShader.uniformVarCount);

		// Samplers: the reflection assigns the engine unit, the compiled shader reports the hardware one
		if (_reflection != nullptr) {
			for (std::size_t i = 0; i < _reflection->TextureCount; i++) {
				const ShaderCompiler::TextureBinding& t = _reflection->Textures[i];
				const std::uint32_t engineUnit = std::uint32_t(t.Unit >= 0 ? t.Unit : std::int32_t(i));
				for (std::uint32_t j = 0; j < _pixelShader.samplerVarCount; j++) {
					if (std::strcmp(_pixelShader.samplerVars[j].name, t.Name) == 0) {
						_samplers.push_back({_pixelShader.samplerVars[j].location, engineUnit});
						break;
					}
				}
			}
		}
	}

	const GX2FetchShader* Gx2ShaderProgram::GetFetchShader()
	{
		if (!_hasBinaries) {
			return nullptr;
		}
		const std::uint64_t fingerprint = _vertexFormat.CalculateFingerprint();
		for (const FetchShaderEntry& entry : _fetchShaders) {
			if (entry.Fingerprint == fingerprint) {
				return &entry.Shader;
			}
		}

		GX2AttribStream streams[Gx2VertexFormat::MaxAttributes];
		std::uint32_t streamCount = 0;
		for (std::uint32_t i = 0; i < _vertexFormat.GetAttributeCount() && streamCount < Gx2VertexFormat::MaxAttributes; i++) {
			const Gx2VertexFormat::Attribute& attribute = _vertexFormat[i];
			if (!attribute.IsEnabled()) {
				continue;
			}
			// The engine numbers attributes by reflection location, the shader by its own semantic slots;
			// they meet by name
			const Gx2Attribute* engineAttribute = nullptr;
			for (const Gx2Attribute& a : _attributes) {
				if (std::uint32_t(a.GetLocation()) == attribute.GetIndex()) {
					engineAttribute = &a;
					break;
				}
			}
			if (engineAttribute == nullptr) {
				continue;
			}
			std::int32_t location = -1;
			for (std::uint32_t j = 0; j < _vertexShader.attribVarCount; j++) {
				if (std::strcmp(_vertexShader.attribVars[j].name, engineAttribute->GetName()) == 0) {
					location = std::int32_t(_vertexShader.attribVars[j].location);
					break;
				}
			}
			if (location < 0) {
				// Optimised out of the compiled shader
				continue;
			}

			const std::int32_t components = std::max(1, std::min(4, attribute.GetSize()));
			GX2AttribFormat format;
			std::uint32_t mask;
			if (attribute.GetType() == std::uint32_t(VertexAttribType::UnsignedByte)) {
				format = (attribute.IsNormalized() || !IsIntegerType(engineAttribute->GetType()))
					? GX2_ATTRIB_FORMAT_UNORM_8_8_8_8 : GX2_ATTRIB_FORMAT_UINT_8_8_8_8;
				mask = GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
			} else if (IsIntegerType(engineAttribute->GetType())) {
				// Only scalar integer attributes occur (the batched mesh index)
				format = GX2AttribFormat(GX2_ATTRIB_FLAG_INTEGER | GX2_ATTRIB_TYPE_32);
				mask = GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
			} else {
				static const GX2AttribFormat FloatFormats[] = { GX2_ATTRIB_FORMAT_FLOAT_32, GX2_ATTRIB_FORMAT_FLOAT_32_32,
					GX2_ATTRIB_FORMAT_FLOAT_32_32_32, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32 };
				static const std::uint32_t FloatMasks[] = {
					GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_0, GX2_SQ_SEL_0, GX2_SQ_SEL_1),
					GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1),
					GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_1),
					GX2_COMP_MAP(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W) };
				format = FloatFormats[components - 1];
				mask = FloatMasks[components - 1];
			}

			GX2AttribStream& stream = streams[streamCount++];
			stream.location = std::uint32_t(location);
			// One attribute buffer per engine attribute slot, all starting at the vertex; the attribute's
			// offset within the vertex goes into the stream
			stream.buffer = attribute.GetIndex();
			stream.offset = std::uint32_t(reinterpret_cast<std::uintptr_t>(attribute.GetPointer()));
			stream.format = format;
			stream.type = GX2_ATTRIB_INDEX_PER_VERTEX;
			stream.aluDivisor = 0;
			stream.mask = mask;
			stream.endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
		}

		FetchShaderEntry entry;
		entry.Fingerprint = fingerprint;
		std::memset(&entry.Shader, 0, sizeof(entry.Shader));
		const std::uint32_t size = GX2CalcFetchShaderSizeEx(streamCount, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
		entry.Program = Gx2Memory::Alloc(size, GX2_SHADER_PROGRAM_ALIGNMENT);
		if (!entry.Program.IsValid()) {
			return nullptr;
		}
		GX2InitFetchShaderEx(&entry.Shader, static_cast<std::uint8_t*>(entry.Program.Base), streamCount, streams,
			GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
		Gx2Memory::FlushCpuWrites(entry.Program.Base, size, GX2_INVALIDATE_MODE_CPU_SHADER);
		_fetchShaders.push_back(entry);
		return &_fetchShaders.back().Shader;
	}

	bool Gx2ShaderProgram::FinalizeAfterLinking(Introspection introspection)
	{
		_introspection = introspection;
		_status = Status::Linked;

		if (_introspection == Introspection::Disabled || _reflection == nullptr) {
			return true;
		}

		_uniforms.clear();
		_uniformBlocks.clear();
		_attributes.clear();
		_uniformsSize = 0;
		_uniformBlocksSize = 0;
		_resolvedUniforms.clear();

		ImportReflection(_introspection);

		_status = Status::LinkedWithIntrospection;
		return true;
	}

	void Gx2ShaderProgram::ImportReflection(Introspection introspection)
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

			// A BATCH_SIZE-sized instance array uses the explicitly set batch size, or as many elements as fit
			// the 64 KB a GX2 uniform block may hold - the same figure the generated shaders bake in
			std::uint32_t effectiveBatchSize = 0;
			std::uint32_t dataSize = b.BaseSize;
			if (b.InstanceStride > 0) {
				effectiveBatchSize = (_batchSize != std::uint32_t(DefaultBatchSize) && _batchSize > 0)
					? _batchSize : (std::uint32_t(Gx2Device::GetMaxUniformBlockSize()) - b.BaseSize) / b.InstanceStride;
				if (effectiveBatchSize > Gx2Device::MaxBatchSize) {
					LOGE("Uniform block \"{}\" wants a batch of {} but the backend caps it at {} - the "
						"IntValues::MaxBatchSize capability was not applied, and the batcher will overrun the instance array",
						b.Name, effectiveBatchSize, Gx2Device::MaxBatchSize);
					effectiveBatchSize = Gx2Device::MaxBatchSize;
				}
				dataSize += b.InstanceStride * effectiveBatchSize;
			}

			const std::uint32_t blockIndex = std::uint32_t(_uniformBlocks.size());
			_uniformBlocks.emplace_back(blockIndex, b.Name, std::int32_t(dataSize));
			Gx2UniformBlock& block = _uniformBlocks.back();
			_uniformBlocksSize += block.GetSize();

			if (introspection != Introspection::NoUniformsInBlocks) {
				block._members.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					Gx2Uniform member;
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
			_vertexFormat[std::uint32_t(location)].Init(std::uint32_t(location),
				std::int32_t(UniformTypeInfo::ComponentCount(a.Type)), 0);
		}
	}

	void Gx2ShaderProgram::Use()
	{
		Gx2Device::BindProgram(this);
	}

	bool Gx2ShaderProgram::HasAttribute(const char* name) const
	{
		for (const Gx2Attribute& attribute : _attributes) {
			if (std::strcmp(attribute.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	bool Gx2ShaderProgram::IsIntegerAttribute(const char* name) const
	{
		for (const Gx2Attribute& attribute : _attributes) {
			if (std::strcmp(attribute.GetName(), name) != 0) {
				continue;
			}
			switch (attribute.GetType()) {
				case ShaderCompiler::UniformType::Int:
				case ShaderCompiler::UniformType::IVec2:
				case ShaderCompiler::UniformType::IVec3:
				case ShaderCompiler::UniformType::IVec4:
				case ShaderCompiler::UniformType::UInt:
				case ShaderCompiler::UniformType::UVec2:
				case ShaderCompiler::UniformType::UVec3:
				case ShaderCompiler::UniformType::UVec4:
					return true;
				default:
					return false;
			}
		}
		return false;
	}

	Gx2VertexFormat::Attribute* Gx2ShaderProgram::GetAttribute(const char* name)
	{
		for (const Gx2Attribute& attribute : _attributes) {
			if (std::strcmp(attribute.GetName(), name) == 0) {
				return &_vertexFormat[std::uint32_t(attribute.GetLocation())];
			}
		}
		return nullptr;
	}

	void Gx2ShaderProgram::DefineVertexFormat(const Gx2BufferObject* vbo, const Gx2BufferObject* ibo, std::uint32_t vboOffset)
	{
		for (std::uint32_t i = 0; i < _vertexFormat.GetAttributeCount(); i++) {
			Gx2VertexFormat::Attribute& attribute = _vertexFormat[i];
			if (attribute.IsEnabled()) {
				attribute.SetVbo(vbo);
				attribute.SetBaseOffset(vboOffset);
			}
		}
		_vertexFormat.SetIbo(ibo);
	}

	void Gx2ShaderProgram::Reset()
	{
		_uniforms.clear();
		_uniformBlocks.clear();
		_attributes.clear();
		_uniformsSize = 0;
		_uniformBlocksSize = 0;
		_resolvedUniforms.clear();
		_vertexFormat.Reset();
		ReleaseBinaries();
		_status = Status::NotLinked;
	}

	void Gx2ShaderProgram::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}

	void Gx2ShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		for (auto& entry : _resolvedUniforms) {
			if (std::strcmp(entry.first, name) == 0) {
				entry.second = data;
				return;
			}
		}
		_resolvedUniforms.emplace_back(name, data);
	}

	const std::uint8_t* Gx2ShaderProgram::ResolveUniform(const char* name) const
	{
		for (const auto& entry : _resolvedUniforms) {
			if (std::strcmp(entry.first, name) == 0) {
				return entry.second;
			}
		}
		return nullptr;
	}
}

#endif
