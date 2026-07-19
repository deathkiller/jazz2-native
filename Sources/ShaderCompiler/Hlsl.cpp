#include "Hlsl.h"
#include "ShaderParser.h"
#include "ConstFold.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

#include <Base/Format.h>
#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>

using namespace Death::Containers::Literals;

namespace ShaderCompiler
{
	namespace
	{
		// --- Type system of the translated subset --------------------------------------------------

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

		/** A type plus, for Ty::Struct, the user struct name (so struct-typed members resolve their fields) */
		struct TyRef
		{
			Ty T = Ty::Unknown;
			String S;		// struct name when T == Struct
		};

		bool IsMatrix(Ty t) { return (t == Ty::Mat2 || t == Ty::Mat3 || t == Ty::Mat4); }
		bool IsVector(Ty t)
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
		bool IsSampler(Ty t) { return (t == Ty::Sampler2D || t == Ty::Sampler3D || t == Ty::SamplerCube); }

		// Vector/scalar component count (1 for scalar; matrices report their dimension)
		std::int32_t Comps(Ty t)
		{
			switch (t) {
				case Ty::Vec2: case Ty::IVec2: case Ty::UVec2: case Ty::BVec2: case Ty::Mat2: return 2;
				case Ty::Vec3: case Ty::IVec3: case Ty::UVec3: case Ty::BVec3: case Ty::Mat3: return 3;
				case Ty::Vec4: case Ty::IVec4: case Ty::UVec4: case Ty::BVec4: case Ty::Mat4: return 4;
				default: return 1;
			}
		}

		// The scalar element type of a vector/scalar (Float for matrices)
		Ty BaseScalar(Ty t)
		{
			switch (t) {
				case Ty::Int: case Ty::IVec2: case Ty::IVec3: case Ty::IVec4: return Ty::Int;
				case Ty::UInt: case Ty::UVec2: case Ty::UVec3: case Ty::UVec4: return Ty::UInt;
				case Ty::Bool: case Ty::BVec2: case Ty::BVec3: case Ty::BVec4: return Ty::Bool;
				default: return Ty::Float;
			}
		}

		// A vector type from a scalar base and a component count (n==1 yields the scalar itself)
		Ty MakeVec(Ty scalar, std::int32_t n)
		{
			if (n <= 1) return scalar;
			if (scalar == Ty::Int) return (n == 2 ? Ty::IVec2 : n == 3 ? Ty::IVec3 : Ty::IVec4);
			if (scalar == Ty::UInt) return (n == 2 ? Ty::UVec2 : n == 3 ? Ty::UVec3 : Ty::UVec4);
			if (scalar == Ty::Bool) return (n == 2 ? Ty::BVec2 : n == 3 ? Ty::BVec3 : Ty::BVec4);
			return (n == 2 ? Ty::Vec2 : n == 3 ? Ty::Vec3 : Ty::Vec4);
		}

		// The column vector of a matrix (Mat4 -> Vec4), for matrix subscripting
		Ty MatrixColumn(Ty t)
		{
			switch (t) {
				case Ty::Mat2: return Ty::Vec2;
				case Ty::Mat3: return Ty::Vec3;
				case Ty::Mat4: return Ty::Vec4;
				default: return Ty::Unknown;
			}
		}

		// Recognizes a built-in GLSL type keyword (uint/uvec kept distinct — HLSL has them)
		bool TryBuiltinType(StringView k, Ty& out)
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

		// HLSL spelling of a subset type
		String HlslType(const TyRef& t)
		{
			switch (t.T) {
				case Ty::Void: return "void"_s;
				case Ty::Float: return "float"_s;
				case Ty::Int: return "int"_s;
				case Ty::UInt: return "uint"_s;
				case Ty::Bool: return "bool"_s;
				case Ty::Vec2: return "float2"_s;
				case Ty::Vec3: return "float3"_s;
				case Ty::Vec4: return "float4"_s;
				case Ty::IVec2: return "int2"_s;
				case Ty::IVec3: return "int3"_s;
				case Ty::IVec4: return "int4"_s;
				case Ty::UVec2: return "uint2"_s;
				case Ty::UVec3: return "uint3"_s;
				case Ty::UVec4: return "uint4"_s;
				case Ty::BVec2: return "bool2"_s;
				case Ty::BVec3: return "bool3"_s;
				case Ty::BVec4: return "bool4"_s;
				case Ty::Mat2: return "float2x2"_s;
				case Ty::Mat3: return "float3x3"_s;
				case Ty::Mat4: return "float4x4"_s;
				case Ty::Sampler2D: case Ty::Sampler3D: return "Texture2D"_s;
				case Ty::SamplerCube: return "TextureCube"_s;
				case Ty::Struct: return t.S;
				default: return "float"_s;
			}
		}

		bool IsQualifier(StringView k)
		{
			return (k == "const" || k == "highp" || k == "mediump" || k == "lowp" ||
				k == "flat" || k == "smooth" || k == "invariant" || k == "centroid" || k == "noperspective");
		}

