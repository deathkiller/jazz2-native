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
		@brief Level packs menu section

		Shows the installed level packs as a grid of tiles with a preview each, several of them in a row if the
		screen is wide enough. A pack is opened in @ref LevelPackDetailSection.
	*/
	class LevelPacksSection : public MenuSection
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param mode           How a level of a pack is started
		 * @param privateServer  Whether a hosted online server should be private
		 */
		LevelPacksSection(EpisodeSelectMode mode = EpisodeSelectMode::Singleplayer, bool privateServer = false);

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, Vector2i viewSize) override;

		/** @brief Makes the preview of a pack, its own picture or the preview of its first level, `nullptr` if it has none */
		static std::unique_ptr<Texture> LoadPreview(const LevelPacks::LevelPackManifest& pack, Vector2i& size);
		/** @brief Returns @p text shortened to fit @p maxWidth when drawn at @p scale */
		static String FitText(IMenuContainer* root, StringView text, float scale, float maxWidth);

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ItemData {
			std::shared_ptr<const LevelPacks::LevelPackManifest> Pack;
			std::unique_ptr<Texture> Preview;
			Vector2i PreviewSize;
			bool PreviewLoaded = false;
			Rectf Bounds;
		};
#endif

		static constexpr std::int32_t TopLine = 31;
		static constexpr std::int32_t BottomLine = 42;
		static constexpr float PreviewWidth = 128.0f;
		static constexpr float PreviewHeight = 72.0f;
		static constexpr float TileWidth = PreviewWidth + 8.0f;
		static constexpr float TileHeight = PreviewHeight + 34.0f;
		static constexpr float TileGap = 10.0f;

		SmallVector<ItemData, 0> _items;
		EpisodeSelectMode _mode;
		bool _privateServer;
		std::int32_t _selectedIndex;
		std::int32_t _columns;
		float _animation;
		float _y;
		float _height;
		float _availableHeight;
		float _noiseCooldown;
		std::int32_t _pressedCount;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;

		void RefreshItems();
		void MoveSelection(std::int32_t offset);
		void EnsureVisibleSelected();
		void ExecuteSelected();
	};
}

#endif
