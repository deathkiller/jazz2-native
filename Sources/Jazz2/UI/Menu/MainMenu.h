#pragma once

#include "IMenuContainer.h"
#include "MenuSection.h"
#include "../../IStateHandler.h"
#include "../../IRootController.h"
#include "../Canvas.h"
#include "../UpscaleRenderPass.h"
#include "../../ContentResolver.h"
#include "../../Tiles/TileMap.h"

#include "../../../nCine/Graphics/Camera.h"
#include "../../../nCine/Graphics/Shader.h"
#include "../../../nCine/Input/InputEvents.h"
#include "../../../nCine/Audio/AudioStreamPlayer.h"

using namespace Jazz2::Tiles;

namespace Jazz2::UI::Menu
{
	class BeginSection;
	class RefreshCacheSection;
	class StartGameOptionsSection;

	class MainMenu : public IStateHandler, public IMenuContainer
	{
		friend class BeginSection;
		friend class RefreshCacheSection;
		friend class StartGameOptionsSection;

	public:
		static constexpr int DefaultWidth = 720;
		static constexpr int DefaultHeight = 405;

		MainMenu(IRootController* root, bool afterIntro);
		~MainMenu() override;

		void OnBeginFrame() override;
		void OnInitializeViewport(int width, int height) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTouchEvent(const nCine::TouchEvent& event) override;

		void SwitchToSectionDirect(std::unique_ptr<MenuSection> section) override;
		void LeaveSection() override;
		void ChangeLevel(Jazz2::LevelInitialization&& levelInit) override;
		void ApplyPreferencesChanges(ChangedPreferencesType type) override;
		bool ActionPressed(PlayerActions action) override;
		bool ActionHit(PlayerActions action) override;

		Vector2i GetViewSize() override {
			return _canvasBackground->ViewSize;
		}

		void DrawElement(const StringView& name, int frame, float x, float y, uint16_t z, Alignment align, const Colorf& color,
			float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false) override;
		void DrawElement(const StringView& name, float x, float y, uint16_t z, Alignment align, const Colorf& color,
			const Vector2f& size, const Vector4f& texCoords) override;
		void DrawSolid(float x, float y, uint16_t z, Alignment align, const Vector2f& size, const Colorf& color, bool additiveBlending = false) override;
		void DrawStringShadow(const StringView& text, int& charOffset, float x, float y, uint16_t z, Alignment align, const Colorf& color,
			float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f,
			float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f) override;
		void PlaySfx(const StringView& identifier, float gain = 1.0f) override;

	private:
		IRootController* _root;

		enum class ActiveCanvas {
			Background,
			Clipped,
			Overlay
		};

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
				setVisitOrderState(SceneNode::VisitOrderState::DISABLED);
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

		TexturedBackgroundPass _texturedBackgroundPass;
		UI::UpscaleRenderPassWithClipping _upscalePass;

		std::unique_ptr<TileSet> _tileSet;
		TileMapLayer _texturedBackgroundLayer;
		Vector2f _texturedBackgroundPos;
		float _texturedBackgroundPhase;
		Vector3f _backgroundColor;

		std::unique_ptr<MenuBackgroundCanvas> _canvasBackground;
		std::unique_ptr<MenuClippedCanvas> _canvasClipped;
		std::unique_ptr<MenuOverlayCanvas> _canvasOverlay;
		ActiveCanvas _activeCanvas;
		HashMap<String, GraphicResource>* _graphics;
		Font* _smallFont;
		Font* _mediumFont;

		float _transitionWhite;
		float _logoTransition;
		std::unique_ptr<AudioStreamPlayer> _music;
		HashMap<String, SoundResource>* _sounds;
		SmallVector<std::shared_ptr<AudioBufferPlayer>> _playingSounds;

		SmallVector<std::unique_ptr<MenuSection>, 8> _sections;
		BitArray _pressedKeys;
		uint32_t _pressedActions;
		float _touchButtonsTimer;

		void UpdatePressedActions();
		void PrepareTexturedBackground();
		void RenderTexturedBackground(RenderQueue& renderQueue);

		inline Canvas* GetActiveCanvas()
		{
			switch (_activeCanvas) {
				default:
				case ActiveCanvas::Background: return _canvasBackground.get();
				case ActiveCanvas::Clipped: return _canvasClipped.get();
				case ActiveCanvas::Overlay: return _canvasOverlay.get();
			}
		}
	};
}