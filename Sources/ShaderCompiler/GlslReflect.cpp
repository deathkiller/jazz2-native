#include "GlslReflect.h"

namespace ShaderCompiler
{
	namespace
	{
		bool IsDigit(char c)
		{
			return (c >= '0' && c <= '9');
		}

		bool IsIdentStart(char c)
		{
			return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
		}

		bool IsIdentChar(char c)
		{
			return (IsIdentStart(c) || IsDigit(c));
		}

		std::uint32_t RoundUp(std::uint32_t value, std::uint32_t alignment)
		{
			return ((value + alignment - 1) / alignment) * alignment;
		}

		bool Fail(Diagnostic& diag, const std::string& message, std::int32_t line)
		{
			diag.Message = message;
			diag.Line = line;
			return false;
		}

		enum class TokType : std::uint8_t
		{
			Identifier,
			Number,
			Punct,
			End
		};

		struct Tok
		{
			TokType Type = TokType::End;
			std::string Text;
			std::int32_t Line = 0;
		};

		void Tokenize(const std::vector<SourceLine>& lines, std::vector<Tok>& out)
		{
			std::int32_t lastLine = 1;
			for (const SourceLine& line : lines) {
				lastLine = line.Line;
				const std::string& s = line.Text;
				std::size_t i = 0;
				while (i < s.size()) {
					char c = s[i];
					if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
						i++;
						continue;
					}
					Tok t;
					t.Line = line.Line;
					if (IsIdentStart(c)) {
						std::size_t begin = i;
						while (i < s.size() && IsIdentChar(s[i])) {
							i++;
						}
						t.Type = TokType::Identifier;
						t.Text = s.substr(begin, i - begin);
					} else if (IsDigit(c) || (c == '.' && i + 1 < s.size() && IsDigit(s[i + 1]))) {
						// Numeric literal — consume digits, dots and alphanumeric suffixes (1.0f, 0x1F, 1e5)
						std::size_t begin = i;
						while (i < s.size() && (IsIdentChar(s[i]) || s[i] == '.')) {
							i++;
						}
						t.Type = TokType::Number;
						t.Text = s.substr(begin, i - begin);
					} else {
						t.Type = TokType::Punct;
						t.Text = std::string(1, c);
						i++;
					}
					out.push_back(std::move(t));
				}
			}
			Tok end;
			end.Type = TokType::End;
			end.Text.clear();
			end.Line = lastLine;
			out.push_back(std::move(end));
		}

		bool ParseDecimalU32(const std::string& s, std::uint32_t& value)
		{
			if (s.empty()) {
				return false;
			}
			std::uint64_t v = 0;
			for (std::size_t i = 0; i < s.size(); i++) {
				if (!IsDigit(s[i])) {
					return false;
				}
				v = v * 10 + static_cast<std::uint64_t>(s[i] - '0');
				if (v > 0xFFFFFFFFull) {
					return false;
				}
			}
			value = static_cast<std::uint32_t>(v);
			return true;
		}

		struct BuiltinType
		{
			const char* Name;
			GlslType Type;
			std::uint32_t Size;
			std::uint32_t Align;
			bool Sampler;
		};

		const BuiltinType BuiltinTypes[] = {
			{ "float", GlslType::Float, 4, 4, false },
			{ "int", GlslType::Int, 4, 4, false },
			{ "uint", GlslType::UInt, 4, 4, false },
			{ "bool", GlslType::Bool, 4, 4, false },
			{ "vec2", GlslType::Vec2, 8, 8, false },
			{ "vec3", GlslType::Vec3, 12, 16, false },
			{ "vec4", GlslType::Vec4, 16, 16, false },
			{ "ivec2", GlslType::IVec2, 8, 8, false },
			{ "ivec3", GlslType::IVec3, 12, 16, false },
			{ "ivec4", GlslType::IVec4, 16, 16, false },
			{ "uvec2", GlslType::UVec2, 8, 8, false },
			{ "uvec3", GlslType::UVec3, 12, 16, false },
			{ "uvec4", GlslType::UVec4, 16, 16, false },
			{ "bvec2", GlslType::BVec2, 8, 8, false },
			{ "bvec3", GlslType::BVec3, 12, 16, false },
			{ "bvec4", GlslType::BVec4, 16, 16, false },
			// Matrices are laid out as N columns with vec4 stride
			{ "mat2", GlslType::Mat2, 32, 16, false },
			{ "mat3", GlslType::Mat3, 48, 16, false },
			{ "mat4", GlslType::Mat4, 64, 16, false },
			{ "sampler2D", GlslType::Sampler2D, 0, 0, true },
			{ "sampler3D", GlslType::Sampler3D, 0, 0, true },
			{ "samplerCube", GlslType::SamplerCube, 0, 0, true }
		};

