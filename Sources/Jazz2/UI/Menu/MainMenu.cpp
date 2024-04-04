#include "MainMenu.h"
#include "MenuResources.h"
#include "../../PreferencesCache.h"
#include "../ControlScheme.h"
#include "BeginSection.h"
#include "FirstRunSection.h"

#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
#	include "../DiscordRpcClient.h"
#endif

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/RenderQueue.h"
#include "../../../nCine/Audio/AudioReaderMpt.h"
#include "../../../nCine/Base/Random.h"

#include <Containers/StringConcatenable.h>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	MainMenu::MainMenu(IRootController* root, bool afterIntro)
		: _root(root), _activeCanvas(ActiveCanvas::Background), _transitionWhite(afterIntro ? 1.0f : 0.0f),
			_logoTransition(0.0f), _texturedBackgroundPass(this), _texturedBackgroundPhase(0.0f),
			_pressedKeys((uint32_t)KeySym::COUNT), _pressedActions(0), _touchButtonsTimer(0.0f)
	{
		theApplication().gfxDevice().setWindowTitle("Jazz² Resurrection"_s);

		_texturedBackgroundLayer.Visible = false;
		_canvasBackground = std::make_unique<MenuBackgroundCanvas>(this);
		_canvasClipped = std::make_unique<MenuClippedCanvas>(this);
		_canvasOverlay = std::make_unique<MenuOverlayCanvas>(this);

		auto& resolver = ContentResolver::Get();
		resolver.BeginLoading();

		// It will also replace palette for subsequent `RequestMetadata()`
		PrepareTexturedBackground();

		_metadata = resolver.RequestMetadata("UI/MainMenu"_s);
		ASSERT_MSG(_metadata != nullptr, "Cannot load required metadata");

		_smallFont = resolver.GetFont(FontType::Small);
		_mediumFont = resolver.GetFont(FontType::Medium);

		PlayMenuMusic();

		resolver.EndLoading();

		// Mark Fire and Menu button as already pressed to avoid some issues
		_pressedActions = (1 << (int32_t)PlayerActions::Fire) | (1 << ((int32_t)PlayerActions::Fire + 16)) |
			(1 << (int32_t)PlayerActions::Menu) | (1 << ((int32_t)PlayerActions::Menu + 16));

		SwitchToSection<BeginSection>();

#if !defined(DEATH_TARGET_EMSCRIPTEN)
		bool isPlayable = ((_root->GetFlags() & IRootController::Flags::IsPlayable) == IRootController::Flags::IsPlayable);
		if (PreferencesCache::FirstRun && isPlayable) {
			SwitchToSection<FirstRunSection>();
		}
#endif

		UpdateRichPresence();
	}

	MainMenu::~MainMenu()
	{
		_canvasBackground->setParent(nullptr);
		_canvasClipped->setParent(nullptr);
		_canvasOverlay->setParent(nullptr);
	}

	void MainMenu::Reset()
	{
		bool shouldSwitch = false;
		while (!_sections.empty()) {
			if (_sections.size() == 1 && dynamic_cast<BeginSection*>(_sections.back().get())) {
				if (shouldSwitch) {
					auto& lastSection = _sections.back();
					lastSection->OnShow(this);

					if (_contentBounds != Recti::Empty) {
						Recti clipRectangle = lastSection->GetClipRectangle(_contentBounds);
						_upscalePass.SetClipRectangle(clipRectangle);
					}
				}
				return;
			}

			_sections.pop_back();
			shouldSwitch = true;
		}
		
		SwitchToSection<BeginSection>();
	}

	void MainMenu::OnBeginFrame()
	{
		float timeMult = theApplication().timeMult();

		UpdatePressedActions();
		UpdateDebris(timeMult);

		// Destroy stopped players
		for (int32_t i = (int32_t)_playingSounds.size() - 1; i >= 0; i--) {
			if (_playingSounds[i]->state() == IAudioPlayer::PlayerState::Stopped) {
				_playingSounds.erase(&_playingSounds[i]);
			} else {
				break;
			}
		}

		_texturedBackgroundPos.X += timeMult * 1.2f;
		_texturedBackgroundPos.Y += timeMult * -0.2f + timeMult * sinf(_texturedBackgroundPhase) * 0.6f;
		_texturedBackgroundPhase += timeMult * 0.001f;

		if (_transitionWhite > 0.0f) {
			_transitionWhite -= 0.02f * timeMult;
			if (_transitionWhite < 0.0f) {
				_transitionWhite = 0.0f;
			}
		}
		if (_logoTransition < 1.0f) {
			_logoTransition += timeMult * 0.04f;
			if (_logoTransition > 1.0f) {
				_logoTransition = 1.0f;
			}
		}
		if (_touchButtonsTimer > 0.0f) {
			_touchButtonsTimer -= timeMult;
		}

		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			lastSection->OnUpdate(timeMult);
		}
	}

	void MainMenu::OnInitializeViewport(int32_t width, int32_t height)
	{
		constexpr float defaultRatio = (float)DefaultWidth / DefaultHeight;
		float currentRatio = (float)width / height;

		int32_t w, h;
		if (currentRatio > defaultRatio) {
			w = std::min(DefaultWidth, width);
			h = (int32_t)(w / currentRatio);
		} else if (currentRatio < defaultRatio) {
			h = std::min(DefaultHeight, height);
			w = (int32_t)(h * currentRatio);
		} else {
			w = std::min(DefaultWidth, width);
			h = std::min(DefaultHeight, height);
		}

		_upscalePass.Initialize(w, h, width, height);
		UpdateContentBounds(Vector2i(w, h));

		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			Recti clipRectangle = lastSection->GetClipRectangle(_contentBounds);
			_upscalePass.SetClipRectangle(clipRectangle);
		}

		// Viewports must be registered in reverse order
		_upscalePass.Register();

		_canvasBackground->setParent(_upscalePass.GetNode());
		_canvasClipped->setParent(_upscalePass.GetClippedNode());
		_canvasOverlay->setParent(_upscalePass.GetOverlayNode());

		_texturedBackgroundPass.Initialize();
	}

	void MainMenu::OnKeyPressed(const KeyboardEvent& event)
	{
		_pressedKeys.Set((uint32_t)event.sym);
	}

	void MainMenu::OnKeyReleased(const KeyboardEvent& event)
	{
		_pressedKeys.Reset((uint32_t)event.sym);
	}

	void MainMenu::OnTouchEvent(const nCine::TouchEvent& event)
	{
		if (!_sections.empty()) {
			_touchButtonsTimer = 1200.0f;

			auto& lastSection = _sections.back();
			lastSection->OnTouchEvent(event, _canvasBackground->ViewSize);
		}
	}

	bool MainMenu::MenuBackgroundCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_upscalePass.GetViewSize();

		_owner->_activeCanvas = ActiveCanvas::Background;

		if (PreferencesCache::EnableReforgedMainMenu || !_owner->RenderLegacyBackground(renderQueue)) {
			_owner->RenderTexturedBackground(renderQueue);
		}

		Vector2i center = ViewSize / 2;
		int32_t charOffset = 0;
		int32_t charOffsetShadow = 0;

		float titleY = _owner->_contentBounds.Y - 30.0f;
		float logoScale = 1.0f + (1.0f - _owner->_logoTransition) * 7.0f;
		float logoTextScale = 1.0f + (1.0f - _owner->_logoTransition) * 2.0f;
		float logoTranslateX = 1.0f + (1.0f - _owner->_logoTransition) * 1.2f;
		float logoTranslateY = (1.0f - _owner->_logoTransition) * 120.0f;
		float logoTextTranslate = (1.0f - _owner->_logoTransition) * 60.0f;

		if (_owner->_touchButtonsTimer > 0.0f && _owner->_sections.size() >= 2) {
			_owner->DrawElement(MenuLineArrow, -1, static_cast<float>(center.X), titleY - 30.0f, ShadowLayer, Alignment::Center, Colorf::White);
		}

		// Title
		_owner->DrawElement(MenuCarrot, -1, center.X - 76.0f * logoTranslateX, titleY - 6.0f + logoTranslateY + 2.0f, ShadowLayer + 200, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.8f * logoScale, 0.8f * logoScale);
		_owner->DrawElement(MenuCarrot, -1, center.X - 76.0f * logoTranslateX, titleY - 6.0f + logoTranslateY, MainLayer + 200, Alignment::Center, Colorf::White, 0.8f * logoScale, 0.8f * logoScale);

		_owner->_mediumFont->DrawString(this, "Jazz"_s, charOffsetShadow, center.X - 63.0f, titleY + logoTranslateY + 2.0f, FontShadowLayer + 200,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.75f * logoTextScale, 1.65f, 3.0f, 3.0f, 0.0f, 0.92f);
		_owner->_mediumFont->DrawString(this, "2"_s, charOffsetShadow, center.X - 19.0f, titleY - 8.0f + logoTranslateY + 2.0f, FontShadowLayer + 200,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.5f * logoTextScale, 0.0f, 0.0f, 0.0f, 0.0f);
		_owner->_mediumFont->DrawString(this, "Resurrection"_s, charOffsetShadow, center.X - 10.0f, titleY + 4.0f + logoTranslateY + 2.5f, FontShadowLayer + 200,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.5f * logoTextScale, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);

		_owner->_mediumFont->DrawString(this, "Jazz"_s, charOffset, center.X - 63.0f * logoTranslateX + logoTextTranslate, titleY + logoTranslateY, FontLayer + 200,
			Alignment::Left, Colorf(0.54f, 0.44f, 0.34f, 0.5f), 0.75f * logoTextScale, 1.65f, 3.0f, 3.0f, 0.0f, 0.92f);
		_owner->_mediumFont->DrawString(this, "2"_s, charOffset, center.X - 19.0f * logoTranslateX + logoTextTranslate, titleY - 8.0f + logoTranslateY, FontLayer + 200,
			Alignment::Left, Colorf(0.54f, 0.44f, 0.34f, 0.5f), 0.5f * logoTextScale, 0.0f, 0.0f, 0.0f, 0.0f);
		_owner->_mediumFont->DrawString(this, "Resurrection"_s, charOffset, center.X - 10.0f * logoTranslateX + logoTextTranslate, titleY + 4.0f + logoTranslateY, FontLayer + 200,
			Alignment::Left, Colorf(0.6f, 0.42f, 0.42f, 0.5f), 0.5f * logoTextScale, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);

		// Version
		Vector2f bottomRight = Vector2f(ViewSize.X, ViewSize.Y);
		bottomRight.X = ViewSize.X - 24.0f;
		bottomRight.Y -= 10.0f;

		auto newestVersion = _owner->_root->GetNewestVersion();
		if (!newestVersion.empty() && newestVersion != NCINE_VERSION) {
			String newerVersion = "v" NCINE_VERSION "  › \f[c:0x9e7056]v" + newestVersion;
			_owner->DrawStringShadow(newerVersion, charOffset, bottomRight.X, bottomRight.Y, IMenuContainer::FontLayer,
				Alignment::BottomRight, Font::DefaultColor, 0.7f, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);
		} else {
			_owner->DrawStringShadow("v" NCINE_VERSION, charOffset, bottomRight.X, bottomRight.Y, IMenuContainer::FontLayer,
				Alignment::BottomRight, Font::DefaultColor, 0.7f, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);
		}

		// Copyright
		Vector2f bottomLeft = bottomRight;
		bottomLeft.X = 24.0f;
		_owner->DrawStringShadow("© 2016-" NCINE_BUILD_YEAR "  Dan R."_s, charOffset, bottomLeft.X, bottomLeft.Y, IMenuContainer::FontLayer,
			Alignment::BottomLeft, Font::DefaultColor, 0.7f, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);

