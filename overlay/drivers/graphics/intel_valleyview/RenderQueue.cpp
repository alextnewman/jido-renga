// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include "Driver.h"

#include <common/intel_valleyview/RenderCommandCore.h>
#include <common/intel_valleyview/RenderSubmitCore.h>

#include <fs/select_sync_pool.h>
#include <KernelExport.h>

#include <stdlib.h>
#include <string.h>


namespace {

void
LockQueue(ValleyViewDevice& device)
{
	mutex_lock(&device.renderQueueLock);
}


void
UnlockQueue(ValleyViewDevice& device)
{
	mutex_unlock(&device.renderQueueLock);
}


void
FreeRenderJob(ValleyViewRenderJob* job)
{
	if (job == NULL)
		return;
	free(job->batch);
	free(job);
}


void
PruneCompletionLocked(ValleyViewClient& client)
{
	if (client.completionCount < valleyview::kRenderMaxCompletionRecords)
		return;
	ValleyViewRenderCompletion* completion = client.completionHead;
	if (completion == NULL)
		return;
	client.completionHead = completion->next;
	if (client.completionHead == NULL)
		client.completionTail = NULL;
	client.completionCount--;
	free(completion);
}


bool
AppendCompletionLocked(ValleyViewClient& client,
	const ValleyViewRenderJob& job, valleyview::RenderFenceResult result,
	status_t status, bigtime_t startedAt, bigtime_t retiredAt)
{
	PruneCompletionLocked(client);
	ValleyViewRenderCompletion* completion
		= static_cast<ValleyViewRenderCompletion*>(
			calloc(1, sizeof(ValleyViewRenderCompletion)));
	if (completion == NULL)
		return false;

	completion->record.header = valleyview::MakeRenderAbiHeader(
		sizeof(completion->record));
	completion->record.result = result;
	completion->record.fence = job.fence;
	completion->record.status = status;
	if (job.kind == ValleyViewRenderJob::kDirectPresent) {
		completion->record.flags
			|= valleyview::kRenderQueueCompletionDirectPresent;
		if (job.dropPresent) {
			completion->record.flags
				|= valleyview::kRenderQueueCompletionPresentDropped;
		}
	}
	completion->record.stage = job.submit.stage;
	completion->record.diagnosticFlags = job.submit.diagnosticFlags;
	completion->record.parserReason = job.submit.parserReason;
	completion->record.parsedCommandCount = job.submit.parsedCommandCount;
	completion->record.primitiveCount = job.submit.primitiveCount;
	completion->record.resetStatus = job.submit.resetStatus;
	completion->record.ringRestoreStatus = job.submit.ringRestoreStatus;
	completion->record.cacheRestoreStatus = job.submit.cacheRestoreStatus;
	completion->record.ppgttControlRestoreStatus
		= job.submit.ppgttControlRestoreStatus;
	completion->record.forcewakeReleaseStatus
		= job.submit.forcewakeReleaseStatus;
	completion->record.wakeRestoreStatus = job.submit.wakeRestoreStatus;
	completion->record.enqueuedUs = job.enqueuedAt;
	completion->record.startedUs = startedAt;
	completion->record.retiredUs = retiredAt;

	if (client.completionTail != NULL)
		client.completionTail->next = completion;
	else
		client.completionHead = completion;
	client.completionTail = completion;
	client.completionCount++;
	return true;
}


void
NotifyCompletion(ValleyViewClient& client, bool failed)
{
	release_sem_etc(client.completionSem, 1, B_DO_NOT_RESCHEDULE);
	if (client.selectPool != NULL) {
		notify_select_event_pool(client.selectPool, B_SELECT_READ);
		if (failed)
			notify_select_event_pool(client.selectPool, B_SELECT_ERROR);
	}
}


ValleyViewClient*
SelectReadyClientLocked(ValleyViewDevice& device)
{
	if (device.renderClients == NULL)
		return NULL;

	ValleyViewClient* start = device.renderQueueCursor != NULL
		? device.renderQueueCursor->queueNext : device.renderClients;
	if (start == NULL)
		start = device.renderClients;
	ValleyViewClient* client = start;
	do {
		if (!client->closing && !client->queueLost
			&& client->queueMode != valleyview::kRenderQueueModeSafe
			&& client->activeJob == NULL && client->queueHead != NULL) {
			device.renderQueueCursor = client;
			return client;
		}
		client = client->queueNext;
		if (client == NULL)
			client = device.renderClients;
	} while (client != start);
	return NULL;
}


void
ReleaseJobReferences(ValleyViewRenderJob& job)
{
	if (job.kind == ValleyViewRenderJob::kDirectPresent) {
		ReleaseRenderQueueReferences(*job.client,
			&job.present.sourceHandle, 1);
	} else {
		ReleaseRenderQueueReferences(*job.client, job.submit.objectHandles,
			job.submit.objectCount);
	}
}


int32
RenderQueueWorker(void* cookie)
{
	ValleyViewDevice& device = *static_cast<ValleyViewDevice*>(cookie);
	for (;;) {
		status_t waitStatus = acquire_sem(device.renderQueueSem);
		if (waitStatus != B_OK && waitStatus != B_INTERRUPTED)
			break;

		LockQueue(device);
		if (!device.renderQueueRunning) {
			UnlockQueue(device);
			break;
		}

		ValleyViewClient* client = SelectReadyClientLocked(device);
		if (client == NULL) {
			UnlockQueue(device);
			continue;
		}

		ValleyViewRenderJob* job = client->queueHead;
		client->queueHead = job->next;
		if (client->queueHead == NULL)
			client->queueTail = NULL;
		job->next = NULL;
		if (!valleyview::StartRenderJob(client->queueState, job->fence)) {
			client->queueLost = true;
			client->lastFailedFence = job->fence;
			client->lastFailureStatus = B_BAD_DATA;
		}
		client->activeJob = job;
		if (device.renderQueuedJobs > 0)
			device.renderQueuedJobs--;
		const bool injectFailure = client->failNextSubmission;
		const bool commandJob = job->kind == ValleyViewRenderJob::kCommands;
		const bool resetAfterSubmission = commandJob
			&& client->queueMode
				!= valleyview::kRenderQueueModeFailureOnlyReset;
		client->failNextSubmission = false;
		if (client->selectPool != NULL)
			notify_select_event_pool(client->selectPool, B_SELECT_WRITE);
		UnlockQueue(device);

		const bigtime_t startedAt = system_time();
		status_t status;
		if (injectFailure) {
			status = B_TIMED_OUT;
			job->submit.status = status;
		} else if (job->kind == ValleyViewRenderJob::kDirectPresent) {
			status = job->dropPresent
				? B_OK
				: SubmitRenderDirectPresent(*client, job->present, true);
		} else {
			status = SubmitRenderCommands(*client, job->submit, job->batch,
				resetAfterSubmission);
		}
		const bigtime_t retiredAt = system_time();
		ReleaseJobReferences(*job);

		ValleyViewRenderJob* cancelled = NULL;
		uint32 completionSignals = 0;
		LockQueue(device);
		const uint64 queueLatency = startedAt > job->enqueuedAt
			? static_cast<uint64>(startedAt - job->enqueuedAt) : 0;
		const uint64 execution = retiredAt > startedAt
			? static_cast<uint64>(retiredAt - startedAt) : 0;
		client->totalQueueLatencyUs += queueLatency;
		client->totalExecutionUs += execution;
		if (queueLatency > client->maxQueueLatencyUs)
			client->maxQueueLatencyUs = queueLatency;
		if (execution > client->maxExecutionUs)
			client->maxExecutionUs = execution;

		if (status == B_OK) {
			if (!valleyview::CompleteRenderJob(client->queueState, job->fence))
				status = B_BAD_DATA;
		}
		if (status == B_OK) {
			client->completedJobs++;
			if (commandJob && !resetAfterSubmission)
				client->noResetJobs++;
			if (job->kind == ValleyViewRenderJob::kDirectPresent
				&& job->dropPresent) {
				device.renderDirectPresentsDropped++;
			}
			if (AppendCompletionLocked(*client, *job,
					valleyview::kRenderFenceComplete, B_OK, startedAt,
					retiredAt)) {
				completionSignals++;
			}
		} else {
			valleyview::FailRenderJob(client->queueState, job->fence);
			client->queueLost = true;
			client->lastFailedFence = job->fence;
			client->lastFailureStatus = status;
			client->failedJobs++;
			if (AppendCompletionLocked(*client, *job,
					valleyview::kRenderFenceFailed, status, startedAt,
					retiredAt)) {
				completionSignals++;
			}

			cancelled = client->queueHead;
			client->queueHead = NULL;
			client->queueTail = NULL;
			for (ValleyViewRenderJob* item = cancelled; item != NULL;
					item = item->next) {
				valleyview::CancelQueuedRenderJob(client->queueState,
					item->fence);
				if (device.renderQueuedJobs > 0)
					device.renderQueuedJobs--;
				client->cancelledJobs++;
				if (AppendCompletionLocked(*client, *item,
						valleyview::kRenderFenceCancelled, B_CANCELED, 0,
						retiredAt)) {
					completionSignals++;
				}
			}
		}
		UnlockQueue(device);

		FreeRenderJob(job);
		while (cancelled != NULL) {
			ValleyViewRenderJob* next = cancelled->next;
			ReleaseJobReferences(*cancelled);
			FreeRenderJob(cancelled);
			cancelled = next;
		}
		for (uint32 index = 0; index < completionSignals; index++)
			NotifyCompletion(*client, status != B_OK);
		LockQueue(device);
		client->activeJob = NULL;
		const bool closing = client->closing;
		UnlockQueue(device);
		if (closing)
			release_sem_etc(client->queueIdleSem, 1, B_DO_NOT_RESCHEDULE);
	}
	return B_OK;
}


bool
FindFenceLocked(ValleyViewClient& client, uint64 fence,
	valleyview::RenderFenceResult& result, status_t& status)
{
	for (ValleyViewRenderCompletion* completion = client.completionHead;
			completion != NULL; completion = completion->next) {
		if (completion->record.fence == fence) {
			result = completion->record.result;
			status = completion->record.status;
			return true;
		}
	}

	if (!valleyview::RenderFenceIsRetired(client.queueState, fence))
		return false;
	if (client.lastFailedFence == 0 || fence < client.lastFailedFence) {
		result = valleyview::kRenderFenceComplete;
		status = B_OK;
	} else if (fence == client.lastFailedFence) {
		result = valleyview::kRenderFenceFailed;
		status = client.lastFailureStatus;
	} else {
		result = valleyview::kRenderFenceCancelled;
		status = B_CANCELED;
	}
	return true;
}


} // namespace


