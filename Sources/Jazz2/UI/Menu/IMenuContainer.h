#pragma once

#include "../../AnimState.h"
#include "../../ContentResolver.h"
#include "../../LevelInitialization.h"
#include "../../PlayerAction.h"
#include "../Alignment.h"

#if defined(WITH_MULTIPLAYER)
#	include "../../Multiplayer/ServerInitialization.h"
#endif

#include "../../../nCine/I18n.h"

namespace Jazz2::UI::Menu
{
	class MenuSection;

	/** @brief Changed preferences, supports a bitwise combination of its member values */
	enum class ChangedPreferencesType {
		None = 0x00,					/**< None */
		Audio = 0x01,					/**< Audio */
		Graphics = 0x02,				/**< Graphics */
		Gameplay = 0x04,				/**< Gameplay */
		Language = 0x08,				/**< Language */
		ControlScheme = 0x10,			/**< Control Scheme */
		MainMenu = 0x20					/**< Main Menu */
	};

	DEATH_ENUM_FLAGS(ChangedPreferencesType);

	/** @brief Base interface of a menu container */
	class IMenuContainer
	{
		DEATH_RUNTIME_OBJECT();

	public:
#ifndef DOXYGEN_GENERATING_OUTPUT
		static constexpr std::uint16_t MainLayer = 600;
		static constexpr std::uint16_t ShadowLayer = 580;
		static constexpr std::uint16_t BackgroundLayer = 550;
		static constexpr std::uint16_t FontLayer = 700;
		static constexpr std::uint16_t FontShadowLayer = 620;
#endif

		virtual ~IMenuContainer() { }

		/** @brief Creates a section by type and switches to it */
		template<typename T, typename... Params>
		T* SwitchToSection(Params&&... args)
		{
			return static_cast<T*>(SwitchToSectionDirect(std::make_unique<T>(std::forward<Params>(args)...)));
		}

		/** @brief Switches to already created section */
		virtual MenuSection* SwitchToSectionDirect(std::unique_ptr<MenuSection> section) = 0;
		/** @brief Leaves current section */
		virtual void LeaveSection() = 0;
		/** @brief Returns the current section */
		virtual MenuSection* GetCurrentSection() const = 0;
		/** @brief Returns the next section after the top one */
		virtual MenuSection* GetUnderlyingSection() const = 0;
		/** @brief Changes current level */
		virtual void ChangeLevel(LevelInitialization&& levelInit) = 0;
		/** @brief Returns `true` if a saved state can be resumed */
		virtual bool HasResumableState() const = 0;
		/** @brief Resumes a saved state */
		virtual void ResumeSavedState() = 0;
#if defined(WITH_MULTIPLAYER) || defined(DOXYGEN_GENERATING_OUTPUT)
		/** @brief Connects to a multiplayer server asynchronously */
		virtual void ConnectToServer(StringView endpoint, std::uint16_t defaultPort) = 0;
		/** @brief Creates a multiplayer server */
		virtual bool CreateServer(Jazz2::Multiplayer::ServerInitialization&& serverInit) = 0;
#endif
		/** @brief Applies changes to preferences */
		virtual void ApplyPreferencesChanges(ChangedPreferencesType type) = 0;
		/** @brief Returns `true` if specified action is pressed */
		virtual bool ActionPressed(PlayerAction action) = 0;
		/** @brief Returns `true` if specified action is hit (newly pressed) */
		virtual bool ActionHit(PlayerAction action) = 0;

		/** @brief Returns viewport size of the menu */
		virtual Vector2i GetViewSize() const = 0;
		/** @brief Returns content bounds */
		virtual Recti GetContentBounds() const = 0;

		/** @brief Draws a textured element */
		virtual void DrawElement(AnimState state, std::int32_t frame, float x, float y, std::uint16_t z, Alignment align,
			const Colorf& color, float scaleX = 1.0f, float scaleY = 1.0f, bool additiveBlending = false, bool unaligned = false) = 0;
		/** @overload */
		virtual void DrawElement(AnimState state, float x, float y, std::uint16_t z, Alignment align,
			const Colorf& color, Vector2f size, const Vector4f& texCoords, bool unaligned = false) = 0;
		/** @brief Draws a solid rectangle */
		virtual void DrawSolid(float x, float y, std::uint16_t z, Alignment align, Vector2f size, const Colorf& color, bool additiveBlending = false) = 0;
		/** @brief Draws a textured rectangle */
		virtual void DrawTexture(const Texture& texture, float x, float y, std::uint16_t z, Alignment align, Vector2f size, const Colorf& color, bool unaligned = false) = 0;
		/** @brief Measures a string */
		virtual Vector2f MeasureString(StringView text, float scale = 1.0f, float charSpacing = 1.0f, float lineSpacing = 1.0f) = 0;
		/** @brief Draws a string with shadow */
		virtual void DrawStringShadow(StringView text, std::int32_t& charOffset, float x, float y, std::uint16_t z, Alignment align,
			const Colorf& color, float scale = 1.0f, float angleOffset = 0.0f, float varianceX = 4.0f, float varianceY = 4.0f,
			float speed = 0.4f, float charSpacing = 1.0f, float lineSpacing = 1.0f) = 0;
		/** @brief Plays a common sound effect */
		virtual void PlaySfx(StringView identifier, float gain = 1.0f) = 0;

		static float EaseOutElastic(float t)
		{
			constexpr float p = 0.3f;
			return powf(2.0f, -10.0f * t) * sinf((t - p / 4.0f) * (2.0f * fPi) / p) + 1.0f;
		}

		static float EaseOutCubic(float t)
		{
			float x = 1.0f - t;
			return 1.0f - x * x * x;
		}
	};
}