#if defined(WITH_ANGELSCRIPT)

#include "RegisterDictionary.h"
#include "RegisterArray.h"
#include "../../Common.h"

#include <new>

using namespace Death::Containers::Literals;

namespace Jazz2::Scripting
{
	// We just define a number here that we assume nobody else is using for
	// object type user data. The add-ons have reserved the numbers 1000
	// through 1999 for this purpose, so we should be fine.
	const asPWORD DICTIONARY_CACHE = 1003;

	// This cache holds the object type of the dictionary type and array type
	// so it isn't necessary to look this up each time the dictionary or array
	// is created.
	struct SDictionaryCache
	{
		asITypeInfo* dictType;
		asITypeInfo* arrayType;
		asITypeInfo* keyType;

		// This is called from RegisterScriptDictionary
		static void Setup(asIScriptEngine* engine)
		{
			SDictionaryCache* cache = reinterpret_cast<SDictionaryCache*>(engine->GetUserData(DICTIONARY_CACHE));
			if (cache == nullptr) {
				cache = new SDictionaryCache;
				engine->SetUserData(cache, DICTIONARY_CACHE);
				engine->SetEngineUserDataCleanupCallback(SDictionaryCache::Cleanup, DICTIONARY_CACHE);

				cache->dictType = engine->GetTypeInfoByName("dictionary");
				cache->arrayType = engine->GetTypeInfoByDecl("array<string>");
				cache->keyType = engine->GetTypeInfoByDecl("string");
			}
		}

		// This is called from the engine when shutting down
		static void Cleanup(asIScriptEngine* engine)
		{
			SDictionaryCache* cache = reinterpret_cast<SDictionaryCache*>(engine->GetUserData(DICTIONARY_CACHE));
			if (cache != nullptr) {
				delete cache;
			}
		}
	};

	//--------------------------------------------------------------------------
	// CScriptDictionary implementation

	CScriptDictionary* CScriptDictionary::Create(asIScriptEngine* engine)
	{
		// Use the custom memory routine from AngelScript to allow application to better control how much memory is used
		CScriptDictionary* obj = (CScriptDictionary*)asAllocMem(sizeof(CScriptDictionary));
		new(obj) CScriptDictionary(engine);
		return obj;
	}

	CScriptDictionary* CScriptDictionary::Create(asBYTE* buffer)
	{
		// Use the custom memory routine from AngelScript to allow application to better control how much memory is used
		CScriptDictionary* obj = (CScriptDictionary*)asAllocMem(sizeof(CScriptDictionary));
		new(obj) CScriptDictionary(buffer);
		return obj;
	}

	CScriptDictionary::CScriptDictionary(asIScriptEngine* engine)
	{
		Init(engine);
	}

	void CScriptDictionary::Init(asIScriptEngine* e)
	{
		// We start with one reference
		refCount = 1;
		gcFlag = false;

		// Keep a reference to the engine for as long as we live
		// We don't increment the reference counter, because the
		// engine will hold a pointer to the object in the GC.
		engine = e;

		// The dictionary object type is cached to avoid dynamically parsing it each time
		SDictionaryCache* cache = reinterpret_cast<SDictionaryCache*>(engine->GetUserData(DICTIONARY_CACHE));

		// Notify the garbage collector of this object
		engine->NotifyGarbageCollectorOfNewObject(this, cache->dictType);
	}

