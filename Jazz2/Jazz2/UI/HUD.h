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

		void ShowLevelText(const StringView& text);
		void ShowCoins(int count);
		void ShowGems(int count);

	private:
		static constexpr uint16_t MainLayer = 100;
		static constexpr uint16_t ShadowLayer = 80;
		static constexpr uint16_t FontLayer = 400;
		static constexpr uint16_t FontShadowLayer = 120;

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

		void DrawLevelText(int& charOffset);
		void DrawCoins(int& charOffset);
		void DrawGems(int& charOffset);

		void DrawElement(const StringView& name, int frame, float x, float y, uint16_t z, Alignment alignment, const Colorf& color, float scaleX = 1.0f, float scaleY = 1.0f);
		StringView GetCurrentWeapon(Actors::Player* player, WeaponType weapon, Vector2f& offset);
	};
}