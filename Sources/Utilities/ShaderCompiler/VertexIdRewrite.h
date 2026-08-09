#pragma once

/**
	@file VertexIdRewrite.h

	Shared rewrite of the engine's `gl_VertexID` quad-synthesis expressions into reads of vertex
	attributes, for the emitters targeting a stage language that has no vertex-ID input.

	Two backends need exactly the same substitution, which is why it lives here rather than in either
	of them: the **ESSL 100** profile (OpenGL|ES 2.0 has no `gl_VertexID`, and ESSL 100 additionally
	has no integer bit/modulo operators) and the **Cg** dialect emitted for the PS Vita's sceGxm
	backend (the GXM parameter semantics are the fixed-function-era Cg set — POSITION, TEXCOORD,
	COLOR, NORMAL, … — with no vertex-index semantic at all, so the corner cannot be recomputed
	in-shader there either).

	Because both consumers substitute the identical expressions, a shader that works on the ES 2.0
	profile keeps working on GXM: the runtime already supplies the `aQuadCorner` / `aInstanceIndex`
	attribute streams for the ES 2.0 profile, which the PS Vita build has always used, so the GXM
	vertex layout is the one the engine already produces.
*/


#include <cstring>

#include <Containers/GrowableArray.h>
#include <Containers/String.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringView.h>

namespace ShaderCompiler
{
	using namespace Death::Containers;
	using namespace Death::Containers::Literals;

	namespace VertexIdRewrite
	{
		/** @brief "Not found" sentinel of the string helpers below */
		constexpr std::size_t Npos = ~std::size_t{0};

		/** @brief Byte offset of @p needle in @p haystack, or `Npos` */
		inline std::size_t FindSubstring(StringView haystack, StringView needle)
		{
			if (needle.empty() || haystack.size() < needle.size()) {
				return Npos;
			}
			for (std::size_t i = 0; i + needle.size() <= haystack.size(); i++) {
				if (std::memcmp(haystack.data() + i, needle.data(), needle.size()) == 0) {
					return i;
				}
			}
			return Npos;
		}

		/** @brief Replaces every occurrence of @p from in @p s with @p to */
		inline String ReplaceAllOf(StringView s, StringView from, StringView to)
		{
			if (from.empty()) {
				return String{s.data(), s.size()};
			}
			Array<char> out;
			std::size_t i = 0;
			while (i < s.size()) {
				std::size_t p = FindSubstring(s.exceptPrefix(i), from);
				if (p == Npos) {
					arrayAppend(out, s.exceptPrefix(i));
					break;
				}
				p += i;
				arrayAppend(out, s.slice(i, p));
				arrayAppend(out, to);
				i = p + from.size();
			}
			return String{out.data(), out.size()};
		}

		/**
			Rewrites the engine's gl_VertexID quad-synthesis expressions into reads of the vertex
			attributes the runtime supplies (a static per-vertex [0,1]² corner, and a per-vertex instance
			index for the batched six-vertices-per-sprite path). Sets @p usedCorner / @p usedInstance so
			the caller declares only the attributes that end up referenced.

			Every single-quad formula is a function of the two terms float(id>>1) and float(id%2);
			substituting them with (1 - aQuadCorner.x) and aQuadCorner.y reproduces the exact corner of any
			of them (plain sprite, Lighting's 0.5-offset, TouchCircle) after constant folding, given the
			runtime supplies aQuadCorner = {(1,0),(1,1),(0,0),(0,1)} for the 4-vertex strip. The batched
			corner + instance index are fixed expressions replaced wholesale.
		*/
		inline String Apply(StringView src, bool& usedCorner, bool& usedInstance)
		{
			String text{src.data(), src.size()};
			// Batched per-instance index (before the batched corner terms, which also contain gl_VertexID)
			if (FindSubstring(text, "gl_VertexID / 6"_s) != Npos) {
				text = ReplaceAllOf(text, "gl_VertexID / 6"_s, "int(aInstanceIndex)"_s);
				usedInstance = true;
			}
			// Batched six-vertex corner terms (two triangles). Both forms in use — "1.0 - <term>" (sprites)
			// and "-0.5 + <term>" (BatchedLighting) — are functions of these, so substituting the terms with
			// (1 - aQuadCorner.{x,y}) reproduces either corner after folding, given the runtime's batched
			// aQuadCorner = {(1,1),(0,1),(0,0),(0,0),(1,0),(1,1)}.
			if (FindSubstring(text, "float(((gl_VertexID + 2) / 3) % 2)"_s) != Npos) {
				text = ReplaceAllOf(text, "float(((gl_VertexID + 2) / 3) % 2)"_s, "(1.0 - aQuadCorner.x)"_s);
				usedCorner = true;
			}
			if (FindSubstring(text, "float(((gl_VertexID + 1) / 3) % 2)"_s) != Npos) {
				text = ReplaceAllOf(text, "float(((gl_VertexID + 1) / 3) % 2)"_s, "(1.0 - aQuadCorner.y)"_s);
				usedCorner = true;
			}
			// Single-quad corner terms
			if (FindSubstring(text, "float(gl_VertexID >> 1)"_s) != Npos) {
				text = ReplaceAllOf(text, "float(gl_VertexID >> 1)"_s, "(1.0 - aQuadCorner.x)"_s);
				usedCorner = true;
			}
			if (FindSubstring(text, "float(gl_VertexID % 2)"_s) != Npos) {
				text = ReplaceAllOf(text, "float(gl_VertexID % 2)"_s, "aQuadCorner.y"_s);
				usedCorner = true;
			}
			// Batched-mesh instance index: "uint aMeshIndex" is an integer vertex attribute, which ES2 forbids —
			// the declaration is remapped to "float" by the interface rewrite (MapEs2AttributeType), so wrap its
			// array-index uses in int() here so "instances[aMeshIndex]" stays a valid integer index under ES2.
			// Cg accepts the integer attribute as declared, but the extra int() is a no-op there.
			if (FindSubstring(text, "[aMeshIndex]"_s) != Npos) {
				text = ReplaceAllOf(text, "[aMeshIndex]"_s, "[int(aMeshIndex)]"_s);
			}
			return text;
		}
	}
}
