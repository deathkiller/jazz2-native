#include "ConsoleFixedFunction.h"
#include "GlslAst.h"
#include "ConstFold.h"

#include <cstdlib>
#include <map>
#include <memory>
#include <utility>

#include <Base/Format.h>
#include <Containers/SmallVector.h>
#include <Containers/StringConcatenable.h>

using namespace Death::Containers::Literals;

namespace ShaderCompiler
{
	namespace
	{
		// --- Type system of the fixed_function subset -----------------------------------------------
		// Deliberately smaller than the software transpiler's: the block vocabulary only ever needs
		// float scalars/vectors, int loop counters, bool conditions and the opaque pass descriptor.

		enum class Ty
		{
			Float, Int, Bool,
			Vec2, Vec3, Vec4,
			Pass		// nCine::RHI::FixedFunctionPass — only declarable and submittable, never a value
		};

		bool IsVec(Ty t) { return (t == Ty::Vec2 || t == Ty::Vec3 || t == Ty::Vec4); }
		bool IsNumericScalar(Ty t) { return (t == Ty::Float || t == Ty::Int); }

		std::int32_t Comps(Ty t)
		{
			switch (t) {
				case Ty::Vec2: return 2;
				case Ty::Vec3: return 3;
				case Ty::Vec4: return 4;
				default: return 1;
			}
		}

		Ty VecOf(std::int32_t n)
		{
			return (n == 2 ? Ty::Vec2 : n == 3 ? Ty::Vec3 : Ty::Vec4);
		}

		// Human-readable type name for diagnostics (matches the GLSL spelling)
		StringView TyName(Ty t)
		{
			switch (t) {
				case Ty::Float: return "float"_s;
				case Ty::Int: return "int"_s;
				case Ty::Bool: return "bool"_s;
				case Ty::Vec2: return "vec2"_s;
				case Ty::Vec3: return "vec3"_s;
				case Ty::Vec4: return "vec4"_s;
				case Ty::Pass: return "pass"_s;
			}
			return "?"_s;
		}

		// Recognizes a declarable local type keyword of the subset ("pass" is handled separately)
		bool TryLocalType(StringView k, Ty& out)
		{
			if (k == "float") { out = Ty::Float; return true; }
			if (k == "int") { out = Ty::Int; return true; }
			if (k == "bool") { out = Ty::Bool; return true; }
			if (k == "vec2") { out = Ty::Vec2; return true; }
			if (k == "vec3") { out = Ty::Vec3; return true; }
			if (k == "vec4") { out = Ty::Vec4; return true; }
			return false;
		}

		// GLSL assignment conversion: identical types, plus the implicit int -> float widening
		bool CanAssign(Ty dst, Ty src)
		{
			if (dst == src) return true;
			return (dst == Ty::Float && src == Ty::Int);
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

		// The pass-field enum vocabularies, mapped to their FixedFunctionPass spellings
		const char* BlendValueName(StringView v)
		{
			if (v == "MATERIAL") return "Material";
			if (v == "ADD") return "Additive";
			if (v == "OPAQUE") return "Opaque";
			if (v == "ALPHA") return "Alpha";
			return nullptr;
		}
		const char* TevValueName(StringView v)
		{
			if (v == "MODULATE") return "Modulate";
			if (v == "SILHOUETTE") return "Silhouette";
			if (v == "MODULATE_X2") return "ModulateX2";
			if (v == "MODULATE_X4") return "ModulateX4";
			if (v == "TINT_MIX") return "TintMix";
			if (v == "LUMA_RAMP") return "LumaRamp";
			return nullptr;
		}
		// The preset only the GX's programmable combiner can express: LUMA_RAMP needs per-texel
		// luminance (channel swizzles through the TEV swap tables plus a KONST-weighted dot product)
		// feeding a two-endpoint ramp. The CLX2 modulates a texel by the vertex colour and adds an
		// offset colour, and that is the whole vocabulary; the PSP's GE cannot either (its five texture
		// functions are modulate/decal/blend/replace/add over ONE texel and the fragment colour); the
		// GS's four texture functions are just as fixed; and the N64's RDP colour combiner computes
		// (A - B) * C + D per cycle over registers and the texel - it has no dot product, so it cannot
		// derive a luminance to pick the ramp tone with. Any block other than one targeting the GX
		// ALONE is a hard error rather than a silently wrong console frame (design doc section 5, the
		// `tev[]`/`swap` rows of the GX table).
		bool IsGxOnlyTevValueName(StringView v)
		{
			return (v == "LUMA_RAMP");
		}
		// The preset a lerping combiner can express: TINT_MIX is mix(texel, colour, alpha) with an
		// opaque result. The GX does it in one TEV stage (d + mix(a, b, c)); the N64's RDP colour
		// combiner IS that lerp - one cycle of (PRIM - TEX) * PRIM_ALPHA + TEX with the pass colour in
		// the PRIM register; and OpenGL 1.3's GL_INTERPOLATE is the same operation again. Nothing else
		// on this tier can: the CLX2 only modulates and adds an offset colour, the GE's five texture
		// functions have no lerp of a texel toward a constant weighted by an interpolated alpha
		// (GU_TFX_BLEND weighs by the TEXEL), and the GS's four texture functions are just as fixed.
		// Checked per block target through HasTintMixCombiner below.
		bool IsTintMixTevValueName(StringView v)
		{
			return (v == "TINT_MIX");
		}
		// Presets the GE has no form of at all: its texture environment applies no output scale to the
		// combined colour, so a x2/x4 modulate cannot be expressed by any single GE draw. A legacy GL
		// has both (GL_RGB_SCALE takes 1, 2 or 4), like the GX. The PVR
		// silently IGNORES them (it always modulates), which is why they are not "gx-only" - but that
		// also means a block shared with the gu target would be honoured by only some of the backends
		// it serves, so any block that reaches the GU (a gu block, a target list naming gu, or a
		// generic block) rejects them. Boosts belong in passes on this tier (the additive split
		// Colorized uses on the PVR). The RDP splits the pair: MODULATE_X2 it can express (see
		// HasCombinerOutputScale below), MODULATE_X4 it cannot.
		bool IsScaledModulateTevValueName(StringView v)
		{
			return (v == "MODULATE_X2" || v == "MODULATE_X4");
		}
		bool IsEnumValueName(StringView v)
		{
			return (BlendValueName(v) != nullptr || TevValueName(v) != nullptr);
		}

		// The two enums name the same five consoles - the parser's one describes what a block claims,
		// the emitter's which aggregate header is being written - so capability checks over a block's
		// target list translate its entries into backends through this
		FixedFunctionBackend BackendOfTarget(FixedFunctionTarget target)
		{
			switch (target) {
				case FixedFunctionTarget::Gx: return FixedFunctionBackend::Gx;
				case FixedFunctionTarget::Gu: return FixedFunctionBackend::Gu;
				case FixedFunctionTarget::Gs: return FixedFunctionBackend::Gs;
				case FixedFunctionTarget::Rdp: return FixedFunctionBackend::Rdp;
				case FixedFunctionTarget::LegacyGl: return FixedFunctionBackend::LegacyGl;
				default: return FixedFunctionBackend::Pvr;
			}
		}

		// How many vertices the backend's strip-builder scratch holds (EffectContext::MaxStripVertices).
		// The contract's floor is 8; the GX raises it to 16 so a radially subdivided iris wedge is one
		// strip instead of three, and every other backend matches it (the GE takes a strip of any
		// length in one draw call, one GIF packet carries a triangle strip of any length, the RSP's
		// vertex cache loads well over 16 vertices in one command, and a GL draws an array of any
		// length, so there is nothing to gain from splitting the geometry into small pieces). Literal indices and counts are checked
		// against it at generation time, because at runtime an out-of-range index is dropped and an
		// oversized count clamped - which would silently draw the wrong geometry. A block serving several
		// backends may only rely on the SMALLEST of their capacities, so a "pvr, gx" block is held to the
		// PVR's 8 vertices.
		std::int32_t StripCapacity(FixedFunctionBackend backend)
		{
			return (backend == FixedFunctionBackend::Pvr ? 8 : 16);
		}
		const char* BackendName(FixedFunctionBackend backend)
		{
			switch (backend) {
				case FixedFunctionBackend::Gx: return "gx";
				case FixedFunctionBackend::Gu: return "gu";
				case FixedFunctionBackend::Gs: return "gs";
				case FixedFunctionBackend::Rdp: return "rdp";
				case FixedFunctionBackend::LegacyGl: return "legacygl";
				default: return "pvr";
			}
		}

		// Whether @p backend's texture environment has the combiner output scale the preset needs
		// (@p x4 distinguishes MODULATE_X4 from MODULATE_X2). The GX has both scales natively. The
		// N64's RDP has no scale stage, but its colour combiner runs up to TWO cycles of
		// (A - B) * C + D, and the second cycle can double the first one's output -
		// (1 - 0) * COMBINED + COMBINED - so a x2 modulate is one two-cycle setting there, while a x4
		// would need a third cycle the hardware does not have. Nothing else on this tier scales at
		// all: the PVR always modulates, the GE's five texture functions combine one texel with the
		// fragment colour and nothing else, and the GS's four (MODULATE/DECAL/HIGHLIGHT/HIGHLIGHT2)
		// have no scale stage either.
		bool HasCombinerOutputScale(FixedFunctionBackend backend, bool x4)
		{
			// OpenGL 1.3's GL_COMBINE has GL_RGB_SCALE, which takes 1, 2 or 4 - both scales, exactly
			if (backend == FixedFunctionBackend::Gx || backend == FixedFunctionBackend::LegacyGl) return true;
			if (backend == FixedFunctionBackend::Rdp) return !x4;
			return false;
		}

		// Whether @p backend's texture combiner can lerp a texel toward a constant colour by an
		// interpolated alpha, which is what TINT_MIX (mix(texel, colour, alpha), opaque result) needs.
		// The GX does it in one TEV stage (d + mix(a, b, c)); the RDP's one-cycle
		// (PRIM - TEX) * PRIM_ALPHA + TEX IS that lerp, with the pass colour in the PRIM register. The
		// CLX2, the GE and the GS cannot (see IsTintMixTevValueName above).
		bool HasTintMixCombiner(FixedFunctionBackend backend)
		{
			// GL_INTERPOLATE is that lerp: Arg0 * Arg2 + Arg1 * (1 - Arg2), with the texel in Arg0, the
			// pass colour in the texture environment colour and the interpolated alpha in Arg2
			return (backend == FixedFunctionBackend::Gx || backend == FixedFunctionBackend::Rdp ||
				backend == FixedFunctionBackend::LegacyGl);
		}

		// --- Statement AST ---------------------------------------------------------------------------
		// The expression AST (Expr / ExprKind / MakeExpr) is shared and lives in GlslAst.h; the block's
		// statement model is local. Every statement carries the 1-based input line it started on, so
		// semantic errors (which surface during emission) can point at real source lines.

		enum class StmtKind { Block, VarDecl, ExprStmt, If, For };

		struct Stmt
		{
			StmtKind Kind;
			std::int32_t Line = 0;
			SmallVector<std::unique_ptr<Stmt>, 0> Body;		// Block
			Ty DeclType = Ty::Float;						// VarDecl
			String DeclName;
			ExprPtr Init;
			SmallVector<std::pair<String, ExprPtr>, 0> ExtraDecls;
			ExprPtr E;										// ExprStmt
			ExprPtr Cond;									// If
			std::unique_ptr<Stmt> Then, Else;
			std::unique_ptr<Stmt> ForInit; ExprPtr ForCond; ExprPtr ForUpdate; std::unique_ptr<Stmt> ForBody;
		};
		using StmtPtr = std::unique_ptr<Stmt>;

		StmtPtr MakeStmt(StmtKind kind, std::int32_t line)
		{
			auto s = std::make_unique<Stmt>();
			s->Kind = kind;
			s->Line = line;
			return s;
		}

		// --- Parser ----------------------------------------------------------------------------------
		// The block body is a plain statement list (no declarations/functions around it), so the parser
		// is the statement/expression half of the software transpiler's grammar plus "pass" declarations.

		class Parser
		{
		public:
			explicit Parser(const SmallVectorImpl<GlslToken>& tokens) : _toks(tokens) {}

			StmtPtr Run()
			{
				StmtPtr block = MakeStmt(StmtKind::Block, CurLine());
				while (!AtEnd() && _ok) {
					if (IsOp(";")) { Next(); continue; }
					StmtPtr s = ParseStatement();
					if (s != nullptr) block->Body.push_back(std::move(s));
				}
				return block;
			}

			bool Ok() const { return _ok; }
			const String& Reason() const { return _reason; }
			std::int32_t ErrorLine() const { return _errLine; }

		private:
			const SmallVectorImpl<GlslToken>& _toks;
			std::size_t _pos = 0;
			bool _ok = true;
			String _reason;
			std::int32_t _errLine = 0;

			const GlslToken& Cur() const { return _toks[_pos]; }
			const GlslToken& At(std::size_t p) const { return _toks[p < _toks.size() ? p : _toks.size() - 1]; }
			bool AtEnd() const { return Cur().Type == GlslTokenType::End; }
			bool Is(GlslTokenType t) const { return Cur().Type == t; }
			bool IsOp(const char* s) const { return Cur().Type == GlslTokenType::Operator && Cur().Text == s; }
			bool IsKw(const char* s) const { return Cur().Type == GlslTokenType::Identifier && Cur().Text == s; }
			void Next() { if (Cur().Type != GlslTokenType::End) _pos++; }

			// Tokens carry the ORIGINAL input line in their Index (the tokenizer is fed per-line); the
			// End token carries the last line, so errors at end-of-block still point somewhere sensible
			std::int32_t CurLine() const
			{
				if (!AtEnd()) return static_cast<std::int32_t>(Cur().Index);
				return (_pos > 0 ? static_cast<std::int32_t>(At(_pos - 1).Index) : 0);
			}

			void Fail(String why) { if (_ok) { _ok = false; _reason = std::move(why); _errLine = CurLine(); } }

			bool Expect(const char* op)
			{
				if (IsOp(op)) { Next(); return true; }
				String context = (AtEnd() ? String{} : String(" before '"_s + Cur().Text + "'"_s));
				Fail("expected '"_s + op + "'"_s + context);
				return false;
			}

			bool LooksLikeDecl() const
			{
				if (At(_pos).Type != GlslTokenType::Identifier) return false;
				Ty t;
				return (TryLocalType(At(_pos).Text, t) || At(_pos).Text == "pass"_s);
			}

			StmtPtr ParseBlock()
			{
				std::int32_t line = CurLine();
				if (!Expect("{")) return nullptr;
				StmtPtr block = MakeStmt(StmtKind::Block, line);
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
				if (IsKw("while") || IsKw("do") || IsKw("switch") || IsKw("return") ||
					IsKw("discard") || IsKw("break") || IsKw("continue")) {
					Fail("'"_s + Cur().Text + "' is not part of the fixed_function grammar (statements: pass/local declarations, assignments, if/else, for, submit_quad)"_s);
					return nullptr;
				}
				if (LooksLikeDecl()) return ParseLocalDecl();

				std::int32_t line = CurLine();
				ExprPtr e = ParseExpression(0);
				if (!_ok) return nullptr;
				Expect(";");
				StmtPtr s = MakeStmt(StmtKind::ExprStmt, line);
				s->E = std::move(e);
				return s;
			}

			StmtPtr ParseLocalDecl()
			{
				std::int32_t line = CurLine();
				bool isPass = (Cur().Text == "pass"_s);
				Ty t = Ty::Pass;
				if (!isPass && !TryLocalType(Cur().Text, t)) {
					Fail("unknown type '"_s + Cur().Text + "'"_s);
					return nullptr;
				}
				Next();
				if (!Is(GlslTokenType::Identifier)) {
					Fail("expected a variable name"_s);
					return nullptr;
				}
				StmtPtr s = MakeStmt(StmtKind::VarDecl, line);
				s->DeclType = t;
				s->DeclName = Cur().Text;
				Next();
				if (IsOp("[")) { Fail("array declarations are not part of the fixed_function grammar"_s); return nullptr; }
				if (isPass) {
					// A pass always starts from the engine defaults (a plain modulated material pass);
					// an initializer would suggest copyable pass values, which the vocabulary avoids
					if (IsOp("=")) { Fail("a 'pass' declaration takes no initializer (fields are assigned one by one)"_s); return nullptr; }
					if (IsOp(",")) { Fail("declare each pass in its own statement"_s); return nullptr; }
					Expect(";");
					return s;
				}
				if (IsOp("=")) { Next(); s->Init = ParseExpression(1); if (!_ok) return nullptr; }
				while (IsOp(",")) {
					Next();
					if (!Is(GlslTokenType::Identifier)) { Fail("expected a variable name"_s); return nullptr; }
					String name = Cur().Text;
					Next();
					if (IsOp("[")) { Fail("array declarations are not part of the fixed_function grammar"_s); return nullptr; }
					ExprPtr init;
					if (IsOp("=")) { Next(); init = ParseExpression(1); if (!_ok) return nullptr; }
					s->ExtraDecls.emplace_back(std::move(name), std::move(init));
				}
				Expect(";");
				return s;
			}

			StmtPtr ParseIf()
			{
				std::int32_t line = CurLine();
				Next();
				Expect("(");
				ExprPtr cond = ParseExpression(0);
				Expect(")");
				StmtPtr th = ParseStatement();
				StmtPtr el;
				if (IsKw("else")) { Next(); el = ParseStatement(); }
				StmtPtr s = MakeStmt(StmtKind::If, line);
				s->Cond = std::move(cond);
				s->Then = std::move(th);
				s->Else = std::move(el);
				return s;
			}

			StmtPtr ParseFor()
			{
				std::int32_t line = CurLine();
				Next();
				Expect("(");
				StmtPtr init;
				if (IsOp(";")) { Next(); }
				else if (LooksLikeDecl()) { init = ParseLocalDecl(); }
				else {
					std::int32_t initLine = CurLine();
					ExprPtr e = ParseExpression(0);
					Expect(";");
					init = MakeStmt(StmtKind::ExprStmt, initLine);
					init->E = std::move(e);
				}
				ExprPtr cond;
				if (!IsOp(";")) cond = ParseExpression(0);
				Expect(";");
				ExprPtr upd;
				if (!IsOp(")")) upd = ParseExpression(0);
				Expect(")");
				StmtPtr body = ParseStatement();
				StmtPtr s = MakeStmt(StmtKind::For, line);
				s->ForInit = std::move(init);
				s->ForCond = std::move(cond);
				s->ForUpdate = std::move(upd);
				s->ForBody = std::move(body);
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
						String o = Cur().Text;
						Next();
						ExprPtr v = ParseUnary();
						ExprPtr node = MakeExpr(ExprKind::Unary, std::move(o));
						node->A = std::move(v);
						return node;
					}
					if (op == "~") { Fail("unary '~' is not part of the fixed_function grammar"_s); return nullptr; }
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
						Fail("indexing/arrays are not part of the fixed_function grammar"_s);
						return e;
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
					case GlslTokenType::IntLiteral: {
						ExprPtr e = MakeExpr(ExprKind::IntLit, t.Text);
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
						Fail("unexpected end of the fixed_function block"_s);
						return nullptr;
				}
			}
		};

