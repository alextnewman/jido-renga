// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef _JR_SHIM_DEVICE_LIST_H
#define _JR_SHIM_DEVICE_LIST_H

// Host shim of the driver's "DeviceList.h" (vendored from hid_shared). The
// object-table logic under test never touches the device list; the driver
// headers only forward-declare a "DeviceList *gDeviceList" pointer, so an empty
// type is all the compiler needs. Not linked against the real DeviceList.cpp.

class DeviceList {
};

#endif	// _JR_SHIM_DEVICE_LIST_H