		// GLSL built-in functions that keep their name in HLSL (a few are remapped separately)
		bool IsPassthroughBuiltin(StringView name)
		{
			static const char* const kNames[] = {
				"radians", "degrees", "sin", "cos", "tan", "asin", "acos", "sinh", "cosh", "tanh",
				"pow", "exp", "log", "exp2", "log2", "sqrt", "abs", "sign",
				"floor", "ceil", "round", "trunc", "min", "max", "clamp",
				"step", "smoothstep", "dot", "cross", "length", "distance",
				"normalize", "reflect", "refract", "fwidth", "transpose", "determinant",
				"saturate", "lerp", "frac", "ddx", "ddy", "rsqrt", "atan2", "fmod", "isnan", "isinf",
				"any", "all"
			};
			for (const char* n : kNames) {
				if (name == n) {
					return true;
				}
			}
			return false;
		}

		// An HLSL reserved keyword or an intrinsic name a user identifier must not shadow (else e.g. a local
		// named "frac" breaks the emitted frac() intrinsic, and "point" is a geometry-shader keyword). User
		// locals/params/functions matching one are suffixed with '_' (consistently at declaration and use).
		bool IsReservedIdent(StringView name)
		{
			static const char* const kReserved[] = {
				// HLSL keywords that are hard errors as identifiers
				"point", "line", "triangle", "lineadj", "triangleadj", "sample", "matrix", "vector",
				"pixel", "half", "linear", "centroid", "nointerpolation", "noperspective", "precise",
				"snorm", "unorm", "dword", "row_major", "column_major", "min16float", "min16int", "min16uint",
				"cbuffer", "tbuffer", "register", "packoffset", "groupshared", "globallycoherent",
				// intrinsic names a same-named variable would shadow, breaking our emitted calls
				"frac", "lerp", "mul", "saturate", "ddx", "ddy", "rsqrt", "fmod", "atan2", "mad",
				"dot", "cross", "length", "distance", "normalize", "reflect", "refract", "transpose",
				"determinant", "min", "max", "clamp", "abs", "floor", "ceil", "round", "trunc", "sign",
				"step", "smoothstep", "sqrt", "pow", "exp", "log", "exp2", "log2", "sin", "cos", "tan",
				"radians", "degrees", "any", "all", "sample", "texture"
			};
			for (const char* n : kReserved) {
				if (name == n) return true;
			}
			return false;
		}

		String SanitizeIdent(StringView name)
		{
			if (IsReservedIdent(name)) return String{name} + "_"_s;
			return String{name};
		}

		// Precedence of a binary/assignment operator (assignment = 1, ternary handled separately at 2)
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

		bool IsComparisonOrLogical(StringView op)
		{
			return (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=" ||
				op == "&&" || op == "||" || op == "^^");
		}

		// --- Abstract syntax tree --------------------------------------------------------------------

		enum class ExprKind { IntLit, UIntLit, FloatLit, BoolLit, Ident, Member, Index, Call, Unary, Binary, Assign, Conditional };

		struct Expr
		{
			ExprKind Kind;
			String Text;			// literal text / identifier / member field / callee name / operator
			bool Postfix = false;	// Unary: x++/x-- vs ++x/--x
			std::unique_ptr<Expr> A, B, C;
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

		StmtPtr MakeStmt(StmtKind kind)
		{
			auto s = std::make_unique<Stmt>();
			s->Kind = kind;
			return s;
		}

		struct Param { TyRef Type; String Name; std::int32_t ArraySize = 0; String Qualifier; };	// Qualifier: "" / "out" / "inout"
		struct Function { TyRef RetType; String Name; std::vector<Param> Params; StmtPtr Body; };

		// --- Declaration records ---------------------------------------------------------------------

		struct Field { String Name; TyRef Type; std::int32_t ArraySize = 0; bool SymbolicArray = false; };
		struct StructDecl { String Name; std::vector<Field> Fields; };
		struct BlockDecl { String Name; String Instance; std::vector<Field> Members; };
		struct UniformDecl { String Name; TyRef Type; std::int32_t ArraySize = 0; };
		struct SamplerDecl { String Name; Ty Type = Ty::Sampler2D; };
		struct VaryingDecl { String Name; TyRef Type; bool Flat = false; };
		struct AttributeDecl { String Name; TyRef Type; std::int32_t Location = -1; };
		struct GlobalVarDecl { String Name; TyRef Type; bool IsConst = false; ExprPtr Init; };

		// --- Parser ----------------------------------------------------------------------------------

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

		// --- Emitter ---------------------------------------------------------------------------------