#if defined(DEATH_TARGET_WINDOWS_RT)
		// Show active external drive indicator on Xbox
		StringView sourcePath = ContentResolver::Get().GetSourcePath();
		if (!sourcePath.empty() && sourcePath.hasPrefix("\\\\?\\"_s) && sourcePath.exceptPrefix(5).hasPrefix(":\\Games\\"_s)) {
			_owner->DrawStringShadow("·"_s, charOffset, bottomLeft.X + 116.0f, bottomLeft.Y, IMenuContainer::FontLayer,
				Alignment::BottomLeft, Font::DefaultColor, 0.7f, 0.4f, 0.0f, 0.8f, 0.46f, 0.8f);
			_owner->DrawElement(Storage, -1, bottomLeft.X + 146.0f, bottomLeft.Y - 3.0f, IMenuContainer::FontLayer, Alignment::BottomRight, Colorf(1.0f, 1.0f, 1.0f, std::max(_owner->_logoTransition - 0.6f, 0.0f) * 2.5f));
			_owner->DrawStringShadow(sourcePath.slice(4, 7), charOffset, bottomLeft.X + 150.0f, bottomLeft.Y, IMenuContainer::FontLayer,
				Alignment::BottomLeft, Font::DefaultColor, 0.7f, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);
		}
#endif

		if (!_owner->_sections.empty()) {
			auto& lastSection = _owner->_sections.back();
			lastSection->OnDraw(this);
		}

		return true;
	}

	bool MainMenu::MenuClippedCanvas::OnDraw(RenderQueue& renderQueue)
	{
		if (_owner->_sections.empty()) {
			return false;
		}

		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_upscalePass.GetViewSize();

		_owner->_activeCanvas = ActiveCanvas::Clipped;

		auto& lastSection = _owner->_sections.back();
		lastSection->OnDrawClipped(this);

		return true;
	}

	bool MainMenu::MenuOverlayCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_upscalePass.GetViewSize();

		_owner->_activeCanvas = ActiveCanvas::Overlay;

		if (!_owner->_sections.empty()) {
			auto& lastSection = _owner->_sections.back();
			lastSection->OnDrawOverlay(this);
		}

		_owner->DrawDebris(renderQueue);

		if (_owner->_transitionWhite > 0.0f) {
			DrawSolid(Vector2f::Zero, 950, Vector2f(static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y)), Colorf(1.0f, 1.0f, 1.0f, _owner->_transitionWhite));
		}

		return true;
	}

	MenuSection* MainMenu::SwitchToSectionDirect(std::unique_ptr<MenuSection> section)
	{
		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			lastSection->OnHide();
		}

		auto& currentSection = _sections.emplace_back(std::move(section));
		currentSection->OnShow(this);

		if (_contentBounds != Recti::Empty) {
			Recti clipRectangle = currentSection->GetClipRectangle(_contentBounds);
			_upscalePass.SetClipRectangle(clipRectangle);
		}

		return currentSection.get();
	}

	void MainMenu::LeaveSection()
	{
		if (_sections.empty()) {
			return;
		}

		_sections.pop_back();

		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			lastSection->OnShow(this);

			if (_contentBounds != Recti::Empty) {
				Recti clipRectangle = lastSection->GetClipRectangle(_contentBounds);
				_upscalePass.SetClipRectangle(clipRectangle);
			}
		}
	}

	MenuSection* MainMenu::GetUnderlyingSection() const
	{
		std::size_t count = _sections.size();
		return (count >= 2 ? _sections[count - 2].get() : nullptr);
	}

	void MainMenu::ChangeLevel(LevelInitialization&& levelInit)
	{
		_root->ChangeLevel(std::move(levelInit));
	}

	bool MainMenu::HasResumableState() const
	{
		return _root->HasResumableState();
	}

	void MainMenu::ResumeSavedState()
	{
		_root->ResumeSavedState();
	}

