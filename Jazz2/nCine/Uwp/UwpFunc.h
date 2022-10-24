#pragma once
#ifndef _UWP_FUNC_H
#define _UWP_FUNC_H

#ifdef __cplusplus
extern "C" {
#endif
	void uwp_get_localfolder(char* output);
	void uwp_create_save_dir(void);

#ifdef __cplusplus
}
#endif

#endif _UWP_FUNC_H