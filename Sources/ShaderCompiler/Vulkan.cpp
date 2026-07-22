#include "Vulkan.h"
#include "GlslTypedAst.h"

#include <vector>

#include <Base/Format.h>
#include <Containers/StringConcatenable.h>

using namespace Death::Containers::Literals;

namespace ShaderCompiler
{
	namespace
	{
		/** GLSL keyword of a reflected uniform type (for the gathered "_Globals" block members) */
		StringView GlslReflectedTypeName(GlslType t)
		{
			switch (t) {
				case GlslType::Float: return "float"_s;
				case GlslType::Int: return "int"_s;
				case GlslType::UInt: return "uint"_s;
				case GlslType::Bool: return "bool"_s;
				case GlslType::Vec2: return "vec2"_s;
				case GlslType::Vec3: return "vec3"_s;
				case GlslType::Vec4: return "vec4"_s;
				case GlslType::IVec2: return "ivec2"_s;
				case GlslType::IVec3: return "ivec3"_s;
				case GlslType::IVec4: return "ivec4"_s;
				case GlslType::UVec2: return "uvec2"_s;
				case GlslType::UVec3: return "uvec3"_s;
				case GlslType::UVec4: return "uvec4"_s;
				case GlslType::BVec2: return "bvec2"_s;
				case GlslType::BVec3: return "bvec3"_s;
				case GlslType::BVec4: return "bvec4"_s;
				case GlslType::Mat2: return "mat2"_s;
				case GlslType::Mat3: return "mat3"_s;
				case GlslType::Mat4: return "mat4"_s;
				case GlslType::Sampler2D: return "sampler2D"_s;
				case GlslType::Sampler3D: return "sampler3D"_s;
				case GlslType::SamplerCube: return "samplerCube"_s;
				default: return "float"_s;
			}
		}

		/** GLSL spelling of a typed-AST type (uint/uvec/matrices/user structs are kept verbatim) */
		String GlslTypeSpelling(const TyRef& t)
		{
			switch (t.T) {
				case Ty::Void: return "void"_s;
				case Ty::Float: return "float"_s;
				case Ty::Int: return "int"_s;
				case Ty::UInt: return "uint"_s;
				case Ty::Bool: return "bool"_s;
				case Ty::Vec2: return "vec2"_s;
				case Ty::Vec3: return "vec3"_s;
				case Ty::Vec4: return "vec4"_s;
				case Ty::IVec2: return "ivec2"_s;
				case Ty::IVec3: return "ivec3"_s;
				case Ty::IVec4: return "ivec4"_s;
				case Ty::UVec2: return "uvec2"_s;
				case Ty::UVec3: return "uvec3"_s;
				case Ty::UVec4: return "uvec4"_s;
				case Ty::BVec2: return "bvec2"_s;
				case Ty::BVec3: return "bvec3"_s;
				case Ty::BVec4: return "bvec4"_s;
				case Ty::Mat2: return "mat2"_s;
				case Ty::Mat3: return "mat3"_s;
				case Ty::Mat4: return "mat4"_s;
				case Ty::Sampler2D: return "sampler2D"_s;
				case Ty::Sampler3D: return "sampler3D"_s;
				case Ty::SamplerCube: return "samplerCube"_s;
				case Ty::Struct: return t.S;
				default: return "float"_s;
			}
		}

		// --- Vulkan GLSL emitter ---------------------------------------------------------------------
		//
		// Re-emits a stage's shared typed AST as Vulkan-flavored GLSL ("#version 450"). GLSL is close to
		// the lowered modern-GLSL input, so expression/statement emission is a faithful pretty-print (no
		// mul()/swizzle/built-in rewriting, unlike the HLSL emitter) — only declarations gain explicit
		// set/binding/location decorations and gl_VertexID/gl_InstanceID become their Vulkan spellings.

