#include "Hlsl.h"
#include "GlslTypedAst.h"
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
		// The translated-subset type system (Ty/TyRef and its helpers), the declaration records, the
		// statement model (Stmt) and the front-end Parser now live in the shared GlslTypedAst.h, consumed
		// by the HLSL, Vulkan and ESSL 100 emitters alike. Only the HLSL-specific spelling/name helpers
		// and the Emitter below remain here.

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

		bool IsComparisonOrLogical(StringView op)
		{
			return (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=" ||
				op == "&&" || op == "||" || op == "^^");
		}

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
