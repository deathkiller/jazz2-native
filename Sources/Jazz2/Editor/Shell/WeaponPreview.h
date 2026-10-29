#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../UI/UiElement.h"
#include "../../WeaponDefinition.h"

#include "../../../nCine/Primitives/Rect.h"
#include "../../../nCine/Primitives/Vector2.h"

#include <Containers/Function.h>
#include <Containers/SmallVector.h>

namespace Jazz2::Resources
{
	struct GraphicResource;
}

namespace Jazz2::Editor
{
	class IEditorContext;
	class UiPainter;

#ifndef DOXYGEN_GENERATING_OUTPUT
	using Resources::GraphicResource;
#endif

	/** @brief How a shot of the firing range ended, see @ref SimulateWeapon() */
	enum class SimulatedShotEnd : std::uint8_t
	{
		Lifetime,			/**< Its time ran out */
		Wall,				/**< It hit a wall, the floor or the ceiling */
		Target,				/**< It hit the target */
		Triggered			/**< It went off by its trigger */
	};

	/** @brief A shot of the firing range, frame by frame */
	struct SimulatedShot
	{
		/** @brief Position in each frame, a beam has only where it starts and where it ends */
		SmallVector<Vector2f, 0> Points;
		/** @brief Angle it flies at in each frame */
		SmallVector<float, 0> Angles;
		/** @brief Frames it lasts */
		std::int32_t Frames = 0;
		/** @brief Frame it's fired at, a weapon held fires again and again */
		std::int32_t StartFrame = 0;
		/** @brief Whether it has the second sprite, see @ref WeaponShotOptions::AltSprite */
		bool AltSprite = false;
		/** @brief Whether its sprite is flipped horizontally, beyond turning to where it flies */
		bool FlippedX = false;
		/** @brief Whether its sprite is flipped vertically */
		bool FlippedY = false;
		/** @brief How it ended */
		SimulatedShotEnd End = SimulatedShotEnd::Lifetime;
		/** @brief How many times it hit the target */
		std::int32_t TargetHits = 0;
	};

	/**
		@brief The firing range the preview of a weapon shows

		The player fires from the muzzle towards the target, the floor is below, the ceiling above and a wall on the
		right. All in pixels of the level.
	*/
	struct FiringRange
	{
		/** @brief Height of the floor */
		static constexpr float FloorY = 20.0f;
		/** @brief Height of the ceiling */
		static constexpr float CeilingY = -76.0f;
		/** @brief Distance of the wall */
		static constexpr float WallX = 300.0f;
		/** @brief Most frames of a shot that are simulated */
		static constexpr std::int32_t MaxFrames = 600;

		/** @brief Muzzle of the gun of the player */
		Vector2f Muzzle = Vector2f(0.0f, 0.0f);
		/** @brief Where the target stands, the middle of its bottom */
		Vector2f Target = Vector2f(182.0f, FloorY);
		/** @brief Whether the player faces to the left */
		bool FacingLeft = false;

		/** @brief Returns the target */
		Rectf GetTarget() const {
			return Rectf(Target.X - 12.0f, Target.Y - 32.0f, 24.0f, 32.0f);
		}

		bool operator==(const FiringRange& other) const {
			return (Muzzle == other.Muzzle && Target == other.Target && FacingLeft == other.FacingLeft);
		}
	};

	/**
	 * @brief Simulates the shots one press of fire makes of a custom weapon in the firing range
	 *
	 * Deterministic, a random spread is spread evenly instead, @p volley takes the next samples of it for a weapon
	 * that fires again. Fills one shot for each fired at once.
	 */
	void SimulateWeapon(const WeaponDefinition& weapon, bool powered, SmallVectorImpl<SimulatedShot>& shots, const FiringRange& range = FiringRange(),
		std::int32_t volley = 0);

	/**
	 * @brief Simulates a weapon held for a while, it fires again whenever it can, only once if it fires once per press
	 *
	 * The shots of each press start at its frame, see @ref SimulatedShot::StartFrame.
	 */
	void SimulateHeldWeapon(const WeaponDefinition& weapon, bool powered, SmallVectorImpl<SimulatedShot>& shots, const FiringRange& range = FiringRange());

	/** @brief Returns an animation of the game or of an image of the level, the first one of the metadata if it has no such state, `nullptr` for no metadata */
	const GraphicResource* FindWeaponAnimation(StringView metadataPath, std::uint32_t animState);

	/**
	 * @brief Draws a frame of an animation fitted in a rectangle with its hotspot in the middle
	 *
	 * All frames of the animation are at the same scale and place, at most a pixel a unit of the interface, a whole
	 * scale if it's larger. With @p additive, it's added to what's behind, as shots that glow are.
	 */
	void DrawFittedFrame(UiPainter& painter, const GraphicResource& resource, std::int32_t frame, const Rectf& rect, bool additive = false);

	/**
		@brief Preview of a custom weapon in a firing range

		Shows the paths of the shots and the shots themselves flying along them, with their sprites, again and again.
		The player and the target are moved by dragging them.
	*/
	class WeaponPreview : public UiElement
	{
	public:
		/** @brief Creates a new instance, the functions return the weapons of the level, the weapon and whether it's powered up */
		WeaponPreview(IEditorContext& session, Function<const WeaponsDescription*()>&& getWeapons, Function<WeaponType()>&& getSlot,
			Function<bool()>&& isPowered);

		/** @brief Returns what one press of fire does, in numbers, empty if there's nothing shown */
		String GetSummary();

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Update(float timeMult) override;
		void Draw(UiPainter& painter) override;
		bool AcceptsPointer() const override;
		bool OnPointerDown(const UiPointerEvent& event) override;
		void OnPointerMove(const UiPointerEvent& event) override;
		void OnPointerUp(const UiPointerEvent& event, bool inside) override;
		void OnPointerCancel() override;
		StringView GetTooltip(Vector2f pos) const override;

	private:
		enum class DragTarget : std::uint8_t { None, Player, Target };

		IEditorContext& _session;
		Function<const WeaponsDescription*()> _getWeapons;
		Function<WeaponType()> _getSlot;
		Function<bool()> _isPowered;
		FiringRange _range;
		// The hotspot of the player, the muzzle is where the gun of the sprite is from it
		Vector2f _playerPos = Vector2f(0.0f, FiringRange::FloorY - 20.0f);
		Vector2f _gunOffset = Vector2f(14.0f, -2.0f);
		WeaponDefinition _simulated;
		FiringRange _simulatedRange;
		bool _simulatedPowered = false;
		bool _hasSimulation = false;
		SmallVector<SimulatedShot, 4> _shots;
		DragTarget _dragging = DragTarget::None;
		DragTarget _pointedAt = DragTarget::None;
		Vector2f _dragOffset;

		const WeaponDefinition* GetWeapon();
		void UpdateRange();
		void Simulate(const WeaponDefinition& weapon, bool powered);
		float GetScale() const;
		Vector2f GetOrigin() const;
		Vector2f ToRange(Vector2f screen) const;
		Rectf GetPlayerRect() const;
		DragTarget HitTestDraggable(Vector2f screen) const;
	};

	/**
		@brief An animation of a weapon in a small box, as it plays in the game

		Nothing is shown while the function returns `nullptr`.
	*/
	class WeaponSpriteView : public UiElement
	{
	public:
		/** @brief Creates a new instance, the function returns the animation and whether it glows */
		WeaponSpriteView(IEditorContext& session, Function<const GraphicResource*(bool&)>&& getResource);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Draw(UiPainter& painter) override;

	private:
		IEditorContext& _session;
		Function<const GraphicResource*(bool&)> _getResource;
	};
}

#endif
