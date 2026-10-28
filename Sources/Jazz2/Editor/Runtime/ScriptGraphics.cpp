#include "ScriptGraphics.h"

#if defined(WITH_EDITOR) && defined(WITH_ANGELSCRIPT)

#include "../../ContentResolver.h"
#include "../../Events/EventMap.h"
#include "../../LevelHandler.h"
#include "../../PreferencesCache.h"
#include "../../Scripting/LevelScriptLoader.h"
#include "../../Scripting/Legacy/AnimationTables.h"
#include "../../Scripting/Legacy/Enums.h"
#include "../../Scripting/Legacy/EventTranslator.h"
#include "../../Scripting/Legacy/Objects.h"
#include "../../UI/Canvas.h"
#include "../../../nCine/Graphics/Texture.h"

#include <IO/MemoryStream.h>

#include <algorithm>

using namespace Death::IO;

namespace Jazz2::Editor
{
	namespace
	{
		// At most so many frames of an animation are taken, the sheet isn't wider than this
		constexpr std::uint32_t MaxFrames = 16;
		constexpr std::int32_t MaxSheetWidth = 4096;
		// At most so many other parameters of an event are tried for another look
		constexpr std::size_t MaxVariants = 16;
		// HANDLING::PICKUP of JJ2+
		constexpr std::uint32_t PlayerHandlingPickup = 5;

		bool IsGem(std::uint8_t eventId)
		{
			using namespace Scripting::Legacy;
			switch (eventId) {
				case aREDGEM: case aRECTREDGEM: case aREDGEMTEMP:
				case aGREENGEM: case aRECTGREENGEM:
				case aBLUEGEM: case aRECTBLUEGEM:
				case aPURPLEGEM:
				case aGEMRING:
					return true;
				default:
					return false;
			}
		}

		/** A level only for running its script until onLevelLoad() returns, with no players and nothing shown */
		class ScriptGraphicsLevelHandler : public LevelHandler
		{
		public:
			explicit ScriptGraphicsLevelHandler(IRootController* root)
				: LevelHandler(root)
			{
			}

			bool Initialize(const LevelDocument& document, StringView levelName, StringView fullPath)
			{
				MemoryStream s;
				if (!document.Save(s, true, 0)) {
					return false;
				}
				_levelData.assign(s.GetBuffer(), s.GetBuffer() + s.GetSize());
				_fullPath = fullPath;

				LevelInitialization levelInit(levelName, GameDifficulty::Normal, PreferencesCache::EnableReforgedGameplay, false, PlayerType::Jazz);
				bool result = LevelHandler::Initialize(levelInit);
				_levelData = SmallVector<std::uint8_t, 0>();
				return result;
			}

			Scripting::LevelScriptLoader* GetScripts() const
			{
				return _scripts.get();
			}

			bool IsSandbox() const override
			{
				return true;
			}

			// Nothing of it is heard
			std::shared_ptr<AudioBufferPlayer> PlaySfx(Actors::ActorBase* self, StringView identifier, AudioBuffer* buffer, const Vector3f& pos,
				bool sourceRelative, float gain, float pitch, SfxFlags flags) override
			{
				return nullptr;
			}

			std::shared_ptr<AudioBufferPlayer> PlayCommonSfx(StringView identifier, const Vector3f& pos, float gain, float pitch) override
			{
				return nullptr;
			}

			bool BeginPlayMusic(StringView path, bool setDefault, bool forceReload) override
			{
				return false;
			}

		protected:
			bool LoadLevel(LevelDescriptor& descriptor) override
			{
				MemoryStream s(_levelData.data(), std::int64_t(_levelData.size()));
				return ContentResolver::Get().TryLoadLevel(s, _fullPath, GetLevelLoadOptions(), descriptor);
			}

			void SpawnPlayers(const LevelInitialization& levelInit) override
			{
			}

			void OnInitialized() override
			{
				// Only the script, nothing of the level is played
				if (_scripts != nullptr) {
					_scripts->OnLevelLoad();
				}
			}

		private:
			SmallVector<std::uint8_t, 0> _levelData;
			String _fullPath;
		};
	}

	ScriptGraphics::ScriptGraphics()
	{
	}

	ScriptGraphics::~ScriptGraphics()
	{
	}

