#pragma once

/**
	@file GlslTypedAst.h

	Shared TYPED GLSL AST and front-end parser for the AST-based ShaderCompiler lowerings.

	Where @ref GlslAst.h holds only the type-FREE expression node (@ref Expr / @ref ExprKind) plus the
	stage tokenizer, this header holds the parts that DO carry type information but are nevertheless the
	same for every non-software target: the translated-subset type system (@ref Ty / @ref TyRef and its
	helpers), the declaration records (uniform blocks, samplers, attributes, varyings, fragment outputs,
	global vars, user structs), the statement model (@ref Stmt / @ref StmtKind) and the @ref Parser that
	grows all of them from the shared lexer/preprocessor. It is consumed by every emitter that re-emits a
	whole stage from a real AST — the GLSL-to-HLSL emitter (@ref Hlsl.h), the Vulkan-flavored GLSL emitter
	(@ref Vulkan.h) and the ESSL 100 emitter (@ref Essl100.h) — each of which keeps only its own target
	spelling / declaration formatting.

	The software transpiler (@ref GlslToCpp.h) is NOT a consumer: it folds `uint`/matrices away and grows
	its own lighter type-folded AST, so its model stays private to that unit.

	This is a HEADER-ONLY unit on purpose: the ESSL 100 emitter is compiled into the engine's OpenGL|ES 2.0
	profile (see cmake/ncine_sources.cmake), so the shared parser must not require a new translation unit.
*/

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <Containers/StringConcatenable.h>

#include "GlslAst.h"			// Expr / ExprKind / MakeExpr / TokenizeStage / IsQualifier / BinPrec
#include "ConstFold.h"		// GlslToken, GlslTokenType
#include "ShaderParser.h"	// Diagnostic, StringView

namespace ShaderCompiler
{
	// The parser builds diagnostics with the `_s` StringView literal; every consumer of this header
	// (Hlsl.cpp, Vulkan.cpp, Essl100.cpp) already opens the same namespace at file scope, so importing
	// it here is redundant for them and harmless (a using-directive stays local to the including TU).
	using namespace Death::Containers::Literals;

	// --- Type system of the translated subset --------------------------------------------------------

	/** @brief Scalar/vector/matrix/opaque type of the translated GLSL subset (uint/uvec kept distinct) */
	enum class Ty
	{
		Void, Float, Int, UInt, Bool,
		Vec2, Vec3, Vec4,
		IVec2, IVec3, IVec4,
		UVec2, UVec3, UVec4,
		BVec2, BVec3, BVec4,
		Mat2, Mat3, Mat4,
		Sampler2D, Sampler3D, SamplerCube,
		Struct, Unknown
	};

	/** @brief A type plus, for Ty::Struct, the user struct name (so struct-typed members resolve their fields) */
	struct TyRef
	{
		Ty T = Ty::Unknown;
		String S;		// struct name when T == Struct
	};

	inline bool IsMatrix(Ty t) { return (t == Ty::Mat2 || t == Ty::Mat3 || t == Ty::Mat4); }
	inline bool IsVector(Ty t)
	{
		switch (t) {
			case Ty::Vec2: case Ty::Vec3: case Ty::Vec4:
			case Ty::IVec2: case Ty::IVec3: case Ty::IVec4:
			case Ty::UVec2: case Ty::UVec3: case Ty::UVec4:
			case Ty::BVec2: case Ty::BVec3: case Ty::BVec4:
				return true;
			default: return false;
		}
	}
	inline bool IsSampler(Ty t) { return (t == Ty::Sampler2D || t == Ty::Sampler3D || t == Ty::SamplerCube); }

	// Vector/scalar component count (1 for scalar; matrices report their dimension)
	inline std::int32_t Comps(Ty t)
	{
		switch (t) {
			case Ty::Vec2: case Ty::IVec2: case Ty::UVec2: case Ty::BVec2: case Ty::Mat2: return 2;
			case Ty::Vec3: case Ty::IVec3: case Ty::UVec3: case Ty::BVec3: case Ty::Mat3: return 3;
			case Ty::Vec4: case Ty::IVec4: case Ty::UVec4: case Ty::BVec4: case Ty::Mat4: return 4;
			default: return 1;
		}
	}

