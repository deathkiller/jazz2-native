#pragma once

#include "MenuContainerBase.h"
#include "../../IStateHandler.h"
#include "../../IRootController.h"
#include "../../Rendering/UpscaleRenderPass.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Graphics/Camera.h"
#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Audio/AudioStreamPlayer.h"

using namespace Jazz2::Tiles;

namespace Jazz2::UI::Menu
{
	class BeginSection;
	class RefreshCacheSection;
	class StartGameOptionsSection;
#if defined(WITH_MULTIPLAYER)
	class CreateServerOptionsSection;
#endif

	/**
		@brief Main menu

		Top-level state handler and @ref MenuContainerBase that drives the standalone main menu. It renders the animated
		menu background and logo, plays the menu music, and hosts the stack of menu sections starting from
		@ref BeginSection.
	*/
	class MainMenu : public IStateHandler, public MenuContainerBase
	{
		DEATH_RUNTIME_OBJECT(IStateHandler, MenuContainerBase);

		friend class BeginSection;
		friend class RefreshCacheSection;
		friend class StartGameOptionsSection;
#if defined(WITH_MULTIPLAYER)
		friend class CreateServerOptionsSection;
#endif

	public:
		/** @{ @name Constants */

		/** @brief Default width of viewport */
		static constexpr int32_t DefaultWidth = 720;
		/** @brief Default height of viewport */
		static constexpr int32_t DefaultHeight = 405;

		/** @} */

		/**
		 * @brief Creates a new instance
		 *
		 * @param root        Root controller the menu is attached to
		 * @param afterIntro  Whether the menu is shown right after the intro, playing a fade-in transition
		 */
		MainMenu(IRootController* root, bool afterIntro);
		~MainMenu() override;

		/** @brief Recreates the menu to the default state */
		void Reset();

		Vector2i GetViewSize() const override;

		void OnBeginFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTextInput(const TextInputEvent& event) override;
		void OnTouchEvent(const nCine::TouchEvent& event) override;

		void ChangeLevel(LevelInitialization&& levelInit) override;
		bool HasResumableState() const override;
		void ResumeSavedState() override;
#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)
		void ConnectToServer(StringView endpoint, std::uint16_t defaultPort) override;
		bool CreateServer(Jazz2::Multiplayer::ServerInitialization&& serverInit) override;
#endif
		void ApplyPreferencesChanges(ChangedPreferencesType type) override;

		// Overridden to add the logo intro wobble on top of the shared implementation
		void DrawStringShadow(StringView text, std::int32_t& charOffset, float x, float y, uint16_t z, Alignment align, const Colorf& color,
			float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f,
			float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f) override;

	private:
		IRootController* _root;

		enum class Preset {
			None,
			Default,
			Carrotus,
			SharewareDemo,
			Xmas
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		class MenuBackgroundCanvas : public Canvas
		{
		public:
			MenuBackgroundCanvas(MainMenu* owner) : _owner(owner) { }

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			MainMenu* _owner;
		};

		class MenuClippedCanvas : public Canvas
		{
		public:
			MenuClippedCanvas(MainMenu* owner) : _owner(owner) { }

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			MainMenu* _owner;
		};

		class MenuOverlayCanvas : public Canvas
		{
		public:
			MenuOverlayCanvas(MainMenu* owner) : _owner(owner) { }

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			MainMenu* _owner;
		};

		class TexturedBackgroundPass : public SceneNode
		{
			friend class MainMenu;

		public:
			TexturedBackgroundPass(MainMenu* owner) : _owner(owner), _alreadyRendered(false)
			{
				setVisitOrderState(SceneNode::VisitOrderState::Disabled);
			}

			void Initialize();

			bool OnDraw(RenderQueue& renderQueue) override;

		private:
			MainMenu* _owner;
			std::unique_ptr<Texture> _target;
			std::unique_ptr<Viewport> _view;
			std::unique_ptr<Camera> _camera;
			SmallVector<std::unique_ptr<RenderCommand>, 0> _renderCommands;
			RenderCommand _outputRenderCommand;
			bool _alreadyRendered;
		};
#endif

		TexturedBackgroundPass _texturedBackgroundPass;
		Rendering::UpscaleRenderPassWithClipping _upscalePass;

		std::unique_ptr<TileSet> _tileSet;
		TileMapLayer _texturedBackgroundLayer;
		Vector2f _texturedBackgroundPos;
		float _texturedBackgroundPhase;
		Preset _preset;

		float _transitionWhite;
		float _logoTransition;
#if defined(WITH_AUDIO)
		std::unique_ptr<AudioStreamPlayer> _music;
#endif
		SmallVector<Tiles::TileMap::DestructibleDebris, 0> _debrisList;

		BitArray _pressedKeys;

		void PlayMenuMusic();
		void UpdateRichPresence();
		void UpdateDebris(float timeMult);
		void DrawDebris(RenderQueue& renderQueue);
		void PrepareTexturedBackground();
		bool TryLoadBackgroundPreset(Preset preset);
		void RenderTexturedBackground(RenderQueue& renderQueue);
		bool RenderLegacyBackground(RenderQueue& renderQueue);

	protected:
		Rendering::UpscaleRenderPassWithClipping& GetUpscalePass() override {
			return _upscalePass;
		}
		const BitArray& GetPressedKeys() const override {
			return _pressedKeys;
		}
	};
}
