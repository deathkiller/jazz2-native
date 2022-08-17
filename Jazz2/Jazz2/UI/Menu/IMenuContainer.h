#pragma once

#include "../../ContentResolver.h"
#include "../../PlayerActions.h"
#include "../Alignment.h"

namespace Jazz2::UI::Menu
{
	class MenuSection;

	class IMenuContainer
	{
	public:
		static constexpr uint16_t MainLayer = 100;
		static constexpr uint16_t ShadowLayer = 80;
		static constexpr uint16_t FontLayer = 400;
		static constexpr uint16_t FontShadowLayer = 120;

		template<typename T>
		void SwitchToSection()
		{
			SwitchToSectionPtr(std::make_unique<T>());
		}

		virtual void SwitchToSectionPtr(std::unique_ptr<MenuSection> section) = 0;
		virtual void LeaveSection() = 0;
		virtual void ChangeLevel(Jazz2::LevelInitialization&& levelInit) = 0;
		virtual bool ActionHit(PlayerActions action) = 0;

		virtual void DrawElement(const StringView& name, int frame, float x, float y, uint16_t z, Alignment align,
			const Colorf& color, float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false) = 0;
		virtual void DrawStringShadow(const StringView& text, int charOffset, float x, float y, Alignment align,
			const Colorf& color, float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f,
			float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f) = 0;
		virtual void PlaySfx(const StringView& identifier, float gain = 1.0f) = 0;

		static float EaseOutElastic(float t)
		{
			constexpr float p = 0.3f;
			return powf(2.0f, -10.0f * t) * sinf((t - p / 4.0f) * (2.0f * fPi) / p) + 1.0f;
		}

		static float EaseOutCubic(float t)
		{
			float x = 1.0f - t;
			return 1.0f - x * x * x;
		}
	};
}