#pragma once

#if defined(WITH_EDITOR) || defined(DOXYGEN_GENERATING_OUTPUT)

#include "../UI/UiElement.h"

namespace Jazz2::Editor
{
	class IEditorContext;

	/**
		@brief Shows which of the 47 ways the cells around can be solid have tiles to choose from

		Each pattern is drawn as its 3x3 cells, green with a rule of the current terrain, gray with tiles learned from
		the levels only, and red with neither, where the smart tools have to make do with something else. A pattern of
		the layer being edited, the sprite layer or the others, as the tools choose by it.
	*/
	class TerrainCoverageView : public UiElement
	{
	public:
		explicit TerrainCoverageView(IEditorContext& session);

		Vector2f Measure(const UiContext& context, Vector2f available) override;
		void Update(float timeMult) override;
		void Draw(UiPainter& painter) override;

		/** @brief Returns the number of patterns without any tile to choose */
		std::int32_t GetMissingCount() const {
			return _missing;
		}

	private:
		IEditorContext& _session;
		std::uint8_t _coverage[256];
		std::uint8_t _patterns[47];
		std::int32_t _patternCount;
		std::int32_t _missing;
		// The coverage is counted again at most this often, it goes through everything learned
		float _refresh;
	};
}

#endif