	CScriptDictionary::CScriptDictionary(asBYTE* buffer)
	{
		// This constructor will always be called from a script
		// so we can get the engine from the active context
		asIScriptContext* ctx = asGetActiveContext();
		Init(ctx->GetEngine());

		// Determine if the dictionary key type is registered as reference type or value type
		SDictionaryCache& cache = *reinterpret_cast<SDictionaryCache*>(engine->GetUserData(DICTIONARY_CACHE));
		bool keyAsRef = cache.keyType->GetFlags() & asOBJ_REF ? true : false;

		// Initialize the dictionary from the buffer
		asUINT length = *(asUINT*)buffer;
		buffer += 4;

		while (length--) {
			// Align the buffer pointer on a 4 byte boundary in
			// case previous value was smaller than 4 bytes
			if (asPWORD(buffer) & 0x3) {
				buffer += 4 - (asPWORD(buffer) & 0x3);
			}
			// Get the name value pair from the buffer and insert it in the dictionary
			dictKey_t name;
			if (keyAsRef) {
				name = **(dictKey_t**)buffer;
				buffer += sizeof(dictKey_t*);
			} else {
				name = *(dictKey_t*)buffer;
				buffer += sizeof(dictKey_t);
			}

			// Get the type id of the value
			int typeId = *(int*)buffer;
			buffer += sizeof(int);

			// Depending on the type id, the value will inline in the buffer or a pointer
			void* ref = (void*)buffer;

			if (typeId >= asTYPEID_INT8 && typeId <= asTYPEID_DOUBLE) {
				// Convert primitive values to either int64 or double, so we can use the overloaded Set methods
				asINT64 i64;
				double d;
				switch (typeId) {
					case asTYPEID_INT8:   i64 = *(char*)ref; break;
					case asTYPEID_INT16:  i64 = *(short*)ref; break;
					case asTYPEID_INT32:  i64 = *(int*)ref; break;
					case asTYPEID_INT64:  i64 = *(asINT64*)ref; break;
					case asTYPEID_UINT8:  i64 = *(unsigned char*)ref; break;
					case asTYPEID_UINT16: i64 = *(unsigned short*)ref; break;
					case asTYPEID_UINT32: i64 = *(unsigned int*)ref; break;
					case asTYPEID_UINT64: i64 = *(asINT64*)ref; break;
					case asTYPEID_FLOAT:  d = *(float*)ref; break;
					case asTYPEID_DOUBLE: d = *(double*)ref; break;
				}

				if (typeId >= asTYPEID_FLOAT) {
					Set(name, d);
				} else {
					Set(name, i64);
				}
			} else {
				if ((typeId & asTYPEID_MASK_OBJECT) &&
					!(typeId & asTYPEID_OBJHANDLE) &&
					(engine->GetTypeInfoById(typeId)->GetFlags() & asOBJ_REF)) {
					// Dereference the pointer to get the reference to the actual object
					ref = *(void**)ref;
				}

				Set(name, ref, typeId);
			}

			// Advance the buffer pointer with the size of the value
			if (typeId & asTYPEID_MASK_OBJECT) {
				asITypeInfo* ti = engine->GetTypeInfoById(typeId);
				if (ti->GetFlags() & asOBJ_VALUE) {
					buffer += ti->GetSize();
				} else {
					buffer += sizeof(void*);
				}
			} else if (typeId == 0) {
				// null pointer
				buffer += sizeof(void*);
			} else {
				buffer += engine->GetSizeOfPrimitiveType(typeId);
			}
		}
	}

	CScriptDictionary::~CScriptDictionary()
	{
		// Delete all keys and values
		DeleteAll();
	}

	void CScriptDictionary::AddRef() const
	{
		// We need to clear the GC flag
		gcFlag = false;
		asAtomicInc(refCount);
	}

	void CScriptDictionary::Release() const
	{
		// We need to clear the GC flag
		gcFlag = false;
		if (asAtomicDec(refCount) == 0) {
			this->~CScriptDictionary();
			asFreeMem(const_cast<CScriptDictionary*>(this));
		}
	}

	int CScriptDictionary::GetRefCount()
	{
		return refCount;
	}

	void CScriptDictionary::SetGCFlag()
	{
		gcFlag = true;
	}

	bool CScriptDictionary::GetGCFlag()
	{
		return gcFlag;
	}

	void CScriptDictionary::EnumReferences(asIScriptEngine* inEngine)
	{
		// TODO: If garbage collection can be done from a separate thread, then this method must be
		//       protected so that it doesn't get lost during the iteration if the dictionary is modified

		// Call the gc enum callback for each of the objects
		dictMap_t::iterator it;
		for (it = dict.begin(); it != dict.end(); it++) {
			if (it->second.m_typeId & asTYPEID_MASK_OBJECT) {
				asITypeInfo* subType = engine->GetTypeInfoById(it->second.m_typeId);
				if ((subType->GetFlags() & asOBJ_VALUE) && (subType->GetFlags() & asOBJ_GC)) {
					// For value types we need to forward the enum callback
					// to the object so it can decide what to do
					engine->ForwardGCEnumReferences(it->second.m_valueObj, subType);
				} else {
					// For others, simply notify the GC about the reference
					inEngine->GCEnumCallback(it->second.m_valueObj);
				}
			}
		}
	}

	void CScriptDictionary::ReleaseAllReferences(asIScriptEngine* /*engine*/)
	{
		// We're being told to release all references in
		// order to break circular references for dead objects
		DeleteAll();
	}