		// --- Emitter + type checker --------------------------------------------------------------------
		// Single pass: emission infers every expression's type on the way and rejects anything outside
		// the vocabulary, so invalid blocks fail HERE (on the dev machine, with a line number) and never
		// reach a console compiler as broken C++.

		class Emitter
		{
		public:
			Emitter(FixedFunctionBackend backend, const SmallVectorImpl<FixedFunctionTarget>& targets)
				: _backend(backend), _targets(InPlaceInit, targets.begin(), targets.end())
			{
				// The portable core emits identically for every backend; what varies is only which
				// capabilities are reachable (GX-only TEV presets, the combiner output scales, the
				// strip-builder capacity), and that is decided by the block's own target list rather
				// than by the header being written - see the gates below
			}

			bool Ok() const { return _ok; }
			const String& Reason() const { return _reason; }
			std::int32_t ErrorLine() const { return _errLine; }
			bool UsesColor() const { return _usesColor; }
			bool UsesOffsetColor() const { return _usesOffsetColor; }
			FixedFunctionRequirements Requirements() const { return FixedFunctionRequirements(_requirements); }

			String EmitBody(const Stmt* topBlock, const String& indent)
			{
				_scopes.emplace_back();
				String out;
				for (const StmtPtr& s : topBlock->Body) {
					EmitStmt(s.get(), indent, out);
				}
				_scopes.pop_back();
				return out;
			}

		private:
			FixedFunctionBackend _backend;
			/**
				The targets the block names, in declaration order - EMPTY for the generic block.

				Every capability check below validates against the INTERSECTION of these, not against
				the backend whose header is currently being written: a block may only use what ALL the
				backends it serves can do, otherwise a shared description would be honoured by some of
				them and silently ignored by the rest. The generic block, which serves every backend, is
				the empty-list case and stays in the portable quad-only core.
			*/
			SmallVector<FixedFunctionTarget, 0> _targets;
			SmallVector<std::map<String, Ty>, 0> _scopes;
			bool _ok = true;
			String _reason;
			std::int32_t _errLine = 0;
			std::int32_t _stmtLine = 0;		// line of the statement being emitted (expression errors point here)
			bool _usesColor = false;
			bool _usesOffsetColor = false;
			std::uint8_t _requirements = 0;	// FixedFunctionRequirements bits, collected during emission

			void Fail(String why)
			{
				if (_ok) { _ok = false; _reason = std::move(why); _errLine = _stmtLine; }
			}

			// Records that the emitted code calls one of the optional context facilities; the bits
			// end up in the generated table so the backends can skip the setup for the unused ones
			void Require(FixedFunctionRequirements bit)
			{
				_requirements |= std::uint8_t(bit);
			}

			// Whether the block's target list names @p target
			bool NamesTarget(FixedFunctionTarget target) const
			{
				for (FixedFunctionTarget listed : _targets) {
					if (listed == target) {
						return true;
					}
				}
				return false;
			}

			// The listed targets other than @p target, spelled for a diagnostic - what a capability
			// message needs to name when the block would have been valid on its own but its list drags
			// in a backend that cannot express the feature
			String OtherTargets(FixedFunctionTarget target) const
			{
				String result;
				for (FixedFunctionTarget listed : _targets) {
					if (listed == target) {
						continue;
					}
					if (!result.empty()) {
						result += ", ";
					}
					result += FixedFunctionTargetName(listed);
				}
				return result;
			}

