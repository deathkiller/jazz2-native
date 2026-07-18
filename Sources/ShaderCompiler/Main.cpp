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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <Base/Format.h>
#include <Containers/StringConcatenable.h>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#	include <windows.h>
#	include <d3dcompiler.h>
#endif

using namespace ShaderCompiler;
using namespace Death::Containers;
using namespace Death::Containers::Literals;

namespace
{
	bool ReadFileToString(const char* path, String& content)
	{
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (!file) {
			return false;
		}
		file.seekg(0, std::ios::end);
		std::streampos size = file.tellg();
		if (size < 0) {
			return false;
		}
		content = String{NoInit, static_cast<std::size_t>(size)};
		file.seekg(0, std::ios::beg);
		if (size > 0) {
			file.read(content.data(), static_cast<std::streamsize>(size));
		}
		return !file.fail();
	}

	bool WriteStringToFile(const char* path, const String& content)
	{
		std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
		if (!file) {
			return false;
		}
		file.write(content.data(), static_cast<std::streamsize>(content.size()));
		file.flush();
		return !file.fail();
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
			"  --vulkan          Print the Vulkan GLSL (#version 450) transform of every stage to stdout\n"
			"  --glslang <path>  glslangValidator to compile SPIR-V with (default: VULKAN_SDK / PATH)\n"
			"  --target <t>      Selects an inspection target for the transform dump (only: essl100)\n"
			"\n"
			"Standalone modes:\n"
			"  --hlsl-check <input.shader ...>                    Emit + D3DCompile each stage as HLSL; print a pass/fail table\n"
			"  --spirv-check [--glslang <path>] <input.shader ...> Emit + glslang-compile each stage to SPIR-V; print a pass/fail table\n");
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
		std::vector<SourceLine> lines = document.Prelude;
		const std::vector<SourceLine>& stage = (vertexStage ? document.VertexLines : document.FragmentLines);
		lines.insert(lines.end(), stage.begin(), stage.end());

		ShaderParser::StripComments(lines);

