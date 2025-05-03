#if defined(WITH_ANGELSCRIPT)

#include "JJ2PlusDefinitions.h"
#include "LevelScriptLoader.h"

#include "../LevelHandler.h"
#include "../Actors/ActorBase.h"
#include "../Actors/Player.h"
#include "../Compatibility/JJ2Strings.h"
#include "../UI/HUD.h"
#include "../UI/InGameConsole.h"

#include "../../nCine/Application.h"
#include "../../nCine/Base/Random.h"

#include <regex>

#include <Containers/DateTime.h>
#include <Containers/StringStl.h>

namespace Jazz2::Scripting
{
	static void Unimplemented(const char* sourceName) {
		auto ctx = asGetActiveContext();
		if (ctx != nullptr) {
			const char* sectionName;
			std::int32_t lineNumber = ctx->GetLineNumber(0, nullptr, &sectionName);
			LOGW("%s (called from \"%s:%i\")", sourceName, sectionName, lineNumber);
		} else {
			LOGW("%s", sourceName);
		}
	}

#if defined(DEATH_TRACE)
#	define noop() Unimplemented(NCINE_CURRENT_FUNCTION)
#else
#	define noop() do {} while (false)
#endif

	namespace Legacy
	{
		void jjTEXTAPPEARANCE::constructor(void* self) {
			noop();
			new(self) jjTEXTAPPEARANCE{};
		}
		void jjTEXTAPPEARANCE::constructorMode(std::uint32_t mode, void* self) {
			noop();
			new(self) jjTEXTAPPEARANCE{};
		}

		jjTEXTAPPEARANCE& jjTEXTAPPEARANCE::operator=(std::uint32_t other) {
			noop(); return *this;
		}

		void jjPALCOLOR::Create(void* self) {
			noop();
			new(self) jjPALCOLOR{};
		}
		void jjPALCOLOR::CreateFromRgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue, void* self) {
			noop();
			new(self) jjPALCOLOR{red, green, blue};
		}

		std::uint8_t jjPALCOLOR::getHue() {
			noop(); return 0;
		}
		std::uint8_t jjPALCOLOR::getSat() {
			noop(); return 0;
		}
		std::uint8_t jjPALCOLOR::getLight() {
			noop(); return 0;
		}

		void jjPALCOLOR::swizzle(std::uint32_t redc, std::uint32_t greenc, std::uint32_t bluec) {
			noop();

			std::uint8_t r = red;
			std::uint8_t g = green;
			std::uint8_t b = blue;

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
		void jjPALCOLOR::setHSL(std::int32_t hue, std::uint8_t sat, std::uint8_t light) {
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

		jjPAL& jjPAL::operator=(const jjPAL& o) {
			noop();
			return *this;
		}

		bool jjPAL::operator==(const jjPAL& o) {
			noop();
			return (this == &o);
		}

		jjPALCOLOR& jjPAL::getColor(std::uint8_t idx) {
			noop();
			return _palette[idx];
		}

		const jjPALCOLOR& jjPAL::getConstColor(std::uint8_t idx) const {
			noop();
			return _palette[idx];
		}

		jjPALCOLOR& jjPAL::setColorEntry(std::uint8_t idx, jjPALCOLOR& value) {
			noop();
			_palette[idx] = value;
			return _palette[idx];
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
		void jjPAL::fill(std::uint8_t red, std::uint8_t green, std::uint8_t blue, float opacity) {
			noop();
		}
		void jjPAL::fillTint(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t start, std::uint8_t length, float opacity) {
			noop();
		}
		void jjPAL::fillFromColor(jjPALCOLOR color, float opacity) {
			noop();
		}
		void jjPAL::fillTintFromColor(jjPALCOLOR color, std::uint8_t start, std::uint8_t length, float opacity) {
			noop();
		}
		void jjPAL::gradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive) {
			noop();
		}
		void jjPAL::gradientFromColor(jjPALCOLOR color1, jjPALCOLOR color2, std::uint8_t start, std::uint8_t length, float opacity, bool inclusive) {
			noop();
		}
		void jjPAL::copyFrom(std::uint8_t start, std::uint8_t length, std::uint8_t start2, const jjPAL& source, float opacity) {
			noop();
		}
		std::uint8_t jjPAL::findNearestColor(jjPALCOLOR color) {
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

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjSTREAM));
			return new(mem) jjSTREAM();
		}
		jjSTREAM* jjSTREAM::CreateFromFile(const String& filename) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

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

		std::uint32_t jjSTREAM::getSize() const {
			noop();
			return 0;
		}

		bool jjSTREAM::isEmpty() const {
			noop();
			return false;
		}

		bool jjSTREAM::save(const String& tilename) const {
			noop();
			return true;
		}

		void jjSTREAM::clear() {
			noop();
		}

		bool jjSTREAM::discard(std::uint32_t count) {
			return 0;
		}

