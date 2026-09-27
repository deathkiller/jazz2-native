#include "Msl.h"
#include "GlslTypedAst.h"
#include "ShaderParser.h"
#include "ConstFold.h"

#include <map>
#include <memory>
#include <set>

#include <Base/Format.h>
#include <Containers/GrowableArray.h>
#include <Containers/SmallVector.h>
#include <Containers/StringConcatenable.h>

using namespace Death::Containers::Literals;

namespace ShaderCompiler
{
	namespace
	{
		// The translated-subset type system (Ty/TyRef), the declaration records, the statement model and the
		// front-end Parser live in the shared GlslTypedAst.h; only the MSL spelling and the emitter are here.

		// MSL spelling of a subset type
		String MslType(const TyRef& t)
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
				case Ty::Sampler2D: return "texture2d<float>"_s;
				case Ty::Sampler3D: return "texture3d<float>"_s;
				case Ty::SamplerCube: return "texturecube<float>"_s;
				case Ty::Struct: return t.S;
				default: return "float"_s;
			}
		}

		/** The typed-AST type of a reflected uniform type (the reflection keeps its own enum) */
		Ty TyFromGlslType(GlslType t)
		{
			switch (t) {
				case GlslType::Float: return Ty::Float;
				case GlslType::Int: return Ty::Int;
				case GlslType::UInt: return Ty::UInt;
				case GlslType::Bool: return Ty::Bool;
				case GlslType::Vec2: return Ty::Vec2;
				case GlslType::Vec3: return Ty::Vec3;
				case GlslType::Vec4: return Ty::Vec4;
				case GlslType::IVec2: return Ty::IVec2;
				case GlslType::IVec3: return Ty::IVec3;
				case GlslType::IVec4: return Ty::IVec4;
				case GlslType::UVec2: return Ty::UVec2;
				case GlslType::UVec3: return Ty::UVec3;
				case GlslType::UVec4: return Ty::UVec4;
				case GlslType::BVec2: return Ty::BVec2;
				case GlslType::BVec3: return Ty::BVec3;
				case GlslType::BVec4: return Ty::BVec4;
				case GlslType::Mat2: return Ty::Mat2;
				case GlslType::Mat3: return Ty::Mat3;
				case GlslType::Mat4: return Ty::Mat4;
				case GlslType::Sampler2D: return Ty::Sampler2D;
				case GlslType::Sampler3D: return Ty::Sampler3D;
				case GlslType::SamplerCube: return Ty::SamplerCube;
				case GlslType::Struct: return Ty::Struct;
			}
			return Ty::Unknown;
		}

		// GLSL built-in functions that keep their name in MSL (a few are remapped separately)
		bool IsPassthroughBuiltin(StringView name)
		{
			static const char* const kNames[] = {
				"sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
				"pow", "exp", "log", "exp2", "log2", "sqrt", "abs", "sign",
				"floor", "ceil", "round", "trunc", "min", "max", "clamp", "mix", "fract",
				"step", "smoothstep", "dot", "cross", "length", "distance",
				"normalize", "reflect", "refract", "fwidth", "transpose", "determinant",
				"saturate", "isnan", "isinf", "any", "all", "fma", "modf"
			};
			for (const char* n : kNames) {
				if (name == n) {
					return true;
				}
			}
			return false;
		}

		// An MSL / C++ keyword or an intrinsic name a user identifier must not shadow (a local named "mix"
		// would break the emitted mix() call). User locals/params/functions matching one are suffixed with '_'
		// (consistently at declaration and use).
		bool IsReservedIdent(StringView name)
		{
			static const char* const kReserved[] = {
				// C++ keywords MSL inherits
				"alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "case", "catch",
				"char", "char16_t", "char32_t", "class", "compl", "constexpr", "const_cast", "continue",
				"decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit",
				"export", "extern", "friend", "goto", "inline", "long", "mutable", "namespace", "new",
				"noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected",
				"public", "register", "reinterpret_cast", "short", "signed", "sizeof", "static",
				"static_assert", "static_cast", "switch", "template", "this", "thread_local", "throw", "try",
				"typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "volatile",
				"wchar_t", "xor", "xor_eq", "override", "final",
				// MSL keywords and address spaces
				"kernel", "vertex", "fragment", "constant", "device", "thread", "threadgroup", "half",
				"uchar", "ushort", "ulong", "size_t", "ptrdiff_t", "sampler", "texture", "metal",
				// intrinsic names a same-named variable would shadow, breaking our emitted calls
				"mix", "fract", "select", "rsqrt", "dfdx", "dfdy", "fwidth", "saturate", "atan2", "fmod",
				"dot", "cross", "length", "distance", "normalize", "reflect", "refract", "transpose",
				"determinant", "min", "max", "clamp", "abs", "floor", "ceil", "round", "trunc", "sign",
				"step", "smoothstep", "sqrt", "pow", "exp", "log", "exp2", "log2", "sin", "cos", "tan",
				"any", "all", "level", "bias", "gradient2d", "read", "sample", "write", "main"
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

		std::uint32_t RoundUp(std::uint32_t value, std::uint32_t alignment)
		{
			return (alignment == 0) ? value : ((value + alignment - 1) / alignment) * alignment;
		}

		// std140 base alignment and (unpadded) size of a scalar/vector/matrix type (the same rules
		// GlslReflect applies, repeated here for the gathered "_Globals" block whose members are loose)
		void Std140BaseLayout(Ty t, std::uint32_t& align, std::uint32_t& size)
		{
			switch (t) {
				case Ty::Float: case Ty::Int: case Ty::UInt: case Ty::Bool: align = 4; size = 4; break;
				case Ty::Vec2: case Ty::IVec2: case Ty::UVec2: case Ty::BVec2: align = 8; size = 8; break;
				case Ty::Vec3: case Ty::IVec3: case Ty::UVec3: case Ty::BVec3: align = 16; size = 12; break;
				case Ty::Vec4: case Ty::IVec4: case Ty::UVec4: case Ty::BVec4: align = 16; size = 16; break;
				case Ty::Mat2: align = 16; size = 32; break;
				case Ty::Mat3: align = 16; size = 48; break;
				case Ty::Mat4: align = 16; size = 64; break;
				default: align = 16; size = 16; break;
			}
		}

		// --- Emitter ---------------------------------------------------------------------------------

		/**
			What a function reaches for beyond its own parameters and locals - the entry-point arguments and
			locals MSL cannot let it see implicitly. Computed for every helper (and the entry point) in a first
			pass, closed over the call graph, then turned into the trailing parameters of each helper and the
			matching arguments of each call.
		*/
		struct FnNeeds
		{
			bool Globals = false;						// any loose uniform (the "_Globals" block)
			std::set<String> Blocks;					// std140 blocks, by block name
			std::set<String> Samplers;					// samplers, by uniform name
			bool VertexInput = false;					// a vertex attribute (VS "_vin")
			bool Io = false;							// a varying / gl_Position (VS "_out") or a varying / gl_FragCoord (FS "_in")
			std::set<String> Outputs;					// fragment outputs, by name
			std::set<String> MutableGlobals;			// non-const file-scope globals (hoisted into the entry point)
			bool VertexId = false;
			bool InstanceId = false;
			std::set<String> Callees;					// user functions called

			/** Unions a callee's needs into this one; returns true when anything was added */
			bool Absorb(const FnNeeds& other)
			{
				bool changed = false;
				auto setBool = [&changed](bool& dst, bool src) { if (src && !dst) { dst = true; changed = true; } };
				auto setSet = [&changed](std::set<String>& dst, const std::set<String>& src) {
					for (const String& s : src) { if (dst.insert(s).second) changed = true; }
				};
				setBool(Globals, other.Globals);
				setSet(Blocks, other.Blocks);
				setSet(Samplers, other.Samplers);
				setBool(VertexInput, other.VertexInput);
				setBool(Io, other.Io);
				setSet(Outputs, other.Outputs);
				setSet(MutableGlobals, other.MutableGlobals);
				setBool(VertexId, other.VertexId);
				setBool(InstanceId, other.InstanceId);
				return changed;
			}
		};

		/** One member of an aggregate whose MSL layout has to reproduce a std140 one */
		struct LayoutMember
		{
			String Name;
			TyRef Type;
			std::int32_t ArraySize = 0;			// 0 = not an array
			bool SymbolicArray = false;			// "[BATCH_SIZE]"
			std::uint32_t Offset = 0;			// std140 offset within the aggregate
			std::uint32_t ArrayStride = 0;		// std140 element stride (arrays only)
		};

		class Emitter
		{
		public:
			Emitter(Parser& parser, bool vertexStage, const StageReflection& reflection)
				: _p(parser), _vertexStage(vertexStage), _reflection(reflection)
			{
				for (const StructDecl& s : _p.Structs) _structByName[s.Name] = &s;
				for (const BlockDecl& b : _p.Blocks) {
					if (!b.Instance.empty()) _blockOfInstance[b.Instance] = b.Name;
					for (const Field& m : b.Members) {
						_memberType[m.Name] = m.Type;
						_blockOfMember[m.Name] = b.Name;
						if (m.SymbolicArray) _hasSymbolicArray = true;
					}
				}
				for (const UniformDecl& u : _p.Uniforms) _uniformByName[u.Name] = u.Type;
				for (const SamplerDecl& s : _p.Samplers) _samplerType[s.Name] = s.Type;
				for (const VaryingDecl& v : _p.Varyings) _varyingType[v.Name] = v.Type;
				for (const AttributeDecl& a : _p.Attributes) _attributeType[a.Name] = a.Type;
				for (const GlobalVarDecl& g : _p.GlobalVars) {
					_globalVarType[g.Name] = g.Type;
					if (!g.IsConst) _mutableGlobals.insert(g.Name);
				}
				for (const Function& f : _p.Functions) if (f.Name != "main") _funcRet[f.Name] = f.RetType;
				for (const Parser::FragOutputDecl& o : _p.FragOutputs) _outputType[o.Name] = o.Type;
			}

			bool Ok() const { return _ok; }
			const String& Reason() const { return _reason; }

			String Emit()
			{
				const Function* main = nullptr;
				for (const Function& fn : _p.Functions) if (fn.Name == "main") { main = &fn; break; }
				if (main == nullptr) { Fail("no main() function found"_s); return {}; }
				if (!_vertexStage && _p.FragOutputs.empty()) { Fail("fragment shader has no color output"_s); return {}; }

				// A mutable global of struct or array type cannot be hoisted the simple way; none exists today
				for (const GlobalVarDecl& g : _p.GlobalVars) {
					if (!g.IsConst && g.Type.T == Ty::Struct) { Fail("a non-const struct-typed global variable is unsupported ('"_s + g.Name + "')"_s); return {}; }
				}

				// -- Pass 1: what every function reaches for. Emitted into a scratch string with call-site
				// extras suppressed, then closed over the call graph so a helper carries what its callees need.
				_collect = true;
				for (const Function& fn : _p.Functions) {
					_cur = &_needs[fn.Name];
					String scratch;
					if (fn.Name == "main") {
						_inEntryMain = true;
						EmitBodyOf(fn, scratch);
						_inEntryMain = false;
					} else {
						EmitBodyOf(fn, scratch);
					}
					if (!_ok) return {};
				}
				_collect = false;
				_cur = nullptr;
				for (bool changed = true; changed; ) {
					changed = false;
					for (auto& kv : _needs) {
						for (const String& callee : kv.second.Callees) {
							auto it = _needs.find(callee);
							if (it != _needs.end() && &it->second != &kv.second && kv.second.Absorb(it->second)) {
								changed = true;
							}
						}
					}
				}

				// -- Pass 2: the real emission. Helpers first (their signatures now carry the closed needs),
				// then the entry point.
				String helpersOut;
				for (const Function& fn : _p.Functions) {
					if (fn.Name == "main") continue;
					_cur = &_needs[fn.Name];
					helpersOut += EmitHelper(fn);
					helpersOut += "\n"_s;
				}
				_cur = &_needs["main"];
				String entryOut = EmitEntry(*main);
				if (!_ok) return {};

				String out;
				out += "// Generated MSL (Metal, macOS/iOS) by ShaderCompiler. Do not edit manually.\n"_s;
				out += "#include <metal_stdlib>\n"_s;
				out += "using namespace metal;\n\n"_s;
				if (_hasSymbolicArray) {
					out += "#define BATCH_SIZE "_s + Death::format("{}", BatchSize()) + "\n\n"_s;
				}

				// User structs (declared before the blocks that use them), laid out to their std140 reflection
				for (const StructDecl& s : _p.Structs) {
					out += EmitStructDecl(s);
					if (!_ok) return {};
				}

				// The gathered "_Globals" block of the loose uniforms: members + std140 layout from the merged
				// reflection, so both stages (and the backend, which copies the values in) agree on one layout
				if (!_p.Uniforms.empty()) {
					out += EmitGlobalsDecl();
					if (!_ok) return {};
				}

				// std140 uniform blocks, each checked against the reflected offsets
				for (const BlockDecl& b : _p.Blocks) {
					out += EmitBlockDecl(b);
					if (!_ok) return {};
				}

				// File-scope const globals (bayer matrices, colour-space matrices, ...) are `constant` data
				for (const GlobalVarDecl& g : _p.GlobalVars) {
					if (!g.IsConst) continue;
					_locals.clear();
					_arrayVars.clear();
					out += "constant "_s + MslType(g.Type) + " "_s + SanitizeIdent(g.Name);
					if (g.Init != nullptr) out += " = "_s + EmitExpr(g.Init.get(), 0);
					out += ";\n"_s;
				}
				if (HasConstGlobals()) out += "\n"_s;

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

			std::map<String, const StructDecl*> _structByName;
			std::map<String, String> _blockOfInstance;		// block instance name -> block name
			std::map<String, String> _blockOfMember;		// block member -> owning block name
			std::map<String, TyRef> _memberType;			// block member -> type (element type for arrays)
			std::map<String, TyRef> _uniformByName;
			std::map<String, Ty> _samplerType;
			std::map<String, TyRef> _varyingType;
			std::map<String, TyRef> _attributeType;
			std::map<String, TyRef> _globalVarType;
			std::set<String> _mutableGlobals;
			std::map<String, TyRef> _funcRet;
			std::map<String, TyRef> _outputType;
			std::map<String, TyRef> _locals;				// current-function locals + params (for type inference)
			std::set<String> _arrayVars;					// locals/params that are arrays (Index yields the element type, not a swizzle)

			// Natural MSL layout of every emitted struct (after its std140 padding), for members of that type
			struct NaturalLayout { std::uint32_t Align; std::uint32_t Size; };
			std::map<String, NaturalLayout> _structNatural;

			std::map<String, FnNeeds> _needs;
			FnNeeds* _cur = nullptr;			// needs of the function being emitted
			bool _collect = false;				// pass 1: record needs, suppress call-site extras
			bool _inEntryMain = false;
			std::int32_t _padCounter = 0;

			void Fail(String why) { if (_ok) { _ok = false; _reason = std::move(why); } }

			bool HasConstGlobals() const
			{
				for (const GlobalVarDecl& g : _p.GlobalVars) if (g.IsConst) return true;
				return false;
			}

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

			// --- Binding scheme (mirrors what the Metal backend reconstructs from the same reflection) --

			std::int32_t UboBase() const { return (_reflection.Uniforms.empty() ? 0 : 1); }

			// buffer (uboBase + i) for the i-th std140 block in reflection order
			std::int32_t BlockBufferIndex(StringView name) const
			{
				for (std::size_t i = 0; i < _reflection.Blocks.size(); i++) {
					if (_reflection.Blocks[i].Name == name) return UboBase() + static_cast<std::int32_t>(i);
				}
				return UboBase();
			}

			// texture/sampler index j for the j-th sampler in reflection order
			std::int32_t SamplerIndex(StringView name) const
			{
				for (std::size_t j = 0; j < _reflection.Textures.size(); j++) {
					if (_reflection.Textures[j].Name == name) return static_cast<std::int32_t>(j);
				}
				return 0;
			}

			std::int32_t AttributeLocation(std::size_t declIndex) const
			{
				const AttributeDecl& a = _p.Attributes[declIndex];
				for (const AttributeInfo& ra : _reflection.Attributes) {
					if (ra.Name == a.Name && ra.Location >= 0) return ra.Location;
				}
				if (a.Location >= 0) return a.Location;
				return static_cast<std::int32_t>(declIndex);
			}

			static String BlockVar(StringView blockName) { return "_b"_s + blockName; }
			static String SamplerVar(StringView name) { return String{name} + "_smplr"_s; }

			// --- std140-faithful aggregates -------------------------------------------------------------

			/** Natural MSL alignment/size of a member type; false when MSL has no layout-compatible spelling */
			bool NaturalLayoutOf(const TyRef& t, bool packedVec3, std::uint32_t& align, std::uint32_t& size, String& why)
			{
				switch (t.T) {
					case Ty::Float: case Ty::Int: case Ty::UInt: align = 4; size = 4; return true;
					case Ty::Vec2: case Ty::IVec2: case Ty::UVec2: align = 8; size = 8; return true;
					case Ty::Vec3: case Ty::IVec3: case Ty::UVec3:
						if (packedVec3) { align = 4; size = 12; } else { align = 16; size = 16; }
						return true;
					case Ty::Vec4: case Ty::IVec4: case Ty::UVec4: align = 16; size = 16; return true;
					case Ty::Mat3: align = 16; size = 48; return true;
					case Ty::Mat4: align = 16; size = 64; return true;
					case Ty::Mat2:
						why = "a mat2 uniform (std140 stores it as two vec4 columns, which MSL's float2x2 does not)"_s;
						return false;
					case Ty::Bool: case Ty::BVec2: case Ty::BVec3: case Ty::BVec4:
						why = "a bool uniform (4 bytes in std140, 1 byte in MSL)"_s;
						return false;
					case Ty::Struct: {
						auto it = _structNatural.find(t.S);
						if (it == _structNatural.end()) { why = "struct '"_s + t.S + "' used before its layout was emitted"_s; return false; }
						align = it->second.Align;
						size = it->second.Size;
						return true;
					}
					default:
						why = "an unsupported uniform type"_s;
						return false;
				}
			}

			/**
				Emits `struct <name> { ... };` whose MSL layout reproduces the std140 one described by @p members
				(explicit padding where MSL would place a member earlier, `packed_float3` where a later member sits
				in a vec3's tail). @p std140Size pads the tail (0 = leave it, for a block never placed in an array).
				Records the natural layout under @p name so a later aggregate can embed it.
			*/
			String EmitAggregate(StringView name, const SmallVectorImpl<LayoutMember>& members, std::uint32_t std140Size)
			{
				String out;
				out += "struct "_s + name + "\n{\n"_s;
				std::uint32_t running = 0;
				std::uint32_t maxAlign = 1;
				for (std::size_t i = 0; i < members.size(); i++) {
					const LayoutMember& m = members[i];
					// Where must the member land, and does MSL let it? A vec3 whose successor starts inside its
					// tail padding (offset < +16) has to be packed; anything else keeps its natural spelling.
					bool packed = false;
					if ((m.Type.T == Ty::Vec3 || m.Type.T == Ty::IVec3 || m.Type.T == Ty::UVec3) && m.ArraySize == 0 && !m.SymbolicArray) {
						const std::uint32_t nextOffset = (i + 1 < members.size() ? members[i + 1].Offset : (std140Size != 0 ? std140Size : m.Offset + 16));
						if (nextOffset < m.Offset + 16) packed = true;
					}
					std::uint32_t align = 0, size = 0;
					String why;
					if (!NaturalLayoutOf(m.Type, packed, align, size, why)) {
						Fail("cannot lay out '"_s + name + "."_s + m.Name + "' in MSL: "_s + why);
						return {};
					}
					const bool isArray = (m.ArraySize > 0 || m.SymbolicArray);
					if (isArray) {
						// An array's natural stride (size rounded to alignment) must be the std140 one, which rounds
						// the element to 16 bytes - equal for vec4/mat/struct elements, not for scalars or vec2
						const std::uint32_t naturalStride = RoundUp(size, align);
						if (m.ArrayStride != 0 && naturalStride != m.ArrayStride) {
							Fail("cannot lay out '"_s + name + "."_s + m.Name + "' in MSL: its std140 array stride is "_s +
								Death::format("{}", m.ArrayStride) + " bytes but MSL's natural stride is "_s + Death::format("{}", naturalStride));
							return {};
						}
					}
					const std::uint32_t natural = RoundUp(running, align);
					if (natural > m.Offset) {
						Fail("cannot lay out '"_s + name + "."_s + m.Name + "' in MSL: std140 places it at "_s + Death::format("{}", m.Offset) +
							" but MSL cannot place it before "_s + Death::format("{}", natural));
						return {};
					}
					if (natural < m.Offset) {
						// MSL would put it earlier: hold it back with explicit bytes (char has alignment 1)
						out += "\tchar _pad"_s + Death::format("{}", _padCounter++) + "["_s + Death::format("{}", m.Offset - running) + "];\n"_s;
						running = m.Offset;
					} else {
						running = natural;
					}
					String typeName = MslType(m.Type);
					if (packed) {
						typeName = (m.Type.T == Ty::Vec3 ? String("packed_float3"_s) : (m.Type.T == Ty::IVec3 ? String("packed_int3"_s) : String("packed_uint3"_s)));
					}
					out += "\t"_s + typeName + " "_s + m.Name;
					if (m.SymbolicArray) out += "[BATCH_SIZE]"_s;
					else if (m.ArraySize > 0) out += "["_s + Death::format("{}", m.ArraySize) + "]"_s;
					out += ";\n"_s;
					if (m.SymbolicArray) {
						running += RoundUp(size, align);		// a symbolic array is always last; its extent is open-ended
					} else if (m.ArraySize > 0) {
						running += RoundUp(size, align) * std::uint32_t(m.ArraySize);
					} else {
						running += size;
					}
					if (align > maxAlign) maxAlign = align;
				}
				if (std140Size != 0) {
					const std::uint32_t naturalSize = RoundUp(running, maxAlign);
					if (naturalSize < std140Size) {
						out += "\tchar _pad"_s + Death::format("{}", _padCounter++) + "["_s + Death::format("{}", std140Size - running) + "];\n"_s;
						running = std140Size;
					} else if (naturalSize > std140Size) {
						Fail("cannot lay out '"_s + name + "' in MSL: std140 size "_s + Death::format("{}", std140Size) +
							" is smaller than MSL's natural size "_s + Death::format("{}", naturalSize));
						return {};
					}
					_structNatural[String{name}] = { maxAlign, std140Size };
				} else {
					_structNatural[String{name}] = { maxAlign, RoundUp(running, maxAlign) };
				}
				out += "};\n\n"_s;
				return out;
			}

			String EmitStructDecl(const StructDecl& s)
			{
				const StructInfo* info = nullptr;
				for (const StructInfo& si : _reflection.Structs) if (si.Name == s.Name) { info = &si; break; }
				if (info == nullptr) {
					// A struct the reflection does not know (never used by a uniform block): an ordinary MSL struct
					String out = "struct "_s + s.Name + "\n{\n"_s;
					for (const Field& f : s.Fields) {
						out += "\t"_s + MslType(f.Type) + " "_s + f.Name;
						if (f.ArraySize > 0) out += "["_s + Death::format("{}", f.ArraySize) + "]"_s;
						out += ";\n"_s;
					}
					out += "};\n\n"_s;
					return out;
				}
				SmallVector<LayoutMember, 0> members;
				for (const Field& f : s.Fields) {
					const MemberInfo* mi = nullptr;
					for (const MemberInfo& m : info->Fields) if (m.Name == f.Name) { mi = &m; break; }
					if (mi == nullptr) { Fail("struct member '"_s + s.Name + "."_s + f.Name + "' has no reflection"_s); return {}; }
					LayoutMember lm;
					lm.Name = f.Name;
					lm.Type = f.Type;
					lm.ArraySize = f.ArraySize;
					lm.SymbolicArray = f.SymbolicArray;
					lm.Offset = mi->Offset;
					lm.ArrayStride = mi->ArrayStride;
					members.push_back(std::move(lm));
				}
				return EmitAggregate(s.Name, members, info->Size);
			}

			String EmitBlockDecl(const BlockDecl& b)
			{
				const BlockInfo* info = nullptr;
				for (const BlockInfo& bi : _reflection.Blocks) if (bi.Name == b.Name) { info = &bi; break; }
				if (info == nullptr) { Fail("uniform block '"_s + b.Name + "' has no reflection"_s); return {}; }
				SmallVector<LayoutMember, 0> members;
				for (const Field& f : b.Members) {
					const MemberInfo* mi = nullptr;
					for (const MemberInfo& m : info->Members) if (m.Name == f.Name) { mi = &m; break; }
					if (mi == nullptr) { Fail("block member '"_s + b.Name + "."_s + f.Name + "' has no reflection"_s); return {}; }
					LayoutMember lm;
					lm.Name = f.Name;
					lm.Type = f.Type;
					lm.ArraySize = f.ArraySize;
					lm.SymbolicArray = f.SymbolicArray;
					lm.Offset = mi->Offset;
					lm.ArrayStride = (f.SymbolicArray ? info->InstanceStride : mi->ArrayStride);
					members.push_back(std::move(lm));
				}
				return EmitAggregate(b.Name, members, 0);
			}

			String EmitGlobalsDecl()
			{
				// The same std140 packing the Vulkan emitter and both backends apply to the loose uniforms
				SmallVector<LayoutMember, 0> members;
				std::uint32_t offset = 0;
				for (const UniformInfo& u : _reflection.Uniforms) {
					LayoutMember lm;
					lm.Name = u.Name;
					lm.Type = { TyFromGlslType(u.Type), u.TypeName };
					lm.ArraySize = static_cast<std::int32_t>(u.ArraySize);
					std::uint32_t baseAlign = 0, baseSize = 0;
					Std140BaseLayout(lm.Type.T, baseAlign, baseSize);
					std::uint32_t align = baseAlign, size = baseSize;
					if (u.ArraySize > 0) {
						align = RoundUp(baseAlign, 16);
						lm.ArrayStride = RoundUp(baseSize, 16);
						size = lm.ArrayStride * u.ArraySize;
					}
					offset = RoundUp(offset, align);
					lm.Offset = offset;
					offset += size;
					members.push_back(std::move(lm));
				}
				return EmitAggregate("_Globals"_s, members, RoundUp(offset, 16));
			}

			// --- I/O structs -------------------------------------------------------------------------------

			// Integer and `flat` varyings must not be interpolated; Metal rejects an interpolated integer
			static bool IsFlat(const VaryingDecl& v)
			{
				Ty base = BaseScalar(v.Type.T);
				return (v.Flat || base == Ty::Int || base == Ty::UInt || base == Ty::Bool);
			}

			bool HasVertexInput() const { return _vertexStage && !_p.Attributes.empty(); }

			String EmitIoStructs()
			{
				String out;
				if (_vertexStage) {
					if (HasVertexInput()) {
						out += "struct VsIn\n{\n"_s;
						for (std::size_t i = 0; i < _p.Attributes.size(); i++) {
							out += "\t"_s + MslType(_p.Attributes[i].Type) + " "_s + _p.Attributes[i].Name +
								" [[attribute("_s + Death::format("{}", AttributeLocation(i)) + ")]];\n"_s;
						}
						out += "};\n\n"_s;
					}
					out += "struct VsOut\n{\n\tfloat4 gl_Position [[position]];\n"_s;
					for (std::size_t i = 0; i < _p.Varyings.size(); i++) {
						const VaryingDecl& v = _p.Varyings[i];
						out += "\t"_s + MslType(v.Type) + " "_s + v.Name + " [[user(locn"_s + Death::format("{}", i) + ")"_s +
							(IsFlat(v) ? ", flat"_s : ""_s) + "]];\n"_s;
					}
					out += "};\n\n"_s;
				} else {
					// The fragment position is only declared when read: an unused [[position]] is harmless, but
					// the struct is left out entirely when nothing at all flows into the stage
					out += "struct FsIn\n{\n"_s;
					out += "\tfloat4 gl_FragCoord [[position]];\n"_s;
					for (std::size_t i = 0; i < _p.Varyings.size(); i++) {
						const VaryingDecl& v = _p.Varyings[i];
						out += "\t"_s + MslType(v.Type) + " "_s + v.Name + " [[user(locn"_s + Death::format("{}", i) + ")"_s +
							(IsFlat(v) ? ", flat"_s : ""_s) + "]];\n"_s;
					}
					out += "};\n\n"_s;
					// Multiple render targets: one [[color(i)]] per fragment output, in declaration order
					if (_p.FragOutputs.size() > 1) {
						out += "struct FsOut\n{\n"_s;
						for (std::size_t i = 0; i < _p.FragOutputs.size(); i++) {
							out += "\t"_s + MslType(_p.FragOutputs[i].Type) + " "_s + _p.FragOutputs[i].Name +
								" [[color("_s + Death::format("{}", i) + ")]];\n"_s;
						}
						out += "};\n\n"_s;
					}
				}
				return out;
			}

			// --- Trailing parameters / arguments carrying the needs -------------------------------------

			/** Parameter declarations for @p n; @p entry decorates them with their binding attributes */
			String ExtraParams(const FnNeeds& n, bool entry, bool& first)
			{
				String out;
				auto sep = [&]() { if (!first) out += ", "_s; first = false; };
				if (n.Globals) {
					sep();
					out += "constant _Globals& _globals"_s;
					if (entry) out += " [[buffer(0)]]"_s;
				}
				for (const BlockDecl& b : _p.Blocks) {
					if (n.Blocks.find(b.Name) == n.Blocks.end()) continue;
					sep();
					out += "constant "_s + b.Name + "& "_s + BlockVar(b.Name);
					if (entry) out += " [[buffer("_s + Death::format("{}", BlockBufferIndex(b.Name)) + ")]]"_s;
				}
				for (const SamplerDecl& s : _p.Samplers) {
					if (n.Samplers.find(s.Name) == n.Samplers.end()) continue;
					sep();
					out += MslType({ s.Type, {} }) + " "_s + s.Name;
					if (entry) out += " [[texture("_s + Death::format("{}", SamplerIndex(s.Name)) + ")]]"_s;
					out += ", sampler "_s + SamplerVar(s.Name);
					if (entry) out += " [[sampler("_s + Death::format("{}", SamplerIndex(s.Name)) + ")]]"_s;
				}
				if (!entry) {
					// The entry point owns these as its stage_in argument / locals; helpers borrow references
					if (_vertexStage) {
						if (n.VertexInput && HasVertexInput()) { sep(); out += "thread VsIn& _vin"_s; }
						if (n.Io) { sep(); out += "thread VsOut& _out"_s; }
						if (n.VertexId) { sep(); out += "int gl_VertexID"_s; }
						if (n.InstanceId) { sep(); out += "int gl_InstanceID"_s; }
					} else {
						if (n.Io) { sep(); out += "thread FsIn& _in"_s; }
						for (const Parser::FragOutputDecl& o : _p.FragOutputs) {
							if (n.Outputs.find(o.Name) == n.Outputs.end()) continue;
							sep();
							out += "thread "_s + MslType(o.Type) + "& "_s + o.Name;
						}
					}
					for (const GlobalVarDecl& g : _p.GlobalVars) {
						if (g.IsConst || n.MutableGlobals.find(g.Name) == n.MutableGlobals.end()) continue;
						sep();
						out += "thread "_s + MslType(g.Type) + "& "_s + SanitizeIdent(g.Name);
					}
				}
				return out;
			}

			/** Arguments a call has to forward for the callee's needs (same spellings exist in every caller) */
			String ExtraArgs(const FnNeeds& n, bool& first)
			{
				String out;
				auto sep = [&]() { if (!first) out += ", "_s; first = false; };
				if (n.Globals) { sep(); out += "_globals"_s; }
				for (const BlockDecl& b : _p.Blocks) {
					if (n.Blocks.find(b.Name) == n.Blocks.end()) continue;
					sep();
					out += BlockVar(b.Name);
				}
				for (const SamplerDecl& s : _p.Samplers) {
					if (n.Samplers.find(s.Name) == n.Samplers.end()) continue;
					sep();
					out += s.Name + ", "_s + SamplerVar(s.Name);
				}
				if (_vertexStage) {
					if (n.VertexInput && HasVertexInput()) { sep(); out += "_vin"_s; }
					if (n.Io) { sep(); out += "_out"_s; }
					if (n.VertexId) { sep(); out += "gl_VertexID"_s; }
					if (n.InstanceId) { sep(); out += "gl_InstanceID"_s; }
				} else {
					if (n.Io) { sep(); out += "_in"_s; }
					for (const Parser::FragOutputDecl& o : _p.FragOutputs) {
						if (n.Outputs.find(o.Name) == n.Outputs.end()) continue;
						sep();
						out += o.Name;
					}
				}
				for (const GlobalVarDecl& g : _p.GlobalVars) {
					if (g.IsConst || n.MutableGlobals.find(g.Name) == n.MutableGlobals.end()) continue;
					sep();
					out += SanitizeIdent(g.Name);
				}
				return out;
			}

			// --- Entry point ------------------------------------------------------------------------------

			String ReturnEpilogue(const String& indent)
			{
				if (!_vertexStage) {
					if (_p.FragOutputs.size() <= 1) {
						return indent + "return "_s + _p.FragOutputs[0].Name + ";\n"_s;
					}
					String mrt;
					mrt += indent + "FsOut _fsOut;\n"_s;
					for (const Parser::FragOutputDecl& o : _p.FragOutputs) {
						mrt += indent + "_fsOut."_s + o.Name + " = "_s + o.Name + ";\n"_s;
					}
					mrt += indent + "return _fsOut;\n"_s;
					return mrt;
				}
				// Clip space needs no conversion: Metal puts y = +1 at the top, as the RHI does (see Msl.h)
				return indent + "return _out;\n"_s;
			}

			/** Emits the body statements of @p fn (used by both passes) */
			void EmitBodyOf(const Function& fn, String& out)
			{
				_locals.clear();
				_arrayVars.clear();
				for (const Param& p : fn.Params) {
					_locals[p.Name] = p.Type;
					if (p.ArraySize > 0) _arrayVars.insert(p.Name);
				}
				EmitBlockInner(fn.Body.get(), "\t"_s, out);
			}

			String EmitEntry(const Function& main)
			{
				const FnNeeds& n = *_cur;
				String body;
				_inEntryMain = true;
				EmitBodyOf(main, body);
				_inEntryMain = false;
				if (!_ok) return {};

				String out;
				bool first = true;
				if (_vertexStage) {
					out += "vertex VsOut "_s + MslEmitter::VertexEntryPoint + "("_s;
					if (HasVertexInput()) { out += "VsIn _vin [[stage_in]]"_s; first = false; }
					if (n.VertexId) { if (!first) out += ", "_s; out += "uint _vid [[vertex_id]]"_s; first = false; }
					if (n.InstanceId) { if (!first) out += ", "_s; out += "uint _iid [[instance_id]]"_s; first = false; }
					out += ExtraParams(n, true, first);
					out += ")\n{\n"_s;
					out += "\tVsOut _out = {};\n"_s;
					if (n.VertexId) out += "\tint gl_VertexID = int(_vid);\n"_s;
					if (n.InstanceId) out += "\tint gl_InstanceID = int(_iid);\n"_s;
				} else {
					const String returnType = (_p.FragOutputs.size() > 1 ? String("FsOut"_s) : MslType(_p.FragOutputs[0].Type));
					out += "fragment "_s + returnType + " "_s + MslEmitter::FragmentEntryPoint + "("_s;
					out += "FsIn _in [[stage_in]]"_s;
					first = false;
					out += ExtraParams(n, true, first);
					out += ")\n{\n"_s;
					// Fragment outputs are locals returned at the end (undefined-until-written in GLSL; zeroed
					// here so an unwritten channel is deterministic rather than a compiler warning)
					for (const Parser::FragOutputDecl& o : _p.FragOutputs) {
						out += "\t"_s + MslType(o.Type) + " "_s + o.Name + " = "_s + MslType(o.Type) + "(0);\n"_s;
					}
				}
				// Mutable file-scope globals become entry-point locals (helpers receive references)
				for (const GlobalVarDecl& g : _p.GlobalVars) {
					if (g.IsConst) continue;
					_locals.clear();
					_arrayVars.clear();
					out += "\t"_s + MslType(g.Type) + " "_s + SanitizeIdent(g.Name);
					if (g.Init != nullptr) out += " = "_s + EmitExpr(g.Init.get(), 0);
					else out += " = "_s + MslType(g.Type) + "(0)"_s;
					out += ";\n"_s;
				}
				out += body;
				out += ReturnEpilogue("\t"_s);
				out += "}\n"_s;
				return out;
			}

			String EmitHelper(const Function& fn)
			{
				const FnNeeds& n = *_cur;
				String out;
				out += MslType(fn.RetType) + " "_s + SanitizeIdent(fn.Name) + "("_s;
				bool first = true;
				for (const Param& pp : fn.Params) {
					if (!first) out += ", "_s;
					first = false;
					if (pp.ArraySize > 0) {
						// An array parameter is a reference to the caller's array (GLSL passes arrays by value for
						// `in`, by reference for `out`/`inout`; every shipped use is read-only or inout, so a
						// reference matches both)
						out += "thread "_s + MslType(pp.Type) + " (&"_s + SanitizeIdent(pp.Name) + ")["_s + Death::format("{}", pp.ArraySize) + "]"_s;
					} else if (!pp.Qualifier.empty()) {
						out += "thread "_s + MslType(pp.Type) + "& "_s + SanitizeIdent(pp.Name);
					} else {
						out += MslType(pp.Type) + " "_s + SanitizeIdent(pp.Name);
					}
				}
				out += ExtraParams(n, false, first);
				out += ")\n{\n"_s;
				EmitBodyOf(fn, out);
				out += "}\n"_s;
				return out;
			}

			// --- Statements -------------------------------------------------------------------------------

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
						out += indent + (s->DeclConst ? String("const ") : String{}) + MslType(s->DeclType) + " "_s + SanitizeIdent(s->DeclName) +
							(s->DeclArraySize > 0 ? String("["_s + Death::format("{}", s->DeclArraySize) + "]"_s) : String{});
						if (s->Init != nullptr) out += " = "_s + EmitExpr(s->Init.get(), 0);
						out += ";\n"_s;
						for (const std::pair<String, ExprPtr>& d : s->ExtraDecls) {
							_locals[d.first] = s->DeclType;
							out += indent + (s->DeclConst ? String("const ") : String{}) + MslType(s->DeclType) + " "_s + SanitizeIdent(d.first);
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
					String r = MslType(s->DeclType) + " "_s + SanitizeIdent(s->DeclName);
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

			// --- Type inference (drives the constructor / comparison / select rewrites) -----------------

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
				if (name == "gl_VertexID" || name == "gl_InstanceID") return { Ty::Int, {} };
				auto oit = _outputType.find(String{name});
				if (oit != _outputType.end()) return oit->second;
				auto sit = _samplerType.find(String{name});
				if (sit != _samplerType.end()) return { sit->second, {} };
				return { Ty::Unknown, {} };
			}

			const Field* FindStructField(StringView structName, StringView field) const
			{
				auto it = _structByName.find(String{structName});
				if (it == _structByName.end()) return nullptr;
				for (const Field& f : it->second->Fields) if (f.Name == field) return &f;
				return nullptr;
			}

			// "instance.member" of a block instance -> the block name (empty otherwise)
			bool IsBlockInstanceMember(const Expr* e, String& outBlock, StringView& outMember) const
			{
				if (e->Kind != ExprKind::Member || e->A == nullptr || e->A->Kind != ExprKind::Ident) return false;
				if (_locals.find(e->A->Text) != _locals.end()) return false;		// a local shadows the instance name
				auto it = _blockOfInstance.find(e->A->Text);
				if (it == _blockOfInstance.end()) return false;
				if (_blockOfMember.find(e->Text) == _blockOfMember.end()) return false;
				outBlock = it->second;
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
						String block;
						StringView member;
						if (IsBlockInstanceMember(e, block, member)) {
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
						Ty bt;
						if (TryBuiltinType(e->Text, bt)) return { bt, {} };
						if (_structByName.find(e->Text) != _structByName.end()) return { Ty::Struct, e->Text };
						if (e->Text == "texture" || e->Text == "textureLod" || e->Text == "texelFetch") return { Ty::Vec4, {} };
						if (e->Text == "textureSize") return { Ty::IVec2, {} };
						if (e->Text == "dot" || e->Text == "length" || e->Text == "distance" || e->Text == "determinant") return { Ty::Float, {} };
						if (e->Text == "cross") return { Ty::Vec3, {} };
						if (e->Text == "any" || e->Text == "all") return { Ty::Bool, {} };
						if (e->Text == "lessThan" || e->Text == "lessThanEqual" || e->Text == "greaterThan" ||
							e->Text == "greaterThanEqual" || e->Text == "equal" || e->Text == "notEqual") {
							if (!e->Args.empty()) return { MakeVec(Ty::Bool, Comps(InferTy(e->Args[0].get()).T)), {} };
							return { Ty::Bool, {} };
						}
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
						if (e->Text == "*") {
							if (IsMatrix(a.T) && IsMatrix(b.T)) return a;
							if (IsMatrix(a.T) && IsVector(b.T)) return b;
							if (IsVector(a.T) && IsMatrix(b.T)) return a;
						}
						return Wider(a, b);
					}
					case ExprKind::Assign: return InferTy(e->A.get());
					case ExprKind::Conditional: return Wider(InferTy(e->B.get()), InferTy(e->C.get()));
				}
				return {};
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
				// Map every component to the xyzw set (MSL has xyzw and rgba but no stpq)
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
				String block;
				StringView member;
				if (IsBlockInstanceMember(e, block, member)) {
					NoteBlock(block);
					return BlockVar(block) + "."_s + member;
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

			/** `vecN(x)` / `matN(x)`: a splat, a conversion, a truncation or a resize, spelled the MSL way */
			String EmitConstructor(Ty ct, const Expr* e)
			{
				const String typeName = MslType({ ct, {} });
				if (e->Args.size() != 1) {
					return typeName + "("_s + EmitArgs(e) + ")"_s;
				}
				const Expr* arg = e->Args[0].get();
				TyRef at = InferTy(arg);
				if (IsVector(ct)) {
					if (IsVector(at.T)) {
						const std::int32_t from = Comps(at.T), to = Comps(ct);
						if (from > to) {
							// GLSL truncates a wider vector; MSL has no such constructor - take the leading components
							const char* sw = (to == 2 ? "xy" : "xyz");
							String inner = EmitExpr(arg, 100) + "."_s + sw;
							if (BaseScalar(at.T) == BaseScalar(ct)) return inner;
							return typeName + "("_s + inner + ")"_s;		// a conversion on top of the truncation
						}
					}
					return typeName + "("_s + EmitExpr(arg, 0) + ")"_s;		// splat, or a same-size conversion
				}
				if (IsMatrix(ct)) {
					if (IsMatrix(at.T) && at.T != ct) {
						String m = EmitExpr(arg, 100);
						if (ct == Ty::Mat3 && at.T == Ty::Mat4) {
							return "float3x3("_s + m + "[0].xyz, "_s + m + "[1].xyz, "_s + m + "[2].xyz)"_s;
						}
						if (ct == Ty::Mat2 && (at.T == Ty::Mat4 || at.T == Ty::Mat3)) {
							return "float2x2("_s + m + "[0].xy, "_s + m + "[1].xy)"_s;
						}
						if (ct == Ty::Mat4 && at.T == Ty::Mat3) {
							return "float4x4(float4("_s + m + "[0], 0.0), float4("_s + m + "[1], 0.0), float4("_s + m + "[2], 0.0), float4(0.0, 0.0, 0.0, 1.0))"_s;
						}
						if (ct == Ty::Mat3 && at.T == Ty::Mat2) {
							return "float3x3(float3("_s + m + "[0], 0.0), float3("_s + m + "[1], 0.0), float3(0.0, 0.0, 1.0))"_s;
						}
					}
					return typeName + "("_s + EmitExpr(arg, 0) + ")"_s;		// diagonal splat or a copy
				}
				// Scalar conversions: float(x), int(x), uint(x), bool(x)
				return typeName + "("_s + EmitExpr(arg, 0) + ")"_s;
			}

			String EmitCall(const Expr* e)
			{
				StringView name = e->Text;

				Ty ct;
				if (TryBuiltinType(name, ct)) {
					return EmitConstructor(ct, e);
				}
				if (_structByName.find(String{name}) != _structByName.end()) {
					// MSL structs have no constructor call syntax; brace-initialize in member order
					return String{name} + "{"_s + EmitArgs(e) + "}"_s;
				}

				if (name == "texture" || name == "textureLod" || name == "texelFetch" || name == "textureSize") {
					if (e->Args.size() < 1) { Fail(String{name} + "() needs a sampler argument"_s); return {}; }
					const Expr* sampler = e->Args[0].get();
					if (sampler->Kind != ExprKind::Ident || _samplerType.find(sampler->Text) == _samplerType.end() ||
						_locals.find(sampler->Text) != _locals.end()) {
						Fail("first argument to "_s + name + "() must be a sampler uniform"_s);
						return {};
					}
					NoteSampler(sampler->Text);
					const String tex = sampler->Text;
					const String smp = SamplerVar(sampler->Text);
					if (name == "textureSize") {
						String lod = (e->Args.size() >= 2 ? EmitExpr(e->Args[1].get(), 0) : String("0"_s));
						return "int2("_s + tex + ".get_width("_s + lod + "), "_s + tex + ".get_height("_s + lod + "))"_s;
					}
					if (e->Args.size() < 2) { Fail(String{name} + "() needs at least (sampler, coordinates)"_s); return {}; }
					String coords = EmitExpr(e->Args[1].get(), 0);
					if (name == "texelFetch") {
						String lod = (e->Args.size() >= 3 ? EmitExpr(e->Args[2].get(), 0) : String("0"_s));
						return tex + ".read(uint2("_s + coords + "), "_s + lod + ")"_s;
					}
					String call = tex + ".sample("_s + smp + ", "_s + coords;
					if (name == "textureLod") {
						if (e->Args.size() < 3) { Fail("textureLod() needs (sampler, uv, lod)"_s); return {}; }
						call += ", level("_s + EmitExpr(e->Args[2].get(), 0) + ")"_s;
					} else if (e->Args.size() >= 3) {
						call += ", bias("_s + EmitExpr(e->Args[2].get(), 0) + ")"_s;
					}
					return call + ")"_s;
				}

				// Remapped built-ins (name differs from GLSL)
				if (name == "inversesqrt") return "rsqrt("_s + EmitArgs(e) + ")"_s;
				if (name == "dFdx") return "dfdx("_s + EmitArgs(e) + ")"_s;
				if (name == "dFdy") return "dfdy("_s + EmitArgs(e) + ")"_s;
				if (name == "atan" && e->Args.size() == 2) return "atan2("_s + EmitArgs(e) + ")"_s;
				if (name == "radians" && e->Args.size() == 1) return "("_s + EmitExpr(e->Args[0].get(), 13) + " * 0.017453292519943295)"_s;
				if (name == "degrees" && e->Args.size() == 1) return "("_s + EmitExpr(e->Args[0].get(), 13) + " * 57.29577951308232)"_s;
				if (name == "mod" && e->Args.size() == 2) {
					// GLSL mod(a,b) = a - b*floor(a/b) (fmod truncates toward zero for negatives)
					String a = EmitExpr(e->Args[0].get(), 0);
					String b = EmitExpr(e->Args[1].get(), 0);
					return "("_s + a + " - ("_s + b + ") * floor(("_s + a + ") / ("_s + b + ")))"_s;
				}
				if (name == "mix" && e->Args.size() == 3 && BaseScalar(InferTy(e->Args[2].get()).T) == Ty::Bool) {
					// A boolean selector picks per component: MSL spells that select(a, b, c) (c ? b : a)
					return "select("_s + EmitArgs(e) + ")"_s;
				}
				if (name == "floatBitsToInt" && e->Args.size() == 1) return "as_type<"_s + MslType({ MakeVec(Ty::Int, Comps(InferTy(e->Args[0].get()).T)), {} }) + ">("_s + EmitArgs(e) + ")"_s;
				if (name == "floatBitsToUint" && e->Args.size() == 1) return "as_type<"_s + MslType({ MakeVec(Ty::UInt, Comps(InferTy(e->Args[0].get()).T)), {} }) + ">("_s + EmitArgs(e) + ")"_s;
				if (name == "intBitsToFloat" && e->Args.size() == 1) return "as_type<"_s + MslType({ MakeVec(Ty::Float, Comps(InferTy(e->Args[0].get()).T)), {} }) + ">("_s + EmitArgs(e) + ")"_s;
				if (name == "uintBitsToFloat" && e->Args.size() == 1) return "as_type<"_s + MslType({ MakeVec(Ty::Float, Comps(InferTy(e->Args[0].get()).T)), {} }) + ">("_s + EmitArgs(e) + ")"_s;
				if (name == "matrixCompMult" && e->Args.size() == 2) {
					Fail("matrixCompMult() has no MSL equivalent in this emitter"_s);
					return {};
				}
				// GLSL component-wise relational built-ins -> MSL operators (each yields a bool vector)
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
				if (it != _funcRet.end()) {
					if (_cur != nullptr) _cur->Callees.insert(String{name});
					String call = SanitizeIdent(name) + "("_s + EmitArgs(e);
					if (!_collect) {
						// Forward whatever the callee (transitively) reaches for
						auto nit = _needs.find(String{name});
						if (nit != _needs.end()) {
							bool first = e->Args.empty();
							call += ExtraArgs(nit->second, first);
						}
					}
					return call + ")"_s;
				}

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
						std::int32_t p = BinPrec(e->Text);
						if (e->Text == "==" || e->Text == "!=") {
							// GLSL's vector equality is one bool over all components; MSL's is component-wise
							TyRef a = InferTy(e->A.get());
							TyRef b = InferTy(e->B.get());
							if (IsVector(a.T) || IsVector(b.T) || IsMatrix(a.T) || IsMatrix(b.T)) {
								return (e->Text == "==" ? "all("_s : "any("_s) + EmitExpr(e->A.get(), p) + " "_s + e->Text + " "_s + EmitExpr(e->B.get(), p + 1) + ")"_s;
							}
						}
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

			// --- Needs bookkeeping ---------------------------------------------------------------------

			void NoteBlock(StringView block) { if (_cur != nullptr) _cur->Blocks.insert(String{block}); }
			void NoteSampler(StringView name) { if (_cur != nullptr) _cur->Samplers.insert(String{name}); }

			String EmitIdent(StringView name)
			{
				// Locals/params shadow everything (and may carry reserved names)
				if (_locals.find(String{name}) != _locals.end()) return SanitizeIdent(name);
				if (name == "gl_VertexID") { if (_cur) _cur->VertexId = true; return "gl_VertexID"_s; }
				if (name == "gl_InstanceID") { if (_cur) _cur->InstanceId = true; return "gl_InstanceID"_s; }
				if (name == "gl_Position") { if (_cur) _cur->Io = true; return "_out.gl_Position"_s; }
				if (name == "gl_FragCoord") { if (_cur) _cur->Io = true; return "_in.gl_FragCoord"_s; }
				if (_uniformByName.find(String{name}) != _uniformByName.end()) {
					if (_cur) _cur->Globals = true;
					return "_globals."_s + name;
				}
				{
					auto it = _blockOfMember.find(String{name});
					if (it != _blockOfMember.end()) {
						NoteBlock(it->second);
						return BlockVar(it->second) + "."_s + name;
					}
				}
				if (_samplerType.find(String{name}) != _samplerType.end()) {
					NoteSampler(name);
					return String{name};
				}
				if (_attributeType.find(String{name}) != _attributeType.end()) {
					if (_cur) _cur->VertexInput = true;
					return "_vin."_s + name;
				}
				if (_varyingType.find(String{name}) != _varyingType.end()) {
					if (_cur) _cur->Io = true;
					return (_vertexStage ? "_out."_s : "_in."_s) + name;
				}
				if (_outputType.find(String{name}) != _outputType.end()) {
					if (_cur) _cur->Outputs.insert(String{name});
					return String{name};
				}
				if (_mutableGlobals.find(String{name}) != _mutableGlobals.end()) {
					if (_cur) _cur->MutableGlobals.insert(String{name});
					return SanitizeIdent(name);
				}
				if (_globalVarType.find(String{name}) != _globalVarType.end()) return SanitizeIdent(name);
				return String{name};
			}
		};
	}

	bool MslEmitter::Transform(StringView modernSource, bool vertexStage, const StageReflection& reflection,
		String& out, Diagnostic& diag)
	{
		SmallVector<GlslToken, 0> tokens;
		String reason;
		if (!TokenizeStage(modernSource, tokens, reason)) {
			diag.Message = std::move(reason);
			diag.Line = 1;
			return false;
		}
		Parser parser(tokens, vertexStage);
		parser.Run();
		if (!parser.Ok()) {
			diag.Message = "MSL emit: "_s + parser.Reason();
			diag.Line = 1;
			return false;
		}
		Emitter emitter(parser, vertexStage, reflection);
		String code = emitter.Emit();
		if (!emitter.Ok()) {
			diag.Message = "MSL emit: "_s + emitter.Reason();
			diag.Line = 1;
			return false;
		}
		out = std::move(code);
		return true;
	}
}
