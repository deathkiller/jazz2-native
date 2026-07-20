#include "GlslToCpp.h"
#include "ShaderParser.h"
#include "ConstFold.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

#include <Base/Format.h>
#include <Containers/StringConcatenable.h>

using namespace Death::Containers::Literals;

namespace ShaderCompiler
{
	namespace
	{
		// --- Type system of the supported subset ---------------------------------------------------

		enum class Ty
		{
			Void, Float, Int, Bool,
			Vec2, Vec3, Vec4,
			IVec2, IVec3, IVec4,
			BVec2, BVec3, BVec4,
			Sampler,		// sampler2D/3D/Cube — only valid as a uniform, never a value
			Unsupported		// a recognized-but-unsupported type (matrices)
		};

		// Recognizes a GLSL type keyword. uint/uvec are folded to the signed types (the CPU path treats
		// them identically); mat* is recognized as Unsupported so the caller can decline cleanly.
		bool TryType(StringView k, Ty& out)
		{
			if (k == "void") { out = Ty::Void; return true; }
			if (k == "float") { out = Ty::Float; return true; }
			if (k == "int" || k == "uint") { out = Ty::Int; return true; }
			if (k == "bool") { out = Ty::Bool; return true; }
			if (k == "vec2") { out = Ty::Vec2; return true; }
			if (k == "vec3") { out = Ty::Vec3; return true; }
			if (k == "vec4") { out = Ty::Vec4; return true; }
			if (k == "ivec2" || k == "uvec2") { out = Ty::IVec2; return true; }
			if (k == "ivec3" || k == "uvec3") { out = Ty::IVec3; return true; }
			if (k == "ivec4" || k == "uvec4") { out = Ty::IVec4; return true; }
			if (k == "bvec2") { out = Ty::BVec2; return true; }
			if (k == "bvec3") { out = Ty::BVec3; return true; }
			if (k == "bvec4") { out = Ty::BVec4; return true; }
			if (k == "mat2" || k == "mat3" || k == "mat4") { out = Ty::Unsupported; return true; }
			if (k == "sampler2D" || k == "sampler3D" || k == "samplerCube") { out = Ty::Sampler; return true; }
			return false;
		}

		// C++ spelling of a subset type; vector types are qualified with the runtime namespace when used
		// outside a function body (struct fields, helper signatures) where the `using` is not in scope.
		String CppTypeName(Ty t, bool qualified)
		{
			StringView ns = (qualified ? "nCine::RHI::Software::sw::"_s : ""_s);
			switch (t) {
				case Ty::Void: return "void"_s;
				case Ty::Float: return "float"_s;
				case Ty::Int: return "int"_s;
				case Ty::Bool: return "bool"_s;
				case Ty::Vec2: return ns + "vec2"_s;
				case Ty::Vec3: return ns + "vec3"_s;
				case Ty::Vec4: return ns + "vec4"_s;
				case Ty::IVec2: return ns + "ivec2"_s;
				case Ty::IVec3: return ns + "ivec3"_s;
				case Ty::IVec4: return ns + "ivec4"_s;
				case Ty::BVec2: return ns + "bvec2"_s;
				case Ty::BVec3: return ns + "bvec3"_s;
				case Ty::BVec4: return ns + "bvec4"_s;
				default: return "void"_s;
			}
		}

		// Number of scalar components of a value type (1 for float/int/bool, N for vecN/ivecN/bvecN; 0 for
		// matrices/samplers/void, which have no constant-varying representation)
		std::uint32_t ComponentCount(Ty t)
		{
			switch (t) {
				case Ty::Float: case Ty::Int: case Ty::Bool: return 1;
				case Ty::Vec2: case Ty::IVec2: case Ty::BVec2: return 2;
				case Ty::Vec3: case Ty::IVec3: case Ty::BVec3: return 3;
				case Ty::Vec4: case Ty::IVec4: case Ty::BVec4: return 4;
				default: return 0;
			}
		}

		// C++ float scalar/vector type spelling for a component count (float / vec2 / vec3 / vec4)
		String CppFloatVecType(std::uint32_t componentCount, bool qualified)
		{
			StringView ns = (qualified ? "nCine::RHI::Software::sw::"_s : ""_s);
			switch (componentCount) {
				case 2: return ns + "vec2"_s;
				case 3: return ns + "vec3"_s;
				case 4: return ns + "vec4"_s;
				default: return "float"_s;
			}
		}

		// Normalizes a GLSL float literal to a single-precision C++ literal (drops a GLSL suffix, adds 'f')
		String NormalizeFloatLiteral(StringView text)
		{
			String s = text;
			while (!s.empty()) {
				char c = s[s.size() - 1];
				if (c == 'f' || c == 'F' || c == 'l' || c == 'L') s = String{s.prefix(s.size() - 1)};
				else break;
			}
			return s + "f"_s;
		}

		bool IsQualifier(StringView k)
		{
			return (k == "const" || k == "highp" || k == "mediump" || k == "lowp" ||
				k == "flat" || k == "smooth" || k == "invariant" || k == "centroid" || k == "noperspective");
		}

		bool IsBuiltin(StringView name)
		{
			static const char* const kNames[] = {
				"radians", "degrees", "sin", "cos", "tan", "asin", "acos", "atan",
				"pow", "exp2", "log2", "sqrt", "inversesqrt", "abs", "sign",
				"floor", "ceil", "round", "fract", "mod", "min", "max", "clamp",
				"mix", "step", "smoothstep", "dot", "cross", "length", "distance",
				"normalize", "reflect",
				// Screen-space derivatives: the CPU per-pixel path has no 2x2 quad, so the runtime approximates
				// them with a small constant (SwShaderRuntime.h). They are used only to widen an anti-aliasing
				// edge (aastep/fwidth), so the approximation is visually acceptable and lets derivative-based
				// effects (e.g. the frozen-enemy mask) transpile instead of being declined.
				"dFdx", "dFdy", "fwidth"
			};
			for (const char* n : kNames) {
				if (name == n) {
					return true;
				}
			}
			return false;
		}

		bool IsBannedCall(StringView name)
		{
			// dFdx/dFdy/fwidth are intentionally NOT banned: they are handled as approximated builtins so
			// derivative-based anti-aliasing (e.g. the frozen-enemy mask) can run in software.
			return (name == "texelFetch" || name == "textureSize" || name == "textureLod" ||
				name == "textureProj" || name == "textureGrad" || name == "textureOffset");
		}

		// The software runtime (SwShaderRuntime.h) exposes multi-component read swizzles as a fixed set of
		// const methods; a swizzle outside it (e.g. .yy, .xz, .www, .xxzz) would call a method that does not
		// exist and fail to compile. Single components are always real fields. Returns false for a
		// multi-component swizzle the runtime does not provide, so the caller declines the shader (matching the
		// transpiler's "decline cleanly rather than mistranslate" contract) instead of emitting broken C++.
		// This list must mirror the swizzle methods in SwShaderRuntime.h.
		bool IsRuntimeSwizzle(StringView s)
		{
			if (s.size() <= 1) {
				return true;
			}
			static const char* const kProvided[] = {
				"xy", "rg", "xx", "zw", "ba", "xyz", "rgb", "yzw", "gba", "xyzw", "rgba"
			};
			for (const char* p : kProvided) {
				if (s == p) {
					return true;
				}
			}
			return false;
		}

