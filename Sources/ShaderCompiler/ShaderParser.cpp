#include "ShaderParser.h"
#include "ConstFold.h"

#include <algorithm>
#include <cstring>

#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>

using namespace Death::Containers::Literals;

namespace ShaderCompiler
{
	namespace
	{
		constexpr std::int32_t MaxExpressionDepth = 32;
		constexpr std::int32_t MaxExpansionPasses = 16;

		// std::string-semantics position/substring helpers over StringView (Death's find returns a view,
		// so these keep the index-based scanning logic byte-for-byte identical)
		constexpr std::size_t Npos = ~std::size_t{0};

		StringView Substr(StringView s, std::size_t pos, std::size_t count = Npos)
		{
			std::size_t size = s.size();
			if (pos > size) pos = size;
			std::size_t avail = size - pos;
			if (count > avail) count = avail;
			return s.slice(pos, pos + count);
		}

		std::size_t Find(StringView s, char c, std::size_t pos = 0)
		{
			for (std::size_t size = s.size(); pos < size; pos++) if (s[pos] == c) return pos;
			return Npos;
		}

		std::size_t Find(StringView s, StringView needle, std::size_t pos = 0)
		{
			std::size_t size = s.size(), n = needle.size();
			if (n == 0) return (pos <= size ? pos : Npos);
			if (n > size) return Npos;
			for (std::size_t last = size - n; pos <= last; pos++) {
				if (std::memcmp(s.data() + pos, needle.data(), n) == 0) return pos;
				if (pos == last) break;
			}
			return Npos;
		}

		std::size_t Rfind(StringView s, char c)
		{
			for (std::size_t i = s.size(); i > 0; i--) if (s[i - 1] == c) return i - 1;
			return Npos;
		}

		std::size_t FindFirstNotOf(StringView s, StringView set, std::size_t pos = 0)
		{
			for (std::size_t size = s.size(); pos < size; pos++) if (!set.contains(s[pos])) return pos;
			return Npos;
		}

		std::size_t FindLastNotOf(StringView s, StringView set)
		{
			for (std::size_t i = s.size(); i > 0; i--) if (!set.contains(s[i - 1])) return i - 1;
			return Npos;
		}

		std::size_t FindLastOf(StringView s, StringView set)
		{
			for (std::size_t i = s.size(); i > 0; i--) if (set.contains(s[i - 1])) return i - 1;
			return Npos;
		}

		bool IsSpace(char c)
		{
			return (c == ' ' || c == '\t');
		}

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

		String Trim(StringView s)
		{
			std::size_t begin = 0;
			std::size_t end = s.size();
			while (begin < end && IsSpace(s[begin])) {
				begin++;
			}
			while (end > begin && IsSpace(s[end - 1])) {
				end--;
			}
			return Substr(s, begin, end - begin);
		}

		bool IsIdentifier(StringView s)
		{
			if (s.empty() || !IsIdentStart(s[0])) {
				return false;
			}
			for (std::size_t i = 1; i < s.size(); i++) {
				if (!IsIdentChar(s[i])) {
					return false;
				}
			}
			return true;
		}

		bool ParseDecimal(StringView s, std::int64_t& value)
		{
			if (s.empty()) {
				return false;
			}
			std::size_t i = 0;
			bool negative = false;
			if (s[0] == '-') {
				negative = true;
				i++;
			}
			if (i >= s.size()) {
				return false;
			}
			std::int64_t v = 0;
			for (; i < s.size(); i++) {
				if (!IsDigit(s[i])) {
					return false;
				}
				v = v * 10 + (s[i] - '0');
				if (v > 0x7FFFFFFFLL) {
					return false;
				}
			}
			value = (negative ? -v : v);
			return true;
		}

		void SplitWords(StringView s, std::vector<String>& words)
		{
			std::size_t i = 0;
			while (i < s.size()) {
				while (i < s.size() && IsSpace(s[i])) {
					i++;
				}
				std::size_t begin = i;
				while (i < s.size() && !IsSpace(s[i])) {
					i++;
				}
				if (i > begin) {
					words.push_back(Substr(s, begin, i - begin));
				}
			}
		}

		String FirstIdentifier(StringView s)
		{
			std::size_t i = 0;
			while (i < s.size() && !IsIdentStart(s[i])) {
				i++;
			}
			std::size_t begin = i;
			while (i < s.size() && IsIdentChar(s[i])) {
				i++;
			}
			return Substr(s, begin, i - begin);
		}

		bool Fail(Diagnostic& diag, const String& message, std::int32_t line)
		{
			diag.Message = message;
			diag.Line = line;
			return false;
		}

		// --- #if expression evaluation ------------------------------------------------------------

		enum class ExprTokenType : std::uint8_t
		{
			Number,
			Identifier,
			Operator,
			End
		};

		struct ExprToken
		{
			ExprTokenType Type = ExprTokenType::End;
			String Text;
			std::int64_t Value = 0;
		};

		/** Recursive-descent evaluator for #if/#elif integer constant expressions */
		class ExprEvaluator
		{
		public:
			ExprEvaluator(const Preprocessor& pp, std::int32_t line, std::int32_t depth, Diagnostic& diag)
				: _pp(pp), _line(line), _depth(depth), _diag(diag), _pos(0)
			{
			}

			bool Evaluate(StringView expression, std::int64_t& value)
			{
				if (_depth > MaxExpressionDepth) {
					return Fail(_diag, "macro expansion too deep in #if expression", _line);
				}
				if (!Tokenize(expression)) {
					return false;
				}
				if (!ParseOr(value)) {
					return false;
				}
				if (Peek().Type != ExprTokenType::End) {
					return Fail(_diag, "unexpected token '"_s + Peek().Text + "' in #if expression"_s, _line);
				}
				return true;
			}

		private:
			const Preprocessor& _pp;
			std::int32_t _line;
			std::int32_t _depth;
			Diagnostic& _diag;
			std::vector<ExprToken> _tokens;
			std::size_t _pos;

			const ExprToken& Peek() const
			{
				return _tokens[_pos];
			}

			ExprToken Get()
			{
				ExprToken t = _tokens[_pos];
				if (_pos + 1 < _tokens.size()) {
					_pos++;
				}
				return t;
			}

			bool AcceptOp(const char* text)
			{
				if (Peek().Type == ExprTokenType::Operator && Peek().Text == text) {
					Get();
					return true;
				}
				return false;
			}

			bool Tokenize(StringView s)
			{
				std::size_t i = 0;
				while (i < s.size()) {
					char c = s[i];
					if (IsSpace(c)) {
						i++;
						continue;
					}
					ExprToken t;
					if (IsDigit(c)) {
						std::size_t begin = i;
						while (i < s.size() && IsDigit(s[i])) {
							i++;
						}
						// Skip integer suffixes (u, U, l, L)
						while (i < s.size() && (s[i] == 'u' || s[i] == 'U' || s[i] == 'l' || s[i] == 'L')) {
							i++;
						}
						t.Type = ExprTokenType::Number;
						t.Text = Substr(s, begin, i - begin);
						String digits = t.Text;
						while (!digits.empty() && !IsDigit(digits[digits.size() - 1])) {
							digits = String{digits.prefix(digits.size() - 1)};
						}
						if (!ParseDecimal(digits, t.Value)) {
							return Fail(_diag, "invalid integer literal '"_s + t.Text + "' in #if expression"_s, _line);
						}
					} else if (IsIdentStart(c)) {
						std::size_t begin = i;
						while (i < s.size() && IsIdentChar(s[i])) {
							i++;
						}
						t.Type = ExprTokenType::Identifier;
						t.Text = Substr(s, begin, i - begin);
					} else {
						t.Type = ExprTokenType::Operator;
						if (i + 1 < s.size()) {
							String two = Substr(s, i, 2);
							if (two == "&&" || two == "||" || two == "==" || two == "!=" || two == "<=" || two == ">=") {
								t.Text = two;
								i += 2;
								_tokens.push_back(t);
								continue;
							}
						}
						char single = s[i];
						if (single == '!' || single == '<' || single == '>' || single == '(' || single == ')' ||
							single == '+' || single == '-' || single == '*' || single == '/' || single == '%') {
							t.Text = String{&single, 1};
							i++;
						} else {
							return Fail(_diag, "unexpected character '"_s + String{&single, 1} + "' in #if expression"_s, _line);
						}
					}
					_tokens.push_back(t);
				}
				ExprToken end;
				end.Type = ExprTokenType::End;
				_tokens.push_back(end);
				return true;
			}

			bool ParseOr(std::int64_t& v)
			{
				if (!ParseAnd(v)) {
					return false;
				}
				while (AcceptOp("||")) {
					std::int64_t rhs;
					if (!ParseAnd(rhs)) {
						return false;
					}
					v = ((v != 0 || rhs != 0) ? 1 : 0);
				}
				return true;
			}

			bool ParseAnd(std::int64_t& v)
			{
				if (!ParseEquality(v)) {
					return false;
				}
				while (AcceptOp("&&")) {
					std::int64_t rhs;
					if (!ParseEquality(rhs)) {
						return false;
					}
					v = ((v != 0 && rhs != 0) ? 1 : 0);
				}
				return true;
			}

			bool ParseEquality(std::int64_t& v)
			{
				if (!ParseRelational(v)) {
					return false;
				}
				while (true) {
					bool equal;
					if (AcceptOp("==")) {
						equal = true;
					} else if (AcceptOp("!=")) {
						equal = false;
					} else {
						return true;
					}
					std::int64_t rhs;
					if (!ParseRelational(rhs)) {
						return false;
					}
					v = ((equal ? (v == rhs) : (v != rhs)) ? 1 : 0);
				}
			}

			bool ParseRelational(std::int64_t& v)
			{
				if (!ParseAdditive(v)) {
					return false;
				}
				while (true) {
					String op;
					if (AcceptOp("<=")) {
						op = "<=";
					} else if (AcceptOp(">=")) {
						op = ">=";
					} else if (AcceptOp("<")) {
						op = "<";
					} else if (AcceptOp(">")) {
						op = ">";
					} else {
						return true;
					}
					std::int64_t rhs;
					if (!ParseAdditive(rhs)) {
						return false;
					}
					bool result;
					if (op == "<=") {
						result = (v <= rhs);
					} else if (op == ">=") {
						result = (v >= rhs);
					} else if (op == "<") {
						result = (v < rhs);
					} else {
						result = (v > rhs);
					}
					v = (result ? 1 : 0);
				}
			}

			bool ParseAdditive(std::int64_t& v)
			{
				if (!ParseMultiplicative(v)) {
					return false;
				}
				while (true) {
					bool add;
					if (AcceptOp("+")) {
						add = true;
					} else if (AcceptOp("-")) {
						add = false;
					} else {
						return true;
					}
					std::int64_t rhs;
					if (!ParseMultiplicative(rhs)) {
						return false;
					}
					v = (add ? v + rhs : v - rhs);
				}
			}

			bool ParseMultiplicative(std::int64_t& v)
			{
				if (!ParseUnary(v)) {
					return false;
				}
				while (true) {
					char op;
					if (AcceptOp("*")) {
						op = '*';
					} else if (AcceptOp("/")) {
						op = '/';
					} else if (AcceptOp("%")) {
						op = '%';
					} else {
						return true;
					}
					std::int64_t rhs;
					if (!ParseUnary(rhs)) {
						return false;
					}
					if (op == '*') {
						v = v * rhs;
					} else {
						if (rhs == 0) {
							return Fail(_diag, "division by zero in #if expression", _line);
						}
						v = (op == '/' ? v / rhs : v % rhs);
					}
				}
			}

			bool ParseUnary(std::int64_t& v)
			{
				if (AcceptOp("!")) {
					if (!ParseUnary(v)) {
						return false;
					}
					v = (v == 0 ? 1 : 0);
					return true;
				}
				if (AcceptOp("-")) {
					if (!ParseUnary(v)) {
						return false;
					}
					v = -v;
					return true;
				}
				if (AcceptOp("+")) {
					return ParseUnary(v);
				}
				return ParsePrimary(v);
			}

			bool ParsePrimary(std::int64_t& v)
			{
				const ExprToken t = Get();
				if (t.Type == ExprTokenType::Number) {
					v = t.Value;
					return true;
				}
				if (t.Type == ExprTokenType::Operator && t.Text == "(") {
					if (!ParseOr(v)) {
						return false;
					}
					if (!AcceptOp(")")) {
						return Fail(_diag, "missing ')' in #if expression", _line);
					}
					return true;
				}
				if (t.Type == ExprTokenType::Identifier) {
					if (t.Text == "defined") {
						bool parenthesized = AcceptOp("(");
						const ExprToken id = Get();
						if (id.Type != ExprTokenType::Identifier) {
							return Fail(_diag, "expected identifier after 'defined'", _line);
						}
						if (parenthesized && !AcceptOp(")")) {
							return Fail(_diag, "missing ')' after 'defined'", _line);
						}
						v = (_pp.IsDefined(id.Text) ? 1 : 0);
						return true;
					}
					if (t.Text == "true") {
						v = 1;
						return true;
					}
					if (t.Text == "false") {
						v = 0;
						return true;
					}
					String body;
					if (_pp.TryGetMacroBody(t.Text, body)) {
						if (Trim(body).empty()) {
							v = 1;
							return true;
						}
						ExprEvaluator nested(_pp, _line, _depth + 1, _diag);
						return nested.Evaluate(body, v);
					}
					if (t.Text == "BATCH_SIZE") {
						// Symbolic constant — treated as defined with value 1 inside expressions
						v = 1;
						return true;
					}
					// Undefined identifiers evaluate as 0 (C preprocessor rule)
					v = 0;
					return true;
				}
				return Fail(_diag, "unexpected token '"_s + t.Text + "' in #if expression"_s, _line);
			}
		};
	}

	// --- ShaderParser -------------------------------------------------------------------------------

	void ShaderParser::SplitLines(StringView content, std::vector<SourceLine>& lines)
	{
		std::vector<SourceLine> raw;
		Array<char> current;
		std::int32_t lineNumber = 1;
		std::size_t i = 0;
		while (i < content.size()) {
			char c = content[i];
			if (c == '\r') {
				if (i + 1 < content.size() && content[i + 1] == '\n') {
					i++;
				}
				c = '\n';
			}
			if (c == '\n') {
				SourceLine l;
				l.Text = String{current.data(), current.size()};
				l.Line = lineNumber;
				raw.push_back(std::move(l));
				arrayResize(current, 0);
				lineNumber++;
			} else {
				arrayAppend(current, c);
			}
			i++;
		}
		SourceLine last;
		last.Text = String{current.data(), current.size()};
		last.Line = lineNumber;
		raw.push_back(std::move(last));

		// Join backslash-newline continuations (origin = first physical line)
		std::size_t j = 0;
		while (j < raw.size()) {
			SourceLine merged = raw[j];
			while (!merged.Text.empty() && merged.Text[merged.Text.size() - 1] == '\\' && j + 1 < raw.size()) {
				merged.Text = String{merged.Text.prefix(merged.Text.size() - 1)};
				j++;
				merged.Text = merged.Text + raw[j].Text;
			}
			lines.push_back(std::move(merged));
			j++;
		}
	}

	void ShaderParser::StripComments(std::vector<SourceLine>& lines)
	{
		bool inBlockComment = false;
		for (SourceLine& line : lines) {
			String& text = line.Text;
			std::size_t i = 0;
			while (i < text.size()) {
				if (inBlockComment) {
					if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '/') {
						text[i] = ' ';
						text[i + 1] = ' ';
						i += 2;
						inBlockComment = false;
					} else {
						text[i] = ' ';
						i++;
					}
				} else if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
					text = String{text.prefix(i)};
					break;
				} else if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*') {
					text[i] = ' ';
					text[i + 1] = ' ';
					i += 2;
					inBlockComment = true;
				} else {
					i++;
				}
			}
		}
	}

	// --- Syntax front-end (directive keywords + vertex()/fragment() lowering) ------------------------

	namespace
	{
		String TrimRight(StringView s)
		{
			std::size_t end = s.size();
			while (end > 0 && IsSpace(s[end - 1])) {
				end--;
			}
			return Substr(s, 0, end);
		}

		String LastIdentifier(StringView s)
		{
			String last;
			std::size_t i = 0;
			while (i < s.size()) {
				if (IsIdentStart(s[i])) {
					std::size_t begin = i;
					while (i < s.size() && IsIdentChar(s[i])) {
						i++;
					}
					last = Substr(s, begin, i - begin);
				} else {
					i++;
				}
			}
			return last;
		}

		/** Returns the identifier starting at the first non-space character from @p pos (empty if none) and advances @p pos */
		String WordAt(StringView s, std::size_t& pos)
		{
			while (pos < s.size() && IsSpace(s[pos])) {
				pos++;
			}
			std::size_t begin = pos;
			while (pos < s.size() && IsIdentChar(s[pos])) {
				pos++;
			}
			return Substr(s, begin, pos - begin);
		}

		bool StartsWithWord(StringView s, const char* word)
		{
			std::size_t length = std::char_traits<char>::length(word);
			return (Substr(s, 0, length) == word && (s.size() == length || !IsIdentChar(s[length])));
		}

		/** @brief Parsed "varying" declaration */
		struct VaryingDecl
		{
			String Declaration;	// Declaration without the "varying"/"flat" keywords, e.g. "highp float vExtra"
			bool Flat = false;
			std::int32_t Line = 0;
			// Backend selector of a varying wrapped in a global-scope "#ifdef/#ifndef SOFTWARE_RENDERER"
			// conditional: 0 = every backend, 1 = software renderer only, 2 = every backend EXCEPT the
			// software renderer. The lowering re-emits a tagged declaration wrapped in the matching
			// directive so BuildStageSource resolves it per backend (the transpiler sees SW-only varyings,
			// the GL/ES2/HLSL/SPIR-V emissions never do - their output stays byte-identical).
			std::int32_t SwMode = 0;
		};

		/** @brief Parsed "attribute" declaration (lowered to a vertex-stage-only "in" global) */
		struct AttributeDecl
		{
			String Declaration;	// Declaration without the "attribute" keyword, e.g. "layout(location = 0) vec2 aPosition"
			std::int32_t Line = 0;
		};

		/** @brief Intermediate parse state of a ".shader" file before lowering */
		struct ParsedShader
		{
			String ProgramName;
			String BatchedName;
			String FragmentPrecision = "mediump";	// "precision mediump|highp;" directive (GL ES float precision of the fragment stage)
			std::vector<String> Variants;
			std::vector<TextureDirective> Textures;
			std::uint32_t RenderModes = 0;
			std::vector<VaryingDecl> Varyings;
			std::vector<AttributeDecl> Attributes;
			std::vector<SourceLine> Globals;
			std::vector<SourceLine> VertexBody;
			std::vector<SourceLine> FragmentBody;
			std::int32_t BatchedLine = 0;
			bool CanvasItem = false;		// "shader_type canvas_item;" seen ("custom" is the default)
			bool HasVertexBody = false;
			bool HasFragmentBody = false;
		};

		// Vertex-stage template of the canvas_item lowering. The default (no "vertex()") output used to
		// stay byte-identical to Include/DefaultSpriteVs.inc (and the retired DefaultBatchedSpritesVs.inc,
		// whose text now lives only here) — the fidelity contract that let migrated files provably
		// regenerate the same GLSL. Dead-store removal (step 3.13) supersedes exact byte-identity for
		// programs with TRIMMED varyings: their epilogue stores and declarations disappear. Programs
		// whose fragment stage reads all three template varyings still emit this text verbatim.

		constexpr char CanvasSpriteVsHead[] =
R"GLSL(uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

layout (std140) uniform InstanceBlock
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
	// Flat index into the palette texture (added to the per-pixel index for the palette lookup). Lands in the
	// std140 tail padding after spriteSize, so the block stays 112 bytes. Only read by palette shaders.
	float palOffset;
};