status_t
InitializeRenderQueue(ValleyViewDevice& device)
{
	device.renderQueueSem = create_sem(0, "intel_valleyview render queue");
	if (device.renderQueueSem < B_OK)
		return device.renderQueueSem;

	device.renderQueueRunning = true;
	device.renderQueueThread = spawn_kernel_thread(RenderQueueWorker,
		"intel_valleyview render queue", B_REAL_TIME_DISPLAY_PRIORITY, &device);
	if (device.renderQueueThread < B_OK) {
		status_t status = device.renderQueueThread;
		device.renderQueueRunning = false;
		delete_sem(device.renderQueueSem);
		device.renderQueueSem = -1;
		return status;
	}
	status_t status = resume_thread(device.renderQueueThread);
	if (status != B_OK) {
		device.renderQueueRunning = false;
		kill_thread(device.renderQueueThread);
		delete_sem(device.renderQueueSem);
		device.renderQueueThread = -1;
		device.renderQueueSem = -1;
		return status;
	}
	device.renderQueueReady = true;
	return B_OK;
}


void
ShutdownRenderQueue(ValleyViewDevice& device)
{
	if (device.renderQueueSem < B_OK)
		return;
	LockQueue(device);
	device.renderQueueReady = false;
	device.renderQueueRunning = false;
	UnlockQueue(device);
	release_sem(device.renderQueueSem);
	if (device.renderQueueThread >= B_OK) {
		status_t result;
		wait_for_thread(device.renderQueueThread, &result);
	}
	delete_sem(device.renderQueueSem);
	device.renderQueueSem = -1;
	device.renderQueueThread = -1;
}


