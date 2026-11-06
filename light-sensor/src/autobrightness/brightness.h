/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include "common.h"

typedef struct {
    GDBusConnection *system_bus;
    GDBusConnection *session_bus;

    BrightnessBackend backend;
    gboolean kde_screen_bus_present;
    gboolean kde_power_bus_present;

    guint screen_watch_id;
    guint kde5_watch_id;
    guint screen_added_sub_id;
    guint screen_removed_sub_id;
    guint screen_range_sub_id;

    gchar *internal_display_name;
    gchar *internal_display_path;
    gchar *sysfs_backlight_name;
    gchar *sysfs_backlight_dir;

    gint32 brightness_max;
    double current_pct;

    void (*on_display_changed)(gpointer user_data);
    gpointer user_data;
} BrightnessManager;

void brightness_mgr_init(BrightnessManager *mgr,
                         GDBusConnection *system_bus,
                         GDBusConnection *session_bus,
                         void (*on_display_changed)(gpointer user_data),
                         gpointer user_data);

void brightness_mgr_cleanup(BrightnessManager *mgr);

void brightness_mgr_select_backend(BrightnessManager *mgr);

gboolean brightness_mgr_is_ready(const BrightnessManager *mgr);

void brightness_mgr_sync(BrightnessManager *mgr, gboolean reset_target);

void brightness_mgr_apply(BrightnessManager *mgr, double pct);