	// The scalar element type of a vector/scalar (Float for matrices)
	inline Ty BaseScalar(Ty t)
	{
		switch (t) {
			case Ty::Int: case Ty::IVec2: case Ty::IVec3: case Ty::IVec4: return Ty::Int;
			case Ty::UInt: case Ty::UVec2: case Ty::UVec3: case Ty::UVec4: return Ty::UInt;
			case Ty::Bool: case Ty::BVec2: case Ty::BVec3: case Ty::BVec4: return Ty::Bool;
			default: return Ty::Float;
		}
	}

	// A vector type from a scalar base and a component count (n==1 yields the scalar itself)
	inline Ty MakeVec(Ty scalar, std::int32_t n)
	{
		if (n <= 1) return scalar;
		if (scalar == Ty::Int) return (n == 2 ? Ty::IVec2 : n == 3 ? Ty::IVec3 : Ty::IVec4);
		if (scalar == Ty::UInt) return (n == 2 ? Ty::UVec2 : n == 3 ? Ty::UVec3 : Ty::UVec4);
		if (scalar == Ty::Bool) return (n == 2 ? Ty::BVec2 : n == 3 ? Ty::BVec3 : Ty::BVec4);
		return (n == 2 ? Ty::Vec2 : n == 3 ? Ty::Vec3 : Ty::Vec4);
	}

	// The column vector of a matrix (Mat4 -> Vec4), for matrix subscripting
	inline Ty MatrixColumn(Ty t)
	{
		switch (t) {
			case Ty::Mat2: return Ty::Vec2;
			case Ty::Mat3: return Ty::Vec3;
			case Ty::Mat4: return Ty::Vec4;
			default: return Ty::Unknown;
		}
	}

	// Recognizes a built-in GLSL type keyword (uint/uvec kept distinct — HLSL has them)
	inline bool TryBuiltinType(StringView k, Ty& out)
	{
		if (k == "void") { out = Ty::Void; return true; }
		if (k == "float") { out = Ty::Float; return true; }
		if (k == "int") { out = Ty::Int; return true; }
		if (k == "uint") { out = Ty::UInt; return true; }
		if (k == "bool") { out = Ty::Bool; return true; }
		if (k == "vec2") { out = Ty::Vec2; return true; }
		if (k == "vec3") { out = Ty::Vec3; return true; }
		if (k == "vec4") { out = Ty::Vec4; return true; }
		if (k == "ivec2") { out = Ty::IVec2; return true; }
		if (k == "ivec3") { out = Ty::IVec3; return true; }
		if (k == "ivec4") { out = Ty::IVec4; return true; }
		if (k == "uvec2") { out = Ty::UVec2; return true; }
		if (k == "uvec3") { out = Ty::UVec3; return true; }
		if (k == "uvec4") { out = Ty::UVec4; return true; }
		if (k == "bvec2") { out = Ty::BVec2; return true; }
		if (k == "bvec3") { out = Ty::BVec3; return true; }
		if (k == "bvec4") { out = Ty::BVec4; return true; }
		if (k == "mat2") { out = Ty::Mat2; return true; }
		if (k == "mat3") { out = Ty::Mat3; return true; }
		if (k == "mat4") { out = Ty::Mat4; return true; }
		if (k == "sampler2D") { out = Ty::Sampler2D; return true; }
		if (k == "sampler3D") { out = Ty::Sampler3D; return true; }
		if (k == "samplerCube") { out = Ty::SamplerCube; return true; }
		return false;
	}

	// --- Statement AST -------------------------------------------------------------------------------
	// The expression AST (Expr / ExprKind / ExprPtr / MakeExpr) is shared and lives in GlslAst.h; the
	// statement/declaration model is shared here across the AST-based non-software emitters.

	enum class StmtKind { Block, VarDecl, ExprStmt, If, For, Return };

