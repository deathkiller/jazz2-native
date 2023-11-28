#pragma once

#if defined(WITH_ANGELSCRIPT)

#include "../../Common.h"
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

	enum class ScriptContextType {
		Unknown,
		AlreadyIncluded,
		Legacy,
		Standard
	};

	class ScriptLoader
	{
	public:
		static constexpr asPWORD EngineToOwner = 0;

		ScriptLoader();
		virtual ~ScriptLoader();

		asIScriptEngine* GetEngine() const {
			return _engine;
		}

		asIScriptModule* GetMainModule() const {
			return _module;
		}

		ScriptContextType GetContextType() const {
			return _scriptContextType;
		}

	protected:
		asIScriptEngine* _engine;
		asIScriptModule* _module;
		ScriptContextType _scriptContextType;

		ScriptContextType AddScriptFromFile(const StringView& path, const HashMap<String, bool>& definedSymbols);
		int Build();

		ArrayView<String> GetMetadataForType(int typeId);
		ArrayView<String> GetMetadataForFunction(asIScriptFunction* func);
		ArrayView<String> GetMetadataForVariable(int varIdx);
		ArrayView<String> GetMetadataForTypeProperty(int typeId, int varIdx);
		ArrayView<String> GetMetadataForTypeMethod(int typeId, asIScriptFunction* method);

		virtual String OnProcessInclude(const StringView& includePath, const StringView& scriptPath) = 0;
		virtual void OnProcessPragma(const StringView& content, ScriptContextType& contextType) { }

		static String MakeRelativePath(const StringView& path, const StringView& relativeToFile);

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
			RawMetadataDeclaration(SmallVectorImpl<String>& m, const String& n, const String& d, MetadataType t, const String& c, const String& ns) : Metadata(std::move(m)), Name(n), Declaration(d), Type(t), ParentClass(c), Namespace(ns) { }

			SmallVector<String, 0> Metadata;
			String Name;
			String Declaration;
			MetadataType Type;
			String ParentClass;
			String Namespace;
		};

		struct ClassMetadata {
			HashMap<int, Array<String>> FuncMetadataMap;
			HashMap<int, Array<String>> VarMetadataMap;
		};

		SmallVector<asIScriptContext*, 4> _contextPool;

		HashMap<String, bool> _includedFiles;
		SmallVector<RawMetadataDeclaration, 0> _foundDeclarations;
		HashMap<int, Array<String>> _typeMetadataMap;
		HashMap<int, Array<String>> _funcMetadataMap;
		HashMap<int, Array<String>> _varMetadataMap;
		HashMap<int, ClassMetadata> _classMetadataMap;

		int ExcludeCode(String& scriptContent, int pos);
		int SkipStatement(String& scriptContent, int pos);
		int ExtractMetadata(MutableStringView scriptContent, int pos, SmallVectorImpl<String>& metadata);
		int ExtractDeclaration(const StringView& scriptContent, int pos, String& name, String& declaration, MetadataType& type);

		static asIScriptContext* RequestContextCallback(asIScriptEngine* engine, void* param);
		static void ReturnContextCallback(asIScriptEngine* engine, asIScriptContext* ctx, void* param);

		void Message(const asSMessageInfo& msg);
	};
}

#endif