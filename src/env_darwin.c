/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#include <stdlib.h>

#include "ofc/types.h"
#include "ofc/env.h"
#include "ofc/libc.h"

#include "ofc/heap.h"

static const char *env2str[OFC_ENV_NUM] =
        {
                "OPEN_FILES_HOME",
                "OPEN_FILES_INSTALL",
                "OPEN_FILES_ROOT",
		"OPEN_FILES_MODE
        };

OFC_BOOL
ofc_env_get_impl(OFC_ENV_VALUE value, OFC_TCHAR *ptr, OFC_SIZET len) {
    char *env;
    OFC_BOOL ret;
    OFC_TCHAR *path;

    /*
     * Darwin does not support reentrant getenv
     */
    ret = OFC_FALSE;
    if (ptr != OFC_NULL && value < OFC_ENV_NUM) {
        env = getenv(env2str[value]);
        if (env != NULL) {
            path = ofc_cstr2tstr(env);
            ofc_tstrncpy(ptr, path, len);
            ofc_free(path);
            ret = OFC_TRUE;
        }

    }
    return (ret);
}

