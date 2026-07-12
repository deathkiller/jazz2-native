#include "ConstFold.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ShaderCompiler
{
	namespace
	{
		constexpr std::int64_t Int32Min = -2147483647LL - 1;
		constexpr std::int64_t Int32Max = 2147483647LL;

		bool IsSpace(char c)
		{
			return (c == ' ' || c == '\t' || c == '\f' || c == '\v');
		}

		bool IsDigit(char c)
		{
			return (c >= '0' && c <= '9');
		}

		bool IsHexDigit(char c)
		{
			return (IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
		}

		bool IsIdentStart(char c)
		{
			return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
		}

		bool IsIdentChar(char c)
		{
			return (IsIdentStart(c) || IsDigit(c));
		}

		// Multi-character operators, longest first (single characters are matched as a fallback)
		const char* const MultiCharOperators[] = {
			"<<=", ">>=",
			"==", "!=", "<=", ">=", "&&", "||", "^^", "<<", ">>",
			"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "++", "--"
		};

		/** @brief Folded value of a parsed subexpression together with its token span */
		struct Value
		{
			enum class Kind : std::uint8_t
			{
				Invalid,	// Parse failure at this position (no tokens consumed by the primary)
				Opaque,		// Well-formed but not a literal-only subexpression
				Int,
				Float,
				Bool
			};

			Kind K = Kind::Invalid;
			std::int64_t I = 0;
			double F = 0.0;
			bool B = false;
			std::size_t First = 0;		// Index of the first token of the subexpression
			std::size_t Last = 0;		// Index of the last token of the subexpression (inclusive)
		};

		Value MakeOpaque(std::size_t first, std::size_t last)
		{
			Value v;
			v.K = Value::Kind::Opaque;
			v.First = first;
			v.Last = last;
			return v;
		}

		/** Returns the precedence of a binary operator (higher binds tighter), or -1 when @p text is not one */
		std::int32_t BinaryPrecedence(const std::string& text)
		{
			if (text == "=" || text == "+=" || text == "-=" || text == "*=" || text == "/=" ||
				text == "%=" || text == "&=" || text == "|=" || text == "^=" || text == "<<=" || text == ">>=") {
				return 1;
			}
			// 2 is the ternary ?: (handled separately)
			if (text == "||") return 3;
			if (text == "^^") return 4;
			if (text == "&&") return 5;
			if (text == "|") return 6;
			if (text == "^") return 7;
			if (text == "&") return 8;
			if (text == "==" || text == "!=") return 9;
			if (text == "<" || text == ">" || text == "<=" || text == ">=") return 10;
			if (text == "<<" || text == ">>") return 11;
			if (text == "+" || text == "-") return 12;
			if (text == "*" || text == "/" || text == "%") return 13;
			return -1;
		}

		bool IsAssignmentOperator(const std::string& text)
		{
			return (BinaryPrecedence(text) == 1);
		}

		/**
			Tolerant precedence-climbing parser over the token stream of one fold unit.
			Every subexpression evaluates to a Value; literal-only subtrees carry their
			folded result, everything else is Opaque. A folded subtree emits a rewrite
			("finalizes") exactly when it is consumed by an opaque context — its parent
			combine fails or goes opaque, or it completes as a whole statement-level
			expression — so only MAXIMAL folded spans are rewritten.
		*/
		class Folder
		{
		public:
			Folder(const std::vector<FoldInputLine>& lines, const std::vector<GlslToken>& tokens,
				const std::vector<bool>& allowed, std::vector<FoldEdit>& edits)
				: _lines(lines), _tokens(tokens), _allowed(allowed), _edits(edits), _pos(0)
			{
			}

			void Run()
			{
				while (Peek().Type != GlslTokenType::End) {
					std::size_t before = _pos;
					Value v = ParseExpression(0);
					Finalize(v);
					if (_pos == before) {
						_pos++;		// Resync on tokens no expression can start with (';', '}', ',', ')', ...)
					}
				}
			}

		private:
			const std::vector<FoldInputLine>& _lines;
			const std::vector<GlslToken>& _tokens;
			const std::vector<bool>& _allowed;
			std::vector<FoldEdit>& _edits;
			std::size_t _pos;

			const GlslToken& Peek() const
			{
				return _tokens[_pos];
			}

			bool PeekOp(const char* text) const
			{
				return (Peek().Type == GlslTokenType::Operator && Peek().Text == text);
			}

			const std::string* LineTextOf(std::size_t index) const
			{
				for (const FoldInputLine& line : _lines) {
					if (line.Index == index) {
						return line.Text;
					}
				}
				return nullptr;
			}

			/** Emits the rewrite for a maximal folded subexpression (all skip conditions live here) */
			void Finalize(const Value& v)
			{
				if (v.K == Value::Kind::Invalid || v.K == Value::Kind::Opaque || v.Last <= v.First) {
					return;		// Not folded, or a single literal token (nothing collapses)
				}
				const GlslToken& first = _tokens[v.First];
				const GlslToken& last = _tokens[v.Last];
				if (first.Index != last.Index) {
					return;		// The span crosses lines — not rewritable in place
				}
				if (!_allowed[v.First]) {
					return;		// Suppressed after a mid-statement preprocessor barrier
				}
				std::string replacement = Render(v);
				if (replacement.empty()) {
					return;
				}
				const std::string* lineText = LineTextOf(first.Index);
				if (lineText == nullptr || last.End > lineText->size()) {
					return;
				}
				std::string original = lineText->substr(first.Begin, last.End - first.Begin);
				if (replacement.size() > original.size() || replacement == original) {
					return;		// No gain, or nothing changes
				}
				FoldEdit edit;
				edit.Index = first.Index;
				edit.Begin = first.Begin;
				edit.End = last.End;
				edit.Replacement = std::move(replacement);
				_edits.push_back(std::move(edit));
			}

			/** Renders a folded value as GLSL literal text, or "" when it cannot be represented exactly */
			std::string Render(const Value& v) const
			{
				switch (v.K) {
					case Value::Kind::Int:
						return std::to_string(v.I);
					case Value::Kind::Bool:
						return (v.B ? "true" : "false");
					case Value::Kind::Float: {
						if (!std::isfinite(v.F)) {
							return std::string();
						}
						char buffer[64];
						std::snprintf(buffer, sizeof(buffer), "%.9g", v.F);
						// Emit only when the printed text round-trips to the exact computed value
						if (std::strtod(buffer, nullptr) != v.F) {
							return std::string();
						}
						std::string s = buffer;
						if (s.find_first_of(".eE") == std::string::npos) {
							s += ".0";
						}
						return s;
					}
					default:
						return std::string();
				}
			}

			Value ParseExpression(std::int32_t minPrec)
			{
				Value lhs = ParseUnary();
				if (lhs.K == Value::Kind::Invalid) {
					return lhs;
				}
				while (Peek().Type == GlslTokenType::Operator) {
					const std::string opText = Peek().Text;
					if (opText == "?") {
						if (minPrec > 2) {
							break;
						}
						_pos++;
						Value mid = ParseExpression(0);
						if (PeekOp(":")) {
							_pos++;
						}
						Value rhs = ParseExpression(2);		// Right-associative
						Finalize(lhs);
						Finalize(mid);
						Finalize(rhs);
						lhs = MakeOpaque(lhs.First, (_pos > 0 ? _pos - 1 : 0));
						continue;
					}
					std::int32_t prec = BinaryPrecedence(opText);
					if (prec < 0 || prec < minPrec) {
						break;
					}
					_pos++;
					Value rhs = ParseExpression(IsAssignmentOperator(opText) ? prec : prec + 1);
					if (rhs.K == Value::Kind::Invalid) {
						// Malformed tail ("x + )") — give up on this expression, keep any lhs fold
						Finalize(lhs);
						return MakeOpaque(lhs.First, (_pos > 0 ? _pos - 1 : 0));
					}
					lhs = Combine(opText, lhs, rhs);
				}
				return lhs;
			}

			Value ParseUnary()
			{
				const GlslToken& t = Peek();
				if (t.Type == GlslTokenType::Operator &&
					(t.Text == "+" || t.Text == "-" || t.Text == "!" || t.Text == "~" || t.Text == "++" || t.Text == "--")) {
					std::string opText = t.Text;
					std::size_t opTok = _pos++;
					Value v = ParseUnary();
					if (v.K == Value::Kind::Invalid) {
						return v;
					}
					Value r = ApplyUnary(opText, v);
					r.First = opTok;
					return r;
				}
				return ParsePostfix();
			}

			Value ApplyUnary(const std::string& op, Value v)
			{
				if (op == "-") {
					if (v.K == Value::Kind::Int && -v.I >= Int32Min && -v.I <= Int32Max) {
						v.I = -v.I;
						return v;
					}
					if (v.K == Value::Kind::Float) {
						v.F = -v.F;
						return v;
					}
				} else if (op == "+") {
					if (v.K == Value::Kind::Int || v.K == Value::Kind::Float) {
						return v;
					}
				} else if (op == "!") {
					if (v.K == Value::Kind::Bool) {
						v.B = !v.B;
						return v;
					}
				}
				// ~, ++, -- and type mismatches never fold
				Finalize(v);
				return MakeOpaque(v.First, v.Last);
			}

			Value ParsePostfix()
			{
				Value v = ParsePrimary();
				if (v.K == Value::Kind::Invalid) {
					return v;
				}
				while (Peek().Type == GlslTokenType::Operator) {
					const std::string& opText = Peek().Text;
					if (opText == "(") {
						// Call/constructor — the callee never folds, arguments fold individually
						_pos++;
						ParseArguments();
						v = MakeOpaque(v.First, (_pos > 0 ? _pos - 1 : 0));
					} else if (opText == "[") {
						_pos++;
						Value index = ParseExpression(0);
						Finalize(index);
						if (PeekOp("]")) {
							_pos++;
						}
						v = MakeOpaque(v.First, (_pos > 0 ? _pos - 1 : 0));
					} else if (opText == ".") {
						// Member/swizzle access — a fold here could produce "3.0.x", drop it silently
						_pos++;
						if (Peek().Type == GlslTokenType::Identifier) {
							_pos++;
						}
						v = MakeOpaque(v.First, (_pos > 0 ? _pos - 1 : 0));
					} else if (opText == "++" || opText == "--") {
						_pos++;
						v = MakeOpaque(v.First, (_pos > 0 ? _pos - 1 : 0));
					} else {
						break;
					}
				}
				return v;
			}

			/** Parses a parenthesized argument list, folding each argument expression individually */
			void ParseArguments()
			{
				while (true) {
					const GlslToken& t = Peek();
					if (t.Type == GlslTokenType::End) {
						return;
					}
					if (t.Type == GlslTokenType::Operator) {
						if (t.Text == ")") {
							_pos++;
							return;
						}
						// ',' separates arguments; ';' appears in for(;;) headers parsed as a "call"
						if (t.Text == "," || t.Text == ";") {
							_pos++;
							continue;
						}
					}
					std::size_t before = _pos;
					Value argument = ParseExpression(0);
					Finalize(argument);
					if (_pos == before) {
						_pos++;		// Resync
					}
				}
			}

			Value ParsePrimary()
			{
				const GlslToken& t = Peek();
				switch (t.Type) {
					case GlslTokenType::IntLiteral: {
						Value v = MakeOpaque(_pos, _pos);
						if (!t.Suffixed) {
							const char* text = t.Text.c_str();
							char* end = nullptr;
							long long parsed = std::strtoll(text, &end, 0);	// Base 0 handles decimal, hex and octal
							if (end != nullptr && *end == '\0' && parsed >= 0 && parsed <= Int32Max) {
								v.K = Value::Kind::Int;
								v.I = parsed;
							}
						}
						_pos++;
						return v;
					}
					case GlslTokenType::FloatLiteral: {
						Value v = MakeOpaque(_pos, _pos);
						if (!t.Suffixed) {
							const char* text = t.Text.c_str();
							char* end = nullptr;
							double parsed = std::strtod(text, &end);
							if (end != nullptr && *end == '\0' && std::isfinite(parsed)) {
								v.K = Value::Kind::Float;
								v.F = parsed;
							}
						}
						_pos++;
						return v;
					}
					case GlslTokenType::BoolLiteral: {
						Value v = MakeOpaque(_pos, _pos);
						v.K = Value::Kind::Bool;
						v.B = (t.Text == "true");
						_pos++;
						return v;
					}
					case GlslTokenType::UIntLiteral:
					case GlslTokenType::Identifier: {
						// Unsigned literals fold with uint wraparound semantics — skipped in v1
						Value v = MakeOpaque(_pos, _pos);
						_pos++;
						return v;
					}
					case GlslTokenType::Operator:
						if (t.Text == "(") {
							std::size_t open = _pos++;
							Value inner = ParseExpression(0);
							if (inner.K == Value::Kind::Invalid) {
								// "()" or a stray token — treat the '(' as opaque, resync outside
								return MakeOpaque(open, open);
							}
							if (PeekOp(")")) {
								_pos++;
								// Parentheses keep the folded value; the span grows to include them
								inner.First = open;
								inner.Last = _pos - 1;
								return inner;
							}
							// Unterminated group (barrier split, malformed text)
							Finalize(inner);
							return MakeOpaque(open, (_pos > 0 ? _pos - 1 : 0));
						}
						return Value();		// Invalid
					default:
						return Value();		// Invalid
				}
			}

			Value Combine(const std::string& op, Value a, Value b)
			{
				const std::size_t first = a.First;
				const std::size_t last = b.Last;
				const bool bothFolded = (a.K != Value::Kind::Opaque && b.K != Value::Kind::Opaque);

				if (bothFolded && a.K == b.K) {
					if (a.K == Value::Kind::Int) {
						std::int64_t r = 0;
						bool folded = true;
						if (op == "+") {
							r = a.I + b.I;
						} else if (op == "-") {
							r = a.I - b.I;
						} else if (op == "*") {
							r = a.I * b.I;
						} else if (op == "/" && b.I != 0) {
							r = a.I / b.I;		// Truncates toward zero, matching GLSL
						} else if (op == "%" && b.I != 0) {
							r = a.I % b.I;
						} else if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") {
							Value v;
							v.K = Value::Kind::Bool;
							v.B = (op == "==" ? a.I == b.I : op == "!=" ? a.I != b.I : op == "<" ? a.I < b.I :
								op == ">" ? a.I > b.I : op == "<=" ? a.I <= b.I : a.I >= b.I);
							v.First = first;
							v.Last = last;
							return v;
						} else {
							folded = false;		// Shifts, bitwise ops, division by zero — never folded
						}
						if (folded && r >= Int32Min && r <= Int32Max) {
							Value v;
							v.K = Value::Kind::Int;
							v.I = r;
							v.First = first;
							v.Last = last;
							return v;
						}
					} else if (a.K == Value::Kind::Float) {
						if (op == "+" || op == "-" || op == "*" || op == "/") {
							double r = (op == "+" ? a.F + b.F : op == "-" ? a.F - b.F : op == "*" ? a.F * b.F : a.F / b.F);
							if (std::isfinite(r)) {
								Value v;
								v.K = Value::Kind::Float;
								v.F = r;
								v.First = first;
								v.Last = last;
								return v;
							}
						}
						// Float comparisons and '%' never fold
					} else {	// Bool
						if (op == "&&" || op == "||" || op == "==" || op == "!=" || op == "^^") {
							Value v;
							v.K = Value::Kind::Bool;
							v.B = (op == "&&" ? (a.B && b.B) : op == "||" ? (a.B || b.B) :
								op == "==" ? (a.B == b.B) : (a.B != b.B));
							v.First = first;
							v.Last = last;
							return v;
						}
					}
				}

				// Mixed int/float, opaque operands or a non-foldable operator — the children are maximal
				Finalize(a);
				Finalize(b);
				return MakeOpaque(first, last);
			}
		};
	}

	// --- GlslExprTokenizer ----------------------------------------------------------------------

	void GlslExprTokenizer::Tokenize(const std::string& text, std::size_t begin, std::size_t end, std::size_t index, std::vector<GlslToken>& out)
	{
		if (end > text.size()) {
			end = text.size();
		}
		std::size_t i = begin;
		while (i < end) {
			char c = text[i];
			if (IsSpace(c)) {
				i++;
				continue;
			}
			GlslToken t;
			t.Index = index;
			t.Begin = i;
			if (IsIdentStart(c)) {
				while (i < end && IsIdentChar(text[i])) {
					i++;
				}
				t.Text = text.substr(t.Begin, i - t.Begin);
				t.Type = ((t.Text == "true" || t.Text == "false") ? GlslTokenType::BoolLiteral : GlslTokenType::Identifier);
			} else if (IsDigit(c) || (c == '.' && i + 1 < end && IsDigit(text[i + 1]))) {
				bool isFloat = false;
				if (c == '0' && i + 1 < end && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
					i += 2;
					while (i < end && IsHexDigit(text[i])) {
						i++;
					}
				} else {
					while (i < end && IsDigit(text[i])) {
						i++;
					}
					if (i < end && text[i] == '.') {
						isFloat = true;
						i++;
						while (i < end && IsDigit(text[i])) {
							i++;
						}
					}
					if (i < end && (text[i] == 'e' || text[i] == 'E')) {
						std::size_t exponent = i + 1;
						if (exponent < end && (text[exponent] == '+' || text[exponent] == '-')) {
							exponent++;
						}
						if (exponent < end && IsDigit(text[exponent])) {
							isFloat = true;
							i = exponent;
							while (i < end && IsDigit(text[i])) {
								i++;
							}
						}
					}
				}
				bool isUnsigned = false;
				while (i < end && IsIdentChar(text[i])) {
					if ((text[i] == 'u' || text[i] == 'U') && !isFloat && !t.Suffixed) {
						isUnsigned = true;
					} else {
						t.Suffixed = true;		// f/F/lf/LF or anything else — opaque literal
					}
					i++;
				}
				t.Text = text.substr(t.Begin, i - t.Begin);
				t.Type = (isFloat ? GlslTokenType::FloatLiteral : (isUnsigned ? GlslTokenType::UIntLiteral : GlslTokenType::IntLiteral));
			} else {
				t.Type = GlslTokenType::Operator;
				std::size_t length = 1;
				for (const char* op : MultiCharOperators) {
					std::size_t opLength = std::char_traits<char>::length(op);
					if (i + opLength <= end && text.compare(i, opLength, op) == 0) {
						length = opLength;
						break;
					}
				}
				t.Text = text.substr(i, length);
				i += length;
			}
			t.End = i;
			out.push_back(std::move(t));
		}
	}

	// --- ConstFolder ----------------------------------------------------------------------------

	void ConstFolder::ComputeFolds(const std::vector<FoldInputLine>& lines, std::vector<FoldEdit>& edits)
	{
		std::vector<GlslToken> tokens;
		std::vector<bool> allowed;

		// Barrier scan: a preprocessor conditional that starts mid-statement suppresses folding
		// until the next statement boundary (';', '{' or '}') in every branch it affects, so
		// tokens from one #ifdef branch can never lend the other branch a bogus parse context
		bool atBoundary = true;			// The last significant token ended a statement
		bool suppressed = false;
		std::vector<bool> conditionalStack;

		for (const FoldInputLine& line : lines) {
			const std::string& text = *line.Text;
			std::size_t first = text.find_first_not_of(" \t", line.Begin);
			if (first != std::string::npos && first < line.End && text[first] == '#') {
				std::size_t p = first + 1;
				while (p < line.End && IsSpace(text[p])) {
					p++;
				}
				std::size_t nameBegin = p;
				while (p < line.End && IsIdentChar(text[p])) {
					p++;
				}
				std::string name = text.substr(nameBegin, p - nameBegin);
				if (name == "if" || name == "ifdef" || name == "ifndef") {
					conditionalStack.push_back(atBoundary);
				} else if (name == "elif" || name == "else") {
					// The branch after #else continues from the state at the matching #if
					atBoundary = (conditionalStack.empty() ? atBoundary : conditionalStack.back());
				} else if (name == "endif") {
					bool saved = (conditionalStack.empty() ? true : conditionalStack.back());
					if (!conditionalStack.empty()) {
						conditionalStack.pop_back();
					}
					atBoundary = (atBoundary && saved);
				}
				if (!atBoundary) {
					suppressed = true;
				}
				continue;
			}

			std::size_t tokenBegin = tokens.size();
			GlslExprTokenizer::Tokenize(text, line.Begin, line.End, line.Index, tokens);
			for (std::size_t k = tokenBegin; k < tokens.size(); k++) {
				const GlslToken& t = tokens[k];
				bool boundary = (t.Type == GlslTokenType::Operator && (t.Text == ";" || t.Text == "{" || t.Text == "}"));
				if (boundary) {
					suppressed = false;
				}
				atBoundary = boundary;
				allowed.push_back(!suppressed);
			}
		}

		GlslToken endToken;
		endToken.Type = GlslTokenType::End;
		tokens.push_back(std::move(endToken));
		allowed.push_back(false);

		Folder folder(lines, tokens, allowed, edits);
		folder.Run();

		std::sort(edits.begin(), edits.end(), [](const FoldEdit& a, const FoldEdit& b) {
			return (a.Index < b.Index || (a.Index == b.Index && a.Begin < b.Begin));
		});
	}
}
