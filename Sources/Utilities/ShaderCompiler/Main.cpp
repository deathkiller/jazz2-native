/**
	@file Main.cpp

	Command-line entry point of ShaderCompiler — the offline shader preprocessor of
	Jazz² Resurrection. Reads an annotated ".shader" file, expands its variants,
	reflects the GLSL declarations of every variant (uniforms, std140 blocks with
	computed offsets/strides, texture bindings, vertex attributes) and emits a
	self-contained C++ header with the sources and reflection data, so the runtime
	does not need glGetActiveUniform introspection or double compilation of
	batched shaders.

	Usage:
	    ShaderCompiler <input.shader> -o <output.h> [-n <namespace>] [--check]

	With --check the tool parses the input and prints a human-readable reflection
	dump to stdout instead of writing the output header. With --essl100-check (or
	--target essl100) it prints the ESSL 100 (OpenGL ES 2.0) transform of every
	variant's stage sources to stdout, for inspection — a tool-only
	surface that does NOT change the emitted headers. Errors are reported to
	stderr as "<file>:<line>: error: <message>" and the exit code is non-zero.
*/

#include "Emit.h"
#include "Essl100.h"
#include "Hlsl.h"
#include "Vulkan.h"
#include "GlslToCpp.h"
#include "ConsoleFixedFunction.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include <Base/Format.h>
#include <Containers/GrowableArray.h>
#include <Containers/SmallVector.h>
#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>
#include <IO/Stream.h>
#include <Utf8.h>

#if defined(DEATH_TARGET_WINDOWS)
#	include <CommonWindows.h>
#	include <d3dcompiler.h>
#else
#	include <cerrno>
#	include <fcntl.h>
#	include <spawn.h>
#	include <sys/types.h>
#	include <sys/wait.h>
#	include <unistd.h>
// posix_spawn needs the caller's environment passed explicitly, and it is not declared by any header
extern char** environ;
#endif

using namespace ShaderCompiler;
using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace
{
	/** Reads the file at @p path in full; false when it cannot be opened or read to its end */
	bool ReadFileToString(StringView path, String& content)
	{
		std::unique_ptr<Stream> s = fs::Open(path, FileAccess::Read);
		if (s == nullptr || !s->IsValid()) {
			return false;
		}
		std::int64_t size = s->GetSize();
		if (size < 0) {
			return false;
		}
		content = String{NoInit, static_cast<std::size_t>(size)};
		return (size == 0 || s->Read(content.data(), size) == size);
	}

	/** Truncates the file at @p path to @p content, creating it when it does not exist yet */
	bool WriteStringToFile(StringView path, StringView content)
	{
		std::unique_ptr<Stream> s = fs::Open(path, FileAccess::Write);
		if (s == nullptr || !s->IsValid()) {
			return false;
		}
		std::int64_t size = static_cast<std::int64_t>(content.size());
		return (size == 0 || s->Write(content.data(), size) == size);
	}

	/**
		Collects the names (not paths) of the files in @p directory whose name ends with @p extension,
		sorted by a plain byte-wise comparison so the enumeration order — and therefore every aggregate
		artifact built from it — is identical on every machine, shell and locale.
	*/
	bool ListFilesInDirectory(StringView directory, StringView extension, SmallVectorImpl<String>& outNames)
	{
		if (!fs::DirectoryExists(directory)) {
			return false;
		}
		for (StringView path : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories | fs::EnumerationOptions::SkipSpecial)) {
			StringView name = fs::GetFileName(path);
			if (name.hasSuffix(extension)) {
				outNames.emplace_back(name);
			}
		}
		std::sort(outNames.begin(), outNames.end(), [](const String& a, const String& b) {
			return std::strcmp(a.data(), b.data()) < 0;
		});
		return true;
	}

	/** Identifies the running process, so concurrent tool invocations cannot collide in the temp directory */
	std::uint32_t CurrentProcessId()
	{
#if defined(DEATH_TARGET_WINDOWS)
		return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
		return static_cast<std::uint32_t>(::getpid());
#endif
	}

	/** Creates a fresh, empty directory below the system temp location for the staleness guard */
	bool CreateTemporaryDirectory(String& outPath)
	{
		String temp = fs::GetTempDirectory();
		if (temp.empty()) {
			return false;
		}
		for (std::uint32_t attempt = 0; attempt < 64; attempt++) {
			String candidate = fs::CombinePath(temp, Death::format("Jazz2-ShaderCheck-{:.8X}", CurrentProcessId() + attempt * 7919u));
			if (fs::Exists(candidate)) {
				continue;
			}
			if (fs::CreateDirectories(candidate)) {
				outPath = Death::move(candidate);
				return true;
			}
		}
		return false;
	}

	/**
		Builds the shared path prefix of one scratch file set below the system temp directory, unique per
		process and per @p serial. Empty when the temp directory cannot be resolved.
	*/
	String MakeTemporaryPathPrefix(StringView tag, unsigned serial)
	{
		String temp = fs::GetTempDirectory();
		if (temp.empty()) {
			return {};
		}
		return fs::CombinePath(temp, Death::format("{}_{}_{}", tag, CurrentProcessId(), serial));
	}

	/**
		Reads the environment variable @p name as UTF-8; false when it is not set or is empty.

		Windows keeps the environment in UTF-16, so the wide entry point is the only lossless one:
		GetEnvironmentVariableA transcodes through the active code page and mangles (or outright
		rejects) any path that is not representable in it, which the SDK and Program Files paths
		this is used for very much can be.
	*/
	bool TryGetEnvironmentVariable(StringView name, String& outValue)
	{
#if defined(DEATH_TARGET_WINDOWS)
		Array<wchar_t> nameW = Death::Utf8::ToUtf16(name);
		SmallVector<wchar_t, MAX_PATH> valueW(DefaultInit, MAX_PATH);
		DWORD length = ::GetEnvironmentVariableW(nameW, valueW.data(), DWORD(valueW.size()));
		if (length >= valueW.size()) {
			// The buffer was too small, the call reported how much is actually needed (including the terminator)
			valueW.resize_for_overwrite(length);
			length = ::GetEnvironmentVariableW(nameW, valueW.data(), DWORD(valueW.size()));
		}
		if (length == 0 || length >= valueW.size()) {
			return false;
		}
		outValue = Death::Utf8::FromUtf16(valueW.data(), std::int32_t(length));
		return !outValue.empty();
#else
		const char* value = std::getenv(String::nullTerminatedView(name).data());
		if (value == nullptr || value[0] == '\0') {
			return false;
		}
		outValue = value;
		return true;
#endif
	}

	/** Byte-compares two files; false when either cannot be read or their contents differ */
	bool FilesHaveEqualContent(StringView a, StringView b)
	{
		String left, right;
		if (!ReadFileToString(a, left) || !ReadFileToString(b, right)) {
			return false;
		}
		return (left.size() == right.size() && std::memcmp(left.data(), right.data(), left.size()) == 0);
	}

	bool IsValidNamespace(StringView ns)
	{
		if (ns.empty() || (ns[0] >= '0' && ns[0] <= '9')) {
			return false;
		}
		for (std::size_t i = 0; i < ns.size(); i++) {
			char c = ns[i];
			bool valid = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ':');
			if (!valid) {
				return false;
			}
		}
		return true;
	}

	void PrintUsage()
	{
		std::fprintf(stderr,
			"ShaderCompiler - offline shader variant expansion and reflection\n"
			"\n"
			"Usage: ShaderCompiler <input.shader> -o <output.h> [-n <namespace>] [--check]\n"
			"\n"
			"  -o <output.h>     Path of the generated C++ header\n"
			"  -n <namespace>    Namespace for the generated program data (default: ShaderArtifacts)\n"
			"  --check           Parse only and print a reflection dump to stdout (no output written)\n"
			"  --essl100-check   Print the ESSL 100 (OpenGL ES 2.0) transform of every stage to stdout\n"
			"  --hlsl            Print the HLSL (Shader Model 4/5) transform of every stage to stdout\n"
			"  --cg              Print the Cg transform of every stage to stdout (PS Vita / sceGxm dialect)\n"
			"  --no-dxbc         Embed HLSL sources instead of precompiled DXBC bytecode (default: DXBC via\n"
			"                    d3dcompiler_47 when available, with the HLSL text left out of the header)\n"
			"  --vulkan          Print the Vulkan GLSL (#version 450) transform of every stage to stdout\n"
			"  --glslang <path>  glslangValidator to compile SPIR-V with (default: VULKAN_SDK / PATH)\n"
			"  --cgcomp <path>   cgcomp to compile RSX microcode with (default: PS3DEV / PATH)\n"
			"  --target <t>      Selects an inspection target for the transform dump (only: essl100)\n"
			"\n"
			"Standalone modes:\n"
			"  --generate-all [--shaders-dir <dir>] [--out-dir <dir>] [--check] [--no-dxbc] [--glslang <path>] [--cgcomp <path>]\n"
			"                                                    Regenerate every committed artifact (the shared types, one\n"
			"                                                    header per shader, the umbrella and the five aggregates)\n"
			"                                                    from one enumeration of the shader directory; --check only\n"
			"                                                    compares and never writes into the tree. Both directories\n"
			"                                                    are auto-detected\n"
			"  --hlsl-check <input.shader ...>                    Emit + D3DCompile each stage as HLSL; print a pass/fail table\n"
			"  --spirv-check [--glslang <path>] <input.shader ...> Emit + glslang-compile each stage to SPIR-V; print a pass/fail table\n"
			"  --emit-cg <output.h> <input.shader ...>            Transform every stage to Cg into the PS Vita aggregate header\n"
			"  --emit-rsx <output.h> [--cgcomp <path>] <input.shader ...>  Compile every stage to RSX microcode (PlayStation 3)\n"
			"  --emit-fixed-function <pvr|gx|gu|gs> <output.h> <input.shader ...> Transpile fixed_function blocks into a per-backend aggregate header\n");
	}

	int ReportError(const char* inputPath, const Diagnostic& diag)
	{
		std::fprintf(stderr, "%s:%d: error: %s\n", inputPath, diag.Line, diag.Message.data());
		return 1;
	}

	/** Runs strip-comments + preprocess + reflect for one stage of one variant */
	bool ReflectVariantStage(const ShaderDocument& document, bool vertexStage, StringView define,
		StageReflection& result, Diagnostic& diag)
	{
		SmallVector<SourceLine, 0> lines = document.Prelude;
		const SmallVectorImpl<SourceLine>& stage = (vertexStage ? document.VertexLines : document.FragmentLines);
		lines.insert(lines.end(), stage.begin(), stage.end());

		ShaderParser::StripComments(lines);

		Preprocessor preprocessor;
		if (!define.empty()) {
			preprocessor.Define(define, "1");
		}
		SmallVector<SourceLine, 0> preprocessed;
		if (!preprocessor.Run(lines, preprocessed, diag)) {
			return false;
		}
		return GlslReflector::ReflectStage(preprocessed, vertexStage, result, diag);
	}

	/**
		Builds the ESSL 100 (OpenGL ES 2.0) transform dump printed by --essl100-check: one section
		per program / variant / stage, with either the transformed source or an "unsupported"
		diagnostic. Inspection-only — never touches the emitted header.
	*/
	String BuildEssl100Dump(const SmallVectorImpl<ProgramReflection>& programs)
	{
		String dump;
		for (const ProgramReflection& program : programs) {
			dump += "program " + program.Document->ProgramName + "\n";
			for (const VariantReflection& v : program.Variants) {
				dump += (v.Name.empty() ? String("variant (base)\n") : String("variant " + v.Name + "\n"));
				for (std::int32_t stage = 0; stage < 2; stage++) {
					bool vertexStage = (stage == 0);
					dump += (vertexStage ? "--- vertex (essl100) ---\n" : "--- fragment (essl100) ---\n");
					String modern = ShaderParser::BuildStageSource(*program.Document, vertexStage, v.Define);
					String es2;
					Diagnostic diag;
					if (Essl100Emitter::Transform(modern, vertexStage, es2, diag)) {
						dump += es2;
						if (!es2.empty() && es2.back() != '\n') {
							dump += "\n";
						}
					} else {
						dump += "essl100: " + diag.Message + " (line " + Death::format("{}", diag.Line) + ")\n";
					}
				}
			}
		}
		return dump;
	}

	/**
		Builds the HLSL (Shader Model 4/5) transform dump printed by --hlsl: one section per program /
		variant / stage, with either the emitted HLSL or an "unsupported" diagnostic. Inspection-only.
	*/
	/** Dumps the HLSL (@p dialect Hlsl) or the Cg (@p dialect Cg) transform of every stage of every variant */
	String BuildHlslDump(const SmallVectorImpl<ProgramReflection>& programs,
		HlslEmitter::Dialect dialect = HlslEmitter::Dialect::Hlsl)
	{
		const bool cg = (dialect == HlslEmitter::Dialect::Cg);
		const char* tag = (cg ? "cg" : "hlsl");
		String dump;
		for (const ProgramReflection& program : programs) {
			dump += "program " + program.Document->ProgramName + "\n";
			for (const VariantReflection& v : program.Variants) {
				dump += (v.Name.empty() ? String("variant (base)\n") : String("variant " + v.Name + "\n"));
				for (std::int32_t stage = 0; stage < 2; stage++) {
					bool vertexStage = (stage == 0);
					dump += String(vertexStage ? "--- vertex (" : "--- fragment (") + tag + ") ---\n";
					String modern = ShaderParser::BuildStageSource(*program.Document, vertexStage, v.Define);
					String transformed;
					Diagnostic diag;
					if (HlslEmitter::Transform(modern, vertexStage, v.Reflection, transformed, diag, dialect)) {
						dump += transformed;
						if (!transformed.empty() && transformed.back() != '\n') {
							dump += "\n";
						}
					} else {
						dump += String(tag) + ": " + diag.Message + " (line " + Death::format("{}", diag.Line) + ")\n";
					}
				}
			}
		}
		return dump;
	}

	/**
		Builds the Vulkan-flavored GLSL ("#version 450") transform dump printed by --vulkan: one section per
		program / variant / stage, with either the emitted Vulkan GLSL or an "unsupported" diagnostic.
		Inspection-only — never touches the emitted header, and does not require glslang.
	*/
	String BuildVulkanDump(const SmallVectorImpl<ProgramReflection>& programs)
	{
		String dump;
		for (const ProgramReflection& program : programs) {
			dump += "program " + program.Document->ProgramName + "\n";
			for (const VariantReflection& v : program.Variants) {
				dump += (v.Name.empty() ? String("variant (base)\n") : String("variant " + v.Name + "\n"));
				for (std::int32_t stage = 0; stage < 2; stage++) {
					bool vertexStage = (stage == 0);
					dump += (vertexStage ? "--- vertex (vulkan) ---\n" : "--- fragment (vulkan) ---\n");
					String modern = ShaderParser::BuildStageSource(*program.Document, vertexStage, v.Define);
					String vulkanGlsl;
					Diagnostic diag;
					if (VulkanGlslEmitter::Transform(modern, vertexStage, v.Reflection, vulkanGlsl, diag)) {
						dump += vulkanGlsl;
						if (!vulkanGlsl.empty() && vulkanGlsl.back() != '\n') {
							dump += "\n";
						}
					} else {
						dump += "vulkan: " + diag.Message + " (line " + Death::format("{}", diag.Line) + ")\n";
					}
				}
			}
		}
		return dump;
	}

	// --- SwGeneratedShaders.h emission (GLSL-to-C++ software fragment functions) -------------------

	/** One non-sampler uniform of a transpiled shader's "<Program>_Uniforms" struct */
	struct GeneratedUniformField
	{
		String Name;					// GLSL uniform name (== the emitted struct field name)
		std::uint32_t ComponentCount;	// 1 for float/int/bool, N for vecN/ivecN/bvecN
	};

	/** One shader the transpiler accepted, ready to be written into the aggregate header */
	struct GeneratedShaderEntry
	{
		String Prefix;								// program prefix, e.g. "Colorized" or "Tinted_USE_PALETTE"
		String Code;								// the transpiled struct + fragment function
		SmallVector<GeneratedUniformField, 0> Fields;	// non-sampler uniform layout of the struct
		bool HasComputeVaryings = false;			// a "<Prefix>_ComputeVaryings" was emitted (per-instance-constant varyings)
	};

	/** Scalar-component count of a reflected GLSL type (0 for matrices/structs/samplers - not a varying member) */
	std::uint32_t ComponentCountOfGlslType(GlslType t)
	{
		switch (t) {
			case GlslType::Float: case GlslType::Int: case GlslType::UInt: case GlslType::Bool: return 1;
			case GlslType::Vec2: case GlslType::IVec2: case GlslType::UVec2: case GlslType::BVec2: return 2;
			case GlslType::Vec3: case GlslType::IVec3: case GlslType::UVec3: case GlslType::BVec3: return 3;
			case GlslType::Vec4: case GlslType::IVec4: case GlslType::UVec4: case GlslType::BVec4: return 4;
			default: return 0;
		}
	}

	/**
		Flattens the per-instance std140 block members into the (name, offset, componentCount) table the
		constant-varying analysis reads. A batched program exposes its instance data as one struct-typed array
		member ("instances"); its element struct's fields are expanded, their offsets already being relative to
		one instance's start - exactly what the device's per-instance block pointer addresses. Matrix members
		(only used by gl_Position) are dropped.
	*/
	void BuildInstanceMembers(const StageReflection& reflection, SmallVectorImpl<GlslInstanceMember>& out)
	{
		for (const BlockInfo& block : reflection.Blocks) {
			for (const MemberInfo& m : block.Members) {
				if (m.Type == GlslType::Struct) {
					for (const StructInfo& s : reflection.Structs) {
						if (s.Name != m.TypeName) {
							continue;
						}
						for (const MemberInfo& f : s.Fields) {
							std::uint32_t cc = ComponentCountOfGlslType(f.Type);
							if (cc == 0) {
								continue;
							}
							GlslInstanceMember im;
							im.Name = f.Name;
							im.Offset = f.Offset;
							im.ComponentCount = cc;
							out.push_back(std::move(im));
						}
						break;
					}
				} else {
					std::uint32_t cc = ComponentCountOfGlslType(m.Type);
					if (cc == 0) {
						continue;
					}
					GlslInstanceMember im;
					im.Name = m.Name;
					im.Offset = m.Offset;
					im.ComponentCount = cc;
					out.push_back(std::move(im));
				}
			}
		}
	}

	/** Number of 4-byte components of an emitted C++ uniform field type (float/int/bool or vecN/ivecN/bvecN) */
	std::uint32_t ComponentCountFromType(StringView type)
	{
		if (type.empty()) {
			return 1;
		}
		switch (type.back()) {
			case '2': return 2;
			case '3': return 3;
			case '4': return 4;
			default: return 1;		// float / int / bool
		}
	}

	/**
		Parses the "struct <prefix>_Uniforms { ... };" the transpiler emitted at the head of @p code and
		records each field's name and component count, so the device can populate the struct generically.
		Reading the emitted struct (rather than the reflection) guarantees the field set and order match the
		compiled layout exactly - the fragment source seen by the transpiler may drop uniforms the merged
		reflection still lists (dead-code elimination keeps the reflection but strips unused per-stage decls).
	*/
	void ExtractUniformFields(StringView code, StringView prefix, SmallVectorImpl<GeneratedUniformField>& out)
	{
		constexpr StringView Blanks = " \t\r"_s;

		const String marker = "struct "_s + prefix + "_Uniforms"_s;
		StringView found = code.find(marker);
		if (found.empty()) {
			return;
		}
		StringView brace = code.suffix(found.end()).find('{');
		if (brace.empty()) {
			return;
		}
		for (StringView rawLine : code.suffix(brace.end()).split('\n')) {
			StringView line = rawLine.trimmed(Blanks);
			if (line.empty()) {
				continue;
			}
			if (line.front() == '}') {
				break;					// the closing "};"
			}
			if (line.back() != ';') {
				continue;
			}
			line = line.exceptSuffix(1).trimmed(Blanks);	// drop the trailing ';'
			StringView space = line.findLastAny(" \t"_s);
			if (space.empty()) {
				continue;
			}
			GeneratedUniformField f;
			f.Name = line.suffix(space.end());
			f.ComponentCount = ComponentCountFromType(line.prefix(space.begin()).trimmed(Blanks));
			out.push_back(std::move(f));
		}
	}

	/**
		Rejects transpiled code that would not compile against the software runtime, catching a known
		limitation of the transpiler that it does not detect itself (and cannot be fixed here):

		It always lowers the `vTexCoords` varying to the fragment's own 2-component texture coordinate
		`vec2(in.u, in.v)`. A shader that declares vTexCoords wider than `vec2` (e.g. LightingMesh packs data
		into a vec4 vTexCoords) and reads a 3rd/4th component would touch a component `sw::vec2`
		does not have. Such a shader also cannot be reproduced by the sprite-quad path anyway.

		(Helpers referencing the fragment input `in` used to be rejected here too, but helpers now take `in`
		as their first parameter and re-derive `unis`, so that case compiles and the check was removed.)
	*/
	bool EmittedFragmentIsCompilable(const String& code, StringView prefix, String& reason)
	{
		static_cast<void>(prefix);

		// vTexCoords lowered to vec2(in.u, in.v) must only be read with components sw::vec2 provides
		constexpr StringView Needle = "vec2(in.u, in.v)"_s;
		constexpr StringView SwizzleChars = "xyzwrgbastpq"_s;
		StringView rest = code;
		while (true) {
			StringView found = rest.find(Needle);
			if (found.empty()) {
				break;
			}
			rest = rest.suffix(found.end());
			if (rest.empty() || rest.front() != '.') {
				continue;
			}
			std::size_t length = 1;
			while (length < rest.size() && SwizzleChars.contains(rest[length])) {
				length++;
			}
			StringView sw = rest.slice(1, length);
			if (sw.empty()) {
				continue;
			}
			bool ok;
			if (sw.size() == 1) {
				ok = "xyrgst"_s.contains(sw[0]);
			} else {
				ok = (sw == "xy"_s || sw == "rg"_s);	// the only swizzle methods sw::vec2 provides
			}
			if (!ok) {
				reason = "reads '."_s + sw + "' of vTexCoords, which the software path only exposes as a 2D texture coordinate"_s;
				return false;
			}
		}
		return true;
	}

	/**
		Loads one ".shader" file and reflects every variant of every program it declares (offline flow).
		With @p strictTextureUnits a "texture_unit(N)" hint that matches no sampler is an error instead of
		being ignored — the aggregate emitters stay lenient (they only decline that one shader), while the
		header emission behind --generate-all rejects it exactly like the single-file mode does.
		@p outErrorLine receives the source line of a failure when the caller wants to report it.
	*/
	bool LoadProgramsForFile(const char* inputPath, SmallVectorImpl<ShaderDocument>& documents,
		SmallVectorImpl<ProgramReflection>& programs, String& errorMsg, bool strictTextureUnits = false,
		std::int32_t* outErrorLine = nullptr)
	{
		String content;
		if (!ReadFileToString(inputPath, content)) {
			errorMsg = "cannot read input file";
			return false;
		}
		{
			String includeError;
			FileReader reader = [](StringView path, String& out) {
				return ReadFileToString(path, out);
			};
			if (!ShaderParser::ExpandIncludes(content, ShaderParser::DirectoryOf(inputPath), reader, 0, includeError)) {
				errorMsg = includeError;
				return false;
			}
		}
		Diagnostic diag;
		if (!ShaderParser::ParseDocuments(content, documents, diag)) {
			errorMsg = diag.Message;
			if (outErrorLine != nullptr) {
				*outErrorLine = diag.Line;
			}
			return false;
		}
		programs.reserve(documents.size());
		for (const ShaderDocument& document : documents) {
			ProgramReflection program;
			program.Document = &document;
			program.Variants.emplace_back();
			for (const String& name : document.Variants) {
				VariantReflection v;
				v.Name = name;
				v.Define = name;
				program.Variants.push_back(std::move(v));
			}
			for (VariantReflection& v : program.Variants) {
				StageReflection vertex, fragment;
				if (!ReflectVariantStage(document, true, v.Define, vertex, diag) ||
					!ReflectVariantStage(document, false, v.Define, fragment, diag) ||
					!GlslReflector::MergeStages(vertex, fragment, v.Reflection, diag)) {
					errorMsg = diag.Message;
					if (outErrorLine != nullptr) {
						*outErrorLine = diag.Line;
					}
					return false;
				}
			}
			// Apply "texture_unit(N)" hints. Leniently by default - an unmatched hint just leaves the sampler
			// unassigned, which makes the transpiler decline that shader rather than fail the whole aggregate run
			for (const TextureDirective& directive : document.Textures) {
				bool found = false;
				for (VariantReflection& v : program.Variants) {
					for (TextureInfo& t : v.Reflection.Textures) {
						if (t.Name == directive.Name) {
							t.Unit = directive.Unit;
							found = true;
						}
					}
				}
				if (!found && strictTextureUnits) {
					errorMsg = "texture unit assignment \"" + directive.Name + "\" does not match any sampler uniform";
					if (outErrorLine != nullptr) {
						*outErrorLine = directive.Line;
					}
					return false;
				}
			}
			programs.push_back(std::move(program));
		}
		return true;
	}

	// --- HLSL validation via d3dcompiler_47's D3DCompile ------------------------------------------