		const BuiltinType* FindBuiltin(const std::string& name)
		{
			for (const BuiltinType& b : BuiltinTypes) {
				if (name == b.Name) {
					return &b;
				}
			}
			return nullptr;
		}

		bool IsPrecisionQualifier(const std::string& s)
		{
			return (s == "highp" || s == "mediump" || s == "lowp");
		}

		/** Global-scope declaration parser for one preprocessed stage */
		class Parser
		{
		public:
			Parser(const std::vector<Tok>& tokens, bool vertexStage, StageReflection& result, Diagnostic& diag)
				: _tokens(tokens), _pos(0), _vertexStage(vertexStage), _result(result), _diag(diag)
			{
			}

			bool Run()
			{
				while (Peek().Type != TokType::End) {
					const Tok& t = Peek();
					if (t.Type == TokType::Identifier) {
						if (t.Text == "struct" && Peek(1).Type == TokType::Identifier && Peek(2).Text == "{") {
							if (!ParseStruct()) {
								return false;
							}
							continue;
						}
						if (t.Text == "layout") {
							if (!ParseLayoutDecl()) {
								return false;
							}
							continue;
						}
						if (t.Text == "uniform") {
							Get();
							if (Peek(0).Type == TokType::Identifier && Peek(1).Text == "{") {
								return Fail(_diag, "uniform blocks require an explicit layout(std140) qualifier", Peek(0).Line);
							}
							if (!ParseLooseUniform()) {
								return false;
							}
							continue;
						}
						if (t.Text == "in") {
							if (_vertexStage) {
								Get();
								if (!ParseAttributeDecl(-1)) {
									return false;
								}
							} else if (!SkipStatement()) {
								return false;
							}
							continue;
						}
					}
					if (!SkipStatement()) {
						return false;
					}
				}
				return true;
			}

		private:
			const std::vector<Tok>& _tokens;
			std::size_t _pos;
			bool _vertexStage;
			StageReflection& _result;
			Diagnostic& _diag;

			const Tok& Peek(std::size_t ahead = 0) const
			{
				std::size_t i = _pos + ahead;
				if (i >= _tokens.size()) {
					i = _tokens.size() - 1;	// End token
				}
				return _tokens[i];
			}

			const Tok& Get()
			{
				const Tok& t = _tokens[_pos];
				if (_pos + 1 < _tokens.size()) {
					_pos++;
				}
				return t;
			}

			bool Accept(const char* text)
			{
				if (Peek().Type != TokType::End && Peek().Text == text) {
					Get();
					return true;
				}
				return false;
			}

			bool Expect(const char* text)
			{
				if (!Accept(text)) {
					return Fail(_diag, std::string("expected '") + text + "' but found '" + Peek().Text + "'", Peek().Line);
				}
				return true;
			}

			const StructInfo* FindStruct(const std::string& name) const
			{
				for (const StructInfo& s : _result.Structs) {
					if (s.Name == name) {
						return &s;
					}
				}
				return nullptr;
			}

			bool ResolveType(const Tok& typeTok, GlslType& type, bool& sampler) const
			{
				const BuiltinType* builtin = FindBuiltin(typeTok.Text);
				if (builtin != nullptr) {
					type = builtin->Type;
					sampler = builtin->Sampler;
					return true;
				}
				if (FindStruct(typeTok.Text) != nullptr) {
					type = GlslType::Struct;
					sampler = false;
					return true;
				}
				return Fail(_diag, "unknown type '" + typeTok.Text + "'", typeTok.Line);
			}

