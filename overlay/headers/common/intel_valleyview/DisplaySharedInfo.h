// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#ifndef INTEL_VALLEYVIEW_DISPLAY_SHARED_INFO_H
#define INTEL_VALLEYVIEW_DISPLAY_SHARED_INFO_H

#include <common/intel_valleyview/Protocol.h>

#include <Accelerant.h>


namespace valleyview {

struct DisplaySharedInfo {
	AbiHeader		header;
	area_id			modeListArea;
	uint32			modeCount;
	display_mode	currentMode;
	uint32			bytesPerRow;
	uint64			framebufferPhysical;
	uint64			framebufferSize;
	uint8			nativeActive;
	uint8			reserved[3];
};

} // namespace valleyview

#endif
