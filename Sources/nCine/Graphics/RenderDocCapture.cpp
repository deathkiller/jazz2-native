//#if !defined(_WIN32) && !defined(__APPLE__)
//	#include <dlfcn.h>
//	#include "renderdoc.h"
//#else
//	#include "common_windefines.h"
//	#include <libloaderapi.h>
//	#include "renderdoc_app.h"
//#endif
//
//#include "RenderDocCapture.h"
//
//namespace nCine {
//
/////////////////////////////////////////////////////////////
//// STATIC DEFINITIONS
/////////////////////////////////////////////////////////////
//
//namespace {
//	RENDERDOC_API_1_1_2 *rdocApi = nullptr;
//}
//
/////////////////////////////////////////////////////////////
//// PUBLIC FUNCTIONS
/////////////////////////////////////////////////////////////
//
//bool RenderDocCapture::IsAvailable()
//{
//	return (rdocApi != nullptr);
//}
//
//bool RenderDocCapture::IsTargetControlConnected()
//{
//	if (rdocApi)
//		return rdocApi->IsTargetControlConnected();
//	return false;
//}
//
//bool RenderDocCapture::IsFrameCapturing()
//{
//	if (rdocApi)
//		return rdocApi->IsFrameCapturing();
//	return false;
//}
//
//void RenderDocCapture::GetApiVersion(int *major, int *minor, int *patch)
//{
//	if (rdocApi)
//		rdocApi->GetAPIVersion(major, minor, patch);
//}
//
//bool RenderDocCapture::IsOverlayEnabled()
//{
//	unsigned int overlayBits = 0;
//	if (rdocApi)
//		overlayBits = rdocApi->GetOverlayBits();
//
//	return overlayBits & (1 << RENDERDOC_OverlayBits::eRENDERDOC_Overlay_Enabled);
//}
//
//void RenderDocCapture::EnableOverlay(bool enabled)
//{
//	if (rdocApi)
//	{
//		if (enabled)
//		{
//			rdocApi->MaskOverlayBits(RENDERDOC_OverlayBits::eRENDERDOC_Overlay_All,
//			                         RENDERDOC_OverlayBits::eRENDERDOC_Overlay_Enabled);
//		}
//		else
//		{
//			rdocApi->MaskOverlayBits(RENDERDOC_OverlayBits::eRENDERDOC_Overlay_All &
//			                             ~RENDERDOC_OverlayBits::eRENDERDOC_Overlay_Enabled,
//			                         RENDERDOC_OverlayBits::eRENDERDOC_Overlay_None);
//		}
//	}
//}
//
//void RenderDocCapture::TriggerCapture()
//{
//	if (rdocApi)
//		rdocApi->TriggerCapture();
//}
//
//void RenderDocCapture::TriggerMultiFrameCapture(uint32_t numFrames)
//{
//	if (rdocApi)
//		rdocApi->TriggerMultiFrameCapture(numFrames);
//}
//
//bool RenderDocCapture::EndFrameCapture()
//{
//	if (rdocApi)
//		return rdocApi->EndFrameCapture(nullptr, nullptr);
//	return false;
//}
//
//bool RenderDocCapture::DiscardFrameCapture()
//{
//	if (rdocApi)
//		return rdocApi->DiscardFrameCapture(nullptr, nullptr);
//	return false;
//}
//
//uint32_t RenderDocCapture::GetCaptureCount()
//{
//	if (rdocApi)
//		return rdocApi->GetNumCaptures();
//	return 0;
//}
//
//uint32_t RenderDocCapture::GetCaptureInfo(uint32_t idx, char *filename, uint32_t *pathlength, uint64_t *timestamp)
//{
//	if (rdocApi)
//		return rdocApi->GetCapture(idx, filename, pathlength, timestamp);
//	return 0;
//}
//
//const char *RenderDocCapture::GetCaptureFilePathTemplate()
//{
//	if (rdocApi)
//		return rdocApi->GetCaptureFilePathTemplate();
//	return nullptr;
//}
//
//void RenderDocCapture::SetCaptureFilePathTemplate(const char *pathTemplate)
//{
//	if (rdocApi)
//		rdocApi->SetCaptureFilePathTemplate(pathTemplate);
//}
//
//void RenderDocCapture::SetCaptureFileComments(const char *filePath, const char *comments)
//{
//	if (rdocApi)
//		rdocApi->SetCaptureFileComments(filePath, comments);
//}
//
//uint32_t RenderDocCapture::LaunchReplayUI(uint32_t connectTargetControl, const char *cmdLine)
//{
//	if (rdocApi)
//		return rdocApi->LaunchReplayUI(connectTargetControl, cmdLine);
//	return 0;
//}
//
//void RenderDocCapture::UnloadCrashHandler()
//{
//	if (rdocApi)
//		rdocApi->UnloadCrashHandler();
//}
//
/////////////////////////////////////////////////////////////
//// PRIVATE FUNCTIONS
/////////////////////////////////////////////////////////////
//
//void RenderDocCapture::Init()
//{
//#if defined(_WIN32)
//	if (HMODULE mod = GetModuleHandleA("renderdoc.dll"))
//	{
//		pRENDERDOC_GetAPI RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
//		const int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void **>(&rdocApi));
//		//ASSERT(ret == 1);
//	}
//#elif !defined(__APPLE__)
//	#if defined(__ANDROID__)
//	if (void *mod = dlopen("libVkLayer_GLES_RenderDoc.so", RTLD_NOW | RTLD_NOLOAD))
//	#else
//	if (void *mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD))
//	#endif
//	{
//		pRENDERDOC_GetAPI RENDERDOC_GetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(dlsym(mod, "RENDERDOC_GetAPI"));
//		const int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, reinterpret_cast<void **>(&rdocApi));
//		//ASSERT(ret == 1);
//	}
//#endif
//
//	if (rdocApi)
//	{
//		int major, minor, patch;
//		apiVersion(&major, &minor, &patch);
//		LOGI("RenderDoc API: {}.{}.{}", major, minor, patch);
//	}
//}
//
//void RenderDocCapture::RemoveHooks()
//{
//	if (rdocApi)
//		rdocApi->RemoveHooks();
//}
//
//void RenderDocCapture::StartFrameCapture()
//{
//	if (rdocApi)
//		rdocApi->StartFrameCapture(nullptr, nullptr);
//}
//
//}
