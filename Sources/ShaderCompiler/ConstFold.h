#pragma once

/**
	@file ConstFold.h

	GLSL expression tokenizer, precedence-climbing parser and literal constant folder
	for ShaderCompiler.

	The folder rewrites multi-token literal-only subexpressions into a single literal
	with EXACT GLSL semantics: int and float never mix (a mixed expression such as
	"1 / 255.0" is left untouched), int arithmetic folds only while the result stays in
	32-bit signed range (overflow simply skips the fold), int division/modulo truncate
	toward zero, and floats are computed in double and emitted only when the "%.9g"
	text round-trips to the exact same value (always containing '.' or 'e', never
	longer than the original expression). Unsigned ("2u") and suffixed ("1.0f")
	literals are treated as opaque and never fold. Nothing containing identifiers,
	function calls, swizzles or indexing folds — but constructor/call ARGUMENTS fold
	individually ("vec2(1.0 + 2.0, 3.0)" becomes "vec2(3.0, 3.0)") and the parser
	models the full operator-precedence structure, so a literal run that associates
	with an opaque operand ("x - 1 + 2") is correctly left alone.

	The input is the comment-stripped text of one fold unit (typically a function
	body) as a list of line ranges. Preprocessor directive lines act as barriers:
	tokens never combine across them, and a conditional that splits a statement
	suppresses folding until the next statement boundary in every affected branch,
	so alternative "#ifdef" branches can never lend each other a bogus parse
	context. The computed edits never span multiple lines.

	The tokenizer/parser is deliberately reusable — it is the seed of the expression
	AST that future non-GLSL emitters (D3D11/software) will grow from.
*/

#include <cstddef>
#include <cstdint>
#include <vector>

#include <Containers/String.h>
#include <Containers/StringView.h>

namespace ShaderCompiler
{
	using namespace Death::Containers;

	/** @brief Classification of one GLSL expression token */
	enum class GlslTokenType : std::uint8_t
	{
		Identifier,			// Identifiers and keywords ("true"/"false" are BoolLiteral instead)
		IntLiteral,			// Decimal, hex or octal integer without a suffix
		UIntLiteral,		// Integer with a u/U suffix (never folded)
		FloatLiteral,		// Contains '.' or an exponent; Suffixed marks 1.0f/1.0lf forms (never folded)
		BoolLiteral,		// "true" or "false"
		Operator,			// Operators and punctuation, longest-match ("<<=", "==", "(", ";", ...)
		End					// Synthetic terminator
	};

	/** @brief One GLSL expression token with its position in the source line it came from */
	struct GlslToken
	{
		GlslTokenType Type = GlslTokenType::End;
		String Text;
		std::size_t Index = 0;		// Caller-defined line identifier (FoldInputLine::Index)
		std::size_t Begin = 0;		// First column of the token on its line
		std::size_t End = 0;		// One past the last column of the token
		bool Suffixed = false;		// Numeric literal carries a non-'u' suffix (1.0f, 1.0lf) — opaque
	};

	/** @brief One input line range of a fold unit (positions must be valid in the rewritable original text) */
	struct FoldInputLine
	{
		const String* Text = nullptr;		// Comment-stripped line text
		std::size_t Begin = 0;				// First column of the foldable range on this line
		std::size_t End = 0;				// One past the last foldable column on this line
		std::size_t Index = 0;				// Caller-defined line identifier reported back in FoldEdit
	};

	/** @brief One computed rewrite: columns [Begin, End) of line Index collapse to Replacement */
	struct FoldEdit
	{
		std::size_t Index = 0;
		std::size_t Begin = 0;
		std::size_t End = 0;
		String Replacement;
	};

	/** @brief GLSL expression tokenizer (reusable outside the folder) */
	class GlslExprTokenizer
	{
	public:
		/** Appends the tokens of columns [begin, end) of @p text to @p out, tagging them with @p index (no End token is added) */
		static void Tokenize(StringView text, std::size_t begin, std::size_t end, std::size_t index, std::vector<GlslToken>& out);
	};

	/** @brief Computes literal constant-folding rewrites for one fold unit */
	class ConstFolder
	{
	public:
		/**
			Parses the token stream of @p lines and appends one FoldEdit per maximal
			multi-token literal-only subexpression that collapses to a single literal.
			Edits are sorted by (Index, Begin), never overlap and never span lines;
			the caller applies them back to front.
		*/
		static void ComputeFolds(const std::vector<FoldInputLine>& lines, std::vector<FoldEdit>& edits);
	};
}
