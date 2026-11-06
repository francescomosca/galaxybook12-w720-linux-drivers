/* SPDX-License-Identifier: GPL-2.0-only */
#include "curve.h"

static const CurvePoint ALS_CURVE[] = {
    { 0.0,     0.0 },
    { 4.0,    10.0 },
    { 15.0,   38.0 },
    { 40.0,   50.0 },
    { 100.0,  65.0 },
    { 300.0,  80.0 },
    { 800.0,  92.0 },
    { 1500.0, 100.0 }
};

#define ALS_CURVE_SIZE (sizeof(ALS_CURVE) / sizeof(ALS_CURVE[0]))

double lux_to_target_pct(double lux, double min_pct)
{
    double pct = 50.0;

    if (lux <= ALS_CURVE[0].lux)
        pct = ALS_CURVE[0].pct;
    else if (lux >= ALS_CURVE[ALS_CURVE_SIZE - 1].lux)
        pct = ALS_CURVE[ALS_CURVE_SIZE - 1].pct;
    else {
        for (size_t i = 0; i < ALS_CURVE_SIZE - 1; i++) {
            double x0 = ALS_CURVE[i].lux;
            double y0 = ALS_CURVE[i].pct;
            double x1 = ALS_CURVE[i + 1].lux;
            double y1 = ALS_CURVE[i + 1].pct;

            if (lux >= x0 && lux <= x1) {
                pct = y0 + (lux - x0) * (y1 - y0) / (x1 - x0);
                break;
            }
        }
    }

    if (pct < min_pct)
        pct = min_pct;
    if (pct > 100.0)
        pct = 100.0;

    return pct;
}