#if defined(DEATH_TARGET_WINDOWS)
	pD3DCompile g_D3DCompile = nullptr;

	/** Loads d3dcompiler_47.dll (ships with Windows) and resolves D3DCompile; false if unavailable */
	bool LoadD3DCompiler(String& error)
	{
		if (g_D3DCompile != nullptr) {
			return true;
		}
		HMODULE mod = ::LoadLibraryW(L"d3dcompiler_47.dll");
		if (mod == nullptr) {
			error = "cannot load d3dcompiler_47.dll";
			return false;
		}
		g_D3DCompile = reinterpret_cast<pD3DCompile>(GetProcAddress(mod, "D3DCompile"));
		if (g_D3DCompile == nullptr) {
			error = "d3dcompiler_47.dll has no D3DCompile export";
			return false;
		}
		return true;
	}

	/**
		Compiles @p source as HLSL (entry @p entry, profile @p target) to DXBC bytes in @p dxbc; returns
		success + the compiler log. The flags match the D3D11 backend's runtime-compilation contract —
		column-major cbuffer matrix packing (the emitter's mul(M, v) column-vector algebra reads the engine's
		OpenGL-convention uniform data verbatim) and strictness — but with full optimization, since this runs
		offline where compile time doesn't matter.
	*/
	bool CompileHlslToDxbc(const String& source, const char* entry, const char* target, SmallVectorImpl<std::uint8_t>& dxbc, String& log)
	{
		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		const UINT flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
		HRESULT hr = g_D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
			entry, target, flags, 0, &code, &errors);
		if (errors != nullptr) {
			log = String{reinterpret_cast<const char*>(errors->GetBufferPointer())};
			errors->Release();
		}
		if (code != nullptr) {
			if (SUCCEEDED(hr)) {
				dxbc.assign(reinterpret_cast<const std::uint8_t*>(code->GetBufferPointer()),
					reinterpret_cast<const std::uint8_t*>(code->GetBufferPointer()) + code->GetBufferSize());
			}
			code->Release();
		}
		return SUCCEEDED(hr) && !dxbc.empty();
	}

	/** Compiles @p source as HLSL (entry @p entry, profile @p target); returns success + the compiler log */
	bool CompileHlsl(const String& source, const char* entry, const char* target, String& log)
	{
		SmallVector<std::uint8_t, 0> dxbc;
		return CompileHlslToDxbc(source, entry, target, dxbc, log);
	}

	// --- Vulkan SPIR-V compilation via a child glslangValidator process ---------------------------

	/** Matches @p name against @p pattern, where the pattern may contain "*" and "?" wildcards */
	bool MatchesWildcard(StringView pattern, StringView name)
	{
		constexpr std::size_t Npos = ~std::size_t{0};
		std::size_t p = 0, n = 0, star = Npos, mark = 0;
		while (n < name.size()) {
			if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == name[n])) {
				p++;
				n++;
			} else if (p < pattern.size() && pattern[p] == '*') {
				star = p++;
				mark = n;
			} else if (star != Npos) {
				p = star + 1;
				n = ++mark;
			} else {
				return false;
			}
		}
		while (p < pattern.size() && pattern[p] == '*') {
			p++;
		}
		return (p == pattern.size());
	}

	/**
		Resolves the first existing file matching @p pattern, whose components may contain wildcards
		("C:\...\Microsoft Visual Studio\*\*\...\glslangValidator.exe"). Only needed to dig the copy that
		ships inside a Visual Studio installation out of its version/edition/extension-id directories.
	*/
	bool ResolveWildcardPath(StringView pattern, String& outPath)
	{
		SmallVector<String, 0> pending;
		std::size_t start = 0;
		// Split off the (wildcard-free) root so the walk starts from a real directory
		for (std::size_t i = 0; i < pattern.size(); i++) {
			if (pattern[i] == '\\' || pattern[i] == '/') {
				StringView component = pattern.slice(start, i);
				if (component.contains('*') || component.contains('?')) {
					break;
				}
				start = i + 1;
			}
		}
		if (start == 0) {
			return false;
		}
		pending.emplace_back(pattern.prefix(start - 1));

		while (start < pattern.size()) {
			std::size_t end = start;
			while (end < pattern.size() && pattern[end] != '\\' && pattern[end] != '/') {
				end++;
			}
			StringView component = pattern.slice(start, end);
			const bool lastComponent = (end >= pattern.size());

			SmallVector<String, 0> next;
			for (const String& directory : pending) {
				if (!component.contains('*') && !component.contains('?')) {
					String candidate = fs::CombinePath(directory, component);
					if (lastComponent ? fs::FileExists(candidate) : fs::DirectoryExists(candidate)) {
						next.push_back(std::move(candidate));
					}
					continue;
				}
				// The last component names the file to find, every earlier one a directory to descend into
				for (StringView entry : fs::Directory(directory, lastComponent
						? fs::EnumerationOptions::SkipDirectories | fs::EnumerationOptions::SkipSpecial
						: fs::EnumerationOptions::SkipFiles | fs::EnumerationOptions::SkipSpecial)) {
					StringView name = fs::GetFileName(entry);
					if (MatchesWildcard(component, name)) {
						next.push_back(fs::CombinePath(directory, name));
					}
				}
			}
			if (next.empty()) {
				return false;
			}
			// Deterministic pick when several installations match
			std::sort(next.begin(), next.end(), [](const String& a, const String& b) {
				return std::strcmp(a.data(), b.data()) < 0;
			});
			pending = std::move(next);
			start = end + 1;
		}

		if (pending.empty()) {
			return false;
		}
		outPath = pending.front();
		return true;
	}

	/**
		Locates the offline SPIR-V compiler (glslangValidator). An explicit @p overridePath wins (and is an
		error when it does not exist); otherwise "%VULKAN_SDK%\Bin[32]\glslangValidator.exe", the executable
		search PATH, a Visual Studio-bundled copy and finally a repo-local build-tree copy below
		@p searchRoot are tried. Returns false when none is found — SPIR-V is then omitted (the Vulkan
		backend is not buildable), which the callers handle gracefully. glslang is only ever a BUILD-TIME
		dependency.
	*/
	bool LocateGlslang(StringView overridePath, String& outPath, StringView searchRoot = {})
	{
		if (!overridePath.empty()) {
			String candidate = overridePath;
			if (fs::FileExists(candidate)) {
				outPath = std::move(candidate);
				return true;
			}
			return false;
		}
		String sdk;
		if (TryGetEnvironmentVariable("VULKAN_SDK"_s, sdk)) {
			for (StringView relative : { "Bin\\glslangValidator.exe"_s, "Bin32\\glslangValidator.exe"_s }) {
				String candidate = fs::CombinePath(sdk, relative);
				if (fs::FileExists(candidate)) {
					outPath = std::move(candidate);
					return true;
				}
			}
		}
		wchar_t found[MAX_PATH];
		DWORD foundLength = ::SearchPathW(nullptr, L"glslangValidator.exe", nullptr, MAX_PATH, found, nullptr);
		if (foundLength > 0 && foundLength < MAX_PATH) {
			outPath = Death::Utf8::FromUtf16(found, std::int32_t(foundLength));
			return true;
		}
		// Visual Studio ships a copy with its shader tooling extension
		for (StringView variable : { "ProgramW6432"_s, "ProgramFiles"_s, "ProgramFiles(x86)"_s }) {
			String programFiles;
			if (!TryGetEnvironmentVariable(variable, programFiles)) {
				continue;
			}
			String pattern = fs::CombinePath(programFiles,
				"Microsoft Visual Studio\\*\\*\\Common7\\IDE\\Extensions\\*\\external\\glslangValidator.exe"_s);
			if (ResolveWildcardPath(pattern, outPath)) {
				return true;
			}
		}
		// Repo-local build-tree copy (may be transient)
		if (!searchRoot.empty()) {
			String candidate = fs::CombinePath(searchRoot, ".fake\\_legacy\\.fake\\glsl\\glslangValidator.exe"_s);
			if (fs::FileExists(candidate)) {
				outPath = std::move(candidate);
				return true;
			}
		}
		return false;
	}

	/**
		Runs @p commandLine with stdout+stderr redirected to @p logFile; false if the process could not start.
		Both are widened first: an SDK or Program Files path outside the active code page would otherwise be
		mangled on the way into the ANSI entry points.
	*/
	bool RunProcessCaptured(StringView commandLine, StringView logFile, DWORD& exitCode)
	{
		exitCode = ~DWORD{0};
		SECURITY_ATTRIBUTES security = {};
		security.nLength = sizeof(security);
		security.bInheritHandle = TRUE;
		HANDLE log = ::CreateFileW(Death::Utf8::ToUtf16(logFile), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
			&security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (log == INVALID_HANDLE_VALUE) {
			return false;
		}
		STARTUPINFOW startup = {};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
		startup.hStdOutput = log;
		startup.hStdError = log;
		PROCESS_INFORMATION process = {};
		// CreateProcessW may modify the command line in place, so it cannot be the read-only conversion result
		Array<wchar_t> commandLineW = Death::Utf8::ToUtf16(commandLine);
		SmallVector<wchar_t, 0> mutableCommand(InPlaceInit, commandLineW.begin(), commandLineW.end());
		mutableCommand.push_back(L'\0');
		BOOL created = ::CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
			nullptr, nullptr, &startup, &process);
		CloseHandle(log);
		if (!created) {
			return false;
		}
		WaitForSingleObject(process.hProcess, INFINITE);
		GetExitCodeProcess(process.hProcess, &exitCode);
		CloseHandle(process.hProcess);
		CloseHandle(process.hThread);
		return true;
	}

	/**
		Compiles @p source (Vulkan-flavored GLSL) to SPIR-V words via glslangValidator (child process, targeting
		SPIR-V for Vulkan 1.0 so the output is deterministic across glslang builds bar the generator id). Writes
		the stage to a temp file, runs the compiler with "-o", and reads the words back. Returns true and fills
		@p spirv on success; @p log receives the compiler output either way.
	*/
	bool CompileSpirvWithGlslang(const String& glslangPath, const String& source, bool vertexStage,
		SmallVectorImpl<std::uint32_t>& spirv, String& log)
	{
		spirv.clear();
		log = {};
		static unsigned counter = 0;
		String base = MakeTemporaryPathPrefix("sc_vk"_s, counter++);
		if (base.empty()) {
			log = "cannot resolve the temporary directory"_s;
			return false;
		}
		String inputPath = base + (vertexStage ? ".vert" : ".frag");
		String spirvPath = base + ".spv";
		String logPath = base + ".log";

		if (!WriteStringToFile(inputPath, source)) {
			log = "cannot write the temporary shader input";
			return false;
		}

		String command = "\""_s + glslangPath + "\" -V --target-env vulkan1.0 -S "_s +
			(vertexStage ? "vert"_s : "frag"_s) + " \""_s + inputPath + "\" -o \""_s + spirvPath + "\""_s;
		DWORD exitCode = ~DWORD{0};
		bool ran = RunProcessCaptured(command, logPath, exitCode);
		{
			String logContent;
			if (ReadFileToString(logPath, logContent)) {
				log = std::move(logContent);
			}
		}
		bool ok = ran && (exitCode == 0);
		if (ok) {
			String bytes;
			if (ReadFileToString(spirvPath, bytes) && bytes.size() >= 4 && (bytes.size() % 4) == 0) {
				std::size_t words = bytes.size() / 4;
				spirv.resize(words);
				std::memcpy(spirv.data(), bytes.data(), words * 4);
				if (spirv.empty() || spirv[0] != 0x07230203u) {		// SPIR-V magic number
					spirv.clear();
					ok = false;
					if (log.empty()) {
						log = "glslang output is not a SPIR-V module";
					}
				}
			} else {
				ok = false;
				if (log.empty()) {
					log = "glslang produced no SPIR-V output";
				}
			}
		}
		fs::RemoveFile(inputPath);
		fs::RemoveFile(spirvPath);
		fs::RemoveFile(logPath);
		return ok && !spirv.empty();
	}
