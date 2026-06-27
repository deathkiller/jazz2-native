#include "InGameMenu.h"
#include "MenuResources.h"
#include "PauseSection.h"
#include "../../Input/ControlScheme.h"
#include "../../LevelHandler.h"
#include "../../PreferencesCache.h"
#include "../../Rendering/PlayerViewport.h"
#include "../HUD.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/RenderQueue.h"

#if defined(WITH_MULTIPLAYER)
#	include "../../Multiplayer/MpLevelHandler.h"
#endif

#if defined(DEATH_TARGET_ANDROID)
#	include "../../../nCine/Backends/Android/AndroidApplication.h"
#endif

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	InGameMenu::InGameMenu(LevelHandler* root)
		: _root(root)
	{
		_canvasBackground = std::make_unique<MenuBackgroundCanvas>(this);
		_canvasClipped = std::make_unique<MenuClippedCanvas>(this);
		_canvasOverlay = std::make_unique<MenuOverlayCanvas>(this);

		auto& overlayPass = root->GetActiveOverlayPass();
		_canvasBackground->setParent(overlayPass.GetNode());
		_canvasClipped->setParent(overlayPass.GetClippedNode());
		_canvasOverlay->setParent(overlayPass.GetOverlayNode());

		UpdateContentBounds(overlayPass.GetViewSize());

		auto& resolver = ContentResolver::Get();

		_metadata = resolver.RequestMetadata("UI/MainMenu"_s);
		DEATH_ASSERT(_metadata != nullptr, "Cannot load required metadata", );

		_smallFont = resolver.GetFont(FontType::Small);
		_mediumFont = resolver.GetFont(FontType::Medium);

		// Mark Menu button as already pressed to avoid some issues
		_pressedActions = (1 << (std::int32_t)PlayerAction::Menu) | (1 << ((std::int32_t)PlayerAction::Menu + 16));

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

		_owner->UpdateActiveSection(timeMult);
	}

	void InGameMenu::OnKeyPressed(const nCine::KeyboardEvent& event)
	{
		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			lastSection->OnKeyPressed(event);
		}
	}

	void InGameMenu::OnKeyReleased(const nCine::KeyboardEvent& event)
	{
		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			lastSection->OnKeyReleased(event);
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
		// The active overlay pass can change (e.g. when the splitscreen player count changes), so re-attach the canvases
		auto& overlayPass = _root->GetActiveOverlayPass();
		_canvasBackground->setParent(overlayPass.GetNode());
		_canvasClipped->setParent(overlayPass.GetClippedNode());
		_canvasOverlay->setParent(overlayPass.GetOverlayNode());

		UpdateContentBounds(Vector2i(width, height));

		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			Recti clipRectangle = lastSection->GetClipRectangle(_contentBounds);
			overlayPass.SetClipRectangle(clipRectangle);
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
			Texture* blurTexture = viewport->_blurPass4.GetTarget();
			if DEATH_LIKELY(blurTexture != nullptr) {
				DrawTexture(*blurTexture, scopedView.GetLocation(), 500, scopedView.GetSize(), Vector4f(1.0f, 0.0f, 1.0f, 0.0f), Colorf(0.5f, 0.5f, 0.5f, std::min(AnimTime * 8.0f, 1.0f)));
			} else {
				DrawSolid(scopedView.GetLocation(), 500, scopedView.GetSize(), Colorf(0.0f, 0.0f, 0.0f, std::min(AnimTime * 8.0f, 0.7f)));
			}

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

#if defined(DEATH_TARGET_ANDROID)
		if (!static_cast<AndroidApplication&>(theApplication()).IsScreenRound())
#endif
		{
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
		}

		_owner->DrawSections(this, ActiveCanvas::Background);

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

		_owner->DrawSections(this, ActiveCanvas::Clipped);

		return true;
	}

	bool InGameMenu::MenuOverlayCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_root->_upscalePass.GetViewSize();

		_owner->_activeCanvas = ActiveCanvas::Overlay;

		_owner->DrawSections(this, ActiveCanvas::Overlay);

		return true;
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
	void InGameMenu::ConnectToServer(StringView endpoint, std::uint16_t defaultPort)
	{
		_root->_root->ConnectToServer(endpoint, defaultPort);
	}

	bool InGameMenu::CreateServer(Jazz2::Multiplayer::ServerInitialization&& serverInit)
	{
		return _root->_root->CreateServer(std::move(serverInit));
	}
#endif

	void InGameMenu::ApplyPreferencesChanges(ChangedPreferencesType type)
	{
		if ((type & ChangedPreferencesType::Graphics) == ChangedPreferencesType::Graphics) {
			Viewport::GetChain().clear();
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
			_transition.Skip();
			_sections.clear();
			SwitchToSection<PauseSection>();
		}

		if ((type & ChangedPreferencesType::ControlScheme) == ChangedPreferencesType::ControlScheme) {
			// Mark all buttons as already pressed to avoid some issues
			_pressedActions = 0xffff | (0xffff << 16);
		}

		if ((type & ChangedPreferencesType::TouchButtons) == ChangedPreferencesType::TouchButtons) {
			if (_root->_hud != nullptr) {
				_root->_hud->RefreshTouchButtons();
			}
		}
	}

	bool InGameMenu::IsLocalSession() const
	{
		return _root->IsLocalSession();
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

#if defined(WITH_MULTIPLAYER)
	bool InGameMenu::IsSpectating()
	{
		if (auto* mpLevelHandler = runtime_cast<Jazz2::Multiplayer::MpLevelHandler>(_root)) {
			return mpLevelHandler->IsSpectating();
		}

		return false;
	}

	bool InGameMenu::IsSpectateAvailable()
	{
		if (auto* mpLevelHandler = runtime_cast<Jazz2::Multiplayer::MpLevelHandler>(_root)) {
			return mpLevelHandler->IsSpectateAvailable();
		}

		return false;
	}

	void InGameMenu::EnterSpectate()
	{
		if (auto* mpLevelHandler = runtime_cast<Jazz2::Multiplayer::MpLevelHandler>(_root)) {
			mpLevelHandler->RequestSpectateMode(true);
		}

		_root->ResumeGame();
	}

	void InGameMenu::ShowCharacterSelect()
	{
		if (auto* mpLevelHandler = runtime_cast<Jazz2::Multiplayer::MpLevelHandler>(_root)) {
			mpLevelHandler->ShowCharacterSelectLobby();
		}

		_root->ResumeGame();
	}

	Jazz2::Multiplayer::MpLevelHandler* InGameMenu::GetMultiplayerHandler()
	{
		return runtime_cast<Jazz2::Multiplayer::MpLevelHandler>(_root);
	}
#endif
}
