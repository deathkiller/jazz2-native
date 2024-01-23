#include "InGameMenu.h"
#include "MenuResources.h"
#include "PauseSection.h"
#include "../ControlScheme.h"
#include "../../PreferencesCache.h"
#include "../../LevelHandler.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/RenderQueue.h"
#include "../../../nCine/Audio/AudioReaderMpt.h"
#include "../../../nCine/Base/Random.h"

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	InGameMenu::InGameMenu(LevelHandler* root)
		: _root(root), _pressedActions(0), _touchButtonsTimer(0.0f)
	{
		_canvasBackground = std::make_unique<MenuBackgroundCanvas>(this);
		_canvasClipped = std::make_unique<MenuClippedCanvas>(this);
		_canvasOverlay = std::make_unique<MenuOverlayCanvas>(this);

		_canvasBackground->setParent(root->_upscalePass.GetNode());
		_canvasClipped->setParent(root->_upscalePass.GetClippedNode());
		_canvasOverlay->setParent(root->_upscalePass.GetOverlayNode());

		auto& resolver = ContentResolver::Get();

		_metadata = resolver.RequestMetadata("UI/MainMenu"_s);
		ASSERT_MSG(_metadata != nullptr, "Cannot load required metadata");

		_smallFont = resolver.GetFont(FontType::Small);
		_mediumFont = resolver.GetFont(FontType::Medium);

		// Mark Menu button as already pressed to avoid some issues
		_pressedActions = (1 << (int32_t)PlayerActions::Menu) | (1 << ((int32_t)PlayerActions::Menu + 16));

		SwitchToSection<PauseSection>();

		PlaySfx("MenuSelect"_s, 0.5f);
	}

	InGameMenu::~InGameMenu()
	{
		_canvasBackground->setParent(nullptr);
		_canvasClipped->setParent(nullptr);
		_canvasOverlay->setParent(nullptr);
	}

	void InGameMenu::MenuBackgroundCanvas::OnUpdate(float timeMult)
	{
		Canvas::OnUpdate(timeMult);

		_owner->UpdatePressedActions();

		// Destroy stopped players
		for (int32_t i = (int32_t)_owner->_playingSounds.size() - 1; i >= 0; i--) {
			if (_owner->_playingSounds[i]->state() == IAudioPlayer::PlayerState::Stopped) {
				_owner->_playingSounds.erase(&_owner->_playingSounds[i]);
			} else {
				break;
			}
		}

		if (_owner->_touchButtonsTimer > 0.0f) {
			_owner->_touchButtonsTimer -= timeMult;
		}

		if (!_owner->_sections.empty()) {
			auto& lastSection = _owner->_sections.back();
			lastSection->OnUpdate(timeMult);
		}
	}

	void InGameMenu::OnTouchEvent(const nCine::TouchEvent& event)
	{
		if (!_sections.empty()) {
			_touchButtonsTimer = 1200.0f;

			auto& lastSection = _sections.back();
			lastSection->OnTouchEvent(event, _canvasBackground->ViewSize);
		}
	}

	void InGameMenu::OnInitializeViewport(int32_t width, int32_t height)
	{
		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			Recti clipRectangle = lastSection->GetClipRectangle(Vector2i(width, height));
			_root->_upscalePass.SetClipRectangle(clipRectangle);
		}
	}

	bool InGameMenu::MenuBackgroundCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_root->_upscalePass.GetViewSize();

		_owner->_activeCanvas = ActiveCanvas::Background;

		Vector2i center = ViewSize / 2;

		int32_t charOffset = 0;
		int32_t charOffsetShadow = 0;

		constexpr float logoScale = 1.0f;
		constexpr float logoTextScale = 1.0f;
		constexpr float logoTranslateX = 1.0f;
		constexpr float logoTranslateY = 0.0f;
		constexpr float logoTextTranslate = 0.0f;

		// Show blurred viewport behind
		DrawTexture(*_owner->_root->_blurPass4.GetTarget(), Vector2f::Zero, 500, Vector2f(static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y)), Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf(0.5f, 0.5f, 0.5f, std::min(AnimTime * 8.0f, 1.0f)));
		Vector4f ambientColor = _owner->_root->_ambientColor;
		if (ambientColor.W < 1.0f) {
			DrawSolid(Vector2f::Zero, 502, Vector2f(static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y)), Colorf(ambientColor.X, ambientColor.Y, ambientColor.Z, (1.0f - ambientColor.W) * std::min(AnimTime * 8.0f, 1.0f)));
		}

		if (_owner->_touchButtonsTimer > 0.0f && _owner->_sections.size() >= 2) {
			_owner->DrawElement(MenuLineArrow, -1, static_cast<float>(center.X), 40.0f, ShadowLayer, Alignment::Center, Colorf::White);
		}

		// Title
		_owner->DrawElement(MenuCarrot, -1, center.X - 76.0f * logoTranslateX, 64.0f + logoTranslateY + 2.0f, ShadowLayer + 200, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.8f * logoScale, 0.8f * logoScale);
		_owner->DrawElement(MenuCarrot, -1, center.X - 76.0f * logoTranslateX, 64.0f + logoTranslateY, MainLayer + 200, Alignment::Center, Colorf::White, 0.8f * logoScale, 0.8f * logoScale);

		_owner->_mediumFont->DrawString(this, "Jazz"_s, charOffsetShadow, center.X - 63.0f, 70.0f + logoTranslateY + 2.0f, FontShadowLayer + 200,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.75f * logoTextScale, 1.65f, 3.0f, 3.0f, 0.0f, 0.92f);
		_owner->_mediumFont->DrawString(this, "2"_s, charOffsetShadow, center.X - 19.0f, 70.0f - 8.0f + logoTranslateY + 2.0f, FontShadowLayer + 200,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.32f), 0.5f * logoTextScale, 0.0f, 0.0f, 0.0f, 0.0f);
		_owner->_mediumFont->DrawString(this, "Resurrection"_s, charOffsetShadow, center.X - 10.0f, 70.0f + 4.0f + logoTranslateY + 2.5f, FontShadowLayer + 200,
			Alignment::Left, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.5f * logoTextScale, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);

		_owner->_mediumFont->DrawString(this, "Jazz"_s, charOffset, center.X - 63.0f * logoTranslateX + logoTextTranslate, 70.0f + logoTranslateY, FontLayer + 200,
			Alignment::Left, Colorf(0.54f, 0.44f, 0.34f, 0.5f), 0.75f * logoTextScale, 1.65f, 3.0f, 3.0f, 0.0f, 0.92f);
		_owner->_mediumFont->DrawString(this, "2"_s, charOffset, center.X - 19.0f * logoTranslateX + logoTextTranslate, 70.0f - 8.0f + logoTranslateY, FontLayer + 200,
			Alignment::Left, Colorf(0.54f, 0.44f, 0.34f, 0.5f), 0.5f * logoTextScale, 0.0f, 0.0f, 0.0f, 0.0f);
		_owner->_mediumFont->DrawString(this, "Resurrection"_s, charOffset, center.X - 10.0f * logoTranslateX + logoTextTranslate, 70.0f + 4.0f + logoTranslateY, FontLayer + 200,
			Alignment::Left, Colorf(0.6f, 0.42f, 0.42f, 0.5f), 0.5f * logoTextScale, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);

		// Version
		Vector2f bottomRight = Vector2f(static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y));
		bottomRight.X = ViewSize.X - 24.0f;
		bottomRight.Y -= 10.0f;
		_owner->DrawStringShadow("v" NCINE_VERSION, charOffset, bottomRight.X, bottomRight.Y, IMenuContainer::FontLayer,
			Alignment::BottomRight, Colorf(0.45f, 0.45f, 0.45f, 0.5f), 0.7f, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);

		// Copyright
		Vector2f bottomLeft = bottomRight;
		bottomLeft.X = 24.0f;
		_owner->DrawStringShadow("© 2016-" NCINE_BUILD_YEAR "  Dan R."_s, charOffset, bottomLeft.X, bottomLeft.Y, IMenuContainer::FontLayer,
			Alignment::BottomLeft, Colorf(0.45f, 0.45f, 0.45f, 0.5f), 0.7f, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);

		if (!_owner->_sections.empty()) {
			auto& lastSection = _owner->_sections.back();
			lastSection->OnDraw(this);
		}

		return true;
	}

	bool InGameMenu::MenuClippedCanvas::OnDraw(RenderQueue& renderQueue)
	{
		if (_owner->_sections.empty()) {
			return false;
		}

		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_root->_upscalePass.GetViewSize();

		_owner->_activeCanvas = ActiveCanvas::Clipped;

		auto& lastSection = _owner->_sections.back();
		lastSection->OnDrawClipped(this);

		return true;
	}

	bool InGameMenu::MenuOverlayCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_root->_upscalePass.GetViewSize();

		_owner->_activeCanvas = ActiveCanvas::Overlay;

		if (!_owner->_sections.empty()) {
			auto& lastSection = _owner->_sections.back();
			lastSection->OnDrawOverlay(this);
		}

		return true;
	}

	MenuSection* InGameMenu::SwitchToSectionDirect(std::unique_ptr<MenuSection> section)
	{
		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			lastSection->OnHide();
		}

		auto& currentSection = _sections.emplace_back(std::move(section));
		currentSection->OnShow(this);

		if (_canvasBackground->ViewSize != Vector2i::Zero) {
			Recti clipRectangle = currentSection->GetClipRectangle(_canvasBackground->ViewSize);
			_root->_upscalePass.SetClipRectangle(clipRectangle);
		}

		return currentSection.get();
	}

	void InGameMenu::LeaveSection()
	{
		if (_sections.empty()) {
			return;
		}

		_sections.pop_back();

		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			lastSection->OnShow(this);

			if (_canvasBackground->ViewSize != Vector2i::Zero) {
				Recti clipRectangle = lastSection->GetClipRectangle(_canvasBackground->ViewSize);
				_root->_upscalePass.SetClipRectangle(clipRectangle);
			}
		}
	}

	MenuSection* InGameMenu::GetUnderlyingSection() const
	{
		std::size_t count = _sections.size();
		return (count >= 2 ? _sections[count - 2].get() : nullptr);
	}

	void InGameMenu::ChangeLevel(LevelInitialization&& levelInit)
	{
		_root->_root->ChangeLevel(std::move(levelInit));
	}

	bool InGameMenu::HasResumableState() const
	{
		return _root->_root->HasResumableState();
	}

	void InGameMenu::ResumeSavedState()
	{
		_root->_root->ResumeSavedState();
	}