#else
	// D3DCompile is a Windows DLL entry point with no counterpart elsewhere, so the HLSL/DXBC paths stay
	// unavailable; the child-process ones below do not have that excuse and are implemented for real.
	bool LoadD3DCompiler(String& error) { error = "D3DCompile is only available on Windows"; return false; }
	bool CompileHlslToDxbc(const String&, const char*, const char*, SmallVectorImpl<std::uint8_t>&, String&) { return false; }
	bool CompileHlsl(const String&, const char*, const char*, String&) { return false; }

	/** Runs @p argv with stdout+stderr redirected to @p logFile; false if the process could not start */
	bool RunProcessCaptured(const SmallVectorImpl<String>& argv, StringView logFile, int& exitCode)
	{
		exitCode = -1;
		if (argv.empty()) {
			return false;
		}

		SmallVector<char*, 0> args;
		args.reserve(argv.size() + 1);
		for (const String& a : argv) {
			args.push_back(const_cast<char*>(a.data()));
		}
		args.push_back(nullptr);

		posix_spawn_file_actions_t actions;
		if (posix_spawn_file_actions_init(&actions) != 0) {
			return false;
		}
		// Both streams go to the same file, so a compiler that writes diagnostics to either is captured
		posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, String::nullTerminatedView(logFile).data(),
			O_WRONLY | O_CREAT | O_TRUNC, 0644);
		posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

		pid_t pid = 0;
		// posix_spawnp so a bare tool name is resolved against PATH, matching how the Windows arm behaves
		const int spawned = ::posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), ::environ);
		posix_spawn_file_actions_destroy(&actions);
		if (spawned != 0) {
			return false;
		}

		int status = 0;
		while (::waitpid(pid, &status, 0) < 0) {
			if (errno != EINTR) {
				return false;
			}
		}
		exitCode = (WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return true;
	}

	/** Resolves `glslangValidator`, from @p explicitPath, then `$VULKAN_SDK/bin`, then `PATH` */
	bool LocateGlslang(StringView explicitPath, String& outPath, StringView = {})
	{
		if (!explicitPath.empty()) {
			outPath = String{explicitPath};
			return fs::FileExists(outPath);
		}
		String sdk;
		if (TryGetEnvironmentVariable("VULKAN_SDK"_s, sdk)) {
			String candidate = fs::CombinePath({ sdk, "bin"_s, "glslangValidator"_s });
			if (fs::FileExists(candidate)) {
				outPath = std::move(candidate);
				return true;
			}
		}
		// Left as a bare name for posix_spawnp to resolve against PATH
		outPath = "glslangValidator"_s;
		return true;
	}

	bool CompileSpirvWithGlslang(const String& glslangPath, const String& source, bool vertexStage,
		SmallVectorImpl<std::uint32_t>& spirv, String& log)
	{
		spirv.clear();
		log = {};
		static unsigned counter = 0;
		String base = MakeTemporaryPathPrefix("sc_vk"_s, counter++);
		if (base.empty()) {
			log = "cannot resolve the temporary directory"_s;
			return false;
		}
		String inputPath = base + (vertexStage ? ".vert"_s : ".frag"_s);
		String spirvPath = base + ".spv"_s;
		String logPath = base + ".log"_s;

		if (!WriteStringToFile(inputPath, source)) {
			log = "cannot write the temporary shader input";
			return false;
		}

		SmallVector<String, 0> argv{InPlaceInit, { glslangPath, "-V"_s, "--target-env"_s, "vulkan1.0"_s, "-S"_s,
			(vertexStage ? "vert"_s : "frag"_s), inputPath, "-o"_s, spirvPath }};
		int exitCode = -1;
		const bool ran = RunProcessCaptured(argv, logPath, exitCode);
		{
			String logContent;
			if (ReadFileToString(logPath, logContent)) {
				log = std::move(logContent);
			}
		}
		bool ok = ran && (exitCode == 0);
		if (ok) {
			String bytes;
			if (ReadFileToString(spirvPath, bytes) && bytes.size() >= 4 && (bytes.size() % 4) == 0) {
				std::size_t words = bytes.size() / 4;
				spirv.resize(words);
				std::memcpy(spirv.data(), bytes.data(), words * 4);
				if (spirv.empty() || spirv[0] != 0x07230203u) {		// SPIR-V magic number
					spirv.clear();
					ok = false;
					if (log.empty()) {
						log = "glslang output is not a SPIR-V module";
					}
				}
			} else {
				ok = false;
				if (log.empty()) {
					log = "glslang produced no SPIR-V output";
				}
			}
		}
		fs::RemoveFile(inputPath);
		fs::RemoveFile(spirvPath);
		fs::RemoveFile(logPath);
		return ok && !spirv.empty();
	}
