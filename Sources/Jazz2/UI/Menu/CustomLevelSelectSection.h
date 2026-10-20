#pragma once

#include "MenuSection.h"

#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
#	include "../../../nCine/Threading/Thread.h"
#endif
#include "../../../nCine/Graphics/Texture.h"

#include <atomic>
#include <memory>

#include <Threading/Spinlock.h>

namespace Jazz2::UI::Menu
{
	/**
		@brief Custom level selection menu section
		
		Lists the installed custom (standalone) levels and lets the player pick one to play or host as a multiplayer
		server.
	*/
	class CustomLevelSelectSection : public MenuSection
	{
	public:
		/**
		 * @brief Creates a new instance
		 *
		 * @param multiplayer    Whether the selected level should be played as a multiplayer game (online or local)
		 * @param privateServer  Whether the hosted online server should be private (not publicly listed)
		 * @param localGame      Whether the selected level should start as a local splitscreen multiplayer match
		 *                       instead of hosting an online server
		 */
		CustomLevelSelectSection(bool multiplayer = false, bool privateServer = false, bool localGame = false);
		~CustomLevelSelectSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnTouchEvent(const TouchEvent& event, Vector2i viewSize) override;

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ItemData {
			String LevelName;
			String DisplayName;
			/** @brief Path of the level file, to read its preview from */
			String Path;
			float Y = 0.0f;
			/** @brief Whether the level file has a preview, see @ref LevelFlags::HasPreview */
			bool HasPreview = false;
		};

		// A preview read from a level file, as RGBA texels - empty if the level has none that can be read
		struct PreviewData {
			String Path;
			SmallVector<std::uint8_t, 0> Texels;
			Vector2i Size;
		};
#endif

		static constexpr std::int32_t ItemHeight = 20;
		static constexpr std::int32_t TopLine = 31;
		static constexpr std::int32_t BottomLine = 42;
		// Levels whose previews are kept, so going back to one of them shows it at once
		static constexpr std::int32_t MaxCachedPreviews = 8;
		// How far the shadow of the preview reaches out of it
		static constexpr std::int32_t PreviewShadowSpread = 6;

		// Only ever touched by the main thread, the indexing thread hands its levels over through _indexedItems
		SmallVector<ItemData> _items;
		// Levels found by the indexing thread and not yet moved to _items (see TakeIndexedItems())
		SmallVector<ItemData, 0> _indexedItems;
		Death::Threading::Spinlock _indexedItemsLock;
		// Set by the indexing thread (under _indexedItemsLock) once it found all levels, the list is sorted then
		std::atomic<bool> _indexingFinished;
		// Tells the indexing thread to stop, the section is being destroyed
		std::atomic<bool> _indexingCancelled;
		bool _itemsSorted;
		bool _multiplayer;
		bool _privateServer;
		bool _localGame;
		std::int32_t _selectedIndex;
		float _animation;
		float _y;
		float _height;
		float _availableHeight;
		Vector2f _touchStart;
		Vector2f _touchLast;
		float _touchTime;
		float _touchSpeed;
		std::int32_t _pressedCount;
		float _noiseCooldown;
		std::int8_t _touchDirection;
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		Thread _indexingThread;
#endif
		// The preview of the selected level, read once it stays selected for a while (see Formats::LevelFile::PreviewChunk)
		std::unique_ptr<Texture> _preview;
		Vector2i _previewSize;
		// From 0 to 1 as the preview fades and slides in, each time one is shown
		float _previewAnimation;
		// Soft shadow under the preview drawn at the size it's drawn, made again only when that changes
		std::unique_ptr<Texture> _previewShadow;
		Vector2i _previewShadowSize;
		// The level the preview is shown for (or known to have none), by the path, as the list is sorted once all
		// levels are found
		String _previewPath;
		// How long the selection rests on _previewWaitIndex, a preview is read only once it stops moving
		float _previewWait;
		std::int32_t _previewWaitIndex;
		// Most recently read last
		SmallVector<PreviewData, 0> _previewCache;
#if defined(WITH_THREADS) && !defined(DEATH_TARGET_EMSCRIPTEN)
		// Reads the preview of _previewRequest.Path off the main thread, _previewRequestDone is set once it's filled
		Thread _previewThread;
		PreviewData _previewRequest;
		std::atomic<bool> _previewRequestDone;
		bool _previewRequestActive;
#endif

		void ExecuteSelected();
		void UpdatePreview(float timeMult);
		void ShowPreview(const PreviewData& preview);
		void UpdatePreviewShadow(Vector2i size);
		void CachePreview(PreviewData&& preview);
		void DrawPreview(float right, float centerY, float maxWidth);
		void EnsureVisibleSelected(std::int32_t offset = 0);
		void TakeIndexedItems();
		void AddCustomLevels();
		void AddLevel(StringView levelFile);
		void PublishItem(ItemData&& item);

		/** @brief Reads the preview of a level file, it can be called from any thread */
		static void ReadPreview(PreviewData& preview);
	};
}