			bool TypeLayout(const MemberInfo& m, std::uint32_t& size, std::uint32_t& align, std::int32_t line) const
			{
				if (m.Type == GlslType::Struct) {
					const StructInfo* s = FindStruct(m.TypeName);
					if (s == nullptr) {
						return Fail(_diag, "unknown struct '" + m.TypeName + "'", line);
					}
					size = s->Size;
					align = s->Align;
					return true;
				}
				const BuiltinType* builtin = FindBuiltin(m.TypeName);
				if (builtin == nullptr || builtin->Sampler) {
					return Fail(_diag, "type '" + m.TypeName + "' has no std140 layout", line);
				}
				size = builtin->Size;
				align = builtin->Align;
				return true;
			}

			bool SkipBraces(std::int32_t startLine)
			{
				std::int32_t depth = 1;
				while (depth > 0) {
					if (Peek().Type == TokType::End) {
						return Fail(_diag, "unbalanced braces", startLine);
					}
					const Tok& t = Get();
					if (t.Text == "{") {
						depth++;
					} else if (t.Text == "}") {
						depth--;
					}
				}
				return true;
			}

			bool SkipStatement()
			{
				while (true) {
					if (Peek().Type == TokType::End) {
						return true;
					}
					const Tok t = Get();
					if (t.Text == ";") {
						return true;
					}
					if (t.Text == "{") {
						if (!SkipBraces(t.Line)) {
							return false;
						}
						// A trailing ';' (struct-like declarations) is consumed, a function body ends here
						if (Peek().Text == ";") {
							Get();
						}
						return true;
					}
				}
			}

			bool SkipInitializer()
			{
				std::int32_t depth = 0;
				while (true) {
					const Tok& t = Peek();
					if (t.Type == TokType::End) {
						return Fail(_diag, "unexpected end of file in initializer", t.Line);
					}
					if (depth == 0 && (t.Text == "," || t.Text == ";")) {
						return true;
					}
					if (t.Text == "(" || t.Text == "[") {
						depth++;
					} else if (t.Text == ")" || t.Text == "]") {
						depth--;
					}
					Get();
				}
			}

			bool ParseArraySuffix(MemberInfo& m, bool allowSymbolic)
			{
				Get();	// '['
				const Tok sizeTok = Get();
				if (sizeTok.Type == TokType::Number) {
					std::uint32_t n;
					if (!ParseDecimalU32(sizeTok.Text, n) || n == 0 || n > 65534) {
						return Fail(_diag, "invalid array size '" + sizeTok.Text + "'", sizeTok.Line);
					}
					m.ArraySize = n;
				} else if (sizeTok.Type == TokType::Identifier && sizeTok.Text == "BATCH_SIZE") {
					if (!allowSymbolic) {
						return Fail(_diag, "BATCH_SIZE-sized arrays are only allowed as uniform block members", sizeTok.Line);
					}
					m.SymbolicArray = true;
				} else {
					return Fail(_diag, "unsupported array size '" + sizeTok.Text + "' (expected integer literal or BATCH_SIZE)", sizeTok.Line);
				}
				return Expect("]");
			}

			bool ParseMemberList(std::vector<MemberInfo>& members, bool allowSymbolic)
			{
				while (true) {
					if (Accept("}")) {
						return true;
					}
					if (Peek().Type == TokType::End) {
						return Fail(_diag, "unexpected end of file in member list", Peek().Line);
					}
					while (Peek().Type == TokType::Identifier && IsPrecisionQualifier(Peek().Text)) {
						Get();
					}
					const Tok typeTok = Get();
					if (typeTok.Type != TokType::Identifier) {
						return Fail(_diag, "expected type name but found '" + typeTok.Text + "'", typeTok.Line);
					}
					MemberInfo m;
					bool sampler = false;
					if (!ResolveType(typeTok, m.Type, sampler)) {
						return false;
					}
					if (sampler) {
						return Fail(_diag, "sampler types are not allowed inside structs or uniform blocks", typeTok.Line);
					}
					m.TypeName = typeTok.Text;
					bool typeArray = false;
					if (Peek().Text == "[") {
						if (!ParseArraySuffix(m, allowSymbolic)) {
							return false;
						}
						typeArray = true;
					}
					const Tok nameTok = Get();
					if (nameTok.Type != TokType::Identifier) {
						return Fail(_diag, "expected member name but found '" + nameTok.Text + "'", nameTok.Line);
					}
					m.Name = nameTok.Text;
					if (Peek().Text == "[") {
						if (typeArray) {
							return Fail(_diag, "multi-dimensional arrays are not supported", Peek().Line);
						}
						if (!ParseArraySuffix(m, allowSymbolic)) {
							return false;
						}
					}
					if (!Expect(";")) {
						return false;
					}
					members.push_back(std::move(m));
				}
			}