	bool ScriptGraphics::Capture(IRootController* root, const LevelDocument& document, StringView levelName, StringView fullPath)
	{
		Clear();
		auto& resolver = ContentResolver::Get();
		if (resolver.IsHeadless() || fullPath.empty()) {
			return false;
		}

		// The script may change the palette of the sprites, which the editor shares
		auto palettes = resolver.GetPalettes();
		std::uint32_t palette[ContentResolver::ColorsPerPalette];
		std::copy(palettes.begin(), palettes.begin() + ContentResolver::ColorsPerPalette, palette);
		// Loading this level would release what the level the editor shows uses, it requests nothing of it
		resolver.RetainLoadedResources();
		{
			auto handler = std::make_shared<ScriptGraphicsLevelHandler>(root);
			if (handler->Initialize(document, levelName, fullPath)) {
				Scripting::LevelScriptLoader* scripts = handler->GetScripts();
				Events::EventMap* eventMap = handler->EventMap();
				if (scripts != nullptr && eventMap != nullptr) {
					TakeSprites(*scripts, eventMap->GetSize());
				}
			}
		}
		resolver.ReleaseLoadedResources();
		resolver.SetSpritePalette(ArrayView<const std::uint32_t>(palette, ContentResolver::ColorsPerPalette));

		std::int32_t count = 0;
		for (const auto& sprite : _sprites) {
			count += (sprite != nullptr ? 1 : 0);
		}
		for (const Variant& variant : _variants) {
			count += (variant.Look != nullptr ? 1 : 0);
		}
		LOGI("The script of the level gives {} objects sprites of their own", count);
		return !IsEmpty();
	}

	std::uint32_t ScriptGraphics::MakeKey(std::uint8_t eventId, std::uint32_t params)
	{
		return (std::uint32_t(eventId) << Scripting::Legacy::EventTranslator::ParamBits) | params;
	}

	void ScriptGraphics::TakeSprites(Scripting::LevelScriptLoader& scripts, Vector2i levelSize)
	{
		using Scripting::Legacy::EventTranslator;

		// Only what the script asked for, the other templates are as the game has them - unless the animation they have
		// was given other frames without asking for them (sweets in place of the food)
		auto& tables = scripts.GetAnimationTables();
		bool scripted[256] = {};
		for (std::int32_t id = 1; id < 256; id++) {
			if (scripts.FindObjectPreset(std::uint8_t(id)) == nullptr) {
				std::uint32_t defaultAnim = scripts.GetDefaultPresetAnimation(std::uint8_t(id));
				if (defaultAnim == 0 || !tables.IsAnimationChanged(defaultAnim)) {
					continue;
				}
				scripts.GetObjectPreset(std::uint8_t(id))->Release();
			}
			scripted[id] = true;
		}

		// A behavior may look at the parameters of the event, so each of them is tried where it's in the level
		struct Placement
		{
			std::uint32_t Key;
			std::int32_t X, Y;
		};
		SmallVector<Placement, 0> placements;
		for (std::int32_t y = 0; y < levelSize.Y; y++) {
			for (std::int32_t x = 0; x < levelSize.X; x++) {
				std::uint32_t word = scripts.GetOriginalEvent(x, y);
				if (scripted[word & 0xFF]) {
					placements.push_back(Placement{MakeKey(std::uint8_t(word & 0xFF), word >> EventTranslator::ParamsShift), x, y});
				}
			}
		}
		std::stable_sort(placements.begin(), placements.end(), [](const Placement& a, const Placement& b) { return a.Key < b.Key; });
		placements.erase(std::unique(placements.begin(), placements.end(), [](const Placement& a, const Placement& b) { return a.Key == b.Key; }), placements.end());

		SmallVector<std::uint32_t, 0> colors;
		std::size_t next = 0;
		for (std::int32_t id = 1; id < 256; id++) {
			if (!scripted[id]) {
				continue;
			}
			std::size_t first = next;
			while (next < placements.size() && (placements[next].Key >> EventTranslator::ParamBits) == std::uint32_t(id)) {
				next++;
			}

			// A behavior may start the same animation from another frame by the parameters (a pole of each length),
			// the frame is chosen by them then, it isn't animated
			Look looks[MaxVariants + 1];
			bool found[MaxVariants + 1];
			std::size_t count = std::clamp<std::size_t>(next - first, 1, MaxVariants + 1);
			for (std::size_t i = 0; i < count; i++) {
				bool placed = (first + i < next);
				found[i] = GetLook(scripts, std::uint8_t(id), (placed ? placements[first + i].X : 0), (placed ? placements[first + i].Y : 0), looks[i]);
			}
			bool chosenFrame = false;
			for (std::size_t i = 0; i < count && !chosenFrame; i++) {
				for (std::size_t j = i + 1; j < count && !chosenFrame; j++) {
					chosenFrame = (found[i] && found[j] && looks[i].FirstFrame == looks[j].FirstFrame && looks[i].FrameCount == looks[j].FrameCount &&
						looks[i].Frame != looks[j].Frame);
				}
			}
			if (chosenFrame) {
				for (std::size_t i = 0; i < count; i++) {
					looks[i].FirstFrame = looks[i].Frame;
					looks[i].FrameCount = 1;
				}
			}

			// The look of the first parameters is the look of the event, the others are kept only if they look otherwise
			if (found[0]) {
				_sprites[id] = MakeSprite(scripts, std::uint8_t(id), looks[0], colors);
			}
			bool hasBase = (_sprites[id] != nullptr);
			for (std::size_t i = 1; i < count; i++) {
				if (!found[i]) {
					// It looks as the game has it with these
					if (hasBase) {
						_variants.push_back(Variant{placements[first + i].Key, nullptr});
					}
					continue;
				}
				if (hasBase && looks[i] == looks[0]) {
					continue;
				}
				if (auto sprite = MakeSprite(scripts, std::uint8_t(id), looks[i], colors)) {
					_variants.push_back(Variant{placements[first + i].Key, Death::move(sprite)});
				}
			}
		}

		if (_translator == nullptr) {
			_translator = std::make_unique<EventTranslator>();
		}
		for (std::int32_t id = 1; id < 256; id++) {
			if (_sprites[id] != nullptr) {
				_types.push_back(std::uint16_t(_translator->FromOriginal(std::uint8_t(id), 0).Type));
			}
		}
		for (const Variant& variant : _variants) {
			if (variant.Look != nullptr) {
				_types.push_back(std::uint16_t(_translator->FromOriginal(std::uint8_t(variant.Key >> EventTranslator::ParamBits), 0).Type));
			}
		}
		std::sort(_types.begin(), _types.end());
		_types.erase(std::unique(_types.begin(), _types.end()), _types.end());
	}