status_t
RegisterRenderQueueClient(ValleyViewClient& client)
{
	client.completionSem = create_sem(0, "intel_valleyview render completion");
	if (client.completionSem < B_OK)
		return client.completionSem;
	client.queueIdleSem = create_sem(0, "intel_valleyview render idle");
	if (client.queueIdleSem < B_OK) {
		status_t status = client.queueIdleSem;
		delete_sem(client.completionSem);
		client.completionSem = -1;
		return status;
	}
	client.queueState = valleyview::InitializeRenderClientQueueState();
	client.queueMode = valleyview::kRenderQueueModeSafe;

	ValleyViewDevice& device = *client.device;
	LockQueue(device);
	client.queueNext = device.renderClients;
	device.renderClients = &client;
	UnlockQueue(device);
	return B_OK;
}


void
ShutdownRenderQueueClient(ValleyViewClient& client)
{
	ValleyViewDevice& device = *client.device;
	ValleyViewRenderJob* cancelled;
	bool waitForActive;
	LockQueue(device);
	client.closing = true;
	cancelled = client.queueHead;
	client.queueHead = NULL;
	client.queueTail = NULL;
	for (ValleyViewRenderJob* item = cancelled; item != NULL; item = item->next) {
		valleyview::CancelQueuedRenderJob(client.queueState, item->fence);
		if (device.renderQueuedJobs > 0)
			device.renderQueuedJobs--;
	}
	waitForActive = client.activeJob != NULL;
	UnlockQueue(device);

	while (cancelled != NULL) {
		ValleyViewRenderJob* next = cancelled->next;
		ReleaseJobReferences(*cancelled);
		FreeRenderJob(cancelled);
		cancelled = next;
	}
	if (waitForActive)
		acquire_sem(client.queueIdleSem);

	LockQueue(device);
	ValleyViewClient** link = &device.renderClients;
	while (*link != NULL && *link != &client)
		link = &(*link)->queueNext;
	if (*link == &client)
		*link = client.queueNext;
	if (device.renderQueueCursor == &client)
		device.renderQueueCursor = NULL;
	ValleyViewRenderCompletion* completion = client.completionHead;
	client.completionHead = NULL;
	client.completionTail = NULL;
	client.completionCount = 0;
	UnlockQueue(device);

	while (completion != NULL) {
		ValleyViewRenderCompletion* next = completion->next;
		free(completion);
		completion = next;
	}
	if (client.selectPool != NULL)
		delete_select_sync_pool(client.selectPool);
	delete_sem(client.queueIdleSem);
	delete_sem(client.completionSem);
	client.queueIdleSem = -1;
	client.completionSem = -1;
}


