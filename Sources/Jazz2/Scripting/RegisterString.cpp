#if defined(WITH_ANGELSCRIPT)

#include "RegisterString.h"
#include "../../Common.h"

#include "../../nCine/Base/Algorithms.h"
#include "../../nCine/Base/HashMap.h"

#include <memory>
#include <new>

#if !defined(DEATH_TARGET_ANDROID) && !defined(_WIN32_WCE) && !defined(__psp2__)
#	include <locale.h>		// setlocale()
#endif

#define AS_USE_ACCESSORS 1

#include <Containers/StringConcatenable.h>

using namespace Death::Containers;
using namespace Death::Containers::Literals;
using namespace nCine;

namespace Jazz2::Scripting
{
	class StringFactory : public asIStringFactory
	{
	public:
		struct InternalData {
			std::unique_ptr<String> Data;
			int RefCount;

			InternalData(int refCount) : RefCount(refCount) { }
		};

		StringFactory() { }
		~StringFactory()
		{
			// The script engine must release each string constant that it has requested
			ASSERT(_stringCache.size() == 0);
		}

		const void* GetStringConstant(const char* data, asUINT length)
		{
			// The string factory might be modified from multiple threads, so it is necessary to use a mutex
			asAcquireExclusiveLock();

			StringView stringView(data, length);
			auto it = _stringCache.find(String::nullTerminatedView(stringView));
			if (it != _stringCache.end()) {
				it->second.RefCount++;;
			} else {
				it = _stringCache.emplace(String(stringView), 1).first;
				it->second.Data = std::make_unique<String>(stringView);
			}
			asReleaseExclusiveLock();

			return reinterpret_cast<const void*>(it->second.Data.get());
		}

		int ReleaseStringConstant(const void* str)
		{
			if (str == nullptr) return asERROR;

			int ret = asSUCCESS;

			// The string factory might be modified from multiple threads, so it is necessary to use a mutex
			asAcquireExclusiveLock();

			auto it = _stringCache.find(*reinterpret_cast<const String*>(str));
			if (it == _stringCache.end()) {
				ret = asERROR;
			} else {
				it->second.RefCount--;
				if (it->second.RefCount == 0) {
					_stringCache.erase(it);
				}
			}

			asReleaseExclusiveLock();

			return ret;
		}

		int GetRawStringData(const void* str, char* data, asUINT* length) const
		{
			if (str == nullptr) return asERROR;

			const String* string = reinterpret_cast<const String*>(str);
			if (length) {
				*length = (asUINT)string->size();
			}
			if (data != nullptr) {
				memcpy(data, string->data(), string->size());
			}
			return asSUCCESS;
		}

		HashMap<String, InternalData> _stringCache;
	};

	static StringFactory* _stringFactory = nullptr;

	StringFactory* GetStringFactoryInstance()
	{
		if (_stringFactory == nullptr) {
			// The following instance will be destroyed by the global StringFactoryCleaner instance upon application shutdown
			_stringFactory = new StringFactory();
		}
		return _stringFactory;
	}

	class StringFactoryCleaner
	{
	public:
		~StringFactoryCleaner()
		{
			if (_stringFactory != nullptr) {
				// Only delete the string factory if the stringCache is empty. If it is not empty, it means that someone might still attempt
				// to release string constants, so if we delete the string factory, the application might crash. Not deleting the cache would
				// lead to a memory leak, but since this is only happens when the application is shutting down anyway, it is not important.
				if (_stringFactory->_stringCache.empty()) {
					delete _stringFactory;
					_stringFactory = nullptr;
				}
			}
		}
	};

	static StringFactoryCleaner cleaner;

	static void ConstructString(String* thisPointer)
	{
		new(thisPointer) String();
	}

	static void CopyConstructString(const String& other, String* thisPointer)
	{
		new(thisPointer) String(other);
	}

	static void DestructString(String* thisPointer)
	{
		thisPointer->~String();
	}