			// Gates the extended vocabulary (strip builder, quad geometry, resolved uniforms) to
			// blocks that name their backends: a generic fixed_function block must stay in the portable
			// quad-only core (design doc section 5), so a shared description can never silently depend
			// on geometry synthesis one backend implements differently from another. A target list is
			// backend-specific enough - every backend it serves is named in it.
			bool RequireExtended(StringView what)
			{
				if (_targets.empty()) {
					Fail(what + " is only available in a backend-specific fixed_function block that names its targets - fixed_function(pvr), (gx), (gu), (gs), (rdp) or a list of them (generic blocks keep the portable quad-only core)"_s);
					return false;
				}
				return true;
			}

			// Gates a capability only one backend's hardware has to a block targeting that backend and
			// NOTHING else: the intersection of a list that also names another console cannot include
			// it, and a generic block is rejected as well (it is transpiled for every backend).
			bool RequireGxOnly(StringView what)
			{
				if (NamesTarget(FixedFunctionTarget::Gx) && _targets.size() == 1) {
					return true;
				}
				String why = what + " is a GX-only capability - it needs the programmable TEV combiner, so it is only available in a fixed_function(gx) block"_s;
				if (NamesTarget(FixedFunctionTarget::Gx)) {
					// The list form: say which of the block's own targets is the one that cannot have it
					why += ", not in one that also targets "_s + OtherTargets(FixedFunctionTarget::Gx);
				}
				Fail(std::move(why));
				return false;
			}

			// Rejects the combiner output scales for every block that reaches a backend without one: the
			// GE's texture environment and the GS's texture function both lack a scale stage, so nothing
			// either can be programmed to do reproduces them, and the RDP's two combiner cycles reach a
			// x2 but not a x4. Unlike the GX-only presets this is not a per-block capability - a pvr
			// block may keep using them, since the PVR ignores tev entirely - but a target list naming
			// gu or gs (or rdp for the x4), and a generic block (transpiled for every backend), would
			// otherwise be honoured by only some of the backends they serve, which is exactly what
			// these checks exist to prevent.
			bool RequireCombinerOutputScale(StringView what)
			{
				const bool x4 = (what == "MODULATE_X4"_s);
				// A block with a target list is held to the intersection of its own targets; a generic one
				// has no list, so the backend whose header is being emitted is what decides
				FixedFunctionBackend lacking = _backend;
				bool listed = false;
				if (!_targets.empty()) {
					for (FixedFunctionTarget target : _targets) {
						if (!HasCombinerOutputScale(BackendOfTarget(target), x4)) {
							lacking = BackendOfTarget(target);
							listed = true;
							break;
						}
					}
					if (!listed) {
						return true;
					}
				} else if (HasCombinerOutputScale(_backend, x4)) {
					return true;
				}

				const char* name = BackendName(lacking);
				String why = what + " cannot be expressed for the "_s + name + " target - "_s +
					(lacking == FixedFunctionBackend::Gs
						? "the Graphics Synthesizer's texture function has no combiner output scale"_s
						: (lacking == FixedFunctionBackend::Gu
							? "the Graphics Engine's texture environment has no combiner output scale"_s
							: (lacking == FixedFunctionBackend::Rdp
								? "the Reality Display Processor's second combiner cycle can double its output but not quadruple it"_s
								: "that backend has no combiner output scale"_s)));
				if (listed && _targets.size() > 1) {
					// The list form: the block would have been valid without that target in it
					why += ", which this block also names"_s;
				}
				why += " (write the boost as passes in a fixed_function("_s + name + ") block, e.g. an additive one)"_s;
				Fail(std::move(why));
				return false;
			}

			// Rejects TINT_MIX for every block that reaches a backend whose texture combiner cannot lerp
			// a texel toward a constant colour (see HasTintMixCombiner) - the GX, the RDP and a legacy GL
			// can, so a block targeting any of them (or several, from one body) may use it, and the same
			// block is a hard error as soon as its list drags in the CLX2, the GE or the GS, which would
			// silently draw something else. A generic block is checked against the backend whose header is being
			// emitted, so it fails while the first no-combiner console's aggregate is written.
			bool RequireTintMixCombiner(StringView what)
			{
				// A block with a target list is held to the intersection of its own targets; a generic one
				// has no list, so the backend whose header is being emitted is what decides
				FixedFunctionBackend lacking = _backend;
				bool listed = false;
				if (!_targets.empty()) {
					for (FixedFunctionTarget target : _targets) {
						if (!HasTintMixCombiner(BackendOfTarget(target))) {
							lacking = BackendOfTarget(target);
							listed = true;
							break;
						}
					}
					if (!listed) {
						return true;
					}
				} else if (HasTintMixCombiner(_backend)) {
					return true;
				}

				const char* name = BackendName(lacking);
				String why = what + " cannot be expressed for the "_s + name + " target - "_s +
					(lacking == FixedFunctionBackend::Gs
						? "the Graphics Synthesizer's texture functions cannot lerp a texel toward a constant colour"_s
						: (lacking == FixedFunctionBackend::Gu
							? "the Graphics Engine's texture functions cannot lerp a texel toward a constant colour"_s
							: "the CLX2's texture environment can only modulate a texel and add an offset colour, not lerp one toward a colour"_s));
				if (listed && _targets.size() > 1) {
					// The list form: the block would have been valid without that target in it
					why += ", which this block also names"_s;
				}
				why += " (keep TINT_MIX in a fixed_function block that only targets gx, rdp and/or legacygl, and give the other backends their own block)"_s;
				Fail(std::move(why));
				return false;
			}

			// The strip-builder capacity the block may rely on - the SMALLEST among the backends it
			// serves, since the geometry has to fit every one of them. @p limiting receives the target
			// that capacity belongs to (the first one listed with it, so a single-target block names
			// itself); a generic block cannot reach the strip builder at all, so it falls back to the
			// backend being emitted.
			std::int32_t StripCapacityLimit(const char*& limiting) const
			{
				if (_targets.empty()) {
					limiting = BackendName(_backend);
					return StripCapacity(_backend);
				}
				std::int32_t capacity = 0;
				for (FixedFunctionTarget listed : _targets) {
					const std::int32_t own = StripCapacity(BackendOfTarget(listed));
					if (capacity == 0 || own < capacity) {
						capacity = own;
						limiting = FixedFunctionTargetName(listed);
					}
				}
				return capacity;
			}

			// Constant folding has already run, so a strip index or vertex count that the author wrote
			// as a literal is still an IntLit node here; anything computed stays unchecked (the runtime
			// drops out-of-range indices and clamps oversized counts, as it always did)
			bool CheckStripLiteral(const Expr* e, StringView what, std::int32_t lo)
			{
				if (e == nullptr || e->Kind != ExprKind::IntLit) {
					return true;
				}
				const char* limiting = "";
				const std::int32_t capacity = StripCapacityLimit(limiting);
				const std::int32_t value = std::atoi(String{e->Text}.data());
				if (value < lo || value > capacity - (lo == 0 ? 1 : 0)) {
					String why = what + " "_s + e->Text + " is outside the "_s + limiting +
						" strip builder's capacity of "_s + Death::format("{}", capacity) + " vertices"_s;
					if (_targets.size() > 1) {
						// The list form: name why THAT target's capacity is the one that applies
						why += " (the smallest capacity among the block's targets)"_s;
					}
					Fail(std::move(why));
					return false;
				}
				return true;
			}

			bool Lookup(StringView name, Ty& out) const
			{
				for (std::size_t i = _scopes.size(); i > 0; i--) {
					auto it = _scopes[i - 1].find(String{name});
					if (it != _scopes[i - 1].end()) { out = it->second; return true; }
				}
				return false;
			}

			void Declare(const String& name, Ty t)
			{
				if (name == "COLOR"_s || name == "ctx"_s) {
					Fail("'"_s + name + "' cannot be redeclared inside a fixed_function block"_s);
					return;
				}
				if (_scopes.back().find(name) != _scopes.back().end()) {
					Fail("'"_s + name + "' is already declared in this scope"_s);
					return;
				}
				if (IsEnumValueName(name)) {
					Fail("'"_s + name + "' is a blend/tev preset name and cannot be used as a variable"_s);
					return;
				}
				_scopes.back()[name] = t;
			}

			// --- Statements ----------------------------------------------------------------------------

			void EmitStmt(const Stmt* s, const String& indent, String& out)
			{
				if (s == nullptr || !_ok) return;
				_stmtLine = s->Line;
				switch (s->Kind) {
					case StmtKind::Block: {
						_scopes.emplace_back();
						out += indent + "{\n"_s;
						for (const StmtPtr& c : s->Body) EmitStmt(c.get(), indent + "\t"_s, out);
						out += indent + "}\n"_s;
						_scopes.pop_back();
						break;
					}
					case StmtKind::VarDecl:
						EmitVarDecl(s, indent, out);
						break;
					case StmtKind::ExprStmt:
						EmitExprStatement(s->E.get(), indent, out);
						break;
					case StmtKind::If: {
						Ty condTy = Ty::Bool;
						String cond = EmitExpr(s->Cond.get(), 0, condTy);
						if (!_ok) return;
						if (condTy != Ty::Bool) { Fail("the if condition must be a bool expression"_s); return; }
						out += indent + "if ("_s + cond + ") "_s;
						EmitBranch(s->Then.get(), indent, out);
						if (s->Else != nullptr) {
							out += " else "_s;
							EmitBranch(s->Else.get(), indent, out);
						}
						out += "\n"_s;
						break;
					}
					case StmtKind::For:
						EmitFor(s, indent, out);
						break;
				}
			}

			// Emits the body of a control-flow branch as brace-wrapped statements
			void EmitBranch(const Stmt* s, const String& indent, String& out)
			{
				_scopes.emplace_back();
				String inner = indent + "\t"_s;
				out += "{\n"_s;
				if (s != nullptr && s->Kind == StmtKind::Block) {
					for (const StmtPtr& c : s->Body) EmitStmt(c.get(), inner, out);
				} else if (s != nullptr) {
					EmitStmt(s, inner, out);
				}
				out += indent + "}"_s;
				_scopes.pop_back();
			}

			void EmitVarDecl(const Stmt* s, const String& indent, String& out)
			{
				if (s->DeclType == Ty::Pass) {
					// Default-constructed = the engine defaults documented on FixedFunctionPass
					Declare(s->DeclName, Ty::Pass);
					if (!_ok) return;
					out += indent + "FixedFunctionPass "_s + s->DeclName + ";\n"_s;
					return;
				}
				EmitOneDecl(s->DeclType, s->DeclName, s->Init.get(), indent, out);
				for (const std::pair<String, ExprPtr>& d : s->ExtraDecls) {
					EmitOneDecl(s->DeclType, d.first, d.second.get(), indent, out);
				}
			}

			void EmitOneDecl(Ty t, const String& name, const Expr* init, const String& indent, String& out)
			{
				if (!_ok) return;
				String initCode;
				if (init != nullptr) {
					Ty initTy = t;
					initCode = EmitExpr(init, 1, initTy);
					if (!_ok) return;
					if (!CanAssign(t, initTy)) {
						Fail("cannot initialize "_s + TyName(t) + " '"_s + name + "' with a "_s + TyName(initTy) + " expression"_s);
						return;
					}
				}
				Declare(name, t);
				if (!_ok) return;
				out += indent + TyName(t) + " "_s + name;
				if (init != nullptr) out += " = "_s + initCode;
				out += ";\n"_s;
			}

