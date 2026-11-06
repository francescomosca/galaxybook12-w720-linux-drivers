/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include "common.h"

typedef void (*SensorLightLevelCb)(double lux, gpointer user_data);

typedef struct {
    GDBusConnection *system_bus;
    guint sensor_watch_id;
    guint sensor_sub_id;
    gboolean sensor_present;
    gboolean claimed;

    SensorLightLevelCb on_light_level;
    void (*on_sensor_appeared)(gpointer user_data);
    void (*on_sensor_vanished)(gpointer user_data);
    gpointer user_data;
} SensorManager;

void sensor_mgr_init(SensorManager *sm,
                     GDBusConnection *system_bus,
                     SensorLightLevelCb on_light_level,
                     void (*on_sensor_appeared)(gpointer user_data),
                     void (*on_sensor_vanished)(gpointer user_data),
                     gpointer user_data);

void sensor_mgr_cleanup(SensorManager *sm);

void sensor_mgr_claim(SensorManager *sm);

void sensor_mgr_release(SensorManager *sm);

void sensor_mgr_fetch_light_level(SensorManager *sm);