#endif

	/** Collapses a multi-line compiler log to a single line for the summary table */
	String FirstLine(StringView s)
	{
		std::size_t nl = 0;
		while (nl < s.size() && s[nl] != '\n' && s[nl] != '\r') {
			nl++;
		}
		return String{s.prefix(nl)};
	}

	/**
		Emits the VS + PS HLSL of every program variant across all @p inputPaths and compiles each stage via
		D3DCompile (vs_4_0 / ps_4_0 with the same flags the embedded DXBC and the D3D11 backend's runtime
		compilation use). Prints a pass/fail table and a summary. Tool-only validation surface.
	*/
	int RunHlslCheck(char** inputPaths, int inputCount)
	{
		String loadError;
		if (!LoadD3DCompiler(loadError)) {
			std::fprintf(stderr, "error: %s\n", loadError.data());
			return 1;
		}

		std::size_t stagesTotal = 0, stagesPassed = 0;
		std::size_t variantsTotal = 0, variantsPassed = 0;
		SmallVector<String, 0> failures;		// detailed "prefix STAGE: <reason>" lines

		for (int fi = 0; fi < inputCount; fi++) {
			const char* inputPath = inputPaths[fi];
			SmallVector<ShaderDocument, 0> documents;
			SmallVector<ProgramReflection, 0> programs;
			String errorMsg;
			if (!LoadProgramsForFile(inputPath, documents, programs, errorMsg)) {
				std::fprintf(stderr, "%s: error: %s\n", inputPath, errorMsg.data());
				return 1;
			}
			for (const ProgramReflection& program : programs) {
				const String& progName = program.Document->ProgramName;
				for (const VariantReflection& v : program.Variants) {
					String prefix = (v.Name.empty() ? progName : String(progName + "_" + v.Name));
					variantsTotal++;
					bool bothOk = true;
					String rowStatus;
					for (std::int32_t stage = 0; stage < 2; stage++) {
						bool vertexStage = (stage == 0);
						const char* stageName = (vertexStage ? "VS" : "PS");
						const char* entry = (vertexStage ? "VSMain" : "PSMain");
						const char* target = (vertexStage ? "vs_4_0" : "ps_4_0");
						stagesTotal++;

						String source = ShaderParser::BuildStageSource(*program.Document, vertexStage, v.Define);
						String hlsl;
						Diagnostic diag;
						bool ok;
						String reason;
						if (!HlslEmitter::Transform(source, vertexStage, v.Reflection, hlsl, diag)) {
							ok = false;
							reason = diag.Message;
						} else {
							String log;
							ok = CompileHlsl(hlsl, entry, target, log);
							if (!ok) {
								reason = "D3DCompile: " + FirstLine(log);
							}
						}
						if (ok) {
							stagesPassed++;
							rowStatus += String(" ") + stageName + "=PASS";
						} else {
							bothOk = false;
							rowStatus += String(" ") + stageName + "=FAIL";
							failures.push_back(prefix + " " + stageName + ": " + reason);
						}
					}
					if (bothOk) {
						variantsPassed++;
					}
					std::fprintf(stdout, "  %-42s%s\n", prefix.data(), rowStatus.data());
				}
			}
		}

		std::fprintf(stdout, "\n[HlslCheck] %zu/%zu program-variants compiled both stages; %zu/%zu stages compiled\n",
			variantsPassed, variantsTotal, stagesPassed, stagesTotal);
		if (!failures.empty()) {
			std::fprintf(stdout, "Failures (%zu):\n", failures.size());
			for (const String& f : failures) {
				std::fprintf(stdout, "  %s\n", f.data());
			}
		}
		return 0;
	}

	/**
		Emits the VS + FS Vulkan-flavored GLSL of every program variant across all @p inputPaths and compiles
		each stage to SPIR-V via glslangValidator (Vulkan 1.0 target). Prints a pass/fail table and a summary.
		Tool-only validation surface mirroring --hlsl-check. Optional leading "--glslang <path>" overrides the
		compiler discovery. Exits non-zero when glslang cannot be located.
	*/
	int RunSpirvCheck(char** args, int count)
	{
		StringView glslangOverride;
		int start = 0;
		while (start < count && StringView(args[start]) == "--glslang") {
			if (start + 1 >= count) {
				std::fprintf(stderr, "error: --glslang requires a path argument\n");
				return 2;
			}
			glslangOverride = args[start + 1];
			start += 2;
		}
		if (start >= count) {
			std::fprintf(stderr, "error: --spirv-check requires at least one input .shader\n");
			return 2;
		}

		String glslang;
		if (!LocateGlslang(glslangOverride, glslang)) {
			std::fprintf(stderr, "error: glslang not found, cannot produce SPIR-V.\n"
				"       Install the Vulkan SDK (sets VULKAN_SDK), put glslangValidator on PATH, or pass --glslang <path>.\n");
			return 1;
		}
		std::fprintf(stdout, "[SpirvCheck] using glslang: %s\n", glslang.data());

		std::size_t stagesTotal = 0, stagesPassed = 0;
		std::size_t variantsTotal = 0, variantsPassed = 0;
		SmallVector<String, 0> failures;

		for (int fi = start; fi < count; fi++) {
			const char* inputPath = args[fi];
			SmallVector<ShaderDocument, 0> documents;
			SmallVector<ProgramReflection, 0> programs;
			String errorMsg;
			if (!LoadProgramsForFile(inputPath, documents, programs, errorMsg)) {
				std::fprintf(stderr, "%s: error: %s\n", inputPath, errorMsg.data());
				return 1;
			}
			for (const ProgramReflection& program : programs) {
				const String& progName = program.Document->ProgramName;
				for (const VariantReflection& v : program.Variants) {
					String prefix = (v.Name.empty() ? progName : String(progName + "_" + v.Name));
					variantsTotal++;
					bool bothOk = true;
					String rowStatus;
					for (std::int32_t stage = 0; stage < 2; stage++) {
						bool vertexStage = (stage == 0);
						const char* stageName = (vertexStage ? "VS" : "FS");
						stagesTotal++;

						String source = ShaderParser::BuildStageSource(*program.Document, vertexStage, v.Define);
						String vulkanGlsl;
						Diagnostic diag;
						bool ok;
						String reason;
						if (!VulkanGlslEmitter::Transform(source, vertexStage, v.Reflection, vulkanGlsl, diag)) {
							ok = false;
							reason = diag.Message;
						} else {
							SmallVector<std::uint32_t, 0> spirv;
							String log;
							ok = CompileSpirvWithGlslang(glslang, vulkanGlsl, vertexStage, spirv, log);
							if (!ok) {
								reason = "glslang: " + FirstLine(log);
							}
						}
						if (ok) {
							stagesPassed++;
							rowStatus += String(" ") + stageName + "=PASS";
						} else {
							bothOk = false;
							rowStatus += String(" ") + stageName + "=FAIL";
							failures.push_back(prefix + " " + stageName + ": " + reason);
						}
					}
					if (bothOk) {
						variantsPassed++;
					}
					std::fprintf(stdout, "  %-42s%s\n", prefix.data(), rowStatus.data());
				}
			}
		}

		std::fprintf(stdout, "\n[SpirvCheck] %zu/%zu program-variants compiled both stages; %zu/%zu stages compiled\n",
			variantsPassed, variantsTotal, stagesPassed, stagesTotal);
		if (!failures.empty()) {
			std::fprintf(stdout, "Failures (%zu):\n", failures.size());
			for (const String& f : failures) {
				std::fprintf(stdout, "  %s\n", f.data());
			}
		}
		return 0;
	}

	/** Builds the aggregate "SwGeneratedShaders.h" contents from the accepted shaders */
	String BuildSwGeneratedHeader(const SmallVectorImpl<GeneratedShaderEntry>& supported)
	{
		String out;
		out += "// Generated by ShaderCompiler (--emit-sw-generated). Do not edit manually.\n";
		out += "#pragma once\n\n";
		out += "#if defined(WITH_RHI_SOFTWARE)\n\n";
		out += "#include \"../../nCine/Graphics/RHI/Software/SwShaderRuntime.h\"\n\n";
		out += "#include <cstddef>\n";
		out += "#include <cstdint>\n";
		out += "#include <cstring>\n\n";
		out += "namespace nCine::RHI::Software\n{\n";
		// The transpiled fragment functions are plain (non-inline) free functions. Wrapping the whole payload
		// in an anonymous namespace gives them internal linkage so the header is ODR-safe even if it is ever
		// included by more than one translation unit (today only SwDevice.cpp includes it).
		out += "\tnamespace\n\t{\n";

		for (const GeneratedShaderEntry& e : supported) {
			out += "\t\t// --- " + e.Prefix + " ---\n";
			out += e.Code;
			if (e.Code.size() != 0 && e.Code[e.Code.size() - 1] != '\n') {
				out += "\n";
			}
			out += "\n";
		}

		out += "\t\tstruct SwGeneratedUniformField { const char* name; std::uint32_t offset; std::uint32_t componentCount; };\n";
		// computeVaryings is null unless the shader reads per-instance-constant varyings; the device calls it
		// once per instance (with that instance's block pointer) to fill those varyings before the draw
		out += "\t\tusing SwGeneratedComputeVaryingsFn = void (*)(void* inputs, const std::uint8_t* instanceBlock);\n";
		out += "\t\tstruct SwGeneratedShaderInfo { const char* name; nCine::RHI::Software::FragmentShaderFn fragment; std::uint32_t uniformsSize; const SwGeneratedUniformField* uniformFields; std::uint32_t uniformFieldCount; SwGeneratedComputeVaryingsFn computeVaryings; };\n\n";

		for (const GeneratedShaderEntry& e : supported) {
			if (e.Fields.empty()) {
				continue;
			}
			out += "\t\tconst SwGeneratedUniformField " + e.Prefix + "_Fields[] = {\n";
			for (const GeneratedUniformField& f : e.Fields) {
				out += "\t\t\t{ \"" + f.Name + "\", (std::uint32_t)offsetof(" + e.Prefix + "_Uniforms, " + f.Name + "), " +
					Death::format("{}", f.ComponentCount) + " },\n";
			}
			out += "\t\t};\n";
		}
		out += "\n";

		if (supported.empty()) {
			out += "\t\tconst SwGeneratedShaderInfo* FindGeneratedShader(const char*) { return nullptr; }\n";
		} else {
			out += "\t\tconst SwGeneratedShaderInfo SwGeneratedShaders[] = {\n";
			for (const GeneratedShaderEntry& e : supported) {
				String fieldsPtr = (e.Fields.empty() ? String("nullptr") : String(e.Prefix + "_Fields"));
				String computeVaryingsPtr = (e.HasComputeVaryings ? String("&" + e.Prefix + "_ComputeVaryings") : String("nullptr"));
				out += "\t\t\t{ \"" + e.Prefix + "\", &" + e.Prefix + "_Fragment, (std::uint32_t)sizeof(" + e.Prefix + "_Uniforms), " +
					fieldsPtr + ", " + Death::format("{}", e.Fields.size()) + ", " + computeVaryingsPtr + " },\n";
			}
			out += "\t\t};\n\n";
			out += "\t\tconst SwGeneratedShaderInfo* FindGeneratedShader(const char* name)\n\t\t{\n";
			out += "\t\t\tif (name == nullptr) {\n\t\t\t\treturn nullptr;\n\t\t\t}\n";
			out += "\t\t\t// Exact match first: for most programs the runtime object label (the shader name the\n";
			out += "\t\t\t// content pipeline registers) equals the generated prefix verbatim.\n";
			out += "\t\t\tfor (const SwGeneratedShaderInfo& info : SwGeneratedShaders) {\n";
			out += "\t\t\t\tif (std::strcmp(info.name, name) == 0) {\n\t\t\t\t\treturn &info;\n\t\t\t\t}\n";
			out += "\t\t\t}\n";
			out += "\t\t\t// Otherwise match ignoring '_' and letter case, so a variant label that bakes the variant\n";
			out += "\t\t\t// into the shader name (e.g. \"TexturedBackgroundDither\") still resolves to the generated\n";
			out += "\t\t\t// prefix that separates it with an underscore and upper-cases the define (\"..._DITHER\").\n";
			out += "\t\t\t// A \"use\" token is also skipped on both sides so a \"USE_PALETTE\" define matches a runtime\n";
			out += "\t\t\t// label that spells the same variant as \"...Palette\" (the engine drops the \"USE_\" prefix).\n";
			out += "\t\t\tfor (const SwGeneratedShaderInfo& info : SwGeneratedShaders) {\n";
			out += "\t\t\t\tconst char* a = info.name;\n";
			out += "\t\t\t\tconst char* b = name;\n";
			out += "\t\t\t\tbool equal = true;\n";
			out += "\t\t\t\tfor (;;) {\n";
			out += "\t\t\t\t\twhile (*a == '_') { a++; }\n";
			out += "\t\t\t\t\twhile (*b == '_') { b++; }\n";
			out += "\t\t\t\t\tif ((a[0] == 'u' || a[0] == 'U') && (a[1] == 's' || a[1] == 'S') && (a[2] == 'e' || a[2] == 'E')) { a += 3; continue; }\n";
			out += "\t\t\t\t\tif ((b[0] == 'u' || b[0] == 'U') && (b[1] == 's' || b[1] == 'S') && (b[2] == 'e' || b[2] == 'E')) { b += 3; continue; }\n";
			out += "\t\t\t\t\tchar ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;\n";
			out += "\t\t\t\t\tchar cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;\n";
			out += "\t\t\t\t\tif (ca != cb) { equal = false; break; }\n";
			out += "\t\t\t\t\tif (ca == '\\0') { break; }\n";
			out += "\t\t\t\t\ta++; b++;\n";
			out += "\t\t\t\t}\n";
			out += "\t\t\t\tif (equal) {\n\t\t\t\t\treturn &info;\n\t\t\t\t}\n";
			out += "\t\t\t}\n";
			out += "\t\t\treturn nullptr;\n\t\t}\n";
		}

		out += "\t}\n";	// anonymous namespace
		out += "}\n";		// namespace nCine::RHI::Software
		out += "\n#endif\n";
		return out;
	}

	// --- Fixed-function aggregate emission (PvrGeneratedEffects.h / GxGeneratedEffects.h) ----------

	/** One (program, variant) table row of the aggregate header */
	struct GeneratedEffectEntry
	{
		String Program;		// program name, e.g. "WhiteMask"
		String Variant;		// variant define name ("" for the base variant)
		String Prefix;		// mangled prefix, e.g. "WhiteMask" or "WhiteMask_USE_PALETTE" (for the summary log)
		String Intrinsic;	// FixedFunctionIntrinsic member name from a "pipeline <name>;" block ("" = none)
		bool UsesOffsetColor = false;	// whether the function can ever write a pass offset colour
		FixedFunctionRequirements Requirements = FixedFunctionRequirements::None;	// optional context facilities the function calls
		std::int32_t FnIndex = -1;		// index into the unique-function list (-1 for a pipeline intrinsic)
	};

	/**
		One UNIQUE emitted effect function. Many (program, variant) blocks transpile to byte-identical
		bodies - batched twins and USE_PALETTE variants differ in the dispatch loop's instance decoding,
		not in their pass code - so the aggregate emits each distinct body once, named after its first
		occurrence, and every matching table row points at the shared function.
	*/
	struct GeneratedEffectFunction
	{
		String Name;			// function name, "<first prefix>_Effect"
		String Body;			// the emitted body (statements at 3-tab indent, no signature) - the dedup key
		String Provenance;		// "<File>:fixed_function(<target>)" of the first occurrence
		SmallVector<String, 0> SharedBy;			// "<Program>" / "<Program> (<VARIANT>)" per sharing row, in table order
		SmallVector<String, 0> SharedProvenance;	// each sharer's own provenance (programs from other files can share too)
	};

	/** Renders a requirements bitmask as the runtime enum's member spelling for the generated table */
	String RequirementsSpelling(FixedFunctionRequirements reqs)
	{
		if (std::uint8_t(reqs) == 0) {
			return String{"FixedFunctionRequirements::None"};
		}
		static const std::pair<FixedFunctionRequirements, const char*> bits[] = {
			{ FixedFunctionRequirements::NeedsTexelStep, "NeedsTexelStep" },
			{ FixedFunctionRequirements::NeedsUniforms, "NeedsUniforms" },
			{ FixedFunctionRequirements::NeedsStripBuilder, "NeedsStripBuilder" },
			{ FixedFunctionRequirements::NeedsQuadAxes, "NeedsQuadAxes" },
		};
		String out;
		for (const auto& bit : bits) {
			if ((std::uint8_t(reqs) & std::uint8_t(bit.first)) != 0) {
				if (!out.empty()) {
					out += " | ";
				}
				out += "FixedFunctionRequirements::";
				out += bit.second;
			}
		}
		return out;
	}

	/** Returns the file-name part of @p path (for the provenance comments in the generated header) */
	String BaseNameOf(StringView path)
	{
		std::size_t begin = 0;
		for (std::size_t i = 0; i < path.size(); i++) {
			if (path[i] == '/' || path[i] == '\\') {
				begin = i + 1;
			}
		}
		return String{path.exceptPrefix(begin)};
	}

	/** Builds the aggregate "PvrGeneratedEffects.h" / "GxGeneratedEffects.h" / "GuGeneratedEffects.h" / "GsGeneratedEffects.h" contents from the transpiled effects */
	String BuildFixedFunctionHeader(FixedFunctionBackend backend, const SmallVectorImpl<GeneratedEffectEntry>& entries,
		const SmallVectorImpl<GeneratedEffectFunction>& functions)
	{
		// The mode argument, the build guard and the namespace of one backend. Every target is named
		// after the RENDERING BACKEND rather than after the console it runs on, so the block target, the
		// mode argument and the engine-side namespace always agree: "pvr" is the Dreamcast's chip, "gx"
		// the Wii/GameCube one, "gu" the sceGu library that drives the PSP's Graphics Engine, and "gs"
		// the PlayStation 2's Graphics Synthesizer.
		const char* backendArg;
		const char* guard;
		const char* ns;
		switch (backend) {
			case FixedFunctionBackend::Gx:
				backendArg = "gx"; guard = "WITH_RHI_GX"; ns = "nCine::RHI::GX";
				break;
			case FixedFunctionBackend::Gu:
				backendArg = "gu"; guard = "WITH_RHI_GU"; ns = "nCine::RHI::GU";
				break;
			case FixedFunctionBackend::Gs:
				backendArg = "gs"; guard = "WITH_RHI_GS"; ns = "nCine::RHI::GS";
				break;
			default:
				backendArg = "pvr"; guard = "WITH_RHI_PVR"; ns = "nCine::RHI::PVR";
				break;
		}

		String out;
		out += "// Generated by ShaderCompiler (--emit-fixed-function "_s + backendArg + "). Do not edit manually.\n"_s;
		out += "#pragma once\n\n";
		out += "#if defined("_s + guard + ")\n\n"_s;
		out += "#include \"../../nCine/Graphics/RHI/FixedFunctionPass.h\"\n\n";
		out += "#include <cmath>\n";
		out += "#include <cstddef>\n\n";
		out += "// The including device file provides the concrete EffectContext (see the contract in\n";
		out += "// FixedFunctionPass.h) with a \"using EffectContext = ...;\" alias BEFORE this include.\n\n";
		out += "namespace "_s + ns + "\n{\n"_s;
		out += "\t/**\n";
		out += "\t\tOne generated fixed-function effect: the (program, variant) it implements, whether any of\n";
		out += "\t\tits passes can write an offset colour (the PVR compiles specular into the base polygon\n";
		out += "\t\theader, so this is needed before any pass runs), which optional EffectContext facilities\n";
		out += "\t\tthe function can ever call (so Dispatch skips the setup for the rest), and either its\n";
		out += "\t\tfunction or the backend pipeline stage a \"pipeline <name>;\" block bound it to (never\n";
		out += "\t\tboth). Byte-identical bodies are emitted once and shared by all their rows.\n\n";
		out += "\t\tDefined at namespace scope (unlike the functions and the table below) so the backend's\n";
		out += "\t\tShaderProgram can forward-declare it and store a resolved entry pointer; only this\n";
		out += "\t\ttranslation unit ever sees the definition.\n";
		out += "\t*/\n";
		out += "\tstruct FixedFunctionGeneratedEffect { const char* Program; const char* Variant; bool UsesOffsetColor; FixedFunctionRequirements Requirements; FixedFunctionIntrinsic Intrinsic; void (*Fn)(EffectContext&); };\n\n";
		// Like SwGeneratedShaders.h, the payload lives in an anonymous namespace: the generated
		// effect functions get internal linkage, so the header is ODR-safe even if it is ever included
		// by more than one translation unit (today only the backend's device file includes it).
		out += "\tnamespace\n\t{\n";
		out += ConsoleFixedFunction::BuildRuntimeSupport();
		out += "\n";

		for (const GeneratedEffectFunction& fn : functions) {
			// The provenance comment names the first occurrence and, when the body deduplicated,
			// every (program, variant) sharing the function - a sharer from another .shader file
			// carries its own provenance in brackets so the mapping back to source stays obvious
			out += "\t\t// "_s + fn.SharedBy[0] + " - from "_s + fn.Provenance + "\n"_s;
			if (fn.SharedBy.size() > 1) {
				out += "\t\t// Shared by: ";
				for (std::size_t i = 0; i < fn.SharedBy.size(); i++) {
					if (i != 0) {
						out += ", ";
					}
					out += fn.SharedBy[i];
					if (fn.SharedProvenance[i] != fn.Provenance) {
						out += " ["_s + fn.SharedProvenance[i] + "]"_s;
					}
				}
				out += "\n";
			}
			out += "\t\tvoid "_s + fn.Name + "(EffectContext& ctx)\n\t\t{\n"_s;
			out += fn.Body;
			out += "\t\t}\n\n";
		}

		if (entries.empty()) {
			// A zero-length array is not valid C++, so the empty set degrades to a null table pointer
			out += "\t\t// No program in the input set carries an applicable fixed_function block\n";
			out += "\t\tconstexpr const FixedFunctionGeneratedEffect* FixedFunctionGeneratedEffects = nullptr;\n";
			out += "\t\tconstexpr std::size_t FixedFunctionGeneratedEffectCount = 0;\n";
		} else {
			out += "\t\tconstexpr FixedFunctionGeneratedEffect FixedFunctionGeneratedEffects[] = {\n";
			for (const GeneratedEffectEntry& e : entries) {
				out += "\t\t\t{ \""_s + e.Program + "\", \""_s + e.Variant + "\", "_s +
					(e.UsesOffsetColor ? "true"_s : "false"_s) + ", "_s +
					RequirementsSpelling(e.Requirements) + ", FixedFunctionIntrinsic::"_s +
					(e.Intrinsic.empty() ? StringView{"None"} : StringView{e.Intrinsic}) + ", "_s;
				if (e.FnIndex >= 0) {
					out += "&"_s + functions[std::size_t(e.FnIndex)].Name;
				} else {
					out += "nullptr";
				}
				out += " },\n";
			}
			out += "\t\t};\n";
			out += "\t\tconstexpr std::size_t FixedFunctionGeneratedEffectCount = sizeof(FixedFunctionGeneratedEffects) / sizeof(FixedFunctionGeneratedEffects[0]);\n";
		}

		out += "\t}\n";	// anonymous namespace
		out += "}\n";		// backend namespace
		out += "\n#endif\n";
		return out;
	}

	/**
		Transpiles the applicable fixed_function block of every program variant across all @p inputPaths to
		C++ and writes one per-backend aggregate header. Backend selection: a block naming this backend —
		alone (`pvr`) or in a target list (`pvr, gu`) — overrides the generic fixed_function block for it;
		programs with no applicable block are simply absent from the emitted table. Unlike the software
		transpiler (which declines unsupported shaders), any error inside a block is FATAL, reported as
		"<file>:<line>: error: ..." — the block is authored intent.
	*/
	int RunEmitFixedFunction(const char* backendName, const char* outputPath, char** inputPaths, int inputCount)
	{
		FixedFunctionBackend backend;
		FixedFunctionTarget overrideTarget;
		if (StringView(backendName) == "pvr") {
			backend = FixedFunctionBackend::Pvr;
			overrideTarget = FixedFunctionTarget::Pvr;
		} else if (StringView(backendName) == "gx") {
			backend = FixedFunctionBackend::Gx;
			overrideTarget = FixedFunctionTarget::Gx;
		} else if (StringView(backendName) == "gu") {
			backend = FixedFunctionBackend::Gu;
			overrideTarget = FixedFunctionTarget::Gu;
		} else if (StringView(backendName) == "gs") {
			backend = FixedFunctionBackend::Gs;
			overrideTarget = FixedFunctionTarget::Gs;
		} else {
			std::fprintf(stderr, "error: unknown fixed-function backend \"%s\" (expected pvr, gx, gu or gs)\n", backendName);
			return 2;
		}

		SmallVector<GeneratedEffectEntry, 0> entries;
		SmallVector<GeneratedEffectFunction, 0> functions;
		std::size_t programsWithBlock = 0, programsWithout = 0;

		for (int fi = 0; fi < inputCount; fi++) {
			const char* inputPath = inputPaths[fi];
			String content;
			if (!ReadFileToString(inputPath, content)) {
				std::fprintf(stderr, "%s: error: cannot read input file\n", inputPath);
				return 1;
			}
			{
				String includeError;
				FileReader reader = [](StringView path, String& out) {
					return ReadFileToString(path, out);
				};
				if (!ShaderParser::ExpandIncludes(content, ShaderParser::DirectoryOf(inputPath), reader, 0, includeError)) {
					std::fprintf(stderr, "%s: error: %s\n", inputPath, includeError.data());
					return 1;
				}
			}
			Diagnostic diag;
			SmallVector<ShaderDocument, 0> documents;
			if (!ShaderParser::ParseDocuments(content, documents, diag)) {
				return ReportError(inputPath, diag);
			}

			for (const ShaderDocument& document : documents) {
				// A block that NAMES this backend — on its own or inside a target list — overrides the
				// generic fixed_function block for it, regardless of declaration order
				const FixedFunctionBlock* block = nullptr;
				for (const FixedFunctionBlock& b : document.FixedFunctionBlocks) {
					bool namesBackend = false;
					for (FixedFunctionTarget t : b.Targets) {
						if (t == overrideTarget) {
							namesBackend = true;
							break;
						}
					}
					if (namesBackend) {
						block = &b;
						break;
					}
					if (b.Targets.empty() && block == nullptr) {
						block = &b;
					}
				}
				if (block == nullptr) {
					programsWithout++;
					continue;
				}
				programsWithBlock++;

				// The provenance names the block AS WRITTEN (its whole target list, not just the
				// backend being emitted), so a generated function points at one declaration in the
				// shader file even when several consoles share it
				String provenance = BaseNameOf(inputPath) + ":fixed_function("_s +
					FixedFunctionTargetList(block->Targets) + ")"_s;
				for (std::size_t vi = 0; vi < document.Variants.size() + 1; vi++) {
					// The unnamed base variant first, then one entry per declared variant — the same
					// per-variant expansion (and prefix mangling) the Sw generated functions use
					String variant = (vi == 0 ? String{} : document.Variants[vi - 1]);
					String prefix = (variant.empty() ? document.ProgramName : String(document.ProgramName + "_" + variant));
					FixedFunctionResult r = ConsoleFixedFunction::TranspileBlock(*block, variant, backend);
					if (!r.Ok) {
						std::fprintf(stderr, "%s:%d: error: %s\n", inputPath, r.Line, r.Error.data());
						return 1;
					}
					GeneratedEffectEntry e;
					e.Program = document.ProgramName;
					e.Variant = std::move(variant);
					e.Intrinsic = std::move(r.Intrinsic);
					e.UsesOffsetColor = r.UsesOffsetColor;
					e.Requirements = r.Requirements;
					if (e.Intrinsic.empty()) {
						// Deduplicate on the exact body text: bodies are name-free, so batched twins and
						// palette variants whose difference lives in the dispatch loop (not in the pass
						// code) collapse into the first occurrence's function. UsesOffsetColor and
						// Requirements are derived from the body during emission, so equal bodies are
						// guaranteed to agree on them - no separate key needed.
						std::size_t fi = 0;
						while (fi < functions.size() && functions[fi].Body != r.Body) {
							fi++;
						}
						if (fi == functions.size()) {
							GeneratedEffectFunction fn;
							fn.Name = prefix + "_Effect"_s;
							fn.Body = std::move(r.Body);
							fn.Provenance = provenance;
							functions.push_back(std::move(fn));
						}
						String label = (e.Variant.empty()
							? e.Program : String(e.Program + " ("_s + e.Variant + ")"_s));
						functions[fi].SharedBy.push_back(std::move(label));
						functions[fi].SharedProvenance.push_back(provenance);
						e.FnIndex = std::int32_t(fi);
					}
					e.Prefix = std::move(prefix);
					entries.push_back(std::move(e));
				}
			}
		}

		String header = BuildFixedFunctionHeader(backend, entries, functions);
		if (!WriteStringToFile(outputPath, header)) {
			std::fprintf(stderr, "error: cannot write output file \"%s\"\n", outputPath);
			return 1;
		}

		std::fprintf(stdout, "[FixedFunction:%s] emitted %zu unique effect function(s) for %zu table entries from %zu program(s); %zu program(s) have no block\n",
			backendName, functions.size(), entries.size(), programsWithBlock, programsWithout);
		for (const GeneratedEffectEntry& e : entries) {
			if (e.FnIndex >= 0 && functions[std::size_t(e.FnIndex)].Name != String(e.Prefix + "_Effect"_s)) {
				// A deduplicated row: the entry reuses another (program, variant)'s function
				std::fprintf(stdout, "  emitted: %s (= %s)\n", e.Prefix.data(), functions[std::size_t(e.FnIndex)].Name.data());
			} else {
				std::fprintf(stdout, "  emitted: %s\n", e.Prefix.data());
			}
		}
		return 0;
	}

	/**
		Transpiles the fragment stage of every program variant across all @p inputPaths to C++ and writes the
		single aggregate "SwGeneratedShaders.h". Prints a summary of accepted vs. declined shaders to stdout.
	*/
	/** One program-variant's pair of Cg stage sources for the aggregate GXM header */
	struct CgShaderEntry
	{
		String ProgramName;
		String VariantName;
		String VertexSource;
		String FragmentSource;
	};

	/** Emits a Cg source string as a C++ raw string literal (the sources contain no `)"` sequence) */
	String CgSourceLiteral(const String& source)
	{
		return String("R\"(") + source + ")\"";
	}

	/**
		Builds the aggregate Cg header the GXM backend consumes.

		Deliberately an aggregate of its own rather than two more fields in the per-shader headers: those
		carry the precompiled DXBC and SPIR-V blobs, which only a Windows run with `d3dcompiler_47` and
		`glslangValidator` can produce, so extending them would force a full regeneration that ANY other
		machine would complete by writing nulls over that baked binary data. Cg is plain text with no
		external compiler, so its aggregate regenerates anywhere - the same reasoning that gives the
		software renderer and the three console backends aggregates of their own.
	*/
	String BuildCgGeneratedHeader(const SmallVectorImpl<CgShaderEntry>& entries)
	{
		String out;
		out += "// Generated by ShaderCompiler (--emit-cg). Do not edit manually.\n";
		out += "#pragma once\n\n";
		out += "#if defined(WITH_RHI_GXM)\n\n";
		out += "#include <cstddef>\n";
		out += "#include <cstring>\n\n";
		out += "namespace nCine::RHI::GXM\n{\n";
		out += "\tnamespace\n\t{\n";
		out += "\t\t/** @brief Cg stage sources of one program variant, compiled on the console by SceShaccCg */\n";
		out += "\t\tstruct GeneratedCgShader\n\t\t{\n";
		out += "\t\t\tconst char* ProgramName;\n";
		out += "\t\t\tconst char* VariantName;\n";
		out += "\t\t\tconst char* VertexSource;\n";
		out += "\t\t\tconst char* FragmentSource;\n";
		out += "\t\t};\n\n";
		out += "\t\tconstexpr GeneratedCgShader GeneratedCgShaders[] = {\n";
		for (const CgShaderEntry& e : entries) {
			out += "\t\t\t{ \"" + e.ProgramName + "\", \"" + e.VariantName + "\",\n";
			out += "\t\t\t\t" + CgSourceLiteral(e.VertexSource) + ",\n";
			out += "\t\t\t\t" + CgSourceLiteral(e.FragmentSource) + " },\n";
		}
		out += "\t\t};\n\n";
		out += "\t\t/**\n";
		out += "\t\t\t@brief Looks the sources of a program variant up by the identity the loader plumbed in\n\n";
		out += "\t\t\tResolved from `ShaderProgram::SetProgramIdentity()` exactly as the console backends resolve\n";
		out += "\t\t\ttheir fixed-function effects, so a program absent from the table is a shader whose Cg\n";
		out += "\t\t\ttransform the emitter declined rather than a lookup mistake.\n";
		out += "\t\t*/\n";
		out += "\t\tinline const GeneratedCgShader* FindGeneratedCgShader(const char* programName, const char* variantName)\n";
		out += "\t\t{\n";
		out += "\t\t\tif (programName == nullptr) {\n\t\t\t\treturn nullptr;\n\t\t\t}\n";
		out += "\t\t\tconst char* wantedVariant = (variantName != nullptr ? variantName : \"\");\n";
		out += "\t\t\tfor (const GeneratedCgShader& s : GeneratedCgShaders) {\n";
		out += "\t\t\t\tif (std::strcmp(s.ProgramName, programName) == 0 && std::strcmp(s.VariantName, wantedVariant) == 0) {\n";
		out += "\t\t\t\t\treturn &s;\n\t\t\t\t}\n";
		out += "\t\t\t}\n";
		out += "\t\t\treturn nullptr;\n";
		out += "\t\t}\n";
		out += "\t}\n";
		out += "}\n\n";
		out += "#endif\n";
		return out;
	}

	int RunEmitCg(const char* outputPath, char** inputPaths, int inputCount)
	{
		SmallVector<CgShaderEntry, 0> entries;
		SmallVector<std::pair<String, String>, 0> declined;	// (prefix, reason)

		for (int fi = 0; fi < inputCount; fi++) {
			const char* inputPath = inputPaths[fi];
			SmallVector<ShaderDocument, 0> documents;
			SmallVector<ProgramReflection, 0> programs;
			String errorMsg;
			if (!LoadProgramsForFile(inputPath, documents, programs, errorMsg)) {
				std::fprintf(stderr, "%s: error: %s\n", inputPath, errorMsg.data());
				return 1;
			}
			for (const ProgramReflection& program : programs) {
				const String& progName = program.Document->ProgramName;
				// The Vita runs these full-screen procedural backgrounds in its fragment stage. The
				// software branch retains the moving warp while omitting stars, dither and expensive
				// curve evaluation; apply it only to these two Cg programs.
				const bool useBackgroundQualityPath = (progName == "TexturedBackground"_s || progName == "TexturedBackgroundCircle"_s);
				for (const VariantReflection& v : program.Variants) {
					String prefix = (v.Name.empty() ? progName : String(progName + "_" + v.Name));
					CgShaderEntry e;
					e.ProgramName = progName;
					e.VariantName = v.Name;
					bool ok = true;
					for (std::int32_t stage = 0; stage < 2 && ok; stage++) {
						const bool vertexStage = (stage == 0);
						String modern = ShaderParser::BuildStageSource(*program.Document, vertexStage, v.Define,
							useBackgroundQualityPath);
						String cg;
						Diagnostic diag;
						if (HlslEmitter::Transform(modern, vertexStage, v.Reflection, cg, diag,
								HlslEmitter::Dialect::Cg)) {
							(vertexStage ? e.VertexSource : e.FragmentSource) = std::move(cg);
						} else {
							declined.emplace_back(prefix, diag.Message);
							ok = false;
						}
					}
					if (ok) {
						entries.push_back(std::move(e));
					}
				}
			}
		}

		String header = BuildCgGeneratedHeader(entries);
		if (!WriteStringToFile(outputPath, header)) {
			std::fprintf(stderr, "error: cannot write output file \"%s\"\n", outputPath);
			return 1;
		}

		std::fprintf(stdout, "[Cg] emitted %zu program-variant(s), declined %zu\n", entries.size(), declined.size());
		for (const auto& d : declined) {
			std::fprintf(stdout, "  declined %s: %s\n", d.first.data(), d.second.data());
		}
		return 0;
	}

	// --- PlayStation 3 RSX microcode via a child cgcomp process -----------------------------------

	/**
		@brief Largest batch a PlayStation 3 shader is compiled for

		Must match `RsxDevice::MaxBatchSize`. A batched instance array reaches the vertex program through
		constant registers rather than a bindable buffer - the `vp40` profile allows a program 544 of them -
		and the backend's batched corner stream, which supplies the instance index, covers exactly this many
		sprites. Measured against the batched shaders they stop compiling somewhere above 40, so this is the
		value they all accept with margin.
	*/
	constexpr std::int32_t RsxMaxBatchSize = 32;

	/** One program-variant's pair of compiled RSX microcode blobs */
	struct RsxShaderEntry
	{
		String ProgramName;
		String VariantName;
		SmallVector<std::uint8_t, 0> VertexUcode;
		SmallVector<std::uint8_t, 0> FragmentUcode;
	};

	/** Resolves `cgcomp`, from @p explicitPath, then `$PS3DEV/bin`, then `PATH` */
	bool LocateCgcomp(StringView explicitPath, String& outPath)
	{
		if (!explicitPath.empty()) {
			outPath = String{explicitPath};
			return fs::FileExists(outPath);
		}
		String ps3dev;
		if (TryGetEnvironmentVariable("PS3DEV"_s, ps3dev)) {
#if defined(DEATH_TARGET_WINDOWS)
			String candidate = fs::CombinePath({ ps3dev, "bin"_s, "cgcomp.exe"_s });
#else
			String candidate = fs::CombinePath({ ps3dev, "bin"_s, "cgcomp"_s });
#endif
			if (fs::FileExists(candidate)) {
				outPath = Death::move(candidate);
				return true;
			}
		}
		// Left as a bare name for the spawn to resolve against PATH
		outPath = "cgcomp"_s;
		return true;
	}

	/**
		Compiles one Cg stage to NV40 microcode by running @p cgcompPath over it.

		cgcomp is a front end over NVIDIA's `libCg.so`, so it fails with "Unable to load Cg" when the Cg
		Toolkit is not on the library path - reported through @p log like any other compile error.
	*/
	bool CompileRsxMicrocode(const String& cgcompPath, const String& source, bool vertexStage,
		SmallVectorImpl<std::uint8_t>& ucode, String& log)
	{
		ucode.clear();
		log = {};
		static unsigned counter = 0;
		String base = MakeTemporaryPathPrefix("sc_rsx"_s, counter++);
		if (base.empty()) {
			log = "cannot resolve the temporary directory"_s;
			return false;
		}
		// cgcomp picks its profile from the flag rather than the extension, but naming the input the way
		// the SDK does keeps a leftover temporary self-describing
		String inputPath = base + (vertexStage ? ".vcg"_s : ".fcg"_s);
		String outputPath = base + (vertexStage ? ".vpo"_s : ".fpo"_s);
		String logPath = base + ".log"_s;

		if (!WriteStringToFile(inputPath, source)) {
			log = "cannot write the temporary shader input"_s;
			return false;
		}

		int exitCode = -1;
		bool ran;
#if defined(DEATH_TARGET_WINDOWS)
		String command = "\""_s + cgcompPath + "\" "_s + (vertexStage ? "-v"_s : "-f"_s) +
			" \""_s + inputPath + "\" \""_s + outputPath + "\""_s;
		DWORD win32ExitCode = ~DWORD{0};
		ran = RunProcessCaptured(command, logPath, win32ExitCode);
		exitCode = static_cast<int>(win32ExitCode);
#else
		SmallVector<String, 0> argv{InPlaceInit, { cgcompPath, (vertexStage ? "-v"_s : "-f"_s), inputPath, outputPath }};
		ran = RunProcessCaptured(argv, logPath, exitCode);
#endif
		{
			String logContent;
			if (ReadFileToString(logPath, logContent)) {
				log = Death::move(logContent);
			}
		}
		bool ok = ran && (exitCode == 0);
		if (ok) {
			String bytes;
			if (ReadFileToString(outputPath, bytes) && !bytes.empty()) {
				const std::uint8_t* first = reinterpret_cast<const std::uint8_t*>(bytes.data());
				// Emitted verbatim. A post-pass that rewrote the register type of every source slot cgcomp
				// leaves encoded as 0 was tried here once, to stop RPCS3 abandoning the batched programs with
				// "Src check failed"; it corrupted real constant fetches, because a type of 0 is not the
				// "unused slot" it assumed. Batching is left undefined on the RSX instead (RHI_CAP_BATCHING in
				// RhiFwd.h) - do not reach for a microcode patch again without first pinning the instruction
				// encoding down against cgcomp's own `-d` assembly dump.
				ucode.assign(first, first + bytes.size());
			} else {
				ok = false;
				if (log.empty()) {
					log = "cgcomp produced no microcode output"_s;
				}
			}
		}
		fs::RemoveFile(inputPath);
		fs::RemoveFile(outputPath);
		fs::RemoveFile(logPath);
		return ok && !ucode.empty();
	}

	/** Emits one microcode blob as a C++ byte-array initializer, wrapped so the header stays readable */
	String RsxByteArrayLiteral(const SmallVectorImpl<std::uint8_t>& data, StringView indent)
	{
		String out;
		for (std::size_t i = 0; i < data.size(); i += 16) {
			out += indent;
			const std::size_t lineEnd = (i + 16 < data.size() ? i + 16 : data.size());
			for (std::size_t j = i; j < lineEnd; j++) {
				out += Death::format("0x{:.2x}", data[j]);
				// The separator goes before the next byte on the same line, so a line never ends in
				// trailing whitespace
				if (j + 1 < lineEnd) {
					out += ", "_s;
				}
			}
			if (lineEnd < data.size()) {
				out += ","_s;
			}
			out += "\n"_s;
		}
		return out;
	}

	/** Builds the aggregate microcode header the RSX backend binds */
	String BuildRsxGeneratedHeader(const SmallVectorImpl<RsxShaderEntry>& entries)
	{
		String out;
		out += "// Generated by ShaderCompiler (--emit-rsx). Do not edit manually.\n"_s;
		out += "#pragma once\n\n"_s;
		out += "#if defined(WITH_RHI_RSX)\n\n"_s;
		out += "#include <cstddef>\n"_s;
		out += "#include <cstdint>\n"_s;
		out += "#include <cstring>\n\n"_s;
		out += "namespace nCine::RHI::RSX\n{\n"_s;
		out += "\tnamespace\n\t{\n"_s;
		out += "\t\t/** @brief NV40 microcode of one program variant, compiled offline by cgcomp */\n"_s;
		out += "\t\tstruct GeneratedRsxShader\n\t\t{\n"_s;
		out += "\t\t\tconst char* ProgramName;\n"_s;
		out += "\t\t\tconst char* VariantName;\n"_s;
		out += "\t\t\tconst std::uint8_t* VertexProgram;\n"_s;
		out += "\t\t\tconst std::uint8_t* FragmentProgram;\n"_s;
		out += "\t\t};\n\n"_s;

		for (std::size_t i = 0; i < entries.size(); i++) {
			const RsxShaderEntry& e = entries[i];
			out += "\t\t// "_s + e.ProgramName;
			if (!e.VariantName.empty()) {
				out += " ("_s + e.VariantName + ")"_s;
			}
			out += "\n"_s;
			// 16-byte aligned: the microcode headers are read as structures of 32-bit fields, and the
			// fragment blob is copied into local memory the GPU fetches in aligned bursts
			out += Death::format("\t\talignas(16) const std::uint8_t _rsxVs{}[] = {{\n", i);
			out += RsxByteArrayLiteral(e.VertexUcode, "\t\t\t"_s);
			out += "\t\t};\n"_s;
			out += Death::format("\t\talignas(16) const std::uint8_t _rsxFs{}[] = {{\n", i);
			out += RsxByteArrayLiteral(e.FragmentUcode, "\t\t\t"_s);
			out += "\t\t};\n\n"_s;
		}

		out += "\t\tconstexpr GeneratedRsxShader GeneratedRsxShaders[] = {\n"_s;
		for (std::size_t i = 0; i < entries.size(); i++) {
			out += Death::format("\t\t\t{{ \"{}\", \"{}\", _rsxVs{}, _rsxFs{} }},\n",
				entries[i].ProgramName, entries[i].VariantName, i, i);
		}
		out += "\t\t};\n\n"_s;
		out += "\t\t/**\n"_s;
		out += "\t\t\t@brief Looks up the microcode pair of one program variant\n\n"_s;
		out += "\t\t\tReturns `nullptr` for a variant whose Cg the vp40/fp40 profiles could not express when\n"_s;
		out += "\t\t\tthis table was generated; the backend reports that as a load-time failure of that one\n"_s;
		out += "\t\t\tprogram, because there is no runtime compiler to fall back on.\n"_s;
		out += "\t\t*/\n"_s;
		out += "\t\tinline const GeneratedRsxShader* FindGeneratedRsxShader(const char* programName, const char* variantName)\n"_s;
		out += "\t\t{\n"_s;
		out += "\t\t\tif (programName == nullptr) return nullptr;\n"_s;
		out += "\t\t\tif (variantName == nullptr) variantName = \"\";\n"_s;
		out += "\t\t\tfor (const GeneratedRsxShader& s : GeneratedRsxShaders) {\n"_s;
		out += "\t\t\t\tif (std::strcmp(s.ProgramName, programName) == 0 && std::strcmp(s.VariantName, variantName) == 0) {\n"_s;
		out += "\t\t\t\t\treturn &s;\n"_s;
		out += "\t\t\t\t}\n"_s;
		out += "\t\t\t}\n"_s;
		out += "\t\t\treturn nullptr;\n"_s;
		out += "\t\t}\n"_s;
		out += "\t}\n}\n\n"_s;
		out += "#endif\n"_s;
		return out;
	}

	/**
		Compiles the built-in Cg shaders that have no `.shader` file behind them.

		The RSX backend needs a present shader to flip its intermediate screen surface into the display
		buffer, and no other backend does, so it has no GLSL counterpart to be lowered from - it is written
		as Cg directly, next to the shaders it is generated alongside.
	*/
	bool AppendRsxBuiltinShaders(const String& cgcompPath, StringView shadersDir,
		SmallVectorImpl<RsxShaderEntry>& entries, String& error)
	{
		struct Builtin { const char* Name; const char* VertexFile; const char* FragmentFile; };
		static const Builtin Builtins[] = {
			{ "__Present", "Rsx/Present.vcg", "Rsx/Present.fcg" }
		};

		for (const Builtin& builtin : Builtins) {
			String vertexPath = fs::CombinePath(String{shadersDir}, String{builtin.VertexFile});
			String fragmentPath = fs::CombinePath(String{shadersDir}, String{builtin.FragmentFile});
			String vertexSource, fragmentSource;
			if (!ReadFileToString(vertexPath, vertexSource) ||
				!ReadFileToString(fragmentPath, fragmentSource)) {
				error = "cannot read the built-in shader \""_s + String{builtin.Name} + "\""_s;
				return false;
			}

			RsxShaderEntry e;
			e.ProgramName = String{builtin.Name};
			String log;
			if (!CompileRsxMicrocode(cgcompPath, vertexSource, true, e.VertexUcode, log) ||
				!CompileRsxMicrocode(cgcompPath, fragmentSource, false, e.FragmentUcode, log)) {
				error = "the built-in shader \""_s + String{builtin.Name} + "\" failed to compile: "_s + FirstLine(log);
				return false;
			}
			entries.push_back(Death::move(e));
		}
		return true;
	}

	int RunEmitRsx(const char* outputPath, StringView cgcompOption, StringView shadersDir,
		char** inputPaths, int inputCount)
	{
		String cgcompPath;
		if (!LocateCgcomp(cgcompOption, cgcompPath)) {
			std::fprintf(stderr, "error: cgcomp not found at \"%s\"\n", String{cgcompOption}.data());
			return 1;
		}

		SmallVector<RsxShaderEntry, 0> entries;
		SmallVector<std::pair<String, String>, 0> declined;	// (prefix, reason)

		// The built-in present shader goes first, so a table that lost it is obvious at a glance
		String builtinError;
		if (!AppendRsxBuiltinShaders(cgcompPath, shadersDir, entries, builtinError)) {
			std::fprintf(stderr, "error: %s\n", builtinError.data());
			return 1;
		}

		for (int fi = 0; fi < inputCount; fi++) {
			const char* inputPath = inputPaths[fi];
			SmallVector<ShaderDocument, 0> documents;
			SmallVector<ProgramReflection, 0> programs;
			String errorMsg;
			if (!LoadProgramsForFile(inputPath, documents, programs, errorMsg)) {
				std::fprintf(stderr, "%s: error: %s\n", inputPath, errorMsg.data());
				return 1;
			}
			for (const ProgramReflection& program : programs) {
				const String& progName = program.Document->ProgramName;
				for (const VariantReflection& v : program.Variants) {
					String prefix = (v.Name.empty() ? progName : String(progName + "_" + v.Name));
					RsxShaderEntry e;
					e.ProgramName = progName;
					e.VariantName = v.Name;
					bool ok = true;
					for (std::int32_t stage = 0; stage < 2 && ok; stage++) {
						const bool vertexStage = (stage == 0);
						// The RSX is the one target built with NO_DYNAMIC_BRANCHING: a fragment stage whose
						// control flow survives into NV40 IF/LOOP/BRK comes out corrupt, because the branch
						// body reuses registers the surrounding code is still holding
						String modern = ShaderParser::BuildStageSource(*program.Document, vertexStage, v.Define,
							/*softwareRenderer*/ false, /*noDynamicBranching*/ true);
						String cg;
						Diagnostic diag;
						// The same Cg the PS Vita gets, only with the batch capped to what the console's
						// constant registers hold - the one thing that differs between the two
						if (!HlslEmitter::Transform(modern, vertexStage, v.Reflection, cg, diag,
								HlslEmitter::Dialect::Cg, RsxMaxBatchSize)) {
							declined.emplace_back(prefix, diag.Message);
							ok = false;
							break;
						}
						String log;
						SmallVectorImpl<std::uint8_t>& ucode = (vertexStage ? e.VertexUcode : e.FragmentUcode);
						if (!CompileRsxMicrocode(cgcompPath, cg, vertexStage, ucode, log)) {
							declined.emplace_back(prefix, FirstLine(log));
							ok = false;
						}
					}
					if (ok) {
						entries.push_back(Death::move(e));
					}
				}
			}
		}

		String header = BuildRsxGeneratedHeader(entries);
		if (!WriteStringToFile(outputPath, header)) {
			std::fprintf(stderr, "error: cannot write output file \"%s\"\n", outputPath);
			return 1;
		}

		std::size_t total = 0;
		for (const RsxShaderEntry& e : entries) {
			total += e.VertexUcode.size() + e.FragmentUcode.size();
		}
		std::fprintf(stdout, "[RSX] emitted %zu program-variant(s), %zu bytes of microcode, declined %zu\n",
			entries.size(), total, declined.size());
		for (const auto& d : declined) {
			std::fprintf(stdout, "  declined %s: %s\n", d.first.data(), d.second.data());
		}
		return 0;
	}

	int RunEmitSwGenerated(const char* outputPath, char** inputPaths, int inputCount)
	{
		SmallVector<GeneratedShaderEntry, 0> supported;
		SmallVector<std::pair<String, String>, 0> declined;	// (prefix, reason)

		for (int fi = 0; fi < inputCount; fi++) {
			const char* inputPath = inputPaths[fi];
			SmallVector<ShaderDocument, 0> documents;
			SmallVector<ProgramReflection, 0> programs;
			String errorMsg;
			if (!LoadProgramsForFile(inputPath, documents, programs, errorMsg)) {
				std::fprintf(stderr, "%s: error: %s\n", inputPath, errorMsg.data());
				return 1;
			}
			for (const ProgramReflection& program : programs) {
				const String& progName = program.Document->ProgramName;
				for (const VariantReflection& v : program.Variants) {
					String prefix = (v.Name.empty() ? progName : String(progName + "_" + v.Name));
					// Only this transpile path builds sources with SOFTWARE_RENDERER defined, so a shader can
					// substitute a cheaper CPU variant of an expensive fragment path (e.g. TexturedBackground)
					// without changing any other backend's emitted output.
					String fs = ShaderParser::BuildStageSource(*program.Document, false, v.Define, /*softwareRenderer*/ true);
					String vs = ShaderParser::BuildStageSource(*program.Document, true, v.Define, /*softwareRenderer*/ true);
					SmallVector<SamplerBinding, 0> samplers;
					for (const TextureInfo& t : v.Reflection.Textures) {
						SamplerBinding sb;
						sb.Name = t.Name;
						sb.Unit = t.Unit;
						samplers.push_back(std::move(sb));
					}
					SmallVector<GlslInstanceMember, 0> instanceMembers;
					BuildInstanceMembers(v.Reflection, instanceMembers);
					GlslToCppResult r = GlslToCpp::TranspileFragment(prefix, fs, vs, samplers, instanceMembers);
					String rejectReason;
					if (r.Supported && !EmittedFragmentIsCompilable(r.Code, prefix, rejectReason)) {
						declined.emplace_back(prefix, rejectReason);
					} else if (r.Supported) {
						GeneratedShaderEntry e;
						e.Prefix = prefix;
						e.Code = std::move(r.Code);
						e.HasComputeVaryings = r.HasComputeVaryings;
						ExtractUniformFields(e.Code, e.Prefix, e.Fields);
						// Constant-varying fields share the struct with the loose uniforms but are filled by
						// "<Prefix>_ComputeVaryings" (not ResolveUniform), so drop them from the uniform list.
						for (const String& vn : r.ConstVaryingNames) {
							for (std::size_t i = 0; i < e.Fields.size();) {
								if (e.Fields[i].Name == vn) {
									e.Fields.erase(e.Fields.begin() + std::ptrdiff_t(i));
								} else {
									i++;
								}
							}
						}
						supported.push_back(std::move(e));
					} else {
						declined.emplace_back(prefix, r.UnsupportedReason);
					}
				}
			}
		}

		String header = BuildSwGeneratedHeader(supported);
		if (!WriteStringToFile(outputPath, header)) {
			std::fprintf(stderr, "error: cannot write output file \"%s\"\n", outputPath);
			return 1;
		}

		std::fprintf(stdout, "[SwGenerated] emitted %zu supported fragment function(s), declined %zu\n",
			supported.size(), declined.size());
		for (const GeneratedShaderEntry& e : supported) {
			std::fprintf(stdout, "  emitted:  %s (%zu uniform field(s))\n", e.Prefix.data(), e.Fields.size());
		}
		for (const std::pair<String, String>& d : declined) {
			std::fprintf(stdout, "  declined: %s - %s\n", d.first.data(), d.second.data());
		}
		return 0;
	}

	// --- Whole-directory regeneration (--generate-all) --------------------------------------------

	/**
		Options of the --generate-all driver, which produces every committed artifact under
		"Sources/Shaders/Generated" in one process, from one enumeration of the shader directory. Doing it
		here rather than from a shell script is what makes a regeneration all-or-nothing: a partial run
		(some aggregate written from an older shader list, one header emitted without SPIR-V) cannot leave
		the committed set internally inconsistent, and the file order no longer depends on the shell's
		locale-sensitive sorting.
	*/
	struct GenerateAllOptions
	{
		String ShadersDirectory;		// Empty: auto-detected from the executable, then the working directory
		String OutputDirectory;			// Empty: "<ShadersDirectory>/Generated"
		const char* GlslangOverride = nullptr;
		const char* CgcompPath = nullptr;	// Empty: "$PS3DEV/bin", then PATH
		bool Check = false;
		bool NoDxbc = false;
	};

	/**
		Finds the "Sources/Shaders" directory by walking up from the executable (the tool normally lives in
		"Sources/Utilities/ShaderCompiler/x64/Release") and then from the working directory. Any build layout
		this does not cover is served by an explicit --shaders-dir.
	*/
	bool AutoDetectShadersDirectory(String& outPath)
	{
		const String starts[] = {
			fs::GetDirectoryName(fs::GetExecutablePath()),
			fs::GetWorkingDirectory()
		};
		for (const String& start : starts) {
			String directory = start;
			for (std::int32_t level = 0; level < 10 && !directory.empty(); level++) {
				String candidates[] = {
					fs::CombinePath({ directory, "Sources"_s, "Shaders"_s }),
					fs::CombinePath(directory, "Shaders"_s)
				};
				for (String& candidate : candidates) {
					if (fs::DirectoryExists(candidate)) {
						outPath = std::move(candidate);
						return true;
					}
				}
				directory = fs::GetDirectoryName(directory);
			}
		}
		return false;
	}

	/**
		Builds the umbrella header: one include per generated program header plus the per-namespace
		AllPrograms[] index arrays, so runtime code has a single include and can enumerate the programs.
		A canvas_item file with a "batched" directive contributes two program symbols from one header.
	*/
	String BuildUmbrellaHeader(const SmallVectorImpl<String>& includeStems,
		const SmallVectorImpl<String>& jazz2Programs, const SmallVectorImpl<String>& ncinePrograms)
	{
		String out;
		out += "// Generated by ShaderCompiler (--generate-all). Do not edit manually.\n";
		out += "#pragma once\n\n";
		for (const String& stem : includeStems) {
			out += "#include \"" + stem + ".h\"\n";
		}
		out += "\n";
		// The generated shader data namespaces carry no public API and are excluded from the API
		// documentation (Doxygen defines `DOXYGEN_GENERATING_OUTPUT`), keeping this header out of it
		out += "#ifndef DOXYGEN_GENERATING_OUTPUT\n";
		const struct { const char* Namespace; const char* Comment; const SmallVectorImpl<String>* Programs; } sections[] = {
			{ "Jazz2::ShadersGen", "All generated Jazz2 shader programs, sorted by name", &jazz2Programs },
			{ "nCine::ShadersGen", "All generated nCine default shader programs, sorted by name", &ncinePrograms }
		};
		for (const auto& section : sections) {
			out += "namespace ";
			out += section.Namespace;
			out += "\n{\n\t// ";
			out += section.Comment;
			out += "\n\tinline constexpr const ShaderCompiler::Program* AllPrograms[] = {\n";
			for (const String& name : *section.Programs) {
				out += "\t\t&" + name + ",\n";
			}
			out += "\t};\n}\n";
			if (&section == &sections[0]) {
				out += "\n";
			}
		}
		out += "#endif\n";
		return out;
	}

	/**
		Writes the whole artifact set into @p outputDirectory and appends the name of every file it wrote to
		@p writtenNames. Split out of RunGenerateAll so that the caller owns the temporary directory of the
		staleness guard and can clean it up on every path.
	*/
	/**
		Wraps the bodies collected in BackendArtifacts into one complete aggregate header

		@p guard is the `WITH_RHI_*` macro the whole file hides behind, so a build without that backend
		compiles none of it. @p bodies pairs each namespace with the declarations belonging to it, in the
		order the shaders were enumerated - the same order the per-shader headers were written in.
	*/
	String BuildBackendAggregate(StringView what, StringView guard,
		const SmallVectorImpl<std::pair<String, String>>& bodies)
	{
		String output;
		output += "// Generated by ShaderCompiler (--generate-all). Do not edit manually.\n";
		output += "//\n";
		output += "// " + String{what} + " stage artifacts for every shader, kept apart from the per-shader headers because\n";
		output += "// producing them needs a compiler that is not available everywhere. A regeneration on a machine\n";
		output += "// without it leaves this file alone rather than replacing its contents with nulls.\n";
		output += "#pragma once\n";
		output += "\n";
		output += "#include \"ShaderCompilerTypes.h\"\n";
		output += "\n";
		output += "#if defined(" + String{guard} + ")\n";
		output += "#ifndef DOXYGEN_GENERATING_OUTPUT\n";
		for (const auto& body : bodies) {
			output += "\n";
			output += "namespace " + body.first + "\n";
			output += "{\n";
			output += body.second;
			output += "}\n";
		}
		output += "\n";
		output += "#endif\n";
		output += "#endif\n";
		return output;
	}

	int GenerateAllArtifacts(StringView shadersDirectory, const SmallVectorImpl<String>& shaderNames,
		StringView outputDirectory, SpirvCompileFn& compileSpirv, DxbcCompileFn& compileDxbc,
		StringView cgcompOption, SmallVectorImpl<String>& writtenNames)
	{
		// The shared reflection types every generated header includes
		{
			String path = fs::CombinePath(outputDirectory, "ShaderCompilerTypes.h"_s);
			if (!WriteStringToFile(path, Emitter::BuildTypesHeader())) {
				std::fprintf(stderr, "error: cannot write output file \"%s\"\n", path.data());
				return 1;
			}
			writtenNames.emplace_back("ShaderCompilerTypes.h"_s);
		}

		// One header per shader; "Default*.shader" are the nCine default programs, everything else is Jazz2
		SmallVector<String, 0> includeStems, jazz2Programs, ncinePrograms;
		// The Direct3D 11 and Vulkan stage artifacts of every shader, collected here and written as one
		// aggregate each below - see BackendArtifacts for why they do not go into the per-shader headers
		SmallVector<std::pair<String, String>, 0> d3d11Bodies, vulkanBodies;
		for (const String& shaderName : shaderNames) {
			StringView stem = fs::GetFileNameWithoutExtension(shaderName);
			const bool isDefault = stem.hasPrefix("Default"_s);
			const char* ns = (isDefault ? "nCine::ShadersGen" : "Jazz2::ShadersGen");
			String inputPath = fs::CombinePath(shadersDirectory, shaderName);

			SmallVector<ShaderDocument, 0> documents;
			SmallVector<ProgramReflection, 0> programs;
			String errorMsg;
			std::int32_t errorLine = 0;
			if (!LoadProgramsForFile(inputPath.data(), documents, programs, errorMsg, /*strictTextureUnits*/ true, &errorLine)) {
				std::fprintf(stderr, "%s:%d: error: %s\n", inputPath.data(), errorLine, errorMsg.data());
				return 1;
			}

			String output;
			BackendArtifacts artifacts;
			Diagnostic diag;
			if (!Emitter::EmitHeader(programs, ns, inputPath, compileSpirv, compileDxbc, output, artifacts, diag)) {
				std::fprintf(stderr, "%s:%d: error: %s\n", inputPath.data(), diag.Line, diag.Message.data());
				return 1;
			}
			d3d11Bodies.emplace_back(String{ns}, std::move(artifacts.D3d11));
			vulkanBodies.emplace_back(String{ns}, std::move(artifacts.Vulkan));
			String headerName = stem + ".h"_s;
			String headerPath = fs::CombinePath(outputDirectory, headerName);
			if (!WriteStringToFile(headerPath, output)) {
				std::fprintf(stderr, "error: cannot write output file \"%s\"\n", headerPath.data());
				return 1;
			}

			writtenNames.push_back(headerName);
			includeStems.emplace_back(stem);
			for (const ProgramReflection& program : programs) {
				(isDefault ? ncinePrograms : jazz2Programs).push_back(program.Document->ProgramName);
			}
			std::fprintf(stdout, "ok: %s -> %s [%s]\n", shaderName.data(), headerName.data(), ns);
		}

		// The two aggregates. Each is written ONLY when the compiler that fills it actually ran: without it
		// every blob in the file would come out null, and unlike a missing console effect table that is not
		// a graceful degradation - the Vulkan backend skips every draw of a program with no SPIR-V module,
		// and Direct3D 11 falls back to compiling HLSL text at runtime. The committed file stays valid
		// until someone who can regenerate it does, which is the same bargain RsxGeneratedShaders.h makes.
		{
			// Whether the compiler WORKS, not whether a callback was installed. LocateGlslang() is happy to
			// hand back a bare "glslangValidator" for PATH to resolve, which it then does not - so every
			// compile failed, every module came out empty, and an aggregate written on that basis would be
			// all nulls. Probing with a trivial shader is what the cgcomp path below already does.
			SmallVector<std::uint32_t, 0> probeWords;
			SmallVector<std::uint8_t, 0> probeBytes;
			String probeLog;
			const bool spirvWorks = compileSpirv &&
				compileSpirv("#version 450\nvoid main() { gl_Position = vec4(0.0); }"_s, true, probeWords, probeLog) &&
				!probeWords.empty();
			const bool dxbcWorks = compileDxbc &&
				compileDxbc("float4 VSMain() : SV_Position { return 0; }"_s, true, probeBytes, probeLog) &&
				!probeBytes.empty();

			const struct {
				const char* What; const char* Guard; const char* FileName;
				const SmallVectorImpl<std::pair<String, String>>* Bodies; bool Rebuildable; const char* Missing;
			} aggregates[] = {
				{ "Direct3D 11", "WITH_RHI_D3D11", "D3d11GeneratedShaders.h", &d3d11Bodies,
					dxbcWorks, "no working DXBC compiler (D3DCompile is Windows-only)" },
				{ "Vulkan", "WITH_RHI_VULKAN", "VulkanGeneratedShaders.h", &vulkanBodies,
					spirvWorks, "no working glslang" }
			};
			for (const auto& aggregate : aggregates) {
				String path = fs::CombinePath(outputDirectory, aggregate.FileName);
				// An aggregate that does not exist yet is written even with nothing to put in it: the
				// per-shader headers include it unconditionally under the backend's guard, so a missing file
				// is a build error rather than a missing optimisation. A null one at least links, and the
				// backend reports the empty program itself. Only an EXISTING file is protected.
				if (!aggregate.Rebuildable && fs::FileExists(path)) {
					std::fprintf(stdout, "skipped: %s (%s)\n", aggregate.FileName, aggregate.Missing);
					continue;
				}
				if (!WriteStringToFile(path, BuildBackendAggregate(aggregate.What, aggregate.Guard, *aggregate.Bodies))) {
					std::fprintf(stderr, "error: cannot write output file \"%s\"\n", path.data());
					return 1;
				}
				writtenNames.emplace_back(aggregate.FileName);
				std::fprintf(stdout, "ok: %s stage artifacts -> %s\n", aggregate.What, aggregate.FileName);
			}
		}

		// The umbrella header (includes sorted by name, index arrays in shader order)
		std::sort(includeStems.begin(), includeStems.end(), [](const String& a, const String& b) {
			return std::strcmp(a.data(), b.data()) < 0;
		});
		{
			String path = fs::CombinePath(outputDirectory, "ShadersGen.h"_s);
			if (!WriteStringToFile(path, BuildUmbrellaHeader(includeStems, jazz2Programs, ncinePrograms))) {
				std::fprintf(stderr, "error: cannot write output file \"%s\"\n", path.data());
				return 1;
			}
			writtenNames.emplace_back("ShadersGen.h"_s);
			std::fprintf(stdout, "ok: umbrella -> ShadersGen.h (%zu Jazz2 + %zu nCine programs)\n",
				jazz2Programs.size(), ncinePrograms.size());
		}

		// The aggregates reuse the standalone modes verbatim, so both entry points stay one implementation
		SmallVector<String, 0> inputPaths;
		inputPaths.reserve(shaderNames.size());
		for (const String& shaderName : shaderNames) {
			inputPaths.push_back(fs::CombinePath(shadersDirectory, shaderName));
		}
		SmallVector<char*, 0> inputArgs;
		inputArgs.reserve(inputPaths.size());
		for (String& inputPath : inputPaths) {
			inputArgs.push_back(inputPath.data());
		}
		const int inputCount = static_cast<int>(inputArgs.size());

		{
			String path = fs::CombinePath(outputDirectory, "SwGeneratedShaders.h"_s);
			if (RunEmitSwGenerated(path.data(), inputArgs.data(), inputCount) != 0) {
				return 1;
			}
			writtenNames.emplace_back("SwGeneratedShaders.h"_s);
			std::fprintf(stdout, "ok: software fragments -> SwGeneratedShaders.h\n");
		}
		{
			String path = fs::CombinePath(outputDirectory, "CgGeneratedShaders.h"_s);
			if (RunEmitCg(path.data(), inputArgs.data(), inputCount) != 0) {
				return 1;
			}
			writtenNames.emplace_back("CgGeneratedShaders.h"_s);
			std::fprintf(stdout, "ok: Cg stage sources -> CgGeneratedShaders.h\n");
		}
		{
			// The PlayStation 3 header needs cgcomp, which only a machine with the PS3 toolchain and
			// NVIDIA's Cg Toolkit has. Unlike the DXBC and SPIR-V fields - which degrade to nulls the
			// backend falls back from - an empty microcode table would leave the RSX backend with no
			// shaders at all and no way to make any, so a missing compiler SKIPS the file rather than
			// rewriting it. The committed one then stays valid until someone who can regenerate it does.
			String cgcompPath;
			String probeLog;
			SmallVector<std::uint8_t, 0> probeUcode;
			const bool haveCgcomp = LocateCgcomp(cgcompOption, cgcompPath) &&
				CompileRsxMicrocode(cgcompPath, "void main(out float4 p : POSITION) { p = 0; }"_s,
					true, probeUcode, probeLog);
			if (haveCgcomp) {
				String path = fs::CombinePath(outputDirectory, "RsxGeneratedShaders.h"_s);
				if (RunEmitRsx(path.data(), cgcompPath, shadersDirectory, inputArgs.data(), inputCount) != 0) {
					return 1;
				}
				writtenNames.emplace_back("RsxGeneratedShaders.h"_s);
				std::fprintf(stdout, "ok: RSX microcode -> RsxGeneratedShaders.h\n");
			} else {
				std::fprintf(stdout, "skipped: RsxGeneratedShaders.h (cgcomp unavailable: %s)\n",
					FirstLine(probeLog).data());
			}
		}

		const struct { const char* Backend; const char* FileName; } fixedFunctionTargets[] = {
			{ "pvr", "PvrGeneratedEffects.h" }, { "gx", "GxGeneratedEffects.h" }, { "gu", "GuGeneratedEffects.h" },
			{ "gs", "GsGeneratedEffects.h" }
		};
		for (const auto& target : fixedFunctionTargets) {
			String path = fs::CombinePath(outputDirectory, target.FileName);
			if (RunEmitFixedFunction(target.Backend, path.data(), inputArgs.data(), inputCount) != 0) {
				return 1;
			}
			writtenNames.emplace_back(target.FileName);
			std::fprintf(stdout, "ok: fixed-function effects (%s) -> %s\n", target.Backend, target.FileName);
		}
		return 0;
	}

	int RunGenerateAll(const GenerateAllOptions& options)
	{
		String shadersDirectory = options.ShadersDirectory;
		if (shadersDirectory.empty() && !AutoDetectShadersDirectory(shadersDirectory)) {
			std::fprintf(stderr, "error: cannot locate the shader directory - pass --shaders-dir <dir>\n");
			return 1;
		}
		if (!fs::DirectoryExists(shadersDirectory)) {
			std::fprintf(stderr, "error: shader directory \"%s\" does not exist\n", shadersDirectory.data());
			return 1;
		}
		String committedDirectory = (options.OutputDirectory.empty()
			? fs::CombinePath(shadersDirectory, "Generated"_s) : options.OutputDirectory);

		SmallVector<String, 0> shaderNames;
		if (!ListFilesInDirectory(shadersDirectory, ".shader"_s, shaderNames) || shaderNames.empty()) {
			std::fprintf(stderr, "error: no .shader files found in \"%s\"\n", shadersDirectory.data());
			return 1;
		}

		// SPIR-V is embedded when a glslang is around; without it the fields stay null and the headers still
		// build (see LocateGlslang). The repo-local fallback is looked for next to "<shaders>/../..".
		SpirvCompileFn compileSpirv;
		{
			String glslang;
			StringView override = (options.GlslangOverride != nullptr ? StringView{options.GlslangOverride} : StringView{});
			if (LocateGlslang(override, glslang, fs::GetDirectoryName(fs::GetDirectoryName(shadersDirectory)))) {
				std::fprintf(stdout, "using glslang: %s\n", glslang.data());
				compileSpirv = [glslang](StringView vulkanGlsl, bool vertexStage, SmallVectorImpl<std::uint32_t>& spirv, String& log) {
					return CompileSpirvWithGlslang(glslang, String{vulkanGlsl}, vertexStage, spirv, log);
				};
			} else if (!override.empty()) {
				std::fprintf(stderr, "warning: glslangValidator not found at \"%s\" - Vulkan SPIR-V will be omitted\n",
					options.GlslangOverride);
			} else {
				std::fprintf(stderr, "warning: glslangValidator not found - Vulkan SPIR-V will be omitted "
					"(install the Vulkan SDK for the Vulkan backend)\n");
			}
		}

		DxbcCompileFn compileDxbc;
		if (options.NoDxbc) {
			std::fprintf(stdout, "DXBC embedding disabled (--no-dxbc) - HLSL sources will be embedded instead\n");
		} else {
			String loadError;
			if (LoadD3DCompiler(loadError)) {
				compileDxbc = [](StringView hlsl, bool vertexStage, SmallVectorImpl<std::uint8_t>& dxbc, String& log) {
					bool ok = CompileHlslToDxbc(String{hlsl}, vertexStage ? "VSMain" : "PSMain",
						vertexStage ? "vs_4_0" : "ps_4_0", dxbc, log);
					if (!ok) {
						std::fprintf(stderr, "warning: D3DCompile (%s) failed - embedding the HLSL source instead: %s\n",
							vertexStage ? "vs_4_0" : "ps_4_0", FirstLine(log).data());
					}
					return ok;
				};
			}
#if defined(DEATH_TARGET_WINDOWS)
			else {
				std::fprintf(stderr, "warning: %s - DXBC will be omitted (HLSL sources embedded instead)\n", loadError.data());
			}
#endif
		}

		// --check never touches the tree: everything is generated into a temporary directory and compared
		String outputDirectory = committedDirectory;
		String temporaryDirectory;
		if (options.Check) {
			if (!CreateTemporaryDirectory(temporaryDirectory)) {
				std::fprintf(stderr, "error: cannot create a temporary directory for --check\n");
				return 1;
			}
			outputDirectory = temporaryDirectory;
			if (!compileSpirv) {
				std::fprintf(stderr, "warning: --check without glslang - committed headers with embedded SPIR-V will be reported stale\n");
			}
			if (options.NoDxbc) {
				std::fprintf(stderr, "warning: --check with --no-dxbc - committed headers with embedded DXBC will be reported stale\n");
			}
		} else if (!fs::CreateDirectories(outputDirectory)) {
			std::fprintf(stderr, "error: cannot create output directory \"%s\"\n", outputDirectory.data());
			return 1;
		}

		SmallVector<String, 0> writtenNames;
		int result = GenerateAllArtifacts(shadersDirectory, shaderNames, outputDirectory, compileSpirv,
			compileDxbc, (options.CgcompPath != nullptr ? StringView{options.CgcompPath} : StringView{}),
			writtenNames);

		if (!options.Check) {
			if (result == 0) {
				std::fprintf(stdout, "All %zu shaders generated successfully.\n", shaderNames.size());
			}
			return result;
		}

		// Byte-compare the fresh artifacts against the committed ones; missing and extra files count as stale
		SmallVector<String, 0> stale;
		if (result == 0) {
			for (const String& name : writtenNames) {
				String fresh = fs::CombinePath(outputDirectory, name);
				String committed = fs::CombinePath(committedDirectory, name);
				if (!fs::FileExists(committed)) {
					stale.push_back(name + " (missing from Generated)"_s);
				} else if (!FilesHaveEqualContent(fresh, committed)) {
					stale.push_back(name);
				}
			}
			SmallVector<String, 0> committedNames;
			ListFilesInDirectory(committedDirectory, ".h"_s, committedNames);
			for (const String& name : committedNames) {
				const bool generated = std::any_of(writtenNames.begin(), writtenNames.end(),
					[&name](const String& written) { return written == name; });
				if (!generated) {
					stale.push_back(name + " (committed but no longer generated)"_s);
				}
			}
		}

		for (const String& name : writtenNames) {
			String path = fs::CombinePath(outputDirectory, name);
			fs::RemoveFile(path);
		}
		fs::RemoveDirectoryRecursive(outputDirectory);

		if (result != 0) {
			return result;
		}
		if (!stale.empty()) {
			std::fprintf(stderr, "error: %zu generated header(s) are STALE - re-run --generate-all and commit:\n", stale.size());
			for (const String& name : stale) {
				std::fprintf(stderr, "  %s\n", name.data());
			}
			return 1;
		}
		std::fprintf(stdout, "All %zu shaders verified up to date.\n", shaderNames.size());
		return 0;
	}
}