			// A statement-level expression: (compound) assignment, ++/--, or a submission/builder call
			void EmitExprStatement(const Expr* e, const String& indent, String& out)
			{
				if (e == nullptr) { Fail("empty statement"_s); return; }
				if (e->Kind == ExprKind::Call && e->Text == "submit_quad"_s) {
					out += indent + EmitSubmitQuad(e) + ";\n"_s;
					return;
				}
				if (e->Kind == ExprKind::Call && (e->Text == "submit_strip"_s || e->Text == "submit_strip_shaded"_s)) {
					out += indent + EmitSubmitStrip(e) + ";\n"_s;
					return;
				}
				if (e->Kind == ExprKind::Call &&
					(e->Text == "strip_position"_s || e->Text == "strip_uv"_s || e->Text == "strip_color"_s)) {
					out += indent + EmitStripSetter(e) + ";\n"_s;
					return;
				}
				if (e->Kind == ExprKind::Assign) {
					EmitAssignStatement(e, indent, out);
					return;
				}
				if (e->Kind == ExprKind::Unary && (e->Text == "++"_s || e->Text == "--"_s)) {
					out += indent + EmitIncDec(e) + ";\n"_s;
					return;
				}
				Fail("only assignments, '++'/'--', submit_quad/submit_strip[_shaded](...) and strip_*(...) can stand alone as statements"_s);
			}

			String EmitSubmitQuad(const Expr* e)
			{
				if (e->Args.size() != 1 || e->Args[0]->Kind != ExprKind::Ident) {
					Fail("usage: submit_quad(<pass variable>);"_s);
					return {};
				}
				Ty t;
				const String& name = e->Args[0]->Text;
				if (!Lookup(name, t) || t != Ty::Pass) {
					Fail("submit_quad() takes a declared pass variable ('"_s + name + "' is not one)"_s);
					return {};
				}
				// The quad is always textured, so the dispatch must refuse to run this effect with
				// nothing bound to the program's sampler (see FixedFunctionRequirements::SamplesTexture)
				Require(FixedFunctionRequirements::SamplesTexture);
				return "ctx.SubmitQuad("_s + name + ")"_s;
			}

			// submit_strip(pass, count) / submit_strip_shaded(pass, count) - submits the first `count`
			// vertices of the context's strip builder (positions + UVs under the pass's flat colour, or
			// positions + per-vertex colours for the shaded, untextured form)
			String EmitSubmitStrip(const Expr* e)
			{
				if (!RequireExtended(e->Text)) return {};
				Require(FixedFunctionRequirements::NeedsStripBuilder);
				if (e->Args.size() != 2 || e->Args[0]->Kind != ExprKind::Ident) {
					Fail("usage: "_s + e->Text + "(<pass variable>, <vertex count>);"_s);
					return {};
				}
				Ty t;
				const String& name = e->Args[0]->Text;
				if (!Lookup(name, t) || t != Ty::Pass) {
					Fail(e->Text + "() takes a declared pass variable ('"_s + name + "' is not one)"_s);
					return {};
				}
				Ty countTy;
				String count = EmitExpr(e->Args[1].get(), 0, countTy);
				if (!_ok) return {};
				if (countTy != Ty::Int) {
					Fail(e->Text + "() takes an int vertex count (got "_s + TyName(countTy) + ")"_s);
					return {};
				}
				// A literal count above the scratch capacity would be clamped at runtime, drawing a
				// truncated strip; below 3 it would be dropped entirely
				if (!CheckStripLiteral(e->Args[1].get(), "vertex count"_s, 3)) return {};
				// The flat form is textured; the shaded one only samples when a pass sets TINT_MIX, which
				// is recorded where that assignment is emitted (see FixedFunctionRequirements::SamplesTexture)
				if (e->Text == "submit_strip"_s) {
					Require(FixedFunctionRequirements::SamplesTexture);
				}
				const char* method = (e->Text == "submit_strip"_s ? "SubmitStrip" : "SubmitStripShaded");
				return "ctx."_s + method + "("_s + name + ", "_s + count + ")"_s;
			}

			// strip_position(i, vec2) / strip_uv(i, vec2) / strip_color(i, vec4) - the strip-builder
			// vertex setters (UVs are given in the shader's texture space; the backend folds its
			// padded-store scale, exactly like the quad corner synthesis does)
			String EmitStripSetter(const Expr* e)
			{
				if (!RequireExtended(e->Text)) return {};
				Require(FixedFunctionRequirements::NeedsStripBuilder);
				const bool isColor = (e->Text == "strip_color"_s);
				const Ty valueTy = (isColor ? Ty::Vec4 : Ty::Vec2);
				if (e->Args.size() != 2) {
					Fail("usage: "_s + e->Text + "(<vertex index>, <"_s + TyName(valueTy) + ">);"_s);
					return {};
				}
				Ty indexTy;
				String index = EmitExpr(e->Args[0].get(), 0, indexTy);
				if (!_ok) return {};
				if (indexTy != Ty::Int) {
					Fail(e->Text + "() takes an int vertex index (got "_s + TyName(indexTy) + ")"_s);
					return {};
				}
				// A literal index past the scratch capacity would be dropped at runtime, leaving that
				// vertex at whatever the previous strip put there
				if (!CheckStripLiteral(e->Args[0].get(), "vertex index"_s, 0)) return {};
				Ty vTy;
				String value = EmitExpr(e->Args[1].get(), 0, vTy);
				if (!_ok) return {};
				if (vTy != valueTy) {
					Fail(e->Text + "() takes a "_s + TyName(valueTy) + " value (got "_s + TyName(vTy) + ")"_s);
					return {};
				}
				const char* helper = (e->Text == "strip_position"_s ? "StripPosition"
					: e->Text == "strip_uv"_s ? "StripUv" : "StripColor");
				return String{helper} + "(ctx, "_s + index + ", "_s + value + ")"_s;
			}

			String EmitIncDec(const Expr* e)
			{
				if (e->A == nullptr || e->A->Kind != ExprKind::Ident) {
					Fail("'++'/'--' can only be applied to an int variable"_s);
					return {};
				}
				Ty t;
				if (!Lookup(e->A->Text, t) || t != Ty::Int) {
					Fail("'++'/'--' can only be applied to an int variable ('"_s + e->A->Text + "' is not one)"_s);
					return {};
				}
				return (e->Postfix ? String(e->A->Text + e->Text) : String(e->Text + e->A->Text));
			}

			void EmitAssignStatement(const Expr* e, const String& indent, String& out)
			{
				const Expr* target = e->A.get();
				if (target == nullptr) { Fail("malformed assignment"_s); return; }

				// "p.<field> = ..." — the pass-descriptor vocabulary
				if (target->Kind == ExprKind::Member && target->A != nullptr && target->A->Kind == ExprKind::Ident) {
					Ty baseTy;
					if (Lookup(target->A->Text, baseTy) && baseTy == Ty::Pass) {
						EmitPassFieldAssign(target->A->Text, target->Text, e, indent, out);
						return;
					}
				}
				out += indent + EmitLocalAssign(e) + ";\n"_s;
			}

			void EmitPassFieldAssign(const String& passName, const String& field, const Expr* assign, const String& indent, String& out)
			{
				if (assign->Text != "="_s) {
					Fail("pass fields only support plain '=' assignment"_s);
					return;
				}
				const Expr* rhs = assign->B.get();
				if (field == "blend"_s || field == "tev"_s) {
					if (rhs == nullptr || rhs->Kind != ExprKind::Ident) {
						Fail((field == "blend"_s)
							? String{"p.blend takes one of: MATERIAL, ADD, OPAQUE, ALPHA"_s}
							: String{"p.tev takes one of: MODULATE, SILHOUETTE, MODULATE_X2, MODULATE_X4, TINT_MIX, LUMA_RAMP"_s});
						return;
					}
					if (field == "blend"_s) {
						const char* value = BlendValueName(rhs->Text);
						if (value == nullptr) {
							Fail("unknown blend mode '"_s + rhs->Text + "' (expected MATERIAL, ADD, OPAQUE or ALPHA)"_s);
							return;
						}
						out += indent + passName + ".Blend = FixedFunctionPass::BlendMode::"_s + value + ";\n"_s;
					} else {
						const char* value = TevValueName(rhs->Text);
						if (value == nullptr) {
							Fail("unknown tev preset '"_s + rhs->Text + "' (expected MODULATE, SILHOUETTE, MODULATE_X2, MODULATE_X4, TINT_MIX or LUMA_RAMP)"_s);
							return;
						}
						if (IsGxOnlyTevValueName(rhs->Text) && !RequireGxOnly(rhs->Text)) {
							return;
						}
						if (IsTintMixTevValueName(rhs->Text)) {
							// The one preset that makes a SHADED strip consume the texel as well, so an
							// effect that can set it depends on a bound texture like a quad does
							Require(FixedFunctionRequirements::SamplesTexture);
						}
						if (IsTintMixTevValueName(rhs->Text) && !RequireTintMixCombiner(rhs->Text)) {
							return;
						}
						if (IsScaledModulateTevValueName(rhs->Text) && !RequireCombinerOutputScale(rhs->Text)) {
							return;
						}
						out += indent + passName + ".Tev = FixedFunctionPass::TevPreset::"_s + value + ";\n"_s;
					}
					return;
				}

				// luma_gain is the only scalar pass field; it parameterizes the LUMA_RAMP preset above
				if (field == "luma_gain"_s) {
					Ty gainTy;
					String gain = EmitExpr(rhs, 0, gainTy);
					if (!_ok) return;
					if (!CanAssign(Ty::Float, gainTy)) {
						Fail("p.luma_gain takes a float expression (got "_s + TyName(gainTy) + ")"_s);
						return;
					}
					out += indent + passName + ".LumaGain = "_s + gain + ";\n"_s;
					return;
				}

				Ty expected;
				const char* member;
				if (field == "color"_s) { expected = Ty::Vec4; member = "Color"; }
				else if (field == "offset_color"_s) { expected = Ty::Vec3; member = "OffsetColor"; }
				else if (field == "screen_offset"_s) { expected = Ty::Vec2; member = "ScreenOffset"; }
				else {
					Fail("unknown pass field '."_s + field + "' (fields: color, offset_color, screen_offset, blend, tev, luma_gain)"_s);
					return;
				}
				Ty rhsTy;
				String code = EmitExpr(rhs, 0, rhsTy);
				if (!_ok) return;
				if (rhsTy != expected) {
					Fail("p."_s + field + " takes a "_s + TyName(expected) + " expression (got "_s + TyName(rhsTy) + ")"_s);
					return;
				}
				out += indent + "Store("_s + passName + "."_s + member + ", "_s + code + ");\n"_s;
				if (field == "offset_color"_s) {
					// Writing the offset colour is what enables it (PVR_SPECULAR on the polygon header)
					out += indent + passName + ".HasOffsetColor = true;\n"_s;
					// Also recorded per (program, variant) in the generated table: the PVR compiles
					// specular into the BASE polygon header, so the dispatch has to know before any
					// pass runs whether the function can ever write an offset colour
					_usesOffsetColor = true;
				}
			}