		// Precedence of a binary/assignment operator, mirroring ConstFold's model (assignment = 1,
		// ternary handled separately at 2). Returns -1 for anything that is not a binary operator.
		std::int32_t BinPrec(StringView op)
		{
			if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
				op == "%=" || op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") return 1;
			if (op == "||") return 3;
			if (op == "^^") return 4;
			if (op == "&&") return 5;
			if (op == "|") return 6;
			if (op == "^") return 7;
			if (op == "&") return 8;
			if (op == "==" || op == "!=") return 9;
			if (op == "<" || op == ">" || op == "<=" || op == ">=") return 10;
			if (op == "<<" || op == ">>") return 11;
			if (op == "+" || op == "-") return 12;
			if (op == "*" || op == "/" || op == "%") return 13;
			return -1;
		}

		// --- Abstract syntax tree --------------------------------------------------------------------

		enum class ExprKind { IntLit, FloatLit, BoolLit, Ident, Member, Index, Call, Unary, Binary, Assign, Conditional };

		struct Expr
		{
			ExprKind Kind;
			String Text;		// literal text / identifier / member field / callee name / operator
			bool Postfix = false;	// Unary: `x++`/`x--` (operator follows the operand) vs. a prefix `++x`
			std::unique_ptr<Expr> A, B, C;	// Index: A = base, B = index
			std::vector<std::unique_ptr<Expr>> Args;
		};
		using ExprPtr = std::unique_ptr<Expr>;

		ExprPtr MakeExpr(ExprKind kind, String text = {})
		{
			auto e = std::make_unique<Expr>();
			e->Kind = kind;
			e->Text = std::move(text);
			return e;
		}

		enum class StmtKind { Block, VarDecl, ExprStmt, If, For, Return };

		struct Stmt
		{
			StmtKind Kind;
			std::vector<std::unique_ptr<Stmt>> Body;		// Block
			Ty DeclType = Ty::Void;							// VarDecl
			String DeclName;								// VarDecl (first declarator)
			ExprPtr Init;									// VarDecl initializer (first declarator)
			// Additional declarators of the same type sharing one statement ("vec2 a, b = c;"), emitted as
			// separate C++ declarations of the shared DeclType
			std::vector<std::pair<String, ExprPtr>> ExtraDecls;
			ExprPtr E;										// ExprStmt / Return value
			ExprPtr Cond;									// If condition
			std::unique_ptr<Stmt> Then, Else;				// If branches
			std::unique_ptr<Stmt> ForInit; ExprPtr ForCond; ExprPtr ForUpdate; std::unique_ptr<Stmt> ForBody;
		};
		using StmtPtr = std::unique_ptr<Stmt>;

		StmtPtr MakeStmt(StmtKind kind)
		{
			auto s = std::make_unique<Stmt>();
			s->Kind = kind;
			return s;
		}

		struct Param { Ty Type; String Name; };
		struct Function { Ty RetType = Ty::Void; String Name; std::vector<Param> Params; StmtPtr Body; };

		enum class SymKind { Varying, Sampler, Uniform, Output, GlobalConst };
		struct Global { SymKind Kind; Ty Type = Ty::Void; std::int32_t Unit = -1; };

		// --- Parser ----------------------------------------------------------------------------------

		class Parser
		{
		public:
			// @p vertexMode relaxes the grammar for the vertex stage: matrix uniforms, std140 uniform blocks
			// and user structs are tolerated (recorded/skipped rather than declined), and array indexing is
			// parsed. The fragment path keeps vertexMode false, so its supported subset is unchanged.
			Parser(const std::vector<GlslToken>& tokens, const std::vector<SamplerBinding>& samplers, bool vertexMode = false)
				: _toks(tokens), _samplers(samplers), _vertexMode(vertexMode) {}

			void Run()
			{
				while (!AtEnd() && _ok) {
					if (IsOp(";")) { Next(); continue; }
					ParseTopLevel();
				}
			}

			bool Ok() const { return _ok; }
			const String& Reason() const { return _reason; }

			std::map<String, Global>& Globals() { return _globals; }
			std::vector<std::pair<String, Ty>>& Uniforms() { return _uniforms; }
			std::map<String, ExprPtr>& ConstInits() { return _constInits; }
			std::vector<Function>& Functions() { return _functions; }

		private:
			const std::vector<GlslToken>& _toks;
			const std::vector<SamplerBinding>& _samplers;
			bool _vertexMode = false;
			std::size_t _pos = 0;
			bool _ok = true;
			String _reason;

			std::map<String, Global> _globals;
			std::vector<std::pair<String, Ty>> _uniforms;
			std::map<String, ExprPtr> _constInits;
			std::vector<Function> _functions;

			const GlslToken& Cur() const { return _toks[_pos]; }
			const GlslToken& At(std::size_t p) const { return _toks[p < _toks.size() ? p : _toks.size() - 1]; }
			bool AtEnd() const { return Cur().Type == GlslTokenType::End; }
			bool Is(GlslTokenType t) const { return Cur().Type == t; }
			bool IsOp(const char* s) const { return Cur().Type == GlslTokenType::Operator && Cur().Text == s; }
			bool IsKw(const char* s) const { return Cur().Type == GlslTokenType::Identifier && Cur().Text == s; }
			void Next() { if (Cur().Type != GlslTokenType::End) _pos++; }

			void Fail(String why) { if (_ok) { _ok = false; _reason = std::move(why); } }

			bool Expect(const char* op)
			{
				if (IsOp(op)) { Next(); return true; }
				Fail("expected '"_s + op + "'"_s);
				return false;
			}

			std::int32_t LookupUnit(StringView name) const
			{
				for (const SamplerBinding& s : _samplers) {
					if (s.Name == name) {
						return s.Unit;
					}
				}
				return -1;
			}

			void SkipQualifiers()
			{
				while (Is(GlslTokenType::Identifier) && IsQualifier(Cur().Text)) {
					Next();
				}
			}

			Ty ParseType()
			{
				if (!Is(GlslTokenType::Identifier)) { Fail("expected a type name"_s); return Ty::Unsupported; }
				Ty t;
				if (!TryType(Cur().Text, t)) { Fail("unknown type '"_s + Cur().Text + "'"_s); return Ty::Unsupported; }
				Next();
				return t;
			}

			// True if, after any leading qualifiers, the next token is a type keyword
			bool LooksLikeDecl() const
			{
				std::size_t p = _pos;
				while (At(p).Type == GlslTokenType::Identifier && IsQualifier(At(p).Text)) p++;
				if (At(p).Type != GlslTokenType::Identifier) return false;
				Ty t;
				return TryType(At(p).Text, t);
			}

			void ParseTopLevel()
			{
				// The vertex stage may declare user structs (the batched instance struct); the CPU path reads
				// instance members at their reflected std140 offsets, so the declaration itself is skipped
				if (_vertexMode && IsKw("struct")) {
					Next();
					if (Is(GlslTokenType::Identifier)) Next();		// struct name
					if (IsOp("{")) { Next(); SkipBalancedBraces(); }
					if (IsOp(";")) Next();
					return;
				}

				if (IsKw("layout")) {
					Next();
					if (Expect("(")) { SkipBalancedParens(); }
				}
				if (IsKw("precision")) {
					// precision <qualifier> <type> ;
					while (!AtEnd() && !IsOp(";") && _ok) Next();
					if (IsOp(";")) Next();
					return;
				}

				bool isIn = false, isOut = false, isUniform = false, isConst = false;
				for (;;) {
					if (IsKw("in")) { isIn = true; Next(); }
					else if (IsKw("out")) { isOut = true; Next(); }
					else if (IsKw("uniform")) { isUniform = true; Next(); }
					else if (IsKw("const")) { isConst = true; Next(); }
					else if (Is(GlslTokenType::Identifier) && IsQualifier(Cur().Text)) { Next(); }
					else break;
				}

				// A std140 uniform block ("uniform <Name> { ... } [inst];") is not a typed declaration - its
				// members are read from the bound block at reflected offsets, so the whole block is skipped
				if (_vertexMode && isUniform && Is(GlslTokenType::Identifier) && At(_pos + 1).Type == GlslTokenType::Operator && At(_pos + 1).Text == "{") {
					Next();							// block name
					Next();							// '{'
					SkipBalancedBraces();
					if (Is(GlslTokenType::Identifier)) Next();	// optional instance name
					if (IsOp(";")) Next();
					return;
				}

				Ty t = ParseType();
				if (!_ok) return;

				if (!Is(GlslTokenType::Identifier)) { Fail("expected a name after the type"_s); return; }
				String name = Cur().Text;
				Next();

				if (IsOp("(")) {
					ParseFunctionRest(t, std::move(name));
					return;
				}
				ParseGlobalRest(isIn, isOut, isUniform, isConst, t, std::move(name));
			}

