#include "PoFile.h"

#if defined(WITH_EDITOR)

#include <Containers/StringConcatenable.h>

#include <Containers/SmallVector.h>

#include <cstdlib>
#include <cstring>

using namespace Death::Containers::Literals;
using Death::Containers::SmallVector;

namespace Jazz2::Editor
{
	namespace
	{
		// The catalog grows by many small pieces, a buffer that grows by doubling keeps that linear
		class TextBuilder
		{
		public:
			TextBuilder& operator<<(StringView value) {
				_data.append(value.data(), value.data() + value.size());
				return *this;
			}
			TextBuilder& operator<<(char c) {
				_data.push_back(c);
				return *this;
			}

			bool HasSuffix(StringView suffix) const {
				return (_data.size() >= suffix.size() && std::memcmp(_data.data() + _data.size() - suffix.size(), suffix.data(), suffix.size()) == 0);
			}
			void RemoveSuffix(std::size_t size) {
				_data.truncate(_data.size() - size);
			}

			String ToString() const {
				return String(_data.data(), _data.size());
			}

		private:
			SmallVector<char, 0> _data;
		};

		void AppendEscaped(TextBuilder& result, StringView value, bool multiline)
		{
			for (char c : value) {
				switch (c) {
					case '"': result << "\\\""_s; break;
					case '\\': result << "\\\\"_s; break;
					case '\t': result << "\\t"_s; break;
					case '\f': result << "\\f"_s; break;
					case '\r': result << "\\r"_s; break;
					case '\n': result << (multiline ? "\\n\"\n\""_s : "\\n"_s); break;
					default: result << c; break;
				}
			}
		}

		void AppendQuoted(TextBuilder& result, StringView value)
		{
			// A text of more lines is written a line on each, after an empty string, as gettext tools do
			bool multiline = false;
			for (std::size_t i = 0; i + 1 < value.size(); i++) {
				if (value[i] == '\n') {
					multiline = true;
					break;
				}
			}
			if (multiline) {
				result << "\"\"\n"_s;
			}
			result << '"';
			AppendEscaped(result, value, multiline);
			result << '"';
			// An empty string left by the last line break
			if (multiline && result.HasSuffix("\n\"\""_s)) {
				result.RemoveSuffix(3);
			}
		}

		// A line of the header, its value escaped as any other string
		void AppendHeaderLine(TextBuilder& result, StringView name, StringView value)
		{
			result << '"' << name << ": "_s;
			AppendEscaped(result, value, false);
			result << "\\n\"\n"_s;
		}

		// A comment of one line for each line of the text, a line break would end it otherwise
		void AppendComment(TextBuilder& result, StringView prefix, StringView text)
		{
			if (text.empty()) {
				result << prefix << '\n';
				return;
			}
			for (StringView line : text.split('\n')) {
				result << prefix;
				if (line.hasSuffix('\r')) {
					line = line.exceptSuffix(1);
				}
				result << line << '\n';
			}
		}

		// The value of a quoted string at the start of the view, the view is moved past it
		bool ParseQuoted(StringView& line, String& value)
		{
			line = line.trimmedPrefix();
			if (line.empty() || line[0] != '"') {
				return false;
			}
			std::size_t i = 1;
			SmallVector<char, 0> result;
			while (i < line.size() && line[i] != '"') {
				char c = line[i++];
				if (c == '\\' && i < line.size()) {
					char e = line[i++];
					switch (e) {
						case 'n': result.push_back('\n'); break;
						case 't': result.push_back('\t'); break;
						case 'f': result.push_back('\f'); break;
						case 'r': result.push_back('\r'); break;
						default: result.push_back(e); break;
					}
				} else {
					result.push_back(c);
				}
			}
			if (i >= line.size()) {
				return false;
			}
			line = line.exceptPrefix(i + 1);
			value += StringView(result.data(), result.size());
			return true;
		}
	}

