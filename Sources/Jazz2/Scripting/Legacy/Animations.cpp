#if defined(WITH_ANGELSCRIPT)

#include "Animations.h"
#include "AnimationTables.h"
#include "Internal.h"
#include "PixelMaps.h"

#include "../../../nCine/Base/Algorithms.h"

#include <algorithm>
#include <cmath>

#include <Containers/StringConcatenable.h>

using namespace Jazz2::Tiles;

namespace Jazz2::Scripting
{
	namespace Legacy
	{
		// The entries of the tables

		jjANIMFRAME::jjANIMFRAME(std::uint32_t index)
			: _refCount(1), _index(index), _transparent(false), _watched(false), _source(-1), _owner(0), _version(0), _seenSpots{}
		{
		}

		jjANIMFRAME::~jjANIMFRAME()
		{
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

		jjANIMFRAME& jjANIMFRAME::operator=(const jjANIMFRAME& o)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			owner->GetAnimationTables().AssignFrame(*this, o);
			return *this;
		}

		jjANIMFRAME* jjANIMFRAME::get_jjAnimFrames(std::uint32_t index)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->GetAnimationTables().GetFrame(index);
		}

		bool jjANIMFRAME::get_transparent() const
		{
			return _transparent;
		}

		bool jjANIMFRAME::set_transparent(bool value)
		{
			if (_transparent != value) {
				_transparent = value;
				auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
				owner->GetAnimationTables().MarkChanged(*this);
			}
			return value;
		}

		bool jjANIMFRAME::get_is32Bit() const
		{
			return (_colors != nullptr);
		}

		bool jjANIMFRAME::doesCollide(std::int32_t xPos, std::int32_t yPos, std::int32_t direction, const jjANIMFRAME* frame2, std::int32_t xPos2, std::int32_t yPos2, std::int32_t direction2, bool always) const
		{
			if (frame2 == nullptr) {
				return false;
			}

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			bool result = owner->GetAnimationTables().DoFramesCollide(*this, xPos, yPos, direction, *frame2, xPos2, yPos2, direction2, always);
			// A handle passed to a native function is the function's to release
			const_cast<jjANIMFRAME*>(frame2)->Release();
			return result;
		}

		jjANIMATION::jjANIMATION(std::uint32_t index)
			: _refCount(1), _index(index), _firstFrame(0), _watched(false), _seenFrameCount(0), _native(-1), _graphicsSignature(0)
		{
		}

		jjANIMATION::~jjANIMATION()
		{
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

		jjANIMATION& jjANIMATION::operator=(const jjANIMATION& o)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			owner->GetAnimationTables().AssignAnimation(*this, o);
			return *this;
		}

		bool jjANIMATION::save(const String& filename, const jjPAL& palette) const
		{
			noop();
			return false;
		}

		bool jjANIMATION::load(const String& filename, std::int32_t hotSpotX, std::int32_t hotSpotY, std::int32_t coldSpotYOffset, std::int32_t firstFrameToOverwrite)
		{
			noop();
			return false;
		}

