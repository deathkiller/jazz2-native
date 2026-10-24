#if defined(WITH_ANGELSCRIPT)

#include "AnimationTables.h"
#include "../LevelScriptLoader.h"
#include "../../Compatibility/AnimSetMapping.h"
#include "../../Compatibility/JJ2Anims.h"

#include "../../../nCine/Graphics/Texture.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Containers/StringConcatenable.h>
#include <IO/FileSystem.h>

using namespace Jazz2::Resources;

#if defined(DEATH_TRACE)
#	define noop() Unimplemented(NCINE_CURRENT_FUNCTION)
#else
#	define noop() do {} while (false)
#endif

namespace Jazz2::Scripting::Legacy
{
	namespace
	{
		// Set IDs of the sets of the original in the animation file of 1.24 and of 1.23, by `anim`, -1 where the version
		// has no such set. The sets of the files are in the order of their names, but the files have sets that aren't
		// named here (and 1.23 has none of Lori and the Christmas enemies), so the two drift apart after the dragon.
		struct OriginalSetID {
			std::int16_t TSF;
			std::int16_t BaseGame;
		};

		constexpr OriginalSetID OriginalSetIDs[] = {
			{ 0, 0 },		// AMMO
			{ 1, 1 },		// BAT
			{ 2, 2 },		// BEEBOY
			{ 3, 3 },		// BEES
			{ 4, 4 },		// BIGBOX
			{ 5, 5 },		// BIGROCK
			{ 6, 6 },		// BIGTREE
			{ 7, 7 },		// BILSBOSS
			{ 8, 8 },		// BIRD
			{ 9, 9 },		// BIRD3D
			{ 10, 10 },		// BOLLPLAT
			{ 11, 11 },		// BONUS
			{ 12, 12 },		// BOSS
			{ 13, 13 },		// BRIDGE
			{ 14, 14 },		// BUBBA
			{ 15, 15 },		// BUMBEE
			{ 16, 16 },		// BUTTERFLY
			{ 17, 17 },		// CARROTPOLE
			{ 18, 18 },		// CAT
			{ 19, 19 },		// CAT2
			{ 20, 20 },		// CATERPIL
			{ 21, 21 },		// CHUCK
			{ 22, 22 },		// COMMON
			{ 23, 23 },		// CONTINUE
			{ 24, 24 },		// DEMON
			{ 25, 25 },		// DESTSCEN
			{ 26, 26 },		// DEVAN
			{ 27, 27 },		// DEVILDEVAN
			{ 28, 28 },		// DIAMPOLE
			{ 29, 29 },		// DOG
			{ 30, 30 },		// DOOR
			{ 31, 31 },		// DRAGFLY
			{ 32, 32 },		// DRAGON
			{ 38, 37 },		// EVA
			{ 39, 38 },		// FACES
			{ 41, 40 },		// FATCHK
			{ 42, 41 },		// FENCER
			{ 43, 42 },		// FISH
			{ 44, 43 },		// FLAG
			{ 45, 44 },		// FLARE
			{ 46, 45 },		// FONT
			{ 47, 46 },		// FROG
			{ 48, 47 },		// FRUITPLAT
			{ 49, 48 },		// GEMRING
			{ 50, 49 },		// GLOVE
			{ 51, 50 },		// GRASSPLAT
			{ 52, 51 },		// HATTER
			{ 53, 52 },		// HELMUT
			{ 55, 54 },		// JAZZ
			{ 56, 55 },		// JAZZ3D
			{ 58, 57 },		// JUNGLEPOLE
			{ 59, 58 },		// LABRAT
			{ 60, 59 },		// LIZARD
			{ 61, -1 },		// LORI
			{ 62, -1 },		// LORI2
			{ 64, 60 },		// MENU
			{ 65, 61 },		// MENUFONT
			{ 67, 63 },		// MONKEY
			{ 68, 64 },		// MOTH
			{ 71, 67 },		// PICKUPS
			{ 72, 68 },		// PINBALL
			{ 73, 69 },		// PINKPLAT
			{ 74, 70 },		// PSYCHPOLE
			{ 75, 71 },		// QUEEN
			{ 76, 72 },		// RAPIER
			{ 77, 73 },		// RAVEN
			{ 78, 74 },		// ROBOT
			{ 79, 75 },		// ROCK
			{ 80, 76 },		// ROCKTURT
			{ 83, 79 },		// SKELETON
			{ 84, 80 },		// SMALTREE
			{ 85, 81 },		// SNOW
			{ 86, 82 },		// SONCSHIP
			{ 87, 83 },		// SONICPLAT
			{ 88, 84 },		// SPARK
			{ 89, 85 },		// SPAZ
			{ 90, 86 },		// SPAZ2
			{ 91, 87 },		// SPAZ3D
			{ 93, 89 },		// SPIKEBOLL
			{ 94, 90 },		// SPIKEBOLL3D
			{ 95, 91 },		// SPIKEPLAT
			{ 96, 92 },		// SPRING
			{ 97, 93 },		// STEAM
			{ 99, 95 },		// SUCKER
			{ 100, 96 },	// TUBETURT
			{ 101, 97 },	// TUFBOSS
			{ 102, 98 },	// TUFTURT
			{ 103, 99 },	// TURTLE
			{ 104, 100 },	// TWEEDLE
			{ 105, 101 },	// UTERUS
			{ 106, 102 },	// VINE
			{ 107, 103 },	// WARP10
			{ 108, 104 },	// WARP100
			{ 109, 105 },	// WARP20
			{ 110, 106 },	// WARP50
			{ 112, 108 },	// WITCH
			{ 113, -1 },	// XBILSY
			{ 114, -1 },	// XLIZARD
			{ 115, -1 },	// XTURTLE
			{ 116, -1 },	// ZDOG
			{ 117, -1 }		// ZSPARK
		};

		static_assert(sizeof(OriginalSetIDs) / sizeof(OriginalSetIDs[0]) == mZZAMMO, "There has to be an entry for every set of the original");

		// Number of sets of the animation file of each version, the sets of `plus.j2a` follow them
		constexpr std::uint32_t AnimsSetCountTSF = 118;
		constexpr std::uint32_t AnimsSetCountBaseGame = 109;

		// The set of the older `plus.j2a` (9 sets, the one AnimSetMapping and the converter know) of each set of the one
		// of JJ2+ 6, by `anim` from `mZZAMMO`, -1 where it has no such set
		constexpr std::int8_t OlderPlusSets[] = { 0, 1, 2, 3, -1, 4, -1, -1, -1, 5, 6, -1, -1, -1 };

		static_assert(sizeof(OlderPlusSets) / sizeof(OlderPlusSets[0]) == mCOUNT - mZZAMMO, "There has to be an entry for every set of plus.j2a");

		// Reads a set of `plus.j2a` of the original by its index from `mZZAMMO`, from either version of the file
		bool ReadPlusAnimationSet(std::uint32_t plusSet, SmallVectorImpl<Compatibility::JJ2Anims::AnimSection>& anims)
		{
			String path = fs::FindPathCaseInsensitive(fs::CombinePath(ContentResolver::Get().GetSourcePath(), "plus.j2a"_s));
			if (plusSet >= arraySize(OlderPlusSets) || !fs::IsReadableFile(path)) {
				return false;
			}
			auto s = fs::Open(path, FileAccess::Read);
			if (!s->IsValid()) {
				return false;
			}
			// The number of sets after the signature, the length of the header, its unknown, the length and the CRC of the file
			s->Seek(24, SeekOrigin::Begin);
			std::int32_t setCount = s->ReadValueAsLE<std::int32_t>();
			s->Seek(0, SeekOrigin::Begin);
			std::int32_t fileSet = (setCount >= std::int32_t(arraySize(OlderPlusSets)) ? std::int32_t(plusSet) : OlderPlusSets[plusSet]);
			return (fileSet >= 0 && Compatibility::JJ2Anims::ReadAnimationSet(s, fileSet, anims));
		}

		// Reads a set of the animation file of the original by its set ID in the file
		bool ReadOriginalAnimationSet(std::uint32_t fileSet, SmallVectorImpl<Compatibility::JJ2Anims::AnimSection>& anims)
		{
			String path = fs::FindPathCaseInsensitive(fs::CombinePath(ContentResolver::Get().GetSourcePath(), "Anims.j2a"_s));
			if (!fs::IsReadableFile(path)) {
				return false;
			}
			auto s = fs::Open(path, FileAccess::Read);
			return (s->IsValid() && Compatibility::JJ2Anims::ReadAnimationSet(s, std::int32_t(fileSet), anims));
		}

