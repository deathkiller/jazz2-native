#pragma once

#include "Canvas.h"
#include "Font.h"
#include "../LevelHandler.h"
#include "../Actors/Player.h"
#include "../Input/ControlScheme.h"
#include "../Input/RgbLights.h"
#include "../Rendering/PlayerViewport.h"

#include "../../nCine/Input/InputEvents.h"

#if defined(WITH_ANGELSCRIPT)
namespace Jazz2::Scripting::Legacy
{
	struct jjCANVAS;
}
#endif

namespace Jazz2::UI
{
	/** @brief Player HUD */
	class HUD : public Canvas
	{
#if defined(WITH_ANGELSCRIPT)
		friend struct Scripting::Legacy::jjCANVAS;
#endif

	public:
#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr float DpadLeft = 0.02f;
		static constexpr float DpadBottom = 0.1f;
		static constexpr float DpadThreshold = 0.09f;
		static constexpr float DpadSize = 0.37f;
		static constexpr float ButtonSize = 0.172f;
		static constexpr float SmallButtonSize = 0.098f;
#endif

		HUD(LevelHandler* levelHandler);
		~HUD();

		void OnUpdate(float timeMult) override;
		bool OnDraw(RenderQueue& renderQueue) override;
		void OnTouchEvent(const TouchEvent& event, std::uint32_t& overrideActions);

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

	protected:
#ifndef DOXYGEN_GENERATING_OUTPUT
		// Doxygen 1.12.0 outputs also private structs/unions even if it shouldn't
		struct TouchButtonInfo {
			PlayerAction Action;

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

		/** @{ @name Constants */

		static constexpr std::uint16_t MainLayer = 100;
		static constexpr std::uint16_t ShadowLayer = 80;
		static constexpr std::uint16_t FontLayer = 200;
		static constexpr std::uint16_t FontShadowLayer = 120;
		static constexpr std::uint16_t TouchButtonsLayer = 400;

		/** @} */
		
#ifndef DOXYGEN_GENERATING_OUTPUT
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
#endif

		/** @brief Called when health of the player needs to be drawn */
		virtual void OnDrawHealth(const Rectf& view, const Rectf& adjustedView, Actors::Player* player);
		/** @brief Called when score of the player needs to be drawn */
		virtual void OnDrawScore(const Rectf& view, Actors::Player* player);
		/** @brief Called when weapon ammo of the player needs to be drawn */
		virtual void OnDrawWeaponAmmo(const Rectf& adjustedView, Actors::Player* player);
		/** @brief Called when health of the active boss needs to be drawn */
		virtual void OnDrawActiveBoss(const Rectf& adjustedView);
		/** @brief Called when a text notification needs to be drawn */
		virtual void OnDrawLevelText(std::int32_t& charOffset);
		/** @brief Called when a notification about coins of the player needs to be drawn */
		virtual void OnDrawCoins(const Rectf& view, std::int32_t& charOffset);
		/** @brief Called when a notification about gems of the player needs to be drawn */
		virtual void OnDrawGems(const Rectf& view, std::int32_t& charOffset);

		/** @brief Draws carrotized health bar (Reforged) */
		void DrawHealthCarrots(float x, float y, std::int32_t health);
		/** @brief Draws separators of split-screen viewports */
		void DrawViewportSeparators();
		/** @brief Draws a textured element */
		void DrawElement(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align, const Colorf& color,
			float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false, float angle = 0.0f);
		/** @brief Draws a textured element with clipping */
		void DrawElementClipped(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align,
			const Colorf& color, float clipX, float clipY);
	
	private:
		enum class TransitionState {
			None,
			FadeIn,
			FadeOut,
			WaitingForFadeOut
		};

		static constexpr std::uint32_t VertexBytes = sizeof(Vertex);
		static constexpr std::uint32_t VertexFloats = VertexBytes / sizeof(float);

		static constexpr Alignment Fixed = (Alignment)0x40;
		static constexpr Alignment AllowRollover = (Alignment)0x80;

		static constexpr std::int32_t TouchButtonsCount = 11;
		static constexpr float WeaponWheelAnimDuration = 20.0f;
		static constexpr std::int32_t WeaponWheelMaxVertices = 512;

		float _rgbAmbientLight;
		float _rgbHealthLast;

		WeaponWheelState _weaponWheel[ControlScheme::MaxSupportedPlayers];
		float _rgbLightsAnim;
		float _rgbLightsTime;
		TransitionState _transitionState;
		float _transitionTime;

		TouchButtonInfo _touchButtons[TouchButtonsCount];
		float _touchButtonsTimer;

		AnimState GetCurrentWeapon(Actors::Player* player, WeaponType weapon, Vector2f& offset);
		void DrawWeaponWheel(const Rectf& view, Actors::Player* player);
		void UpdateWeaponWheel(float timeMult);
		bool PrepareWeaponWheel(Actors::Player* player, std::int32_t& weaponCount);
		static std::int32_t GetWeaponCount(Actors::Player* player);
		void DrawWeaponWheelSegment(WeaponWheelState& state, float x, float y, float width, float height, std::uint16_t z, float minAngle, float maxAngle, const Texture& texture, const Colorf& color);

		TouchButtonInfo CreateTouchButton(PlayerAction action, AnimState state, Alignment align, float x, float y, float w, float h);
		bool IsOnButton(const TouchButtonInfo& button, float x, float y);

		void UpdateRgbLights(float timeMult, Rendering::PlayerViewport* viewport);
		static Color ApplyRgbGradientAlpha(Color color, std::int32_t x, std::int32_t y, float animProgress, float ambientLight);
		static AuraLight KeyToAuraLight(Keys key);
	};
}