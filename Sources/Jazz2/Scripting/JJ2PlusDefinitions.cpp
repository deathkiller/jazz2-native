#if defined(WITH_ANGELSCRIPT)

#include "JJ2PlusDefinitions.h"
#include "LevelScriptLoader.h"

#include "../LevelHandler.h"
#include "../Actors/ActorBase.h"
#include "../Actors/Player.h"
#include "../Compatibility/JJ2Strings.h"

#include "../../nCine/Base/Random.h"

namespace Jazz2::Scripting
{
	static void Unimplemented(const char* sourceName) {
		auto ctx = asGetActiveContext();
		if (ctx != nullptr) {
			const char* sectionName;
			int lineNumber = ctx->GetLineNumber(0, nullptr, &sectionName);
			LOGW("%s (called from \"%s:%i\")", sourceName, sectionName, lineNumber);
		} else {
			LOGW("%s", sourceName);
		}
	}

#if defined(DEATH_TARGET_GCC)
#	define noop() Unimplemented(__PRETTY_FUNCTION__)
#elif defined(DEATH_TARGET_MSVC)
#	define noop() Unimplemented(__FUNCTION__)
#else
#	define noop() Unimplemented(__func__)
#endif

	jjTEXTAPPEARANCE jjTEXTAPPEARANCE::constructor() {
		noop(); return { };
	}
	jjTEXTAPPEARANCE jjTEXTAPPEARANCE::constructorMode(uint32_t mode) {
		noop(); return { };
	}

	jjTEXTAPPEARANCE& jjTEXTAPPEARANCE::operator=(uint32_t other) {
		noop(); return *this;
	}

	jjPALCOLOR jjPALCOLOR::Create() {
		noop(); return { };
	}
	jjPALCOLOR jjPALCOLOR::CreateFromRgb(uint8_t red, uint8_t green, uint8_t blue) {
		noop(); return { red, green, blue };
	}

	uint8_t jjPALCOLOR::getHue() {
		noop(); return 0;
	}
	uint8_t jjPALCOLOR::getSat() {
		noop(); return 0;
	}
	uint8_t jjPALCOLOR::getLight() {
		noop(); return 0;
	}

	void jjPALCOLOR::swizzle(uint32_t redc, uint32_t greenc, uint32_t bluec) {
		noop();

		uint8_t r = red;
		uint8_t g = green;
		uint8_t b = blue;

		switch (redc) {
			case 1: red = g; break;
			case 2: red = b; break;
		}
		switch (greenc) {
			case 0: green = r; break;
			case 2: green = b; break;
		}
		switch (bluec) {
			case 0: blue = r; break;
			case 1: blue = g; break;
		}
	}
	void jjPALCOLOR::setHSL(int hue, uint8_t sat, uint8_t light) {
		noop();
	}

	jjPALCOLOR& jjPALCOLOR::operator=(const jjPALCOLOR& other) {
		noop();

		red = other.red;
		green = other.green;
		blue = other.blue;
		return *this;
	}
	bool jjPALCOLOR::operator==(const jjPALCOLOR& other) {
		noop(); return (red == other.red && green == other.green && blue == other.blue);
	}

	jjPAL::jjPAL() : _refCount(1) {
		noop();
	}
	jjPAL::~jjPAL() {
		noop();
	}

	jjPAL* jjPAL::Create(jjPAL* self) {
		noop();
		return new(self) jjPAL();
	}

	void jjPAL::AddRef()
	{
		_refCount++;
	}

	void jjPAL::Release()
	{
		if (--_refCount == 0) {
			this->~jjPAL();
			asFreeMem(this);
		}
	}

	void jjPAL::reset() {
		noop();
	}
	void jjPAL::apply() {
		noop();
	}
	bool jjPAL::load(const String& filename) {
		noop();
		return false;
	}
	void jjPAL::fill(uint8_t red, uint8_t green, uint8_t blue, float opacity) {
		noop();
	}
	void jjPAL::fillTint(uint8_t red, uint8_t green, uint8_t blue, uint8_t start, uint8_t length, float opacity) {
		noop();
	}
	void jjPAL::fillFromColor(jjPALCOLOR color, float opacity) {
		noop();
	}
	void jjPAL::fillTintFromColor(jjPALCOLOR color, uint8_t start, uint8_t length, float opacity) {
		noop();
	}
	void jjPAL::gradient(uint8_t red1, uint8_t green1, uint8_t blue1, uint8_t red2, uint8_t green2, uint8_t blue2, uint8_t start, uint8_t length, float opacity, bool inclusive) {
		noop();
	}
	void jjPAL::gradientFromColor(jjPALCOLOR color1, jjPALCOLOR color2, uint8_t start, uint8_t length, float opacity, bool inclusive) {
		noop();
	}
	void jjPAL::copyFrom(uint8_t start, uint8_t length, uint8_t start2, const jjPAL& source, float opacity) {
		noop();
	}
	uint8_t jjPAL::findNearestColor(jjPALCOLOR color) {
		noop();
		return 0;
	}

	jjSTREAM::jjSTREAM() : _refCount(1) {
		noop();
	}
	jjSTREAM::~jjSTREAM() {
		noop();
	}