	CScriptDictionary& CScriptDictionary::operator =(const CScriptDictionary& other)
	{
		// Clear everything we had before
		DeleteAll();

		// Do a shallow copy of the dictionary
		dictMap_t::const_iterator it;
		for (it = other.dict.begin(); it != other.dict.end(); it++) {
			if (it->second.m_typeId & asTYPEID_OBJHANDLE) {
				Set(it->first, (void*)&it->second.m_valueObj, it->second.m_typeId);
			} else if (it->second.m_typeId & asTYPEID_MASK_OBJECT) {
				Set(it->first, (void*)it->second.m_valueObj, it->second.m_typeId);
			} else {
				Set(it->first, (void*)&it->second.m_valueInt, it->second.m_typeId);
			}
		}

		return *this;
	}

	CScriptDictValue* CScriptDictionary::operator[](const dictKey_t& key)
	{
		// Return the existing value if it exists, else insert an empty value
		return &dict[key];
	}

	const CScriptDictValue* CScriptDictionary::operator[](const dictKey_t& key) const
	{
		// Return the existing value if it exists
		dictMap_t::const_iterator it;
		it = dict.find(key);
		if (it != dict.end()) {
			return &it->second;
		}
		// Else raise an exception
		asIScriptContext* ctx = asGetActiveContext();
		if (ctx != nullptr) {
			ctx->SetException("Invalid access to non-existing value");
		}
		return 0;
	}

	void CScriptDictionary::Set(const dictKey_t& key, void* value, int typeId)
	{
		dictMap_t::iterator it;
		it = dict.find(key);
		if (it == dict.end()) {
			it = dict.insert(dictMap_t::value_type(key, CScriptDictValue())).first;
		}
		it->second.Set(engine, value, typeId);
	}

	// This overloaded method is implemented so that all integer and
	// unsigned integers types will be stored in the dictionary as int64
	// through implicit conversions. This simplifies the management of the
	// numeric types when the script retrieves the stored value using a
	// different type.
	void CScriptDictionary::Set(const dictKey_t& key, const asINT64& value)
	{
		Set(key, const_cast<asINT64*>(&value), asTYPEID_INT64);
	}

	// This overloaded method is implemented so that all floating point types
	// will be stored in the dictionary as double through implicit conversions.
	// This simplifies the management of the numeric types when the script
	// retrieves the stored value using a different type.
	void CScriptDictionary::Set(const dictKey_t& key, const double& value)
	{
		Set(key, const_cast<double*>(&value), asTYPEID_DOUBLE);
	}

	// Returns true if the value was successfully retrieved
	bool CScriptDictionary::Get(const dictKey_t& key, void* value, int typeId) const
	{
		dictMap_t::const_iterator it;
		it = dict.find(key);
		if (it != dict.end()) {
			return it->second.Get(engine, value, typeId);
		}
		// AngelScript has already initialized the value with a default value,
		// so we don't have to do anything if we don't find the element, or if
		// the element is incompatible with the requested type.

		return false;
	}

	// Returns the type id of the stored value
	int CScriptDictionary::GetTypeId(const dictKey_t& key) const
	{
		dictMap_t::const_iterator it;
		it = dict.find(key);
		if (it != dict.end()) {
			return it->second.m_typeId;
		}
		return -1;
	}

	bool CScriptDictionary::Get(const dictKey_t& key, asINT64& value) const
	{
		return Get(key, &value, asTYPEID_INT64);
	}

	bool CScriptDictionary::Get(const dictKey_t& key, double& value) const
	{
		return Get(key, &value, asTYPEID_DOUBLE);
	}

	bool CScriptDictionary::Exists(const dictKey_t& key) const
	{
		dictMap_t::const_iterator it;
		it = dict.find(key);
		if (it != dict.end()) {
			return true;
		}
		return false;
	}

	bool CScriptDictionary::IsEmpty() const
	{
		if (dict.empty()) {
			return true;
		}
		return false;
	}

	asUINT CScriptDictionary::GetSize() const
	{
		return asUINT(dict.size());
	}

	bool CScriptDictionary::Delete(const dictKey_t& key)
	{
		dictMap_t::iterator it;
		it = dict.find(key);
		if (it != dict.end()) {
			it->second.FreeValue(engine);
			dict.erase(it);
			return true;
		}

		return false;
	}

	void CScriptDictionary::DeleteAll()
	{
		dictMap_t::iterator it;
		for (it = dict.begin(); it != dict.end(); it++) {
			it->second.FreeValue(engine);
		}
		dict.clear();
	}

	CScriptArray* CScriptDictionary::GetKeys() const
	{
		// Retrieve the object type for the array<string> from the cache
		SDictionaryCache* cache = reinterpret_cast<SDictionaryCache*>(engine->GetUserData(DICTIONARY_CACHE));
		asITypeInfo* ti = cache->arrayType;

		// Create the array object
		CScriptArray* array = CScriptArray::Create(ti, asUINT(dict.size()));
		long current = -1;
		dictMap_t::const_iterator it;
		for (it = dict.begin(); it != dict.end(); it++) {
			current++;
			*(dictKey_t*)array->At(current) = it->first;
		}

		return array;
	}