		class VulkanEmitter
		{
		public:
			VulkanEmitter(Parser& parser, bool vertexStage, const StageReflection& reflection)
				: _p(parser), _vertexStage(vertexStage), _reflection(reflection)
			{
				for (const BlockDecl& b : _p.Blocks) {
					for (const Field& m : b.Members) {
						if (m.SymbolicArray) _hasSymbolicArray = true;
					}
				}
			}

			bool Ok() const { return _ok; }
			const String& Reason() const { return _reason; }

			String Emit()
			{
				const Function* main = nullptr;
				for (const Function& fn : _p.Functions) if (fn.Name == "main") { main = &fn; break; }
				if (main == nullptr) { Fail("no main() function found"_s); return {}; }
				if (!_vertexStage && _p.FragOutputs.empty()) { Fail("fragment shader has no color output"_s); return {}; }

				// Binding assignment from the MERGED reflection (so the two stages agree, and the backend
				// reconstructs the same numbering): set 0 for everything, UBOs first, then samplers.
				const int uboBase = (_reflection.Uniforms.empty() ? 0 : 1);

				String out;
				out += "#version 450\n"_s;

				// Loose (default-block) uniforms are illegal in Vulkan GLSL — gather them into one
				// instance-name-less "_Globals" UBO at binding 0, so bare member reads still resolve.
				// Members + std140 layout come from the merged reflection, so both stages share one layout.
				if (!_p.Uniforms.empty()) {
					out += "layout(set = 0, binding = 0, std140) uniform _Globals\n{\n"_s;
					for (const UniformInfo& u : _reflection.Uniforms) {
						out += "\t"_s + GlslReflectedTypeName(u.Type) + " "_s + u.Name +
							(u.ArraySize > 0 ? String("["_s + Death::format("{}", u.ArraySize) + "]"_s) : String{}) + ";\n"_s;
					}
					out += "};\n\n"_s;
				}

				if (_hasSymbolicArray) {
					out += "#define BATCH_SIZE "_s + Death::format("{}", BatchSize()) + "\n\n"_s;
				}

				// User structs (before the blocks that use them)
				for (const StructDecl& s : _p.Structs) {
					out += "struct "_s + s.Name + "\n{\n"_s;
					for (const Field& f : s.Fields) {
						out += "\t"_s + GlslTypeSpelling(f.Type) + " "_s + f.Name + ArraySuffix(f) + ";\n"_s;
					}
					out += "};\n\n"_s;
				}

				// std140 uniform blocks with an explicit set/binding (keeping any instance name)
				for (const BlockDecl& b : _p.Blocks) {
					out += "layout(set = 0, binding = "_s + Death::format("{}", BlockBinding(b.Name, uboBase)) + ", std140) uniform "_s + b.Name + "\n{\n"_s;
					for (const Field& m : b.Members) {
						out += "\t"_s + GlslTypeSpelling(m.Type) + " "_s + m.Name + ArraySuffix(m) + ";\n"_s;
					}
					out += "}"_s + (b.Instance.empty() ? String{} : " "_s + b.Instance) + ";\n\n"_s;
				}

				// Combined image samplers with an explicit set/binding
				for (const SamplerDecl& s : _p.Samplers) {
					out += "layout(set = 0, binding = "_s + Death::format("{}", SamplerBinding(s.Name, uboBase)) + ") uniform "_s +
						GlslTypeSpelling({ s.Type, {} }) + " "_s + s.Name + ";\n"_s;
				}
				if (!_p.Samplers.empty()) out += "\n"_s;

				// File-scope const/mutable globals
				for (const GlobalVarDecl& g : _p.GlobalVars) {
					out += (g.IsConst ? String("const ") : String{}) + GlslTypeSpelling(g.Type) + " "_s + g.Name;
					if (g.Init != nullptr) out += " = "_s + EmitExpr(g.Init.get(), 0);
					out += ";\n"_s;
				}
				if (!_p.GlobalVars.empty()) out += "\n"_s;

				// SPIR-V has no automatic interface locations: attributes, varyings and fragment outputs
				// each get sequential "layout(location = N)" in DECLARATION ORDER (matching across stages).
				if (_vertexStage) {
					for (std::size_t i = 0; i < _p.Attributes.size(); i++) {
						out += "layout(location = "_s + Death::format("{}", i) + ") in "_s +
							GlslTypeSpelling(_p.Attributes[i].Type) + " "_s + _p.Attributes[i].Name + ";\n"_s;
					}
				}
				for (std::size_t i = 0; i < _p.Varyings.size(); i++) {
					const VaryingDecl& v = _p.Varyings[i];
					out += "layout(location = "_s + Death::format("{}", i) + ") "_s + (v.Flat ? String("flat "_s) : String{}) +
						(_vertexStage ? "out "_s : "in "_s) + GlslTypeSpelling(v.Type) + " "_s + v.Name + ";\n"_s;
				}
				if (!_vertexStage) {
					for (std::size_t i = 0; i < _p.FragOutputs.size(); i++) {
						out += "layout(location = "_s + Death::format("{}", i) + ") out "_s +
							GlslTypeSpelling(_p.FragOutputs[i].Type) + " "_s + _p.FragOutputs[i].Name + ";\n"_s;
					}
				}
				if (!_p.Attributes.empty() || !_p.Varyings.empty() || (!_vertexStage && !_p.FragOutputs.empty())) out += "\n"_s;

				// Helper functions, then main() — both emitted straight from the AST
				for (const Function& fn : _p.Functions) {
					if (fn.Name == "main") continue;
					out += EmitFunction(fn);
					out += "\n"_s;
				}
				out += EmitFunction(*main);
				if (!_ok) return {};
				return out;
			}