	static String& AddAssignStringToString(const String& str, String& dest)
	{
		// We don't register the method directly because some compilers and standard libraries inline the definition,
		// resulting in the linker being unable to find the declaration. Example: CLang/LLVM with XCode 4.3 on OSX 10.7
		dest += str;
		return dest;
	}

	// bool string::empty()
	static bool StringIsEmpty(const String& str)
	{
		// We don't register the method directly because some compilers
		// and standard libraries inline the definition, resulting in the
		// linker being unable to find the declaration
		// Example: CLang/LLVM with XCode 4.3 on OSX 10.7
		return str.empty();
	}

	static String& AssignUInt64ToString(asQWORD i, String& dest)
	{
		char tmpBuffer[32];
		u64tos(i, tmpBuffer);
		dest = String(tmpBuffer);
		return dest;
	}

	static String& AddAssignUInt64ToString(asQWORD i, String& dest)
	{
		char tmpBuffer[32];
		u64tos(i, tmpBuffer);
		dest += StringView(tmpBuffer);
		return dest;
	}

	static String AddStringUInt64(const String& str, asQWORD i)
	{
		char tmpBuffer[32];
		u64tos(i, tmpBuffer);
		return str + StringView(tmpBuffer);
	}

	static String AddInt64String(asINT64 i, const String& str)
	{
		char tmpBuffer[32];
		i64tos(i, tmpBuffer);
		return StringView(tmpBuffer) + str;
	}

	static String& AssignInt64ToString(asINT64 i, String& dest)
	{
		char tmpBuffer[32];
		i64tos(i, tmpBuffer);
		dest = String(tmpBuffer);
		return dest;
	}

	static String& AddAssignInt64ToString(asINT64 i, String& dest)
	{
		char tmpBuffer[32];
		i64tos(i, tmpBuffer);
		dest += StringView(tmpBuffer);
		return dest;
	}

	static String AddStringInt64(const String& str, asINT64 i)
	{
		char tmpBuffer[32];
		i64tos(i, tmpBuffer);
		return str + StringView(tmpBuffer);
	}

	static String AddUInt64String(asQWORD i, const String& str)
	{
		char tmpBuffer[32];
		u64tos(i, tmpBuffer);
		return StringView(tmpBuffer) + str;
	}

	static String& AssignDoubleToString(double f, String& dest)
	{
		char tmpBuffer[32];
		ftos(f, tmpBuffer, countof(tmpBuffer));
		dest = String(tmpBuffer);
		return dest;
	}

	static String& AddAssignDoubleToString(double f, String& dest)
	{
		char tmpBuffer[32];
		ftos(f, tmpBuffer, countof(tmpBuffer));
		dest += StringView(tmpBuffer);
		return dest;
	}

	static String& AssignFloatToString(float f, String& dest)
	{
		char tmpBuffer[32];
		ftos(f, tmpBuffer, countof(tmpBuffer));
		dest = String(tmpBuffer);
		return dest;
	}

	static String& AddAssignFloatToString(float f, String& dest)
	{
		char tmpBuffer[32];
		ftos(f, tmpBuffer, countof(tmpBuffer));
		dest += StringView(tmpBuffer);
		return dest;
	}

	static String& AssignBoolToString(bool b, String& dest)
	{
		dest = (b ? "true"_s : "false"_s);
		return dest;
	}

	static String& AddAssignBoolToString(bool b, String& dest)
	{
		dest += (b ? "true"_s : "false"_s);
		return dest;
	}

	static String AddStringDouble(const String& str, double f)
	{
		char tmpBuffer[32];
		ftos(f, tmpBuffer, countof(tmpBuffer));
		return str + StringView(tmpBuffer);
	}

	static String AddDoubleString(double f, const String& str)
	{
		char tmpBuffer[32];
		ftos(f, tmpBuffer, countof(tmpBuffer));
		return StringView(tmpBuffer) + str;
	}

	static String AddStringFloat(const String& str, float f)
	{
		char tmpBuffer[32];
		ftos(f, tmpBuffer, countof(tmpBuffer));
		return str + StringView(tmpBuffer);
	}