#if defined(WITH_MULTIPLAYER)
	bool InGameMenu::ConnectToServer(const StringView address, std::uint16_t port)
	{
		return _root->_root->ConnectToServer(address, port);
	}

	bool InGameMenu::CreateServer(LevelInitialization&& levelInit, std::uint16_t port)
	{
		return _root->_root->CreateServer(std::move(levelInit), port);
	}
#endif

	void InGameMenu::ApplyPreferencesChanges(ChangedPreferencesType type)
	{
		if ((type & ChangedPreferencesType::Graphics) == ChangedPreferencesType::Graphics) {
			Viewport::chain().clear();
			Vector2i res = theApplication().resolution();
			_root->OnInitializeViewport(res.X, res.Y);
		}

		if ((type & ChangedPreferencesType::Audio) == ChangedPreferencesType::Audio) {
			if (_root->_music != nullptr) {
				_root->_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			}
			if (_root->_sugarRushMusic != nullptr) {
				_root->_sugarRushMusic->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			}
		}

		if ((type & ChangedPreferencesType::Language) == ChangedPreferencesType::Language) {
			// All sections have to be recreated to load new language
			_sections.clear();
			SwitchToSection<PauseSection>();
		}

		if ((type & ChangedPreferencesType::ControlScheme) == ChangedPreferencesType::ControlScheme) {
			// Mark all buttons as already pressed to avoid some issues
			_pressedActions = 0xffff | (0xffff << 16);
		}
	}

	void InGameMenu::DrawElement(AnimState state, int32_t frame, float x, float y, uint16_t z, Alignment align, const Colorf& color, float scaleX, float scaleY, bool additiveBlending)
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

	void InGameMenu::DrawElement(AnimState state, float x, float y, uint16_t z, Alignment align, const Colorf& color, const Vector2f& size, const Vector4f& texCoords)
	{
		auto* res = _metadata->FindAnimation(state);
		if (res == nullptr) {
			return;
		}

		Canvas* currentCanvas = GetActiveCanvas();
		GenericGraphicResource* base = res->Base;
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);

		currentCanvas->DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, false);
	}

	void InGameMenu::DrawSolid(float x, float y, uint16_t z, Alignment align, const Vector2f& size, const Colorf& color, bool additiveBlending)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);

		currentCanvas->DrawSolid(adjustedPos, z, size, color, additiveBlending);
	}

	void InGameMenu::DrawTexture(const Texture& texture, float x, float y, uint16_t z, Alignment align, const Vector2f& size, const Colorf& color)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);

		currentCanvas->DrawTexture(texture, adjustedPos, z, size, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), color);
	}

	Vector2f InGameMenu::MeasureString(const StringView text, float scale, float charSpacing, float lineSpacing)
	{
		return _smallFont->MeasureString(text, scale, charSpacing, lineSpacing);
	}

	void InGameMenu::DrawStringShadow(const StringView text, int& charOffset, float x, float y, uint16_t z, Alignment align, const Colorf& color, float scale,
		float angleOffset, float varianceX, float varianceY, float speed, float charSpacing, float lineSpacing)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		int32_t charOffsetShadow = charOffset;
		_smallFont->DrawString(currentCanvas, text, charOffsetShadow, x, y + 2.8f * scale, FontShadowLayer,
			align, Colorf(0.0f, 0.0f, 0.0f, 0.29f), scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
		_smallFont->DrawString(currentCanvas, text, charOffset, x, y, z,
			align, color, scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
	}

	void InGameMenu::PlaySfx(const StringView identifier, float gain)
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

	void InGameMenu::ResumeGame()
	{
		_root->ResumeGame();
	}

	void InGameMenu::GoToMainMenu()
	{
#if !defined(SHAREWARE_DEMO_ONLY)
		_root->_root->SaveCurrentStateIfAny();
#endif
		_root->_root->GoToMainMenu(false);
	}

	bool InGameMenu::ActionPressed(PlayerActions action)
	{
		return ((_pressedActions & (1 << (int32_t)action)) == (1 << (int32_t)action));
	}

	bool InGameMenu::ActionHit(PlayerActions action)
	{
		return ((_pressedActions & ((1 << (int32_t)action) | (1 << (16 + (int32_t)action)))) == (1 << (int32_t)action));
	}

	void InGameMenu::UpdatePressedActions()
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

		_pressedActions |= ControlScheme::FetchNativation(0, _root->_pressedKeys, ArrayView(joyStates, joyStatesCount), allowGamepads);
	}
}