	bool ScriptGraphics::GetLook(Scripting::LevelScriptLoader& scripts, std::uint8_t eventId, std::int32_t tileX, std::int32_t tileY, Look& look)
	{
		// What a new object of the template looks like once its behavior started it - its animation from
		// the frame it shows, from the first one if the animation was given other frames, the frame
		// alone if the animation isn't one or the behavior draws a frame of its own choosing there (puppets
		// before the strings pull them)
		auto& tables = scripts.GetAnimationTables();
		Scripting::LevelScriptLoader::StartingLook start;
		if (!scripts.GetStartingLook(eventId, tileX, tileY, start) || (start.CurAnim <= 0 && start.CurFrame == 0 && start.DrawnFrame == 0)) {
			return false;
		}
		std::uint32_t anim = std::uint32_t(std::max<std::int32_t>(start.CurAnim, 0));
		std::uint32_t frame = start.CurFrame;
		std::uint32_t animFrameCount = 0, animFirstFrame = 0;
		bool animValid = (anim != 0 && tables.GetAnimationFrames(anim, animFrameCount, animFirstFrame) && animFrameCount > 0);
		auto isInAnim = [&](std::uint32_t f) { return (f >= animFirstFrame && f < animFirstFrame + animFrameCount); };
		if (animValid && !isInAnim(frame)) {
			frame = animFirstFrame;
		}
		if (start.DrawnFrame != 0) {
			if (animValid && !isInAnim(start.DrawnFrame)) {
				animValid = false;
			}
			frame = start.DrawnFrame;
		}
		if (frame == 0) {
			return false;
		}
		std::uint32_t defaultAnim = scripts.GetDefaultPresetAnimation(eventId);
		if (anim == defaultAnim && animValid && !tables.IsAnimationChanged(anim)) {
			return false;
		}

		look.Restyled = (anim == defaultAnim && animValid);
		// A script that keeps showing the frame it chose doesn't animate it (one that turns only to a player)
		if (start.Steady) {
			animValid = false;
		}
		look.FirstFrame = (animValid ? animFirstFrame : frame);
		look.FrameCount = (animValid ? std::int32_t(std::min<std::uint32_t>(animFrameCount, MaxFrames)) : 1);
		look.Frame = frame;
		return true;
	}

