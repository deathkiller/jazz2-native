#pragma once

#include "IMenuContainer.h"
#include "MenuSection.h"
#include "../../IStateHandler.h"
#include "../../LevelHandler.h"
#include "../Canvas.h"
#include "../../ContentResolver.h"

#include "../../../nCine/Graphics/Camera.h"
#include "../../../nCine/Graphics/Shader.h"
#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Audio/AudioBufferPlayer.h"

using namespace Jazz2::Tiles;

namespace Jazz2::UI::Menu
{
	class InGameMenu : public IMenuContainer
	{
	public:
		static constexpr int32_t DefaultWidth = 720;
		static constexpr int32_t DefaultHeight = 405;

		InGameMenu(LevelHandler* root);
		~InGameMenu();

		void OnTouchEvent(const nCine::TouchEvent& event);
		void OnInitializeViewport(int32_t width, int32_t height);

		MenuSection* SwitchToSectionDirect(std::unique_ptr<MenuSection> section) override;
		void LeaveSection() override;
		void ChangeLevel(Jazz2::LevelInitialization&& levelInit) override;
		bool HasResumableState() const override;
		void ResumeSavedState() override;
#if defined(WITH_MULTIPLAYER)
		bool ConnectToServer(const StringView& address, std::uint16_t port) override;
		bool CreateServer(std::uint16_t port) override;
#endif
		void ApplyPreferencesChanges(ChangedPreferencesType type) override;
		bool ActionPressed(PlayerActions action) override;
		bool ActionHit(PlayerActions action) override;

		Vector2i GetViewSize() override {
			return _canvasBackground->ViewSize;
		}

		void DrawElement(AnimState state, int32_t frame, float x, float y, uint16_t z, Alignment align, const Colorf& color,
			float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false) override;
		void DrawElement(AnimState state, float x, float y, uint16_t z, Alignment align, const Colorf& color,
			const Vector2f& size, const Vector4f& texCoords) override;
		void DrawSolid(float x, float y, uint16_t z, Alignment align, const Vector2f& size, const Colorf& color, bool additiveBlending = false) override;
		Vector2f MeasureString(const StringView& text, float scale = 1.0f, float charSpacing = 1.0f, float lineSpacing = 1.0f) override;
		void DrawStringShadow(const StringView& text, int32_t& charOffset, float x, float y, uint16_t z, Alignment align, const Colorf& color,
			float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f,
			float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f) override;
		void PlaySfx(const StringView& identifier, float gain = 1.0f) override;

		void ResumeGame();
		void GoToMainMenu();

	private:
		LevelHandler* _root;

		enum class ActiveCanvas {
			Background,
			Clipped,
			Overlay
		};

		class MenuBackgroundCanvas : public Canvas
		{
		public:
			MenuBackgroundCanvas(InGameMenu* owner) : _owner(owner) { }

			void OnUpdate(float timeMult) override;
			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			InGameMenu* _owner;
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

		std::unique_ptr<MenuBackgroundCanvas> _canvasBackground;
		std::unique_ptr<MenuClippedCanvas> _canvasClipped;
		std::unique_ptr<MenuOverlayCanvas> _canvasOverlay;
		ActiveCanvas _activeCanvas;
		Metadata* _metadata;
		Font* _smallFont;
		Font* _mediumFont;
		SmallVector<std::shared_ptr<AudioBufferPlayer>> _playingSounds;

		SmallVector<std::unique_ptr<MenuSection>, 8> _sections;
		uint32_t _pressedActions;
		float _touchButtonsTimer;

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