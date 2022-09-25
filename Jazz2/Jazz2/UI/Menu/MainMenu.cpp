#include "MainMenu.h"
#include "../../PreferencesCache.h"
#include "../ControlScheme.h"
#include "BeginSection.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Graphics/RenderQueue.h"
#include "../../../nCine/Graphics/Viewport.h"
#include "../../../nCine/Input/IInputManager.h"
#include "../../../nCine/Audio/AudioReaderMpt.h"
#include "../../../nCine/Base/Random.h"

namespace Jazz2::UI::Menu
{
	MainMenu::MainMenu(IRootController* root, bool afterIntro)
		:
		_root(root),
		_transitionWhite(afterIntro ? 1.0f : 0.0f),
		_logoTransition(0.0f),
		_texturedBackgroundPass(this),
		_texturedBackgroundPhase(0.0f),
		_pressedKeys((uint32_t)KeySym::COUNT_BASE),
		_pressedActions(0),
		_touchButtonsTimer(0.0f)
	{
		theApplication().gfxDevice().setWindowTitle("Jazz² Resurrection"_s);

		_texturedBackgroundLayer.Visible = false;
		_canvas = std::make_unique<MenuCanvas>(this);

		auto& resolver = ContentResolver::Current();
		resolver.ApplyDefaultPalette();

		Metadata* metadata = resolver.RequestMetadata("UI/MainMenu"_s);
		if (metadata != nullptr) {
			_graphics = &metadata->Graphics;
			_sounds = &metadata->Sounds;
		}

		_smallFont = resolver.GetFont(FontType::Small);
		_mediumFont = resolver.GetFont(FontType::Medium);

#if defined(WITH_OPENMPT)
		if (PreferencesCache::EnableReforged) {
			_music = resolver.GetMusic(Random().NextBool() ? "bonus2.j2b"_s : "bonus3.j2b"_s);
		}
		if (_music == nullptr) {
			_music = resolver.GetMusic("menu.j2b"_s);
		}
		if (_music != nullptr) {
			_music->setLooping(true);
			_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			_music->setSourceRelative(true);
			_music->play();
		}
#endif

		PrepareTexturedBackground();

		// Mark Fire and Menu button as already pressed to avoid some issues
		_pressedActions = (1 << (int)PlayerActions::Fire) | (1 << ((int)PlayerActions::Fire + 16)) |
			(1 << (int)PlayerActions::Menu) | (1 << ((int)PlayerActions::Menu + 16));

		SwitchToSection<BeginSection>();
	}

	MainMenu::~MainMenu()
	{
		_canvas->setParent(nullptr);
	}