	CScriptDictValue::CScriptDictValue()
	{
		m_valueObj = 0;
		m_typeId = 0;
	}

	CScriptDictValue::CScriptDictValue(asIScriptEngine* engine, void* value, int typeId)
	{
		m_valueObj = 0;
		m_typeId = 0;
		Set(engine, value, typeId);
	}

	CScriptDictValue::~CScriptDictValue()
	{
		if (m_valueObj != nullptr && m_typeId != 0) {
			asIScriptContext* ctx = asGetActiveContext();
			if (ctx != nullptr) {
				FreeValue(ctx->GetEngine());
			} else {
				// Must not hold an object when destroyed, as then the object will never be freed
				RETURN_ASSERT((m_typeId & asTYPEID_MASK_OBJECT) == 0);
			}
		}
	}

	void CScriptDictValue::FreeValue(asIScriptEngine* engine)
	{
		// If it is a handle or a ref counted object, call release
		if (m_typeId & asTYPEID_MASK_OBJECT) {
			// Let the engine release the object
			engine->ReleaseScriptObject(m_valueObj, engine->GetTypeInfoById(m_typeId));
			m_valueObj = 0;
			m_typeId = 0;
		}

		// For primitives, there's nothing to do
	}

	void CScriptDictValue::EnumReferences(asIScriptEngine* inEngine)
	{
		// If we're holding a reference, we'll notify the garbage collector of it
		if (m_valueObj != nullptr) {
			inEngine->GCEnumCallback(m_valueObj);
		}
		// The object type itself is also garbage collected
		if (m_typeId != 0) {
			inEngine->GCEnumCallback(inEngine->GetTypeInfoById(m_typeId));
		}
	}

	void CScriptDictValue::Set(asIScriptEngine* engine, void* value, int typeId)
	{
		FreeValue(engine);

		m_typeId = typeId;
		if (typeId & asTYPEID_OBJHANDLE) {
			// We're receiving a reference to the handle, so we need to dereference it
			m_valueObj = *(void**)value;
			engine->AddRefScriptObject(m_valueObj, engine->GetTypeInfoById(typeId));
		} else if (typeId & asTYPEID_MASK_OBJECT) {
			// Create a copy of the object
			m_valueObj = engine->CreateScriptObjectCopy(value, engine->GetTypeInfoById(typeId));
			if (m_valueObj == 0) {
				asIScriptContext* ctx = asGetActiveContext();
				if (ctx != nullptr) {
					ctx->SetException("Cannot create copy of object");
				}
			}
		} else {
			// Copy the primitive value
			// We receive a pointer to the value.
			int size = engine->GetSizeOfPrimitiveType(typeId);
			memcpy(&m_valueInt, value, size);
		}
	}

	void CScriptDictValue::Set(asIScriptEngine* engine, CScriptDictValue& value)
	{
		if (value.m_typeId & asTYPEID_OBJHANDLE) {
			Set(engine, (void*)&value.m_valueObj, value.m_typeId);
		} else if (value.m_typeId & asTYPEID_MASK_OBJECT) {
			Set(engine, (void*)value.m_valueObj, value.m_typeId);
		} else {
			Set(engine, (void*)&value.m_valueInt, value.m_typeId);
		}
	}

	// This overloaded method is implemented so that all integer and
	// unsigned integers types will be stored in the dictionary as int64
	// through implicit conversions. This simplifies the management of the
	// numeric types when the script retrieves the stored value using a
	// different type.
	void CScriptDictValue::Set(asIScriptEngine* engine, const asINT64& value)
	{
		Set(engine, const_cast<asINT64*>(&value), asTYPEID_INT64);
	}

	// This overloaded method is implemented so that all floating point types
	// will be stored in the dictionary as double through implicit conversions.
	// This simplifies the management of the numeric types when the script
	// retrieves the stored value using a different type.
	void CScriptDictValue::Set(asIScriptEngine* engine, const double& value)
	{
		Set(engine, const_cast<double*>(&value), asTYPEID_DOUBLE);
	}