		class Emitter
		{
		public:
			Emitter(Parser& parser, bool vertexStage, const StageReflection& reflection)
				: _p(parser), _vertexStage(vertexStage), _reflection(reflection)
			{
				for (const StructDecl& s : _p.Structs) _structByName[s.Name] = &s;
				for (const BlockDecl& b : _p.Blocks) {
					if (!b.Instance.empty()) _blockInstances.insert(b.Instance);
					for (const Field& m : b.Members) {
						_memberType[m.Name] = m.Type;
						_blockMemberNames.insert(m.Name);
						if (m.SymbolicArray) _hasSymbolicArray = true;
					}
				}
				for (const UniformDecl& u : _p.Uniforms) _uniformByName[u.Name] = u.Type;
				for (const SamplerDecl& s : _p.Samplers) _samplerNames.insert(s.Name);
				for (const VaryingDecl& v : _p.Varyings) _varyingType[v.Name] = v.Type;
				for (const AttributeDecl& a : _p.Attributes) _attributeType[a.Name] = a.Type;
				for (const GlobalVarDecl& g : _p.GlobalVars) _globalVarType[g.Name] = g.Type;
				for (const Function& f : _p.Functions) if (f.Name != "main") _funcRet[f.Name] = f.RetType;
			}

			bool Ok() const { return _ok; }
			const String& Reason() const { return _reason; }

			String Emit()
			{
				const Function* main = nullptr;
				for (const Function& fn : _p.Functions) if (fn.Name == "main") { main = &fn; break; }
				if (main == nullptr) { Fail("no main() function found"_s); return {}; }
				if (!_vertexStage && _p.FragOutputs.empty()) { Fail("fragment shader has no color output"_s); return {}; }

				// Emit helper + entry bodies FIRST: this records gl_VertexID / gl_FragCoord usage that the I/O
				// structs and copy-in prologue depend on.
				String helpersOut;
				for (const Function& fn : _p.Functions) {
					if (fn.Name == "main") continue;
					helpersOut += EmitHelper(fn);
					helpersOut += "\n"_s;
				}
				String entryOut = EmitEntry(*main);
				if (!_ok) return {};

				String out;
				out += "// Generated HLSL (Shader Model 4/5) by ShaderCompiler. Do not edit manually.\n"_s;
				if (_hasSymbolicArray) {
					out += "#define BATCH_SIZE "_s + Death::format("{}", BatchSize()) + "\n\n"_s;
				}

				// User structs (declared before the cbuffers that use them)
				for (const StructDecl& s : _p.Structs) {
					out += "struct "_s + s.Name + "\n{\n"_s;
					for (const Field& f : s.Fields) {
						out += "\t"_s + HlslType(f.Type) + " "_s + f.Name + ArraySuffix(f) + ";\n"_s;
					}
					out += "};\n\n"_s;
				}

				// Uniforms: loose ones gather into a globals cbuffer, each block becomes its own cbuffer
				std::int32_t cbSlot = 0;
				if (!_p.Uniforms.empty()) {
					out += "cbuffer _Globals : register(b"_s + Death::format("{}", cbSlot++) + ")\n{\n"_s;
					for (const UniformDecl& u : _p.Uniforms) {
						out += "\t"_s + HlslType(u.Type) + " "_s + u.Name +
							(u.ArraySize > 0 ? String("["_s + Death::format("{}", u.ArraySize) + "]"_s) : String{}) + ";\n"_s;
					}
					out += "};\n\n"_s;
				}
				for (const BlockDecl& b : _p.Blocks) {
					out += "cbuffer "_s + b.Name + " : register(b"_s + Death::format("{}", cbSlot++) + ")\n{\n"_s;
					for (const Field& m : b.Members) {
						out += "\t"_s + HlslType(m.Type) + " "_s + m.Name + ArraySuffix(m) + ";\n"_s;
					}
					out += "};\n\n"_s;
				}

				// Textures + separate sampler state objects
				for (std::size_t i = 0; i < _p.Samplers.size(); i++) {
					const SamplerDecl& s = _p.Samplers[i];
					std::int32_t unit = ReflectedUnit(s.Name, static_cast<std::int32_t>(i));
					out += HlslType({ s.Type, {} }) + " "_s + s.Name + " : register(t"_s + Death::format("{}", unit) + ");\n"_s;
					out += "SamplerState "_s + s.Name + "_sampler : register(s"_s + Death::format("{}", unit) + ");\n"_s;
				}
				if (!_p.Samplers.empty()) out += "\n"_s;

				// File-scope const/mutable globals (folded away from the cbuffers; e.g. bayer matrices)
				for (const GlobalVarDecl& g : _p.GlobalVars) {
					_locals.clear();
					_arrayVars.clear();
					out += "static "_s + (g.IsConst ? String("const ") : String{}) + HlslType(g.Type) + " "_s + g.Name;
					if (g.Init != nullptr) out += " = "_s + EmitExpr(g.Init.get(), 0);
					out += ";\n"_s;
				}
				if (!_p.GlobalVars.empty()) out += "\n"_s;

				// Attributes / varyings / gl_* / the fragment color are file-scope static globals, so helper
				// functions read them exactly as the GLSL globals they were.
				out += EmitStaticGlobals();

				// I/O struct definitions
				out += EmitIoStructs();

				out += helpersOut;
				out += entryOut;
				return out;
			}

		private:
			Parser& _p;
			bool _vertexStage;
			const StageReflection& _reflection;
			bool _ok = true;
			String _reason;
			bool _hasSymbolicArray = false;
			bool _usedVertexID = false;
			bool _usedInstanceID = false;
			bool _usedFragCoord = false;

