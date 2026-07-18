// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_DRIVER_H
#define INTEL_VALLEYVIEW_DRIVER_H

#include <device_manager.h>
#include <drivers/bus/PCI.h>
#include <lock.h>

#include <common/intel_valleyview/DisplaySharedInfo.h>
#include <common/intel_valleyview/Protocol.h>


constexpr const char* kValleyViewDriverModuleName
	= "drivers/graphics/intel_valleyview/driver_v1";
constexpr const char* kValleyViewDeviceModuleName
	= "drivers/graphics/intel_valleyview/device_v1";
constexpr const char* kValleyViewAccelerantName
	= "intel_valleyview.accelerant";

struct ValleyViewDevice {
	device_node*				node;
	pci_device_module_info*	pci;
	pci_device*				pciDevice;
	pci_info					pciInfo;
	mutex						lock;
	int32						openCount;
	bool						enabled;
	bool						allowModeset;
	bool						graphicsPublished;
	bool						gpuFaulted;
	uint32						gpuTestGeneration;
	area_id						gpuTestArea;
	area_id						sharedArea;
	valleyview::DisplaySharedInfo* sharedInfo;
	area_id						framebufferArea;
	void*						framebuffer;
	valleyview::FirmwareSnapshot	snapshot;
};


extern device_manager_info* gDeviceManager;
extern device_module_info gValleyViewDeviceModule;

status_t PublishValleyViewGraphics(ValleyViewDevice& device);
status_t CaptureGpuDiagnostics(ValleyViewDevice& device,
	valleyview::GpuDiagnostics& diagnostics);
status_t RunGpuSelfTest(ValleyViewDevice& device,
	valleyview::GpuDiagnostics& diagnostics);

#endif