status_t
ConfigureRenderQueue(ValleyViewClient& client,
	valleyview::RenderQueueConfigure& request)
{
	request.status = B_NO_INIT;
	if (request.mode == valleyview::kRenderQueueModeFailureOnlyReset) {
		request.status = B_NOT_SUPPORTED;
		return request.status;
	}
	if (request.mode != valleyview::kRenderQueueModeSafe
		&& request.mode != valleyview::kRenderQueueModeAsynchronous
		&& request.mode != valleyview::kRenderQueueModeDirectPresent) {
		request.status = B_BAD_VALUE;
		return request.status;
	}
	if ((request.flags & ~valleyview::kRenderQueueFailNextSubmission) != 0) {
		request.status = B_BAD_VALUE;
		return request.status;
	}

	ValleyViewDevice& device = *client.device;
	LockQueue(device);
	if (client.activeJob != NULL || client.queueHead != NULL) {
		request.status = B_BUSY;
	} else if (request.mode != valleyview::kRenderQueueModeSafe
		&& !device.renderQueueReady) {
		request.status = B_NOT_SUPPORTED;
	} else if (request.mode == valleyview::kRenderQueueModeDirectPresent
		&& (!device.nativeActive || !device.bcsReady || device.gpuFaulted
			|| device.p0MemoryQuarantined
			|| device.renderMemoryQuarantined)) {
		request.status = B_NOT_SUPPORTED;
	} else {
		client.queueMode
			= static_cast<valleyview::RenderQueueMode>(request.mode);
		client.failNextSubmission
			= (request.flags & valleyview::kRenderQueueFailNextSubmission) != 0;
		client.queueLost = false;
		client.lastFailedFence = 0;
		client.lastFailureStatus = B_OK;
		request.status = B_OK;
	}
	UnlockQueue(device);
	return request.status;
}