	void MainMenu::OnBeginFrame()
	{
		float timeMult = theApplication().timeMult();

		UpdatePressedActions();

		// Destroy stopped players
		for (int i = (int)_playingSounds.size() - 1; i >= 0; i--) {
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

	void MainMenu::OnInitializeViewport(int width, int height)
	{
		constexpr float defaultRatio = (float)DefaultWidth / DefaultHeight;
		float currentRatio = (float)width / height;

		int w, h;
		if (currentRatio > defaultRatio) {
			w = std::min(DefaultWidth, width);
			h = (int)(w / currentRatio);
		} else if (currentRatio < defaultRatio) {
			h = std::min(DefaultHeight, height);
			w = (int)(h * currentRatio);
		} else {
			w = std::min(DefaultWidth, width);
			h = std::min(DefaultHeight, height);
		}

		_upscalePass.Initialize(w, h, width, height);

		// Viewports must be registered in reverse order
		_upscalePass.Register();

		_canvas->setParent(_upscalePass.GetNode());

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
			lastSection->OnTouchEvent(event, _canvas->ViewSize);
		}
	}

	bool MainMenu::MenuCanvas::OnDraw(RenderQueue& renderQueue)
	{
		Canvas::OnDraw(renderQueue);

		ViewSize = _owner->_upscalePass.GetViewSize();

		_owner->RenderTexturedBackground(renderQueue);

		Vector2i center = ViewSize / 2;

		int charOffset = 0;
		int charOffsetShadow = 0;

		float logoScale = 1.0 + (1.0f - _owner->_logoTransition) * 7.0f;
		float logoTextScale = 1.0 + (1.0f - _owner->_logoTransition) * 2.0f;
		float logoTranslateX = 1.0 + (1.0f - _owner->_logoTransition) * 1.2f;
		float logoTranslateY = (1.0f - _owner->_logoTransition) * 120.0f;
		float logoTextTranslate = (1.0f - _owner->_logoTransition) * 60.0f;

		if (_owner->_touchButtonsTimer > 0.0f && _owner->_sections.size() >= 2) {
			_owner->DrawElement("MenuLineArrow"_s, -1, center.X, 40.0f, ShadowLayer, Alignment::Center, Colorf::White);
		}

		// Title
		_owner->DrawElement("MenuCarrot"_s, -1, center.X - 76.0f * logoTranslateX, 64.0f + logoTranslateY + 2.0f, ShadowLayer + 200, Alignment::Center, Colorf(0.0f, 0.0f, 0.0f, 0.3f), 0.8f * logoScale, 0.8f * logoScale);
		_owner->DrawElement("MenuCarrot"_s, -1, center.X - 76.0f * logoTranslateX, 64.0f + logoTranslateY, MainLayer + 200, Alignment::Center, Colorf::White, 0.8f * logoScale, 0.8f * logoScale);

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
		Vector2f bottomRight = Vector2f(ViewSize.X, ViewSize.Y);
		bottomRight.X = ViewSize.X - 24.0f;
		bottomRight.Y -= 10.0f;
		_owner->DrawStringShadow("v1.0.0"_s, charOffset, bottomRight.X, bottomRight.Y, IMenuContainer::FontLayer,
			Alignment::BottomRight, Font::DefaultColor, 0.7f, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);

		// Copyright
		Vector2f bottomLeft = bottomRight;
		bottomLeft.X = 24.0f;
		_owner->DrawStringShadow("© 2016-2022  Dan R."_s, charOffset, bottomLeft.X, bottomLeft.Y, IMenuContainer::FontLayer,
			Alignment::BottomLeft, Font::DefaultColor, 0.7f, 0.4f, 1.2f, 1.2f, 0.46f, 0.8f);

		if (!_owner->_sections.empty()) {
			auto& lastSection = _owner->_sections.back();
			lastSection->OnDraw(this);
		}

		if (_owner->_transitionWhite > 0.0f) {
			DrawSolid(Vector2f::Zero, 950, Vector2f(ViewSize.X, ViewSize.Y), Colorf(1.0f, 1.0f, 1.0f, _owner->_transitionWhite));
		}

		return true;
	}

	void MainMenu::SwitchToSectionPtr(std::unique_ptr<MenuSection> section)
	{
		if (!_sections.empty()) {
			auto& lastSection = _sections.back();
			lastSection->OnHide();
		}

		auto& currentSection = _sections.emplace_back(std::move(section));
		currentSection->OnShow(this);
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
		}
	}

	void MainMenu::ChangeLevel(Jazz2::LevelInitialization&& levelInit)
	{
		_root->ChangeLevel(std::move(levelInit));
	}

	void MainMenu::ApplyPreferencesChanges()
	{
		// Graphics
		Viewport::chain().clear();
		Vector2i res = theApplication().resolutionInt();
		OnInitializeViewport(res.X, res.Y);

		// Sounds
		if (_music != nullptr) {
			_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
		}
	}