			std::map<String, const StructDecl*> _structByName;
			std::set<String> _blockInstances;
			std::set<String> _blockMemberNames;
			std::map<String, TyRef> _memberType;		// block member -> type (element type for arrays)
			std::map<String, TyRef> _uniformByName;
			std::set<String> _samplerNames;
			std::map<String, TyRef> _varyingType;
			std::map<String, TyRef> _attributeType;
			std::map<String, TyRef> _globalVarType;
			std::map<String, TyRef> _funcRet;
			std::map<String, TyRef> _locals;			// current-function locals + params (for type inference)
			std::set<String> _arrayVars;				// locals/params that are arrays (Index yields the element type, not a swizzle)

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

			std::int32_t ReflectedUnit(StringView name, std::int32_t fallback) const
			{
				for (const TextureInfo& t : _reflection.Textures) {
					if (t.Name == name && t.Unit >= 0) return t.Unit;
				}
				return fallback;
			}

			String ArraySuffix(const Field& f) const
			{
				if (f.SymbolicArray) return "[BATCH_SIZE]"_s;
				if (f.ArraySize > 0) return "["_s + Death::format("{}", f.ArraySize) + "]"_s;
				return {};
			}

			// --- Static IO globals + copy-in / copy-out --------------------------------------------

			String EmitStaticGlobals()
			{
				String out;
				if (_vertexStage) {
					if (_usedVertexID) out += "static uint gl_VertexID;\n"_s;
					if (_usedInstanceID) out += "static uint gl_InstanceID;\n"_s;
					for (const AttributeDecl& a : _p.Attributes) out += "static "_s + HlslType(a.Type) + " "_s + a.Name + ";\n"_s;
					out += "static float4 gl_Position;\n"_s;
					for (const VaryingDecl& v : _p.Varyings) out += "static "_s + HlslType(v.Type) + " "_s + v.Name + ";\n"_s;
				} else {
					if (_usedFragCoord) out += "static float4 gl_FragCoord;\n"_s;
					for (const VaryingDecl& v : _p.Varyings) out += "static "_s + HlslType(v.Type) + " "_s + v.Name + ";\n"_s;
					for (const Parser::FragOutputDecl& o : _p.FragOutputs) {
						out += "static "_s + HlslType(o.Type) + " "_s + o.Name + ";\n"_s;
					}
				}
				out += "\n"_s;
				return out;
			}

			// TEXCOORD interpolant semantic; "nointerpolation" for flat / integer varyings
			static String InterpQualifier(const VaryingDecl& v)
			{
				Ty base = BaseScalar(v.Type.T);
				if (v.Flat || base == Ty::Int || base == Ty::UInt || base == Ty::Bool) return "nointerpolation "_s;
				return {};
			}

			String EmitIoStructs()
			{
				String out;
				if (_vertexStage) {
					out += "struct VsInput\n{\n"_s;
					if (_usedVertexID) out += "\tuint _vertexID : SV_VertexID;\n"_s;
					if (_usedInstanceID) out += "\tuint _instanceID : SV_InstanceID;\n"_s;
					for (std::size_t i = 0; i < _p.Attributes.size(); i++) {
						out += "\t"_s + HlslType(_p.Attributes[i].Type) + " "_s + _p.Attributes[i].Name +
							" : TEXCOORD"_s + Death::format("{}", i) + ";\n"_s;
					}
					if (!_usedVertexID && !_usedInstanceID && _p.Attributes.empty()) {
						out += "\tuint _vertexID : SV_VertexID;\n"_s;		// a VS input struct must not be empty
					}
					out += "};\n\n"_s;

					out += "struct VsOutput\n{\n\tfloat4 _clipPosition : SV_Position;\n"_s;
					for (std::size_t i = 0; i < _p.Varyings.size(); i++) {
						out += "\t"_s + InterpQualifier(_p.Varyings[i]) + HlslType(_p.Varyings[i].Type) + " "_s +
							_p.Varyings[i].Name + " : TEXCOORD"_s + Death::format("{}", i) + ";\n"_s;
					}
					out += "};\n\n"_s;
				} else {
					out += "struct PsInput\n{\n\tfloat4 _fragCoord : SV_Position;\n"_s;
					for (std::size_t i = 0; i < _p.Varyings.size(); i++) {
						out += "\t"_s + InterpQualifier(_p.Varyings[i]) + HlslType(_p.Varyings[i].Type) + " "_s +
							_p.Varyings[i].Name + " : TEXCOORD"_s + Death::format("{}", i) + ";\n"_s;
					}
					out += "};\n\n"_s;
					// Multiple render targets: one SV_Target<i> per fragment output, in declaration order
					// (a single output keeps the historical "PSMain(...) : SV_Target" return instead)
					if (_p.FragOutputs.size() > 1) {
						out += "struct PsOutput\n{\n"_s;
						for (std::size_t i = 0; i < _p.FragOutputs.size(); i++) {
							out += "\t"_s + HlslType(_p.FragOutputs[i].Type) + " "_s + _p.FragOutputs[i].Name +
								" : SV_Target"_s + Death::format("{}", i) + ";\n"_s;
						}
						out += "};\n\n"_s;
					}
				}
				return out;
			}