		// Returns `true` if the converter converted any animation of a set of an animation file
		bool HasConvertedAnimations(Compatibility::AnimSetMapping& mapping, std::uint32_t fileSet)
		{
			std::uint32_t count = mapping.GetItemCount(fileSet);
			for (std::uint32_t i = 0; i < count; i++) {
				auto* entry = mapping.Get(fileSet, i);
				if (entry != nullptr && entry->Category != Compatibility::AnimSetMapping::Discard && !entry->Name.empty()) {
					return true;
				}
			}
			return false;
		}

		// Alpha the converter gives the pixels of a frame that's drawn translucent (see JJ2Anims::ImportAnimations())
		constexpr std::uint8_t TranslucentAlpha = 140;
		// Pixels kept between two frames of a sheet, so filtering doesn't bleed one into the other
		constexpr std::int32_t FrameSpacing = GenericGraphicResource::FrameBorder;
		// Largest sheet made of the frames of an animation
		constexpr std::int32_t MaxSheetSize = 4096;

		struct PackedRect {
			std::int32_t X, Y, W, H;
		};

		// Lays rectangles out in rows, the tallest first, returns `false` if they don't fit into MaxSheetSize²
		bool PackRows(SmallVectorImpl<PackedRect>& rects, std::int32_t& width, std::int32_t& height)
		{
			std::int64_t area = 0;
			std::int32_t widest = 1;
			for (const PackedRect& rect : rects) {
				area += std::int64_t(rect.W + FrameSpacing) * (rect.H + FrameSpacing);
				widest = std::max<std::int32_t>(widest, rect.W + FrameSpacing);
			}
			std::int32_t rowWidth = std::clamp<std::int32_t>(std::int32_t(std::sqrt(double(area))) + 1, widest, MaxSheetSize);

			SmallVector<std::int32_t, 16> order(rects.size());
			for (std::int32_t i = 0; i < std::int32_t(order.size()); i++) {
				order[i] = i;
			}
			std::sort(order.begin(), order.end(), [&rects](std::int32_t a, std::int32_t b) {
				return (rects[a].H > rects[b].H);
			});

			std::int32_t x = 0, y = 0, rowHeight = 0;
			width = 1;
			height = 1;
			for (std::int32_t i : order) {
				PackedRect& rect = rects[i];
				if (rect.W <= 0 || rect.H <= 0) {
					rect.X = rect.Y = 0;
					continue;
				}
				if (x > 0 && x + rect.W > rowWidth) {
					x = 0;
					y += rowHeight + FrameSpacing;
					rowHeight = 0;
				}
				rect.X = x;
				rect.Y = y;
				x += rect.W + FrameSpacing;
				rowHeight = std::max<std::int32_t>(rowHeight, rect.H);
				width = std::max<std::int32_t>(width, rect.X + rect.W);
				height = std::max<std::int32_t>(height, rect.Y + rect.H);
			}
			return (width <= MaxSheetSize && height <= MaxSheetSize);
		}

		// Reads a pixel of a sprite sheet as a palette index, the transparent ones as 0, and reports whether it's translucent
		std::uint8_t ReadSheetIndex(const ContentResolver::SpriteSheet& sheet, std::int32_t x, std::int32_t y, bool& translucent)
		{
			const std::uint8_t* pixel = &sheet.Pixels[(std::size_t(y) * sheet.Size.X + x) * sheet.ChannelCount];
			std::uint8_t alpha;
			switch (sheet.ChannelCount) {
				case 1: alpha = (pixel[0] != 0 ? 255 : 0); break;
				case 2: alpha = pixel[1]; break;
				default: alpha = pixel[3]; break;
			}
			if (alpha == 0) {
				return 0;
			}
			if (alpha != 255) {
				translucent = true;
			}
			return pixel[0];
		}

		void SetScriptException(const char* message)
		{
			if (asIScriptContext* ctx = asGetActiveContext()) {
				ctx->SetException(message);
			}
		}
	}

	AnimationTables::AnimationTables(bool isTSF)
		: _isTSF(isTSF), _changes(0), _flushedChanges(0), _lastFrameVersion(0), _warnedTrueColor(false)
	{
		_mapping = std::make_unique<Compatibility::AnimSetMapping>(Compatibility::AnimSetMapping::GetAnimMapping(
			isTSF ? Compatibility::JJ2Version::TSF : Compatibility::JJ2Version::BaseGame));
		_plusMapping = std::make_unique<Compatibility::AnimSetMapping>(Compatibility::AnimSetMapping::GetAnimMapping(
			Compatibility::JJ2Version::PlusExtension));
		_plusFirstSetID = (isTSF ? AnimsSetCountTSF : AnimsSetCountBaseGame);
		_plusSetCount = mCOUNT - mZZAMMO;

		_sets.resize(SetCount, nullptr);
		// The empty first entries, see the header
		while (_animations.size() < FirstSetAnim) {
			AddAnimation();
		}
		AddFrame(0);
	}

	AnimationTables::~AnimationTables()
	{
		// The next level gets the graphics as they were, unless they were given back already
		RestoreNativeGraphics();

		// A script may still hold some entries, they go away with the last reference
		for (jjANIMSET* set : _sets) {
			if (set != nullptr) {
				set->Release();
			}
		}
		for (jjANIMATION* anim : _animations) {
			anim->Release();
		}
		for (jjANIMFRAME* frame : _frames) {
			frame->Release();
		}
	}

	std::int32_t AnimationTables::GetSetID(anim set, bool isTSF)
	{
		if (set < mZZAMMO) {
			const OriginalSetID& ids = OriginalSetIDs[set];
			return (isTSF ? ids.TSF : ids.BaseGame);
		}
		if (set < mCOUNT) {
			return std::int32_t((isTSF ? AnimsSetCountTSF : AnimsSetCountBaseGame) + (set - mZZAMMO));
		}
		return -1;
	}

	jjANIMSET* AnimationTables::GetSet(std::uint32_t setID)
	{
		if (setID >= SetCount) {
			SetScriptException("Index out of bounds");
			return nullptr;
		}

		jjANIMSET* set = EnsureSet(setID);
		ResolveSet(*set);
		Watch(*set);
		set->AddRef();
		return set;
	}

	jjANIMATION* AnimationTables::GetAnimation(std::uint32_t index)
	{
		jjANIMATION* anim = EnsureAnimation(index);
		if (anim == nullptr) {
			SetScriptException("Index out of bounds");
			return nullptr;
		}

		Watch(*anim);
		anim->AddRef();
		return anim;
	}

	jjANIMFRAME* AnimationTables::GetFrame(std::uint32_t index)
	{
		jjANIMFRAME* frame = EnsureFrame(index);
		if (frame == nullptr) {
			SetScriptException("Index out of bounds");
			return nullptr;
		}

		EnsureFrameImage(*frame);
		Watch(*frame);
		frame->AddRef();
		return frame;
	}

	std::uint32_t AnimationTables::GetFirstAnim(std::int32_t setID)
	{
		if (setID < 0 || std::uint32_t(setID) >= SetCount) {
			return 0;
		}

		jjANIMSET* set = EnsureSet(std::uint32_t(setID));
		ResolveSet(*set);
		return set->firstAnim;
	}

	bool AnimationTables::GetAnimationFrames(std::uint32_t animIndex, std::uint32_t& frameCount, std::uint32_t& firstFrame)
	{
		if (animIndex >= _animations.size()) {
			return false;
		}

		const jjANIMATION* anim = _animations[animIndex];
		frameCount = anim->frameCount;
		firstFrame = anim->_firstFrame;
		return true;
	}

	std::int32_t AnimationTables::FindChangedFrame(std::int32_t setID, std::uint32_t animation, std::uint32_t frame) const
	{
		// Without loading the set, a set nothing loaded has no frame a script changed
		if (setID < 0 || std::uint32_t(setID) >= _sets.size() || _sets[setID] == nullptr || !_sets[setID]->_resolved) {
			return -1;
		}
		std::uint32_t animIndex = _sets[setID]->firstAnim + animation;
		if (_sets[setID]->firstAnim == 0 || animIndex >= _animations.size()) {
			return -1;
		}
		const jjANIMATION* anim = _animations[animIndex];
		std::uint32_t frameIndex = anim->_firstFrame + frame;
		if (frame >= anim->frameCount || frameIndex >= _frames.size() || _frames[frameIndex]->_version == 0) {
			return -1;
		}
		return std::int32_t(frameIndex);
	}

