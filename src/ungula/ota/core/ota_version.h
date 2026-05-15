// SPDX-License-Identifier: MIT
// Copyright (c) 2024-2026 Alex Conesa
// See LICENSE file for details.

#pragma once

#include <cstdio>

namespace ungula::ota
{

/// Compare two semantic version strings (x.y.z format).
/// Returns  1 if remote > current
/// Returns  0 if remote == current
/// Returns -1 if remote < current
inline int compareVersions(const char *remote, const char *current)
{
        int rMajor = 0, rMinor = 0, rPatch = 0;
        int cMajor = 0, cMinor = 0, cPatch = 0;

        sscanf(remote, "%d.%d.%d", &rMajor, &rMinor, &rPatch);
        sscanf(current, "%d.%d.%d", &cMajor, &cMinor, &cPatch);

        if (rMajor != cMajor)
                return (rMajor > cMajor) ? 1 : -1;
        if (rMinor != cMinor)
                return (rMinor > cMinor) ? 1 : -1;
        if (rPatch != cPatch)
                return (rPatch > cPatch) ? 1 : -1;

        return 0;
}

} // namespace ungula::ota
