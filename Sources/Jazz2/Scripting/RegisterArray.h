#pragma once

#if defined(WITH_ANGELSCRIPT) || defined(DOXYGEN_GENERATING_OUTPUT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	struct SArrayBuffer;
	struct SArrayCache;

	/** @brief **AngelScript** array */
	class CScriptArray
	{
	public:
		// Factory functions
		static CScriptArray* Create(asITypeInfo* ot);
		static CScriptArray* Create(asITypeInfo* ot, std::uint32_t length);
		static CScriptArray* Create(asITypeInfo* ot, std::uint32_t length, void* defaultValue);
		static CScriptArray* Create(asITypeInfo* ot, void* listBuffer);

		// Memory management
		void AddRef() const;
		void Release() const;

		// Type information
		asITypeInfo* GetArrayObjectType() const;
		std::int32_t GetArrayTypeId() const;
		std::int32_t GetElementTypeId() const;

		// Get the current size
		std::uint32_t GetSize() const;

		// Returns true if the array is empty
		bool IsEmpty() const;

		// Pre-allocates memory for elements
		void Reserve(std::uint32_t maxElements);

		// Resize the array
		void Resize(std::uint32_t numElements);

		// Get a pointer to an element. Returns 0 if out of bounds
		void* At(std::uint32_t index);
		const void* At(std::uint32_t index) const;

		// Set value of an element. 
		// The value arg should be a pointer to the value that will be copied to the element.
		// Remember, if the array holds handles the value parameter should be the 
		// address of the handle. The refCount of the object will also be incremented
		void SetValue(std::uint32_t index, void* value);

		// Copy the contents of one array to another (only if the types are the same)
		CScriptArray& operator=(const CScriptArray&);

		// Compare two arrays
		bool operator==(const CScriptArray&) const;

		// Array manipulation
		void InsertAt(std::uint32_t index, void* value);
		void InsertAt(std::uint32_t index, const CScriptArray& arr);
		void InsertLast(void* value);
		void RemoveAt(std::uint32_t index);
		void RemoveLast();
		void RemoveRange(std::uint32_t start, std::uint32_t count);
		void SortAsc();
		void SortDesc();
		void SortAsc(std::uint32_t startAt, std::uint32_t count);
		void SortDesc(std::uint32_t startAt, std::uint32_t count);
		void Sort(std::uint32_t startAt, std::uint32_t count, bool asc);
		void Sort(asIScriptFunction* less, std::uint32_t startAt, std::uint32_t count);
		void Reverse();
		int Find(void* value) const;
		int Find(std::uint32_t startAt, void* value) const;
		int FindByRef(void* ref) const;
		int FindByRef(std::uint32_t startAt, void* ref) const;

		// Return the address of internal buffer for direct manipulation of elements
		void* GetBuffer();

		// GC methods
		std::int32_t GetRefCount();
		void SetFlag();
		bool GetFlag();
		void EnumReferences(asIScriptEngine* engine);
		void ReleaseAllHandles(asIScriptEngine* engine);

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		mutable std::int32_t refCount;
		mutable bool gcFlag;
		asITypeInfo* objType;
		SArrayBuffer* buffer;
		std::int32_t elementSize;
		std::int32_t subTypeId;
#endif

		// Constructors
		CScriptArray(asITypeInfo* ot, void* initBuf); // Called from script when initialized with list
		CScriptArray(std::uint32_t length, asITypeInfo* ot);
		CScriptArray(std::uint32_t length, void* defVal, asITypeInfo* ot);
		CScriptArray(const CScriptArray& other);
		virtual ~CScriptArray();

		bool  Less(const void* a, const void* b, bool asc);
		void* GetArrayItemPointer(std::int32_t index);
		void* GetDataPointer(void* buffer);
		void  Copy(void* dst, void* src);
		void  Swap(void* a, void* b);
		void  Precache();
		bool  CheckMaxSize(std::uint32_t numElements);
		void  Resize(std::int32_t delta, std::uint32_t at);
		void  CreateBuffer(SArrayBuffer** buf, std::uint32_t numElements);
		void  DeleteBuffer(SArrayBuffer* buf);
		void  CopyBuffer(SArrayBuffer* dst, SArrayBuffer* src);
		void  Construct(SArrayBuffer* buf, std::uint32_t start, std::uint32_t end);
		void  Destruct(SArrayBuffer* buf, std::uint32_t start, std::uint32_t end);
		bool  Equals(const void* a, const void* b, asIScriptContext* ctx, SArrayCache* cache) const;
	};

	/** @relatesalso CScriptArray
		@brief Registers `array` type to **AngelScript** engine
	*/
	void RegisterArray(asIScriptEngine* engine);
}

#endif