	bool AnimationTables::GetFrameBounds(std::uint32_t frameIndex, Recti& frameRect, Recti& pixelBounds)
	{
		if (frameIndex >= _frames.size()) {
			return false;
		}

		jjANIMFRAME& frame = *_frames[frameIndex];
		EnsureFrameImage(frame);
		std::int32_t width = frame.width;
		std::int32_t height = frame.height;
		if (width == 0 || height == 0) {
			return false;
		}

		frameRect = Recti(frame.hotSpotX, frame.hotSpotY, width, height);
		const std::uint8_t* pixels = frame._pixels.get();
		if (pixels == nullptr) {
			// There's no image to find the pixels in, the whole frame is taken
			pixelBounds = frameRect;
			return true;
		}

		std::int32_t minX = width, minY = height, maxX = -1, maxY = -1;
		for (std::int32_t y = 0; y < height; y++) {
			for (std::int32_t x = 0; x < width; x++) {
				if (pixels[y * width + x] != 0) {
					minX = std::min(minX, x);
					maxX = std::max(maxX, x);
					minY = std::min(minY, y);
					maxY = std::max(maxY, y);
				}
			}
		}
		if (maxX < 0) {
			return false;
		}

		pixelBounds = Recti(frame.hotSpotX + minX, frame.hotSpotY + minY, maxX - minX + 1, maxY - minY + 1);
		return true;
	}

	bool AnimationTables::GetFrameSpots(std::uint32_t frameIndex, Recti& frameRect, Vector2i& coldSpot)
	{
		if (frameIndex >= _frames.size()) {
			return false;
		}

		jjANIMFRAME& frame = *_frames[frameIndex];
		EnsureFrameImage(frame);
		frameRect = Recti(frame.hotSpotX, frame.hotSpotY, frame.width, frame.height);
		coldSpot = Vector2i(frame.coldSpotX, frame.coldSpotY);
		return true;
	}

	bool AnimationTables::LoadSet(jjANIMSET& set, std::uint32_t fileSetID, StringView filename, std::int32_t firstAnimToOverwrite, std::int32_t firstFrameToOverwrite)
	{
		// The graphics of the game of a set of the original have to know what they show before the set changes
		ResolveSet(set);

		if (filename.empty()) {
			// The default set ID means the set itself, which is always loaded if it's one of the original
			if (fileSetID == 2048 || fileSetID == set._index) {
				return (set.firstAnim != 0);
			}

			// Another set of the original, its animations are copied in
			std::uint32_t sourceFirstAnim = GetFirstAnim(std::int32_t(fileSetID));
			if (sourceFirstAnim == 0) {
				return false;
			}
			std::uint32_t animCount = _sets[fileSetID]->_animCount;

			std::uint32_t animIndex = (firstAnimToOverwrite >= 0 ? std::uint32_t(firstAnimToOverwrite) : std::uint32_t(_animations.size()));
			std::uint32_t frameIndex = (firstFrameToOverwrite >= 0 ? std::uint32_t(firstFrameToOverwrite) : std::uint32_t(_frames.size()));
			for (std::uint32_t i = 0; i < animCount && sourceFirstAnim + i < _animations.size(); i++) {
				// The source may move while the tables grow, so it's looked up again every time
				jjANIMATION* target = EnsureAnimation(animIndex + i);
				if (target == nullptr) {
					return false;
				}
				const jjANIMATION& source = *_animations[sourceFirstAnim + i];
				target->frameCount = source.frameCount;
				target->fps = source.fps;
				target->_firstFrame = (source.frameCount > 0 ? frameIndex : 0);
				std::uint32_t sourceFirstFrame = source._firstFrame;
				std::uint32_t sourceFrameCount = source.frameCount;
				for (std::uint32_t j = 0; j < sourceFrameCount; j++) {
					jjANIMFRAME* frame = EnsureFrame(frameIndex++);
					if (frame == nullptr) {
						return false;
					}
					frame->_owner = target->_index;
					if (sourceFirstFrame + j < _frames.size()) {
						AssignFrame(*frame, *_frames[sourceFirstFrame + j]);
					}
				}
				MarkChanged(*target);
			}
			set.firstAnim = animIndex;
			_changes++;
			return true;
		}

		if (!IsScriptFileNameSafe(filename)) {
			return false;
		}
		auto s = OpenScriptFile(filename);
		if (s == nullptr || !s->IsValid()) {
			LOGW("Cannot open animation file \"{}\"", filename);
			return false;
		}

		SmallVector<Compatibility::JJ2Anims::AnimSection, 0> anims;
		if (!Compatibility::JJ2Anims::ReadAnimationSet(s, std::int32_t(fileSetID), anims)) {
			LOGW("Cannot read set {} of animation file \"{}\"", fileSetID, filename);
			return false;
		}

		std::uint32_t animIndex = (firstAnimToOverwrite >= 0 ? std::uint32_t(firstAnimToOverwrite) : std::uint32_t(_animations.size()));
		std::uint32_t frameIndex = (firstFrameToOverwrite >= 0 ? std::uint32_t(firstFrameToOverwrite) : std::uint32_t(_frames.size()));
		for (std::uint32_t i = 0; i < anims.size(); i++) {
			Compatibility::JJ2Anims::AnimSection& section = anims[i];
			jjANIMATION* anim = EnsureAnimation(animIndex + i);
			if (anim == nullptr) {
				return false;
			}

			anim->frameCount = std::uint16_t(section.Frames.size());
			anim->fps = std::int16_t(section.FrameRate);
			anim->_firstFrame = (anim->frameCount > 0 ? frameIndex : 0);
			for (Compatibility::JJ2Anims::AnimFrameSection& source : section.Frames) {
				jjANIMFRAME* frame = EnsureFrame(frameIndex++);
				if (frame == nullptr) {
					return false;
				}
				frame->_source = -1;
				frame->_owner = anim->_index;
				frame->width = std::uint16_t(source.SizeX);
				frame->height = std::uint16_t(source.SizeY);
				frame->hotSpotX = source.HotspotX;
				frame->hotSpotY = source.HotspotY;
				frame->coldSpotX = source.ColdspotX;
				frame->coldSpotY = source.ColdspotY;
				frame->gunSpotX = source.GunspotX;
				frame->gunSpotY = source.GunspotY;
				frame->_transparent = source.DrawTransparent;
				frame->_pixels = Death::move(source.ImageData);
				MarkChanged(*frame);
			}
			MarkChanged(*anim);
		}

		set.firstAnim = animIndex;
		_changes++;
		return true;
	}

	void AnimationTables::AllocateSet(jjANIMSET& set, ArrayView<const std::uint32_t> frameCounts)
	{
		ResolveSet(set);

		std::uint64_t frameTotal = 0;
		for (std::uint32_t frameCount : frameCounts) {
			frameTotal += std::min<std::uint32_t>(frameCount, UINT16_MAX);
		}
		if (_animations.size() + frameCounts.size() > MaxAnimations || _frames.size() + frameTotal > MaxFrames) {
			SetScriptException("Too many animations or frames");
			return;
		}

		set.firstAnim = std::uint32_t(_animations.size());
		for (std::uint32_t frameCount : frameCounts) {
			jjANIMATION* anim = AddAnimation();
			anim->frameCount = std::uint16_t(std::min<std::uint32_t>(frameCount, UINT16_MAX));
			anim->_firstFrame = (anim->frameCount > 0 ? std::uint32_t(_frames.size()) : 0);
			for (std::uint32_t j = 0; j < anim->frameCount; j++) {
				AddFrame(anim->_index);
			}
			MarkChanged(*anim);
		}
		_changes++;
	}

	void AnimationTables::AssignAnimation(jjANIMATION& target, const jjANIMATION& source)
	{
		target.frameCount = source.frameCount;
		target.fps = source.fps;
		target._firstFrame = source._firstFrame;
		MarkChanged(target);
	}

	void AnimationTables::SetFirstFrame(jjANIMATION& anim, std::uint32_t firstFrame)
	{
		anim._firstFrame = firstFrame;
		MarkChanged(anim);
	}

	void AnimationTables::AssignFrame(jjANIMFRAME& target, const jjANIMFRAME& source)
	{
		if (&target == &source) {
			return;
		}

		EnsureFrameImage(const_cast<jjANIMFRAME&>(source));
		// What isn't read yet would be read over what's copied
		target._source = -1;

		target.hotSpotX = source.hotSpotX;
		target.hotSpotY = source.hotSpotY;
		target.coldSpotX = source.coldSpotX;
		target.coldSpotY = source.coldSpotY;
		target.gunSpotX = source.gunSpotX;
		target.gunSpotY = source.gunSpotY;
		target._transparent = source._transparent;
		if (source._colors != nullptr) {
			SetFrameColors(target, source._colors.get(), source._pixels.get(), source.width, source.height);
		} else {
			SetFramePixels(target, source._pixels.get(), source.width, source.height);
		}
	}