	std::unique_ptr<ScriptGraphics::Sprite> ScriptGraphics::MakeSprite(Scripting::LevelScriptLoader& scripts, std::uint8_t eventId, const Look& look,
		SmallVectorImpl<std::uint32_t>& colors)
	{
		auto& tables = scripts.GetAnimationTables();
		Scripting::Legacy::jjOBJ* preset = scripts.FindObjectPreset(eventId);
		if (preset == nullptr) {
			return nullptr;
		}

		// Gems are drawn in the colors of their gradient (SPRITE::GEM), as the game draws their objects
		const std::uint32_t* palette = nullptr;
		std::uint32_t gemPalette[ContentResolver::ColorsPerPalette];
		if (IsGem(eventId)) {
			// The gems of a ring are red
			std::int32_t gem = (eventId == Scripting::Legacy::aGEMRING ? 0 : preset->get_var(0) - 1);
			if (gem >= 0 && gem < 256) {
				gemPalette[0] = 0;
				for (std::int32_t i = 1; i < ContentResolver::ColorsPerPalette; i++) {
					gemPalette[i] = ContentResolver::Get().GetGemColor(std::uint8_t(gem), std::uint8_t(i));
				}
				palette = gemPalette;
			}
		}

		// All frames of the animation, still at the one it starts with
		Vector2i cellSize, hotspot;
		bool changed;
		if (!tables.GetFramesImage(look.FirstFrame, look.FrameCount, palette, colors, cellSize, hotspot, changed) || cellSize.X <= 0 || cellSize.Y <= 0 ||
			cellSize.X > MaxSheetWidth) {
			return nullptr;
		}
		std::int32_t shownFrames = std::clamp(MaxSheetWidth / cellSize.X, 1, look.FrameCount);
		std::int32_t width = cellSize.X * shownFrames;
		if (shownFrames < look.FrameCount) {
			std::int32_t stride = cellSize.X * look.FrameCount;
			for (std::int32_t y = 1; y < cellSize.Y; y++) {
				std::copy_n(&colors[std::size_t(y) * std::size_t(stride)], width, &colors[std::size_t(y) * std::size_t(width)]);
			}
		}
		std::int32_t stillFrame = std::min<std::int32_t>(std::int32_t(look.Frame - look.FirstFrame), shownFrames - 1);
		// A behavior of JJ2 shows the next frame every 8 ticks, a pickup every 4, in the time of the animations of the editor
		// (60 frames in a second, a tick is 60/70 of a frame)
		float ticksPerFrame = (preset->get_playerHandling() == PlayerHandlingPickup ? 4.0f : 8.0f);
		float animDuration = float(shownFrames) * ticksPerFrame * (60.0f / 70.0f) * UI::Canvas::AnimTimeMultiplier;

		auto sprite = std::make_unique<Sprite>();
		sprite->Restyled = look.Restyled;
		sprite->Base = std::make_unique<Resources::GenericGraphicResource>();
		Resources::GenericGraphicResource& base = *sprite->Base;
		base.TextureDiffuse = std::make_unique<Texture>("EditorScriptSprite", Texture::Format::RGBA8, width, cellSize.Y);
		// In pixels, as the sprites of the game
		base.TextureDiffuse->SetMinFiltering(SamplerFilter::Nearest);
		base.TextureDiffuse->SetMagFiltering(SamplerFilter::Nearest);
		base.TextureDiffuse->SetWrap(SamplerWrapping::ClampToEdge);
		base.TextureDiffuse->LoadFromTexels(reinterpret_cast<const std::uint8_t*>(colors.data()), 0, 0, width, cellSize.Y);
		base.FrameDimensions = cellSize;
		base.FrameConfiguration = Vector2i(shownFrames, 1);
		base.FrameCount = shownFrames;
		base.AnimDuration = animDuration;
		base.Hotspot = hotspot;

		Resources::GraphicResource& animated = sprite->Animated;
		animated.Base = &base;
		animated.State = AnimState::Idle;
		animated.AnimDuration = animDuration;
		animated.FrameCount = shownFrames;
		animated.FrameOffset = 0;
		animated.LoopMode = AnimationLoopMode::Loop;
		Resources::GraphicResource& still = sprite->Still;
		still.Base = &base;
		still.State = AnimState::Idle;
		still.AnimDuration = 0.0f;
		still.FrameCount = 1;
		still.FrameOffset = stillFrame;
		still.LoopMode = AnimationLoopMode::Once;
		return sprite;
	}

	void ScriptGraphics::Clear()
	{
		for (auto& sprite : _sprites) {
			sprite = nullptr;
		}
		_variants.clear();
		_types.clear();
	}

	bool ScriptGraphics::IsEmpty() const
	{
		return _types.empty();
	}

	ScriptGraphics::Sprite* ScriptGraphics::FindSprite(const Formats::EventRecord& event)
	{
		if (_types.empty() || !std::binary_search(_types.begin(), _types.end(), event.Type) || _translator == nullptr) {
			return nullptr;
		}
		std::uint32_t word = _translator->ToOriginal(EventType(event.Type), event.Params);
		std::uint8_t id = std::uint8_t(word & 0xFF);
		if (!_variants.empty()) {
			std::uint32_t key = MakeKey(id, word >> Scripting::Legacy::EventTranslator::ParamsShift);
			auto it = std::lower_bound(_variants.begin(), _variants.end(), key, [](const Variant& variant, std::uint32_t key) { return variant.Key < key; });
			if (it != _variants.end() && it->Key == key) {
				return it->Look.get();
			}
		}
		return _sprites[id].get();
	}

	Resources::GraphicResource* ScriptGraphics::Find(const Formats::EventRecord& event, bool animated, bool ownAnimated)
	{
		Sprite* sprite = FindSprite(event);
		if (sprite == nullptr) {
			return nullptr;
		}
		bool plays = (animated && (ownAnimated || !sprite->Restyled));
		return (plays ? &sprite->Animated : &sprite->Still);
	}
}

#endif
