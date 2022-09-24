#include "Cinematics.h"
#include "ControlScheme.h"
#include "../PreferencesCache.h"
#include "../PlayerActions.h"

#include "../../nCine/Application.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Graphics/Viewport.h"
#include "../../nCine/Input/IInputManager.h"
#include "../../nCine/Audio/AudioReaderMpt.h"
#include "../../nCine/Base/FrameTimer.h"
#include "../../nCine/IO/CompressionUtils.h"

namespace Jazz2::UI
{
	Cinematics::Cinematics(IRootController* root, const String& path, const std::function<bool(IRootController*, bool)>& callback)
		:
		_root(root),
		_callback(callback),
		_frameDelay(0.0f),
		_frameProgress(0.0f),
		_framesLeft(0),
		_currentOffsets { },
		_pressedKeys((uint32_t)KeySym::COUNT_BASE),
		_pressedActions(0)
	{
		theApplication().gfxDevice().setWindowTitle("Jazz² Resurrection"_s);

		_canvas = std::make_unique<CinematicsCanvas>(this);

		auto& resolver = ContentResolver::Current();

		// Try to load cinematics file
		if (!LoadFromFile(path)) {
			_framesLeft = 0;
			return;
		}

#ifdef WITH_OPENMPT
		_music = resolver.GetMusic(path + ".j2b"_s);
		if (_music != nullptr) {
			_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			_music->setSourceRelative(true);
			_music->play();
		}
#endif

		// Mark Fire button as already pressed to avoid some issues
		_pressedActions = (1 << (int)PlayerActions::Fire) | (1 << ((int)PlayerActions::Fire + 16));
	}

	Cinematics::~Cinematics()
	{
		_canvas->setParent(nullptr);
	}

	void Cinematics::OnBeginFrame()
	{
		float timeMult = theApplication().timeMult();

		if (_framesLeft <= 0) {
			if (_callback != nullptr && _callback(_root, _frameDelay != 0.0f)) {
				_callback = nullptr;
			}
			return;
		}

		_frameProgress += timeMult;

		while (_frameProgress >= _frameDelay) {
			_frameProgress -= _frameDelay;
			_framesLeft--;
			PrepareNextFrame();
		}

		UpdatePressedActions();

		if ((_pressedActions & ((1 << (int)PlayerActions::Fire) | (1 << (16 + (int)PlayerActions::Fire)))) == (1 << (int)PlayerActions::Fire)) {
			if (_callback != nullptr && _callback(_root, false)) {
				_callback = nullptr;
				_framesLeft = 0;
			}
		}
	}