	void AnimationTables::SetFramePixels(jjANIMFRAME& frame, const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height)
	{
		EnsureFrameImage(frame);

		width = std::min<std::uint32_t>(width, UINT16_MAX);
		height = std::min<std::uint32_t>(height, UINT16_MAX);
		std::size_t count = std::size_t(width) * height;
		std::unique_ptr<std::uint8_t[]> copy;
		if (count > 0) {
			copy = std::make_unique<std::uint8_t[]>(count);
			if (pixels != nullptr) {
				std::memcpy(copy.get(), pixels, count);
			} else {
				std::memset(copy.get(), 0, count);
			}
		}
		frame._pixels = Death::move(copy);
		frame._colors = nullptr;
		frame.width = std::uint16_t(width);
		frame.height = std::uint16_t(height);
		MarkChanged(frame);
	}

	void AnimationTables::RecolorFrame(jjANIMFRAME& frame, const std::uint8_t* colors)
	{
		EnsureFrameImage(frame);
		if (frame._pixels != nullptr) {
			std::size_t count = std::size_t(frame.width) * frame.height;
			for (std::size_t i = 0; i < count; i++) {
				frame._pixels[i] = colors[frame._pixels[i]];
			}
		}
		frame._colors = nullptr;
		MarkChanged(frame);
	}

	void AnimationTables::SetFrameColors(jjANIMFRAME& frame, const jjPALCOLOR* colors, const std::uint8_t* indices, std::uint32_t width, std::uint32_t height)
	{
		SetFramePixels(frame, indices, width, height);

		// The size may have been clamped
		std::size_t count = std::size_t(frame.width) * frame.height;
		if (count > 0 && colors != nullptr) {
			auto copy = std::make_unique<jjPALCOLOR[]>(count);
			for (std::uint32_t y = 0; y < frame.height; y++) {
				for (std::uint32_t x = 0; x < frame.width; x++) {
					copy[std::size_t(y) * frame.width + x] = colors[std::size_t(y) * width + x];
				}
			}
			frame._colors = Death::move(copy);
		}
	}

	void AnimationTables::OnPaletteChanged()
	{
		_paletteVersion++;
		_changes++;
	}

	void AnimationTables::MarkChanged(jjANIMFRAME& frame)
	{
		frame._version = ++_lastFrameVersion;
		frame._seenSpots[0] = frame.hotSpotX;
		frame._seenSpots[1] = frame.hotSpotY;
		frame._seenSpots[2] = frame.coldSpotX;
		frame._seenSpots[3] = frame.coldSpotY;
		frame._seenSpots[4] = frame.gunSpotX;
		frame._seenSpots[5] = frame.gunSpotY;
		_changes++;
	}

	void AnimationTables::MarkChanged(jjANIMATION& anim)
	{
		anim._seenFrameCount = anim.frameCount;
		_changes++;
	}

	bool AnimationTables::DoFramesCollide(const jjANIMFRAME& frame1, std::int32_t x1, std::int32_t y1, std::int32_t direction1,
		const jjANIMFRAME& frame2, std::int32_t x2, std::int32_t y2, std::int32_t direction2, bool always)
	{
		EnsureFrameImage(const_cast<jjANIMFRAME&>(frame1));
		EnsureFrameImage(const_cast<jjANIMFRAME&>(frame2));
		if (frame1._pixels == nullptr || frame2._pixels == nullptr) {
			return false;
		}

		struct Placement {
			std::int32_t Left, Top;
			bool FlipX, FlipY;
		};
		// The direction is `SPRITE::Direction` - the sign flips horizontally, the next bit when it differs from it vertically
		auto place = [](const jjANIMFRAME& frame, std::int32_t x, std::int32_t y, std::int32_t direction) {
			std::uint8_t bits = std::uint8_t(direction);
			Placement p;
			p.FlipX = ((bits & 0x80) != 0);
			p.FlipY = (((bits & 0x80) != 0) != ((bits & 0x40) != 0));
			p.Left = (p.FlipX ? x - frame.hotSpotX - frame.width : x + frame.hotSpotX);
			p.Top = (p.FlipY ? y - frame.hotSpotY - frame.height : y + frame.hotSpotY);
			return p;
		};
		auto sample = [](const jjANIMFRAME& frame, const Placement& p, std::int32_t x, std::int32_t y) {
			std::int32_t u = x - p.Left;
			std::int32_t v = y - p.Top;
			if (p.FlipX) {
				u = frame.width - 1 - u;
			}
			if (p.FlipY) {
				v = frame.height - 1 - v;
			}
			return frame._pixels[std::size_t(v) * frame.width + u];
		};

		Placement p1 = place(frame1, x1, y1, direction1);
		Placement p2 = place(frame2, x2, y2, direction2);
		std::int32_t left = std::max<std::int32_t>(p1.Left, p2.Left);
		std::int32_t top = std::max<std::int32_t>(p1.Top, p2.Top);
		std::int32_t right = std::min<std::int32_t>(p1.Left + frame1.width, p2.Left + frame2.width);
		std::int32_t bottom = std::min<std::int32_t>(p1.Top + frame1.height, p2.Top + frame2.height);

		// A single pixel is enough for bullets, otherwise it takes about eight as JJ2+ says
		std::int32_t required = (always ? 1 : 8);
		std::int32_t common = 0;
		for (std::int32_t y = top; y < bottom; y++) {
			for (std::int32_t x = left; x < right; x++) {
				if (sample(frame1, p1, x, y) != 0 && sample(frame2, p2, x, y) != 0) {
					if (++common >= required) {
						return true;
					}
				}
			}
		}
		return false;
	}

	GenericGraphicResource* AnimationTables::ResolveSprite(std::int32_t setID, std::int32_t animation, std::int32_t frame, std::int32_t& cell)
	{
		std::uint32_t firstAnim = GetFirstAnim(setID);
		if (firstAnim == 0 || animation < 0) {
			return nullptr;
		}

		std::uint32_t animIndex = firstAnim + std::uint32_t(animation);
		if (animIndex >= _animations.size()) {
			return nullptr;
		}

		jjANIMATION& anim = *_animations[animIndex];
		if (anim.frameCount == 0) {
			return nullptr;
		}

		std::int32_t position = frame % anim.frameCount;
		if (position < 0) {
			position += anim.frameCount;
		}

		// The graphics of the game repeat the frames of the animation over their own frames, so the first ones are it
		if (anim._native >= 0) {
			NativeAnimation& native = _natives[anim._native];
			if (native.ShownAnim == animIndex && std::uint32_t(position) < native.FrameCount) {
				if (auto* graphics = RequestNativeGraphics(native)) {
					cell = position;
					return graphics;
				}
			}
		}

		cell = position;
		return RequestOwnGraphics(anim);
	}

	GenericGraphicResource* AnimationTables::ResolveFrame(std::uint32_t frameIndex, std::int32_t& cell)
	{
		if (frameIndex == 0 || frameIndex >= _frames.size()) {
			return nullptr;
		}

		// The animation the frame was made for draws it, as long as it still has it
		jjANIMFRAME& frame = *_frames[frameIndex];
		if (frame._owner != 0 && frame._owner < _animations.size()) {
			jjANIMATION& owner = *_animations[frame._owner];
			if (frameIndex >= owner._firstFrame && frameIndex < owner._firstFrame + owner.frameCount) {
				std::uint32_t position = frameIndex - owner._firstFrame;
				if (owner._native >= 0) {
					NativeAnimation& native = _natives[owner._native];
					if (native.ShownAnim == owner._index && position < native.FrameCount) {
						if (auto* graphics = RequestNativeGraphics(native)) {
							cell = std::int32_t(position);
							return graphics;
						}
					}
				}
				cell = std::int32_t(position);
				return RequestOwnGraphics(owner);
			}
		}

		// Otherwise it gets graphics of its own
		EnsureFrameImage(frame);
		auto it = _frameGraphics.find(frameIndex);
		if (it == _frameGraphics.end()) {
			it = _frameGraphics.emplace(frameIndex, FrameGraphics{}).first;
		}
		FrameGraphics& entry = it->second;
		if (entry.Graphics == nullptr || entry.Version != frame._version) {
			if (entry.Graphics == nullptr) {
				entry.Graphics = std::make_unique<GenericGraphicResource>();
			}
			jjANIMFRAME* frames[] = { &frame };
			ComposeGraphics(*entry.Graphics, ArrayView<jjANIMFRAME* const>(frames, 1), 0, false);
			entry.Version = frame._version;
		}
		cell = 0;
		return entry.Graphics.get();
	}