			String ReturnEpilogue(const String& indent)
			{
				if (!_vertexStage) {
					if (_p.FragOutputs.size() <= 1) {
						return indent + "return "_s + _p.FragOutputs[0].Name + ";\n"_s;
					}
					String mrt;
					mrt += indent + "PsOutput _output = (PsOutput)0;\n"_s;
					for (const Parser::FragOutputDecl& o : _p.FragOutputs) {
						mrt += indent + "_output."_s + o.Name + " = "_s + o.Name + ";\n"_s;
					}
					mrt += indent + "return _output;\n"_s;
					return mrt;
				}
				String out;
				out += indent + "VsOutput _output = (VsOutput)0;\n"_s;
				out += indent + "_output._clipPosition = gl_Position;\n"_s;
				for (const VaryingDecl& v : _p.Varyings) {
					out += indent + "_output."_s + v.Name + " = "_s + v.Name + ";\n"_s;
				}
				out += indent + "return _output;\n"_s;
				return out;
			}

			String EmitEntry(const Function& main)
			{
				String body;
				_locals.clear();
				_arrayVars.clear();
				_inEntryMain = true;
				EmitBlockInner(main.Body.get(), "\t"_s, body);
				_inEntryMain = false;
				if (!_ok) return {};

				String out;
				if (_vertexStage) {
					out += "VsOutput VSMain(VsInput _input)\n{\n"_s;
					if (_usedVertexID) out += "\tgl_VertexID = _input._vertexID;\n"_s;
					if (_usedInstanceID) out += "\tgl_InstanceID = _input._instanceID;\n"_s;
					for (const AttributeDecl& a : _p.Attributes) out += "\t"_s + a.Name + " = _input."_s + a.Name + ";\n"_s;
				} else {
					if (_p.FragOutputs.size() > 1) {
						out += "PsOutput PSMain(PsInput _input)\n{\n"_s;
					} else {
						out += "float4 PSMain(PsInput _input) : SV_Target\n{\n"_s;
					}
					if (_usedFragCoord) out += "\tgl_FragCoord = _input._fragCoord;\n"_s;
					for (const VaryingDecl& v : _p.Varyings) out += "\t"_s + v.Name + " = _input."_s + v.Name + ";\n"_s;
				}
				out += body;
				out += ReturnEpilogue("\t"_s);
				out += "}\n"_s;
				return out;
			}

			String EmitHelper(const Function& fn)
			{
				_locals.clear();
				_arrayVars.clear();
				for (const Param& p : fn.Params) {
					_locals[p.Name] = p.Type;
					if (p.ArraySize > 0) _arrayVars.insert(p.Name);
				}
				String out;
				out += HlslType(fn.RetType) + " "_s + SanitizeIdent(fn.Name) + "("_s;
				for (std::size_t i = 0; i < fn.Params.size(); i++) {
					if (i != 0) out += ", "_s;
					const Param& pp = fn.Params[i];
					out += (pp.Qualifier.empty() ? String{} : pp.Qualifier + " "_s) + HlslType(pp.Type) + " "_s + SanitizeIdent(pp.Name) +
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
						_locals[s->DeclName] = s->DeclType;
						if (s->DeclArraySize > 0) _arrayVars.insert(s->DeclName);
						out += indent + (s->DeclConst ? String("const ") : String{}) + HlslType(s->DeclType) + " "_s + SanitizeIdent(s->DeclName) +
							(s->DeclArraySize > 0 ? String("["_s + Death::format("{}", s->DeclArraySize) + "]"_s) : String{});
						if (s->Init != nullptr) out += " = "_s + EmitExpr(s->Init.get(), 0);
						out += ";\n"_s;
						for (const std::pair<String, ExprPtr>& d : s->ExtraDecls) {
							_locals[d.first] = s->DeclType;
							out += indent + (s->DeclConst ? String("const ") : String{}) + HlslType(s->DeclType) + " "_s + SanitizeIdent(d.first);
							if (d.second != nullptr) out += " = "_s + EmitExpr(d.second.get(), 0);
							out += ";\n"_s;
						}
						break;
					case StmtKind::ExprStmt:
						out += indent + EmitExpr(s->E.get(), 0) + ";\n"_s;
						break;
					case StmtKind::Return:
						if (s->E == nullptr && _inEntryMain) {
							out += ReturnEpilogue(indent);
						} else {
							out += indent + "return"_s;
							if (s->E != nullptr) out += " "_s + EmitExpr(s->E.get(), 0);
							out += ";\n"_s;
						}
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
					_locals[s->DeclName] = s->DeclType;
					String r = HlslType(s->DeclType) + " "_s + SanitizeIdent(s->DeclName);
					if (s->Init != nullptr) r += " = "_s + EmitExpr(s->Init.get(), 0);
					for (const std::pair<String, ExprPtr>& d : s->ExtraDecls) {
						_locals[d.first] = s->DeclType;
						r += ", "_s + SanitizeIdent(d.first);
						if (d.second != nullptr) r += " = "_s + EmitExpr(d.second.get(), 0);
					}
					return r;
				}
				if (s->Kind == StmtKind::ExprStmt) return EmitExpr(s->E.get(), 0);
				return {};
			}

