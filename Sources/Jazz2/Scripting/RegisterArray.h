#pragma once

#if defined(WITH_ANGELSCRIPT)

#include <angelscript.h>

namespace Jazz2::Scripting
{
	struct SArrayBuffer;
	struct SArrayCache;

	class CScriptArray
	{
	public:
		// Factory functions
		static CScriptArray* Create(asITypeInfo* ot);
		static CScriptArray* Create(asITypeInfo* ot, asUINT length);
		static CScriptArray* Create(asITypeInfo* ot, asUINT length, void* defaultValue);
		static CScriptArray* Create(asITypeInfo* ot, void* listBuffer);

		// Memory management
		void AddRef() const;
		void Release() const;

		// Type information
		asITypeInfo* GetArrayObjectType() const;
		int GetArrayTypeId() const;
		int GetElementTypeId() const;

		// Get the current size
		asUINT GetSize() const;

		// Returns true if the array is empty
		bool IsEmpty() const;

		// Pre-allocates memory for elements
		void Reserve(asUINT maxElements);

		// Resize the array
		void Resize(asUINT numElements);

		// Get a pointer to an element. Returns 0 if out of bounds
		void* At(asUINT index);
		const void* At(asUINT index) const;

		// Set value of an element. 
		// The value arg should be a pointer to the value that will be copied to the element.
		// Remember, if the array holds handles the value parameter should be the 
		// address of the handle. The refCount of the object will also be incremented
		void SetValue(asUINT index, void* value);

		// Copy the contents of one array to another (only if the types are the same)
		CScriptArray& operator=(const CScriptArray&);

		// Compare two arrays
		bool operator==(const CScriptArray&) const;

		// Array manipulation
		void InsertAt(asUINT index, void* value);
		void InsertAt(asUINT index, const CScriptArray& arr);
		void InsertLast(void* value);
		void RemoveAt(asUINT index);
		void RemoveLast();
		void RemoveRange(asUINT start, asUINT count);
		void SortAsc();
		void SortDesc();
		void SortAsc(asUINT startAt, asUINT count);
		void SortDesc(asUINT startAt, asUINT count);
		void Sort(asUINT startAt, asUINT count, bool asc);
		void Sort(asIScriptFunction* less, asUINT startAt, asUINT count);
		void Reverse();
		int Find(void* value) const;
		int Find(asUINT startAt, void* value) const;
		int FindByRef(void* ref) const;
		int FindByRef(asUINT startAt, void* ref) const;

		// Return the address of internal buffer for direct manipulation of elements
		void* GetBuffer();

		// GC methods
		int GetRefCount();
		void SetFlag();
		bool GetFlag();
		void EnumReferences(asIScriptEngine* engine);
		void ReleaseAllHandles(asIScriptEngine* engine);

	protected:
		mutable int refCount;
		mutable bool gcFlag;
		asITypeInfo* objType;
		SArrayBuffer* buffer;
		int elementSize;
		int subTypeId;

		// Constructors
		CScriptArray(asITypeInfo* ot, void* initBuf); // Called from script when initialized with list
		CScriptArray(asUINT length, asITypeInfo* ot);
		CScriptArray(asUINT length, void* defVal, asITypeInfo* ot);
		CScriptArray(const CScriptArray& other);
		virtual ~CScriptArray();

		bool  Less(const void* a, const void* b, bool asc);
		void* GetArrayItemPointer(int index);
		void* GetDataPointer(void* buffer);
		void  Copy(void* dst, void* src);
		void  Swap(void* a, void* b);
		void  Precache();
		bool  CheckMaxSize(asUINT numElements);
		void  Resize(int delta, asUINT at);
		void  CreateBuffer(SArrayBuffer** buf, asUINT numElements);
		void  DeleteBuffer(SArrayBuffer* buf);
		void  CopyBuffer(SArrayBuffer* dst, SArrayBuffer* src);
		void  Construct(SArrayBuffer* buf, asUINT start, asUINT end);
		void  Destruct(SArrayBuffer* buf, asUINT start, asUINT end);
		bool  Equals(const void* a, const void* b, asIScriptContext* ctx, SArrayCache* cache) const;
	};

	void RegisterArray(asIScriptEngine* engine);
}

#endif
