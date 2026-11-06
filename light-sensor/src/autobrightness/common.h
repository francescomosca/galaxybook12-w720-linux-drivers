/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

#define SENSOR_BUS_NAME         "net.hadess.SensorProxy"
#define SENSOR_OBJECT_PATH      "/net/hadess/SensorProxy"
#define SENSOR_INTERFACE        "net.hadess.SensorProxy"

/* KDE Plasma 6 per-display interface */
#define SCREEN_BUS_NAME         "org.kde.ScreenBrightness"
#define SCREEN_MGR_PATH         "/org/kde/ScreenBrightness"
#define SCREEN_MGR_INTERFACE    "org.kde.ScreenBrightness"
#define SCREEN_DISP_INTERFACE   "org.kde.ScreenBrightness.Display"
#define SUPPRESS_INDICATOR_FLAG 1u

/* KDE Plasma 5 legacy interface */
#define KDE5_POWER_BUS_NAME     "org.kde.Solid.PowerManagement"
#define KDE5_BRIGHTNESS_PATH    "/org/kde/Solid/PowerManagement/Actions/BrightnessControl"
#define KDE5_BRIGHTNESS_IFACE   "org.kde.Solid.PowerManagement.Actions.BrightnessControl"

/* Universal systemd-logind interface */
#define LOGIN1_BUS_NAME         "org.freedesktop.login1"
#define LOGIN1_SESSION_AUTO     "/org/freedesktop/login1/session/auto"
#define LOGIN1_SESSION_SELF     "/org/freedesktop/login1/session/self"
#define LOGIN1_SESSION_IFACE    "org.freedesktop.login1.Session"

typedef enum {
    BACKEND_SYSFS_LOGIND = 0,
    BACKEND_KDE_PER_DISPLAY,
    BACKEND_KDE_LEGACY
} BrightnessBackend;

typedef struct {
    double lux;
    double pct;
} CurvePoint;