		Preprocessor preprocessor;
		if (!define.empty()) {
			preprocessor.Define(define, "1");
		}
		std::vector<SourceLine> preprocessed;
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
	String BuildEssl100Dump(const std::vector<ProgramReflection>& programs)
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
	String BuildHlslDump(const std::vector<ProgramReflection>& programs)
	{
		String dump;
		for (const ProgramReflection& program : programs) {
			dump += "program " + program.Document->ProgramName + "\n";
			for (const VariantReflection& v : program.Variants) {
				dump += (v.Name.empty() ? String("variant (base)\n") : String("variant " + v.Name + "\n"));
				for (std::int32_t stage = 0; stage < 2; stage++) {
					bool vertexStage = (stage == 0);
					dump += (vertexStage ? "--- vertex (hlsl) ---\n" : "--- fragment (hlsl) ---\n");
					String modern = ShaderParser::BuildStageSource(*program.Document, vertexStage, v.Define);
					String hlsl;
					Diagnostic diag;
					if (HlslEmitter::Transform(modern, vertexStage, v.Reflection, hlsl, diag)) {
						dump += hlsl;
						if (!hlsl.empty() && hlsl.back() != '\n') {
							dump += "\n";
						}
					} else {
						dump += "hlsl: " + diag.Message + " (line " + Death::format("{}", diag.Line) + ")\n";
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
	String BuildVulkanDump(const std::vector<ProgramReflection>& programs)
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
		std::vector<GeneratedUniformField> Fields;	// non-sampler uniform layout of the struct
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
	void BuildInstanceMembers(const StageReflection& reflection, std::vector<GlslInstanceMember>& out)
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
	std::uint32_t ComponentCountFromType(const std::string& type)
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
	void ExtractUniformFields(StringView code, StringView prefix, std::vector<GeneratedUniformField>& out)
	{
		std::string s(code.data(), code.size());
		std::string marker = "struct ";
		marker.append(prefix.data(), prefix.size());
		marker += "_Uniforms";
		std::size_t pos = s.find(marker);
		if (pos == std::string::npos) {
			return;
		}
		std::size_t brace = s.find('{', pos);
		if (brace == std::string::npos) {
			return;
		}
		auto trim = [](std::string& t) {
			std::size_t a = t.find_first_not_of(" \t\r");
			if (a == std::string::npos) { t.clear(); return; }
			std::size_t b = t.find_last_not_of(" \t\r");
			t = t.substr(a, b - a + 1);
		};
		std::size_t i = brace + 1;
		while (i < s.size()) {
			std::size_t eol = s.find('\n', i);
			if (eol == std::string::npos) {
				eol = s.size();
			}
			std::string line = s.substr(i, eol - i);
			i = eol + 1;
			trim(line);
			if (line.empty()) {
				continue;
			}
			if (line[0] == '}') {
				break;					// the closing "};"
			}
			if (line.back() != ';') {
				continue;
			}
			line.pop_back();			// drop the trailing ';'
			trim(line);
			std::size_t sp = line.find_last_of(" \t");
			if (sp == std::string::npos) {
				continue;
			}
			std::string type = line.substr(0, sp);
			std::string name = line.substr(sp + 1);
			trim(type);
			GeneratedUniformField f;
			f.Name = String{name.c_str()};
			f.ComponentCount = ComponentCountFromType(type);
			out.push_back(std::move(f));
		}
	}

	/**
		Rejects transpiled code that would not compile against the software runtime, catching a known
		limitation of the transpiler that it does not detect itself (and cannot be fixed here):

		It always lowers the `vTexCoords` varying to the fragment's own 2-component texture coordinate
		`vec2(in.u, in.v)`. A shader that declares vTexCoords wider than `vec2` (e.g. the Lighting family
		packs data into a vec4 vTexCoords) and reads a 3rd/4th component would touch a component `sw::vec2`
		does not have. Such a shader also cannot be reproduced by the sprite-quad path anyway.

		(Helpers referencing the fragment input `in` used to be rejected here too, but helpers now take `in`
		as their first parameter and re-derive `unis`, so that case compiles and the check was removed.)
	*/
	bool EmittedFragmentIsCompilable(const String& code, StringView prefix, String& reason)
	{
		static_cast<void>(prefix);
		std::string s(code.data(), code.size());

		// vTexCoords lowered to vec2(in.u, in.v) must only be read with components sw::vec2 provides
		const std::string needle = "vec2(in.u, in.v)";
		const std::string swizzleChars = "xyzwrgbastpq";
		std::size_t p = 0;
		while ((p = s.find(needle, p)) != std::string::npos) {
			std::size_t after = p + needle.size();
			p = after;
			if (after >= s.size() || s[after] != '.') {
				continue;
			}
			std::string sw;
			for (std::size_t i = after + 1; i < s.size() && swizzleChars.find(s[i]) != std::string::npos; i++) {
				sw.push_back(s[i]);
			}
			if (sw.empty()) {
				continue;
			}
			bool ok;
			if (sw.size() == 1) {
				ok = (std::string("xyrgst").find(sw[0]) != std::string::npos);
			} else {
				ok = (sw == "xy" || sw == "rg");		// the only swizzle methods sw::vec2 provides
			}
			if (!ok) {
				std::string r = "reads '." + sw + "' of vTexCoords, which the software path only exposes as a 2D texture coordinate";
				reason = String{r.c_str()};
				return false;
			}
		}
		return true;
	}

	/** Loads one ".shader" file and reflects every variant of every program it declares (offline flow) */
	bool LoadProgramsForFile(const char* inputPath, std::vector<ShaderDocument>& documents,
		std::vector<ProgramReflection>& programs, String& errorMsg)
	{
		String content;
		if (!ReadFileToString(inputPath, content)) {
			errorMsg = "cannot read input file";
			return false;
		}
		{
			String includeError;
			FileReader reader = [](StringView path, String& out) {
				return ReadFileToString(String::nullTerminatedView(path).data(), out);
			};
			if (!ShaderParser::ExpandIncludes(content, ShaderParser::DirectoryOf(inputPath), reader, 0, includeError)) {
				errorMsg = includeError;
				return false;
			}
		}
		Diagnostic diag;
		if (!ShaderParser::ParseDocuments(content, documents, diag)) {
			errorMsg = diag.Message;
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
					!ReflectVariantStage(document, false, v.Define, fragment, diag)) {
					errorMsg = diag.Message;
					return false;
				}
				if (!GlslReflector::MergeStages(vertex, fragment, v.Reflection, diag)) {
					errorMsg = diag.Message;
					return false;
				}
			}
			// Apply "texture_unit(N)" hints (leniently - an unmatched hint just leaves the sampler unassigned,
			// which makes the transpiler decline that shader rather than fail the whole aggregate run)
			for (const TextureDirective& directive : document.Textures) {
				for (VariantReflection& v : program.Variants) {
					for (TextureInfo& t : v.Reflection.Textures) {
						if (t.Name == directive.Name) {
							t.Unit = directive.Unit;
						}
					}
				}
			}
			programs.push_back(std::move(program));
		}
		return true;
	}

	// --- HLSL validation via d3dcompiler_47's D3DCompile ------------------------------------------

#if defined(_WIN32)
	pD3DCompile g_D3DCompile = nullptr;

	/** Loads d3dcompiler_47.dll (ships with Windows) and resolves D3DCompile; false if unavailable */
	bool LoadD3DCompiler(String& error)
	{
		if (g_D3DCompile != nullptr) {
			return true;
		}
		HMODULE mod = LoadLibraryA("d3dcompiler_47.dll");
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

	/** Compiles @p source as HLSL (entry @p entry, profile @p target); returns success + the compiler log */
	bool CompileHlsl(const String& source, const char* entry, const char* target, String& log)
	{
		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		HRESULT hr = g_D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr,
			entry, target, 0, 0, &code, &errors);
		if (errors != nullptr) {
			log = String{reinterpret_cast<const char*>(errors->GetBufferPointer())};
			errors->Release();
		}
		if (code != nullptr) {
			code->Release();
		}
		return SUCCEEDED(hr);
	}

	// --- Vulkan SPIR-V compilation via a child glslangValidator process ---------------------------

	/** True when @p path names an existing file (not a directory) */
	bool FileExistsAt(const char* path)
	{
		DWORD attributes = GetFileAttributesA(path);
		return (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
	}

	/**
		Locates the offline SPIR-V compiler (glslangValidator). An explicit @p overridePath wins (and is an
		error when it does not exist); otherwise "%VULKAN_SDK%\Bin[32]\glslangValidator.exe" then the executable
		search PATH are tried. Returns false when none is found — SPIR-V is then omitted (the Vulkan backend is
		not buildable), which the callers handle gracefully. glslang is only ever a BUILD-TIME dependency.
	*/
	bool LocateGlslang(StringView overridePath, String& outPath)
	{
		if (!overridePath.empty()) {
			String candidate = overridePath;
			if (FileExistsAt(candidate.data())) {
				outPath = std::move(candidate);
				return true;
			}
			return false;
		}
		char sdk[MAX_PATH];
		DWORD sdkLen = GetEnvironmentVariableA("VULKAN_SDK", sdk, MAX_PATH);
		if (sdkLen > 0 && sdkLen < MAX_PATH) {
			const char* const relatives[] = { "\\Bin\\glslangValidator.exe", "\\Bin32\\glslangValidator.exe" };
			for (const char* relative : relatives) {
				String candidate = String{sdk, sdkLen} + relative;
				if (FileExistsAt(candidate.data())) {
					outPath = std::move(candidate);
					return true;
				}
			}
		}
		char found[MAX_PATH];
		if (SearchPathA(nullptr, "glslangValidator.exe", nullptr, MAX_PATH, found, nullptr) > 0) {
			outPath = String{found};
			return true;
		}
		return false;
	}

	/** Runs @p commandLine with stdout+stderr redirected to @p logFile; false if the process could not start */
	bool RunProcessCaptured(const String& commandLine, const char* logFile, DWORD& exitCode)
	{
		exitCode = ~DWORD{0};
		SECURITY_ATTRIBUTES security = {};
		security.nLength = sizeof(security);
		security.bInheritHandle = TRUE;
		HANDLE log = CreateFileA(logFile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (log == INVALID_HANDLE_VALUE) {
			return false;
		}
		STARTUPINFOA startup = {};
		startup.cb = sizeof(startup);
		startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startup.hStdOutput = log;
		startup.hStdError = log;
		PROCESS_INFORMATION process = {};
		std::vector<char> mutableCommand(commandLine.data(), commandLine.data() + commandLine.size());
		mutableCommand.push_back('\0');
		BOOL created = CreateProcessA(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
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
		std::vector<std::uint32_t>& spirv, String& log)
	{
		spirv.clear();
		log = {};
		char tempDir[MAX_PATH];
		DWORD tempLen = GetTempPathA(MAX_PATH, tempDir);
		if (tempLen == 0 || tempLen >= MAX_PATH) {
			log = "cannot resolve the temporary directory";
			return false;
		}
		static unsigned counter = 0;
		String base = String{tempDir, tempLen} + "sc_vk_" +
			Death::format("{}_{}", static_cast<unsigned>(GetCurrentProcessId()), counter++);
		String inputPath = base + (vertexStage ? ".vert" : ".frag");
		String spirvPath = base + ".spv";
		String logPath = base + ".log";

		if (!WriteStringToFile(inputPath.data(), source)) {
			log = "cannot write the temporary shader input";
			return false;
		}

		String command = "\""_s + glslangPath + "\" -V --target-env vulkan1.0 -S "_s +
			(vertexStage ? "vert"_s : "frag"_s) + " \""_s + inputPath + "\" -o \""_s + spirvPath + "\""_s;
		DWORD exitCode = ~DWORD{0};
		bool ran = RunProcessCaptured(command, logPath.data(), exitCode);
		{
			String logContent;
			if (ReadFileToString(logPath.data(), logContent)) {
				log = std::move(logContent);
			}
		}
		bool ok = ran && (exitCode == 0);
		if (ok) {
			String bytes;
			if (ReadFileToString(spirvPath.data(), bytes) && bytes.size() >= 4 && (bytes.size() % 4) == 0) {
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
		DeleteFileA(inputPath.data());
		DeleteFileA(spirvPath.data());
		DeleteFileA(logPath.data());
		return ok && !spirv.empty();
	}
#else
	bool LoadD3DCompiler(String& error) { error = "D3DCompile is only available on Windows"; return false; }
	bool CompileHlsl(const String&, const char*, const char*, String&) { return false; }
	bool LocateGlslang(StringView, String&) { return false; }
	bool CompileSpirvWithGlslang(const String&, const String&, bool, std::vector<std::uint32_t>&, String& log)
	{
		log = "glslang integration is only implemented on Windows";
		return false;
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
		D3DCompile (vs_5_0 / ps_5_0). Prints a pass/fail table and a summary. Tool-only validation surface.
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
		std::vector<String> failures;		// detailed "prefix STAGE: <reason>" lines

		for (int fi = 0; fi < inputCount; fi++) {
			const char* inputPath = inputPaths[fi];
			std::vector<ShaderDocument> documents;
			std::vector<ProgramReflection> programs;
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
						const char* target = (vertexStage ? "vs_5_0" : "ps_5_0");
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
		std::vector<String> failures;

		for (int fi = start; fi < count; fi++) {
			const char* inputPath = args[fi];
			std::vector<ShaderDocument> documents;
			std::vector<ProgramReflection> programs;
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
							std::vector<std::uint32_t> spirv;
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
	String BuildSwGeneratedHeader(const std::vector<GeneratedShaderEntry>& supported)
	{
		String out;
		out += "// Generated by ShaderCompiler (--emit-sw-generated). Do not edit manually.\n";
		out += "#pragma once\n\n";
		out += "#if defined(WITH_RHI_SOFTWARE)\n\n";
		out += "#include \"../../nCine/Graphics/RHI/Software/SwShaderRuntime.h\"\n\n";
		out += "#include <cstddef>\n";
		out += "#include <cstdint>\n";
		out += "#include <cstring>\n\n";
		out += "namespace nCine::RhiSoftware\n{\n";
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
		out += "\t\tstruct SwGeneratedShaderInfo { const char* name; nCine::RhiSoftware::FragmentShaderFn fragment; std::uint32_t uniformsSize; const SwGeneratedUniformField* uniformFields; std::uint32_t uniformFieldCount; SwGeneratedComputeVaryingsFn computeVaryings; };\n\n";

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
		out += "}\n";		// namespace nCine::RhiSoftware
		out += "\n#endif\n";
		return out;
	}

	/**
		Transpiles the fragment stage of every program variant across all @p inputPaths to C++ and writes the
		single aggregate "SwGeneratedShaders.h". Prints a summary of accepted vs. declined shaders to stdout.
	*/
	int RunEmitSwGenerated(const char* outputPath, char** inputPaths, int inputCount)
	{
		std::vector<GeneratedShaderEntry> supported;
		std::vector<std::pair<String, String>> declined;	// (prefix, reason)

		for (int fi = 0; fi < inputCount; fi++) {
			const char* inputPath = inputPaths[fi];
			std::vector<ShaderDocument> documents;
			std::vector<ProgramReflection> programs;
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
					std::vector<SamplerBinding> samplers;
					for (const TextureInfo& t : v.Reflection.Textures) {
						SamplerBinding sb;
						sb.Name = t.Name;
						sb.Unit = t.Unit;
						samplers.push_back(std::move(sb));
					}
					std::vector<GlslInstanceMember> instanceMembers;
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
	bool vulkanDump = false;
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

	if (inputPath == nullptr || (!checkOnly && !essl100Check && !hlslDump && !vulkanDump && outputPath == nullptr)) {
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
			return ReadFileToString(String::nullTerminatedView(path).data(), out);
		};
		if (!ShaderParser::ExpandIncludes(content, ShaderParser::DirectoryOf(inputPath), reader, 0, includeError)) {
			std::fprintf(stderr, "%s: error: %s\n", inputPath, includeError.data());
			return 1;
		}
	}

	// Custom-mode files produce one document; canvas_item files may add the "batched" twin program
	Diagnostic diag;
	std::vector<ShaderDocument> documents;
	if (!ShaderParser::ParseDocuments(content, documents, diag)) {
		return ReportError(inputPath, diag);
	}

	std::vector<ProgramReflection> programs;
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
			compileSpirv = [glslang](StringView vulkanGlsl, bool vertexStage, std::vector<std::uint32_t>& spirv, String& log) {
				return CompileSpirvWithGlslang(glslang, String{vulkanGlsl}, vertexStage, spirv, log);
			};
		}
	}

	String output;
	if (!Emitter::EmitHeader(programs, ns, inputPath, compileSpirv, output, diag)) {
		return ReportError(inputPath, diag);
	}
	if (!WriteStringToFile(outputPath, output)) {
		std::fprintf(stderr, "error: cannot write output file \"%s\"\n", outputPath);
		return 1;
	}
	return 0;
}
