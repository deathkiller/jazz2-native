#pragma once

#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "MenuSection.h"
#include "TextInputBuffer.h"
#include "../../Multiplayer/ServerDiscovery.h"

#include <Threading/Spinlock.h>

namespace Jazz2::UI::Menu
{
	/**
		@brief Server selection menu section
		
		Browses the discovered public and LAN servers and lets the player join one or connect directly by entering an
		address. Servers the local client is compatible with are listed first.
	*/
	class ServerSelectSection : public MenuSection, public Jazz2::Multiplayer::IServerObserver
	{
	public:
		/** @brief Creates a new instance */
		ServerSelectSection();
		~ServerSelectSection();

		Recti GetClipRectangle(const Recti& contentBounds) override;

		/** @brief Opens the platform's on-screen keyboard for the "connect to IP" field, if it has one */
		void ShowScreenKeyboardForIpInput();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnDrawClipped(Canvas* canvas) override;
		void OnDrawOverlay(Canvas* canvas) override;
		void OnKeyPressed(const nCine::KeyboardEvent& event) override;
		void OnTextInput(const nCine::TextInputEvent& event) override;
		void OnTouchEvent(const TouchEvent& event, Vector2i viewSize) override;
		NavigationFlags GetNavigationFlags() const override;

		void OnServerFound(Jazz2::Multiplayer::ServerDescription&& desc) override;

#if defined(DEATH_TARGET_PSP) || defined(DOXYGEN_GENERATING_OUTPUT)
		/**
		 * @brief Switches between Wi-Fi and ad hoc mode and starts discovering servers again
		 *
		 * @partialsupport Available only on @ref DEATH_TARGET_PSP "PlayStation Portable" platform.
		 */
		void ToggleAdhocMode();
#endif

	private:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct ItemData {
			Jazz2::Multiplayer::ServerDescription Desc;
			float Y;

			ItemData(Jazz2::Multiplayer::ServerDescription&& desc);
		};
#endif

		static constexpr std::int32_t ItemHeight = 20;
		static constexpr std::int32_t TopLine = 31;
		static constexpr std::int32_t BottomLine = 42;

		SmallVector<ItemData> _items;
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
		// Servers reported by the discovery thread, waiting for the main thread to merge them into `_items` (see
		// OnServerFound()); declared before `_discovery`, so they outlive its thread
		SmallVector<Jazz2::Multiplayer::ServerDescription, 0> _pendingServers;
		Death::Threading::Spinlock _pendingServersLock;
		// Owned indirectly, so it can be restarted when the transport mode changes (see ToggleAdhocMode())
		std::unique_ptr<Jazz2::Multiplayer::ServerDiscovery> _discovery;
		std::int8_t _touchDirection;

		Jazz2::Multiplayer::ServerDescription _selectedServer;
		float _transitionTime;
		bool _shouldStart;
		bool _isConnecting;
#if defined(DEATH_TARGET_PSP)
		// A transport mode switch in progress (see ToggleAdhocMode()): the discovery thread has been asked to
		// end and OnUpdate() finishes the switch once it has, so the menu never waits on it
		bool _switchingMode;
#endif
		bool _waitForIpInput;
		bool _keyboardVisible;
		TextInputBuffer _ipInput;
#if defined(DEATH_TARGET_ANDROID)
		Vector2i _initialVisibleSize;
		Recti _currentVisibleBounds;
		float _recalcVisibleBoundsTimeLeft;
#endif

		// Two answers describe the same server when they carry the same announced identifier, which is how the
		// IPv4 and IPv6 halves of local discovery are folded into one entry (see ServerDiscovery)
		static bool HasSameUniqueServerID(const Jazz2::Multiplayer::ServerDescription& a, const Jazz2::Multiplayer::ServerDescription& b);
		// Adds an endpoint to an entry's list, keeping the order they were found in and skipping one already there
		static String AppendEndpoint(StringView existing, StringView added);
		// Decides whether the local client can play on a server, from the protocol versions it announced
		static bool IsServerCompatible(const Jazz2::Multiplayer::ServerDescription& desc);

		void ProcessPendingServers();
		void AddOrUpdateServer(Jazz2::Multiplayer::ServerDescription&& desc);
		// Inserts an entry at the end of its group (compatible servers first, then the rest) and returns its index
		std::int32_t InsertItem(ItemData&& item);
		// Moves an entry whose compatibility changed to its new group, keeping the selection on the same server
		void MoveItemToGroup(std::int32_t index);

		void ExecuteSelected();
		void OnAfterTransition();
		void EnsureVisibleSelected(std::int32_t offset = 0);
		void RecalcLayoutForScreenKeyboard();
	};
}

#endif