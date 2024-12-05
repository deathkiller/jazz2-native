#pragma once

#include <CommonBase.h>

#if defined(SHAREWARE_DEMO_ONLY) && defined(DEATH_TARGET_EMSCRIPTEN)

#include "MenuSection.h"
#include "../../../nCine/Base/HashMap.h"

#include <IO/EmscriptenFileStream.h>

using namespace Death::IO;

namespace Jazz2::UI::Menu
{
	class ImportSection : public MenuSection
	{
	public:
		ImportSection();

		void OnShow(IMenuContainer* root) override;
		void OnUpdate(float timeMult) override;
		void OnDraw(Canvas* canvas) override;
		void OnTouchEvent(const nCine::TouchEvent& event, const Vector2i& viewSize) override;

	private:
		enum class State {
			Waiting,
			Loading,
			NothingSelected,
			NothingImported,
			Success
		};

		static constexpr std::int32_t TopLine = 31;
		static constexpr std::int32_t BottomLine = 42;

		float _animation;
		State _state;
		std::int32_t _fileCount;
		float _timeout;
		HashMap<String, bool> _foundLevels;
		EmscriptenFilePicker _picker;

		void OnFilesReceived(ArrayView<EmscriptenFileStream> files);

		void ShowPicker();
		void CheckFoundLevels();
		bool HasAllLevels(ArrayView<const StringView> levelNames);
	};
}

#endif