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
#include <common/intel_valleyview/RenderProtocol.h>


constexpr const char* kValleyViewDriverModuleName
	= "drivers/graphics/intel_valleyview/driver_v1";
constexpr const char* kValleyViewDeviceModuleName
	= "drivers/graphics/intel_valleyview/device_v1";
constexpr const char* kValleyViewAccelerantName
	= "intel_valleyview.accelerant";

struct ValleyViewDevice;

struct ValleyViewRenderBuffer {
	area_id					area;
	void*					address;
	area_id					mappingArea;
	team_id					mappingTeam;
	uint64*					physicalPages;
	uint32*					savedPtes;
	uint64					size;
	uint32					pageCount;
	uint32					handle;
	uint32					flags;
	uint32					ggttOffset;
	valleyview::RenderBufferDomain domain;
	bool					quarantined;
	ValleyViewRenderBuffer*	next;
};

struct ValleyViewClient {
	ValleyViewDevice*		device;
	ValleyViewRenderBuffer*	buffers;
	uint64					allocatedBytes;
	uint32					bufferCount;
	uint32					nextHandle;
};

struct ValleyViewDevice {
	device_node*				node;
	pci_device_module_info*	pci;
	pci_device*				pciDevice;
	pci_info					pciInfo;
	mutex						lock;
	mutex						renderLock;
	mutex						presentLock;
	mutex						bcsLock;
	int32						openCount;
	bool						enabled;
	bool						allowModeset;
	bool						graphicsPublished;
	bool						gpuFaulted;
	bool						nativeActive;
	bool						bcsReady;
	bool						rcsReady;
	bool						cursorReady;
	bool						softBlanked;
	bool						cursorVisible;
	bool						p0MemoryQuarantined;
	bool						renderMemoryQuarantined;
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
	area_id						scanoutArea[2];
	void*						scanout[2];
	phys_addr_t					scanoutPhysical[2];
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
	uint32						cursorMode;
	int32						cursorX;
	int32						cursorY;
	int32						nativeStatus;
	int32						bcsStatus;
	int32						rcsStatus;
	int32						presentStatus;
	int32						presentBcsStatus;
	thread_id					presentThread;
	bool						presentRunning;
	bool						presentEnabled;
	bool						presentUsesBcs;
	bool						presentPendingTimedOut;
	int32						activeScanout;
	int32						pendingScanout;
	bigtime_t					presentFlipStarted;
	uint64						bcsSubmissions;
	uint64						bcsFailures;
	uint64						bcsFillRequests;
	uint64						bcsBlitRequests;
	uint64						bcsPresentRequests;
	uint64						cpuFillFallbacks;
	uint64						cpuBlitFallbacks;
	uint64						presentFrames;
	uint64						presentFailures;
	uint64						presentBcsCopies;
	uint64						presentCpuCopies;
	uint64						presentCopyLastUs;
	uint64						presentCopyMaxUs;
	uint64						presentFlipLastUs;
	uint64						presentFlipMaxUs;
	uint64						cursorShapeUpdates;
	uint64						cursorBitmapUpdates;
	uint64						cursorMoveUpdates;
	uint64						cursorShowUpdates;
	uint64						renderMemoryTests;
	uint64						renderMemoryFailures;
	uint64						rcsTests;
	uint64						rcsFailures;
	uint64						rcsResets;
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
void ReleaseP0Areas(ValleyViewDevice& device);
status_t ValidateP0FirmwareState(const ValleyViewDevice& device);
void GetP0Status(ValleyViewDevice& device, valleyview::P0Status& status);
status_t InitializeBcsRuntime(ValleyViewDevice& device);
status_t QuiesceBcsRuntime(ValleyViewDevice& device);
status_t SubmitBcsPresent(ValleyViewDevice& device, uint32 sourceOffset,
	uint32 destinationOffset);
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
status_t SetCursorBitmap(ValleyViewDevice& device,
	const valleyview::CursorBitmapRequest& request);
status_t MoveCursor(ValleyViewDevice& device,
	const valleyview::CursorMoveRequest& request);
status_t ShowCursor(ValleyViewDevice& device,
	const valleyview::CursorShowRequest& request);
status_t CreateRenderBuffer(ValleyViewClient& client,
	valleyview::RenderBufferCreate& request);
status_t MapRenderBuffer(ValleyViewClient& client,
	valleyview::RenderBufferMap& request);
status_t DiscardRenderBufferMapping(ValleyViewClient& client, uint32 handle,
	area_id area);
status_t CloseRenderBuffer(ValleyViewClient& client, uint32 handle);
status_t SetRenderBufferDomain(ValleyViewClient& client,
	valleyview::RenderBufferSetDomain& request);
status_t RunRenderMemoryTest(ValleyViewClient& client,
	valleyview::RenderMemoryTest& test);
status_t RunRcsDiagnostic(ValleyViewClient& client,
	valleyview::RcsDiagnostic& diagnostics);
void DestroyRenderClient(ValleyViewClient& client);
status_t BindRenderBufferGgtt(ValleyViewDevice& device,
	ValleyViewRenderBuffer& buffer);
status_t UnbindRenderBufferGgtt(ValleyViewDevice& device,
	ValleyViewRenderBuffer& buffer, uint32* observedPtes = NULL);
status_t SubmitRenderBcsCopy(ValleyViewDevice& device, uint32 sourceOffset,
	uint32 destinationOffset, uint32& completionMarker);
status_t ExecuteRcsDiagnostic(ValleyViewDevice& device,
	ValleyViewRenderBuffer& buffer, valleyview::RcsDiagnostic& diagnostics);

#endif