out vec2 vTexCoords;
out vec4 vColor;
out highp float vPaletteOffset;
)GLSL";

		constexpr char CanvasBatchedVsHead[] =
R"GLSL(uniform mat4 uProjectionMatrix;
uniform mat4 uViewMatrix;

struct Instance
{
	mat4 modelMatrix;
	vec4 color;
	vec4 texRect;
	vec2 spriteSize;
	// Flat index into the palette texture; lands in the std140 tail padding, so the stride stays 112 bytes
	float palOffset;
};

layout (std140) uniform InstancesBlock
{
#ifndef BATCH_SIZE
	#define BATCH_SIZE (585) // 64 Kb / 112 b
#endif
	Instance[BATCH_SIZE] instances;
} block;

out vec2 vTexCoords;
out vec4 vColor;
out highp float vPaletteOffset;
)GLSL";

		constexpr char CanvasSpriteVsMain[] =
R"GLSL(void main()
{
	vec2 aPosition = vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2));
	vec4 position = vec4(aPosition.x * spriteSize.x, aPosition.y * spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * modelMatrix * position;
	vTexCoords = vec2(aPosition.x * texRect.x + texRect.y, aPosition.y * texRect.z + texRect.w);
	vColor = color;
	vPaletteOffset = palOffset;
}
)GLSL";

		constexpr char CanvasBatchedVsMain[] =