	String PoFile::Write(ArrayView<const PoEntry> entries, StringView language, StringView projectName)
	{
		TextBuilder result;
		AppendComment(result, "# Texts of "_s, projectName);
		result << "msgid \"\"\nmsgstr \"\"\n"_s;
		AppendHeaderLine(result, "Project-Id-Version"_s, projectName);
		result << "\"MIME-Version: 1.0\\n\"\n"_s;
		result << "\"Content-Type: text/plain; charset=UTF-8\\n\"\n"_s;
		result << "\"Content-Transfer-Encoding: 8bit\\n\"\n"_s;
		if (!language.empty()) {
			AppendHeaderLine(result, "Language"_s, language);
		}
		for (const PoEntry& entry : entries) {
			result << '\n';
			if (!entry.Comment.empty()) {
				AppendComment(result, "#. "_s, entry.Comment);
			}
			if (!entry.Context.empty()) {
				result << "msgctxt "_s;
				AppendQuoted(result, entry.Context);
				result << '\n';
			}
			result << "msgid "_s;
			AppendQuoted(result, entry.Id);
			result << "\nmsgstr "_s;
			AppendQuoted(result, language.empty() ? StringView() : StringView(entry.Translation));
			result << '\n';
		}
		return result.ToString();
	}

	bool PoFile::Read(StringView text, SmallVectorImpl<PoEntry>& entries, String& language)
	{
		entries.clear();
		language = {};

		// Editors on Windows like to start the file with a byte order mark
		if (text.hasPrefix("\xEF\xBB\xBF"_s)) {
			text = text.exceptPrefix(3);
		}

		enum class Field { None, Context, Id, Plural, Translation, OtherTranslation };
		PoEntry current;
		Field field = Field::None;
		bool hasId = false;
		// A fuzzy translation is only a guess of a tool waiting for a translator, it's taken as no translation;
		// the flag comes before the entry it belongs to
		bool nextFuzzy = false, currentFuzzy = false;
		auto finish = [&]() {
			if (hasId) {
				if (current.Id.empty() && current.Context.empty()) {
					// The header, the language is one of its lines, a template has it fuzzy
					for (StringView line : current.Translation.split('\n')) {
						line = line.trimmed();
						if (line.hasPrefix("Language:"_s)) {
							language = line.exceptPrefix("Language:"_s).trimmed();
						}
					}
				} else {
					if (currentFuzzy) {
						current.Translation = {};
					}
					entries.push_back(Death::move(current));
				}
			}
			current = PoEntry();
			field = Field::None;
			hasId = false;
			currentFuzzy = false;
		};
		// The flags before an entry apply to it once it starts
		auto start = [&]() {
			if (hasId) {
				finish();
			}
			if (field == Field::None) {
				currentFuzzy = nextFuzzy;
				nextFuzzy = false;
			}
		};

		for (StringView line : text.split('\n')) {
			line = line.trimmed();
			if (line.empty()) {
				continue;
			}
			if (line[0] == '#') {
				// Flags, as "#, fuzzy, c-format"
				if (line.size() >= 2 && line[1] == ',') {
					for (StringView flag : line.exceptPrefix(2).split(',')) {
						if (flag.trimmed() == "fuzzy"_s) {
							nextFuzzy = true;
						}
					}
				}
				// Obsolete entries are left out with the comments
				continue;
			}
			String* target = nullptr;
			if (line.hasPrefix("msgctxt"_s)) {
				start();
				field = Field::Context;
				line = line.exceptPrefix("msgctxt"_s);
				target = &current.Context;
			} else if (line.hasPrefix("msgid_plural"_s)) {
				field = Field::Plural;
				continue;
			} else if (line.hasPrefix("msgid"_s)) {
				start();
				field = Field::Id;
				hasId = true;
				line = line.exceptPrefix("msgid"_s);
				target = &current.Id;
			} else if (line.hasPrefix("msgstr[0]"_s)) {
				field = Field::Translation;
				line = line.exceptPrefix("msgstr[0]"_s);
				target = &current.Translation;
			} else if (line.hasPrefix("msgstr["_s)) {
				field = Field::OtherTranslation;
				continue;
			} else if (line.hasPrefix("msgstr"_s)) {
				field = Field::Translation;
				line = line.exceptPrefix("msgstr"_s);
				target = &current.Translation;
			} else if (line[0] == '"') {
				// A line that continues the field before it
				switch (field) {
					case Field::Context: target = &current.Context; break;
					case Field::Id: target = &current.Id; break;
					case Field::Translation: target = &current.Translation; break;
					case Field::Plural:
					case Field::OtherTranslation: continue;
					default: return false;
				}
			} else {
				return false;
			}
			if (!ParseQuoted(line, *target)) {
				return false;
			}
		}
		finish();
		return true;
	}
}

#endif
