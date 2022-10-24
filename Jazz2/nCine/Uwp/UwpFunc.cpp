#include "uwpfunc.h"
#include <string>
#include <windows.h>
#include <fileapifromapp.h>

using namespace Windows::System;
using namespace Windows::Foundation;
using namespace Windows::Storage;

//Platform::String^ local_folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path+L"\\";
extern "C" {
	void uwp_get_localfolder(char* output)
	{
		Platform::String^ local_folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path;
		std::wstring ws(local_folder->Data());
		std::string str(ws.begin(), ws.end());
		int a = str.length();
		strcpy(output, str.c_str());
	}
	void uwp_create_save_dir(void)
	{
		Platform::String^ local_folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\Cache";
		CreateDirectoryFromAppW(local_folder->Data(), NULL);
		local_folder = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\Source";
		CreateDirectoryFromAppW(local_folder->Data(), NULL);
	}
}