R"GLSL(void main()
{
	vec2 aPosition = vec2(1.0 - float(((gl_VertexID + 2) / 3) % 2), 1.0 - float(((gl_VertexID + 1) / 3) % 2));
	vec4 position = vec4(aPosition.x * i.spriteSize.x, aPosition.y * i.spriteSize.y, 0.0, 1.0);

	gl_Position = uProjectionMatrix * uViewMatrix * i.modelMatrix * position;
	vTexCoords = vec2(aPosition.x * i.texRect.x + i.texRect.y, aPosition.y * i.texRect.z + i.texRect.w);
	vColor = i.color;
	vPaletteOffset = i.palOffset;
}
)GLSL";

		// Note: the batched and non-batched corner formulas differ (4-vertex strip vs 6 vertices per sprite)
		constexpr char CanvasSpriteCorner[] = "vec2(1.0 - float(gl_VertexID >> 1), float(gl_VertexID % 2))";
		constexpr char CanvasBatchedCorner[] = "vec2(1.0 - float(((gl_VertexID + 2) / 3) % 2), 1.0 - float(((gl_VertexID + 1) / 3) % 2))";

		/** Appends the lines of a template raw string (with synthetic line number 0) to @p target */
		void AppendTemplate(std::vector<SourceLine>& target, const char* text)
		{
			std::vector<SourceLine> lines;
			ShaderParser::SplitLines(text, lines);
			// Raw-string templates end with a newline, so the final split line is empty — drop it
			if (!lines.empty() && lines.back().Text.empty()) {
				lines.pop_back();
			}
			for (SourceLine& line : lines) {
				line.Line = 0;
				target.push_back(std::move(line));
			}
		}

		/**
			Scans one line of a captured vertex()/fragment() body, starting at @p bodyBegin (0 for
			continuation lines, the position after the opening brace for the entry's first line).
			Appends body text from the original @p text (comments preserved) while counting braces on
			the comment-stripped @p bare; sets @p done when the entry's closing brace is reached.
		*/
		bool CaptureBodyLine(StringView text, StringView bare, std::size_t bodyBegin, std::int32_t lineNumber,
			std::vector<SourceLine>& body, std::int32_t& depth, bool& done, Diagnostic& diag)
		{
			std::size_t pos = bodyBegin;
			bool closed = false;
			while (pos < bare.size()) {
				char c = bare[pos];
				if (c == '{') {
					depth++;
				} else if (c == '}') {
					depth--;
					if (depth == 0) {
						closed = true;
						break;
					}
				}
				pos++;
			}
			// Comment stripping only blanks or truncates, so brace positions in "bare" are valid in "text"
			if (closed) {
				String prefix = TrimRight(Substr(text, bodyBegin, pos - bodyBegin));
				if (!Trim(prefix).empty()) {
					body.push_back({ std::move(prefix), lineNumber });
				}
				if (FindFirstNotOf(bare, " \t", pos + 1) != Npos) {
					return Fail(diag, "unexpected text after the closing '}'", lineNumber);
				}
				done = true;
				return true;
			}
			if (bodyBegin == 0) {
				body.push_back({ text, lineNumber });
			} else {
				String rest = TrimRight(Substr(text, bodyBegin));
				if (!Trim(rest).empty()) {
					body.push_back({ std::move(rest), lineNumber });
				}
			}
			done = false;
			return true;
		}

		/** Skips spaces and consumes the terminating ';' of a directive statement */
		bool ExpectSemicolon(StringView bare, std::size_t cursor, const char* statement, std::int32_t lineNumber, Diagnostic& diag)
		{
			while (cursor < bare.size() && IsSpace(bare[cursor])) {
				cursor++;
			}
			if (cursor >= bare.size() || bare[cursor] != ';') {
				return Fail(diag, "expected ';' after \""_s + statement + "\""_s, lineNumber);
			}
			return true;
		}

		/** Parses one directive-keyword statement ("program"/"batched"/"variant"), with @p cursor just past the keyword */
		bool ParseDirective(StringView kind, StringView bare, std::size_t cursor, std::int32_t lineNumber,
			ParsedShader& src, bool& seenProgram, Diagnostic& diag)
		{
			if (!seenProgram && kind != "program") {
				return Fail(diag, "the first directive must be \"program <Name>;\"", lineNumber);
			}

			if (kind == "program") {
				if (seenProgram) {
					return Fail(diag, "duplicate \"program\" directive", lineNumber);
				}
				String name = WordAt(bare, cursor);
				if (!IsIdentifier(name)) {
					return Fail(diag, "usage: program <Name>; (identifier)", lineNumber);
				}
				if (!ExpectSemicolon(bare, cursor, "program <Name>", lineNumber, diag)) {
					return false;
				}
				src.ProgramName = std::move(name);
				seenProgram = true;
			} else if (kind == "batched") {
				if (!src.BatchedName.empty()) {
					return Fail(diag, "duplicate \"batched\" directive", lineNumber);
				}
				String name = WordAt(bare, cursor);
				if (!IsIdentifier(name)) {
					return Fail(diag, "usage: batched <Name>; (identifier)", lineNumber);
				}
				if (!ExpectSemicolon(bare, cursor, "batched <Name>", lineNumber, diag)) {
					return false;
				}
				if (name == src.ProgramName) {
					return Fail(diag, "the batched twin must have a different name than the program", lineNumber);
				}
				src.BatchedName = std::move(name);
				src.BatchedLine = lineNumber;
			} else {	// "variant"
				String name = WordAt(bare, cursor);
				if (!IsIdentifier(name)) {
					return Fail(diag, "usage: variant <NAME>; (identifier)", lineNumber);
				}
				if (!ExpectSemicolon(bare, cursor, "variant <NAME>", lineNumber, diag)) {
					return false;
				}
				for (const String& v : src.Variants) {
					if (v == name) {
						return Fail(diag, "duplicate variant \""_s + name + "\""_s, lineNumber);
					}
				}
				src.Variants.push_back(std::move(name));
			}
			return true;
		}

		/** Parses the uniform hint list between ':' and ';' — "texture_unit(N)" becomes a TextureDirective, known hints drop */
		bool ParseUniformHints(StringView hints, StringView uniformName, std::int32_t lineNumber,
			ParsedShader& src, Diagnostic& diag)
		{
			std::size_t start = 0;
			while (true) {
				// Split on top-level commas only — hint_range(0.0, 1.0) contains commas inside parentheses
				std::size_t pos = start;
				std::int32_t parens = 0;
				while (pos < hints.size() && (parens > 0 || hints[pos] != ',')) {
					if (hints[pos] == '(') {
						parens++;
					} else if (hints[pos] == ')') {
						parens--;
					}
					pos++;
				}
				String hint = Trim(Substr(hints, start, pos - start));
				if (hint.empty()) {
					return Fail(diag, "empty uniform hint", lineNumber);
				}

				if (StartsWithWord(hint, "texture_unit")) {
					std::size_t open = Find(hint, '(');
					std::size_t close = Rfind(hint, ')');
					std::int64_t unit = -1;
					if (open == Npos || close == Npos || close < open ||
						!ParseDecimal(Trim(Substr(hint, open + 1, close - open - 1)), unit) || unit < 0 || unit > 31) {
						return Fail(diag, "invalid hint \""_s + hint + "\" (expected texture_unit(0-31))"_s, lineNumber);
					}
					for (const TextureDirective& t : src.Textures) {
						if (t.Name == uniformName) {
							return Fail(diag, "duplicate texture unit assignment for \""_s + uniformName + "\""_s, lineNumber);
						}
					}
					TextureDirective directive;
					directive.Name = uniformName;
					directive.Unit = static_cast<std::int32_t>(unit);
					directive.Line = lineNumber;
					src.Textures.push_back(std::move(directive));
				} else if (hint == "source_color" || hint == "filter_nearest" || hint == "filter_linear" ||
					hint == "repeat_enable" || hint == "repeat_disable" || StartsWithWord(hint, "hint_range")) {
					// Accepted hints without an equivalent here - parsed and silently dropped
				} else {
					return Fail(diag, "unknown uniform hint \""_s + hint + "\""_s, lineNumber);
				}

				if (pos >= hints.size()) {
					return true;
				}
				start = pos + 1;
			}
		}

		/** Parses a "render_mode <mode>[, <mode>];" statement into the RenderModes bitmask */
		bool ParseRenderMode(StringView bare, std::size_t cursor, std::int32_t lineNumber, ParsedShader& src, Diagnostic& diag)
		{
			std::size_t semi = Find(bare, ';', cursor);
			if (semi == Npos) {
				return Fail(diag, "render_mode statement must end with ';'", lineNumber);
			}
			String list = Substr(bare, cursor, semi - cursor);
			std::size_t start = 0;
			while (true) {
				std::size_t comma = Find(list, ',', start);
				String mode = Trim(Substr(list, start, comma == Npos ? Npos : comma - start));
				if (mode == "blend_mix") {
					src.RenderModes |= RenderModeBlendMix;
				} else if (mode == "blend_add") {
					src.RenderModes |= RenderModeBlendAdd;
				} else if (mode == "blend_sub") {
					src.RenderModes |= RenderModeBlendSub;
				} else if (mode == "blend_mul") {
					src.RenderModes |= RenderModeBlendMul;
				} else if (mode == "blend_premul_alpha") {
					src.RenderModes |= RenderModeBlendPremulAlpha;
				} else if (mode == "unshaded") {
					src.RenderModes |= RenderModeUnshaded;
				} else if (mode.empty()) {
					return Fail(diag, "empty render_mode entry", lineNumber);
				} else {
					return Fail(diag, "unknown render_mode \""_s + mode + "\""_s, lineNumber);
				}
				if (comma == Npos) {
					return true;
				}
				start = comma + 1;
			}
		}

		/** Parses a ".shader" file into its intermediate representation */
		bool ParseShaderSource(const std::vector<SourceLine>& lines, const std::vector<SourceLine>& stripped, ParsedShader& src, Diagnostic& diag)
		{
			bool seenProgram = false;
			bool seenShaderType = false;
			bool seenPrecision = false;
			std::int32_t globalDepth = 0;
			std::int32_t capturing = 0;			// 0 = none, 1 = vertex(), 2 = fragment()
			std::int32_t captureDepth = 0;
			std::int32_t captureStartLine = 0;
			bool awaitingBrace = false;
			// Global-scope "#ifdef/#ifndef SOFTWARE_RENDERER" conditional around varying declarations:
			// which backend the lines currently parsed belong to (VaryingDecl::SwMode semantics), and
			// whether the conditional's "#else" was already seen. Such a conditional may only wrap varying
			// declarations (plus comments/blank lines) - its directive lines are consumed here so they
			// cannot leak into the shared globals, and the tagged varyings are re-wrapped by the lowering.
			std::int32_t swVaryingMode = 0;
			bool swVaryingSeenElse = false;
			std::int32_t swVaryingLine = 0;

			for (std::size_t index = 0; index < lines.size(); index++) {
				const SourceLine& line = lines[index];
				const String& text = line.Text;
				const String& bare = stripped[index].Text;

				// Inside a vertex()/fragment() body — capture verbatim until the matching brace
				if (capturing != 0) {
					std::vector<SourceLine>& body = (capturing == 1 ? src.VertexBody : src.FragmentBody);
					std::size_t bodyBegin = 0;
					if (awaitingBrace) {
						std::size_t open = FindFirstNotOf(bare, " \t");
						if (open == Npos) {
							continue;
						}
						if (bare[open] != '{') {
							return Fail(diag, "expected '{' after \""_s + (capturing == 1 ? "vertex()" : "fragment()") + "\""_s, line.Line);
						}
						awaitingBrace = false;
						captureDepth = 1;
						bodyBegin = open + 1;
					}
					bool done = false;
					if (!CaptureBodyLine(text, bare, bodyBegin, line.Line, body, captureDepth, done, diag)) {
						return false;
					}
					if (done) {
						capturing = 0;
					}
					continue;
				}

				// Inside a global-scope aggregate or function body — pass through verbatim
				if (globalDepth > 0) {
					for (char c : bare) {
						if (c == '{') {
							globalDepth++;
						} else if (c == '}') {
							globalDepth--;
						}
					}
					if (globalDepth < 0) {
						return Fail(diag, "unmatched '}' at global scope", line.Line);
					}
					src.Globals.push_back(line);
					continue;
				}

				// A global-scope "#ifdef/#ifndef SOFTWARE_RENDERER" conditional selects backend-specific
				// varying declarations (the transpiler's per-instance-constant varying machinery). The
				// directive lines are consumed here - the collected varyings are tagged instead and the
				// lowering re-wraps them, so the shared globals never carry the (empty) resolved block and
				// every other backend's emitted text stays byte-identical. Only varying declarations,
				// comments and blank lines are allowed inside such a conditional.
				{
					std::size_t hash = FindFirstNotOf(bare, " \t"_s);
					if (hash != Npos && bare[hash] == '#') {
						std::size_t p = hash + 1;
						while (p < bare.size() && IsSpace(bare[p])) {
							p++;
						}
						std::size_t nameBegin = p;
						while (p < bare.size() && IsIdentChar(bare[p])) {
							p++;
						}
						String directive = Substr(bare, nameBegin, p - nameBegin);
						if ((directive == "ifdef" || directive == "ifndef") && FirstIdentifier(Substr(bare, p)) == "SOFTWARE_RENDERER") {
							if (swVaryingMode != 0) {
								return Fail(diag, "nested SOFTWARE_RENDERER conditionals are not supported at global scope", line.Line);
							}
							swVaryingMode = (directive == "ifdef" ? 1 : 2);
							swVaryingSeenElse = false;
							swVaryingLine = line.Line;
							continue;
						}
						if (swVaryingMode != 0) {
							if (directive == "else") {
								if (swVaryingSeenElse) {
									return Fail(diag, "duplicate #else in a SOFTWARE_RENDERER conditional", line.Line);
								}
								swVaryingSeenElse = true;
								swVaryingMode = (swVaryingMode == 1 ? 2 : 1);
								continue;
							}
							if (directive == "endif") {
								swVaryingMode = 0;
								continue;
							}
							return Fail(diag, "a global-scope SOFTWARE_RENDERER conditional supports only #else/#endif inside (no nested directives)", line.Line);
						}
					}
				}
				if (swVaryingMode != 0 && Trim(bare).empty()) {
					continue;		// Comment-only or blank lines inside the conditional are dropped with it
				}

				std::size_t cursor = 0;
				String word = WordAt(bare, cursor);
				if (swVaryingMode != 0 && word != "varying") {
					return Fail(diag, "a global-scope SOFTWARE_RENDERER conditional may only contain varying declarations", line.Line);
				}

				if (word == "program" || word == "batched" || word == "variant") {
					if (!ParseDirective(word, bare, cursor, line.Line, src, seenProgram, diag)) {
						return false;
					}
					continue;
				}

				if (word == "shader_type") {
					if (!seenProgram) {
						return Fail(diag, "the first directive must be \"program <Name>;\"", line.Line);
					}
					if (seenShaderType) {
						return Fail(diag, "duplicate shader_type statement", line.Line);
					}
					String type = WordAt(bare, cursor);
					if (type != "canvas_item" && type != "custom") {
						return Fail(diag, "unsupported shader_type \""_s + type + "\" (expected canvas_item or custom)"_s, line.Line);
					}
					String shaderType = "shader_type "_s + type;
					if (!ExpectSemicolon(bare, cursor, shaderType.data(), line.Line, diag)) {
						return false;
					}
					src.CanvasItem = (type == "canvas_item");
					seenShaderType = true;
					continue;
				}

				if (word == "render_mode") {
					if (!ParseRenderMode(bare, cursor, line.Line, src, diag)) {
						return false;
					}
					continue;
				}

				if (word == "precision") {
					// Only the two-token form "precision <p>;" is a directive — a real GLSL global
					// precision statement ("precision highp float;", with a type) passes through below
					std::size_t qualifierCursor = cursor;
					String qualifier = WordAt(bare, qualifierCursor);
					std::size_t after = qualifierCursor;
					while (after < bare.size() && IsSpace(bare[after])) {
						after++;
					}
					if (after < bare.size() && bare[after] == ';') {
						if (!seenProgram) {
							return Fail(diag, "the first directive must be \"program <Name>;\"", line.Line);
						}
						if (seenPrecision) {
							return Fail(diag, "duplicate precision directive", line.Line);
						}
						if (qualifier != "mediump" && qualifier != "highp") {
							return Fail(diag, "unsupported precision \""_s + qualifier + "\" (expected mediump or highp)"_s, line.Line);
						}
						src.FragmentPrecision = std::move(qualifier);
						seenPrecision = true;
						continue;
					}
					// A plain GLSL precision statement — falls through to the global pass-through below
				}

				if (word == "varying") {
					std::size_t semi = Find(bare, ';', cursor);
					if (semi == Npos) {
						return Fail(diag, "varying declaration must end with ';'", line.Line);
					}
					String decl = Trim(Substr(bare, cursor, semi - cursor));
					VaryingDecl varying;
					if (StartsWithWord(decl, "flat")) {
						varying.Flat = true;
						decl = Trim(Substr(decl, 4));
					}
					if (decl.empty()) {
						return Fail(diag, "usage: varying [flat] [precision] <type> <name>;", line.Line);
					}
					varying.Declaration = std::move(decl);
					varying.Line = line.Line;
					varying.SwMode = swVaryingMode;
					src.Varyings.push_back(std::move(varying));
					continue;
				}

				if (word == "attribute") {
					std::size_t semi = Find(bare, ';', cursor);
					if (semi == Npos) {
						return Fail(diag, "attribute declaration must end with ';'", line.Line);
					}
					String decl = Trim(Substr(bare, cursor, semi - cursor));
					if (decl.empty()) {
						return Fail(diag, "usage: attribute [layout(location = N)] <type> <name>;", line.Line);
					}
					AttributeDecl attribute;
					attribute.Declaration = std::move(decl);
					attribute.Line = line.Line;
					src.Attributes.push_back(std::move(attribute));
					continue;
				}

				if (word == "void") {
					std::size_t nameCursor = cursor;
					String name = WordAt(bare, nameCursor);
					if (name == "vertex" || name == "fragment") {
						if (!seenProgram) {
							return Fail(diag, "the first directive must be \"program <Name>;\"", line.Line);
						}
						bool isVertex = (name == "vertex");
						if (isVertex ? src.HasVertexBody : src.HasFragmentBody) {
							return Fail(diag, "duplicate \"void "_s + name + "()\" entry"_s, line.Line);
						}
						cursor = nameCursor;
						while (cursor < bare.size() && IsSpace(bare[cursor])) {
							cursor++;
						}
						if (cursor >= bare.size() || bare[cursor] != '(') {
							return Fail(diag, "expected \"void "_s + name + "()\""_s, line.Line);
						}
						cursor++;
						while (cursor < bare.size() && IsSpace(bare[cursor])) {
							cursor++;
						}
						if (cursor >= bare.size() || bare[cursor] != ')') {
							return Fail(diag, "\""_s + name + "()\" must not declare parameters"_s, line.Line);
						}
						cursor++;
						while (cursor < bare.size() && IsSpace(bare[cursor])) {
							cursor++;
						}
						(isVertex ? src.HasVertexBody : src.HasFragmentBody) = true;
						capturing = (isVertex ? 1 : 2);
						captureStartLine = line.Line;
						if (cursor >= bare.size()) {
							awaitingBrace = true;
						} else if (bare[cursor] == '{') {
							awaitingBrace = false;
							captureDepth = 1;
							std::vector<SourceLine>& body = (isVertex ? src.VertexBody : src.FragmentBody);
							bool done = false;
							if (!CaptureBodyLine(text, bare, cursor + 1, line.Line, body, captureDepth, done, diag)) {
								return false;
							}
							if (done) {
								capturing = 0;
							}
						} else {
							return Fail(diag, "expected '{' after \""_s + name + "()\""_s, line.Line);
						}
						continue;
					}
					// A plain "void" helper function — falls through to the global pass-through below
				}

				if (word == "uniform") {
					std::size_t colon = Find(bare, ':', cursor);
					std::size_t semi = Find(bare, ';', cursor);
					if (colon != Npos && (semi == Npos || colon < semi)) {
						if (semi == Npos) {
							return Fail(diag, "a uniform declaration with hints must end with ';' on the same line", line.Line);
						}
						String uniformName = LastIdentifier(Substr(bare, 0, colon));
						if (uniformName.empty() || uniformName == "uniform") {
							return Fail(diag, "cannot parse the uniform declaration before ':'", line.Line);
						}
						if (!ParseUniformHints(Substr(bare, colon + 1, semi - colon - 1), uniformName, line.Line, src, diag)) {
							return false;
						}
						// The lowered declaration keeps the original text with the hint list stripped
						src.Globals.push_back({ TrimRight(Substr(text, 0, colon)) + ";"_s, line.Line });
						continue;
					}
					// A plain uniform declaration — falls through to the global pass-through below
				}

				// Global scope pass-through (uniforms, blocks, consts, structs, helper functions,
				// preprocessor lines, comments and blank lines) — shared by both lowered stages
				for (char c : bare) {
					if (c == '{') {
						globalDepth++;
					} else if (c == '}') {
						globalDepth--;
					}
				}
				if (globalDepth < 0) {
					return Fail(diag, "unmatched '}' at global scope", line.Line);
				}
				src.Globals.push_back(line);
			}

			if (capturing != 0) {
				return Fail(diag, "unterminated \""_s + (capturing == 1 ? "vertex()" : "fragment()") + "\" body"_s, captureStartLine);
			}
			if (swVaryingMode != 0) {
				return Fail(diag, "unterminated SOFTWARE_RENDERER conditional at global scope", swVaryingLine);
			}
			if (globalDepth != 0) {
				return Fail(diag, "unbalanced braces at global scope", lines.empty() ? 1 : lines.back().Line);
			}
			if (!seenProgram) {
				return Fail(diag, "missing \"program <Name>;\" directive", 1);
			}
			if (!src.CanvasItem) {
				if (!src.BatchedName.empty()) {
					return Fail(diag, "\"batched\" twins require \"shader_type canvas_item;\"", src.BatchedLine);
				}
				if (!src.HasVertexBody) {
					return Fail(diag, "missing \"void vertex()\" entry (required unless \"shader_type canvas_item;\" is used)", 1);
				}
			}
			if (!src.HasFragmentBody) {
				return Fail(diag, "missing \"void fragment()\" entry", 1);
			}

			// Trim leading/trailing blank lines of the globals — the lowering inserts its own separators
			while (!src.Globals.empty() && Trim(src.Globals.front().Text).empty()) {
				src.Globals.erase(src.Globals.begin());
			}
			while (!src.Globals.empty() && Trim(src.Globals.back().Text).empty()) {
				src.Globals.pop_back();
			}
			return true;
		}

		/**
			Searches @p lines for a whole-identifier occurrence of @p name outside comments.
			Returns true and sets @p foundLine on the first hit.
		*/
		bool FindIdentifier(const std::vector<SourceLine>& lines, const char* name, std::int32_t& foundLine)
		{
			const std::size_t nameLength = std::strlen(name);
			bool inBlockComment = false;
			for (const SourceLine& line : lines) {
				const String& text = line.Text;
				std::size_t i = 0;
				char previous = '\0';
				while (i < text.size()) {
					char c = text[i];
					if (inBlockComment) {
						if (c == '*' && i + 1 < text.size() && text[i + 1] == '/') {
							i += 2;
							inBlockComment = false;
							previous = '/';
						} else {
							previous = c;
							i++;
						}
						continue;
					}
					if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
						break;
					}
					if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
						i += 2;
						inBlockComment = true;
						previous = '*';
						continue;
					}
					if (IsIdentStart(c) && !IsIdentChar(previous) && previous != '.') {
						std::size_t begin = i;
						while (i < text.size() && IsIdentChar(text[i])) {
							i++;
						}
						if (i - begin == nameLength && Substr(text, begin, nameLength) == name) {
							foundLine = line.Line;
							return true;
						}
						previous = text[i - 1];
						continue;
					}
					previous = c;
					i++;
				}
			}
			return false;
		}

		/**
			Returns true when @p globals contain a global uniform declaration of @p name
			(line-based scan on a comment-stripped copy; hint lists were already stripped
			by the uniform-hint parsing, so a declaration ends with ';' on its line).
		*/
		bool DeclaresUniform(const std::vector<SourceLine>& globals, const char* name)
		{
			std::vector<SourceLine> stripped = globals;
			ShaderParser::StripComments(stripped);
			for (const SourceLine& line : stripped) {
				std::size_t cursor = 0;
				if (WordAt(line.Text, cursor) != "uniform") {
					continue;
				}
				std::size_t semi = Find(line.Text, ';', cursor);
				if (LastIdentifier(Substr(line.Text, cursor, semi == Npos ? Npos : semi - cursor)) == name) {
					return true;
				}
			}
			return false;
		}

		/**
			Rewrites the fragment() body for the lowered canvas_item fragment stage: whole-identifier
			substitutions UV -> vTexCoords, TEXTURE -> uTexture, PALETTE_OFFSET -> vPaletteOffset
			(COLOR stays — it is the fragment output variable), skipping comments. Custom mode runs
			no substitutions at all.
		*/
		bool LowerCanvasFragmentBody(std::vector<SourceLine>& body, Diagnostic& diag)
		{
			bool inBlockComment = false;
			for (SourceLine& line : body) {
				const String& text = line.Text;
				Array<char> out;
				arrayReserve(out, text.size());
				std::size_t i = 0;
				char previous = '\0';
				while (i < text.size()) {
					char c = text[i];
					if (inBlockComment) {
						if (c == '*' && i + 1 < text.size() && text[i + 1] == '/') {
							arrayAppend(out, "*/"_s);
							i += 2;
							inBlockComment = false;
							previous = '/';
						} else {
							arrayAppend(out, c);
							previous = c;
							i++;
						}
						continue;
					}
					if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
						arrayAppend(out, Substr(text, i));
						break;
					}
					if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
						arrayAppend(out, "/*"_s);
						i += 2;
						inBlockComment = true;
						previous = '*';
						continue;
					}
					if (IsIdentStart(c) && !IsIdentChar(previous) && previous != '.') {
						std::size_t idBegin = i;
						while (i < text.size() && IsIdentChar(text[i])) {
							i++;
						}
						String id = Substr(text, idBegin, i - idBegin);
						if (id == "UV") {
							arrayAppend(out, "vTexCoords"_s);
						} else if (id == "TEXTURE") {
							arrayAppend(out, "uTexture"_s);
						} else if (id == "PALETTE_OFFSET") {
							arrayAppend(out, "vPaletteOffset"_s);
						} else if (id == "VERTEX" || id == "NORMAL" || id == "SCREEN_UV" || id == "SCREEN_PIXEL_SIZE" ||
							id == "TIME" || id == "POINT_COORD") {
							return Fail(diag, "Built-in \""_s + id + "\" is not supported in fragment() (only UV, TEXTURE, COLOR and PALETTE_OFFSET are available)"_s, line.Line);
						} else {
							arrayAppend(out, id);
						}
						previous = out[out.size() - 1];
						continue;
					}
					arrayAppend(out, c);
					previous = c;
					i++;
				}
				line.Text = String{out.data(), out.size()};
			}
			return true;
		}

		// --- Unused-function elimination ------------------------------------------------------------

		/** @brief Extent of one global-scope function definition inside a stage line stream (inclusive line/column positions) */
		struct FunctionDef
		{
			String Name;
			std::size_t StartLine = 0;		// Index of the line where the definition (return type) starts
			std::size_t StartCol = 0;
			std::size_t EndLine = 0;		// Index of the line holding the closing '}' of the body
			std::size_t EndCol = 0;
		};

		/**
			Scans the comment-stripped stage stream for global-scope function definitions
			("<ident…> <name>(…) {…}", multi-line signatures allowed; prototypes without bodies and
			aggregate declarations like uniform blocks are skipped). Preprocessor lines never
			participate in brace counting. Returns false when the stream looks unbalanced —
			the caller then skips elimination entirely.
		*/
		bool ScanFunctionDefinitions(const std::vector<SourceLine>& stripped, std::vector<FunctionDef>& defs)
		{
			constexpr std::size_t NoLine = std::size_t(-1);
			enum class Mode { TopLevel, Parens, AwaitBrace, Body };

			Mode mode = Mode::TopLevel;
			std::int32_t depth = 0;
			std::int32_t parenDepth = 0;
			std::size_t stmtLine = NoLine, stmtCol = 0;
			String lastIdent;
			FunctionDef current;

			for (std::size_t i = 0; i < stripped.size(); i++) {
				const String& text = stripped[i].Text;
				std::size_t first = FindFirstNotOf(text, " \t");
				if (first != Npos && text[first] == '#') {
					// Preprocessor line — no brace counting; abort a half-parsed signature
					if (mode == Mode::Parens || mode == Mode::AwaitBrace) {
						mode = Mode::TopLevel;
					}
					if (mode == Mode::TopLevel) {
						stmtLine = NoLine;
						lastIdent = {};
					}
					continue;
				}
				std::size_t pos = 0;
				while (pos < text.size()) {
					char c = text[pos];
					if (mode == Mode::Parens) {
						if (c == '(') {
							parenDepth++;
						} else if (c == ')') {
							parenDepth--;
							if (parenDepth == 0) {
								mode = Mode::AwaitBrace;
							}
						}
						pos++;
						continue;
					}
					if (mode == Mode::AwaitBrace) {
						if (IsSpace(c)) {
							pos++;
							continue;
						}
						if (c == '{') {
							depth++;
							mode = Mode::Body;
							pos++;
							continue;
						}
						// Not a definition (prototype, declaration or initializer) — reprocess the character
						mode = Mode::TopLevel;
						lastIdent = {};
						continue;
					}
					if (mode == Mode::Body) {
						if (c == '{') {
							depth++;
						} else if (c == '}') {
							depth--;
							if (depth < 0) {
								return false;
							}
							if (depth == 0) {
								current.EndLine = i;
								current.EndCol = pos;
								defs.push_back(current);
								mode = Mode::TopLevel;
								stmtLine = NoLine;
								lastIdent = {};
							}
						}
						pos++;
						continue;
					}
					// Mode::TopLevel
					if (depth > 0) {
						// Inside a global-scope aggregate (struct or uniform block) — skip its body
						if (c == '{') {
							depth++;
						} else if (c == '}') {
							depth--;
							if (depth < 0) {
								return false;
							}
							if (depth == 0) {
								stmtLine = NoLine;
								lastIdent = {};
							}
						}
						pos++;
						continue;
					}
					if (IsSpace(c)) {
						pos++;
						continue;
					}
					if (stmtLine == NoLine) {
						stmtLine = i;
						stmtCol = pos;
					}
					if (IsIdentStart(c)) {
						std::size_t begin = pos;
						while (pos < text.size() && IsIdentChar(text[pos])) {
							pos++;
						}
						lastIdent = Substr(text, begin, pos - begin);
						continue;
					}
					if (c == '(') {
						if (!lastIdent.empty()) {
							current = FunctionDef();
							current.Name = lastIdent;
							current.StartLine = stmtLine;
							current.StartCol = stmtCol;
							mode = Mode::Parens;
							parenDepth = 1;
						}
						pos++;
						continue;
					}
					if (c == '{') {
						depth++;
						lastIdent = {};
						pos++;
						continue;
					}
					if (c == '}') {
						return false;
					}
					if (c == ';') {
						stmtLine = NoLine;
						lastIdent = {};
						pos++;
						continue;
					}
					pos++;
				}
			}
			return (mode == Mode::TopLevel && depth == 0);
		}

		/** Returns true when position (@p line, @p col) lies inside one of the definitions in @p defs */
		bool InsideDefinition(const std::vector<const FunctionDef*>& defs, std::size_t line, std::size_t col)
		{
			for (const FunctionDef* def : defs) {
				bool afterStart = (line > def->StartLine || (line == def->StartLine && col >= def->StartCol));
				bool beforeEnd = (line < def->EndLine || (line == def->EndLine && col <= def->EndCol));
				if (afterStart && beforeEnd) {
					return true;
				}
			}
			return false;
		}

		/** Counts whole-identifier occurrences of @p name in @p stripped outside the definition ranges in @p ownDefs */
		std::size_t CountExternalReferences(const std::vector<SourceLine>& stripped, StringView name,
			const std::vector<const FunctionDef*>& ownDefs)
		{
			std::size_t count = 0;
			for (std::size_t i = 0; i < stripped.size(); i++) {
				const String& text = stripped[i].Text;
				std::size_t pos = 0;
				char previous = '\0';
				while (pos < text.size()) {
					char c = text[pos];
					if (IsIdentStart(c) && !IsIdentChar(previous) && previous != '.') {
						std::size_t begin = pos;
						while (pos < text.size() && IsIdentChar(text[pos])) {
							pos++;
						}
						if (pos - begin == name.size() && Substr(text, begin, name.size()) == name &&
							!InsideDefinition(ownDefs, i, begin)) {
							count++;
						}
						previous = text[pos - 1];
						continue;
					}
					previous = c;
					pos++;
				}
			}
			return count;
		}

		/**
			Removes every global-scope function whose name is never referenced outside its own
			definition(s) from the assembled stage stream, iterating to a fixpoint so that
			helpers only used by removed functions disappear too. main()/vertex()/fragment() are
			roots; overloads share fate by name; references are counted conservatively (an
			identifier occurrence anywhere outside comments counts, including inactive
			preprocessor branches). This is what makes stage guards around helper functions
			unnecessary — an FS-only helper simply vanishes from the emitted vertex stage.
		*/
		void EliminateUnusedFunctions(std::vector<SourceLine>& lines)
		{
			for (std::int32_t pass = 0; pass < 32; pass++) {
				std::vector<SourceLine> stripped = lines;
				ShaderParser::StripComments(stripped);

				std::vector<FunctionDef> defs;
				if (!ScanFunctionDefinitions(stripped, defs)) {
					return;		// Unbalanced/unscannable stream — leave the text untouched
				}

				// Group definitions by name (overloads share fate)
				std::map<String, std::vector<const FunctionDef*>> byName;
				for (const FunctionDef& def : defs) {
					byName[def.Name].push_back(&def);
				}

				std::vector<const FunctionDef*> removable;
				for (const auto& pair : byName) {
					if (pair.first == "main" || pair.first == "vertex" || pair.first == "fragment") {
						continue;
					}
					if (CountExternalReferences(stripped, pair.first, pair.second) == 0) {
						removable.insert(removable.end(), pair.second.begin(), pair.second.end());
					}
				}
				if (removable.empty()) {
					return;
				}

				// Remove in descending position order so earlier ranges stay valid
				std::sort(removable.begin(), removable.end(), [](const FunctionDef* a, const FunctionDef* b) {
					return (a->StartLine > b->StartLine || (a->StartLine == b->StartLine && a->StartCol > b->StartCol));
				});
				for (const FunctionDef* def : removable) {
					const String& startStripped = stripped[def->StartLine].Text;
					const String& endStripped = stripped[def->EndLine].Text;
					bool firstWhole = (FindFirstNotOf(startStripped, " \t") >= def->StartCol);
					bool lastWhole = (FindFirstNotOf(endStripped, " \t", def->EndCol + 1) == Npos);
					if (firstWhole && lastWhole) {
						// Comment-only lines directly above the definition belong to it — remove them too
						std::size_t begin = def->StartLine;
						while (begin > 0 && Trim(stripped[begin - 1].Text).empty() && !Trim(lines[begin - 1].Text).empty()) {
							begin--;
						}
						lines.erase(lines.begin() + begin, lines.begin() + def->EndLine + 1);
						// Collapse the blank separator the removed function leaves behind
						if (begin < lines.size() && Trim(lines[begin].Text).empty() &&
							(begin == 0 || Trim(lines[begin - 1].Text).empty())) {
							lines.erase(lines.begin() + begin);
						}
					} else if (def->StartLine == def->EndLine) {
						String& t = lines[def->StartLine].Text;
						t = t.prefix(def->StartCol) + t.exceptPrefix(def->EndCol + 1);
					} else {
						if (!lastWhole) {
							lines[def->EndLine].Text = String{lines[def->EndLine].Text.exceptPrefix(def->EndCol + 1)};
							lines.erase(lines.begin() + def->StartLine + 1, lines.begin() + def->EndLine);
						} else {
							lines.erase(lines.begin() + def->StartLine + 1, lines.begin() + def->EndLine + 1);
						}
						lines[def->StartLine].Text = String{lines[def->StartLine].Text.prefix(def->StartCol)};
					}
				}
			}
		}

		// --- Unused-varying trimming ----------------------------------------------------------------

		/** @brief One global-scope "in"/"out" interface declaration found in an assembled stage stream */
		struct InterfaceDecl
		{
			std::size_t LineIndex = 0;
			std::vector<String> Names;										// All names declared on the line ("out vec2 a, b;")
			std::vector<std::pair<std::size_t, std::size_t>> QualifierSpans;	// Column ranges of the qualifiers stripped on demotion
		};

		/**
			Parses one comment-stripped global-scope line as an interface declaration of the given
			@p direction ("in"/"out"). Accepts interpolation/storage qualifiers in either order, an
			optional precision qualifier and multiple comma-separated names. Returns false for
			anything else — arrays, initializers, layout qualifiers and "invariant" declarations
			are conservatively left untouched by the trimming pass.
		*/
		bool ParseInterfaceDeclLine(StringView bare, const char* direction, InterfaceDecl& decl)
		{
			std::size_t pos = 0;
			bool seenDirection = false;
			String word;
			while (true) {
				while (pos < bare.size() && IsSpace(bare[pos])) {
					pos++;
				}
				std::size_t begin = pos;
				while (pos < bare.size() && IsIdentChar(bare[pos])) {
					pos++;
				}
				word = Substr(bare, begin, pos - begin);
				if (word.empty()) {
					return false;
				}
				bool qualifier = false;
				if (!seenDirection && word == direction) {
					seenDirection = true;
					qualifier = true;
				} else if (word == "flat" || word == "smooth" || word == "noperspective" || word == "centroid") {
					qualifier = true;
				} else if (word == "invariant" || word == "in" || word == "out") {
					// "invariant" outs must stay declared; a second (or opposite) direction is not a match
					return false;
				}
				if (qualifier) {
					// The span to strip includes the trailing whitespace run
					std::size_t spanEnd = pos;
					while (spanEnd < bare.size() && IsSpace(bare[spanEnd])) {
						spanEnd++;
					}
					decl.QualifierSpans.emplace_back(begin, spanEnd);
					continue;
				}
				break;
			}
			if (!seenDirection) {
				return false;
			}
			if (word == "highp" || word == "mediump" || word == "lowp") {
				while (pos < bare.size() && IsSpace(bare[pos])) {
					pos++;
				}
				std::size_t begin = pos;
				while (pos < bare.size() && IsIdentChar(bare[pos])) {
					pos++;
				}
				word = Substr(bare, begin, pos - begin);
			}
			if (!IsIdentifier(word)) {
				return false;		// The type
			}
			while (true) {
				while (pos < bare.size() && IsSpace(bare[pos])) {
					pos++;
				}
				std::size_t begin = pos;
				while (pos < bare.size() && IsIdentChar(bare[pos])) {
					pos++;
				}
				String name = Substr(bare, begin, pos - begin);
				if (!IsIdentifier(name)) {
					return false;
				}
				decl.Names.push_back(std::move(name));
				while (pos < bare.size() && IsSpace(bare[pos])) {
					pos++;
				}
				if (pos < bare.size() && bare[pos] == ',') {
					pos++;
					continue;
				}
				if (pos < bare.size() && bare[pos] == ';') {
					return (FindFirstNotOf(bare, " \t", pos + 1) == Npos);
				}
				return false;		// Arrays, initializers and other tails are not touched
			}
		}

		/**
			Collects the global-scope interface declarations of the given direction from a
			comment-stripped stage stream. Preprocessor lines never participate in brace counting
			(declarations inside conditional branches still count — conservative). Returns false
			when the stream looks unbalanced — the caller then skips trimming entirely.
		*/
		bool CollectInterfaceDecls(const std::vector<SourceLine>& stripped, const char* direction, std::vector<InterfaceDecl>& decls)
		{
			std::int32_t depth = 0;
			for (std::size_t i = 0; i < stripped.size(); i++) {
				const String& text = stripped[i].Text;
				std::size_t first = FindFirstNotOf(text, " \t");
				if (first == Npos) {
					continue;
				}
				if (text[first] == '#') {
					continue;
				}
				if (depth == 0) {
					InterfaceDecl decl;
					if (ParseInterfaceDeclLine(text, direction, decl)) {
						decl.LineIndex = i;
						decls.push_back(std::move(decl));
						continue;	// A matched declaration line contains no braces
					}
				}
				for (char c : text) {
					if (c == '{') {
						depth++;
					} else if (c == '}') {
						depth--;
						if (depth < 0) {
							return false;
						}
					}
				}
			}
			return (depth == 0);
		}

		/** Returns true when @p name occurs as a whole identifier on any line NOT listed in @p excludedLines */
		bool IdentifierReadOutsideLines(const std::vector<SourceLine>& stripped, StringView name, const std::vector<std::size_t>& excludedLines)
		{
			for (std::size_t i = 0; i < stripped.size(); i++) {
				if (std::find(excludedLines.begin(), excludedLines.end(), i) != excludedLines.end()) {
					continue;
				}
				const String& text = stripped[i].Text;
				std::size_t pos = 0;
				char previous = '\0';
				while (pos < text.size()) {
					char c = text[pos];
					if (IsIdentStart(c) && !IsIdentChar(previous) && previous != '.') {
						std::size_t begin = pos;
						while (pos < text.size() && IsIdentChar(text[pos])) {
							pos++;
						}
						if (pos - begin == name.size() && Substr(text, begin, name.size()) == name) {
							return true;
						}
						previous = text[pos - 1];
						continue;
					}
					previous = c;
					pos++;
				}
			}
			return false;
		}

		// --- Dead-store removal for trimmed varyings ------------------------------------------------

		/** Returns true when a comment-stripped line is a preprocessor directive */
		bool IsPreprocessorLine(StringView text)
		{
			std::size_t first = FindFirstNotOf(text, " \t");
			return (first != Npos && text[first] == '#');
		}

		/**
			Returns true when @p name may be called inside a removable store's right-hand side:
			the GLSL type constructors and a fixed list of known pure builtins. Any other
			identifier followed by '(' (user functions, macros) disqualifies the store.
		*/
		bool IsPureCallee(StringView name)
		{
			static const char* PureCallees[] = {
				"vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4",
				"bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4", "float", "int", "uint", "bool",
				"sin", "cos", "tan", "floor", "ceil", "fract", "abs", "sign", "mod", "min", "max",
				"clamp", "mix", "step", "smoothstep", "dot", "cross", "length", "distance",
				"normalize", "pow", "exp", "log", "exp2", "log2", "sqrt", "inversesqrt",
				"texture", "texelFetch", "textureLod"
			};
			for (const char* callee : PureCallees) {
				if (name == callee) {
					return true;
				}
			}
			return false;
		}

		/**
			Advances (@p li, @p pos) to the next non-whitespace character of a statement extent,
			crossing line breaks. Returns false at the end of the stream or when the cursor would
			land on a preprocessor line — a directive inside a statement extent disqualifies it.
		*/
		bool SkipStatementWhitespace(const std::vector<SourceLine>& stripped, std::size_t& li, std::size_t& pos)
		{
			bool sameLine = true;
			while (li < stripped.size()) {
				const String& text = stripped[li].Text;
				while (pos < text.size() && IsSpace(text[pos])) {
					pos++;
				}
				if (pos < text.size()) {
					if (!sameLine && IsPreprocessorLine(text)) {
						return false;
					}
					return true;
				}
				li++;
				pos = 0;
				sameLine = false;
			}
			return false;
		}

		/**
			Scans a PURE expression in the comment-stripped stream from (@p li, @p pos) up to
			@p terminator (';' or ']') at nesting depth 0, leaving the cursor ON the terminator.
			Pure means: no assignment (plain '=' and every compound form), no '++'/'--', no
			braces, no comma outside a whitelisted call's argument list, and no calls except the
			whitelisted type constructors and pure builtins — any other identifier followed by
			'(' disqualifies. Preprocessor lines inside the extent disqualify too.
		*/
		bool ScanPureExpression(const std::vector<SourceLine>& stripped, std::size_t& li, std::size_t& pos, char terminator)
		{
			std::vector<bool> callArgs;		// One entry per open '('/'[' — true = whitelisted call argument list
			while (true) {
				if (!SkipStatementWhitespace(stripped, li, pos)) {
					return false;
				}
				const String& text = stripped[li].Text;
				char c = text[pos];
				if (callArgs.empty() && c == terminator) {
					return true;
				}
				if (c == '#' || c == '{' || c == '}' || c == ';') {
					return false;		// ';' here means the ']' terminator was never reached
				}
				if (IsIdentStart(c)) {
					std::size_t begin = pos;
					while (pos < text.size() && IsIdentChar(text[pos])) {
						pos++;
					}
					String name = Substr(text, begin, pos - begin);
					// An identifier followed by '(' is a call — whitelisted callees only
					std::size_t peekLine = li, peekPos = pos;
					if (SkipStatementWhitespace(stripped, peekLine, peekPos) && stripped[peekLine].Text[peekPos] == '(') {
						if (!IsPureCallee(name)) {
							return false;
						}
						li = peekLine;
						pos = peekPos + 1;
						callArgs.push_back(true);
					}
					continue;
				}
				if (c == '(' || c == '[') {
					callArgs.push_back(false);
					pos++;
					continue;
				}
				if (c == ')' || c == ']') {
					if (callArgs.empty()) {
						return false;
					}
					callArgs.pop_back();
					pos++;
					continue;
				}
				if (c == ',') {
					if (callArgs.empty() || !callArgs.back()) {
						return false;	// Comma operator (or a comma outside whitelisted call arguments)
					}
					pos++;
					continue;
				}
				if (c == '+' || c == '-') {
					if (pos + 1 < text.size() && (text[pos + 1] == c || text[pos + 1] == '=')) {
						return false;	// Increment/decrement or compound assignment
					}
					pos++;
					continue;
				}
				if (c == '*' || c == '/' || c == '%' || c == '^') {
					if (pos + 1 < text.size() && text[pos + 1] == '=') {
						return false;	// Compound assignment
					}
					pos++;
					continue;
				}
				if (c == '&' || c == '|') {
					if (pos + 1 < text.size() && text[pos + 1] == '=') {
						return false;	// Compound assignment
					}
					if (pos + 1 < text.size() && text[pos + 1] == c) {
						pos += 2;		// Logical && / ||
						continue;
					}
					pos++;
					continue;
				}
				if (c == '<' || c == '>') {
					if (pos + 1 < text.size() && text[pos + 1] == c) {
						if (pos + 2 < text.size() && text[pos + 2] == '=') {
							return false;	// Shift-assignment
						}
						pos += 2;		// Shift
						continue;
					}
					if (pos + 1 < text.size() && text[pos + 1] == '=') {
						pos += 2;		// Comparison
						continue;
					}
					pos++;
					continue;
				}
				if (c == '!') {
					if (pos + 1 < text.size() && text[pos + 1] == '=') {
						pos += 2;		// Comparison
						continue;
					}
					pos++;
					continue;
				}
				if (c == '=') {
					if (pos + 1 < text.size() && text[pos + 1] == '=') {
						pos += 2;		// Equality comparison
						continue;
					}
					return false;		// Nested assignment
				}
				if (c == '.' || c == '?' || c == ':' || c == '~' || IsDigit(c)) {
					pos++;
					continue;
				}
				return false;			// Anything unrecognized — conservative
			}
		}

		/**
			Returns true when the identifier starting at (@p li, @p col) begins a STANDALONE
			statement: the previous significant character in the comment-stripped stream is '{',
			'}' or ';' — never an unbraced if/else/for body or an expression context — with no
			preprocessor line in between.
		*/
		bool IsStandaloneStatementStart(const std::vector<SourceLine>& stripped, std::size_t li, std::size_t col)
		{
			std::size_t line = li;
			std::size_t pos = col;
			while (true) {
				const String& text = stripped[line].Text;
				while (pos > 0 && IsSpace(text[pos - 1])) {
					pos--;
				}
				if (pos > 0) {
					if (line != li && IsPreprocessorLine(text)) {
						return false;	// The previous significant token is a preprocessor directive
					}
					char c = text[pos - 1];
					return (c == '{' || c == '}' || c == ';');
				}
				if (line == 0) {
					return false;		// Start of the stream — not inside any function body
				}
				line--;
				pos = stripped[line].Text.size();
			}
		}

		/**
			Validates the tail of a removable store statement whose target name was just read,
			cursor at (@p li, @p pos) directly after the name: an optional sequence of '.swizzle'
			and '[index]' selectors (index must be pure), then a SINGLE '=' (not '==') and a pure
			right-hand side up to the terminating ';'. Returns true with (@p endLine, @p endCol)
			at the ';'.
		*/
		bool ScanRemovableStoreTail(const std::vector<SourceLine>& stripped, std::size_t li, std::size_t pos,
			std::size_t& endLine, std::size_t& endCol)
		{
			while (true) {
				if (!SkipStatementWhitespace(stripped, li, pos)) {
					return false;
				}
				char c = stripped[li].Text[pos];
				if (c == '.') {
					pos++;
					if (!SkipStatementWhitespace(stripped, li, pos)) {
						return false;
					}
					const String& text = stripped[li].Text;
					if (!IsIdentStart(text[pos])) {
						return false;
					}
					while (pos < text.size() && IsIdentChar(text[pos])) {
						pos++;
					}
					continue;
				}
				if (c == '[') {
					pos++;
					if (!ScanPureExpression(stripped, li, pos, ']')) {
						return false;
					}
					pos++;		// Past the ']'
					continue;
				}
				if (c == '=') {
					if (pos + 1 < stripped[li].Text.size() && stripped[li].Text[pos + 1] == '=') {
						return false;	// A comparison — the name is READ, not stored to
					}
					pos++;
					if (!ScanPureExpression(stripped, li, pos, ';')) {
						return false;
					}
					endLine = li;
					endCol = pos;
					return true;
				}
				return false;			// Compound assignment, a read or anything else
			}
		}

		/** @brief Character extent [Start..End] of one removable statement (or declaration line) in a stage stream */
		struct StoreExtent
		{
			std::size_t StartLine = 0;
			std::size_t StartCol = 0;
			std::size_t EndLine = 0;
			std::size_t EndCol = 0;
		};

		/**
			Checks whether EVERY whole-identifier occurrence of @p name in the comment-stripped
			vertex stream is either (a) on its own declaration line (@p declLine) or (b) the
			assignment target of a standalone statement with a provably pure right-hand side.
			The paren depth is tracked across the stream, so a store buried in a for-header or a
			call argument list never qualifies; an occurrence on a preprocessor line disqualifies
			too. On success the store extents are appended to @p stores and the caller may remove
			the declaration AND all stores; ANY failing occurrence keeps the demotion fallback
			for the whole name (all-or-nothing — no partial removal).
		*/
		bool CollectRemovableStores(const std::vector<SourceLine>& stripped, StringView name,
			std::size_t declLine, std::vector<StoreExtent>& stores)
		{
			std::int32_t parenDepth = 0;
			for (std::size_t i = 0; i < stripped.size(); i++) {
				const String& text = stripped[i].Text;
				bool preprocessor = IsPreprocessorLine(text);
				std::size_t pos = 0;
				char previous = '\0';
				while (pos < text.size()) {
					char c = text[pos];
					if (IsIdentStart(c) && !IsIdentChar(previous) && previous != '.') {
						std::size_t begin = pos;
						while (pos < text.size() && IsIdentChar(text[pos])) {
							pos++;
						}
						previous = text[pos - 1];
						if (pos - begin != name.size() || Substr(text, begin, name.size()) != name) {
							continue;
						}
						if (i == declLine) {
							continue;	// (a) its own declaration line
						}
						if (preprocessor || parenDepth != 0 || !IsStandaloneStatementStart(stripped, i, begin)) {
							return false;
						}
						StoreExtent extent;
						extent.StartLine = i;
						extent.StartCol = begin;
						if (!ScanRemovableStoreTail(stripped, i, pos, extent.EndLine, extent.EndCol)) {
							return false;
						}
						stores.push_back(extent);
						continue;
					}
					if (!preprocessor) {
						if (c == '(') {
							parenDepth++;
						} else if (c == ')') {
							parenDepth--;
							if (parenDepth < 0) {
								return false;
							}
						}
					}
					previous = c;
					pos++;
				}
			}
			return true;
		}

		/**
			Removes every fragment-stage "in" (varying) declaration whose name the fragment text
			never reads (comment-aware whole-identifier scan, conservative — an occurrence inside
			any preprocessor branch counts), then cleans the matching vertex-stage "out"
			declarations. When EVERY vertex-stage occurrence of every declared name is provably a
			dead store — the assignment target (optionally '.swizzle'/'[index]') of a standalone
			statement whose right-hand side is pure (no assignments, no '++'/'--', no comma
			operator, only whitelisted type-constructor/pure-builtin calls) — the declaration and
			all its stores are REMOVED outright. Otherwise the declaration is DEMOTED to a plain
			global variable: the storage and interpolation qualifiers are stripped, precision +
			type + names stay, so every existing store still compiles and the driver eliminates
			the dead code (all-or-nothing per name — a single unprovable occurrence keeps every
			store). Vertex outs that never had a fragment "in" at all (stage-guarded declarations)
			are handled under the same rule. Multi-name declarations are handled conservatively
			(kept when ANY name is read; removed only when ALL names qualify). Runs after
			unused-function elimination so references inside eliminated helpers do not count, and
			BEFORE TrimUnusedUniforms so uniforms/blocks read only by removed stores cascade away
			in its fixpoint. Reflection is unaffected — varyings do not reflect and demoted plain
			globals are skipped by the reflection parser.
		*/
		void TrimUnusedVaryings(ShaderDocument& document)
		{
			// Fragment side — drop the unread "in" declarations
			{
				std::vector<SourceLine> stripped = document.FragmentLines;
				ShaderParser::StripComments(stripped);
				std::vector<InterfaceDecl> decls;
				if (!CollectInterfaceDecls(stripped, "in", decls)) {
					return;
				}
				std::map<String, std::vector<std::size_t>> declLinesByName;
				for (const InterfaceDecl& decl : decls) {
					for (const String& name : decl.Names) {
						declLinesByName[name].push_back(decl.LineIndex);
					}
				}
				std::vector<std::size_t> removable;
				for (const InterfaceDecl& decl : decls) {
					bool anyRead = false;
					for (const String& name : decl.Names) {
						if (IdentifierReadOutsideLines(stripped, name, declLinesByName[name])) {
							anyRead = true;
							break;
						}
					}
					if (!anyRead) {
						removable.push_back(decl.LineIndex);
					}
				}
				for (auto it = removable.rbegin(); it != removable.rend(); ++it) {
					document.FragmentLines.erase(document.FragmentLines.begin() + *it);
				}
			}

			// Vertex side — remove or demote every "out" declaration the (trimmed) fragment stage
			// never reads. Unread declarations are already gone from the fragment stream, so any
			// occurrence left there (including a surviving declaration line) counts as a read.
			{
				std::vector<SourceLine> fragment = document.FragmentLines;
				ShaderParser::StripComments(fragment);
				std::vector<SourceLine> stripped = document.VertexLines;
				ShaderParser::StripComments(stripped);
				std::vector<InterfaceDecl> decls;
				if (!CollectInterfaceDecls(stripped, "out", decls)) {
					return;
				}
				const std::vector<std::size_t> noExcludedLines;
				std::vector<StoreExtent> removals;
				for (const InterfaceDecl& decl : decls) {
					bool anyRead = false;
					for (const String& name : decl.Names) {
						if (IdentifierReadOutsideLines(fragment, name, noExcludedLines)) {
							anyRead = true;
							break;
						}
					}
					if (anyRead) {
						continue;
					}
					// Dead-store removal: when every occurrence of every declared name is provably
					// a dead store, the declaration and all its stores are removed outright
					{
						std::vector<StoreExtent> stores;
						bool removable = true;
						for (const String& name : decl.Names) {
							if (!CollectRemovableStores(stripped, name, decl.LineIndex, stores)) {
								removable = false;
								break;
							}
						}
						if (removable) {
							// The declaration goes too — interface declaration lines are whole-line
							// by construction (nothing follows the ';')
							StoreExtent declExtent;
							declExtent.StartLine = decl.LineIndex;
							declExtent.StartCol = FindFirstNotOf(stripped[decl.LineIndex].Text, " \t");
							declExtent.EndLine = decl.LineIndex;
							declExtent.EndCol = FindLastNotOf(stripped[decl.LineIndex].Text, " \t");
							removals.push_back(declExtent);
							removals.insert(removals.end(), stores.begin(), stores.end());
							continue;
						}
					}
					// Demotion fallback — strip the qualifier spans back to front (positions computed
					// on the stripped copy are valid in the original — comments only blank or truncate)
					String& line = document.VertexLines[decl.LineIndex].Text;
					std::vector<std::pair<std::size_t, std::size_t>> spans = decl.QualifierSpans;
					std::sort(spans.begin(), spans.end());
					bool valid = true;
					for (const auto& span : spans) {
						if (span.second > line.size() ||
							Substr(line, span.first, span.second - span.first) != Substr(stripped[decl.LineIndex].Text, span.first, span.second - span.first)) {
							valid = false;
							break;
						}
					}
					if (!valid) {
						continue;
					}
					for (auto it = spans.rbegin(); it != spans.rend(); ++it) {
						line = line.prefix(it->first) + line.exceptPrefix(it->second);
					}
				}
				// Apply the removals in descending position order so earlier extents stay valid
				// (demotions edit lines in place and never shift indices). Whole-line statements
				// take their lines with them; a resulting double-blank seam collapses to one blank
				std::sort(removals.begin(), removals.end(), [](const StoreExtent& a, const StoreExtent& b) {
					return (a.StartLine > b.StartLine || (a.StartLine == b.StartLine && a.StartCol > b.StartCol));
				});
				std::vector<SourceLine>& lines = document.VertexLines;
				for (const StoreExtent& extent : removals) {
					const String& startStripped = stripped[extent.StartLine].Text;
					const String& endStripped = stripped[extent.EndLine].Text;
					bool firstWhole = (FindFirstNotOf(startStripped, " \t") >= extent.StartCol);
					bool lastWhole = (FindFirstNotOf(endStripped, " \t", extent.EndCol + 1) == Npos);
					if (firstWhole && lastWhole) {
						lines.erase(lines.begin() + extent.StartLine, lines.begin() + extent.EndLine + 1);
						while (extent.StartLine < lines.size() && Trim(lines[extent.StartLine].Text).empty() &&
							(extent.StartLine == 0 || Trim(lines[extent.StartLine - 1].Text).empty())) {
							lines.erase(lines.begin() + extent.StartLine);
						}
					} else if (extent.StartLine == extent.EndLine) {
						String& t = lines[extent.StartLine].Text;
						t = t.prefix(extent.StartCol) + t.exceptPrefix(extent.EndCol + 1);
					} else {
						if (!lastWhole) {
							lines[extent.EndLine].Text = String{lines[extent.EndLine].Text.exceptPrefix(extent.EndCol + 1)};
							lines.erase(lines.begin() + extent.StartLine + 1, lines.begin() + extent.EndLine);
						} else {
							lines.erase(lines.begin() + extent.StartLine + 1, lines.begin() + extent.EndLine + 1);
						}
						lines[extent.StartLine].Text = String{lines[extent.StartLine].Text.prefix(extent.StartCol)};
					}
				}
			}
		}

		// --- Unused-uniform/block elimination ---------------------------------------------------------

		/** @brief Kind of a removable global-scope declaration recognized by TrimUnusedUniforms */
		enum class GlobalDeclKind : std::uint8_t
		{
			Define,			// "#define NAME ..." (object- or function-like) at unconditional global scope
			DefineTrio,		// "#ifndef NAME" + "#define NAME ..." + "#endif" fallback block (removed as a unit)
			LooseUniform,	// "uniform <type> <name>[, <name>...];" on a single line (samplers included)
			Block,			// "layout (std140) uniform <BlockName> { ... } [instanceName];"
			Struct			// "struct <Name> { ... };"
		};

		/** @brief One global-scope declaration with its whole-line extent [StartLine..EndLine] in a stage line stream */
		struct GlobalDecl
		{
			GlobalDeclKind Kind = GlobalDeclKind::Define;
			std::vector<String> DeclaredNames;		// Reflection-rule keys (uniform names / the block name); empty = exempt (defines, structs)
			std::vector<String> ReferenceNames;	// Names whose external references pin the declaration
			std::size_t StartLine = 0;
			std::size_t EndLine = 0;
		};

		/**
			Advances (@p li, @p pos) over whitespace and line breaks. Returns false at the end of the
			stream or when the next token would sit on a preprocessor line — a declaration header
			never continues across one.
		*/
		bool SkipDeclWhitespace(const std::vector<SourceLine>& lines, std::size_t& li, std::size_t& pos)
		{
			bool sameLine = true;
			while (li < lines.size()) {
				const String& text = lines[li].Text;
				while (pos < text.size() && IsSpace(text[pos])) {
					pos++;
				}
				if (pos < text.size()) {
					if (!sameLine && text[pos] == '#') {
						return false;
					}
					return true;
				}
				li++;
				pos = 0;
				sameLine = false;
			}
			return false;
		}

		/** Reads the identifier at the cursor (empty when the next token is not an identifier) */
		String ReadDeclWord(const std::vector<SourceLine>& lines, std::size_t& li, std::size_t& pos)
		{
			if (!SkipDeclWhitespace(lines, li, pos)) {
				return String{};
			}
			const String& text = lines[li].Text;
			std::size_t begin = pos;
			while (pos < text.size() && IsIdentChar(text[pos])) {
				pos++;
			}
			return Substr(text, begin, pos - begin);
		}

		/** Consumes the expected character at the cursor, returns false when something else follows */
		bool ConsumeDeclChar(const std::vector<SourceLine>& lines, std::size_t& li, std::size_t& pos, char expected)
		{
			if (!SkipDeclWhitespace(lines, li, pos)) {
				return false;
			}
			if (lines[li].Text[pos] != expected) {
				return false;
			}
			pos++;
			return true;
		}

		/** Finds the '}' matching the '{' at (@p li, @p pos); preprocessor lines never participate in brace counting */
		bool FindDeclClosingBrace(const std::vector<SourceLine>& lines, std::size_t li, std::size_t pos, std::size_t& endLine, std::size_t& endPos)
		{
			std::int32_t depth = 0;
			for (std::size_t j = li; j < lines.size(); j++) {
				const String& text = lines[j].Text;
				std::size_t first = FindFirstNotOf(text, " \t");
				if (j != li && first != Npos && text[first] == '#') {
					continue;
				}
				for (std::size_t k = (j == li ? pos : 0); k < text.size(); k++) {
					if (text[k] == '{') {
						depth++;
					} else if (text[k] == '}') {
						depth--;
						if (depth == 0) {
							endLine = j;
							endPos = k;
							return true;
						}
						if (depth < 0) {
							return false;
						}
					}
				}
			}
			return false;
		}

		/**
			Extracts the declared name(s) from a declarator list ("mat4 uMatrix" / "vec2 a, b" /
			"Instance[BATCH_SIZE] instances"). Bracketed array sizes are blanked out first, so a
			symbolic size is never mistaken for the name. Returns false when parsing fails — the
			caller then leaves the declaration untouched (conservative).
		*/
		bool ExtractDeclaratorNames(StringView list, std::vector<String>& names)
		{
			std::size_t start = 0;
			bool firstChunk = true;
			while (start <= list.size()) {
				std::size_t pos = start;
				std::int32_t nesting = 0;
				while (pos < list.size() && (nesting > 0 || list[pos] != ',')) {
					if (list[pos] == '(' || list[pos] == '[') {
						nesting++;
					} else if (list[pos] == ')' || list[pos] == ']') {
						nesting--;
					}
					pos++;
				}
				Array<char> cleaned;
				std::int32_t brackets = 0;
				for (std::size_t c = start; c < pos; c++) {
					if (list[c] == '[') {
						brackets++;
					} else if (list[c] == ']') {
						brackets--;
					} else if (brackets == 0) {
						arrayAppend(cleaned, list[c]);
					}
				}
				StringView cleanedView{cleaned.data(), cleaned.size()};
				String name = LastIdentifier(cleanedView);
				if (!IsIdentifier(name)) {
					return false;
				}
				if (firstChunk) {
					// The first chunk holds the type too — require a separate type identifier
					if (FirstIdentifier(cleanedView) == name) {
						return false;
					}
					firstChunk = false;
				}
				names.push_back(std::move(name));
				if (pos >= list.size()) {
					return true;
				}
				start = pos + 1;
			}
			return true;
		}

		/** Returns true when @p text is a "#define <name> ..." line for exactly @p name */
		bool IsDefineOfName(StringView text, StringView name)
		{
			std::size_t first = FindFirstNotOf(text, " \t"_s);
			if (first == Npos || text[first] != '#') {
				return false;
			}
			std::size_t p = first + 1;
			while (p < text.size() && IsSpace(text[p])) {
				p++;
			}
			if (Substr(text, p, 6) != "define"_s || (p + 6 < text.size() && IsIdentChar(text[p + 6]))) {
				return false;
			}
			return (FirstIdentifier(Substr(text, p + 6)) == name);
		}

		/** Returns true when @p text is an "#endif" line */
		bool IsEndifLine(StringView text)
		{
			std::size_t first = FindFirstNotOf(text, " \t"_s);
			if (first == Npos || text[first] != '#') {
				return false;
			}
			std::size_t p = first + 1;
			while (p < text.size() && IsSpace(text[p])) {
				p++;
			}
			std::size_t begin = p;
			while (p < text.size() && IsIdentChar(text[p])) {
				p++;
			}
			return (p - begin == 5 && Substr(text, begin, p - begin) == "endif"_s);
		}

		/**
			Scans the comment-stripped stage stream for removable global-scope declarations: loose
			uniforms (single-line), std140 uniform blocks, struct declarations, "#define" lines and
			"#ifndef X/#define X/#endif" fallback trios. Only declarations at unconditional global
			scope (outside functions AND outside every preprocessor conditional) are recorded —
			removing a declaration from inside a conditional could leave an empty guard and its
			presence may differ per variant. Function bodies are skipped by brace counting;
			preprocessor lines never participate in it. Returns false when the stream looks
			unbalanced — the caller then skips trimming entirely.
		*/
		bool ScanGlobalDeclarations(const std::vector<SourceLine>& stripped, std::vector<GlobalDecl>& decls)
		{
			std::int32_t braceDepth = 0;
			std::int32_t condDepth = 0;
			std::size_t i = 0;
			while (i < stripped.size()) {
				StringView text = stripped[i].Text;
				std::size_t first = FindFirstNotOf(text, " \t"_s);
				if (first == Npos) {
					i++;
					continue;
				}
				if (text[first] == '#') {
					std::size_t p = first + 1;
					while (p < text.size() && IsSpace(text[p])) {
						p++;
					}
					std::size_t nameBegin = p;
					while (p < text.size() && IsIdentChar(text[p])) {
						p++;
					}
					String name = Substr(text, nameBegin, p - nameBegin);
					String rest = Substr(text, p);
					if (name == "if" || name == "ifdef" || name == "ifndef") {
						// "#ifndef X" + "#define X ..." + "#endif" fallback trio — recorded as a unit
						if (name == "ifndef" && braceDepth == 0 && condDepth == 0 && i + 2 < stripped.size()) {
							String guard = FirstIdentifier(rest);
							if (IsIdentifier(guard) && IsDefineOfName(stripped[i + 1].Text, guard) && IsEndifLine(stripped[i + 2].Text)) {
								GlobalDecl decl;
								decl.Kind = GlobalDeclKind::DefineTrio;
								decl.ReferenceNames.push_back(std::move(guard));
								decl.StartLine = i;
								decl.EndLine = i + 2;
								decls.push_back(std::move(decl));
								i += 3;
								continue;
							}
						}
						condDepth++;
					} else if (name == "endif") {
						if (condDepth == 0) {
							return false;
						}
						condDepth--;
					} else if (name == "define") {
						if (braceDepth == 0 && condDepth == 0) {
							String macro = FirstIdentifier(rest);
							if (IsIdentifier(macro)) {
								GlobalDecl decl;
								decl.Kind = GlobalDeclKind::Define;
								decl.ReferenceNames.push_back(std::move(macro));
								decl.StartLine = i;
								decl.EndLine = i;
								decls.push_back(std::move(decl));
							}
						}
					}
					i++;
					continue;
				}

				if (braceDepth == 0 && condDepth == 0) {
					std::size_t cursor = first;
					String word = WordAt(text, cursor);

					// "struct <Name> { ... };" (nothing else on the closing line)
					if (word == "struct") {
						std::size_t li = i, pos = cursor;
						String structName = ReadDeclWord(stripped, li, pos);
						std::size_t endLine, endPos;
						if (IsIdentifier(structName) && ConsumeDeclChar(stripped, li, pos, '{') &&
							FindDeclClosingBrace(stripped, li, pos - 1, endLine, endPos)) {
							StringView endText = stripped[endLine].Text;
							std::size_t after = FindFirstNotOf(endText, " \t"_s, endPos + 1);
							if (after != Npos && endText[after] == ';' &&
								FindFirstNotOf(endText, " \t"_s, after + 1) == Npos) {
								GlobalDecl decl;
								decl.Kind = GlobalDeclKind::Struct;
								decl.ReferenceNames.push_back(std::move(structName));
								decl.StartLine = i;
								decl.EndLine = endLine;
								decls.push_back(std::move(decl));
								i = endLine + 1;
								continue;
							}
						}
					}

					// "layout (std140) uniform <BlockName> { <members> } [instanceName];"
					if (word == "layout") {
						std::size_t li = i, pos = cursor;
						bool parsed = false;
						if (ConsumeDeclChar(stripped, li, pos, '(')) {
							std::size_t layoutBegin = pos;
							std::size_t close = Find(stripped[li].Text, ')', pos);
							if (close != Npos) {
								String layoutList = " "_s + Substr(stripped[li].Text, layoutBegin, close - layoutBegin) + " "_s;
								bool std140 = false;
								std::size_t q = 0;
								String qualifier;
								while (!(qualifier = FirstIdentifier(Substr(layoutList, q))).empty()) {
									if (qualifier == "std140") {
										std140 = true;
										break;
									}
									q = Find(layoutList, qualifier, q) + qualifier.size();
								}
								pos = close + 1;
								if (std140 && ReadDeclWord(stripped, li, pos) == "uniform") {
									String blockName = ReadDeclWord(stripped, li, pos);
									std::size_t bodyLine = li, bodyPos = pos;
									std::size_t endLine, endPos;
									if (IsIdentifier(blockName) && ConsumeDeclChar(stripped, bodyLine, bodyPos, '{') &&
										FindDeclClosingBrace(stripped, bodyLine, bodyPos - 1, endLine, endPos)) {
										// Tail on the closing line: optional instance name + ';', nothing after
										StringView endText = stripped[endLine].Text;
										std::size_t tailPos = endPos + 1;
										String instanceName;
										std::size_t idBegin = FindFirstNotOf(endText, " \t"_s, tailPos);
										if (idBegin != Npos && IsIdentStart(endText[idBegin])) {
											tailPos = idBegin;
											while (tailPos < endText.size() && IsIdentChar(endText[tailPos])) {
												tailPos++;
											}
											instanceName = Substr(endText, idBegin, tailPos - idBegin);
										}
										std::size_t semi = FindFirstNotOf(endText, " \t"_s, tailPos);
										if (semi != Npos && endText[semi] == ';' &&
											FindFirstNotOf(endText, " \t"_s, semi + 1) == Npos) {
											// Member names: the ';'-separated declarations between the braces
											Array<char> body;
											for (std::size_t j = bodyLine; j <= endLine; j++) {
												StringView memberText = stripped[j].Text;
												std::size_t memberFirst = FindFirstNotOf(memberText, " \t"_s);
												if (memberFirst != Npos && memberText[memberFirst] == '#') {
													continue;
												}
												std::size_t from = (j == bodyLine ? bodyPos : 0);
												std::size_t to = (j == endLine ? endPos : memberText.size());
												arrayAppend(body, Substr(memberText, from, to - from));
												arrayAppend(body, ' ');
											}
											StringView bodyView{body.data(), body.size()};
											GlobalDecl decl;
											decl.Kind = GlobalDeclKind::Block;
											decl.DeclaredNames.push_back(blockName);
											decl.ReferenceNames.push_back(blockName);
											if (!instanceName.empty()) {
												decl.ReferenceNames.push_back(instanceName);
											}
											bool membersOk = true;
											std::size_t stmtStart = 0;
											while (stmtStart < bodyView.size()) {
												std::size_t stmtEnd = Find(bodyView, ';', stmtStart);
												if (stmtEnd == Npos) {
													membersOk = Trim(Substr(bodyView, stmtStart)).empty();
													break;
												}
												String stmt = Trim(Substr(bodyView, stmtStart, stmtEnd - stmtStart));
												if (!stmt.empty() && !ExtractDeclaratorNames(stmt, decl.ReferenceNames)) {
													membersOk = false;
													break;
												}
												stmtStart = stmtEnd + 1;
											}
											if (membersOk) {
												decl.StartLine = i;
												decl.EndLine = endLine;
												decls.push_back(std::move(decl));
												i = endLine + 1;
												parsed = true;
											}
										}
									}
								}
							}
						}
						if (parsed) {
							continue;
						}
					}

					// "uniform <type> <name>[, <name>...];" — single line, whole statement
					if (word == "uniform" && Find(text, '{') == Npos && Find(text, '}') == Npos) {
						std::size_t semi = Find(text, ';', cursor);
						if (semi != Npos && FindFirstNotOf(text, " \t"_s, semi + 1) == Npos) {
							GlobalDecl decl;
							decl.Kind = GlobalDeclKind::LooseUniform;
							if (ExtractDeclaratorNames(Trim(Substr(text, cursor, semi - cursor)), decl.DeclaredNames)) {
								decl.ReferenceNames = decl.DeclaredNames;
								decl.StartLine = i;
								decl.EndLine = i;
								decls.push_back(std::move(decl));
								i++;
								continue;
							}
						}
					}
				}

				for (char c : text) {
					if (c == '{') {
						braceDepth++;
					} else if (c == '}') {
						braceDepth--;
						if (braceDepth < 0) {
							return false;
						}
					}
				}
				i++;
			}
			return (braceDepth == 0 && condDepth == 0);
		}

		/**
			Removes per-stage unused uniform declarations, std140 uniform blocks, dead "#define"
			lines and dead struct declarations from the final stage streams of a document,
			iterating to a fixpoint (comment-aware, whole-identifier, conservative — a reference
			inside any preprocessor branch counts, and only declarations at unconditional global
			scope are considered). HARD reflection-preservation rule: a uniform or block is removed
			from a stage only when the same declaration survives in the OTHER stage of the same
			document — a declaration unused in both stages stays in both, so the merged per-variant
			reflection is unchanged (the engine addresses uniforms by reflected name; a vanished
			entry would break its call sites). Dead "#define"s (a NAME referenced nowhere else in
			the stage; the "#ifndef X/#define X/#endif" fallback trio is matched and removed as a
			unit so no empty guard survives) and dead structs never reflect, so they are exempt.
			Cascades resolve naturally over the fixpoint: removing "#define i block.instances[...]"
			unpins InstancesBlock, whose removal unpins "struct Instance" and the BATCH_SIZE
			fallback inside its extent. This reproduces exactly the minimal per-stage declaration
			sets the old VERTEX_STAGE/FRAGMENT_STAGE guards used to produce — automatically.
		*/
		void TrimUnusedUniforms(ShaderDocument& document)
		{
			for (std::int32_t pass = 0; pass < 32; pass++) {
				std::vector<SourceLine>* stageLines[2] = { &document.VertexLines, &document.FragmentLines };
				std::vector<SourceLine> stripped[2];
				std::vector<GlobalDecl> decls[2];
				for (std::int32_t s = 0; s < 2; s++) {
					stripped[s] = *stageLines[s];
					ShaderParser::StripComments(stripped[s]);
					if (!ScanGlobalDeclarations(stripped[s], decls[s])) {
						return;		// Unbalanced/unscannable stream — leave both stages untouched
					}
				}

				// A declaration is a candidate when none of its names is referenced outside its own extent
				std::vector<bool> candidate[2];
				for (std::int32_t s = 0; s < 2; s++) {
					candidate[s].resize(decls[s].size());
					for (std::size_t d = 0; d < decls[s].size(); d++) {
						std::vector<std::size_t> excluded;
						for (std::size_t l = decls[s][d].StartLine; l <= decls[s][d].EndLine; l++) {
							excluded.push_back(l);
						}
						bool referenced = false;
						for (const String& name : decls[s][d].ReferenceNames) {
							if (IdentifierReadOutsideLines(stripped[s], name, excluded)) {
								referenced = true;
								break;
							}
						}
						candidate[s][d] = !referenced;
					}
				}

				// Names of uniform/block declarations that survive this pass, per stage and kind
				// (the reflection-preservation rule checks against the OTHER stage's kept set)
				auto keptNames = [&decls, &candidate](std::int32_t s, GlobalDeclKind kind) {
					std::vector<String> kept;
					for (std::size_t d = 0; d < decls[s].size(); d++) {
						if (decls[s][d].Kind == kind && !candidate[s][d]) {
							kept.insert(kept.end(), decls[s][d].DeclaredNames.begin(), decls[s][d].DeclaredNames.end());
						}
					}
					return kept;
				};

				bool anyRemoved = false;
				for (std::int32_t s = 0; s < 2; s++) {
					const std::int32_t other = 1 - s;
					std::vector<std::pair<std::size_t, std::size_t>> extents;
					for (std::size_t d = 0; d < decls[s].size(); d++) {
						if (!candidate[s][d]) {
							continue;
						}
						const GlobalDecl& decl = decls[s][d];
						if (decl.Kind == GlobalDeclKind::LooseUniform || decl.Kind == GlobalDeclKind::Block) {
							// Reflection-preservation rule — every declared name must survive in the other stage
							const std::vector<String> kept = keptNames(other, decl.Kind);
							bool allKept = true;
							for (const String& name : decl.DeclaredNames) {
								if (std::find(kept.begin(), kept.end(), name) == kept.end()) {
									allKept = false;
									break;
								}
							}
							if (!allKept) {
								continue;
							}
						}
						extents.emplace_back(decl.StartLine, decl.EndLine);
					}
					if (extents.empty()) {
						continue;
					}
					anyRemoved = true;
					// Remove in descending position order so earlier extents stay valid
					std::sort(extents.begin(), extents.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
					std::vector<SourceLine>& lines = *stageLines[s];
					for (const auto& extent : extents) {
						lines.erase(lines.begin() + extent.first, lines.begin() + extent.second + 1);
						// Collapse the blank seam the removed declaration leaves behind to a single blank line
						while (extent.first < lines.size() && Trim(lines[extent.first].Text).empty() &&
							(extent.first == 0 || Trim(lines[extent.first - 1].Text).empty())) {
							lines.erase(lines.begin() + extent.first);
						}
					}
				}
				if (!anyRemoved) {
					return;
				}
			}
		}

		// --- Constant folding -------------------------------------------------------------------------

		/**
			Applies literal constant folding (see ConstFold.h) to every function-definition extent
			of an assembled stage stream. Global-scope declarations are never touched, so the
			reflection preprocessor sees the exact same declarations before and after the pass.
			Fold positions are computed on the comment-stripped copy (position-identical to the
			original); a span overlapping a comment is skipped.
		*/
		void FoldConstantExpressions(std::vector<SourceLine>& lines)
		{
			std::vector<SourceLine> stripped = lines;
			ShaderParser::StripComments(stripped);
			std::vector<FunctionDef> defs;
			if (!ScanFunctionDefinitions(stripped, defs)) {
				return;		// Unbalanced/unscannable stream — leave the text untouched
			}
			for (const FunctionDef& def : defs) {
				std::vector<FoldInputLine> region;
				for (std::size_t i = def.StartLine; i <= def.EndLine; i++) {
					FoldInputLine line;
					line.Text = &stripped[i].Text;
					line.Begin = (i == def.StartLine ? def.StartCol : 0);
					line.End = (i == def.EndLine ? def.EndCol + 1 : stripped[i].Text.size());
					line.Index = i;
					region.push_back(line);
				}
				std::vector<FoldEdit> edits;
				ConstFolder::ComputeFolds(region, edits);
				for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
					String& original = lines[it->Index].Text;
					std::size_t length = it->End - it->Begin;
					if (it->End > original.size() ||
						Substr(original, it->Begin, length) != Substr(stripped[it->Index].Text, it->Begin, length)) {
						continue;	// A comment overlaps the span
					}
					String replacement = it->Replacement;
					if (replacement[0] == '-' && it->Begin > 0 &&
						(original[it->Begin - 1] == '-' || original[it->Begin - 1] == '+')) {
						replacement = " "_s + replacement;	// "x--2" would tokenize as a decrement
					}
					original = Substr(original, 0, it->Begin) + StringView{replacement} + Substr(original, it->Begin + length);
				}
			}
		}

		/**
			Splices a captured vertex()/fragment() entry body into the generated main() VERBATIM —
			no scope block, no re-indentation; the body lines keep their exact source text. User
			locals therefore live directly in main()'s scope: redeclaring a name the surrounding
			main() declares (e.g. a canvas vertex() prologue built-in) is a GLSL compile error,
			which is intended — better than silent shadowing.
		*/
		void AppendEntryBody(std::vector<SourceLine>& lines, const std::vector<SourceLine>& body)
		{
			for (const SourceLine& bodyLine : body) {
				lines.push_back(bodyLine);
			}
		}

		/**
			Formats one collected "attribute" declaration as its vertex-stage "in" global. A leading
			"layout(...)" qualifier stays in front of the "in" keyword ("attribute layout(location = 0)
			vec2 aPosition;" emits "layout(location = 0) in vec2 aPosition;"), everything else passes
			through verbatim after "in ".
		*/
		String FormatAttributeDecl(StringView declaration)
		{
			if (StartsWithWord(declaration, "layout")) {
				std::size_t pos = Find(declaration, '(');
				if (pos != Npos) {
					std::int32_t depth = 0;
					while (pos < declaration.size()) {
						if (declaration[pos] == '(') {
							depth++;
						} else if (declaration[pos] == ')') {
							depth--;
							if (depth == 0) {
								break;
							}
						}
						pos++;
					}
					if (pos < declaration.size()) {
						return Substr(declaration, 0, pos + 1) + " in "_s + Trim(Substr(declaration, pos + 1)) + ";"_s;
					}
				}
			}
			return "in "_s + declaration + ";"_s;
		}

		/** Returns true when @p s contains a whole-identifier occurrence of VERTEX_STAGE or FRAGMENT_STAGE */
		bool ContainsStageMacro(StringView s)
		{
			std::size_t i = 0;
			char previous = '\0';
			while (i < s.size()) {
				char c = s[i];
				if (IsIdentStart(c) && !IsIdentChar(previous)) {
					std::size_t begin = i;
					while (i < s.size() && IsIdentChar(s[i])) {
						i++;
					}
					StringView id = Substr(s, begin, i - begin);
					if (id == "VERTEX_STAGE" || id == "FRAGMENT_STAGE") {
						return true;
					}
					previous = s[i - 1];
					continue;
				}
				previous = c;
				i++;
			}
			return false;
		}

		/**
			Partial preprocessing of an assembled stage stream: resolves ONLY conditionals of the
			exact forms "#ifdef/#ifndef VERTEX_STAGE|FRAGMENT_STAGE" (with an optional "#else" and
			the matching "#endif"), keeping or dropping the enclosed lines according to the stage
			being built. Every other preprocessor conditional passes through textually untouched.
			Nesting works in both directions: an unknown conditional inside a resolved block is
			kept/dropped with the block, and a stage conditional inside an unknown conditional is
			still resolved in place (its truth does not depend on the outer condition). Any other
			preprocessor use of a stage macro (#if/#elif expressions, #define, #undef) is an
			error — the emitted sources contain no stage macros at all.
		*/
		bool ResolveStageConditionals(std::vector<SourceLine>& lines, bool vertexStage, Diagnostic& diag)
		{
			struct Cond
			{
				bool Stage;			// true = resolved stage conditional (its directive lines are removed)
				bool Keep;			// Stage conditionals only — the current branch is the active one
				bool SeenElse;
				std::int32_t Line;
			};

			// Directives are detected on a comment-stripped copy (a block comment could hide a '#')
			std::vector<SourceLine> stripped = lines;
			ShaderParser::StripComments(stripped);

			std::vector<SourceLine> output;
			output.reserve(lines.size());
			std::vector<Cond> stack;
			bool removed = false;		// The previous input line was removed (a directive or a dropped block line)

			auto dropping = [&stack]() {
				for (const Cond& c : stack) {
					if (c.Stage && !c.Keep) {
						return true;
					}
				}
				return false;
			};

			constexpr char OnlyIfdefForms[] = "VERTEX_STAGE/FRAGMENT_STAGE are resolved at compile time and support only the \"#ifdef\"/\"#ifndef\" forms";

			for (std::size_t index = 0; index < lines.size(); index++) {
				StringView bare = stripped[index].Text;
				const std::int32_t lineNumber = lines[index].Line;
				std::size_t begin = FindFirstNotOf(bare, " \t"_s);
				if (begin != Npos && bare[begin] == '#') {
					std::size_t p = begin + 1;
					while (p < bare.size() && IsSpace(bare[p])) {
						p++;
					}
					std::size_t nameBegin = p;
					while (p < bare.size() && IsIdentChar(bare[p])) {
						p++;
					}
					String name = Substr(bare, nameBegin, p - nameBegin);
					String rest = Substr(bare, p);

					if (name == "ifdef" || name == "ifndef") {
						String id = FirstIdentifier(rest);
						if (id == "VERTEX_STAGE" || id == "FRAGMENT_STAGE") {
							bool value = ((id == "VERTEX_STAGE") == vertexStage);
							if (name == "ifndef") {
								value = !value;
							}
							stack.push_back({ true, value, false, lineNumber });
							removed = true;
							continue;
						}
						stack.push_back({ false, true, false, lineNumber });
					} else if (name == "if") {
						if (ContainsStageMacro(rest)) {
							return Fail(diag, OnlyIfdefForms, lineNumber);
						}
						stack.push_back({ false, true, false, lineNumber });
					} else if (name == "elif") {
						if (stack.empty()) {
							return Fail(diag, "#elif without matching #if", lineNumber);
						}
						if (stack.back().Stage) {
							return Fail(diag, "#elif cannot follow #ifdef/#ifndef VERTEX_STAGE|FRAGMENT_STAGE (only #else and #endif can)", lineNumber);
						}
						if (ContainsStageMacro(rest)) {
							return Fail(diag, OnlyIfdefForms, lineNumber);
						}
					} else if (name == "else") {
						if (stack.empty()) {
							return Fail(diag, "#else without matching #if", lineNumber);
						}
						Cond& top = stack.back();
						if (top.Stage) {
							if (top.SeenElse) {
								return Fail(diag, "duplicate #else", lineNumber);
							}
							top.Keep = !top.Keep;
							top.SeenElse = true;
							removed = true;
							continue;
						}
					} else if (name == "endif") {
						if (stack.empty()) {
							return Fail(diag, "#endif without matching #if", lineNumber);
						}
						bool wasStage = stack.back().Stage;
						stack.pop_back();
						if (wasStage) {
							removed = true;
							continue;
						}
					} else if (name == "define" || name == "undef") {
						if (ContainsStageMacro(rest)) {
							return Fail(diag, "VERTEX_STAGE/FRAGMENT_STAGE cannot be defined or undefined - they are resolved at compile time and never defined in the emitted sources", lineNumber);
						}
					}
					// Other directives (#version, #extension, #pragma, ...) pass through
				}

				if (dropping()) {
					removed = true;
					continue;
				}
				// Collapse the blank separator a removed directive/block leaves behind
				if (removed && Trim(lines[index].Text).empty() && (output.empty() || Trim(output.back().Text).empty())) {
					continue;
				}
				removed = false;
				output.push_back(std::move(lines[index]));
			}

			if (!stack.empty()) {
				return Fail(diag, "unterminated #if/#ifdef/#ifndef", stack.back().Line);
			}
			lines = std::move(output);
			return true;
		}

		/**
			Partial preprocessing of an assembled stage stream (BuildStageSource): resolves ONLY
			conditionals of the exact forms "#ifdef/#ifndef SOFTWARE_RENDERER" (with an optional
			"#else" and the matching "#endif"), keeping or dropping the enclosed lines according
			to @p softwareRenderer - the backend the source is being built for. Every other
			preprocessor conditional passes through textually untouched (nesting works in both
			directions, exactly like ResolveStageConditionals), so the built sources never contain
			the SOFTWARE_RENDERER macro at all and shaders without such blocks come out byte-for-byte
			unchanged. Unlike the stage-macro resolver this runs on already assembled/validated
			streams and carries no diagnostics: on a malformed conditional structure the stream is
			left untouched (a leaked block is then caught by the generated-header staleness checks).
		*/
		void ResolveSoftwareRendererConditionals(std::vector<SourceLine>& lines, bool softwareRenderer)
		{
			struct Cond
			{
				bool Software;		// true = resolved SOFTWARE_RENDERER conditional (its directive lines are removed)
				bool Keep;			// Software conditionals only - the current branch is the active one
				bool SeenElse;
			};

			// Directives are detected on a comment-stripped copy (a block comment could hide a '#')
			std::vector<SourceLine> stripped = lines;
			ShaderParser::StripComments(stripped);

			std::vector<SourceLine> output;
			output.reserve(lines.size());
			std::vector<Cond> stack;
			bool removed = false;		// The previous input line was removed (a directive or a dropped block line)

			auto dropping = [&stack]() {
				for (const Cond& c : stack) {
					if (c.Software && !c.Keep) {
						return true;
					}
				}
				return false;
			};

			for (std::size_t index = 0; index < lines.size(); index++) {
				StringView bare = stripped[index].Text;
				std::size_t begin = FindFirstNotOf(bare, " \t"_s);
				if (begin != Npos && bare[begin] == '#') {
					std::size_t p = begin + 1;
					while (p < bare.size() && IsSpace(bare[p])) {
						p++;
					}
					std::size_t nameBegin = p;
					while (p < bare.size() && IsIdentChar(bare[p])) {
						p++;
					}
					String name = Substr(bare, nameBegin, p - nameBegin);
					String rest = Substr(bare, p);

					if (name == "ifdef" || name == "ifndef") {
						String id = FirstIdentifier(rest);
						if (id == "SOFTWARE_RENDERER") {
							bool value = softwareRenderer;
							if (name == "ifndef") {
								value = !value;
							}
							stack.push_back({ true, value, false });
							removed = true;
							continue;
						}
						stack.push_back({ false, true, false });
					} else if (name == "if") {
						stack.push_back({ false, true, false });
					} else if (name == "elif") {
						if (stack.empty() || stack.back().Software) {
							return;		// Malformed (or an unsupported #elif on a resolved conditional) - leave untouched
						}
					} else if (name == "else") {
						if (stack.empty()) {
							return;		// Malformed - leave untouched
						}
						Cond& top = stack.back();
						if (top.Software) {
							if (top.SeenElse) {
								return;	// Malformed - leave untouched
							}
							top.Keep = !top.Keep;
							top.SeenElse = true;
							removed = true;
							continue;
						}
					} else if (name == "endif") {
						if (stack.empty()) {
							return;		// Malformed - leave untouched
						}
						bool wasSoftware = stack.back().Software;
						stack.pop_back();
						if (wasSoftware) {
							removed = true;
							continue;
						}
					}
					// Other directives (#version, #extension, #pragma, ...) pass through
				}

				if (dropping()) {
					removed = true;
					continue;
				}
				// Collapse the blank separator a removed directive/block leaves behind
				if (removed && Trim(lines[index].Text).empty() && (output.empty() || Trim(output.back().Text).empty())) {
					continue;
				}
				removed = false;
				output.push_back(lines[index]);	// A copy - a malformed stream must leave "lines" fully intact
			}

			if (!stack.empty()) {
				return;					// Unterminated conditional - leave untouched
			}
			lines = std::move(output);
		}

		/** Returns true when @p s contains a whole-identifier occurrence of @p name (member accesses excluded) */
		bool ContainsIdentifier(StringView s, const char* name)
		{
			const std::size_t nameLength = std::strlen(name);
			std::size_t i = 0;
			char previous = '\0';
			while (i < s.size()) {
				char c = s[i];
				if (IsIdentStart(c) && !IsIdentChar(previous) && previous != '.') {
					std::size_t begin = i;
					while (i < s.size() && IsIdentChar(s[i])) {
						i++;
					}
					if (i - begin == nameLength && Substr(s, begin, nameLength) == name) {
						return true;
					}
					previous = s[i - 1];
					continue;
				}
				previous = c;
				i++;
			}
			return false;
		}

		/**
			Decides whether the "COLOR = vColor;" default of a lowered canvas fragment stage is
			provably dead, i.e. the fragment() body overwrites it before anything can observe it.
			Returns true only when the FIRST whole-identifier occurrence of COLOR in the
			(comment-stripped, already substituted) body is an unconditional (preprocessor-
			conditional depth 0, tracked across #if/#ifdef/#ifndef/#endif within the body), top-level (brace
			depth 0) FULL plain assignment - "COLOR" followed by a single "=" (no "==", no
			compound operators, no ".xyz"/"[i]" component targets) - whose right-hand side does
			not read COLOR back before the terminating ';', with no "return" token anywhere
			before it (a returning path would ship the default; "discard" is harmless and
			ignored). A COLOR occurrence anywhere in the shared user globals keeps the init
			(a helper function could observe it), as does a body with no COLOR occurrence at
			all (the default IS the documented output). Anything unexpected keeps the init.
		*/
		bool CanvasColorInitIsDead(const std::vector<SourceLine>& body, const std::vector<SourceLine>& globals)
		{
			{
				std::int32_t foundLine;
				if (FindIdentifier(globals, "COLOR", foundLine)) {
					return false;
				}
			}

			std::vector<SourceLine> stripped = body;
			ShaderParser::StripComments(stripped);

			std::int32_t braceDepth = 0;
			std::int32_t condDepth = 0;
			bool returnSeen = false;
			bool inAssignmentTail = false;		// Between the qualifying "COLOR =" and its terminating ';'

			for (const SourceLine& line : stripped) {
				StringView text = line.Text;
				std::size_t first = FindFirstNotOf(text, " \t"_s);
				if (first != Npos && text[first] == '#') {
					if (inAssignmentTail) {
						return false;	// A conditional splits the statement - conservative
					}
					std::size_t p = first + 1;
					while (p < text.size() && IsSpace(text[p])) {
						p++;
					}
					std::size_t nameBegin = p;
					while (p < text.size() && IsIdentChar(text[p])) {
						p++;
					}
					String name = Substr(text, nameBegin, p - nameBegin);
					if (name == "if" || name == "ifdef" || name == "ifndef") {
						condDepth++;
					} else if (name == "endif") {
						if (condDepth == 0) {
							return false;	// Unbalanced within the body - conservative
						}
						condDepth--;
					}
					// A directive could smuggle the identifiers into the body (e.g. through a #define) - conservative
					if (ContainsIdentifier(Substr(text, p), "COLOR")) {
						return false;
					}
					if (ContainsIdentifier(Substr(text, p), "return")) {
						returnSeen = true;
					}
					continue;
				}

				std::size_t i = 0;
				char previous = '\0';
				while (i < text.size()) {
					char c = text[i];
					if (IsIdentStart(c) && !IsIdentChar(previous) && previous != '.') {
						std::size_t begin = i;
						while (i < text.size() && IsIdentChar(text[i])) {
							i++;
						}
						std::size_t length = i - begin;
						if (length == 5 && Substr(text, begin, 5) == "COLOR"_s) {
							if (inAssignmentTail) {
								return false;	// The right-hand side reads the default back
							}
							if (returnSeen || braceDepth != 0 || condDepth != 0) {
								return false;
							}
							std::size_t p = i;
							while (p < text.size() && IsSpace(text[p])) {
								p++;
							}
							// '.', '[', compound/comparison operators, a read or a line break all disqualify
							if (p >= text.size() || text[p] != '=' || (p + 1 < text.size() && text[p + 1] == '=')) {
								return false;
							}
							inAssignmentTail = true;
							i = p + 1;
							previous = '=';
							continue;
						}
						if (length == 6 && Substr(text, begin, 6) == "return"_s) {
							returnSeen = true;
						}
						previous = text[i - 1];
						continue;
					}
					if (c == ';' && inAssignmentTail) {
						return true;	// The statement completed without reading COLOR - the default is dead
					}
					if (c == '{') {
						braceDepth++;
					} else if (c == '}') {
						braceDepth--;
						if (braceDepth < 0) {
							return false;
						}
					}
					previous = c;
					i++;
				}
			}
			return false;		// No qualifying occurrence (or an unterminated statement) - keep the init
		}

		/**
			Appends one lowered varying declaration. A backend-tagged varying (VaryingDecl::SwMode, from a
			global-scope SOFTWARE_RENDERER conditional) is re-wrapped in the matching directive so
			BuildStageSource resolves it per backend; an untagged one is emitted verbatim as before.
		*/
		void AppendVaryingDecl(std::vector<SourceLine>& lines, const VaryingDecl& varying, const char* prefix, const char* flatPrefix)
		{
			if (varying.SwMode == 1) {
				lines.push_back({ "#ifdef SOFTWARE_RENDERER", 0 });
			} else if (varying.SwMode == 2) {
				lines.push_back({ "#ifndef SOFTWARE_RENDERER", 0 });
			}
			lines.push_back({ String(varying.Flat ? flatPrefix : prefix) + varying.Declaration + ";"_s, varying.Line });
			if (varying.SwMode != 0) {
				lines.push_back({ "#endif", 0 });
			}
		}

		/** Builds the vertex stage of a lowered canvas_item document (default template or vertex() epilogue form) */
		void BuildCanvasVertexStage(const ParsedShader& src, bool batched, bool implicitTexture, ShaderDocument& document)
		{
			std::vector<SourceLine>& lines = document.VertexLines;
			AppendTemplate(lines, batched ? CanvasBatchedVsHead : CanvasSpriteVsHead);
			for (const VaryingDecl& varying : src.Varyings) {
				AppendVaryingDecl(lines, varying, "out ", "flat out ");
			}
			for (const AttributeDecl& attribute : src.Attributes) {
				lines.push_back({ FormatAttributeDecl(attribute.Declaration), attribute.Line });
			}
			lines.push_back({ "", 0 });
			if (batched) {
				lines.push_back({ "#define i block.instances[gl_VertexID / 6]", 0 });
				lines.push_back({ "", 0 });
			}

			if (!src.HasVertexBody) {
				AppendTemplate(lines, batched ? CanvasBatchedVsMain : CanvasSpriteVsMain);
				return;
			}

			// User globals are shared with the fragment stage; the custom vertex() body may reference them
			// (an implicit TEXTURE declaration behaves exactly like an explicit one written first)
			if (implicitTexture || !src.Globals.empty()) {
				if (implicitTexture) {
					lines.push_back({ "uniform sampler2D uTexture;", 0 });
				}
				for (const SourceLine& global : src.Globals) {
					lines.push_back(global);
				}
				lines.push_back({ "", 0 });
			}

			// The built-ins are locals of the generated main(); the user body is spliced verbatim
			// between the prologue and the epilogue ("return" inside vertex() is a parse error,
			// so the epilogue below always runs; redeclaring a prologue local is a GLSL error)
			const char* p = (batched ? "i." : "");
			lines.push_back({ "void main()", 0 });
			lines.push_back({ "{", 0 });
			lines.push_back({ "\tvec2 aPosition = "_s + StringView(batched ? CanvasBatchedCorner : CanvasSpriteCorner) + ";"_s, 0 });
			lines.push_back({ "\tvec2 VERTEX = vec2(aPosition.x * "_s + StringView(p) + "spriteSize.x, aPosition.y * "_s + StringView(p) + "spriteSize.y);"_s, 0 });
			lines.push_back({ "\tvec2 UV = vec2(aPosition.x * "_s + StringView(p) + "texRect.x + "_s + StringView(p) + "texRect.y, aPosition.y * "_s + StringView(p) + "texRect.z + "_s + StringView(p) + "texRect.w);"_s, 0 });
			lines.push_back({ "\tvec4 COLOR = "_s + StringView(p) + "color;"_s, 0 });
			lines.push_back({ "\thighp float PALETTE_OFFSET = "_s + StringView(p) + "palOffset;"_s, 0 });
			lines.push_back({ "", 0 });
			AppendEntryBody(lines, src.VertexBody);
			lines.push_back({ "", 0 });
			lines.push_back({ "\tgl_Position = uProjectionMatrix * uViewMatrix * "_s + StringView(p) + "modelMatrix * vec4(VERTEX, 0.0, 1.0);"_s, 0 });
			lines.push_back({ "\tvTexCoords = UV;", 0 });
			lines.push_back({ "\tvColor = COLOR;", 0 });
			lines.push_back({ "\tvPaletteOffset = PALETTE_OFFSET;", 0 });
			lines.push_back({ "}", 0 });
		}

		/** Builds the fragment stage of a lowered canvas_item document (prologue + "out vec4 COLOR;" + main() with the body spliced verbatim after the "COLOR = vColor;" default, which is omitted when provably dead) */
		void BuildCanvasFragmentStage(const ParsedShader& src, bool implicitTexture, ShaderDocument& document)
		{
			std::vector<SourceLine>& lines = document.FragmentLines;
			lines.push_back({ "#ifdef GL_ES", 0 });
			lines.push_back({ "precision "_s + src.FragmentPrecision + " float;"_s, 0 });
			lines.push_back({ "#endif", 0 });
			lines.push_back({ "", 0 });
			// The varyings come before the globals, so helper functions there can reference them
			// (through the substituted built-in names, e.g. PALETTE_OFFSET)
			lines.push_back({ "in vec2 vTexCoords;", 0 });
			lines.push_back({ "in vec4 vColor;", 0 });
			lines.push_back({ "in highp float vPaletteOffset;", 0 });
			for (const VaryingDecl& varying : src.Varyings) {
				AppendVaryingDecl(lines, varying, "in ", "flat in ");
			}
			lines.push_back({ "", 0 });
			// An implicit TEXTURE declaration takes the head of the globals — exactly where the
			// migrated files used to declare it explicitly, so the emitted text stays identical
			if (implicitTexture || !src.Globals.empty()) {
				if (implicitTexture) {
					lines.push_back({ "uniform sampler2D uTexture;", 0 });
				}
				for (const SourceLine& global : src.Globals) {
					lines.push_back(global);
				}
				lines.push_back({ "", 0 });
			}
			lines.push_back({ "out vec4 COLOR;", 0 });
			lines.push_back({ "", 0 });
			// COLOR is the fragment output variable itself — the body is spliced verbatim after
			// the instance-color default (that IS the built-in's semantic, bodies read COLOR),
			// with no epilogue, so an early "return" inside fragment() is safe. The default is
			// dropped when the body provably overwrites it before anything can observe it
			lines.push_back({ "void main() {", 0 });
			if (!CanvasColorInitIsDead(src.FragmentBody, src.Globals)) {
				lines.push_back({ "\tCOLOR = vColor;", 0 });
			}
			AppendEntryBody(lines, src.FragmentBody);
			lines.push_back({ "}", 0 });
			lines.push_back({ "", 0 });
		}

		/** Builds one lowered canvas_item document (the primary program or its batched twin) */
		bool BuildCanvasDocument(const ParsedShader& src, bool batched, bool implicitTexture, ShaderDocument& document, Diagnostic& diag)
		{
			document.ProgramName = (batched ? src.BatchedName : src.ProgramName);
			document.Variants = src.Variants;
			document.Textures = src.Textures;
			document.RenderModes = src.RenderModes;
			document.HasVertexStage = true;
			document.HasFragmentStage = true;
			// Cosmetic leading blank line before both stage sources
			document.Prelude.push_back({ "", 0 });
			BuildCanvasVertexStage(src, batched, implicitTexture, document);
			BuildCanvasFragmentStage(src, implicitTexture, document);
			// Stage conditionals are evaluated at assembly time, before unused-function elimination,
			// so declarations and helpers resolved away are simply absent from the emitted stage
			if (!ResolveStageConditionals(document.VertexLines, true, diag) ||
				!ResolveStageConditionals(document.FragmentLines, false, diag)) {
				return false;
			}
			EliminateUnusedFunctions(document.VertexLines);
			EliminateUnusedFunctions(document.FragmentLines);
			// Interface/uniform trimming and constant folding come after elimination, so references
			// inside removed helpers do not count and the folded text is the final emitted text
			TrimUnusedVaryings(document);
			TrimUnusedUniforms(document);
			FoldConstantExpressions(document.VertexLines);
			FoldConstantExpressions(document.FragmentLines);
			return true;
		}

		/** Builds the vertex stage of a custom-mode document — no template, vertex() becomes main() verbatim */
		void BuildCustomVertexStage(const ParsedShader& src, ShaderDocument& document)
		{
			std::vector<SourceLine>& lines = document.VertexLines;
			for (const AttributeDecl& attribute : src.Attributes) {
				lines.push_back({ FormatAttributeDecl(attribute.Declaration), attribute.Line });
			}
			if (!src.Attributes.empty()) {
				lines.push_back({ "", 0 });
			}
			for (const VaryingDecl& varying : src.Varyings) {
				AppendVaryingDecl(lines, varying, "out ", "flat out ");
			}
			if (!src.Varyings.empty()) {
				lines.push_back({ "", 0 });
			}
			if (!src.Globals.empty()) {
				for (const SourceLine& global : src.Globals) {
					lines.push_back(global);
				}
				lines.push_back({ "", 0 });
			}
			lines.push_back({ "void main()", 0 });
			lines.push_back({ "{", 0 });
			for (const SourceLine& bodyLine : src.VertexBody) {
				lines.push_back(bodyLine);
			}
			lines.push_back({ "}", 0 });
		}

		/** Builds the fragment stage of a custom-mode document (prologue + "out vec4 COLOR;" + main() wrapping the body verbatim) */
		void BuildCustomFragmentStage(const ParsedShader& src, ShaderDocument& document)
		{
			std::vector<SourceLine>& lines = document.FragmentLines;
			lines.push_back({ "#ifdef GL_ES", 0 });
			lines.push_back({ "precision "_s + src.FragmentPrecision + " float;"_s, 0 });
			lines.push_back({ "#endif", 0 });
			lines.push_back({ "", 0 });
			// The varyings come before the globals, so helper functions there can reference them
			for (const VaryingDecl& varying : src.Varyings) {
				AppendVaryingDecl(lines, varying, "in ", "flat in ");
			}
			if (!src.Varyings.empty()) {
				lines.push_back({ "", 0 });
			}
			if (!src.Globals.empty()) {
				for (const SourceLine& global : src.Globals) {
					lines.push_back(global);
				}
				lines.push_back({ "", 0 });
			}
			lines.push_back({ "out vec4 COLOR;", 0 });
			lines.push_back({ "", 0 });
			// COLOR is the fragment output variable itself — the body becomes main() verbatim,
			// with no default and no epilogue, so an early "return" inside fragment() is safe.
			// COLOR is undefined until written (like any GLSL output variable); custom-mode
			// bodies must assign it on every path
			lines.push_back({ "void main() {", 0 });
			AppendEntryBody(lines, src.FragmentBody);
			lines.push_back({ "}", 0 });
			lines.push_back({ "", 0 });
		}

		/** Builds the lowered custom-mode document — globals are shared verbatim by both stages, no built-in substitutions */
		bool BuildCustomDocument(const ParsedShader& src, ShaderDocument& document, Diagnostic& diag)
		{
			document.ProgramName = src.ProgramName;
			document.Variants = src.Variants;
			document.Textures = src.Textures;
			document.RenderModes = src.RenderModes;
			document.HasVertexStage = true;
			document.HasFragmentStage = true;
			// Cosmetic leading blank line before both stage sources
			document.Prelude.push_back({ "", 0 });
			BuildCustomVertexStage(src, document);
			BuildCustomFragmentStage(src, document);
			// Stage conditionals are evaluated at assembly time, before unused-function elimination,
			// so declarations and helpers resolved away are simply absent from the emitted stage
			if (!ResolveStageConditionals(document.VertexLines, true, diag) ||
				!ResolveStageConditionals(document.FragmentLines, false, diag)) {
				return false;
			}
			EliminateUnusedFunctions(document.VertexLines);
			EliminateUnusedFunctions(document.FragmentLines);
			// Interface/uniform trimming and constant folding come after elimination, so references
			// inside removed helpers do not count and the folded text is the final emitted text
			TrimUnusedVaryings(document);
			TrimUnusedUniforms(document);
			FoldConstantExpressions(document.VertexLines);
			FoldConstantExpressions(document.FragmentLines);
			return true;
		}
	}

	bool ShaderParser::ParseDocuments(StringView content, std::vector<ShaderDocument>& documents, Diagnostic& diag)
	{
		std::vector<SourceLine> lines;
		SplitLines(content, lines);
		std::vector<SourceLine> stripped = lines;
		StripComments(stripped);

		ParsedShader src;
		if (!ParseShaderSource(lines, stripped, src, diag)) {
			return false;
		}

		// The fragment output variable is COLOR itself ("out vec4 COLOR;") — the fragColor name
		// does not exist in generated code at all, so any reference to it is a leftover mistake
		{
			std::int32_t foundLine;
			if (FindIdentifier(src.FragmentBody, "fragColor", foundLine) || FindIdentifier(src.Globals, "fragColor", foundLine)) {
				return Fail(diag, "\"fragColor\" cannot be referenced here - write COLOR, it is the fragment output variable", foundLine);
			}
		}

		// A canvas vertex() body is spliced into the generated main() BEFORE the real epilogue
		// (gl_Position + varying stores), so a "return" there would skip it — reject it. The
		// fragment stage has no epilogue (COLOR is the output itself) and a custom-mode vertex()
		// becomes main() verbatim, so "return" is safe in both.
		{
			std::int32_t foundLine;
			if (src.CanvasItem && src.HasVertexBody && FindIdentifier(src.VertexBody, "return", foundLine)) {
				return Fail(diag, "\"return\" is not allowed in vertex() - the inlined epilogue must run; restructure with if/else", foundLine);
			}
		}

		if (!src.CanvasItem) {
			// Custom mode (the default) — one document, user identifiers pass through untouched
			documents.emplace_back();
			return BuildCustomDocument(src, documents.back(), diag);
		}

		// Implicit TEXTURE (canvas mode only): a document that references the TEXTURE built-in
		// without declaring uTexture gets "uniform sampler2D uTexture;" auto-declared at the head
		// of the fragment globals, plus texture unit 0 registered for it. Explicit declarations
		// (with or without a "texture_unit(N)" hint) win — no double declaration, no unit conflict;
		// a hint always sits on a declaration, so it can never race the implicit registration.
		// PALETTE/uTexturePalette stays fully explicit.
		bool implicitTexture = false;
		{
			std::int32_t foundLine;
			if ((FindIdentifier(src.FragmentBody, "TEXTURE", foundLine) ||
				FindIdentifier(src.VertexBody, "TEXTURE", foundLine) ||
				FindIdentifier(src.Globals, "TEXTURE", foundLine)) && !DeclaresUniform(src.Globals, "uTexture")) {
				implicitTexture = true;
				TextureDirective directive;
				directive.Name = "uTexture";
				directive.Unit = 0;
				src.Textures.push_back(std::move(directive));
			}
		}

		if (!LowerCanvasFragmentBody(src.FragmentBody, diag)) {
			return false;
		}
		// Globals get the same substitutions, so helper functions can read the fragment built-ins
		// (in the vertex stage the substituted names resolve to the "out" varyings and compile too)
		if (!LowerCanvasFragmentBody(src.Globals, diag)) {
			return false;
		}

		documents.emplace_back();
		if (!BuildCanvasDocument(src, false, implicitTexture, documents.back(), diag)) {
			return false;
		}
		if (!src.BatchedName.empty()) {
			documents.emplace_back();
			if (!BuildCanvasDocument(src, true, implicitTexture, documents.back(), diag)) {
				return false;
			}
		}
		return true;
	}

	// --- Preprocessor -------------------------------------------------------------------------------

	void Preprocessor::Define(StringView name, StringView body)
	{
		Macro m;
		m.Body = body;
		m.FunctionLike = false;
		_macros[name] = std::move(m);
	}

	bool Preprocessor::IsDefined(StringView name) const
	{
		if (name == "BATCH_SIZE") {
			// Symbolic constant — always treated as defined
			return true;
		}
		return (_macros.find(name) != _macros.end());
	}

	bool Preprocessor::TryGetMacroBody(StringView name, String& body) const
	{
		auto it = _macros.find(name);
		if (it == _macros.end() || it->second.FunctionLike) {
			return false;
		}
		body = it->second.Body;
		return true;
	}

	bool Preprocessor::EvaluateExpression(StringView expression, std::int32_t line, std::int32_t depth, std::int64_t& value, Diagnostic& diag) const
	{
		ExprEvaluator evaluator(*this, line, depth, diag);
		return evaluator.Evaluate(expression, value);
	}

	String Preprocessor::ExpandMacros(StringView text) const
	{
		String current = text;
		for (std::int32_t pass = 0; pass < MaxExpansionPasses; pass++) {
			Array<char> next;
			arrayReserve(next, current.size());
			bool changed = false;
			std::size_t i = 0;
			char previous = '\0';
			while (i < current.size()) {
				char c = current[i];
				// Identifier must not continue a preceding number/identifier (e.g. the "f" in "1.0f")
				if (IsIdentStart(c) && !IsIdentChar(previous) && previous != '.') {
					std::size_t begin = i;
					while (i < current.size() && IsIdentChar(current[i])) {
						i++;
					}
					StringView id = Substr(current, begin, i - begin);
					auto it = _macros.find(id);
					if (it != _macros.end() && !it->second.FunctionLike) {
						arrayAppend(next, StringView{it->second.Body});
						changed = true;
						previous = ' ';
					} else {
						arrayAppend(next, id);
						previous = id[id.size() - 1];
					}
				} else {
					arrayAppend(next, c);
					previous = c;
					i++;
				}
			}
			current = String{next.data(), next.size()};
			if (!changed) {
				break;
			}
		}
		return current;
	}

	bool Preprocessor::Run(const std::vector<SourceLine>& input, std::vector<SourceLine>& output, Diagnostic& diag)
	{
		struct Cond
		{
			bool Taken;
			bool AnyTaken;
			bool SeenElse;
			std::int32_t Line;
		};
		std::vector<Cond> stack;

		auto allTaken = [](const std::vector<Cond>& s, std::size_t count) {
			for (std::size_t j = 0; j < count; j++) {
				if (!s[j].Taken) {
					return false;
				}
			}
			return true;
		};

		for (const SourceLine& line : input) {
			std::size_t begin = FindFirstNotOf(line.Text, " \t"_s);
			if (begin != Npos && line.Text[begin] == '#') {
				std::size_t p = begin + 1;
				while (p < line.Text.size() && IsSpace(line.Text[p])) {
					p++;
				}
				std::size_t nameBegin = p;
				while (p < line.Text.size() && IsIdentChar(line.Text[p])) {
					p++;
				}
				String name = Substr(line.Text, nameBegin, p - nameBegin);
				String rest = Trim(Substr(line.Text, p));

				if (name == "if" || name == "ifdef" || name == "ifndef") {
					bool parentActive = allTaken(stack, stack.size());
					bool value = false;
					if (parentActive) {
						if (name == "if") {
							std::int64_t v;
							if (!EvaluateExpression(rest, line.Line, 0, v, diag)) {
								return false;
							}
							value = (v != 0);
						} else {
							String id = FirstIdentifier(rest);
							if (id.empty()) {
								return Fail(diag, "#"_s + name + " requires an identifier"_s, line.Line);
							}
							value = IsDefined(id);
							if (name == "ifndef") {
								value = !value;
							}
						}
					}
					Cond c;
					c.Taken = value;
					c.AnyTaken = value;
					c.SeenElse = false;
					c.Line = line.Line;
					stack.push_back(c);
				} else if (name == "elif") {
					if (stack.empty()) {
						return Fail(diag, "#elif without matching #if", line.Line);
					}
					Cond& top = stack.back();
					if (top.SeenElse) {
						return Fail(diag, "#elif after #else", line.Line);
					}
					bool parentActive = allTaken(stack, stack.size() - 1);
					if (parentActive && !top.AnyTaken) {
						std::int64_t v;
						if (!EvaluateExpression(rest, line.Line, 0, v, diag)) {
							return false;
						}
						top.Taken = (v != 0);
						top.AnyTaken = top.Taken;
					} else {
						top.Taken = false;
					}
				} else if (name == "else") {
					if (stack.empty()) {
						return Fail(diag, "#else without matching #if", line.Line);
					}
					Cond& top = stack.back();
					if (top.SeenElse) {
						return Fail(diag, "duplicate #else", line.Line);
					}
					bool parentActive = allTaken(stack, stack.size() - 1);
					top.Taken = (parentActive && !top.AnyTaken);
					top.AnyTaken = true;
					top.SeenElse = true;
				} else if (name == "endif") {
					if (stack.empty()) {
						return Fail(diag, "#endif without matching #if", line.Line);
					}
					stack.pop_back();
				} else if (name == "define") {
					if (allTaken(stack, stack.size())) {
						std::size_t q = 0;
						while (q < rest.size() && IsSpace(rest[q])) {
							q++;
						}
						std::size_t idBegin = q;
						while (q < rest.size() && IsIdentChar(rest[q])) {
							q++;
						}
						String macroName = Substr(rest, idBegin, q - idBegin);
						if (!IsIdentifier(macroName)) {
							return Fail(diag, "#define requires an identifier", line.Line);
						}
						Macro m;
						if (q < rest.size() && rest[q] == '(') {
							// Function-like macros are recorded (for defined()) but never expanded
							m.FunctionLike = true;
							m.Body = Substr(rest, q);
						} else {
							m.FunctionLike = false;
							m.Body = Trim(Substr(rest, q));
						}
						_macros[macroName] = std::move(m);
					}
				} else if (name == "undef") {
					if (allTaken(stack, stack.size())) {
						String id = FirstIdentifier(rest);
						if (id.empty()) {
							return Fail(diag, "#undef requires an identifier", line.Line);
						}
						_macros.erase(id);
					}
				} else {
					// #version, #extension, #pragma, #line, ... — dropped from the reflection stream
				}
				continue;
			}

			if (allTaken(stack, stack.size())) {
				SourceLine expanded;
				expanded.Text = ExpandMacros(line.Text);
				expanded.Line = line.Line;
				output.push_back(std::move(expanded));
			}
		}

		if (!stack.empty()) {
			return Fail(diag, "unterminated #if/#ifdef/#ifndef", stack.back().Line);
		}
		return true;
	}
	String ShaderParser::DirectoryOf(StringView path)
	{
		std::size_t pos = FindLastOf(path, "/\\"_s);
		if (pos == Npos) {
			return "."_s;
		}
		return Substr(path, 0, pos);
	}

	bool ShaderParser::ExpandIncludes(String& content, StringView baseDir, const FileReader& reader, std::int32_t depth, String& error)
	{
		if (depth > 8) {
			error = "include depth limit exceeded (a cycle between included files?)";
			return false;
		}

		Array<char> result;
		arrayReserve(result, content.size());

		std::size_t lineStart = 0;
		while (lineStart <= content.size()) {
			std::size_t lineEnd = Find(content, '\n', lineStart);
			const bool lastLine = (lineEnd == Npos);
			StringView line = Substr(content, lineStart, lastLine ? Npos : lineEnd - lineStart + 1);

			std::size_t i = FindFirstNotOf(line, " \t"_s);
			bool isInclude = false;
			if (i != Npos && line[i] == '#') {
				std::size_t k = FindFirstNotOf(line, " \t"_s, i + 1);
				if (k != Npos && Substr(line, k, 7) == "include"_s &&
					(k + 7 >= line.size() || !IsIdentChar(line[k + 7]))) {
					isInclude = true;
					std::size_t openQuote = Find(line, '"', k + 7);
					std::size_t closeQuote = (openQuote == Npos ? Npos : Find(line, '"', openQuote + 1));
					if (closeQuote == Npos) {
						error = "malformed include directive, expected: #include \"path\"";
						return false;
					}
					StringView relPath = Substr(line, openQuote + 1, closeQuote - openQuote - 1);
					String fullPath = baseDir + "/"_s + relPath;
					String included;
					if (!reader(fullPath, included)) {
						error = "cannot read included file \""_s + fullPath + "\""_s;
						return false;
					}
					if (!ExpandIncludes(included, DirectoryOf(fullPath), reader, depth + 1, error)) {
						return false;
					}
					arrayAppend(result, included);
					if (!included.empty() && included[included.size() - 1] != '\n') {
						arrayAppend(result, '\n');
					}
				}
			}
			if (!isInclude) {
				arrayAppend(result, line);
			}

			if (lastLine) {
				break;
			}
			lineStart = lineEnd + 1;
		}

		content = String{result.data(), result.size()};
		return true;
	}

	String ShaderParser::BuildStageSource(const ShaderDocument& document, bool vertexStage, StringView define, bool softwareRenderer)
	{
		const std::vector<SourceLine>& stage = (vertexStage ? document.VertexLines : document.FragmentLines);

		// Resolve "#ifdef/#ifndef SOFTWARE_RENDERER" blocks according to the backend the source is
		// built for (see the header comment). Gated on a quick reference scan so every document
		// without such a block keeps the verbatim fast path (and provably identical output).
		auto referencesMacro = [](const std::vector<SourceLine>& lines) {
			for (const SourceLine& line : lines) {
				if (line.Text.contains("SOFTWARE_RENDERER"_s)) {
					return true;
				}
			}
			return false;
		};
		std::vector<SourceLine> resolved;
		bool useResolved = (referencesMacro(document.Prelude) || referencesMacro(stage));
		if (useResolved) {
			resolved.reserve(document.Prelude.size() + stage.size());
			resolved.insert(resolved.end(), document.Prelude.begin(), document.Prelude.end());
			resolved.insert(resolved.end(), stage.begin(), stage.end());
			ResolveSoftwareRendererConditionals(resolved, softwareRenderer);
		}

		Array<char> out;
		if (!define.empty()) {
			arrayAppend(out, "#define "_s);
			arrayAppend(out, define);
			arrayAppend(out, " (1)\n"_s);
		}
		arrayAppend(out, "#line 1\n"_s);
		if (useResolved) {
			for (const SourceLine& line : resolved) {
				arrayAppend(out, line.Text);
				arrayAppend(out, '\n');
			}
		} else {
			for (const SourceLine& line : document.Prelude) {
				arrayAppend(out, line.Text);
				arrayAppend(out, '\n');
			}
			for (const SourceLine& line : stage) {
				arrayAppend(out, line.Text);
				arrayAppend(out, '\n');
			}
		}
		return String{out.data(), out.size()};
	}
}