			bool ComputeStd140(std::vector<MemberInfo>& members, bool allowSymbolic, std::uint32_t& size, std::uint32_t& align, std::uint32_t& instanceStride, std::int32_t line)
			{
				std::uint32_t offset = 0;
				std::uint32_t maxAlign = 16;	// aggregates are aligned to at least 16 in std140
				bool sawSymbolic = false;
				instanceStride = 0;

				for (MemberInfo& m : members) {
					if (sawSymbolic) {
						return Fail(_diag, "members after a BATCH_SIZE-sized array are not supported", line);
					}
					std::uint32_t elemSize, elemAlign;
					if (!TypeLayout(m, elemSize, elemAlign, line)) {
						return false;
					}
					std::uint32_t memberAlign;
					if (m.ArraySize != 0 || m.SymbolicArray) {
						std::uint32_t arrayAlign = RoundUp(elemAlign, 16);
						std::uint32_t stride = RoundUp(elemSize, arrayAlign);
						m.ArrayStride = stride;
						offset = RoundUp(offset, arrayAlign);
						m.Offset = offset;
						if (m.SymbolicArray) {
							if (!allowSymbolic) {
								return Fail(_diag, "BATCH_SIZE-sized arrays are only allowed in uniform blocks", line);
							}
							instanceStride = stride;
							sawSymbolic = true;
							// Symbolic arrays contribute 0 elements to the base size
						} else {
							offset += stride * m.ArraySize;
						}
						memberAlign = arrayAlign;
					} else {
						offset = RoundUp(offset, elemAlign);
						m.Offset = offset;
						offset += elemSize;
						memberAlign = elemAlign;
					}
					if (memberAlign > maxAlign) {
						maxAlign = memberAlign;
					}
				}

				align = maxAlign;
				size = RoundUp(offset, align);
				return true;
			}

			bool ParseStruct()
			{
				const Tok structTok = Get();	// "struct"
				const Tok nameTok = Get();
				if (nameTok.Type != TokType::Identifier) {
					return Fail(_diag, "expected struct name", nameTok.Line);
				}
				if (FindStruct(nameTok.Text) != nullptr) {
					return Fail(_diag, "struct '" + nameTok.Text + "' redefined", nameTok.Line);
				}
				if (!Expect("{")) {
					return false;
				}
				StructInfo info;
				info.Name = nameTok.Text;
				if (!ParseMemberList(info.Fields, false)) {
					return false;
				}
				if (!Expect(";")) {
					return false;
				}
				std::uint32_t unusedStride;
				if (!ComputeStd140(info.Fields, false, info.Size, info.Align, unusedStride, structTok.Line)) {
					return false;
				}
				_result.Structs.push_back(std::move(info));
				return true;
			}

			bool ParseLayoutDecl()
			{
				const Tok layoutTok = Get();	// "layout"
				if (!Expect("(")) {
					return false;
				}
				bool std140 = false;
				std::int32_t location = -1;
				while (true) {
					const Tok qualifier = Get();
					if (qualifier.Type != TokType::Identifier) {
						return Fail(_diag, "expected layout qualifier but found '" + qualifier.Text + "'", qualifier.Line);
					}
					if (qualifier.Text == "std140") {
						std140 = true;
					}
					if (Peek().Text == "=") {
						Get();
						const Tok num = Get();
						if (num.Type != TokType::Number) {
							return Fail(_diag, "expected integer in layout qualifier", num.Line);
						}
						std::uint32_t value;
						if (!ParseDecimalU32(num.Text, value)) {
							return Fail(_diag, "invalid integer '" + num.Text + "' in layout qualifier", num.Line);
						}
						if (qualifier.Text == "location") {
							location = static_cast<std::int32_t>(value);
						}
					}
					if (Accept(",")) {
						continue;
					}
					if (!Expect(")")) {
						return false;
					}
					break;
				}

				if (Peek().Text == "uniform") {
					Get();
					if (Peek(0).Type == TokType::Identifier && Peek(1).Text == "{") {
						if (!std140) {
							return Fail(_diag, "only std140 uniform blocks are supported", layoutTok.Line);
						}
						return ParseBlock();
					}
					// layout-qualified loose uniform
					return ParseLooseUniform();
				}
				if (_vertexStage && Peek().Text == "in") {
					Get();
					return ParseAttributeDecl(location);
				}
				return SkipStatement();
			}

