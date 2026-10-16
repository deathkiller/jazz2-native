#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../UI/UiPainter.h"
#include "../../../nCine/Graphics/RenderCommand.h"
#include "../../../nCine/Graphics/Texture.h"

#include <Containers/SmallVector.h>

#include <memory>

namespace Jazz2::Rendering
{
	class PolylineRenderer;
}

namespace Jazz2::Editor
{
#ifndef DOXYGEN_GENERATING_OUTPUT
	using Death::Containers::SmallVector;
	using nCine::RenderCommand;
	using nCine::Texture;
#endif

	/**
		@brief Reveals the level being played from the editor in a growing ring

		A thick ring appears in the middle of the screen and grows until it's larger than the screen. Outside of it the
		last image of what was shown is still there, inside of it what is shown now is seen already. The ring has the
		color of the level, see @ref FindAccentColor(). It's drawn above the slot of the painter, so whatever the editor
		still shows outside of it in that slot is covered by it.

		With an image of the inside, the transition is drawn in two parts: the outside under the interface, and the
		inside with the ring over it, so the interface is still seen outside of the ring.
	*/
	class PlayTransition
	{
	public:
		/** @brief Length of the transition in frames at 60 a second */
		static constexpr float Duration = 26.0f;

		PlayTransition();
		~PlayTransition();

		PlayTransition(const PlayTransition&) = delete;
		PlayTransition& operator=(const PlayTransition&) = delete;

		/**
		 * @brief Starts the transition
		 *
		 * @p outside is shown outside of the ring, it has to stay as it is until the transition ends. @p inside is shown
		 * inside of it, or `nullptr` if what is under the transition is seen there. The ring is drawn in @p color.
		 */
		void Start(Texture* outside, Texture* inside, const Colorf& color);
		/** @brief Ends the transition at once */
		void Stop();
		/** @brief Returns `true` while the transition is shown */
		bool IsActive() const {
			return (_outside != nullptr);
		}
		/** @brief Returns `true` if the transition has an image of the inside, see @ref DrawRing() */
		bool HasInside() const {
			return (_inside != nullptr);
		}

		/** @brief Shows the transition at a point between 0 and 1, @p hold stops it there, for testing */
		void SetProgress(float progress, bool hold);
		/** @brief Advances the transition */
		void Update(float timeMult);
		/** @brief Draws the transition over a screen of a size, both parts at once */
		void Draw(UiPainter& painter, Vector2f screenSize);
		/** @brief Draws only the image of the outside, under what is drawn after it */
		void DrawOutside(UiPainter& painter, Vector2f screenSize);
		/** @brief Draws only the image of the inside, if there's one, and the ring */
		void DrawRing(UiPainter& painter, Vector2f screenSize);
		/**
		 * @brief Returns the circle in which the game is seen, on a screen of a size at a scale of the interface
		 *
		 * The ring goes around it, @p outer is where the ring ends and the image of the editor starts.
		 */
		void GetHole(Vector2f screenSize, float uiScale, Vector2f& center, float& radius, float& outer) const;

	private:
		// A band between two circles: the images of the outside and the inside, the ring and its edges, the dark line inside
		struct Band
		{
			std::unique_ptr<RenderCommand> Command;
			SmallVector<float, 0> Vertices;
		};

		static constexpr std::int32_t BandCount = 7;

		Texture* _outside;
		Texture* _inside;
		Colorf _color;
		float _time;
		bool _hold;
		Band _bands[BandCount];
		// Only lends its feather texture, which gives the ring its soft edges
		std::unique_ptr<Rendering::PolylineRenderer> _ring;

		RenderCommand* DrawBand(std::int32_t index, Vector2f center, float innerRadius, float outerRadius, Texture& texture,
			Vector2f screenSize, float innerU, float outerU, std::uint16_t depth, const Colorf& color);
	};
}

#endif