status_t
EnqueueRenderCommands(ValleyViewClient& client,
	valleyview::RenderQueueSubmit& request)
{
	request.fence = valleyview::kInvalidRenderFence;
	request.status = B_NO_INIT;
	request.parserReason = valleyview::kRenderCommandReasonNone;
	request.parsedCommandCount = 0;
	request.primitiveCount = 0;
	if (request.flags != 0
		|| !valleyview::ValidateRenderObjectReferences(request.objects,
			request.objectCount, request.batchHandle)
		|| !valleyview::ValidateRenderSubmitBatchRange(
			valleyview::kRenderMaxBufferSize, request.batchOffset,
			request.batchLength)) {
		request.status = B_BAD_VALUE;
		return request.status;
	}

	ValleyViewRenderJob* job = static_cast<ValleyViewRenderJob*>(
		calloc(1, sizeof(ValleyViewRenderJob)));
	if (job == NULL) {
		request.status = B_NO_MEMORY;
		return request.status;
	}
	job->batch = static_cast<uint8*>(malloc(request.batchLength));
	if (job->batch == NULL) {
		FreeRenderJob(job);
		request.status = B_NO_MEMORY;
		return request.status;
	}

	ValleyViewDevice& device = *client.device;
	mutex_lock(&device.renderLock);
	status_t status = B_OK;
	ValleyViewRenderBuffer* batchBuffer = NULL;
	for (uint32 index = 0; status == B_OK && index < request.objectCount;
			index++) {
		ValleyViewRenderBuffer* buffer = FindClientRenderBuffer(client,
			request.objects[index].handle);
		const bool orderedDomain = buffer != NULL
			&& (buffer->domain == valleyview::kRenderDomainCpu
				|| (buffer->queuedReferenceCount != 0
					&& (buffer->domain == valleyview::kRenderDomainRcs
						|| buffer->domain == valleyview::kRenderDomainBcs)));
		if (buffer == NULL || buffer->quarantined || buffer->closePending
			|| !orderedDomain
			|| (buffer->handle == request.batchHandle
				&& buffer->domain != valleyview::kRenderDomainCpu)
			|| buffer->ppgttOffset
				== valleyview::kInvalidRenderPpgttOffset) {
			status = B_BAD_VALUE;
			break;
		}
		if (buffer->handle == request.batchHandle)
			batchBuffer = buffer;
	}
	if (status == B_OK && (batchBuffer == NULL
		|| !valleyview::ValidateRenderSubmitBatchRange(batchBuffer->size,
			request.batchOffset, request.batchLength))) {
		status = B_BAD_VALUE;
	}
	if (status == B_OK) {
		memcpy(job->batch,
			static_cast<const uint8*>(batchBuffer->address)
				+ request.batchOffset,
			request.batchLength);
		const valleyview::RenderCommandResult parser
			= valleyview::ParseRenderCommands(job->batch, request.batchLength);
		request.parserReason = parser.reason;
		request.parsedCommandCount = parser.parsedCommandCount;
		request.primitiveCount = parser.primitiveCount;
		if (parser.status != valleyview::kRenderCommandStatusAccepted)
			status = B_NOT_ALLOWED;
	}

	if (status == B_OK) {
		LockQueue(device);
		if (!device.renderQueueReady || client.closing || client.queueLost
			|| client.queueMode == valleyview::kRenderQueueModeSafe) {
			status = B_NOT_ALLOWED;
		} else if (!valleyview::QueueRenderJob(client.queueState,
				device.renderQueuedJobs, job->fence)) {
			status = B_WOULD_BLOCK;
		} else {
			job->client = &client;
			job->enqueuedAt = system_time();
			job->submit.header = valleyview::MakeRenderAbiHeader(
				sizeof(job->submit));
			job->submit.contextHandle = request.contextHandle;
			job->submit.batchHandle = request.batchHandle;
			job->submit.batchOffset = request.batchOffset;
			job->submit.batchLength = request.batchLength;
			job->submit.objectCount = request.objectCount;
			job->submit.status = B_NO_INIT;
			job->submit.parserReason = request.parserReason;
			job->submit.parsedCommandCount = request.parsedCommandCount;
			job->submit.primitiveCount = request.primitiveCount;
			job->submit.resetStatus = B_NO_INIT;
			job->submit.ringRestoreStatus = B_NO_INIT;
			job->submit.cacheRestoreStatus = B_NO_INIT;
			job->submit.ppgttControlRestoreStatus = B_NO_INIT;
			job->submit.forcewakeReleaseStatus = B_NO_INIT;
			job->submit.wakeRestoreStatus = B_NO_INIT;
			for (uint32 index = 0; index < request.objectCount; index++) {
				job->submit.objectHandles[index] = request.objects[index].handle;
				ValleyViewRenderBuffer* buffer = FindClientRenderBuffer(client,
					request.objects[index].handle);
				buffer->queuedReferenceCount++;
			}
			if (client.queueTail != NULL)
				client.queueTail->next = job;
			else
				client.queueHead = job;
			client.queueTail = job;
			device.renderQueuedJobs++;
			if (device.renderQueuedJobs > device.renderQueueHighWater)
				device.renderQueueHighWater = device.renderQueuedJobs;
			if (client.queueState.queuedJobs > client.queueHighWater)
				client.queueHighWater = client.queueState.queuedJobs;
			client.submittedJobs++;
			request.fence = job->fence;
			request.status = B_OK;
		}
		UnlockQueue(device);
	}
	mutex_unlock(&device.renderLock);

	if (status != B_OK) {
		FreeRenderJob(job);
		request.status = status;
		return status;
	}
	release_sem(device.renderQueueSem);
	return B_OK;
}