		private:
			Parser& _p;
			bool _vertexStage;
			const StageReflection& _reflection;
			bool _ok = true;
			String _reason;
			bool _hasSymbolicArray = false;

			void Fail(String why) { if (_ok) { _ok = false; _reason = std::move(why); } }

			std::int32_t BatchSize() const
			{
				for (const BlockDecl& b : _p.Blocks) {
					for (const Field& m : b.Members) {
						if (!m.SymbolicArray) continue;
						for (const BlockInfo& rb : _reflection.Blocks) {
							if (rb.Name == b.Name && rb.InstanceStride > 0) {
								std::int32_t n = static_cast<std::int32_t>(65536u / rb.InstanceStride);
								return (n < 1 ? 1 : (n > 4096 ? 4096 : n));
							}
						}
					}
				}
				return 512;		// safe default when the stride is not reflected
			}

			// binding (uboBase + i) for the i-th std140 block in reflection order (unreflected blocks stay in range)
			int BlockBinding(StringView name, int uboBase) const
			{
				for (std::size_t i = 0; i < _reflection.Blocks.size(); i++) {
					if (_reflection.Blocks[i].Name == name) return uboBase + static_cast<int>(i);
				}
				return uboBase;
			}

			// binding (uboBase + BlockCount + j) for the j-th sampler in reflection order
			int SamplerBinding(StringView name, int uboBase) const
			{
				int base = uboBase + static_cast<int>(_reflection.Blocks.size());
				for (std::size_t j = 0; j < _reflection.Textures.size(); j++) {
					if (_reflection.Textures[j].Name == name) return base + static_cast<int>(j);
				}
				return base;
			}

			String ArraySuffix(const Field& f) const
			{
				if (f.SymbolicArray) return "[BATCH_SIZE]"_s;
				if (f.ArraySize > 0) return "["_s + Death::format("{}", f.ArraySize) + "]"_s;
				return {};
			}

			// --- Function / statement emission ---------------------------------------------------------