			bool ParseBlock()
			{
				const Tok nameTok = Get();
				if (nameTok.Type != TokType::Identifier) {
					return Fail(_diag, "expected uniform block name", nameTok.Line);
				}
				for (const BlockInfo& b : _result.Blocks) {
					if (b.Name == nameTok.Text) {
						return Fail(_diag, "uniform block '" + nameTok.Text + "' redefined", nameTok.Line);
					}
				}
				if (!Expect("{")) {
					return false;
				}
				BlockInfo block;
				block.Name = nameTok.Text;
				if (!ParseMemberList(block.Members, true)) {
					return false;
				}
				if (Peek().Type == TokType::Identifier) {
					Get();	// optional instance name
				}
				if (!Expect(";")) {
					return false;
				}
				std::uint32_t align;
				if (!ComputeStd140(block.Members, true, block.BaseSize, align, block.InstanceStride, nameTok.Line)) {
					return false;
				}
				_result.Blocks.push_back(std::move(block));
				return true;
			}

			bool ParseLooseUniform()
			{
				while (Peek().Type == TokType::Identifier && IsPrecisionQualifier(Peek().Text)) {
					Get();
				}
				const Tok typeTok = Get();
				if (typeTok.Type != TokType::Identifier) {
					return Fail(_diag, "expected uniform type but found '" + typeTok.Text + "'", typeTok.Line);
				}
				GlslType type;
				bool sampler = false;
				if (!ResolveType(typeTok, type, sampler)) {
					return false;
				}
				if (type == GlslType::Struct) {
					return Fail(_diag, "struct-typed loose uniforms are not supported", typeTok.Line);
				}
				MemberInfo proto;
				proto.Type = type;
				proto.TypeName = typeTok.Text;
				bool typeArray = false;
				if (Peek().Text == "[") {
					if (!ParseArraySuffix(proto, false)) {
						return false;
					}
					typeArray = true;
				}
				while (true) {
					const Tok nameTok = Get();
					if (nameTok.Type != TokType::Identifier) {
						return Fail(_diag, "expected uniform name but found '" + nameTok.Text + "'", nameTok.Line);
					}
					MemberInfo decl = proto;
					if (Peek().Text == "[") {
						if (typeArray) {
							return Fail(_diag, "multi-dimensional arrays are not supported", Peek().Line);
						}
						if (!ParseArraySuffix(decl, false)) {
							return false;
						}
					}
					if (sampler) {
						if (decl.ArraySize != 0) {
							return Fail(_diag, "sampler arrays are not supported", nameTok.Line);
						}
						TextureInfo texture;
						texture.Name = nameTok.Text;
						texture.Type = type;
						texture.Unit = -1;
						_result.Textures.push_back(std::move(texture));
					} else {
						UniformInfo uniform;
						uniform.Name = nameTok.Text;
						uniform.Type = type;
						uniform.TypeName = typeTok.Text;
						uniform.ArraySize = decl.ArraySize;
						_result.Uniforms.push_back(std::move(uniform));
					}
					if (Accept("=")) {
						if (!SkipInitializer()) {
							return false;
						}
					}
					if (Accept(",")) {
						continue;
					}
					return Expect(";");
				}
			}

			bool ParseAttributeDecl(std::int32_t location)
			{
				while (Peek().Type == TokType::Identifier && IsPrecisionQualifier(Peek().Text)) {
					Get();
				}
				const Tok typeTok = Get();
				if (typeTok.Type != TokType::Identifier) {
					return Fail(_diag, "expected attribute type but found '" + typeTok.Text + "'", typeTok.Line);
				}
				GlslType type;
				bool sampler = false;
				if (!ResolveType(typeTok, type, sampler)) {
					return false;
				}
				if (sampler || type == GlslType::Struct) {
					return Fail(_diag, "unsupported vertex attribute type '" + typeTok.Text + "'", typeTok.Line);
				}
				const Tok nameTok = Get();
				if (nameTok.Type != TokType::Identifier) {
					return Fail(_diag, "expected attribute name but found '" + nameTok.Text + "'", nameTok.Line);
				}
				if (Peek().Text == "[") {
					return Fail(_diag, "array vertex attributes are not supported", Peek().Line);
				}
				if (!Expect(";")) {
					return false;
				}
				AttributeInfo attribute;
				attribute.Name = nameTok.Text;
				attribute.Type = type;
				attribute.TypeName = typeTok.Text;
				attribute.Location = location;
				_result.Attributes.push_back(std::move(attribute));
				return true;
			}
		};

