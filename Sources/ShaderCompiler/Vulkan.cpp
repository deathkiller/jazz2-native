#include "Vulkan.h"

#include <Base/Format.h>
#include <Containers/GrowableArray.h>
#include <Containers/StringConcatenable.h>

using namespace Death::Containers::Literals;

namespace ShaderCompiler
{
	namespace
	{
		bool IsSpace(char c) { return (c == ' ' || c == '\t'); }
		bool IsDigit(char c) { return (c >= '0' && c <= '9'); }
		bool IsIdentStart(char c) { return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'); }
		bool IsIdentChar(char c) { return (IsIdentStart(c) || IsDigit(c)); }

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
			Splits @p source on '\n', keeping a trailing empty segment when the source ends with a newline,
			so JoinLines() reproduces the input byte-for-byte (line insert/remove aside).
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

		/** GLSL keyword of a reflected uniform type (for the gathered "_Globals" block members) */
		StringView GlslTypeName(GlslType t)
		{
			switch (t) {
				case GlslType::Float: return "float"_s;
				case GlslType::Int: return "int"_s;
				case GlslType::UInt: return "uint"_s;
				case GlslType::Bool: return "bool"_s;
				case GlslType::Vec2: return "vec2"_s;
				case GlslType::Vec3: return "vec3"_s;
				case GlslType::Vec4: return "vec4"_s;
				case GlslType::IVec2: return "ivec2"_s;
				case GlslType::IVec3: return "ivec3"_s;
				case GlslType::IVec4: return "ivec4"_s;
				case GlslType::UVec2: return "uvec2"_s;
				case GlslType::UVec3: return "uvec3"_s;
				case GlslType::UVec4: return "uvec4"_s;
				case GlslType::BVec2: return "bvec2"_s;
				case GlslType::BVec3: return "bvec3"_s;
				case GlslType::BVec4: return "bvec4"_s;
				case GlslType::Mat2: return "mat2"_s;
				case GlslType::Mat3: return "mat3"_s;
				case GlslType::Mat4: return "mat4"_s;
				case GlslType::Sampler2D: return "sampler2D"_s;
				case GlslType::Sampler3D: return "sampler3D"_s;
				case GlslType::SamplerCube: return "samplerCube"_s;
				default: return "float"_s;
			}
		}

		/** The whole identifier starting at @p pos in @p s (empty when @p pos is not on an identifier) */
		String WordAt(StringView s, std::size_t pos)
		{
			std::size_t begin = pos;
			while (pos < s.size() && IsIdentChar(s[pos])) {
				pos++;
			}
			return Substr(s, begin, pos - begin);
		}

		/** True if the trimmed comment-free @p code declares a std140 uniform block; captures the block name */
		bool ParseBlockStart(StringView code, String& blockName)
		{
			if (code.size() < 6 || Substr(code, 0, 6) != "layout"_s) {
				return false;
			}
			if (FindStr(code, "std140"_s) == Npos) {
				return false;
			}
			std::size_t up = FindStr(code, "uniform"_s);
			if (up == Npos) {
				return false;
			}
			std::size_t i = up + 7;		// length of "uniform"
			while (i < code.size() && IsSpace(code[i])) {
				i++;
			}
			blockName = WordAt(code, i);
			return !blockName.empty();
		}

		/** True if the trimmed comment-free @p code is a sampler uniform ("uniform sampler2D name;"); captures the name */
		bool ParseSamplerDecl(StringView code, String& samplerName)
		{
			if (code.empty() || code.back() != ';' || WordAt(code, 0) != "uniform"_s) {
				return false;
			}
			std::size_t i = 7;			// length of "uniform"
			while (i < code.size() && IsSpace(code[i])) {
				i++;
			}
			String type = WordAt(code, i);
			if (type != "sampler2D"_s && type != "sampler3D"_s && type != "samplerCube"_s) {
				return false;
			}
			i += type.size();
			while (i < code.size() && IsSpace(code[i])) {
				i++;
			}
			samplerName = WordAt(code, i);
			return !samplerName.empty();
		}

		/** True if the trimmed comment-free @p code is a loose (default-block) uniform ("uniform <type> <name>;") */
		bool ParseLooseUniform(StringView code)
		{
			// Reached only after the block and sampler checks, so a first "uniform" token here is a loose uniform
			return (!code.empty() && code.back() == ';' && FindStr(code, "{"_s) == Npos && WordAt(code, 0) == "uniform"_s);
		}

		/**
			If the trimmed comment-free @p code is a global interface declaration
			("[flat] [layout(...)] in|out <tail>;"), sets @p keyword to "in"/"out", @p tail to the part after
			the keyword (e.g. "vec4 aColor;"), @p flat to whether a "flat" qualifier was present, and returns true.
		*/
		bool ParseInterfaceDecl(StringView code, String& keyword, String& tail, bool& flat)
		{
			flat = false;
			String s = Trim(code);
			if (s.empty() || s.back() != ';') {
				return false;
			}
			std::size_t i = 0;
			auto skipSpace = [&]() { while (i < s.size() && IsSpace(s[i])) i++; };

			skipSpace();
			if (WordAt(s, i) == "flat"_s) {
				flat = true;
				i += 4;
				skipSpace();
			}
			if (WordAt(s, i) == "layout"_s) {
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
			String kw = WordAt(s, i);
			if (kw != "in"_s && kw != "out"_s) {
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

		/** Rewrites the "layout(...)" qualifier on a std140 block-start @p line to add "set = 0, binding = B" */
		String RewriteBlockLayout(StringView line, int binding)
		{
			std::size_t layoutPos = FindStr(line, "layout"_s);
			if (layoutPos == Npos) {
				return String{line};
			}
			std::size_t open = Find(line, '(', layoutPos);
			if (open == Npos) {
				return String{line};
			}
			int parens = 0;
			std::size_t close = open;
			for (std::size_t i = open; i < line.size(); i++) {
				if (line[i] == '(') {
					parens++;
				} else if (line[i] == ')') {
					parens--;
					if (parens == 0) {
						close = i;
						break;
					}
				}
			}
			return Substr(line, 0, layoutPos) + "layout(set = 0, binding = "_s + Death::format("{}", binding) +
				", std140)"_s + Substr(line, close + 1);
		}

		/**
			Comment-aware whole-identifier rewrite of the vertex-id built-ins to their Vulkan spellings
			("gl_VertexID" -> "gl_VertexIndex", "gl_InstanceID" -> "gl_InstanceIndex").
		*/
		void RewriteGlBuiltins(std::vector<String>& lines)
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
						if (id == "gl_VertexID"_s) {
							arrayAppend(out, "gl_VertexIndex"_s);
						} else if (id == "gl_InstanceID"_s) {
							arrayAppend(out, "gl_InstanceIndex"_s);
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

	bool VulkanGlslEmitter::Transform(StringView modernSource, bool vertexStage, const StageReflection& reflection,
		String& out, Diagnostic& diag)
	{
		// --- Binding assignment (from the merged reflection; the two stages therefore agree, and slice 2 can
		//     reconstruct the same numbering) — set 0 for all resources, UBOs first, then samplers -------------
		const int uboBase = (reflection.Uniforms.empty() ? 0 : 1);
		auto blockBinding = [&](StringView name) -> int {
			for (std::size_t i = 0; i < reflection.Blocks.size(); i++) {
				if (reflection.Blocks[i].Name == name) {
					return uboBase + static_cast<int>(i);
				}
			}
			return uboBase;		// unreflected block (should not happen) — keep it in the UBO range
		};
		auto samplerBinding = [&](StringView name) -> int {
			int base = uboBase + static_cast<int>(reflection.Blocks.size());
			for (std::size_t j = 0; j < reflection.Textures.size(); j++) {
				if (reflection.Textures[j].Name == name) {
					return base + static_cast<int>(j);
				}
			}
			return base;
		};

		std::vector<String> lines = SplitLines(modernSource);
		std::vector<String> stripped = StripComments(lines);

		std::vector<String> result;
		result.reserve(lines.size() + reflection.Uniforms.size() + 8);
		int braceDepth = 0;
		int attributeLocation = 0;
		int varyingLocation = 0;
		int fragOutputLocation = 0;
		bool stageHasLooseUniform = false;

		for (std::size_t idx = 0; idx < lines.size(); idx++) {
			const String& code = stripped[idx];
			const String& orig = lines[idx];
			String trimmed = Trim(code);
			bool handled = false;

			if (braceDepth == 0 && !trimmed.empty()) {
				String name;
				if (ParseBlockStart(trimmed, name)) {
					result.push_back(RewriteBlockLayout(orig, blockBinding(name)));
					handled = true;
				} else if (ParseSamplerDecl(trimmed, name)) {
					result.push_back(LeadingWhitespace(orig) + "layout(set = 0, binding = "_s +
						Death::format("{}", samplerBinding(name)) + ") "_s + trimmed);
					handled = true;
				} else if (ParseLooseUniform(trimmed)) {
					// Removed here; gathered into the "_Globals" block emitted at the top (from the reflection)
					stageHasLooseUniform = true;
					handled = true;
				} else {
					String keyword, tail;
					bool flat;
					if (ParseInterfaceDecl(trimmed, keyword, tail, flat)) {
						int location;
						if (keyword == "in"_s) {
							location = (vertexStage ? attributeLocation++ : varyingLocation++);
						} else {
							location = (vertexStage ? varyingLocation++ : fragOutputLocation++);
						}
						result.push_back(LeadingWhitespace(orig) + "layout(location = "_s + Death::format("{}", location) +
							") "_s + (flat ? String("flat "_s) : String{}) + keyword + " "_s + tail);
						handled = true;
					}
				}
			}

			if (!handled) {
				result.push_back(orig);
			}
			braceDepth += CountBraces(code);
		}

		// gl_VertexID / gl_InstanceID -> the Vulkan gl_VertexIndex / gl_InstanceIndex (Vulkan keeps the vertex id)
		RewriteGlBuiltins(result);

		String body = JoinLines(result);
		if (body.contains("gl_VertexID"_s) || body.contains("gl_InstanceID"_s)) {
			diag.Message = "Vulkan GLSL emit: gl_VertexID/gl_InstanceID survived the rewrite"_s;
			diag.Line = 1;
			return false;
		}

		out = "#version 450\n"_s;
		if (stageHasLooseUniform) {
			// The gathered loose uniforms as one instance-name-less UBO, so the bodies' bare member reads still
			// resolve. Members + std140 layout come from the merged reflection, so both stages share one layout.
			out += "layout(set = 0, binding = 0, std140) uniform _Globals\n{\n"_s;
			for (const UniformInfo& u : reflection.Uniforms) {
				out += "\t"_s + GlslTypeName(u.Type) + " "_s + u.Name +
					(u.ArraySize > 0 ? String("["_s + Death::format("{}", u.ArraySize) + "]"_s) : String{}) + ";\n"_s;
			}
			out += "};\n"_s;
		}
		out += body;
		return true;
	}
}