			void SkipBalancedParens()
			{
				std::int32_t depth = 1;
				while (!AtEnd() && depth > 0) {
					if (IsOp("(")) depth++;
					else if (IsOp(")")) depth--;
					Next();
				}
			}

			// Skips tokens up to and including the '}' matching an already-consumed '{'
			void SkipBalancedBraces()
			{
				std::int32_t depth = 1;
				while (!AtEnd() && depth > 0) {
					if (IsOp("{")) depth++;
					else if (IsOp("}")) depth--;
					Next();
				}
			}

			void ParseGlobalRest(bool isIn, bool isOut, bool isUniform, bool isConst, Ty t, String name)
			{
				if (IsOp("[")) { Fail("array declarations are unsupported"_s); return; }

				Global g;
				if (isUniform) {
					if (t == Ty::Sampler) { g.Kind = SymKind::Sampler; g.Unit = LookupUnit(name); }
					else if (t == Ty::Unsupported) {
						// Matrix uniforms are only used by the vertex stage's gl_Position (never read by a
						// supported varying), so tolerate them there: record the name/kind but keep them out of
						// the fragment uniform struct. The fragment path still declines matrix uniforms.
						if (!_vertexMode) { Fail("matrix uniforms are unsupported"_s); return; }
						g.Kind = SymKind::Uniform; g.Type = Ty::Unsupported;
					}
					else if (t == Ty::Void) { Fail("void uniform is invalid"_s); return; }
					else { g.Kind = SymKind::Uniform; g.Type = t; _uniforms.emplace_back(name, t); }
				} else if (isIn) {
					g.Kind = SymKind::Varying; g.Type = t;
				} else if (isOut) {
					g.Kind = SymKind::Output; g.Type = t;
				} else {
					g.Kind = SymKind::GlobalConst; g.Type = t;
					if (isConst) { /* constness is implicit in the emitted `static const` */ }
				}

				if (IsOp("=")) {
					Next();
					ExprPtr init = ParseExpression(0);
					if (!_ok) return;
					if (g.Kind == SymKind::GlobalConst) {
						_constInits[name] = std::move(init);
					}
				}
				if (IsOp(",")) { Fail("multiple declarators in one statement are unsupported"_s); return; }
				if (!Expect(";")) return;
				_globals[std::move(name)] = std::move(g);
			}

			void ParseFunctionRest(Ty retType, String name)
			{
				Expect("(");
				Function fn;
				fn.RetType = retType;
				fn.Name = std::move(name);

				if (IsKw("void")) { Next(); }		// void or (void) parameter list
				while (!IsOp(")") && !AtEnd() && _ok) {
					// Parameter direction qualifiers ("in"/"out"/"inout") precede the (optional) precision
					// qualifiers and the type; the CPU path passes everything by value/reference identically,
					// so they are skipped
					while (IsKw("in") || IsKw("out") || IsKw("inout")) { Next(); }
					SkipQualifiers();
					Ty pt = ParseType();
					if (!_ok) return;
					if (pt == Ty::Unsupported || pt == Ty::Sampler || pt == Ty::Void) { Fail("unsupported parameter type"_s); return; }
					if (!Is(GlslTokenType::Identifier)) { Fail("expected a parameter name"_s); return; }
					Param p; p.Type = pt; p.Name = Cur().Text; Next();
					if (IsOp("[")) { Fail("array parameters are unsupported"_s); return; }
					fn.Params.push_back(std::move(p));
					if (IsOp(",")) { Next(); continue; }
					break;
				}
				if (!Expect(")")) return;
				fn.Body = ParseBlock();
				if (!_ok) return;
				_functions.push_back(std::move(fn));
			}

			StmtPtr ParseBlock()
			{
				if (!Expect("{")) return nullptr;
				StmtPtr block = MakeStmt(StmtKind::Block);
				while (!IsOp("}") && !AtEnd() && _ok) {
					StmtPtr s = ParseStatement();
					if (s != nullptr) block->Body.push_back(std::move(s));
				}
				Expect("}");
				return block;
			}

			StmtPtr ParseStatement()
			{
				if (IsOp("{")) return ParseBlock();
				if (IsKw("if")) return ParseIf();
				if (IsKw("for")) return ParseFor();
				if (IsKw("return")) return ParseReturn();
				if (IsKw("while") || IsKw("do") || IsKw("switch")) { Fail("'"_s + Cur().Text + "' statements are unsupported"_s); return nullptr; }
				if (IsKw("discard") || IsKw("break") || IsKw("continue")) { Fail("'"_s + Cur().Text + "' statements are unsupported"_s); return nullptr; }

				if (LooksLikeDecl()) return ParseLocalDecl();

				ExprPtr e = ParseExpression(0);
				if (!_ok) return nullptr;
				Expect(";");
				StmtPtr s = MakeStmt(StmtKind::ExprStmt);
				s->E = std::move(e);
				return s;
			}

			StmtPtr ParseLocalDecl()
			{
				SkipQualifiers();
				Ty t = ParseType();
				if (!_ok) return nullptr;
				if (t == Ty::Unsupported) { Fail("matrix locals are unsupported"_s); return nullptr; }
				if (t == Ty::Sampler) { Fail("sampler locals are unsupported"_s); return nullptr; }
				if (t == Ty::Void) { Fail("void local is invalid"_s); return nullptr; }
				if (!Is(GlslTokenType::Identifier)) { Fail("expected a variable name"_s); return nullptr; }
				StmtPtr s = MakeStmt(StmtKind::VarDecl);
				s->DeclType = t;
				s->DeclName = Cur().Text;
				Next();
				if (IsOp("[")) { Fail("array declarations are unsupported"_s); return nullptr; }
				// Initializer is an assignment_expression (minPrec 1): allows a nested assignment/ternary but
				// stops at the declarator-separating comma (which is not a parsed binary operator)
				if (IsOp("=")) { Next(); s->Init = ParseExpression(1); if (!_ok) return nullptr; }
				// Additional comma-separated declarators of the same type ("vec2 a, b = c;") are emitted as
				// separate C++ declarations of the shared type
				while (IsOp(",")) {
					Next();
					if (!Is(GlslTokenType::Identifier)) { Fail("expected a variable name"_s); return nullptr; }
					String name = Cur().Text;
					Next();
					if (IsOp("[")) { Fail("array declarations are unsupported"_s); return nullptr; }
					ExprPtr init;
					if (IsOp("=")) { Next(); init = ParseExpression(1); if (!_ok) return nullptr; }
					s->ExtraDecls.emplace_back(std::move(name), std::move(init));
				}
				Expect(";");
				return s;
			}

