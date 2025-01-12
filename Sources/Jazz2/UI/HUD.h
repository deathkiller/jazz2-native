#pragma once

#include "Canvas.h"
#include "ControlScheme.h"
#include "Font.h"
#include "RgbLights.h"
#include "../ILevelHandler.h"
#include "../Rendering/PlayerViewport.h"
#include "../Actors/Player.h"

#include "../../nCine/Input/InputEvents.h"

#if defined(WITH_ANGELSCRIPT)
namespace Jazz2::Scripting
{
	class jjCANVAS;
}
#endif

namespace Jazz2::UI
{
	/** @brief Player HUD */
	class HUD : public Canvas
	{
#if defined(WITH_ANGELSCRIPT)
		friend class Scripting::jjCANVAS;
#endif

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

		/** @brief Shows a text notification */
		void ShowLevelText(StringView text);
		/** @brief Shows a notification about coins */
		void ShowCoins(std::int32_t count);
		/** @brief Shows a notification about gems */
		void ShowGems(std::uint8_t gemType, std::int32_t count);

		/** @brief Begins a fullscreen fade-in transition */
		void BeginFadeIn();
		/** @brief Begins a fullscreen face-out transition */
		void BeginFadeOut(float delay = 0.0f);

		/** @brief Returns `true` if weapon wheel is visible */
		bool IsWeaponWheelVisible(std::int32_t playerIndex) const;

	private:
		enum class TransitionState {
			None,
			FadeIn,
			FadeOut,
			WaitingForFadeOut
		};

#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct TouchButtonInfo {
			PlayerActions Action;

			float Left;
			float Top;
			float Width;
			float Height;

			GraphicResource* Graphics;
			std::int32_t CurrentPointerId;
			Alignment Align;
		};

		struct Vertex {
			float X, Y;
			float U, V;

			Vertex()
				: X(0.0f), Y(0.0f), U(0.0f), V(0.0f) {}
			Vertex(float x, float y, float u, float v)
				: X(x), Y(y), U(u), V(v) {}
		};

		struct WeaponWheelState {
			SmallVector<std::unique_ptr<RenderCommand>, 0> RenderCommands;
			std::unique_ptr<Vertex[]> Vertices;
			std::int32_t WeaponCount;
			std::int32_t  RenderCommandsCount;
			std::int32_t VerticesCount;
			std::int32_t LastIndex;
			float Anim;
			bool Shown;

			WeaponWheelState();
		};
#endif

		static constexpr std::uint32_t VertexBytes = sizeof(Vertex);
		static constexpr std::uint32_t VertexFloats = VertexBytes / sizeof(float);

		static constexpr Alignment Fixed = (Alignment)0x40;
		static constexpr Alignment AllowRollover = (Alignment)0x80;

		static constexpr std::uint16_t MainLayer = 100;
		static constexpr std::uint16_t ShadowLayer = 80;
		static constexpr std::uint16_t FontLayer = 200;
		static constexpr std::uint16_t FontShadowLayer = 120;
		static constexpr std::uint16_t TouchButtonsLayer = 400;
		static constexpr std::int32_t TouchButtonsCount = 10;

		static constexpr float WeaponWheelAnimDuration = 20.0f;
		static constexpr std::int32_t WeaponWheelMaxVertices = 512;
		
		LevelHandler* _levelHandler;
		Metadata* _metadata;
		Font* _smallFont;

		String _levelText;
		float _levelTextTime;
		std::int32_t _coins;
		std::int32_t _gems;
		float _coinsTime;
		float _gemsTime;
		std::uint8_t _gemsLastType;
		float _activeBossTime;
		float _rgbAmbientLight;
		float _rgbHealthLast;

		WeaponWheelState _weaponWheel[ControlScheme::MaxSupportedPlayers];
		float _rgbLightsAnim;
		float _rgbLightsTime;
		TransitionState _transitionState;
		float _transitionTime;

		TouchButtonInfo _touchButtons[TouchButtonsCount];
		float _touchButtonsTimer;

		void DrawHealth(const Rectf& view, const Rectf& adjustedView, Actors::Player* player);
		void DrawHealthCarrots(float x, float y, std::int32_t health);
		void DrawScore(const Rectf& view, Actors::Player* player);
		void DrawWeaponAmmo(const Rectf& adjustedView, Actors::Player* player);
		void DrawActiveBoss(const Rectf& adjustedView);
		void DrawLevelText(std::int32_t& charOffset);
		void DrawViewportSeparators();
		void DrawCoins(const Rectf& view, std::int32_t& charOffset);
		void DrawGems(const Rectf& view, std::int32_t& charOffset);

		void DrawElement(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false, float angle = 0.0f);
		void DrawElementClipped(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color, float clipX, float clipY);
		AnimState GetCurrentWeapon(Actors::Player* player, WeaponType weapon, Vector2f& offset);

		void DrawWeaponWheel(const Rectf& view, Actors::Player* player);
		void UpdateWeaponWheel(float timeMult);
		bool PrepareWeaponWheel(Actors::Player* player, std::int32_t& weaponCount);
		static std::int32_t GetWeaponCount(Actors::Player* player);
		void DrawWeaponWheelSegment(WeaponWheelState& state, float x, float y, float width, float height, std::uint16_t z, float minAngle, float maxAngle, const Texture& texture, const Colorf& color);

		TouchButtonInfo CreateTouchButton(PlayerActions action, AnimState state, Alignment align, float x, float y, float w, float h);
		bool IsOnButton(const TouchButtonInfo& button, float x, float y);

		void UpdateRgbLights(float timeMult, Rendering::PlayerViewport* viewport);
		static Color ApplyRgbGradientAlpha(Color color, std::int32_t x, std::int32_t y, float animProgress, float ambientLight);
		static AuraLight KeyToAuraLight(Keys key);
	};
}