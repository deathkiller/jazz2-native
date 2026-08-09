#include "Emit.h"
#include "Essl100.h"
#include "Hlsl.h"
#include "Vulkan.h"

#include <utility>

#include <Base/Format.h>
#include <Containers/SmallVector.h>
#include <Containers/StringConcatenable.h>

using namespace Death::Containers::Literals;

namespace ShaderCompiler
{
	namespace
	{
		constexpr std::size_t Npos = ~std::size_t{0};

		/** Substring [pos, pos + count), clamping count to the available length (never throws for pos <= size) */
		StringView Substr(StringView s, std::size_t pos, std::size_t count = Npos)
		{
			std::size_t size = s.size();
			if (pos > size) {
				pos = size;
			}
			std::size_t avail = size - pos;
			if (count > avail) {
				count = avail;
			}
			return s.slice(pos, pos + count);
		}

		/** Index of the last character that is any of @p set, or Npos */
		std::size_t FindLastOf(StringView s, StringView set)
		{
			for (std::size_t i = s.size(); i > 0; i--) {
				if (set.contains(s[i - 1])) {
					return i - 1;
				}
			}
			return Npos;
		}

		/** C++ names of the UniformType enumerators, indexed by GlslType */
		const char* EnumName(GlslType type)
		{
			switch (type) {
				case GlslType::Float: return "Float";
				case GlslType::Int: return "Int";
				case GlslType::UInt: return "UInt";
				case GlslType::Bool: return "Bool";
				case GlslType::Vec2: return "Vec2";
				case GlslType::Vec3: return "Vec3";
				case GlslType::Vec4: return "Vec4";
				case GlslType::IVec2: return "IVec2";
				case GlslType::IVec3: return "IVec3";
				case GlslType::IVec4: return "IVec4";
				case GlslType::UVec2: return "UVec2";
				case GlslType::UVec3: return "UVec3";
				case GlslType::UVec4: return "UVec4";
				case GlslType::BVec2: return "BVec2";
				case GlslType::BVec3: return "BVec3";
				case GlslType::BVec4: return "BVec4";
				case GlslType::Mat2: return "Mat2";
				case GlslType::Mat3: return "Mat3";
				case GlslType::Mat4: return "Mat4";
				case GlslType::Sampler2D: return "Sampler2D";
				case GlslType::Sampler3D: return "Sampler3D";
				case GlslType::SamplerCube: return "SamplerCube";
				case GlslType::Struct: return "Struct";
			}
			return "Float";
		}

		String ArraySizeExpr(std::uint32_t arraySize, bool symbolic)
		{
			if (symbolic) {
				return "ShaderCompiler::SymbolicArraySize";
			}
			return Death::format("{}", arraySize);
		}

		String ArraySuffix(std::uint32_t arraySize, bool symbolic)
		{
			if (symbolic) {
				return "[*]";
			}
			if (arraySize != 0) {
				return "["_s + Death::format("{}", arraySize) + "]"_s;
			}
			return {};
		}

		/** "0x"-prefixed, 8-digit, 'u'-suffixed hexadecimal of a 32-bit SPIR-V word */
		String Hex32(std::uint32_t v)
		{
			static const char* const Digits = "0123456789abcdef";
			char buffer[11];
			buffer[0] = '0';
			buffer[1] = 'x';
			for (int i = 0; i < 8; i++) {
				buffer[2 + i] = Digits[(v >> ((7 - i) * 4)) & 0xFu];
			}
			buffer[10] = 'u';
			return String{buffer, sizeof(buffer)};
		}

		/** "0x"-prefixed, 2-digit hexadecimal of one DXBC byte */
		String Hex8(std::uint8_t v)
		{
			static const char* const Digits = "0123456789abcdef";
			char buffer[4];
			buffer[0] = '0';
			buffer[1] = 'x';
			buffer[2] = Digits[(v >> 4) & 0xFu];
			buffer[3] = Digits[v & 0xFu];
			return String{buffer, sizeof(buffer)};
		}