			// A (compound) assignment to a local scalar/vector or one component of a local vector
			String EmitLocalAssign(const Expr* e)
			{
				const Expr* target = e->A.get();
				Ty dstTy;
				String dstCode;

				if (target->Kind == ExprKind::Ident) {
					if (!Lookup(target->Text, dstTy)) {
						Fail("cannot assign to unknown variable '"_s + target->Text + "'"_s);
						return {};
					}
					if (dstTy == Ty::Pass) {
						Fail("a pass variable itself cannot be assigned (assign its fields)"_s);
						return {};
					}
					if (target->Text == "COLOR"_s) { Fail("COLOR is read-only"_s); return {}; }
					dstCode = target->Text;
				} else if (target->Kind == ExprKind::Member && target->A != nullptr && target->A->Kind == ExprKind::Ident) {
					// Single-component swizzle store ("v.x = ..."); multi-component stores stay out of
					// the vocabulary (the runtime vec types expose multi-swizzles as read-only methods)
					Ty baseTy;
					if (!Lookup(target->A->Text, baseTy)) {
						Fail("cannot assign to unknown variable '"_s + target->A->Text + "'"_s);
						return {};
					}
					if (target->A->Text == "COLOR"_s) { Fail("COLOR is read-only"_s); return {}; }
					if (!IsVec(baseTy)) {
						Fail("'."_s + target->Text + "' store target must be a vector variable"_s);
						return {};
					}
					String component = NormalizeSwizzle(target->Text, Comps(baseTy));
					if (!_ok) return {};
					if (component.size() != 1) {
						Fail("only single components of a vector can be assigned ('."_s + target->Text + "' is a multi-component swizzle)"_s);
						return {};
					}
					dstTy = Ty::Float;
					dstCode = target->A->Text + "."_s + component;
				} else {
					Fail("unsupported assignment target"_s);
					return {};
				}

				Ty rhsTy;
				String rhs = EmitExpr(e->B.get(), 1, rhsTy);
				if (!_ok) return {};

				if (e->Text == "="_s) {
					if (!CanAssign(dstTy, rhsTy)) {
						Fail("cannot assign a "_s + TyName(rhsTy) + " expression to "_s + TyName(dstTy) + " '"_s + dstCode + "'"_s);
						return {};
					}
				} else if (e->Text == "+="_s || e->Text == "-="_s || e->Text == "*="_s || e->Text == "/="_s) {
					bool ok;
					if (dstTy == Ty::Int) ok = (rhsTy == Ty::Int);
					else if (dstTy == Ty::Float) ok = IsNumericScalar(rhsTy);
					else if (IsVec(dstTy)) ok = (rhsTy == dstTy || IsNumericScalar(rhsTy));	// vec op= scalar broadcasts
					else ok = false;
					if (!ok) {
						Fail("cannot apply '"_s + e->Text + "' with a "_s + TyName(rhsTy) + " operand to "_s + TyName(dstTy) + " '"_s + dstCode + "'"_s);
						return {};
					}
				} else {
					Fail("assignment operator '"_s + e->Text + "' is not part of the fixed_function grammar"_s);
					return {};
				}
				return dstCode + " "_s + e->Text + " "_s + rhs;
			}

			void EmitFor(const Stmt* s, const String& indent, String& out)
			{
				_scopes.emplace_back();

				// "for with integer bounds": the induction variable is an int, the condition a bool
				String init;
				if (s->ForInit == nullptr) {
					Fail("for loops need an init statement (an int counter)"_s);
				} else if (s->ForInit->Kind == StmtKind::VarDecl) {
					if (s->ForInit->DeclType != Ty::Int) {
						Fail("the for-loop counter must be an int"_s);
					} else {
						String declOut;
						EmitVarDecl(s->ForInit.get(), ""_s, declOut);
						// Reuse the declaration emission, then strip its statement dressing (";\n")
						if (_ok && declOut.size() >= 2) init = String{declOut.prefix(declOut.size() - 2)};
					}
				} else if (s->ForInit->Kind == StmtKind::ExprStmt && s->ForInit->E != nullptr && s->ForInit->E->Kind == ExprKind::Assign) {
					init = EmitLocalAssign(s->ForInit->E.get());
				} else {
					Fail("the for-loop init must declare or assign an int counter"_s);
				}
				if (!_ok) { _scopes.pop_back(); return; }

				if (s->ForCond == nullptr) {
					Fail("for loops need an integer-bound condition"_s);
					_scopes.pop_back();
					return;
				}
				Ty condTy;
				String cond = EmitExpr(s->ForCond.get(), 0, condTy);
				if (_ok && condTy != Ty::Bool) Fail("the for-loop condition must be a bool expression"_s);
				if (!_ok) { _scopes.pop_back(); return; }

				String update;
				if (s->ForUpdate != nullptr) {
					if (s->ForUpdate->Kind == ExprKind::Unary && (s->ForUpdate->Text == "++"_s || s->ForUpdate->Text == "--"_s)) {
						update = EmitIncDec(s->ForUpdate.get());
					} else if (s->ForUpdate->Kind == ExprKind::Assign) {
						update = EmitLocalAssign(s->ForUpdate.get());
					} else {
						Fail("the for-loop update must be an assignment or '++'/'--'"_s);
					}
				}
				if (!_ok) { _scopes.pop_back(); return; }

				out += indent + "for ("_s + init + "; "_s + cond + "; "_s + update + ") "_s;
				EmitBranch(s->ForBody.get(), indent, out);
				out += "\n"_s;
				_scopes.pop_back();
			}

			// --- Expressions ---------------------------------------------------------------------------

			// Precedence of an emitted expression, so children get minimal correct parentheses
			std::int32_t EmitPrec(const Expr* e) const
			{
				switch (e->Kind) {
					case ExprKind::Binary: return BinPrec(e->Text);
					case ExprKind::Conditional: return 2;
					case ExprKind::Unary: return 90;
					default: return 100;
				}
			}

			String EmitExpr(const Expr* e, std::int32_t minPrec, Ty& t)
			{
				if (e == nullptr) { Fail("missing expression"_s); return {}; }
				String s = EmitCore(e, t);
				if (!_ok) return {};
				if (EmitPrec(e) < minPrec) return "("_s + s + ")"_s;
				return s;
			}

			String EmitCore(const Expr* e, Ty& t)
			{
				switch (e->Kind) {
					case ExprKind::IntLit: case ExprKind::UIntLit:
						t = Ty::Int;
						return e->Text;
					case ExprKind::FloatLit:
						t = Ty::Float;
						return NormalizeFloatLiteral(e->Text);
					case ExprKind::BoolLit:
						t = Ty::Bool;
						return e->Text;
					case ExprKind::Ident:
						return EmitIdent(e->Text, t);
					case ExprKind::Member:
						return EmitMember(e, t);
					case ExprKind::Call:
						return EmitCall(e, t);
					case ExprKind::Unary: {
						if (e->Text == "++"_s || e->Text == "--"_s) {
							Fail("'++'/'--' can only be used as a statement or a for-loop update"_s);
							return {};
						}
						Ty inner;
						String code = EmitExpr(e->A.get(), 90, inner);
						if (!_ok) return {};
						if (e->Text == "!"_s) {
							if (inner != Ty::Bool) { Fail("'!' takes a bool operand"_s); return {}; }
							t = Ty::Bool;
							return "!"_s + code;
						}
						// Unary minus on any numeric scalar or vector
						if (!IsNumericScalar(inner) && !IsVec(inner)) { Fail("unary '-' takes a numeric operand"_s); return {}; }
						t = inner;
						if (!code.empty() && code[0] == '-') return "- "_s + code;	// avoid a spurious "--"
						return "-"_s + code;
					}
					case ExprKind::Binary:
						return EmitBinary(e, t);
					case ExprKind::Assign:
						Fail("assignments cannot be nested inside expressions"_s);
						return {};
					case ExprKind::Conditional: {
						Ty condTy, thenTy, elseTy;
						String cond = EmitExpr(e->A.get(), 3, condTy);
						String thenCode = EmitExpr(e->B.get(), 3, thenTy);
						String elseCode = EmitExpr(e->C.get(), 2, elseTy);
						if (!_ok) return {};
						if (condTy != Ty::Bool) { Fail("the ?: condition must be a bool expression"_s); return {}; }
						if (thenTy != elseTy) { Fail("both ?: branches must have the same type"_s); return {}; }
						t = thenTy;
						return cond + " ? "_s + thenCode + " : "_s + elseCode;
					}
					case ExprKind::Index:
						break;
				}
				Fail("unsupported expression"_s);
				return {};
			}

			String EmitIdent(const String& name, Ty& t)
			{
				if (name == "COLOR"_s) {
					// The instance colour of the draw being dispatched (the shader's COLOR built-in);
					// loaded once in the function prologue
					_usesColor = true;
					t = Ty::Vec4;
					return "COLOR"_s;
				}
				if (Lookup(name, t)) {
					if (t == Ty::Pass) {
						Fail("a pass variable ('"_s + name + "') is not a value; assign its fields or submit_quad() it"_s);
						return {};
					}
					return name;
				}
				if (IsEnumValueName(name)) {
					Fail("'"_s + name + "' is a blend/tev preset and can only be assigned to a pass's .blend/.tev"_s);
					return {};
				}
				Fail("unknown identifier '"_s + name + "' (built-ins: COLOR, texel_size(), has_texel_size())"_s);
				return {};
			}

			// Maps rgba/stpq swizzle spellings onto xyzw and validates the components against the base width
			String NormalizeSwizzle(StringView field, std::int32_t baseComps)
			{
				String normalized;
				for (std::size_t i = 0; i < field.size(); i++) {
					char c = field[i];
					char n;
					switch (c) {
						case 'x': case 'r': case 's': n = 'x'; break;
						case 'y': case 'g': case 't': n = 'y'; break;
						case 'z': case 'b': case 'p': n = 'z'; break;
						case 'w': case 'a': case 'q': n = 'w'; break;
						default:
							Fail("member '."_s + field + "' is not a swizzle"_s);
							return {};
					}
					std::int32_t index = (n == 'x' ? 0 : n == 'y' ? 1 : n == 'z' ? 2 : 3);
					if (index >= baseComps) {
						Fail("swizzle '."_s + field + "' reads past the end of a "_s + TyName(VecOf(baseComps)) + ""_s);
						return {};
					}
					normalized += String{&n, 1};
				}
				if (normalized.empty() || normalized.size() > 4) {
					Fail("swizzle '."_s + field + "' has an unsupported length"_s);
					return {};
				}
				return normalized;
			}

			String EmitMember(const Expr* e, Ty& t)
			{
				Ty baseTy;
				String base = EmitExpr(e->A.get(), 100, baseTy);
				if (!_ok) return {};
				if (baseTy == Ty::Pass) {
					Fail("pass fields cannot be read back (they are write-only descriptors)"_s);
					return {};
				}
				if (!IsVec(baseTy)) {
					Fail("member '."_s + e->Text + "' applied to a non-vector "_s + TyName(baseTy) + ""_s);
					return {};
				}
				String sw = NormalizeSwizzle(e->Text, Comps(baseTy));
				if (!_ok) return {};
				if (sw.size() == 1) {
					t = Ty::Float;
					return base + "."_s + sw;
				}
				// The identity swizzle is a no-op; other multi-swizzles map to the runtime's method set
				if (static_cast<std::int32_t>(sw.size()) == Comps(baseTy) &&
					(sw == "xy"_s || sw == "xyz"_s || sw == "xyzw"_s)) {
					t = baseTy;
					return base;
				}
				if (sw == "xy"_s || sw == "zw"_s || sw == "xyz"_s || sw == "yzw"_s) {
					if (sw == "zw"_s && baseTy != Ty::Vec4) { Fail("swizzle '.zw' needs a vec4 base"_s); return {}; }
					if ((sw == "xyz"_s || sw == "yzw"_s) && baseTy != Ty::Vec4) { Fail("swizzle '."_s + sw + "' needs a vec4 base"_s); return {}; }
					t = VecOf(static_cast<std::int32_t>(sw.size()));
					return base + "."_s + sw + "()"_s;
				}
				Fail("swizzle '."_s + e->Text + "' is not supported (single components, .xy, .zw, .xyz, .yzw and their rgba spellings)"_s);
				return {};
			}

