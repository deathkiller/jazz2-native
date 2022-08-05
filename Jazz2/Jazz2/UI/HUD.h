#pragma once

#include "Canvas.h"
#include "Font.h"
#include "../ILevelHandler.h"
#include "../Actors/Player.h"

namespace Jazz2::UI
{
	class HUD : public Canvas
	{
	public:
		HUD(LevelHandler* levelHandler);

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;
		void OnTouchEvent(const TouchEvent& event, uint32_t& overrideActions);

		void ShowLevelText(const StringView& text);
		void ShowCoins(int count);
		void ShowGems(int count);

	private:
		struct TouchButtonInfo {
			PlayerActions Action;

			float Left;
			float Top;
			float Width;
			float Height;

			GraphicResource* Graphics;
			int CurrentPointerId;
			Alignment Align;
		};

		static constexpr Alignment AllowRollover = (Alignment)0x80;

		static constexpr uint16_t MainLayer = 100;
		static constexpr uint16_t ShadowLayer = 80;
		static constexpr uint16_t FontLayer = 400;
		static constexpr uint16_t FontShadowLayer = 120;
		static constexpr uint16_t TouchButtonsLayer = 900;

		static constexpr int32_t TouchButtonsCount = 9;
		static constexpr float DpadLeft = 0.02f;
		static constexpr float DpadBottom = 0.1f;
		static constexpr float DpadThreshold = 0.09f;
		static constexpr float DpadSize = 0.37f;
		static constexpr float ButtonSize = 0.172f;
		static constexpr float SmallButtonSize = 0.098f;

		LevelHandler* _levelHandler;
		HashMap<String, GraphicResource>* _graphics;
		std::shared_ptr<Actors::Player> _attachedPlayer;
		std::unique_ptr<Font> _smallFont;

		String _levelText;
		float _levelTextTime;
		int _coins;
		int _gems;
		float _coinsTime;
		float _gemsTime;

		TouchButtonInfo _touchButtons[TouchButtonsCount];
		float _touchButtonsTimer;

		void DrawLevelText(int& charOffset);
		void DrawCoins(int& charOffset);
		void DrawGems(int& charOffset);

		void DrawElement(const StringView& name, int frame, float x, float y, uint16_t z, Alignment align, const Colorf& color, float scaleX = 1.0f, float scaleY = 1.0f);
		StringView GetCurrentWeapon(Actors::Player* player, WeaponType weapon, Vector2f& offset);

		TouchButtonInfo CreateTouchButton(PlayerActions action, const StringView& identifier, Alignment align, float x, float y, float w, float h);
		bool IsOnButton(const TouchButtonInfo& button, float x, float y);
	};
}