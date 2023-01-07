#if defined(WITH_ANGELSCRIPT)

#include "LevelScripts.h"
#include "../LevelHandler.h"

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

namespace Jazz2::Scripting
{
	LevelScripts::LevelScripts(LevelHandler* levelHandler, const StringView& scriptPath)
		:
		_levelHandler(levelHandler),
		_module(nullptr),
		_scriptContextType(ScriptContextType::Unknown),
		_onLevelUpdate(nullptr),
		_onLevelUpdateLastFrame(-1)
	{
		_engine = asCreateScriptEngine();
		_engine->SetUserData(this, EngineToOwner);
		_engine->SetContextCallbacks(RequestContextCallback, ReturnContextCallback, this);

		int r;
		r = _engine->SetMessageCallback(asMETHOD(LevelScripts, Message), this, asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		_module = _engine->GetModule("Main", asGM_ALWAYS_CREATE); RETURN_ASSERT(_module != nullptr);

		// Try to load the script
		HashMap<String, bool> DefinedSymbols = {
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
#	if defined(DEATH_TARGET_WINDOWS_RT)
			{ "TARGET_WINDOWS_RT"_s, true },
#	endif
#elif defined(DEATH_TARGET_UNIX)
			{ "TARGET_UNIX"_s, true },
#endif

#if defined(DEATH_TARGET_BIG_ENDIAN)
			{ "TARGET_BIG_ENDIAN"_s, true },
#endif

#if defined(WITH_OPENGLES)
			{ "WITH_OPENGLES"_s, true },
#endif
#if defined(WITH_AUDIO)
			{ "WITH_AUDIO"_s, true },
#endif
#if defined(WITH_VORBIS)
			{ "WITH_VORBIS"_s, true },
#endif
#if defined(WITH_OPENMPT)
			{ "WITH_OPENMPT"_s, true },
#endif
#if defined(WITH_THREADS)
			{ "WITH_THREADS"_s, true },
#endif
			{ "Resurrection"_s, true }
		};

		_scriptContextType = AddScriptFromFile(scriptPath, DefinedSymbols);
		if (_scriptContextType == ScriptContextType::Unknown) {
			LOGE("Cannot compile the script. Please correct the code and try again.");
			return;
		}

		RegisterBuiltInFunctions(_engine);
		switch (_scriptContextType) {
			case ScriptContextType::Legacy:
				LOGV("Compiled script with \"Legacy\" context");
				RegisterLegacyFunctions(_engine);
				break;
			case ScriptContextType::Standard:
				LOGV("Compiled script with \"Standard\" context");
				RegisterStandardFunctions(_engine, _module);
				break;
		}

		r = _module->Build(); RETURN_ASSERT_MSG(r >= 0, "Cannot compile the script. Please correct the code and try again.");

		asIScriptFunction* onLevelLoad = _module->GetFunctionByDecl("void onLevelLoad()");
		if (onLevelLoad != nullptr) {
			asIScriptContext* ctx = _engine->RequestContext();

			ctx->Prepare(onLevelLoad);
			r = ctx->Execute();
			if (r == asEXECUTION_EXCEPTION) {
				LOGE_X("An exception \"%s\" occurred in \"%s\". Please correct the code and try again.", ctx->GetExceptionString(), ctx->GetExceptionFunction()->GetDeclaration());
			}

			_engine->ReturnContext(ctx);
		}

		switch (_scriptContextType) {
			case ScriptContextType::Legacy:
				_onLevelUpdate = _module->GetFunctionByDecl("void onMain()");
				break;
			case ScriptContextType::Standard:
				_onLevelUpdate = _module->GetFunctionByDecl("void onLevelUpdate(float)");
				break;
		}
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

	ScriptContextType LevelScripts::AddScriptFromFile(const StringView& path, const HashMap<String, bool>& definedSymbols)
	{
		auto s = fs::Open(path, FileAccessMode::Read);
		if (s->GetSize() <= 0) {
			return ScriptContextType::Unknown;
		}

		String scriptContent(NoInit, s->GetSize());
		s->Read(scriptContent.data(), s->GetSize());
		s->Close();

		ScriptContextType contextType = ScriptContextType::Legacy;
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
							String includePath = ConstructPath(StringView(&scriptContent[pos + 1], len - 2), path);
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

						ProcessPragma(scriptContent.slice(start + 7, pos).trimmed(), contextType);

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
				if (AddScriptFromFile(include, definedSymbols) == ScriptContextType::Unknown) {
					return ScriptContextType::Unknown;
				}
			}
		}

		return contextType;
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

	void LevelScripts::ProcessPragma(const StringView& content, ScriptContextType& contextType)
	{
		// #pragma target Jazz² Resurrection - Changes script context type to Standard
		if (content == "target Jazz² Resurrection"_s || content == "target Jazz2 Resurrection"_s) {
			contextType = ScriptContextType::Standard;
		}
	}

	String LevelScripts::ConstructPath(const StringView& path, const StringView& relativeToFile)
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
}

#endif