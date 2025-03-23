#pragma once

#include "IMenuContainer.h"
#include "MenuSection.h"
#include "../../LevelHandler.h"
#include "../Canvas.h"
#include "../../ContentResolver.h"

#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Audio/AudioBufferPlayer.h"

using namespace Jazz2::Tiles;

namespace Jazz2::UI::Menu
{
	/** @brief In-game menu */
	class InGameMenu : public IMenuContainer
	{
		DEATH_RUNTIME_OBJECT(IMenuContainer);

	public:
		/** @{ @name Constants */

		/** @brief Default width of viewport */
		static constexpr std::int32_t DefaultWidth = 720;
		/** @brief Default height of viewport */
		static constexpr std::int32_t DefaultHeight = 405;

		/** @} */

		InGameMenu(LevelHandler* root);
		~InGameMenu();

		/** @brief Called when a key is pressed */
		void OnKeyPressed(const nCine::KeyboardEvent& event);
		/** @brief Called when a key is released */
		void OnKeyReleased(const nCine::KeyboardEvent& event);
		/** @brief Called when a touch event is triggered */
		void OnTouchEvent(const nCine::TouchEvent& event);
		/** @brief Called when the viewport needs to be initialized (e.g., when the resolution is changed) */
		void OnInitializeViewport(std::int32_t width, std::int32_t height);

		MenuSection* SwitchToSectionDirect(std::unique_ptr<MenuSection> section) override;
		void LeaveSection() override;
		MenuSection* GetUnderlyingSection() const override;
		void ChangeLevel(LevelInitialization&& levelInit) override;
		bool HasResumableState() const override;
		void ResumeSavedState() override;
#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)
		void ConnectToServer(StringView endpoint, std::uint16_t defaultPort) override;
		bool CreateServer(Jazz2::Multiplayer::ServerInitialization&& serverInit) override;
#endif
		void ApplyPreferencesChanges(ChangedPreferencesType type) override;
		bool ActionPressed(PlayerAction action) override;
		bool ActionHit(PlayerAction action) override;

		Vector2i GetViewSize() const override {
			return _canvasBackground->ViewSize;
		}

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
		void PlaySfx(StringView identifier, float gain = 1.0f) override;

		/** @brief Returns `true` if the level handler is on a local session */
		bool IsLocalSession() const;
		/** @brief Hides the in-game menu and resumes paused game */
		void ResumeGame();
		/** @brief Leaves the paused game and switches back to the main menu */
		void GoToMainMenu();

	private:
		LevelHandler* _root;

		enum class ActiveCanvas {
			Background,
			Clipped,
			Overlay
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class MenuBackgroundCanvas : public Canvas
		{
		public:
			MenuBackgroundCanvas(InGameMenu* owner) : _owner(owner) { }

			void OnUpdate(float timeMult) override;
			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			InGameMenu* _owner;

			void DrawViewportSeparators();
		};

		class MenuClippedCanvas : public Canvas
		{
		public:
			MenuClippedCanvas(InGameMenu* owner) : _owner(owner) { }

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			InGameMenu* _owner;
		};

		class MenuOverlayCanvas : public Canvas
		{
		public:
			MenuOverlayCanvas(InGameMenu* owner) : _owner(owner) { }

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			InGameMenu* _owner;
		};
#endif

		Recti _contentBounds;
		std::unique_ptr<MenuBackgroundCanvas> _canvasBackground;
		std::unique_ptr<MenuClippedCanvas> _canvasClipped;
		std::unique_ptr<MenuOverlayCanvas> _canvasOverlay;
		ActiveCanvas _activeCanvas;
		Metadata* _metadata;
		Font* _smallFont;
		Font* _mediumFont;
#if defined(WITH_AUDIO)
		SmallVector<std::shared_ptr<AudioBufferPlayer>> _playingSounds;
#endif

		SmallVector<std::unique_ptr<MenuSection>, 8> _sections;
		std::uint32_t _pressedActions;
		NavigationFlags _lastNavigationFlags;
		float _touchButtonsTimer;

		void UpdateContentBounds(Vector2i viewSize);
		void UpdatePressedActions();

		inline Canvas* GetActiveCanvas()
		{
			switch (_activeCanvas) {
				default:
				case ActiveCanvas::Background: return _canvasBackground.get();
				case ActiveCanvas::Clipped: return _canvasClipped.get();
				case ActiveCanvas::Overlay: return _canvasOverlay.get();
			}
		}
	};
}