	bool CScriptDictValue::Get(asIScriptEngine* engine, void* value, int typeId) const
	{
		// Return the value
		if (typeId & asTYPEID_OBJHANDLE) {
			// A handle can be retrieved if the stored type is a handle of same or compatible type
			// or if the stored type is an object that implements the interface that the handle refer to.
			if ((m_typeId & asTYPEID_MASK_OBJECT)) {
				// Don't allow the get if the stored handle is to a const, but the desired handle is not
				if ((m_typeId & asTYPEID_HANDLETOCONST) && !(typeId & asTYPEID_HANDLETOCONST)) {
					return false;
				}
				// RefCastObject will increment the refcount if successful
				engine->RefCastObject(m_valueObj, engine->GetTypeInfoById(m_typeId), engine->GetTypeInfoById(typeId), reinterpret_cast<void**>(value));

				return true;
			}
		} else if (typeId & asTYPEID_MASK_OBJECT) {
			// Verify that the copy can be made
			bool isCompatible = false;

			// Allow a handle to be value assigned if the wanted type is not a handle
			if ((m_typeId & ~(asTYPEID_OBJHANDLE | asTYPEID_HANDLETOCONST)) == typeId && m_valueObj != 0) {
				isCompatible = true;
			}
			// Copy the object into the given reference
			if (isCompatible) {
				engine->AssignScriptObject(value, m_valueObj, engine->GetTypeInfoById(typeId));

				return true;
			}
		} else {
			if (m_typeId == typeId) {
				int size = engine->GetSizeOfPrimitiveType(typeId);
				memcpy(value, &m_valueInt, size);
				return true;
			}

			// We know all numbers are stored as either int64 or double, since we register overloaded functions for those
			// Only bool and enums needs to be treated separately
			if (typeId == asTYPEID_DOUBLE) {
				if (m_typeId == asTYPEID_INT64)
					*(double*)value = double(m_valueInt);
				else if (m_typeId == asTYPEID_BOOL) {
					// Use memcpy instead of type cast to make sure the code is endianess agnostic
					char localValue;
					memcpy(&localValue, &m_valueInt, sizeof(char));
					*(double*)value = localValue ? 1.0 : 0.0;
				} else if (m_typeId > asTYPEID_DOUBLE && (m_typeId & asTYPEID_MASK_OBJECT) == 0) {
					// Use memcpy instead of type cast to make sure the code is endianess agnostic
					int localValue;
					memcpy(&localValue, &m_valueInt, sizeof(int));
					*(double*)value = double(localValue); // enums are 32bit
				} else {
					// The stored type is an object
					// TODO: Check if the object has a conversion operator to a primitive value
					*(double*)value = 0;
					return false;
				}
				return true;
			} else if (typeId == asTYPEID_INT64) {
				if (m_typeId == asTYPEID_DOUBLE)
					*(asINT64*)value = asINT64(m_valueFlt);
				else if (m_typeId == asTYPEID_BOOL) {
					// Use memcpy instead of type cast to make sure the code is endianess agnostic
					char localValue;
					memcpy(&localValue, &m_valueInt, sizeof(char));
					*(asINT64*)value = localValue ? 1 : 0;
				} else if (m_typeId > asTYPEID_DOUBLE && (m_typeId & asTYPEID_MASK_OBJECT) == 0) {
					// Use memcpy instead of type cast to make sure the code is endianess agnostic
					int localValue;
					memcpy(&localValue, &m_valueInt, sizeof(int));
					*(asINT64*)value = localValue; // enums are 32bit
				} else {
					// The stored type is an object
					// TODO: Check if the object has a conversion operator to a primitive value
					*(asINT64*)value = 0;
					return false;
				}
				return true;
			} else if (typeId > asTYPEID_DOUBLE && (m_typeId & asTYPEID_MASK_OBJECT) == 0) {
				// The desired type is an enum. These are always 32bit integers
				if (m_typeId == asTYPEID_DOUBLE) {
					*(int*)value = int(m_valueFlt);
				} else if (m_typeId == asTYPEID_INT64) {
					*(int*)value = int(m_valueInt);
				} else if (m_typeId == asTYPEID_BOOL) {
					// Use memcpy instead of type cast to make sure the code is endianess agnostic
					char localValue;
					memcpy(&localValue, &m_valueInt, sizeof(char));
					*(int*)value = localValue ? 1 : 0;
				} else if (m_typeId > asTYPEID_DOUBLE && (m_typeId & asTYPEID_MASK_OBJECT) == 0) {
					// Use memcpy instead of type cast to make sure the code is endianess agnostic
					int localValue;
					memcpy(&localValue, &m_valueInt, sizeof(int));
					*(int*)value = localValue; // enums are 32bit
				} else {
					// The stored type is an object
					// TODO: Check if the object has a conversion operator to a primitive value
					*(int*)value = 0;
					return false;
				}
				return true;
			} else if (typeId == asTYPEID_BOOL) {
				if (m_typeId & asTYPEID_OBJHANDLE) {
					// TODO: Check if the object has a conversion operator to a primitive value
					*(bool*)value = m_valueObj ? true : false;
				} else if (m_typeId & asTYPEID_MASK_OBJECT) {
					// TODO: Check if the object has a conversion operator to a primitive value
					*(bool*)value = true;
				} else {
					// Compare only the bytes that were actually set
					asQWORD zero = 0;
					int size = engine->GetSizeOfPrimitiveType(m_typeId);
					*(bool*)value = memcmp(&m_valueInt, &zero, size) == 0 ? false : true;
				}
				return true;
			}
		}

		// It was not possible to retrieve the value using the desired typeId
		return false;
	}