	void Cinematics::OnInitializeViewport(int width, int height)
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
	}

	void Cinematics::OnKeyPressed(const KeyboardEvent& event)
	{
		_pressedKeys.Set((uint32_t)event.sym);
	}

	void Cinematics::OnKeyReleased(const KeyboardEvent& event)
	{
		_pressedKeys.Reset((uint32_t)event.sym);
	}

	void Cinematics::OnTouchEvent(const TouchEvent& event)
	{
		if (event.type == TouchEventType::Down) {
			if (_callback != nullptr && _callback(_root, false)) {
				_callback = nullptr;
				_framesLeft = 0;
			}
		}
	}

	bool Cinematics::LoadFromFile(const String& path)
	{
		// Try "Content" directory first, then "Source" directory
		String fullPath = fs::JoinPath({ "Content"_s, "Cinematics"_s, path + ".j2v" });
		if (!fs::IsReadableFile(fullPath)) {
			fullPath = fs::FindPathCaseInsensitive(fs::JoinPath("Source"_s, path + ".j2v"));
		}

		std::unique_ptr<IFileStream> s = fs::Open(fullPath, FileAccessMode::Read);
		if (s->GetSize() < 32 || s->GetSize() > 64 * 1024 * 1024) {
			return false;
		}

		// "CineFeed" + file size (uint32_t) + CRC of lowercase filename (uint32_t)
		uint8_t internalBuffer[16];
		s->Read(internalBuffer, 16);
		if (strncmp((const char*)internalBuffer, "CineFeed", sizeof("CineFeed") - 1) != 0) {
			return false;
		}

		_width = s->ReadValue<uint32_t>();
		_height = s->ReadValue<uint32_t>();
		s->Seek(2, SeekOrigin::Current); // Bits per pixel
		_frameDelay = s->ReadValue<uint16_t>() / (FrameTimer::SecondsPerFrame * 1000); // Delay in milliseconds
		_framesLeft = s->ReadValue<uint32_t>();
		s->Seek(20, SeekOrigin::Current);

		_texture = std::make_unique<Texture>("Cinematics", Texture::Format::RGBA8, _width, _height);
		_buffer = std::make_unique<uint8_t[]>(_width * _height);
		_lastBuffer = std::make_unique<uint8_t[]>(_width * _height);
		_currentFrame = std::make_unique<uint32_t[]>(_width * _height);

		// Read all 4 compressed streams
		SmallVector<uint8_t, 0> compressedStreams[_countof(_decompressedStreams)];
		uint32_t currentOffsets[_countof(_decompressedStreams)] { };
		uint32_t totalOffset = s->GetPosition();

		while (totalOffset < s->GetSize()) {
			for (int i = 0; i < _countof(_decompressedStreams); i++) {
				uint32_t bytesLeft = s->ReadValue<uint32_t>();
				totalOffset += 4 + bytesLeft;

				compressedStreams[i].resize_for_overwrite(currentOffsets[i] + bytesLeft);

				while (bytesLeft > 0) {
					uint32_t bytesRead = s->Read(&compressedStreams[i][currentOffsets[i]], bytesLeft);
					currentOffsets[i] += bytesRead;
					bytesLeft -= bytesRead;
				}
			}
		}

		for (int i = 0; i < _countof(_decompressedStreams); i++) {
			// Stream 3 contains pixel data and is probably compressed with higher ratio
			_decompressedStreams[i].resize_for_overwrite(currentOffsets[i] * (i == 3 ? 8 : 3));

		Retry:
			int compressedSize = currentOffsets[i] - 2;
			int decompressedSize = _decompressedStreams[i].size();
			auto result = CompressionUtils::Inflate(compressedStreams[i].begin() + 2, compressedSize, _decompressedStreams[i].begin(), decompressedSize);
			if (result == DecompressionResult::BufferTooSmall) {
				_decompressedStreams[i].resize_for_overwrite(_decompressedStreams[i].size() * 2);
				LOGI_X("Cinematics stream %i was larger than expected, resizing buffer to %i", i, _decompressedStreams[i].size());
				goto Retry;
			}
		}

		return true;
	}

	void Cinematics::PrepareNextFrame()
	{
		// Check if palette was changed
		if (ReadValue<uint8_t>(0) == 0x01) {
			Read(3, _palette, sizeof(_palette));
		}

		// Read pixels into the buffer
		for (int y = 0; y < _height; y++) {
			uint8_t c;
			int x = 0;
			while ((c = ReadValue<uint8_t>(0)) != 0x80) {
				if (c < 0x80) {
					int u;
					if (c == 0x00) {
						u = ReadValue<uint16_t>(0);
					} else {
						u = c;
					}

					// Read specified number of pixels in row
					for (int i = 0; i < u; i++) {
						_buffer[y * _width + x] = ReadValue<uint8_t>(3);
						x++;
					}
				} else {
					int u;
					if (c == 0x81) {
						u = ReadValue<uint16_t>(0);
					} else {
						u = c - 0x6A;
					}

					// Copy specified number of pixels from previous frame
					int n = ReadValue<uint16_t>(1) + (ReadValue<uint8_t>(2) + y - 127) * _width;
					for (int i = 0; i < u; i++) {
						_buffer[y * _width + x] = _lastBuffer[n];
						x++;
						n++;
					}
				}
			}
		}

		// Apply current palette to indices
		for (int i = 0; i < _width * _height; i++) {
			_currentFrame[i] = _palette[_buffer[i]];
		}

		// Upload new texture to GPU
		_texture->loadFromTexels((unsigned char*)_currentFrame.get(), 0, 0, _width, _height);

		// Create copy of the buffer
		memcpy(_lastBuffer.get(), _buffer.get(), _width * _height);
	}

	void Cinematics::UpdatePressedActions()
	{
		auto& input = theApplication().inputManager();
		_pressedActions = ((_pressedActions & 0xffff) << 16);

		if (_pressedKeys[(uint32_t)KeySym::RETURN] || _pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Fire)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Fire)] ||
			_pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Jump)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Jump)] ||
			_pressedKeys[(uint32_t)ControlScheme::Key1(0, PlayerActions::Menu)] || _pressedKeys[(uint32_t)ControlScheme::Key2(0, PlayerActions::Menu)]) {
			_pressedActions |= (1 << (int)PlayerActions::Fire);
		}

		// Try to get 8 connected joysticks
		const JoyMappedState* joyStates[8];
		int jc = 0;
		for (int i = 0; i < IInputManager::MaxNumJoysticks && jc < _countof(joyStates); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[jc++] = &input.joyMappedState(i);
			}
		}

		ButtonName jb; int ji1, ji2;
		jb = ControlScheme::Gamepad(0, PlayerActions::Jump, ji1);
		jb = ControlScheme::Gamepad(0, PlayerActions::Fire, ji2);
		if (ji1 == ji2) ji2 = -1;

		if ((ji1 >= 0 && ji1 < jc && (joyStates[ji1]->isButtonPressed(ButtonName::A) || joyStates[ji1]->isButtonPressed(ButtonName::B) || joyStates[ji1]->isButtonPressed(ButtonName::X) || joyStates[ji1]->isButtonPressed(ButtonName::START))) ||
			(ji2 >= 0 && ji2 < jc && (joyStates[ji2]->isButtonPressed(ButtonName::A) || joyStates[ji2]->isButtonPressed(ButtonName::B) || joyStates[ji2]->isButtonPressed(ButtonName::X) || joyStates[ji2]->isButtonPressed(ButtonName::START)))) {
			_pressedActions |= (1 << (int)PlayerActions::Fire);
		}
	}

	void Cinematics::CinematicsCanvas::Initialize()
	{
		// Prepare output render command
		_renderCommand.material().setShaderProgramType(Material::ShaderProgramType::SPRITE);
		_renderCommand.material().reserveUniformsDataMemory();
		_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

		GLUniformCache* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
		if (textureUniform && textureUniform->intValue(0) != 0) {
			textureUniform->setIntValue(0); // GL_TEXTURE0
		}
	}

	bool Cinematics::CinematicsCanvas::OnDraw(RenderQueue& renderQueue)
	{
		if (_owner->_frameDelay == 0.0f) {
			return false;
		}

		Vector2i viewSize = _owner->_upscalePass.GetViewSize();

		float ratioTarget = (float)viewSize.Y / viewSize.X;
		float ratioSource = (float)_owner->_height / _owner->_width;
		// Try to adjust ratio a bit, otherwise show black bars
		float ratio = std::clamp(ratioTarget, ratioSource - 0.16f, ratioSource);

		auto instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->uniform(Material::TexRectUniformName)->setFloatValue(1.0f, 0.0f, -1.0f, 1.0f);
		instanceBlock->uniform(Material::SpriteSizeUniformName)->setFloatValue(viewSize.X, viewSize.X * ratio);
		instanceBlock->uniform(Material::ColorUniformName)->setFloatVector(Colorf::White.Data());

		_renderCommand.setTransformation(Matrix4x4f::Translation(0.0f, 0.0f, 0.0f));
		_renderCommand.material().setTexture(*_owner->_texture);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}
}