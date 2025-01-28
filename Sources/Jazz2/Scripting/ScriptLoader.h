#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../Main.h"
#include "../../nCine/Base/HashMap.h"

#include <angelscript.h>

#include <Containers/SmallVector.h>
#include <Containers/String.h>
#include <Containers/StringView.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace nCine;

namespace Jazz2::Scripting
{
	class CScriptArray;

	/** @brief Script context type */
	enum class ScriptContextType {
		/** @brief Unknown/unsupported script part */
		Unknown,
		/** @brief Already included script part */
		AlreadyIncluded,
		/** @brief Legacy (JJ2+ compatible) script part */
		Legacy,
		/** @brief Standard (Jazz² Resurrection compatible) script part */
		Standard
	};

	/** @brief Result of @ref ScriptLoader::Build() */
	enum class ScriptBuildResult {
		/** @brief The engine succeeded */
		Success = asSUCCESS,
		/** @brief The engine configuration is invalid */
		InvalidConfiguration = asINVALID_CONFIGURATION,
		/** @brief The script failed to build */
		BuildFailed = asERROR,
		/** @brief Another thread is currently building */
		BuildInProgress = asBUILD_IN_PROGRESS,
		/** @brief It was not possible to initialize at least one of the global variables */
		InitGlobalVarsFailed = asINIT_GLOBAL_VARS_FAILED,
		/** @brief Compiler support is disabled in the engine */
		NotSupported = asNOT_SUPPORTED,
		/** @brief The code in the module is still being used and and cannot be removed */
		ModuleIsInUse = asMODULE_IS_IN_USE
	};

	/**
		@brief Generic **AngelScript** script loader with `#include` and `#pragma` directive support
	
		@experimental
	*/
	class ScriptLoader
	{
	public:
		/** @brief Returns @ref ScriptLoader instance from active **AngelScript** context if exists */
		template<typename T = ScriptLoader, class = typename std::enable_if<std::is_base_of<ScriptLoader, T>::value>::type>
		static T* FromActiveContext() {
			auto* ctx = asGetActiveContext();
			DEATH_ASSERT(ctx != nullptr, "Active context is not set", nullptr);
			return static_cast<T*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		}

		ScriptLoader();
		virtual ~ScriptLoader();

		/** @brief Returns **AngelScript** engine */
		asIScriptEngine* GetEngine() const {
			return _engine;
		}

		/** @brief Returns **AngelScript** main module */
		asIScriptModule* GetMainModule() const {
			return _module;
		}

		/** @brief Returns context type */
		ScriptContextType GetContextType() const {
			return _scriptContextType;
		}

	protected:
		/** @brief Adds a script path from file to the main module */
		ScriptContextType AddScriptFromFile(StringView path, const HashMap<String, bool>& definedSymbols);
		/** @brief Builds the main module and extracts metadata */
		ScriptBuildResult Build();
		/** @brief Sets context type */
		void SetContextType(ScriptContextType value);

		/** @brief Returns metadata for specified type */
		ArrayView<String> GetMetadataForType(std::int32_t typeId);
		/** @brief Returns metadata for specified function */
		ArrayView<String> GetMetadataForFunction(asIScriptFunction* func);
		/** @brief Returns metadata for specified variable */
		ArrayView<String> GetMetadataForVariable(std::int32_t varIdx);
		/** @brief Returns metadata for specified property */
		ArrayView<String> GetMetadataForTypeProperty(std::int32_t typeId, std::int32_t varIdx);
		/** @brief Returns metadata for specified method */
		ArrayView<String> GetMetadataForTypeMethod(std::int32_t typeId, asIScriptFunction* method);

		/** @brief Called when `#include` directive is encountered in a script file */
		virtual String OnProcessInclude(StringView includePath, StringView scriptPath) = 0;
		/** @brief Called when `#pragma` directive is encountered in a script file */
		virtual void OnProcessPragma(StringView content, ScriptContextType& contextType) = 0;

		/** @brief Creates a relative path */
		static String MakeRelativePath(StringView path, StringView relativeToFile);

	private:
		enum class MetadataType {
			Unknown,
			Type,
			Function,
			Variable,
			VirtualProperty,
			FunctionOrVariable
		};

		struct RawMetadataDeclaration {
			RawMetadataDeclaration(SmallVectorImpl<String>& m, const String& n, const String& d, MetadataType t, const String& c, const String& ns) : Metadata(std::move(m)), Name(n), Declaration(d), Type(t), ParentClass(c), Namespace(ns) {}

			SmallVector<String, 0> Metadata;
			String Name;
			String Declaration;
			MetadataType Type;
			String ParentClass;
			String Namespace;
		};

		struct ClassMetadata {
			HashMap<std::int32_t, Array<String>> FuncMetadataMap;
			HashMap<std::int32_t, Array<String>> VarMetadataMap;
		};

		static constexpr asPWORD EngineToOwner = 0;

		asIScriptEngine* _engine;
		asIScriptModule* _module;
		ScriptContextType _scriptContextType;
		SmallVector<asIScriptContext*, 4> _contextPool;

		HashMap<String, bool> _includedFiles;
		SmallVector<RawMetadataDeclaration, 0> _foundDeclarations;
		HashMap<std::int32_t, Array<String>> _typeMetadataMap;
		HashMap<std::int32_t, Array<String>> _funcMetadataMap;
		HashMap<std::int32_t, Array<String>> _varMetadataMap;
		HashMap<std::int32_t, ClassMetadata> _classMetadataMap;

		std::int32_t ExcludeCode(String& scriptContent, std::int32_t pos);
		std::int32_t SkipStatement(String& scriptContent, std::int32_t pos);
		std::int32_t ExtractMetadata(MutableStringView scriptContent, std::int32_t pos, SmallVectorImpl<String>& metadata);
		std::int32_t ExtractDeclaration(StringView scriptContent, std::int32_t pos, String& name, String& declaration, MetadataType& type);

		static asIScriptContext* RequestContextCallback(asIScriptEngine* engine, void* param);
		static void ReturnContextCallback(asIScriptEngine* engine, asIScriptContext* ctx, void* param);

		void Message(const asSMessageInfo& msg);
	};
}

#endif