	static String AddFloatString(float f, const String& str)
	{
		char tmpBuffer[32];
		ftos(f, tmpBuffer, countof(tmpBuffer));
		return StringView(tmpBuffer) + str;
	}

	static String AddStringBool(const String& str, bool b)
	{
		return str + (b ? "true"_s : "false"_s);
	}

	static String AddBoolString(bool b, const String& str)
	{
		return (b ? "true"_s : "false"_s) + str;
	}

	static char* StringCharAt(unsigned int i, String& str)
	{
		if (i >= str.size()) {
			asIScriptContext* ctx = asGetActiveContext();
			ctx->SetException("Out of range");
			return nullptr;
		}

		return &str[i];
	}

	// int string::opCmp(const string &in) const
	static int StringCmp(const String& a, const String& b)
	{
		int cmp = 0;
		if (a < b) cmp = -1;
		else if (a > b) cmp = 1;
		return cmp;
	}

	// This function returns the index of the first position where the substring
	// exists in the input string. If the substring doesn't exist in the input
	// string -1 is returned.
	//
	// int string::findFirst(const string &in sub, uint start = 0) const
	static int StringFindFirst(const String& sub, asUINT start, const String& str)
	{
		auto found = str.exceptPrefix(start).find(sub);
		return (found != nullptr ? (int)(found.begin() - str.begin()) : -1);
	}

	// This function returns the index of the first position where the one of the bytes in substring
	// exists in the input string. If the characters in the substring doesn't exist in the input
	// string -1 is returned.
	//
	// int string::findFirstOf(const string &in sub, uint start = 0) const
	static int StringFindFirstOf(const String& sub, asUINT start, const String& str)
	{
		auto found = str.exceptPrefix(start).findAny(sub);
		return (found != nullptr ? (int)(found.begin() - str.begin()) : -1);
	}

	// This function returns the index of the last position where the one of the bytes in substring
	// exists in the input string. If the characters in the substring doesn't exist in the input
	// string -1 is returned.
	//
	// int string::findLastOf(const string &in sub, uint start = 0) const
	static int StringFindLastOf(const String& sub, asUINT start, const String& str)
	{
		auto found = str.exceptSuffix(start).findLastAny(sub);
		return (found != nullptr ? (int)(found.begin() - str.begin()) : -1);
	}

	/*
	// This function returns the index of the first position where a byte other than those in substring
	// exists in the input string. If none is found -1 is returned.
	//
	// int string::findFirstNotOf(const string &in sub, uint start = 0) const
	static int StringFindFirstNotOf(const String& sub, asUINT start, const String& str)
	{
		// We don't register the method directly because the argument types change between 32-bit and 64-bit platforms
		return (int)str.find_first_not_of(sub, (size_t)(start < 0 ? string::npos : start));
	}

	// This function returns the index of the last position where a byte other than those in substring
	// exists in the input string. If none is found -1 is returned.
	//
	// int string::findLastNotOf(const string &in sub, uint start = -1) const
	static int StringFindLastNotOf(const String& sub, asUINT start, const String& str)
	{
		// We don't register the method directly because the argument types change between 32-bit and 64-bit platforms
		return (int)str.find_last_not_of(sub, (size_t)(start < 0 ? string::npos : start));
	}*/

	// This function returns the index of the last position where the substring
	// exists in the input string. If the substring doesn't exist in the input
	// string -1 is returned.
	//
	// int string::findLast(const string &in sub, int start = 0) const
	static int StringFindLast(const String& sub, int start, const String& str)
	{
		auto found = str.exceptSuffix(start).findLast(sub);
		return (found != nullptr ? (int)(found.begin() - str.begin()) : -1);
	}

	/*
	// void string::insert(uint pos, const string &in other)
	static void StringInsert(unsigned int pos, const String& other, String& str)
	{
		// We don't register the method directly because the argument types change between 32-bit and 64-bit platforms
		str.insert(pos, other);
	}

	// void string::erase(uint pos, int count = -1)
	static void StringErase(unsigned int pos, int count, String& str)
	{
		// We don't register the method directly because the argument types change between 32-bit and 64-bit platforms
		str.erase(pos, (size_t)(count < 0 ? string::npos : count));
	}*/