	std::uint32_t AnimationTables::FindAnimationOfGraphics(const GenericGraphicResource* graphics)
	{
		if (graphics == nullptr) {
			return 0;
		}

		auto it = _graphicsAnimations.find(graphics);
		if (it == _graphicsAnimations.end()) {
			if (_pathAnimations.empty()) {
				// Every animation of the sets of the original by the path of its sprite sheet, as ResolveSet() makes them,
				// with the separators the cache of the graphics has
				for (std::uint32_t setID = 0; setID < _plusFirstSetID + _plusSetCount && setID < CustomFirstSetID; setID++) {
					std::uint32_t fileSet;
					auto* mapping = GetFileSet(setID, fileSet);
					std::uint32_t count = (mapping != nullptr ? mapping->GetItemCount(fileSet) : 0);
					for (std::uint32_t i = 0; i < count && i <= 0xFFFF; i++) {
						auto* entry = mapping->Get(fileSet, i);
						if (entry == nullptr || entry->Name.empty()) {
							continue;
						}
						if (entry->Category != Compatibility::AnimSetMapping::Discard) {
							String path = entry->Category + '/' + entry->Name + ".aura"_s;
							_pathAnimations.emplace(fs::ToNativeSeparators(path), (setID << 16) | i);
						} else {
							// The objects of the game show the graphics as this item of the original (the gems of a level, not
							// those of the HUD), whatever else shows them too
							String path = fs::ToNativeSeparators(entry->Name + ".aura"_s);
							auto existing = _pathAnimations.find(path);
							if (existing != _pathAnimations.end()) {
								existing->second = (setID << 16) | i;
							} else {
								_pathAnimations.emplace(Death::move(path), (setID << 16) | i);
							}
						}
					}
				}
			}
			auto pathIt = _pathAnimations.find(ContentResolver::Get().FindGraphicsPath(graphics));
			it = _graphicsAnimations.emplace(graphics, pathIt != _pathAnimations.end() ? pathIt->second : UINT32_MAX).first;
		}
		if (it->second == UINT32_MAX) {
			return 0;
		}

		// Where the set has its animations now
		jjANIMSET* set = EnsureSet(it->second >> 16);
		ResolveSet(*set);
		return (set->firstAnim != 0 ? set->firstAnim + (it->second & 0xFFFF) : 0);
	}

	GenericGraphicResource* AnimationTables::ResolveAnimation(std::uint32_t animIndex, std::int32_t& cellCount)
	{
		if (animIndex == 0 || animIndex >= _animations.size()) {
			return nullptr;
		}

		jjANIMATION& anim = *_animations[animIndex];
		if (anim.frameCount == 0) {
			return nullptr;
		}

		if (anim._native >= 0) {
			NativeAnimation& native = _natives[anim._native];
			if (native.ShownAnim == animIndex) {
				if (auto* graphics = RequestNativeGraphics(native)) {
					cellCount = graphics->FrameCount;
					return graphics;
				}
			}
		}

		auto* graphics = RequestOwnGraphics(anim);
		cellCount = (graphics != nullptr ? graphics->FrameCount : 0);
		return graphics;
	}

	void AnimationTables::Flush()
	{
		// What scripts wrote to the properties of the entries they hold, an entry no script holds anymore can't change
		for (std::size_t i = 0; i < _watchedFrames.size(); ) {
			jjANIMFRAME* frame = _watchedFrames[i];
			const std::int16_t spots[6] = { frame->hotSpotX, frame->hotSpotY, frame->coldSpotX, frame->coldSpotY, frame->gunSpotX, frame->gunSpotY };
			if (std::memcmp(spots, frame->_seenSpots, sizeof(spots)) != 0) {
				MarkChanged(*frame);
			}
			if (frame->_refCount <= 1) {
				frame->_watched = false;
				_watchedFrames[i] = _watchedFrames.back();
				_watchedFrames.pop_back();
			} else {
				i++;
			}
		}
		for (std::size_t i = 0; i < _watchedAnimations.size(); ) {
			jjANIMATION* anim = _watchedAnimations[i];
			if (anim->frameCount != anim->_seenFrameCount) {
				MarkChanged(*anim);
			}
			if (anim->_refCount <= 1) {
				anim->_watched = false;
				_watchedAnimations[i] = _watchedAnimations.back();
				_watchedAnimations.pop_back();
			} else {
				i++;
			}
		}
		for (std::size_t i = 0; i < _watchedSets.size(); ) {
			jjANIMSET* set = _watchedSets[i];
			if (set->firstAnim != set->_seenFirstAnim) {
				set->_seenFirstAnim = set->firstAnim;
				_changes++;
			}
			if (set->_refCount <= 1) {
				set->_watched = false;
				_watchedSets[i] = _watchedSets.back();
				_watchedSets.pop_back();
			} else {
				i++;
			}
		}

		if (_changes == _flushedChanges || _nativeGraphicsRestored) {
			return;
		}
		_flushedChanges = _changes;

		// The graphics of the game show what's at `firstAnim + n` of their set now
		for (std::size_t i = 0; i < _natives.size(); i++) {
			NativeAnimation& native = _natives[i];
			if (native.Path.empty()) {
				continue;
			}

			const jjANIMSET* set = _sets[native.SetID];
			std::uint32_t animIndex = set->firstAnim + native.AnimInSet;
			if (set->firstAnim == 0 || animIndex >= _animations.size()) {
				continue;
			}

			jjANIMATION& anim = *_animations[animIndex];
			std::uint64_t signature = GetSignature(anim);
			if (animIndex == native.ShownAnim && signature == native.ShownSignature) {
				continue;
			}

			if (animIndex != native.ShownAnim) {
				jjANIMATION& previous = *_animations[native.ShownAnim];
				if (previous._native == std::int32_t(i)) {
					previous._native = -1;
				}
			}

			if (animIndex == native.OriginalAnim && signature == native.OriginalSignature) {
				RestoreNativeGraphics(native);
			} else {
				ShowInNativeGraphics(native, anim);
			}

			anim._native = std::int32_t(i);
			native.ShownAnim = animIndex;
			native.ShownSignature = signature;
		}
	}

	Compatibility::AnimSetMapping* AnimationTables::GetFileSet(std::uint32_t setID, std::uint32_t& fileSet)
	{
		if (setID < _plusFirstSetID) {
			fileSet = setID;
			return _mapping.get();
		}
		if (setID - _plusFirstSetID < _plusSetCount && OlderPlusSets[setID - _plusFirstSetID] >= 0) {
			fileSet = std::uint32_t(OlderPlusSets[setID - _plusFirstSetID]);
			return _plusMapping.get();
		}
		return nullptr;
	}

