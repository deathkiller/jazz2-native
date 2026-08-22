#include "GsShaderProgram.h"
#include "GsDevice.h"
#include "../../RenderResources.h"

#include <cstring>

#include <Asserts.h>

namespace nCine::RHI::GS
{
	std::uint32_t GsShaderProgram::_nextHandle = 1;

	GsShaderProgram::GsShaderProgram()
		: GsShaderProgram(QueryPhase::Immediate)
	{
	}

	GsShaderProgram::GsShaderProgram(QueryPhase queryPhase)
		: _handle(_nextHandle++), _status(Status::NotLinked), _introspection(Introspection::Disabled), _queryPhase(queryPhase),
			_batchSize(DefaultBatchSize), _shouldLogOnErrors(true), _uniformsSize(0), _uniformBlocksSize(0),
			_reflection(nullptr), _effectReflection(nullptr), _generatedEffect(nullptr), _ditherVariant(false), _usesPalette(false),
			_boundVbo(nullptr), _boundVboOffset(0), _boundIbo(nullptr)
	{
	}

	GsShaderProgram::GsShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: GsShaderProgram(queryPhase)
	{
		static_cast<void>(vertexFile);
		static_cast<void>(fragmentFile);
		static_cast<void>(introspection);
	}

	GsShaderProgram::GsShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: GsShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	GsShaderProgram::GsShaderProgram(StringView vertexFile, StringView fragmentFile)
		: GsShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	GsShaderProgram::~GsShaderProgram()
	{
		// The pipeline keys per-shader camera uniform data on the program pointer; drop this program's entry
		// so RenderResources::Dispose() finds the map empty (mirrors GLShaderProgram's destructor)
		RenderResources::RemoveCameraUniformData(this);
	}

	bool GsShaderProgram::IsLinked() const
	{
		return (_status == Status::Linked || _status == Status::LinkedWithDeferredQueries || _status == Status::LinkedWithIntrospection);
	}

