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

	bool Essl100Emitter::Transform(StringView modernSource, bool vertexStage, String& out, Diagnostic& diag)
	{
		std::vector<String> lines = SplitLines(modernSource);

		// --- 1. Detect ES2-unsupported features (deferred to P5 slice 2) ---------------------------
		{
			std::vector<String> stripped = StripComments(lines);
			int vertexIdLine = FindIdentifierLine(stripped, "gl_VertexID");
			int std140Line = FindIdentifierLine(stripped, "std140");
			if (vertexIdLine >= 0 || std140Line >= 0) {
				const char* detail;
				int line;
				if (vertexIdLine >= 0 && (std140Line < 0 || vertexIdLine <= std140Line)) {
					detail = "gl_VertexID";
					line = vertexIdLine;
				} else {
					detail = "std140 uniform block";
					line = std140Line;
				}
				diag.Message = "unsupported in ES2 (slice 2: needs uniform-array batching + corner attribute): "_s + detail;
				diag.Line = line;
				return false;
			}
		}

		// --- 2. Unwrap "#ifdef GL_ES ... #endif" (always active under "#version 100") ---------------
		lines = UnwrapGlEs(lines);

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
					result.push_back(LeadingWhitespace(orig) + newKeyword + " "_s + tail);
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
		return true;
	}
}