	void AnimationTables::ResolveSet(jjANIMSET& set)
	{
		if (set._resolved) {
			return;
		}
		set._resolved = true;

		std::uint32_t fileSet;
		auto* mapping = GetFileSet(set._index, fileSet);
		std::uint32_t animCount = (mapping != nullptr ? mapping->GetItemCount(fileSet) : 0);

		// Most of `plus.j2a` isn't converted (warp, reticles, the fonts...), what isn't comes from the file itself if it's there,
		// as does a set of the original that isn't converted at all (the fonts, which scripts draw as sprites too)
		SmallVector<Compatibility::JJ2Anims::AnimSection, 0> fileAnims;
		// JJ2+ draws the font of the original with its own of more characters (127 is ∞, SwitchTestV2 draws it so)
		bool isFont = (std::int32_t(set._index) == GetSetID(mFONT, _plusFirstSetID == AnimsSetCountTSF));
		if (set._index >= _plusFirstSetID && set._index - _plusFirstSetID < _plusSetCount &&
			ReadPlusAnimationSet(set._index - _plusFirstSetID, fileAnims)) {
			animCount = std::max(animCount, std::uint32_t(fileAnims.size()));
		} else if (isFont && ReadPlusAnimationSet(mZZFONT - mZZAMMO, fileAnims)) {
			animCount = std::max(animCount, std::uint32_t(fileAnims.size()));
		} else if (set._index < _plusFirstSetID && mapping != nullptr && animCount > 0 && !HasConvertedAnimations(*mapping, fileSet) &&
			ReadOriginalAnimationSet(fileSet, fileAnims)) {
			animCount = std::max(animCount, std::uint32_t(fileAnims.size()));
		}

		if (animCount == 0 || _animations.size() + animCount > MaxAnimations) {
			// Not one of the original or the version doesn't have it, the set isn't loaded
			return;
		}

		auto& resolver = ContentResolver::Get();
		std::uint32_t firstAnim = std::uint32_t(_animations.size());
		for (std::uint32_t i = 0; i < animCount; i++) {
			jjANIMATION* anim = AddAnimation();
			std::uint32_t nativeIndex = std::uint32_t(_natives.size());
			NativeAnimation& native = _natives.emplace_back();
			native.Graphics = nullptr;
			native.SetID = set._index;
			native.AnimInSet = i;
			native.FrameCount = 0;
			native.FirstFrame = 0;
			native.OriginalAnim = anim->_index;
			native.ShownAnim = anim->_index;

			auto* entry = (mapping != nullptr && i < mapping->GetItemCount(fileSet) ? mapping->Get(fileSet, i) : nullptr);
			if (entry != nullptr && !entry->Name.empty()) {
				// A discarded item the game draws with the graphics of another one has them too (see AnimSetMapping::DiscardItemAs())
				String path = (entry->Category != Compatibility::AnimSetMapping::Discard ? entry->Category + '/' + entry->Name : String(entry->Name)) + ".aura"_s;
				ContentResolver::SpriteSheet sheet;
				if (resolver.ReadSpriteSheet(path, sheet, false) && sheet.FrameCount > 0 &&
					_frames.size() + std::uint32_t(sheet.FrameCount) <= MaxFrames) {
					native.Path = Death::move(path);
					native.FrameCount = std::uint32_t(sheet.FrameCount);
					native.FirstFrame = std::uint32_t(_frames.size());
					anim->frameCount = std::uint16_t(sheet.FrameCount);
					// The converter stores AnimDuration = 5 / FrameRate
					anim->fps = (sheet.AnimDuration > 0.0f ? std::int16_t(5.0f / sheet.AnimDuration + 0.5f) : 0);
					anim->_firstFrame = native.FirstFrame;
					for (std::int32_t j = 0; j < sheet.FrameCount; j++) {
						jjANIMFRAME* frame = AddFrame(anim->_index);
						frame->_source = std::int32_t(nativeIndex);
					}
				}
			}

			if (anim->frameCount == 0 && i < fileAnims.size() && _frames.size() + fileAnims[i].Frames.size() <= MaxFrames) {
				Compatibility::JJ2Anims::AnimSection& section = fileAnims[i];
				anim->frameCount = std::uint16_t(section.Frames.size());
				anim->fps = std::int16_t(section.FrameRate);
				anim->_firstFrame = (anim->frameCount > 0 ? std::uint32_t(_frames.size()) : 0);
				for (Compatibility::JJ2Anims::AnimFrameSection& source : section.Frames) {
					jjANIMFRAME* frame = AddFrame(anim->_index);
					frame->_source = -1;
					frame->width = std::uint16_t(source.SizeX);
					frame->height = std::uint16_t(source.SizeY);
					frame->hotSpotX = source.HotspotX;
					frame->hotSpotY = source.HotspotY;
					frame->coldSpotX = source.ColdspotX;
					frame->coldSpotY = source.ColdspotY;
					frame->gunSpotX = source.GunspotX;
					frame->gunSpotY = source.GunspotY;
					frame->_transparent = source.DrawTransparent;
					frame->_pixels = Death::move(source.ImageData);
				}
			}

			anim->_seenFrameCount = anim->frameCount;
			anim->_native = std::int32_t(nativeIndex);
			native.OriginalSignature = GetSignature(*anim);
			native.ShownSignature = native.OriginalSignature;
		}

		set.firstAnim = firstAnim;
		set._seenFirstAnim = firstAnim;
		set._animCount = animCount;
	}

	jjANIMSET* AnimationTables::EnsureSet(std::uint32_t setID)
	{
		jjANIMSET*& set = _sets[setID];
		if (set == nullptr) {
			void* mem = asAllocMem(sizeof(jjANIMSET));
			set = new(mem) jjANIMSET(setID);
		}
		return set;
	}

	jjANIMATION* AnimationTables::EnsureAnimation(std::uint32_t index)
	{
		if (index >= MaxAnimations) {
			return nullptr;
		}
		while (_animations.size() <= index) {
			AddAnimation();
		}
		return _animations[index];
	}

	jjANIMFRAME* AnimationTables::EnsureFrame(std::uint32_t index)
	{
		if (index >= MaxFrames) {
			return nullptr;
		}
		while (_frames.size() <= index) {
			AddFrame(0);
		}
		return _frames[index];
	}

	jjANIMATION* AnimationTables::AddAnimation()
	{
		void* mem = asAllocMem(sizeof(jjANIMATION));
		jjANIMATION* anim = new(mem) jjANIMATION(std::uint32_t(_animations.size()));
		_animations.push_back(anim);
		return anim;
	}

	jjANIMFRAME* AnimationTables::AddFrame(std::uint32_t owner)
	{
		void* mem = asAllocMem(sizeof(jjANIMFRAME));
		jjANIMFRAME* frame = new(mem) jjANIMFRAME(std::uint32_t(_frames.size()));
		frame->_owner = owner;
		_frames.push_back(frame);
		return frame;
	}

	void AnimationTables::EnsureFrameImage(jjANIMFRAME& frame)
	{
		if (frame._source >= 0) {
			ReadNativeFrames(std::uint32_t(frame._source));
		}
	}

	void AnimationTables::ReadNativeFrames(std::uint32_t nativeIndex)
	{
		NativeAnimation& native = _natives[nativeIndex];

		ContentResolver::SpriteSheet sheet;
		bool hasSheet = (!native.Path.empty() && ContentResolver::Get().ReadSpriteSheet(native.Path, sheet, true));
		if (hasSheet && !sheet.IsIndexed) {
			// The few sprites of the menu are converted to colors, there are no palette indices to give scripts
			if (!_warnedTrueColor) {
				_warnedTrueColor = true;
				LOGW("Frames of \"{}\" aren't palette-indexed, scripts see them empty", native.Path);
			}
			hasSheet = false;
		}

		for (std::uint32_t i = 0; i < native.FrameCount; i++) {
			jjANIMFRAME& frame = *_frames[native.FirstFrame + i];
			if (frame._source != std::int32_t(nativeIndex)) {
				// Already read, or overwritten since
				continue;
			}
			frame._source = -1;
			if (!hasSheet || std::int32_t(i) >= sheet.FrameCount) {
				continue;
			}

			// The area of the frame in the sheet and where it begins in the cell of the frame
			std::int32_t x, y, w, h, offsetX, offsetY;
			if (!sheet.FrameRects.empty()) {
				// Qualified, <windows.h> declares a global FrameRect()
				const Resources::FrameRect& rect = sheet.FrameRects[i];
				x = rect.X;
				y = rect.Y;
				w = rect.W;
				h = rect.H;
				offsetX = rect.OffsetX;
				offsetY = rect.OffsetY;
			} else {
				// A regular grid has only the cells, the frame is what isn't transparent of its cell
				std::int32_t columns = std::max<std::int32_t>(sheet.FrameConfiguration.X, 1);
				std::int32_t cellX = (std::int32_t(i) % columns) * sheet.FrameDimensions.X;
				std::int32_t cellY = (std::int32_t(i) / columns) * sheet.FrameDimensions.Y;
				std::int32_t minX = INT32_MAX, minY = INT32_MAX, maxX = -1, maxY = -1;
				bool translucent = false;
				for (std::int32_t v = 0; v < sheet.FrameDimensions.Y; v++) {
					for (std::int32_t u = 0; u < sheet.FrameDimensions.X; u++) {
						if (ReadSheetIndex(sheet, cellX + u, cellY + v, translucent) != 0) {
							minX = std::min<std::int32_t>(minX, u);
							minY = std::min<std::int32_t>(minY, v);
							maxX = std::max<std::int32_t>(maxX, u);
							maxY = std::max<std::int32_t>(maxY, v);
						}
					}
				}
				if (maxX < 0) {
					continue;
				}
				x = cellX + minX;
				y = cellY + minY;
				w = maxX - minX + 1;
				h = maxY - minY + 1;
				offsetX = minX;
				offsetY = minY;
			}

			if (w <= 0 || h <= 0 || x + w > sheet.Size.X || y + h > sheet.Size.Y) {
				continue;
			}

			frame.width = std::uint16_t(w);
			frame.height = std::uint16_t(h);
			frame._pixels = std::make_unique<std::uint8_t[]>(std::size_t(w) * h);
			bool translucent = false;
			for (std::int32_t v = 0; v < h; v++) {
				for (std::int32_t u = 0; u < w; u++) {
					frame._pixels[std::size_t(v) * w + u] = ReadSheetIndex(sheet, x + u, y + v, translucent);
				}
			}
			frame._transparent = translucent;

			// Every spot is a point of the cell, the frame has them as the original does - where the point lies in the frame,
			// negated. The cell has one coldspot and one gunspot, they keep their place in it.
			frame.hotSpotX = std::int16_t(offsetX - sheet.Hotspot.X);
			frame.hotSpotY = std::int16_t(offsetY - sheet.Hotspot.Y);
			if (sheet.Coldspot.X != ContentResolver::InvalidValue) {
				frame.coldSpotX = std::int16_t(offsetX - sheet.Coldspot.X);
				frame.coldSpotY = std::int16_t(offsetY - sheet.Coldspot.Y);
			}
			if (sheet.Gunspot.X != ContentResolver::InvalidValue) {
				frame.gunSpotX = std::int16_t(offsetX - sheet.Gunspot.X);
				frame.gunSpotY = std::int16_t(offsetY - sheet.Gunspot.Y);
			}
			// The frame is as the game has it, nothing to show
			frame._seenSpots[0] = frame.hotSpotX;
			frame._seenSpots[1] = frame.hotSpotY;
			frame._seenSpots[2] = frame.coldSpotX;
			frame._seenSpots[3] = frame.coldSpotY;
			frame._seenSpots[4] = frame.gunSpotX;
			frame._seenSpots[5] = frame.gunSpotY;
		}
	}

