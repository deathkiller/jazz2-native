#include "Essl100.h"

#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>

using namespace Death::Containers::Literals;

namespace ShaderCompiler
{
	namespace
	{
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

		constexpr std::size_t Npos = ~std::size_t{0};

		/** Standard substr(pos, count) semantics (clamps count to the available length; returns a view) */
		StringView Substr(StringView s, std::size_t pos, std::size_t count = Npos)
		{
			std::size_t size = s.size();
			if (pos > size) {
				pos = size;
			}
			std::size_t avail = size - pos;
			if (count > avail) {
				count = avail;
			}
			return s.slice(pos, pos + count);
		}

		/** Standard find(char, pos) semantics (returns Npos when not found) */
		std::size_t Find(StringView s, char c, std::size_t pos = 0)
		{
			for (std::size_t size = s.size(); pos < size; pos++) {
				if (s[pos] == c) {
					return pos;
				}
			}
			return Npos;
		}

		/** Standard find_first_not_of(set, pos) semantics (returns Npos when every character is in the set) */
		std::size_t FindFirstNotOf(StringView s, StringView set, std::size_t pos = 0)
		{
			for (std::size_t size = s.size(); pos < size; pos++) {
				if (!set.contains(s[pos])) {
					return pos;
				}
			}
			return Npos;
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

		/** Leading whitespace of @p s (empty when the line has none or is all whitespace) */
		String LeadingWhitespace(StringView s)
		{
			std::size_t i = 0;
			while (i < s.size() && IsSpace(s[i])) {
				i++;
			}
			return Substr(s, 0, i);
		}

		/** Net brace balance ('{' minus '}') on a comment-free code string */
		int CountBraces(StringView code)
		{
			int depth = 0;
			for (char c : code) {
				if (c == '{') {
					depth++;
				} else if (c == '}') {
					depth--;
				}
			}
			return depth;
		}

		/**
			Splits @p source on '\n', keeping a trailing empty segment when the source ends with a
			newline, so JoinLines() reproduces the input byte-for-byte (line insert/remove aside).
		*/
		std::vector<String> SplitLines(StringView source)
		{
			std::vector<String> lines;
			std::size_t start = 0;
			while (true) {
				std::size_t nl = Find(source, '\n', start);
				if (nl == Npos) {
					lines.push_back(String{Substr(source, start)});
					break;
				}
				lines.push_back(String{Substr(source, start, nl - start)});
				start = nl + 1;
			}
			return lines;
		}

		String JoinLines(const std::vector<String>& lines)
		{
			Array<char> out;
			for (std::size_t i = 0; i < lines.size(); i++) {
				arrayAppend(out, lines[i]);
				if (i + 1 < lines.size()) {
					arrayAppend(out, '\n');
				}
			}
			return String{out.data(), out.size()};
		}

		/** Comment-free copy of @p lines (reuses the tool's block/line comment stripper; columns preserved) */
		std::vector<String> StripComments(const std::vector<String>& lines)
		{
			std::vector<SourceLine> tmp;
			tmp.reserve(lines.size());
			for (const String& line : lines) {
				tmp.push_back({ line, 0 });
			}
			ShaderParser::StripComments(tmp);
			std::vector<String> out;
			out.reserve(tmp.size());
			for (const SourceLine& line : tmp) {
				out.push_back(line.Text);
			}
			return out;
		}

		/** First whole-identifier occurrence of @p name across the comment-free @p stripped lines (1-based line, or -1) */
		int FindIdentifierLine(const std::vector<String>& stripped, const char* name)
		{
			const std::size_t nameLength = std::char_traits<char>::length(name);
			for (std::size_t li = 0; li < stripped.size(); li++) {
				const String& text = stripped[li];
				char previous = '\0';
				std::size_t i = 0;
				while (i < text.size()) {
					char c = text[i];
					if (IsIdentStart(c) && !IsIdentChar(previous)) {
						std::size_t begin = i;
						while (i < text.size() && IsIdentChar(text[i])) {
							i++;
						}
						if (i - begin == nameLength && Substr(text, begin, nameLength) == StringView{name}) {
							return static_cast<int>(li) + 1;
						}
						previous = text[i - 1];
						continue;
					}
					previous = c;
					i++;
				}
			}
			return -1;
		}

		/** The preprocessor keyword of a comment-free line (e.g. "ifdef"), empty if the line is not a directive */
		String DirectiveWord(StringView code, String& arg)
		{
			arg = {};
			std::size_t i = FindFirstNotOf(code, " \t"_s);
			if (i == Npos || code[i] != '#') {
				return String{};
			}
			i++;
			while (i < code.size() && IsSpace(code[i])) {
				i++;
			}
			std::size_t begin = i;
			while (i < code.size() && IsIdentChar(code[i])) {
				i++;
			}
			String word = Substr(code, begin, i - begin);
			// Capture the first following identifier as the argument (used for "#ifdef GL_ES")
			while (i < code.size() && IsSpace(code[i])) {
				i++;
			}
			std::size_t argBegin = i;
			while (i < code.size() && IsIdentChar(code[i])) {
				i++;
			}
			arg = Substr(code, argBegin, i - argBegin);
			return word;
		}

		/**
			Unwraps "#ifdef GL_ES ... [#else ...] #endif" blocks: keeps the GL_ES-defined branch and
			drops the guard directives (GL_ES is predefined under "#version 100"). Nesting-aware — a
			non-GL_ES "#if*" inside the block is preserved with its own "#endif".
		*/
		std::vector<String> UnwrapGlEs(const std::vector<String>& lines)
		{
			std::vector<String> stripped = StripComments(lines);
			std::vector<String> result;
			result.reserve(lines.size());
			std::size_t i = 0;
			while (i < lines.size()) {
				String arg;
				String word = DirectiveWord(stripped[i], arg);
				if (word == "ifdef" && arg == "GL_ES") {
					// Find the matching #endif and any top-level #else
					int depth = 1;
					std::size_t elseIdx = Npos;
					std::size_t j = i + 1;
					for (; j < lines.size(); j++) {
						String innerArg;
						String innerWord = DirectiveWord(stripped[j], innerArg);
						if (innerWord == "if" || innerWord == "ifdef" || innerWord == "ifndef") {
							depth++;
						} else if (innerWord == "endif") {
							depth--;
							if (depth == 0) {
								break;
							}
						} else if (innerWord == "else" && depth == 1) {
							elseIdx = j;
						}
					}
					// Keep the then-branch [i+1, end) and drop the else-branch and all directives
					std::size_t end = (elseIdx != Npos ? elseIdx : j);
					for (std::size_t k = i + 1; k < end; k++) {
						result.push_back(lines[k]);
					}
					i = (j < lines.size() ? j + 1 : lines.size());
					continue;
				}
				result.push_back(lines[i]);
				i++;
			}
			return result;
		}

		/**
			If the comment-free @p code is a global interface declaration
			("[flat] [layout(...)] in|out <tail>;"), sets @p keyword to "in"/"out" and @p tail to the
			part after the keyword (e.g. "vec4 aColor;") and returns true.
		*/
		bool ParseInterfaceDecl(StringView code, String& keyword, String& tail)
		{
			String s = Trim(code);
			if (s.empty() || s.back() != ';') {
				return false;
			}
			std::size_t i = 0;
			auto skipSpace = [&]() { while (i < s.size() && IsSpace(s[i])) i++; };
			auto wordAt = [&](std::size_t p) {
				std::size_t begin = p;
				while (p < s.size() && IsIdentChar(s[p])) p++;
				return Substr(s, begin, p - begin);
			};

			skipSpace();
			// Optional "flat" interpolation qualifier (no ES2 equivalent — dropped)
			if (wordAt(i) == "flat") {
				i += 4;
				skipSpace();
			}
			// Optional "layout(...)" qualifier (no ES2 equivalent — dropped)
			if (wordAt(i) == "layout") {
				i += 6;
				skipSpace();
				if (i >= s.size() || s[i] != '(') {
					return false;
				}
				int parens = 0;
				while (i < s.size()) {
					if (s[i] == '(') {
						parens++;
					} else if (s[i] == ')') {
						parens--;
						i++;
						if (parens == 0) {
							break;
						}
						continue;
					}
					i++;
				}
				skipSpace();
			}
			String kw = wordAt(i);
			if (kw != "in" && kw != "out") {
				return false;
			}
			i += kw.size();
			skipSpace();
			String rest = Trim(Substr(s, i));
			if (rest.empty()) {
				return false;
			}
			keyword = kw;
			tail = rest;
			return true;
		}

		/** True when the trimmed comment-free @p code is the "void main(" signature line */
		bool IsMainSignature(StringView code)
		{
			String s = Trim(code);
			std::size_t i = 0;
			auto wordAt = [&](std::size_t p) {
				std::size_t begin = p;
				while (p < s.size() && IsIdentChar(s[p])) p++;
				return Substr(s, begin, p - begin);
			};
			if (wordAt(i) != "void") {
				return false;
			}
			i += 4;
			while (i < s.size() && IsSpace(s[i])) {
				i++;
			}
			if (wordAt(i) != "main") {
				return false;
			}
			i += 4;
			while (i < s.size() && IsSpace(s[i])) {
				i++;
			}
			return (i < s.size() && s[i] == '(');
		}

		/**
			Inserts "gl_FragColor = COLOR; " before every whole-identifier "return" that is immediately
			followed (in the comment-free @p code) by ';'. Edits @p original at the same columns
			(StripComments preserves them), right-to-left so earlier match positions stay valid.
		*/
		String RetargetReturns(StringView original, StringView code)
		{
			std::vector<std::size_t> insertAt;
			char previous = '\0';
			std::size_t i = 0;
			while (i < code.size()) {
				char c = code[i];
				if (IsIdentStart(c) && !IsIdentChar(previous)) {
					std::size_t begin = i;
					while (i < code.size() && IsIdentChar(code[i])) {
						i++;
					}
					if (i - begin == 6 && Substr(code, begin, 6) == "return"_s) {
						std::size_t j = i;
						while (j < code.size() && IsSpace(code[j])) {
							j++;
						}
						if (j < code.size() && code[j] == ';') {
							insertAt.push_back(begin);
						}
					}
					previous = code[i - 1];
					continue;
				}
				previous = c;
				i++;
			}
			String result = original;
			for (auto it = insertAt.rbegin(); it != insertAt.rend(); ++it) {
				if (*it <= result.size()) {
					result = result.prefix(*it) + "gl_FragColor = COLOR; "_s + result.exceptPrefix(*it);
				}
			}
			return result;
		}

		/** Comment-aware whole-identifier rewrite of texture()/textureLod() to their ES2 spellings */
		void RewriteTextureCalls(std::vector<String>& lines)
		{
			bool inBlockComment = false;
			for (String& line : lines) {
				Array<char> out;
				arrayReserve(out, line.size() + 8);
				std::size_t i = 0;
				char previous = '\0';
				while (i < line.size()) {
					char c = line[i];
					if (inBlockComment) {
						if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
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
					if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
						arrayAppend(out, Substr(line, i));
						break;
					}
					if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
						arrayAppend(out, "/*"_s);
						i += 2;
						inBlockComment = true;
						previous = '*';
						continue;
					}
					if (IsIdentStart(c) && !IsIdentChar(previous)) {
						std::size_t begin = i;
						while (i < line.size() && IsIdentChar(line[i])) {
							i++;
						}
						String id = Substr(line, begin, i - begin);
						if (id == "texture") {
							arrayAppend(out, "texture2D"_s);
						} else if (id == "textureLod") {
							arrayAppend(out, "texture2DLod"_s);
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
				line = String{out.data(), out.size()};
			}
		}
	}

	namespace
	{
		/** First index of @p needle in @p s at or after @p pos (Npos when absent) */
		std::size_t FindStr(StringView s, StringView needle, std::size_t pos = 0)
		{
			std::size_t n = needle.size();
			if (n == 0 || n > s.size()) {
				return Npos;
			}
			for (std::size_t i = pos; i + n <= s.size(); i++) {
				std::size_t j = 0;
				while (j < n && s[i + j] == needle[j]) {
					j++;
				}
				if (j == n) {
					return i;
				}
			}
			return Npos;
		}

		/** Every occurrence of @p from in @p s replaced by @p to */
		String ReplaceAll(StringView s, StringView from, StringView to)
		{
			if (from.empty()) {
				return String{s.data(), s.size()};
			}
			Array<char> out;
			std::size_t i = 0;
			while (i < s.size()) {
				std::size_t p = FindStr(s, from, i);
				if (p == Npos) {
					arrayAppend(out, s.exceptPrefix(i));
					break;
				}
				arrayAppend(out, s.slice(i, p));
				arrayAppend(out, to);
				i = p + from.size();
			}
			return String{out.data(), out.size()};
		}

		/**
			Rewrites the engine's gl_VertexID quad-synthesis expressions into reads of the ES2 vertex
			attributes the runtime supplies (a static per-vertex [0,1]² corner, and a per-vertex instance
			index for the batched six-vertices-per-sprite path). Sets @p usedCorner / @p usedInstance so
			the caller declares only the attributes that end up referenced.

			ES2 has no gl_VertexID and ESSL 100 has no integer bit/modulo operators, so the corner cannot be
			recomputed in-shader. Every single-quad formula is a function of the two terms float(id>>1) and
			float(id%2); substituting them with (1 - aQuadCorner.x) and aQuadCorner.y reproduces the exact
			corner of any of them (plain sprite, Lighting's 0.5-offset, TouchCircle) after constant folding,
			given the runtime supplies aQuadCorner = {(1,0),(1,1),(0,0),(0,1)} for the 4-vertex strip. The
			batched corner + instance index are fixed expressions replaced wholesale.
		*/
		String RewriteVertexId(StringView src, bool& usedCorner, bool& usedInstance)
		{
			String text{src.data(), src.size()};
			// Batched per-instance index (before the batched corner terms, which also contain gl_VertexID)
			if (FindStr(text, "gl_VertexID / 6"_s) != Npos) {
				text = ReplaceAll(text, "gl_VertexID / 6"_s, "int(aInstanceIndex)"_s);
				usedInstance = true;
			}
			// Batched six-vertex corner terms (two triangles). Both forms in use — "1.0 - <term>" (sprites)
			// and "-0.5 + <term>" (BatchedLighting) — are functions of these, so substituting the terms with
			// (1 - aQuadCorner.{x,y}) reproduces either corner after folding, given the runtime's batched
			// aQuadCorner = {(1,1),(0,1),(0,0),(0,0),(1,0),(1,1)}.
			if (FindStr(text, "float(((gl_VertexID + 2) / 3) % 2)"_s) != Npos) {
				text = ReplaceAll(text, "float(((gl_VertexID + 2) / 3) % 2)"_s, "(1.0 - aQuadCorner.x)"_s);
				usedCorner = true;
			}
			if (FindStr(text, "float(((gl_VertexID + 1) / 3) % 2)"_s) != Npos) {
				text = ReplaceAll(text, "float(((gl_VertexID + 1) / 3) % 2)"_s, "(1.0 - aQuadCorner.y)"_s);
				usedCorner = true;
			}
			// Single-quad corner terms
			if (FindStr(text, "float(gl_VertexID >> 1)"_s) != Npos) {
				text = ReplaceAll(text, "float(gl_VertexID >> 1)"_s, "(1.0 - aQuadCorner.x)"_s);
				usedCorner = true;
			}
			if (FindStr(text, "float(gl_VertexID % 2)"_s) != Npos) {
				text = ReplaceAll(text, "float(gl_VertexID % 2)"_s, "aQuadCorner.y"_s);
				usedCorner = true;
			}
			// Batched-mesh instance index: "uint aMeshIndex" is an integer vertex attribute, which ES2 forbids —
			// the declaration is remapped to "float" by the interface rewrite (MapEs2AttributeType), so wrap its
			// array-index uses in int() here so "instances[aMeshIndex]" stays a valid integer index under ES2.
			if (FindStr(text, "[aMeshIndex]"_s) != Npos) {
				text = ReplaceAll(text, "[aMeshIndex]"_s, "[int(aMeshIndex)]"_s);
			}
			return text;
		}

		/** Maps an ES2-illegal integer vertex-attribute type (int/uint/ivecN/uvecN) to its float equivalent */
		String MapEs2AttributeType(StringView tail)
		{
			// @p tail is the declaration after the "attribute" keyword, e.g. "uint aMeshIndex;" -> "float aMeshIndex;"
			static const struct { const char* From; const char* To; } Map[] = {
				{ "uint ", "float " }, { "int ", "float " },
				{ "uvec2 ", "vec2 " }, { "ivec2 ", "vec2 " },
				{ "uvec3 ", "vec3 " }, { "ivec3 ", "vec3 " },
				{ "uvec4 ", "vec4 " }, { "ivec4 ", "vec4 " },
			};
			for (const auto& m : Map) {
				std::size_t n = std::char_traits<char>::length(m.From);
				if (tail.size() >= n && Substr(tail, 0, n) == StringView{m.From}) {
					return StringView{m.To} + tail.exceptPrefix(n);
				}
			}
			return String{tail.data(), tail.size()};
		}

		/** 1-based index of the first comment-free line containing the operator char @p op, or -1 */
		int FindCharLine(const std::vector<String>& lines, char op)
		{
			for (std::size_t i = 0; i < lines.size(); i++) {
				if (Find(lines[i], op) != Npos) {
					return static_cast<int>(i) + 1;
				}
			}
			return -1;
		}

		/** 1-based index of the first comment-free line with an f/F-suffixed float literal (e.g. "1.0f"), or -1 */
		int FindFloatSuffixLine(const std::vector<String>& lines)
		{
			for (std::size_t li = 0; li < lines.size(); li++) {
				const String& s = lines[li];
				for (std::size_t i = 1; i < s.size(); i++) {
					char c = s[i];
					if ((c == 'f' || c == 'F') && IsDigit(s[i - 1])) {
						char next = (i + 1 < s.size() ? s[i + 1] : '\0');
						if (!IsIdentChar(next)) {
							return static_cast<int>(li) + 1;
						}
					}
				}
			}
			return -1;
		}

		/** Emits one std140 block member as a plain ES2 uniform, moving any "[size]" from the type to the name */
		String Std140MemberToUniform(StringView leading, StringView member)
		{
			// @p member is comment-free, trimmed and has no trailing ';'
			std::size_t lastSpace = Npos;
			for (std::size_t i = member.size(); i > 0; i--) {
				char c = member[i - 1];
				if (c == ' ' || c == '\t') {
					lastSpace = i - 1;
					break;
				}
			}
			if (lastSpace == Npos) {
				return leading + "uniform "_s + member + ";"_s;
			}
			String lhs = Trim(member.prefix(lastSpace));
			String name = Trim(member.exceptPrefix(lastSpace + 1));
			std::size_t bracket = FindStr(lhs, "["_s);
			if (bracket != Npos) {
				// "Instance[BATCH_SIZE] instances" -> "uniform Instance instances[BATCH_SIZE]"
				String type = Trim(lhs.prefix(bracket));
				StringView size = lhs.exceptPrefix(bracket);		// "[BATCH_SIZE]"
				return leading + "uniform "_s + type + " "_s + name + size + ";"_s;
			}
			return leading + "uniform "_s + lhs + " "_s + name + ";"_s;
		}

		/**
			Lowers every "layout(std140) uniform <Block> { ... } [instance];" to ES2 (which has no UBOs):
			each scalar/vector/matrix member becomes a loose "uniform"; a struct-array member
			("Instance[BATCH_SIZE] instances;") becomes a "uniform Instance instances[BATCH_SIZE];" array.
			Preprocessor lines inside the block (the BATCH_SIZE guard) are preserved. The block's instance
			name, if any, is stripped from every "<instance>.<member>" access so the members are read
			directly / through the array.
		*/
		void RewriteStd140Blocks(std::vector<String>& lines)
		{
			std::vector<String> stripped = StripComments(lines);
			std::vector<String> result;
			result.reserve(lines.size());
			std::vector<String> instanceNames;
			std::size_t i = 0;
			while (i < lines.size()) {
				const String& s = stripped[i];
				bool isBlockStart = (FindStr(s, "std140"_s) != Npos && FindStr(s, "uniform"_s) != Npos);
				if (!isBlockStart) {
					result.push_back(lines[i]);
					i++;
					continue;
				}
				// Opening brace: on this line or a following one
				std::size_t openLine = i;
				while (openLine < lines.size() && FindStr(stripped[openLine], "{"_s) == Npos) {
					openLine++;
				}
				if (openLine >= lines.size()) {
					result.push_back(lines[i]);
					i++;
					continue;
				}
				// Matching closing brace
				int depth = 0;
				std::size_t closeLine = openLine;
				bool found = false;
				for (std::size_t j = openLine; j < lines.size(); j++) {
					depth += CountBraces(stripped[j]);
					if (depth <= 0) {
						closeLine = j;
						found = true;
						break;
					}
				}
				if (!found) {
					result.push_back(lines[i]);
					i++;
					continue;
				}
				// Optional instance name after '}' on the closing line
				{
					StringView cs = stripped[closeLine];
					std::size_t bp = FindStr(cs, "}"_s);
					if (bp != Npos) {
						std::size_t k = bp + 1;
						while (k < cs.size() && IsSpace(cs[k])) {
							k++;
						}
						std::size_t nb = k;
						while (k < cs.size() && IsIdentChar(cs[k])) {
							k++;
						}
						if (k > nb) {
							StringView nm = Substr(cs, nb, k - nb);
							instanceNames.push_back(String{nm.data(), nm.size()});
						}
					}
				}
				// Body members -> plain uniforms (directives/blank lines kept verbatim)
				for (std::size_t k = openLine + 1; k < closeLine; k++) {
					String t = Trim(stripped[k]);
					if (t.empty() || t[0] == '#') {
						result.push_back(lines[k]);
						continue;
					}
					if (t[t.size() - 1] != ';') {
						result.push_back(lines[k]);
						continue;
					}
					String member = Trim(t.prefix(t.size() - 1));		// drop trailing ';'
					result.push_back(Std140MemberToUniform(LeadingWhitespace(lines[k]), member));
				}
				i = closeLine + 1;
			}
			// Drop the "<instance>." qualifier now that the block wrapper is gone
			for (String& line : result) {
				for (const String& n : instanceNames) {
					String qualifier = n + "."_s;
					line = ReplaceAll(line, qualifier, ""_s);
				}
			}
			lines = result;
		}
	}

	bool Essl100Emitter::Transform(StringView modernSource, bool vertexStage, String& out, Diagnostic& diag)
	{
		std::vector<String> lines = SplitLines(modernSource);

		// --- 1. Unwrap "#ifdef GL_ES ... #endif" (always active under "#version 100") ---------------
		lines = UnwrapGlEs(lines);

		// --- 2. std140 uniform blocks -> plain uniforms / a uniform array (ES2 has no UBOs) ----------
		RewriteStd140Blocks(lines);

		// --- 3. gl_VertexID quad synthesis -> reads of the ES2 corner / instance-index attributes ----
		bool usedCorner = false;
		bool usedInstance = false;
		{
			String text = RewriteVertexId(JoinLines(lines), usedCorner, usedInstance);
			lines = SplitLines(text);
		}
		// Declare the vertex attributes the rewrite introduced; the interface rewrite below turns the
		// leading "in" into "attribute". Their per-vertex data is supplied by the runtime (a static
		// corner VBO, and the batched instance-index stream).
		if (vertexStage && (usedCorner || usedInstance)) {
			std::vector<String> decls;
			if (usedCorner) {
				decls.push_back(String{"in vec2 aQuadCorner;"});
			}
			if (usedInstance) {
				decls.push_back(String{"in float aInstanceIndex;"});
			}
			lines.insert(lines.begin(), decls.begin(), decls.end());
		}

		// --- 3. Interface rewrite + fragment COLOR/main lowering ------------------------------------
		std::vector<String> stripped = StripComments(lines);
		std::vector<String> result;
		result.reserve(lines.size() + 4);
		int braceDepth = 0;
		bool inMain = false;
		bool mainBodyOpened = false;
		for (std::size_t idx = 0; idx < lines.size(); idx++) {
			const String& code = stripped[idx];
			const String& orig = lines[idx];

			// Enter main() at a global-scope "void main(" line (its body braces follow)
			if (!inMain && braceDepth == 0 && IsMainSignature(code)) {
				inMain = true;
			}

			// Global-scope interface declarations (before main), turned into attribute/varying
			if (!inMain && braceDepth == 0) {
				String keyword, tail;
				if (ParseInterfaceDecl(code, keyword, tail)) {
					if (!vertexStage && keyword == "out") {
						// The single fragment output "out vec4 COLOR;" — dropped; COLOR becomes a local
						// in main() and the result is written to gl_FragColor (see below)
						continue;
					}
					const char* newKeyword = (vertexStage ? (keyword == "in" ? "attribute" : "varying") : "varying");
					// ES2 vertex attributes must be float-typed - map any integer attribute type (e.g. "uint aMeshIndex")
					String outTail = (vertexStage && keyword == "in") ? MapEs2AttributeType(tail) : String{tail.data(), tail.size()};
					result.push_back(LeadingWhitespace(orig) + newKeyword + " "_s + outTail);
					continue;
				}
			}

			if (inMain) {
				int before = braceDepth;
				int after = before + CountBraces(code);
				if (!mainBodyOpened && after >= 1) {
					// The line that opens main()'s body
					result.push_back(vertexStage ? orig : RetargetReturns(orig, code));
					if (!vertexStage) {
						result.push_back("\tvec4 COLOR;"_s);
					}
					mainBodyOpened = true;
					braceDepth = after;
					continue;
				}
				if (mainBodyOpened && before >= 1 && after == 0) {
					// The line that closes main()'s body
					if (!vertexStage) {
						result.push_back("\tgl_FragColor = COLOR;"_s);
					}
					result.push_back(orig);
					braceDepth = after;
					inMain = false;
					continue;
				}
				// A line inside main()'s body
				result.push_back(vertexStage ? orig : RetargetReturns(orig, code));
				braceDepth = after;
				continue;
			}

			result.push_back(orig);
			braceDepth += CountBraces(code);
		}

		// --- 4. texture() -> texture2D() (and textureLod -> texture2DLod), comment-aware ------------
		RewriteTextureCalls(result);

		out = JoinLines(result);

		// --- 5. Derivatives (dFdx/dFdy/fwidth) are core in GLSL ES 3.00 but need an explicitly enabled
		//        extension under ESSL 100 — prepend the pragma when the stage uses any of them ----------
		{
			std::vector<String> check = StripComments(SplitLines(out));
			if (FindIdentifierLine(check, "dFdx") >= 0 || FindIdentifierLine(check, "dFdy") >= 0 || FindIdentifierLine(check, "fwidth") >= 0) {
				out = "#extension GL_OES_standard_derivatives : enable\n"_s + out;
			}
		}

		// Safety net / --essl100-check gate: flag any construct that is core in GLSL ES 3.00 but unsupported
		// under "#version 100", so the offline check catches ES2 breakage that the strict compiler would.
		// (int / ivecN are intentionally NOT flagged — signed integers and int() casts are valid in ESSL 100.)
		{
			std::vector<String> check = StripComments(SplitLines(out));
			const char* detail = nullptr;
			int line = -1;
			int l;
			if ((l = FindIdentifierLine(check, "gl_VertexID")) >= 0) { detail = "gl_VertexID"; line = l; }
			else if ((l = FindIdentifierLine(check, "std140")) >= 0) { detail = "std140 uniform block"; line = l; }
			else if ((l = FindIdentifierLine(check, "uint")) >= 0) { detail = "uint (no unsigned integers in ESSL 100)"; line = l; }
			else if ((l = FindIdentifierLine(check, "uvec2")) >= 0 || (l = FindIdentifierLine(check, "uvec3")) >= 0 || (l = FindIdentifierLine(check, "uvec4")) >= 0) { detail = "uvec (no unsigned integers in ESSL 100)"; line = l; }
			else if ((l = FindIdentifierLine(check, "round")) >= 0) { detail = "round() (GLSL ES 3.00+ only)"; line = l; }
			else if ((l = FindCharLine(check, '%')) >= 0) { detail = "integer % operator (GLSL ES 3.00+ only)"; line = l; }
			else if ((l = FindFloatSuffixLine(check)) >= 0) { detail = "f-suffixed float literal (GLSL ES 3.00+ only)"; line = l; }
			else {
				bool hasDeriv = (FindIdentifierLine(check, "dFdx") >= 0 || FindIdentifierLine(check, "dFdy") >= 0 || FindIdentifierLine(check, "fwidth") >= 0);
				if (hasDeriv && FindStr(JoinLines(check), "GL_OES_standard_derivatives"_s) == Npos) {
					detail = "dFdx/dFdy/fwidth without the GL_OES_standard_derivatives extension";
					line = FindIdentifierLine(check, "dFdx");
				}
			}
			if (detail != nullptr) {
				diag.Message = "ESSL100 transform incomplete (unsupported in ES2): "_s + detail;
				diag.Line = line;
				return false;
			}
		}
		return true;
	}
}