status_t
EnqueueRenderDirectPresent(ValleyViewClient& client,
	valleyview::RenderDirectPresent& request)
{
	request.fence = valleyview::kInvalidRenderFence;
	request.status = B_NO_INIT;
	request.elapsedUs = 0;
	if (request.flags != valleyview::kRenderDirectPresentAsynchronous
		|| request.streamId == 0
		|| !valleyview::ValidateRenderDirectPresentGeometry(request)) {
		request.status = B_BAD_VALUE;
		return request.status;
	}

	ValleyViewRenderJob* job = static_cast<ValleyViewRenderJob*>(
		calloc(1, sizeof(ValleyViewRenderJob)));
	if (job == NULL) {
		request.status = B_NO_MEMORY;
		return request.status;
	}
	job->kind = ValleyViewRenderJob::kDirectPresent;
	job->present = request;
	job->submit.resetStatus = B_NO_INIT;
	job->submit.ringRestoreStatus = B_NO_INIT;
	job->submit.cacheRestoreStatus = B_NO_INIT;
	job->submit.ppgttControlRestoreStatus = B_NO_INIT;
	job->submit.forcewakeReleaseStatus = B_NO_INIT;
	job->submit.wakeRestoreStatus = B_NO_INIT;

	const bigtime_t started = system_time();
	ValleyViewDevice& device = *client.device;
	mutex_lock(&device.renderLock);
	ValleyViewRenderBuffer* buffer = FindClientRenderBuffer(client,
		request.sourceHandle);
	const uint64 sourceBytes
		= valleyview::RenderDirectPresentSourceBytes(request);
	status_t status = !device.nativeActive || !device.bcsReady
			|| device.gpuFaulted || device.p0MemoryQuarantined
			|| device.renderMemoryQuarantined
		? B_NOT_SUPPORTED : B_OK;
	const bool orderedDomain = buffer != NULL
		&& (buffer->domain == valleyview::kRenderDomainCpu
			|| (buffer->queuedReferenceCount != 0
				&& (buffer->domain == valleyview::kRenderDomainRcs
					|| buffer->domain == valleyview::kRenderDomainBcs)));
	if (buffer == NULL || buffer->quarantined || buffer->closePending
		|| !orderedDomain
		|| buffer->ggttOffset == valleyview::kInvalidRenderGgttOffset
		|| request.sourceOffset > buffer->size
		|| sourceBytes > buffer->size - request.sourceOffset) {
		status = B_BAD_VALUE;
	}

	if (status == B_OK) {
		LockQueue(device);
		if (!device.renderQueueReady || client.closing || client.queueLost
			|| client.queueMode
				!= valleyview::kRenderQueueModeDirectPresent) {
			status = B_NOT_ALLOWED;
		} else if (!valleyview::QueueRenderJob(client.queueState,
				device.renderQueuedJobs, job->fence)) {
			status = B_WOULD_BLOCK;
		} else {
			job->client = &client;
			job->enqueuedAt = started;
			job->present.fence = job->fence;
			for (ValleyViewRenderJob* item = client.queueHead;
					item != NULL; item = item->next) {
				if (item->kind == ValleyViewRenderJob::kDirectPresent
					&& valleyview::ShouldDropQueuedPresentation(
						item->fence, item->present.streamId,
						job->fence, job->present.streamId)) {
					item->dropPresent = true;
				}
			}
			buffer->queuedReferenceCount++;
			if (client.queueTail != NULL)
				client.queueTail->next = job;
			else
				client.queueHead = job;
			client.queueTail = job;
			device.renderQueuedJobs++;
			if (device.renderQueuedJobs > device.renderQueueHighWater)
				device.renderQueueHighWater = device.renderQueuedJobs;
			if (client.queueState.queuedJobs > client.queueHighWater)
				client.queueHighWater = client.queueState.queuedJobs;
			client.submittedJobs++;
			device.renderDirectPresentsQueued++;
			request.fence = job->fence;
			request.status = B_OK;
		}
		UnlockQueue(device);
	}
	mutex_unlock(&device.renderLock);

	const bigtime_t elapsed = system_time() - started;
	request.elapsedUs = elapsed > 0 ? static_cast<uint64>(elapsed) : 0;
	if (status != B_OK) {
		FreeRenderJob(job);
		request.status = status;
		return status;
	}
	release_sem(device.renderQueueSem);
	return B_OK;
}