		bool StructsEqual(const StructInfo& a, const StructInfo& b)
		{
			if (a.Size != b.Size || a.Align != b.Align || a.Fields.size() != b.Fields.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a.Fields.size(); i++) {
				const MemberInfo& fa = a.Fields[i];
				const MemberInfo& fb = b.Fields[i];
				if (fa.Name != fb.Name || fa.TypeName != fb.TypeName || fa.ArraySize != fb.ArraySize ||
					fa.SymbolicArray != fb.SymbolicArray || fa.Offset != fb.Offset) {
					return false;
				}
			}
			return true;
		}

		bool BlocksEqual(const BlockInfo& a, const BlockInfo& b)
		{
			if (a.BaseSize != b.BaseSize || a.InstanceStride != b.InstanceStride || a.Members.size() != b.Members.size()) {
				return false;
			}
			for (std::size_t i = 0; i < a.Members.size(); i++) {
				const MemberInfo& ma = a.Members[i];
				const MemberInfo& mb = b.Members[i];
				if (ma.Name != mb.Name || ma.TypeName != mb.TypeName || ma.ArraySize != mb.ArraySize ||
					ma.SymbolicArray != mb.SymbolicArray || ma.Offset != mb.Offset) {
					return false;
				}
			}
			return true;
		}
	}

	bool GlslReflector::ReflectStage(const std::vector<SourceLine>& lines, bool vertexStage, StageReflection& result, Diagnostic& diag)
	{
		std::vector<Tok> tokens;
		Tokenize(lines, tokens);
		Parser parser(tokens, vertexStage, result, diag);
		return parser.Run();
	}

	bool GlslReflector::MergeStages(const StageReflection& vertex, const StageReflection& fragment, StageReflection& merged, Diagnostic& diag)
	{
		merged = vertex;

		for (const StructInfo& s : fragment.Structs) {
			bool found = false;
			for (const StructInfo& existing : merged.Structs) {
				if (existing.Name == s.Name) {
					if (!StructsEqual(existing, s)) {
						return Fail(diag, "struct '" + s.Name + "' is declared differently in vertex and fragment stages", 1);
					}
					found = true;
					break;
				}
			}
			if (!found) {
				merged.Structs.push_back(s);
			}
		}

		for (const UniformInfo& u : fragment.Uniforms) {
			bool found = false;
			for (const UniformInfo& existing : merged.Uniforms) {
				if (existing.Name == u.Name) {
					if (existing.TypeName != u.TypeName || existing.ArraySize != u.ArraySize) {
						return Fail(diag, "uniform '" + u.Name + "' is declared differently in vertex and fragment stages", 1);
					}
					found = true;
					break;
				}
			}
			if (!found) {
				merged.Uniforms.push_back(u);
			}
		}

		for (const BlockInfo& b : fragment.Blocks) {
			bool found = false;
			for (const BlockInfo& existing : merged.Blocks) {
				if (existing.Name == b.Name) {
					if (!BlocksEqual(existing, b)) {
						return Fail(diag, "uniform block '" + b.Name + "' is declared differently in vertex and fragment stages", 1);
					}
					found = true;
					break;
				}
			}
			if (!found) {
				merged.Blocks.push_back(b);
			}
		}

		for (const TextureInfo& t : fragment.Textures) {
			bool found = false;
			for (const TextureInfo& existing : merged.Textures) {
				if (existing.Name == t.Name) {
					if (existing.Type != t.Type) {
						return Fail(diag, "sampler '" + t.Name + "' is declared differently in vertex and fragment stages", 1);
					}
					found = true;
					break;
				}
			}
			if (!found) {
				merged.Textures.push_back(t);
			}
		}

		// Attributes come only from the vertex stage — already copied
		return true;
	}
}