		bool jjSTREAM::write(const String& value) {
			noop();
			return true;
		}
		bool jjSTREAM::write(const jjSTREAM& value) {
			noop();
			return true;
		}
		bool jjSTREAM::get(String& value, std::uint32_t count) {
			noop();
			return false;
		}
		bool jjSTREAM::get(jjSTREAM& value, std::uint32_t count) {
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
		bool jjSTREAM::push(std::uint8_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::int8_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::uint16_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::int16_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::uint32_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::int32_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::uint64_t value) {
			noop();
			return false;
		}
		bool jjSTREAM::push(std::int64_t value) {
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
		bool jjSTREAM::pop(std::uint8_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::int8_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::uint16_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::int16_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::uint32_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::int32_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::uint64_t& value) {
			noop();
			return false;
		}
		bool jjSTREAM::pop(std::int64_t& value) {
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

		jjRNG::jjRNG(std::uint64_t seed) : _refCount(1) {
			noop();
		}
		jjRNG::~jjRNG() {
			noop();
		}
		jjRNG* jjRNG::Create(std::uint64_t seed) {
			noop();
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
			_random.Initialize(value, DefaultInitSequence);
		}
		void jjRNG::discard(std::uint64_t count) {
			for (std::uint64_t i = 0; i < count; i++) {
				_random.Next();
			}
		}

		jjBEHAVIOR* jjBEHAVIOR::Create(jjBEHAVIOR* self) {
			noop();
			return new(self) jjBEHAVIOR();
		}
		jjBEHAVIOR* jjBEHAVIOR::CreateFromBehavior(std::uint32_t behavior, jjBEHAVIOR* self) {
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
		jjBEHAVIOR& jjBEHAVIOR::operator=(std::uint32_t other) {
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
		bool jjBEHAVIOR::operator==(std::uint32_t other) const {
			noop();
			return false;
		}
		bool jjBEHAVIOR::operator==(const asIScriptFunction* other) const {
			noop();
			return false;
		}
		jjBEHAVIOR::operator std::uint32_t() {
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

		jjANIMFRAME* jjANIMFRAME::get_jjAnimFrames(std::uint32_t index) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

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
		bool jjANIMFRAME::doesCollide(std::int32_t xPos, std::int32_t yPos, std::int32_t direction, const jjANIMFRAME* frame2, std::int32_t xPos2, std::int32_t yPos2, std::int32_t direction2, bool always) const {
			noop();
			return false;
		}

		jjANIMATION::jjANIMATION(std::uint32_t index) : _refCount(1), _index(index) {
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
		bool jjANIMATION::load(const String& filename, std::int32_t hotSpotX, std::int32_t hotSpotY, std::int32_t coldSpotYOffset, std::int32_t firstFrameToOverwrite) {
			noop();
			return false;
		}

		jjANIMATION* jjANIMATION::get_jjAnimations(std::uint32_t index) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjANIMATION));
			return new(mem) jjANIMATION(index);
		}

		std::uint32_t jjANIMATION::get_firstFrame() const {
			noop();
			return 0;
		}
		std::uint32_t jjANIMATION::set_firstFrame(std::uint32_t index) const {
			noop();
			return 0;
		}

		std::uint32_t jjANIMATION::getAnimFirstFrame() {
			noop();
			return 0;
		}

		jjANIMSET::jjANIMSET(std::uint32_t index) : _refCount(1), _index(index) {
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

		jjANIMSET* jjANIMSET::get_jjAnimSets(std::uint32_t index) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjANIMSET));
			return new(mem) jjANIMSET(index);
		}

		std::uint32_t jjANIMSET::convertAnimSetToUint() {
			noop();
			return _index;
		}

		jjANIMSET* jjANIMSET::load(std::uint32_t fileSetID, const String& filename, std::int32_t firstAnimToOverwrite, std::int32_t firstFrameToOverwrite) {
			noop();
			return this;
		}
		jjANIMSET* jjANIMSET::allocate(const CScriptArray& frameCounts) {
			noop();
			return this;
		}

		jjCANVAS::jjCANVAS(UI::HUD* hud, const Rectf& view)
			: Hud(hud), View(view)
		{
		}

		void jjCANVAS::DrawPixel(std::int32_t xPixel, int32_t yPixel, std::uint8_t color, std::uint32_t mode, std::uint8_t param) {
			noop();
		}
		void jjCANVAS::DrawRectangle(std::int32_t xPixel, int32_t yPixel, int32_t width, int32_t height, std::uint8_t color, std::uint32_t mode, std::uint8_t param) {
			noop();
		}
		void jjCANVAS::DrawSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int8_t direction, std::uint32_t mode, std::uint8_t param) {
			noop();
		}
		void jjCANVAS::DrawCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int8_t direction, std::uint32_t mode, std::uint8_t param) {
			noop();
		}
		void jjCANVAS::DrawResizedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			noop();
		}
		void jjCANVAS::DrawResizedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			noop();
		}
		void jjCANVAS::DrawTransformedSprite(std::int32_t xPixel, int32_t yPixel, int32_t setID, std::uint8_t animation, std::uint8_t frame, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			noop();
		}
		void jjCANVAS::DrawTransformedCurFrameSprite(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t angle, float xScale, float yScale, std::uint32_t mode, std::uint8_t param) {
			noop();
		}
		void jjCANVAS::DrawSwingingVine(std::int32_t xPixel, int32_t yPixel, std::uint32_t sprite, int32_t length, int32_t curvature, std::uint32_t mode, std::uint8_t param) {
			noop();
		}

		void jjCANVAS::ExternalDrawTile(std::int32_t xPixel, int32_t yPixel, std::uint16_t tile, std::uint32_t tileQuadrant) {
			noop();
		}
		void jjCANVAS::DrawTextBasicSize(std::int32_t xPixel, int32_t yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param) {
			noop();
			// TODO
			float scale;
			switch (size) {
				default:
				case 0:	// MEDIUM
					scale = 0.8f;
				case 1:	// SMALL
					scale = 0.6f;
				case 2:	// LARGE
					scale = 1.1f;
			}

			std::int32_t charOffset = 0;
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			Hud->_smallFont->DrawString(Hud, recodedText, charOffset, xPixel, yPixel, UI::HUD::MainLayer,
					UI::Alignment::TopLeft, UI::Font::DefaultColor, scale, 0.0f, 0.0f, 0.0f, 0.0f);
		}
		void jjCANVAS::DrawTextExtSize(std::int32_t xPixel, int32_t yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t mode, std::uint8_t param) {
			noop();
			// TODO
			Colorf color = UI::Font::DefaultColor;
			if (mode == spriteType_BLENDNORMAL) {
				color.A = param / 255.0f;
			}

			float scale;
			switch (size) {
				default:
				case 0:	// MEDIUM
					scale = 0.8f;
				case 1:	// SMALL
					scale = 0.6f;
				case 2:	// LARGE
					scale = 1.1f;
			}

			std::int32_t charOffset = 0;
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			Hud->_smallFont->DrawString(Hud, recodedText, charOffset, xPixel, yPixel, UI::HUD::MainLayer,
					UI::Alignment::TopLeft, color, scale, 0.0f, 0.0f, 0.0f, 0.0f);
		}

		void jjCANVAS::drawString(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param) {
			noop();
			// TODO
			std::int32_t charOffset = 0;
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			Hud->_smallFont->DrawString(Hud, recodedText, charOffset, xPixel, yPixel, UI::HUD::MainLayer,
					UI::Alignment::TopLeft, UI::Font::DefaultColor, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
		}

		void jjCANVAS::drawStringEx(std::int32_t xPixel, std::int32_t yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2) {
			noop();
		}

		void jjCANVAS::jjDrawString(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
			noop();
		}

		void jjCANVAS::jjDrawStringEx(float xPixel, float yPixel, const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, std::uint32_t spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
			noop();
		}

		int jjCANVAS::jjGetStringWidth(const String& text, const jjANIMATION& animation, const jjTEXTAPPEARANCE& style) {
			noop();
			// TODO
			return text.size() * 12.0f;
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
		std::uint32_t jjOBJ::get_lightType() const {
			noop();
			return 0;
		}
		std::uint32_t jjOBJ::set_lightType(std::uint32_t value) const {
			noop();
			return 0;
		}

		jjOBJ* jjOBJ::objectHit(jjOBJ* target, std::uint32_t playerHandling) {
			noop();
			return nullptr;
		}
		void jjOBJ::blast(std::int32_t maxDistance, bool blastObjects) {
			noop();
		}

		void jjOBJ::behave1(std::uint32_t behavior, bool draw) {
			noop();
		}
		void jjOBJ::behave2(jjBEHAVIOR behavior, bool draw) {
			noop();
		}
		void jjOBJ::behave3(jjVOIDFUNCOBJ behavior, bool draw) {
			noop();
		}

		std::int32_t jjOBJ::jjAddObject(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, std::uint32_t behavior) {
			noop();
			return 0;
		}
		std::int32_t jjOBJ::jjAddObjectEx(std::uint8_t eventID, float xPixel, float yPixel, std::uint16_t creatorID, std::uint32_t creatorType, jjVOIDFUNCOBJ behavior) {
			noop();
			return 0;
		}

		void jjOBJ::jjDeleteObject(std::int32_t objectID) {
			noop();
		}
		void jjOBJ::jjKillObject(std::int32_t objectID) {
			noop();
		}

		std::uint32_t jjOBJ::determineCurFrame(bool change) {
			noop(); return 0;
		}

		std::uint16_t jjOBJ::get_creatorID() const {
			noop(); return 0;
		}
		std::uint16_t jjOBJ::set_creatorID(std::uint16_t value) const {
			noop(); return 0;
		}
		std::uint32_t jjOBJ::get_creatorType() const {
			noop(); return 0;
		}
		std::uint32_t jjOBJ::set_creatorType(std::uint32_t value) const {
			noop(); return 0;
		}

		int16_t jjOBJ::determineCurAnim(std::uint8_t setID, std::uint8_t animation, bool change) {
			noop(); return 0;
		}

		std::uint32_t jjOBJ::get_bulletHandling() {
			noop(); return 0;
		}
		std::uint32_t jjOBJ::set_bulletHandling(std::uint32_t value) {
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

		std::uint32_t jjOBJ::get_playerHandling() {
			noop(); return false;
		}
		std::uint32_t jjOBJ::set_playerHandling(std::uint32_t value) {
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

		std::int32_t jjOBJ::get_var(std::uint8_t x) {
			noop(); return 0;
		}
		std::int32_t jjOBJ::set_var(std::uint8_t x, std::int32_t value) {
			noop(); return 0;
		}

		std::int32_t jjOBJ::draw() {
			noop();
			return 0;
		}
		std::int32_t jjOBJ::beSolid(bool shouldCheckForStompingLocalPlayers) {
			noop();
			return 0;
		}
		void jjOBJ::bePlatform(float xOld, float yOld, std::int32_t width, std::int32_t height) {
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
		std::int32_t jjOBJ::unfreeze(std::int32_t style) {
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
		int32_t jjOBJ::fireBullet(std::uint8_t eventID) {
			noop();
			return 0;
		}
		void jjOBJ::particlePixelExplosion(std::int32_t style) {
			noop();
		}
		void jjOBJ::grantPickup(jjPLAYER* player, std::int32_t frequency) {
			noop();
		}

		std::int32_t jjOBJ::findNearestPlayer(std::int32_t maxDistance) const {
			noop();
			return 0;
		}
		std::int32_t jjOBJ::findNearestPlayerEx(std::int32_t maxDistance, std::int32_t& foundDistance) const {
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

		std::uint8_t jjPARTICLEPIXEL::get_color(std::int32_t i) const {
			noop(); return 0;
		}
		std::uint8_t jjPARTICLEPIXEL::set_color(std::int32_t i, std::uint8_t value) {
			noop(); return 0;
		}

		String jjPARTICLESTRING::get_text() const {
			noop(); return {};
		}
		void jjPARTICLESTRING::set_text(String text) {
			noop();
		}

		jjPARTICLE::jjPARTICLE() : _refCount(1) {
			noop();
		}
		jjPARTICLE::~jjPARTICLE() {
			noop();
		}

		void jjPARTICLE::AddRef() {
			_refCount++;
		}

		void jjPARTICLE::Release() {
			if (--_refCount == 0) {
				this->~jjPARTICLE();
				asFreeMem(this);
			}
		}

		jjPLAYER::jjPLAYER(LevelScriptLoader* levelScripts, Actors::Player* player)
			: _levelScriptLoader(levelScripts), _player(player), _timerCallback(nullptr),
				_timerState(0), _timerLeft(0.0f), _timerPersists(false) {
			noop();
		}
		jjPLAYER::~jjPLAYER() {
			noop();
		}

		jjPLAYER& jjPLAYER::operator=(const jjPLAYER& o)
		{
			// Copy only the content, not the script proxy class
			//_value = o._value;
			return *this;
		}

		std::int32_t jjPLAYER::get_score() const {
			noop();
			return _player->_score;
		}
		std::int32_t jjPLAYER::set_score(std::int32_t value) {
			noop();
			_player->_score = value;
			return _player->_score;
		}
		std::int32_t jjPLAYER::get_scoreDisplayed() const {
			noop();
			return _player->_score;
		}
		std::int32_t jjPLAYER::set_scoreDisplayed(std::int32_t value) {
			noop();
			_player->_score = value;
			return _player->_score;
		}

		std::int32_t jjPLAYER::setScore(std::int32_t value) {
			noop();
			_player->_score = value;
			return _player->_score;
		}

		float jjPLAYER::get_xPos() const {
			noop();
			return _player->_pos.X;
		}
		float jjPLAYER::set_xPos(float value) {
			noop();
			_player->_pos.X = value;
			return _player->_pos.X;
		}
		float jjPLAYER::get_yPos() const {
			noop();
			return _player->_pos.Y;
		}
		float jjPLAYER::set_yPos(float value) {
			noop();
			_player->_pos.Y = value;
			return _player->_pos.Y;
		}
		float jjPLAYER::get_xAcc() const {
			noop();
			return _player->_externalForce.X;
		}
		float jjPLAYER::set_xAcc(float value) {
			noop();
			_player->_externalForce.X = value;
			return _player->_externalForce.X;
		}
		float jjPLAYER::get_yAcc() const {
			noop();
			return _player->_externalForce.Y;
		}
		float jjPLAYER::set_yAcc(float value) {
			noop();
			_player->_externalForce.Y = value;
			return _player->_externalForce.Y;
		}
		float jjPLAYER::get_xOrg() const {
			noop();
			// TODO
			return _player->_pos.X;
		}
		float jjPLAYER::set_xOrg(float value) {
			noop();
			// TODO
			return _player->_pos.X;
		}
		float jjPLAYER::get_yOrg() const {
			noop();
			// TODO
			return _player->_pos.Y;
		}
		float jjPLAYER::set_yOrg(float value) {
			noop();
			// TODO
			return _player->_pos.Y;
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
			} else if (_player->_frozenTimeLeft > 0.01f) {
				_player->_frozenTimeLeft = 0.01f;
			}
		}
		std::int32_t jjPLAYER::get_currTile() {
			noop();
			auto pos = _player->_pos;
			return ((std::int32_t)pos.X / 32) + ((std::int32_t)pos.Y / 32 * 65536);
		}
		bool jjPLAYER::startSugarRush(std::int32_t time) {
			noop();
			// TODO: if boss active, return false
			_player->ActivateSugarRush((float)time * 60.0f / 70.0f);
			return true;
		}
		std::int8_t jjPLAYER::get_health() const {
			noop();
			return (std::int8_t)_player->_health;
		}
		std::int8_t jjPLAYER::set_health(std::int8_t value) {
			noop();
			_player->SetHealth(value);
			return value;
		}

		std::int32_t jjPLAYER::get_fastfire() const {
			noop();
			return (_player->_weaponUpgrades[(std::int32_t)WeaponType::Blaster] >> 1);
		}
		std::int32_t jjPLAYER::set_fastfire(std::int32_t value) {
			noop(); 
			_player->_weaponUpgrades[(std::int32_t)WeaponType::Blaster] = (std::uint8_t)((_player->_weaponUpgrades[(std::int32_t)WeaponType::Blaster] & 0x1) | (value << 1));
			return value;
		}

		std::int8_t jjPLAYER::get_currWeapon() const {
			noop();
			return (std::int8_t)_player->_currentWeapon;
		}
		std::int8_t jjPLAYER::set_currWeapon(std::int8_t value) {
			noop();
			if (value < 0 || value >= (int8_t)WeaponType::Count) {
				return (std::int8_t)_player->_currentWeapon;
			}
			_player->_currentWeapon = (WeaponType)value;
			return value;
		}

		/*std::int32_t jjPLAYER::get_lives() const {
			noop();
			return _player->_lives;
		}
		std::int32_t jjPLAYER::set_lives(std::int32_t value) {
			noop();
			_player->_lives = value;
			return _player->_lives;
		}*/

		std::int32_t jjPLAYER::get_invincibility() const {
			noop(); return 0;
		}
		std::int32_t jjPLAYER::set_invincibility(std::int32_t value) {
			noop(); return 0;
		}

		std::int32_t jjPLAYER::get_blink() const {
			noop(); return 0;
		}
		std::int32_t jjPLAYER::set_blink(std::int32_t value) {
			noop(); return 0;
		}

		std::int32_t jjPLAYER::extendInvincibility(std::int32_t duration) {
			noop();
			return 0;
		}

		std::int32_t jjPLAYER::get_food() const {
			noop(); return _player->_foodEaten;
		}
		std::int32_t jjPLAYER::set_food(std::int32_t value) {
			noop();
			_player->_foodEaten = value;
			return _player->_foodEaten;
		}
		std::int32_t jjPLAYER::get_coins() const {
			noop();
			return _player->_coins;
		}
		std::int32_t jjPLAYER::set_coins(std::int32_t value) {
			noop();
			_player->_coins = value;
			return _player->_coins;
		}

		bool jjPLAYER::testForCoins(std::int32_t numberOfCoins) {
			noop();
			if (numberOfCoins > _player->_coins) {
				return false;
			}
			_player->AddCoins(-numberOfCoins);
			return true;
		}
		std::int32_t jjPLAYER::get_gems(std::uint32_t type) const {
			noop();
			return (type < arraySize(_player->_gems) ? _player->_gems[type] : 0);
		}
		std::int32_t jjPLAYER::set_gems(std::uint32_t type, std::int32_t value) {
			noop();
			if (type < arraySize(_player->_gems)) {
				_player->_gems[type] = value;
			}
			return (type < arraySize(_player->_gems) ? _player->_gems[type] : 0);
		}
		bool jjPLAYER::testForGems(std::int32_t numberOfGems, std::uint32_t type) {
			noop();
			std::uint8_t currentCount = (type < arraySize(_player->_gems) ? _player->_gems[type] : 0);
			if (numberOfGems <= currentCount) {
				if (numberOfGems > 0) {
					_player->_gems[type] -= numberOfGems;
				}
				return true;
			}
			// TODO: Show warning message
			return false;
		}

		std::int32_t jjPLAYER::get_shieldType() const {
			noop();
			return (std::int32_t)_player->_activeShield;
		}
		std::int32_t jjPLAYER::set_shieldType(std::int32_t value) {
			noop();
			if (value >= 0 && value < (std::int32_t)ShieldType::Count) {
				_player->_activeShield = (ShieldType)value;
			}
			return value;
		}
		std::int32_t jjPLAYER::get_shieldTime() const {
			noop();
			return (std::int32_t)(_player->_activeShieldTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_shieldTime(std::int32_t value) {
			noop();
			_player->_activeShieldTime = value * FrameTimer::FramesPerSecond / 70.0f;
			return value;
		}

		std::int32_t jjPLAYER::get_rolling() const {
			noop();
			return (std::int32_t)(_player->_inTubeTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_rolling(std::int32_t value) {
			noop(); 
		
			if (value > 0) {
				if (_player->_inTubeTime <= 0) {
					_player->EndDamagingMove();
					_player->SetAnimation(AnimState::Dash | AnimState::Jump);
					_player->_controllable = false;
					_player->SetState(Actors::ActorState::CanJump | Actors::ActorState::ApplyGravitation, false);
				}
				_player->_inTubeTime = value * FrameTimer::FramesPerSecond / 70.0f;
			} else {
				if (_player->_inTubeTime > 0) {
					_player->_controllable = true;
					_player->SetState(Actors::ActorState::ApplyGravitation | Actors::ActorState::CollideWithTileset, true);
					_player->_inTubeTime = 0.0f;
				}
			}
		
			return value;
		}

		std::int8_t jjPLAYER::get_playerID() const {
			noop(); return (std::int8_t)_player->_playerIndex;
		}
		std::int32_t jjPLAYER::get_localPlayerID() const {
			noop(); return _player->_playerIndex;
		}

		bool jjPLAYER::get_running() const {
			noop(); return _player->_isRunPressed;
		}
		bool jjPLAYER::set_running(bool value) {
			noop();
			_player->_isRunPressed = value;
			return _player->_isRunPressed;
		}

		std::int32_t jjPLAYER::get_stoned() {
			noop();
			return (std::int32_t)(_player->_dizzyTime * 70.0f / FrameTimer::FramesPerSecond);
		}
		std::int32_t jjPLAYER::set_stoned(std::int32_t value) {
			noop();
			_player->SetDizzy(value * FrameTimer::FramesPerSecond / 70.0f);
			return value;
		}

		void jjPLAYER::suckerTube(std::int32_t xSpeed, std::int32_t ySpeed, bool center, bool noclip, bool trigSample) {
			noop();
		}
		void jjPLAYER::poleSpin(float xSpeed, float ySpeed, std::uint32_t delay) {
			noop();
		}
		void jjPLAYER::spring(float xSpeed, float ySpeed, bool keepZeroSpeeds, bool sample) {
			noop();
		}

		bool jjPLAYER::get_isConnecting() const {
			noop();
			// TODO Whether the player is a client who has not finished joining the current online server yet.
			return false;
		}
		bool jjPLAYER::get_isIdle() const {
			noop();
			// TODO: Whether the player is idle and does not appear in the level or player list. Currently this can only ever be true of the server.
			return false;
		}
		bool jjPLAYER::get_isOut() const {
			noop();
			// TODO: Equals true if the player has lost all their lives (or joined too late) in an LRS-based gamemode.
			return false;
		}
		bool jjPLAYER::get_isSpectating() const {
			noop();
			// TODO: Equals true if the player is spectating normally, i.e. not forced into spectating by being out or an idle server.
			return false;
		}
		bool jjPLAYER::get_isInGame() const {
			noop();
			// TODO: Equals true if isActive is true but isConnecting, isIdle, isOut, and isSpectating are all false.
			return true;
		}

		String jjPLAYER::get_name() const {
			noop();
			return {};
		}
		String jjPLAYER::get_nameUnformatted() const {
			noop();
			return {};
		}
		bool jjPLAYER::setName(const String& name) {
			noop();
			return false;
		}
		std::int8_t jjPLAYER::get_light() const {
			noop();
			return 0;
		}
		std::int8_t jjPLAYER::set_light(std::int8_t value) {
			noop();
			return 0;
		}
		std::uint32_t jjPLAYER::get_fur() const {
			noop();
			return 0;
		}
		std::uint32_t jjPLAYER::set_fur(std::uint32_t value) {
			noop();
			return 0;
		}
		void jjPLAYER::getFur(std::uint8_t& a, std::uint8_t& b, std::uint8_t& c, std::uint8_t& d) const {
			noop();
		}
		void jjPLAYER::setFur(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
			noop();
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
		std::uint8_t jjPLAYER::get_lighting() const {
			noop();
			// TODO: Viewports
			return (std::uint8_t)(_levelScriptLoader->_levelHandler->GetAmbientLight(_player) * 64.0f);
		}
		std::uint8_t jjPLAYER::set_lighting(std::uint8_t value) {
			noop();
			_levelScriptLoader->_levelHandler->SetAmbientLight(_player, value / 64.0f);
			return value;
		}
		std::uint8_t jjPLAYER::resetLight() {
			noop();
			return 0;
		}

		bool jjPLAYER::get_playerKeyLeftPressed() {
			noop();
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Left);
		}
		bool jjPLAYER::get_playerKeyRightPressed() {
			noop();
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Right);
		}
		bool jjPLAYER::get_playerKeyUpPressed() {
			noop();
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Up);
		}
		bool jjPLAYER::get_playerKeyDownPressed() {
			noop();
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Down);
		}
		bool jjPLAYER::get_playerKeyFirePressed() {
			noop();
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Fire);
		}
		bool jjPLAYER::get_playerKeySelectPressed() {
			noop();
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::ChangeWeapon);
		}
		bool jjPLAYER::get_playerKeyJumpPressed() {
			noop();
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Jump);
		}
		bool jjPLAYER::get_playerKeyRunPressed() {
			noop();
			return _player->_levelHandler->PlayerActionPressed(_player, PlayerAction::Run);
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

		bool jjPLAYER::get_powerup(std::uint8_t index) {
			noop();
			if (index < 0 || index >= (std::int8_t)WeaponType::Count) {
				return 0;
			}
			return (_player->_weaponUpgrades[index] & 0x01) == 0x01;
		}
		bool jjPLAYER::set_powerup(std::uint8_t index, bool value) {
			noop();
			if (index < 0 || index >= (std::int8_t)WeaponType::Count) {
				return 0;
			}
			_player->_weaponUpgrades[index] = (value ? 0x01 : 0x00);
			return value;
		}
		std::int32_t jjPLAYER::get_ammo(std::uint8_t index) const {
			noop();
			if (index < 0 || index >= (std::int8_t)WeaponType::Count) {
				return 0;
			}
			return _player->_weaponAmmo[index];
		}
		std::int32_t jjPLAYER::set_ammo(std::uint8_t index, std::int32_t value) {
			noop();
			if (index < 0 || index >= (std::int8_t)WeaponType::Count) {
				return 0;
			}
			_player->_weaponAmmo[index] = value * 256;
			return value;
		}

		bool jjPLAYER::offsetPosition(std::int32_t xPixels, std::int32_t yPixels) {
			noop();

			Vector2f pos = _player->GetPos();
			_player->WarpToPosition(Vector2f(pos.X + xPixels, pos.Y + yPixels), WarpFlags::Fast);
			return true;
		}
		bool jjPLAYER::warpToTile(std::int32_t xTile, std::int32_t yTile, bool fast) {
			noop();

			_player->WarpToPosition(Vector2f(xTile * TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2,
				yTile * TileSet::DefaultTileSize + Tiles::TileSet::DefaultTileSize / 2),
				fast ? WarpFlags::Fast : WarpFlags::Default);
			return true;
		}
		bool jjPLAYER::warpToID(std::uint8_t warpID, bool fast) {
			noop();

			auto events = _levelScriptLoader->_levelHandler->EventMap();
			Vector2f c = events->GetWarpTarget(warpID);
			if (c.X >= 0.0f && c.Y >= 0.0f) {
				_player->WarpToPosition(c, fast ? WarpFlags::Fast : WarpFlags::Default);
				return true;
			}
			return false;
		}

		std::uint32_t jjPLAYER::morph(bool rabbitsOnly, bool morphEffect) {
			noop();
			return 0;
		}
		std::uint32_t jjPLAYER::morphTo(std::uint32_t charNew, bool morphEffect) {
			noop();
			// TODO: morphEffect
			_player->MorphTo((PlayerType)charNew);
			return (std::uint32_t)_player->_playerType;
		}
		std::uint32_t jjPLAYER::revertMorph(bool morphEffect) {
			noop();
			// TODO: morphEffect
			_player->MorphRevert();
			return (std::uint32_t)_player->_playerType;
		}
		std::uint32_t jjPLAYER::get_charCurr() const {
			noop();
			return (std::uint32_t)_player->_playerType;
		}

		void jjPLAYER::kill() {
			noop();
			_player->DecreaseHealth(INT32_MAX);
		}
		bool jjPLAYER::hurt(std::int8_t damage, bool forceHurt, jjPLAYER* attacker) {
			noop();

			// TODO: forceHurt and return value
			_player->TakeDamage(damage);
			return false;
		}

		std::uint32_t jjPLAYER::get_timerState() const {
			noop();
			return _timerState;
		}
		bool jjPLAYER::get_timerPersists() const {
			noop();
			return _timerPersists;
		}
		bool jjPLAYER::set_timerPersists(bool value) {
			noop();
			_timerPersists = value;
			return value;
		}
		std::uint32_t jjPLAYER::timerStart(std::int32_t ticks, bool startPaused) {
			noop();
			_timerLeft = (float)ticks * FrameTimer::FramesPerSecond / 70;
			_timerState = (startPaused ? 2 : 1);
			return _timerState;
		}
		std::uint32_t jjPLAYER::timerPause() {
			noop();
			_timerState = 2; // PAUSED
			return _timerState;
		}
		std::uint32_t jjPLAYER::timerResume() {
			noop();
			_timerState = 1; // STARTED
			return _timerState;
		}
		std::uint32_t jjPLAYER::timerStop() {
			noop();
			_timerLeft = 0.0f;
			_timerState = 0; // STOPPED
			return _timerState;
		}
		std::int32_t jjPLAYER::get_timerTime() const {
			noop();
			std::int32_t jjTicksLeft = (std::int32_t)(_timerLeft * 70 / FrameTimer::FramesPerSecond);
			return jjTicksLeft;
		}
		std::int32_t jjPLAYER::set_timerTime(std::int32_t value) {
			noop();
			_timerLeft = (float)value * FrameTimer::FramesPerSecond / 70;
			return value;
		}
		void jjPLAYER::timerFunction(const String& functionName) {
			noop();
			asIScriptFunction* func = _levelScriptLoader->GetMainModule()->GetFunctionByName(String::nullTerminatedView(functionName).data());
			_timerCallback = func;
		}
		void jjPLAYER::timerFunctionPtr(void* function) {
			noop();
			_timerCallback = function;
		}
		void jjPLAYER::timerFunctionFuncPtr(void* function) {
			noop();
			_timerCallback = function;
		}

		bool jjPLAYER::activateBoss(bool activate) {
			noop();

			// TODO: activate
			_levelScriptLoader->_levelHandler->BroadcastTriggeredEvent(_player, EventType::AreaActivateBoss, nullptr);
			return true;
		}
		bool jjPLAYER::limitXScroll(std::uint16_t left, std::uint16_t width) {
			noop();

			_levelScriptLoader->_levelHandler->LimitCameraView(_player, left * Tiles::TileSet::DefaultTileSize, width * Tiles::TileSet::DefaultTileSize);
			return true;
		}
		void jjPLAYER::cameraFreezeFF(float xPixel, float yPixel, bool centered, bool instant) {
			noop();
			// TODO: instant
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, xPixel, yPixel, !centered);
		}
		void jjPLAYER::cameraFreezeBF(bool xUnfreeze, float yPixel, bool centered, bool instant) {
			noop();
			// TODO: instant
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, INFINITY, yPixel, !centered);
		}
		void jjPLAYER::cameraFreezeFB(float xPixel, bool yUnfreeze, bool centered, bool instant) {
			noop();
			// TODO: instant
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, xPixel, INFINITY, !centered);
		}
		void jjPLAYER::cameraFreezeBB(bool xUnfreeze, bool yUnfreeze, bool centered, bool instant) {
			noop();
			// TODO: instant, centered
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, INFINITY, INFINITY, false);
		}
		void jjPLAYER::cameraUnfreeze(bool instant) {
			noop();
			// TODO: instant
			_levelScriptLoader->_levelHandler->OverrideCameraView(_player, INFINITY, INFINITY, false);
		}
		void jjPLAYER::showText(const String& text, std::uint32_t size) {
			noop();

			// TODO: size
			// Input string must be recoded in Legacy context
			auto recodedText = Compatibility::JJ2Strings::RecodeString(text);
			_levelScriptLoader->_levelHandler->ShowLevelText(recodedText);
		}
		void jjPLAYER::showTextByID(std::uint32_t textID, std::uint32_t offset, std::uint32_t size) {
			noop();

			// TODO: size
			auto text = _levelScriptLoader->_levelHandler->GetLevelText(textID, offset, '|');
			_levelScriptLoader->_levelHandler->ShowLevelText(text);
		}

		std::uint32_t jjPLAYER::get_fly() const {
			noop();
			return 0;
		}
		std::uint32_t jjPLAYER::set_fly(std::uint32_t value) {
			noop();
			return 0;
		}

		std::int32_t jjPLAYER::fireBulletDirection(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, std::uint32_t direction) {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::fireBulletAngle(std::uint8_t gun, bool depleteAmmo, bool requireAmmo, float angle) {
			noop();
			return 0;
		}

		float jjPLAYER::get_cameraX() const {
			noop();
			return _levelScriptLoader->_levelHandler->GetCameraPos(_player).X;
		}
		float jjPLAYER::get_cameraY() const {
			noop();
			return _levelScriptLoader->_levelHandler->GetCameraPos(_player).Y;
		}
		std::int32_t jjPLAYER::get_deaths() const {
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
		std::int32_t jjPLAYER::get_lrsLives() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_roasts() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_laps() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_lapTimeCurrent() const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_lapTimes(std::uint32_t index) const {
			noop();
			return 0;
		}
		std::int32_t jjPLAYER::get_lapTimeBest() const {
			noop();
			return 0;
		}
		bool jjPLAYER::get_isAdmin() const {
			noop();
			return false;
		}
		bool jjPLAYER::hasPrivilege(const String& privilege, std::uint32_t moduleID) const {
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
		bool jjPLAYER::objectHit(jjOBJ* target, std::int32_t force, std::uint32_t playerHandling) {
			noop();
			return false;
		}
		bool jjPLAYER::isEnemy(const jjPLAYER* victim) const {
			noop();
			return false;
		}

		void jjPLAYER::SyncPropertiesToBackingStore() {
			// TODO
			_backingStoreDirty = true;

			lives = _player->_lives;
		}
		void jjPLAYER::SyncPropertiesFromBackingStore() {
			// TODO
			if (!_backingStoreDirty) {
				return;
			}

			if (lives != _player->_lives) {
				LOGW("SYNC");
			}

			lives = _player->_lives;
		}

		jjPIXELMAP::jjPIXELMAP() : _refCount(1) {
			noop();
		}
		jjPIXELMAP::~jjPIXELMAP() {
			noop();
		}

		jjPIXELMAP* jjPIXELMAP::CreateFromTile() {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromSize(std::uint32_t width, std::uint32_t height) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromFrame(const jjANIMFRAME* animFrame) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromLayer(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, std::uint32_t layer) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromLayerObject(std::uint32_t left, std::uint32_t top, std::uint32_t width, std::uint32_t height, const jjLAYER* layer) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromTexture(std::uint32_t animFrame) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjPIXELMAP));
			return new(mem) jjPIXELMAP();
		}
		jjPIXELMAP* jjPIXELMAP::CreateFromFilename(const String& filename, const jjPAL* palette, std::uint8_t threshold) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

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

		// TODO: return type std::uint8_t& instead?
		std::uint8_t jjPIXELMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			noop();
			return 0;
		}

		bool jjPIXELMAP::saveToTile(std::uint16_t tileID, bool hFlip) const {
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

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjMASKMAP));
			return new(mem) jjMASKMAP();
		}
		jjMASKMAP* jjMASKMAP::CreateFromTile(std::uint16_t tileID) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

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
		bool jjMASKMAP::GetPixel(std::uint32_t x, std::uint32_t y) {
			noop();
			return false;
		}

		bool jjMASKMAP::save(std::uint16_t tileID, bool hFlip) const {
			noop();
			return false;
		}

		jjLAYER::jjLAYER() : _refCount(1) {
			noop();
		}
		jjLAYER::~jjLAYER() {
			noop();
		}

		jjLAYER* jjLAYER::CreateFromSize(std::uint32_t width, std::uint32_t height) {
			noop();

			void* mem = asAllocMem(sizeof(jjLAYER));
			return new(mem) jjLAYER();
		}
		jjLAYER* jjLAYER::CreateCopy(jjLAYER* other) {
			noop();

			void* mem = asAllocMem(sizeof(jjLAYER));
			return new(mem) jjLAYER();
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

		jjLAYER* jjLAYER::get_jjLayers(std::int32_t index) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

			void* mem = asAllocMem(sizeof(jjLAYER));
			return new(mem) jjLAYER();
		}

		std::uint32_t jjLAYER::get_spriteMode() const {
			noop();
			return 0;
		}
		std::uint32_t jjLAYER::set_spriteMode(std::uint32_t value) const {
			noop();
			return 0;
		}
		std::uint8_t jjLAYER::get_spriteParam() const {
			noop();
			return 0;
		}
		std::uint8_t jjLAYER::set_spriteParam(std::uint8_t value) const {
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

		std::int32_t jjLAYER::GetTextureMode() const {
			noop();
			return 0;
		}
		void jjLAYER::SetTextureMode(std::int32_t value) const {
			noop();
		}
		std::int32_t jjLAYER::GetTexture() const {
			noop();
			return 0;
		}
		void jjLAYER::SetTexture(std::int32_t value) const {
			noop();
		}

		CScriptArray* jjLAYER::jjLayerOrderGet() {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return CScriptArray::Create(owner->GetEngine()->GetTypeInfoByDecl("array<jjLAYER@>"), 16);
		}
		bool jjLAYER::jjLayerOrderSet(const CScriptArray& order) {
			noop();
			return false;
		}
		CScriptArray* jjLAYER::jjLayersFromLevel(const String& filename, const CScriptArray& layerIDs, std::int32_t tileIDAdjustmentFactor) {
			noop();

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return CScriptArray::Create(owner->GetEngine()->GetTypeInfoByDecl("array<jjLAYER@>"), 16);
		}
		bool jjLAYER::jjTilesFromTileset(const String& filename, std::uint32_t firstTileID, std::uint32_t tileCount, const CScriptArray* paletteColorMapping) {
			noop();
			return false;
		}

		std::uint16_t jjLAYER::tileGet(int xTile, int yTile) {
			noop();
			return 0;
		}
		std::uint16_t jjLAYER::tileSet(int xTile, int yTile, std::uint16_t newTile) {
			noop();
			return 0;
		}
		void jjLAYER::generateSettableTileArea(int xTile, int yTile, int width, int height) {
			noop();
		}
		void jjLAYER::generateSettableTileAreaAll() {
			noop();
		}

		bool jjPLAYERDRAW::get_shield(std::int32_t shield) const {
			noop();
			return false;
		}
		bool jjPLAYERDRAW::set_shield(std::int32_t shield, bool enable) {
			noop();
			return false;
		}
		jjPLAYER* jjPLAYERDRAW::get_player() const {
			noop();
			return nullptr;
		}
	}

	jjOBJ* get_jjObjects(std::int32_t index)
	{
		noop();
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		void* mem = asAllocMem(sizeof(jjOBJ));
		return new(mem) jjOBJ();
	}

	jjOBJ* get_jjObjectPresets(std::int8_t id)
	{
		noop();
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		void* mem = asAllocMem(sizeof(jjOBJ));
		return new(mem) jjOBJ();
	}

	void jjAddParticleTileExplosion(std::uint16_t xTile, std::uint16_t yTile, std::uint16_t tile, bool collapseSceneryStyle) {
		noop();
	}
	void jjAddParticlePixelExplosion(float xPixel, float yPixel, int curFrame, int direction, int mode) {
		noop();
	}

	jjPARTICLE* GetParticle(std::int32_t index) {
		noop();
		// TODO
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		void* mem = asAllocMem(sizeof(jjPARTICLE));
		return new(mem) jjPARTICLE();
	}

	jjPARTICLE* AddParticle(std::int32_t particleType) {
		noop();
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();

		void* mem = asAllocMem(sizeof(jjPARTICLE));
		return new(mem) jjPARTICLE();
	}

	std::int32_t get_jjPlayerCount() {
		// TODO
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayers().size();
	}
	std::int32_t get_jjLocalPlayerCount() {
		// TODO
		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayers().size();
	}

	jjPLAYER* get_jjP() {
		noop();

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayerBackingStore(0);
	}
	jjPLAYER* get_jjPlayers(std::uint8_t index) {
		noop();

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayerBackingStore(index);
	}
	jjPLAYER* get_jjLocalPlayers(std::uint8_t index) {
		noop();

		auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return owner->GetPlayerBackingStore(index);
	}

	bool mlleSetup() {
		noop();
		return true;
	}

	void mlleReapplyPalette() {
		noop();
	}

	void mlleSpawnOffgrids() {
		noop();
	}

	void mlleSpawnOffgridsLocal() {
		noop();
	}

	float get_sinTable(std::uint32_t angle) {
		noop();
		return sinf(angle * fTwoPi / 1024.0f);
	};
	float get_cosTable(std::uint32_t angle) {
		noop();
		return cosf(angle * fTwoPi / 1024.0f);
	};
	std::uint32_t RandWord32() {
		noop();
		return Random().Next();
	}
	std::uint64_t unixTimeSec() {
		noop();
		return Containers::DateTime::Now().ToUnixMilliseconds();
	}
	std::uint64_t unixTimeMs() {
		noop();
		return Containers::DateTime::Now().ToUnixMilliseconds() / 1000;
	}

	bool jjRegexIsValid(const String& expression) {
		noop();

		try {
			std::regex r(expression.begin(), expression.end());
			return true;
		} catch (std::regex_error& e) {
			return false;
		}
	}
	bool jjRegexMatch(const String& text, const String& expression, bool ignoreCase) {
		noop();
		
		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::regex r(expression.begin(), expression.end(), flags);
			return std::regex_match(text.begin(), text.end(), r);
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: %s", e.what());
			return false;
		}
	}
	bool jjRegexMatchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase) {
		noop();

		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::match_results<const char*> m;
			std::regex r(expression.begin(), expression.end(), flags);
			bool success = std::regex_match(text.begin(), text.end(), m, r);
			// TODO: Results
			return success;
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: %s", e.what());
			return false;
		}
	}
	bool jjRegexSearch(const String& text, const String& expression, bool ignoreCase) {
		noop();

		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::regex r(expression.begin(), expression.end(), flags);
			return std::regex_search(text.begin(), text.end(), r);
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: %s", e.what());
			return false;
		}
	}
	bool jjRegexSearchWithResults(const String& text, const String& expression, CScriptArray& results, bool ignoreCase) {
		noop();

		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::match_results<const char*> m;
			std::regex r(expression.begin(), expression.end(), flags);
			bool success = std::regex_search(text.begin(), text.end(), m, r);
			// TODO: Results
			return success;
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: %s", e.what());
			return false;
		}
	}
	String jjRegexReplace(const String& text, const String& expression, const String& replacement, bool ignoreCase) {
		noop();

		try {
			auto flags = std::regex_constants::ECMAScript;
			if (ignoreCase) {
				flags |= std::regex_constants::icase;
			}
			std::regex r(expression.begin(), expression.end(), flags);
			std::string textStr = text;
			std::string replacementStr = replacement;
			return std::regex_replace(textStr, r, replacementStr);
		} catch (std::regex_error& e) {
			LOGE("Failed to process regular expression: %s", e.what());
			return text;
		}
	}

	std::int32_t GetFPS() {
		noop();
		return (std::int32_t)theApplication().GetFrameTimer().GetAverageFps();
	}

	bool isAdmin() {
		noop(); return false;
	}

	std::int32_t LevelScriptLoader::GetDifficulty() {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		switch (_this->_levelHandler->_difficulty) {
			case GameDifficulty::Easy: return 0;
			default:
			case GameDifficulty::Normal: return 1;
			case GameDifficulty::Hard: return 2;
		}
	}
	std::int32_t LevelScriptLoader::SetDifficulty(std::int32_t value) {
		noop();

		if (value >= 0 && value <= 2) {
			auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			_this->_levelHandler->_difficulty = (GameDifficulty)value;
		}
		return value;
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

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_musicCurrentPath;
	}
	String get_jjTilesetFileName() {
		noop(); return "";
	}

	String LevelScriptLoader::get_jjHelpStrings(std::uint32_t index) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->GetLevelText(index);
	}
	void LevelScriptLoader::set_jjHelpStrings(std::uint32_t index, const String& text) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->OverrideLevelText(index, text);
	}

	std::int32_t get_gameState() {
		noop(); return 0;
	}

	// TODO

	void LevelScriptLoader::jjAlert(const String& text, bool sendToAll, std::uint32_t size)
	{
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->ShowLevelText(text);
	}
	void LevelScriptLoader::jjPrint(const String& text, bool timestamp) {
		LOGW("%s", text.data());
	}
	void LevelScriptLoader::jjDebug(const String& text, bool timestamp) {
		LOGD("%s", text.data());
	}
	void LevelScriptLoader::jjChat(const String& text, bool teamchat) {
		LOGW("%s", text.data());

		if (text.hasPrefix('/')) {
			// TODO: Process command
			return;
		}

		// TODO: teamchat
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->_console->WriteLine(UI::MessageLevel::Info, text);
	}
	void LevelScriptLoader::jjConsole(const String& text, bool sendToAll) {
		LOGW("%s", text.data());

		// TODO: sendToAll
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->_console->WriteLine(UI::MessageLevel::Important, text);
	}
	void LevelScriptLoader::jjSpy(const String& text) {
		LOGD("%s", text.data());
	}

	// TODO

	std::int32_t getBorderWidth() {
		noop(); return 0;
	}
	std::int32_t getBorderHeight() {
		noop(); return 0;
	}
	bool getSplitscreenType() {
		noop(); return false;
	}
	bool setSplitscreenType() {
		noop(); return false;
	}

	// TODO

	std::int32_t get_teamScore(std::int32_t color) {
		noop(); return 0;
	}
	std::int32_t GetMaxHealth() {
		noop(); return 0;
	}
	std::int32_t GetStartHealth() {
		noop(); return 0;
	}

	// TODO

	float LevelScriptLoader::get_layerXOffset(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.OffsetX : 0);
	}
	float LevelScriptLoader::set_layerXOffset(std::uint8_t id, float value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.OffsetX = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	float LevelScriptLoader::get_layerYOffset(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.OffsetY : 0);
	}
	float LevelScriptLoader::set_layerYOffset(std::uint8_t id, float value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.OffsetY = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	std::int32_t LevelScriptLoader::get_layerWidth(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].LayoutSize.X : 0);
	}
	std::int32_t LevelScriptLoader::get_layerRealWidth(std::uint8_t id) {
		noop();
		return get_layerWidth(id);
	}
	std::int32_t LevelScriptLoader::get_layerRoundedWidth(std::uint8_t id) {
		noop();
		return (get_layerWidth(id) + 3) / 4;
	}
	std::int32_t LevelScriptLoader::get_layerHeight(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].LayoutSize.Y : 0);
	}
	float LevelScriptLoader::get_layerXSpeed(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.SpeedX : 0.0f);
	}
	float LevelScriptLoader::set_layerXSpeed(std::uint8_t id, float value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.SpeedX = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	float LevelScriptLoader::get_layerYSpeed(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.SpeedY : 0.0f);
	}
	float LevelScriptLoader::set_layerYSpeed(std::uint8_t id, float value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.SpeedY = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	float LevelScriptLoader::get_layerXAutoSpeed(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.AutoSpeedX : 0.0f);
	}
	float LevelScriptLoader::set_layerXAutoSpeed(std::uint8_t id, float value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.AutoSpeedX = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	float LevelScriptLoader::get_layerYAutoSpeed(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() ? tileMap->_layers[id].Description.AutoSpeedY : 0.0f);
	}
	float LevelScriptLoader::set_layerYAutoSpeed(std::uint8_t id, float value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.AutoSpeedY = value;
			return value;
		} else {
			return 0.0f;
		}
	}
	bool LevelScriptLoader::get_layerHasTiles(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() && tileMap->_layers[id].Visible);
	}
	bool LevelScriptLoader::set_layerHasTiles(std::uint8_t id, bool value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Visible = value;
			return value;
		} else {
			return false;
		}
	}
	bool LevelScriptLoader::get_layerTileHeight(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() && tileMap->_layers[id].Description.RepeatY);
	}
	bool LevelScriptLoader::set_layerTileHeight(std::uint8_t id, bool value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.RepeatY = value;
			return value;
		} else {
			return false;
		}
	}
	bool LevelScriptLoader::get_layerTileWidth(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() && tileMap->_layers[id].Description.RepeatX);
	}
	bool LevelScriptLoader::set_layerTileWidth(std::uint8_t id, bool value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.RepeatX = value;
			return value;
		} else {
			return false;
		}
	}
	bool LevelScriptLoader::get_layerLimitVisibleRegion(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		return (id < tileMap->_layers.size() && tileMap->_layers[id].Description.UseInherentOffset);
	}
	bool LevelScriptLoader::set_layerLimitVisibleRegion(std::uint8_t id, bool value) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			tileMap->_layers[id].Description.UseInherentOffset = value;
			return value;
		} else {
			return false;
		}
	}

	void LevelScriptLoader::setLayerXSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) {
		noop();

		// TODO: Shortcut global functions for setXSpeed and setYSpeed on the same-indexed jjLayers objects.
		// TODO: Changes the X or Y speed.Unlike the basic properties like xSpeed and yAutoSpeed,
		// these functions will ensure that the layer remains in the same position it was before its speeds were changed.

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			if (newSpeedIsAnAutoSpeed) {
				tileMap->_layers[id].Description.AutoSpeedX = newspeed;
			} else {
				tileMap->_layers[id].Description.SpeedX = newspeed;
			}
		}
	}
	void LevelScriptLoader::setLayerYSpeedSeamlessly(std::uint8_t id, float newspeed, bool newSpeedIsAnAutoSpeed) {
		noop();

		// TODO: Shortcut global functions for setXSpeed and setYSpeed on the same-indexed jjLayers objects.
		// TODO: Changes the X or Y speed.Unlike the basic properties like xSpeed and yAutoSpeed,
		// these functions will ensure that the layer remains in the same position it was before its speeds were changed.

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto tileMap = _this->_levelHandler->_tileMap.get();
		if (id < tileMap->_layers.size()) {
			if (newSpeedIsAnAutoSpeed) {
				tileMap->_layers[id].Description.AutoSpeedY = newspeed;
			} else {
				tileMap->_layers[id].Description.SpeedY = newspeed;
			}
		}
	}

	// TODO

	void LevelScriptLoader::jjDrawPixel(float xPixel, float yPixel, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawRectangle(float xPixel, float yPixel, std::int32_t width, std::int32_t height, std::uint8_t color, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int8_t direction, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawResizedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawResizedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawRotatedSprite(float xPixel, float yPixel, std::int32_t setID, std::uint8_t animation, std::uint8_t frame, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawRotatedSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t angle, float xScale, float yScale, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawSwingingVineSpriteFromCurFrame(float xPixel, float yPixel, std::uint32_t sprite, std::int32_t length, std::int32_t curvature, spriteType mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawTile(float xPixel, float yPixel, std::uint16_t tile, std::uint32_t tileQuadrant, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawString(float xPixel, float yPixel, const String& text, std::uint32_t size, std::uint32_t mode, std::uint8_t param, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	void LevelScriptLoader::jjDrawStringEx(float xPixel, float yPixel, const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& appearance, std::uint8_t param1, spriteType spriteMode, std::uint8_t param2, std::int8_t layerZ, std::uint8_t layerXY, std::int8_t playerID) {
		noop();
	}

	std::int32_t LevelScriptLoader::jjGetStringWidth(const String& text, std::uint32_t size, const jjTEXTAPPEARANCE& style) {
		//noop();
		
		float scale;
		switch (size) {
			default:
			case 0:	// MEDIUM
				scale = 0.8f;
			case 1:	// SMALL
				scale = 0.6f;
			case 2:	// LARGE
				scale = 1.1f;
		}

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		auto measuredSize = _this->_levelHandler->_hud->_smallFont->MeasureString(text, scale);
		return (std::int32_t)measuredSize.X;
	}

	void jjSetDarknessColor(jjPALCOLOR color) {
		noop();
	}
	void jjSetFadeColors(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
		noop();
	}
	void jjSetFadeColorsFromPalette(std::uint8_t paletteColorID) {
		noop();
	}
	void jjSetFadeColorsFromPalcolor(jjPALCOLOR color) {
		noop();
	}
	jjPALCOLOR jjGetFadeColors() {
		noop(); return {};
	}
	void jjUpdateTexturedBG() {
		noop();
	}
	std::int32_t get_jjTexturedBGTexture(jjPALCOLOR color) {
		noop(); return 0;
	}
	std::int32_t set_jjTexturedBGTexture(std::int32_t texture) {
		noop(); return 0;
	}
	std::int32_t get_jjTexturedBGStyle() {
		noop(); return 0;
	}
	std::int32_t set_jjTexturedBGStyle(std::int32_t style) {
		noop(); return 0;
	}
	bool get_jjTexturedBGUsed(jjPALCOLOR color) {
		noop(); return false;
	}
	bool set_jjTexturedBGUsed(bool used) {
		noop(); return false;
	}
	bool get_jjTexturedBGStars(bool used) {
		noop(); return false;
	}
	bool set_jjTexturedBGStars(bool used) {
		noop(); return false;
	}
	float get_jjTexturedBGFadePositionX() {
		noop(); return 0.0f;
	}
	float set_jjTexturedBGFadePositionX(float value) {
		noop(); return 0.0f;
	}
	float get_jjTexturedBGFadePositionY() {
		noop(); return 0.0f;
	}
	float set_jjTexturedBGFadePositionY(float value) {
		noop(); return 0.0f;
	}

	bool LevelScriptLoader::get_jjTriggers(std::uint8_t id) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->GetTrigger(id);
	}
	bool LevelScriptLoader::set_jjTriggers(std::uint8_t id, bool value) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->SetTrigger(id, value);
		return value;
	}
	bool LevelScriptLoader::jjSwitchTrigger(std::uint8_t id) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->SetTrigger(id, !_this->_levelHandler->GetTrigger(id));
		return _this->_levelHandler->GetTrigger(id);
	}

	bool LevelScriptLoader::isNumberedASFunctionEnabled(std::uint8_t id) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_enabledCallbacks[id];
	}
	bool LevelScriptLoader::setNumberedASFunctionEnabled(std::uint8_t id, bool value) {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_enabledCallbacks.set(id, value);
		return value;
	}
	void LevelScriptLoader::reenableAllNumberedASFunctions() {
		//noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_enabledCallbacks.setAll();
	}

	float LevelScriptLoader::getWaterLevel() {
		noop();

		// TODO
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_waterLevel;
	}
	float LevelScriptLoader::getWaterLevel2() {
		noop();

		// TODO
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		return _this->_levelHandler->_waterLevel;
	}
	float LevelScriptLoader::setWaterLevel(float value, bool instant) {
		noop();

		// TODO: instant
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->_waterLevel = value;
		return value;
	}
	float LevelScriptLoader::get_waterChangeSpeed() {
		noop(); return 0;
	}
	float LevelScriptLoader::set_waterChangeSpeed(float value) {
		noop(); return 0;
	}
	std::int32_t LevelScriptLoader::get_waterLayer() {
		noop(); return 0;
	}
	std::int32_t LevelScriptLoader::set_waterLayer(std::int32_t value) {
		noop(); return 0;
	}
	void LevelScriptLoader::setWaterGradient(std::uint8_t red1, std::uint8_t green1, std::uint8_t blue1, std::uint8_t red2, std::uint8_t green2, std::uint8_t blue2) {
		noop();
	}
	void LevelScriptLoader::setWaterGradientFromColors(jjPALCOLOR color1, jjPALCOLOR color2) {
		noop();
	}
	void LevelScriptLoader::setWaterGradientToTBG() {
		noop();
	}
	void LevelScriptLoader::resetWaterGradient() {
		noop();
	}

	void LevelScriptLoader::triggerRock(std::uint8_t id) {
		noop();

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		std::uint8_t eventParams[] = { id };
		_this->_levelHandler->BroadcastTriggeredEvent(nullptr, EventType::RollingRockTrigger, eventParams);
	}

	void LevelScriptLoader::cycleTo(const String& filename, bool warp, bool fast) {
		noop();
	}
	void LevelScriptLoader::jjNxt(bool warp, bool fast)
	{
		ExitType exitType = (warp ? ExitType::Warp : ExitType::Normal);
		if (fast) {
			exitType |= ExitType::FastTransition;
		}

		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->BeginLevelChange(nullptr, exitType, {});
	}

	bool getEnabledTeam(std::uint8_t team) {
		noop();
		return false;
	}

	bool getKeyDown(std::uint8_t key) {
		//noop();
		return false;
	}
	std::int32_t getCursorX() {
		//noop();
		return 0;
	}
	std::int32_t getCursorY() {
		//noop();
		return 0;
	}

	bool LevelScriptLoader::jjMusicLoad(const String& filename, bool forceReload, bool temporary) {
		noop();

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		_this->_levelHandler->BeginPlayMusic(filename, !temporary, forceReload);
#endif
		return false;
	}
	void LevelScriptLoader::jjMusicStop() {
		noop();

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->stop();
		}
#endif
	}
	void LevelScriptLoader::jjMusicPlay() {
		noop();

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->play();
		}
#endif
	}
	void LevelScriptLoader::jjMusicPause() {
		noop();

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr) {
			_this->_levelHandler->_music->stop();
		}