	void AnimationTables::Watch(jjANIMSET& set)
	{
		if (!set._watched) {
			set._watched = true;
			_watchedSets.push_back(&set);
		}
	}

	void AnimationTables::Watch(jjANIMATION& anim)
	{
		if (!anim._watched) {
			anim._watched = true;
			_watchedAnimations.push_back(&anim);
		}
	}

	void AnimationTables::Watch(jjANIMFRAME& frame)
	{
		if (!frame._watched) {
			frame._watched = true;
			_watchedFrames.push_back(&frame);
		}
	}

	std::uint64_t AnimationTables::GetSignature(const jjANIMATION& anim) const
	{
		// FNV-1a over what decides the image of the animation
		std::uint64_t hash = 14695981039346656037ull;
		auto mix = [&hash](std::uint64_t value) {
			hash ^= value;
			hash *= 1099511628211ull;
		};
		mix(anim.frameCount);
		mix(anim._firstFrame);
		mix(std::uint16_t(anim.fps));
		bool hasTrueColor = false;
		for (std::uint32_t i = 0; i < anim.frameCount; i++) {
			std::uint32_t index = anim._firstFrame + i;
			if (index < _frames.size()) {
				mix(_frames[index]->_version);
				hasTrueColor |= (_frames[index]->_colors != nullptr);
			} else {
				mix(UINT32_MAX);
			}
		}
		if (hasTrueColor) {
			// The 8-bit frames of true color graphics are in the colors of the palette (see ComposeGraphics())
			mix(_paletteVersion);
		}
		return hash;
	}

	GenericGraphicResource* AnimationTables::RequestNativeGraphics(NativeAnimation& native)
	{
		if (native.Graphics == nullptr && !native.Path.empty()) {
			// Indexed, as the actors request it, so this is the very same instance they draw
			native.Graphics = ContentResolver::Get().RequestGraphics(native.Path, 0, true);
		}
		return native.Graphics;
	}

	void AnimationTables::ShowInNativeGraphics(NativeAnimation& native, jjANIMATION& anim)
	{
		GenericGraphicResource* graphics = RequestNativeGraphics(native);
		if (graphics == nullptr) {
			return;
		}
		// Changed graphics can be true color, those of 32-bit frames
		if (native.Original == nullptr && (graphics->Flags & GenericGraphicResourceFlags::Indexed) != GenericGraphicResourceFlags::Indexed) {
			if (!_warnedTrueColor) {
				_warnedTrueColor = true;
				LOGW("Graphics \"{}\" aren't palette-indexed, scripts can't change them", native.Path);
			}
			return;
		}

		if (native.Original == nullptr) {
			auto original = std::make_unique<OriginalGraphics>();
			if (!ContentResolver::Get().ReadSpriteSheet(native.Path, original->Sheet, true)) {
				return;
			}
			original->FrameRects = graphics->FrameRects;
			native.Original = Death::move(original);
		}

		SmallVector<jjANIMFRAME*, 16> frames;
		GetFrames(anim, frames);
		ComposeGraphics(*graphics, ArrayView<jjANIMFRAME* const>(frames.data(), frames.size()), anim.fps, true);
	}

	void AnimationTables::RestoreNativeGraphics()
	{
		_nativeGraphicsRestored = true;
		for (NativeAnimation& native : _natives) {
			RestoreNativeGraphics(native);
		}
	}

	void AnimationTables::RestoreNativeGraphics(NativeAnimation& native)
	{
		if (native.Original == nullptr || native.Graphics == nullptr) {
			return;
		}
		// Once the level is left, the next one may have been loaded already, and it frees what it doesn't need - so they're
		// given back only if they're still there, a level that loads them again reads them as they were
		if (!ContentResolver::Get().IsGraphicsCached(native.Graphics, native.Path, 0, true)) {
			native.Original = nullptr;
			native.Graphics = nullptr;
			return;
		}

		const ContentResolver::SpriteSheet& sheet = native.Original->Sheet;
		std::size_t count = std::size_t(sheet.Size.X) * sheet.Size.Y;
		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(count * 2);
		for (std::size_t i = 0; i < count; i++) {
			const std::uint8_t* pixel = &sheet.Pixels[i * sheet.ChannelCount];
			pixels[i * 2] = pixel[0];
			switch (sheet.ChannelCount) {
				case 1: pixels[i * 2 + 1] = (pixel[0] != 0 ? 255 : 0); break;
				case 2: pixels[i * 2 + 1] = pixel[1]; break;
				default: pixels[i * 2 + 1] = pixel[3]; break;
			}
		}

		native.Graphics->FrameRects = native.Original->FrameRects;
		UploadGraphics(*native.Graphics, pixels.get(), sheet.Size, native.Path.data(), false);
		native.Original = nullptr;
	}

	GenericGraphicResource* AnimationTables::RequestOwnGraphics(jjANIMATION& anim)
	{
		std::uint64_t signature = GetSignature(anim);
		if (anim._graphics == nullptr || anim._graphicsSignature != signature) {
			if (anim._graphics == nullptr) {
				anim._graphics = std::make_unique<GenericGraphicResource>();
			}
			SmallVector<jjANIMFRAME*, 16> frames;
			GetFrames(anim, frames);
			ComposeGraphics(*anim._graphics, ArrayView<jjANIMFRAME* const>(frames.data(), frames.size()), anim.fps, false);
			anim._graphicsSignature = signature;
		}
		return (anim._graphics->FrameCount > 0 ? anim._graphics.get() : nullptr);
	}

	void AnimationTables::GetFrames(const jjANIMATION& anim, SmallVectorImpl<jjANIMFRAME*>& frames)
	{
		frames.clear();
		frames.reserve(anim.frameCount);
		for (std::uint32_t i = 0; i < anim.frameCount; i++) {
			std::uint32_t index = anim._firstFrame + i;
			// A frame out of the table is the empty first one
			jjANIMFRAME* frame = _frames[index < _frames.size() ? index : 0];
			EnsureFrameImage(*frame);
			frames.push_back(frame);
		}
	}