			// --- Type inference (drives the matrix-multiply rewrite) -------------------------------

			TyRef InferIdent(StringView name)
			{
				auto lit = _locals.find(String{name});
				if (lit != _locals.end()) return lit->second;
				auto uit = _uniformByName.find(String{name});
				if (uit != _uniformByName.end()) return uit->second;
				auto mit = _memberType.find(String{name});
				if (mit != _memberType.end()) return mit->second;
				auto vit = _varyingType.find(String{name});
				if (vit != _varyingType.end()) return vit->second;
				auto ait = _attributeType.find(String{name});
				if (ait != _attributeType.end()) return ait->second;
				auto git = _globalVarType.find(String{name});
				if (git != _globalVarType.end()) return git->second;
				if (name == "gl_Position") return { Ty::Vec4, {} };
				if (name == "gl_FragCoord") return { Ty::Vec4, {} };
				if (name == "gl_VertexID" || name == "gl_InstanceID") return { Ty::UInt, {} };
				if (!_vertexStage) {
					for (const Parser::FragOutputDecl& o : _p.FragOutputs) {
						if (name == o.Name) return o.Type;
					}
				}
				if (_samplerNames.find(String{name}) != _samplerNames.end()) return { Ty::Sampler2D, {} };
				return { Ty::Unknown, {} };
			}

			const Field* FindStructField(StringView structName, StringView field) const
			{
				auto it = _structByName.find(String{structName});
				if (it == _structByName.end()) return nullptr;
				for (const Field& f : it->second->Fields) if (f.Name == field) return &f;
				return nullptr;
			}

			bool IsBlockInstanceMember(const Expr* e, StringView& outMember) const
			{
				if (e->Kind != ExprKind::Member || e->A == nullptr || e->A->Kind != ExprKind::Ident) return false;
				if (_blockInstances.find(e->A->Text) == _blockInstances.end()) return false;
				if (_blockMemberNames.find(e->Text) == _blockMemberNames.end()) return false;
				outMember = e->Text;
				return true;
			}

			TyRef SwizzleType(Ty baseT, StringView field)
			{
				std::int32_t n = static_cast<std::int32_t>(field.size());
				return { MakeVec(BaseScalar(baseT), n), {} };
			}

			TyRef InferTy(const Expr* e)
			{
				if (e == nullptr) return {};
				switch (e->Kind) {
					case ExprKind::IntLit: return { Ty::Int, {} };
					case ExprKind::UIntLit: return { Ty::UInt, {} };
					case ExprKind::FloatLit: return { Ty::Float, {} };
					case ExprKind::BoolLit: return { Ty::Bool, {} };
					case ExprKind::Ident: return InferIdent(e->Text);
					case ExprKind::Member: {
						StringView member;
						if (IsBlockInstanceMember(e, member)) {
							auto it = _memberType.find(String{member});
							return (it != _memberType.end() ? it->second : TyRef{});
						}
						TyRef bt = InferTy(e->A.get());
						if (bt.T == Ty::Struct) {
							const Field* f = FindStructField(bt.S, e->Text);
							return (f != nullptr ? f->Type : TyRef{});
						}
						return SwizzleType(bt.T, e->Text);
					}
					case ExprKind::Index: {
						// Indexing a local/param array yields its element type (stored in _locals for arrays)
						if (e->A != nullptr && e->A->Kind == ExprKind::Ident && _arrayVars.find(e->A->Text) != _arrayVars.end()) {
							auto it = _locals.find(e->A->Text);
							return (it != _locals.end() ? it->second : TyRef{});
						}
						TyRef bt = InferTy(e->A.get());
						if (IsMatrix(bt.T)) return { MatrixColumn(bt.T), {} };
						if (IsVector(bt.T)) return { BaseScalar(bt.T), {} };
						return bt;		// array element (struct) passthrough
					}
					case ExprKind::Call: {
						TyRef ct;
						Ty bt;
						if (TryBuiltinType(e->Text, bt)) return { bt, {} };
						if (_structByName.find(e->Text) != _structByName.end()) return { Ty::Struct, e->Text };
						if (e->Text == "texture" || e->Text == "textureLod" || e->Text == "texelFetch") return { Ty::Vec4, {} };
						if (e->Text == "dot" || e->Text == "length" || e->Text == "distance" || e->Text == "determinant") return { Ty::Float, {} };
						if (e->Text == "cross") return { Ty::Vec3, {} };
						{
							auto it = _funcRet.find(e->Text);
							if (it != _funcRet.end()) return it->second;
						}
						// Most element-wise built-ins return the type of their first argument
						if (!e->Args.empty()) return InferTy(e->Args[0].get());
						return { Ty::Float, {} };
					}
					case ExprKind::Unary:
						if (e->Text == "!") return { Ty::Bool, {} };
						return InferTy(e->A.get());
					case ExprKind::Binary: {
						if (IsComparisonOrLogical(e->Text)) return { Ty::Bool, {} };
						TyRef a = InferTy(e->A.get());
						TyRef b = InferTy(e->B.get());
						if (e->Text == "*" && NeedsMul(a.T, b.T)) return MulResult(a, b);
						return Wider(a, b);
					}
					case ExprKind::Assign: return InferTy(e->A.get());
					case ExprKind::Conditional: return Wider(InferTy(e->B.get()), InferTy(e->C.get()));
				}
				return {};
			}

