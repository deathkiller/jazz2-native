#if defined(WITH_RHI_RSX)

#include "RsxShaderProgram.h"
#include "RsxShaderUniforms.h"
#include "RsxBufferObject.h"
#include "RsxDevice.h"

#include "../../../../Main.h"
#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"
#include "../../../../Shaders/Generated/RsxGeneratedShaders.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <rsx/rsx.h>
#include <rsx/commands.h>

namespace nCine::RHI::RSX
{
	std::uint32_t RsxShaderProgram::_nextHandle = 1;

	RsxShaderProgram::RsxShaderProgram()
		: RsxShaderProgram(QueryPhase::Immediate)
	{
	}

	RsxShaderProgram::RsxShaderProgram(QueryPhase queryPhase)
		: _handle(_nextHandle++), _status(Status::NotLinked), _introspection(Introspection::Enabled),
			_queryPhase(queryPhase), _batchSize(std::uint32_t(DefaultBatchSize)), _shouldLogOnErrors(true),
			_reflection(nullptr), _programName(nullptr), _variantName(nullptr),
			_vertexProgram(nullptr), _fragmentProgram(nullptr), _vertexUcode(nullptr),
			_uniformsSize(0), _uniformBlocksSize(0)
	{
	}

	RsxShaderProgram::RsxShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: RsxShaderProgram(queryPhase)
	{
		static_cast<void>(vertexFile);
		static_cast<void>(fragmentFile);
		_introspection = introspection;
	}

	RsxShaderProgram::RsxShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: RsxShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	RsxShaderProgram::RsxShaderProgram(StringView vertexFile, StringView fragmentFile)
		: RsxShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	RsxShaderProgram::~RsxShaderProgram()
	{
		// The device tracks the bound program by pointer, so it has to forget this one before it goes
		RsxDevice::OnProgramDestroyed(this);
		if (_fragmentUcode.IsValid()) {
			RsxDevice::RetireBlock(_fragmentUcode);
		}
	}

	bool RsxShaderProgram::IsLinked() const
	{
		return (_status == Status::Linked || _status == Status::LinkedWithDeferredQueries ||
			_status == Status::LinkedWithIntrospection);
	}

