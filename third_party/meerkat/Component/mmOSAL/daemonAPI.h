/*
 * Copyright 2018 Samsung Electronics Co., Ltd
 *
 * Licensed under the Flora License, Version 1.1 (the License);
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://floralicense.org/license/
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __INCLUDE_DAEMONAPI_H__
#define __INCLUDE_DAEMONAPI_H__

#include "bDataType.h"

BOOL __OSAL_DaemonAPI_Init();
BOOL __OSAL_DaemonAPI_DeInit();

VOID __OSAL_DaemonAPI_Daemonize(const char* name);
BOOL __OSAL_DaemonAPI_IsRunning();

#endif // __INCLUDE_DAEMONAPI_H__
