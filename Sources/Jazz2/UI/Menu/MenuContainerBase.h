#pragma once

#include "IMenuContainer.h"
#include "MenuSection.h"
#include "MenuTransition.h"
#include "../Canvas.h"
#include "../../ContentResolver.h"
#include "../../Rendering/UpscaleRenderPass.h"

#include "../../../nCine/Input/InputEvents.h"

#if defined(WITH_AUDIO)
#	include "../../../nCine/Audio/AudioBufferPlayer.h"
#endif

namespace Jazz2::UI::Menu
{
	/**
		@brief Shared base of the menu containers

		Common implementation behind both @ref MainMenu and @ref InGameMenu. It owns the stack of @ref MenuSection
		screens, the section @ref MenuTransition, the three drawing layers (background, scissor-clipped middle, and
		overlay), input state, and content bounds, and implements all of the shared @ref IMenuContainer drawing,
		string, sound and navigation primitives in a single place. Subclasses provide only what genuinely differs
		through a small set of hooks (@ref GetUpscalePass, @ref GetPressedKeys) and keep their own backdrop, logo and
		per-container behavior.

		@note The shared state is intentionally public: each container hosts its own @ref Canvas renderer classes that
			composite the section layers, and those renderers (not being derived from this base) need direct access.
	*/
	class MenuContainerBase : public IMenuContainer
	{
		DEATH_RUNTIME_OBJECT(IMenuContainer);

	public:
		MenuContainerBase();
		~MenuContainerBase() override;

		MenuSection* SwitchToSectionDirect(std::unique_ptr<MenuSection> section) override;
		void LeaveSection() override;
		MenuSection* GetCurrentSection() const override;
		MenuSection* GetUnderlyingSection() const override;
		bool ActionPressed(PlayerAction action) override;
		bool ActionHit(PlayerAction action) override;

		Recti GetContentBounds() const override {
			return _contentBounds;
		}

		void DrawElement(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color,
			float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false, bool unaligned = false) override;
		void DrawElement(AnimState state, float x, float y, std::uint16_t z, Alignment align, const Colorf& color,
			Vector2f size, const Vector4f& texCoords, bool unaligned = false) override;
		void DrawSolid(float x, float y, std::uint16_t z, Alignment align, Vector2f size, const Colorf& color, bool additiveBlending = false) override;
		void DrawTexture(const Texture& texture, float x, float y, std::uint16_t z, Alignment align, Vector2f size, const Colorf& color, bool unaligned = false) override;
		Vector2f MeasureString(StringView text, float scale = 1.0f, float charSpacing = 1.0f, float lineSpacing = 1.0f) override;
		void DrawStringShadow(StringView text, std::int32_t& charOffset, float x, float y, std::uint16_t z, Alignment align, const Colorf& color,
			float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f,
			float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f) override;
		void DrawStringGlow(StringView text, std::int32_t& charOffset, float x, float y, std::uint16_t z, Alignment align, const Colorf& color,
			float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f,
			float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f) override;
		void PlaySfx(StringView identifier, float gain = 1.0f) override;

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Which of the three drawing layers the section primitives currently target
		enum class ActiveCanvas {
			Background,
			Clipped,
			Overlay
		};

		// Shared state - public so the per-container Canvas renderer classes can access it directly
		SmallVector<std::unique_ptr<MenuSection>, 8> _sections;
		MenuTransition _transition;
		Recti _contentBounds;
		std::unique_ptr<Canvas> _canvasBackground;
		std::unique_ptr<Canvas> _canvasClipped;
		std::unique_ptr<Canvas> _canvasOverlay;
		ActiveCanvas _activeCanvas;
		Metadata* _metadata;
		Font* _smallFont;
		Font* _mediumFont;
		std::uint32_t _pressedActions;
		NavigationFlags _lastNavigationFlags;
		float _touchButtonsTimer;
#	if defined(WITH_AUDIO)
		SmallVector<std::shared_ptr<AudioBufferPlayer>> _playingSounds;
#	endif

		/** @brief Returns the canvas of the currently active drawing layer */
		Canvas* GetActiveCanvas() {
			switch (_activeCanvas) {
				default:
				case ActiveCanvas::Background: return _canvasBackground.get();
				case ActiveCanvas::Clipped: return _canvasClipped.get();
				case ActiveCanvas::Overlay: return _canvasOverlay.get();
			}
		}

		/** @brief Draws the active section(s) on the given layer, compositing the transition pair when one is playing */
		void DrawSections(Canvas* canvas, ActiveCanvas layer);
		/** @brief Updates the active section, or advances the transition (restoring the precise clip when it finishes) */
		void UpdateActiveSection(float timeMult);
		/** @brief Recomputes navigation actions for the current frame */
		void UpdatePressedActions();
		/** @brief Recomputes content bounds for the given viewport size */
		void UpdateContentBounds(Vector2i viewSize);
#endif

	protected:
		/** @brief Returns the upscale render pass used to present the menu (and to set the clip rectangle) */
		virtual Rendering::UpscaleRenderPassWithClipping& GetUpscalePass() = 0;
		/** @brief Returns the set of currently pressed keys used for navigation */
		virtual const BitArray& GetPressedKeys() const = 0;
	};
}