#if defined(WITH_MULTIPLAYER)
	bool MainMenu::ConnectToServer(const StringView address, std::uint16_t port)
	{
		return _root->ConnectToServer(address, port);
	}

	bool MainMenu::CreateServer(LevelInitialization&& levelInit, std::uint16_t port)
	{
		return _root->CreateServer(std::move(levelInit), port);
	}
#endif

	void MainMenu::ApplyPreferencesChanges(ChangedPreferencesType type)
	{
		if ((type & ChangedPreferencesType::Graphics) == ChangedPreferencesType::Graphics) {
			Viewport::chain().clear();
			Vector2i res = theApplication().resolution();
			OnInitializeViewport(res.X, res.Y);
		}

		if ((type & ChangedPreferencesType::Audio) == ChangedPreferencesType::Audio) {
			if (_music != nullptr) {
				_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			}
		}

		if ((type & ChangedPreferencesType::Language) == ChangedPreferencesType::Language) {
			// All sections have to be recreated to load new language
			_sections.clear();
			SwitchToSection<BeginSection>();
		}

		if ((type & ChangedPreferencesType::ControlScheme) == ChangedPreferencesType::ControlScheme) {
			// Mark all buttons as already pressed to avoid some issues
			_pressedActions = 0xffff | (0xffff << 16);
		}

		if ((type & ChangedPreferencesType::MainMenu) == ChangedPreferencesType::MainMenu) {
			PlayMenuMusic();
		}
	}

	void MainMenu::DrawElement(AnimState state, int32_t frame, float x, float y, uint16_t z, Alignment align, const Colorf& color, float scaleX, float scaleY, bool additiveBlending, bool unaligned)
	{
		auto* res = _metadata->FindAnimation(state);
		if (res == nullptr) {
			return;
		}

		if (frame < 0) {
			frame = res->FrameOffset + ((int32_t)(_canvasBackground->AnimTime * res->FrameCount / res->AnimDuration) % res->FrameCount);
		}

		Canvas* currentCanvas = GetActiveCanvas();
		GenericGraphicResource* base = res->Base;
		Vector2f size = Vector2f(base->FrameDimensions.X * scaleX, base->FrameDimensions.Y * scaleY);
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);
		if (!unaligned) {
			adjustedPos.X = std::round(adjustedPos.X);
			adjustedPos.Y = std::round(adjustedPos.Y);
		}

		Vector2i texSize = base->TextureDiffuse->size();
		int32_t col = frame % base->FrameConfiguration.X;
		int32_t row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			float(base->FrameDimensions.X) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			float(base->FrameDimensions.Y) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);
		
		currentCanvas->DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, additiveBlending);
	}

	void MainMenu::DrawElement(AnimState state, float x, float y, uint16_t z, Alignment align, const Colorf& color, const Vector2f& size, const Vector4f& texCoords, bool unaligned)
	{
		auto* res = _metadata->FindAnimation(state);
		if (res == nullptr) {
			return;
		}

		Canvas* currentCanvas = GetActiveCanvas();
		GenericGraphicResource* base = res->Base;
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);
		if (!unaligned) {
			adjustedPos.X = std::round(adjustedPos.X);
			adjustedPos.Y = std::round(adjustedPos.Y);
		}

		currentCanvas->DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, false);
	}

	void MainMenu::DrawSolid(float x, float y, uint16_t z, Alignment align, const Vector2f& size, const Colorf& color, bool additiveBlending)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);
		adjustedPos.X = std::round(adjustedPos.X);
		adjustedPos.Y = std::round(adjustedPos.Y);

		currentCanvas->DrawSolid(adjustedPos, z, size, color, additiveBlending);
	}

	void MainMenu::DrawTexture(const Texture& texture, float x, float y, uint16_t z, Alignment align, const Vector2f& size, const Colorf& color, bool unaligned)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);
		if (!unaligned) {
			adjustedPos.X = std::round(adjustedPos.X);
			adjustedPos.Y = std::round(adjustedPos.Y);
		}

		currentCanvas->DrawTexture(texture, adjustedPos, z, size, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), color);
	}

	Vector2f MainMenu::MeasureString(const StringView text, float scale, float charSpacing, float lineSpacing)
	{
		return _smallFont->MeasureString(text, scale, charSpacing, lineSpacing);
	}

	void MainMenu::DrawStringShadow(const StringView text, int32_t& charOffset, float x, float y, uint16_t z, Alignment align, const Colorf& color, float scale,
		float angleOffset, float varianceX, float varianceY, float speed, float charSpacing, float lineSpacing)
	{
		if (_logoTransition < 1.0f) {
			float transitionText = (1.0f - _logoTransition) * 2.0f;
			angleOffset += 0.5f * transitionText;
			varianceX += 400.0f * transitionText;
			varianceY += 400.0f * transitionText;
			speed -= speed * 0.6f * transitionText;
		}

		Canvas* currentCanvas = GetActiveCanvas();
		int32_t charOffsetShadow = charOffset;
		_smallFont->DrawString(currentCanvas, text, charOffsetShadow, x, y + 2.8f * scale, FontShadowLayer,
			align, Colorf(0.0f, 0.0f, 0.0f, 0.29f), scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
		_smallFont->DrawString(currentCanvas, text, charOffset, x, y, z,
			align, color, scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
	}

	void MainMenu::PlaySfx(const StringView identifier, float gain)
	{
		auto it = _metadata->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _metadata->Sounds.end()) {
			int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (int32_t)it->second.Buffers.size()) : 0);
			auto& player = _playingSounds.emplace_back(std::make_shared<AudioBufferPlayer>(&it->second.Buffers[idx]->Buffer));
			player->setPosition(Vector3f(0.0f, 0.0f, 100.0f));
			player->setGain(gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
			player->setSourceRelative(true);

			player->play();
		} else {
			LOGE("Sound effect \"%s\" was not found", identifier.data());
		}
	}

	bool MainMenu::ActionPressed(PlayerActions action)
	{
		return ((_pressedActions & (1 << (int32_t)action)) == (1 << (int32_t)action));
	}

	bool MainMenu::ActionHit(PlayerActions action)
	{
		return ((_pressedActions & ((1 << (int32_t)action) | (1 << (16 + (int32_t)action)))) == (1 << (int32_t)action));
	}

	void MainMenu::PlayMenuMusic()
	{
#if defined(WITH_OPENMPT)
		auto& resolver = ContentResolver::Get();

		if (PreferencesCache::EnableReforgedMainMenu) {
			_music = resolver.GetMusic(Random().NextBool() ? "bonus2.j2b"_s : "bonus3.j2b"_s);
		} else {
			_music = nullptr;
		}

;		if (_music == nullptr) {
			_music = resolver.GetMusic("menu.j2b"_s);
		}

		if (_music != nullptr) {
			_music->setLooping(true);
			_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			_music->setSourceRelative(true);
			_music->play();
		}
#endif
	}

	void MainMenu::UpdateContentBounds(Vector2i viewSize)
	{
		float titleY = std::clamp((200.0f * viewSize.Y / viewSize.X) - 40.0f, 30.0f, 70.0f);
		_contentBounds = Recti(0, titleY + 30, viewSize.X, viewSize.Y - (titleY + 30));
	}

	void MainMenu::UpdatePressedActions()
	{
		auto& input = theApplication().inputManager();
		_pressedActions = ((_pressedActions & 0xFFFF) << 16);

		const JoyMappedState* joyStates[UI::ControlScheme::MaxConnectedGamepads];
		std::int32_t joyStatesCount = 0;
		for (std::int32_t i = 0; i < IInputManager::MaxNumJoysticks && joyStatesCount < countof(joyStates); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[joyStatesCount++] = &input.joyMappedState(i);
			}
		}

		bool allowGamepads = true;
		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			allowGamepads = lastSection->IsGamepadNavigationEnabled();
		}

		_pressedActions |= ControlScheme::FetchNativation(0, _pressedKeys, ArrayView(joyStates, joyStatesCount), allowGamepads);
	}

	void MainMenu::UpdateRichPresence()
	{
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		if (!PreferencesCache::EnableDiscordIntegration || !DiscordRpcClient::Get().IsSupported()) {
			return;
		}

		DiscordRpcClient::RichPresence richPresence;
		richPresence.State = "Resting in main menu"_s;
		richPresence.LargeImage = "main-transparent"_s;
		DiscordRpcClient::Get().SetRichPresence(richPresence);
#endif
	}

	void MainMenu::UpdateDebris(float timeMult)
	{
		if (_preset == Preset::Xmas && PreferencesCache::EnableReforgedMainMenu) {
			int32_t weatherIntensity = Random().Fast(0, (int32_t)(3 * timeMult) + 1);
			for (int32_t i = 0; i < weatherIntensity; i++) {
				Vector2i viewSize = _canvasOverlay->ViewSize;
				Vector2f debrisPos = Vector2f(Random().FastFloat(viewSize.X * -0.3f, viewSize.X * 1.3f),
					Random().NextFloat(viewSize.Y * -0.5f, viewSize.Y * 0.5f));

				auto* res = _metadata->FindAnimation((AnimState)1); // Snow
				if (res != nullptr) {
					auto& resBase = res->Base;
					Vector2i texSize = resBase->TextureDiffuse->size();
					float scale = Random().FastFloat(0.4f, 1.1f);
					float speedX = Random().FastFloat(-1.6f, -1.2f) * scale;
					float speedY = Random().FastFloat(3.0f, 4.0f) * scale;
					float accel = Random().FastFloat(-0.008f, 0.008f) * scale;

					TileMap::DestructibleDebris debris = { };
					debris.Pos = debrisPos;
					debris.Depth = MainLayer - 100 + 200 * scale;
					debris.Size = Vector2f(resBase->FrameDimensions.X, resBase->FrameDimensions.Y);
					debris.Speed = Vector2f(speedX, speedY);
					debris.Acceleration = Vector2f(accel, std::abs(accel));

					debris.Scale = scale;
					debris.ScaleSpeed = 0.0f;
					debris.Angle = Random().FastFloat(0.0f, fTwoPi);
					debris.AngleSpeed = speedX * 0.02f;
					debris.Alpha = 1.0f;
					debris.AlphaSpeed = 0.0f;

					debris.Time = 160.0f;

					int32_t curAnimFrame = res->FrameOffset + Random().Next(0, res->FrameCount);
					int32_t col = curAnimFrame % resBase->FrameConfiguration.X;
					int32_t row = curAnimFrame / resBase->FrameConfiguration.X;
					debris.TexScaleX = (float(resBase->FrameDimensions.X) / float(texSize.X));
					debris.TexBiasX = (float(resBase->FrameDimensions.X * col) / float(texSize.X));
					debris.TexScaleY = (float(resBase->FrameDimensions.Y) / float(texSize.Y));
					debris.TexBiasY = (float(resBase->FrameDimensions.Y * row) / float(texSize.Y));

					debris.DiffuseTexture = resBase->TextureDiffuse.get();

					_debrisList.push_back(debris);
				}
			}
		}

		int32_t size = (int32_t)_debrisList.size();
		for (int32_t i = 0; i < size; i++) {
			Tiles::TileMap::DestructibleDebris& debris = _debrisList[i];

			if (debris.Scale <= 0.0f || debris.Alpha <= 0.0f) {
				std::swap(debris, _debrisList[size - 1]);
				_debrisList.pop_back();
				i--;
				size--;
				continue;
			}

			debris.Time -= timeMult;
			if (debris.Time <= 0.0f) {
				debris.AlphaSpeed = -std::min(0.02f, debris.Alpha);
			}

			debris.Pos.X += debris.Speed.X * timeMult + 0.5f * debris.Acceleration.X * timeMult * timeMult;
			debris.Pos.Y += debris.Speed.Y * timeMult + 0.5f * debris.Acceleration.Y * timeMult * timeMult;

			if (debris.Acceleration.X != 0.0f) {
				debris.Speed.X = std::min(debris.Speed.X + debris.Acceleration.X * timeMult, 10.0f);
			}
			if (debris.Acceleration.Y != 0.0f) {
				debris.Speed.Y = std::min(debris.Speed.Y + debris.Acceleration.Y * timeMult, 10.0f);
			}

			debris.Scale += debris.ScaleSpeed * timeMult;
			debris.Angle += debris.AngleSpeed * timeMult;
			debris.Alpha += debris.AlphaSpeed * timeMult;
		}
	}

	void MainMenu::DrawDebris(RenderQueue& renderQueue)
	{
		for (auto& debris : _debrisList) {
			auto command = _canvasOverlay->RentRenderCommand();
			if (command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE)) {
				command->material().reserveUniformsDataMemory();
				command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
				// Required to reset render command properly
				//command->setTransformation(command->transformation());

				GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->intValue(0) != 0) {
					textureUniform->setIntValue(0); // GL_TEXTURE0
				}
			}

			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(debris.TexScaleX, debris.TexBiasX, debris.TexScaleY, debris.TexBiasY);
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(debris.Size.X, debris.Size.Y);
			instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, debris.Alpha).Data());

			Matrix4x4f worldMatrix = Matrix4x4f::Translation(debris.Pos.X, debris.Pos.Y, 0.0f);
			worldMatrix.RotateZ(debris.Angle);
			worldMatrix.Scale(debris.Scale, debris.Scale, 1.0f);
			worldMatrix.Translate(debris.Size.X * -0.5f, debris.Size.Y * -0.5f, 0.0f);
			command->setTransformation(worldMatrix);
			command->setLayer(debris.Depth);
			command->material().setTexture(*debris.DiffuseTexture);

			renderQueue.addCommand(command);
		}
	}

	void MainMenu::PrepareTexturedBackground()
	{
		time_t t = time(nullptr);
#if defined(DEATH_TARGET_WINDOWS)
		struct tm local; localtime_s(&local, &t);
		int32_t month = local.tm_mon;
#else
		struct tm* local = localtime(&t);
		int32_t month = local->tm_mon;
#endif
		bool hasXmas = ((month == 11 || month == 0) && TryLoadBackgroundPreset(Preset::Xmas));
		if (!hasXmas &&
			!TryLoadBackgroundPreset(Preset::Default) &&
			!TryLoadBackgroundPreset(Preset::Carrotus) &&
			!TryLoadBackgroundPreset(Preset::SharewareDemo)) {
			// Cannot load any available preset
			TryLoadBackgroundPreset(Preset::None);
			return;
		}

		constexpr int32_t Width = 8;
		constexpr int32_t Height = 8;

		constexpr int32_t StartIndexDefault = 360;
		constexpr int32_t StartIndexXmas = 420;
		constexpr int32_t StartIndexDemo = 240;
		constexpr int32_t AdditionalIndexDemo = 451;
		constexpr int32_t SplitRowDemo = 6;

		std::unique_ptr<LayerTile[]> layout = std::make_unique<LayerTile[]>(Width * Height);

		int32_t n = 0;
		if (_preset == Preset::SharewareDemo) {
			// Shareware Demo tileset is not contiguous for some reason
			for (int32_t i = StartIndexDemo; i < StartIndexDemo + SplitRowDemo * 10; i += 10) {
				for (int32_t j = 0; j < 8; j++) {
					LayerTile& tile = layout[n++];
					tile.TileID = i + j;
				}
			}
			for (int32_t i = AdditionalIndexDemo; i < AdditionalIndexDemo + (Height - SplitRowDemo) * 10; i += 10) {
				for (int32_t j = 0; j < 8; j++) {
					LayerTile& tile = layout[n++];
					tile.TileID = i + j;
				}
			}
		} else {
			int32_t startIndex = (_preset == Preset::Xmas ? StartIndexXmas : StartIndexDefault);
			for (int32_t i = startIndex; i < startIndex + Height * 10; i += 10) {
				for (int32_t j = 0; j < 8; j++) {
					LayerTile& tile = layout[n++];
					tile.TileID = i + j;
				}
			}
		}

		TileMapLayer& newLayer = _texturedBackgroundLayer;
		newLayer.Visible = true;
		newLayer.LayoutSize = Vector2i(Width, Height);
		newLayer.Layout = std::move(layout);
	}

	bool MainMenu::TryLoadBackgroundPreset(Preset preset)
	{
		auto& resolver = ContentResolver::Get();

		switch (preset) {
			case Preset::Default: _tileSet = resolver.RequestTileSet("easter99"_s, 0, true); break;
			case Preset::Carrotus: _tileSet = resolver.RequestTileSet("carrot1"_s, 0, true); break;
			case Preset::SharewareDemo: _tileSet = resolver.RequestTileSet("diam2"_s, 0, true); break;
			case Preset::Xmas: _tileSet = resolver.RequestTileSet("xmas2"_s, 0, true); break;
			default: _tileSet = nullptr; resolver.ApplyDefaultPalette(); _preset = Preset::None; return true;
		}

		if (_tileSet == nullptr) {
			return false;
		}

		_preset = preset;
		return true;
	}

	void MainMenu::RenderTexturedBackground(RenderQueue& renderQueue)
	{
		auto target = _texturedBackgroundPass._target.get();
		if (target == nullptr) {
			return;
		}

		Vector4f horizonColor;
		switch (_preset) {
			case Preset::Default: horizonColor = Vector4f(0.098f, 0.35f, 1.0f, 0.0f); break;
			default: horizonColor = Vector4f(0.0f, 0.0f, 0.06f, 1.0f); break;
		}

		Vector2i viewSize = _canvasBackground->ViewSize;
		auto command = &_texturedBackgroundPass._outputRenderCommand;

		auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(static_cast<float>(viewSize.X), static_cast<float>(viewSize.Y));
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		command->material().uniform("uViewSize")->setFloatValue(static_cast<float>(viewSize.X), static_cast<float>(viewSize.Y));
		command->material().uniform("uShift")->setFloatVector(_texturedBackgroundPos.Data());
		command->material().uniform("uHorizonColor")->setFloatVector(horizonColor.Data());

		command->setTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
		command->material().setTexture(*target);

		renderQueue.addCommand(command);
	}

	bool MainMenu::RenderLegacyBackground(RenderQueue& renderQueue)
	{
		auto* res16 = _metadata->FindAnimation(Menu16);
		auto* res32 = _metadata->FindAnimation(Menu32);
		auto* res128 = _metadata->FindAnimation(Menu128);
		if (res16 == nullptr || res32 == nullptr || res128 == nullptr) {
			return false;
		}

		float animTime = _canvasBackground->AnimTime;
		Vector2f center = (_canvasBackground->ViewSize / 2).As<float>();

		// 16
		{
			GenericGraphicResource* base = res16->Base;
			base->TextureDiffuse->setMinFiltering(SamplerFilter::Nearest);
			base->TextureDiffuse->setMagFiltering(SamplerFilter::Nearest);
			base->TextureDiffuse->setWrap(SamplerWrapping::Repeat);

			constexpr float repeats = 96.0f;
			float scale = (0.6f + 0.04f * sinf(animTime * 0.2f)) * repeats;
			Vector2f size = base->FrameDimensions.As<float>() * scale;

			auto command = _canvasBackground->RentRenderCommand();
			if (command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE)) {
				command->material().reserveUniformsDataMemory();
				command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
				// Required to reset render command properly
				//command->setTransformation(command->transformation());

				GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->intValue(0) != 0) {
					textureUniform->setIntValue(0); // GL_TEXTURE0
				}
			}

			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(repeats, 0.0f, repeats, 0.0f);
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(size.Data());
			instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

			Matrix4x4f worldMatrix = Matrix4x4f::Translation(center.X, center.Y, 0.0f);
			worldMatrix.RotateZ(animTime * -0.2f);
			worldMatrix.Translate(size.X * -0.5f, size.Y * -0.5f, 0.0f);
			command->setTransformation(worldMatrix);
			command->setLayer(100);
			command->material().setTexture(*base->TextureDiffuse.get());

			renderQueue.addCommand(command);
		}
		
		// 32
		{
			GenericGraphicResource* base = res32->Base;
			base->TextureDiffuse->setMinFiltering(SamplerFilter::Nearest);
			base->TextureDiffuse->setMagFiltering(SamplerFilter::Nearest);
			base->TextureDiffuse->setWrap(SamplerWrapping::Repeat);

			constexpr float repeats = 56.0f;
			float scale = (0.6f + 0.04f * sinf(animTime * 0.2f)) * repeats;
			Vector2f size = base->FrameDimensions.As<float>() * scale;

			Vector2f centerBg = center;
			centerBg.X += 96.0f * sinf(animTime * 0.37f);
			centerBg.Y += 96.0f * cosf(animTime * 0.31f);

			auto command = _canvasBackground->RentRenderCommand();
			if (command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE)) {
				command->material().reserveUniformsDataMemory();
				command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
				// Required to reset render command properly
				//command->setTransformation(command->transformation());

				GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->intValue(0) != 0) {
					textureUniform->setIntValue(0); // GL_TEXTURE0
				}
			}

			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(repeats, 0.0f, repeats, 0.0f);
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(size.Data());
			instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

			Matrix4x4f worldMatrix = Matrix4x4f::Translation(centerBg.X, centerBg.Y, 0.0f);
			worldMatrix.RotateZ(animTime * 0.4f);
			worldMatrix.Translate(size.X * -0.5f, size.Y * -0.5f, 0.0f);
			command->setTransformation(worldMatrix);
			command->setLayer(110);
			command->material().setTexture(*base->TextureDiffuse.get());

			renderQueue.addCommand(command);
		}
		
		// 128
		{
			GenericGraphicResource* base = res128->Base;
			base->TextureDiffuse->setMinFiltering(SamplerFilter::Nearest);
			base->TextureDiffuse->setMagFiltering(SamplerFilter::Nearest);
			base->TextureDiffuse->setWrap(SamplerWrapping::Repeat);

			constexpr float repeats = 20.0f;
			float scale = (0.6f + 0.2f * sinf(animTime * 0.4f)) * repeats;
			Vector2f size = base->FrameDimensions.As<float>() * scale;

			Vector2f centerBg = center;
			centerBg.X += 64.0f * sinf(animTime * 0.25f);
			centerBg.Y += 64.0f * cosf(animTime * 0.32f);

			auto command = _canvasBackground->RentRenderCommand();
			if (command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE)) {
				command->material().reserveUniformsDataMemory();
				command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);
				// Required to reset render command properly
				//command->setTransformation(command->transformation());

				GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->intValue(0) != 0) {
					textureUniform->setIntValue(0); // GL_TEXTURE0
				}
			}

			command->material().setBlendingFactors(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
			instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(repeats, 0.0f, repeats, 0.0f);
			instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatVector(size.Data());
			instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

			Matrix4x4f worldMatrix = Matrix4x4f::Translation(centerBg.X, centerBg.Y, 0.0f);
			worldMatrix.RotateZ(animTime * 0.3f);
			worldMatrix.Translate(size.X * -0.5f, size.Y * -0.5f, 0.0f);
			command->setTransformation(worldMatrix);
			command->setLayer(120);
			command->material().setTexture(*base->TextureDiffuse.get());

			renderQueue.addCommand(command);
		}

		float titleY = _contentBounds.Y - 30;
		DrawElement(MenuGlow, 0, center.X, titleY, 130, Alignment::Center, Colorf(1.0f, 1.0f, 1.0f, 0.14f), 16.0f, 10.0f, true, true);

		return true;
	}

	void MainMenu::TexturedBackgroundPass::Initialize()
	{
		bool notInitialized = (_view == nullptr);

		if (notInitialized) {
			Vector2i layoutSize = _owner->_texturedBackgroundLayer.LayoutSize;
			int32_t width = layoutSize.X * TileSet::DefaultTileSize;
			int32_t height = layoutSize.Y * TileSet::DefaultTileSize;

			_camera = std::make_unique<Camera>();
			_camera->setOrthoProjection(0, static_cast<float>(width), 0, static_cast<float>(height));
			_camera->setView(0, 0, 0, 1);
			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::None);
			_view->setRootNode(this);
			_view->setCamera(_camera.get());
			//_view->setClearMode(Viewport::ClearMode::Never);
			_target->setMagFiltering(SamplerFilter::Linear);
			_target->setWrap(SamplerWrapping::Repeat);

			// Prepare render commands
			int32_t renderCommandCount = (width * height) / (TileSet::DefaultTileSize * TileSet::DefaultTileSize);
			_renderCommands.reserve(renderCommandCount);
			for (int32_t i = 0; i < renderCommandCount; i++) {
				std::unique_ptr<RenderCommand>& command = _renderCommands.emplace_back(std::make_unique<RenderCommand>());
				command->material().setShaderProgramType(Material::ShaderProgramType::SPRITE);
				command->material().reserveUniformsDataMemory();
				command->geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

				GLUniformCache* textureUniform = command->material().uniform(Material::TextureUniformName);
				if (textureUniform && textureUniform->intValue(0) != 0) {
					textureUniform->setIntValue(0); // GL_TEXTURE0
				}
			}

			// Prepare output render command
			_outputRenderCommand.material().setShader(ContentResolver::Get().GetShader(PrecompiledShader::TexturedBackground));
			_outputRenderCommand.material().reserveUniformsDataMemory();
			_outputRenderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

			GLUniformCache* textureUniform = _outputRenderCommand.material().uniform(Material::TextureUniformName);
			if (textureUniform && textureUniform->intValue(0) != 0) {
				textureUniform->setIntValue(0); // GL_TEXTURE0
			}
		}

		Viewport::chain().push_back(_view.get());
	}

	bool MainMenu::TexturedBackgroundPass::OnDraw(RenderQueue& renderQueue)
	{
		TileMapLayer& layer = _owner->_texturedBackgroundLayer;
		Vector2i layoutSize = layer.LayoutSize;

		int32_t renderCommandIndex = 0;
		bool isAnimated = false;

		for (int32_t y = 0; y < layoutSize.Y; y++) {
			for (int32_t x = 0; x < layoutSize.X; x++) {
				LayerTile& tile = layer.Layout[y * layer.LayoutSize.X + x];

				auto command = _renderCommands[renderCommandIndex++].get();

				Vector2i texSize = _owner->_tileSet->TextureDiffuse->size();
				float texScaleX = TileSet::DefaultTileSize / float(texSize.X);
				float texBiasX = ((tile.TileID % _owner->_tileSet->TilesPerRow) * (TileSet::DefaultTileSize + 2.0f) + 1.0f) / float(texSize.X);
				float texScaleY = TileSet::DefaultTileSize / float(texSize.Y);
				float texBiasY = ((tile.TileID / _owner->_tileSet->TilesPerRow) * (TileSet::DefaultTileSize + 2.0f) + 1.0f) / float(texSize.Y);

				auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
				instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(TileSet::DefaultTileSize, TileSet::DefaultTileSize);
				instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());
				
				command->setTransformation(Matrix4x4f::Translation(x * TileSet::DefaultTileSize, y * TileSet::DefaultTileSize, 0.0f));
				command->material().setTexture(*_owner->_tileSet->TextureDiffuse);

				renderQueue.addCommand(command);
			}
		}

		if (!isAnimated && _alreadyRendered) {
			// If it's not animated, it can be rendered only once
			for (int32_t i = Viewport::chain().size() - 1; i >= 0; i--) {
				auto& item = Viewport::chain()[i];
				if (item == _view.get()) {
					Viewport::chain().erase(&item);
					break;
				}
			}
		}

		_alreadyRendered = true;
		return true;
	}
}