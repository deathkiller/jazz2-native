#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../../LevelHandler.h"
#include "../../UI/Canvas.h"
#include "../Document/LevelDocument.h"
#include "EditorCamera.h"

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

		void ApplyCamera();
	};
}

#endif
