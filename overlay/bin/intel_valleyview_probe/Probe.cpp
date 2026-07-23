// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include <common/intel_valleyview/CrocusTriangleCore.h>
#include <common/intel_valleyview/FirmwareState.h>
#include <common/intel_valleyview/P0Core.h>
#include <common/intel_valleyview/PpgttCore.h>
#include <common/intel_valleyview/Protocol.h>
#include <common/intel_valleyview/RenderMemoryCore.h>
#include <common/intel_valleyview/RenderProtocol.h>

#include <Accelerant.h>
#include <OS.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>


namespace {

constexpr const char* kDevicePath = "/dev/misc/intel_valleyview_probe";


const char*
YesNo(bool value)
{
	return value ? "yes" : "no";
}


void
PrintSnapshot(const valleyview::FirmwareSnapshot& snapshot)
{
	printf("capture generation=%" B_PRIu32 " mmio_status=%" B_PRId32
		" opregion_status=%" B_PRId32 "\n", snapshot.generation,
		snapshot.mmioStatus, snapshot.opRegionStatus);
	printf("mmio physical=%#" B_PRIx64 " size=%#" B_PRIx64
		" mapped=%s\n", snapshot.mmioPhysical, snapshot.mmioSize,
		YesNo((snapshot.flags & valleyview::kSnapshotMmioMapped) != 0));
	printf("opregion asls=%#" B_PRIx32 " valid=%s version=%u.%u.%u"
		" size_kib=%u mboxes=%#" B_PRIx32 " vbt=%s source=%u"
		" address=%#" B_PRIx64 " size=%" B_PRIu32 "\n", snapshot.asls,
		YesNo((snapshot.flags & valleyview::kSnapshotOpRegionValid) != 0),
		snapshot.opRegionMajor, snapshot.opRegionMinor,
		snapshot.opRegionRevision,
		snapshot.opRegionSizeKiB, snapshot.opRegionMboxes,
		YesNo((snapshot.flags & valleyview::kSnapshotVbtPresent) != 0),
		snapshot.vbtSource, snapshot.vbtAddress, snapshot.vbtSize);
	printf("mode display=%ux%u total=%ux%u source=%ux%u stride=%" B_PRIu32
		"\n", snapshot.hDisplay, snapshot.vDisplay, snapshot.hTotalPixels,
		snapshot.vTotalLines, snapshot.sourceWidth, snapshot.sourceHeight,
		snapshot.planeStride);
	printf("dpll_a=%#08" B_PRIx32 " enabled=%s locked=%s\n",
		snapshot.dpllA,
		YesNo((snapshot.flags & valleyview::kSnapshotDpllEnabled) != 0),
		YesNo((snapshot.flags & valleyview::kSnapshotDpllLocked) != 0));
	printf("pipe_a=%#08" B_PRIx32 " enabled=%s htotal=%#08" B_PRIx32
		" hblank=%#08" B_PRIx32 " hsync=%#08" B_PRIx32 "\n",
		snapshot.pipeConfig,
		YesNo((snapshot.flags & valleyview::kSnapshotPipeEnabled) != 0),
		snapshot.hTotal, snapshot.hBlank, snapshot.hSync);
	printf("vertical vtotal=%#08" B_PRIx32 " vblank=%#08" B_PRIx32
		" vsync=%#08" B_PRIx32 "\n", snapshot.vTotal, snapshot.vBlank,
		snapshot.vSync);
	printf("plane_a=%#08" B_PRIx32 " enabled=%s surface=%#08" B_PRIx32
		" live=%#08" B_PRIx32 " addr=%#08" B_PRIx32
		" linoff=%#08" B_PRIx32 " tileoff=%#08" B_PRIx32 "\n",
		snapshot.planeControl,
		YesNo((snapshot.flags & valleyview::kSnapshotPlaneEnabled) != 0),
		snapshot.planeSurface, snapshot.planeSurfaceLive,
		snapshot.planeAddressVlv, snapshot.planeLinearOffset,
		snapshot.planeTileOffset);
	printf("scanout gmadr=%#" B_PRIx64 " size=%#" B_PRIx64
		" ggtt_offset=%#08" B_PRIx32 " aperture=%#" B_PRIx64
		" pte=%#08" B_PRIx32 " backing=%#" B_PRIx64
		" pages=%" B_PRIu32 "/%" B_PRIu32
		" present=%s matches_boot=%s\n",
		snapshot.gmadrBase, snapshot.gmadrSize, snapshot.planeGgttOffset,
		snapshot.scanoutAperture, snapshot.gttPte,
		snapshot.scanoutPhysical, snapshot.gttPresentPages,
		snapshot.gttRequiredPages,
		YesNo((snapshot.flags & valleyview::kSnapshotGttRangePresent) != 0),
		YesNo((snapshot.flags & valleyview::kSnapshotScanoutMatchesBoot) != 0));
	printf("panel_fitter control=%#08" B_PRIx32 " enabled=%s programmed=%#08"
		B_PRIx32 " auto=%#08" B_PRIx32 "\n", snapshot.panelFitterControl,
		YesNo((snapshot.flags & valleyview::kSnapshotPanelFitterEnabled) != 0),
		snapshot.panelFitterProgrammedRatios,
		snapshot.panelFitterAutoRatios);
	printf("dp_c=%#08" B_PRIx32 " enabled=%s pipe=%u\n", snapshot.dpC,
		YesNo((snapshot.flags & valleyview::kSnapshotPortEnabled) != 0),
		snapshot.dpPipe);
	printf("pps status=%#08" B_PRIx32 " control=%#08" B_PRIx32
		" on=%s ready=%s port=%u on_delays=%#08" B_PRIx32
		" off_delays=%#08" B_PRIx32 " divisor=%#08" B_PRIx32 "\n",
		snapshot.ppsStatus, snapshot.ppsControl,
		YesNo((snapshot.flags & valleyview::kSnapshotPpsOn) != 0),
		YesNo((snapshot.flags & valleyview::kSnapshotPpsReady) != 0),
		snapshot.ppsPort, snapshot.ppsOnDelays, snapshot.ppsOffDelays,
		snapshot.ppsDivisor);
	printf("pwm control2=%#08" B_PRIx32 " enabled=%s duty=%u period=%u\n",
		snapshot.pwmControl2,
		YesNo((snapshot.flags & valleyview::kSnapshotPwmEnabled) != 0),
		snapshot.pwmDuty, snapshot.pwmPeriod);
	printf("cursor control=%#08" B_PRIx32 " enabled=%s base=%#08" B_PRIx32
		" position=%#08" B_PRIx32 " live=%#08" B_PRIx32 "\n",
		snapshot.cursorControl,
		YesNo((snapshot.flags & valleyview::kSnapshotCursorEnabled) != 0),
		snapshot.cursorBase, snapshot.cursorPosition,
		snapshot.cursorSurfaceLive);
	printf("boot_framebuffer status=%" B_PRId32 " physical=%#" B_PRIx64
		" size=%#" B_PRIx64 " area=%" B_PRId32 " mode=%" B_PRIu32
		"x%" B_PRIu32 "x%" B_PRIu32 " stride=%" B_PRIu32
		" compatible=%s adoption_status=%" B_PRId32 "\n",
		snapshot.bootFramebufferStatus, snapshot.bootFramebufferPhysical,
		snapshot.bootFramebufferSize, snapshot.bootFramebufferArea,
		snapshot.bootWidth, snapshot.bootHeight, snapshot.bootDepth,
		snapshot.bootBytesPerRow,
		YesNo((snapshot.flags & valleyview::kSnapshotAdoptionCompatible) != 0),
		snapshot.adoptionStatus);
}


void
PrintGpuRegisterSnapshot(const char* label,
	const valleyview::GpuRegisterSnapshot& snapshot)
{
	printf("gpu_%s wake=%#08" B_PRIx32 "/%#08" B_PRIx32
		" forcewake=%#08" B_PRIx32 "/%#08" B_PRIx32
		" media=%#08" B_PRIx32 "/%#08" B_PRIx32
		" fifo=%#08" B_PRIx32 " debug=%#08" B_PRIx32
		" thread=%#08" B_PRIx32 " c0=%" B_PRIu32 "/%" B_PRIu32
		" reset=%#08" B_PRIx32 "\n",
		label, snapshot.gtlcWakeControl, snapshot.gtlcPowerStatus,
		snapshot.forcewakeRender, snapshot.forcewakeAckRender,
		snapshot.forcewakeMedia, snapshot.forcewakeAckMedia,
		snapshot.gtFifoControl, snapshot.gtFifoDebug,
		snapshot.gtThreadStatus, snapshot.renderC0Count,
		snapshot.mediaC0Count, snapshot.gdrst);
	printf("bcs_%s tail=%#08" B_PRIx32 " head=%#08" B_PRIx32
		" start=%#08" B_PRIx32 " control=%#08" B_PRIx32
		" hws=%#08" B_PRIx32 " mi_mode=%#08" B_PRIx32
		" mode=%#08" B_PRIx32
		" acthd=%#08" B_PRIx32 " ipehr=%#08" B_PRIx32
		" ipeir=%#08" B_PRIx32 " instdone=%#08" B_PRIx32 "\n",
		label, snapshot.bcsTail, snapshot.bcsHead, snapshot.bcsStart,
		snapshot.bcsControl, snapshot.bcsHws, snapshot.bcsMiMode,
		snapshot.bcsMode,
		snapshot.bcsActhd, snapshot.bcsIpehr, snapshot.bcsIpeir,
		snapshot.bcsInstdone);
}


void
PrintGpuDiagnostics(const valleyview::GpuDiagnostics& diagnostics)
{
	printf("gpu_test generation=%" B_PRIu32 " command=%#08" B_PRIx32
		" status=%" B_PRId32 " stage=%u flags=%#08" B_PRIx32
		" elapsed_us=%" B_PRIu32 "\n", diagnostics.generation,
		diagnostics.command, diagnostics.status, diagnostics.stage,
		diagnostics.flags, diagnostics.elapsedUs);
	printf("gpu_memory physical=%#" B_PRIx64 " ggtt_offset=%#08" B_PRIx32
		" ring_tail=%" B_PRIu32 "\n", diagnostics.testPhysical,
		diagnostics.ggttOffset, diagnostics.ringTailBytes);
	printf("gpu_ptes before=%#08" B_PRIx32 ",%#08" B_PRIx32
		",%#08" B_PRIx32 ",%#08" B_PRIx32
		" test=%#08" B_PRIx32 ",%#08" B_PRIx32 ",%#08" B_PRIx32
		",%#08" B_PRIx32 " after=%#08" B_PRIx32 ",%#08" B_PRIx32
		",%#08" B_PRIx32 ",%#08" B_PRIx32 "\n",
		diagnostics.pteBefore[0], diagnostics.pteBefore[1],
		diagnostics.pteBefore[2], diagnostics.pteBefore[3],
		diagnostics.pteTest[0], diagnostics.pteTest[1],
		diagnostics.pteTest[2], diagnostics.pteTest[3],
		diagnostics.pteAfter[0], diagnostics.pteAfter[1],
		diagnostics.pteAfter[2], diagnostics.pteAfter[3]);
	printf("gpu_verify pattern=%#08" B_PRIx32 " marker=%#08" B_PRIx32
		" source_mismatch=%#" B_PRIx32 "/%#08" B_PRIx32
		" destination_mismatch=%#" B_PRIx32 "/%#08" B_PRIx32
		" display=%#" B_PRIx64 "/%#" B_PRIx64 "\n",
		diagnostics.expectedPattern, diagnostics.completionMarker,
		diagnostics.sourceMismatchOffset, diagnostics.sourceObserved,
		diagnostics.destinationMismatchOffset,
		diagnostics.destinationObserved,
		diagnostics.displaySignatureBefore,
		diagnostics.displaySignatureAfter);
	PrintGpuRegisterSnapshot("before", diagnostics.before);
	PrintGpuRegisterSnapshot("active", diagnostics.active);
	PrintGpuRegisterSnapshot("after", diagnostics.after);
}


void
PrintP0Status(const valleyview::P0Status& status)
{
	printf("p0 flags=%#08" B_PRIx32 " native_status=%" B_PRId32
		" bcs_status=%" B_PRId32 " present_status=%" B_PRId32
		"/%" B_PRId32 " mode=%" B_PRIu32 "x%" B_PRIu32
		" stride=%" B_PRIu32 " dpms=%#08" B_PRIx32 "\n",
		status.flags, status.nativeStatus, status.bcsStatus,
		status.presentStatus, status.presentBcsStatus, status.width,
		status.height, status.bytesPerRow, status.dpmsMode);
	printf("p0_memory render=%#" B_PRIx64 "/%#08" B_PRIx32
		" scanout=%#" B_PRIx64 "/%#08" B_PRIx32 ",%#" B_PRIx64
		"/%#08" B_PRIx32 " ggtt=%#08" B_PRIx32 " pages=%" B_PRIu32
		"\n", status.physical, status.framebufferOffset,
		status.scanoutPhysical[0], status.scanoutOffset[0],
		status.scanoutPhysical[1], status.scanoutOffset[1],
		status.ggttOffset, status.ggttPages);
	printf("p0_private cursor=%#08" B_PRIx32 " ring=%#08" B_PRIx32
		" status=%#08" B_PRIx32 "\n", status.cursorOffset,
		status.ringOffset, status.statusOffset);
	printf("p0_pwm duty=%" B_PRIu32 " period=%" B_PRIu32
		" bcs_submissions=%" B_PRIu64 " failures=%" B_PRIu64 "\n",
		status.pwmDuty, status.pwmPeriod, status.bcsSubmissions,
		status.bcsFailures);
	printf("p0_live pipe_source=%#08" B_PRIx32 " plane=%#08" B_PRIx32
		" stride=%" B_PRIu32 " surface=%#08" B_PRIx32
		" live=%#08" B_PRIx32 "\n", status.pipeSource,
		status.planeControl, status.planeStride, status.planeSurface,
		status.planeSurfaceLive);
	printf("p0_pfit control=%#08" B_PRIx32 " programmed=%#08" B_PRIx32
		" auto=%#08" B_PRIx32 "\n", status.panelFitterControl,
		status.panelFitterProgrammedRatios,
		status.panelFitterAutoRatios);
	printf("p0_cursor control=%#08" B_PRIx32 " base=%#08" B_PRIx32
		" position=%#08" B_PRIx32 " live=%#08" B_PRIx32
		" visible=%s shape=%" B_PRIu64 " bitmap=%" B_PRIu64
		" move=%" B_PRIu64 " show=%" B_PRIu64 "\n",
		status.cursorControl, status.cursorBase, status.cursorPosition,
		status.cursorSurfaceLive, YesNo(status.cursorVisible != 0),
		status.cursorShapeUpdates, status.cursorBitmapUpdates,
		status.cursorMoveUpdates, status.cursorShowUpdates);
	printf("p0_requests fill=%" B_PRIu64 " blit=%" B_PRIu64
		" present=%" B_PRIu64 " cpu_fill=%" B_PRIu64
		" cpu_blit=%" B_PRIu64 "\n", status.bcsFillRequests,
		status.bcsBlitRequests, status.bcsPresentRequests,
		status.cpuFillFallbacks, status.cpuBlitFallbacks);
	printf("p0_present active=%" B_PRId32 " pending=%" B_PRId32
		" frames=%" B_PRIu64 " failures=%" B_PRIu64
		" copies=%" B_PRIu64 "/%" B_PRIu64
		" copy_us=%" B_PRIu64 "/%" B_PRIu64
		" flip_us=%" B_PRIu64 "/%" B_PRIu64 "\n",
		status.activeScanout, status.pendingScanout, status.presentFrames,
		status.presentFailures, status.presentBcsCopies,
		status.presentCpuCopies, status.presentCopyLastUs,
		status.presentCopyMaxUs, status.presentFlipLastUs,
		status.presentFlipMaxUs);
}


void
PrintRenderDeviceInfo(const valleyview::RenderDeviceInfo& info)
{
	printf("render status=%" B_PRId32 " ready=%s generation=%u"
		" address_bits=%u device=%04x:%04x revision=%u\n",
		info.status, YesNo(valleyview::IsRenderReady(info)),
		info.graphicsGeneration, info.gpuAddressBits, info.vendorId,
		info.deviceId, info.revision);
	printf("render_memory aperture=%#" B_PRIx64 "/%#" B_PRIx64
		" display_reserved=%#" B_PRIx64 "/%#" B_PRIx64
		" page_size=%" B_PRIu32 " flags=%#08" B_PRIx32 "\n",
		info.apertureBase, info.apertureSize, info.displayReservedOffset,
		info.displayReservedSize, info.pageSize, info.deviceFlags);
	printf("render_contract capabilities=%#016" B_PRIx64
		" required=%#016" B_PRIx64 " proven_engines=%#08" B_PRIx32
		" submission_engines=%#08" B_PRIx32 "\n",
		info.capabilities, valleyview::kRenderRequiredCapabilities,
		info.provenEngines, info.submissionEngines);
}


void
PrintRcsRegisterSnapshot(const char* label,
	const valleyview::RcsRegisterSnapshot& snapshot)
{
	printf("rcs_%s tail=%#08" B_PRIx32 " head=%#08" B_PRIx32
		" start=%#08" B_PRIx32 " control=%#08" B_PRIx32
		" hws=%#08" B_PRIx32 " mi_mode=%#08" B_PRIx32
		" mode=%#08" B_PRIx32 " instpm=%#08" B_PRIx32 "\n",
		label, snapshot.tail, snapshot.head, snapshot.start, snapshot.control,
		snapshot.hws, snapshot.miMode, snapshot.mode, snapshot.instpm);
	printf("rcs_state_%s acthd=%#08" B_PRIx32 " ipehr=%#08" B_PRIx32
		" ipeir=%#08" B_PRIx32 " instdone=%#08" B_PRIx32
		" bbstate=%#08" B_PRIx32 " bbaddr=%#08" B_PRIx32
		" timestamp=%#08" B_PRIx32 " fault=%#08" B_PRIx32 "\n",
		label, snapshot.acthd, snapshot.ipehr, snapshot.ipeir,
		snapshot.instdone, snapshot.bbstate, snapshot.bbaddr,
		snapshot.timestamp, snapshot.faultRegister);
	printf("rcs_context_%s control=%#08" B_PRIx32 " status=%#08" B_PRIx32
		" ccid=%#08" B_PRIx32 " pp_dir=%#08" B_PRIx32 "/%#08"
		B_PRIx32 "\n",
		label, snapshot.contextControl, snapshot.contextStatus,
		snapshot.ccid, snapshot.ppDirDclv, snapshot.ppDirBase);
	printf("rcs_fault_%s valid=%s space=%s address=%#08" B_PRIx32
		" source=%" B_PRIu32 " type=%" B_PRIu32 "\n",
		label, YesNo(valleyview::RcsFaultIsValid(snapshot.faultRegister)),
		(snapshot.faultRegister & valleyview::kRcsFaultGgtt) != 0
			? "ggtt" : "ppgtt",
		valleyview::RcsFaultAddress(snapshot.faultRegister),
		valleyview::RcsFaultSource(snapshot.faultRegister),
		valleyview::RcsFaultType(snapshot.faultRegister));
}


void
PrintRcsShaderPtes(const valleyview::RcsDiagnostic& diagnostics)
{
	for (uint32 page = 0; page < valleyview::kRcsShaderTotalPages; page++) {
		printf("rcs_shader_pte page=%" B_PRIu32 " before=%#08" B_PRIx32
			" bound=%#08" B_PRIx32 " after=%#08" B_PRIx32 "\n",
			page, diagnostics.shaderPteBefore[page],
			diagnostics.shaderPteBound[page],
			diagnostics.shaderPteAfter[page]);
	}
}


void
PrintRcsDiagnostic(const valleyview::RcsDiagnostic& diagnostics)
{
	printf("rcs_test status=%" B_PRId32 " stage=%u flags=%#08" B_PRIx32
		" elapsed_us=%" B_PRIu64 " cleanup=%" B_PRId32 "/%" B_PRId32
		"/%" B_PRId32 "/%" B_PRId32 "/%" B_PRId32 "\n",
		diagnostics.status, diagnostics.stage, diagnostics.flags,
		diagnostics.elapsedUs, diagnostics.resetStatus,
		diagnostics.ringRestoreStatus, diagnostics.ggttRestoreStatus,
		diagnostics.forcewakeReleaseStatus,
		diagnostics.wakeRestoreStatus);
	printf("rcs_memory ring=%#08" B_PRIx32 " status=%#08" B_PRIx32
		" batch=%#08" B_PRIx32 "/%" B_PRIu32
		" result=%#08" B_PRIx32 " tail=%" B_PRIu32 "\n",
		diagnostics.ringOffset, diagnostics.statusOffset,
		diagnostics.batchOffset, diagnostics.batchBytes,
		diagnostics.resultOffset, diagnostics.ringTailBytes);
	printf("rcs_markers batch=%#08" B_PRIx32 "/%#08" B_PRIx32
		" completion=%#08" B_PRIx32 "/%#08" B_PRIx32
		" timestamp=%#08" B_PRIx32 "/%#08" B_PRIx32 "/%#08"
		B_PRIx32 "\n",
		diagnostics.batchMarker, diagnostics.observedBatchMarker,
		diagnostics.completionMarker,
		diagnostics.observedCompletionMarker,
		diagnostics.timestampBefore, diagnostics.observedTimestamp,
		diagnostics.timestampAfter);
	printf("rcs_counts tests=%" B_PRIu64 " failures=%" B_PRIu64
		" resets=%" B_PRIu64 " display=%#" B_PRIx64 "/%#" B_PRIx64 "\n",
		diagnostics.testCount, diagnostics.failureCount,
		diagnostics.resetCount, diagnostics.displaySignatureBefore,
		diagnostics.displaySignatureAfter);
	printf("rcs_ptes before=%#08" B_PRIx32 ",%#08" B_PRIx32
		",%#08" B_PRIx32 ",%#08" B_PRIx32
		" bound=%#08" B_PRIx32 ",%#08" B_PRIx32
		",%#08" B_PRIx32 ",%#08" B_PRIx32
		" after=%#08" B_PRIx32 ",%#08" B_PRIx32
		",%#08" B_PRIx32 ",%#08" B_PRIx32 "\n",
		diagnostics.pteBefore[0], diagnostics.pteBefore[1],
		diagnostics.pteBefore[2], diagnostics.pteBefore[3],
		diagnostics.pteBound[0], diagnostics.pteBound[1],
		diagnostics.pteBound[2], diagnostics.pteBound[3],
		diagnostics.pteAfter[0], diagnostics.pteAfter[1],
		diagnostics.pteAfter[2], diagnostics.pteAfter[3]);
	printf("rcs_shader status=%" B_PRId32 " stage=%u offset=%#08"
		B_PRIx32 " pages=%" B_PRIu32 " command=%" B_PRIu32
		" marker=%#08" B_PRIx32 "\n",
		diagnostics.shaderStatus, diagnostics.shaderStage,
		diagnostics.shaderOffset, diagnostics.shaderPages,
		diagnostics.shaderCommandBytes, diagnostics.shaderCompletionMarker);
	printf("rcs_shader_layout kernel=%#08" B_PRIx32
		" surface_state=%#08" B_PRIx32 " binding=%#08" B_PRIx32
		" descriptor=%#08" B_PRIx32 " surface=%#08" B_PRIx32
		"/%" B_PRIu32 " guard=%#08" B_PRIx32 "/%" B_PRIu32 "\n",
		diagnostics.shaderKernelOffset, diagnostics.shaderSurfaceStateOffset,
		diagnostics.shaderBindingTableOffset, diagnostics.shaderDescriptorOffset,
		diagnostics.shaderSurfaceOffset, diagnostics.shaderSurfaceBytes,
		diagnostics.shaderGuardOffset, diagnostics.shaderGuardBytes);
	printf("rcs_shader_output zero=%" B_PRIu32 " sentinel=%" B_PRIu32
		" unexpected=%" B_PRIu32 " changed=%#08" B_PRIx32 "/%#08"
		B_PRIx32 " first_bad=%#08" B_PRIx32 "/%#08" B_PRIx32
		" guard=%#08" B_PRIx32 "/%#08" B_PRIx32 "\n",
		diagnostics.shaderZeroDwords, diagnostics.shaderSentinelDwords,
		diagnostics.shaderUnexpectedDwords,
		diagnostics.shaderFirstChangedOffset, diagnostics.shaderLastChangedOffset,
		diagnostics.shaderFirstUnexpectedOffset,
		diagnostics.shaderFirstUnexpectedValue,
		diagnostics.shaderGuardMismatchOffset, diagnostics.shaderGuardObserved);
	printf("rcs_shader_checksum before=%#016" B_PRIx64
		" after=%#016" B_PRIx64 " cache_mode0=%#08" B_PRIx32
		"/%#08" B_PRIx32 " cache_mode1=%#08" B_PRIx32 "/%#08"
		B_PRIx32 "\n",
		diagnostics.shaderChecksumBefore, diagnostics.shaderChecksumAfter,
		diagnostics.cacheMode0Before, diagnostics.cacheMode0After,
		diagnostics.cacheMode1Before, diagnostics.cacheMode1After);
	PrintRcsShaderPtes(diagnostics);
	PrintGpuRegisterSnapshot("rcs_global_before", diagnostics.globalBefore);
	PrintRcsRegisterSnapshot("before", diagnostics.before);
	PrintRcsRegisterSnapshot("active", diagnostics.active);
	if ((diagnostics.flags & valleyview::kRcsFaultCaptured) != 0) {
		PrintGpuRegisterSnapshot("rcs_global_fault", diagnostics.globalFault);
		PrintRcsRegisterSnapshot("fault", diagnostics.fault);
	}
	PrintRcsRegisterSnapshot("after", diagnostics.after);
	PrintGpuRegisterSnapshot("rcs_global_after", diagnostics.globalAfter);
}


status_t
ReadRenderDeviceInfo(int device, valleyview::RenderDeviceInfo& info)
{
	memset(&info, 0, sizeof(info));
	status_t status = ioctl(device, valleyview::kGetRenderDeviceInfo, &info,
		sizeof(info));
	if (status != B_OK)
		return status;
	return valleyview::IsValidRenderAbiHeader(info.header, sizeof(info))
		? B_OK : B_BAD_DATA;
}


status_t
CreateRenderContextProbe(int device, valleyview::RenderContextCreate& context)
{
	memset(&context, 0, sizeof(context));
	context.header = valleyview::MakeRenderAbiHeader(sizeof(context));
	status_t status = ioctl(device, valleyview::kRenderCreateContext,
		&context, sizeof(context));
	if (!valleyview::IsValidRenderAbiHeader(context.header, sizeof(context)))
		return status == B_OK ? B_BAD_DATA : status;
	printf("render_context create_status=%" B_PRId32 " handle=%" B_PRIu32
		" address_bits=%" B_PRIu32 " address_space=%#" B_PRIx64
		" page_size=%" B_PRIu32 " pp_dir=%#08" B_PRIx32 "\n",
		context.status, context.handle, context.addressBits,
		context.addressSpaceSize, context.pageSize, context.ppDirBase);
	if (status != B_OK)
		return status;
	if (context.status != B_OK)
		return context.status;
	if (context.handle == 0
		|| context.addressBits != valleyview::kPpgttAddressBits
		|| context.addressSpaceSize != valleyview::kPpgttVirtualAddressBytes
		|| context.pageSize != valleyview::kPpgttPageBytes
		|| (context.ppDirBase
			& (valleyview::kPpgttDirectoryAlignment - 1)) != 0) {
		return B_BAD_DATA;
	}
	return B_OK;
}


status_t
RejectDuplicateRenderContextProbe(int device,
	const valleyview::RenderContextCreate& active)
{
	valleyview::RenderContextCreate duplicate = {};
	duplicate.header = valleyview::MakeRenderAbiHeader(sizeof(duplicate));
	status_t status = ioctl(device, valleyview::kRenderCreateContext,
		&duplicate, sizeof(duplicate));
	printf("render_context duplicate_status=%" B_PRId32
		" handle=%" B_PRIu32 "\n", duplicate.status, active.handle);
	if (status != B_BUSY || duplicate.status != B_BUSY
		|| duplicate.handle != 0) {
		return B_BAD_DATA;
	}
	return B_OK;
}


status_t
DestroyRenderContextProbe(int device,
	const valleyview::RenderContextCreate& context)
{
	if (context.handle == 0)
		return B_OK;
	valleyview::RenderContextDestroy request = {};
	request.header = valleyview::MakeRenderAbiHeader(sizeof(request));
	request.handle = context.handle;
	status_t status = ioctl(device, valleyview::kRenderDestroyContext,
		&request, sizeof(request));
	printf("render_context destroy_status=%" B_PRId32
		" handle=%" B_PRIu32 "\n", request.status, request.handle);
	if (!valleyview::IsValidRenderAbiHeader(request.header, sizeof(request)))
		return status == B_OK ? B_BAD_DATA : status;
	return status == B_OK ? request.status : status;
}


status_t
RunRcsProbe(int device)
{
	valleyview::RcsDiagnostic diagnostics = {};
	diagnostics.header = valleyview::MakeRenderAbiHeader(sizeof(diagnostics));
	diagnostics.command = valleyview::kRcsDiagnosticArm;
	status_t status = ioctl(device, valleyview::kRunRcsDiagnostic,
		&diagnostics, sizeof(diagnostics));
	if (!valleyview::IsValidRenderAbiHeader(diagnostics.header,
			sizeof(diagnostics))) {
		return status == B_OK ? B_BAD_DATA : status;
	}
	PrintRcsDiagnostic(diagnostics);
	return status == B_OK ? diagnostics.status : status;
}


status_t CloseRenderBuffer(int device, uint32 handle);


void
PrintRenderSubmit(const valleyview::RenderSubmit& submit)
{
	printf("render_submit status=%" B_PRId32 " stage=%u flags=%#08"
		B_PRIx32 " context=%" B_PRIu32 " batch=%" B_PRIu32
		"/%#" B_PRIx32 "+%" B_PRIu32 " objects=%" B_PRIu32
		" sequence=%" B_PRIu32 " workspace=%#08" B_PRIx32 "/%"
		B_PRIu32 " elapsed_us=%" B_PRIu64 "\n",
		submit.status, submit.stage, submit.diagnosticFlags,
		submit.contextHandle, submit.batchHandle, submit.batchOffset,
		submit.batchLength, submit.objectCount, submit.sequence,
		submit.workspaceOffset, submit.workspacePages, submit.elapsedUs);
	printf("render_submit_parser reason=%u fail=%#08" B_PRIx32 "/%#08"
		B_PRIx32 " commands=%" B_PRIu32 " lri=%" B_PRIu32
		" pipe_control=%" B_PRIu32 " primitive=%" B_PRIu32 "\n",
		submit.parserReason, submit.parserFailingOffset,
		submit.parserFailingDword, submit.parsedCommandCount,
		submit.lriRegisterCount, submit.pipeControlCount,
		submit.primitiveCount);
	printf("render_submit_completion marker=%#08" B_PRIx32 "/%#08"
		B_PRIx32 " ring_tail=%" B_PRIu32 " reset=%" B_PRId32
		" ring_restore=%" B_PRId32 " cache_restore=%" B_PRId32
		" forcewake_release=%" B_PRId32 " wake_restore=%" B_PRId32 "\n",
		submit.completionMarker, submit.observedCompletionMarker,
		submit.ringTailBytes, submit.resetStatus, submit.ringRestoreStatus,
		submit.cacheRestoreStatus, submit.forcewakeReleaseStatus,
		submit.wakeRestoreStatus);
	printf("render_submit_l3 before=%#08" B_PRIx32 "/%#08" B_PRIx32
		"/%#08" B_PRIx32 " after=%#08" B_PRIx32 "/%#08" B_PRIx32
		"/%#08" B_PRIx32 "\n",
		submit.l3Before[0], submit.l3Before[1], submit.l3Before[2],
		submit.l3After[0], submit.l3After[1], submit.l3After[2]);
	PrintGpuRegisterSnapshot("render_submit_global_before",
		submit.globalBefore);
	PrintRcsRegisterSnapshot("render_submit_before", submit.before);
	PrintRcsRegisterSnapshot("render_submit_active", submit.active);
	if ((submit.diagnosticFlags
			& valleyview::kRenderSubmitFaultCaptured) != 0) {
		PrintGpuRegisterSnapshot("render_submit_global_fault",
			submit.globalFault);
		PrintRcsRegisterSnapshot("render_submit_fault", submit.fault);
	}
	PrintRcsRegisterSnapshot("render_submit_after", submit.after);
	PrintGpuRegisterSnapshot("render_submit_global_after",
		submit.globalAfter);
}


status_t
RunRcsSubmissionProbe(int device,
	const valleyview::RenderContextCreate& context)
{
	valleyview::RenderBufferCreate batch = {};
	batch.header = valleyview::MakeRenderAbiHeader(sizeof(batch));
	batch.requestedSize = valleyview::kPageSize;
	batch.flags = valleyview::kRenderBufferCpuCached;
	status_t status = ioctl(device, valleyview::kRenderCreateBuffer, &batch,
		sizeof(batch));
	if (status != B_OK)
		return status;

	valleyview::RenderBufferMap mapping = {};
	mapping.header = valleyview::MakeRenderAbiHeader(sizeof(mapping));
	mapping.handle = batch.handle;
	status = ioctl(device, valleyview::kRenderMapBuffer, &mapping,
		sizeof(mapping));
	if (status != B_OK)
		goto cleanup;
	if (mapping.address == 0
		|| !valleyview::ValidatePpgttVaRange(batch.renderAddress, batch.size)) {
		status = B_BAD_DATA;
		goto cleanup;
	}

	{
		uint32* commands = reinterpret_cast<uint32*>(
			static_cast<addr_t>(mapping.address));
		commands[0] = valleyview::kMiBatchBufferEnd;
		__sync_synchronize();
	}

	{
		valleyview::RenderSubmit submit = {};
		submit.header = valleyview::MakeRenderAbiHeader(sizeof(submit));
		submit.contextHandle = context.handle;
		submit.batchHandle = batch.handle;
		submit.batchLength = sizeof(uint32);
		submit.objectCount = 1;
		submit.objectHandles[0] = batch.handle;
		status = ioctl(device, valleyview::kRenderSubmit, &submit,
			sizeof(submit));
		PrintRenderSubmit(submit);
		if (status == B_OK && submit.status != B_OK)
			status = submit.status;
	}

cleanup:
	{
		status_t closeStatus = CloseRenderBuffer(device, batch.handle);
		if (status == B_OK)
			status = closeStatus;
	}
	return status;
}


status_t
RunCrocusTriangleProbe(int device,
	const valleyview::RenderContextCreate& context)
{
	valleyview::RenderBufferCreate buffers[valleyview::kCrocusTriangleBoCount]
		= {};
	valleyview::RenderBufferMap mappings[valleyview::kCrocusTriangleBoCount]
		= {};
	void* addresses[valleyview::kCrocusTriangleBoCount] = {};
	uint32 renderAddresses[valleyview::kCrocusTriangleBoCount] = {};
	size_t byteCounts[valleyview::kCrocusTriangleBoCount] = {};
	status_t status = B_OK;

	for (uint32 index = 0;
			status == B_OK && index < valleyview::kCrocusTriangleBoCount;
			index++) {
		buffers[index].header
			= valleyview::MakeRenderAbiHeader(sizeof(buffers[index]));
		buffers[index].requestedSize
			= valleyview::kCrocusTriangleBoSizes[index];
		buffers[index].flags = valleyview::kRenderBufferCpuCached;
		status = ioctl(device, valleyview::kRenderCreateBuffer,
			&buffers[index], sizeof(buffers[index]));
		if (status != B_OK)
			break;
		if (buffers[index].renderAddress > UINT32_MAX
			|| !valleyview::ValidatePpgttVaRange(
				buffers[index].renderAddress, buffers[index].size)) {
			status = B_BAD_DATA;
			break;
		}

		mappings[index].header
			= valleyview::MakeRenderAbiHeader(sizeof(mappings[index]));
		mappings[index].handle = buffers[index].handle;
		mappings[index].area = -1;
		status = ioctl(device, valleyview::kRenderMapBuffer,
			&mappings[index], sizeof(mappings[index]));
		if (status != B_OK)
			break;
		addresses[index] = reinterpret_cast<void*>(
			static_cast<addr_t>(mappings[index].address));
		renderAddresses[index]
			= static_cast<uint32>(buffers[index].renderAddress);
		byteCounts[index] = mappings[index].size;
		if (!valleyview::InitializeCrocusTriangleBo(index, addresses[index],
				byteCounts[index])) {
			status = B_BAD_DATA;
		}
	}
	if (status == B_OK
		&& !valleyview::PatchCrocusTriangleRelocations(addresses,
			renderAddresses, byteCounts, valleyview::kCrocusTriangleBoCount)) {
		status = B_BAD_DATA;
	}
	__sync_synchronize();

	valleyview::RenderSubmit submit = {};
	if (status == B_OK) {
		submit.header = valleyview::MakeRenderAbiHeader(sizeof(submit));
		submit.contextHandle = context.handle;
		submit.batchHandle
			= buffers[valleyview::kCrocusTriangleCommandBo].handle;
		submit.batchLength = valleyview::kCrocusTriangleBatchBytes;
		submit.objectCount = valleyview::kCrocusTriangleBoCount;
		for (uint32 index = 0;
				index < valleyview::kCrocusTriangleBoCount; index++) {
			submit.objectHandles[index] = buffers[index].handle;
		}
		status = ioctl(device, valleyview::kRenderSubmit, &submit,
			sizeof(submit));
		PrintRenderSubmit(submit);
		if (status == B_OK && submit.status != B_OK)
			status = submit.status;
	}

	__sync_synchronize();
	valleyview::CrocusTriangleAnalysis analysis = {};
	const bool rasterValid = addresses[valleyview::kCrocusTriangleTargetBo]
			!= NULL
		&& valleyview::AnalyzeCrocusTriangle(
			static_cast<const uint32*>(
				addresses[valleyview::kCrocusTriangleTargetBo]),
			byteCounts[valleyview::kCrocusTriangleTargetBo], analysis);
	uint32 fenceValue = 0;
	if (addresses[valleyview::kCrocusTriangleFenceBo] != NULL) {
		fenceValue = *static_cast<const uint32*>(
			addresses[valleyview::kCrocusTriangleFenceBo]);
	}
	printf("crocus_triangle status=%" B_PRId32 " valid=%s"
		" target=%" B_PRIu32 "/%#08" B_PRIx32 "/%" B_PRIu32
		" visible_sentinel=%" B_PRIu32 " padding_bad=%" B_PRIu32
		" opaque=%" B_PRIu32 " clear=%" B_PRIu32
		" triangle=%" B_PRIu32 " rgb=%" B_PRIu32 "/%" B_PRIu32
		"/%" B_PRIu32 " geometry=%" B_PRIu32
		" interpolation=%" B_PRIu32 " fence=%#08" B_PRIx32
		" checksum=%#016" B_PRIx64 "\n",
		status, YesNo(rasterValid),
		buffers[valleyview::kCrocusTriangleTargetBo].handle,
		renderAddresses[valleyview::kCrocusTriangleTargetBo],
		valleyview::kCrocusTriangleTargetBytes,
		analysis.visibleSentinelPixels, analysis.paddingMismatchDwords,
		analysis.opaquePixels, analysis.clearPixels, analysis.trianglePixels,
		analysis.redPixels, analysis.greenPixels, analysis.bluePixels,
		analysis.coverageRowsMatched, analysis.interpolationSamplesMatched,
		fenceValue, analysis.checksum);
	if (status == B_OK && (!rasterValid || fenceValue != 1))
		status = B_BAD_DATA;

	for (uint32 index = valleyview::kCrocusTriangleBoCount; index > 0;
			index--) {
		status_t closeStatus = CloseRenderBuffer(device,
			buffers[index - 1].handle);
		if (status == B_OK)
			status = closeStatus;
	}
	return status;
}


status_t
CloseRenderBuffer(int device, uint32 handle)
{
	if (handle == 0)
		return B_OK;
	valleyview::RenderBufferClose request = {};
	request.header = valleyview::MakeRenderAbiHeader(sizeof(request));
	request.handle = handle;
	return ioctl(device, valleyview::kRenderCloseBuffer, &request,
		sizeof(request));
}


status_t
CycleRenderBufferDomain(int device, uint32 handle)
{
	valleyview::RenderBufferSetDomain request = {};
	request.header = valleyview::MakeRenderAbiHeader(sizeof(request));
	request.handle = handle;
	request.domain = valleyview::kRenderDomainBcs;
	status_t status = ioctl(device, valleyview::kRenderSetBufferDomain,
		&request, sizeof(request));
	if (status != B_OK || request.previousDomain != valleyview::kRenderDomainCpu)
		return status == B_OK ? B_BAD_DATA : status;

	request.header = valleyview::MakeRenderAbiHeader(sizeof(request));
	request.domain = valleyview::kRenderDomainCpu;
	status = ioctl(device, valleyview::kRenderSetBufferDomain, &request,
		sizeof(request));
	if (status != B_OK
		|| request.previousDomain != valleyview::kRenderDomainBcs) {
		return status == B_OK ? B_BAD_DATA : status;
	}
	return B_OK;
}


status_t
RunRenderMemoryProbe(int device, bool expectPpgtt = false)
{
	valleyview::RenderBufferCreate source = {};
	valleyview::RenderBufferCreate destination = {};
	valleyview::RenderBufferMap sourceMap = {};
	valleyview::RenderBufferMap destinationMap = {};
	sourceMap.area = -1;
	destinationMap.area = -1;
	status_t status = B_OK;

	source.header = valleyview::MakeRenderAbiHeader(sizeof(source));
	source.requestedSize = valleyview::kRenderMemoryTestBytes;
	source.flags = valleyview::kRenderBufferCpuCached;
	status = ioctl(device, valleyview::kRenderCreateBuffer, &source,
		sizeof(source));
	if (status != B_OK)
		goto cleanup;

	destination.header = valleyview::MakeRenderAbiHeader(sizeof(destination));
	destination.requestedSize = valleyview::kRenderMemoryTestBytes;
	destination.flags = valleyview::kRenderBufferCpuCached;
	status = ioctl(device, valleyview::kRenderCreateBuffer, &destination,
		sizeof(destination));
	if (status != B_OK)
		goto cleanup;

	if (expectPpgtt) {
		const uint64 sourceEnd = source.renderAddress + source.size;
		const uint64 destinationEnd
			= destination.renderAddress + destination.size;
		if (!valleyview::ValidatePpgttVaRange(source.renderAddress,
				source.size)
			|| !valleyview::ValidatePpgttVaRange(destination.renderAddress,
				destination.size)
			|| !(sourceEnd <= destination.renderAddress
				|| destinationEnd <= source.renderAddress)) {
			status = B_BAD_DATA;
			goto cleanup;
		}
	} else if (source.renderAddress != 0 || destination.renderAddress != 0) {
		status = B_BAD_DATA;
		goto cleanup;
	}

	sourceMap.header = valleyview::MakeRenderAbiHeader(sizeof(sourceMap));
	sourceMap.handle = source.handle;
	status = ioctl(device, valleyview::kRenderMapBuffer, &sourceMap,
		sizeof(sourceMap));
	if (status != B_OK)
		goto cleanup;

	destinationMap.header
		= valleyview::MakeRenderAbiHeader(sizeof(destinationMap));
	destinationMap.handle = destination.handle;
	status = ioctl(device, valleyview::kRenderMapBuffer, &destinationMap,
		sizeof(destinationMap));
	if (status != B_OK)
		goto cleanup;

	{
		const status_t sourceDeleteStatus = delete_area(sourceMap.area);
		const status_t destinationDeleteStatus
			= delete_area(destinationMap.area);
		if (sourceDeleteStatus != B_NOT_ALLOWED
			|| destinationDeleteStatus != B_NOT_ALLOWED) {
			status = B_BAD_DATA;
			goto cleanup;
		}
	}

	if (sourceMap.address == 0 || destinationMap.address == 0
		|| sourceMap.size < valleyview::kRenderMemoryTestBytes
		|| destinationMap.size < valleyview::kRenderMemoryTestBytes) {
		status = B_BAD_DATA;
		goto cleanup;
	}

	{
		uint32* sourceWords = reinterpret_cast<uint32*>(
			static_cast<addr_t>(sourceMap.address));
		uint32* destinationWords = reinterpret_cast<uint32*>(
			static_cast<addr_t>(destinationMap.address));
		for (uint32 index = 0;
				index < valleyview::kRenderMemoryTestWords; index++) {
			sourceWords[index] = valleyview::RenderMemoryTestWord(index,
				valleyview::kRenderMemoryTestDefaultSeed);
			destinationWords[index]
				= valleyview::RenderMemoryTestDestinationWord(index,
					valleyview::kRenderMemoryTestDefaultSeed);
		}
		__sync_synchronize();
	}

	status = CycleRenderBufferDomain(device, source.handle);
	if (status == B_OK)
		status = CycleRenderBufferDomain(device, destination.handle);
	if (status != B_OK)
		goto cleanup;

	{
		valleyview::RenderMemoryTest test = {};
		test.header = valleyview::MakeRenderAbiHeader(sizeof(test));
		test.sourceHandle = source.handle;
		test.destinationHandle = destination.handle;
		test.seed = valleyview::kRenderMemoryTestDefaultSeed;
		status = ioctl(device, valleyview::kRunRenderMemoryTest, &test,
			sizeof(test));
		printf("render_memory_test status=%" B_PRId32 " stage=%u"
			" marker=%#08" B_PRIx32 " elapsed_us=%" B_PRIu64
			" mismatch=%#" B_PRIx32 "/%#08" B_PRIx32 "\n",
			test.status, test.stage, test.completionMarker, test.elapsedUs,
			test.mismatchOffset, test.observed);
		if (status == B_OK && test.status != B_OK)
			status = test.status;
		if (status != B_OK)
			goto cleanup;
	}

	{
		const uint32* destinationWords = reinterpret_cast<const uint32*>(
			static_cast<addr_t>(destinationMap.address));
		for (uint32 index = 0;
				index < valleyview::kRenderMemoryTestWords; index++) {
			const uint32 expected = valleyview::RenderMemoryTestWord(index,
				valleyview::kRenderMemoryTestDefaultSeed);
			if (destinationWords[index] != expected) {
				status = B_BAD_DATA;
				break;
			}
		}
	}
	if (status == B_OK) {
		printf("render_memory handles=%" B_PRIu32 "/%" B_PRIu32
			" ggtt=%#08" B_PRIx64 "/%#08" B_PRIx64
			" ppgtt=%#08" B_PRIx64 "/%#08" B_PRIx64
			" size=%" B_PRIu64 " mapping_owned=yes verified=yes\n",
			source.handle, destination.handle, source.gpuOffset,
			destination.gpuOffset, source.renderAddress,
			destination.renderAddress, source.size);
	}

cleanup:
	status_t closeStatus = CloseRenderBuffer(device, destination.handle);
	if (status == B_OK)
		status = closeStatus;
	closeStatus = CloseRenderBuffer(device, source.handle);
	if (status == B_OK)
		status = closeStatus;
	return status;
}


status_t
ReadP0Status(int device, valleyview::P0Status& status)
{
	memset(&status, 0, sizeof(status));
	status_t result = ioctl(device, valleyview::kGetP0Status, &status,
		sizeof(status));
	if (result != B_OK)
		return result;
	return valleyview::IsValidAbiHeader(status.header, sizeof(status))
		? B_OK : B_BAD_DATA;
}


bool
P0TransportHealthy(const valleyview::P0Status& before,
	const valleyview::P0Status& after)
{
	const uint32 required = valleyview::kP0NativeScanout
		| valleyview::kP0BcsReady | valleyview::kP0PresentReady
		| valleyview::kP0PresentBcs;
	return (after.flags & required) == required
		&& (after.flags & valleyview::kP0Faulted) == 0
		&& after.nativeStatus == B_OK
		&& after.bcsStatus == B_OK
		&& after.presentStatus == B_OK
		&& after.presentBcsStatus == B_OK
		&& after.bcsFailures == before.bcsFailures
		&& after.presentFailures == before.presentFailures
		&& after.bcsSubmissions > before.bcsSubmissions;
}


status_t
RunRenderTransportProbe(int device)
{
	valleyview::RenderDeviceInfo info = {};
	status_t infoStatus = ReadRenderDeviceInfo(device, info);
	if (infoStatus == B_OK)
		PrintRenderDeviceInfo(info);

	valleyview::P0Status before = {};
	status_t beforeStatus = ReadP0Status(device, before);
	if (beforeStatus == B_OK) {
		printf("render_transport phase=p0_before\n");
		PrintP0Status(before);
	}

	status_t rcsStatus = infoStatus == B_OK ? RunRcsProbe(device) : infoStatus;
	valleyview::RenderContextCreate context = {};
	status_t contextStatus = rcsStatus == B_OK
		? CreateRenderContextProbe(device, context) : rcsStatus;
	valleyview::RenderDeviceInfo contextInfo = {};
	if (contextStatus == B_OK) {
		status_t status = ReadRenderDeviceInfo(device, contextInfo);
		if (status == B_OK) {
			printf("render_transport phase=context_ready\n");
			PrintRenderDeviceInfo(contextInfo);
			const uint64 required
				= valleyview::kRenderCapabilityPpgtt
					| valleyview::kRenderCapabilityRenderContexts
					| valleyview::kRenderCapabilityCommandIsolation
					| valleyview::kRenderCapabilityResetRecovery;
			if ((contextInfo.capabilities & required) != required
				|| (contextInfo.capabilities
					& valleyview::kRenderCapabilityCompletionFences) != 0
				|| (((contextInfo.capabilities
						& valleyview::kRenderCapabilityRcsSubmission) != 0)
					!= ((contextInfo.submissionEngines
						& valleyview::kRenderEngineRcs) != 0))) {
				status = B_BAD_DATA;
			}
		}
		if (status != B_OK)
			contextStatus = status;
	}
	if (contextStatus == B_OK)
		contextStatus = RejectDuplicateRenderContextProbe(device, context);

	status_t submitStatus = context.handle != 0
		? RunRcsSubmissionProbe(device, context) : contextStatus;
	valleyview::RenderDeviceInfo submissionInfo = {};
	status_t submissionInfoStatus = submitStatus == B_OK
		? ReadRenderDeviceInfo(device, submissionInfo) : submitStatus;
	if (submissionInfoStatus == B_OK) {
		printf("render_transport phase=submission_ready\n");
		PrintRenderDeviceInfo(submissionInfo);
		if ((submissionInfo.capabilities
				& valleyview::kRenderCapabilityRcsSubmission) == 0
			|| (submissionInfo.submissionEngines
				& valleyview::kRenderEngineRcs) == 0) {
			submissionInfoStatus = B_BAD_DATA;
		}
	}
	if (submitStatus == B_OK)
		submitStatus = submissionInfoStatus;
	status_t rasterStatus = context.handle != 0
		? RunCrocusTriangleProbe(device, context) : contextStatus;
	status_t memoryStatus = context.handle != 0
		? RunRenderMemoryProbe(device, true) : contextStatus;
	status_t destroyStatus = DestroyRenderContextProbe(device, context);
	if (contextStatus == B_OK)
		contextStatus = destroyStatus;

	valleyview::RenderDeviceInfo afterInfo = {};
	status_t afterInfoStatus = ReadRenderDeviceInfo(device, afterInfo);
	if (afterInfoStatus == B_OK) {
		printf("render_transport phase=render_after\n");
		PrintRenderDeviceInfo(afterInfo);
	}
	const bool rcsProven = afterInfoStatus == B_OK
		&& (afterInfo.provenEngines & valleyview::kRenderEngineRcs) != 0;
	const bool contextReleased = afterInfoStatus == B_OK
		&& (afterInfo.capabilities
			& (valleyview::kRenderCapabilityPpgtt
				| valleyview::kRenderCapabilityRcsSubmission)) == 0;

	valleyview::P0Status after = {};
	status_t afterStatus = ReadP0Status(device, after);
	if (afterStatus == B_OK) {
		printf("render_transport phase=p0_after\n");
		PrintP0Status(after);
	}
	const bool p0Healthy = beforeStatus == B_OK && afterStatus == B_OK
		&& P0TransportHealthy(before, after);
	printf("render_transport info=%s context=%s released=%s memory=%s"
		" rcs=%s submit=%s raster=%s proven=%s p0=%s\n",
		YesNo(infoStatus == B_OK), YesNo(contextStatus == B_OK),
		YesNo(contextReleased), YesNo(memoryStatus == B_OK),
		YesNo(rcsStatus == B_OK), YesNo(submitStatus == B_OK),
		YesNo(rasterStatus == B_OK), YesNo(rcsProven), YesNo(p0Healthy));

	if (infoStatus != B_OK)
		return infoStatus;
	if (beforeStatus != B_OK)
		return beforeStatus;
	if (contextStatus != B_OK)
		return contextStatus;
	if (!contextReleased)
		return afterInfoStatus == B_OK ? B_BAD_DATA : afterInfoStatus;
	if (memoryStatus != B_OK)
		return memoryStatus;
	if (rcsStatus != B_OK)
		return rcsStatus;
	if (submitStatus != B_OK)
		return submitStatus;
	if (rasterStatus != B_OK)
		return rasterStatus;
	if (!rcsProven)
		return afterInfoStatus == B_OK ? B_BAD_DATA : afterInfoStatus;
	if (afterStatus != B_OK)
		return afterStatus;
	return p0Healthy ? B_OK : B_BAD_DATA;
}


double
MebibytesPerSecond(uint64 bytes, bigtime_t elapsed)
{
	return elapsed > 0
		? static_cast<double>(bytes) * 1000000.0
			/ (static_cast<double>(elapsed) * 1024.0 * 1024.0)
		: 0.0;
}


uint32
GridPixel(uint32 x, uint32 y)
{
	if (x == 0 || y == 0 || x == 127 || y == 127)
		return 0x00ffffff;
	if (x == y || x + y == 127)
		return 0x00ffff00;
	if ((x % 16) == 0 || (y % 16) == 0)
		return 0x004080ff;
	const uint32 quadrant = (x >= 64 ? 1 : 0) | (y >= 64 ? 2 : 0);
	const uint32 colors[4] = {
		0x00202040, 0x00402020, 0x00204020, 0x00404020
	};
	return colors[quadrant];
}


status_t
RunP0Benchmark(int device, const valleyview::P0Status& initial)
{
	constexpr uint32 kWidth = 256;
	constexpr uint32 kHeight = 128;
	constexpr uint32 kUploadIterations = 128;
	constexpr uint32 kRmwIterations = 16;
	constexpr uint64 kRequiredPresentFrames = 2;
	if ((initial.flags & (valleyview::kP0NativeScanout
			| valleyview::kP0PresentReady))
			!= (valleyview::kP0NativeScanout
				| valleyview::kP0PresentReady)
		|| initial.width < kWidth || initial.height < kHeight) {
		return B_NO_INIT;
	}

	area_info info = {};
	status_t result = ioctl(device, valleyview::kCloneFramebuffer, &info,
		sizeof(info));
	if (result != B_OK)
		return result;
	const uint64 requiredSize
		= static_cast<uint64>(initial.bytesPerRow) * initial.height;
	if (info.address == NULL || info.size < requiredSize) {
		delete_area(info.area);
		return B_BAD_DATA;
	}

	const size_t tilePixels = static_cast<size_t>(kWidth) * kHeight;
	const size_t tileBytes = tilePixels * sizeof(uint32);
	uint32* first = static_cast<uint32*>(malloc(tileBytes));
	uint32* second = static_cast<uint32*>(malloc(tileBytes));
	if (first == NULL || second == NULL) {
		free(first);
		free(second);
		delete_area(info.area);
		return B_NO_MEMORY;
	}
	for (size_t index = 0; index < tilePixels; index++) {
		first[index] = 0x00204080;
		second[index] = 0x00804020;
	}

	const uint32 left = 0;
	const uint32 top = initial.height - kHeight;
	uint8* framebuffer = static_cast<uint8*>(info.address);
	const bigtime_t uploadStarted = system_time();
	for (uint32 iteration = 0; iteration < kUploadIterations; iteration++) {
		const uint32* source = (iteration & 1) != 0 ? second : first;
		for (uint32 row = 0; row < kHeight; row++) {
			memcpy(framebuffer + (top + row) * initial.bytesPerRow
					+ left * sizeof(uint32),
				source + row * kWidth, kWidth * sizeof(uint32));
		}
		__sync_synchronize();
	}
	const bigtime_t uploadElapsed = system_time() - uploadStarted;

	const bigtime_t rmwStarted = system_time();
	for (uint32 iteration = 0; iteration < kRmwIterations; iteration++) {
		for (uint32 row = 0; row < kHeight; row++) {
			volatile uint32* destination
				= reinterpret_cast<volatile uint32*>(
					framebuffer + (top + row) * initial.bytesPerRow);
			for (uint32 x = 0; x < kWidth; x++)
				destination[x] ^= 0x00ffffff;
		}
		__sync_synchronize();
	}
	const bigtime_t rmwElapsed = system_time() - rmwStarted;

	valleyview::P0Status before = {};
	if (result == B_OK)
		result = ReadP0Status(device, before);

	for (uint32 row = 0; row < kHeight; row++) {
		volatile uint32* destination = reinterpret_cast<volatile uint32*>(
			framebuffer + (top + row) * initial.bytesPerRow);
		for (uint32 x = 0; x < kWidth / 2; x++) {
			const uint32 pixel = GridPixel(x, row);
			destination[x] = pixel;
			destination[x + kWidth / 2] = pixel;
		}
	}
	__sync_synchronize();

	valleyview::P0Status after = before;
	const bigtime_t presentStarted = system_time();
	const bigtime_t presentDeadline = presentStarted + 250000;
	while (result == B_OK
		&& after.presentFrames - before.presentFrames
			< kRequiredPresentFrames
		&& system_time() < presentDeadline) {
		snooze(1000);
		result = ReadP0Status(device, after);
	}
	const bigtime_t presentElapsed = system_time() - presentStarted;
	const uint64 uploadBytes = static_cast<uint64>(tileBytes)
		* kUploadIterations;
	const uint64 rmwBytes = static_cast<uint64>(tileBytes)
		* kRmwIterations * 2;
	printf("p0_benchmark region=%ux%u+%u+%u\n", kWidth, kHeight, left, top);
	printf("p0_benchmark shadow_upload_us=%" B_PRIdBIGTIME
		" mib_s=%.1f cpu_rmw_us=%" B_PRIdBIGTIME " effective_mib_s=%.1f\n",
		uploadElapsed, MebibytesPerSecond(uploadBytes, uploadElapsed),
		rmwElapsed, MebibytesPerSecond(rmwBytes, rmwElapsed));
	if (result == B_OK) {
		const uint64 frames = after.presentFrames - before.presentFrames;
		const uint64 failures
			= after.presentFailures - before.presentFailures;
		const uint64 bcsCopies
			= after.presentBcsCopies - before.presentBcsCopies;
		const uint64 cpuCopies
			= after.presentCpuCopies - before.presentCpuCopies;
		const bool presentReady
			= (after.flags & valleyview::kP0PresentReady) != 0;
		const bool liveMatches = after.activeScanout >= 0
			&& after.activeScanout < 2
			&& (after.planeSurfaceLive & ~valleyview::kPageMask)
				== after.scanoutOffset[after.activeScanout];
		printf("p0_benchmark present_wait_us=%" B_PRIdBIGTIME
			" frames=%" B_PRIu64 " failures=%" B_PRIu64
			" copies=%" B_PRIu64 "/%" B_PRIu64
			" mode=%s active=%" B_PRId32 " pending=%" B_PRId32 "\n",
			presentElapsed, frames, failures, bcsCopies, cpuCopies,
			(after.flags & valleyview::kP0PresentBcs) != 0 ? "bcs" : "cpu",
			after.activeScanout, after.pendingScanout);
		printf("p0_benchmark copy_us=%" B_PRIu64 "/%" B_PRIu64
			" flip_us=%" B_PRIu64 "/%" B_PRIu64
			" live_matches=%s\n", after.presentCopyLastUs,
			after.presentCopyMaxUs, after.presentFlipLastUs,
			after.presentFlipMaxUs, YesNo(liveMatches));
		if (frames < kRequiredPresentFrames || failures != 0
			|| bcsCopies + cpuCopies == 0 || !presentReady
			|| !liveMatches) {
			result = B_BAD_DATA;
		}
	}

	free(first);
	free(second);
	delete_area(info.area);
	return result;
}


} // namespace


