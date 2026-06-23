#pragma once

#include "WidgetSection.h"

namespace nCine
{
	class Texture;
}

namespace Jazz2
{
	enum class PlayerColorMode : std::uint8_t;

	namespace Resources
	{
		struct Metadata;
	}
}

namespace Jazz2::UI::Menu
{
	/**
		@brief User profile options menu section

		Lets the player edit the player profile, such as the displayed name, character fur colors, and color mode,
		with a live recolored character preview. Built declaratively on top of @ref WidgetSection using a @ref TextInput
		for the name and @ref CustomValueItem rows for the color pickers and identifier.
	*/
	class UserProfileOptionsSection : public WidgetSection
	{
	public:
		/** @brief Creates a new instance */
		UserProfileOptionsSection();
		~UserProfileOptionsSection() override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;

	protected:
		void OnBackPressed() override;

	private:
		constexpr static std::uint32_t MaxPlayerNameLength = 24;

		bool _isDirty;
		String _localPlayerName;
		std::uint32_t _furColor;
		PlayerColorMode _colorMode;
		TextInput* _nameInput;
		// Live character preview (recolored idle frames of Jazz/Spaz/Lori): indexed metadata + one palette per
		// character (they use different recolor schemes, so the same fur color yields a different palette each)
		Jazz2::Resources::Metadata* _previewMetadata[3];
		bool _previewLoaded;
		std::unique_ptr<nCine::Texture> _previewPalette[3];
		std::uint32_t _previewPaletteColor;
#if defined(DEATH_TARGET_ANDROID)
		String _deviceId;
		Vector2i _initialVisibleSize;
		Recti _currentVisibleBounds;
		float _recalcVisibleBoundsTimeLeft;
#endif

		void RecalcLayoutForScreenKeyboard();
		void CycleFurSection(std::int32_t section, std::int32_t direction);
		void CycleColorMode(std::int32_t direction);
	};
}