			StmtPtr ParseIf()
			{
				Next();
				Expect("(");
				ExprPtr cond = ParseExpression(0);
				Expect(")");
				StmtPtr th = ParseStatement();
				StmtPtr el;
				if (IsKw("else")) { Next(); el = ParseStatement(); }
				StmtPtr s = MakeStmt(StmtKind::If);
				s->Cond = std::move(cond);
				s->Then = std::move(th);
				s->Else = std::move(el);
				return s;
			}

			StmtPtr ParseFor()
			{
				Next();
				Expect("(");
				StmtPtr init;
				if (IsOp(";")) { Next(); }
				else if (LooksLikeDecl()) { init = ParseLocalDecl(); }
				else {
					ExprPtr e = ParseExpression(0);
					Expect(";");
					init = MakeStmt(StmtKind::ExprStmt);
					init->E = std::move(e);
				}
				ExprPtr cond;
				if (!IsOp(";")) cond = ParseExpression(0);
				Expect(";");
				ExprPtr upd;
				if (!IsOp(")")) upd = ParseExpression(0);
				Expect(")");
				StmtPtr body = ParseStatement();
				StmtPtr s = MakeStmt(StmtKind::For);
				s->ForInit = std::move(init);
				s->ForCond = std::move(cond);
				s->ForUpdate = std::move(upd);
				s->ForBody = std::move(body);
				return s;
			}

			StmtPtr ParseReturn()
			{
				Next();
				StmtPtr s = MakeStmt(StmtKind::Return);
				if (!IsOp(";")) s->E = ParseExpression(0);
				Expect(";");
				return s;
			}

			ExprPtr ParseExpression(std::int32_t minPrec)
			{
				ExprPtr lhs = ParseUnary();
				if (!_ok) return lhs;
				while (Is(GlslTokenType::Operator) && _ok) {
					StringView op = Cur().Text;
					if (op == "?") {
						if (minPrec > 2) break;
						Next();
						ExprPtr mid = ParseExpression(0);
						Expect(":");
						ExprPtr rhs = ParseExpression(2);
						ExprPtr cond = MakeExpr(ExprKind::Conditional);
						cond->A = std::move(lhs);
						cond->B = std::move(mid);
						cond->C = std::move(rhs);
						lhs = std::move(cond);
						continue;
					}
					std::int32_t prec = BinPrec(op);
					if (prec < 0 || prec < minPrec) break;
					String opText = Cur().Text;
					Next();
					bool assign = (prec == 1);
					ExprPtr rhs = ParseExpression(assign ? prec : prec + 1);
					if (!_ok) return lhs;
					ExprPtr node = MakeExpr(assign ? ExprKind::Assign : ExprKind::Binary, std::move(opText));
					node->A = std::move(lhs);
					node->B = std::move(rhs);
					lhs = std::move(node);
				}
				return lhs;
			}

			ExprPtr ParseUnary()
			{
				if (Is(GlslTokenType::Operator)) {
					StringView op = Cur().Text;
					if (op == "+") { Next(); return ParseUnary(); }
					if (op == "-" || op == "!" || op == "++" || op == "--") {
						// Prefix "++"/"--" (as in a C-style for-loop update) map straight to C++
						String o = Cur().Text;
						Next();
						ExprPtr v = ParseUnary();
						ExprPtr node = MakeExpr(ExprKind::Unary, std::move(o));
						node->A = std::move(v);
						return node;
					}
					if (op == "~") { Fail("unary '~' is unsupported"_s); return nullptr; }
				}
				return ParsePostfix();
			}

			ExprPtr ParsePostfix()
			{
				ExprPtr e = ParsePrimary();
				if (!_ok) return e;
				while (Is(GlslTokenType::Operator) && _ok) {
					if (IsOp(".")) {
						Next();
						if (!Is(GlslTokenType::Identifier)) { Fail("expected a member name after '.'"_s); return e; }
						ExprPtr m = MakeExpr(ExprKind::Member, Cur().Text);
						Next();
						m->A = std::move(e);
						e = std::move(m);
					} else if (IsOp("[")) {
						// Array subscripting is only reached in vertex mode (the batched instance selector
						// "block.instances[gl_VertexID / 6]"); the fragment path still declines it
						if (!_vertexMode) { Fail("indexing/arrays are unsupported"_s); return e; }
						Next();
						ExprPtr idx = ParseExpression(0);
						if (!_ok) return e;
						if (!Expect("]")) return e;
						ExprPtr node = MakeExpr(ExprKind::Index);
						node->A = std::move(e);
						node->B = std::move(idx);
						e = std::move(node);
					} else if (IsOp("++") || IsOp("--")) {
						// Postfix "++"/"--" (a for-loop update or discarded expression statement)
						ExprPtr node = MakeExpr(ExprKind::Unary, Cur().Text);
						node->Postfix = true;
						node->A = std::move(e);
						Next();
						e = std::move(node);
					} else {
						break;
					}
				}
				return e;
			}

			ExprPtr ParsePrimary()
			{
				const GlslToken& t = Cur();
				switch (t.Type) {
					case GlslTokenType::IntLiteral: {
						ExprPtr e = MakeExpr(ExprKind::IntLit, t.Text);
						Next();
						return e;
					}
					case GlslTokenType::FloatLiteral: {
						ExprPtr e = MakeExpr(ExprKind::FloatLit, t.Text);
						Next();
						return e;
					}
					case GlslTokenType::BoolLiteral: {
						ExprPtr e = MakeExpr(ExprKind::BoolLit, t.Text);
						Next();
						return e;
					}
					case GlslTokenType::UIntLiteral: {
						// Treat unsigned literals as int: drop the trailing u/U suffix
						String text = t.Text;
						while (!text.empty() && (text[text.size() - 1] == 'u' || text[text.size() - 1] == 'U')) {
							text = String{text.prefix(text.size() - 1)};
						}
						ExprPtr e = MakeExpr(ExprKind::IntLit, std::move(text));
						Next();
						return e;
					}
					case GlslTokenType::Identifier: {
						String name = t.Text;
						Next();
						if (IsOp("(")) {
							Next();
							ExprPtr call = MakeExpr(ExprKind::Call, std::move(name));
							if (!IsOp(")")) {
								for (;;) {
									call->Args.push_back(ParseExpression(0));
									if (!_ok) return call;
									if (IsOp(",")) { Next(); continue; }
									break;
								}
							}
							Expect(")");
							return call;
						}
						return MakeExpr(ExprKind::Ident, std::move(name));
					}
					case GlslTokenType::Operator:
						if (IsOp("(")) {
							Next();
							ExprPtr e = ParseExpression(0);
							Expect(")");
							return e;
						}
						Fail("unexpected token '"_s + t.Text + "'"_s);
						return nullptr;
					default:
						Fail("unexpected end of input"_s);
						return nullptr;
				}
			}
		};

		// --- Constant-varying analysis (vertex stage) ------------------------------------------------

		/** @brief One per-instance-constant varying the fragment may read, plus how to recompute it on the CPU */
		struct ConstVaryingInfo
		{
			bool Ok = false;
			String Reason;						// why it is not usable (only meaningful when !Ok)
			std::uint32_t ComponentCount = 0;	// the varying's own width (1..4) = its struct field type
			String ComputeExpr;					// C++ expression over `instanceBlock`/`io` that recomputes it
		};

		/**
			Analyzes the vertex `main()` to decide which varyings are per-instance-constant and, for those,
			builds the C++ expression that recomputes them from the bound instance block. A varying is constant
			iff its final vertex assignment does not (transitively) depend on the per-vertex input `gl_VertexID`
			- with one exception: the batched instance selector `instances[gl_VertexID / 6]` is recognized
			atomically, since its index only picks the instance the device already advances the block pointer
			to. Instance-block members are read at their reflected std140 byte offsets.
		*/
		class VertexAnalyzer
		{
		public:
			VertexAnalyzer(Parser& parser, const std::vector<GlslInstanceMember>& instanceMembers,
				const std::vector<std::pair<String, Ty>>& fragmentUniforms)
				: _globals(parser.Globals()), _functions(parser.Functions()),
				  _instanceMembers(instanceMembers), _fragmentUniforms(fragmentUniforms) {}

