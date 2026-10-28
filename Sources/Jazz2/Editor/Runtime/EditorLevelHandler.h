#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../LevelHandler.h"
#include "../../UI/Canvas.h"
#include "../Document/LevelDocument.h"
#include "EditorCamera.h"

namespace Jazz2::Rendering
{
	class BlurRenderPass;
}

namespace Jazz2::Editor
{
	/**
		@brief Canvas in the level, drawn among its layers in pixels of the level

		What the editor shows as a part of the level, such as the sprites of the events, is drawn into it when the
		level is drawn, so the layers in front of it cover it.
	*/
	class EditorSceneCanvas : public UI::Canvas
	{
	public:
		/** @brief Draws into the canvas, called when it's drawn */
		Function<void(UI::Canvas&)> DrawContent;

		bool OnDraw(RenderQueue& renderQueue) override;
	};

	/**
		@brief Shows a level being edited

		A level handler without players, scripts or a HUD, which only draws the level. It's loaded from the
		document by the same loader as any level, rendered at the size of the window, and looked at through
		the free @ref EditorCamera. The level music plays muffled, as under the pause menu.
	*/
	class EditorLevelHandler : public LevelHandler
	{
		DEATH_RUNTIME_OBJECT(LevelHandler);

	public:
		/** @brief How much the music is muffled while the level is edited, a bit more than by the pause menu, 1 for not at all */
		static constexpr float EditMusicLowPass = 0.06f;

		/** @brief Creates a new instance, the root controller is the editor session */
		explicit EditorLevelHandler(IRootController* root);
		~EditorLevelHandler() override;

		/**
		 * @brief Loads the level from a document
		 *
		 * @param document   The level
		 * @param levelName  Name of the level as `episode/level`
		 * @param fullPath   Path of the level file, may be empty for a level that wasn't saved yet
		 */
		bool Initialize(const LevelDocument& document, StringView levelName, StringView fullPath);

		/** @brief Returns the camera */
		EditorCamera& GetCamera() {
			return _camera;
		}
		/** @brief Returns the viewport, or `nullptr` before the viewports are initialized */
		Rendering::PlayerViewport* GetViewport() const;
		/** @brief Returns the size of the view the game would show the level in, for the parallax of the layers */
		Vector2i GetReferenceViewSize() const {
			return _referenceViewSize;
		}

		/** @brief Sets what is drawn into the level, among its layers, see @ref EditorSceneCanvas */
		void SetSceneContent(Function<void(UI::Canvas&)>&& drawContent);

		/**
		 * @brief Places the camera before the level is loaded, otherwise it starts where the level does
		 *
		 * With @p relative the scale is relative to the scale at which the view shows as much as the game, which is
		 * known only once the view is, and it's snapped to the nearest step of the zoom then.
		 */
		void SetInitialCamera(Vector2f center, float scale, bool relative = false);
		/** @brief Sets how much the music is muffled, 1 for not at all */
		void SetMusicLowPass(float value);
		/** @brief Returns how much the music is muffled, 1 for not at all */
		float GetMusicLowPass() const {
			return _musicLowPass;
		}
		/** @brief Pauses or continues the music, while the level is played */
		void SetMusicPaused(bool paused);
		/** @brief Shows level properties that don't need the level loaded again, such as the water level, starts other music */
		void ApplyLevelProperties(const Formats::LevelProperties& properties);
		/** @brief Returns `true` if the level has to be loaded again to show the document as it is now */
		bool NeedsReload(const LevelDocument& document) const;
		/** @brief Returns the last image of the level as shown, it stays while the rendering is suspended */
		Texture* GetFrameTexture() const {
			return _upscalePass.GetTargetTexture();
		}
		/**
		 * @brief Draws a node into the image of the level, over the level, see @ref GetFrameTexture()
		 *
		 * The handler doesn't own it, it's taken out of the image when the handler is destroyed. Does nothing before the
		 * viewports are initialized.
		 */
		void SetFrameContent(SceneNode* node);
		/**
		 * @brief Blurs the image of the level with what is drawn into it, below a layer and as a whole
		 *
		 * Called every frame. With @p below, the image is blurred in the middle of being drawn when it's asked for by
		 * @ref RequestFrameBlurBelow(), with @p whole once it's drawn, see @ref GetFrameBlur(). The passes are made the
		 * first time and only drawn while they're enabled. Nothing is blurred without the blur effects.
		 */
		void SetFrameBlur(bool below, bool whole);
		/**
		 * @brief Blurs the image of the level as it's drawn below a layer in this frame, returns the blurred image or `nullptr`
		 *
		 * Called while the image is visited, by what is drawn into it over the layer, see @ref SetFrameBlur().
		 */
		Texture* RequestFrameBlurBelow(std::uint16_t layer);
		/** @brief Returns `true` if the image can be blurred below a layer in this frame, see @ref RequestFrameBlurBelow() */
		bool CanBlurFrameBelow() const;
		/** @brief Returns the whole image of the level blurred with what is drawn into it, or `nullptr` if it isn't blurred, see @ref SetFrameBlur() */
		Texture* GetFrameBlur() const;

		bool IsPausable() const override;

		void OnBeginFrame() override;
		void OnEndFrame() override;
		void OnInitializeViewport(std::int32_t width, std::int32_t height) override;

		void OnKeyPressed(const KeyboardEvent& event) override;
		void OnKeyReleased(const KeyboardEvent& event) override;
		void OnTextInput(const TextInputEvent& event) override;
		void OnTouchEvent(const TouchEvent& event) override;

	protected:
		ViewportSetup GetViewportSetup(std::int32_t width, std::int32_t height) override;
		bool LoadLevel(LevelDescriptor& descriptor) override;
		void LoadLevelScripts(StringView fullPath) override;
		void SpawnPlayers(const LevelInitialization& levelInit) override;

	private:
		EditorCamera _camera;
		SmallVector<std::uint8_t, 0> _levelData;
		String _fullPath;
		// What the level was loaded with that can't change without loading it again
		String _loadedTileSet;
		LevelFlags _loadedFlags;
		Vector2i _referenceViewSize;
		float _musicLowPass;
		bool _musicPaused;
		bool _cameraPlaced;
		Vector2f _initialCameraCenter;
		float _initialCameraScale;
		bool _initialCameraRelative = false;
		std::unique_ptr<EditorSceneCanvas> _sceneCanvas;
		// Downsampled, then blurred at a half and at a quarter of the size, as the bloom of the level is
		struct FrameBlur
		{
			std::unique_ptr<Rendering::BlurRenderPass> Passes[5];
			Vector2i Size;

			void Prepare(Texture* frame, Rendering::PlayerViewport* viewport);
			bool IsInChain() const;
			void RemoveFromChain();
			void InsertIntoChain();
			Texture* GetTarget() const;
		};

		// Drawn in the middle of the image, see Viewport::AddSplitPass(), and after the image
		FrameBlur _belowBlur;
		std::uint32_t _belowBlurBits = 0;
		FrameBlur _wholeBlur;
		// Only compared with the children of the image, it may be destroyed already
		SceneNode* _frameContent = nullptr;

		void ApplyCamera();
	};
}

#endif
