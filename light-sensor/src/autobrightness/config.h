/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include "common.h"

typedef void (*ConfigChangedCb)(gboolean enabled, double min_brightness, gpointer user_data);

typedef struct {
    gboolean is_kde_session;
    gchar *kwin_config_path;
    GFileMonitor *kwin_config_monitor;

    gchar *user_config_path;
    GFileMonitor *user_config_monitor;

    gchar *sys_config_path;
    GFileMonitor *sys_config_monitor;

    guint reload_timer_id;
    GSettings *de_power_settings;

    gboolean auto_brightness_enabled;
    double min_brightness_pct;
    gboolean min_brightness_cli_set;

    ConfigChangedCb on_changed;
    gpointer user_data;
} ConfigManager;

void config_mgr_init(ConfigManager *cfg,
                     double initial_min_brightness,
                     gboolean cli_set,
                     ConfigChangedCb on_changed,
                     gpointer user_data);

void config_mgr_cleanup(ConfigManager *cfg);

int parse_kwin_auto_brightness_json(const char *json, gsize len);
int parse_config_file(const gchar *path, int *out_enabled, double *out_min_brightness);