	void MainMenu::DrawElement(const StringView& name, int frame, float x, float y, uint16_t z, Alignment align, const Colorf& color, float scaleX, float scaleY, bool additiveBlending)
	{
		auto it = _graphics->find(String::nullTerminatedView(name));
		if (it == _graphics->end()) {
			return;
		}

		if (frame < 0) {
			frame = it->second.FrameOffset + ((int)(_canvas->AnimTime * it->second.FrameCount / it->second.FrameDuration) % it->second.FrameCount);
		}

		GenericGraphicResource* base = it->second.Base;
		Vector2f size = Vector2f(base->FrameDimensions.X * scaleX, base->FrameDimensions.Y * scaleY);
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x - _canvas->ViewSize.X * 0.5f, _canvas->ViewSize.Y * 0.5f - y), size);

		Vector2i texSize = base->TextureDiffuse->size();
		int col = frame % base->FrameConfiguration.X;
		int row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			float(base->FrameDimensions.X) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			float(base->FrameDimensions.Y) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);

		texCoords.W += texCoords.Z;
		texCoords.Z *= -1;

		_canvas->DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, additiveBlending);
	}

	void MainMenu::DrawElement(const StringView& name, float x, float y, uint16_t z, Alignment align, const Colorf& color, const Vector2f& size, const Vector4f& texCoords)
	{
		auto it = _graphics->find(String::nullTerminatedView(name));
		if (it == _graphics->end()) {
			return;
		}

		GenericGraphicResource* base = it->second.Base;
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x - _canvas->ViewSize.X * 0.5f, _canvas->ViewSize.Y * 0.5f - y), size);

		_canvas->DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, false);
	}

	void MainMenu::DrawSolid(float x, float y, uint16_t z, Alignment align, const Vector2f& size, const Colorf& color, bool additiveBlending)
	{
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x - _canvas->ViewSize.X * 0.5f, _canvas->ViewSize.Y * 0.5f - y), size);
		_canvas->DrawSolid(adjustedPos, z, size, color, additiveBlending);
	}

	void MainMenu::DrawStringShadow(const StringView& text, int& charOffset, float x, float y, uint16_t z, Alignment align, const Colorf& color, float scale,
		float angleOffset, float varianceX, float varianceY, float speed, float charSpacing, float lineSpacing)
	{
		if (_logoTransition < 1.0f) {
			float transitionText = (1.0f - _logoTransition) * 2.0f;
			angleOffset += 0.5f * transitionText;
			varianceX += 400.0f * transitionText;
			varianceY += 400.0f * transitionText;
			speed -= speed * 0.6f * transitionText;
		}

		int charOffsetShadow = charOffset;
		_smallFont->DrawString(_canvas.get(), text, charOffsetShadow, x, y + 2.8f * scale, FontShadowLayer,
			align, Colorf(0.0f, 0.0f, 0.0f, 0.29f), scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
		_smallFont->DrawString(_canvas.get(), text, charOffset, x, y, z,
			align, color, scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
	}

	void MainMenu::PlaySfx(const StringView& identifier, float gain)
	{
		auto it = _sounds->find(String::nullTerminatedView(identifier));
		if (it != _sounds->end()) {
			int idx = (it->second.Buffers.size() > 1 ? Random().Next(0, (int)it->second.Buffers.size()) : 0);
			auto& player = _playingSounds.emplace_back(std::make_shared<AudioBufferPlayer>(it->second.Buffers[idx].get()));
			player->setPosition(Vector3f(0.0f, 0.0f, 100.0f));
			player->setGain(gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
			player->setSourceRelative(true);

			player->play();
		} else {
			LOGE_X("Sound effect \"%s\" was not found", identifier.data());
		}
	}

	bool MainMenu::ActionHit(PlayerActions action)
	{
		return ((_pressedActions & ((1 << (int)action) | (1 << (16 + (int)action)))) == (1 << (int)action));
	}

	void MainMenu::UpdatePressedActions()
	{
		auto& input = theApplication().inputManager();
		_pressedActions = ((_pressedActions & 0xffff) << 16);

		if (_pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Up)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Up)]) {
			_pressedActions |= (1 << (int)PlayerActions::Up);
		}
		if (_pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Down)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Down)]) {
			_pressedActions |= (1 << (int)PlayerActions::Down);
		}
		if (_pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Left)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Left)]) {
			_pressedActions |= (1 << (int)PlayerActions::Left);
		}
		if (_pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Right)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Right)]) {
			_pressedActions |= (1 << (int)PlayerActions::Right);
		}
		// Also allow Return (Enter) as confirm key
		if (_pressedKeys[(uint32_t)KeySym::RETURN] || _pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Fire)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Fire)] ||
			_pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Jump)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Jump)]) {
			_pressedActions |= (1 << (int)PlayerActions::Fire);
		}
		if (_pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Menu)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Menu)]) {
			_pressedActions |= (1 << (int)PlayerActions::Menu);
		}
		// Use SwitchWeapon action as Delete key
		if (_pressedKeys[(uint32_t)KeySym::DELETE]) {
			_pressedActions |= (1 << (int)PlayerActions::ChangeWeapon);
		}

		// Try to get 8 connected joysticks
		const JoyMappedState* joyStates[8];
		int jc = 0;
		for (int i = 0; i < IInputManager::MaxNumJoysticks && jc < _countof(joyStates); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[jc++] = &input.joyMappedState(i);
			}
		}

		ButtonName jb; int ji1, ji2, ji3, ji4;

		jb = ControlScheme::Gamepad(0, PlayerActions::Up, ji1);
		if (ji1 >= 0 && ji1 < jc && joyStates[ji1]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int)PlayerActions::Up);
		}
		jb = ControlScheme::Gamepad(0, PlayerActions::Down, ji2);
		if (ji2 >= 0 && ji2 < jc && joyStates[ji2]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int)PlayerActions::Down);
		}
		jb = ControlScheme::Gamepad(0, PlayerActions::Left, ji3);
		if (ji3 >= 0 && ji3 < jc && joyStates[ji3]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int)PlayerActions::Left);
		}
		jb = ControlScheme::Gamepad(0, PlayerActions::Right, ji4);
		if (ji4 >= 0 && ji4 < jc && joyStates[ji4]->isButtonPressed(jb)) {
			_pressedActions |= (1 << (int)PlayerActions::Right);
		}

		// Use analog controls only if all movement buttons are mapped to the same joystick
		if (ji1 == ji2 && ji2 == ji3 && ji3 == ji4 && ji1 >= 0 && ji1 < jc) {
			float x = joyStates[ji1]->axisValue(AxisName::LX);
			float y = joyStates[ji1]->axisValue(AxisName::LY);

			if (x < -0.6f) {
				_pressedActions |= (1 << (int)PlayerActions::Left);
			} else if (x > 0.6f) {
				_pressedActions |= (1 << (int)PlayerActions::Right);
			}
			if (y < -0.6f) {
				_pressedActions |= (1 << (int)PlayerActions::Up);
			} else if (y > 0.6f) {
				_pressedActions |= (1 << (int)PlayerActions::Down);
			}
		}

		jb = ControlScheme::Gamepad(0, PlayerActions::Jump, ji1);
		jb = ControlScheme::Gamepad(0, PlayerActions::Fire, ji2);
		if (ji1 == ji2) ji2 = -1;

		if ((ji1 >= 0 && ji1 < jc && (joyStates[ji1]->isButtonPressed(ButtonName::A) || joyStates[ji1]->isButtonPressed(ButtonName::X))) ||
			(ji2 >= 0 && ji2 < jc && (joyStates[ji2]->isButtonPressed(ButtonName::A) || joyStates[ji2]->isButtonPressed(ButtonName::X)))) {
			_pressedActions |= (1 << (int)PlayerActions::Fire);
		}

		if ((ji1 >= 0 && ji1 < jc && (joyStates[ji1]->isButtonPressed(ButtonName::B) || joyStates[ji1]->isButtonPressed(ButtonName::START))) ||
			(ji2 >= 0 && ji2 < jc && (joyStates[ji2]->isButtonPressed(ButtonName::B) || joyStates[ji2]->isButtonPressed(ButtonName::START)))) {
			_pressedActions |= (1 << (int)PlayerActions::Menu);
		}

		jb = ControlScheme::Gamepad(0, PlayerActions::ChangeWeapon, ji1);
		if (ji1 >= 0 && ji1 < jc && joyStates[ji1]->isButtonPressed(ButtonName::Y)) {
			_pressedActions |= (1 << (int)PlayerActions::ChangeWeapon);
		}
	}

	void MainMenu::PrepareTexturedBackground()
	{
		_tileSet = ContentResolver::Current().RequestTileSet("easter99"_s, 0, true);
		_backgroundColor = Vector3f(0.098f, 0.35f, 1.0f);
		if (_tileSet == nullptr) {
			_tileSet = ContentResolver::Current().RequestTileSet("carrot1"_s, 0, true);
			_backgroundColor = Vector3f(0.007f, 0.18f, 0.4f);
			if (_tileSet == nullptr) {
				_backgroundColor = Vector3f::Zero;
				return;
			}
		}

		auto s = fs::Open(fs::JoinPath({ "Content"_s, "Animations"_s, "main_menu.layer"_s }), FileAccessMode::Read);
		if (s->GetSize() < 8) {
			return;
		}

		int32_t width = s->ReadValue<int32_t>();
		int32_t height = s->ReadValue<int32_t>();

		std::unique_ptr<LayerTile[]> layout = std::make_unique<LayerTile[]>(width * height);

		for (int i = 0; i < (width * height); i++) {
			uint16_t tileType = s->ReadValue<uint16_t>();
			uint8_t tileFlags = s->ReadValue<uint8_t>();
			uint8_t tileModifier = (uint8_t)(tileFlags >> 4);

			LayerTile& tile = layout[i];
			tile.TileID = tileType;
			tile.Flags = (LayerTileFlags)(tileFlags & 0x0f);

			if (tileModifier == 1 /*Translucent*/) {
				tile.Alpha = /*127*/140;
			} else if (tileModifier == 2 /*Invisible*/) {
				tile.Alpha = 0;
			} else {
				tile.Alpha = 255;
			}
		}

		TileMapLayer& newLayer = _texturedBackgroundLayer;
		newLayer.Visible = true;
		newLayer.LayoutSize = Vector2i(width, height);
		newLayer.Layout = std::move(layout);
	}

	void MainMenu::RenderTexturedBackground(RenderQueue& renderQueue)
	{
		auto target = _texturedBackgroundPass._target.get();
		if (target == nullptr) {
			return;
		}

		auto command = &_texturedBackgroundPass._outputRenderCommand;

		auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(_canvas->ViewSize.X, _canvas->ViewSize.Y);
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf(1.0f, 1.0f, 1.0f, 1.0f).Data());

		command->material().uniform("uViewSize")->setFloatValue(_canvas->ViewSize.X, _canvas->ViewSize.Y);
		command->material().uniform("uShift")->setFloatVector(_texturedBackgroundPos.Data());
		command->material().uniform("uHorizonColor")->setFloatVector(_backgroundColor.Data());
		command->material().uniform("uParallaxStarsEnabled")->setFloatValue(0.0f);

		command->setTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
		command->material().setTexture(*target);

		renderQueue.addCommand(command);
	}

	void MainMenu::TexturedBackgroundPass::Initialize()
	{
		bool notInitialized = (_view == nullptr);

		if (notInitialized) {
			Vector2i layoutSize = _owner->_texturedBackgroundLayer.LayoutSize;
			int width = layoutSize.X * TileSet::DefaultTileSize;
			int height = layoutSize.Y * TileSet::DefaultTileSize;

			_camera = std::make_unique<Camera>();
			_camera->setOrthoProjection(0, width, 0, height);
			_camera->setView(0, 0, 0, 1);
			_target = std::make_unique<Texture>(nullptr, Texture::Format::RGB8, width, height);
			_view = std::make_unique<Viewport>(_target.get(), Viewport::DepthStencilFormat::NONE);
			_view->setRootNode(this);
			_view->setCamera(_camera.get());
			_view->setClearMode(Viewport::ClearMode::NEVER);
			_target->setMagFiltering(SamplerFilter::Linear);
			_target->setWrap(SamplerWrapping::Repeat);

			// Prepare render commands
			int renderCommandCount = (width * height) / (TileSet::DefaultTileSize * TileSet::DefaultTileSize);
			_renderCommands.reserve(renderCommandCount);
			for (int i = 0; i < renderCommandCount; i++) {
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
			_outputRenderCommand.material().setShader(ContentResolver::Current().GetShader(PrecompiledShader::TexturedBackground));
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
		Vector2i targetSize = _target->size();

		int renderCommandIndex = 0;
		bool isAnimated = false;

		for (int y = 0; y < layoutSize.Y; y++) {
			for (int x = 0; x < layoutSize.X; x++) {
				LayerTile& tile = layer.Layout[y * layer.LayoutSize.X + x];

				auto command = _renderCommands[renderCommandIndex++].get();

				Vector2i texSize = _owner->_tileSet->TextureDiffuse->size();
				float texScaleX = TileSet::DefaultTileSize / float(texSize.X);
				float texBiasX = (tile.TileID % _owner->_tileSet->TilesPerRow) * TileSet::DefaultTileSize / float(texSize.X);
				float texScaleY = TileSet::DefaultTileSize / float(texSize.Y);
				float texBiasY = (tile.TileID / _owner->_tileSet->TilesPerRow) * TileSet::DefaultTileSize / float(texSize.Y);

				// TODO: Flip normal map somehow
				if ((tile.Flags & LayerTileFlags::FlipX) == LayerTileFlags::FlipX) {
					texBiasX += texScaleX;
					texScaleX *= -1;
				}
				if ((tile.Flags & LayerTileFlags::FlipY) == LayerTileFlags::FlipY) {
					texBiasY += texScaleY;
					texScaleY *= -1;
				}

				if ((targetSize.X & 1) == 1) {
					texBiasX += 0.5f / float(texSize.X);
				}
				if ((targetSize.Y & 1) == 1) {
					texBiasY -= 0.5f / float(texSize.Y);
				}

				auto instanceBlock = command->material().uniformBlock(Material::InstanceBlockName);
				instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(texScaleX, texBiasX, texScaleY, texBiasY);
				instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(TileSet::DefaultTileSize, TileSet::DefaultTileSize);
				instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

				command->setTransformation(Matrix4x4f::Translation(x * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2), y * TileSet::DefaultTileSize + (TileSet::DefaultTileSize / 2), 0.0f));
				command->material().setTexture(*_owner->_tileSet->TextureDiffuse);

				renderQueue.addCommand(command);
			}
		}

		if (!isAnimated && _alreadyRendered) {
			// If it's not animated, it can be rendered only once
			for (int i = Viewport::chain().size() - 1; i >= 0; i--) {
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