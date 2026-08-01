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
#include <common/intel_valleyview/RenderQueueCore.h>
#include <common/intel_valleyview/RenderProtocol.h>


constexpr const char* kValleyViewDriverModuleName
	= "drivers/graphics/intel_valleyview/driver_v1";
constexpr const char* kValleyViewDeviceModuleName
	= "drivers/graphics/intel_valleyview/device_v1";
constexpr const char* kValleyViewAccelerantName
	= "intel_valleyview.accelerant";

struct ValleyViewDevice;
struct select_sync_pool;

enum ValleyViewRenderGgttEncoding {
	kValleyViewRenderGgttData,
	kValleyViewRenderGgttPpgttDirectory
};

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
	uint32					ppgttOffset;
	uint32					ggttAlignmentPages;
	ValleyViewRenderGgttEncoding ggttEncoding;
	valleyview::RenderBufferDomain domain;
	uint32					queuedReferenceCount;
	bool					closePending;
	bool					quarantined;
	ValleyViewRenderBuffer*	next;
};

struct ValleyViewPpgttState {
	ValleyViewRenderBuffer*	directoryBuffer;
	ValleyViewRenderBuffer*	scratchBuffer;
	uint8*					bitmap;
	uint32					ppDirBase;
	bool					ready;
	bool					quarantined;
};

struct ValleyViewRenderJob {
	ValleyViewRenderJob*	next;
	struct ValleyViewClient* client;
	valleyview::RenderSubmit submit;
	uint8*					batch;
	uint64					fence;
	bigtime_t				enqueuedAt;
};

struct ValleyViewRenderCompletion {
	ValleyViewRenderCompletion* next;
	valleyview::RenderQueueCompletion record;
};

struct ValleyViewClient {
	ValleyViewDevice*		device;
	ValleyViewClient*		queueNext;
	ValleyViewRenderBuffer*	buffers;
	uint64					allocatedBytes;
	uint32					bufferCount;
	uint32					nextHandle;
	uint32					contextHandle;
	uint32					contextGeneration;
	ValleyViewPpgttState		ppgtt;
	valleyview::RenderClientQueueState queueState;
	valleyview::RenderQueueMode queueMode;
	ValleyViewRenderJob*		queueHead;
	ValleyViewRenderJob*		queueTail;
	ValleyViewRenderJob*		activeJob;
	ValleyViewRenderCompletion* completionHead;
	ValleyViewRenderCompletion* completionTail;
	uint32					completionCount;
	uint32					queueHighWater;
	uint64					lastFailedFence;
	int32					lastFailureStatus;
	uint64					submittedJobs;
	uint64					completedJobs;
	uint64					failedJobs;
	uint64					cancelledJobs;
	uint64					noResetJobs;
	uint64					totalQueueLatencyUs;
	uint64					maxQueueLatencyUs;
	uint64					totalExecutionUs;
	uint64					maxExecutionUs;
	sem_id					completionSem;
	sem_id					queueIdleSem;
	select_sync_pool*		selectPool;
	bool					failNextSubmission;
	bool					queueLost;
	bool					closing;
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
	mutex						renderQueueLock;
	int32						openCount;
	bool						enabled;
	bool						allowModeset;
	bool						graphicsPublished;
	bool						gpuFaulted;
	bool						nativeActive;
	bool						bcsReady;
	bool						rcsReady;
	bool						rcsSubmissionReady;
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
	uint64						rcsSubmissions;
	uint64						rcsSubmissionFailures;
	uint64						renderDirectPresents;
	uint64						renderDirectPresentFailures;
	sem_id						renderQueueSem;
	thread_id					renderQueueThread;
	ValleyViewClient*		renderClients;
	ValleyViewClient*		renderQueueCursor;
	uint32						renderQueuedJobs;
	uint32						renderQueueHighWater;
	bool						renderQueueRunning;
	bool						renderQueueReady;
	uint32						bcsSequence;
	uint32						rcsSubmitSequence;
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
status_t SubmitBcsRenderCopy(ValleyViewDevice& device, uint32 sourceOffset,
	uint32 sourceStride, const valleyview::RenderDirectPresent& request);
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
status_t CreateRenderContext(ValleyViewClient& client,
	valleyview::RenderContextCreate& request);
status_t DestroyRenderContext(ValleyViewClient& client,
	valleyview::RenderContextDestroy& request);
status_t SubmitRenderCommands(ValleyViewClient& client,
	valleyview::RenderSubmit& submit, const void* immutableBatch = NULL,
	bool resetAfterSubmission = true);
status_t InitializeRenderQueue(ValleyViewDevice& device);
void ShutdownRenderQueue(ValleyViewDevice& device);
status_t RegisterRenderQueueClient(ValleyViewClient& client);
void ShutdownRenderQueueClient(ValleyViewClient& client);
status_t ConfigureRenderQueue(ValleyViewClient& client,
	valleyview::RenderQueueConfigure& request);
status_t EnqueueRenderCommands(ValleyViewClient& client,
	valleyview::RenderQueueSubmit& request);
status_t WaitRenderQueueFence(ValleyViewClient& client,
	valleyview::RenderQueueWait& request);
status_t DequeueRenderCompletion(ValleyViewClient& client,
	valleyview::RenderQueueCompletion& request);
status_t GetRenderQueueInfo(ValleyViewClient& client,
	valleyview::RenderQueueInfo& info);
status_t SubmitRenderDirectPresent(ValleyViewClient& client,
	valleyview::RenderDirectPresent& request);
status_t SelectRenderQueue(ValleyViewClient& client, uint8 event,
	selectsync* sync);
status_t DeselectRenderQueue(ValleyViewClient& client, uint8 event,
	selectsync* sync);
ValleyViewRenderBuffer* FindClientRenderBuffer(ValleyViewClient& client,
	uint32 handle);
void ReleaseRenderQueueReferences(ValleyViewClient& client,
	const uint32* handles, uint32 count);
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
	ValleyViewRenderBuffer& buffer, ValleyViewRenderBuffer& shaderBuffer,
	valleyview::RcsDiagnostic& diagnostics);
status_t ExecuteRcsSubmission(ValleyViewDevice& device,
	ValleyViewRenderBuffer& workspace, uint32 ppDirBase,
	valleyview::RenderSubmit& submit, bool resetAfterSubmission = true);

#endif