	// uint string::size() const
	static asUINT StringSize(const String& str)
	{
		// We don't register the method directly because the return type changes between 32-bit and 64-bit platforms
		return (asUINT)str.size();
	}

	// void string::resize(uint l)
	/*static void StringResize(asUINT l, String& str)
	{
		// We don't register the method directly because the argument types change between 32bit and 64bit platforms
		str.resize(l);
	}*/

	// string formatInt(int64 val, const string &in options, uint width)
	static String formatInt(asINT64 value, const String& options, asUINT width)
	{
		bool leftJustify = options.find("l") != nullptr;
		bool padWithZero = options.find("0") != nullptr;
		bool alwaysSign = options.find("+") != nullptr;
		bool spaceOnSign = options.find(" ") != nullptr;
		bool hexSmall = options.find("h") != nullptr;
		bool hexLarge = options.find("H") != nullptr;

		String fmt = "%";
		if (leftJustify) fmt += "-";
		if (alwaysSign) fmt += "+";
		if (spaceOnSign) fmt += " ";
		if (padWithZero) fmt += "0";

#if defined(DEATH_TARGET_WINDOWS)
		fmt += "*I64";
#elif defined(_LP64)
		fmt += "*l";
#else
		fmt += "*ll";
#endif

		if (hexSmall) fmt += "x";
		else if (hexLarge) fmt += "X";
		else fmt += "d";

		char buffer[64];
#if defined(DEATH_TARGET_MSVC)
		// MSVC 8.0 / 2005 or newer
		sprintf_s(buffer, sizeof(buffer), fmt.data(), width, value);
#else
		sprintf(buffer, fmt.data(), width, value);
#endif
		return buffer;
	}

	// string formatUInt(uint64 val, const string &in options, uint width)
	static String formatUInt(asQWORD value, const String& options, asUINT width)
	{
		bool leftJustify = options.find('l') != nullptr;
		bool padWithZero = options.find('0') != nullptr;
		bool alwaysSign = options.find('+') != nullptr;
		bool spaceOnSign = options.find(' ') != nullptr;
		bool hexSmall = options.find('h') != nullptr;
		bool hexLarge = options.find('H') != nullptr;

		String fmt = "%";
		if (leftJustify) fmt += "-";
		if (alwaysSign) fmt += "+";
		if (spaceOnSign) fmt += " ";
		if (padWithZero) fmt += "0";

#if defined(DEATH_TARGET_WINDOWS)
		fmt += "*I64";
#elif defined(_LP64)
		fmt += "*l";
#else
		fmt += "*ll";
#endif

		if (hexSmall) fmt += "x";
		else if (hexLarge) fmt += "X";
		else fmt += "u";

		char buffer[64];
#if defined(DEATH_TARGET_MSVC)
		// MSVC 8.0 / 2005 or newer
		sprintf_s(buffer, sizeof(buffer), fmt.data(), width, value);
#else
		sprintf(buffer, fmt.data(), width, value);
#endif
		return buffer;
	}

	// string formatFloat(double val, const string &in options, uint width, uint precision)
	static String formatFloat(double value, const String& options, asUINT width, asUINT precision)
	{
		bool leftJustify = options.find("l") != nullptr;
		bool padWithZero = options.find("0") != nullptr;
		bool alwaysSign = options.find("+") != nullptr;
		bool spaceOnSign = options.find(" ") != nullptr;
		bool expSmall = options.find("e") != nullptr;
		bool expLarge = options.find("E") != nullptr;

		String fmt = "%";
		if (leftJustify) fmt += "-";
		if (alwaysSign) fmt += "+";
		if (spaceOnSign) fmt += " ";
		if (padWithZero) fmt += "0";

		fmt += "*.*";

		if (expSmall) fmt += "e";
		else if (expLarge) fmt += "E";
		else fmt += "f";

		char buffer[64];
#if defined(DEATH_TARGET_MSVC)
		// MSVC 8.0 / 2005 or newer
		sprintf_s(buffer, sizeof(buffer), fmt.data(), width, precision, value);
#else
		sprintf(buffer, fmt.data(), width, precision, value);
#endif
		return buffer;
	}

