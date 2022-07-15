#if defined(_WIN32)
#   if defined(_M_X64)
#       pragma comment(lib, "../Libs/Deflate/x64/libdeflate.lib")
#   elif defined(_M_IX86)
#       pragma comment(lib, "../Libs/Deflate/x86/libdeflate.lib")
#   else
#       error Unsupported architecture
#   endif
#endif

#include "Common.h"

#include "nCine/PCApplication.h"
#include "nCine/IAppEventHandler.h"
#include "nCine/Input/IInputEventHandler.h"
#include "nCine/IO/FileSystem.h"

#include "Jazz2/ContentResolver.h"
#include "Jazz2/LevelHandler.h"

#if defined(_WIN32) && !defined(WITH_QT5)
#	include <cstdlib> // for `__argc` and `__argv`
extern int __argc;
extern char** __argv;
#endif

using namespace nCine;

class GameEventHandler : public IAppEventHandler, public IInputEventHandler
{
public:
	static constexpr int DefaultWidth = 720;
	static constexpr int DefaultHeight = 405;

	void onPreInit(AppConfiguration& config) override;
	void onInit() override;
	void onFrameStart() override;
	void onShutdown() override;
	void onRootViewportResized(int width, int height) override;

#if defined(__ANDROID__)
	void onTouchDown(const TouchEvent& event) override;
	void onTouchUp(const TouchEvent& event) override;
#endif
	void onKeyPressed(const KeyboardEvent& event) override;
	void onKeyReleased(const KeyboardEvent& event) override;
	void onMouseButtonPressed(const MouseEvent& event) override;
	void onMouseButtonReleased(const MouseEvent& event) override;
	void onJoyMappedButtonPressed(const JoyMappedButtonEvent& event) override;
	void onJoyMappedButtonReleased(const JoyMappedButtonEvent& event) override;

private:
	std::unique_ptr<Jazz2::ILevelHandler> _currentHandler;
};

void GameEventHandler::onPreInit(AppConfiguration& config)
{
#if defined(__ANDROID__)
	config.dataPath() = "asset::";
#elif defined(__EMSCRIPTEN__)
	config.dataPath() = "/";
#elif defined(NCPROJECT_DEFAULT_DATA_DIR)
	config.dataPath() = NCPROJECT_DEFAULT_DATA_DIR;
#else
	config.dataPath() = "Content/";
#endif

	config.windowTitle = "Jazz² Resurrection";
	config.resolution.Set(Jazz2::LevelHandler::DefaultWidth, Jazz2::LevelHandler::DefaultHeight);
}

void GameEventHandler::onInit()
{
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
	theApplication().setAutoSuspension(false);
	theApplication().inputManager().setCursor(IInputManager::Cursor::Hidden);
#endif

	// TODO
	Jazz2::PlayerType players[] = { Jazz2::PlayerType::Jazz };
	Jazz2::LevelInitialization data("unknown", "unknown", Jazz2::GameDifficulty::Normal, false, false, players, _countof(players));


	_currentHandler = std::make_unique<Jazz2::LevelHandler>(data);

}

void GameEventHandler::onFrameStart()
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnFrameStart();
	}
}

void GameEventHandler::onShutdown()
{
	_currentHandler = nullptr;

	Jazz2::ContentResolver::Current().Release();
}

void GameEventHandler::onRootViewportResized(int width, int height)
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnRootViewportResized(width, height);
	}
}

#if defined(__ANDROID__)
void GameEventHandler::onTouchDown(const TouchEvent& event)
{
}

void GameEventHandler::onTouchUp(const TouchEvent& event)
{
}
#endif

void GameEventHandler::onKeyPressed(const KeyboardEvent& event)
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnKeyPressed(event);
	}
}

void GameEventHandler::onKeyReleased(const KeyboardEvent& event)
{
	if (_currentHandler != nullptr) {
		_currentHandler->OnKeyReleased(event);
	}
}

void GameEventHandler::onMouseButtonPressed(const MouseEvent& event)
{
}

void GameEventHandler::onMouseButtonReleased(const MouseEvent& event)
{
}

void GameEventHandler::onJoyMappedButtonPressed(const JoyMappedButtonEvent& event)
{
}

void GameEventHandler::onJoyMappedButtonReleased(const JoyMappedButtonEvent& event)
{
}

#if defined(_WIN32) && !defined(WITH_QT5)
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
#else
int main(int argc, char** argv)
#endif
{
	return PCApplication::start([]() -> std::unique_ptr<IAppEventHandler> {
		return std::make_unique<GameEventHandler>();
	}, __argc, __argv);
}