		/** Emits an embedded DXBC blob as a "constexpr std::uint8_t <symbol>[] = { ... };" byte array */
		void EmitDxbcArray(String& output, const String& symbol, const SmallVectorImpl<std::uint8_t>& bytes)
		{
			output += "\tinline constexpr std::uint8_t " + symbol + "[] = {\n";
			for (std::size_t i = 0; i < bytes.size(); i++) {
				if ((i % 16) == 0) {
					output += "\t\t";
				}
				output += Hex8(bytes[i]) + ",";
				output += (((i % 16) == 15 || i + 1 == bytes.size()) ? "\n"_s : ""_s);
			}
			output += "\t};\n";
			output += "\n";
		}

		/** Emits an embedded SPIR-V module as a "constexpr std::uint32_t <symbol>[] = { ... };" word array */
		void EmitSpirvArray(String& output, const String& symbol, const SmallVectorImpl<std::uint32_t>& words)
		{
			output += "\tinline constexpr std::uint32_t " + symbol + "[] = {\n";
			for (std::size_t i = 0; i < words.size(); i++) {
				if ((i % 8) == 0) {
					output += "\t\t";
				}
				output += Hex32(words[i]) + ",";
				output += (((i % 8) == 7 || i + 1 == words.size()) ? "\n"_s : " "_s);
			}
			output += "\t};\n";
			output += "\n";
		}

		/**
			Common reflection types for the standalone "ShaderCompilerTypes.h" header, which every
			generated header includes. Kept in the fixed "ShaderCompiler" namespace (not the -n
			namespace) so that generated headers with different data namespaces share one set.
		*/
		const char ReflectionTypes[] = R"TYPES(#ifndef SHADERCOMPILER_REFLECTION_TYPES
#define SHADERCOMPILER_REFLECTION_TYPES

namespace ShaderCompiler
{
	// Scalar/vector/matrix/opaque type of a uniform, block member or vertex attribute
	enum class UniformType : std::uint8_t
	{
		Float, Int, UInt, Bool,
		Vec2, Vec3, Vec4,
		IVec2, IVec3, IVec4,
		UVec2, UVec3, UVec4,
		BVec2, BVec3, BVec4,
		Mat2, Mat3, Mat4,
		Sampler2D, Sampler3D, SamplerCube,
		Struct
	};

	// ArraySize value marking an array sized by the symbolic BATCH_SIZE constant
	inline constexpr std::uint16_t SymbolicArraySize = 0xFFFFu;

	// Loose (non-block, non-sampler) uniform
	struct Uniform
	{
		const char* Name;
		UniformType Type;
		std::uint16_t ArraySize;		// 0 = not an array
	};

	// Member of a std140 uniform block
	struct BlockMember
	{
		const char* Name;
		UniformType Type;
		std::uint16_t ArraySize;		// 0 = not an array, SymbolicArraySize = BATCH_SIZE-sized
		std::uint32_t Offset;			// std140 offset from the start of the block
	};

	// std140 uniform block
	struct UniformBlock
	{
		const char* Name;
		std::uint32_t BaseSize;			// std140 size covering everything except symbolic arrays
		std::uint32_t InstanceStride;	// std140 element stride of the BATCH_SIZE-sized array (0 if none);
										// the runtime computes the batch size as maxUniformBlockSize / InstanceStride
		std::size_t MemberCount;
		const BlockMember* Members;
	};

	// Sampler uniform with its assigned texture unit
	struct TextureBinding
	{
		const char* Name;
		std::int32_t Unit;				// -1 = not assigned
	};

	// Vertex attribute
	struct Attribute
	{
		const char* Name;
		UniformType Type;
		std::int32_t Location;			// -1 = unspecified
	};

	// "render_mode" flags carried on Program (bitmask; 0 when no render_mode is declared)
	enum class RenderMode : std::uint32_t
	{
		BlendMix = 0x01,
		BlendAdd = 0x02,
		BlendSub = 0x04,
		BlendMul = 0x08,
		BlendPremulAlpha = 0x10,
		Unshaded = 0x20
	};

