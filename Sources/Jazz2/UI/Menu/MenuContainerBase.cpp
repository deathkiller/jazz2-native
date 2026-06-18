#include "MenuContainerBase.h"
#include "MenuResources.h"
#include "../Font.h"
#include "../../PreferencesCache.h"
#include "../../Input/ControlScheme.h"

#include "../../../nCine/Application.h"
#include "../../../nCine/Base/Random.h"
#include "../../../nCine/Input/JoyMapping.h"

#include <algorithm>

using namespace Jazz2::UI::Menu::Resources;

namespace Jazz2::UI::Menu
{
	MenuContainerBase::MenuContainerBase()
		: _contentBounds(), _activeCanvas(ActiveCanvas::Background), _metadata(nullptr), _smallFont(nullptr),
			_mediumFont(nullptr), _pressedActions(0), _lastNavigationFlags(NavigationFlags::AllowAll), _touchButtonsTimer(0.0f)
	{
	}

	MenuContainerBase::~MenuContainerBase()
	{
		// The canvases are detached from the scene graph by the concrete container's destructor (while its upscale
		// pass is still alive); here they are simply released.
	}

	MenuSection* MenuContainerBase::SwitchToSectionDirect(std::unique_ptr<MenuSection> section)
	{
		// Settle any in-flight transition before manipulating the stack to avoid re-entrancy issues
		_transition.Skip();

		MenuSection* outgoing = (_sections.empty() ? nullptr : _sections.back().get());

		TransitionInfo transition = section->GetTransition();
		bool animate = (outgoing != nullptr && _contentBounds != Recti::Empty &&
			transition.Style != TransitionStyle::None && transition.Duration > 0.0f);

		if (!animate && outgoing != nullptr) {
			// Instant switch hides the outgoing section before showing the new one (legacy behavior)
			outgoing->OnHide();
		}

		auto& currentSection = _sections.emplace_back(std::move(section));
		currentSection->OnShow(this);

		if (animate) {
			// Clip to the area covering both sections so the clipped layer stays clipped while they animate
			GetUpscalePass().SetClipRectangle(MenuTransition::CombineClip(outgoing->GetClipRectangle(_contentBounds),
				currentSection->GetClipRectangle(_contentBounds), GetUpscalePass().GetViewSize()));
			_transition.BeginForward(outgoing, transition);
		} else if (_contentBounds != Recti::Empty) {
			GetUpscalePass().SetClipRectangle(currentSection->GetClipRectangle(_contentBounds));
		}

		return currentSection.get();
	}

	void MenuContainerBase::LeaveSection()
	{
		if (_sections.empty()) {
			return;
		}

		// Settle any in-flight transition first - otherwise its cleanup could hide the section we are about to
		// reveal (a forward transition's outgoing section is the one LeaveSection re-shows), leaving _root null
		_transition.Skip();

		std::unique_ptr<MenuSection> outgoing = std::move(_sections.back());
		_sections.pop_back();

		MenuSection* revealed = (_sections.empty() ? nullptr : _sections.back().get());
		if (revealed != nullptr) {
			revealed->OnShow(this);
		}

		TransitionInfo transition = outgoing->GetTransition();
		bool animate = (revealed != nullptr && _contentBounds != Recti::Empty &&
			transition.Style != TransitionStyle::None && transition.Duration > 0.0f);
		if (animate) {
			GetUpscalePass().SetClipRectangle(MenuTransition::CombineClip(outgoing->GetClipRectangle(_contentBounds),
				revealed->GetClipRectangle(_contentBounds), GetUpscalePass().GetViewSize()));
			_transition.BeginBackward(std::move(outgoing), transition);
		} else if (revealed != nullptr && _contentBounds != Recti::Empty) {
			// outgoing is destroyed here (legacy behavior)
			GetUpscalePass().SetClipRectangle(revealed->GetClipRectangle(_contentBounds));
		}
	}

	MenuSection* MenuContainerBase::GetCurrentSection() const
	{
		std::size_t count = _sections.size();
		return (count >= 1 ? _sections[count - 1].get() : nullptr);
	}

	MenuSection* MenuContainerBase::GetUnderlyingSection() const
	{
		std::size_t count = _sections.size();
		return (count >= 2 ? _sections[count - 2].get() : nullptr);
	}

	void MenuContainerBase::DrawSections(Canvas* canvas, ActiveCanvas layer)
	{
		auto drawOne = [layer](MenuSection* section, Canvas* c) {
			switch (layer) {
				case ActiveCanvas::Background: section->OnDraw(c); break;
				case ActiveCanvas::Clipped: section->OnDrawClipped(c); break;
				case ActiveCanvas::Overlay: section->OnDrawOverlay(c); break;
			}
		};

		if (_transition.IsActive()) {
			// Draw the outgoing section first, then the incoming one, each with its own animated transform
			if (auto* outgoing = _transition.GetOutgoing()) {
				_transition.ApplyTransform(canvas, false, canvas->ViewSize);
				drawOne(outgoing, canvas);
			}
			if (auto* incoming = GetCurrentSection()) {
				_transition.ApplyTransform(canvas, true, canvas->ViewSize);
				drawOne(incoming, canvas);
			}
			MenuTransition::ResetTransform(canvas);
		} else if (!_sections.empty()) {
			drawOne(_sections.back().get(), canvas);
		}
	}

	void MenuContainerBase::UpdateActiveSection(float timeMult)
	{
		if (_transition.IsActive() && _transition.Update(timeMult)) {
			// Transition finished - restore the precise clip rectangle of the now-active section
			if (auto* current = GetCurrentSection()) {
				GetUpscalePass().SetClipRectangle(current->GetClipRectangle(_contentBounds));
			}
		}

		// Always update the active (incoming) section so its intro animations run during the transition too
		if (!_sections.empty()) {
			_sections.back()->OnUpdate(timeMult);
		}
	}

