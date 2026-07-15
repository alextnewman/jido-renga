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

#include "DeviceList.h"


#define DRIVER_NAME	"i2c_atmel_mxt"
#define DEVICE_PATH_SUFFIX	"i2c"
#define DEVICE_NAME	"I2C"


extern DeviceList *gDeviceList;


//#define TRACE_I2C_ATMEL_MXT
//#define TRACE_I2C_ATMEL_MXT_TOUCH
#ifdef TRACE_I2C_ATMEL_MXT
#	define TRACE(x...) dprintf(DRIVER_NAME ": " x)
#else
#	define TRACE(x...)
#endif
#ifdef TRACE_I2C_ATMEL_MXT_TOUCH
#	define TOUCH_TRACE(x...) dprintf(DRIVER_NAME ": touch: " x)
#else
#	define TOUCH_TRACE(x...)
#endif
#define ERROR(x...) dprintf(DRIVER_NAME ": " x)
#define TRACE_ALWAYS(x...)	dprintf(DRIVER_NAME ": " x)


#endif	// _I2C_ATMEL_MXT_DRIVER_H
