#include "D3D11ShaderProgram.h"
#include "D3D11Device.h"
#include "D3D11BufferObject.h"
#include "../../RenderResources.h"
#include "../../../Application.h"
#include "../../../Base/HashFunctions.h"

#include "../../../../Shaders/Generated/ShaderCompilerTypes.h"

#include <cstring>
#include <memory>

#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>

#include <Asserts.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace nCine::RhiD3D11
{
	namespace
	{
		template<class T>
		void SafeRelease(T*& p)
		{
			if (p != nullptr) {
				p->Release();
				p = nullptr;
			}
		}

		// On-disk DXBC blob cache (gated to the D3D11 backend). Compiling ~120 HLSL stages with D3DCompile at
		// startup takes many seconds; caching the compiled bytecode keyed by a hash of the HLSL source means a
		// warm run only recompiles shaders whose source actually changed. Format: magic, version, VS size, PS
		// size, then the two raw DXBC blobs. Stored under <shaderCachePath>/D3D11.
		constexpr std::uint32_t kBlobCacheMagic = 0x31314458u;	// 'XD11'
		constexpr std::uint32_t kBlobCacheVersion = 1u;

		String BlobCachePath(const char* vsSource, const char* fsSource)
		{
			const String& base = theApplication().GetAppConfiguration().shaderCachePath;
			if (base.empty()) {
				return {};
			}
			const std::uint64_t h = xxHash3(fsSource, std::strlen(fsSource), xxHash3(vsSource, std::strlen(vsSource)));
			char name[24];
			for (std::int32_t i = 0; i < 16; i++) {
				name[i] = "0123456789abcdef"[(h >> ((15 - i) * 4)) & 0xFu];
			}
			name[16] = '.'; name[17] = 'd'; name[18] = 'x'; name[19] = 'b'; name[20] = 'c';
			return fs::CombinePath(fs::CombinePath(base, "D3D11"_s), StringView(name, 21));
		}

		bool LoadBlobCache(const String& path, std::vector<std::uint8_t>& vs, std::vector<std::uint8_t>& ps)
		{
			std::unique_ptr<Stream> s = fs::Open(path, FileAccess::Read);
			if (s == nullptr || !s->IsValid() || s->GetSize() < 16) {
				return false;
			}
			if (s->ReadValueAsLE<std::uint32_t>() != kBlobCacheMagic || s->ReadValueAsLE<std::uint32_t>() != kBlobCacheVersion) {
				return false;
			}
			const std::uint32_t vsSize = s->ReadValueAsLE<std::uint32_t>();
			const std::uint32_t psSize = s->ReadValueAsLE<std::uint32_t>();
			if (vsSize == 0 || psSize == 0 || vsSize > 8u * 1024 * 1024 || psSize > 8u * 1024 * 1024) {
				return false;
			}
			vs.resize(vsSize);
			ps.resize(psSize);
			return (s->Read(vs.data(), std::int64_t(vsSize)) == std::int64_t(vsSize) &&
					s->Read(ps.data(), std::int64_t(psSize)) == std::int64_t(psSize));
		}

		void SaveBlobCache(const String& path, const std::vector<std::uint8_t>& vs, const std::vector<std::uint8_t>& ps)
		{
			fs::CreateDirectories(fs::GetDirectoryName(path));
			std::unique_ptr<Stream> s = fs::Open(path, FileAccess::Write);
			if (s == nullptr || !s->IsValid()) {
				return;
			}
			s->WriteValueAsLE<std::uint32_t>(kBlobCacheMagic);
			s->WriteValueAsLE<std::uint32_t>(kBlobCacheVersion);
			s->WriteValueAsLE<std::uint32_t>(std::uint32_t(vs.size()));
			s->WriteValueAsLE<std::uint32_t>(std::uint32_t(ps.size()));
			s->Write(vs.data(), std::int64_t(vs.size()));
			s->Write(ps.data(), std::int64_t(ps.size()));
		}

		// DXGI format + byte size for a reflected vertex attribute type (used by the input layout builder)
		DXGI_FORMAT AttributeFormat(ShaderCompiler::UniformType type)
		{
			switch (type) {
				case ShaderCompiler::UniformType::Float: return DXGI_FORMAT_R32_FLOAT;
				case ShaderCompiler::UniformType::Vec2: return DXGI_FORMAT_R32G32_FLOAT;
				case ShaderCompiler::UniformType::Vec3: return DXGI_FORMAT_R32G32B32_FLOAT;
				case ShaderCompiler::UniformType::Vec4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
				case ShaderCompiler::UniformType::Int: return DXGI_FORMAT_R32_SINT;
				case ShaderCompiler::UniformType::IVec2: return DXGI_FORMAT_R32G32_SINT;
				case ShaderCompiler::UniformType::IVec4: return DXGI_FORMAT_R32G32B32A32_SINT;
				case ShaderCompiler::UniformType::UInt: return DXGI_FORMAT_R32_UINT;
				case ShaderCompiler::UniformType::UVec2: return DXGI_FORMAT_R32G32_UINT;
				default: return DXGI_FORMAT_R32G32B32A32_FLOAT;
			}
		}
	}

	std::uint32_t D3D11ShaderProgram::nextHandle_ = 1;

	D3D11ShaderProgram::D3D11ShaderProgram()
		: D3D11ShaderProgram(QueryPhase::Immediate)
	{
	}

	D3D11ShaderProgram::D3D11ShaderProgram(QueryPhase queryPhase)
		: handle_(nextHandle_++), status_(Status::NotLinked), introspection_(Introspection::Disabled), queryPhase_(queryPhase),
			batchSize_(DefaultBatchSize), shouldLogOnErrors_(true), uniformsSize_(0), uniformBlocksSize_(0),
			reflection_(nullptr), boundVbo_(nullptr), boundIbo_(nullptr),
			vertexShader_(nullptr), pixelShader_(nullptr), inputLayout_(nullptr), inputLayoutFingerprint_(~std::uint64_t(0))
	{
	}

	D3D11ShaderProgram::D3D11ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection, QueryPhase queryPhase)
		: D3D11ShaderProgram(queryPhase)
	{
		static_cast<void>(vertexFile);
		static_cast<void>(fragmentFile);
		static_cast<void>(introspection);
	}

	D3D11ShaderProgram::D3D11ShaderProgram(StringView vertexFile, StringView fragmentFile, Introspection introspection)
		: D3D11ShaderProgram(vertexFile, fragmentFile, introspection, QueryPhase::Immediate)
	{
	}

	D3D11ShaderProgram::D3D11ShaderProgram(StringView vertexFile, StringView fragmentFile)
		: D3D11ShaderProgram(vertexFile, fragmentFile, Introspection::Enabled, QueryPhase::Immediate)
	{
	}

	D3D11ShaderProgram::~D3D11ShaderProgram()
	{
		// Clear this program from the device's current-program and last-bound-shader tracking before the
		// shader objects are released, so a recycled pointer can't be mistaken for a still-bound shader
		D3D11Device::OnProgramDestroyed(this);
		SafeRelease(inputLayout_);
		SafeRelease(pixelShader_);
		SafeRelease(vertexShader_);
		// The pipeline keys per-shader camera uniform data on the program pointer; drop this program's
		// entry so RenderResources::Dispose() finds the map empty (mirrors GLShaderProgram's destructor)
		RenderResources::RemoveCameraUniformData(this);
	}

	bool D3D11ShaderProgram::IsLinked() const
	{
		return (status_ == Status::Linked || status_ == Status::LinkedWithDeferredQueries || status_ == Status::LinkedWithIntrospection);
	}

	bool D3D11ShaderProgram::AttachShaderFromFile(ShaderStage stage, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(filename);
		return true;
	}

	bool D3D11ShaderProgram::AttachShaderFromString(ShaderStage stage, StringView string)
	{
		static_cast<void>(stage);
		static_cast<void>(string);
		return true;
	}

	bool D3D11ShaderProgram::AttachShaderFromStrings(ShaderStage stage, ArrayView<const StringView> strings)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		return true;
	}

	bool D3D11ShaderProgram::AttachShaderFromStringsAndFile(ShaderStage stage, ArrayView<const StringView> strings, StringView filename)
	{
		static_cast<void>(stage);
		static_cast<void>(strings);
		static_cast<void>(filename);
		return true;
	}

	bool D3D11ShaderProgram::Link(Introspection introspection)
	{
		return FinalizeAfterLinking(introspection);
	}

	bool D3D11ShaderProgram::FinalizeAfterLinking(Introspection introspection)
	{
		introspection_ = introspection;
		PerformIntrospection();
		return true;
	}

	void D3D11ShaderProgram::Use()
	{
		D3D11Device::BindProgram(this);
	}

	void D3D11ShaderProgram::PerformIntrospection()
	{
		if (introspection_ != Introspection::Disabled && status_ != Status::LinkedWithIntrospection) {
			uniformsSize_ = 0;
			uniformBlocksSize_ = 0;

			if (reflection_ != nullptr) {
				ImportReflection();
				// Compile the HLSL stage sources while the reflection is still available. Attribute/block
				// imports above populate the data the cbuffer reflection maps block cbuffers against.
				CompileHlsl();
			}
			status_ = Status::LinkedWithIntrospection;
		}
		// The reflection is consumed by introspection (its layout is imported into the members above)
		reflection_ = nullptr;
	}

	void D3D11ShaderProgram::ImportReflection()
	{
		const ShaderCompiler::ProgramVariant& reflection = *reflection_;
		std::int32_t nextLocation = 0;

		// Loose uniforms - samplers are kept in a separate reflection list but treated as loose uniforms here
		for (std::size_t i = 0; i < reflection.UniformCount; i++) {
			const ShaderCompiler::Uniform& u = reflection.Uniforms[i];
			uniforms_.emplace_back(this, u.Name, u.Type, std::int32_t(u.ArraySize), nextLocation++);
			uniformsSize_ += uniforms_.back().GetMemorySize();
		}
		for (std::size_t i = 0; i < reflection.TextureCount; i++) {
			const ShaderCompiler::TextureBinding& t = reflection.Textures[i];
			uniforms_.emplace_back(this, t.Name, ShaderCompiler::UniformType::Sampler2D, 1, nextLocation++);
			uniformsSize_ += uniforms_.back().GetMemorySize();
		}

		uniformBlocks_.reserve(reflection.BlockCount);
		for (std::size_t i = 0; i < reflection.BlockCount; i++) {
			const ShaderCompiler::UniformBlock& b = reflection.Blocks[i];

			// A BATCH_SIZE-sized instance array uses the explicitly set batch size, or the same 64 KB-based
			// fallback the in-shader "#ifndef BATCH_SIZE" defaults assume when no size is injected
			std::uint32_t effectiveBatchSize = 0;
			std::uint32_t dataSize = b.BaseSize;
			if (b.InstanceStride > 0) {
				effectiveBatchSize = (batchSize_ != std::uint32_t(DefaultBatchSize) && batchSize_ > 0)
					? batchSize_ : (64u * 1024u) / b.InstanceStride;
				dataSize += b.InstanceStride * effectiveBatchSize;
			}

			const std::uint32_t blockIndex = std::uint32_t(uniformBlocks_.size());
			uniformBlocks_.emplace_back(blockIndex, b.Name, std::int32_t(dataSize));
			D3D11UniformBlock& block = uniformBlocks_.back();
			uniformBlocksSize_ += block.GetSize();

			if (introspection_ != Introspection::NoUniformsInBlocks) {
				block.members_.reserve(b.MemberCount);
				for (std::size_t j = 0; j < b.MemberCount; j++) {
					const ShaderCompiler::BlockMember& m = b.Members[j];
					if (m.Type == ShaderCompiler::UniformType::Struct) {
						// Struct aggregates are never read by name; only flat leaf members matter
						continue;
					}
					D3D11Uniform member;
					member.SetName(m.Name);
					member.type_ = m.Type;
					member.size_ = (m.ArraySize == ShaderCompiler::SymbolicArraySize)
						? std::int32_t(effectiveBatchSize)
						: (m.ArraySize > 0 ? std::int32_t(m.ArraySize) : 1);
					member.blockIndex_ = std::int32_t(blockIndex);
					member.offset_ = std::int32_t(m.Offset);
					member.owner_ = this;
					block.members_.push_back(member);
				}
			}
		}

		for (std::size_t i = 0; i < reflection.AttributeCount; i++) {
			const ShaderCompiler::Attribute& a = reflection.Attributes[i];
			const std::int32_t location = (a.Location >= 0 ? a.Location : std::int32_t(i));
			attributes_.emplace_back(a.Name, a.Type, location);
			vertexFormat_[std::uint32_t(location)].Init(std::uint32_t(location), std::int32_t(UniformTypeInfo::ComponentCount(a.Type)), 0);
		}
	}

	void D3D11ShaderProgram::CompileHlsl()
	{
		const char* vsSource = reflection_->HlslVsSource;
		const char* fsSource = reflection_->HlslFsSource;
		if (vsSource == nullptr || fsSource == nullptr) {
			// No HLSL lowering available (e.g. a runtime-compiled shader outside the emitter's subset)
			if (shouldLogOnErrors_) {
				LOGW("Program {} has no HLSL sources; draws with it are skipped", handle_);
			}
			return;
		}

		ID3D11Device* device = D3D11Device::GetD3DDevice();
		if (device == nullptr) {
			LOGE("Cannot compile HLSL: Direct3D 11 device is not created yet");
			return;
		}

		std::vector<std::uint8_t> vsBytes;
		std::vector<std::uint8_t> psBytes;

		// Warm path: load the precompiled DXBC from the on-disk cache (keyed by the HLSL source hash)
		const String cachePath = BlobCachePath(vsSource, fsSource);
		bool loaded = (!cachePath.empty() && LoadBlobCache(cachePath, vsBytes, psBytes));

		if (!loaded) {
			// Force column-major cbuffer matrix packing so the emitter's mul(M, v) column-vector algebra reads
			// the engine's column-major (OpenGL-convention) uniform data verbatim - no per-matrix transpose
			// needed. Skip optimization to keep the cold-start compile fast (this is a debug backend build).
			const UINT compileFlags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_SKIP_OPTIMIZATION;

			auto compileStage = [&](const char* source, const char* entry, const char* target, std::vector<std::uint8_t>& out) -> bool {
				ID3DBlob* blob = nullptr;
				ID3DBlob* errorBlob = nullptr;
				const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
					entry, target, compileFlags, 0, &blob, &errorBlob);
				if (FAILED(hr)) {
					if (errorBlob != nullptr) {
						LOGE("HLSL {} compile failed: {}", entry, reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
						errorBlob->Release();
					} else {
						LOGE("HLSL {} compile failed: 0x{:.8x}", entry, static_cast<std::uint32_t>(hr));
					}
					if (blob != nullptr) {
						blob->Release();
					}
					return false;
				}
				if (errorBlob != nullptr) {
					errorBlob->Release();
				}
				out.assign(reinterpret_cast<const std::uint8_t*>(blob->GetBufferPointer()),
					reinterpret_cast<const std::uint8_t*>(blob->GetBufferPointer()) + blob->GetBufferSize());
				blob->Release();
				return true;
			};

			if (!compileStage(vsSource, "VSMain", "vs_4_0", vsBytes) || !compileStage(fsSource, "PSMain", "ps_4_0", psBytes)) {
				status_ = Status::CompilationFailed;
				return;
			}
			if (!cachePath.empty()) {
				SaveBlobCache(cachePath, vsBytes, psBytes);
			}
		}

		HRESULT hr = device->CreateVertexShader(vsBytes.data(), vsBytes.size(), nullptr, &vertexShader_);
		if (SUCCEEDED(hr)) {
			hr = device->CreatePixelShader(psBytes.data(), psBytes.size(), nullptr, &pixelShader_);
		}
		if (FAILED(hr)) {
			LOGE("Failed to create Direct3D 11 shader objects: 0x{:.8x}", static_cast<std::uint32_t>(hr));
			SafeRelease(vertexShader_);
			SafeRelease(pixelShader_);
			status_ = Status::CompilationFailed;
			return;
		}

		// Keep the VS bytecode so input layouts can be validated/created against it later
		vsByteCode_ = vsBytes;

		ReflectStageCBuffers(vsBytes.data(), vsBytes.size(), vsCBuffers_, vsTextureMask_);
		ReflectStageCBuffers(psBytes.data(), psBytes.size(), psCBuffers_, psTextureMask_);
	}

	void D3D11ShaderProgram::ReflectStageCBuffers(const void* byteCode, std::size_t byteCodeSize, std::vector<D3D11CBufferSlot>& slots, std::uint32_t& textureMask)
	{
		ID3D11ShaderReflection* refl = nullptr;
		if (FAILED(D3DReflect(byteCode, byteCodeSize, __uuidof(ID3D11ShaderReflection), reinterpret_cast<void**>(&refl))) || refl == nullptr) {
			return;
		}

		D3D11_SHADER_DESC shaderDesc;
		refl->GetDesc(&shaderDesc);

		// Record which texture/sampler registers the stage actually binds, so the device only touches those
		// slots at draw time instead of re-binding all units to both stages every draw
		textureMask = 0;
		for (UINT r = 0; r < shaderDesc.BoundResources; r++) {
			D3D11_SHADER_INPUT_BIND_DESC bindDesc;
			if (SUCCEEDED(refl->GetResourceBindingDesc(r, &bindDesc)) &&
				(bindDesc.Type == D3D_SIT_TEXTURE || bindDesc.Type == D3D_SIT_SAMPLER) && bindDesc.BindPoint < 32) {
				textureMask |= (1u << bindDesc.BindPoint);
			}
		}

		for (UINT i = 0; i < shaderDesc.ConstantBuffers; i++) {
			ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(i);
			D3D11_SHADER_BUFFER_DESC cbDesc;
			if (FAILED(cb->GetDesc(&cbDesc))) {
				continue;
			}

			// Resolve the cbuffer's register (bN) from the resource bindings
			std::uint32_t reg = 0;
			for (UINT r = 0; r < shaderDesc.BoundResources; r++) {
				D3D11_SHADER_INPUT_BIND_DESC bindDesc;
				if (SUCCEEDED(refl->GetResourceBindingDesc(r, &bindDesc)) &&
					bindDesc.Type == D3D_SIT_CBUFFER && std::strcmp(bindDesc.Name, cbDesc.Name) == 0) {
					reg = bindDesc.BindPoint;
					break;
				}
			}

			D3D11CBufferSlot slot;
			slot.Register = reg;
			slot.ByteSize = (cbDesc.Size + 15u) & ~15u;

			if (std::strcmp(cbDesc.Name, "_Globals") == 0) {
				slot.IsGlobals = true;
				for (UINT v = 0; v < cbDesc.Variables; v++) {
					ID3D11ShaderReflectionVariable* var = cb->GetVariableByIndex(v);
					D3D11_SHADER_VARIABLE_DESC varDesc;
					if (SUCCEEDED(var->GetDesc(&varDesc))) {
						slot.Globals.push_back({ std::string(varDesc.Name), varDesc.StartOffset, varDesc.Size });
					}
				}
			} else {
				slot.IsGlobals = false;
				slot.BlockIndex = -1;
				for (std::size_t b = 0; b < uniformBlocks_.size(); b++) {
					if (std::strcmp(uniformBlocks_[b].GetName(), cbDesc.Name) == 0) {
						slot.BlockIndex = std::int32_t(b);
						break;
					}
				}
			}

			slots.push_back(std::move(slot));
		}

		refl->Release();
	}

	ID3D11InputLayout* D3D11ShaderProgram::GetInputLayout()
	{
		if (attributes_.empty() || vsByteCode_.empty()) {
			return nullptr;
		}
		// The attribute set is fixed per program, so the layout is built once and cached (the per-vertex byte
		// offsets/stride the pipeline set on the vertex format are stable for this program's geometry).
		if (inputLayout_ != nullptr) {
			return inputLayout_;
		}

		ID3D11Device* device = D3D11Device::GetD3DDevice();
		if (device == nullptr) {
			return nullptr;
		}

		// One input element per reflected attribute. The HLSL emitter assigns each attribute the semantic
		// TEXCOORD<location>, so the semantic index is the attribute location; the within-vertex byte offset
		// comes from the vertex format the pipeline defined (its `pointer_`), matching the GL backend which
		// feeds that same value to glVertexAttribPointer.
		std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
		elements.reserve(attributes_.size());
		for (const D3D11Attribute& a : attributes_) {
			const std::int32_t loc = a.GetLocation();
			if (loc < 0) {
				continue;
			}
			const D3D11VertexFormat::Attribute& fa = vertexFormat_[std::uint32_t(loc)];
			D3D11_INPUT_ELEMENT_DESC desc = {};
			desc.SemanticName = "TEXCOORD";
			desc.SemanticIndex = static_cast<UINT>(loc);
			desc.Format = AttributeFormat(a.GetType());
			desc.InputSlot = 0;
			desc.AlignedByteOffset = static_cast<UINT>(reinterpret_cast<std::uintptr_t>(fa.GetPointer()));
			desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
			desc.InstanceDataStepRate = 0;
			elements.push_back(desc);
		}

		if (elements.empty()) {
			return nullptr;
		}

		ID3D11InputLayout* layout = nullptr;
		const HRESULT hr = device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()),
			vsByteCode_.data(), vsByteCode_.size(), &layout);
		if (FAILED(hr)) {
			LOGE("CreateInputLayout failed for program {}: 0x{:.8x}", handle_, static_cast<std::uint32_t>(hr));
			return nullptr;
		}

		inputLayout_ = layout;
		return inputLayout_;
	}

	std::uint32_t D3D11ShaderProgram::GetVertexStride() const
	{
		for (const D3D11Attribute& a : attributes_) {
			if (a.GetLocation() >= 0) {
				const D3D11VertexFormat::Attribute& fa = vertexFormat_[std::uint32_t(a.GetLocation())];
				if (fa.IsEnabled() && fa.GetStride() > 0) {
					return static_cast<std::uint32_t>(fa.GetStride());
				}
			}
		}
		return 0;
	}

	bool D3D11ShaderProgram::HasAttribute(const char* name) const
	{
		for (const D3D11Attribute& a : attributes_) {
			if (std::strcmp(a.GetName(), name) == 0) {
				return true;
			}
		}
		return false;
	}

	D3D11VertexFormat::Attribute* D3D11ShaderProgram::GetAttribute(const char* name)
	{
		for (const D3D11Attribute& a : attributes_) {
			if (std::strcmp(a.GetName(), name) == 0 && a.GetLocation() >= 0) {
				return &vertexFormat_[std::uint32_t(a.GetLocation())];
			}
		}
		return nullptr;
	}

	void D3D11ShaderProgram::DefineVertexFormat(const D3D11BufferObject* vbo, const D3D11BufferObject* ibo, std::uint32_t vboOffset)
	{
		boundVbo_ = vbo;
		boundIbo_ = ibo;
		if (vbo != nullptr) {
			for (const D3D11Attribute& a : attributes_) {
				if (a.GetLocation() >= 0) {
					D3D11VertexFormat::Attribute& attr = vertexFormat_[std::uint32_t(a.GetLocation())];
					attr.setVbo(vbo);
					attr.SetBaseOffset(vboOffset);
				}
			}
			vertexFormat_.SetIbo(ibo);
		}
	}

	void D3D11ShaderProgram::Reset()
	{
		D3D11Device::OnProgramDestroyed(this);
		uniforms_.clear();
		uniformBlocks_.clear();
		attributes_.clear();
		resolvedUniforms_.clear();
		vertexFormat_.Reset();
		vsCBuffers_.clear();
		psCBuffers_.clear();
		vsByteCode_.clear();
		vsTextureMask_ = 0;
		psTextureMask_ = 0;
		SafeRelease(inputLayout_);
		SafeRelease(pixelShader_);
		SafeRelease(vertexShader_);
		inputLayoutFingerprint_ = ~std::uint64_t(0);
		status_ = Status::NotLinked;
		batchSize_ = DefaultBatchSize;
		reflection_ = nullptr;
	}

	void D3D11ShaderProgram::SetObjectLabel(StringView label)
	{
		// The backend selects the HLSL variant from the reflection, not the label, so this is informational only
		static_cast<void>(label);
	}

	void D3D11ShaderProgram::SetResolvedUniform(const char* name, const std::uint8_t* data)
	{
		for (ResolvedUniform& r : resolvedUniforms_) {
			if (r.Name == name) {
				r.Data = data;
				return;
			}
		}
		resolvedUniforms_.push_back({name, data});
	}

	const std::uint8_t* D3D11ShaderProgram::ResolveUniform(const char* name) const
	{
		for (const ResolvedUniform& r : resolvedUniforms_) {
			if (r.Name == name) {
				return r.Data;
			}
		}
		return nullptr;
	}
}