#endif
	}
	void LevelScriptLoader::jjMusicResume() {
		noop();

#if defined(WITH_AUDIO)
		auto _this = ScriptLoader::FromActiveContext<LevelScriptLoader>();
		if (_this->_levelHandler->_music != nullptr && _this->_levelHandler->_music->isPaused()) {
			_this->_levelHandler->_music->play();
		}
#endif
	}

	void playSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t volume, std::int32_t frequency) {
		noop();
	}
	int32_t playLoopedSample(float xPixel, float yPixel, std::int32_t sample, std::int32_t volume, std::int32_t frequency) {
		noop(); return 0;
	}
	void playPrioritySample(std::int32_t sample) {
		noop();
	}
	bool isSampleLoaded(std::int32_t sample) {
		noop(); return false;
	}
	bool loadSample(std::int32_t sample, const String& filename) {
		noop(); return false;
	}

	bool getUseLayer8Speeds() {
		noop(); return false;
	}
	bool setUseLayer8Speeds(bool value) {
		noop(); return false;
	}

	jjWEAPON* get_jjWEAPON(int index) {
		noop();
		static jjWEAPON dummy;
		return &dummy;
	}

	jjCHARACTER* get_jjCHARACTER(int index) {
		noop();
		static jjCHARACTER dummy;
		return &dummy;
	}

	std::int32_t GetEvent(std::uint16_t tx, std::uint16_t ty) {
		noop(); return 0;
	}
	std::int32_t GetEventParamWrapper(std::uint16_t tx, std::uint16_t ty, std::int32_t offset, std::int32_t length) {
		noop(); return 0;
	}
	void SetEventByte(std::uint16_t tx, std::uint16_t ty, std::uint8_t newEventId) {
		noop();
	}
	void SetEventParam(std::uint16_t tx, std::uint16_t ty, std::int8_t offset, std::int8_t length, std::int32_t newValue) {
		noop();
	}
	std::int8_t GetTileType(std::uint16_t tile) {
		noop(); return 0;
	}
	std::int8_t SetTileType(std::uint16_t tile, std::uint16_t value) {
		noop(); return 0;
	}

	// TODO

	std::uint16_t jjGetStaticTile(std::uint16_t tileID) {
		noop();
		return 0;
	}
	std::uint16_t jjTileGet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile) {
		noop();
		return 0;
	}
	std::uint16_t jjTileSet(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::uint16_t newTile) {
		noop();
		return 0;
	}
	void jjGenerateSettableTileArea(std::uint8_t layer, std::int32_t xTile, std::int32_t yTile, std::int32_t width, std::int32_t height) {
		noop();
	}

	// TODO

	bool jjMaskedPixel(std::int32_t xPixel, std::int32_t yPixel) {
		noop();
		return false;
	}
	bool jjMaskedPixelLayer(std::int32_t xPixel, std::int32_t yPixel, std::uint8_t layer) {
		noop();
		return false;
	}
	bool jjMaskedHLine(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel) {
		noop();
		return false;
	}
	bool jjMaskedHLineLayer(std::int32_t xPixel, std::int32_t lineLength, std::int32_t yPixel, std::uint8_t layer) {
		noop();
		return false;
	}
	bool jjMaskedVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength) {
		noop();
		return false;
	}
	bool jjMaskedVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer) {
		noop();
		return false;
	}
	bool jjMaskedTopVLine(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength) {
		noop();
		return false;
	}
	bool jjMaskedTopVLineLayer(std::int32_t xPixel, std::int32_t yPixel, std::int32_t lineLength, std::uint8_t layer) {
		noop();
		return false;
	}

	// TODO

	void jjSetModPosition(std::int32_t order, std::int32_t row, bool reset) {
		noop();
	}
	void jjSlideModChannelVolume(std::int32_t channel, float volume, std::int32_t milliseconds) {
		noop();
	}
	std::int32_t jjGetModOrder() {
		noop();
		return 0;
	}
	std::int32_t jjGetModRow() {
		noop();
		return 0;
	}
	std::int32_t jjGetModTempo() {
		noop();
		return 0;
	}
	void jjSetModTempo(std::uint8_t speed) {
		noop();
	}
	std::int32_t jjGetModSpeed() {
		noop();
		return 0;
	}
	void jjSetModSpeed(std::uint8_t speed) {
		noop();
	}

	std::uint32_t getCustomSetID(std::uint8_t index) {
		noop();
		return mCOUNT + index;
	}
}

#endif