			static bool NeedsMul(Ty a, Ty b)
			{
				if (IsMatrix(a) && (IsMatrix(b) || IsVector(b))) return true;
				if (IsVector(a) && IsMatrix(b)) return true;
				return false;
			}
			static TyRef MulResult(const TyRef& a, const TyRef& b)
			{
				if (IsMatrix(a.T) && IsMatrix(b.T)) return a;
				if (IsMatrix(a.T) && IsVector(b.T)) return b;
				if (IsVector(a.T) && IsMatrix(b.T)) return a;
				return a;
			}
			static TyRef Wider(const TyRef& a, const TyRef& b)
			{
				if (a.T == Ty::Unknown) return b;
				if (b.T == Ty::Unknown) return a;
				if (IsMatrix(a.T)) return a;
				if (IsMatrix(b.T)) return b;
				return (Comps(a.T) >= Comps(b.T) ? a : b);
			}

			// --- Expression emission ---------------------------------------------------------------

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

			static String TranslateSwizzle(StringView field)
			{
				// Map every component to the xyzw set (HLSL has no stpq; rgba maps 1:1 to xyzw)
				String out;
				for (char c : field) {
					switch (c) {
						case 'x': case 'r': case 's': out += "x"_s; break;
						case 'y': case 'g': case 't': out += "y"_s; break;
						case 'z': case 'b': case 'p': out += "z"_s; break;
						case 'w': case 'a': case 'q': out += "w"_s; break;
						default: out += String{&c, 1}; break;
					}
				}
				return out;
			}

			bool IsSwizzle(StringView field) const
			{
				for (char c : field) {
					if (!"xyzwrgbastpq"_s.contains(c)) return false;
				}
				return (field.size() >= 1 && field.size() <= 4);
			}

			String EmitMember(const Expr* e)
			{
				StringView member;
				if (IsBlockInstanceMember(e, member)) {
					return String{member};		// "block.instances" -> "instances" (cbuffer member is global)
				}
				String base = EmitExpr(e->A.get(), 100);
				TyRef bt = InferTy(e->A.get());
				if (bt.T == Ty::Struct) {
					return base + "."_s + e->Text;
				}
				if (IsSwizzle(e->Text)) {
					return base + "."_s + TranslateSwizzle(e->Text);
				}
				// Unknown base: fall back to a raw swizzle (best effort)
				return base + "."_s + TranslateSwizzle(e->Text);
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
				if (TryBuiltinType(name, ct)) {
					// A single-argument vector/matrix constructor is a GLSL splat/convert: emit an HLSL cast so
					// "vec4(0.5)" (splat) and "vec3(someVec4)" (truncate) both compile.
					if ((IsVector(ct) || IsMatrix(ct)) && e->Args.size() == 1) {
						return "(("_s + HlslType({ ct, {} }) + ")"_s + EmitExpr(e->Args[0].get(), 90) + ")"_s;
					}
					return HlslType({ ct, {} }) + "("_s + EmitArgs(e) + ")"_s;
				}
				if (_structByName.find(String{name}) != _structByName.end()) {
					return String{name} + "("_s + EmitArgs(e) + ")"_s;
				}

				if (name == "texture" || name == "textureLod") {
					if (e->Args.size() < 2) { Fail("texture() needs at least (sampler, uv)"_s); return {}; }
					const Expr* sampler = e->Args[0].get();
					if (sampler->Kind != ExprKind::Ident || _samplerNames.find(sampler->Text) == _samplerNames.end()) {
						Fail("first argument to texture() must be a sampler uniform"_s);
						return {};
					}
					String method = (name == "textureLod" ? "SampleLevel"_s : "Sample"_s);
					String args = EmitExpr(e->Args[1].get(), 0);
					for (std::size_t i = 2; i < e->Args.size(); i++) args += ", "_s + EmitExpr(e->Args[i].get(), 0);
					return sampler->Text + "."_s + method + "("_s + sampler->Text + "_sampler, "_s + args + ")"_s;
				}

				// Remapped built-ins (name differs from GLSL)
				if (name == "mix") return "lerp("_s + EmitArgs(e) + ")"_s;
				if (name == "fract") return "frac("_s + EmitArgs(e) + ")"_s;
				if (name == "dFdx") return "ddx("_s + EmitArgs(e) + ")"_s;
				if (name == "dFdy") return "ddy("_s + EmitArgs(e) + ")"_s;
				if (name == "inversesqrt") return "rsqrt("_s + EmitArgs(e) + ")"_s;
				if (name == "atan" && e->Args.size() == 2) return "atan2("_s + EmitArgs(e) + ")"_s;
				if (name == "atan") return "atan("_s + EmitArgs(e) + ")"_s;
				if (name == "mod" && e->Args.size() == 2) {
					// GLSL mod(a,b) = a - b*floor(a/b) (HLSL fmod truncates toward zero for negatives)
					String a = EmitExpr(e->Args[0].get(), 0);
					String b = EmitExpr(e->Args[1].get(), 0);
					return "("_s + a + " - ("_s + b + ") * floor(("_s + a + ") / ("_s + b + ")))"_s;
				}
				// GLSL component-wise relational built-ins -> HLSL operators (each yields a bool vector)
				if (e->Args.size() == 2) {
					const char* relOp = nullptr;
					if (name == "lessThan") relOp = "<";
					else if (name == "lessThanEqual") relOp = "<=";
					else if (name == "greaterThan") relOp = ">";
					else if (name == "greaterThanEqual") relOp = ">=";
					else if (name == "equal") relOp = "==";
					else if (name == "notEqual") relOp = "!=";
					if (relOp != nullptr) {
						return "("_s + EmitExpr(e->Args[0].get(), 0) + " "_s + relOp + " "_s + EmitExpr(e->Args[1].get(), 0) + ")"_s;
					}
				}
				if (name == "not" && e->Args.size() == 1) return "(!"_s + EmitExpr(e->Args[0].get(), 90) + ")"_s;

				if (IsPassthroughBuiltin(name)) return String{name} + "("_s + EmitArgs(e) + ")"_s;

				auto it = _funcRet.find(String{name});
				if (it != _funcRet.end()) return SanitizeIdent(name) + "("_s + EmitArgs(e) + ")"_s;

				Fail("unknown function '"_s + name + "'"_s);
				return {};
			}

