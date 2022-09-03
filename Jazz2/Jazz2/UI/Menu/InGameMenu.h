#pragma once

#include "IMenuContainer.h"
#include "MenuSection.h"
#include "../../IStateHandler.h"
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
		static constexpr int DefaultWidth = 720;
		static constexpr int DefaultHeight = 405;

		InGameMenu(LevelHandler* root);
		~InGameMenu();

		void OnTouchEvent(const nCine::TouchEvent& event);

		void SwitchToSectionPtr(std::unique_ptr<MenuSection> section) override;
		void LeaveSection() override;
		void ChangeLevel(Jazz2::LevelInitialization&& levelInit) override;
		bool ActionHit(PlayerActions action) override;

		void DrawElement(const StringView& name, int frame, float x, float y, uint16_t z, Alignment align, const Colorf& color,
			float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false) override;
		void DrawElement(const StringView& name, float x, float y, uint16_t z, Alignment align, const Colorf& color,
			const Vector2f& size, const Vector4f& texCoords) override;
		void DrawStringShadow(const StringView& text, int charOffset, float x, float y, Alignment align, const Colorf& color,
			float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f,
			float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f) override;
		void PlaySfx(const StringView& identifier, float gain = 1.0f) override;

		void ResumeGame();
		void GoToMainMenu();

	private:
		LevelHandler* _root;

		class MenuCanvas : public Canvas
		{
		public:
			MenuCanvas(InGameMenu* owner)
				: _owner(owner)
			{
			}

			void OnUpdate(float timeMult) override;
			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			InGameMenu* _owner;
		};

		std::unique_ptr<MenuCanvas> _canvas;
		HashMap<String, GraphicResource>* _graphics;
		Font* _smallFont;
		Font* _mediumFont;

		HashMap<String, SoundResource>* _sounds;
		SmallVector<std::shared_ptr<AudioBufferPlayer>> _playingSounds;

		SmallVector<std::unique_ptr<MenuSection>, 8> _sections;
		uint32_t _pressedActions;
		float _touchButtonsTimer;

		void UpdatePressedActions();
	};
}