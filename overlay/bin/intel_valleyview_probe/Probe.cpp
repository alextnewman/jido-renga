// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot

#include <common/intel_valleyview/FirmwareState.h>
#include <common/intel_valleyview/Protocol.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
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
		" position=%#08" B_PRIx32 "\n", snapshot.cursorControl,
		YesNo((snapshot.flags & valleyview::kSnapshotCursorEnabled) != 0),
		snapshot.cursorBase, snapshot.cursorPosition);
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
	} else if (argc != 1) {
		fprintf(stderr, "usage: intel_valleyview_probe [--publish]\n");
		close(device);
		return 1;
	}
	close(device);

	return snapshot.mmioStatus == B_OK
			&& snapshot.opRegionStatus == B_OK
			&& (snapshot.flags & valleyview::kSnapshotVbtPresent) != 0
		? 0 : 1;
}
