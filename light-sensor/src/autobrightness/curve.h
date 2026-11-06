/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include "common.h"

/*
 * Calibrated photometric curve for Samsung AMOLED panel.
 * Interpolates lux to target brightness percentage, clamped to min_pct (0.0 - 100.0).
 */
double lux_to_target_pct(double lux, double min_pct);

