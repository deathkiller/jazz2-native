#if defined(WITH_ANGELSCRIPT)

#include "LevelScripts.h"
#include "RegisterArray.h"
#include "RegisterRef.h"
#include "RegisterString.h"
#include "ScriptActorWrapper.h"
#include "ScriptPlayerWrapper.h"

#include "../LevelHandler.h"
#include "../PreferencesCache.h"
#include "../Actors/ActorBase.h"

#include "../../nCine/Base/Random.h"

#if defined(DEATH_TARGET_WINDOWS) && !defined(CMAKE_BUILD)
#   if defined(_M_X64)
#		if defined(_DEBUG)
#			pragma comment(lib, "../Libs/Windows/x64/angelscriptd.lib")
#		else
#			pragma comment(lib, "../Libs/Windows/x64/angelscript.lib")
#		endif
#   elif defined(_M_IX86)
#		if defined(_DEBUG)
#			pragma comment(lib, "../Libs/Windows/x86/angelscriptd.lib")
#		else
#			pragma comment(lib, "../Libs/Windows/x86/angelscript.lib")
#		endif
#   else
#       error Unsupported architecture
#   endif
#endif

#if !defined(DEATH_TARGET_ANDROID) && !defined(_WIN32_WCE) && !defined(__psp2__)
#	include <locale.h>		// setlocale()
#endif

// Without namespace for shorter log messages
static void asScript(String& msg)
{
	LOGI_X("%s", msg.data());
}

static float asFractionf(float v)
{
	float intPart;
	return modff(v, &intPart);
}

static int asRandom()
{
	return Random().Next();
}

static int asRandom(int max)
{
	return Random().Fast(0, max);
}

static float asRandom(float min, float max)
{
	return Random().FastFloat(min, max);
}

