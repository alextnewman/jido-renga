// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
#pragma once

#include <SupportDefs.h>


namespace gpio {

enum class Level : uint8 {
	Low,
	High
};


enum class Bias : uint8 {
	Firmware,
	None,
	PullUp,
	PullDown
};


enum class Access : uint8 {
	Preserve,
	Input,
	Output,
	Bidirectional
};


enum class Edge : uint8 {
	Rising,
	Falling,
	Both
};

} // namespace gpio
