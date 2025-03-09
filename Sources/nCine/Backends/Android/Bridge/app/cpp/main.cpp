#include <android_native_app_glue.h>
#include <nCine/Backends/Android/AndroidApplication.h>

#include <memory>

namespace nCine
{
	class IAppEventHandler;
}

std::unique_ptr<nCine::IAppEventHandler> CreateAppEventHandler();

void android_main(struct android_app* state)
{
	nCine::AndroidApplication::Run(state, CreateAppEventHandler);
}