		jjANIMATION* jjANIMATION::get_jjAnimations(std::uint32_t index)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->GetAnimationTables().GetAnimation(index);
		}

		std::uint32_t jjANIMATION::get_firstFrame() const
		{
			return _firstFrame;
		}

		std::uint32_t jjANIMATION::set_firstFrame(std::uint32_t index)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			owner->GetAnimationTables().SetFirstFrame(*this, index);
			return _firstFrame;
		}

		std::uint32_t jjANIMATION::getAnimFirstFrame()
		{
			return _firstFrame;
		}

		jjANIMSET::jjANIMSET(std::uint32_t index)
			: _refCount(1), _index(index), _resolved(false), _watched(false), _seenFirstAnim(0)
		{
		}

		jjANIMSET::~jjANIMSET()
		{
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

		jjANIMSET* jjANIMSET::get_jjAnimSets(std::uint32_t index)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			return owner->GetAnimationTables().GetSet(index);
		}

		std::uint32_t jjANIMSET::convertAnimSetToUint()
		{
			return firstAnim;
		}

		jjANIMSET* jjANIMSET::load(std::uint32_t fileSetID, const String& filename, std::int32_t firstAnimToOverwrite, std::int32_t firstFrameToOverwrite)
		{
			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			owner->GetAnimationTables().LoadSet(*this, fileSetID, filename, firstAnimToOverwrite, firstFrameToOverwrite);
			// The handle returned is a new reference
			AddRef();
			return this;
		}

		jjANIMSET* jjANIMSET::allocate(const CScriptArray& frameCounts)
		{
			SmallVector<std::uint32_t, 16> counts;
			counts.reserve(frameCounts.GetSize());
			for (asUINT i = 0; i < frameCounts.GetSize(); i++) {
				counts.push_back(*static_cast<const std::uint32_t*>(frameCounts.At(i)));
			}

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			owner->GetAnimationTables().AllocateSet(*this, ArrayView<const std::uint32_t>(counts.data(), counts.size()));
			AddRef();
			return this;
		}

		jjANIMSET* jjANIMSET::loadSheet(const jjPIXELMAP& sheet, std::uint32_t frameWidth, std::uint32_t frameHeight, std::uint32_t frameSpacingX,
			std::uint32_t frameSpacingY, std::uint32_t startX, std::uint32_t startY, const CScriptArray& coldSpotYOffsets,
			std::int32_t firstAnimToOverwrite, std::int32_t firstFrameToOverwrite)
		{
			// The handle returned is a new reference
			AddRef();
			if (frameWidth == 0 || frameHeight == 0 || frameWidth > UINT16_MAX || frameHeight > UINT16_MAX) {
				return this;
			}

			jjPIXELMAP& source = const_cast<jjPIXELMAP&>(sheet);
			auto isCellEmpty = [&source, frameWidth, frameHeight](std::uint32_t left, std::uint32_t top) {
				for (std::uint32_t y = 0; y < frameHeight; y++) {
					for (std::uint32_t x = 0; x < frameWidth; x++) {
						if (*source.GetPixel(left + x, top + y) != 0) {
							return false;
						}
					}
				}
				return true;
			};

			// Each row an animation, up to the first row without a frame
			SmallVector<std::uint32_t, 16> counts;
			for (std::uint64_t top = startY; top + frameHeight <= sheet.height; top += std::uint64_t(frameHeight) + frameSpacingY) {
				std::uint32_t count = 0;
				for (std::uint64_t left = startX; left + frameWidth <= sheet.width && !isCellEmpty(std::uint32_t(left), std::uint32_t(top));
					left += std::uint64_t(frameWidth) + frameSpacingX) {
					count++;
				}
				if (count == 0) {
					break;
				}
				counts.push_back(count);
			}
			if (counts.empty()) {
				return this;
			}

			auto owner = ScriptLoader::FromActiveContext<LevelScriptLoader>();
			AnimationTables& tables = owner->GetAnimationTables();
			std::uint32_t firstAnimIndex = std::uint32_t(firstAnimToOverwrite);
			if (firstAnimToOverwrite < 0) {
				tables.AllocateSet(*this, ArrayView<const std::uint32_t>(counts.data(), counts.size()));
				firstAnimIndex = firstAnim;
			}

			SmallVector<std::uint8_t, 0> pixels(std::size_t(frameWidth) * frameHeight);
			std::uint32_t nextOverwrittenFrame = std::uint32_t(firstFrameToOverwrite);
			for (std::uint32_t row = 0; row < counts.size(); row++) {
				jjANIMATION* anim = tables.GetAnimation(firstAnimIndex + row);
				if (anim == nullptr) {
					break;
				}
				std::uint32_t firstFrameIndex = anim->getAnimFirstFrame();
				if (firstAnimToOverwrite >= 0) {
					// Into the frames from the one given on, or into those the animation has
					if (firstFrameToOverwrite >= 0) {
						firstFrameIndex = nextOverwrittenFrame;
						nextOverwrittenFrame += counts[row];
					}
					anim->frameCount = std::uint16_t(counts[row]);
					tables.SetFirstFrame(*anim, firstFrameIndex);
				}
				anim->Release();

				std::int32_t coldSpotYOffset = (row < coldSpotYOffsets.GetSize() ? *static_cast<const std::int32_t*>(coldSpotYOffsets.At(row)) : 0);
				std::uint32_t top = startY + row * (frameHeight + frameSpacingY);
				for (std::uint32_t i = 0; i < counts[row]; i++) {
					std::uint32_t left = startX + i * (frameWidth + frameSpacingX);
					for (std::uint32_t y = 0; y < frameHeight; y++) {
						for (std::uint32_t x = 0; x < frameWidth; x++) {
							pixels[std::size_t(y) * frameWidth + x] = *source.GetPixel(left + x, top + y);
						}
					}
					jjANIMFRAME* frame = tables.GetFrame(firstFrameIndex + i);
					if (frame == nullptr) {
						break;
					}
					tables.SetFramePixels(*frame, pixels.data(), frameWidth, frameHeight);
					frame->hotSpotX = std::int16_t(-std::int32_t(frameWidth / 2));
					frame->hotSpotY = std::int16_t(-std::int32_t(frameHeight / 2));
					frame->coldSpotX = frame->hotSpotX;
					frame->coldSpotY = std::int16_t(frame->hotSpotY + coldSpotYOffset);
					tables.MarkChanged(*frame);
					frame->Release();
				}
			}
			return this;
		}
	}

	Legacy::AnimationTables& LevelScriptLoader::GetAnimationTables()
	{
		if (_animTables == nullptr) {
			_animTables = std::make_unique<Legacy::AnimationTables>(versionTSF);
		}
		return *_animTables;
	}

	std::uint32_t getCustomSetID(std::uint8_t index) {
		return Legacy::AnimationTables::CustomFirstSetID + index;
	}

	void LevelScriptLoader::RegisterLegacyAnimations(asIScriptEngine* engine)
	{
		engine->RegisterObjectType("jjANIMFRAME", sizeof(jjANIMFRAME), asOBJ_REF /*| asOBJ_NOCOUNT*/);
		engine->RegisterObjectBehaviour("jjANIMFRAME", asBEHAVE_ADDREF, "void f()", asMETHOD(jjANIMFRAME, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjANIMFRAME", asBEHAVE_RELEASE, "void f()", asMETHOD(jjANIMFRAME, Release), asCALL_THISCALL);
		engine->RegisterGlobalFunction("jjANIMFRAME @get_jjAnimFrames(uint)", asFUNCTION(jjANIMFRAME::get_jjAnimFrames), asCALL_CDECL);
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 hotSpotX", asOFFSET(jjANIMFRAME, hotSpotX));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 hotSpotY", asOFFSET(jjANIMFRAME, hotSpotY));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 coldSpotX", asOFFSET(jjANIMFRAME, coldSpotX));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 coldSpotY", asOFFSET(jjANIMFRAME, coldSpotY));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 gunSpotX", asOFFSET(jjANIMFRAME, gunSpotX));
		engine->RegisterObjectProperty("jjANIMFRAME", "int16 gunSpotY", asOFFSET(jjANIMFRAME, gunSpotY));
		engine->RegisterObjectProperty("jjANIMFRAME", "const uint16 width", asOFFSET(jjANIMFRAME, width));
		engine->RegisterObjectProperty("jjANIMFRAME", "const uint16 height", asOFFSET(jjANIMFRAME, height));
		engine->RegisterObjectMethod("jjANIMFRAME", "jjANIMFRAME& opAssign(const jjANIMFRAME &in)", asMETHOD(jjANIMFRAME, operator=), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMFRAME", "bool get_transparent() const", asMETHOD(jjANIMFRAME, get_transparent), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMFRAME", "bool set_transparent(bool)", asMETHOD(jjANIMFRAME, set_transparent), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMFRAME", "bool get_is32Bit() const", asMETHOD(jjANIMFRAME, get_is32Bit), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMFRAME", "bool doesCollide(int xPos, int yPos, int direction, const jjANIMFRAME@ frame2, int xPos2, int yPos2, int direction2, bool always = false) const", asMETHOD(jjANIMFRAME, doesCollide), asCALL_THISCALL);
		engine->RegisterObjectType("jjANIMATION", sizeof(jjANIMATION), asOBJ_REF /*| asOBJ_NOCOUNT*/);
		engine->RegisterObjectBehaviour("jjANIMATION", asBEHAVE_ADDREF, "void f()", asMETHOD(jjANIMATION, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjANIMATION", asBEHAVE_RELEASE, "void f()", asMETHOD(jjANIMATION, Release), asCALL_THISCALL);
		engine->RegisterGlobalFunction("jjANIMATION @get_jjAnimations(uint)", asFUNCTION(jjANIMATION::get_jjAnimations), asCALL_CDECL);
		engine->RegisterObjectProperty("jjANIMATION", "uint16 frameCount", asOFFSET(jjANIMATION, frameCount));
		engine->RegisterObjectProperty("jjANIMATION", "int16 fps", asOFFSET(jjANIMATION, fps));
		engine->RegisterObjectMethod("jjANIMATION", "uint get_firstFrame() const", asMETHOD(jjANIMATION, get_firstFrame), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMATION", "uint set_firstFrame(uint)", asMETHOD(jjANIMATION, set_firstFrame), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMATION", "uint opImplConv() const", asMETHOD(jjANIMATION, getAnimFirstFrame), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMATION", "jjANIMATION& opAssign(const jjANIMATION &in)", asMETHOD(jjANIMATION, operator=), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMATION", "bool save(const ::string &in filename, const jjPAL &in palette = jjPalette) const", asMETHOD(jjANIMATION, save), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMATION", "bool load(const ::string &in filename, int hotSpotX, int hotSpotY, int coldSpotYOffset = 0, int firstFrameToOverwrite = -1)", asMETHOD(jjANIMATION, load), asCALL_THISCALL);

		engine->RegisterObjectType("jjANIMSET", sizeof(jjANIMSET), asOBJ_REF /*| asOBJ_NOCOUNT*/);
		engine->RegisterObjectBehaviour("jjANIMSET", asBEHAVE_ADDREF, "void f()", asMETHOD(jjANIMSET, AddRef), asCALL_THISCALL);
		engine->RegisterObjectBehaviour("jjANIMSET", asBEHAVE_RELEASE, "void f()", asMETHOD(jjANIMSET, Release), asCALL_THISCALL);
		engine->RegisterGlobalFunction("jjANIMSET @get_jjAnimSets(uint)", asFUNCTION(jjANIMSET::get_jjAnimSets), asCALL_CDECL);
		engine->RegisterObjectProperty("jjANIMSET", "uint firstAnim", asOFFSET(jjANIMSET, firstAnim));
		engine->RegisterObjectMethod("jjANIMSET", "uint opImplConv() const", asMETHOD(jjANIMSET, convertAnimSetToUint), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMSET", "jjANIMSET @load(uint fileSetID = 2048, const string &in filename = '', int firstAnimToOverwrite = -1, int firstFrameToOverwrite = -1)", asMETHOD(jjANIMSET, load), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjANIMSET", "jjANIMSET @allocate(const array<uint> &in frameCounts)", asMETHOD(jjANIMSET, allocate), asCALL_THISCALL);

		engine->RegisterObjectMethod("jjCANVAS", "void drawString(int xPixel, int yPixel, const ::string &in text, const jjANIMATION &in animation, STRING::Mode mode = STRING::NORMAL, uint8 param = 0)", asMETHOD(jjCANVAS, drawString), asCALL_THISCALL);
		engine->RegisterObjectMethod("jjCANVAS", "void drawString(int xPixel, int yPixel, const ::string &in text, const jjANIMATION &in animation, const jjTEXTAPPEARANCE &in appearance, uint8 param1 = 0, SPRITE::Mode spriteMode = SPRITE::PALSHIFT, uint8 param2 = 0)", asMETHOD(jjCANVAS, drawStringEx), asCALL_THISCALL);
		engine->RegisterGlobalFunction("void jjDrawString(float xPixel, float yPixel, const ::string &in text, const jjANIMATION &in animation, STRING::Mode mode = STRING::NORMAL, uint8 param = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjCANVAS::jjDrawString), asCALL_CDECL);
		engine->RegisterGlobalFunction("void jjDrawString(float xPixel, float yPixel, const ::string &in text, const jjANIMATION &in animation, const jjTEXTAPPEARANCE &in appearance, uint8 param1 = 0, SPRITE::Mode spriteMode = SPRITE::PALSHIFT, uint8 param2 = 0, int8 layerZ = 4, uint8 layerXY = 4, int8 playerID = -1)", asFUNCTION(jjCANVAS::jjDrawStringEx), asCALL_CDECL);
		engine->RegisterGlobalFunction("int jjGetStringWidth(const ::string &in text, const jjANIMATION &in animation, const jjTEXTAPPEARANCE &in style)", asFUNCTION(jjCANVAS::jjGetStringWidth), asCALL_CDECL);
	}
}

#endif