	// int64 parseInt(const string &in val, uint base = 10, uint &out byteCount = 0)
	static asINT64 parseInt(const String& val, asUINT base, asUINT* byteCount)
	{
		if (base != 10 && base != 16) {
			if (byteCount) *byteCount = 0;
			return 0;
		}

		const char* end = val.data();

		bool sign = false;
		if (*end == '-') {
			sign = true;
			end++;
		} else if (*end == '+') {
			end++;
		}

		asINT64 res = 0;
		if (base == 10) {
			while (*end >= '0' && *end <= '9') {
				res *= 10;
				res += *end++ - '0';
			}
		} else if (base == 16) {
			while ((*end >= '0' && *end <= '9') ||
					(*end >= 'a' && *end <= 'f') ||
					(*end >= 'A' && *end <= 'F')) {
				res *= 16;
				if (*end >= '0' && *end <= '9') {
					res += *end++ - '0';
				} else if (*end >= 'a' && *end <= 'f') {
					res += *end++ - 'a' + 10;
				} else if (*end >= 'A' && *end <= 'F') {
					res += *end++ - 'A' + 10;
				}
			}
		}

		if (byteCount) {
			*byteCount = asUINT(size_t(end - val.data()));
		}
		if (sign) {
			res = -res;
		}
		return res;
	}

	// uint64 parseUInt(const string &in val, uint base = 10, uint &out byteCount = 0)
	static asQWORD parseUInt(const String& val, asUINT base, asUINT* byteCount)
	{
		if (base != 10 && base != 16) {
			if (byteCount) *byteCount = 0;
			return 0;
		}

		const char* end = val.data();

		asQWORD res = 0;
		if (base == 10) {
			while (*end >= '0' && *end <= '9') {
				res *= 10;
				res += *end++ - '0';
			}
		} else if (base == 16) {
			while ((*end >= '0' && *end <= '9') ||
				(*end >= 'a' && *end <= 'f') ||
				(*end >= 'A' && *end <= 'F')) {
				res *= 16;
				if (*end >= '0' && *end <= '9') {
					res += *end++ - '0';
				} else if (*end >= 'a' && *end <= 'f') {
					res += *end++ - 'a' + 10;
				} else if (*end >= 'A' && *end <= 'F') {
					res += *end++ - 'A' + 10;
				}
			}
		}

		if (byteCount) {
			*byteCount = asUINT(size_t(end - val.data()));
		}
		return res;
	}

	// double parseFloat(const string &in val, uint &out byteCount = 0)
	double parseFloat(const String& val, asUINT* byteCount)
	{
		char* end;

		// WinCE doesn't have setlocale. Some quick testing on my current platform still manages to parse the numbers
		// such as "3.14" even if the decimal for the locale is ",".
#if !defined(DEATH_TARGET_ANDROID) && !defined(_WIN32_WCE) && !defined(__psp2__)
		// Set the locale to C so that we are guaranteed to parse the float value correctly
		char* tmp = setlocale(LC_NUMERIC, nullptr);
		String orig = (tmp ? tmp : "C"_s);
		setlocale(LC_NUMERIC, "C");
#endif

		double res = strtod(val.data(), &end);

#if !defined(DEATH_TARGET_ANDROID) && !defined(_WIN32_WCE) && !defined(__psp2__)
		// Restore the locale
		setlocale(LC_NUMERIC, orig.data());
#endif

		if (byteCount) {
			*byteCount = asUINT(size_t(end - val.data()));
		}
		return res;
	}