			String EmitArgs(const Expr* call, SmallVectorImpl<Ty>& types)
			{
				String r;
				for (std::size_t i = 0; i < call->Args.size(); i++) {
					if (i != 0) r += ", "_s;
					Ty t;
					r += EmitExpr(call->Args[i].get(), 0, t);
					if (!_ok) return {};
					types.push_back(t);
				}
				return r;
			}

			String EmitCall(const Expr* e, Ty& t)
			{
				const String& name = e->Text;

				if (name == "texel_size"_s) {
					if (!e->Args.empty()) { Fail("texel_size() takes no arguments"_s); return {}; }
					// The per-backend displacement of one texel in the quad's own coordinate space
					// (raster space on the PVR, logical pixels on the GX), derived from the texel
					// size in UV space the Outline shader family carries in its instance color.xy -
					// see the EffectContext contract in FixedFunctionPass.h
					Require(FixedFunctionRequirements::NeedsTexelStep);
					t = Ty::Vec2;
					return "vec2(ctx.TexelStepX(), ctx.TexelStepY())"_s;
				}
				if (name == "has_texel_size"_s) {
					if (!e->Args.empty()) { Fail("has_texel_size() takes no arguments"_s); return {}; }
					// Whether texel_size() is derivable at all (a zero texRect has no scale); blocks
					// guard their ring taps with it exactly like the handwritten effects did
					Require(FixedFunctionRequirements::NeedsTexelStep);
					t = Ty::Bool;
					return "ctx.HasTexelStep()"_s;
				}
				if (name == "submit_quad"_s || name == "submit_strip"_s || name == "submit_strip_shaded"_s ||
					name == "strip_position"_s || name == "strip_uv"_s || name == "strip_color"_s) {
					Fail(name + "(...) is a statement, not a value"_s);
					return {};
				}

				// Pre-clip geometry of the instance's quad: the raster position of the sprite's (0,0)
				// corner and the raster displacements of its local axes. Deliberately NOT the corner
				// arrays - those are post-scissor-clip, and geometry synthesized from them (the iris
				// circle, the warp bands) would be distorted by a clipped quad.
				if (name == "quad_origin"_s || name == "quad_axis_x"_s || name == "quad_axis_y"_s) {
					if (!RequireExtended(name)) return {};
					if (!e->Args.empty()) { Fail(name + "() takes no arguments"_s); return {}; }
					Require(FixedFunctionRequirements::NeedsQuadAxes);
					t = Ty::Vec2;
					if (name == "quad_origin"_s) return "vec2(ctx.QuadOriginX(), ctx.QuadOriginY())"_s;
					if (name == "quad_axis_x"_s) return "vec2(ctx.QuadAxisXx(), ctx.QuadAxisXy())"_s;
					return "vec2(ctx.QuadAxisYx(), ctx.QuadAxisYy())"_s;
				}

				// Resolved uniforms by name (the geometry effects consume the same uViewSize/uShift/...
				// values their GLSL does), through the program's existing ResolveUniform machinery. The
				// argument is an identifier rather than a string literal - the expression tokenizer has
				// no string literals, and an identifier is validated as a well-formed uniform name.
				if (name == "has_uniform"_s || name == "uniform_float"_s || name == "uniform_vec2"_s || name == "uniform_vec4"_s) {
					if (!RequireExtended(name)) return {};
					Require(FixedFunctionRequirements::NeedsUniforms);
					if (e->Args.size() != 1 || e->Args[0]->Kind != ExprKind::Ident) {
						Fail("usage: "_s + name + "(<uniform name>), e.g. "_s + name + "(uViewSize)"_s);
						return {};
					}
					const String& uniform = e->Args[0]->Text;
					if (name == "has_uniform"_s) {
						t = Ty::Bool;
						return "ctx.HasUniform(\""_s + uniform + "\")"_s;
					}
					if (name == "uniform_float"_s) {
						t = Ty::Float;
						return "UniformFloat(ctx, \""_s + uniform + "\")"_s;
					}
					t = (name == "uniform_vec2"_s ? Ty::Vec2 : Ty::Vec4);
					return String{name == "uniform_vec2"_s ? "UniformVec2" : "UniformVec4"} +
						"(ctx, \""_s + uniform + "\")"_s;
				}

				// Scalar conversion constructors
				if (name == "float"_s || name == "int"_s) {
					SmallVector<Ty, 0> argTypes;
					String args = EmitArgs(e, argTypes);
					if (!_ok) return {};
					if (argTypes.size() != 1 || !IsNumericScalar(argTypes[0])) {
						Fail(name + "(...) takes exactly one numeric scalar argument"_s);
						return {};
					}
					t = (name == "float"_s ? Ty::Float : Ty::Int);
					return name + "("_s + args + ")"_s;
				}

				// Vector constructors: a single-scalar splat, a same-size vector copy, or any
				// scalar/vector mix whose components sum to the target width (the emitted ff types
				// provide a constructor for every such shape up to vec4)
				Ty ctorTy;
				if (TryLocalType(name, ctorTy) && IsVec(ctorTy)) {
					SmallVector<Ty, 0> argTypes;
					String args = EmitArgs(e, argTypes);
					if (!_ok) return {};
					if (argTypes.empty()) { Fail(name + "() needs arguments"_s); return {}; }
					std::int32_t sum = 0;
					for (Ty a : argTypes) {
						if (a == Ty::Bool || a == Ty::Pass) { Fail(name + "(...) arguments must be numeric"_s); return {}; }
						sum += (IsVec(a) ? Comps(a) : 1);
					}
					bool splat = (argTypes.size() == 1 && IsNumericScalar(argTypes[0]));
					if (!splat && sum != Comps(ctorTy)) {
						Fail(name + "(...) arguments provide "_s + Death::format("{}", sum) + " components, expected "_s + Death::format("{}", Comps(ctorTy)));
						return {};
					}
					t = ctorTy;
					return name + "("_s + args + ")"_s;
				}

				if (name == "min"_s || name == "max"_s) {
					SmallVector<Ty, 0> a;
					String args = EmitArgs(e, a);
					if (!_ok) return {};
					if (a.size() != 2) { Fail(name + "() takes 2 arguments"_s); return {}; }
					// Mixed int/float scalars are rejected (as in GLSL) — the emitted C++ overload set
					// would otherwise be ambiguous
					if (a[0] == Ty::Int && a[1] == Ty::Int) t = Ty::Int;
					else if (a[0] == Ty::Float && a[1] == Ty::Float) t = Ty::Float;
					else if (IsVec(a[0]) && (a[1] == a[0] || a[1] == Ty::Float)) t = a[0];
					else { Fail(name + "() takes (float, float), (int, int), (vecN, vecN) or (vecN, float)"_s); return {}; }
					return name + "("_s + args + ")"_s;
				}
				if (name == "clamp"_s) {
					SmallVector<Ty, 0> a;
					String args = EmitArgs(e, a);
					if (!_ok) return {};
					if (a.size() != 3) { Fail("clamp() takes 3 arguments"_s); return {}; }
					if (a[0] == Ty::Float && a[1] == Ty::Float && a[2] == Ty::Float) t = Ty::Float;
					else if (a[0] == Ty::Int && a[1] == Ty::Int && a[2] == Ty::Int) t = Ty::Int;
					else if (IsVec(a[0]) && ((a[1] == a[0] && a[2] == a[0]) || (a[1] == Ty::Float && a[2] == Ty::Float))) t = a[0];
					else { Fail("clamp() takes (float, float, float), (int, int, int), (vecN, vecN, vecN) or (vecN, float, float)"_s); return {}; }
					return "clamp("_s + args + ")"_s;
				}
				if (name == "mix"_s) {
					SmallVector<Ty, 0> a;
					String args = EmitArgs(e, a);
					if (!_ok) return {};
					if (a.size() != 3) { Fail("mix() takes 3 arguments"_s); return {}; }
					if (a[0] == Ty::Float && a[1] == Ty::Float && a[2] == Ty::Float) t = Ty::Float;
					else if (IsVec(a[0]) && a[1] == a[0] && (a[2] == Ty::Float || a[2] == a[0])) t = a[0];
					else { Fail("mix() takes (float, float, float), (vecN, vecN, float) or (vecN, vecN, vecN)"_s); return {}; }
					return "mix("_s + args + ")"_s;
				}
				if (name == "ceil"_s || name == "floor"_s) {
					SmallVector<Ty, 0> a;
					String args = EmitArgs(e, a);
					if (!_ok) return {};
					if (a.size() != 1 || (a[0] != Ty::Float && !IsVec(a[0]))) {
						Fail(name + "() takes one float or vector argument"_s);
						return {};
					}
					t = a[0];
					return name + "("_s + args + ")"_s;
				}
				// Scalar transcendentals of the geometry effects (the iris fan directions, the circle
				// radii); float-only - the handwritten code they transcribe only ever used floats
				if (name == "abs"_s || name == "sqrt"_s || name == "sin"_s || name == "cos"_s) {
					SmallVector<Ty, 0> a;
					String args = EmitArgs(e, a);
					if (!_ok) return {};
					if (a.size() != 1 || a[0] != Ty::Float) {
						Fail(name + "() takes one float argument"_s);
						return {};
					}
					t = Ty::Float;
					return name + "("_s + args + ")"_s;
				}

				Fail("unknown function '"_s + name + "' (supported: min, max, clamp, mix, ceil, floor, abs, sqrt, sin, cos, texel_size, has_texel_size, quad_origin, quad_axis_x, quad_axis_y, has_uniform, uniform_float, uniform_vec2, uniform_vec4, submit_quad, submit_strip[_shaded], strip_position/uv/color, vec/float/int constructors)"_s);
				return {};
			}

