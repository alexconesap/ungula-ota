// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once
#ifndef __cplusplus
#error UngulaOta requires a C++ compiler
#endif

#include <ungula_core.h>  // triggers lib/ path discovery for Arduino .ino projects

// Core
#include "ota/core/i_firmware_writer.h"
#include "ota/core/i_ota_source.h"
#include "ota/core/ota_types.h"
#include "ota/core/ota_updater.h"
#include "ota/core/ota_version.h"