	struct Stmt
	{
		StmtKind Kind;
		std::vector<std::unique_ptr<Stmt>> Body;		// Block
		TyRef DeclType;									// VarDecl
		bool DeclConst = false;
		std::int32_t DeclArraySize = 0;					// VarDecl: 0 = scalar, >0 = local array "T name[N]"
		String DeclName;
		ExprPtr Init;
		std::vector<std::pair<String, ExprPtr>> ExtraDecls;
		ExprPtr E;										// ExprStmt / Return value
		ExprPtr Cond;									// If
		std::unique_ptr<Stmt> Then, Else;
		std::unique_ptr<Stmt> ForInit; ExprPtr ForCond; ExprPtr ForUpdate; std::unique_ptr<Stmt> ForBody;
	};
	using StmtPtr = std::unique_ptr<Stmt>;

	inline StmtPtr MakeStmt(StmtKind kind)
	{
		auto s = std::make_unique<Stmt>();
		s->Kind = kind;
		return s;
	}

	struct Param { TyRef Type; String Name; std::int32_t ArraySize = 0; String Qualifier; };	// Qualifier: "" / "out" / "inout"
	struct Function { TyRef RetType; String Name; std::vector<Param> Params; StmtPtr Body; };

	// --- Declaration records -------------------------------------------------------------------------

	struct Field { String Name; TyRef Type; std::int32_t ArraySize = 0; bool SymbolicArray = false; };
	struct StructDecl { String Name; std::vector<Field> Fields; };
	struct BlockDecl { String Name; String Instance; std::vector<Field> Members; };
	struct UniformDecl { String Name; TyRef Type; std::int32_t ArraySize = 0; };
	struct SamplerDecl { String Name; Ty Type = Ty::Sampler2D; };
	struct VaryingDecl { String Name; TyRef Type; bool Flat = false; };
	struct AttributeDecl { String Name; TyRef Type; std::int32_t Location = -1; };
	struct GlobalVarDecl { String Name; TyRef Type; bool IsConst = false; ExprPtr Init; };

	// --- Parser --------------------------------------------------------------------------------------

	/**
		@brief Grows the shared typed AST (declarations + per-function statement trees) from an already-lowered
		modern-GLSL stage token stream.

		Handles the translated subset the AST-based emitters share: global-scope struct/block/uniform/sampler/
		interface declarations, layout(location) qualifiers, gl_VertexID, and function bodies as @ref Stmt trees.
		Constructs outside the subset make @ref Ok() return false with a @ref Reason(); the emitter then declines.
	*/
	class Parser
	{
	public:
		Parser(const std::vector<GlslToken>& tokens, bool vertexStage)
			: _toks(tokens), _vertexStage(vertexStage) {}

		void Run()
		{
			while (!AtEnd() && _ok) {
				if (IsOp(";")) { Next(); continue; }
				ParseTopLevel();
			}
		}

		bool Ok() const { return _ok; }
		const String& Reason() const { return _reason; }

		std::vector<StructDecl> Structs;
		std::vector<BlockDecl> Blocks;
		std::vector<UniformDecl> Uniforms;
		std::vector<SamplerDecl> Samplers;
		std::vector<VaryingDecl> Varyings;			// VS out / FS in
		std::vector<AttributeDecl> Attributes;		// VS in
		std::vector<GlobalVarDecl> GlobalVars;
		std::vector<Function> Functions;
		// FS "out" declarations in DECLARATION ORDER; index i renders to SV_Target<i>. A single output
		// keeps the historical "PSMain(...) : SV_Target" shape; multiple emit a PsOutput struct.
		struct FragOutputDecl { String Name; TyRef Type; };
		std::vector<FragOutputDecl> FragOutputs;

	private:
		const std::vector<GlslToken>& _toks;
		bool _vertexStage;
		std::size_t _pos = 0;
		bool _ok = true;
		String _reason;
		std::set<String> _structNames;

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

		void SkipQualifiers()
		{
			while (Is(GlslTokenType::Identifier) && IsQualifier(Cur().Text)) {
				Next();
			}
		}

		bool TryType(StringView k, TyRef& out)
		{
			Ty t;
			if (TryBuiltinType(k, t)) { out = { t, {} }; return true; }
			if (_structNames.find(String{k}) != _structNames.end()) { out = { Ty::Struct, String{k} }; return true; }
			return false;
		}

