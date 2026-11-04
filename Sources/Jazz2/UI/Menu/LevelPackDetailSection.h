#pragma once

#if defined(WITH_LEVEL_PACKS) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MenuSection.h"
#include "EpisodeSelectSection.h"
#include "../../LevelPacks/LevelPackManifest.h"

#include "../../../nCine/Graphics/Texture.h"

#include <memory>

namespace Jazz2::UI::Menu
{
	/**
		@brief Menu section of one installed level pack

		Shows the preview and the details of a pack and lists its episodes and levels, any of them can be started.
		The pack can be removed from here too.
	*/
	class LevelPackDetailSection : public MenuSection
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param pack           The pack
		 * @param mode           How a level of the pack is started
		 * @param privateServer  Whether a hosted online server should be private
		 */
		LevelPackDetailSection(std::shared_ptr<const LevelPacks::LevelPackManifest> pack, EpisodeSelectMode mode, bool privateServer);

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, Vector2i viewSize) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		enum class ItemType {
			Episode,
			Level,
			Remove
		};

		struct ItemData {
			ItemType Type;
			String LevelName;
			String DisplayName;
			float Y = 0.0f;
		};
#endif

		static constexpr std::int32_t TopLine = 31;
		static constexpr std::int32_t BottomLine = 42;
		static constexpr float ItemHeight = 20.0f;
		static constexpr float HeaderHeight = 84.0f;

		std::shared_ptr<const LevelPacks::LevelPackManifest> _pack;
		SmallVector<ItemData, 0> _items;
		EpisodeSelectMode _mode;
		bool _privateServer;
		bool _confirmRemove;
		std::int32_t _selectedIndex;
		float _animation;
		float _y;
		float _height;
		float _availableHeight;
		float _noiseCooldown;
		std::int32_t _pressedCount;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;
		std::unique_ptr<Texture> _preview;
		Vector2i _previewSize;

		void MoveSelection(std::int32_t offset);
		void EnsureVisibleSelected();
		void ExecuteSelected();
		void StartLevel(StringView levelName);
	};
}

#endif
