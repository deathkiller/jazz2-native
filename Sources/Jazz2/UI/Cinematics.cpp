#include "Cinematics.h"
#include "../PreferencesCache.h"
#include "../PlayerAction.h"
#include "../Input/ControlScheme.h"

#include "../../nCine/Application.h"
#include "../../nCine/Graphics/RenderQueue.h"
#include "../../nCine/Input/JoyMapping.h"
#include "../../nCine/Audio/AudioBufferPlayer.h"
#include "../../nCine/Base/FrameTimer.h"

#include <Containers/StringConcatenable.h>
#include <IO/Compression/DeflateStream.h>

using namespace Jazz2::Input;

namespace Jazz2::UI
{
	Cinematics::Cinematics(IRootController* root, StringView path, Function<bool(IRootController*, bool)>&& callback)
		: _root(root), _callback(std::move(callback)), _frameDelay(0.0f), _frameProgress(0.0f), _framesLeft(0), _frameIndex(0),
			_pressedKeys(ValueInit, (std::size_t)Keys::Count), _pressedActions(0)
	{
		Initialize(path);
	}

	Cinematics::~Cinematics()
	{
		_canvas->setParent(nullptr);
	}

	Vector2i Cinematics::GetViewSize() const
	{
		return _upscalePass.GetViewSize();
	}

