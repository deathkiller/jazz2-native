#pragma once

#include "../../AnimState.h"
#include "../../ContentResolver.h"
#include "../../LevelInitialization.h"
#include "../../PlayerActions.h"
#include "../Alignment.h"

#include "../../../nCine/I18n.h"

namespace Jazz2::UI::Menu
{
	class MenuSection;

	enum class ChangedPreferencesType {
		None = 0x00,
		Audio = 0x01,
		Graphics = 0x02,
		Gameplay = 0x04,
		Language = 0x08
	};

	DEFINE_ENUM_OPERATORS(ChangedPreferencesType);

	class IMenuContainer
	{
	public:
		static constexpr uint16_t MainLayer = 600;
		static constexpr uint16_t ShadowLayer = 580;
		static constexpr uint16_t BackgroundLayer = 550;
		static constexpr uint16_t FontLayer = 700;
		static constexpr uint16_t FontShadowLayer = 620;

		virtual ~IMenuContainer() { }

		template<typename T, typename... Params>
		T* SwitchToSection(Params&&... args)
		{
			return static_cast<T*>(SwitchToSectionDirect(std::make_unique<T>(std::forward<Params>(args)...)));
		}

		virtual MenuSection* SwitchToSectionDirect(std::unique_ptr<MenuSection> section) = 0;
		virtual void LeaveSection() = 0;
		virtual void ChangeLevel(Jazz2::LevelInitialization&& levelInit) = 0;
		virtual bool HasResumableState() const = 0;
		virtual void ResumeSavedState() = 0;
#if defined(WITH_MULTIPLAYER)
		virtual bool ConnectToServer(const StringView& address, std::uint16_t port) = 0;
		virtual bool CreateServer(std::uint16_t port) = 0;
#endif
		virtual void ApplyPreferencesChanges(ChangedPreferencesType type) = 0;
		virtual bool ActionPressed(PlayerActions action) = 0;
		virtual bool ActionHit(PlayerActions action) = 0;

		virtual Vector2i GetViewSize() = 0;
		virtual void DrawElement(AnimState state, int32_t frame, float x, float y, uint16_t z, Alignment align,
			const Colorf& color, float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false) = 0;
		virtual void DrawElement(AnimState state, float x, float y, uint16_t z, Alignment align,
			const Colorf& color, const Vector2f& size, const Vector4f& texCoords) = 0;
		virtual void DrawSolid(float x, float y, uint16_t z, Alignment align, const Vector2f& size, const Colorf& color, bool additiveBlending = false) = 0;
		virtual Vector2f MeasureString(const StringView& text, float scale = 1.0f, float charSpacing = 1.0f, float lineSpacing = 1.0f) = 0;
		virtual void DrawStringShadow(const StringView& text, int32_t& charOffset, float x, float y, uint16_t z, Alignment align,
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