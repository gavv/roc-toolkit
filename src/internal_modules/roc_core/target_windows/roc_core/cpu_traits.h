/*
 * Copyright (c) 2024 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_core/target_windows/roc_core/cpu_traits.h
//! @brief CPU traits.

#ifndef ROC_CORE_CPU_TRAITS_H_
#define ROC_CORE_CPU_TRAITS_H_

#include "roc_core/stddefs.h"

//! Values of ROC_CPU_ENDIAN indicating little- or big-endian CPU.
#define ROC_CPU_BE 1
#define ROC_CPU_LE 2

// CPU endianess.
// Assume that windows always runs on little-endian.
#define ROC_CPU_ENDIAN ROC_CPU_LE

// Detect CPU bitness.

#ifdef _WIN64
#define ROC_CPU_BITS 64
#else
#define ROC_CPU_BITS 32
#endif

#endif // ROC_CORE_CPU_TRAITS_H_