	void MenuContainerBase::UpdateContentBounds(Vector2i viewSize)
	{
		float headerY = (viewSize.Y >= 300 ? std::clamp((200.0f * viewSize.Y / viewSize.X) - 40.0f, 30.0f, 70.0f) : 8.0f);
		float footerY = (viewSize.Y >= 300 ? 30.0f : 14.0f);
		_contentBounds = Recti(0, headerY + 30, viewSize.X, viewSize.Y - (headerY + footerY));
	}

	void MenuContainerBase::UpdatePressedActions()
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

		NavigationFlags flags = NavigationFlags::AllowAll;
		if (!_sections.empty()) {
			flags = _sections.back()->GetNavigationFlags();
		}

		_pressedActions |= ControlScheme::FetchNavigation(GetPressedKeys(), ArrayView(joyStates, joyStatesCount), flags);
		if (_lastNavigationFlags != flags) {
			_lastNavigationFlags = flags;
			_pressedActions &= 0xffff;
			_pressedActions |= (_pressedActions << 16);
		}
	}

	bool MenuContainerBase::ActionPressed(PlayerAction action)
	{
		return ((_pressedActions & (1 << (std::int32_t)action)) == (1 << (std::int32_t)action));
	}

	bool MenuContainerBase::ActionHit(PlayerAction action)
	{
		return ((_pressedActions & ((1 << (std::int32_t)action) | (1 << (16 + (std::int32_t)action)))) == (1 << (std::int32_t)action));
	}

	void MenuContainerBase::DrawElement(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float scaleX, float scaleY, bool additiveBlending, bool unaligned)
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

		Vector2i texSize = base->TextureDiffuse->GetSize();
		std::int32_t col = frame % base->FrameConfiguration.X;
		std::int32_t row = frame / base->FrameConfiguration.X;
		Vector4f texCoords = Vector4f(
			float(base->FrameDimensions.X) / float(texSize.X),
			float(base->FrameDimensions.X * col) / float(texSize.X),
			float(base->FrameDimensions.Y) / float(texSize.Y),
			float(base->FrameDimensions.Y * row) / float(texSize.Y)
		);

		std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? res->PaletteOffset : -1);
		currentCanvas->DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, additiveBlending, 0.0f, paletteOffset);
	}

	void MenuContainerBase::DrawElement(AnimState state, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, Vector2f size, const Vector4f& texCoords, bool unaligned)
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

		std::int32_t paletteOffset = ((base->Flags & GenericGraphicResourceFlags::Indexed) == GenericGraphicResourceFlags::Indexed ? res->PaletteOffset : -1);
		currentCanvas->DrawTexture(*base->TextureDiffuse.get(), adjustedPos, z, size, texCoords, color, false, 0.0f, paletteOffset);
	}

	void MenuContainerBase::DrawSolid(float x, float y, std::uint16_t z, Alignment align, Vector2f size, const Colorf& color, bool additiveBlending)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);
		adjustedPos.X = std::round(adjustedPos.X);
		adjustedPos.Y = std::round(adjustedPos.Y);

		currentCanvas->DrawSolid(adjustedPos, z, size, color, additiveBlending);
	}

	void MenuContainerBase::DrawTexture(const Texture& texture, float x, float y, std::uint16_t z, Alignment align, Vector2f size, const Colorf& color, bool unaligned)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		Vector2f adjustedPos = Canvas::ApplyAlignment(align, Vector2f(x, y), size);
		if (!unaligned) {
			adjustedPos.X = std::round(adjustedPos.X);
			adjustedPos.Y = std::round(adjustedPos.Y);
		}

		currentCanvas->DrawTexture(texture, adjustedPos, z, size, Vector4f(1.0f, 0.0f, 1.0f, 0.0f), color);
	}

	Vector2f MenuContainerBase::MeasureString(StringView text, float scale, float charSpacing, float lineSpacing)
	{
		return _smallFont->MeasureString(text, scale, charSpacing, lineSpacing);
	}

	void MenuContainerBase::DrawStringShadow(StringView text, std::int32_t& charOffset, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float scale,
		float angleOffset, float varianceX, float varianceY, float speed, float charSpacing, float lineSpacing)
	{
		Canvas* currentCanvas = GetActiveCanvas();
		std::int32_t charOffsetShadow = charOffset;
		_smallFont->DrawString(currentCanvas, text, charOffsetShadow, x, y + 2.8f * scale, FontShadowLayer,
			align, Colorf(0.0f, 0.0f, 0.0f, 0.29f), scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
		_smallFont->DrawString(currentCanvas, text, charOffset, x, y, z,
			align, color, scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
	}

	void MenuContainerBase::DrawStringGlow(StringView text, std::int32_t& charOffset, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float scale,
		float angleOffset, float varianceX, float varianceY, float speed, float charSpacing, float lineSpacing)
	{
		DrawElement(MenuGlow, 0, x, y, z - 40, align, Colorf(1.0f, 1.0f, 1.0f, 0.4f * scale),
			(MeasureString(text, scale, charSpacing).X + 30.0f) * 0.06f, 4.0f * scale, true, true);

		DrawStringShadow(text, charOffset, x, y, z, align, color, scale, angleOffset, varianceX, varianceY, speed, charSpacing, lineSpacing);
	}

	void MenuContainerBase::PlaySfx(StringView identifier, float gain)
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
			LOGE("Sound effect \"{}\" was not found", identifier);
		}
#endif
	}
}