			String EmitFunction(const Function& fn)
			{
				String out;
				out += GlslTypeSpelling(fn.RetType) + " "_s + fn.Name + "("_s;
				for (std::size_t i = 0; i < fn.Params.size(); i++) {
					if (i != 0) out += ", "_s;
					const Param& pp = fn.Params[i];
					out += (pp.Qualifier.empty() ? String{} : pp.Qualifier + " "_s) + GlslTypeSpelling(pp.Type) + " "_s + pp.Name +
						(pp.ArraySize > 0 ? String("["_s + Death::format("{}", pp.ArraySize) + "]"_s) : String{});
				}
				out += ")\n{\n"_s;
				EmitBlockInner(fn.Body.get(), "\t"_s, out);
				out += "}\n"_s;
				return out;
			}

			void EmitBlockInner(const Stmt* block, const String& indent, String& out)
			{
				if (block == nullptr) return;
				for (const StmtPtr& s : block->Body) EmitStmt(s.get(), indent, out);
			}

			void EmitBranch(const Stmt* s, const String& indent, String& out)
			{
				String inner = indent + "\t"_s;
				out += "{\n"_s;
				if (s != nullptr && s->Kind == StmtKind::Block) EmitBlockInner(s, inner, out);
				else if (s != nullptr) EmitStmt(s, inner, out);
				out += indent + "}"_s;
			}

			void EmitStmt(const Stmt* s, const String& indent, String& out)
			{
				if (s == nullptr) return;
				switch (s->Kind) {
					case StmtKind::Block:
						out += indent + "{\n"_s;
						EmitBlockInner(s, indent + "\t"_s, out);
						out += indent + "}\n"_s;
						break;
					case StmtKind::VarDecl:
						out += indent + (s->DeclConst ? String("const ") : String{}) + GlslTypeSpelling(s->DeclType) + " "_s + s->DeclName +
							(s->DeclArraySize > 0 ? String("["_s + Death::format("{}", s->DeclArraySize) + "]"_s) : String{});
						if (s->Init != nullptr) out += " = "_s + EmitExpr(s->Init.get(), 0);
						out += ";\n"_s;
						for (const std::pair<String, ExprPtr>& d : s->ExtraDecls) {
							out += indent + (s->DeclConst ? String("const ") : String{}) + GlslTypeSpelling(s->DeclType) + " "_s + d.first;
							if (d.second != nullptr) out += " = "_s + EmitExpr(d.second.get(), 0);
							out += ";\n"_s;
						}
						break;
					case StmtKind::ExprStmt:
						out += indent + EmitExpr(s->E.get(), 0) + ";\n"_s;
						break;
					case StmtKind::Return:
						out += indent + "return"_s;
						if (s->E != nullptr) out += " "_s + EmitExpr(s->E.get(), 0);
						out += ";\n"_s;
						break;
					case StmtKind::If:
						out += indent + "if ("_s + EmitExpr(s->Cond.get(), 0) + ") "_s;
						EmitBranch(s->Then.get(), indent, out);
						if (s->Else != nullptr) { out += " else "_s; EmitBranch(s->Else.get(), indent, out); }
						out += "\n"_s;
						break;
					case StmtKind::For:
						out += indent + "for ("_s + EmitForInit(s->ForInit.get()) + "; "_s;
						if (s->ForCond != nullptr) out += EmitExpr(s->ForCond.get(), 0);
						out += "; "_s;
						if (s->ForUpdate != nullptr) out += EmitExpr(s->ForUpdate.get(), 0);
						out += ") "_s;
						EmitBranch(s->ForBody.get(), indent, out);
						out += "\n"_s;
						break;
				}
			}

			String EmitForInit(const Stmt* s)
			{
				if (s == nullptr) return {};
				if (s->Kind == StmtKind::VarDecl) {
					String r = GlslTypeSpelling(s->DeclType) + " "_s + s->DeclName;
					if (s->Init != nullptr) r += " = "_s + EmitExpr(s->Init.get(), 0);
					for (const std::pair<String, ExprPtr>& d : s->ExtraDecls) {
						r += ", "_s + d.first;
						if (d.second != nullptr) r += " = "_s + EmitExpr(d.second.get(), 0);
					}
					return r;
				}
				if (s->Kind == StmtKind::ExprStmt) return EmitExpr(s->E.get(), 0);
				return {};
			}

