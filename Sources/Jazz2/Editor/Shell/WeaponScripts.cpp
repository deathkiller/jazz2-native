#include "WeaponScripts.h"

#if defined(WITH_EDITOR)

#include "../Files/JsonAccess.h"
#include "../Files/JsonFile.h"

#include <Containers/StaticArray.h>
#include <Containers/StringConcatenable.h>
#include <Containers/StringUtils.h>
#include <IO/FileSystem.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace Death::Containers::Literals;
using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		constexpr StringView GeneratedMark = "///@MLLE-Generated"_s;

		bool IsIdentifierChar(char c)
		{
			return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
		}

		// Words and symbols of a script, comments and strings left out
		struct Tokenizer
		{
			StringView Text;
			std::size_t Pos = 0;

			StringView Next()
			{
				while (Pos < Text.size()) {
					char c = Text[Pos];
					if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
						Pos++;
					} else if (c == '/' && Pos + 1 < Text.size() && Text[Pos + 1] == '/') {
						while (Pos < Text.size() && Text[Pos] != '\n') {
							Pos++;
						}
					} else if (c == '/' && Pos + 1 < Text.size() && Text[Pos + 1] == '*') {
						Pos += 2;
						while (Pos + 1 < Text.size() && !(Text[Pos] == '*' && Text[Pos + 1] == '/')) {
							Pos++;
						}
						Pos += 2;
					} else if (c == '"' || c == '\'') {
						Pos++;
						while (Pos < Text.size() && Text[Pos] != c) {
							Pos += (Text[Pos] == '\\' ? 2 : 1);
						}
						Pos++;
					} else if (IsIdentifierChar(c)) {
						std::size_t start = Pos;
						while (Pos < Text.size() && IsIdentifierChar(Text[Pos])) {
							Pos++;
						}
						return Text.slice(start, Pos);
					} else if (c == ':' && Pos + 1 < Text.size() && Text[Pos + 1] == ':') {
						Pos += 2;
						return Text.slice(Pos - 2, Pos);
					} else {
						Pos++;
						return Text.slice(Pos - 1, Pos);
					}
				}
				return {};
			}
		};

		StringView GetQuoted(StringView line)
		{
			StringView start = line.findOr('"', line.end());
			if (start.begin() == line.end()) {
				return {};
			}
			StringView rest = line.suffix(start.begin() + 1);
			return rest.prefix(rest.findOr('"', rest.end()).begin());
		}

		bool IsMlleInclude(StringView name)
		{
			return StringUtils::lowercase(name).hasPrefix("mlle-"_s);
		}

		bool IsScriptFile(StringView name)
		{
			String lowercase = StringUtils::lowercase(name);
			return (lowercase.hasSuffix(".asc"_s) || lowercase.hasSuffix(".as"_s));
		}

		String MakeSetupLine(const WeaponsDescription& weapons)
		{
			std::int32_t last = -1;
			for (std::int32_t i = 0; i < MaxScriptWeapons; i++) {
				const WeaponDefinition& weapon = weapons.Weapons[i];
				if (weapon.Kind == WeaponKind::Script && !weapon.ScriptConstructor.empty()) {
					last = i;
				}
			}
			if (last < 0) {
				return "const bool MLLESetupSuccessful = MLLE::Setup(); "_s + GeneratedMark;
			}
			String entries;
			for (std::int32_t i = 0; i <= last; i++) {
				const WeaponDefinition& weapon = weapons.Weapons[i];
				if (i > 0) {
					entries += ", "_s;
				}
				entries += (weapon.Kind == WeaponKind::Script && !weapon.ScriptConstructor.empty() ? StringView(weapon.ScriptConstructor) : "null"_s);
			}
			return "const bool MLLESetupSuccessful = MLLE::Setup(array<MLLEWeaponApply@> = {"_s + entries + "}); "_s + GeneratedMark;
		}

		bool HasScriptWeapons(const WeaponsDescription& weapons)
		{
			for (std::int32_t i = 0; i < MaxScriptWeapons; i++) {
				if (weapons.Weapons[i].Kind == WeaponKind::Script && !weapons.Weapons[i].ScriptConstructor.empty()) {
					return true;
				}
			}
			return false;
		}
	}

	void FindScriptWeapons(StringView library, StringView text, SmallVectorImpl<ScriptWeaponClass>& weapons)
	{
		// The name of each brace that is open, empty for anything but a namespace
		SmallVector<StringView, 8> scopes;
		StringView pendingNamespace;
		bool pendingNamespaceBrace = false;
		bool abstractClass = false;

		Tokenizer tokens{text};
		for (StringView token = tokens.Next(); !token.empty(); token = tokens.Next()) {
			if (token == "namespace"_s) {
				pendingNamespace = tokens.Next();
				pendingNamespaceBrace = true;
				continue;
			}
			if (token == "{"_s) {
				scopes.push_back(pendingNamespaceBrace ? pendingNamespace : StringView());
				pendingNamespaceBrace = false;
				continue;
			}
			if (token == "}"_s) {
				if (!scopes.empty()) {
					scopes.pop_back();
				}
				continue;
			}
			if (token == "abstract"_s) {
				abstractClass = true;
				continue;
			}
			if (token != "class"_s) {
				if (token == ";"_s) {
					abstractClass = false;
				}
				continue;
			}

			StringView name = tokens.Next();
			bool isWeapon = false;
			bool isAbstract = abstractClass;
			abstractClass = false;
			// The bases up to the body, a declaration ends with a semicolon; "se::WeaponInterface" of the standard weapon
			// interface isn't a weapon MLLE can set up, only one of MLLEWeapons is
			StringView beforePrevious, previous;
			for (StringView part = tokens.Next(); !part.empty(); part = tokens.Next()) {
				if (part == "MLLEWeaponApply"_s || (part == "WeaponInterface"_s && previous == "::"_s && beforePrevious == "MLLEWeapons"_s)) {
					isWeapon = true;
				}
				beforePrevious = previous;
				previous = part;
				if (part == "{"_s) {
					scopes.push_back(StringView());
					break;
				}
				if (part == ";"_s) {
					break;
				}
			}
			if (!isWeapon || isAbstract || name.empty()) {
				continue;
			}

			String constructor;
			for (StringView scope : scopes) {
				if (!scope.empty()) {
					constructor += scope + "::"_s;
				}
			}
			constructor += name + "()"_s;
			ScriptWeaponClass& weapon = weapons.emplace_back();
			weapon.Library = library;
			weapon.Constructor = Death::move(constructor);
		}
	}

	void FindScriptWeapons(Death::Containers::ArrayView<const String> directories, SmallVectorImpl<ScriptWeaponClass>& weapons)
	{
		for (const String& directory : directories) {
			if (directory.empty() || !fs::DirectoryExists(directory)) {
				continue;
			}
			for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
				StringView fileName = fs::GetFileName(item);
				if (!StringUtils::lowercase(fileName).hasSuffix(".asc"_s) || IsMlleInclude(fileName)) {
					continue;
				}
				// The same library in more places is listed once, by the first of them
				bool known = false;
				for (const ScriptWeaponClass& weapon : weapons) {
					if (StringUtils::equalsIgnoreCase(weapon.Library, fileName)) {
						known = true;
						break;
					}
				}
				if (known) {
					continue;
				}
				auto s = fs::Open(item, FileAccess::Read);
				std::int64_t size = (s->IsValid() ? s->GetSize() : 0);
				if (size <= 0 || size > 16 * 1024 * 1024) {
					continue;
				}
				String text{NoInit, std::size_t(size)};
				if (s->Read(text.data(), size) == size) {
					FindScriptWeapons(fileName, text, weapons);
				}
			}
		}
	}

	void FindHandWrittenScriptWeapons(StringView script, String (&setUps)[MaxScriptWeapons])
	{
		static const StringView WeaponNames[MaxScriptWeapons] = {
			"BLASTER"_s, "BOUNCER"_s, "ICE"_s, "SEEKER"_s, "RF"_s, "TOASTER"_s, "TNT"_s, "GUN8"_s, "GUN9"_s
		};
		for (String& setUp : setUps) {
			setUp = {};
		}

		bool inComment = false;
		StringView rest = script;
		while (!rest.empty()) {
			StringView lineEnd = rest.findOr('\n', rest.end());
			StringView line = rest.prefix(lineEnd.begin());
			rest = rest.suffix(lineEnd.begin() == rest.end() ? rest.end() : lineEnd.begin() + 1);
			if (line.contains(GeneratedMark)) {
				continue;
			}
			// Block comments are left out as a whole line, which is enough for weapons set up one call a line
			if (inComment) {
				if (line.contains("*/"_s)) {
					inComment = false;
				}
				continue;
			}
			StringView commentStart = line.find("//"_s);
			if (!commentStart.empty()) {
				line = line.prefix(commentStart.begin());
			}
			StringView blockStart = line.find("/*"_s);
			if (!blockStart.empty()) {
				inComment = !line.suffix(blockStart.end()).contains("*/"_s);
				line = line.prefix(blockStart.begin());
			}

			StringView apply = line.find(".Apply("_s);
			if (apply.empty()) {
				continue;
			}
			StringView argument = line.suffix(apply.end()).trimmed();
			std::int32_t index = -1;
			if (argument.hasPrefix("WEAPON::"_s)) {
				StringView name = argument.exceptPrefix(8);
				for (std::int32_t i = 0; i < MaxScriptWeapons; i++) {
					if (name.hasPrefix(WeaponNames[i]) && (name.size() == WeaponNames[i].size() || !IsIdentifierChar(name[WeaponNames[i].size()]))) {
						index = i;
						break;
					}
				}
			} else if (!argument.empty() && argument[0] >= '1' && argument[0] <= '9' && (argument.size() == 1 || !IsIdentifierChar(argument[1]))) {
				index = argument[0] - '1';
			}
			if (index < 0) {
				continue;
			}
			// What the call is made on, from the start of the statement
			StringView target = line.prefix(apply.begin()).trimmed();
			for (const char* c = target.end(); c > target.begin(); c--) {
				char ch = *(c - 1);
				if (ch == ';' || ch == '{' || ch == '}') {
					target = target.suffix(c).trimmed();
					break;
				}
			}
			setUps[index] = target;
		}
	}

	String UpdateScriptWeaponLines(StringView script, const WeaponsDescription& weapons)
	{
		StringView newline = (script.contains("\r\n"_s) ? "\r\n"_s : "\n"_s);
		bool hasWeapons = HasScriptWeapons(weapons);

		// The libraries the weapons need, each once
		SmallVector<String, 4> libraries;
		for (std::int32_t i = 0; i < MaxScriptWeapons; i++) {
			const WeaponDefinition& weapon = weapons.Weapons[i];
			if (weapon.Kind != WeaponKind::Script || weapon.ScriptConstructor.empty() || weapon.ScriptLibrary.empty()) {
				continue;
			}
			bool known = false;
			for (const String& library : libraries) {
				known |= StringUtils::equalsIgnoreCase(library, weapon.ScriptLibrary);
			}
			if (!known) {
				libraries.push_back(weapon.ScriptLibrary);
			}
		}
		String libraryLines;
		for (const String& library : libraries) {
			libraryLines += "#include \""_s + library + "\" "_s + GeneratedMark + newline;
			libraryLines += "#pragma require \""_s + library + "\" "_s + GeneratedMark + newline;
		}

		String result;
		bool hasSetup = false, hasMlleInclude = false, librariesWritten = false;
		// The libraries go where they were, or after the include of MLLE if there were none
		std::size_t afterMlleInclude = 0;
		StringView rest = script;
		while (!rest.empty()) {
			StringView lineEnd = rest.findOr('\n', rest.end());
			StringView line = rest.prefix(lineEnd.begin() == rest.end() ? rest.end() : lineEnd.begin() + 1);
			rest = rest.suffix(line.end());
			StringView content = line.trimmed();
			if (!content.contains(GeneratedMark)) {
				result += line;
				continue;
			}

			if (content.contains("MLLE::Setup("_s)) {
				hasSetup = true;
				result += MakeSetupLine(weapons) + newline;
				continue;
			}
			StringView quoted = GetQuoted(content);
			bool isInclude = content.hasPrefix("#include"_s);
			bool isRequire = content.hasPrefix("#pragma"_s) && content.contains("require"_s);
			if (isInclude && IsMlleInclude(quoted)) {
				// The include with weapons ends with "w"
				String name = quoted;
				if (hasWeapons && !StringUtils::lowercase(name).hasSuffix("w.asc"_s) && name.size() > 4) {
					name = name.exceptSuffix(4) + "w.asc"_s;
				}
				result += "#include \""_s + name + "\" "_s + GeneratedMark + newline;
				hasMlleInclude = true;
				afterMlleInclude = result.size();
				continue;
			}
			if ((isInclude || isRequire) && IsScriptFile(quoted) && !IsMlleInclude(quoted)) {
				// A library of the weapons, all of them are written again in place of the first one
				if (!librariesWritten) {
					result += libraryLines;
					librariesWritten = true;
				}
				continue;
			}
			result += line;
		}

		if (!librariesWritten && hasMlleInclude && !libraryLines.empty()) {
			result = result.prefix(afterMlleInclude) + libraryLines + result.exceptPrefix(afterMlleInclude);
			librariesWritten = true;
		}
		if (!hasSetup && hasWeapons) {
			String header = MakeSetupLine(weapons) + newline;
			if (!hasMlleInclude) {
				header += "#include \"MLLE-Include-1.7w.asc\" "_s + GeneratedMark + newline + libraryLines;
				librariesWritten = true;
			}
			result = header + result;
		} else if (!librariesWritten && !libraryLines.empty()) {
			result += (result.empty() || result.hasSuffix("\n"_s) ? StringView() : newline);
			result += libraryLines;
		}
		return result;
	}

	ScriptWeaponParam::ScriptWeaponParam()
		: Min(std::numeric_limits<double>::quiet_NaN()), Max(std::numeric_limits<double>::quiet_NaN())
	{
	}

	namespace
	{
		bool ParseParamType(StringView name, ScriptParamType& type)
		{
			static const struct { StringView Name; ScriptParamType Type; } Types[] = {
				{ "bool"_s, ScriptParamType::Bool }, { "boolean"_s, ScriptParamType::Bool },
				{ "int8"_s, ScriptParamType::Int8 }, { "sbyte"_s, ScriptParamType::Int8 },
				{ "uint8"_s, ScriptParamType::UInt8 }, { "byte"_s, ScriptParamType::UInt8 },
				{ "int16"_s, ScriptParamType::Int16 }, { "short"_s, ScriptParamType::Int16 },
				{ "uint16"_s, ScriptParamType::UInt16 }, { "ushort"_s, ScriptParamType::UInt16 },
				{ "int"_s, ScriptParamType::Int32 }, { "int32"_s, ScriptParamType::Int32 },
				{ "uint"_s, ScriptParamType::UInt32 }, { "uint32"_s, ScriptParamType::UInt32 },
				{ "int64"_s, ScriptParamType::Int64 }, { "long"_s, ScriptParamType::Int64 },
				{ "uint64"_s, ScriptParamType::UInt64 }, { "ulong"_s, ScriptParamType::UInt64 },
				{ "float"_s, ScriptParamType::Float }, { "double"_s, ScriptParamType::Double },
				{ "string"_s, ScriptParamType::String },
			};
			String lowercase = StringUtils::lowercase(name.trimmed());
			for (const auto& entry : Types) {
				if (lowercase == entry.Name) {
					type = entry.Type;
					return true;
				}
			}
			return false;
		}

		void GetTypeRange(ScriptParamType type, double& min, double& max)
		{
			switch (type) {
				case ScriptParamType::Bool: min = 0.0; max = 1.0; break;
				case ScriptParamType::Int8: min = -128.0; max = 127.0; break;
				case ScriptParamType::UInt8: min = 0.0; max = 255.0; break;
				case ScriptParamType::Int16: min = -32768.0; max = 32767.0; break;
				case ScriptParamType::UInt16: min = 0.0; max = 65535.0; break;
				case ScriptParamType::Int32: min = -2147483648.0; max = 2147483647.0; break;
				case ScriptParamType::UInt32: min = 0.0; max = 4294967295.0; break;
				case ScriptParamType::Int64: min = -9007199254740992.0; max = 9007199254740992.0; break;
				case ScriptParamType::UInt64: min = 0.0; max = 9007199254740992.0; break;
				default: min = -1.0e9; max = 1.0e9; break;
			}
		}

		// A value of a parameter as text, `true` and `false` are 1 and 0
		bool ParseNumber(StringView text, double& value)
		{
			String trimmed = StringUtils::lowercase(text.trimmed());
			if (trimmed == "true"_s) {
				value = 1.0;
				return true;
			}
			if (trimmed == "false"_s) {
				value = 0.0;
				return true;
			}
			if (trimmed.empty()) {
				return false;
			}
			char* end = nullptr;
			double parsed = std::strtod(trimmed.data(), &end);
			if (end == trimmed.data() || !std::isfinite(parsed)) {
				return false;
			}
			value = parsed;
			return true;
		}

		void FinishParam(ScriptWeaponParam& param)
		{
			double typeMin, typeMax;
			GetTypeRange(param.Type, typeMin, typeMax);
			param.Min = (std::isnan(param.Min) ? typeMin : std::clamp(param.Min, typeMin, typeMax));
			param.Max = (std::isnan(param.Max) ? typeMax : std::clamp(param.Max, param.Min, typeMax));
			param.Default = std::clamp(param.Default, param.Min, param.Max);
		}

		String ReadTextFile(StringView path)
		{
			auto s = fs::Open(path, FileAccess::Read);
			std::int64_t size = (s->IsValid() ? s->GetSize() : 0);
			if (size <= 0 || size > 4 * 1024 * 1024) {
				return {};
			}
			String text{NoInit, std::size_t(size)};
			return (s->Read(text.data(), size) == size ? text : String());
		}

		// "LittleRocksPowerup" as "Little rocks powerup", "node1" as "Node 1"
		String MakeLabel(StringView identifier)
		{
			String label;
			for (std::size_t i = 0; i < identifier.size(); i++) {
				char c = identifier[i];
				char prev = (i > 0 ? identifier[i - 1] : '\0');
				if (c == '_') {
					if (!label.empty() && !label.hasSuffix(' ')) {
						label += " "_s;
					}
					continue;
				}
				bool upper = (c >= 'A' && c <= 'Z'), digit = (c >= '0' && c <= '9');
				bool prevLower = (prev >= 'a' && prev <= 'z'), prevDigit = (prev >= '0' && prev <= '9');
				if (!label.empty() && !label.hasSuffix(' ') && ((upper && (prevLower || prevDigit)) || (digit && !prevDigit) || (!digit && prevDigit))) {
					label += " "_s;
				}
				char lowered = (upper && !label.empty() ? char(c - 'A' + 'a') : c);
				if (label.empty() && lowered >= 'a' && lowered <= 'z') {
					lowered = char(lowered - 'a' + 'A');
				}
				label += StringView(&lowered, 1);
			}
			return label;
		}

		String WithoutSpaces(StringView text)
		{
			String result;
			for (char c : text) {
				if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
					result += StringView(&c, 1);
				}
			}
			return result;
		}

		// Tokens of a text with their positions in it
		void Tokenize(StringView text, SmallVectorImpl<StringView>& tokens)
		{
			Tokenizer tokenizer{text};
			for (StringView token = tokenizer.Next(); !token.empty(); token = tokenizer.Next()) {
				tokens.push_back(token);
			}
		}

		// Index of the token that closes the one at index, which opens a pair of brackets
		std::size_t FindClosing(ArrayView<const StringView> tokens, std::size_t index, StringView open, StringView close)
		{
			std::int32_t depth = 0;
			for (std::size_t i = index; i < tokens.size(); i++) {
				if (tokens[i] == open) {
					depth++;
				} else if (tokens[i] == close && --depth == 0) {
					return i;
				}
			}
			return tokens.size();
		}

		// The type of what's declared as a name, as "int p;" or "bool Rocks = true;"
		bool FindDeclaration(ArrayView<const StringView> tokens, StringView name, ScriptParamType& type, double& defaultValue)
		{
			for (std::size_t i = 1; i + 1 < tokens.size(); i++) {
				if (tokens[i] != name) {
					continue;
				}
				bool typed = ParseParamType(tokens[i - 1], type);
				if (!typed && tokens[i - 1] == ","_s) {
					// "bool A = true, B = true;" has the type at the start of the statement, a list of what a function takes doesn't
					std::size_t start = i - 1;
					bool call = false;
					while (start > 0 && tokens[start - 1] != ";"_s && tokens[start - 1] != "{"_s && tokens[start - 1] != "}"_s) {
						start--;
						call |= (tokens[start] == "("_s);
					}
					if (!call && tokens[start] == "const"_s && start + 1 < i) {
						start++;
					}
					typed = (!call && ParseParamType(tokens[start], type));
				}
				if (!typed) {
					continue;
				}
				StringView next = tokens[i + 1];
				if (next != ";"_s && next != "="_s && next != ","_s && next != ")"_s) {
					continue;
				}
				defaultValue = 0.0;
				if (next == "="_s && i + 2 < tokens.size()) {
					bool negative = (tokens[i + 2] == "-"_s);
					std::size_t at = i + (negative ? 3 : 2);
					String literal = (at < tokens.size() ? String(tokens[at]) : String());
					if (at + 2 < tokens.size() && tokens[at + 1] == "."_s) {
						literal = literal + "."_s + tokens[at + 2];
					}
					double parsed;
					if (ParseNumber(literal, parsed)) {
						defaultValue = (negative ? -parsed : parsed);
					}
				}
				return true;
			}
			return false;
		}
	}

	std::int32_t ReadMlleWeaponTemplates(StringView text, StringView sourcePath, SmallVectorImpl<ScriptWeaponTemplate>& templates)
	{
		String directory = fs::GetDirectoryName(sourcePath);
		std::size_t first = templates.size();
		ScriptWeaponTemplate* current = nullptr;
		StringView rest = text;
		while (!rest.empty()) {
			auto parts = rest.partition('\n');
			StringView line = parts[0].trimmed();
			rest = parts[2];
			if (line.empty() || line.hasPrefix(';') || line.hasPrefix('#')) {
				continue;
			}
			if (line.hasPrefix('[')) {
				current = &templates.emplace_back();
				current->Source = ScriptWeaponTemplateSource::MlleIni;
				current->SourcePath = sourcePath;
				continue;
			}
			auto keyValue = line.partition('=');
			if (current == nullptr || keyValue[1].empty()) {
				continue;
			}
			String key = StringUtils::lowercase(keyValue[0].trimmed());
			StringView value = keyValue[2].trimmed();
			if (key == "name"_s) {
				current->Name = value;
			} else if (key == "imagefilename"_s) {
				current->Image = (value.empty() ? String() : fs::CombinePath(directory, value));
			} else if (key == "libraryfilename"_s) {
				current->Library = value;
			} else if (key == "initialization"_s) {
				current->Constructor = value;
			} else if (key == "hooks"_s) {
				StringView hooks = value;
				while (!hooks.empty()) {
					auto hook = hooks.partition(',');
					if (!hook[0].trimmed().empty()) {
						current->Hooks.emplace_back(hook[0].trimmed());
					}
					hooks = hook[2];
				}
			} else if (key == "options"_s) {
				StringView options = value;
				while (!options.empty() && current->Complete) {
					auto option = options.partition('|');
					options = option[2];
					StringView fields = option[0].trimmed();
					if (fields.empty()) {
						continue;
					}
					auto name = fields.partition(':');
					auto type = name[2].partition(':');
					ScriptWeaponParam param;
					param.Name = name[0].trimmed();
					if (!ParseParamType(type[0], param.Type)) {
						// What follows can't be read without knowing how long this one is
						current->Complete = false;
						break;
					}
					if (param.Type == ScriptParamType::String) {
						param.DefaultText = type[2].trimmed();
					} else {
						ParseNumber(type[2], param.Default);
					}
					FinishParam(param);
					current->Params.push_back(Death::move(param));
				}
			}
		}

		// A section that doesn't say how to create its weapon isn't one
		for (std::size_t i = templates.size(); i > first; i--) {
			if (templates[i - 1].Constructor.empty()) {
				templates.erase(templates.begin() + std::ptrdiff_t(i - 1));
			}
		}
		return std::int32_t(templates.size() - first);
	}

	std::int32_t ReadWeaponTemplates(StringView text, StringView sourcePath, SmallVectorImpl<ScriptWeaponTemplate>& templates)
	{
		using namespace JsonAccess;

		Json::Value doc;
		if (!JsonFile::Parse(text, doc)) {
			return -1;
		}
		const Json::Value* list = &doc;
		const Json::Value& weapons = Member(doc, "Weapons");
		if (weapons.isArray()) {
			list = &weapons;
		}
		String directory = fs::GetDirectoryName(sourcePath);
		std::int32_t added = 0;
		auto addWeapon = [&](const Json::Value& value) {
			ScriptWeaponTemplate weaponTemplate;
			weaponTemplate.Source = ScriptWeaponTemplateSource::Json;
			weaponTemplate.SourcePath = sourcePath;
			weaponTemplate.Name = GetString(value, "Name");
			weaponTemplate.Library = GetString(value, "Library");
			weaponTemplate.Constructor = GetString(value, "Constructor");
			if (weaponTemplate.Constructor.empty()) {
				return;
			}
			String image = GetString(value, "Image");
			weaponTemplate.Image = (image.empty() ? String() : fs::CombinePath(directory, image));
			const Json::Value& hooks = Member(value, "Hooks");
			if (hooks.isArray()) {
				for (const Json::Value& hook : hooks) {
					String name = ToString(hook);
					if (!name.empty()) {
						weaponTemplate.Hooks.push_back(Death::move(name));
					}
				}
			} else if (hooks.isString()) {
				weaponTemplate.Hooks.push_back(ToString(hooks));
			}
			const Json::Value& params = Member(value, "Params");
			if (params.isArray()) {
				for (const Json::Value& item : params) {
					ScriptWeaponParam param;
					param.Name = GetString(item, "Name");
					param.Description = GetString(item, "Description");
					if (!ParseParamType(GetString(item, "Type", "int"_s), param.Type)) {
						weaponTemplate.Complete = false;
						break;
					}
					const Json::Value& defaultValue = Member(item, "Default");
					if (param.Type == ScriptParamType::String) {
						param.DefaultText = ToString(defaultValue);
					} else if (defaultValue.isBool()) {
						param.Default = (defaultValue.asBool() ? 1.0 : 0.0);
					} else if (defaultValue.isNumeric() && std::isfinite(defaultValue.asDouble())) {
						param.Default = defaultValue.asDouble();
					}
					const Json::Value& min = Member(item, "Min");
					const Json::Value& max = Member(item, "Max");
					if (min.isNumeric() && std::isfinite(min.asDouble())) {
						param.Min = min.asDouble();
					}
					if (max.isNumeric() && std::isfinite(max.asDouble())) {
						param.Max = max.asDouble();
					}
					const Json::Value& options = Member(item, "Options");
					if (options.isArray() && param.Type != ScriptParamType::String) {
						for (const Json::Value& option : options) {
							const Json::Value& optionValue = Member(option, "Value");
							if (!optionValue.isNumeric() || !std::isfinite(optionValue.asDouble())) {
								continue;
							}
							ScriptParamChoice& choice = param.Choices.emplace_back();
							choice.Name = GetString(option, "Name");
							choice.Value = optionValue.asDouble();
						}
					}
					FinishParam(param);
					weaponTemplate.Params.push_back(Death::move(param));
				}
			}
			templates.push_back(Death::move(weaponTemplate));
			added++;
		};
		if (list->isArray()) {
			for (const Json::Value& value : *list) {
				addWeapon(value);
			}
		} else {
			addWeapon(*list);
		}
		return added;
	}

	void FindScriptWeaponTemplates(Death::Containers::ArrayView<const String> directories, SmallVectorImpl<ScriptWeaponTemplate>& templates)
	{
		for (const String& directory : directories) {
			if (directory.empty() || !fs::DirectoryExists(directory)) {
				continue;
			}
			for (auto item : fs::Directory(directory, fs::EnumerationOptions::SkipDirectories)) {
				String lowercase = StringUtils::lowercase(item);
				bool ini = lowercase.hasSuffix(".ini"_s);
				if (!ini && !lowercase.hasSuffix(".json"_s)) {
					continue;
				}
				String text = ReadTextFile(item);
				std::int32_t count = (ini ? ReadMlleWeaponTemplates(text, item, templates) : ReadWeaponTemplates(text, item, templates));
				if (count < 0) {
					LOGW("Weapons of \"{}\" can't be read, it isn't JSON", item);
				}
			}
		}
	}

	bool GuessScriptWeaponTemplate(StringView library, StringView text, StringView constructor, ScriptWeaponTemplate& result,
		Death::Containers::ArrayView<const StringView> includes)
	{
		// The namespaces and the class of the weapon
		String qualified = WithoutSpaces(constructor.prefix(constructor.findOr('(', constructor.end()).begin()));
		SmallVector<StringView, 4> names;
		StringView scope = qualified;
		while (!scope.empty()) {
			StringView separator = scope.findOr("::"_s, scope.end());
			names.push_back(scope.prefix(separator.begin()));
			scope = scope.suffix(separator.end());
		}
		if (names.empty()) {
			return false;
		}
		StringView className = names.back();
		names.pop_back();

		SmallVector<StringView, 0> tokens;
		Tokenize(text, tokens);

		// The innermost namespace the class is in, where the functions of the weapon are
		std::size_t regionStart = 0, regionEnd = tokens.size();
		{
			struct Scope { StringView Name; std::size_t Token; };
			SmallVector<Scope, 8> scopes;
			StringView pendingNamespace;
			bool pending = false, found = false;
			std::size_t namespaceDepth = SIZE_MAX;
			for (std::size_t i = 0; i < tokens.size(); i++) {
				StringView token = tokens[i];
				if (token == "namespace"_s && i + 1 < tokens.size()) {
					pendingNamespace = tokens[++i];
					pending = true;
				} else if (token == "{"_s) {
					scopes.push_back(Scope{pending ? pendingNamespace : StringView(), i});
					pending = false;
				} else if (token == "}"_s && !scopes.empty()) {
					scopes.pop_back();
					if (found && scopes.size() == namespaceDepth) {
						regionEnd = i;
						break;
					}
				} else if (!found && token == "class"_s && i + 1 < tokens.size() && tokens[i + 1] == className) {
					SmallVector<StringView, 4> open;
					std::size_t innermost = SIZE_MAX;
					for (std::size_t j = 0; j < scopes.size(); j++) {
						if (!scopes[j].Name.empty()) {
							open.push_back(scopes[j].Name);
							innermost = j;
						}
					}
					bool same = (open.size() == names.size());
					for (std::size_t j = 0; same && j < open.size(); j++) {
						same = (open[j] == names[j]);
					}
					if (same) {
						found = true;
						if (innermost != SIZE_MAX) {
							regionStart = scopes[innermost].Token;
							namespaceDepth = innermost;
						} else {
							break;
						}
					}
				}
			}
			if (!found) {
				return false;
			}
		}
		ArrayView<const StringView> region = arrayView(tokens).slice(regionStart, regionEnd);

		// The function that gets the parameters, by "jjSTREAM@ name" in what it takes, and what it pops of them
		struct Pop { StringView Target; String Label; };
		SmallVector<Pop, 8> pops;
		for (std::size_t i = 0; i + 2 < region.size() && pops.empty(); i++) {
			if (region[i] != "jjSTREAM"_s || region[i + 1] != "@"_s) {
				continue;
			}
			std::size_t at = i + 2;
			while (at < region.size() && (region[at] == "&"_s || region[at] == "in"_s || region[at] == "const"_s)) {
				at++;
			}
			if (at + 1 >= region.size() || (region[at + 1] != ","_s && region[at + 1] != ")"_s && region[at + 1] != "="_s)) {
				continue;
			}
			StringView stream = region[at];
			// The end of what it takes, then its body, a declaration has none
			std::int32_t depth = 0;
			std::size_t body = region.size();
			for (std::size_t j = at; j < region.size(); j++) {
				if (region[j] == "("_s) {
					depth++;
				} else if (region[j] == ")"_s && depth-- == 0) {
					for (std::size_t k = j + 1; k < region.size(); k++) {
						if (region[k] == "{"_s) {
							body = k;
							break;
						}
						if (region[k] == ";"_s || region[k] == ","_s || region[k] == ")"_s) {
							break;
						}
					}
					break;
				}
			}
			if (body >= region.size()) {
				continue;
			}
			std::size_t bodyEnd = FindClosing(region, body, "{"_s, "}"_s);
			for (std::size_t j = body; j + 3 < bodyEnd; j++) {
				if (region[j] != stream || region[j + 1] != "."_s || region[j + 2] != "pop"_s || region[j + 3] != "("_s) {
					continue;
				}
				std::size_t close = FindClosing(region, j + 3, "("_s, ")"_s);
				// "parameter.pop(Regular.Age)" is named by all of it
				StringView target;
				String label;
				for (std::size_t k = j + 4; k < close && k < bodyEnd; k++) {
					if (IsIdentifierChar(region[k][0]) && !(region[k][0] >= '0' && region[k][0] <= '9')) {
						target = region[k];
						label = (label.empty() ? String(target) : String(label + "_"_s + target));
					}
				}
				// "parameter.pop(p); node1 = p;" names it after where it goes
				if (close + 4 < bodyEnd && region[close + 1] == ";"_s && region[close + 3] == "="_s && region[close + 4] == target) {
					label = region[close + 2];
				}
				pops.push_back(Pop{target, Death::move(label)});
				j = close;
			}
		}
		// A weapon with no function that gets them takes no parameters
		result = ScriptWeaponTemplate();
		result.Name = MakeLabel(className == "Weapon"_s && !names.empty() ? names.back() : className);
		result.Library = library;
		result.Constructor = constructor;
		result.Source = ScriptWeaponTemplateSource::Script;
		SmallVector<SmallVector<StringView, 0>, 2> includeTokens;
		for (StringView include : includes) {
			Tokenize(include, includeTokens.emplace_back());
		}
		auto findDeclaration = [&](StringView name, ScriptParamType& type, double& defaultValue) {
			if (FindDeclaration(region, name, type, defaultValue) || FindDeclaration(tokens, name, type, defaultValue)) {
				return true;
			}
			for (const auto& other : includeTokens) {
				if (FindDeclaration(other, name, type, defaultValue)) {
					return true;
				}
			}
			return false;
		};
		for (const Pop& pop : pops) {
			ScriptWeaponParam param;
			if (pop.Target.empty() || !findDeclaration(pop.Target, param.Type, param.Default)) {
				// Nothing after a value of an unknown size can be read
				result.Complete = false;
				break;
			}
			param.Name = MakeLabel(pop.Label.empty() ? pop.Target : StringView(pop.Label));
			FinishParam(param);
			result.Params.push_back(Death::move(param));
		}
		return true;
	}

	std::int32_t GetScriptParamSize(ScriptParamType type)
	{
		switch (type) {
			case ScriptParamType::Bool:
			case ScriptParamType::Int8:
			case ScriptParamType::UInt8: return 1;
			case ScriptParamType::Int16:
			case ScriptParamType::UInt16: return 2;
			case ScriptParamType::Int32:
			case ScriptParamType::UInt32:
			case ScriptParamType::Float: return 4;
			case ScriptParamType::Int64:
			case ScriptParamType::UInt64:
			case ScriptParamType::Double: return 8;
			default: return 0;
		}
	}

	void MakeDefaultScriptParams(const ScriptWeaponTemplate& weaponTemplate, SmallVectorImpl<std::uint8_t>& data)
	{
		SmallVector<ScriptParamValue, 8> values;
		ReadScriptParams(weaponTemplate, {}, values);
		WriteScriptParams(weaponTemplate, values, {}, data);
	}

	std::size_t ReadScriptParams(const ScriptWeaponTemplate& weaponTemplate, Death::Containers::ArrayView<const std::uint8_t> data,
		SmallVectorImpl<ScriptParamValue>& values)
	{
		values.clear();
		std::size_t pos = 0;
		bool complete = true;
		for (const ScriptWeaponParam& param : weaponTemplate.Params) {
			ScriptParamValue& value = values.emplace_back();
			value.Number = param.Default;
			value.Text = param.DefaultText;
			if (!complete) {
				continue;
			}
			if (param.Type == ScriptParamType::String) {
				std::uint32_t length;
				if (data.size() - pos < 4) {
					complete = false;
					continue;
				}
				std::memcpy(&length, &data[pos], 4);
				if (data.size() - pos - 4 < length) {
					complete = false;
					continue;
				}
				value.Text = String(reinterpret_cast<const char*>(&data[pos + 4]), length);
				pos += 4 + length;
				continue;
			}
			std::size_t size = std::size_t(GetScriptParamSize(param.Type));
			if (data.size() - pos < size) {
				complete = false;
				continue;
			}
			const std::uint8_t* p = &data[pos];
			switch (param.Type) {
				case ScriptParamType::Bool: value.Number = (p[0] != 0 ? 1.0 : 0.0); break;
				case ScriptParamType::Int8: { std::int8_t v; std::memcpy(&v, p, 1); value.Number = v; break; }
				case ScriptParamType::UInt8: value.Number = p[0]; break;
				case ScriptParamType::Int16: { std::int16_t v; std::memcpy(&v, p, 2); value.Number = v; break; }
				case ScriptParamType::UInt16: { std::uint16_t v; std::memcpy(&v, p, 2); value.Number = v; break; }
				case ScriptParamType::Int32: { std::int32_t v; std::memcpy(&v, p, 4); value.Number = v; break; }
				case ScriptParamType::UInt32: { std::uint32_t v; std::memcpy(&v, p, 4); value.Number = v; break; }
				case ScriptParamType::Int64: { std::int64_t v; std::memcpy(&v, p, 8); value.Number = double(v); break; }
				case ScriptParamType::UInt64: { std::uint64_t v; std::memcpy(&v, p, 8); value.Number = double(v); break; }
				case ScriptParamType::Float: { float v; std::memcpy(&v, p, 4); value.Number = (std::isfinite(v) ? v : 0.0); break; }
				case ScriptParamType::Double: { double v; std::memcpy(&v, p, 8); value.Number = (std::isfinite(v) ? v : 0.0); break; }
				default: break;
			}
			pos += size;
		}
		return pos;
	}

	void WriteScriptParams(const ScriptWeaponTemplate& weaponTemplate, Death::Containers::ArrayView<const ScriptParamValue> values,
		Death::Containers::ArrayView<const std::uint8_t> rest, SmallVectorImpl<std::uint8_t>& data)
	{
		data.clear();
		auto append = [&data](const void* value, std::size_t size) {
			data.append(static_cast<const std::uint8_t*>(value), static_cast<const std::uint8_t*>(value) + size);
		};
		for (std::size_t i = 0; i < weaponTemplate.Params.size(); i++) {
			const ScriptWeaponParam& param = weaponTemplate.Params[i];
			ScriptParamValue value;
			if (i < values.size()) {
				value = values[i];
			} else {
				value.Number = param.Default;
				value.Text = param.DefaultText;
			}
			double n = std::clamp(value.Number, param.Min, param.Max);
			double r = std::round(n);
			switch (param.Type) {
				case ScriptParamType::Bool: { std::uint8_t v = (n != 0.0 ? 1 : 0); append(&v, 1); break; }
				case ScriptParamType::Int8: { std::int8_t v = std::int8_t(r); append(&v, 1); break; }
				case ScriptParamType::UInt8: { std::uint8_t v = std::uint8_t(r); append(&v, 1); break; }
				case ScriptParamType::Int16: { std::int16_t v = std::int16_t(r); append(&v, 2); break; }
				case ScriptParamType::UInt16: { std::uint16_t v = std::uint16_t(r); append(&v, 2); break; }
				case ScriptParamType::Int32: { std::int32_t v = std::int32_t(r); append(&v, 4); break; }
				case ScriptParamType::UInt32: { std::uint32_t v = std::uint32_t(r); append(&v, 4); break; }
				case ScriptParamType::Int64: { std::int64_t v = std::int64_t(r); append(&v, 8); break; }
				case ScriptParamType::UInt64: { std::uint64_t v = std::uint64_t(r); append(&v, 8); break; }
				case ScriptParamType::Float: { float v = float(n); append(&v, 4); break; }
				case ScriptParamType::Double: append(&n, 8); break;
				case ScriptParamType::String: {
					std::uint32_t length = std::uint32_t(value.Text.size());
					append(&length, 4);
					append(value.Text.data(), length);
					break;
				}
			}
		}
		data.append(rest.begin(), rest.end());
	}

	bool IsScriptWeaponTemplateOf(const ScriptWeaponTemplate& weaponTemplate, StringView library, StringView constructor)
	{
		if (WithoutSpaces(weaponTemplate.Constructor) != WithoutSpaces(constructor)) {
			return false;
		}
		return (weaponTemplate.Library.empty() || library.empty() ||
			StringUtils::equalsIgnoreCase(fs::GetFileName(weaponTemplate.Library), fs::GetFileName(library)));
	}

	StringView GetScriptWeaponHookCall(StringView callback)
	{
		static const struct { StringView Callback; StringView Call; } Calls[] = {
			{ "onMain"_s, "processMain("_s },
			{ "onPlayer"_s, "processPlayer("_s },
			{ "onPlayerInput"_s, "processPlayerInput("_s },
			{ "onReceive"_s, "processPacket("_s },
			{ "onDrawAmmo"_s, "drawAmmo("_s },
		};
		for (const auto& entry : Calls) {
			if (StringUtils::equalsIgnoreCase(entry.Callback, callback.trimmed())) {
				return entry.Call;
			}
		}
		return {};
	}
}

#endif
