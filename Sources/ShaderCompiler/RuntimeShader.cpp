#include "RuntimeShader.h"

namespace ShaderCompiler
{
	// GlslType (internal) and UniformType (artifact view) declare the same enumerators in the same order
	static_assert(std::uint8_t(GlslType::Float) == std::uint8_t(UniformType::Float));
	static_assert(std::uint8_t(GlslType::Mat4) == std::uint8_t(UniformType::Mat4));
	static_assert(std::uint8_t(GlslType::Sampler2D) == std::uint8_t(UniformType::Sampler2D));
	static_assert(std::uint8_t(GlslType::Struct) == std::uint8_t(UniformType::Struct));

	namespace
	{
		std::uint16_t ToViewArraySize(std::uint32_t arraySize, bool symbolic)
		{
			return (symbolic ? SymbolicArraySize : std::uint16_t(arraySize));
		}

		/** Runs strip-comments + preprocess + reflect for one stage of one variant */
		bool ReflectVariantStage(const ShaderDocument& document, bool vertexStage, const std::string& define,
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
	}

	const RuntimeVariant* RuntimeProgram::FindVariant(const std::string& name) const
	{
		// The base variant is unnamed — an empty name resolves to it (always Variants[0])
		if (name.empty()) {
			return (Variants.empty() ? nullptr : &Variants[0]);
		}
		for (const RuntimeVariant& v : Variants) {
			if (v.Name == name) {
				return &v;
			}
		}
		return nullptr;
	}

	void RuntimeProgram::BuildView()
	{
		const std::size_t variantCount = Variants.size();
		viewUniforms_.resize(variantCount);
		viewBlockMembers_.resize(variantCount);
		viewBlocks_.resize(variantCount);
		viewTextures_.resize(variantCount);
		viewAttributes_.resize(variantCount);
		viewVariants_.resize(variantCount);

		for (std::size_t i = 0; i < variantCount; i++) {
			const RuntimeVariant& v = Variants[i];
			const StageReflection& r = v.Reflection;

			viewUniforms_[i].reserve(r.Uniforms.size());
			for (const UniformInfo& u : r.Uniforms) {
				viewUniforms_[i].push_back({ u.Name.c_str(), UniformType(u.Type), std::uint16_t(u.ArraySize) });
			}

			viewBlockMembers_[i].resize(r.Blocks.size());
			viewBlocks_[i].reserve(r.Blocks.size());
			for (std::size_t j = 0; j < r.Blocks.size(); j++) {
				const BlockInfo& b = r.Blocks[j];
				std::vector<BlockMember>& members = viewBlockMembers_[i][j];
				members.reserve(b.Members.size());
				for (const MemberInfo& m : b.Members) {
					members.push_back({ m.Name.c_str(), UniformType(m.Type), ToViewArraySize(m.ArraySize, m.SymbolicArray), m.Offset });
				}
				viewBlocks_[i].push_back({ b.Name.c_str(), b.BaseSize, b.InstanceStride, members.size(), members.data() });
			}

			viewTextures_[i].reserve(r.Textures.size());
			for (const TextureInfo& t : r.Textures) {
				viewTextures_[i].push_back({ t.Name.c_str(), t.Unit });
			}

			viewAttributes_[i].reserve(r.Attributes.size());
			for (const AttributeInfo& a : r.Attributes) {
				viewAttributes_[i].push_back({ a.Name.c_str(), UniformType(a.Type), a.Location });
			}

			viewVariants_[i] = {
				v.Name.c_str(), v.Define.c_str(), v.VsSource.c_str(), v.FsSource.c_str(),
				viewUniforms_[i].size(), viewUniforms_[i].data(),
				viewBlocks_[i].size(), viewBlocks_[i].data(),
				viewTextures_[i].size(), viewTextures_[i].data(),
				viewAttributes_[i].size(), viewAttributes_[i].data()
			};
		}

		view_ = { Name.c_str(), RenderModes, viewVariants_.size(), viewVariants_.data() };
	}

	bool CompileRuntimeProgram(const std::string& content, const std::string& baseDir, const FileReader& reader, RuntimeProgram& out, Diagnostic& diag)
	{
		std::string expanded = content;
		{
			std::string includeError;
			if (!ShaderParser::ExpandIncludes(expanded, baseDir, reader, 0, includeError)) {
				diag.Message = std::move(includeError);
				diag.Line = 0;
				return false;
			}
		}

		std::vector<ShaderDocument> documents;
		if (!ShaderParser::ParseDocuments(expanded, documents, diag)) {
			return false;
		}
		// Batched twin documents ("batched" in canvas_item files) are an offline-only feature
		// for now — the runtime compiles only the first (primary, non-batched) program
		const ShaderDocument& document = documents[0];

		out.Name = document.ProgramName;
		out.RenderModes = document.RenderModes;
		out.Variants.clear();

		// The unnamed base variant (Name "", always Variants[0]) plus one additional entry
		// per declared variant (no cross-products)
		out.Variants.emplace_back();
		for (const std::string& name : document.Variants) {
			RuntimeVariant v;
			v.Name = name;
			v.Define = name;
			out.Variants.push_back(std::move(v));
		}

		for (RuntimeVariant& v : out.Variants) {
			StageReflection vertex, fragment;
			if (!ReflectVariantStage(document, true, v.Define, vertex, diag) ||
				!ReflectVariantStage(document, false, v.Define, fragment, diag)) {
				return false;
			}
			if (!GlslReflector::MergeStages(vertex, fragment, v.Reflection, diag)) {
				return false;
			}

			v.VsSource = ShaderParser::BuildStageSource(document, true, v.Define);
			v.FsSource = ShaderParser::BuildStageSource(document, false, v.Define);
		}

		// Apply "texture_unit(N)" hint unit assignments across all variants
		for (const TextureDirective& directive : document.Textures) {
			bool found = false;
			for (RuntimeVariant& v : out.Variants) {
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
				return false;
			}
		}

		out.BuildView();
		return true;
	}
}