	bool GsShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(filename);
		return true;
	}

	bool GsShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		static_cast<void>(stage);
		static_cast<void>(string);
		return true;
	}

	bool GsShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		return true;
	}

	bool GsShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		static_cast<void>(filename);
		return true;
	}

	bool GsShaderProgram::Link(Introspection introspection)
	{
		return FinalizeAfterLinking(introspection);
	}

	bool GsShaderProgram::FinalizeAfterLinking(Introspection introspection)
	{
		_introspection = introspection;
		PerformIntrospection();
		return true;
	}

	void GsShaderProgram::Use()
	{
		GsDevice::BindProgram(this);
	}

	void GsShaderProgram::PerformIntrospection()
	{
		if (_introspection != Introspection::Disabled && _status != Status::LinkedWithIntrospection) {
			_uniformsSize = 0;
			_uniformBlocksSize = 0;

			if (_reflection != nullptr) {
				_effectReflection = _reflection;
				ImportReflection();
				// A program samples indexed textures through the shared palette exactly when its
				// (variant-resolved) reflection binds the palette sampler - the "...Palette" programs and the
				// USE_PALETTE variants all declare it, everything else cannot possibly remap
				_usesPalette = false;
				for (std::size_t i = 0; i < _reflection->TextureCount; i++) {
					if (std::strcmp(_reflection->Textures[i].Name, "uTexturePalette") == 0) {
						_usesPalette = true;
						break;
					}
				}

				// Everything the dispatch asks of the reflection per draw is a constant of the program,
				// so it is all read out of the name strings exactly once, here (see DispatchFacts)
				_dispatchFacts = {};
				_dispatchFacts.InstanceBlock = FindBlock("InstanceBlock");
				if (_dispatchFacts.InstanceBlock == nullptr) {
					_dispatchFacts.InstanceBlock = FindBlock("InstancesBlock");
				}
				for (std::size_t i = 0; i < _reflection->TextureCount; i++) {
					const ShaderCompiler::TextureBinding& t = _reflection->Textures[i];
					if (std::strcmp(t.Name, "uTexture") == 0) {
						_dispatchFacts.HasTexture = true;
						if (t.Unit >= 0) {
							_dispatchFacts.TextureUnit = t.Unit;
						}
					} else if (std::strcmp(t.Name, "uTexturePalette") == 0 && t.Unit >= 0) {
						_dispatchFacts.PaletteUnit = t.Unit;
					}
				}
				// The instance layout follows the block's own reflected declaration rather than any effect
				// identity: a block that declares texRect uses the textured member offsets whether or not
				// the program samples a texture (the Transition carries texRect but samples nothing)
				_dispatchFacts.TexturedLayout = _dispatchFacts.HasTexture;
				for (std::size_t i = 0; i < _reflection->BlockCount; i++) {
					const ShaderCompiler::UniformBlock& b = _reflection->Blocks[i];
					if (_dispatchFacts.InstanceStride == 0 && b.InstanceStride > 0) {
						_dispatchFacts.InstanceStride = b.InstanceStride;
					}
					if (!_dispatchFacts.TexturedLayout) {
						for (std::size_t j = 0; j < b.MemberCount; j++) {
							if (std::strcmp(b.Members[j].Name, "texRect") == 0) {
								_dispatchFacts.TexturedLayout = true;
								break;
							}
						}
					}
				}
			}
			// Resolve the generated fixed-function effect of this (program, variant) once here, so the draw
			// path only reads a pointer. The key is the TRUE identity plumbed by the loaders via
			// SetProgramIdentity() - no shader name is ever matched; a program that never received an identity
			// (runtime-compiled .shader files) simply has no console effect and is skipped.
			_generatedEffect = (!_programName.empty()
				? GsDevice::FindGeneratedEffect(_programName.data(), _variantName.data()) : nullptr);
			_status = Status::LinkedWithIntrospection;
		}
		// The reflection is consumed by introspection (a copy of its layout is kept in _effectReflection)
		_reflection = nullptr;
	}

	void GsShaderProgram::ImportReflection()
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
			GsUniformBlock& block = _uniformBlocks.back();
			_uniformBlocksSize += block.GetSize();

			if (_introspection != Introspection::NoUniformsInBlocks) {
				block._members.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					GsUniform member;
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

	bool GsShaderProgram::HasAttribute(const char* name) const
	{
		for (const GsAttribute& a : _attributes) {
			if (std::strcmp(a.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	GsVertexFormat::Attribute* GsShaderProgram::GetAttribute(const char* name)
	{
		for (const GsAttribute& a : _attributes) {
			if (std::strcmp(a.GetName(), name) == 0 && a.GetLocation() >= 0) {
				return &_vertexFormat[std::uint32_t(a.GetLocation())];
			}
		}
		return nullptr;
	}

	void GsShaderProgram::DefineVertexFormat(const GsBuffer* vbo, const GsBuffer* ibo, std::uint32_t vboOffset)
	{
		_boundVbo = vbo;
		_boundVboOffset = vboOffset;
		_boundIbo = ibo;
		if (vbo != nullptr) {
			for (const GsAttribute& a : _attributes) {
				if (a.GetLocation() >= 0) {
					GsVertexFormat::Attribute& attr = _vertexFormat[std::uint32_t(a.GetLocation())];
					attr.setVbo(vbo);
					attr.SetBaseOffset(vboOffset);
				}
			}
			_vertexFormat.SetIbo(ibo);
		}
	}

	void GsShaderProgram::Reset()
	{
		_uniforms.clear();
		_uniformBlocks.clear();
		_attributes.clear();
		_resolvedUniforms.clear();
		_vertexFormat.Reset();
		_status = Status::NotLinked;
		_batchSize = DefaultBatchSize;
		_reflection = nullptr;
		_effectReflection = nullptr;
		// A reloaded program gets a fresh identity from its loader (SetProgramIdentity), so nothing stale can
		// leak into the effect resolution of the next PerformIntrospection()
		_programName = {};
		_variantName = {};
		_generatedEffect = nullptr;
		_ditherVariant = false;
		_usesPalette = false;
		// The cached facts point into _uniformBlocks, which was just cleared
		_dispatchFacts = {};
	}

	void GsShaderProgram::SetObjectLabel(StringView label)
	{
		// The label is kept for log messages only - effect identity comes from SetProgramIdentity()
		_label = label;
	}

	void GsShaderProgram::SetProgramIdentity(const char* programName, const char* variantName)
	{
		// The true (program, variant) identity the loader compiled this program from: the .shader program name
		// and the variant define (both baked into the generated tables). Stored as copies because
		// runtime-compiled programs hand in transient strings; the generated-table lookup itself runs in
		// PerformIntrospection(), after the reflection is set alongside.
		_programName = (programName != nullptr ? programName : "");
		_variantName = (variantName != nullptr ? variantName : "");
		// The dithering variant is a variant of the SAME program (TexturedBackground and its circle twin
		// declare "variant DITHER;"), so the flag is exactly the variant name
		_ditherVariant = (_variantName == "DITHER");
	}

	const GsUniformBlock* GsShaderProgram::FindBlock(const char* name) const
	{
		for (const GsUniformBlock& block : _uniformBlocks) {
			if (std::strcmp(block.GetName(), name) == 0) {
				return &block;
			}
		}
		return nullptr;
	}

	void GsShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		// The two names every dispatch reads get direct slots, so the draw path never scans by name
		if (std::strcmp(name, "uProjectionMatrix") == 0) {
			_resolvedProjection = data;
		} else if (std::strcmp(name, "uViewMatrix") == 0) {
			_resolvedView = data;
		}
		for (ResolvedUniform& r : _resolvedUniforms) {
			if (r.Name == name) {
				r.Data = data;
				return;
			}
		}
		_resolvedUniforms.push_back({name, data});
	}

	const std::uint8_t* GsShaderProgram::ResolveUniform(const char* name) const
	{
		for (const ResolvedUniform& r : _resolvedUniforms) {
			if (r.Name == name) {
				return r.Data;
			}
		}
		return nullptr;
	}
}
