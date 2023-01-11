#pragma once

#include <CommonBase.h>

#if defined(SHAREWARE_DEMO_ONLY) && defined(DEATH_TARGET_EMSCRIPTEN)

#include "MenuSection.h"

#include "../../../nCine/Base/HashMap.h"

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

		float _animation;
		State _state;
		int _fileCount;
		float _timeout;
		HashMap<String, bool> _foundLevels;

		static void FileDataCallback(void* context, std::unique_ptr<char[]> data, size_t length, const StringView& name);
		static void FileCountCallback(void* context, int fileCount);

		void CheckFoundLevels();
		bool HasAllLevels(const StringView* levelNames, int count);
	};
}

#endif