			void Run()
			{
				const Function* main = nullptr;
				for (const Function& fn : _functions) {
					if (fn.Name == "main") { main = &fn; break; }
				}
				if (main == nullptr) { return; }

				CollectStmt(main->Body.get());

				// Evaluate every vertex output varying; the fragment picks the ones it actually reads
				for (const std::pair<const String, Global>& g : _globals) {
					if (g.second.Kind != SymKind::Output) { continue; }
					const String& name = g.first;
					ConstVaryingInfo info;
					auto it = _varyingRhs.find(name);
					if (it == _varyingRhs.end() || it->second == nullptr) {
						info.Reason = "varying '"_s + name + "' has no assignment in the vertex main()"_s;
						_varyings[name] = std::move(info);
						continue;
					}
					if (ReferencesPerVertex(it->second)) {
						info.Reason = "non-constant varying '"_s + name + "' (its vertex value depends on the per-vertex input)"_s;
						_varyings[name] = std::move(info);
						continue;
					}
					bool ok = true;
					String reason;
					String cpp = EmitExpr(it->second, ok, reason);
					if (!ok) {
						info.Reason = std::move(reason);
					} else {
						info.Ok = true;
						info.ComponentCount = ComponentCount(g.second.Type);
						if (info.ComponentCount == 0) { info.ComponentCount = 1; }
						info.ComputeExpr = std::move(cpp);
					}
					_varyings[name] = std::move(info);
				}
			}

			const std::map<String, ConstVaryingInfo>& Varyings() const { return _varyings; }

		private:
			std::map<String, Global>& _globals;
			std::vector<Function>& _functions;
			const std::vector<GlslInstanceMember>& _instanceMembers;
			const std::vector<std::pair<String, Ty>>& _fragmentUniforms;

			std::set<String> _tainted;					// locals derived from the per-vertex input
			std::map<String, const Expr*> _localInit;	// untainted locals' initializer (for inlining)
			std::map<String, const Expr*> _varyingRhs;	// last vertex assignment of each varying
			std::map<String, ConstVaryingInfo> _varyings;

			bool IsLocal(StringView name) const
			{
				return (_localInit.find(String{name}) != _localInit.end() || _tainted.find(String{name}) != _tainted.end());
			}
			bool IsVaryingName(StringView name) const
			{
				auto it = _globals.find(String{name});
				return (it != _globals.end() && it->second.Kind == SymKind::Output);
			}
			const GlslInstanceMember* FindInstanceMember(StringView name) const
			{
				for (const GlslInstanceMember& m : _instanceMembers) {
					if (m.Name == name) return &m;
				}
				return nullptr;
			}
			bool IsFragmentUniform(StringView name) const
			{
				for (const std::pair<String, Ty>& u : _fragmentUniforms) {
					if (u.first == name) return true;
				}
				return false;
			}

			// True if `expr` subscripts the batched instance array ("...instances[...]"); its index is the
			// instance selector, not a per-vertex value
			static bool IsInstancesArrayAccess(const Expr* expr)
			{
				if (expr == nullptr || expr->Kind != ExprKind::Index || expr->A == nullptr) return false;
				const Expr* arr = expr->A.get();
				if (arr->Kind == ExprKind::Member) return (arr->Text == "instances");
				if (arr->Kind == ExprKind::Ident) return (arr->Text == "instances");
				return false;
			}
			// True if accessing `field` on `base` reads an instance-block member out of the batched instance
			// array. Recognized atomically so the instance-selector index does not taint the value.
			bool IsInstanceRead(StringView field, const Expr* base) const
			{
				return (FindInstanceMember(field) != nullptr && IsInstancesArrayAccess(base));
			}

			void CollectStmt(const Stmt* s)
			{
				if (s == nullptr) return;
				switch (s->Kind) {
					case StmtKind::Block:
						for (const StmtPtr& c : s->Body) CollectStmt(c.get());
						break;
					case StmtKind::VarDecl:
						RecordLocal(s->DeclName, s->Init.get());
						for (const std::pair<String, ExprPtr>& d : s->ExtraDecls) RecordLocal(d.first, d.second.get());
						break;
					case StmtKind::ExprStmt:
						if (s->E != nullptr && s->E->Kind == ExprKind::Assign) RecordAssign(s->E.get());
						break;
					case StmtKind::If:
						CollectStmt(s->Then.get());
						CollectStmt(s->Else.get());
						break;
					case StmtKind::For:
						CollectStmt(s->ForInit.get());
						CollectStmt(s->ForBody.get());
						break;
					case StmtKind::Return:
						break;
				}
			}

			void RecordLocal(const String& name, const Expr* init)
			{
				if (ReferencesPerVertex(init)) { _tainted.insert(name); }
				else { _localInit[name] = init; }
			}

			void RecordAssign(const Expr* assign)
			{
				const Expr* target = assign->A.get();
				const Expr* rhs = assign->B.get();
				if (target == nullptr) return;
				// Peel member/swizzle and index off the target to find the identifier being written
				const Expr* t = target;
				while (t != nullptr && (t->Kind == ExprKind::Member || t->Kind == ExprKind::Index)) t = t->A.get();
				if (t == nullptr || t->Kind != ExprKind::Ident) return;
				const String& baseName = t->Text;

				if (IsVaryingName(baseName) && target->Kind == ExprKind::Ident) {
					_varyingRhs[baseName] = rhs;		// last write wins
					return;
				}
				// A local written with a per-vertex-derived value becomes tainted; a compound write keeps a
				// prior taint. Taint is monotonic (never cleared), which only ever over-declines.
				if (!IsVaryingName(baseName)) {
					const bool compound = (assign->Text != "="_s);
					if (ReferencesPerVertex(rhs) || (compound && _tainted.count(baseName) != 0)) {
						_tainted.insert(baseName);
						_localInit.erase(baseName);
					}
				}
			}

			bool ReferencesPerVertex(const Expr* e) const
			{
				if (e == nullptr) return false;
				switch (e->Kind) {
					case ExprKind::IntLit: case ExprKind::FloatLit: case ExprKind::BoolLit:
						return false;
					case ExprKind::Ident:
						if (e->Text == "gl_VertexID" || e->Text == "gl_InstanceID") return true;
						return (_tainted.count(e->Text) != 0);
					case ExprKind::Member:
						if (IsInstanceRead(e->Text, e->A.get())) return false;		// constant leaf
						return ReferencesPerVertex(e->A.get());
					case ExprKind::Index:
						return ReferencesPerVertex(e->A.get()) || ReferencesPerVertex(e->B.get());
					case ExprKind::Call:
						for (const ExprPtr& a : e->Args) { if (ReferencesPerVertex(a.get())) return true; }
						return false;
					case ExprKind::Unary:
						return ReferencesPerVertex(e->A.get());
					case ExprKind::Binary: case ExprKind::Assign:
						return ReferencesPerVertex(e->A.get()) || ReferencesPerVertex(e->B.get());
					case ExprKind::Conditional:
						return ReferencesPerVertex(e->A.get()) || ReferencesPerVertex(e->B.get()) || ReferencesPerVertex(e->C.get());
				}
				return true;		// unknown → conservatively per-vertex
			}