status_t
WaitRenderQueueFence(ValleyViewClient& client,
	valleyview::RenderQueueWait& request)
{
	request.result = valleyview::kRenderFencePending;
	request.status = B_WOULD_BLOCK;
	request.retiredFence = 0;
	if (request.flags != 0 || request.reserved != 0
		|| request.fence == valleyview::kInvalidRenderFence
		|| request.timeoutUs < -1) {
		request.status = B_BAD_VALUE;
		return B_BAD_VALUE;
	}

	const bigtime_t deadline = request.timeoutUs > 0
		? system_time() + request.timeoutUs : 0;
	for (;;) {
		ValleyViewDevice& device = *client.device;
		LockQueue(device);
		request.retiredFence = client.queueState.lastRetiredFence;
		status_t fenceStatus;
		if (FindFenceLocked(client, request.fence, request.result,
				fenceStatus)) {
			request.status = fenceStatus;
			UnlockQueue(device);
			return B_OK;
		}
		if (request.fence >= client.queueState.nextFence) {
			request.status = B_BAD_VALUE;
			UnlockQueue(device);
			return B_BAD_VALUE;
		}
		UnlockQueue(device);

		if (request.timeoutUs == 0)
			return B_WOULD_BLOCK;
		status_t status;
		if (request.timeoutUs < 0) {
			status = acquire_sem_etc(client.completionSem, 1,
				B_CAN_INTERRUPT, 0);
		} else {
			const bigtime_t remaining = deadline - system_time();
			if (remaining <= 0)
				return B_TIMED_OUT;
			status = acquire_sem_etc(client.completionSem, 1,
				B_CAN_INTERRUPT | B_RELATIVE_TIMEOUT, remaining);
		}
		if (status != B_OK)
			return status;
	}
}