int main(int argc, char* argv[])
{
	const char* inputPath = nullptr;
	const char* outputPath = nullptr;
	String ns = "ShaderArtifacts";
	bool checkOnly = false;
	bool essl100Check = false;

	// Standalone mode: write the shared reflection-types header and exit
	if (argc == 3 && StringView(argv[1]) == "--emit-types") {
		if (!WriteStringToFile(argv[2], Emitter::BuildTypesHeader())) {
			std::fprintf(stderr, "error: cannot write types header \"%s\"\n", argv[2]);
			return 1;
		}
		return 0;
	}

	// Standalone mode: regenerate every committed artifact of "Sources/Shaders/Generated" in one run - the
	// shared types, the per-shader headers, the umbrella and the five aggregates. Usage:
	//   ShaderCompiler --generate-all [--shaders-dir <dir>] [--out-dir <dir>] [--check] [--no-dxbc] [--glslang <path>]
	if (argc >= 2 && StringView(argv[1]) == "--generate-all") {
		GenerateAllOptions options;
		for (int i = 2; i < argc; i++) {
			StringView arg = argv[i];
			if (arg == "--shaders-dir" || arg == "--out-dir" || arg == "--glslang" || arg == "--cgcomp") {
				if (i + 1 >= argc) {
					std::fprintf(stderr, "error: %s requires a path argument\n", arg.data());
					return 2;
				}
				const char* value = argv[++i];
				if (arg == "--shaders-dir") {
					options.ShadersDirectory = value;
				} else if (arg == "--out-dir") {
					options.OutputDirectory = value;
				} else if (arg == "--cgcomp") {
					options.CgcompPath = value;
				} else {
					options.GlslangOverride = value;
				}
			} else if (arg == "--check") {
				options.Check = true;
			} else if (arg == "--no-dxbc") {
				options.NoDxbc = true;
			} else {
				std::fprintf(stderr, "error: unknown --generate-all option \"%s\"\n", arg.data());
				PrintUsage();
				return 2;
			}
		}
		return RunGenerateAll(options);
	}

	// Standalone mode: transpile every input shader's fragment stage to C++ and write the aggregate
	// "SwGeneratedShaders.h" consumed by the software renderer. Usage:
	//   ShaderCompiler --emit-sw-generated <output.h> <input1.shader> [input2.shader ...]
	if (argc >= 2 && StringView(argv[1]) == "--emit-sw-generated") {
		if (argc < 4) {
			std::fprintf(stderr, "error: --emit-sw-generated requires <output.h> and at least one input .shader\n");
			return 2;
		}
		return RunEmitSwGenerated(argv[2], &argv[3], argc - 3);
	}

	// Standalone mode: transform every input shader's stages into Cg and write the aggregate
	// "CgGeneratedShaders.h" consumed by the PS Vita's sceGxm backend, which compiles them on the console
	// through SceShaccCg. Usage:
	//   ShaderCompiler --emit-cg <output.h> <input1.shader> [input2.shader ...]
	if (argc >= 2 && StringView(argv[1]) == "--emit-cg") {
		if (argc < 4) {
			std::fprintf(stderr, "error: --emit-cg requires <output.h> and at least one input .shader\n");
			return 2;
		}
		return RunEmitCg(argv[2], &argv[3], argc - 3);
	}

	// Standalone mode: transform every input shader's stages into Cg, compile each to NV40 microcode with
	// cgcomp and write the aggregate "RsxGeneratedShaders.h" the PlayStation 3's RSX backend binds --- the
	// console has no shader compiler, so this is the only place its shaders can be produced. Usage:
	//   ShaderCompiler --emit-rsx <output.h> [--cgcomp <path>] [--shaders-dir <dir>] <input1.shader> ...
	if (argc >= 2 && StringView(argv[1]) == "--emit-rsx") {
		if (argc < 4) {
			std::fprintf(stderr, "error: --emit-rsx requires <output.h> and at least one input .shader\n");
			return 2;
		}
		StringView cgcompOption, shadersDirOption;
		SmallVector<char*, 0> inputs;
		for (int i = 3; i < argc; i++) {
			if (StringView(argv[i]) == "--cgcomp" && i + 1 < argc) {
				cgcompOption = StringView(argv[++i]);
			} else if (StringView(argv[i]) == "--shaders-dir" && i + 1 < argc) {
				shadersDirOption = StringView(argv[++i]);
			} else {
				inputs.push_back(argv[i]);
			}
		}
		if (inputs.empty()) {
			std::fprintf(stderr, "error: --emit-rsx requires at least one input .shader\n");
			return 2;
		}
		// The built-in present shader is read relative to the shader directory, so it has to be resolved
		// even when every input was named explicitly
		String shadersDirectory{shadersDirOption};
		if (shadersDirectory.empty() && !AutoDetectShadersDirectory(shadersDirectory)) {
			std::fprintf(stderr, "error: cannot locate the shaders directory, pass --shaders-dir\n");
			return 1;
		}
		return RunEmitRsx(argv[2], cgcompOption, shadersDirectory, inputs.data(), static_cast<int>(inputs.size()));
	}

	// Standalone mode: transpile every input shader's fixed_function block (once per program variant) to
	// C++ and write the per-backend aggregate header consumed by the console fixed-function tier. Usage:
	//   ShaderCompiler --emit-fixed-function <pvr|gx|gu|gs> <output.h> <input1.shader> [input2.shader ...]
	if (argc >= 2 && StringView(argv[1]) == "--emit-fixed-function") {
		if (argc < 5) {
			std::fprintf(stderr, "error: --emit-fixed-function requires <pvr|gx|gu|gs>, <output.h> and at least one input .shader\n");
			return 2;
		}
		return RunEmitFixedFunction(argv[2], argv[3], &argv[4], argc - 4);
	}

	// Standalone mode: emit the VS+PS HLSL of every variant across the input shaders and compile each via
	// d3dcompiler_47's D3DCompile, printing a pass/fail table. Usage:
	//   ShaderCompiler --hlsl-check <input1.shader> [input2.shader ...]
	if (argc >= 2 && StringView(argv[1]) == "--hlsl-check") {
		if (argc < 3) {
			std::fprintf(stderr, "error: --hlsl-check requires at least one input .shader\n");
			return 2;
		}
		return RunHlslCheck(&argv[2], argc - 2);
	}

	// Standalone mode: emit the VS+FS Vulkan GLSL of every variant across the input shaders and compile each to
	// SPIR-V via glslangValidator, printing a pass/fail table. Usage:
	//   ShaderCompiler --spirv-check [--glslang <path>] <input1.shader> [input2.shader ...]
	if (argc >= 2 && StringView(argv[1]) == "--spirv-check") {
		if (argc < 3) {
			std::fprintf(stderr, "error: --spirv-check requires at least one input .shader\n");
			return 2;
		}
		return RunSpirvCheck(&argv[2], argc - 2);
	}

	bool hlslDump = false;
	bool cgDump = false;
	bool vulkanDump = false;
	bool noDxbc = false;
	const char* glslangOverride = nullptr;
	for (int i = 1; i < argc; i++) {
		StringView arg = argv[i];
		if (arg == "-o") {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "error: -o requires a path argument\n");
				return 2;
			}
			outputPath = argv[++i];
		} else if (arg == "-n") {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "error: -n requires a namespace argument\n");
				return 2;
			}
			ns = argv[++i];
		} else if (arg == "--check") {
			checkOnly = true;
		} else if (arg == "--essl100-check") {
			essl100Check = true;
		} else if (arg == "--hlsl") {
			hlslDump = true;
		} else if (arg == "--cg") {
			cgDump = true;
		} else if (arg == "--no-dxbc") {
			noDxbc = true;
		} else if (arg == "--vulkan") {
			vulkanDump = true;
		} else if (arg == "--glslang") {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "error: --glslang requires a path argument\n");
				return 2;
			}
			glslangOverride = argv[++i];
		} else if (arg == "--target") {
			if (i + 1 >= argc) {
				std::fprintf(stderr, "error: --target requires an argument (only: essl100)\n");
				return 2;
			}
			StringView target = argv[++i];
			if (target != "essl100") {
				std::fprintf(stderr, "error: unknown --target \"%s\" (only: essl100)\n", target.data());
				return 2;
			}
			essl100Check = true;
		} else if (arg == "--help" || arg == "-h" || arg == "/?") {
			PrintUsage();
			return 0;
		} else if (!arg.empty() && arg[0] == '-') {
			std::fprintf(stderr, "error: unknown option \"%s\"\n", arg.data());
			PrintUsage();
			return 2;
		} else if (inputPath == nullptr) {
			inputPath = argv[i];
		} else {
			std::fprintf(stderr, "error: multiple input files are not supported\n");
			return 2;
		}
	}

	if (inputPath == nullptr || (!checkOnly && !essl100Check && !hlslDump && !cgDump && !vulkanDump && outputPath == nullptr)) {
		PrintUsage();
		return 2;
	}
	if (!IsValidNamespace(ns)) {
		std::fprintf(stderr, "error: invalid namespace \"%s\"\n", ns.data());
		return 2;
	}

	String content;
	if (!ReadFileToString(inputPath, content)) {
		std::fprintf(stderr, "error: cannot read input file \"%s\"\n", inputPath);
		return 1;
	}

	{
		String includeError;
		FileReader reader = [](StringView path, String& out) {
			return ReadFileToString(path, out);
		};
		if (!ShaderParser::ExpandIncludes(content, ShaderParser::DirectoryOf(inputPath), reader, 0, includeError)) {
			std::fprintf(stderr, "%s: error: %s\n", inputPath, includeError.data());
			return 1;
		}
	}

	// Custom-mode files produce one document; canvas_item files may add the "batched" twin program
	Diagnostic diag;
	SmallVector<ShaderDocument, 0> documents;
	if (!ShaderParser::ParseDocuments(content, documents, diag)) {
		return ReportError(inputPath, diag);
	}

	SmallVector<ProgramReflection, 0> programs;
	programs.reserve(documents.size());
	for (const ShaderDocument& document : documents) {
		ProgramReflection program;
		program.Document = &document;

		// The unnamed base variant (Name "", always Variants[0]) plus one additional entry
		// per declared variant (no cross-products)
		program.Variants.emplace_back();
		for (const String& name : document.Variants) {
			VariantReflection v;
			v.Name = name;
			v.Define = name;
			program.Variants.push_back(std::move(v));
		}

		for (VariantReflection& v : program.Variants) {
			StageReflection vertex, fragment;
			if (!ReflectVariantStage(document, true, v.Define, vertex, diag) ||
				!ReflectVariantStage(document, false, v.Define, fragment, diag)) {
				return ReportError(inputPath, diag);
			}
			if (!GlslReflector::MergeStages(vertex, fragment, v.Reflection, diag)) {
				return ReportError(inputPath, diag);
			}
		}

		// Apply "texture_unit(N)" hint unit assignments across all variants
		for (const TextureDirective& directive : document.Textures) {
			bool found = false;
			for (VariantReflection& v : program.Variants) {
				for (TextureInfo& t : v.Reflection.Textures) {
					if (t.Name == directive.Name) {
						t.Unit = directive.Unit;
						found = true;
					}
				}
			}
			if (!found) {
				diag.Message = "texture unit assignment \"" + directive.Name + "\" does not match any sampler uniform";
				diag.Line = directive.Line;
				return ReportError(inputPath, diag);
			}
		}

		programs.push_back(std::move(program));
	}

	if (essl100Check) {
		String dump = BuildEssl100Dump(programs);
		std::fwrite(dump.data(), 1, dump.size(), stdout);
		return 0;
	}

	if (cgDump) {
		String dump = BuildHlslDump(programs, HlslEmitter::Dialect::Cg);
		std::fwrite(dump.data(), 1, dump.size(), stdout);
		return 0;
	}

	if (hlslDump) {
		String dump = BuildHlslDump(programs);
		std::fwrite(dump.data(), 1, dump.size(), stdout);
		return 0;
	}

	if (vulkanDump) {
		String dump = BuildVulkanDump(programs);
		std::fwrite(dump.data(), 1, dump.size(), stdout);
		return 0;
	}

	if (checkOnly) {
		String dump;
		for (const ProgramReflection& program : programs) {
			dump += Emitter::BuildCheckDump(*program.Document, program.Variants);
		}
		std::fwrite(dump.data(), 1, dump.size(), stdout);
		return 0;
	}

	// Locate glslang (explicit --glslang, else VULKAN_SDK / PATH) and, when found, embed offline-compiled
	// SPIR-V per stage. When it is unavailable the callback stays empty and the VkVsSpirv/VkFsSpirv fields
	// are emitted as nullptr/0, so the headers still build (the Vulkan backend is then not buildable).
	SpirvCompileFn compileSpirv;
	{
		String glslang;
		if (LocateGlslang(glslangOverride != nullptr ? StringView(glslangOverride) : StringView{}, glslang)) {
			compileSpirv = [glslang](StringView vulkanGlsl, bool vertexStage, SmallVectorImpl<std::uint32_t>& spirv, String& log) {
				return CompileSpirvWithGlslang(glslang, String{vulkanGlsl}, vertexStage, spirv, log);
			};
		}
	}

	// Load d3dcompiler_47 (ships with every Windows) and, when available, embed offline-compiled DXBC
	// bytecode per stage INSTEAD of the HLSL text — the D3D11 backend (desktop and UWP/Xbox) then creates
	// its shader objects straight from the blobs, with no runtime D3DCompile and no on-disk cache. When it
	// is unavailable (non-Windows generation) or --no-dxbc is given, the HLSL sources are embedded as
	// before and the backend falls back to runtime compilation.
	DxbcCompileFn compileDxbc;
	if (!noDxbc) {
		String loadError;
		if (LoadD3DCompiler(loadError)) {
			compileDxbc = [](StringView hlsl, bool vertexStage, SmallVectorImpl<std::uint8_t>& dxbc, String& log) {
				bool ok = CompileHlslToDxbc(String{hlsl}, vertexStage ? "VSMain" : "PSMain",
					vertexStage ? "vs_4_0" : "ps_4_0", dxbc, log);
				if (!ok) {
					// A lowered stage that fails D3DCompile falls back to embedding its HLSL source (the
					// runtime compile will fail the same way, so surface it here; --hlsl-check has details)
					std::fprintf(stderr, "warning: D3DCompile (%s) failed - embedding the HLSL source instead: %s\n",
						vertexStage ? "vs_4_0" : "ps_4_0", FirstLine(log).data());
				}
				return ok;
			};
		}
#if defined(DEATH_TARGET_WINDOWS)
		else {
			std::fprintf(stderr, "warning: %s - DXBC will be omitted (HLSL sources embedded instead)\n", loadError.data());
		}
#endif
	}

	String output;
	BackendArtifacts artifacts;
	if (!Emitter::EmitHeader(programs, ns, inputPath, compileSpirv, compileDxbc, output, artifacts, diag)) {
		return ReportError(inputPath, diag);
	}
	if (!WriteStringToFile(outputPath, output)) {
		std::fprintf(stderr, "error: cannot write output file \"%s\"\n", outputPath);
		return 1;
	}
	// This mode emits ONE shader, so it cannot write the aggregates - they hold every shader's artifacts
	// and rewriting them from one input would drop the rest. The header it just wrote references symbols
	// the committed aggregates already define, so it only links against a matching pair; regenerating a
	// shader whose stage artifacts changed means running --generate-all on a machine that can rebuild them.
	if (!artifacts.D3d11.empty() || !artifacts.Vulkan.empty()) {
		std::fprintf(stdout, "note: Direct3D 11 / Vulkan stage artifacts were not written "
			"(they live in the aggregates, which only --generate-all produces)\n");
	}
	return 0;
}
