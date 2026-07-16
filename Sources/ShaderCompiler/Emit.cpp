#include "Emit.h"
#include "Essl100.h"

#include <Base/Format.h>
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
		/** Emits one program (per-variant sources, reflection arrays, variant list and Program descriptor) into @p output */
		bool EmitProgram(const ShaderDocument& document, const std::vector<VariantReflection>& variants,
			String& output, Diagnostic& diag)
		{
			const String& program = document.ProgramName;

			for (const VariantReflection& v : variants) {
				// The unnamed base variant carries no infix ("Lighting_Vs"), named variants keep theirs ("Tinted_USE_PALETTE_Vs")
				const String prefix = (v.Name.empty() ? program : String(program + "_" + v.Name));
				const StageReflection& r = v.Reflection;

				for (std::int32_t stage = 0; stage < 2; stage++) {
					bool vertexStage = (stage == 0);
					String source = ShaderParser::BuildStageSource(document, vertexStage, v.Define);
					if (source.contains(")__SHDR__\""_s)) {
						diag.Message = "shader source contains the raw string terminator sequence )__SHDR__\"";
						diag.Line = 1;
						return false;
					}
					output += "\tinline constexpr char " + prefix + (vertexStage ? "_Vs" : "_Fs") + "[] =\n";
					output += "R\"__SHDR__(";
					output += source;
					output += ")__SHDR__\";\n";
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
					output += "\tinline constexpr char " + prefix + (vertexStage ? "_Vs100" : "_Fs100") + "[] =\n";
					output += "R\"__SHDR__(";
					output += es2source;
					output += ")__SHDR__\";\n";
					output += "\n";
				}

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
			for (const VariantReflection& v : variants) {
				const String prefix = (v.Name.empty() ? program : String(program + "_" + v.Name));
				const StageReflection& r = v.Reflection;
				output += "\t\t{ \"" + v.Name + "\", \"" + v.Define + "\", " + prefix + "_Vs, " + prefix + "_Fs,\n";
				output += "\t\t\t" + Death::format("{}", r.Uniforms.size()) + ", " + (r.Uniforms.empty() ? String("nullptr") : String(prefix + "_Uniforms")) + ", ";
				output += Death::format("{}", r.Blocks.size()) + ", " + (r.Blocks.empty() ? String("nullptr") : String(prefix + "_Blocks")) + ", ";
				output += Death::format("{}", r.Textures.size()) + ", " + (r.Textures.empty() ? String("nullptr") : String(prefix + "_Textures")) + ", ";
				output += Death::format("{}", r.Attributes.size()) + ", " + (r.Attributes.empty() ? String("nullptr") : String(prefix + "_Attributes")) + ",\n";
				output += "\t\t\t" + prefix + "_Vs100, " + prefix + "_Fs100 },\n";
			}
			output += "\t};\n";
			output += "\n";
			output += "\tinline constexpr ShaderCompiler::Program " + program + " = { \"" + program + "\", " +
				Death::format("{}",document.RenderModes) + ", " + Death::format("{}",variants.size()) + ", " + program + "_Variants };\n";
			return true;
		}
	}

	bool Emitter::EmitHeader(const std::vector<ProgramReflection>& programs,
		StringView ns, StringView inputFileName, String& output, Diagnostic& diag)
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
		output += "\n";
		output += "namespace " + ns + "\n";
		output += "{\n";

		for (std::size_t i = 0; i < programs.size(); i++) {
			if (i != 0) {
				output += "\n";
			}
			if (!EmitProgram(*programs[i].Document, programs[i].Variants, output, diag)) {
				return false;
			}
		}

		output += "}\n";
		return true;
	}

	String Emitter::BuildCheckDump(const ShaderDocument& document, const std::vector<VariantReflection>& variants)
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