			// --- Expression emission (faithful GLSL pretty-print) --------------------------------------

			std::int32_t EmitPrec(const Expr* e) const
			{
				switch (e->Kind) {
					case ExprKind::Binary: return BinPrec(e->Text);
					case ExprKind::Assign: return 1;
					case ExprKind::Conditional: return 2;
					case ExprKind::Unary: return 90;
					default: return 100;
				}
			}

			String EmitExpr(const Expr* e, std::int32_t minPrec)
			{
				if (e == nullptr) return {};
				String s = EmitCore(e);
				if (EmitPrec(e) < minPrec) return "("_s + s + ")"_s;
				return s;
			}

			String EmitArgs(const Expr* call)
			{
				String r;
				for (std::size_t i = 0; i < call->Args.size(); i++) {
					if (i != 0) r += ", "_s;
					r += EmitExpr(call->Args[i].get(), 0);
				}
				return r;
			}

			String EmitCore(const Expr* e)
			{
				switch (e->Kind) {
					case ExprKind::IntLit: return e->Text;
					case ExprKind::UIntLit: return e->Text;
					case ExprKind::FloatLit: return e->Text;
					case ExprKind::BoolLit: return e->Text;
					case ExprKind::Ident: return EmitIdent(e->Text);
					case ExprKind::Member: return EmitExpr(e->A.get(), 100) + "."_s + e->Text;
					case ExprKind::Index:
						return EmitExpr(e->A.get(), 100) + "["_s + EmitExpr(e->B.get(), 0) + "]"_s;
					case ExprKind::Call: return String{e->Text} + "("_s + EmitArgs(e) + ")"_s;
					case ExprKind::Unary: {
						String inner = EmitExpr(e->A.get(), 90);
						if (e->Postfix) return inner + e->Text;
						if (e->Text == "-" && !inner.empty() && inner[0] == '-') return "- "_s + inner;
						return e->Text + inner;
					}
					case ExprKind::Binary: {
						std::int32_t p = BinPrec(e->Text);
						return EmitExpr(e->A.get(), p) + " "_s + e->Text + " "_s + EmitExpr(e->B.get(), p + 1);
					}
					case ExprKind::Assign:
						return EmitExpr(e->A.get(), 1) + " "_s + e->Text + " "_s + EmitExpr(e->B.get(), 1);
					case ExprKind::Conditional:
						return EmitExpr(e->A.get(), 3) + " ? "_s + EmitExpr(e->B.get(), 3) + " : "_s + EmitExpr(e->C.get(), 2);
				}
				return {};
			}

			// Vulkan keeps the vertex-id and integer math; only the built-in spelling changes
			String EmitIdent(StringView name)
			{
				if (name == "gl_VertexID") return "gl_VertexIndex"_s;
				if (name == "gl_InstanceID") return "gl_InstanceIndex"_s;
				return String{name};
			}
		};
	}

	bool VulkanGlslEmitter::Transform(StringView modernSource, bool vertexStage, const StageReflection& reflection,
		String& out, Diagnostic& diag)
	{
		std::vector<GlslToken> tokens;
		String reason;
		if (!TokenizeStage(modernSource, tokens, reason)) {
			diag.Message = std::move(reason);
			diag.Line = 1;
			return false;
		}
		Parser parser(tokens, vertexStage);
		parser.Run();
		if (!parser.Ok()) {
			diag.Message = "Vulkan GLSL emit: "_s + parser.Reason();
			diag.Line = 1;
			return false;
		}
		VulkanEmitter emitter(parser, vertexStage, reflection);
		String code = emitter.Emit();
		if (!emitter.Ok()) {
			diag.Message = "Vulkan GLSL emit: "_s + emitter.Reason();
			diag.Line = 1;
			return false;
		}
		out = std::move(code);
		return true;
	}
}
