#pragma once

#include "Canvas.h"
#include "Font.h"
#include "RgbLights.h"
#include "../ILevelHandler.h"
#include "../Actors/Player.h"

#include "../../nCine/Input/InputEvents.h"

namespace Jazz2::UI
{
	class HUD : public Canvas
	{
	public:
		static constexpr float DpadLeft = 0.02f;
		static constexpr float DpadBottom = 0.1f;
		static constexpr float DpadThreshold = 0.09f;
		static constexpr float DpadSize = 0.37f;
		static constexpr float ButtonSize = 0.172f;
		static constexpr float SmallButtonSize = 0.098f;

		HUD(LevelHandler* levelHandler);
		~HUD();

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;
		void OnTouchEvent(const TouchEvent& event, uint32_t& overrideActions);

		void ShowLevelText(const StringView& text);
		void ShowCoins(int count);
		void ShowGems(int count);

		void BeginFadeIn();
		void BeginFadeOut(float delay = 0.0f);

	private:
		enum class TransitionState {
			None,
			FadeIn,
			FadeOut,
			WaitingForFadeOut
		};

		struct TouchButtonInfo {
			PlayerActions Action;

			float Left;
			float Top;
			float Width;
			float Height;

			GraphicResource* Graphics;
			int CurrentPointerId;
			Alignment Align;
		};

		struct Vertex
		{
			float X, Y;
			float U, V;

			Vertex()
				: X(0.0f), Y(0.0f), U(0.0f), V(0.0f) {}
			Vertex(float x, float y, float u, float v)
				: X(x), Y(y), U(u), V(v) {}
		};
		static constexpr unsigned int VertexBytes = sizeof(Vertex);
		static constexpr unsigned int VertexFloats = VertexBytes / sizeof(float);

		static constexpr Alignment Fixed = (Alignment)0x40;
		static constexpr Alignment AllowRollover = (Alignment)0x80;

		static constexpr uint16_t MainLayer = 100;
		static constexpr uint16_t ShadowLayer = 80;
		static constexpr uint16_t FontLayer = 200;
		static constexpr uint16_t FontShadowLayer = 120;
		static constexpr uint16_t TouchButtonsLayer = 400;
		static constexpr int32_t TouchButtonsCount = 10;

		static constexpr float WeaponWheelAnimDuration = 20.0f;
		static constexpr int WeaponWheelMaxVertices = 512;
		
		LevelHandler* _levelHandler;
		HashMap<String, GraphicResource>* _graphics;
		std::shared_ptr<Actors::Player> _attachedPlayer;
		Font* _smallFont;

		String _levelText;
		float _levelTextTime;
		int _coins;
		int _gems;
		float _coinsTime;
		float _gemsTime;
		float _activeBossTime;
		float _rgbAmbientLight;
		float _rgbHealthLast;

		int _weaponWheelCount;
		float _weaponWheelAnim;
		bool _weaponWheelShown;
		SmallVector<std::unique_ptr<RenderCommand>, 0> _weaponWheelRenderCommands;
		int  _weaponWheelRenderCommandsCount;
		std::unique_ptr<Vertex[]> _weaponWheelVertices;
		int _weaponWheelVerticesCount;
		int _lastWeaponWheelIndex;
		float _rgbLightsTime;
		TransitionState _transitionState;
		float _transitionTime;

		TouchButtonInfo _touchButtons[TouchButtonsCount];
		float _touchButtonsTimer;

		void DrawLevelText(int& charOffset);
		void DrawCoins(int& charOffset);
		void DrawGems(int& charOffset);

		void DrawElement(const StringView& name, int frame, float x, float y, uint16_t z, Alignment align, const Colorf& color, float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false, float angle = 0.0f);
		void DrawElementClipped(const StringView& name, int frame, float x, float y, uint16_t z, Alignment align, const Colorf& color, float clipX, float clipY);
		StringView GetCurrentWeapon(Actors::Player* player, WeaponType weapon, Vector2f& offset);

		void DrawWeaponWheel(Actors::Player* player);
		bool PrepareWeaponWheel(Actors::Player* player, int& weaponCount);
		static int GetWeaponCount(Actors::Player* player);
		void DrawWeaponWheelSegment(float x, float y, float width, float height, uint16_t z, float minAngle, float maxAngle, const Texture& texture, const Colorf& color);

		TouchButtonInfo CreateTouchButton(PlayerActions action, const StringView& identifier, Alignment align, float x, float y, float w, float h);
		bool IsOnButton(const TouchButtonInfo& button, float x, float y);

		void UpdateRgbLights(float timeMult, Actors::Player* player);
		static AuraLight KeyToAuraLight(KeySym key);
	};
}