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
	variant's stage sources to stdout, for inspection — a tool-only P5 slice-1
	surface that does NOT change the emitted headers. Errors are reported to
	stderr as "<file>:<line>: error: <message>" and the exit code is non-zero.
*/

#include "Emit.h"
#include "Essl100.h"

#include <cstdio>
#include <fstream>

#include <Base/Format.h>
#include <Containers/StringConcatenable.h>

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
			"  --target <t>      Selects an inspection target for the transform dump (only: essl100)\n");
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
		per program / variant / stage, with either the transformed source or a slice-2 "unsupported"
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

	if (inputPath == nullptr || (!checkOnly && !essl100Check && outputPath == nullptr)) {
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

	if (checkOnly) {
		String dump;
		for (const ProgramReflection& program : programs) {
			dump += Emitter::BuildCheckDump(*program.Document, program.Variants);
		}
		std::fwrite(dump.data(), 1, dump.size(), stdout);
		return 0;
	}

	String output;
	if (!Emitter::EmitHeader(programs, ns, inputPath, output, diag)) {
		return ReportError(inputPath, diag);
	}
	if (!WriteStringToFile(outputPath, output)) {
		std::fprintf(stderr, "error: cannot write output file \"%s\"\n", outputPath);
		return 1;
	}
	return 0;
}