	// One compiled variant of a program
	struct ProgramVariant
	{
		const char* Name;				// "" for the base variant, otherwise the variant name
		const char* Defines;			// "" or the name of the baked "#define <NAME> (1)"
		const char* VsSource;
		const char* FsSource;
		std::size_t UniformCount;
		const Uniform* Uniforms;
		std::size_t BlockCount;
		const UniformBlock* Blocks;
		std::size_t TextureCount;
		const TextureBinding* Textures;
		std::size_t AttributeCount;
		const Attribute* Attributes;
		// OpenGL|ES 2.0 (ESSL 100) stage sources: the Essl100Emitter lowering of VsSource/FsSource — no
		// UBOs (std140 blocks become loose uniforms / a uniform struct-array) and no gl_VertexID (the quad
		// corner and the batched instance index become the aQuadCorner / aInstanceIndex vertex attributes).
		// Consumed under RHI_GL_PROFILE_ES2 with "#version 100"; the GL 3.3 / ES 3.0 path ignores these and
		// uses VsSource/FsSource. Null when the ES2 lowering was not available (e.g. runtime-compiled shaders).
		const char* VsSource100;
		const char* FsSource100;
		// Direct3D 11 (HLSL, Shader Model 4/5) stage sources: the HlslEmitter lowering of VsSource/FsSource —
		// VSMain/PSMain entry points, std140 blocks as cbuffers, separate Texture2D + SamplerState objects and
		// mul()-based matrix algebra. Consumed by the D3D11 backend; GL/ES/software ignore these.
		// Null when the HLSL lowering was not available (a construct outside the emitter's subset) — or when
		// the stages were precompiled to DXBC below (the blob replaces the text in the binary).
		const char* HlslVsSource;
		const char* HlslFsSource;
		// Direct3D 11 precompiled DXBC bytecode: the HLSL lowering of both stages compiled offline through
		// d3dcompiler_47 (VSMain/PSMain, vs_4_0/ps_4_0, column-major matrix packing — the same contract the
		// backend's runtime compilation uses), so the D3D11 backend (desktop and UWP/Xbox alike) creates its
		// shader objects straight from these blobs with no runtime D3DCompile and no on-disk cache. Emitted
		// all-or-nothing per variant: when present (both stages), HlslVsSource/HlslFsSource are null; null/0
		// when d3dcompiler_47 was unavailable at generation time, a stage failed to compile or the shader is
		// runtime-compiled — the backend then falls back to runtime-compiling the HLSL sources above.
		const std::uint8_t* HlslVsDxbc;
		std::size_t HlslVsDxbcSize;
		const std::uint8_t* HlslFsDxbc;
		std::size_t HlslFsDxbcSize;
		// Vulkan (SPIR-V) stage modules: the VulkanGlslEmitter lowering of VsSource/FsSource ("#version 450",
		// explicit set/binding + location decorations, gathered "_Globals" UBO, gl_VertexIndex) compiled offline
		// through glslang to SPIR-V words. Consumed by the Vulkan backend, which builds pipelines
		// straight from these and the descriptor-set layout from the same reflection; other backends ignore them.
		// Null/0 when glslang was unavailable at generation time or the shader is runtime-compiled (the Vulkan
		// backend is then not buildable / falls back). Sizes are the number of 32-bit SPIR-V words.
		const std::uint32_t* VkVsSpirv;
		std::size_t VkVsSpirvSize;
		const std::uint32_t* VkFsSpirv;
		std::size_t VkFsSpirvSize;
	};