status_t
DequeueRenderCompletion(ValleyViewClient& client,
	valleyview::RenderQueueCompletion& request)
{
	if (request.flags != 0)
		return B_BAD_VALUE;
	ValleyViewDevice& device = *client.device;
	LockQueue(device);
	ValleyViewRenderCompletion* completion = client.completionHead;
	if (completion == NULL) {
		UnlockQueue(device);
		return B_WOULD_BLOCK;
	}
	client.completionHead = completion->next;
	if (client.completionHead == NULL)
		client.completionTail = NULL;
	client.completionCount--;
	request = completion->record;
	UnlockQueue(device);
	free(completion);
	acquire_sem_etc(client.completionSem, 1, B_RELATIVE_TIMEOUT, 0);
	return B_OK;
}


status_t
GetRenderQueueInfo(ValleyViewClient& client, valleyview::RenderQueueInfo& info)
{
	const valleyview::RenderAbiHeader header = info.header;
	memset(&info, 0, sizeof(info));
	info.header = header;
	ValleyViewDevice& device = *client.device;
	LockQueue(device);
	info.status = device.renderQueueReady ? B_OK : B_NOT_SUPPORTED;
	info.mode = client.queueMode;
	info.supportedModes = 1u << valleyview::kRenderQueueModeSafe;
	if (device.renderQueueReady)
		info.supportedModes
			|= 1u << valleyview::kRenderQueueModeAsynchronous;
	if (device.renderQueueReady && device.nativeActive && device.bcsReady
		&& !device.gpuFaulted && !device.p0MemoryQuarantined
		&& !device.renderMemoryQuarantined) {
		info.supportedModes
			|= 1u << valleyview::kRenderQueueModeDirectPresent;
	}
	info.queuedJobs = client.queueState.queuedJobs
		+ (client.activeJob != NULL ? 1 : 0);
	info.completionCount = client.completionCount;
	info.queueHighWater = client.queueHighWater;
	info.deviceQueuedJobs = device.renderQueuedJobs;
	info.nextFence = client.queueState.nextFence;
	info.lastStartedFence = client.queueState.lastStartedFence;
	info.lastRetiredFence = client.queueState.lastRetiredFence;
	info.lastFailedFence = client.lastFailedFence;
	info.submittedJobs = client.submittedJobs;
	info.completedJobs = client.completedJobs;
	info.failedJobs = client.failedJobs;
	info.cancelledJobs = client.cancelledJobs;
	info.noResetJobs = client.noResetJobs;
	info.directPresents = device.renderDirectPresents;
	info.directPresentFailures = device.renderDirectPresentFailures;
	info.directPresentsQueued = device.renderDirectPresentsQueued;
	info.directPresentsDropped = device.renderDirectPresentsDropped;
	info.totalQueueLatencyUs = client.totalQueueLatencyUs;
	info.maxQueueLatencyUs = client.maxQueueLatencyUs;
	info.totalExecutionUs = client.totalExecutionUs;
	info.maxExecutionUs = client.maxExecutionUs;
	UnlockQueue(device);
	return info.status;
}


status_t
SelectRenderQueue(ValleyViewClient& client, uint8 event, selectsync* sync)
{
	if (event != B_SELECT_READ && event != B_SELECT_WRITE
		&& event != B_SELECT_ERROR) {
		return B_BAD_VALUE;
	}
	ValleyViewDevice& device = *client.device;
	LockQueue(device);
	status_t status = add_select_sync_pool_entry(&client.selectPool, sync,
		event);
	if (status == B_OK) {
		if ((event == B_SELECT_READ && client.completionHead != NULL)
			|| (event == B_SELECT_ERROR && client.queueLost)
			|| (event == B_SELECT_WRITE && !client.closing
				&& !client.queueLost
				&& client.queueMode != valleyview::kRenderQueueModeSafe
				&& valleyview::CanQueueRenderJob(client.queueState,
					device.renderQueuedJobs))) {
			notify_select_event(sync, event);
		}
	}
	UnlockQueue(device);
	return status;
}


status_t
DeselectRenderQueue(ValleyViewClient& client, uint8 event, selectsync* sync)
{
	if (event != B_SELECT_READ && event != B_SELECT_WRITE
		&& event != B_SELECT_ERROR) {
		return B_BAD_VALUE;
	}
	ValleyViewDevice& device = *client.device;
	LockQueue(device);
	status_t status = remove_select_sync_pool_entry(&client.selectPool, sync,
		event);
	UnlockQueue(device);
	return status;
}