			String EmitCore(const Expr* e)
			{
				switch (e->Kind) {
					case ExprKind::IntLit: return e->Text;
					case ExprKind::UIntLit: return e->Text;
					case ExprKind::FloatLit: return e->Text;
					case ExprKind::BoolLit: return e->Text;
					case ExprKind::Ident: return EmitIdent(e->Text);
					case ExprKind::Member: return EmitMember(e);
					case ExprKind::Index:
						return EmitExpr(e->A.get(), 100) + "["_s + EmitExpr(e->B.get(), 0) + "]"_s;
					case ExprKind::Call: return EmitCall(e);
					case ExprKind::Unary: {
						String inner = EmitExpr(e->A.get(), 90);
						if (e->Postfix) return inner + e->Text;
						if (e->Text == "-" && !inner.empty() && inner[0] == '-') return "- "_s + inner;
						return e->Text + inner;
					}
					case ExprKind::Binary: {
						if (e->Text == "*") {
							TyRef a = InferTy(e->A.get());
							TyRef b = InferTy(e->B.get());
							if (NeedsMul(a.T, b.T)) {
								return "mul("_s + EmitExpr(e->A.get(), 0) + ", "_s + EmitExpr(e->B.get(), 0) + ")"_s;
							}
						}
						std::int32_t p = BinPrec(e->Text);
						String op = (e->Text == "^^" ? String{"!="_s} : e->Text);
						return EmitExpr(e->A.get(), p) + " "_s + op + " "_s + EmitExpr(e->B.get(), p + 1);
					}
					case ExprKind::Assign:
						return EmitExpr(e->A.get(), 1) + " "_s + e->Text + " "_s + EmitExpr(e->B.get(), 1);
					case ExprKind::Conditional:
						return EmitExpr(e->A.get(), 3) + " ? "_s + EmitExpr(e->B.get(), 3) + " : "_s + EmitExpr(e->C.get(), 2);
				}
				return {};
			}

			String EmitIdent(StringView name)
			{
				if (name == "gl_VertexID") { _usedVertexID = true; return "gl_VertexID"_s; }
				if (name == "gl_InstanceID") { _usedInstanceID = true; return "gl_InstanceID"_s; }
				if (name == "gl_FragCoord") { _usedFragCoord = true; return "gl_FragCoord"_s; }
				// Locals/params may carry reserved names (e.g. "frac", "point"); rename them consistently.
				if (_locals.find(String{name}) != _locals.end()) return SanitizeIdent(name);
				return String{name};
			}

			bool _inEntryMain = false;
		};

		// Splits, strips comments from, preprocesses and tokenizes a GLSL stage source
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

	bool HlslEmitter::Transform(StringView modernSource, bool vertexStage, const StageReflection& reflection,
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
			diag.Message = "HLSL emit: "_s + parser.Reason();
			diag.Line = 1;
			return false;
		}
		Emitter emitter(parser, vertexStage, reflection);
		String code = emitter.Emit();
		if (!emitter.Ok()) {
			diag.Message = "HLSL emit: "_s + emitter.Reason();
			diag.Line = 1;
			return false;
		}
		out = std::move(code);
		return true;
	}
}