		TyRef ParseType()
		{
			if (!Is(GlslTokenType::Identifier)) { Fail("expected a type name"_s); return {}; }
			TyRef t;
			if (!TryType(Cur().Text, t)) { Fail("unknown type '"_s + Cur().Text + "'"_s); return {}; }
			Next();
			return t;
		}

		bool LooksLikeDecl() const
		{
			std::size_t p = _pos;
			while (At(p).Type == GlslTokenType::Identifier && IsQualifier(At(p).Text)) p++;
			if (At(p).Type != GlslTokenType::Identifier) return false;
			Ty t;
			if (TryBuiltinType(At(p).Text, t)) return true;
			return (_structNames.find(String{At(p).Text}) != _structNames.end());
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

		// Reads an "[ <int-or-identifier> ]" array suffix (already positioned at '['); sets @p symbolic when
		// the size is a non-numeric token (e.g. BATCH_SIZE). Returns the numeric size, or 1 when symbolic.
		std::int32_t ParseArraySuffix(bool& symbolic)
		{
			symbolic = false;
			Expect("[");
			std::int32_t size = 1;
			if (Is(GlslTokenType::IntLiteral) || Is(GlslTokenType::UIntLiteral)) {
				size = std::atoi(String::nullTerminatedView(Cur().Text).data());
				Next();
			} else {
				// A symbolic size (BATCH_SIZE / LIGHT_COUNT); consume tokens up to ']'
				symbolic = true;
				while (!AtEnd() && !IsOp("]")) Next();
			}
			Expect("]");
			return size;
		}

		// Parses "layout(...)", returning an explicit "location = N" if present (-1 otherwise)
		std::int32_t ParseLayout()
		{
			std::int32_t location = -1;
			Next();		// "layout"
			if (!Expect("(")) return location;
			std::int32_t depth = 1;
			while (!AtEnd() && depth > 0) {
				if (IsOp("(")) { depth++; Next(); continue; }
				if (IsOp(")")) { depth--; Next(); continue; }
				if (IsKw("location")) {
					Next();
					if (IsOp("=")) {
						Next();
						if (Is(GlslTokenType::IntLiteral) || Is(GlslTokenType::UIntLiteral)) {
							location = std::atoi(String::nullTerminatedView(Cur().Text).data());
							Next();
						}
					}
					continue;
				}
				Next();
			}
			return location;
		}

		void ParseTopLevel()
		{
			if (IsKw("struct")) { ParseStruct(); return; }

			std::int32_t location = -1;
			if (IsKw("layout")) {
				location = ParseLayout();
			}
			if (IsKw("precision")) {
				while (!AtEnd() && !IsOp(";")) Next();
				if (IsOp(";")) Next();
				return;
			}

			bool isIn = false, isOut = false, isUniform = false, isConst = false, isFlat = false;
			for (;;) {
				if (IsKw("in")) { isIn = true; Next(); }
				else if (IsKw("out")) { isOut = true; Next(); }
				else if (IsKw("uniform")) { isUniform = true; Next(); }
				else if (IsKw("const")) { isConst = true; Next(); }
				else if (IsKw("flat")) { isFlat = true; Next(); }
				else if (Is(GlslTokenType::Identifier) && IsQualifier(Cur().Text)) { Next(); }
				else break;
			}

			// A std140 uniform block: "uniform <Name> { ... } [instance];"
			if (isUniform && Is(GlslTokenType::Identifier) && !LooksLikeType(Cur().Text) &&
				At(_pos + 1).Type == GlslTokenType::Operator && At(_pos + 1).Text == "{") {
				ParseBlock();
				return;
			}

			TyRef t = ParseType();
			if (!_ok) return;
			if (!Is(GlslTokenType::Identifier)) { Fail("expected a name after the type"_s); return; }
			String name = Cur().Text;
			Next();

			if (IsOp("(")) { ParseFunctionRest(t, std::move(name)); return; }

			// Array suffix on the name (e.g. "uniform vec4 uLights[N];")
			std::int32_t arraySize = 0;
			bool symbolic = false;
			if (IsOp("[")) { arraySize = ParseArraySuffix(symbolic); }

			ExprPtr init;
			if (IsOp("=")) { Next(); init = ParseExpression(0); if (!_ok) return; }
			// Extra declarators are uncommon at global scope; decline rather than mis-handle
			if (IsOp(",")) { Fail("multiple global declarators are unsupported"_s); return; }
			if (!Expect(";")) return;

			if (isUniform) {
				if (IsSampler(t.T)) {
					SamplerDecl s; s.Name = std::move(name); s.Type = t.T;
					Samplers.push_back(std::move(s));
				} else {
					UniformDecl u; u.Name = std::move(name); u.Type = t; u.ArraySize = (symbolic ? 0 : arraySize);
					Uniforms.push_back(std::move(u));
				}
			} else if (isIn) {
				if (_vertexStage) {
					AttributeDecl a; a.Name = std::move(name); a.Type = t; a.Location = location;
					Attributes.push_back(std::move(a));
				} else {
					VaryingDecl v; v.Name = std::move(name); v.Type = t; v.Flat = isFlat;
					Varyings.push_back(std::move(v));
				}
			} else if (isOut) {
				if (_vertexStage) {
					VaryingDecl v; v.Name = std::move(name); v.Type = t; v.Flat = isFlat;
					Varyings.push_back(std::move(v));
				} else {
					// Render-target index = declaration order (SV_Target0..N); an explicit location would
					// diverge from that scheme (and from the offline SPIR-V emission), so it is declined
					if (location >= 0) { Fail("explicit locations on fragment outputs are unsupported (render targets follow declaration order)"_s); return; }
					FragOutputs.push_back({ std::move(name), t });
				}
			} else {
				GlobalVarDecl g; g.Name = std::move(name); g.Type = t; g.IsConst = isConst; g.Init = std::move(init);
				GlobalVars.push_back(std::move(g));
			}
		}

		bool LooksLikeType(StringView k) const
		{
			Ty t;
			if (TryBuiltinType(k, t)) return true;
			return (_structNames.find(String{k}) != _structNames.end());
		}

		// Parses one aggregate member: "[qualifiers] Type [arr] Name [arr] ;"
		bool ParseField(Field& out)
		{
			SkipQualifiers();
			TyRef t = ParseType();
			if (!_ok) return false;
			std::int32_t arraySize = 0;
			bool symbolic = false;
			if (IsOp("[")) { arraySize = ParseArraySuffix(symbolic); }		// "Instance[BATCH_SIZE] instances;"
			if (!Is(GlslTokenType::Identifier)) { Fail("expected a member name"_s); return false; }
			out.Name = Cur().Text;
			out.Type = t;
			Next();
			if (IsOp("[")) { arraySize = ParseArraySuffix(symbolic); }		// "vec4 name[N];"
			out.ArraySize = arraySize;
			out.SymbolicArray = symbolic;
			if (!Expect(";")) return false;
			return true;
		}

		void ParseStruct()
		{
			Next();		// "struct"
			if (!Is(GlslTokenType::Identifier)) { Fail("expected a struct name"_s); return; }
			StructDecl s;
			s.Name = Cur().Text;
			Next();
			if (!Expect("{")) return;
			while (!IsOp("}") && !AtEnd() && _ok) {
				Field f;
				if (!ParseField(f)) return;
				s.Fields.push_back(std::move(f));
			}
			if (!Expect("}")) return;
			if (Is(GlslTokenType::Identifier)) Next();		// optional inline instance name (unused)
			if (IsOp(";")) Next();
			_structNames.insert(s.Name);
			Structs.push_back(std::move(s));
		}

		void ParseBlock()
		{
			BlockDecl b;
			b.Name = Cur().Text;
			Next();
			if (!Expect("{")) return;
			while (!IsOp("}") && !AtEnd() && _ok) {
				Field f;
				if (!ParseField(f)) return;
				b.Members.push_back(std::move(f));
			}
			if (!Expect("}")) return;
			if (Is(GlslTokenType::Identifier)) { b.Instance = Cur().Text; Next(); }
			if (!Expect(";")) return;
			Blocks.push_back(std::move(b));
		}

		void ParseFunctionRest(TyRef retType, String name)
		{
			Expect("(");
			Function fn;
			fn.RetType = retType;
			fn.Name = std::move(name);
			if (IsKw("void") && At(_pos + 1).Type == GlslTokenType::Operator && At(_pos + 1).Text == ")") { Next(); }
			while (!IsOp(")") && !AtEnd() && _ok) {
				String qual;
				while (IsKw("in") || IsKw("out") || IsKw("inout")) {
					if (Cur().Text == "out") qual = "out";
					else if (Cur().Text == "inout") qual = "inout";
					Next();
				}
				SkipQualifiers();
				TyRef pt = ParseType();
				if (!_ok) return;
				std::int32_t arraySize = 0;
				bool sym = false;
				if (IsOp("[")) { arraySize = ParseArraySuffix(sym); }			// "vec3[9] name"
				if (!Is(GlslTokenType::Identifier)) { Fail("expected a parameter name"_s); return; }
				Param p; p.Type = pt; p.Name = Cur().Text; p.Qualifier = std::move(qual); Next();
				if (IsOp("[")) { arraySize = ParseArraySuffix(sym); }			// "vec3 name[9]"
				p.ArraySize = arraySize;
				fn.Params.push_back(std::move(p));
				if (IsOp(",")) { Next(); continue; }
				break;
			}
			if (!Expect(")")) return;
			fn.Body = ParseBlock2();
			if (!_ok) return;
			Functions.push_back(std::move(fn));
		}

		StmtPtr ParseBlock2()
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
			if (IsOp("{")) return ParseBlock2();
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
			bool isConst = false;
			while (Is(GlslTokenType::Identifier) && IsQualifier(Cur().Text)) {
				if (Cur().Text == "const") isConst = true;
				Next();
			}
			TyRef t = ParseType();
			if (!_ok) return nullptr;
			if (!Is(GlslTokenType::Identifier)) { Fail("expected a variable name"_s); return nullptr; }
			StmtPtr s = MakeStmt(StmtKind::VarDecl);
			s->DeclType = t;
			s->DeclConst = isConst;
			s->DeclName = Cur().Text;
			Next();
			if (IsOp("[")) {
				bool sym;
				s->DeclArraySize = ParseArraySuffix(sym);
				if (sym) { Fail("symbolically-sized local arrays are unsupported"_s); return nullptr; }
			}
			if (s->DeclArraySize > 0 && IsOp("=")) { Fail("local array initializers are unsupported"_s); return nullptr; }
			if (IsOp("=")) { Next(); s->Init = ParseExpression(1); if (!_ok) return nullptr; }
			while (IsOp(",")) {
				Next();
				if (!Is(GlslTokenType::Identifier)) { Fail("expected a variable name"_s); return nullptr; }
				String name = Cur().Text;
				Next();
				if (IsOp("[")) { Fail("local array declarations are unsupported"_s); return nullptr; }
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
				if (op == "-" || op == "!" || op == "~" || op == "++" || op == "--") {
					String o = Cur().Text;
					Next();
					ExprPtr v = ParseUnary();
					ExprPtr node = MakeExpr(ExprKind::Unary, std::move(o));
					node->A = std::move(v);
					return node;
				}
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
					Next();
					ExprPtr idx = ParseExpression(0);
					if (!_ok) return e;
					if (!Expect("]")) return e;
					ExprPtr node = MakeExpr(ExprKind::Index);
					node->A = std::move(e);
					node->B = std::move(idx);
					e = std::move(node);
				} else if (IsOp("++") || IsOp("--")) {
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
				case GlslTokenType::IntLiteral: { ExprPtr e = MakeExpr(ExprKind::IntLit, t.Text); Next(); return e; }
				case GlslTokenType::FloatLiteral: { ExprPtr e = MakeExpr(ExprKind::FloatLit, t.Text); Next(); return e; }
				case GlslTokenType::BoolLiteral: { ExprPtr e = MakeExpr(ExprKind::BoolLit, t.Text); Next(); return e; }
				case GlslTokenType::UIntLiteral: { ExprPtr e = MakeExpr(ExprKind::UIntLit, t.Text); Next(); return e; }
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
}