			String EmitBinary(const Expr* e, Ty& t)
			{
				const String& op = e->Text;
				std::int32_t p = BinPrec(op);
				Ty lt, rt;
				String l = EmitExpr(e->A.get(), p, lt);
				String r = EmitExpr(e->B.get(), p + 1, rt);
				if (!_ok) return {};

				if (op == "+"_s || op == "-"_s || op == "*"_s || op == "/"_s) {
					if (lt == Ty::Bool || rt == Ty::Bool || lt == Ty::Pass || rt == Ty::Pass) {
						Fail("'"_s + op + "' takes numeric operands"_s);
						return {};
					}
					if (IsVec(lt) && IsVec(rt)) {
						if (lt != rt) { Fail("'"_s + op + "' operands have mismatched vector sizes"_s); return {}; }
						t = lt;
					} else if (IsVec(lt)) {
						t = lt;			// vec op scalar broadcasts (an int scalar converts to float)
					} else if (IsVec(rt)) {
						t = rt;			// scalar op vec broadcasts
					} else {
						t = (lt == Ty::Int && rt == Ty::Int) ? Ty::Int : Ty::Float;
					}
					return l + " "_s + op + " "_s + r;
				}
				if (op == "%"_s) {
					if (lt != Ty::Int || rt != Ty::Int) { Fail("'%' takes int operands"_s); return {}; }
					t = Ty::Int;
					return l + " % "_s + r;
				}
				if (op == "<"_s || op == ">"_s || op == "<="_s || op == ">="_s) {
					if (!IsNumericScalar(lt) || !IsNumericScalar(rt)) { Fail("'"_s + op + "' compares numeric scalars"_s); return {}; }
					t = Ty::Bool;
					return l + " "_s + op + " "_s + r;
				}
				if (op == "=="_s || op == "!="_s) {
					bool ok = (IsNumericScalar(lt) && IsNumericScalar(rt)) || (lt == Ty::Bool && rt == Ty::Bool);
					if (!ok) { Fail("'"_s + op + "' compares scalars"_s); return {}; }
					t = Ty::Bool;
					return l + " "_s + op + " "_s + r;
				}
				if (op == "&&"_s || op == "||"_s || op == "^^"_s) {
					if (lt != Ty::Bool || rt != Ty::Bool) { Fail("'"_s + op + "' takes bool operands"_s); return {}; }
					t = Ty::Bool;
					return l + " "_s + (op == "^^"_s ? "!="_s : StringView{op}) + " "_s + r;
				}
				Fail("operator '"_s + op + "' is not part of the fixed_function grammar"_s);
				return {};
			}
		};
	}

	// --- Public entry points ---------------------------------------------------------------------------

	FixedFunctionResult ConsoleFixedFunction::TranspileBlock(const FixedFunctionBlock& block,
		StringView define, FixedFunctionBackend backend)
	{
		FixedFunctionResult result;

		// Resolve the variant define exactly like the fragment stage: comments stripped, then the
		// mini preprocessor with "#define <VARIANT> (1)" baked in, so "#ifdef USE_PALETTE" inside
		// the block selects per variant
		SmallVector<SourceLine, 0> lines = block.Lines;
		ShaderParser::StripComments(lines);
		Preprocessor preprocessor;
		if (!define.empty()) {
			preprocessor.Define(define, "1");
		}
		SmallVector<SourceLine, 0> preprocessed;
		Diagnostic diag;
		if (!preprocessor.Run(lines, preprocessed, diag)) {
			result.Error = diag.Message;
			result.Line = diag.Line;
			return result;
		}

		// Tokenize per line, tagging each token with its ORIGINAL 1-based input line so every
		// diagnostic downstream names a real location in the .shader file
		SmallVector<GlslToken, 0> tokens;
		for (const SourceLine& line : preprocessed) {
			GlslExprTokenizer::Tokenize(line.Text, 0, line.Text.size(), static_cast<std::size_t>(line.Line), tokens);
		}
		{
			GlslToken end;
			end.Type = GlslTokenType::End;
			end.Index = static_cast<std::size_t>(preprocessed.empty() ? block.Line : preprocessed.back().Line);
			tokens.push_back(std::move(end));
		}

		// "pipeline <name>;" — binds the program to a backend pipeline stage that consumes an engine
		// data structure. Recognized on the raw token stream (before the statement grammar) because it
		// is not an expression, and required to be the FIRST statement so a reader meets the binding
		// before the passes.
		//
		// A block MAY carry passes after it. The stage keeps whatever it cannot delegate - a per-vertex
		// or per-texel loop over an engine buffer is backend mechanism, and dispatching a transpiled
		// call per tile is not affordable on this tier - while the passes carry the per-draw POLICY that
		// used to be duplicated in every backend (the water overlay of the lighting compositor: two
		// screen quads whose colours and thresholds belong next to the GLSL they approximate). The
		// backend runs the stage first and the function after it, so the passes composite over whatever
		// the stage produced.
		for (std::size_t i = 0; i < tokens.size(); i++) {
			if (tokens[i].Type == GlslTokenType::Identifier && tokens[i].Text == "pipeline"_s) {
				const std::int32_t lineNo = static_cast<std::int32_t>(tokens[i].Index);
				if (i != 0 || tokens.size() < 4 ||
					tokens[1].Type != GlslTokenType::Identifier ||
					!(tokens[2].Type == GlslTokenType::Operator && tokens[2].Text == ";"_s)) {
					result.Error = "\"pipeline <name>;\" must be the first statement of the fixed_function block"_s;
					result.Line = lineNo;
					return result;
				}
				// The known stages map 1:1 onto nCine::RHI::FixedFunctionIntrinsic members. Only the
				// stages that consume engine data structures remain - the geometry-synthesized quad
				// effects (iris, warped background) are ordinary transpiled blocks since phase 4.
				const StringView name = tokens[1].Text;
				const char* intrinsic =
					name == "tile_map_mesh"_s ? "TileMapMesh" :
					name == "lighting_combine"_s ? "LightingCombine" :
					name == "line_strip_mesh"_s ? "LineStripMesh" : nullptr;
				if (intrinsic == nullptr) {
					result.Error = "unknown pipeline \""_s + name + "\" (known: tile_map_mesh, lighting_combine, line_strip_mesh)"_s;
					result.Line = lineNo;
					return result;
				}
				result.Intrinsic = intrinsic;
				// Nothing but the binding: no function at all, the long-standing form
				if (tokens.size() == 4 && tokens[3].Type == GlslTokenType::End) {
					result.Ok = true;
					return result;
				}
				// Passes follow: drop the three binding tokens and transpile the remainder normally
				tokens.erase(tokens.begin(), tokens.begin() + 3);
				break;
			}
		}

		Parser parser(tokens);
		StmtPtr top = parser.Run();
		if (!parser.Ok()) {
			result.Error = parser.Reason();
			result.Line = parser.ErrorLine();
			return result;
		}

		// The block's own target list drives every capability check: the extended vocabulary needs a
		// block that names its backends (a generic one stays in the portable quad-only core), and what
		// a named block may use is the INTERSECTION of what its targets can do
		Emitter emitter(backend, block.Targets);
		String body = emitter.EmitBody(top.get(), "\t\t\t"_s);
		if (!emitter.Ok()) {
			result.Error = emitter.Reason();
			result.Line = emitter.ErrorLine();
			return result;
		}

		// Only the BODY is returned, without the signature or provenance comment: bodies carry no
		// per-program name, so two (program, variant) blocks that transpile to the same passes come
		// out byte-identical, and Main.cpp can fold them into one shared emitted function
		String out;
		out += "\t\t\tusing namespace ff;\n"_s;
		if (emitter.UsesColor()) {
			out += "\t\t\tconst vec4 COLOR = LoadColor(ctx.Color());\n"_s;
		}
		out += body;

		result.Ok = true;
		result.Body = std::move(out);
		result.UsesOffsetColor = emitter.UsesOffsetColor();
		result.Requirements = emitter.Requirements();
		return result;
	}

