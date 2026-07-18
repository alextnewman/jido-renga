// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include <common/intel_valleyview/FirmwareState.h>
#include <common/intel_valleyview/P0Core.h>
#include <common/intel_valleyview/Protocol.h>

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
