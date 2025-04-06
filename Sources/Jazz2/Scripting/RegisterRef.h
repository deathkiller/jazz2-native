#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	/** @brief **AngelScript** reference handle */
	class CScriptHandle
	{
	public:
		// Constructors
		CScriptHandle();
		CScriptHandle(const CScriptHandle& other);
		CScriptHandle(void* ref, asITypeInfo* type);
		~CScriptHandle();

		// Copy the stored value from another any object
		CScriptHandle& operator=(const CScriptHandle& other);

		// Set the reference
		void Set(void* ref, asITypeInfo* type);

		// Compare equalness
		bool operator==(const CScriptHandle& o) const;
		bool operator!=(const CScriptHandle& o) const;
		bool Equals(void* ref, std::int32_t typeId) const;

		// Dynamic cast to desired handle type
		void Cast(void** outRef, std::int32_t typeId);

		// Returns the type of the reference held
		asITypeInfo* GetType() const;
		std::int32_t GetTypeId() const;

		// Get the reference
		void* GetRef();

		// GC callback
		void EnumReferences(asIScriptEngine* engine);
		void ReleaseReferences(asIScriptEngine* engine);

	protected:
		// These functions need to have access to protected members in order to call them from the script engine
		friend void Construct(CScriptHandle* self, void* ref, std::int32_t typeId);
		friend void RegisterRef(asIScriptEngine* engine);

		void ReleaseHandle();
		void AddRefHandle();

		// These shouldn't be called directly by the application as they requires an active context
		CScriptHandle(void* ref, std::int32_t typeId);
		CScriptHandle &Assign(void* ref, std::int32_t typeId);

#ifndef DOXYGEN_GENERATING_OUTPUT
		void* m_ref;
		asITypeInfo* m_type;
#endif
	};

	/** @relatesalso CScriptHandle
		@brief Registers `ref` type to **AngelScript** engine
	*/
	void RegisterRef(asIScriptEngine* engine);
}

#endif