	String ConsoleFixedFunction::BuildRuntimeSupport()
	{
		// Emitted once per aggregate header at 2-tab indent (backend namespace + anonymous
		// namespace). Everything is `inline` so support code the emitted effects happen not to
		// call stays warning-free in the including device translation unit.
		return String{R"SUPPORT(		// Minimal GLSL-style float-vector runtime for the generated effect functions. Defined in the
		// generated header (not shipped as an engine header) so the artifact stays self-contained:
		// FixedFunctionPass.h remains the only contract between the generator and the backends.
		namespace ff
		{
			struct vec2
			{
				float x, y;
				vec2() : x(0.0f), y(0.0f) {}
				explicit vec2(float s) : x(s), y(s) {}
				vec2(float x_, float y_) : x(x_), y(y_) {}
			};
			struct vec3
			{
				float x, y, z;
				vec3() : x(0.0f), y(0.0f), z(0.0f) {}
				explicit vec3(float s) : x(s), y(s), z(s) {}
				vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
				vec3(const vec2& v, float z_) : x(v.x), y(v.y), z(z_) {}
				vec3(float x_, const vec2& v) : x(x_), y(v.x), z(v.y) {}
				vec2 xy() const { return vec2(x, y); }
			};
			struct vec4
			{
				float x, y, z, w;
				vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
				explicit vec4(float s) : x(s), y(s), z(s), w(s) {}
				vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
				vec4(const vec2& v, float z_, float w_) : x(v.x), y(v.y), z(z_), w(w_) {}
				vec4(float x_, const vec2& v, float w_) : x(x_), y(v.x), z(v.y), w(w_) {}
				vec4(float x_, float y_, const vec2& v) : x(x_), y(y_), z(v.x), w(v.y) {}
				vec4(const vec2& a, const vec2& b) : x(a.x), y(a.y), z(b.x), w(b.y) {}
				vec4(const vec3& v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
				vec4(float x_, const vec3& v) : x(x_), y(v.x), z(v.y), w(v.z) {}
				vec2 xy() const { return vec2(x, y); }
				vec2 zw() const { return vec2(z, w); }
				vec3 xyz() const { return vec3(x, y, z); }
				vec3 yzw() const { return vec3(y, z, w); }
			};

			inline vec2 operator+(const vec2& a, const vec2& b) { return vec2(a.x + b.x, a.y + b.y); }
			inline vec2 operator-(const vec2& a, const vec2& b) { return vec2(a.x - b.x, a.y - b.y); }
			inline vec2 operator*(const vec2& a, const vec2& b) { return vec2(a.x * b.x, a.y * b.y); }
			inline vec2 operator/(const vec2& a, const vec2& b) { return vec2(a.x / b.x, a.y / b.y); }
			inline vec2 operator+(const vec2& a, float s) { return vec2(a.x + s, a.y + s); }
			inline vec2 operator-(const vec2& a, float s) { return vec2(a.x - s, a.y - s); }
			inline vec2 operator*(const vec2& a, float s) { return vec2(a.x * s, a.y * s); }
			inline vec2 operator/(const vec2& a, float s) { return vec2(a.x / s, a.y / s); }
			inline vec2 operator+(float s, const vec2& a) { return vec2(s + a.x, s + a.y); }
			inline vec2 operator-(float s, const vec2& a) { return vec2(s - a.x, s - a.y); }
			inline vec2 operator*(float s, const vec2& a) { return vec2(s * a.x, s * a.y); }
			inline vec2 operator/(float s, const vec2& a) { return vec2(s / a.x, s / a.y); }
			inline vec2 operator-(const vec2& a) { return vec2(-a.x, -a.y); }
			inline vec2& operator+=(vec2& a, const vec2& b) { a.x += b.x; a.y += b.y; return a; }
			inline vec2& operator-=(vec2& a, const vec2& b) { a.x -= b.x; a.y -= b.y; return a; }
			inline vec2& operator*=(vec2& a, const vec2& b) { a.x *= b.x; a.y *= b.y; return a; }
			inline vec2& operator/=(vec2& a, const vec2& b) { a.x /= b.x; a.y /= b.y; return a; }
			inline vec2& operator+=(vec2& a, float s) { a.x += s; a.y += s; return a; }
			inline vec2& operator-=(vec2& a, float s) { a.x -= s; a.y -= s; return a; }
			inline vec2& operator*=(vec2& a, float s) { a.x *= s; a.y *= s; return a; }
			inline vec2& operator/=(vec2& a, float s) { a.x /= s; a.y /= s; return a; }

			inline vec3 operator+(const vec3& a, const vec3& b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
			inline vec3 operator-(const vec3& a, const vec3& b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
			inline vec3 operator*(const vec3& a, const vec3& b) { return vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
			inline vec3 operator/(const vec3& a, const vec3& b) { return vec3(a.x / b.x, a.y / b.y, a.z / b.z); }
			inline vec3 operator+(const vec3& a, float s) { return vec3(a.x + s, a.y + s, a.z + s); }
			inline vec3 operator-(const vec3& a, float s) { return vec3(a.x - s, a.y - s, a.z - s); }
			inline vec3 operator*(const vec3& a, float s) { return vec3(a.x * s, a.y * s, a.z * s); }
			inline vec3 operator/(const vec3& a, float s) { return vec3(a.x / s, a.y / s, a.z / s); }
			inline vec3 operator+(float s, const vec3& a) { return vec3(s + a.x, s + a.y, s + a.z); }
			inline vec3 operator-(float s, const vec3& a) { return vec3(s - a.x, s - a.y, s - a.z); }
			inline vec3 operator*(float s, const vec3& a) { return vec3(s * a.x, s * a.y, s * a.z); }
			inline vec3 operator/(float s, const vec3& a) { return vec3(s / a.x, s / a.y, s / a.z); }
			inline vec3 operator-(const vec3& a) { return vec3(-a.x, -a.y, -a.z); }
			inline vec3& operator+=(vec3& a, const vec3& b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
			inline vec3& operator-=(vec3& a, const vec3& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; return a; }
			inline vec3& operator*=(vec3& a, const vec3& b) { a.x *= b.x; a.y *= b.y; a.z *= b.z; return a; }
			inline vec3& operator/=(vec3& a, const vec3& b) { a.x /= b.x; a.y /= b.y; a.z /= b.z; return a; }
			inline vec3& operator+=(vec3& a, float s) { a.x += s; a.y += s; a.z += s; return a; }
			inline vec3& operator-=(vec3& a, float s) { a.x -= s; a.y -= s; a.z -= s; return a; }
			inline vec3& operator*=(vec3& a, float s) { a.x *= s; a.y *= s; a.z *= s; return a; }
			inline vec3& operator/=(vec3& a, float s) { a.x /= s; a.y /= s; a.z /= s; return a; }

			inline vec4 operator+(const vec4& a, const vec4& b) { return vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
			inline vec4 operator-(const vec4& a, const vec4& b) { return vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
			inline vec4 operator*(const vec4& a, const vec4& b) { return vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
			inline vec4 operator/(const vec4& a, const vec4& b) { return vec4(a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w); }
			inline vec4 operator+(const vec4& a, float s) { return vec4(a.x + s, a.y + s, a.z + s, a.w + s); }
			inline vec4 operator-(const vec4& a, float s) { return vec4(a.x - s, a.y - s, a.z - s, a.w - s); }
			inline vec4 operator*(const vec4& a, float s) { return vec4(a.x * s, a.y * s, a.z * s, a.w * s); }
			inline vec4 operator/(const vec4& a, float s) { return vec4(a.x / s, a.y / s, a.z / s, a.w / s); }
			inline vec4 operator+(float s, const vec4& a) { return vec4(s + a.x, s + a.y, s + a.z, s + a.w); }
			inline vec4 operator-(float s, const vec4& a) { return vec4(s - a.x, s - a.y, s - a.z, s - a.w); }
			inline vec4 operator*(float s, const vec4& a) { return vec4(s * a.x, s * a.y, s * a.z, s * a.w); }
			inline vec4 operator/(float s, const vec4& a) { return vec4(s / a.x, s / a.y, s / a.z, s / a.w); }
			inline vec4 operator-(const vec4& a) { return vec4(-a.x, -a.y, -a.z, -a.w); }
			inline vec4& operator+=(vec4& a, const vec4& b) { a.x += b.x; a.y += b.y; a.z += b.z; a.w += b.w; return a; }
			inline vec4& operator-=(vec4& a, const vec4& b) { a.x -= b.x; a.y -= b.y; a.z -= b.z; a.w -= b.w; return a; }
			inline vec4& operator*=(vec4& a, const vec4& b) { a.x *= b.x; a.y *= b.y; a.z *= b.z; a.w *= b.w; return a; }
			inline vec4& operator/=(vec4& a, const vec4& b) { a.x /= b.x; a.y /= b.y; a.z /= b.z; a.w /= b.w; return a; }
			inline vec4& operator+=(vec4& a, float s) { a.x += s; a.y += s; a.z += s; a.w += s; return a; }
			inline vec4& operator-=(vec4& a, float s) { a.x -= s; a.y -= s; a.z -= s; a.w -= s; return a; }
			inline vec4& operator*=(vec4& a, float s) { a.x *= s; a.y *= s; a.z *= s; a.w *= s; return a; }
			inline vec4& operator/=(vec4& a, float s) { a.x /= s; a.y /= s; a.z /= s; a.w /= s; return a; }

			inline float min(float a, float b) { return (b < a ? b : a); }
			inline int min(int a, int b) { return (b < a ? b : a); }
			inline float max(float a, float b) { return (a < b ? b : a); }
			inline int max(int a, int b) { return (a < b ? b : a); }
			inline float clamp(float v, float lo, float hi) { return min(max(v, lo), hi); }
			inline int clamp(int v, int lo, int hi) { return min(max(v, lo), hi); }
			inline float mix(float a, float b, float t) { return a + (b - a) * t; }
			inline float floor(float v) { return std::floor(v); }
			inline float ceil(float v) { return std::ceil(v); }
			inline float abs(float v) { return std::abs(v); }
			inline float sqrt(float v) { return std::sqrt(v); }
			inline float sin(float v) { return std::sin(v); }
			inline float cos(float v) { return std::cos(v); }

			inline vec2 min(const vec2& a, const vec2& b) { return vec2(min(a.x, b.x), min(a.y, b.y)); }
			inline vec3 min(const vec3& a, const vec3& b) { return vec3(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z)); }
			inline vec4 min(const vec4& a, const vec4& b) { return vec4(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z), min(a.w, b.w)); }
			inline vec2 min(const vec2& a, float s) { return vec2(min(a.x, s), min(a.y, s)); }
			inline vec3 min(const vec3& a, float s) { return vec3(min(a.x, s), min(a.y, s), min(a.z, s)); }
			inline vec4 min(const vec4& a, float s) { return vec4(min(a.x, s), min(a.y, s), min(a.z, s), min(a.w, s)); }
			inline vec2 max(const vec2& a, const vec2& b) { return vec2(max(a.x, b.x), max(a.y, b.y)); }
			inline vec3 max(const vec3& a, const vec3& b) { return vec3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z)); }
			inline vec4 max(const vec4& a, const vec4& b) { return vec4(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z), max(a.w, b.w)); }
			inline vec2 max(const vec2& a, float s) { return vec2(max(a.x, s), max(a.y, s)); }
			inline vec3 max(const vec3& a, float s) { return vec3(max(a.x, s), max(a.y, s), max(a.z, s)); }
			inline vec4 max(const vec4& a, float s) { return vec4(max(a.x, s), max(a.y, s), max(a.z, s), max(a.w, s)); }
			inline vec2 clamp(const vec2& v, const vec2& lo, const vec2& hi) { return min(max(v, lo), hi); }
			inline vec3 clamp(const vec3& v, const vec3& lo, const vec3& hi) { return min(max(v, lo), hi); }
			inline vec4 clamp(const vec4& v, const vec4& lo, const vec4& hi) { return min(max(v, lo), hi); }
			inline vec2 clamp(const vec2& v, float lo, float hi) { return min(max(v, lo), hi); }
			inline vec3 clamp(const vec3& v, float lo, float hi) { return min(max(v, lo), hi); }
			inline vec4 clamp(const vec4& v, float lo, float hi) { return min(max(v, lo), hi); }
			inline vec2 mix(const vec2& a, const vec2& b, float t) { return vec2(mix(a.x, b.x, t), mix(a.y, b.y, t)); }
			inline vec3 mix(const vec3& a, const vec3& b, float t) { return vec3(mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t)); }
			inline vec4 mix(const vec4& a, const vec4& b, float t) { return vec4(mix(a.x, b.x, t), mix(a.y, b.y, t), mix(a.z, b.z, t), mix(a.w, b.w, t)); }
			inline vec2 mix(const vec2& a, const vec2& b, const vec2& t) { return vec2(mix(a.x, b.x, t.x), mix(a.y, b.y, t.y)); }
			inline vec3 mix(const vec3& a, const vec3& b, const vec3& t) { return vec3(mix(a.x, b.x, t.x), mix(a.y, b.y, t.y), mix(a.z, b.z, t.z)); }
			inline vec4 mix(const vec4& a, const vec4& b, const vec4& t) { return vec4(mix(a.x, b.x, t.x), mix(a.y, b.y, t.y), mix(a.z, b.z, t.z), mix(a.w, b.w, t.w)); }
			inline vec2 floor(const vec2& v) { return vec2(floor(v.x), floor(v.y)); }
			inline vec3 floor(const vec3& v) { return vec3(floor(v.x), floor(v.y), floor(v.z)); }
			inline vec4 floor(const vec4& v) { return vec4(floor(v.x), floor(v.y), floor(v.z), floor(v.w)); }
			inline vec2 ceil(const vec2& v) { return vec2(ceil(v.x), ceil(v.y)); }
			inline vec3 ceil(const vec3& v) { return vec3(ceil(v.x), ceil(v.y), ceil(v.z)); }
			inline vec4 ceil(const vec4& v) { return vec4(ceil(v.x), ceil(v.y), ceil(v.z), ceil(v.w)); }

			// Pass-field stores (FixedFunctionPass keeps plain float arrays so the runtime side
			// carries no vector types) and the instance-colour load of the COLOR built-in
			inline void Store(float* dst, const vec2& v) { dst[0] = v.x; dst[1] = v.y; }
			inline void Store(float* dst, const vec3& v) { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; }
			inline void Store(float* dst, const vec4& v) { dst[0] = v.x; dst[1] = v.y; dst[2] = v.z; dst[3] = v.w; }
			inline vec4 LoadColor(const float* c) { return vec4(c[0], c[1], c[2], c[3]); }

			// Extended-vocabulary bridges (backend-specific blocks only): resolved-uniform loads and
			// the strip-builder vertex setters, spelled over the EffectContext's scalar methods so the
			// contract in FixedFunctionPass.h stays free of these vector types
			inline float UniformFloat(EffectContext& ctx, const char* name) { float v = 0.0f; ctx.LoadUniform(name, &v, 1); return v; }
			inline vec2 UniformVec2(EffectContext& ctx, const char* name) { float v[2] = { 0.0f, 0.0f }; ctx.LoadUniform(name, v, 2); return vec2(v[0], v[1]); }
			inline vec4 UniformVec4(EffectContext& ctx, const char* name) { float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; ctx.LoadUniform(name, v, 4); return vec4(v[0], v[1], v[2], v[3]); }
			inline void StripPosition(EffectContext& ctx, int i, const vec2& p) { ctx.SetStripVertexPosition(i, p.x, p.y); }
			inline void StripUv(EffectContext& ctx, int i, const vec2& uv) { ctx.SetStripVertexUv(i, uv.x, uv.y); }
			inline void StripColor(EffectContext& ctx, int i, const vec4& c) { ctx.SetStripVertexColor(i, c.x, c.y, c.z, c.w); }
		}
)SUPPORT"};
	}
}