int
main(int argc, char** argv)
{
	int device = open(kDevicePath, O_RDONLY);
	if (device < 0) {
		fprintf(stderr, "intel_valleyview_probe: cannot open %s: %s\n",
			kDevicePath, strerror(errno));
		return 1;
	}

	valleyview::FirmwareSnapshot snapshot = {};
	status_t status = ioctl(device, valleyview::kGetFirmwareSnapshot, &snapshot,
		sizeof(snapshot));
	if (status != B_OK) {
		fprintf(stderr, "intel_valleyview_probe: snapshot ioctl failed: %s\n",
			strerror(status));
		close(device);
		return 1;
	}
	if (!valleyview::IsValidAbiHeader(snapshot.header, sizeof(snapshot))) {
		fprintf(stderr, "intel_valleyview_probe: incompatible snapshot ABI\n");
		close(device);
		return 1;
	}

	PrintSnapshot(snapshot);
	if (argc == 2 && strcmp(argv[1], "--publish") == 0) {
		status = ioctl(device, valleyview::kPublishGraphics, NULL, 0);
		if (status != B_OK) {
			fprintf(stderr,
				"intel_valleyview_probe: graphics publication failed: %s\n",
				strerror(status));
			close(device);
			return 1;
		}
		printf("published /dev/graphics/intel_valleyview_000200\n");
	} else if (argc == 2
		&& (strcmp(argv[1], "--gpu-diagnostics") == 0
			|| strcmp(argv[1], "--gpu-self-test") == 0)) {
		valleyview::GpuDiagnostics diagnostics = {};
		diagnostics.header
			= valleyview::MakeAbiHeader(sizeof(diagnostics));
		const bool runSelfTest = strcmp(argv[1], "--gpu-self-test") == 0;
		if (runSelfTest)
			diagnostics.command = valleyview::kGpuSelfTestArm;
		status = ioctl(device, runSelfTest
				? valleyview::kRunGpuSelfTest
				: valleyview::kGetGpuDiagnostics,
			&diagnostics, sizeof(diagnostics));
		if (valleyview::IsValidAbiHeader(diagnostics.header,
				sizeof(diagnostics))) {
			PrintGpuDiagnostics(diagnostics);
		}
		if (status != B_OK || diagnostics.status != B_OK) {
			const status_t failure
				= status != B_OK ? status : diagnostics.status;
			fprintf(stderr,
				"intel_valleyview_probe: GPU diagnostic failed: %s"
				" (result %" B_PRId32 ")\n", strerror(failure),
				diagnostics.status);
			close(device);
			return 1;
		}
	} else if (argc == 2
		&& strcmp(argv[1], "--render-memory-test") == 0) {
		status = RunRenderMemoryProbe(device);
		if (status != B_OK) {
			fprintf(stderr,
				"intel_valleyview_probe: render memory test failed: %s\n",
				strerror(status));
			close(device);
			return 1;
		}
	} else if (argc == 2 && strcmp(argv[1], "--rcs-test") == 0) {
		status = RunRcsProbe(device);
		if (status != B_OK) {
			fprintf(stderr,
				"intel_valleyview_probe: RCS diagnostic failed: %s\n",
				strerror(status));
			close(device);
			return 1;
		}
	} else if (argc == 2
		&& strcmp(argv[1], "--render-transport-test") == 0) {
		status = RunRenderTransportProbe(device);
		if (status != B_OK) {
			fprintf(stderr,
				"intel_valleyview_probe: render transport failed: %s\n",
				strerror(status));
			close(device);
			return 1;
		}
	} else if (argc == 2 && strcmp(argv[1], "--render-info") == 0) {
		valleyview::RenderDeviceInfo info = {};
		status = ReadRenderDeviceInfo(device, info);
		if (status != B_OK) {
			fprintf(stderr,
				"intel_valleyview_probe: render info failed: %s\n",
				strerror(status));
			close(device);
			return 1;
		}
		PrintRenderDeviceInfo(info);
	} else if (argc == 2
		&& (strcmp(argv[1], "--p0-status") == 0
			|| strcmp(argv[1], "--p0-test") == 0
			|| strcmp(argv[1], "--p0-benchmark") == 0)) {
		valleyview::P0Status p0 = {};
		status = ioctl(device, valleyview::kGetP0Status, &p0, sizeof(p0));
		if (status != B_OK
			|| !valleyview::IsValidAbiHeader(p0.header, sizeof(p0))) {
			fprintf(stderr,
				"intel_valleyview_probe: P0 status failed: %s\n",
				strerror(status));
			close(device);
			return 1;
		}
		PrintP0Status(p0);
		if (strcmp(argv[1], "--p0-benchmark") == 0) {
			status = RunP0Benchmark(device, p0);
			if (status != B_OK) {
				fprintf(stderr,
					"intel_valleyview_probe: P0 benchmark failed: %s\n",
					strerror(status));
				close(device);
				return 1;
			}
		} else if (strcmp(argv[1], "--p0-test") == 0) {
			valleyview::P0SelfTest test = {};
			test.header = valleyview::MakeAbiHeader(sizeof(test));
			test.command = valleyview::kP0SelfTestArm;
			status = ioctl(device, valleyview::kRunP0SelfTest, &test,
				sizeof(test));
			if (valleyview::IsValidAbiHeader(test.header, sizeof(test))) {
				printf("p0_test status=%" B_PRId32 " flags=%#08"
					B_PRIx32 "\n", test.status, test.flags);
				PrintP0Status(test.after);
			}
			if (status != B_OK || test.status != B_OK) {
				fprintf(stderr,
					"intel_valleyview_probe: P0 BCS self-test failed\n");
				close(device);
				return 1;
			}

			valleyview::BrightnessRequest brightness = {};
			status = ioctl(device, valleyview::kGetBrightness, &brightness,
				sizeof(brightness));
			if (status != B_OK) {
				fprintf(stderr,
					"intel_valleyview_probe: brightness read failed\n");
				close(device);
				return 1;
			}
			const float originalBrightness = brightness.value;
			brightness.header
				= valleyview::MakeAbiHeader(sizeof(brightness));
			brightness.value = originalBrightness * 0.75f;
			status = ioctl(device, valleyview::kSetBrightness, &brightness,
				sizeof(brightness));
			brightness.value = originalBrightness;
			status_t restoreStatus = ioctl(device,
				valleyview::kSetBrightness, &brightness,
				sizeof(brightness));
			if (status != B_OK || restoreStatus != B_OK) {
				fprintf(stderr,
					"intel_valleyview_probe: brightness cycle failed\n");
				close(device);
				return 1;
			}

			valleyview::DpmsRequest dpms = {};
			dpms.header = valleyview::MakeAbiHeader(sizeof(dpms));
			dpms.mode = B_DPMS_STAND_BY;
			status = ioctl(device, valleyview::kSetDpms, &dpms,
				sizeof(dpms));
			snooze(100000);
			dpms.mode = B_DPMS_ON;
			restoreStatus = ioctl(device, valleyview::kSetDpms, &dpms,
				sizeof(dpms));
			if (status != B_OK || restoreStatus != B_OK) {
				fprintf(stderr,
					"intel_valleyview_probe: DPMS cycle failed\n");
				close(device);
				return 1;
			}
			printf("p0_test brightness_cycle=yes soft_dpms_cycle=yes\n");
		}
	} else if (argc != 1) {
		fprintf(stderr, "usage: intel_valleyview_probe"
			" [--publish|--gpu-diagnostics|--gpu-self-test"
			"|--render-info|--render-memory-test|--rcs-test"
			"|--render-transport-test"
			"|--p0-status|--p0-test|--p0-benchmark]\n");
		close(device);
		return 1;
	}
	close(device);

	return snapshot.mmioStatus == B_OK
			&& snapshot.opRegionStatus == B_OK
			&& (snapshot.flags & valleyview::kSnapshotVbtPresent) != 0
		? 0 : 1;
}