			String InstanceRead(const GlslInstanceMember& m) const
			{
				return "(*reinterpret_cast<const "_s + CppFloatVecType(m.ComponentCount, false) + "*>(instanceBlock + "_s +
					Death::format("{}", m.Offset) + "))"_s;
			}

			// Emits the C++ for a constant-varying expression; sets ok=false + reason on an unsupported node
			String EmitExpr(const Expr* e, bool& ok, String& reason) const
			{
				if (e == nullptr) { return {}; }
				switch (e->Kind) {
					case ExprKind::IntLit: return e->Text;
					case ExprKind::FloatLit: return NormalizeFloatLiteral(e->Text);
					case ExprKind::BoolLit: return e->Text;
					case ExprKind::Ident: {
						const GlslInstanceMember* m = FindInstanceMember(e->Text);
						if (m != nullptr) return InstanceRead(*m);
						if (IsFragmentUniform(e->Text)) return "io->"_s + e->Text;
						auto li = _localInit.find(e->Text);
						if (li != _localInit.end()) return "("_s + EmitExpr(li->second, ok, reason) + ")"_s;	// inline untainted local
						ok = false; reason = "constant varying references unsupported symbol '"_s + e->Text + "'"_s;
						return {};
					}
					case ExprKind::Member: {
						const GlslInstanceMember* m = FindInstanceMember(e->Text);
						if (m != nullptr && IsInstancesArrayAccess(e->A.get())) return InstanceRead(*m);
						String base = EmitExpr(e->A.get(), ok, reason);
						return EmitSwizzle(base, e->Text, ok, reason);
					}
					case ExprKind::Call: {
						Ty ct;
						if (TryType(e->Text, ct)) {
							if (ct == Ty::Unsupported || ct == Ty::Sampler) { ok = false; reason = "unsupported constructor in constant varying"_s; return {}; }
							return CppTypeName(ct, false) + "("_s + EmitArgs(e, ok, reason) + ")"_s;
						}
						if (IsBuiltin(e->Text)) return String{e->Text} + "("_s + EmitArgs(e, ok, reason) + ")"_s;
						ok = false; reason = "constant varying calls unsupported function '"_s + e->Text + "'"_s;
						return {};
					}
					case ExprKind::Unary:
						return "("_s + e->Text + EmitExpr(e->A.get(), ok, reason) + ")"_s;
					case ExprKind::Binary: {
						String op = (e->Text == "^^" ? String{"!="_s} : e->Text);
						return "("_s + EmitExpr(e->A.get(), ok, reason) + " "_s + op + " "_s + EmitExpr(e->B.get(), ok, reason) + ")"_s;
					}
					case ExprKind::Conditional:
						return "("_s + EmitExpr(e->A.get(), ok, reason) + " ? "_s + EmitExpr(e->B.get(), ok, reason) + " : "_s + EmitExpr(e->C.get(), ok, reason) + ")"_s;
					case ExprKind::Index: case ExprKind::Assign:
						ok = false; reason = "unsupported expression in constant varying"_s;
						return {};
				}
				ok = false; reason = "unsupported expression in constant varying"_s;
				return {};
			}

			String EmitArgs(const Expr* call, bool& ok, String& reason) const
			{
				String r;
				for (std::size_t i = 0; i < call->Args.size(); i++) {
					if (i != 0) r += ", "_s;
					r += EmitExpr(call->Args[i].get(), ok, reason);
				}
				return r;
			}

			String EmitSwizzle(const String& base, StringView field, bool& ok, String& reason) const
			{
				for (std::size_t i = 0; i < field.size(); i++) {
					if (!"xyzwrgbastpq"_s.contains(field[i])) { ok = false; reason = "unsupported swizzle in constant varying"_s; return base; }
				}
				if (!IsRuntimeSwizzle(field)) { ok = false; reason = "constant-varying swizzle '."_s + field + "' not provided by the software runtime"_s; return base; }
				if (field.size() == 1) return base + "."_s + field;
				if (field.size() >= 2 && field.size() <= 4) return base + "."_s + field + "()"_s;
				ok = false; reason = "unsupported swizzle length in constant varying"_s;
				return base;
			}
		};

		// --- Emitter ---------------------------------------------------------------------------------

		class Emitter
		{
		public:
			Emitter(StringView programName, Parser& parser, const std::map<String, ConstVaryingInfo>& constVaryings)
				: _prog(programName), _globals(parser.Globals()), _uniforms(parser.Uniforms()),
				  _constInits(parser.ConstInits()), _functions(parser.Functions()), _constVaryings(constVaryings) {}

			bool Ok() const { return _ok; }
			const String& Reason() const { return _reason; }

			/** @brief `true` once a fragment read of a constant varying required a `<Program>_ComputeVaryings` */
			bool HasComputeVaryings() const { return !_usedVaryings.empty(); }
			/** @brief Names of the constant-varying fields added to the struct (excluded from the loose-uniform list) */
			std::vector<String> ConstVaryingNames() const
			{
				std::vector<String> names;
				for (const std::pair<const String, ConstVaryingInfo>& v : _usedVaryings) names.push_back(v.first);
				return names;
			}

			String Emit()
			{
				const Function* main = nullptr;
				for (const Function& fn : _functions) {
					if (fn.Name == "main") { main = &fn; break; }
				}
				if (main == nullptr) { Fail("no main() function found in the fragment shader"_s); return {}; }

				// The software rasterizer renders to a single color target (packColor writes in.rgba) —
				// COLOR is the only expressible fragment output. Decline any additional "out" declaration
				// cleanly instead of emitting a reference to a never-declared C++ identifier.
				for (const std::pair<const String, Global>& g : _globals) {
					if (g.second.Kind == SymKind::Output && g.first != "COLOR"_s) {
						Fail("fragment output '"_s + g.first + "' is unsupported (the software fragment path has a single COLOR output, no MRT)"_s);
						return {};
					}
				}

				// Emit the helper functions and the fragment entry point first: reading a constant varying
				// during emission records it in _usedVaryings, which the struct and _ComputeVaryings below need.
				String helpersOut;
				for (const Function& fn : _functions) {
					if (fn.Name == "main") continue;
					helpersOut += EmitHelper(fn);
					helpersOut += "\n"_s;
				}
				String mainOut = EmitMain(*main);

				String out;

				// <Program>_Uniforms — one field per non-sampler fragment uniform, then one per constant varying
				// the fragment read (filled by _ComputeVaryings, not by ResolveUniform)
				out += "struct "_s + _prog + "_Uniforms\n{\n"_s;
				for (const std::pair<String, Ty>& u : _uniforms) {
					out += "\t"_s + CppTypeName(u.second, true) + " "_s + u.first + ";\n"_s;
				}
				for (const std::pair<const String, ConstVaryingInfo>& v : _usedVaryings) {
					out += "\t"_s + CppFloatVecType(v.second.ComponentCount, true) + " "_s + v.first + ";\n"_s;
				}
				out += "};\n\n"_s;

				// File-local const globals (tolerated; the corpus has none)
				for (const std::pair<const String, ExprPtr>& c : _constInits) {
					auto it = _globals.find(c.first);
					Ty t = (it != _globals.end() ? it->second.Type : Ty::Float);
					out += "static const "_s + CppTypeName(t, true) + " "_s + _prog + "_"_s + c.first +
						" = "_s + EmitExpr(c.second.get(), 0) + ";\n"_s;
				}
				if (!_constInits.empty()) out += "\n"_s;

				// The per-instance constant-varying filler the device calls once per instance before the draw
				if (!_usedVaryings.empty()) {
					out += "void "_s + _prog + "_ComputeVaryings(void* inputs, const std::uint8_t* instanceBlock)\n{\n"_s;
					out += "\tusing namespace nCine::RHI::Software::sw;\n"_s;
					out += "\t"_s + _prog + "_Uniforms* io = static_cast<"_s + _prog + "_Uniforms*>(inputs);\n"_s;
					out += "\t(void)io;\n\t(void)instanceBlock;\n"_s;
					for (const std::pair<const String, ConstVaryingInfo>& v : _usedVaryings) {
						out += "\tio->"_s + v.first + " = "_s + v.second.ComputeExpr + ";\n"_s;
					}
					out += "}\n\n"_s;
				}

				// User helper functions (file-local, program-prefixed to avoid cross-shader clashes)
				out += helpersOut;

				// The fragment entry point
				out += mainOut;
				return out;
			}