	void Cinematics::OnBeginFrame()
	{
		float timeMult = theApplication().GetTimeMult();

		if (_framesLeft <= 0) {
			if (_callback && _callback(_root, _frameDelay != 0.0f)) {
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

		if ((_pressedActions & ((1 << (std::int32_t)PlayerAction::Fire) | (1 << (16 + (std::int32_t)PlayerAction::Fire)))) == (1 << (std::int32_t)PlayerAction::Fire)) {
			if (_callback && _callback(_root, false)) {
				_callback = nullptr;
				_framesLeft = 0;
			}
		}
	}

	void Cinematics::OnInitializeViewport(std::int32_t width, std::int32_t height)
	{
		constexpr float defaultRatio = (float)DefaultWidth / DefaultHeight;
		float currentRatio = (float)width / height;

		std::int32_t w, h;
		if (currentRatio > defaultRatio) {
			w = std::min(DefaultWidth, width);
			h = (std::int32_t)roundf(w / currentRatio);
		} else if (currentRatio < defaultRatio) {
			h = std::min(DefaultHeight, height);
			w = (std::int32_t)roundf(h * currentRatio);
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
		_pressedKeys.set((std::size_t)event.sym);
	}

	void Cinematics::OnKeyReleased(const KeyboardEvent& event)
	{
		_pressedKeys.reset((std::size_t)event.sym);
	}

	void Cinematics::OnTouchEvent(const TouchEvent& event)
	{
		if (event.type == TouchEventType::Down) {
			if (_callback && _callback(_root, false)) {
				_callback = nullptr;
				_framesLeft = 0;
			}
		}
	}

	void Cinematics::Initialize(StringView path)
	{
		theApplication().GetGfxDevice().setWindowTitle("Jazz² Resurrection"_s);

		_canvas = std::make_unique<CinematicsCanvas>(this);

		auto& resolver = ContentResolver::Get();

		if (!LoadCinematicsFromFile(path)) {
			_framesLeft = 0;
			return;
		}

#if defined(WITH_AUDIO) && defined(WITH_OPENMPT)
		_music = resolver.GetMusic(String(path + ".j2b"_s));
		if (_music != nullptr) {
			_music->setGain(PreferencesCache::MasterVolume * PreferencesCache::MusicVolume);
			_music->setSourceRelative(true);
			_music->play();
		}
#endif

		// Mark Fire button as already pressed to avoid some issues
		_pressedActions = (1 << (std::int32_t)PlayerAction::Fire) | (1 << ((std::int32_t)PlayerAction::Fire + 16));
	}

	bool Cinematics::LoadCinematicsFromFile(StringView path)
	{
		// Try "Content" directory first, then "Source" directory
		auto& resolver = ContentResolver::Get();
		auto s = resolver.OpenContentFile(fs::CombinePath("Cinematics"_s, String(path + ".j2v"_s)));
		if (!s->IsValid()) {
			if (auto alternativePath = fs::FindPathCaseInsensitive(fs::CombinePath(resolver.GetSourcePath(), String(path + ".j2v"_s)))) {
				s = fs::Open(alternativePath, FileAccess::Read);
				if (!s->IsValid()) {
					return false;
				}
			}
		}
		
		DEATH_ASSERT(s->GetSize() > 32 && s->GetSize() < 64 * 1024 * 1024,
			("Cannot load \"{}.j2v\" - unexpected file size", path), false);

		// "CineFeed" + file size (uint32_t) + CRC of lowercase filename (uint32_t)
		std::uint8_t internalBuffer[16];
		s->Read(internalBuffer, 16);
		DEATH_ASSERT(strncmp((const char*)internalBuffer, "CineFeed", sizeof("CineFeed") - 1) == 0,
			("Cannot load \"{}.j2v\" - invalid signature", path), false);

		_width = s->ReadValue<std::uint32_t>();
		_height = s->ReadValue<std::uint32_t>();
		s->Seek(2, SeekOrigin::Current); // Bits per pixel
		_frameDelay = s->ReadValue<std::uint16_t>() / (FrameTimer::SecondsPerFrame * 1000); // Delay in milliseconds
		_framesLeft = s->ReadValue<std::uint32_t>();
		s->Seek(20, SeekOrigin::Current);

		_texture = std::make_unique<Texture>("Cinematics", Texture::Format::RGBA8, _width, _height);
		_buffer = std::make_unique<std::uint8_t[]>(_width * _height);
		_lastBuffer = std::make_unique<std::uint8_t[]>(_width * _height);
		_currentFrame = std::make_unique<std::uint32_t[]>(_width * _height);

		// Read all 4 compressed streams
		std::uint32_t totalOffset = s->GetPosition();

		while (totalOffset < s->GetSize()) {
			for (std::int32_t i = 0; i < std::int32_t(arraySize(_decompressedStreams)); i++) {
				std::int32_t bytesLeft = s->ReadValue<std::int32_t>();
				totalOffset += 4 + bytesLeft;
				_compressedStreams[i].FetchFromStream(*s, bytesLeft);
			}
		}

		for (std::int32_t i = 0; i < std::int32_t(arraySize(_decompressedStreams)); i++) {
			// Skip first two bytes (0x78 0xDA)
			_compressedStreams[i].Seek(2, SeekOrigin::Begin);
			_decompressedStreams[i].Open(_compressedStreams[i]);
		}

		LoadSfxList(path);

		return true;
	}

	bool Cinematics::LoadSfxList(StringView path)
	{
#if defined(WITH_AUDIO)
		auto& resolver = ContentResolver::Get();
		auto s = resolver.OpenContentFile(fs::CombinePath("Cinematics"_s, String(path + ".j2sfx"_s)));
		if (!s->IsValid()) {
			return false;
		}

		if (s->GetSize() <= 16 || s->GetSize() >= 64 * 1024 * 1024) {
			LOGE("Cannot load SFX playlist for \"{}.j2v\" - unexpected file size", path);
			return false;
		}

		std::uint64_t signature = s->ReadValue<std::uint64_t>();
		std::uint8_t fileType = s->ReadValue<std::uint8_t>();
		std::uint16_t version = s->ReadValue<std::uint16_t>();
		if (signature != 0x2095A59FF0BFBBEF || fileType != ContentResolver::SfxListFile || version > SfxListVersion) {
			LOGE("Cannot load SFX playlist for \"{}.j2v\" - invalid signature", path);
			return false;
		}

		std::uint32_t sampleCount = s->ReadValue<std::uint16_t>();
		for (std::uint32_t i = 0; i < sampleCount; i++) {
			std::uint8_t stringSize = s->ReadValue<std::uint8_t>();
			String samplePath = String(NoInit, stringSize);
			s->Read(samplePath.data(), stringSize);

			String samplePathNormalized = fs::ToNativeSeparators(samplePath);
			String fullPath = fs::CombinePath("Animations"_s, samplePathNormalized);
			auto sample = resolver.OpenContentFile(fullPath);
			if (sample->IsValid()) {
				_sfxSamples.emplace_back(std::move(sample), fullPath);
			} else {
				_sfxSamples.emplace_back(); // Sample not found
			}
		}

		std::uint32_t itemCount = s->ReadValue<std::uint16_t>();
		for (std::uint32_t i = 0; i < itemCount; i++) {
			auto& item = _sfxPlaylist.emplace_back();
			item.Frame = s->ReadVariableUint32();
			item.Sample = s->ReadValue<std::uint16_t>();
			item.Gain = s->ReadValue<std::uint8_t>() / 255.0f;
			item.Panning = s->ReadValue<std::int8_t>() / 127.0f;
		}

		return true;
#else
		return false;
#endif
	}

	void Cinematics::PrepareNextFrame()
	{
		// Check if palette was changed
		if (ReadValue<std::uint8_t>(0) == 0x01) {
			Read(3, _palette, sizeof(_palette));
		}

		// Read pixels into the buffer
		for (std::int32_t y = 0; y < _height; y++) {
			std::uint8_t c;
			std::int32_t x = 0;
			while ((c = ReadValue<std::uint8_t>(0)) != 0x80) {
				if (c < 0x80) {
					std::int32_t u;
					if (c == 0x00) {
						u = ReadValue<std::uint16_t>(0);
					} else {
						u = c;
					}

					// Read specified number of pixels in row
					for (std::int32_t i = 0; i < u; i++) {
						_buffer[y * _width + x] = ReadValue<std::uint8_t>(3);
						x++;
					}
				} else {
					std::int32_t u;
					if (c == 0x81) {
						u = ReadValue<std::uint16_t>(0);
					} else {
						u = c - 0x6A;
					}

					// Copy specified number of pixels from previous frame
					std::int32_t n = ReadValue<std::uint16_t>(1) + (ReadValue<std::uint8_t>(2) + y - 127) * _width;
					for (std::int32_t i = 0; i < u; i++) {
						_buffer[y * _width + x] = _lastBuffer[n];
						x++;
						n++;
					}
				}
			}
		}

		// Apply current palette to indices
		for (std::int32_t i = 0; i < _width * _height; i++) {
			_currentFrame[i] = _palette[_buffer[i]];
		}

		// Upload new texture to GPU
		_texture->loadFromTexels((unsigned char*)_currentFrame.get(), 0, 0, _width, _height);

		// Create copy of the buffer
		std::memcpy(_lastBuffer.get(), _buffer.get(), _width * _height);

#if defined(WITH_AUDIO)
		for (std::size_t i = 0; i < _sfxPlaylist.size(); i++) {
			if (_sfxPlaylist[i].Frame == _frameIndex) {
				auto& item = _sfxPlaylist[i];
				auto& sample = _sfxSamples[item.Sample];
				if (sample.Buffer == nullptr) {
					continue;
				}

				item.CurrentPlayer = std::make_unique<nCine::AudioBufferPlayer>(sample.Buffer.get());
				item.CurrentPlayer->setPosition(Vector3f(item.Panning, 0.0f, 0.0f));
				item.CurrentPlayer->setAs2D(true);
				item.CurrentPlayer->setGain(_sfxPlaylist[i].Gain * PreferencesCache::MasterVolume * PreferencesCache::SfxVolume);
				item.CurrentPlayer->play();
			}
		}
#endif

		_frameIndex++;
	}

	void Cinematics::Read(std::int32_t streamIndex, void* buffer, std::uint32_t bytes)
	{
		_decompressedStreams[streamIndex].Read(buffer, bytes);
	}

	void Cinematics::UpdatePressedActions()
	{
		auto& input = theApplication().GetInputManager();
		_pressedActions = ((_pressedActions & 0xFFFF) << 16);

		const JoyMappedState* joyStates[ControlScheme::MaxConnectedGamepads];
		std::int32_t joyStatesCount = 0;
		for (std::int32_t i = 0; i < JoyMapping::MaxNumJoysticks && joyStatesCount < std::int32_t(arraySize(joyStates)); i++) {
			if (input.isJoyMapped(i)) {
				joyStates[joyStatesCount++] = &input.joyMappedState(i);
			}
		}

		_pressedActions |= ControlScheme::FetchNavigation(_pressedKeys, ArrayView(joyStates, joyStatesCount));

		// Also allow Menu action as skip key
		if (_pressedActions & (1 << (std::uint32_t)PlayerAction::Menu)) {
			_pressedActions |= (1 << (std::uint32_t)PlayerAction::Fire);
		}
	}

	void Cinematics::CinematicsCanvas::Initialize()
	{
		// Prepare output render command
		_renderCommand.setType(RenderCommand::Type::Sprite);
		_renderCommand.material().setShaderProgramType(Material::ShaderProgramType::Sprite);
		_renderCommand.material().reserveUniformsDataMemory();
		_renderCommand.geometry().setDrawParameters(GL_TRIANGLE_STRIP, 0, 4);

		auto* textureUniform = _renderCommand.material().uniform(Material::TextureUniformName);
		if (textureUniform && textureUniform->GetIntValue(0) != 0) {
			textureUniform->SetIntValue(0); // GL_TEXTURE0
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

		Vector2f frameSize;
		if (PreferencesCache::KeepAspectRatioInCinematics) {
			if (ratioTarget < ratioSource) {
				frameSize = Vector2f(viewSize.Y / ratioSource, viewSize.Y);
			} else {
				frameSize = Vector2f(viewSize.X, viewSize.X * ratioSource);
			}
		} else {
			// Try to adjust ratio a bit, otherwise show black bars or zoom it in
			float ratio = std::clamp(ratioTarget, ratioSource - 0.16f, ratioSource);
			frameSize = Vector2f(viewSize.X, viewSize.X * ratio);
		}

		Vector2f frameOffset = (viewSize.As<float>() - frameSize) * 0.5f;
		frameOffset.X = std::round(frameOffset.X);
		frameOffset.Y = std::round(frameOffset.Y);

		auto* instanceBlock = _renderCommand.material().uniformBlock(Material::InstanceBlockName);
		instanceBlock->GetUniform(Material::TexRectUniformName)->SetFloatValue(1.0f, 0.0f, 1.0f, 0.0f);
		instanceBlock->GetUniform(Material::SpriteSizeUniformName)->SetFloatVector(frameSize.Data());
		instanceBlock->GetUniform(Material::ColorUniformName)->SetFloatVector(Colorf::White.Data());

		_renderCommand.setTransformation(Matrix4x4f::Translation(frameOffset.X, frameOffset.Y, 0.0f));
		_renderCommand.material().setTexture(*_owner->_texture);

		renderQueue.addCommand(&_renderCommand);

		return true;
	}

#if defined(WITH_AUDIO)
	Cinematics::SfxItem::SfxItem()
	{
	}

	Cinematics::SfxItem::SfxItem(std::unique_ptr<Stream> stream, StringView path)
		: Buffer(std::make_unique<AudioBuffer>(std::move(stream), path))
	{
	}
#endif
}