	const void* CScriptDictValue::GetAddressOfValue() const
	{
		if ((m_typeId & asTYPEID_MASK_OBJECT) && !(m_typeId & asTYPEID_OBJHANDLE)) {
			// Return the address to the object directly
			return m_valueObj;
		}

		// Return the address of the primitive or the pointer to the object
		return reinterpret_cast<const void*>(&m_valueObj);
	}

	bool CScriptDictValue::Get(asIScriptEngine* engine, asINT64& value) const
	{
		return Get(engine, &value, asTYPEID_INT64);
	}

	bool CScriptDictValue::Get(asIScriptEngine* engine, double& value) const
	{
		return Get(engine, &value, asTYPEID_DOUBLE);
	}

	int CScriptDictValue::GetTypeId() const
	{
		return m_typeId;
	}

	static void CScriptDictValue_Construct(void* mem)
	{
		new(mem) CScriptDictValue();
	}

	static void CScriptDictValue_Destruct(CScriptDictValue* obj)
	{
		asIScriptContext* ctx = asGetActiveContext();
		if (ctx != nullptr) {
			asIScriptEngine* engine = ctx->GetEngine();
			obj->FreeValue(engine);
		}
		obj->~CScriptDictValue();
	}

	static CScriptDictValue& CScriptDictValue_opAssign(void* ref, int typeId, CScriptDictValue* obj)
	{
		asIScriptContext* ctx = asGetActiveContext();
		if (ctx != nullptr) {
			asIScriptEngine* engine = ctx->GetEngine();
			obj->Set(engine, ref, typeId);
		}
		return *obj;
	}

	static CScriptDictValue& CScriptDictValue_opAssign(const CScriptDictValue& other, CScriptDictValue* obj)
	{
		asIScriptContext* ctx = asGetActiveContext();
		if (ctx != nullptr) {
			asIScriptEngine* engine = ctx->GetEngine();
			obj->Set(engine, const_cast<CScriptDictValue&>(other));
		}

		return *obj;
	}

	static CScriptDictValue& CScriptDictValue_opAssign(double val, CScriptDictValue* obj)
	{
		return CScriptDictValue_opAssign(&val, asTYPEID_DOUBLE, obj);
	}

	static CScriptDictValue& CScriptDictValue_opAssign(asINT64 val, CScriptDictValue* obj)
	{
		return CScriptDictValue_opAssign(&val, asTYPEID_INT64, obj);
	}

	static void CScriptDictValue_opCast(void* ref, int typeId, CScriptDictValue* obj)
	{
		asIScriptContext* ctx = asGetActiveContext();
		if (ctx != nullptr) {
			asIScriptEngine* engine = ctx->GetEngine();
			obj->Get(engine, ref, typeId);
		}
	}

	static asINT64 CScriptDictValue_opConvInt(CScriptDictValue* obj)
	{
		asINT64 value;
		CScriptDictValue_opCast(&value, asTYPEID_INT64, obj);
		return value;
	}

	static double CScriptDictValue_opConvDouble(CScriptDictValue* obj)
	{
		double value;
		CScriptDictValue_opCast(&value, asTYPEID_DOUBLE, obj);
		return value;
	}

	void ScriptDictionaryFactory_Generic(asIScriptGeneric* gen)
	{
		*(CScriptDictionary**)gen->GetAddressOfReturnLocation() = CScriptDictionary::Create(gen->GetEngine());
	}

	void ScriptDictionaryListFactory_Generic(asIScriptGeneric* gen)
	{
		asBYTE* buffer = (asBYTE*)gen->GetArgAddress(0);
		*(CScriptDictionary**)gen->GetAddressOfReturnLocation() = CScriptDictionary::Create(buffer);
	}