		private:
			String _prog;
			std::map<String, Global>& _globals;
			std::vector<std::pair<String, Ty>>& _uniforms;
			std::map<String, ExprPtr>& _constInits;
			std::vector<Function>& _functions;
			const std::map<String, ConstVaryingInfo>& _constVaryings;	// all constant varyings found in the vertex
			std::map<String, ConstVaryingInfo> _usedVaryings;			// the subset the fragment actually reads
			bool _ok = true;
			String _reason;

			void Fail(String why) { if (_ok) { _ok = false; _reason = std::move(why); } }

			bool IsUserFunc(StringView name) const
			{
				for (const Function& fn : _functions) {
					if (fn.Name != "main" && fn.Name == name) return true;
				}
				return false;
			}

			String EmitMain(const Function& fn)
			{
				String out;
				out += "void "_s + _prog + "_Fragment(const nCine::RHI::Software::FragmentShaderInput& in)\n{\n"_s;
				out += "\tusing namespace nCine::RHI::Software::sw;\n"_s;
				out += "\tconst "_s + _prog + "_Uniforms* unis = static_cast<const "_s + _prog + "_Uniforms*>(in.userData);\n"_s;
				out += "\t(void)unis;\n\t(void)in;\n"_s;
				out += "\tvec4 COLOR;\n"_s;
				EmitBlockInner(fn.Body.get(), "\t"_s, out);
				out += "\tpackColor(COLOR, in.rgba);\n}\n"_s;
				return out;
			}

			String EmitHelper(const Function& fn)
			{
				String out;
				// Every helper takes the fragment input as its first parameter and re-derives `unis`, so a
				// helper that samples a texture, reads a uniform or reads a constant varying compiles the same
				// as the entry point (the caller threads `in` through). Unused ones just ignore it.
				out += "static "_s + CppTypeName(fn.RetType, true) + " "_s + _prog + "_"_s + fn.Name +
					"(const nCine::RHI::Software::FragmentShaderInput& in"_s;
				for (std::size_t i = 0; i < fn.Params.size(); i++) {
					out += ", "_s + CppTypeName(fn.Params[i].Type, true) + " "_s + fn.Params[i].Name;
				}
				out += ")\n{\n\tusing namespace nCine::RHI::Software::sw;\n"_s;
				out += "\tconst "_s + _prog + "_Uniforms* unis = static_cast<const "_s + _prog + "_Uniforms*>(in.userData);\n"_s;
				out += "\t(void)unis;\n\t(void)in;\n"_s;
				EmitBlockInner(fn.Body.get(), "\t"_s, out);
				out += "}\n"_s;
				return out;
			}

			// Emits the statements of a block without its own braces, each at the given indent
			void EmitBlockInner(const Stmt* block, const String& indent, String& out)
			{
				if (block == nullptr) return;
				for (const StmtPtr& s : block->Body) {
					EmitStmt(s.get(), indent, out);
				}
			}

