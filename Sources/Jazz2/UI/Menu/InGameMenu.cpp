#include "InGameMenu.h"
#include "MenuResources.h"
#include "PauseSection.h"
#include "../ControlScheme.h"
#include "../../LevelHandler.h"
#include "../../PlayerViewport.h"
#include "../../PreferencesCache.h"

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

		UpdateContentBounds(_root->_upscalePass.GetViewSize());

		auto& resolver = ContentResolver::Get();

		_metadata = resolver.RequestMetadata("UI/MainMenu"_s);
		ASSERT_MSG(_metadata != nullptr, "Cannot load required metadata");

		_smallFont = resolver.GetFont(FontType::Small);
		_mediumFont = resolver.GetFont(FontType::Medium);

		// Mark Menu button as already pressed to avoid some issues
		_pressedActions = (1 << (std::int32_t)PlayerActions::Menu) | (1 << ((std::int32_t)PlayerActions::Menu + 16));

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

#if defined(WITH_AUDIO)
		// Destroy stopped players
		auto it = _owner->_playingSounds.begin();
		while (it != _owner->_playingSounds.end()) {
			if ((*it)->isStopped()) {
				it = _owner->_playingSounds.eraseUnordered(it);
				continue;
			}
			++it;
		}
#endif

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

	void InGameMenu::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		UpdateContentBounds(Vector2i(width, height));

		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			Recti clipRectangle = lastSection->GetClipRectangle(_contentBounds);
			_root->_upscalePass.SetClipRectangle(clipRectangle);
		}
	}

	bool InGameMenu::MenuBackgroundCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_root->_upscalePass.GetViewSize();

		_owner->_activeCanvas = ActiveCanvas::Background;

		Vector2i center = ViewSize / 2;

		std::int32_t charOffset = 0;
		std::int32_t charOffsetShadow = 0;

		float titleY = _owner->_contentBounds.Y - (ViewSize.Y >= 300 ? 30.0f : 12.0f);
		float logoBaseScale = (ViewSize.Y >= 300 ? 1.0f : 0.85f);
		float logoScale = logoBaseScale;
		float logoTextScale = logoBaseScale;
		float logoTranslateX = logoBaseScale;
		float logoTranslateY = 0.0f;
		float logoTextTranslate = 0.0f;

		// Show blurred viewports behind
		auto& viewports = _owner->_root->_assignedViewports;
		for (std::size_t i = 0; i < viewports.size(); i++) {
			auto& viewport = viewports[i];
			Rectf scopedView = viewport->GetBounds();
			DrawTexture(*viewport->_blurPass4.GetTarget(), scopedView.GetLocation(), 500, scopedView.GetSize(), Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf(0.5f, 0.5f, 0.5f, std::min(AnimTime * 8.0f, 1.0f)));

			Vector4f ambientColor = viewport->_ambientLight;
			if (ambientColor.W < 1.0f) {
				DrawSolid(scopedView.GetLocation(), 502, scopedView.GetSize(), Colorf(ambientColor.X, ambientColor.Y, ambientColor.Z, (1.0f - powf(ambientColor.W, 1.6f)) * std::min(AnimTime * 8.0f, 1.0f)));
			}
		}

		DrawViewportSeparators();

		if (_owner->_touchButtonsTimer > 0.0f && _owner->_sections.size() >= 2) {
			float arrowScale = (ViewSize.Y >= 300 ? 1.0f : 0.7f);
			_owner->DrawElement(MenuLineArrow, -1, static_cast<float>(center.X), titleY - (ViewSize.Y >= 300 ? 30.0f : 12.0f), ShadowLayer, Alignment::Center, Colorf::White, arrowScale, arrowScale);
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
		Vector2f bottomRight = Vector2f(static_cast<float>(ViewSize.X), static_cast<float>(ViewSize.Y));
		bottomRight.X = ViewSize.X - 24.0f;
		bottomRight.Y -= (ViewSize.Y >= 300 ? 10.0f : 4.0f);
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

	void InGameMenu::MenuBackgroundCanvas::DrawViewportSeparators()
	{
		switch (_owner->_root->_assignedViewports.size()) {
			case 2: {
				if (PreferencesCache::PreferVerticalSplitscreen) {
					std::int32_t halfW = ViewSize.X / 2;
					DrawSolid(Vector2f(halfW - 1.0f, 0.0f), ShadowLayer, Vector2f(1.0f, ViewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.2f));
					DrawSolid(Vector2f(halfW, 0.0f), ShadowLayer, Vector2f(1.0f, ViewSize.Y), Colorf(1.0f, 1.0f, 1.0f, 0.02f), true);
				} else {
					std::int32_t halfH = ViewSize.Y / 2;
					DrawSolid(Vector2f(0.0f, halfH - 1.0f), ShadowLayer, Vector2f(ViewSize.X, 1.0f), Colorf(1.0f, 1.0f, 1.0f, 0.02f), true);
					DrawSolid(Vector2f(0.0f, halfH), ShadowLayer, Vector2f(ViewSize.X, 1.0f), Colorf(0.0f, 0.0f, 0.0f, 0.2f));
				}
				break;
			}
			case 3: {
				std::int32_t halfW = (ViewSize.X + 1) / 2;
				std::int32_t halfH = (ViewSize.Y + 1) / 2;
				DrawSolid(Vector2f(halfW, halfH), ShadowLayer, Vector2f(halfW, halfH), Colorf::Black);
				DEATH_FALLTHROUGH
			}
			case 4: {
				std::int32_t halfW = (ViewSize.X + 1) / 2;
				std::int32_t halfH = (ViewSize.Y + 1) / 2;
				DrawSolid(Vector2f(halfW - 1.0f, 0.0f), ShadowLayer, Vector2f(1.0f, ViewSize.Y), Colorf(0.0f, 0.0f, 0.0f, 0.2f));
				DrawSolid(Vector2f(halfW, 0.0f), ShadowLayer, Vector2f(1.0f, ViewSize.Y), Colorf(1.0f, 1.0f, 1.0f, 0.02f), true);
				DrawSolid(Vector2f(0.0f, halfH - 1.0f), ShadowLayer, Vector2f(ViewSize.X, 1.0f), Colorf(1.0f, 1.0f, 1.0f, 0.02f), true);
				DrawSolid(Vector2f(0.0f, halfH), ShadowLayer, Vector2f(ViewSize.X, 1.0f), Colorf(0.0f, 0.0f, 0.0f, 0.2f));
				break;
			}
		}
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

		if (_contentBounds != Recti::Empty) {
			Recti clipRectangle = currentSection->GetClipRectangle(_contentBounds);
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

			if (_contentBounds != Recti::Empty) {
				Recti clipRectangle = lastSection->GetClipRectangle(_contentBounds);
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
			Vector2i res = theApplication().GetResolution();
			_root->OnInitializeViewport(res.X, res.Y);
		}

#if defined(WITH_AUDIO)
		if ((type & ChangedPreferencesType::Audio) == ChangedPreferencesType::Audio) {
			if (_root->_music != nullptr) {
				_root->_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			}
			if (_root->_sugarRushMusic != nullptr) {
				_root->_sugarRushMusic->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			}
		}
#endif

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

	void InGameMenu::DrawElement(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float scaleX, float scaleY, bool additiveBlending, bool unaligned)
	{
		auto* res = _metadata->FindAnimation(state);
		if (res == nullptr) {
			return;
		}

		if (frame < 0) {
			frame = res->FrameOffset + ((std::int32_t)(_canvasBackground->AnimTime * res->FrameCount / res->AnimDuration) % res->FrameCount);
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
		std::int32_t col = frame % base->FrameConfiguration.X;
		std::int32_t row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			float(base->FrameDimensions.X) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			float(base->FrameDimensions.Y) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);

		currentCanvas->DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, additiveBlending);
	}

	void InGameMenu::DrawElement(AnimState state, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, const Vector2f& size, const Vector4f& texCoords, bool unaligned)
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

	void InGameMenu::DrawSolid(float x, float y, std::uint16_t z, Alignment align, const Vector2f& size, const Colorf& color, bool additiveBlending)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);
		adjustedPos.X = std::round(adjustedPos.X);
		adjustedPos.Y = std::round(adjustedPos.Y);

		currentCanvas->DrawSolid(adjustedPos, z, size, color, additiveBlending);
	}

	void InGameMenu::DrawTexture(const Texture& texture, float x, float y, std::uint16_t z, Alignment align, const Vector2f& size, const Colorf& color, bool unaligned)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);
		if (!unaligned) {
			adjustedPos.X = std::round(adjustedPos.X);
			adjustedPos.Y = std::round(adjustedPos.Y);
		}

		currentCanvas->DrawTexture(texture, adjustedPos, z, size, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), color);
	}

	Vector2f InGameMenu::MeasureString(const StringView text, float scale, float charSpacing, float lineSpacing)
	{
		return _smallFont->MeasureString(text, scale, charSpacing, lineSpacing);
	}

	void InGameMenu::DrawStringShadow(const StringView text, int& charOffset, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float scale,
		float angleOffset, float varianceX, float varianceY, float speed, float charSpacing, float lineSpacing)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		std::int32_t charOffsetShadow = charOffset;
		_smallFont->DrawString(currentCanvas, text, charOffsetShadow, x, y + 2.8f * scale, FontShadowLayer,
			align, Colorf(0.0f, 0.0f, 0.0f, 0.29f), scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
		_smallFont->DrawString(currentCanvas, text, charOffset, x, y, z,
			align, color, scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
	}

	void InGameMenu::PlaySfx(const StringView identifier, float gain)
	{
#if defined(WITH_AUDIO)
		auto it = _metadata->Sounds.find(String::nullTerminatedView(identifier));
		if (it != _metadata->Sounds.end()) {
			std::int32_t idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (std::int32_t)it->second.Buffers.size()) : 0);
			auto& player = _playingSounds.emplace_back(std::make_shared<AudioBufferPlayer>(&it->second.Buffers[idx]->Buffer));
			player->setPosition(Vector3f(0.0f, 0.0f, 100.0f));
			player->setGain(gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
			player->setSourceRelative(true);

			player->play();
		} else {
			LOGE("Sound effect \"%s\" was not found", identifier.data());
		}
#endif
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
		return ((_pressedActions & (1 << (std::int32_t)action)) == (1 << (std::int32_t)action));
	}

	bool InGameMenu::ActionHit(PlayerActions action)
	{
		return ((_pressedActions & ((1 << (std::int32_t)action) | (1 << (16 + (std::int32_t)action)))) == (1 << (std::int32_t)action));
	}

	void InGameMenu::UpdateContentBounds(Vector2i viewSize)
	{
		float headerY = (viewSize.Y >= 300 ? std::clamp((200.0f * viewSize.Y / viewSize.X) - 40.0f, 30.0f, 70.0f) : 8.0f);
		float footerY = (viewSize.Y >= 300 ? 30.0f : 14.0f);
		_contentBounds = Recti(0, headerY + 30, viewSize.X, viewSize.Y - (headerY + footerY));
	}

	void InGameMenu::UpdatePressedActions()
	{
		auto& input = theApplication().GetInputManager();
		_pressedActions = ((_pressedActions & 0xFFFF) << 16);

		const JoyMappedState* joyStates[UI::ControlScheme::MaxConnectedGamepads];
		std::int32_t joyStatesCount = 0;
		for (std::int32_t i = 0; i < IInputManager::MaxNumJoysticks && joyStatesCount < static_cast<std::int32_t>(arraySize(joyStates)); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[joyStatesCount++] = &input.joyMappedState(i);
			}
		}

		bool allowGamepads = true;
		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			allowGamepads = lastSection->IsGamepadNavigationEnabled();
		}

		_pressedActions |= ControlScheme::FetchNativation(_root->_pressedKeys, ArrayView(joyStates, joyStatesCount), allowGamepads);
	}
}