	void RegisterDictionary(asIScriptEngine* engine)
	{
		int r;

		// The array<string> type must be available
		RETURN_ASSERT(engine->GetTypeInfoByDecl("array<string>"));

#if AS_CAN_USE_CPP11
		// With C++11 it is possible to use asGetTypeTraits to automatically determine the correct flags that represents the C++ class
		r = engine->RegisterObjectType("dictionaryValue", sizeof(CScriptDictValue), asOBJ_VALUE | asOBJ_ASHANDLE | asOBJ_GC | asGetTypeTraits<CScriptDictValue>()); RETURN_ASSERT(r >= 0);
#else
		r = engine->RegisterObjectType("dictionaryValue", sizeof(CScriptDictValue), asOBJ_VALUE | asOBJ_ASHANDLE | asOBJ_GC | asOBJ_APP_CLASS_CD); RETURN_ASSERT(r >= 0);
#endif
		r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(CScriptDictValue_Construct), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(CScriptDictValue_Destruct), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_ENUMREFS, "void f(int&in)", asMETHOD(CScriptDictValue, EnumReferences), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionaryValue", asBEHAVE_RELEASEREFS, "void f(int&in)", asMETHOD(CScriptDictValue, FreeValue), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(const dictionaryValue &in)", asFUNCTIONPR(CScriptDictValue_opAssign, (const CScriptDictValue&, CScriptDictValue*), CScriptDictValue&), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opHndlAssign(const ?&in)", asFUNCTIONPR(CScriptDictValue_opAssign, (void*, int, CScriptDictValue*), CScriptDictValue&), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opHndlAssign(const dictionaryValue &in)", asFUNCTIONPR(CScriptDictValue_opAssign, (const CScriptDictValue&, CScriptDictValue*), CScriptDictValue&), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(const ?&in)", asFUNCTIONPR(CScriptDictValue_opAssign, (void*, int, CScriptDictValue*), CScriptDictValue&), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(double)", asFUNCTIONPR(CScriptDictValue_opAssign, (double, CScriptDictValue*), CScriptDictValue&), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "dictionaryValue &opAssign(int64)", asFUNCTIONPR(CScriptDictValue_opAssign, (asINT64, CScriptDictValue*), CScriptDictValue&), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "void opCast(?&out)", asFUNCTIONPR(CScriptDictValue_opCast, (void*, int, CScriptDictValue*), void), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "void opConv(?&out)", asFUNCTIONPR(CScriptDictValue_opCast, (void*, int, CScriptDictValue*), void), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "int64 opConv()", asFUNCTIONPR(CScriptDictValue_opConvInt, (CScriptDictValue*), asINT64), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionaryValue", "double opConv()", asFUNCTIONPR(CScriptDictValue_opConvDouble, (CScriptDictValue*), double), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectType("dictionary", sizeof(CScriptDictionary), asOBJ_REF | asOBJ_GC); RETURN_ASSERT(r >= 0);
		// Use the generic interface to construct the object since we need the engine pointer, we could also have retrieved the engine pointer from the active context
		r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_FACTORY, "dictionary@ f()", asFUNCTION(ScriptDictionaryFactory_Generic), asCALL_GENERIC); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_LIST_FACTORY, "dictionary @f(int &in) {repeat {string, ?}}", asFUNCTION(ScriptDictionaryListFactory_Generic), asCALL_GENERIC); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_ADDREF, "void f()", asMETHOD(CScriptDictionary, AddRef), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_RELEASE, "void f()", asMETHOD(CScriptDictionary, Release), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("dictionary", "dictionary &opAssign(const dictionary &in)", asMETHODPR(CScriptDictionary, operator=, (const CScriptDictionary&), CScriptDictionary&), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("dictionary", "void set(const string &in, const ?&in)", asMETHODPR(CScriptDictionary, Set, (const dictKey_t&, void*, int), void), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionary", "bool get(const string &in, ?&out) const", asMETHODPR(CScriptDictionary, Get, (const dictKey_t&, void*, int) const, bool), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("dictionary", "void set(const string &in, const int64&in)", asMETHODPR(CScriptDictionary, Set, (const dictKey_t&, const asINT64&), void), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionary", "bool get(const string &in, int64&out) const", asMETHODPR(CScriptDictionary, Get, (const dictKey_t&, asINT64&) const, bool), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("dictionary", "void set(const string &in, const double&in)", asMETHODPR(CScriptDictionary, Set, (const dictKey_t&, const double&), void), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionary", "bool get(const string &in, double&out) const", asMETHODPR(CScriptDictionary, Get, (const dictKey_t&, double&) const, bool), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("dictionary", "bool exists(const string &in) const", asMETHOD(CScriptDictionary, Exists), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionary", "bool isEmpty() const", asMETHOD(CScriptDictionary, IsEmpty), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionary", "uint getSize() const", asMETHOD(CScriptDictionary, GetSize), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionary", "bool delete(const string &in)", asMETHOD(CScriptDictionary, Delete), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionary", "void deleteAll()", asMETHOD(CScriptDictionary, DeleteAll), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("dictionary", "array<string> @getKeys() const", asMETHOD(CScriptDictionary, GetKeys), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("dictionary", "dictionaryValue &opIndex(const string &in)", asMETHODPR(CScriptDictionary, operator[], (const dictKey_t&), CScriptDictValue*), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("dictionary", "const dictionaryValue &opIndex(const string &in) const", asMETHODPR(CScriptDictionary, operator[], (const dictKey_t&) const, const CScriptDictValue*), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

		// Register GC behaviours
		r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_GETREFCOUNT, "int f()", asMETHOD(CScriptDictionary, GetRefCount), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_SETGCFLAG, "void f()", asMETHOD(CScriptDictionary, SetGCFlag), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_GETGCFLAG, "bool f()", asMETHOD(CScriptDictionary, GetGCFlag), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_ENUMREFS, "void f(int&in)", asMETHOD(CScriptDictionary, EnumReferences), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("dictionary", asBEHAVE_RELEASEREFS, "void f(int&in)", asMETHOD(CScriptDictionary, ReleaseAllReferences), asCALL_THISCALL); RETURN_ASSERT(r >= 0);

#if AS_USE_STLNAMES == 1
		// Same as isEmpty
		r = engine->RegisterObjectMethod("dictionary", "bool empty() const", asMETHOD(CScriptDictionary, IsEmpty), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		// Same as getSize
		r = engine->RegisterObjectMethod("dictionary", "uint size() const", asMETHOD(CScriptDictionary, GetSize), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		// Same as delete
		r = engine->RegisterObjectMethod("dictionary", "void erase(const string &in)", asMETHOD(CScriptDictionary, Delete), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		// Same as deleteAll
		r = engine->RegisterObjectMethod("dictionary", "void clear()", asMETHOD(CScriptDictionary, DeleteAll), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
#endif

		// Cache some things the dictionary will need at runtime
		SDictionaryCache::Setup(engine);
	}

	CScriptDictionary::CIterator CScriptDictionary::begin() const
	{
		return CIterator(*this, dict.begin());
	}

	CScriptDictionary::CIterator CScriptDictionary::end() const
	{
		return CIterator(*this, dict.end());
	}

	CScriptDictionary::CIterator CScriptDictionary::find(const dictKey_t& key) const
	{
		return CIterator(*this, dict.find(key));
	}

	CScriptDictionary::CIterator::CIterator(const CScriptDictionary& dict, dictMap_t::const_iterator it): m_it(it), m_dict(dict)
	{
	}

	void CScriptDictionary::CIterator::operator++()
	{
		++m_it;
	}

	void CScriptDictionary::CIterator::operator++(int)
	{
		++m_it;

		// Normally the post increment would return a copy of the object with the original state,
		// but it is rarely used so we skip this extra copy to avoid unnecessary overhead
	}

	CScriptDictionary::CIterator& CScriptDictionary::CIterator::operator*()
	{
		return *this;
	}

	bool CScriptDictionary::CIterator::operator==(const CIterator& other) const
	{
		return m_it == other.m_it;
	}

	bool CScriptDictionary::CIterator::operator!=(const CIterator& other) const
	{
		return m_it != other.m_it;
	}

	const dictKey_t& CScriptDictionary::CIterator::GetKey() const
	{
		return m_it->first;
	}

	int CScriptDictionary::CIterator::GetTypeId() const
	{
		return m_it->second.m_typeId;
	}

	bool CScriptDictionary::CIterator::GetValue(asINT64& value) const
	{
		return m_it->second.Get(m_dict.engine, &value, asTYPEID_INT64);
	}

	bool CScriptDictionary::CIterator::GetValue(double& value) const
	{
		return m_it->second.Get(m_dict.engine, &value, asTYPEID_DOUBLE);
	}

	bool CScriptDictionary::CIterator::GetValue(void* value, int typeId) const
	{
		return m_it->second.Get(m_dict.engine, value, typeId);
	}

	const void* CScriptDictionary::CIterator::GetAddressOfValue() const
	{
		return m_it->second.GetAddressOfValue();
	}
}

#endif