namespace Jazz2::Scripting
{
	LevelScripts::LevelScripts(LevelHandler* levelHandler, const StringView& scriptPath)
		:
		_levelHandler(levelHandler),
		_module(nullptr),
		_onLevelUpdate(nullptr)
	{
		_engine = asCreateScriptEngine();
		_engine->SetUserData(this, EngineToOwner);
		_engine->SetContextCallbacks(RequestContextCallback, ReturnContextCallback, this);

		int r;
		r = _engine->SetMessageCallback(asMETHOD(LevelScripts, Message), this, asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		_module = _engine->GetModule("Main", asGM_ALWAYS_CREATE); RETURN_ASSERT(_module != nullptr);

		// Built-in types
		RegisterArray(_engine);
		RegisterRef(_engine);
		RegisterString(_engine);

		// Math functions
		r = _engine->RegisterGlobalFunction("float cos(float)", asFUNCTIONPR(cosf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float sin(float)", asFUNCTIONPR(sinf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float tan(float)", asFUNCTIONPR(tanf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = _engine->RegisterGlobalFunction("float acos(float)", asFUNCTIONPR(acosf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float asin(float)", asFUNCTIONPR(asinf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float atan(float)", asFUNCTIONPR(atanf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float atan2(float, float)", asFUNCTIONPR(atan2f, (float, float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = _engine->RegisterGlobalFunction("float cosh(float)", asFUNCTIONPR(coshf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float sinh(float)", asFUNCTIONPR(sinhf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float tanh(float)", asFUNCTIONPR(tanhf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = _engine->RegisterGlobalFunction("float log(float)", asFUNCTIONPR(logf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float log10(float)", asFUNCTIONPR(log10f, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = _engine->RegisterGlobalFunction("float pow(float, float)", asFUNCTIONPR(powf, (float, float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float sqrt(float)", asFUNCTIONPR(sqrtf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = _engine->RegisterGlobalFunction("float ceil(float)", asFUNCTIONPR(ceilf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float abs(float)", asFUNCTIONPR(fabsf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float floor(float)", asFUNCTIONPR(floorf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float fraction(float)", asFUNCTIONPR(asFractionf, (float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = _engine->RegisterGlobalFunction("int Random()", asFUNCTIONPR(asRandom, (), int), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("int Random(int)", asFUNCTIONPR(asRandom, (int), int), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float Random(float, float)", asFUNCTIONPR(asRandom, (float, float), float), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		// Game-specific functions
		r = _engine->RegisterGlobalFunction("void Print(const string &in)", asFUNCTION(asScript), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = _engine->RegisterGlobalFunction("uint8 get_Difficulty() property", asFUNCTION(asGetDifficulty), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("bool get_IsReforged() property", asFUNCTION(asIsReforged), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("int get_LevelWidth() property", asFUNCTION(asGetLevelWidth), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("int get_LevelHeight() property", asFUNCTION(asGetLevelHeight), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float get_ElapsedFrames() property", asFUNCTION(asGetElapsedFrames), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float get_AmbientLight() property", asFUNCTION(asGetAmbientLight), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void set_AmbientLight(float) property", asFUNCTION(asSetAmbientLight), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("float get_WaterLevel() property", asFUNCTION(asGetWaterLevel), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void set_WaterLevel(float) property", asFUNCTION(asSetWaterLevel), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = _engine->RegisterGlobalFunction("void PreloadMetadata(const string &in)", asFUNCTION(asPreloadMetadata), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void RegisterSpawnable(int, const string &in)", asFUNCTION(asRegisterSpawnable), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void Spawn(int, int, int)", asFUNCTION(asSpawnEvent), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void Spawn(int, int, int, const array<uint8> &in)", asFUNCTION(asSpawnEventParams), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void Spawn(const string &in, int, int)", asFUNCTION(asSpawnType), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void Spawn(const string &in, int, int, const array<uint8> &in)", asFUNCTION(asSpawnTypeParams), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		r = _engine->RegisterGlobalFunction("void ChangeLevel(int, const string &in = string())", asFUNCTION(asChangeLevel), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void MusicPlay(const string &in)", asFUNCTION(asMusicPlay), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void ShowLevelText(const string &in)", asFUNCTION(asShowLevelText), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = _engine->RegisterGlobalFunction("void SetWeather(uint8, uint8)", asFUNCTION(asSetWeather), asCALL_CDECL); RETURN_ASSERT(r >= 0);

		// Game-specific definitions
		constexpr char AsDefinitionsLibrary[] = R"(
enum ExitType {
	None,

	Normal,
	Warp,
	Bonus,
	Special,
	Boss,

	FastTransition = 0x80
}

enum GameDifficulty {
	Default,
	Easy,
	Normal,
	Hard
}

enum WeatherType {
	None,

	Snow,
	Flowers,
	Rain,
	Leaf,

	OutdoorsOnly = 0x80
};
)";
		r = _module->AddScriptSection("__Definitions", AsDefinitionsLibrary, _countof(AsDefinitionsLibrary) - 1, 0); RETURN_ASSERT(r >= 0);

		// Game-specific classes
		ScriptActorWrapper::RegisterFactory(_engine, _module);
		ScriptPlayerWrapper::RegisterFactory(_engine);

		// Try to load the script
		HashMap<String, bool> definedSymbols = {
#if defined(DEATH_TARGET_EMSCRIPTEN)
			{ "TARGET_EMSCRIPTEN"_s, true },
#elif defined(DEATH_TARGET_ANDROID)
			{ "TARGET_ANDROID"_s, true },
#elif defined(DEATH_TARGET_APPLE)
			{ "TARGET_APPLE"_s, true },
#	if defined(DEATH_TARGET_IOS)
			{ "TARGET_IOS"_s, true },
#	endif
#elif defined(DEATH_TARGET_WINDOWS)
			{ "TARGET_WINDOWS"_s, true },
#elif defined(DEATH_TARGET_UNIX)
			{ "TARGET_UNIX"_s, true },
#endif
#if defined(DEATH_TARGET_BIG_ENDIAN)
			{ "TARGET_BIG_ENDIAN"_s, true },
#endif
#if defined(WITH_AUDIO)
			{ "WITH_AUDIO"_s, true },
#endif
#if defined(WITH_OPENMPT)
			{ "WITH_OPENMPT"_s, true },
#endif
#if defined(WITH_THREADS)
			{ "WITH_THREADS"_s, true },
#endif
			{ "Resurrection"_s, true }
		};
		if (!AddScriptFromFile(scriptPath, definedSymbols)) {
			LOGE("Cannot compile the script. Please correct the code and try again.");
			return;
		}

		r = _module->Build(); RETURN_ASSERT_MSG(r >= 0, "Cannot compile the script. Please correct the code and try again.");

		asIScriptFunction* func = _module->GetFunctionByDecl("void OnLevelLoad()");
		if (func != nullptr) {
			asIScriptContext* ctx = _engine->RequestContext();

			ctx->Prepare(func);
			r = ctx->Execute();
			if (r == asEXECUTION_EXCEPTION) {
				LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			}

			_engine->ReturnContext(ctx);
		}

		_onLevelUpdate = _module->GetFunctionByDecl("void OnLevelUpdate(float)");
	}

	LevelScripts::~LevelScripts()
	{
		for (auto ctx : _contextPool) {
			ctx->Release();
		}

		if (_engine != nullptr) {
			_engine->ShutDownAndRelease();
			_engine = nullptr;
		}
	}

	bool LevelScripts::AddScriptFromFile(const StringView& path, const HashMap<String, bool>& definedSymbols)
	{
		auto s = fs::Open(path, FileAccessMode::Read);
		if (s->GetSize() <= 0) {
			return false;
		}

		String scriptContent(NoInit, s->GetSize());
		s->Read(scriptContent.data(), s->GetSize());
		s->Close();

		SmallVector<String, 4> includes;
		int scriptSize = (int)scriptContent.size();

		// First perform the checks for #if directives to exclude code that shouldn't be compiled
		int pos = 0;
		int nested = 0;
		while (pos < scriptSize) {
			asUINT len = 0;
			asETokenClass t = _engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
			if (t == asTC_UNKNOWN && scriptContent[pos] == '#' && (pos + 1 < scriptSize)) {
				int start = pos++;

				// Is this an #if directive?
				t = _engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);

				StringView token = scriptContent.slice(pos, pos + len);
				pos += len;

				if (token == "if"_s) {
					t = _engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
					if (t == asTC_WHITESPACE) {
						pos += len;
						t = _engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
					}

					if (t == asTC_IDENTIFIER) {
						StringView word = scriptContent.slice(pos, pos + len);

						// Overwrite the #if directive with space characters to avoid compiler error
						pos += len;

						// Has this identifier been defined by the application or not?
						auto it = definedSymbols.find(String::nullTerminatedView(word));
						bool defined = (it != definedSymbols.end() && it->second);

						for (int i = start; i < pos; i++) {
							if (scriptContent[i] != '\n') {
								scriptContent[i] = ' ';
							}
						}

						if (defined) {
							nested++;
						} else {
							// Exclude all the code until and including the #endif
							pos = ExcludeCode(scriptContent, pos);
						}
					}
				} else if (token == "endif"_s) {
					// Only remove the #endif if there was a matching #if
					if (nested > 0) {
						for (int i = start; i < pos; i++) {
							if (scriptContent[i] != '\n') {
								scriptContent[i] = ' ';
							}
						}
						nested--;
					}
				}
			} else
				pos += len;
		}

		pos = 0;
		while (pos < scriptSize) {
			asUINT len = 0;
			asETokenClass t = _engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
			if (t == asTC_COMMENT || t == asTC_WHITESPACE) {
				pos += len;
				continue;
			}

			StringView token = scriptContent.slice(pos, pos + len);

			// Is this a preprocessor directive?
			if (token == "#"_s && (pos + 1 < scriptSize)) {
				int start = pos++;

				t = _engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
				if (t == asTC_IDENTIFIER) {
					token = scriptContent.slice(pos, pos + len);
					if (token == "include"_s) {
						pos += len;
						t = _engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
						if (t == asTC_WHITESPACE) {
							pos += len;
							t = _engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
						}

						if (t == asTC_VALUE && len > 2 && (scriptContent[pos] == '"' || scriptContent[pos] == '\'')) {
							// Get the include file
							String includePath = MakePath(StringView(&scriptContent[pos + 1], len - 2), path);
							if (!includePath.empty()) {
								includes.push_back(includePath);
							}
							pos += len;

							// Overwrite the include directive with space characters to avoid compiler error
							for (int i = start; i < pos; i++) {
								if (scriptContent[i] != '\n') {
									scriptContent[i] = ' ';
								}
							}
						}
					} else if (token == "pragma"_s) {
						// Read until the end of the line
						pos += len;
						for (; pos < scriptSize && scriptContent[pos] != '\n'; pos++);

						ProcessPragma(scriptContent.slice(start + 7, (scriptContent[pos - 1] == '\r' ? pos - 1 : pos)));

						// Overwrite the pragma directive with space characters to avoid compiler error
						for (int i = start; i < pos; i++) {
							if (scriptContent[i] != '\n') {
								scriptContent[i] = ' ';
							}
						}
					}
				} else {
					// Check for lines starting with #!, e.g. shebang interpreter directive. These will be treated as comments and removed by the preprocessor
					if (scriptContent[pos] == '!') {
						// Read until the end of the line
						pos += len;
						for (; pos < scriptSize && scriptContent[pos] != '\n'; pos++);

						// Overwrite the directive with space characters to avoid compiler error
						for (int i = start; i < pos; i++) {
							if (scriptContent[i] != '\n') {
								scriptContent[i] = ' ';
							}
						}
					}
				}
			} else {
				// Don't search for metadata/includes within statement blocks or between tokens in statements
				pos = SkipStatement(scriptContent, pos);
			}
		}

		// Build the actual script
		_engine->SetEngineProperty(asEP_COPY_SCRIPT_SECTIONS, true);
		_module->AddScriptSection(path.data(), scriptContent.data(), scriptSize, 0);

		if (includes.size() > 0) {
			// Load the included scripts
			for (auto& include : includes) {
				if (!AddScriptFromFile(include, definedSymbols)) {
					return false;
				}
			}
		}

		return true;
	}

	int LevelScripts::ExcludeCode(String& scriptContent, int pos)
	{
		int scriptSize = (int)scriptContent.size();
		asUINT len = 0;
		int nested = 0;

		while (pos < scriptSize) {
			_engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
			if (scriptContent[pos] == '#') {
				scriptContent[pos] = ' ';
				pos++;

				// Is it an #if or #endif directive?
				_engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);

				StringView token = scriptContent.slice(pos, pos + len);

				if (token == "if"_s) {
					nested++;
				} else if (token == "endif"_s) {
					if (nested-- == 0) {
						for (uint32_t i = pos; i < pos + len; i++) {
							if (scriptContent[i] != '\n') {
								scriptContent[i] = ' ';
							}
						}

						pos += len;
						break;
					}
				}

				for (uint32_t i = pos; i < pos + len; i++) {
					if (scriptContent[i] != '\n') {
						scriptContent[i] = ' ';
					}
				}
			} else if (scriptContent[pos] != '\n') {
				for (uint32_t i = pos; i < pos + len; i++) {
					if (scriptContent[i] != '\n') {
						scriptContent[i] = ' ';
					}
				}
			}
			pos += len;
		}

		return pos;
	}

	int LevelScripts::SkipStatement(String& scriptContent, int pos)
	{
		int scriptSize = (int)scriptContent.size();
		asUINT len = 0;

		// Skip until ; or { whichever comes first
		while (pos < scriptSize && scriptContent[pos] != ';' && scriptContent[pos] != '{') {
			_engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
			pos += len;
		}

		// Skip entire statement block
		if (pos < scriptSize && scriptContent[pos] == '{') {
			pos += 1;

			// Find the end of the statement block
			int level = 1;
			while (level > 0 && pos < scriptSize) {
				asETokenClass t = _engine->ParseToken(&scriptContent[pos], scriptSize - pos, &len);
				if (t == asTC_KEYWORD) {
					if (scriptContent[pos] == '{') {
						level++;
					} else if (scriptContent[pos] == '}') {
						level--;
					}
				}

				pos += len;
			}
		} else {
			pos += 1;
		}
		return pos;
	}

	void LevelScripts::ProcessPragma(const StringView& content)
	{
		// TODO
	}

	String LevelScripts::MakePath(const StringView& path, const StringView& relativeToFile)
	{
		if (path.empty() || path.size() > fs::MaxPathLength) return { };

		char result[fs::MaxPathLength + 1];
		size_t length = 0;

		if (path[0] == '/' || path[0] == '\\') {
			// Absolute path from "Content" directory
			const char* src = &path[1];
			const char* srcLast = src;

			auto contentPath = ContentResolver::Current().GetContentPath();
			std::memcpy(result, contentPath.data(), contentPath.size());
			char* dst = result + contentPath.size();
			if (*(dst - 1) == '/' || *(dst - 1) == '\\') {
				dst--;
			}
			char* dstStart = dst;
			char* dstLast = dstStart;

			while (true) {
				bool end = (src - path.begin()) >= path.size();
				if (end || *src == '/' || *src == '\\') {
					if (src > srcLast) {
						size_t length = src - srcLast;
						if (length == 1 && srcLast[0] == '.') {
							// Ignore this
						} else if (length == 2 && srcLast[0] == '.' && srcLast[1] == '.') {
							if (dst != dstStart) {
								if (dst == dstLast && dstStart <= dstLast - 1) {
									dstLast--;
									while (dstStart <= dstLast) {
										if (*dstLast == '/' || *dstLast == '\\') {
											break;
										}
										dstLast--;
									}
								}
								dst = dstLast;
							}
						} else {
							dstLast = dst;

							if ((dst - result) + (sizeof(fs::PathSeparator) - 1) + (src - srcLast) >= fs::MaxPathLength) {
								return { };
							}

							if (dst != result) {
								std::memcpy(dst, fs::PathSeparator, sizeof(fs::PathSeparator) - 1);
								dst += sizeof(fs::PathSeparator) - 1;
							}
							std::memcpy(dst, srcLast, src - srcLast);
							dst += src - srcLast;
						}
					}
					if (end) {
						break;
					}
					srcLast = src + 1;
				}
				src++;
			}
			length = dst - result;
		} else {
			// Relative path to script file
			String dirPath = fs::GetDirectoryName(relativeToFile);
			if (dirPath.empty()) return { };

			const char* src = &path[0];
			const char* srcLast = src;

			std::memcpy(result, dirPath.data(), dirPath.size());
			char* dst = result + dirPath.size();
			if (*(dst - 1) == '/' || *(dst - 1) == '\\') {
				dst--;
			}
			char* searchBack = dst - 2;

			char* dstStart = dst;
			while (result <= searchBack) {
				if (*searchBack == '/' || *searchBack == '\\') {
					dstStart = searchBack + 1;
					break;
				}
				searchBack--;
			}
			char* dstLast = dstStart;

			while (true) {
				bool end = (src - path.begin()) >= path.size();
				if (end || *src == '/' || *src == '\\') {
					if (src > srcLast) {
						size_t length = src - srcLast;
						if (length == 1 && srcLast[0] == '.') {
							// Ignore this
						} else if (length == 2 && srcLast[0] == '.' && srcLast[1] == '.') {
							if (dst != dstStart) {
								if (dst == dstLast && dstStart <= dstLast - 1) {
									dstLast--;
									while (dstStart <= dstLast) {
										if (*dstLast == '/' || *dstLast == '\\') {
											break;
										}
										dstLast--;
									}
								}
								dst = dstLast;
							}
						} else {
							dstLast = dst;

							if ((dst - result) + (sizeof(fs::PathSeparator) - 1) + (src - srcLast) >= fs::MaxPathLength) {
								return { };
							}

							if (dst != result) {
								std::memcpy(dst, fs::PathSeparator, sizeof(fs::PathSeparator) - 1);
								dst += sizeof(fs::PathSeparator) - 1;
							}
							std::memcpy(dst, srcLast, src - srcLast);
							dst += src - srcLast;
						}
					}
					if (end) {
						break;
					}
					srcLast = src + 1;
				}
				src++;
			}
			length = dst - result;
		}

		return String(result, length);
	}

	asIScriptContext* LevelScripts::RequestContextCallback(asIScriptEngine* engine, void* param)
	{
		// Check if there is a free context available in the pool
		auto _this = reinterpret_cast<LevelScripts*>(param);
		if (!_this->_contextPool.empty()) {
			return _this->_contextPool.pop_back_val();
		} else {
			// No free context was available so we'll have to create a new one
			return engine->CreateContext();
		}
	}

	void LevelScripts::ReturnContextCallback(asIScriptEngine* engine, asIScriptContext* ctx, void* param)
	{
		// Unprepare the context to free any objects it may still hold (e.g. return value)
		// This must be done before making the context available for re-use, as the clean
		// up may trigger other script executions, e.g. if a destructor needs to call a function.
		ctx->Unprepare();

		// Place the context into the pool for when it will be needed again
		auto _this = reinterpret_cast<LevelScripts*>(param);
		_this->_contextPool.push_back(ctx);
	}

	void LevelScripts::Message(const asSMessageInfo& msg)
	{
		switch (msg.type) {
			case asMSGTYPE_ERROR: LOGE_X("%s (%i, %i): %s", msg.section, msg.row, msg.col, msg.message); break;
			case asMSGTYPE_WARNING: LOGW_X("%s (%i, %i): %s", msg.section, msg.row, msg.col, msg.message); break;
			default: LOGI_X("%s (%i, %i): %s", msg.section, msg.row, msg.col, msg.message); break;
		}
	}

	Actors::ActorBase* LevelScripts::CreateActorInstance(const StringView& typeName)
	{
		auto nullTerminatedTypeName = String::nullTerminatedView(typeName);

		// Create an instance of the ActorBase script class that inherits from the ScriptActorWrapper C++ class
		asITypeInfo* typeInfo = _module->GetTypeInfoByName(nullTerminatedTypeName.data());
		if (typeInfo == nullptr) {
			return nullptr;
		}

		asIScriptObject* obj = reinterpret_cast<asIScriptObject*>(_engine->CreateScriptObject(typeInfo));

		// Get the pointer to the C++ side of the ActorBase class
		ScriptActorWrapper* obj2 = *reinterpret_cast<ScriptActorWrapper**>(obj->GetAddressOfProperty(0));

		// Increase the reference count to the C++ object, as this is what will be used to control the life time of the object from the application side 
		obj2->AddRef();

		// Release the reference to the script side
		obj->Release();

		return obj2;
	}

	const SmallVectorImpl<Actors::Player*>& LevelScripts::GetPlayers() const
	{
		return _levelHandler->_players;
	}

	void LevelScripts::OnLevelBegin()
	{
		asIScriptFunction* func = _module->GetFunctionByDecl("void OnLevelBegin()");
		if (func == nullptr) {
			return;
		}
			
		asIScriptContext* ctx = _engine->RequestContext();

		ctx->Prepare(func);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
		}

		_engine->ReturnContext(ctx);
	}

	void LevelScripts::OnLevelUpdate(float timeMult)
	{
		if (_onLevelUpdate == nullptr) {
			return;
		}

		asIScriptContext* ctx = _engine->RequestContext();

		ctx->Prepare(_onLevelUpdate);
		ctx->SetArgFloat(0, timeMult);
		int r = ctx->Execute();
		if (r == asEXECUTION_EXCEPTION) {
			LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			// Don't call the method again if an exception occurs
			_onLevelUpdate = nullptr;
		}

		_engine->ReturnContext(ctx);
	}

	void LevelScripts::OnLevelCallback(Actors::ActorBase* initiator, uint8_t* eventParams)
	{
		char funcName[64];
		asIScriptFunction* func;

		// If known player is the initiator, try to call specific variant of the function
		if (auto player = dynamic_cast<Actors::Player*>(initiator)) {
			formatString(funcName, sizeof(funcName), "void OnFunction%i(Player@, uint8)", eventParams[0]);
			func = _module->GetFunctionByDecl(funcName);
			if (func != nullptr) {
				asIScriptContext* ctx = _engine->RequestContext();

				void* mem = asAllocMem(sizeof(ScriptPlayerWrapper));
				ScriptPlayerWrapper* playerWrapper = new(mem) ScriptPlayerWrapper(this, player);

				ctx->Prepare(func);
				ctx->SetArgObject(0, playerWrapper);
				ctx->SetArgByte(1, eventParams[1]);
				int r = ctx->Execute();
				if (r == asEXECUTION_EXCEPTION) {
					LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
				}

				_engine->ReturnContext(ctx);

				playerWrapper->Release();
				return;
			}
		}

		// Try to call parameter-less variant
		formatString(funcName, sizeof(funcName), "void OnFunction%i()", eventParams[0]);
		func = _module->GetFunctionByDecl(funcName);
		if (func != nullptr) {
			asIScriptContext* ctx = _engine->RequestContext();

			ctx->Prepare(func);
			int r = ctx->Execute();
			if (r == asEXECUTION_EXCEPTION) {
				LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			}

			_engine->ReturnContext(ctx);
			return;
		}

		LOGW_X("Callback function \"%s\" was not found in the script. Please correct the code and try again.", funcName);
	}

	uint8_t LevelScripts::asGetDifficulty()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return (uint8_t)_this->_levelHandler->_difficulty;
	}

	bool LevelScripts::asIsReforged()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return (uint8_t)_this->_levelHandler->_isReforged;
	}

	int LevelScripts::asGetLevelWidth()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_tileMap->LevelBounds().X;
	}

	int LevelScripts::asGetLevelHeight()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_tileMap->LevelBounds().Y;
	}

	float LevelScripts::asGetElapsedFrames()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_elapsedFrames;
	}

	float LevelScripts::asGetAmbientLight()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_ambientLightTarget;
	}

	void LevelScripts::asSetAmbientLight(float value)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->_ambientLightTarget = value;
	}

	float LevelScripts::asGetWaterLevel()
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_waterLevel;
	}

	void LevelScripts::asSetWaterLevel(float value)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->_waterLevel = value;
	}

	void LevelScripts::asPreloadMetadata(const String& path)
	{
		ContentResolver::Current().PreloadMetadataAsync(path);
	}

	void LevelScripts::asRegisterSpawnable(int eventType, const String& typeName)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		asITypeInfo* typeInfo = _this->_module->GetTypeInfoByName(typeName.data());
		if (typeInfo == nullptr) {
			return;
		}

		bool added = _this->_eventTypeToTypeInfo.emplace(eventType, typeInfo).second;
		if (added) {
			_this->_levelHandler->EventSpawner()->RegisterSpawnable((EventType)eventType, asRegisterSpawnableCallback);
		}
	}

	std::shared_ptr<Actors::ActorBase> LevelScripts::asRegisterSpawnableCallback(const Actors::ActorActivationDetails& details)
	{
		if (auto levelHandler = dynamic_cast<LevelHandler*>(details.LevelHandler)) {
			auto _this = levelHandler->_scripts.get();
			// Spawn() function with custom event cannot be used in OnLevelLoad(), because _scripts is not assigned yet
			if (_this != nullptr) {
				auto it = _this->_eventTypeToTypeInfo.find((int)details.Type);
				if (it != _this->_eventTypeToTypeInfo.end()) {
					asIScriptObject* obj = reinterpret_cast<asIScriptObject*>(_this->_engine->CreateScriptObject(it->second));
					ScriptActorWrapper* obj2 = *reinterpret_cast<ScriptActorWrapper**>(obj->GetAddressOfProperty(0));
					obj2->AddRef();
					obj->Release();
					obj2->OnActivated(details);
					return std::shared_ptr<Actors::ActorBase>(obj2);
				}
			}
		}
		return nullptr;
	}

	void LevelScripts::asSpawnEvent(int eventType, int x, int y)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		auto actor = _this->_levelHandler->EventSpawner()->SpawnEvent((EventType)eventType, spawnParams, Actors::ActorState::None, Vector3i(x, y, ILevelHandler::MainPlaneZ));
		if (actor != nullptr) {
			_this->_levelHandler->AddActor(actor);
		}
	}

	void LevelScripts::asSpawnEventParams(int eventType, int x, int y, const CScriptArray& eventParams)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		int size = eventParams.GetSize();
		std::memcpy(spawnParams, eventParams.At(0), size);

		auto actor = _this->_levelHandler->EventSpawner()->SpawnEvent((EventType)eventType, spawnParams, Actors::ActorState::None, Vector3i(x, y, ILevelHandler::MainPlaneZ));
		if (actor != nullptr) {
			_this->_levelHandler->AddActor(actor);
		}
	}

	void LevelScripts::asSpawnType(const String& typeName, int x, int y)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		auto actor = _this->CreateActorInstance(typeName);
		if (actor == nullptr) {
			return;
		}

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		actor->OnActivated({
			.LevelHandler = _this->_levelHandler,
			.Pos = Vector3i(x, y, ILevelHandler::MainPlaneZ),
			.Params = spawnParams
		});
		_this->_levelHandler->AddActor(std::shared_ptr<Actors::ActorBase>(actor));
	}

	void LevelScripts::asSpawnTypeParams(const String& typeName, int x, int y, const CScriptArray& eventParams)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));

		auto actor = _this->CreateActorInstance(typeName);
		if (actor == nullptr) {
			return;
		}

		uint8_t spawnParams[Events::EventSpawner::SpawnParamsSize] { };
		int size = eventParams.GetSize();
		std::memcpy(spawnParams, eventParams.At(0), size);

		actor->OnActivated({
			.LevelHandler = _this->_levelHandler,
			.Pos = Vector3i(x, y, ILevelHandler::MainPlaneZ),
			.Params = spawnParams
		});
		_this->_levelHandler->AddActor(std::shared_ptr<Actors::ActorBase>(actor));
	}

	void LevelScripts::asChangeLevel(int exitType, const String& path)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->BeginLevelChange((ExitType)exitType, path);
	}

	void LevelScripts::asMusicPlay(const String& path)
	{
#if defined(WITH_OPENMPT)
		if (path.empty()) {
			return;
		}

		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		auto _levelHandler = _this->_levelHandler;

		if (_levelHandler->_musicPath != path) {
			_levelHandler->_music = ContentResolver::Current().GetMusic(path);
			if (_levelHandler->_music != nullptr) {
				_levelHandler->_musicPath = path;
				_levelHandler->_music->setLooping(true);
				_levelHandler->_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
				_levelHandler->_music->setSourceRelative(true);
				_levelHandler->_music->play();
			}
		}
#endif
	}

	void LevelScripts::asShowLevelText(const String& text)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->ShowLevelText(text);
	}

	void LevelScripts::asSetWeather(uint8_t weatherType, uint8_t intensity)
	{
		auto ctx = asGetActiveContext();
		auto _this = reinterpret_cast<LevelScripts*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->SetWeather((WeatherType)weatherType, intensity);
	}
}

#endif