			// Emits the body of a control-flow branch as brace-wrapped statements
			void EmitBranch(const Stmt* s, const String& indent, String& out)
			{
				String inner = indent + "\t"_s;
				out += "{\n"_s;
				if (s != nullptr && s->Kind == StmtKind::Block) {
					EmitBlockInner(s, inner, out);
				} else if (s != nullptr) {
					EmitStmt(s, inner, out);
				}
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
						out += indent + CppTypeName(s->DeclType, false) + " "_s + s->DeclName;
						if (s->Init != nullptr) out += " = "_s + EmitExpr(s->Init.get(), 0);
						out += ";\n"_s;
						// Each extra declarator becomes its own C++ declaration of the shared type
						for (const std::pair<String, ExprPtr>& d : s->ExtraDecls) {
							out += indent + CppTypeName(s->DeclType, false) + " "_s + d.first;
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
						if (s->Else != nullptr) {
							out += " else "_s;
							EmitBranch(s->Else.get(), indent, out);
						}
						out += "\n"_s;
						break;
					case StmtKind::For: {
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
			}

			String EmitForInit(const Stmt* s)
			{
				if (s == nullptr) return {};
				if (s->Kind == StmtKind::VarDecl) {
					String r = CppTypeName(s->DeclType, false) + " "_s + s->DeclName;
					if (s->Init != nullptr) r += " = "_s + EmitExpr(s->Init.get(), 0);
					// Comma-separated extra declarators share the type keyword in a C++ for-init clause
					for (const std::pair<String, ExprPtr>& d : s->ExtraDecls) {
						r += ", "_s + d.first;
						if (d.second != nullptr) r += " = "_s + EmitExpr(d.second.get(), 0);
					}
					return r;
				}
				if (s->Kind == StmtKind::ExprStmt) {
					return EmitExpr(s->E.get(), 0);
				}
				return {};
			}

			// Precedence of an emitted expression, so children get minimal correct parentheses
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

			String EmitCore(const Expr* e)
			{
				switch (e->Kind) {
					case ExprKind::IntLit: return e->Text;
					case ExprKind::FloatLit: return EmitFloatLiteral(e->Text);
					case ExprKind::BoolLit: return e->Text;
					case ExprKind::Ident: return EmitIdent(e->Text);
					case ExprKind::Member: return EmitMember(e);
					case ExprKind::Index: Fail("array indexing is unsupported in the fragment path"_s); return {};
					case ExprKind::Call: return EmitCall(e);
					case ExprKind::Unary: {
						String inner = EmitExpr(e->A.get(), 90);
						if (e->Postfix) return inner + e->Text;					// x++ / x--
						if (e->Text == "-" && !inner.empty() && inner[0] == '-') return "- "_s + inner;	// avoid a spurious "--"
						return e->Text + inner;
					}
					case ExprKind::Binary: {
						std::int32_t p = BinPrec(e->Text);
						String op = (e->Text == "^^" ? String{"!="_s} : e->Text);		// GLSL logical xor -> C++ !=
						return EmitExpr(e->A.get(), p) + " "_s + op + " "_s + EmitExpr(e->B.get(), p + 1);
					}
					case ExprKind::Assign:
						return EmitExpr(e->A.get(), 1) + " "_s + e->Text + " "_s + EmitExpr(e->B.get(), 1);
					case ExprKind::Conditional:
						return EmitExpr(e->A.get(), 3) + " ? "_s + EmitExpr(e->B.get(), 3) + " : "_s + EmitExpr(e->C.get(), 2);
				}
				return {};
			}

			// Normalizes a GLSL float literal to a C++ float literal (single-precision 'f' suffix)
			String EmitFloatLiteral(StringView text)
			{
				return NormalizeFloatLiteral(text);
			}

			String EmitIdent(StringView name)
			{
				if (name == "COLOR") return "COLOR"_s;
				if (name == "vTexCoords") return "vec2(in.u, in.v)"_s;
				if (name == "vColor") return "vec4(in.color[0], in.color[1], in.color[2], in.color[3])"_s;

				auto it = _globals.find(String{name});
				if (it != _globals.end()) {
					switch (it->second.Kind) {
						case SymKind::Varying: {
							// A per-instance-constant varying is read from the input struct (filled by
							// _ComputeVaryings), just like a uniform; anything else is declined with a reason.
							auto cv = _constVaryings.find(String{name});
							if (cv != _constVaryings.end() && cv->second.Ok) {
								_usedVaryings[String{name}] = cv->second;
								return "unis->"_s + name;
							}
							if (cv != _constVaryings.end()) {
								Fail(cv->second.Reason);
							} else {
								Fail("reads varying '"_s + name + "' (only vTexCoords and vColor are available to the software fragment)"_s);
							}
							return String{name};
						}
						case SymKind::Sampler:
							Fail("sampler '"_s + name + "' used outside of texture()"_s);
							return String{name};
						case SymKind::Uniform:
							return "unis->"_s + name;
						case SymKind::GlobalConst:
							return _prog + "_"_s + name;
						case SymKind::Output:
							return String{name};
					}
				}
				return String{name};		// local variable or function parameter
			}

			String EmitMember(const Expr* e)
			{
				String base = EmitExpr(e->A.get(), 100);
				StringView field = e->Text;
				for (std::size_t i = 0; i < field.size(); i++) {
					if (!"xyzwrgbastpq"_s.contains(field[i])) {
						Fail("member '."_s + field + "' is not a supported swizzle"_s);
						return base;
					}
				}
				if (!IsRuntimeSwizzle(field)) {
					Fail("swizzle '."_s + field + "' is not provided by the software runtime"_s);
					return base;
				}
				if (field.size() == 1) return base + "."_s + field;
				if (field.size() >= 2 && field.size() <= 4) return base + "."_s + field + "()"_s;
				Fail("swizzle '."_s + field + "' has an unsupported length"_s);
				return base;
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

			String EmitCall(const Expr* e)
			{
				StringView name = e->Text;

				Ty ct;
				if (TryType(name, ct)) {
					if (ct == Ty::Unsupported) { Fail("'"_s + name + "(...)' constructor (matrix) is unsupported"_s); return {}; }
					if (ct == Ty::Sampler) { Fail("sampler constructors are unsupported"_s); return {}; }
					return CppTypeName(ct, false) + "("_s + EmitArgs(e) + ")"_s;
				}

				if (name == "texture") {
					if (e->Args.size() != 2) { Fail("only 2-argument texture(sampler, uv) is supported"_s); return {}; }
					const Expr* sampler = e->Args[0].get();
					if (sampler->Kind != ExprKind::Ident) { Fail("the first argument to texture() must be a sampler uniform"_s); return {}; }
					auto it = _globals.find(sampler->Text);
					if (it == _globals.end() || it->second.Kind != SymKind::Sampler) {
						Fail("texture() first argument '"_s + sampler->Text + "' is not a sampler uniform"_s);
						return {};
					}
					if (it->second.Unit < 0) { Fail("sampler '"_s + sampler->Text + "' has no texture-unit assignment"_s); return {}; }
					// Primary-texel fast path: for the exact `texture(uTexture, vTexCoords)` pattern the
					// software rasterizer's gather phase has already fetched this pixel's texel into
					// `in.rgba`, so the runtime can read it back instead of re-sampling. Both conditions
					// are load-bearing: the UV argument must be the plain interpolated texcoord identifier
					// (any offset or computed UV addresses a different texel), and the sampler must be the
					// primary one the software device keys the gather on (SwDevice sets FFState::textureUnit
					// from the reflected unit of the sampler NAMED "uTexture" - the same unit emitted here).
					// The runtime helper itself falls back to swTexture() for the cases the gather does not
					// reproduce byte-exactly (unbound unit, bilinear filtering).
					const Expr* uvArg = e->Args[1].get();
					if (sampler->Text == "uTexture" && uvArg->Kind == ExprKind::Ident && uvArg->Text == "vTexCoords") {
						return "swTexturePrimary(in, "_s + Death::format("{}", it->second.Unit) + ")"_s;
					}
					return "swTexture(in, "_s + Death::format("{}", it->second.Unit) + ", "_s + EmitExpr(e->Args[1].get(), 0) + ")"_s;
				}

				if (IsBannedCall(name)) { Fail("'"_s + name + "' is unsupported in the software fragment path"_s); return {}; }
				if (IsBuiltin(name)) return name + "("_s + EmitArgs(e) + ")"_s;
				if (IsUserFunc(name)) {
					// Helpers take the fragment input as their first argument (see EmitHelper)
					String args = EmitArgs(e);
					return _prog + "_"_s + name + "(in"_s + (args.empty() ? String{} : String(", "_s + args)) + ")"_s;
				}

				Fail("unknown function '"_s + name + "'"_s);
				return {};
			}
		};
	}

	// --- Public entry point --------------------------------------------------------------------------

	namespace
	{
		// Splits, strips comments from, preprocesses and tokenizes a GLSL stage source, appending a trailing
		// End token. Returns false (with a reason) only if preprocessing fails.
		bool TokenizeStage(StringView glsl, std::vector<GlslToken>& tokens, String& reason)
		{
			std::vector<SourceLine> lines;
			ShaderParser::SplitLines(glsl, lines);
			ShaderParser::StripComments(lines);

			Preprocessor preprocessor;
			std::vector<SourceLine> preprocessed;
			Diagnostic diag;
			if (!preprocessor.Run(lines, preprocessed, diag)) {
				reason = "preprocessing failed: "_s + diag.Message;
				return false;
			}

			for (std::size_t i = 0; i < preprocessed.size(); i++) {
				const String& text = preprocessed[i].Text;
				GlslExprTokenizer::Tokenize(text, 0, text.size(), i, tokens);
			}
			GlslToken end;
			end.Type = GlslTokenType::End;
			tokens.push_back(std::move(end));
			return true;
		}
	}

	GlslToCppResult GlslToCpp::TranspileFragment(StringView programName, StringView fragmentGlsl,
		StringView vertexGlsl, const std::vector<SamplerBinding>& samplers,
		const std::vector<GlslInstanceMember>& instanceMembers)
	{
		GlslToCppResult result;

		std::vector<GlslToken> fragTokens;
		if (!TokenizeStage(fragmentGlsl, fragTokens, result.UnsupportedReason)) {
			return result;
		}
		Parser parser(fragTokens, samplers, /*vertexMode*/ false);
		parser.Run();
		if (!parser.Ok()) {
			result.UnsupportedReason = parser.Reason();
			return result;
		}

		// Analyze the vertex stage for per-instance-constant varyings. A vertex parse/preprocess failure is
		// tolerated (yields no constant varyings): it only matters if the fragment actually reads a varying,
		// in which case the read is declined with a clear message.
		std::vector<GlslToken> vertTokens;
		{
			String vertReason;
			if (!TokenizeStage(vertexGlsl, vertTokens, vertReason)) {
				vertTokens.clear();
				GlslToken end;
				end.Type = GlslTokenType::End;
				vertTokens.push_back(std::move(end));
			}
		}
		Parser vertexParser(vertTokens, samplers, /*vertexMode*/ true);
		vertexParser.Run();
		VertexAnalyzer analyzer(vertexParser, instanceMembers, parser.Uniforms());
		analyzer.Run();

		Emitter emitter(programName, parser, analyzer.Varyings());
		String code = emitter.Emit();
		if (!emitter.Ok()) {
			result.UnsupportedReason = emitter.Reason();
			return result;
		}

		result.Supported = true;
		result.Code = std::move(code);
		result.HasComputeVaryings = emitter.HasComputeVaryings();
		result.ConstVaryingNames = emitter.ConstVaryingNames();
		return result;
	}
}