	jjSTREAM* jjSTREAM::Create() {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjSTREAM));
		return new(mem) jjSTREAM();
	}
	jjSTREAM* jjSTREAM::CreateFromFile(const String& filename) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjSTREAM));
		return new(mem) jjSTREAM();
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
		// Copy only the content, not the script proxy class
		//_value = o._value;
		return *this;
	}

	uint32_t jjSTREAM::getSize() const {
		noop();
		return 0;
	}

	bool jjSTREAM::isEmpty() const {
		noop();
		return false;
	}

	bool jjSTREAM::save(const String& tilename) const {
		noop();
		return false;
	}

	void jjSTREAM::clear() {
		noop();
	}

	bool jjSTREAM::discard(uint32_t count) {
		return 0;
	}

	bool jjSTREAM::write(const String& value) {
		noop();
		return false;
	}
	bool jjSTREAM::write(const jjSTREAM& value) {
		noop();
		return false;
	}
	bool jjSTREAM::get(String& value, uint32_t count) {
		noop();
		return false;
	}
	bool jjSTREAM::get(jjSTREAM& value, uint32_t count) {
		noop();
		return false;
	}
	bool jjSTREAM::getLine(String& value, const String& delim) {
		noop();
		return false;
	}

	bool jjSTREAM::push(bool value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(uint8_t value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(int8_t value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(uint16_t value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(int16_t value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(uint32_t value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(int32_t value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(uint64_t value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(int64_t value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(float value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(double value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(const String& value) {
		noop();
		return false;
	}
	bool jjSTREAM::push(const jjSTREAM& value) {
		noop();
		return false;
	}

	bool jjSTREAM::pop(bool& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(uint8_t& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(int8_t& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(uint16_t& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(int16_t& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(uint32_t& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(int32_t& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(uint64_t& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(int64_t& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(float& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(double& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(String& value) {
		noop();
		return false;
	}
	bool jjSTREAM::pop(jjSTREAM& value) {
		noop();
		return false;
	}

	jjBEHAVIOR* jjBEHAVIOR::Create(jjBEHAVIOR* self) {
		noop();
		return new(self) jjBEHAVIOR();
	}
	jjBEHAVIOR* jjBEHAVIOR::CreateFromBehavior(uint32_t behavior, jjBEHAVIOR* self) {
		noop();
		return new(self) jjBEHAVIOR();
	}
	void jjBEHAVIOR::Destroy(jjBEHAVIOR* self) {
		noop();
	}

	jjBEHAVIOR& jjBEHAVIOR::operator=(const jjBEHAVIOR& other) {
		noop();
		return *this;
	}
	jjBEHAVIOR& jjBEHAVIOR::operator=(uint32_t other) {
		noop();
		return *this;
	}
	jjBEHAVIOR& jjBEHAVIOR::operator=(asIScriptFunction* other) {
		noop();
		return *this;
	}
	jjBEHAVIOR& jjBEHAVIOR::operator=(asIScriptObject* other) {
		noop();
		return *this;
	}
	bool jjBEHAVIOR::operator==(const jjBEHAVIOR& other) const {
		noop();
		return false;
	}
	bool jjBEHAVIOR::operator==(uint32_t other) const {
		noop();
		return false;
	}
	bool jjBEHAVIOR::operator==(const asIScriptFunction* other) const {
		noop();
		return false;
	}
	jjBEHAVIOR::operator uint32_t() {
		noop();
		return 0;
	}
	jjBEHAVIOR::operator asIScriptFunction* () {
		noop();
		return nullptr;
	}
	jjBEHAVIOR::operator asIScriptObject* () {
		noop();
		return nullptr;
	}

	jjANIMFRAME::jjANIMFRAME() : _refCount(1) {
		noop();
	}
	jjANIMFRAME::~jjANIMFRAME() {
		noop();
	}

	void jjANIMFRAME::AddRef()
	{
		_refCount++;
	}

	void jjANIMFRAME::Release()
	{
		if (--_refCount == 0) {
			this->~jjANIMFRAME();
			asFreeMem(this);
		}
	}

	// Assignment operator
	jjANIMFRAME& jjANIMFRAME::operator=(const jjANIMFRAME& o)
	{
		// Copy only the content, not the script proxy class
		//_value = o._value;
		return *this;
	}

	jjANIMFRAME* jjANIMFRAME::get_jjAnimFrames(uint32_t index) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjANIMFRAME));
		return new(mem) jjANIMFRAME();
	}

	bool jjANIMFRAME::get_transparent() const {
		noop();
		return false;
	}
	bool jjANIMFRAME::set_transparent(bool value) const {
		noop();
		return false;
	}
	bool jjANIMFRAME::doesCollide(int32_t xPos, int32_t yPos, int32_t direction, const jjANIMFRAME* frame2, int32_t xPos2, int32_t yPos2, int32_t direction2, bool always) const {
		noop();
		return false;
	}

	jjANIMATION::jjANIMATION(uint32_t index) : _refCount(1), _index(index) {
		noop();
	}
	jjANIMATION::~jjANIMATION() {
		noop();
	}

	void jjANIMATION::AddRef()
	{
		_refCount++;
	}

	void jjANIMATION::Release()
	{
		if (--_refCount == 0) {
			this->~jjANIMATION();
			asFreeMem(this);
		}
	}

	// Assignment operator
	jjANIMATION& jjANIMATION::operator=(const jjANIMATION& o)
	{
		// Copy only the content, not the script proxy class
		//_value = o._value;
		return *this;
	}

	bool jjANIMATION::save(const String& filename, const jjPAL& palette) const {
		noop();
		return false;
	}
	bool jjANIMATION::load(const String& filename, int32_t hotSpotX, int32_t hotSpotY, int32_t coldSpotYOffset, int32_t firstFrameToOverwrite) {
		noop();
		return false;
	}

	jjANIMATION* jjANIMATION::get_jjAnimations(uint32_t index) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjANIMATION));
		return new(mem) jjANIMATION(index);
	}

	uint32_t jjANIMATION::get_firstFrame() const {
		noop();
		return 0;
	}
	uint32_t jjANIMATION::set_firstFrame(uint32_t index) const {
		noop();
		return 0;
	}

	uint32_t jjANIMATION::getAnimFirstFrame() {
		noop();
		return 0;
	}

	jjANIMSET::jjANIMSET(uint32_t index) : _refCount(1), _index(index) {
		noop();
	}
	jjANIMSET::~jjANIMSET() {
		noop();
	}

	void jjANIMSET::AddRef()
	{
		_refCount++;
	}

	void jjANIMSET::Release()
	{
		if (--_refCount == 0) {
			this->~jjANIMSET();
			asFreeMem(this);
		}
	}

	jjANIMSET* jjANIMSET::get_jjAnimSets(uint32_t index) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjANIMSET));
		return new(mem) jjANIMSET(index);
	}

	uint32_t jjANIMSET::convertAnimSetToUint() {
		noop();
		return _index;
	}

	jjANIMSET* jjANIMSET::load(uint32_t fileSetID, const String& filename, int32_t firstAnimToOverwrite, int32_t firstFrameToOverwrite) {
		noop();
		return this;
	}
	jjANIMSET* jjANIMSET::allocate(const CScriptArray& frameCounts) {
		noop();
		return this;
	}

	void jjCANVAS::DrawPixel(int32_t xPixel, int32_t yPixel, uint8_t color, uint32_t mode, uint8_t param) {
		noop();
	}
	void jjCANVAS::DrawRectangle(int32_t xPixel, int32_t yPixel, int32_t width, int32_t height, uint8_t color, uint32_t mode, uint8_t param) {
		noop();
	}
	void jjCANVAS::DrawSprite(int32_t xPixel, int32_t yPixel, int32_t setID, uint8_t animation, uint8_t frame, int8_t direction, uint32_t mode, uint8_t param) {
		noop();
	}
	void jjCANVAS::DrawCurFrameSprite(int32_t xPixel, int32_t yPixel, uint32_t sprite, int8_t direction, uint32_t mode, uint8_t param) {
		noop();
	}
	void jjCANVAS::DrawResizedSprite(int32_t xPixel, int32_t yPixel, int32_t setID, uint8_t animation, uint8_t frame, float xScale, float yScale, uint32_t mode, uint8_t param) {
		noop();
	}
	void jjCANVAS::DrawResizedCurFrameSprite(int32_t xPixel, int32_t yPixel, uint32_t sprite, float xScale, float yScale, uint32_t mode, uint8_t param) {
		noop();
	}
	void jjCANVAS::DrawTransformedSprite(int32_t xPixel, int32_t yPixel, int32_t setID, uint8_t animation, uint8_t frame, int32_t angle, float xScale, float yScale, uint32_t mode, uint8_t param) {
		noop();
	}
	void jjCANVAS::DrawTransformedCurFrameSprite(int32_t xPixel, int32_t yPixel, uint32_t sprite, int32_t angle, float xScale, float yScale, uint32_t mode, uint8_t param) {
		noop();
	}
	void jjCANVAS::DrawSwingingVine(int32_t xPixel, int32_t yPixel, uint32_t sprite, int32_t length, int32_t curvature, uint32_t mode, uint8_t param) {
		noop();
	}

	void jjCANVAS::ExternalDrawTile(int32_t xPixel, int32_t yPixel, uint16_t tile, uint32_t tileQuadrant) {
		noop();
	}
	void jjCANVAS::DrawTextBasicSize(int32_t xPixel, int32_t yPixel, const String& text, uint32_t size, uint32_t mode, uint8_t param) {
		noop();
	}
	void jjCANVAS::DrawTextExtSize(int32_t xPixel, int32_t yPixel, const String& text, uint32_t size, const jjTEXTAPPEARANCE& appearance, uint8_t param1, uint32_t mode, uint8_t param) {
		noop();
	}

	void jjCANVAS::drawString(int32_t xPixel, int32_t yPixel, const String& text, const jjANIMATION& animation, uint32_t mode, uint8_t param) {
		noop();
	}

	void jjCANVAS::drawStringEx(int32_t xPixel, int32_t yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, uint8_t param1, uint32_t spriteMode, uint8_t param2) {
		noop();
	}

	void jjCANVAS::jjDrawString(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, uint32_t mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjCANVAS::jjDrawStringEx(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, uint8_t param1, uint32_t spriteMode, uint8_t param2, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	int jjCANVAS::jjGetStringWidth(const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& style) {
		noop();
		return 0;
	}

	jjOBJ::jjOBJ() : _refCount(1) {
		noop();
	}
	jjOBJ::~jjOBJ() {
		noop();
	}

	void jjOBJ::AddRef()
	{
		_refCount++;
	}

	void jjOBJ::Release()
	{
		if (--_refCount == 0) {
			this->~jjOBJ();
			asFreeMem(this);
		}
	}

	bool jjOBJ::get_isActive() const {
		noop();
		return true;
	}
	uint32_t jjOBJ::get_lightType() const {
		noop();
		return 0;
	}
	uint32_t jjOBJ::set_lightType(uint32_t value) const {
		noop();
		return 0;
	}

	jjOBJ* jjOBJ::objectHit(jjOBJ* target, uint32_t playerHandling) {
		noop();
		return nullptr;
	}
	void jjOBJ::blast(int32_t maxDistance, bool blastObjects) {
		noop();
	}

	void jjOBJ::behave1(uint32_t behavior, bool draw) {
		noop();
	}
	void jjOBJ::behave2(jjBEHAVIOR behavior, bool draw) {
		noop();
	}
	void jjOBJ::behave3(jjVOIDFUNCOBJ behavior, bool draw) {
		noop();
	}

	int32_t jjOBJ::jjAddObject(uint8_t eventID, float xPixel, float yPixel, uint16_t creatorID, uint32_t creatorType, uint32_t behavior) {
		noop();
		return 0;
	}
	int32_t jjOBJ::jjAddObjectEx(uint8_t eventID, float xPixel, float yPixel, uint16_t creatorID, uint32_t creatorType, jjVOIDFUNCOBJ behavior) {
		noop();
		return 0;
	}

	void jjOBJ::jjDeleteObject(int32_t objectID) {
		noop();
	}
	void jjOBJ::jjKillObject(int32_t objectID) {
		noop();
	}

	uint32_t jjOBJ::determineCurFrame(bool change) {
		noop(); return 0;
	}

	uint16_t jjOBJ::get_creatorID() const {
		noop(); return 0;
	}
	uint16_t jjOBJ::set_creatorID(uint16_t value) const {
		noop(); return 0;
	}
	uint32_t jjOBJ::get_creatorType() const {
		noop(); return 0;
	}
	uint32_t jjOBJ::set_creatorType(uint32_t value) const {
		noop(); return 0;
	}

	int16_t jjOBJ::determineCurAnim(uint8_t setID, uint8_t animation, bool change) {
		noop(); return 0;
	}

	uint32_t jjOBJ::get_bulletHandling() {
		noop(); return 0;
	}
	uint32_t jjOBJ::set_bulletHandling(uint32_t value) {
		noop(); return 0;
	}
	bool jjOBJ::get_ricochet() {
		noop(); return 0;
	}
	bool jjOBJ::set_ricochet(bool value) {
		noop(); return 0;
	}
	bool jjOBJ::get_freezable() {
		noop(); return 0;
	}
	bool jjOBJ::set_freezable(bool value) {
		noop(); return 0;
	}
	bool jjOBJ::get_blastable() {
		noop(); return 0;
	}
	bool jjOBJ::set_blastable(bool value) {
		noop(); return 0;
	}

	uint32_t jjOBJ::get_playerHandling() {
		noop(); return false;
	}
	uint32_t jjOBJ::set_playerHandling(uint32_t value) {
		noop(); return false;
	}
	bool jjOBJ::get_isTarget() {
		noop(); return false;
	}
	bool jjOBJ::set_isTarget(bool value) {
		noop(); return false;
	}
	bool jjOBJ::get_triggersTNT() {
		noop(); return false;
	}
	bool jjOBJ::set_triggersTNT(bool value) {
		noop(); return false;
	}
	bool jjOBJ::get_deactivates() {
		noop(); return false;
	}
	bool jjOBJ::set_deactivates(bool value) {
		noop(); return false;
	}
	bool jjOBJ::get_scriptedCollisions() {
		noop(); return false;
	}
	bool jjOBJ::set_scriptedCollisions(bool value) {
		noop(); return false;
	}

	int32_t jjOBJ::get_var(uint8_t x) {
		noop(); return 0;
	}
	int32_t jjOBJ::set_var(uint8_t x, int32_t value) {
		noop(); return 0;
	}

	int32_t jjOBJ::draw() {
		noop();
		return 0;
	}
	int32_t jjOBJ::beSolid(bool shouldCheckForStompingLocalPlayers) {
		noop();
		return 0;
	}
	void jjOBJ::bePlatform(float xOld, float yOld, int32_t width, int32_t height) {
		noop();
	}
	void jjOBJ::clearPlatform() {
		noop();
	}
	void jjOBJ::putOnGround(bool precise) {
		noop();
	}
	bool jjOBJ::ricochet() {
		noop();
		return false;
	}
	int32_t jjOBJ::unfreeze(int32_t style) {
		noop();
		return 0;
	}
	void jjOBJ::deleteObject() {
		noop();
	}
	void jjOBJ::deactivate() {
		noop();
	}
	void jjOBJ::pathMovement() {
		noop();
	}
	int32_t jjOBJ::fireBullet(uint8_t eventID) {
		noop();
		return 0;
	}
	void jjOBJ::particlePixelExplosion(int32_t style) {
		noop();
	}
	void jjOBJ::grantPickup(jjPLAYER* player, int32_t frequency) {
		noop();
	}

	int jjOBJ::findNearestPlayer(int maxDistance) const {
		noop();
		return 0;
	}
	int jjOBJ::findNearestPlayerEx(int maxDistance, int& foundDistance) const {
		noop();
		return 0;
	}

	bool jjOBJ::doesCollide(const jjOBJ* object, bool always) const {
		noop();
		return false;
	}
	bool jjOBJ::doesCollidePlayer(const jjPLAYER* object, bool always) const {
		noop();
		return false;
	}

	jjOBJ* get_jjObjects(int index)
	{
		noop();
		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjOBJ));
		return new(mem) jjOBJ();
	}

	jjOBJ* get_jjObjectPresets(int8_t id)
	{
		noop();
		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjOBJ));
		return new(mem) jjOBJ();
	}

	jjPLAYER::jjPLAYER(LevelScriptLoader* levelScripts, int playerIndex) : _levelScriptLoader(levelScripts), _refCount(1) {
		noop();
		auto& players = levelScripts->GetPlayers();
		_player = (playerIndex < players.size() ? players[playerIndex] : nullptr);
	}
	jjPLAYER::jjPLAYER(LevelScriptLoader* levelScripts, Actors::Player* player) : _levelScriptLoader(levelScripts), _refCount(1), _player(player) {
		noop();
	}
	jjPLAYER::~jjPLAYER() {
		noop();
	}

	void jjPLAYER::AddRef()
	{
		_refCount++;
	}

	void jjPLAYER::Release()
	{
		if (--_refCount == 0) {
			this->~jjPLAYER();
			asFreeMem(this);
		}
	}

	// Assignment operator
	jjPLAYER& jjPLAYER::operator=(const jjPLAYER& o)
	{
		// Copy only the content, not the script proxy class
		//_value = o._value;
		return *this;
	}

	int32_t jjPLAYER::setScore(int32_t value) {
		noop(); return 0;
	}

	float jjPLAYER::get_xSpeed() {
		noop();
		return _player->_speed.X;
	}
	float jjPLAYER::set_xSpeed(float value) {
		noop();
		_player->_speed.X = value;
		return value;
	}
	float jjPLAYER::get_ySpeed() {
		noop();
		return _player->_speed.Y;
	}
	float jjPLAYER::set_ySpeed(float value) {
		noop();
		_player->_speed.Y = value;
		return value;
	}

	void jjPLAYER::freeze(bool frozen) {
		noop();
		if (frozen) {
			_player->_frozenTimeLeft = 180.0f;
			_player->_renderer.AnimPaused = true;
		} else {
			_player->_frozenTimeLeft = std::min(1.0f, _player->_frozenTimeLeft);
		}
	}
	int32_t jjPLAYER::get_currTile() {
		noop();
		return 0;
	}
	bool jjPLAYER::startSugarRush(int32_t time) {
		noop();
		// TODO: if boss active, return false
		_player->ActivateSugarRush((float)time * 60.0f / 70.0f);
		return true;
	}
	int8_t jjPLAYER::get_health() const {
		noop();
		return (int8_t)_player->_health;
	}
	int8_t jjPLAYER::set_health(int8_t value) {
		noop();
		_player->SetHealth(value);
		return value;
	}

	int8_t jjPLAYER::get_currWeapon() const {
		noop();
		return (int8_t)_player->_currentWeapon;
	}
	int8_t jjPLAYER::set_currWeapon(int8_t value) {
		noop();
		if (value < 0 || value >= (int8_t)WeaponType::Count) {
			return (int8_t)_player->_currentWeapon;
		}
		_player->_currentWeapon = (WeaponType)value;
		return value;
	}

	int32_t jjPLAYER::extendInvincibility(int32_t duration) {
		noop();
		return 0;
	}

	bool jjPLAYER::testForCoins(int32_t numberOfCoins) {
		noop();
		if (numberOfCoins > _player->_coins) {
			return false;
		}
		_player->AddCoins(-numberOfCoins);
		return true;
	}
	int32_t jjPLAYER::get_gems(uint32_t type) const {
		noop();
		return 0;
	}
	int32_t jjPLAYER::set_gems(uint32_t type, int32_t value) {
		noop();
		return 0;
	}
	bool jjPLAYER::testForGems(int32_t numberOfGems, uint32_t type) {
		noop();
		return false;
	}

	int32_t jjPLAYER::get_stoned() {
		noop();
		return (int32_t)(_player->_dizzyTime * 70.0f / 60.0f);
	}
	int32_t jjPLAYER::set_stoned(int32_t value) {
		noop();
		_player->SetDizzyTime(value * 60.0f / 70.0f);
		return value;
	}

	void jjPLAYER::suckerTube(int32_t xSpeed, int32_t ySpeed, bool center, bool noclip, bool trigSample) {
		noop();
	}
	void jjPLAYER::poleSpin(float xSpeed, float ySpeed, uint32_t delay) {
		noop();
	}
	void jjPLAYER::spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample) {
		noop();
	}

	bool jjPLAYER::get_isConnecting() const {
		noop();
		return false;
	}
	bool jjPLAYER::get_isIdle() const {
		noop();
		return false;
	}
	bool jjPLAYER::get_isOut() const {
		noop();
		return false;
	}
	bool jjPLAYER::get_isSpectating() const {
		noop();
		return false;
	}
	bool jjPLAYER::get_isInGame() const {
		noop();
		return true;
	}

	String jjPLAYER::get_name() const {
		noop();
		return { };
	}
	String jjPLAYER::get_nameUnformatted() const {
		noop();
		return { };
	}
	bool jjPLAYER::setName(const String& name) {
		noop();
		return false;
	}
	int8_t jjPLAYER::get_light() const {
		noop();
		return 0;
	}
	int8_t jjPLAYER::set_light(int8_t value) {
		noop();
		return 0;
	}
	uint32_t jjPLAYER::get_fur() const {
		noop();
		return 0;
	}
	uint32_t jjPLAYER::set_fur(uint32_t value) {
		noop();
		return 0;
	}

	bool jjPLAYER::get_noFire() const {
		noop();
		return !_player->_weaponAllowed;
	}
	bool jjPLAYER::set_noFire(bool value) {
		noop();
		_player->_weaponAllowed = !value;
		return value;
	}
	bool jjPLAYER::get_antiGrav() const {
		noop();
		return false;
	}
	bool jjPLAYER::set_antiGrav(bool value) {
		noop();
		return false;
	}
	bool jjPLAYER::get_invisibility() const {
		noop();
		return _player->GetState(Actors::ActorState::IsInvulnerable);
	}
	bool jjPLAYER::set_invisibility(bool value) {
		noop();
		return false;
	}
	bool jjPLAYER::get_noclipMode() const {
		noop();
		return false;
	}
	bool jjPLAYER::set_noclipMode(bool value) {
		noop();
		return false;
	}
	uint8_t jjPLAYER::get_lighting() const {
		noop();
		return (uint8_t)(_levelScriptLoader->_levelHandler->GetAmbientLight() * 64.0f);
	}
	uint8_t jjPLAYER::set_lighting(uint8_t value) {
		noop();
		_levelScriptLoader->_levelHandler->SetAmbientLight(nullptr, value / 64.0f);
		return value;
	}
	uint8_t jjPLAYER::resetLight() {
		noop();
		return 0;
	}

	bool jjPLAYER::get_playerKeyLeftPressed() {
		noop(); return false;
	}
	bool jjPLAYER::get_playerKeyRightPressed() {
		noop(); return false;
	}
	bool jjPLAYER::get_playerKeyUpPressed() {
		noop(); return false;
	}
	bool jjPLAYER::get_playerKeyDownPressed() {
		noop(); return false;
	}
	bool jjPLAYER::get_playerKeyFirePressed() {
		noop(); return false;
	}
	bool jjPLAYER::get_playerKeySelectPressed() {
		noop(); return false;
	}
	bool jjPLAYER::get_playerKeyJumpPressed() {
		noop(); return false;
	}
	bool jjPLAYER::get_playerKeyRunPressed() {
		noop(); return false;
	}
	void jjPLAYER::set_playerKeyLeftPressed(bool value) {
		noop();
	}
	void jjPLAYER::set_playerKeyRightPressed(bool value) {
		noop();
	}
	void jjPLAYER::set_playerKeyUpPressed(bool value) {
		noop();
	}
	void jjPLAYER::set_playerKeyDownPressed(bool value) {
		noop();
	}
	void jjPLAYER::set_playerKeyFirePressed(bool value) {
		noop();
	}
	void jjPLAYER::set_playerKeySelectPressed(bool value) {
		noop();
	}
	void jjPLAYER::set_playerKeyJumpPressed(bool value) {
		noop();
	}
	void jjPLAYER::set_playerKeyRunPressed(bool value) {
		noop();
	}

	bool jjPLAYER::get_powerup(uint8_t index) {
		noop();
		if (index < 0 || index >= (int8_t)WeaponType::Count) {
			return 0;
		}
		return (_player->_weaponUpgrades[index] & 0x01) == 0x01;
	}
	bool jjPLAYER::set_powerup(uint8_t index, bool value) {
		noop();
		if (index < 0 || index >= (int8_t)WeaponType::Count) {
			return 0;
		}
		_player->_weaponUpgrades[index] = (value ? 0x01 : 0x00);
		return value;
	}
	int32_t jjPLAYER::get_ammo(uint8_t index) const {
		noop();
		if (index < 0 || index >= (int8_t)WeaponType::Count) {
			return 0;
		}
		return _player->_weaponAmmo[index];
	}
	int32_t jjPLAYER::set_ammo(uint8_t index, int32_t value) {
		noop();
		if (index < 0 || index >= (int8_t)WeaponType::Count) {
			return 0;
		}
		_player->_weaponAmmo[index] = value * 256;
		return value;
	}

	bool jjPLAYER::offsetPosition(int32_t xPixels, int32_t yPixels) {
		noop();

		Vector2f pos = _player->GetPos();
		_player->WarpToPosition(Vector2f(pos.X + xPixels, pos.Y + yPixels), Actors::WarpFlags::Fast);
		return true;
	}
	bool jjPLAYER::warpToTile(int32_t xTile, int32_t yTile, bool fast) {
		noop();

		_player->WarpToPosition(Vector2f(xTile * TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2,
			yTile * TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2),
			fast ? Actors::WarpFlags::Fast : Actors::WarpFlags::Default);
		return true;
	}
	bool jjPLAYER::warpToID(uint8_t warpID, bool fast) {
		noop();

		auto events = _levelScriptLoader->_levelHandler->EventMap();
		Vector2f c = events->GetWarpTarget(warpID);
		if (c.X >= 0.0f && c.Y >= 0.0f) {
			_player->WarpToPosition(c, fast ? Actors::WarpFlags::Fast : Actors::WarpFlags::Default);
			return true;
		}
		return false;
	}

	uint32_t jjPLAYER::morph(bool rabbitsOnly, bool morphEffect) {
		noop();
		return 0;
	}
	uint32_t jjPLAYER::morphTo(uint32_t charNew, bool morphEffect) {
		noop();
		// TODO: morphEffect
		_player->MorphTo((PlayerType)charNew);
		return (uint32_t)_player->_playerType;
	}
	uint32_t jjPLAYER::revertMorph(bool morphEffect) {
		noop();
		// TODO: morphEffect
		_player->MorphRevert();
		return (uint32_t)_player->_playerType;
	}
	uint32_t jjPLAYER::get_charCurr() const {
		noop();
		return (uint32_t)_player->_playerType;
	}

	void jjPLAYER::kill() {
		noop();
		_player->DecreaseHealth(INT32_MAX);
	}
	bool jjPLAYER::hurt(int8_t damage, bool forceHurt, jjPLAYER* attacker) {
		noop();

		// TODO: forceHurt and return value
		_player->TakeDamage(damage);
		return false;
	}

	uint32_t jjPLAYER::get_timerState() const {
		noop();
		return 0;
	}
	bool jjPLAYER::get_timerPersists() const {
		noop();
		return false;
	}
	bool jjPLAYER::set_timerPersists(bool value) {
		noop();
		return false;
	}
	uint32_t jjPLAYER::timerStart(int32_t ticks, bool startPaused) {
		noop();
		return 0;
	}
	uint32_t jjPLAYER::timerPause() {
		noop();
		return 0;
	}
	uint32_t jjPLAYER::timerResume() {
		noop();
		return 0;
	}
	uint32_t jjPLAYER::timerStop() {
		noop();
		return 0;
	}
	int32_t jjPLAYER::get_timerTime() const {
		noop();
		return 0;
	}
	int32_t jjPLAYER::set_timerTime(int32_t value) {
		noop();
		return 0;
	}
	void jjPLAYER::timerFunction(const String& functionName) {
		noop();
	}
	void jjPLAYER::timerFunctionPtr(void* function) {
		noop();
	}
	void jjPLAYER::timerFunctionFuncPtr(void* function) {
		noop();
	}

	bool jjPLAYER::activateBoss(bool activate) {
		noop();

		// TODO: activate
		_levelScriptLoader->_levelHandler->BroadcastTriggeredEvent(_player, EventType::AreaActivateBoss, nullptr);
		return true;
	}
	bool jjPLAYER::limitXScroll(uint16_t left, uint16_t width) {
		noop();

		_levelScriptLoader->_levelHandler->LimitCameraView(left * Tiles::TileSet::DefaultTileSize, width * Tiles::TileSet::DefaultTileSize);
		return true;
	}
	void jjPLAYER::cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant) {
		noop();
	}
	void jjPLAYER::cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant) {
		noop();
	}
	void jjPLAYER::cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant) {
		noop();
	}
	void jjPLAYER::cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant) {
		noop();
	}
	void jjPLAYER::cameraUnfreeze(bool instant) {
		noop();
	}
	void jjPLAYER::showText(const String& text, uint32_t size) {
		noop();

		// TODO: size
		// Input string must be recoded in Legacy context
		auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
		_levelScriptLoader->_levelHandler->ShowLevelText(recodedText);
	}
	void jjPLAYER::showTextByID(uint32_t textID, uint32_t offset, uint32_t size) {
		noop();

		// TODO: size
		auto text = _levelScriptLoader->_levelHandler->GetLevelText(textID, offset, '|');
		_levelScriptLoader->_levelHandler->ShowLevelText(text);
	}

	uint32_t jjPLAYER::get_fly() const {
		noop();
		return 0;
	}
	uint32_t jjPLAYER::set_fly(uint32_t value) {
		noop();
		return 0;
	}

	int32_t jjPLAYER::fireBulletDirection(uint8_t gun, bool depleteAmmo, bool requireAmmo, uint32_t direction) {
		noop();
		return 0;
	}
	int32_t jjPLAYER::fireBulletAngle(uint8_t gun, bool depleteAmmo, bool requireAmmo, float angle) {
		noop();
		return 0;
	}

	float jjPLAYER::get_cameraX() const {
		noop();
		return 0.0f;
	}
	float jjPLAYER::get_cameraY() const {
		noop();
		return 0.0f;
	}
	int32_t jjPLAYER::get_deaths() const {
		noop();
		return 0;
	}

	bool jjPLAYER::get_isJailed() const {
		noop();
		return false;
	}
	bool jjPLAYER::get_isZombie() const {
		noop();
		return false;
	}
	int32_t jjPLAYER::get_lrsLives() const {
		noop();
		return 0;
	}
	int32_t jjPLAYER::get_roasts() const {
		noop();
		return 0;
	}
	int32_t jjPLAYER::get_laps() const {
		noop();
		return 0;
	}
	int32_t jjPLAYER::get_lapTimeCurrent() const {
		noop();
		return 0;
	}
	int32_t jjPLAYER::get_lapTimes(uint32_t index) const {
		noop();
		return 0;
	}
	int32_t jjPLAYER::get_lapTimeBest() const {
		noop();
		return 0;
	}
	bool jjPLAYER::get_isAdmin() const {
		noop();
		return false;
	}
	bool jjPLAYER::hasPrivilege(const String& privilege, uint32_t moduleID) const {
		noop();
		return false;
	}

	bool jjPLAYER::doesCollide(const jjOBJ* object, bool always) const {
		noop();
		return false;
	}
	int jjPLAYER::getObjectHitForce(const jjOBJ& target) const {
		noop();
		return 0;
	}
	bool jjPLAYER::objectHit(jjOBJ* target, int force, uint32_t playerHandling) {
		noop();
		return false;
	}
	bool jjPLAYER::isEnemy(const jjPLAYER* victim) const {
		noop();
		return false;
	}

	int32_t get_jjPlayerCount() {
		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPLAYER));
		return owner->GetPlayers().size();
	}
	int32_t get_jjLocalPlayerCount() {
		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPLAYER));
		return owner->GetPlayers().size();
	}

	jjPLAYER* get_jjP() {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPLAYER));
		return new(mem) jjPLAYER(owner, 0);
	}
	jjPLAYER* get_jjPlayers(uint8_t index) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPLAYER));
		return new(mem) jjPLAYER(owner, index);
	}
	jjPLAYER* get_jjLocalPlayers(uint8_t index) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPLAYER));
		return new(mem) jjPLAYER(owner, index);
	}

	jjPIXELMAP::jjPIXELMAP() : _refCount(1) {
		noop();
	}
	jjPIXELMAP::~jjPIXELMAP() {
		noop();
	}

	jjPIXELMAP* jjPIXELMAP::CreateFromTile() {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPIXELMAP));
		return new(mem) jjPIXELMAP();
	}
	jjPIXELMAP* jjPIXELMAP::CreateFromSize(uint32_t width, uint32_t height) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPIXELMAP));
		return new(mem) jjPIXELMAP();
	}
	jjPIXELMAP* jjPIXELMAP::CreateFromFrame(const jjANIMFRAME* animFrame) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPIXELMAP));
		return new(mem) jjPIXELMAP();
	}
	jjPIXELMAP* jjPIXELMAP::CreateFromLayer(uint32_t left, uint32_t top, uint32_t width, uint32_t height, uint32_t layer) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPIXELMAP));
		return new(mem) jjPIXELMAP();
	}
	jjPIXELMAP* jjPIXELMAP::CreateFromLayerObject(uint32_t left, uint32_t top, uint32_t width, uint32_t height, const jjLAYER* layer) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPIXELMAP));
		return new(mem) jjPIXELMAP();
	}
	jjPIXELMAP* jjPIXELMAP::CreateFromTexture(uint32_t animFrame) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPIXELMAP));
		return new(mem) jjPIXELMAP();
	}
	jjPIXELMAP* jjPIXELMAP::CreateFromFilename(const String& filename, const jjPAL* palette, uint8_t threshold) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjPIXELMAP));
		return new(mem) jjPIXELMAP();
	}

	void jjPIXELMAP::AddRef()
	{
		_refCount++;
	}

	void jjPIXELMAP::Release()
	{
		if (--_refCount == 0) {
			this->~jjPIXELMAP();
			asFreeMem(this);
		}
	}

	// Assignment operator
	jjPIXELMAP& jjPIXELMAP::operator=(const jjPIXELMAP& o)
	{
		// Copy only the content, not the script proxy class
		//_value = o._value;
		return *this;
	}

	// TODO: return type uint8_t& instead?
	uint8_t jjPIXELMAP::GetPixel(uint32_t x, uint32_t y) {
		noop();
		return 0;
	}

	bool jjPIXELMAP::saveToTile(uint16_t tileID, bool hFlip) const {
		noop();
		return false;
	}
	bool jjPIXELMAP::saveToFrame(jjANIMFRAME* frame) const {
		noop();
		return false;
	}
	bool jjPIXELMAP::saveToFile(const String& filename, const jjPAL& palette) const {
		noop();
		return false;
	}

	jjMASKMAP::jjMASKMAP() : _refCount(1) {
		noop();
	}
	jjMASKMAP::~jjMASKMAP() {
		noop();
	}

	jjMASKMAP* jjMASKMAP::CreateFromBool(bool filled) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjMASKMAP));
		return new(mem) jjMASKMAP();
	}
	jjMASKMAP* jjMASKMAP::CreateFromTile(uint16_t tileID) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjMASKMAP));
		return new(mem) jjMASKMAP();
	}

	void jjMASKMAP::AddRef()
	{
		_refCount++;
	}

	void jjMASKMAP::Release()
	{
		if (--_refCount == 0) {
			this->~jjMASKMAP();
			asFreeMem(this);
		}
	}

	jjMASKMAP& jjMASKMAP::operator=(const jjMASKMAP& o)
	{
		// Copy only the content, not the script proxy class
		//_value = o._value;
		return *this;
	}

	// TODO: return type bool& instead?
	bool jjMASKMAP::GetPixel(uint32_t x, uint32_t y) {
		noop();
		return false;
	}

	bool jjMASKMAP::save(uint16_t tileID, bool hFlip) const {
		noop();
		return false;
	}

	jjLAYER::jjLAYER() : _refCount(1) {
		noop();
	}
	jjLAYER::~jjLAYER() {
		noop();
	}

	jjLAYER* jjLAYER::CreateFromSize(uint32_t width, uint32_t height, jjLAYER* self) {
		noop();
		return new(self) jjLAYER();
	}
	jjLAYER* jjLAYER::CreateCopy(jjLAYER* other, jjLAYER* self) {
		noop();
		return new(self) jjLAYER();
	}

	void jjLAYER::AddRef()
	{
		_refCount++;
	}

	void jjLAYER::Release()
	{
		if (--_refCount == 0) {
			this->~jjLAYER();
			asFreeMem(this);
		}
	}

	jjLAYER& jjLAYER::operator=(const jjLAYER& o)
	{
		// Copy only the content, not the script proxy class
		//_value = o._value;
		return *this;
	}

	jjLAYER* jjLAYER::get_jjLayers(int32_t index) {
		noop();

		auto ctx = asGetActiveContext();
		auto owner = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(ScriptLoader::EngineToOwner));

		void* mem = asAllocMem(sizeof(jjLAYER));
		return new(mem) jjLAYER();
	}

	uint32_t jjLAYER::get_spriteMode() const {
		noop();
		return 0;
	}
	uint32_t jjLAYER::set_spriteMode(uint32_t value) const {
		noop();
		return 0;
	}
	uint8_t jjLAYER::get_spriteParam() const {
		noop();
		return 0;
	}
	uint8_t jjLAYER::set_spriteParam(uint8_t value) const {
		noop();
		return 0;
	}

	void jjLAYER::setXSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const {
		noop();
	}
	void jjLAYER::setYSpeed(float newspeed, bool newSpeedIsAnAutoSpeed) const {
		noop();
	}
	float jjLAYER::getXPosition(const jjPLAYER* play) const {
		noop();
		return 0;
	}
	float jjLAYER::getYPosition(const jjPLAYER* play) const {
		noop();
		return 0;
	}

	CScriptArray* jjLAYER::jjLayerOrderGet() {
		noop();
		auto ctx = asGetActiveContext();

		auto engine = ctx->GetEngine();
		return CScriptArray::Create(engine->GetTypeInfoByDecl("array<jjLAYER@>"), 16);
	}
	bool jjLAYER::jjLayerOrderSet(const CScriptArray& order) {
		noop();
		return false;
	}
	CScriptArray* jjLAYER::jjLayersFromLevel(const String& filename, const CScriptArray& layerIDs, int32_t tileIDAdjustmentFactor) {
		noop();
		auto ctx = asGetActiveContext();
		auto engine = ctx->GetEngine();
		return CScriptArray::Create(engine->GetTypeInfoByDecl("array<jjLAYER@>"), 16);
	}
	bool jjLAYER::jjTilesFromTileset(const String& filename, uint32_t firstTileID, uint32_t tileCount, const CScriptArray* paletteColorMapping) {
		noop();
		return false;
	}

	bool mlleSetup() {
		noop(); return true;
	}

	float get_sinTable(uint32_t angle) {
		noop();
		return sinf(angle * fTwoPi / 1024.0f);
	};
	float get_cosTable(uint32_t angle) {
		noop();
		return cosf(angle * fTwoPi / 1024.0f);
	};
	uint32_t RandWord32() {
		noop();
		return Random().Next();
	}
	uint64_t unixTimeSec() {
		noop(); return 0;
	}
	uint64_t unixTimeMs() {
		noop(); return 0;
	}

	int32_t GetFPS() {
		noop(); return 0;
	}


	bool isAdmin() {
		noop(); return false;
	}

	int32_t GetDifficulty() {
		noop(); return 0;
	}
	int32_t SetDifficulty(int32_t value) {
		noop(); return 0;
	}

	String getLevelFileName() {
		noop(); return "";
	}
	String getCurrLevelName() {
		noop(); return "";
	}
	void setCurrLevelName(const String& in) {
		noop();
	}
	String LevelScriptLoader::get_jjMusicFileName() {
		noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->_musicCurrentPath;
	}
	String get_jjTilesetFileName() {
		noop(); return "";
	}

	String LevelScriptLoader::get_jjHelpStrings(uint32_t index) {
		noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->GetLevelText(index);
	}
	void LevelScriptLoader::set_jjHelpStrings(uint32_t index, const String& text) {
		noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->OverrideLevelText(index, text);
	}

	int32_t get_gameState() {
		noop(); return 0;
	}

	// TODO

	void LevelScriptLoader::jjAlert(const String& text, bool sendToAll, uint32_t size)
	{
		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->ShowLevelText(text);
	}
	void jjPrint(const String& text, bool timestamp) {
		LOGW("%s", text.data());
	}
	void jjDebug(const String& text, bool timestamp) {
		LOGD("%s", text.data());
	}
	void jjChat(const String& text, bool teamchat) {
		LOGW("%s", text.data());
	}
	void jjConsole(const String& text, bool sendToAll) {
		LOGW("%s", text.data());
	}
	void jjSpy(const String& text) {
		LOGD("%s", text.data());
	}

	// TODO

	int32_t getBorderWidth() {
		noop(); return 0;
	}
	int32_t getBorderHeight() {
		noop(); return 0;
	}
	bool getSplitscreenType() {
		noop(); return false;
	}
	bool setSplitscreenType() {
		noop(); return false;
	}

	// TODO

	int32_t get_teamScore(int32_t color) {
		noop(); return 0;
	}
	int32_t GetMaxHealth() {
		noop(); return 0;
	}
	int32_t GetStartHealth() {
		noop(); return 0;
	}

	// TODO

	float get_layerXOffset(uint8_t id) {
		noop(); return 0;
	}
	float set_layerXOffset(uint8_t id, float value) {
		noop(); return 0;
	}
	float get_layerYOffset(uint8_t id) {
		noop(); return 0;
	}
	float set_layerYOffset(uint8_t id, float value) {
		noop(); return 0;
	}
	int get_layerWidth(uint8_t id) {
		noop(); return 0;
	}
	int get_layerRealWidth(uint8_t id) {
		noop(); return 0;
	}
	int get_layerRoundedWidth(uint8_t id) {
		noop(); return 0;
	}
	int get_layerHeight(uint8_t id) {
		noop(); return 0;
	}
	float get_layerXSpeed(uint8_t id) {
		noop(); return 0;
	}
	float set_layerXSpeed(uint8_t id, float value) {
		noop(); return 0;
	}
	float get_layerYSpeed(uint8_t id) {
		noop(); return 0;
	}
	float set_layerYSpeed(uint8_t id, float value) {
		noop(); return 0;
	}
	float get_layerXAutoSpeed(uint8_t id) {
		noop(); return 0;
	}
	float set_layerXAutoSpeed(uint8_t id, float value) {
		noop(); return 0;
	}
	float get_layerYAutoSpeed(uint8_t id) {
		noop(); return 0;
	}
	float set_layerYAutoSpeed(uint8_t id, float value) {
		noop(); return 0;
	}
	bool get_layerHasTiles(uint8_t id) {
		noop(); return false;
	}
	bool set_layerHasTiles(uint8_t id, bool value) {
		noop(); return false;
	}
	bool get_layerTileHeight(uint8_t id) {
		noop(); return false;
	}
	bool set_layerTileHeight(uint8_t id, bool value) {
		noop(); return false;
	}
	bool get_layerTileWidth(uint8_t id) {
		noop(); return false;
	}
	bool set_layerTileWidth(uint8_t id, bool value) {
		noop(); return false;
	}
	bool get_layerLimitVisibleRegion(uint8_t id) {
		noop(); return false;
	}
	bool set_layerLimitVisibleRegion(uint8_t id, bool value) {
		noop(); return false;
	}

	void setLayerXSpeedSeamlessly(uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) {
		noop();
	}
	void setLayerYSpeedSeamlessly(uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) {
		noop();
	}

	// TODO

	void jjDrawPixel(float xPixel, float yPixel, uint8_t color, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawRectangle(float xPixel, float yPixel, int32_t width, int32_t height, uint8_t color, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawSprite(float xPixel, float yPixel, int32_t setID, uint8_t animation, uint8_t frame, int8_t direction, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawSpriteFromCurFrame(float xPixel, float yPixel, uint32_t sprite, int8_t direction, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawResizedSprite(float xPixel, float yPixel, int32_t setID, uint8_t animation, uint8_t frame, float xScale, float yScale, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, uint32_t sprite, float xScale, float yScale, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawRotatedSprite(float xPixel, float yPixel, int32_t setID, uint8_t animation, uint8_t frame, int32_t angle, float xScale, float yScale, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, uint32_t sprite, int32_t angle, float xScale, float yScale, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, uint32_t sprite, int32_t length, int32_t curvature, spriteType mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawTile(float xPixel, float yPixel, uint16_t tile, uint32_t tileQuadrant, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawString(float xPixel, float yPixel, const String& text, uint32_t size, uint32_t mode, uint8_t param, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	void jjDrawStringEx(float xPixel, float yPixel, const String& text, uint32_t size, const jjTEXTAPPEARANCE& appearance, uint8_t param1, spriteType spriteMode, uint8_t param2, int8_t layerZ, uint8_t layerXY, int8_t playerID) {
		noop();
	}

	int32_t jjGetStringWidth(const String& text, uint32_t size, const jjTEXTAPPEARANCE& style) {
		noop(); return 0;
	}

	bool LevelScriptLoader::get_jjTriggers(uint8_t id) {
		//noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		return _this->_levelHandler->GetTrigger(id);
	}
	bool LevelScriptLoader::set_jjTriggers(uint8_t id, bool value) {
		//noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->SetTrigger(id, value);
		return value;
	}
	bool LevelScriptLoader::jjSwitchTrigger(uint8_t id) {
		//noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->SetTrigger(id, !_this->_levelHandler->GetTrigger(id));
		return _this->_levelHandler->GetTrigger(id);
	}

	bool isNumberedASFunctionEnabled(uint8_t id) {
		noop(); return false;
	}
	bool setNumberedASFunctionEnabled(uint8_t id, bool value) {
		noop(); return false;
	}
	void reenableAllNumberedASFunctions() {
		noop();
	}

	float getWaterLevel() {
		noop(); return 0;
	}
	float getWaterLevel2() {
		noop(); return 0;
	}
	float setWaterLevel(float value, bool instant) {
		noop(); return 0;
	}
	float get_waterChangeSpeed() {
		noop(); return 0;
	}
	float set_waterChangeSpeed(float value) {
		noop(); return 0;
	}
	int32_t get_waterLayer() {
		noop(); return 0;
	}
	int32_t set_waterLayer(int32_t value) {
		noop(); return 0;
	}
	void setWaterGradient(uint8_t red1, uint8_t green1, uint8_t blue1, uint8_t red2, uint8_t green2, uint8_t blue2) {
		noop();
	}
	// TODO: void setWaterGradientFromColors(jjPALCOLOR color1, jjPALCOLOR color2)
	void setWaterGradientToTBG() {
		noop();
	}
	void resetWaterGradient() {
		noop();
	}

	void triggerRock(uint8_t id) {
		noop();
	}

	void cycleTo(const String& filename, bool warp, bool fast) {
		noop();
	}
	void LevelScriptLoader::jjNxt(bool warp, bool fast)
	{
		ExitType exitType = (warp ? ExitType::Warp : ExitType::Normal);
		if (fast) {
			exitType |= ExitType::FastTransition;
		}

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->BeginLevelChange(exitType, { });
	}

	bool getEnabledTeam(uint8_t team) {
		noop(); return false;
	}

	bool getKeyDown(uint8_t key) {
		noop(); return false;
	}
	int32_t getCursorX() {
		noop(); return 0;
	}
	int32_t getCursorY() {
		noop(); return 0;
	}

	bool LevelScriptLoader::jjMusicLoad(const String& filename, bool forceReload, bool temporary) {
		noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		_this->_levelHandler->BeginPlayMusic(filename, !temporary, forceReload);

		return false;
	}
	void LevelScriptLoader::jjMusicStop() {
		noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->stop();
		}
	}
	void LevelScriptLoader::jjMusicPlay() {
		noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->play();
		}
	}
	void LevelScriptLoader::jjMusicPause() {
		noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->stop();
		}
	}
	void LevelScriptLoader::jjMusicResume() {
		noop();

		auto ctx = asGetActiveContext();
		auto _this = static_cast<LevelScriptLoader*>(ctx->GetEngine()->GetUserData(EngineToOwner));
		if (_this->_levelHandler->_music != nullptr && _this->_levelHandler->_music->isPaused()) {
			_this->_levelHandler->_music->play();
		}
	}

	void playSample(float xPixel, float yPixel, int32_t sample, int32_t volume, int32_t frequency) {
		noop();
	}
	int32_t playLoopedSample(float xPixel, float yPixel, int32_t sample, int32_t volume, int32_t frequency) {
		noop(); return 0;
	}
	void playPrioritySample(int32_t sample) {
		noop();
	}
	bool isSampleLoaded(int32_t sample) {
		noop(); return false;
	}
	bool loadSample(int32_t sample, const String& filename) {
		noop(); return false;
	}

	bool getUseLayer8Speeds() {
		noop(); return false;
	}
	bool setUseLayer8Speeds(bool value) {
		noop(); return false;
	}

	// TODO

	int32_t GetEvent(uint16_t tx, uint16_t ty) {
		noop(); return 0;
	}
	int32_t GetEventParamWrapper(uint16_t tx, uint16_t ty, int32_t offset, int32_t length) {
		noop(); return 0;
	}
	void SetEventByte(uint16_t tx, uint16_t ty, uint8_t newEventId) {
		noop();
	}
	void SetEventParam(uint16_t tx, uint16_t ty, int8_t offset, int8_t length, int32_t newValue) {
		noop();
	}
	int8_t GetTileType(uint16_t tile) {
		noop(); return 0;
	}
	int8_t SetTileType(uint16_t tile, uint16_t value) {
		noop(); return 0;
	}

	// TODO

	uint16_t jjGetStaticTile(uint16_t tileID) {
		noop();
		return 0;
	}
	uint16_t jjTileGet(uint8_t layer, int32_t xTile, int32_t yTile) {
		noop();
		return 0;
	}
	uint16_t jjTileSet(uint8_t layer, int32_t xTile, int32_t yTile, uint16_t newTile) {
		noop();
		return 0;
	}
	void jjGenerateSettableTileArea(uint8_t layer, int32_t xTile, int32_t yTile, int32_t width, int32_t height) {
		noop();
	}

	// TODO

	bool jjMaskedPixel(int32_t xPixel, int32_t yPixel) {
		noop();
		return false;
	}
	bool jjMaskedPixelLayer(int32_t xPixel, int32_t yPixel, uint8_t layer) {
		noop();
		return false;
	}
	bool jjMaskedHLine(int32_t xPixel, int32_t lineLength, int32_t yPixel) {
		noop();
		return false;
	}
	bool jjMaskedHLineLayer(int32_t xPixel, int32_t lineLength, int32_t yPixel, uint8_t layer) {
		noop();
		return false;
	}
	bool jjMaskedVLine(int32_t xPixel, int32_t yPixel, int32_t lineLength) {
		noop();
		return false;
	}
	bool jjMaskedVLineLayer(int32_t xPixel, int32_t yPixel, int32_t lineLength, uint8_t layer) {
		noop();
		return false;
	}
	bool jjMaskedTopVLine(int32_t xPixel, int32_t yPixel, int32_t lineLength) {
		noop();
		return false;
	}
	bool jjMaskedTopVLineLayer(int32_t xPixel, int32_t yPixel, int32_t lineLength, uint8_t layer) {
		noop();
		return false;
	}

	// TODO

	void jjSetModPosition(int32_t order, int32_t row, bool reset) {
		noop();
	}
	void jjSlideModChannelVolume(int32_t channel, float volume, int32_t milliseconds) {
		noop();
	}
	int32_t jjGetModOrder() {
		noop();
		return 0;
	}
	int32_t jjGetModRow() {
		noop();
		return 0;
	}
	int32_t jjGetModTempo() {
		noop();
		return 0;
	}
	void jjSetModTempo(uint8_t speed) {
		noop();
	}
	int32_t jjGetModSpeed() {
		noop();
		return 0;
	}
	void jjSetModSpeed(uint8_t speed) {
		noop();
	}

	uint32_t getCustomSetID(uint8_t index) {
		noop();
		return mCOUNT + index;
	}
}

#endif