// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_DRIVER_H
#define INTEL_VALLEYVIEW_DRIVER_H

#include <device_manager.h>
#include <drivers/bus/PCI.h>
#include <lock.h>

#include <common/intel_valleyview/DisplaySharedInfo.h>
#include <common/intel_valleyview/P0Core.h>
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
	bool						nativeActive;
	bool						bcsReady;
	bool						cursorReady;
	bool						softBlanked;
	bool						cursorVisible;
	bool						p0MemoryQuarantined;
	uint32						gpuTestGeneration;
	area_id						gpuTestArea;
	area_id						registerArea;
	volatile uint8*				registers;
	area_id						sharedArea;
	valleyview::DisplaySharedInfo* sharedInfo;
	area_id						framebufferArea;
	void*						framebuffer;
	phys_addr_t					p0Physical;
	uint32						p0Size;
	area_id						p0PrivateArea;
	void*						p0Private;
	phys_addr_t					p0PrivatePhysical;
	valleyview::P0Layout			p0Layout;
	uint32*						p0SavedPtes;
	uint32						originalPipeSource;
	uint32						originalPlaneControl;
	uint32						originalPlaneAddress;
	uint32						originalPlaneLinearOffset;
	uint32						originalPlaneStride;
	uint32						originalPlaneSurface;
	uint32						originalPlaneTileOffset;
	uint32						originalPanelFitterControl;
	uint32						originalCxsr;
	uint32						originalCursorControl;
	uint32						originalCursorBase;
	uint32						originalCursorPosition;
	uint32						originalCursorPalette[2];
	uint32						dpmsMode;
	uint32						savedPwmControl;
	uint32						cursorHotX;
	uint32						cursorHotY;
	int32						cursorX;
	int32						cursorY;
	int32						nativeStatus;
	int32						bcsStatus;
	uint64						bcsSubmissions;
	uint64						bcsFailures;
	uint32						bcsSequence;
	valleyview::FirmwareSnapshot	snapshot;
};


extern device_manager_info* gDeviceManager;
extern device_module_info gValleyViewDeviceModule;

status_t PublishValleyViewGraphics(ValleyViewDevice& device);
status_t CaptureGpuDiagnostics(ValleyViewDevice& device,
	valleyview::GpuDiagnostics& diagnostics);
status_t RunGpuSelfTest(ValleyViewDevice& device,
	valleyview::GpuDiagnostics& diagnostics);
status_t InitializeP0(ValleyViewDevice& device);
status_t ShutdownP0(ValleyViewDevice& device);
status_t ValidateP0FirmwareState(const ValleyViewDevice& device);
void GetP0Status(const ValleyViewDevice& device, valleyview::P0Status& status);
status_t InitializeBcsRuntime(ValleyViewDevice& device);
status_t QuiesceBcsRuntime(ValleyViewDevice& device);
status_t SubmitBcsFill(ValleyViewDevice& device,
	const valleyview::BcsFillRequest& request);
status_t SubmitBcsBlit(ValleyViewDevice& device,
	const valleyview::BcsBlitRequest& request);
status_t GetBrightness(ValleyViewDevice& device,
	valleyview::BrightnessRequest& request);
status_t SetBrightness(ValleyViewDevice& device,
	const valleyview::BrightnessRequest& request);
status_t GetDpms(ValleyViewDevice& device, valleyview::DpmsRequest& request);
status_t SetDpms(ValleyViewDevice& device,
	const valleyview::DpmsRequest& request);
status_t SetCursorShape(ValleyViewDevice& device,
	const valleyview::CursorShapeRequest& request);
status_t MoveCursor(ValleyViewDevice& device,
	const valleyview::CursorMoveRequest& request);
status_t ShowCursor(ValleyViewDevice& device,
	const valleyview::CursorShowRequest& request);

#endif