	void AnimationTables::ComposeGraphics(GenericGraphicResource& graphics, ArrayView<jjANIMFRAME* const> frames, std::int32_t fps, bool keepLayout)
	{
		constexpr std::int32_t Border = GenericGraphicResource::FrameBorder;

		std::int32_t cellCount;
		if (keepLayout) {
			cellCount = graphics.FrameCount;
		} else {
			// The cell fits the frames as the converter makes it, with the hotspot where they all meet
			std::int32_t normalizedX = 0, normalizedY = 0, largestX = 0, largestY = 0;
			for (const jjANIMFRAME* frame : frames) {
				normalizedX = std::max<std::int32_t>(normalizedX, -frame->hotSpotX);
				normalizedY = std::max<std::int32_t>(normalizedY, -frame->hotSpotY);
				largestX = std::max<std::int32_t>(largestX, frame->width + frame->hotSpotX);
				largestY = std::max<std::int32_t>(largestY, frame->height + frame->hotSpotY);
			}

			cellCount = std::int32_t(frames.size());
			graphics.Flags = GenericGraphicResourceFlags::Indexed;
			graphics.FrameCount = cellCount;
			graphics.FrameConfiguration = Vector2i(std::max<std::int32_t>(cellCount, 1), 1);
			graphics.FrameDimensions = Vector2i(normalizedX + largestX + Border * 2, normalizedY + largestY + Border * 2);
			graphics.Hotspot = Vector2i(normalizedX + Border, normalizedY + Border);
			graphics.AnimDuration = (fps > 0 ? 5.0f / fps : 0.0f);

			// One coldspot and one gunspot for the whole animation, those of the first frame
			graphics.Coldspot = Vector2i(ContentResolver::InvalidValue, ContentResolver::InvalidValue);
			graphics.Gunspot = Vector2i(ContentResolver::InvalidValue, ContentResolver::InvalidValue);
			if (!frames.empty()) {
				const jjANIMFRAME& first = *frames[0];
				if (first.coldSpotX != 0 || first.coldSpotY != 0) {
					graphics.Coldspot = graphics.Hotspot + Vector2i(first.hotSpotX - first.coldSpotX, first.hotSpotY - first.coldSpotY);
				}
				if (first.gunSpotX != 0 || first.gunSpotY != 0) {
					graphics.Gunspot = graphics.Hotspot + Vector2i(first.hotSpotX - first.gunSpotX, first.hotSpotY - first.gunSpotY);
				}
			}
		}

		// Every distinct frame gets a rectangle of the sheet, the cells repeat the frames
		SmallVector<const jjANIMFRAME*, 16> distinct;
		SmallVector<PackedRect, 16> rects;
		SmallVector<std::int32_t, 16> rectOfCell(std::max<std::int32_t>(cellCount, 0));
		for (std::int32_t i = 0; i < cellCount; i++) {
			if (frames.empty()) {
				rectOfCell[i] = -1;
				continue;
			}
			const jjANIMFRAME* frame = frames[std::size_t(i) % frames.size()];
			auto it = std::find(distinct.begin(), distinct.end(), frame);
			if (it == distinct.end()) {
				distinct.push_back(frame);
				bool hasImage = (frame->_pixels != nullptr);
				rects.push_back({ 0, 0, hasImage ? frame->width : 0, hasImage ? frame->height : 0 });
				rectOfCell[i] = std::int32_t(distinct.size() - 1);
			} else {
				rectOfCell[i] = std::int32_t(it - distinct.begin());
			}
		}

		std::int32_t width, height;
		if (!PackRows(rects, width, height)) {
			LOGW("Frames of an animation don't fit into a {}x{} sheet, they're left out", MaxSheetSize, MaxSheetSize);
			for (PackedRect& rect : rects) {
				rect.W = rect.H = 0;
			}
			width = height = 1;
		}

		// A sheet is either indexed or true color, a 32-bit frame makes it true color, its 8-bit frames take the colors
		// the palette has now
		bool trueColor = false;
		for (const jjANIMFRAME* frame : distinct) {
			if (frame->_colors != nullptr) {
				trueColor = true;
				break;
			}
		}

		// Palette index and alpha, as the converter has them, or RGBA
		const std::size_t bytesPerPixel = (trueColor ? 4 : 2);
		std::unique_ptr<std::uint8_t[]> pixels = std::make_unique<std::uint8_t[]>(std::size_t(width) * height * bytesPerPixel);
		std::memset(pixels.get(), 0, std::size_t(width) * height * bytesPerPixel);
		const std::uint32_t* palette = ContentResolver::Get().GetPalettes().data();
		for (std::size_t i = 0; i < distinct.size(); i++) {
			const jjANIMFRAME& frame = *distinct[i];
			const PackedRect& rect = rects[i];
			std::uint8_t alpha = (frame._transparent ? TranslucentAlpha : 255);
			for (std::int32_t v = 0; v < rect.H; v++) {
				std::uint8_t* row = &pixels[(std::size_t(rect.Y + v) * width + rect.X) * bytesPerPixel];
				const std::uint8_t* source = &frame._pixels[std::size_t(v) * frame.width];
				if (!trueColor) {
					for (std::int32_t u = 0; u < rect.W; u++) {
						row[u * 2] = source[u];
						row[u * 2 + 1] = (source[u] != 0 ? alpha : 0);
					}
				} else if (frame._colors != nullptr) {
					const jjPALCOLOR* colors = &frame._colors[std::size_t(v) * frame.width];
					for (std::int32_t u = 0; u < rect.W; u++) {
						row[u * 4] = colors[u].red;
						row[u * 4 + 1] = colors[u].green;
						row[u * 4 + 2] = colors[u].blue;
						row[u * 4 + 3] = std::uint8_t(std::uint32_t(colors[u].alpha) * alpha / 255);
					}
				} else {
					for (std::int32_t u = 0; u < rect.W; u++) {
						if (source[u] != 0) {
							std::uint32_t color = palette[source[u]];
							row[u * 4] = std::uint8_t(color);
							row[u * 4 + 1] = std::uint8_t(color >> 8);
							row[u * 4 + 2] = std::uint8_t(color >> 16);
							row[u * 4 + 3] = std::uint8_t(((color >> 24) & 0xFF) * alpha / 255);
						}
					}
				}
			}
		}

		// The frame keeps its own size and hotspot, which the offset in the cell says
		graphics.FrameRects.clear();
		graphics.FrameRects.reserve(std::max<std::int32_t>(cellCount, 1));
		for (std::int32_t i = 0; i < cellCount; i++) {
			Resources::FrameRect& target = graphics.FrameRects.emplace_back();
			if (rectOfCell[i] < 0) {
				target = { 0, 0, 0, 0, 0, 0 };
				continue;
			}
			const jjANIMFRAME& frame = *distinct[rectOfCell[i]];
			const PackedRect& rect = rects[rectOfCell[i]];
			target.X = std::uint16_t(rect.X);
			target.Y = std::uint16_t(rect.Y);
			target.W = std::uint16_t(rect.W);
			target.H = std::uint16_t(rect.H);
			target.OffsetX = std::int16_t(graphics.Hotspot.X + frame.hotSpotX);
			target.OffsetY = std::int16_t(graphics.Hotspot.Y + frame.hotSpotY);
		}
		if (graphics.FrameRects.empty()) {
			// GetFrameRect() falls back to the first one
			graphics.FrameRects.push_back({ 0, 0, 0, 0, 0, 0 });
		}

		UploadGraphics(graphics, pixels.get(), Vector2i(width, height), "JJ2+ animation", trueColor);
	}

	void AnimationTables::UploadGraphics(GenericGraphicResource& graphics, const std::uint8_t* pixels, Vector2i size, const char* name, bool trueColor)
	{
		// One bit per pixel, as ContentResolver makes it
		const std::size_t bytesPerPixel = (trueColor ? 4 : 2);
		std::size_t count = std::size_t(size.X) * size.Y;
		std::unique_ptr<std::uint8_t[]> mask = std::make_unique<std::uint8_t[]>((count + 7) / 8);
		std::memset(mask.get(), 0, (count + 7) / 8);
		for (std::size_t i = 0; i < count; i++) {
			if (pixels[i * bytesPerPixel + bytesPerPixel - 1] > MaskAlphaThreshold) {
				mask[i >> 3] |= std::uint8_t(1) << (i & 7);
			}
		}
		graphics.Mask = Death::move(mask);
		graphics.MaskStride = size.X;

		// The actors that draw the graphics switch their shader by it (see ActorBase::ActorRenderer::OnUpdate())
		if (trueColor) {
			graphics.Flags &= ~GenericGraphicResourceFlags::Indexed;
		} else {
			graphics.Flags |= GenericGraphicResourceFlags::Indexed;
		}

		if (ContentResolver::Get().IsHeadless()) {
			// No textures without a renderer, the collision mask is all
			return;
		}

		// (index, alpha) in RG8 or RGBA8 - the same texture object is initialized again, the renderers of the actors
		// keep a pointer to it
		const Texture::Format format = (trueColor ? Texture::Format::RGBA8 : Texture::Format::RG8);
		if (graphics.TextureDiffuse == nullptr) {
			graphics.TextureDiffuse = std::make_unique<Texture>(name, format, size.X, size.Y);
			graphics.TextureDiffuse->SetMinFiltering(SamplerFilter::Nearest);
			graphics.TextureDiffuse->SetMagFiltering(SamplerFilter::Nearest);
		} else {
			SamplerFilter minFiltering = graphics.TextureDiffuse->GetMinFiltering();
			SamplerFilter magFiltering = graphics.TextureDiffuse->GetMagFiltering();
			graphics.TextureDiffuse->Init(name, format, size.X, size.Y);
			graphics.TextureDiffuse->SetMinFiltering(minFiltering);
			graphics.TextureDiffuse->SetMagFiltering(magFiltering);
		}
		graphics.TextureDiffuse->LoadFromTexels(pixels, 0, 0, size.X, size.Y);
		if (trueColor) {
			graphics.TextureDiffuse->SetSwizzle(SwizzleChannel::Red, SwizzleChannel::Green, SwizzleChannel::Blue, SwizzleChannel::Alpha);
		} else {
			graphics.TextureDiffuse->SetSwizzle(SwizzleChannel::Red, SwizzleChannel::Green, SwizzleChannel::Blue, SwizzleChannel::Green);
		}
	}
}

#endif