	// A shader program with all of its variants (Variants[0] is always the base variant, whose Name is "")
	struct Program
	{
		const char* Name;
		std::uint32_t RenderModes;		// Bitmask of RenderMode flags (0 when no render_mode is declared)
		std::size_t VariantCount;
		const ProgramVariant* Variants;
	};
}

#endif
)TYPES";
	}

	String Emitter::BuildTypesHeader()
	{
		String output;
		output += "// Generated by ShaderCompiler. Do not edit manually.\n";
		output += "// Shared reflection types included by every generated shader artifact header.\n";
		output += "#pragma once\n";
		output += "\n";
		output += "#include <cstddef>\n";
		output += "#include <cstdint>\n";
		output += "\n";
		output += ReflectionTypes;
		return output;
	}

	namespace
	{
		// Both backends' symbols are ALWAYS defined by their aggregate - as the real array when the
		// compiler was there, as a null pointer otherwise - so the per-shader table can name them
		// unconditionally. Sizes are named too (`<symbol>Size`) rather than baked in as literals here: an
		// aggregate that is a generation behind then still describes the blobs it actually holds.

		/** Per-variant embedded-SPIR-V symbol names */
		struct VkSpirvSymbols
		{
			String VsSymbol;
			String FsSymbol;
		};

		/** Per-variant HLSL artifact symbol names: the source strings and the precompiled DXBC blobs */
		struct HlslSymbols
		{
			String VsSource;
			String FsSource;
			String VsDxbc;
			String FsDxbc;
		};

		/** Emits one program (per-variant sources, reflection arrays, variant list and Program descriptor) into @p output */
		bool EmitProgram(const ShaderDocument& document, const SmallVectorImpl<VariantReflection>& variants,
			SpirvCompileFn& compileSpirv, DxbcCompileFn& compileDxbc, String& output,
			BackendArtifacts& artifacts, Diagnostic& diag)
		{
			const String& program = document.ProgramName;

			// Per-variant HLSL artifact symbol names (source strings or DXBC blobs; "nullptr" when the HLSL
			// lowering declined the stage), filled below and referenced by the ProgramVariant initializers.
			SmallVector<HlslSymbols, 0> hlslSymbols;
			// Per-variant embedded-SPIR-V symbol names + sizes (or "nullptr"/0 when no SPIR-V was emitted)
			SmallVector<VkSpirvSymbols, 0> vkSymbols;

			for (const VariantReflection& v : variants) {
				// The unnamed base variant carries no infix ("Lighting_Vs"), named variants keep theirs ("Tinted_USE_PALETTE_Vs")
				const String prefix = (v.Name.empty() ? program : String(program + "_" + v.Name));
				const StageReflection& r = v.Reflection;
				HlslSymbols hlsl;
				// Per-stage HLSL text (empty = the lowering declined) and its offline-compiled DXBC (empty =
				// no compiler or the compile failed), buffered so the emission below is all-or-nothing per
				// variant: blobs replace the source text only when BOTH stages compiled.
				String hlslSources[2];
				SmallVector<std::uint8_t, 0> hlslDxbc[2];
				VkSpirvSymbols vk;

				for (std::int32_t stage = 0; stage < 2; stage++) {
					bool vertexStage = (stage == 0);
					String source = ShaderParser::BuildStageSource(document, vertexStage, v.Define);
					if (source.contains(")__SHDR__\""_s)) {
						diag.Message = "shader source contains the raw string terminator sequence )__SHDR__\"";
						diag.Line = 1;
						return false;
					}
					// OpenGL-family (GL 3.3 / ES 3.0 / WebGL 2) stage source — compiled only into the OpenGL backend
					// build's non-ES2 profile; the RHI_GL_PROFILE_ES2 profile uses the _Vs100/_Fs100 source below
					// instead, so exactly one of the two GL variants is ever compiled in.
					output += "#if defined(WITH_RHI_GL) && !defined(RHI_GL_PROFILE_ES2)\n";
					output += "\tinline constexpr char " + prefix + (vertexStage ? "_Vs" : "_Fs") + "[] =\n";
					output += "R\"__SHDR__(";
					output += source;
					output += ")__SHDR__\";\n";
					output += "#endif\n";
					output += "\n";

					// OpenGL|ES 2.0 (ESSL 100) lowering of the same stage. A decline here should not happen
					// for committed shaders (enforced by --essl100-check); fall back to the modern source so
					// the field is never null for a precompiled program.
					String es2source;
					Diagnostic es2diag;
					if (!Essl100Emitter::Transform(source, vertexStage, es2source, es2diag)) {
						es2source = source;
					}
					if (es2source.contains(")__SHDR__\""_s)) {
						diag.Message = "ES2 shader source contains the raw string terminator sequence )__SHDR__\"";
						diag.Line = 1;
						return false;
					}
					// OpenGL|ES 2.0 (ESSL 100) stage source — compiled only into the OpenGL backend build's ES2
					// profile (RHI_GL_PROFILE_ES2); the non-ES2 profile uses the _Vs/_Fs source above instead.
					output += "#if defined(WITH_RHI_GL) && defined(RHI_GL_PROFILE_ES2)\n";
					output += "\tinline constexpr char " + prefix + (vertexStage ? "_Vs100" : "_Fs100") + "[] =\n";
					output += "R\"__SHDR__(";
					output += es2source;
					output += ")__SHDR__\";\n";
					output += "#endif\n";
					output += "\n";

					// Direct3D 11 (HLSL) lowering of the same stage, buffered for the per-variant emission
					// after this loop (blob-vs-source is an all-or-nothing choice across both stages). Unlike
					// the ES2 path there is no GLSL fallback — a decline leaves the fields null (the D3D11
					// backend will skip draws with such a shader). The terminator guard mirrors the modern/ES2
					// paths but only matters for the source form (a DXBC blob is emitted as a byte array).
					{
						String hlslSource;
						Diagnostic hlslDiag;
						if (HlslEmitter::Transform(source, vertexStage, r, hlslSource, hlslDiag) &&
							!hlslSource.contains(")__SHDR__\""_s)) {
							if (compileDxbc) {
								String log;
								if (!compileDxbc(hlslSource, vertexStage, hlslDxbc[stage], log)) {
									hlslDxbc[stage].clear();
								}
							}
							hlslSources[stage] = std::move(hlslSource);
						}
					}

					// Vulkan (SPIR-V) lowering of the same stage: emit the Vulkan GLSL and, when a glslang
					// compiler was injected, compile it to SPIR-V. The words go to the Vulkan AGGREGATE, not
					// here, so that a machine without glslang leaves the committed ones untouched - an absent
					// module is not a graceful degradation, it makes the backend skip every draw of the
					// program. A decline or a failed compile still defines the symbol, as a null.
					{
						String sym = prefix + (vertexStage ? "_VkVs" : "_VkFs");
						SmallVector<std::uint32_t, 0> words;
						if (compileSpirv) {
							String vkSource;
							Diagnostic vkDiag;
							if (VulkanGlslEmitter::Transform(source, vertexStage, r, vkSource, vkDiag)) {
								String log;
								if (!compileSpirv(vkSource, vertexStage, words, log)) {
									words.clear();
								}
							}
						}
						if (!words.empty()) {
							EmitSpirvArray(artifacts.Vulkan, sym, words);
						} else {
							artifacts.Vulkan += "\tinline constexpr const std::uint32_t* " + sym + " = nullptr;\n";
						}
						artifacts.Vulkan += "\tinline constexpr std::size_t " + sym + "Size = " +
							Death::format("{}", words.size()) + ";\n\n";
						(vertexStage ? vk.VsSymbol : vk.FsSymbol) = std::move(sym);
					}
				}
				// Direct3D 11 artifacts, all into the D3D11 AGGREGATE rather than here - D3DCompile is a
				// Windows DLL entry point, so most machines that build this project cannot rebuild them and
				// must not overwrite them. When BOTH stages precompiled, only the DXBC blobs carry content
				// and the HLSL text stays out of the binary; otherwise the HLSL sources carry it (for
				// whichever stages lowered) and the backend runtime-compiles them. Every symbol is defined
				// either way, as a null when it holds nothing, so the table below can name all four.
				const bool haveDxbc = (!hlslDxbc[0].empty() && !hlslDxbc[1].empty());
				for (std::int32_t stage = 0; stage < 2; stage++) {
					String dxbcSym = prefix + (stage == 0 ? "_VsDxbc" : "_FsDxbc");
					if (haveDxbc) {
						EmitDxbcArray(artifacts.D3d11, dxbcSym, hlslDxbc[stage]);
					} else {
						artifacts.D3d11 += "\tinline constexpr const std::uint8_t* " + dxbcSym + " = nullptr;\n";
					}
					artifacts.D3d11 += "\tinline constexpr std::size_t " + dxbcSym + "Size = " +
						Death::format("{}", haveDxbc ? hlslDxbc[stage].size() : std::size_t(0)) + ";\n\n";
					(stage == 0 ? hlsl.VsDxbc : hlsl.FsDxbc) = std::move(dxbcSym);

					String sourceSym = prefix + (stage == 0 ? "_VsHlsl" : "_FsHlsl");
					if (!haveDxbc && !hlslSources[stage].empty()) {
						artifacts.D3d11 += "\tinline constexpr char " + sourceSym + "[] =\n";
						artifacts.D3d11 += "R\"__SHDR__(";
						artifacts.D3d11 += hlslSources[stage];
						artifacts.D3d11 += ")__SHDR__\";\n\n";
					} else {
						artifacts.D3d11 += "\tinline constexpr const char* " + sourceSym + " = nullptr;\n\n";
					}
					(stage == 0 ? hlsl.VsSource : hlsl.FsSource) = std::move(sourceSym);
				}
				hlslSymbols.push_back(std::move(hlsl));
				vkSymbols.push_back(std::move(vk));

				if (!r.Uniforms.empty()) {
					output += "\tinline constexpr ShaderCompiler::Uniform " + prefix + "_Uniforms[] = {\n";
					for (const UniformInfo& u : r.Uniforms) {
						output += "\t\t{ \"" + u.Name + "\", ShaderCompiler::UniformType::" + EnumName(u.Type) + ", " + ArraySizeExpr(u.ArraySize, false) + " },\n";
					}
					output += "\t};\n";
					output += "\n";
				}

				for (std::size_t i = 0; i < r.Blocks.size(); i++) {
					const BlockInfo& block = r.Blocks[i];
					output += "\tinline constexpr ShaderCompiler::BlockMember " + prefix + "_Block" + Death::format("{}",i) + "_Members[] = {\n";
					for (const MemberInfo& m : block.Members) {
						output += "\t\t{ \"" + m.Name + "\", ShaderCompiler::UniformType::" + EnumName(m.Type) + ", " +
							ArraySizeExpr(m.ArraySize, m.SymbolicArray) + ", " + Death::format("{}",m.Offset) + " },\n";
					}
					output += "\t};\n";
					output += "\n";
				}
				if (!r.Blocks.empty()) {
					output += "\tinline constexpr ShaderCompiler::UniformBlock " + prefix + "_Blocks[] = {\n";
					for (std::size_t i = 0; i < r.Blocks.size(); i++) {
						const BlockInfo& block = r.Blocks[i];
						output += "\t\t{ \"" + block.Name + "\", " + Death::format("{}",block.BaseSize) + ", " +
							Death::format("{}",block.InstanceStride) + ", " + Death::format("{}",block.Members.size()) + ", " +
							prefix + "_Block" + Death::format("{}",i) + "_Members },\n";
					}
					output += "\t};\n";
					output += "\n";
				}

				if (!r.Textures.empty()) {
					output += "\tinline constexpr ShaderCompiler::TextureBinding " + prefix + "_Textures[] = {\n";
					for (const TextureInfo& t : r.Textures) {
						output += "\t\t{ \"" + t.Name + "\", " + Death::format("{}",t.Unit) + " },\n";
					}
					output += "\t};\n";
					output += "\n";
				}

				if (!r.Attributes.empty()) {
					output += "\tinline constexpr ShaderCompiler::Attribute " + prefix + "_Attributes[] = {\n";
					for (const AttributeInfo& a : r.Attributes) {
						output += "\t\t{ \"" + a.Name + "\", ShaderCompiler::UniformType::" + EnumName(a.Type) + ", " + Death::format("{}",a.Location) + " },\n";
					}
					output += "\t};\n";
					output += "\n";
				}
			}

			output += "\tinline constexpr ShaderCompiler::ProgramVariant " + program + "_Variants[] = {\n";
			std::size_t variantIndex = 0;
			for (const VariantReflection& v : variants) {
				const String prefix = (v.Name.empty() ? program : String(program + "_" + v.Name));
				const StageReflection& r = v.Reflection;
				output += "\t\t{ \"" + v.Name + "\", \"" + v.Define + "\",\n";
				// Per-backend stage sources: each group references symbols emitted only under that backend's
				// WITH_RHI_* guard, so it is gated to match and is null on the other backends (which never read it) —
				// a backend build therefore compiles in only its own shader sources. The OpenGL backend is split
				// further by profile: the modern VsSource/FsSource are compiled in only for the non-ES2 profile and
				// the VsSource100/FsSource100 only for RHI_GL_PROFILE_ES2, matching the symbol guards above.
				output += "#if defined(WITH_RHI_GL) && !defined(RHI_GL_PROFILE_ES2)\n";
				output += "\t\t\t" + prefix + "_Vs, " + prefix + "_Fs,\n";
				output += "#else\n";
				output += "\t\t\tnullptr, nullptr,\n";
				output += "#endif\n";
				output += "\t\t\t" + Death::format("{}", r.Uniforms.size()) + ", " + (r.Uniforms.empty() ? String("nullptr") : String(prefix + "_Uniforms")) + ", ";
				output += Death::format("{}", r.Blocks.size()) + ", " + (r.Blocks.empty() ? String("nullptr") : String(prefix + "_Blocks")) + ", ";
				output += Death::format("{}", r.Textures.size()) + ", " + (r.Textures.empty() ? String("nullptr") : String(prefix + "_Textures")) + ", ";
				output += Death::format("{}", r.Attributes.size()) + ", " + (r.Attributes.empty() ? String("nullptr") : String(prefix + "_Attributes")) + ",\n";
				output += "#if defined(WITH_RHI_GL) && defined(RHI_GL_PROFILE_ES2)\n";
				output += "\t\t\t" + prefix + "_Vs100, " + prefix + "_Fs100,\n";
				output += "#else\n";
				output += "\t\t\tnullptr, nullptr,\n";
				output += "#endif\n";
				output += "#if defined(WITH_RHI_D3D11)\n";
				output += "\t\t\t" + hlslSymbols[variantIndex].VsSource + ", " + hlslSymbols[variantIndex].FsSource + ",\n";
				output += "\t\t\t" + hlslSymbols[variantIndex].VsDxbc + ", " + hlslSymbols[variantIndex].VsDxbc + "Size, " +
					hlslSymbols[variantIndex].FsDxbc + ", " + hlslSymbols[variantIndex].FsDxbc + "Size,\n";
				output += "#else\n";
				output += "\t\t\tnullptr, nullptr,\n";
				output += "\t\t\tnullptr, 0, nullptr, 0,\n";
				output += "#endif\n";
				output += "#if defined(WITH_RHI_VULKAN)\n";
				output += "\t\t\t" + vkSymbols[variantIndex].VsSymbol + ", " + vkSymbols[variantIndex].VsSymbol + "Size, " +
					vkSymbols[variantIndex].FsSymbol + ", " + vkSymbols[variantIndex].FsSymbol + "Size },\n";
				output += "#else\n";
				output += "\t\t\tnullptr, 0, nullptr, 0 },\n";
				output += "#endif\n";
				variantIndex++;
			}
			output += "\t};\n";
			output += "\n";
			output += "\tinline constexpr ShaderCompiler::Program " + program + " = { \"" + program + "\", " +
				Death::format("{}",document.RenderModes) + ", " + Death::format("{}",variants.size()) + ", " + program + "_Variants };\n";
			return true;
		}
	}

	bool Emitter::EmitHeader(const SmallVectorImpl<ProgramReflection>& programs, StringView ns, StringView inputFileName,
		SpirvCompileFn& compileSpirv, DxbcCompileFn& compileDxbc, String& output,
		BackendArtifacts& artifacts, Diagnostic& diag)
	{
		// Only the file name is embedded, so generated headers don't differ between machines
		String inputName = inputFileName;
		std::size_t lastSeparator = FindLastOf(inputName, "/\\"_s);
		if (lastSeparator != Npos) {
			inputName = String{Substr(inputName, lastSeparator + 1)};
		}

		output = {};
		output += "// Generated by ShaderCompiler from " + inputName + ". Do not edit manually.\n";
		output += "#pragma once\n";
		output += "\n";
		output += "#include \"ShaderCompilerTypes.h\"\n";
		// The two backends whose artifacts need a compiler this machine may not have live in their own
		// aggregates, which this header only REFERENCES (see BackendArtifacts). Including them here rather
		// than leaving it to the umbrella keeps a single generated header usable on its own.
		output += "#if defined(WITH_RHI_D3D11)\n";
		output += "#	include \"D3d11GeneratedShaders.h\"\n";
		output += "#endif\n";
		output += "#if defined(WITH_RHI_VULKAN)\n";
		output += "#	include \"VulkanGeneratedShaders.h\"\n";
		output += "#endif\n";
		output += "\n";
		// The generated shader data namespace carries no public API and is excluded from the API
		// documentation (Doxygen defines `DOXYGEN_GENERATING_OUTPUT`), keeping these headers out of it.
		output += "#ifndef DOXYGEN_GENERATING_OUTPUT\n";
		output += "namespace " + ns + "\n";
		output += "{\n";

		for (std::size_t i = 0; i < programs.size(); i++) {
			if (i != 0) {
				output += "\n";
			}
			if (!EmitProgram(*programs[i].Document, programs[i].Variants, compileSpirv, compileDxbc, output, artifacts, diag)) {
				return false;
			}
		}

		output += "}\n";
		output += "#endif\n";
		return true;
	}

	String Emitter::BuildCheckDump(const ShaderDocument& document, const SmallVectorImpl<VariantReflection>& variants)
	{
		String out;
		out += "program " + document.ProgramName + "\n";
		if (document.RenderModes != 0) {
			static const struct { std::uint32_t Bit; const char* Name; } RenderModeNames[] = {
				{ RenderModeBlendMix, "blend_mix" }, { RenderModeBlendAdd, "blend_add" },
				{ RenderModeBlendSub, "blend_sub" }, { RenderModeBlendMul, "blend_mul" },
				{ RenderModeBlendPremulAlpha, "blend_premul_alpha" }, { RenderModeUnshaded, "unshaded" }
			};
			out += "render_mode";
			bool first = true;
			for (const auto& mode : RenderModeNames) {
				if ((document.RenderModes & mode.Bit) != 0) {
					out += (first ? " " : ", ");
					out += mode.Name;
					first = false;
				}
			}
			out += "\n";
		}
		for (const VariantReflection& v : variants) {
			const StageReflection& r = v.Reflection;
			out += (v.Name.empty() ? String("variant (base)\n") : String("variant " + v.Name + "\n"));
			if (!v.Define.empty()) {
				out += "  define " + v.Define + "\n";
			}
			for (const StructInfo& s : r.Structs) {
				out += "  struct " + s.Name + " size=" + Death::format("{}",s.Size) + " align=" + Death::format("{}",s.Align) + "\n";
				for (const MemberInfo& f : s.Fields) {
					out += "    field " + f.TypeName + " " + f.Name + ArraySuffix(f.ArraySize, f.SymbolicArray) +
						" offset=" + Death::format("{}",f.Offset) + "\n";
				}
			}
			for (const UniformInfo& u : r.Uniforms) {
				out += "  uniform " + u.TypeName + " " + u.Name + ArraySuffix(u.ArraySize, false) + "\n";
			}
			for (const BlockInfo& b : r.Blocks) {
				out += "  block " + b.Name + " baseSize=" + Death::format("{}",b.BaseSize) +
					" instanceStride=" + Death::format("{}",b.InstanceStride) + "\n";
				for (const MemberInfo& m : b.Members) {
					out += "    member " + m.TypeName + " " + m.Name + ArraySuffix(m.ArraySize, m.SymbolicArray) +
						" offset=" + Death::format("{}",m.Offset) + "\n";
				}
			}
			for (const TextureInfo& t : r.Textures) {
				out += "  texture " + t.Name + " unit=" + Death::format("{}",t.Unit) + "\n";
			}
			for (const AttributeInfo& a : r.Attributes) {
				out += "  attribute " + a.TypeName + " " + a.Name + " location=" + Death::format("{}",a.Location) + "\n";
			}
		}
		return out;
	}
}
