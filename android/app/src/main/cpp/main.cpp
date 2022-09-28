#include <android_native_app_glue.h>
#include <nCine/Android/AndroidApplication.h>

#include <memory>

namespace nCine
{
	class IAppEventHandler;
}

std::unique_ptr<nCine::IAppEventHandler> createAppEventHandler();

void android_main(struct android_app *state)
{
	nCine::AndroidApplication::start(state, createAppEventHandler);
}
