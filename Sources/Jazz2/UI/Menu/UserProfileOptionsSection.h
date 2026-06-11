#pragma once

#include "ScrollableMenuSection.h"
#include "TextInputBuffer.h"

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
#ifndef DOXYGEN_GENERATING_OUTPUT
	enum class UserProfileOptionsItemType {
#if (defined(DEATH_TARGET_WINDOWS) && !defined(DEATH_TARGET_WINDOWS_RT)) || defined(DEATH_TARGET_UNIX)
		EnableDiscordIntegration,
#endif
		PlayerName,
		FurColor1,
		FurColor2,
		FurColor3,
		FurColor4,
		ColorMode,
#if defined(WITH_MULTIPLAYER)
		UniquePlayerID
#endif
	};

	struct UserProfileOptionsItem {
		UserProfileOptionsItemType Type;
		StringView DisplayName;
		bool HasBooleanValue;
		bool IsReadOnly;
	};
#endif

	class UserProfileOptionsSection : public ScrollableMenuSection<UserProfileOptionsItem>
	{
	public:
		UserProfileOptionsSection();
		~UserProfileOptionsSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		void OnTextInput(const nCine::TextInputEvent& event) override;
		NavigationFlags GetNavigationFlags() const override;

	protected:
		void OnHandleInput() override;
		void OnTouchEvent(const nCine::TouchEvent& event, Vector2i viewSize) override;
		void OnTouchUp(std::int32_t newIndex, Vector2i viewSize, Vector2i touchPos) override;
		void OnLayoutItem(Canvas* canvas, ListViewItem& item) override;
		void OnDrawItem(Canvas* canvas, ListViewItem& item, std::int32_t& charOffset, bool isSelected) override;
		void OnExecuteSelected() override;
		void OnBackPressed() override;

	private:
		constexpr static std::uint32_t MaxPlayerNameLength = 24;

		bool _isDirty;
		bool _waitForInput;
		TextInputBuffer _textInput;
		String _localPlayerName;
		std::uint32_t _furColor;
		PlayerColorMode _colorMode;
		// Live character preview (recolored idle frames of Jazz/Spaz/Lori): indexed metadata + the shared palette
		Jazz2::Resources::Metadata* _previewMetadata[3];
		bool _previewLoaded;
		std::unique_ptr<nCine::Texture> _previewPalette;
		std::uint32_t _previewPaletteColor;
#if defined(DEATH_TARGET_ANDROID)
		String _deviceId;
		Vector2i _initialVisibleSize;
		Recti _currentVisibleBounds;
		float _recalcVisibleBoundsTimeLeft;
#endif

		void RecalcLayoutForScreenKeyboard();
		// Returns the fur section index (0..3) for a FurColor* item type, or -1 if it's not a color item
		static std::int32_t GetFurSectionIndex(UserProfileOptionsItemType type);
		void CycleFurSection(std::int32_t section, std::int32_t direction);
		void CycleColorMode(std::int32_t direction);
	};
}