	bool RsxShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		// Stage sources play no part on this backend: the microcode comes from the generated table, keyed by
		// the program identity. Accepted so the shared loading code needs no PS3 arm.
		static_cast<void>(stage);
		static_cast<void>(filename);
		return true;
	}

	bool RsxShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		static_cast<void>(stage);
		static_cast<void>(string);
		return true;
	}

	bool RsxShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		return true;
	}

	bool RsxShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		static_cast<void>(filename);
		return true;
	}

	bool RsxShaderProgram::ResolveGeneratedProgram()
	{
		const char* programName = (_programName != nullptr ? _programName : "");
		const char* variantName = (_variantName != nullptr ? _variantName : "");

		const GeneratedRsxShader* generated = FindGeneratedRsxShader(programName, variantName);
		if (generated == nullptr) {
			// There is no runtime fallback to reach for: the console has no shader compiler, so a variant
			// missing from the table is one whose Cg the vp40/fp40 profiles could not express when the table
			// was generated. That is a build-time fact, so it is reported plainly and once.
			if (_shouldLogOnErrors) {
				LOGE("Shader \"{}\"{}{}{} has no compiled RSX microcode; it was not accepted by the vp40/fp40 "
					"profiles when the shader table was generated", programName,
					variantName[0] != '\0' ? " (variant \"" : "", variantName, variantName[0] != '\0' ? "\")" : "");
			}
			return false;
		}

		_vertexProgram = reinterpret_cast<const rsxVertexProgram*>(generated->VertexProgram);
		_fragmentProgram = reinterpret_cast<const rsxFragmentProgram*>(generated->FragmentProgram);

		// The vertex microcode is fetched by the command processor out of wherever it is given, so the copy
		// in the executable serves directly; the fragment half is not so lucky (see UploadFragmentUcode())
		std::uint32_t ucodeSize = 0;
		void* ucode = nullptr;
		rsxVertexProgramGetUCode(_vertexProgram, &ucode, &ucodeSize);
		_vertexUcode = ucode;
		return (_vertexUcode != nullptr);
	}

	bool RsxShaderProgram::UploadFragmentUcode()
	{
		std::uint32_t ucodeSize = 0;
		void* ucode = nullptr;
		rsxFragmentProgramGetUCode(_fragmentProgram, &ucode, &ucodeSize);
		if (ucode == nullptr || ucodeSize == 0) {
			return false;
		}

		// Local memory is mandatory - the fragment engine cannot fetch microcode from main memory - and the
		// copy is per-program rather than shared because the RSX patches fragment constants into the
		// microcode itself, so two programs over one blob would overwrite each other's uniforms
		_fragmentUcode = RsxVram::AllocFragmentProgram(ucodeSize);
		if (!_fragmentUcode.IsValid()) {
			if (_shouldLogOnErrors) {
				LOGE("Cannot allocate {} bytes of local memory for the fragment microcode of shader \"{}\"",
					ucodeSize, _programName != nullptr ? _programName : "<unnamed>");
			}
			return false;
		}
		std::memcpy(_fragmentUcode.Base, ucode, ucodeSize);
		return true;
	}

	bool RsxShaderProgram::Link(Introspection introspection)
	{
		_introspection = introspection;

		if (_reflection == nullptr) {
			if (_shouldLogOnErrors) {
				LOGE("Cannot link a shader program without offline reflection on this backend");
			}
			_status = Status::CompilationFailed;
			return false;
		}

		if (!ResolveGeneratedProgram() || !UploadFragmentUcode()) {
			_status = Status::CompilationFailed;
			return false;
		}

		return FinalizeAfterLinking(introspection);
	}

	bool RsxShaderProgram::FinalizeAfterLinking(Introspection introspection)
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
		_vertexUniformSlots.clear();
		_fragmentUniformSlots.clear();
		_vertexBlockUploads.clear();
		_fragmentBlockUploads.clear();
		_fragmentSamplers.clear();
		_stageAttributes.clear();
		_resolvedUniforms.clear();
		_instanceArray = RsxInstanceArray{};

		ImportReflection(_introspection);
		ResolveStageInterface();
		ResolveConstants();

		_status = Status::LinkedWithIntrospection;
		return true;
	}

	void RsxShaderProgram::ImportReflection(Introspection introspection)
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

			// A BATCH_SIZE-sized instance array uses the explicitly set batch size, or the same fallback the
			// generated microcode baked in - which on this backend is the RSX's constant-register budget
			// rather than the 64 KB the desktop backends assume (see RsxRhiCapabilities)
			std::uint32_t effectiveBatchSize = 0;
			std::uint32_t dataSize = b.BaseSize;
			if (b.InstanceStride > 0) {
				effectiveBatchSize = (_batchSize != std::uint32_t(DefaultBatchSize) && _batchSize > 0)
					? _batchSize : std::uint32_t(RsxDevice::GetMaxUniformBlockSize()) / b.InstanceStride;
				// Whatever the constant budget would allow, the batch cannot exceed what the device's
				// batched corner stream covers - that stream is what supplies the instance index, so a
				// larger batch would read past it. The generated microcode bakes in the same bound.
				if (effectiveBatchSize > RsxDevice::MaxBatchSize) {
					// Clamping here only bounds what this program advertises; it cannot bound what the engine
					// collects, which comes from AppConfiguration::fixedBatchSize. If the two disagree the
					// batcher writes past the end of the instance array it was given, so say so loudly rather
					// than clamping in silence - that silence is what hid exactly this mismatch before.
					LOGE("Uniform block \"{}\" wants a batch of {} but the backend caps it at {} - set "
						"AppConfiguration::fixedBatchSize to match, or the batcher will overrun the instance array",
						b.Name, effectiveBatchSize, RsxDevice::MaxBatchSize);
					effectiveBatchSize = RsxDevice::MaxBatchSize;
				}
				dataSize += b.InstanceStride * effectiveBatchSize;
			}

			const std::uint32_t blockIndex = std::uint32_t(_uniformBlocks.size());
			_uniformBlocks.emplace_back(blockIndex, b.Name, std::int32_t(dataSize));
			RsxUniformBlock& block = _uniformBlocks.back();
			_uniformBlocksSize += block.GetSize();

			if (introspection != Introspection::NoUniformsInBlocks) {
				block._members.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					RsxUniform member;
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

	void RsxShaderProgram::ResolveStageInterface()
	{
		if (_vertexProgram == nullptr) {
			return;
		}

		// The compiled stage's own attribute table, not the reflection's - see RsxStageAttribute for why the
		// two disagree for every sprite shader
		const std::uint16_t attribCount = rsxVertexProgramGetNumAttrib(_vertexProgram);
		const rsxProgramAttrib* attribs = rsxVertexProgramGetAttribs(_vertexProgram);
		const char* base = reinterpret_cast<const char*>(_vertexProgram);
		for (std::uint16_t i = 0; i < attribCount; i++) {
			const char* name = base + attribs[i].name_off;
			// An attribute the entry point receives through an input struct is named "<struct>.<member>";
			// the engine knows it by the member alone
			if (const char* lastDot = std::strrchr(name, '.')) {
				name = lastDot + 1;
			}
			_stageAttributes.push_back({name, std::uint8_t(attribs[i].index)});
		}

		if (_fragmentProgram == nullptr || _reflection == nullptr) {
			return;
		}

		// Samplers: the reflection assigns the engine unit, the microcode reports the hardware one. They
		// normally agree (the Cg emitter tags each sampler with the reflection's TEXUNIT<n>), but resolving
		// both means a mismatch shows up as a warning rather than as a shader sampling the wrong texture.
		for (std::size_t i = 0; i < _reflection->TextureCount; i++) {
			const ShaderCompiler::TextureBinding& t = _reflection->Textures[i];
			const std::int32_t engineUnit = (t.Unit >= 0 ? t.Unit : std::int32_t(i));
			// Through GetConst() rather than the GetConstIndex() rsx_program.h also declares: librsx declares
			// that one but implements no such symbol, so calling it does not link
			const rsxProgramConst* hardwareConst = rsxFragmentProgramGetConst(_fragmentProgram, t.Name);
			const std::int32_t hardwareIndex = (hardwareConst != nullptr ? std::int32_t(hardwareConst->index) : -1);
			const std::uint32_t hardwareUnit = (hardwareIndex >= 0 ? std::uint32_t(hardwareIndex) : std::uint32_t(engineUnit));
			if (hardwareIndex >= 0 && hardwareUnit != std::uint32_t(engineUnit)) {
				LOGW("Shader \"{}\" samples \"{}\" from texture unit {} but the pipeline binds it to {}",
					_programName != nullptr ? _programName : "<unnamed>", t.Name, hardwareUnit, engineUnit);
			}
			_fragmentSamplers.push_back({t.Name, std::uint32_t(engineUnit), hardwareUnit});
		}
	}

	void RsxShaderProgram::ResolveConstants()
	{
		if (_reflection == nullptr) {
			return;
		}

		// Loose uniforms. A name that resolves in neither stage is not an error - a shader is free to ignore
		// a uniform the engine offers - so it is simply left out of both slot lists and never uploaded.
		for (std::size_t i = 0; i < _reflection->UniformCount; i++) {
			const ShaderCompiler::Uniform& u = _reflection->Uniforms[i];
			const std::uint32_t byteSize = UniformTypeInfo::ComponentCount(u.Type) * 4u *
				(u.ArraySize > 0 ? u.ArraySize : 1u);

			if (_vertexProgram != nullptr) {
				if (const rsxProgramConst* param = rsxVertexProgramGetConst(_vertexProgram, u.Name)) {
					_vertexUniformSlots.push_back({u.Name, param, byteSize});
				}
			}
			if (_fragmentProgram != nullptr) {
				if (const rsxProgramConst* param = rsxFragmentProgramGetConst(_fragmentProgram, u.Name)) {
					_fragmentUniformSlots.push_back({u.Name, param, byteSize});
				}
			}
		}

		// Uniform block members, hoisted to top-level constants by the Cg emitter
		for (std::size_t blockIndex = 0; blockIndex < _reflection->BlockCount; blockIndex++) {
			const ShaderCompiler::UniformBlock& b = _reflection->Blocks[blockIndex];
			bool reached = false;

			for (std::size_t j = 0; j < b.MemberCount; j++) {
				const ShaderCompiler::BlockMember& m = b.Members[j];
				if (m.Type == ShaderCompiler::UniformType::Struct) {
					// The batched instance array: the reflection describes it as one opaque struct, so its
					// field layout is recovered from the compiled microcode instead (see ResolveInstanceArray)
					if (m.ArraySize == ShaderCompiler::SymbolicArraySize && b.InstanceStride > 0) {
						std::uint32_t elements = (_batchSize != std::uint32_t(DefaultBatchSize) && _batchSize > 0
							? _batchSize : std::uint32_t(RsxDevice::GetMaxUniformBlockSize()) / b.InstanceStride);
						if (elements > RsxDevice::MaxBatchSize) {
							elements = RsxDevice::MaxBatchSize;
						}
						ResolveInstanceArray(std::uint32_t(blockIndex), m.Name, b.InstanceStride, elements);
						reached |= _instanceArray.Valid;
					}
					continue;
				}

				const bool batched = (m.ArraySize == ShaderCompiler::SymbolicArraySize);
				static_cast<void>(batched);
				const std::uint32_t elementCount = (m.ArraySize > 0 ? m.ArraySize : 1u);
				const std::uint32_t componentBytes = UniformTypeInfo::ComponentCount(m.Type) * 4u;

				RsxBlockUpload upload;
				upload.Param = nullptr;
				upload.BindingIndex = std::uint32_t(blockIndex);
				upload.SourceOffset = m.Offset;
				upload.MaxByteSize = componentBytes * elementCount;
				upload.Name = m.Name;

				if (_vertexProgram != nullptr) {
					if (const rsxProgramConst* param = rsxVertexProgramGetConst(_vertexProgram, m.Name)) {
						upload.Param = param;
						_vertexBlockUploads.push_back(upload);
						reached = true;
					}
				}
				if (_fragmentProgram != nullptr) {
					if (const rsxProgramConst* param = rsxFragmentProgramGetConst(_fragmentProgram, m.Name)) {
						upload.Param = param;
						_fragmentBlockUploads.push_back(upload);
						reached = true;
					}
				}
			}

			// A uniform block that resolved in neither stage means every draw of this program reads
			// undefined instance data - the failure mode that looks like garbled sprites rather than an error
			if (!reached) {
				LOGW("Shader \"{}\" uniform block \"{}\" reaches neither stage; its draws will read undefined uniforms",
					_programName != nullptr ? _programName : "<unnamed>", b.Name);
			}
		}
	}

	void RsxShaderProgram::ResolveInstanceArray(std::uint32_t blockIndex, const char* memberName,
		std::uint32_t sourceStride, std::uint32_t elementCount)
	{
		_instanceArray = RsxInstanceArray{};
		if (_vertexProgram == nullptr || memberName == nullptr || sourceStride == 0 || elementCount == 0) {
			return;
		}

		// cgcomp names each element's fields individually - "instances[0].modelMatrix", "instances[1]..." -
		// so element 0's entries are what describe the layout, and the rest only confirm the element stride
		char prefix[RsxUniform::MaxNameLength];
		const std::int32_t prefixLength = std::snprintf(prefix, sizeof(prefix), "%s[0].", memberName);
		if (prefixLength <= 0 || std::size_t(prefixLength) >= sizeof(prefix)) {
			return;
		}

		const std::uint16_t constCount = rsxVertexProgramGetNumConst(_vertexProgram);
		const rsxProgramConst* consts = rsxVertexProgramGetConsts(_vertexProgram);
		const char* base = reinterpret_cast<const char*>(_vertexProgram);

		// One entry per REGISTER is emitted for a multi-register field (a mat4 appears four times under the
		// same name), so the lowest index per distinct name is the field's base and the count is its width
		struct FieldScratch { const char* Name; std::uint32_t FirstRegister; std::uint32_t RegisterCount; std::uint8_t Type; };
		SmallVector<FieldScratch, 8> fields;
		std::uint32_t elementBase = 0xFFFFFFFFu;

		for (std::uint16_t i = 0; i < constCount; i++) {
			const char* name = base + consts[i].name_off;
			if (std::strncmp(name, prefix, std::size_t(prefixLength)) != 0) {
				continue;
			}
			const char* fieldName = name + prefixLength;
			const std::uint32_t reg = consts[i].index;
			if (reg < elementBase) {
				elementBase = reg;
			}

			bool found = false;
			for (FieldScratch& f : fields) {
				if (std::strcmp(f.Name, fieldName) == 0) {
					if (reg < f.FirstRegister) {
						f.FirstRegister = reg;
					}
					f.RegisterCount++;
					found = true;
					break;
				}
			}
			if (!found) {
				fields.push_back({fieldName, reg, 1, consts[i].type});
			}
		}

		if (fields.empty() || elementBase == 0xFFFFFFFFu) {
			LOGW("Shader \"{}\" declares a batched instance array, but its microcode has no \"{}[0].*\" constants",
				_programName != nullptr ? _programName : "<unnamed>", memberName);
			return;
		}

		// Sort by register so the std140 walk below follows the shader's own field order
		std::sort(fields.begin(), fields.end(), [](const FieldScratch& a, const FieldScratch& b) {
			return a.FirstRegister < b.FirstRegister;
		});

		// The engine's side is std140, whose packing the offline reflection does not spell out for a struct.
		// It is recovered here from each field's register width, which is what distinguishes the cases that
		// matter: a mat4 (four registers, four aligned vec4s) from a vec4, and both from the scalars std140
		// packs together but the register file does not.
		// The width has to come from the field's declared TYPE, not from how many registers it occupies:
		// the register file gives a vec2 and a trailing float one register each, while std140 aligns the
		// vec2 to 8 and packs the float into the same 16-byte slot behind it. Deriving the offsets from
		// registers would put every field after the first scalar 8 bytes late, for the whole batch.
		std::uint32_t sourceOffset = 0;
		for (const FieldScratch& f : fields) {
			std::uint32_t alignment = 16, size = 16, copyBytes = 16;
			switch (f.Type) {
				case PARAM_FLOAT:  alignment = 4;  size = 4;  copyBytes = 4;  break;
				case PARAM_FLOAT1: alignment = 4;  size = 4;  copyBytes = 4;  break;
				case PARAM_FLOAT2: alignment = 8;  size = 8;  copyBytes = 8;  break;
				case PARAM_FLOAT3: alignment = 16; size = 12; copyBytes = 12; break;
				case PARAM_FLOAT4: alignment = 16; size = 16; copyBytes = 16; break;
				case PARAM_FLOAT3x3: alignment = 16; size = 48; copyBytes = 48; break;
				case PARAM_FLOAT4x4: alignment = 16; size = 64; copyBytes = 64; break;
				default:
					// Fall back to the register width, which is right for everything vec4-shaped
					alignment = 16; size = f.RegisterCount * 16; copyBytes = size;
					break;
			}
			sourceOffset = (sourceOffset + alignment - 1) & ~(alignment - 1);
			_instanceArray.Fields.push_back({sourceOffset, f.FirstRegister - elementBase, f.RegisterCount, copyBytes, f.Name});
			sourceOffset += size;
		}
		// std140 rounds the whole element up to its largest member alignment, which is always 16 here
		sourceOffset = (sourceOffset + 15u) & ~15u;

		// Element stride from two consecutive elements, which is authoritative; the field walk above only
		// has to agree with the engine's std140 stride, and a disagreement means the two sides would drift
		// element by element - so it is reported rather than silently mis-rendered
		std::uint32_t registersPerElement = 0;
		{
			char nextPrefix[RsxUniform::MaxNameLength];
			const std::int32_t nextLength = std::snprintf(nextPrefix, sizeof(nextPrefix), "%s[1].", memberName);
			if (nextLength > 0 && std::size_t(nextLength) < sizeof(nextPrefix)) {
				std::uint32_t nextBase = 0xFFFFFFFFu;
				for (std::uint16_t i = 0; i < constCount; i++) {
					const char* name = base + consts[i].name_off;
					if (std::strncmp(name, nextPrefix, std::size_t(nextLength)) == 0 && consts[i].index < nextBase) {
						nextBase = consts[i].index;
					}
				}
				if (nextBase != 0xFFFFFFFFu && nextBase > elementBase) {
					registersPerElement = nextBase - elementBase;
				}
			}
		}
		if (registersPerElement == 0) {
			registersPerElement = sourceOffset / 16;
		}

		if (sourceOffset != sourceStride) {
			LOGW("Shader \"{}\" batched element is {} bytes by the microcode's field layout but {} by the "
				"engine's; instance data past the first element will be misread",
				_programName != nullptr ? _programName : "<unnamed>", sourceOffset, sourceStride);
		}

		_instanceArray.Valid = true;
		_instanceArray.BindingIndex = blockIndex;
		_instanceArray.BaseRegister = elementBase;
		_instanceArray.RegistersPerElement = registersPerElement;
		_instanceArray.SourceStride = sourceStride;
		_instanceArray.ElementCount = elementCount;

		LOGD("Shader \"{}\" batched instance array: {} fields, {} registers per element, {} elements",
			_programName != nullptr ? _programName : "<unnamed>", _instanceArray.Fields.size(),
			registersPerElement, elementCount);
	}

	void RsxShaderProgram::Use()
	{
		RsxDevice::BindProgram(this);
	}

	bool RsxShaderProgram::HasAttribute(const char* name) const
	{
		for (const RsxAttribute& attribute : _attributes) {
			if (std::strcmp(attribute.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	bool RsxShaderProgram::IsIntegerAttribute(const char* name) const
	{
		for (const RsxAttribute& attribute : _attributes) {
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

	RsxVertexFormat::Attribute* RsxShaderProgram::GetAttribute(const char* name)
	{
		for (const RsxAttribute& attribute : _attributes) {
			if (std::strcmp(attribute.GetName(), name) == 0) {
				return &_vertexFormat[std::uint32_t(attribute.GetLocation())];
			}
		}
		return nullptr;
	}

	std::int32_t RsxShaderProgram::GetStageAttributeRegister(const char* name) const
	{
		for (const RsxStageAttribute& attribute : _stageAttributes) {
			if (std::strcmp(attribute.Name, name) == 0) {
				return std::int32_t(attribute.RegIndex);
			}
		}
		return -1;
	}

	void RsxShaderProgram::DefineVertexFormat(const RsxBufferObject* vbo, const RsxBufferObject* ibo, std::uint32_t vboOffset)
	{
		for (std::uint32_t i = 0; i < _vertexFormat.GetAttributeCount(); i++) {
			RsxVertexFormat::Attribute& attribute = _vertexFormat[i];
			if (attribute.IsEnabled()) {
				attribute.setVbo(vbo);
				attribute.SetBaseOffset(vboOffset);
			}
		}
		_vertexFormat.SetIbo(ibo);
	}

	void RsxShaderProgram::Reset()
	{
		_uniforms.clear();
		_uniformBlocks.clear();
		_attributes.clear();
		_uniformsSize = 0;
		_uniformBlocksSize = 0;
		_vertexUniformSlots.clear();
		_fragmentUniformSlots.clear();
		_vertexBlockUploads.clear();
		_fragmentBlockUploads.clear();
		_fragmentSamplers.clear();
		_stageAttributes.clear();
		_resolvedUniforms.clear();
		_vertexFormat.Reset();

		if (_fragmentUcode.IsValid()) {
			RsxDevice::RetireBlock(_fragmentUcode);
		}
		_vertexProgram = nullptr;
		_fragmentProgram = nullptr;
		_vertexUcode = nullptr;
		_status = Status::NotLinked;
	}

	void RsxShaderProgram::SetObjectLabel(StringView label)
	{
		static_cast<void>(label);
	}

	void RsxShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		for (auto& entry : _resolvedUniforms) {
			if (std::strcmp(entry.first, name) == 0) {
				entry.second = data;
				return;
			}
		}
		_resolvedUniforms.emplace_back(name, data);
	}

	const std::uint8_t* RsxShaderProgram::ResolveUniform(const char* name) const
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