	// This function returns a string containing the substring of the input string
	// determined by the starting index and count of characters.
	//
	// string string::substr(uint start = 0, int count = -1) const
	static String StringSubString(asUINT start, int count, const String& str)
	{
		String ret;
		if (start < str.size() && count != 0) {
			ret = str.slice(start, (size_t)(count < 0 ? str.size() : start + count));
		}
		return ret;
	}

	// String equality comparison.
	// Returns true iff lhs is equal to rhs.
	//
	// For some reason gcc 4.7 has difficulties resolving the asFUNCTIONPR(operator==, (const string &, const string &) macro, so this wrapper was introduced as work around.
	static bool StringEquals(const String& lhs, const String& rhs)
	{
		return lhs == rhs;
	}

	static String StringConcat(const String& lhs, const String& rhs)
	{
		return lhs + rhs;
	}

	void RegisterString(asIScriptEngine* engine)
	{
		int r;

		// Register the string type
		r = engine->RegisterObjectType("string", sizeof(String), asOBJ_VALUE | asGetTypeTraits<String>()); RETURN_ASSERT(r >= 0);

		r = engine->RegisterStringFactory("string", GetStringFactoryInstance());

		// Register the object operator overloads
		r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(ConstructString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("string", asBEHAVE_CONSTRUCT, "void f(const string &in)", asFUNCTION(CopyConstructString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectBehaviour("string", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(DestructString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string &opAssign(const string &in)", asMETHODPR(String, operator =, (const String&), String&), asCALL_THISCALL); RETURN_ASSERT(r >= 0);
		// Need to use a wrapper on Mac OS X 10.7/XCode 4.3 and CLang/LLVM, otherwise the linker fails
		r = engine->RegisterObjectMethod("string", "string &opAddAssign(const string &in)", asFUNCTION(AddAssignStringToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		//r = engine->RegisterObjectMethod("string", "string &opAddAssign(const string &in)", asMETHODPR(string, operator+=, (const string&), string&), asCALL_THISCALL); RETURN_ASSERT( r >= 0 );

		// Need to use a wrapper for operator== otherwise gcc 4.7 fails to compile
		r = engine->RegisterObjectMethod("string", "bool opEquals(const string &in) const", asFUNCTIONPR(StringEquals, (const String&, const String&), bool), asCALL_CDECL_OBJFIRST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "int opCmp(const string &in) const", asFUNCTION(StringCmp), asCALL_CDECL_OBJFIRST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd(const string &in) const", asFUNCTION(StringConcat), asCALL_CDECL_OBJFIRST); RETURN_ASSERT(r >= 0);

		// The string length can be accessed through methods or through virtual property
#if AS_USE_ACCESSORS != 1
		r = engine->RegisterObjectMethod("string", "uint length() const", asFUNCTION(StringSize), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
#endif
		//r = engine->RegisterObjectMethod("string", "void resize(uint)", asFUNCTION(StringResize), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
#if AS_USE_STLNAMES != 1 && AS_USE_ACCESSORS == 1
		// Don't register these if STL names is used, as they conflict with the method size()
		r = engine->RegisterObjectMethod("string", "uint get_length() const property", asFUNCTION(StringSize), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		//r = engine->RegisterObjectMethod("string", "void set_length(uint) property", asFUNCTION(StringResize), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
#endif
		// Need to use a wrapper on Mac OS X 10.7/XCode 4.3 and CLang/LLVM, otherwise the linker fails
		//r = engine->RegisterObjectMethod("string", "bool empty() const", asMETHOD(string, empty), asCALL_THISCALL); RETURN_ASSERT( r >= 0 );
		r = engine->RegisterObjectMethod("string", "bool empty() const", asFUNCTION(StringIsEmpty), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);

		// Register the index operator, both as a mutator and as an inspector
		// Note that we don't register the operator[] directly, as it doesn't do bounds checking
		r = engine->RegisterObjectMethod("string", "uint8 &opIndex(uint)", asFUNCTION(StringCharAt), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "const uint8 &opIndex(uint) const", asFUNCTION(StringCharAt), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);

		// Automatic conversion from values
		r = engine->RegisterObjectMethod("string", "string &opAssign(double)", asFUNCTION(AssignDoubleToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string &opAddAssign(double)", asFUNCTION(AddAssignDoubleToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd(double) const", asFUNCTION(AddStringDouble), asCALL_CDECL_OBJFIRST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd_r(double) const", asFUNCTION(AddDoubleString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("string", "string &opAssign(float)", asFUNCTION(AssignFloatToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string &opAddAssign(float)", asFUNCTION(AddAssignFloatToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd(float) const", asFUNCTION(AddStringFloat), asCALL_CDECL_OBJFIRST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd_r(float) const", asFUNCTION(AddFloatString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("string", "string &opAssign(int64)", asFUNCTION(AssignInt64ToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string &opAddAssign(int64)", asFUNCTION(AddAssignInt64ToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd(int64) const", asFUNCTION(AddStringInt64), asCALL_CDECL_OBJFIRST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd_r(int64) const", asFUNCTION(AddInt64String), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("string", "string &opAssign(uint64)", asFUNCTION(AssignUInt64ToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string &opAddAssign(uint64)", asFUNCTION(AddAssignUInt64ToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd(uint64) const", asFUNCTION(AddStringUInt64), asCALL_CDECL_OBJFIRST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd_r(uint64) const", asFUNCTION(AddUInt64String), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);

		r = engine->RegisterObjectMethod("string", "string &opAssign(bool)", asFUNCTION(AssignBoolToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string &opAddAssign(bool)", asFUNCTION(AddAssignBoolToString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd(bool) const", asFUNCTION(AddStringBool), asCALL_CDECL_OBJFIRST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "string opAdd_r(bool) const", asFUNCTION(AddBoolString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);

		// Utilities
		r = engine->RegisterObjectMethod("string", "string substr(uint start = 0, int count = -1) const", asFUNCTION(StringSubString), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "int findFirst(const string &in, uint start = 0) const", asFUNCTION(StringFindFirst), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "int findFirstOf(const string &in, uint start = 0) const", asFUNCTION(StringFindFirstOf), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		//r = engine->RegisterObjectMethod("string", "int findFirstNotOf(const string &in, uint start = 0) const", asFUNCTION(StringFindFirstNotOf), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "int findLast(const string &in, int start = -1) const", asFUNCTION(StringFindLast), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		r = engine->RegisterObjectMethod("string", "int findLastOf(const string &in, int start = -1) const", asFUNCTION(StringFindLastOf), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		//r = engine->RegisterObjectMethod("string", "int findLastNotOf(const string &in, int start = -1) const", asFUNCTION(StringFindLastNotOf), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		//r = engine->RegisterObjectMethod("string", "void insert(uint pos, const string &in other)", asFUNCTION(StringInsert), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);
		//r = engine->RegisterObjectMethod("string", "void erase(uint pos, int count = -1)", asFUNCTION(StringErase), asCALL_CDECL_OBJLAST); RETURN_ASSERT(r >= 0);

		r = engine->RegisterGlobalFunction("string formatInt(int64 val, const string &in options = \"\", uint width = 0)", asFUNCTION(formatInt), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("string formatUInt(uint64 val, const string &in options = \"\", uint width = 0)", asFUNCTION(formatUInt), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("string formatFloat(double val, const string &in options = \"\", uint width = 0, uint precision = 0)", asFUNCTION(formatFloat), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("int64 parseInt(const string &in, uint base = 10, uint &out byteCount = 0)", asFUNCTION(parseInt), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("uint64 parseUInt(const string &in, uint base = 10, uint &out byteCount = 0)", asFUNCTION(parseUInt), asCALL_CDECL); RETURN_ASSERT(r >= 0);
		r = engine->RegisterGlobalFunction("double parseFloat(const string &in, uint &out byteCount = 0)", asFUNCTION(parseFloat), asCALL_CDECL); RETURN_ASSERT(r >= 0);
	}
}

#endif