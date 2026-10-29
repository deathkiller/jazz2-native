#if defined(WITH_ANGELSCRIPT)

#include "Stream.h"
#include "Internal.h"

#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <IO/FileSystem.h>
#include <IO/MemoryStream.h>
#include <IO/Compression/DeflateStream.h>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		jjSTREAM::jjSTREAM() : _refCount(1), _readPos(0) {
		}
		jjSTREAM::~jjSTREAM() {
		}

		jjSTREAM* jjSTREAM::Create() {
			void* mem = asAllocMem(sizeof(jjSTREAM));
			return new(mem) jjSTREAM();
		}
		jjSTREAM* jjSTREAM::CreateFromFile(const String& filename) {
			void* mem = asAllocMem(sizeof(jjSTREAM));
			jjSTREAM* result = new(mem) jjSTREAM();

			// A missing file is not an error - scripts check isEmpty() to tell "no saved data yet" apart from
			// "saved data that happens to be empty", which is how the original's scripts use it. A rejected
			// path is reported the same way, so a script can't probe the filesystem for what exists either.
			if (!IsScriptFileNameSafe(filename)) {
				LOGW("Script tried to read \"{}\", which is outside the game's data directory", filename);
				return result;
			}

			auto s = fs::Open(fs::CombinePath(ContentResolver::Get().GetSourcePath(), filename), FileAccess::Read);
			if (s->IsValid()) {
				std::int64_t size = s->GetSize();
				if (size > 0 && size < std::numeric_limits<std::int32_t>::max()) {
					result->_data.resize_for_overwrite((std::size_t)size);
					// `resize_for_overwrite()` leaves the bytes uninitialized, so the stream is truncated to what
					// was actually read - the reported size is only an upper bound for a file that is being written
					// to concurrently, and the tail would otherwise hand uninitialized heap to the script
					std::int32_t read = s->Read(result->_data.data(), (std::int32_t)size);
					result->_data.truncate(read > 0 ? (std::size_t)read : 0);
				}
			}
			return result;
		}

		void jjSTREAM::AddRef()
		{
			_refCount++;
		}

		void jjSTREAM::Release()
		{
			if (--_refCount == 0) {
				this->~jjSTREAM();
				asFreeMem(this);
			}
		}

		// Assignment operator
		jjSTREAM& jjSTREAM::operator=(const jjSTREAM& o)
		{
			// jjSTREAM is a reference type on the script side, so two handles can name one object and
			// `s = s;` is expressible - the assign() below would then read from storage it has already cleared
			if (&o == this) {
				// Still has to drop the consumed prefix, because that is the one visible effect of assigning
				if (_readPos > 0) {
					_data.erase(_data.begin(), _data.begin() + _readPos);
					_readPos = 0;
				}
				return *this;
			}

			// Copy only the content, not the script proxy class
			_data.assign(o._data.begin() + o._readPos, o._data.end());
			_readPos = 0;
			return *this;
		}

		const std::uint8_t* jjSTREAM::PeekFront(std::uint32_t count) const {
			if (count > getSize()) {
				return nullptr;
			}
			return _data.data() + _readPos;
		}

		bool jjSTREAM::Take(void* target, std::uint32_t count) {
			const std::uint8_t* front = PeekFront(count);
			if (front == nullptr) {
				return false;
			}
			std::memcpy(target, front, count);
			_readPos += count;
			// Reclaim the consumed prefix once it dominates the buffer, so a stream that is written and read
			// in turns doesn't grow without bound
			if (_readPos > 4096 && _readPos * 2 > _data.size()) {
				_data.erase(_data.begin(), _data.begin() + _readPos);
				_readPos = 0;
			}
			return true;
		}

		void jjSTREAM::Append(const void* source, std::uint32_t count) {
			if (count == 0) {
				return;
			}

			// A stream can be appended to itself - `s.write(s)`, `s.push(s)`, `s.get(s, n)`, `s.pop(s)` all
			// reach here with `source` pointing into `_data` - and the resize below moves the buffer, so what
			// is carried across it is the offset rather than the pointer
			std::uintptr_t begin = (std::uintptr_t)_data.data();
			std::uintptr_t at = (std::uintptr_t)source;
			bool selfReferential = (at >= begin && at < begin + _data.size());
			std::size_t sourceOffset = (selfReferential ? (std::size_t)(at - begin) : 0);

			std::size_t offset = _data.size();
			_data.resize_for_overwrite(offset + count);
			std::memcpy(_data.data() + offset, (selfReferential ? _data.data() + sourceOffset : (const std::uint8_t*)source), count);
		}

		std::uint32_t jjSTREAM::getSize() const {
			return (std::uint32_t)(_data.size() - _readPos);
		}

		bool jjSTREAM::isEmpty() const {
			return (getSize() == 0);
		}

		bool jjSTREAM::save(const String& filename) const {
			// Written next to the game's other data, which is where the original puts it, so a script that
			// saves and reloads its own file finds it again - but only there, see IsScriptFileNameSafe()
			if (!IsScriptFileNameSafe(filename)) {
				LOGW("Script tried to write \"{}\", which is outside the game's data directory", filename);
				return false;
			}
			// The level editor runs the script only for what it shows (see LevelHandler::IsSandbox())
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			if (owner != nullptr && owner->GetLevelHandler()->IsSandbox()) {
				return false;
			}

			auto s = fs::Open(fs::CombinePath(ContentResolver::Get().GetSourcePath(), filename), FileAccess::Write);
			if (!s->IsValid()) {
				return false;
			}
			std::uint32_t size = getSize();
			if (size > 0) {
				s->Write(_data.data() + _readPos, size);
			}
			return true;
		}

		void jjSTREAM::clear() {
			_data.clear();
			_readPos = 0;
		}

		bool jjSTREAM::discard(std::uint32_t count) {
			if (count > getSize()) {
				return false;
			}
			_readPos += count;
			return true;
		}

		bool jjSTREAM::write(const String& value) {
			// Raw bytes, no length prefix - this is the text-friendly counterpart to push(String)
			Append(value.data(), (std::uint32_t)value.size());
			return true;
		}
		bool jjSTREAM::write(const jjSTREAM& value) {
			Append(value._data.data() + value._readPos, value.getSize());
			return true;
		}
		bool jjSTREAM::get(String& value, std::uint32_t count) {
			const std::uint8_t* front = PeekFront(count);
			if (front == nullptr) {
				return false;
			}
			value = String((const char*)front, count);
			_readPos += count;
			return true;
		}
		bool jjSTREAM::get(jjSTREAM& value, std::uint32_t count) {
			const std::uint8_t* front = PeekFront(count);
			if (front == nullptr) {
				return false;
			}
			value.Append(front, count);
			_readPos += count;
			return true;
		}
		bool jjSTREAM::getLine(String& value, const String& delim) {
			std::uint32_t size = getSize();
			if (size == 0 || delim.empty()) {
				return false;
			}

			const std::uint8_t* front = _data.data() + _readPos;
			StringView remaining((const char*)front, size);
			StringView found = remaining.find(delim);
			if (found == nullptr) {
				// No delimiter left, so the remainder is the last line
				value = String((const char*)front, size);
				_readPos += size;
				return true;
			}

			std::uint32_t lineLength = (std::uint32_t)(found.data() - (const char*)front);
			value = String((const char*)front, lineLength);
			// The delimiter itself is consumed but not returned
			_readPos += lineLength + (std::uint32_t)delim.size();
			return true;
		}

		bool jjSTREAM::push(bool value) {
			std::uint8_t raw = (value ? 1 : 0);
			return PushValue(raw);
		}
		bool jjSTREAM::push(std::uint8_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::int8_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::uint16_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::int16_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::uint32_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::int32_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::uint64_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(std::int64_t value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(float value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(double value) {
			return PushValue(value);
		}
		bool jjSTREAM::push(const String& value) {
			// Length-prefixed, so pop(String) can recover it without a delimiter
			std::uint32_t length = (std::uint32_t)value.size();
			PushValue(length);
			Append(value.data(), length);
			return true;
		}
		bool jjSTREAM::push(const jjSTREAM& value) {
			// Length-prefixed as a string, as measured in JJ2+ (and as MLLE keeps the parameters of a weapon)
			std::uint32_t length = value.getSize();
			PushValue(length);
			return write(value);
		}

		bool jjSTREAM::pop(bool& value) {
			std::uint8_t raw;
			if (!PopValue(raw)) {
				return false;
			}
			value = (raw != 0);
			return true;
		}
		bool jjSTREAM::pop(std::uint8_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::int8_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::uint16_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::int16_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::uint32_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::int32_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::uint64_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(std::int64_t& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(float& value) {
			return PopValue(value);
		}
		bool jjSTREAM::pop(double& value) {
			return PopValue(value);
		}
		bool jjSTREAM::PopLength(std::uint32_t& length) {
			// The length prefix is validated before it is consumed, so a truncated payload leaves the stream
			// exactly where it was. Reading it first and rolling back afterwards cannot work: `Take()` may
			// compact the buffer and reset `_readPos` to 0 during that read, and the rollback then underflows
			// the unsigned position to ~4 GB instead of pointing at the length again.
			const std::uint8_t* front = PeekFront(sizeof(length));
			if (front == nullptr) {
				return false;
			}
			std::memcpy(&length, front, sizeof(length));

			// `PeekFront()` above proved there are at least `sizeof(length)` bytes left, so this cannot underflow
			if (length > getSize() - sizeof(length)) {
				// Truncated or not written by push()
				return false;
			}

			_readPos += sizeof(length);
			return true;
		}
		bool jjSTREAM::pop(String& value) {
			std::uint32_t length;
			return PopLength(length) && get(value, length);
		}
		bool jjSTREAM::pop(jjSTREAM& value) {
			std::uint32_t length;
			return PopLength(length) && get(value, length);
		}

		bool jjSTREAM::ZlibCompress(const jjSTREAM& input, jjSTREAM& output) {
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
			Death::IO::MemoryStream ms(std::int64_t(input.getSize() / 2 + 64));
			{
				Death::IO::Compression::DeflateWriter dw(ms, 9, false);
				if (input.getSize() > 0 && dw.Write(input._data.data() + input._readPos, input.getSize()) != input.getSize()) {
					return false;
				}
			}
			output.clear();
			output.Append(ms.GetBuffer(), (std::uint32_t)ms.GetSize());
			return true;
#else
			return false;
#endif
		}
		bool jjSTREAM::ZlibUncompress(const jjSTREAM& input, jjSTREAM& output, std::uint32_t size) {
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
			Death::IO::MemoryStream ms(input._data.data() + input._readPos, input.getSize());
			Death::IO::Compression::DeflateStream ds(ms, std::int32_t(input.getSize()), false);
			SmallVector<std::uint8_t, 0> result;
			result.resize_for_overwrite(size);
			std::int64_t read = (size > 0 ? ds.Read(result.data(), size) : 0);
			std::uint8_t extra;
			// Like zlib's uncompress(), the data must fit into the given size
			if (read < 0 || !ds.IsValid() || ds.Read(&extra, 1) > 0) {
				return false;
			}
			output.clear();
			output.Append(result.data(), (std::uint32_t)read);
			return true;
#else
			return false;
#endif
		}
		std::uint32_t jjSTREAM::CRC32(const jjSTREAM& input, std::uint32_t crc) {
#if defined(WITH_ZLIB) || defined(WITH_MINIZ)
			return (std::uint32_t)crc32(crc, input._data.data() + input._readPos, input.getSize());
#else
			return 0;
#endif
		}

		jjRNG::jjRNG(std::uint64_t seed) : _refCount(1) {
			_random.Init(seed, DefaultInitSequence);
		}
		jjRNG::~jjRNG() {
		}
		jjRNG* jjRNG::Create(std::uint64_t seed) {
			void* mem = asAllocMem(sizeof(jjRNG));
			return new(mem) jjRNG(seed);
		}
		void jjRNG::AddRef() {
			_refCount++;
		}
		void jjRNG::Release() {
			if (--_refCount == 0) {
				this->~jjRNG();
				asFreeMem(this);
			}
		}
		std::uint64_t jjRNG::operator()() {
			// Join two 32-bit values to single 64-bit value
			return _random.Next() | ((std::uint64_t)_random.Next() << 32ull);
		}
		jjRNG& jjRNG::operator=(const jjRNG& o) {
			_random = o._random;
			return *this;
		}
		bool jjRNG::operator==(const jjRNG& o) const {
			return (this == &o);
		}
		void jjRNG::seed(std::uint64_t value) {
			_random.Init(value, DefaultInitSequence);
		}
		void jjRNG::discard(std::uint64_t count) {
			for (std::uint64_t i = 0; i < count; i++) {
				_random.Next();
			}
		}
	}

	// TODO


	bool LevelScriptLoader::jjSendPacket(const jjSTREAM& packet, std::int32_t toClientID, std::uint32_t toScriptModuleID) {
		noop(); return true;
	}

	void LevelScriptLoader::RegisterLegacyStream(asIScriptEngine* engine)
	{
		engine->RegisterObjectType("jjSTREAM", sizeof(jjSTREAM), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjSTREAM", asBEHAVE_FACTORY, "jjSTREAM@ f()", asFUNCTION(jjSTREAM::Create), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjSTREAM", asBEHAVE_FACTORY, "jjSTREAM@ f(const ::string &in filename)", asFUNCTION(jjSTREAM::CreateFromFile), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjSTREAM", asBEHAVE_ADDREF, "void f()", asMETHOD(jjSTREAM, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjSTREAM", asBEHAVE_RELEASE, "void f()", asMETHOD(jjSTREAM, Release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "jjSTREAM& opAssign(const jjSTREAM &in)", asMETHODPR(jjSTREAM, operator=, (const jjSTREAM& other), jjSTREAM&), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "uint getSize() const", asMETHOD(jjSTREAM, getSize), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool isEmpty() const", asMETHOD(jjSTREAM, isEmpty), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool save(const ::string &in filename) const", asMETHOD(jjSTREAM, save), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjSTREAM", "void clear()", asMETHOD(jjSTREAM, clear), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool discard(uint count)", asMETHOD(jjSTREAM, discard), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool write(const ::string &in value)", asMETHODPR(jjSTREAM, write, (const String&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool write(const jjSTREAM &in value)", asMETHODPR(jjSTREAM, write, (const jjSTREAM&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool get(::string &out value, uint count = 1)", asMETHODPR(jjSTREAM, get, (String&, uint32_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool get(jjSTREAM &out value, uint count = 1)", asMETHODPR(jjSTREAM, get, (jjSTREAM&, uint32_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool getLine(::string &out value, const ::string &in delim = '\\n')", asMETHOD(jjSTREAM, getLine), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjSTREAM", "bool push(bool value)", asMETHODPR(jjSTREAM, push, (bool), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(uint8 value)", asMETHODPR(jjSTREAM, push, (uint8_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(int8 value)", asMETHODPR(jjSTREAM, push, (int8_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(uint16 value)", asMETHODPR(jjSTREAM, push, (uint16_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(int16 value)", asMETHODPR(jjSTREAM, push, (int16_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(uint32 value)", asMETHODPR(jjSTREAM, push, (uint32_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(int32 value)", asMETHODPR(jjSTREAM, push, (int32_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(uint64 value)", asMETHODPR(jjSTREAM, push, (uint64_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(int64 value)", asMETHODPR(jjSTREAM, push, (int64_t), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(float value)", asMETHODPR(jjSTREAM, push, (float), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(double value)", asMETHODPR(jjSTREAM, push, (double), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(const ::string &in value)", asMETHODPR(jjSTREAM, push, (const String&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool push(const jjSTREAM &in value)", asMETHODPR(jjSTREAM, push, (const jjSTREAM&), bool), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjSTREAM", "bool pop(bool &out value)", asMETHODPR(jjSTREAM, pop, (bool&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(uint8 &out value)", asMETHODPR(jjSTREAM, pop, (uint8_t&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(int8 &out value)", asMETHODPR(jjSTREAM, pop, (int8_t&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(uint16 &out value)", asMETHODPR(jjSTREAM, pop, (uint16_t&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(int16 &out value)", asMETHODPR(jjSTREAM, pop, (int16_t&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(uint32 &out value)", asMETHODPR(jjSTREAM, pop, (uint32_t&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(int32 &out value)", asMETHODPR(jjSTREAM, pop, (int32_t&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(uint64 &out value)", asMETHODPR(jjSTREAM, pop, (uint64_t&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(int64 &out value)", asMETHODPR(jjSTREAM, pop, (int64_t&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(float &out value)", asMETHODPR(jjSTREAM, pop, (float&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(double &out value)", asMETHODPR(jjSTREAM, pop, (double&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(::string &out value)", asMETHODPR(jjSTREAM, pop, (String&), bool), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjSTREAM", "bool pop(jjSTREAM &out value)", asMETHODPR(jjSTREAM, pop, (jjSTREAM&), bool), asCALL_THISCALL);

		engine->RegisterGlobalFunction("bool jjSendPacket(const jjSTREAM &in packet, int toClientID = 0, uint toScriptModuleID = ::jjScriptModuleID)", asFUNCTION(jjSendPacket), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjZlibCompress(const jjSTREAM &in input, jjSTREAM &out output)", asFUNCTION(jjSTREAM::ZlibCompress), asCALL_CDECL);
		engine->RegisterGlobalFunction("bool jjZlibUncompress(const jjSTREAM &in input, jjSTREAM &out output, uint size)", asFUNCTION(jjSTREAM::ZlibUncompress), asCALL_CDECL);
		engine->RegisterGlobalFunction("uint jjCRC32(const jjSTREAM &in input, uint crc = 0)", asFUNCTION(jjSTREAM::CRC32), asCALL_CDECL);
		// TODO
		//engine->RegisterGlobalFunction("bool jjTakeScreenshot(const string &in filename = '')", asFUNCTION(requestScreenshot), asCALL_CDECL);

		engine->RegisterObjectType("jjRNG", sizeof(jjRNG), asOBJ_REF);
		engine->RegisterObjectBehaviour("jjRNG", asBEHAVE_FACTORY, "jjRNG@ f(uint64 seed = 5489)", asFUNCTION(jjRNG::Create), asCALL_CDECL);
		engine->RegisterObjectBehaviour("jjRNG", asBEHAVE_ADDREF, "void f()", asMETHOD(jjRNG, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjRNG", asBEHAVE_RELEASE, "void f()", asMETHOD(jjRNG, Release), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "uint64 opCall()", asMETHOD(jjRNG, operator()), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "jjRNG& opAssign(const jjRNG &in)", asMETHOD(jjRNG, operator=), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "bool opEquals(const jjRNG &in) const", asMETHOD(jjRNG, operator==), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "void seed(uint64 value = 5489)", asMETHOD(jjRNG, seed), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjRNG", "void discard(uint64 count = 1)", asMETHOD(jjRNG, discard), asCALL_THISCALL);

		engine->RegisterInterface("jjPUBLICINTERFACE");
		engine->RegisterInterfaceMethod("jjPUBLICINTERFACE", "string getVersion() const");
		// TODO
		//engine->RegisterGlobalFunction("jjPUBLICINTERFACE@ jjGetPublicInterface(const string &in moduleName)", asFUNCTION(getPublicInterface), asCALL_CDECL);
	}
}

#endif
