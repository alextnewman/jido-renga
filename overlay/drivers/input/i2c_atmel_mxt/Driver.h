// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with Qwen 3.6
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef _I2C_ATMEL_MXT_DRIVER_H
#define _I2C_ATMEL_MXT_DRIVER_H


#include <Drivers.h>
#include <KernelExport.h>
#include <OS.h>
#include <util/kernel_cpp.h>

#include <common/Trace.h>

#include "DeviceList.h"


#define DRIVER_NAME	"i2c_atmel_mxt"
#define DEVICE_PATH_SUFFIX	"i2c"
#define DEVICE_NAME	"I2C"


extern DeviceList *gDeviceList;


//#define TRACE_I2C_ATMEL_MXT
//#define TRACE_I2C_ATMEL_MXT_TOUCH
#ifdef TRACE_I2C_ATMEL_MXT
#	define TRACE(x...) JR_DIAG_TRACE(DRIVER_NAME, x)
#else
#	define TRACE(x...) JR_DIAG_DISABLED()
#endif
#ifdef TRACE_I2C_ATMEL_MXT_TOUCH
#	define TOUCH_TRACE(x...) JR_DIAG_EVENT(DRIVER_NAME "/touch", x)
#else
#	define TOUCH_TRACE(x...) JR_DIAG_DISABLED()
#endif
#define ERROR(x...) JR_DIAG_ERROR(DRIVER_NAME, x)
#define TRACE_ALWAYS(x...) JR_DIAG_INFO(DRIVER_NAME, x)


#endif	// _I2C_ATMEL_MXT_DRIVER_H
