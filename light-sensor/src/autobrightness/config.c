/* SPDX-License-Identifier: GPL-2.0-only */
#include "config.h"

static gboolean detect_kde_environment(void)
{
    const gchar *xdg = g_getenv("XDG_CURRENT_DESKTOP");
    const gchar *session = g_getenv("DESKTOP_SESSION");
    const gchar *kde_full = g_getenv("KDE_FULL_SESSION");

    if (kde_full && (*kde_full == '1' || g_ascii_strcasecmp(kde_full, "true") == 0))
        return TRUE;
    if (xdg && (g_strstr_len(xdg, -1, "KDE") || g_strstr_len(xdg, -1, "kde") ||
                g_strstr_len(xdg, -1, "Plasma") || g_strstr_len(xdg, -1, "plasma")))
        return TRUE;
    if (session && (g_strstr_len(session, -1, "KDE") || g_strstr_len(session, -1, "kde") ||
                    g_strstr_len(session, -1, "plasma")))
        return TRUE;

    /* Fallback: if kwinoutputconfig.json exists, we are running under KWin */
    gchar *kwin = g_build_filename(g_get_user_config_dir(), "kwinoutputconfig.json", NULL);
    gboolean exists = g_file_test(kwin, G_FILE_TEST_EXISTS);
    g_free(kwin);
    return exists;
}

typedef struct {
    int bracket_depth;
    gboolean is_internal_connector;
    gboolean has_auto_br;
    gboolean auto_br_val;
} JsonObjState;

#define MAX_JSON_DEPTH 32

static const char *skip_ws(const char *p)
{
    while (*p && g_ascii_isspace(*p))
        p++;
    return p;
}

static const char *parse_json_string(const char *p, char *out_buf, size_t out_sz)
{
    if (*p != '"')
        return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (!*p)
                return NULL;
        }
        if (out_buf && i + 1 < out_sz)
            out_buf[i++] = *p;
        p++;
    }
    if (*p != '"')
        return NULL;
    if (out_buf && out_sz > 0)
        out_buf[i] = '\0';
    return p + 1;
}

static gboolean is_internal_connector_name(const char *name)
{
    return (g_ascii_strncasecmp(name, "eDP", 3) == 0 ||
            g_ascii_strncasecmp(name, "LVDS", 4) == 0 ||
            g_ascii_strncasecmp(name, "DSI", 3) == 0);
}

int parse_kwin_auto_brightness_json(const char *json, gsize len)
{
    if (!json || len == 0)
        return -1;

    const char *p = skip_ws(json);
    if (*p == '\0')
        return -1;

    JsonObjState stack[MAX_JSON_DEPTH];
    int depth = 0;
    gboolean saw_root_token = FALSE;
    gboolean found_internal = FALSE;
    gboolean found_has_auto_br = FALSE;
    gboolean internal_enabled = FALSE;

    while (*p) {
        p = skip_ws(p);
        if (!*p)
            break;

        if (*p == '{') {
            saw_root_token = TRUE;
            if (depth < MAX_JSON_DEPTH) {
                memset(&stack[depth], 0, sizeof(JsonObjState));
            }
            depth++;
            p++;
            continue;
        }

        if (*p == '}') {
            if (depth <= 0)
                return -1;
            depth--;
            if (depth < MAX_JSON_DEPTH && stack[depth].is_internal_connector) {
                found_internal = TRUE;
                if (stack[depth].has_auto_br) {
                    found_has_auto_br = TRUE;
                    internal_enabled = stack[depth].auto_br_val;
                }
            }
            p++;
            continue;
        }

        if (*p == '[') {
            saw_root_token = TRUE;
            if (depth > 0 && depth <= MAX_JSON_DEPTH)
                stack[depth - 1].bracket_depth++;
            p++;
            continue;
        }

        if (*p == ']') {
            if (depth > 0 && depth <= MAX_JSON_DEPTH && stack[depth - 1].bracket_depth > 0)
                stack[depth - 1].bracket_depth--;
            p++;
            continue;
        }

        if (*p == '"') {
            char key_buf[128];
            const char *next = parse_json_string(p, key_buf, sizeof(key_buf));
            if (!next)
                return -1;

            p = skip_ws(next);
            if (*p == ':' && depth > 0 && depth <= MAX_JSON_DEPTH &&
                stack[depth - 1].bracket_depth == 0) {
                p = skip_ws(p + 1);
                JsonObjState *cur = &stack[depth - 1];

                if (strcmp(key_buf, "connectorName") == 0 && *p == '"') {
                    char val_buf[64];
                    const char *after_val = parse_json_string(p, val_buf, sizeof(val_buf));
                    if (!after_val)
                        return -1;
                    if (is_internal_connector_name(val_buf))
                        cur->is_internal_connector = TRUE;
                    p = after_val;
                    continue;
                } else if (strcmp(key_buf, "automaticBrightness") == 0) {
                    if (strncmp(p, "true", 4) == 0 && !g_ascii_isalnum(p[4])) {
                        cur->has_auto_br = TRUE;
                        cur->auto_br_val = TRUE;
                        p += 4;
                        continue;
                    } else if (strncmp(p, "false", 5) == 0 && !g_ascii_isalnum(p[5])) {
                        cur->has_auto_br = TRUE;
                        cur->auto_br_val = FALSE;
                        p += 5;
                        continue;
                    }
                }
            }
            continue;
        }

        p++;
    }

    if (!saw_root_token || depth != 0)
        return -1;

    if (found_internal) {
        if (found_has_auto_br)
            return internal_enabled ? 1 : 0;
        return -1;
    }

    return -1;
}

int parse_config_file(const gchar *path, int *out_enabled, double *out_min_brightness)
{
    if (!path)
        return -2;

    gchar *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(path, &contents, &len, NULL))
        return -2;

    int enabled = -2;
    gchar **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines && lines[i]; i++) {
        gchar *line = g_strstrip(lines[i]);
        if (*line == '\0' || *line == '#')
            continue;
        const char *eq = strchr(line, '=');
        if (!eq)
            continue;

        gchar *key = g_strstrip(g_strndup(line, eq - line));
        gchar *val = g_strstrip(g_strdup(eq + 1));

        if (g_ascii_strcasecmp(key, "enabled") == 0) {
            if (g_ascii_strcasecmp(val, "false") == 0 ||
                g_ascii_strcasecmp(val, "0") == 0 ||
                g_ascii_strcasecmp(val, "no") == 0 ||
                g_ascii_strcasecmp(val, "off") == 0) {
                enabled = 0;
            } else if (g_ascii_strcasecmp(val, "true") == 0 ||
                       g_ascii_strcasecmp(val, "1") == 0 ||
                       g_ascii_strcasecmp(val, "yes") == 0 ||
                       g_ascii_strcasecmp(val, "on") == 0) {
                enabled = 1;
            }
        } else if (g_ascii_strcasecmp(key, "min_brightness") == 0 ||
                   g_ascii_strcasecmp(key, "min_pct") == 0 ||
                   g_ascii_strcasecmp(key, "min") == 0) {
            char *endptr = NULL;
            double d = g_ascii_strtod(val, &endptr);
            if (endptr && endptr != val && d >= 0.0 && d <= 100.0) {
                if (out_min_brightness)
                    *out_min_brightness = d;
            }
        }
        g_free(key);
        g_free(val);
    }
    g_strfreev(lines);
    g_free(contents);

    if (out_enabled && enabled != -2)
        *out_enabled = enabled;
    return enabled;
}

static int evaluate_auto_brightness_enabled(ConfigManager *cfg)
{
    int user_enabled = -2;
    parse_config_file(cfg->sys_config_path, &user_enabled, NULL);
    parse_config_file(cfg->user_config_path, &user_enabled, NULL);

    /* 1. Explicit hard kill-switch in config file overrides everything */
    if (user_enabled == 0)
        return 0;

    /* 2. In a KDE session (or if kwinoutputconfig.json exists), respect the internal
     * display's automaticBrightness toggle configured in KDE System Settings */
    if (cfg->kwin_config_path && g_file_test(cfg->kwin_config_path, G_FILE_TEST_EXISTS)) {
        gchar *contents = NULL;
        gsize len = 0;
        GError *error = NULL;

        if (!g_file_get_contents(cfg->kwin_config_path, &contents, &len, &error)) {
            if (error) {
                gboolean not_found = g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
                g_error_free(error);
                if (!not_found)
                    return -1;
            }
        } else {
            int res = parse_kwin_auto_brightness_json(contents, len);
            g_free(contents);
            if (res >= 0)
                return res;
        }
    }

    /* 3. In GNOME / Cinnamon sessions with ambient-enabled GSettings */
    if (cfg->de_power_settings) {
        return g_settings_get_boolean(cfg->de_power_settings, "ambient-enabled") ? 1 : 0;
    }

    /* 4. Fallback: if user explicitly configured enabled=true in config file */
    if (user_enabled == 1)
        return 1;

    /* 5. Default fallback: enabled */
    return 1;
}

static void reload_min_brightness_setting(ConfigManager *cfg)
{
    if (cfg->min_brightness_cli_set)
        return;

    int user_enabled = -2;
    double cfg_min = -1.0;

    parse_config_file(cfg->sys_config_path, &user_enabled, &cfg_min);
    parse_config_file(cfg->user_config_path, &user_enabled, &cfg_min);

    if (cfg_min >= 0.0) {
        cfg->min_brightness_pct = cfg_min;
    }
}

static gboolean on_config_reload_timeout(gpointer user_data)
{
    ConfigManager *cfg = (ConfigManager *)user_data;
    cfg->reload_timer_id = 0;

    double old_min = cfg->min_brightness_pct;
    gboolean old_enabled = cfg->auto_brightness_enabled;

    reload_min_brightness_setting(cfg);

    int state = evaluate_auto_brightness_enabled(cfg);
    if (state < 0) {
        cfg->reload_timer_id = g_timeout_add(50, on_config_reload_timeout, cfg);
        return G_SOURCE_REMOVE;
    }

    gboolean new_enabled = (state == 1);
    gboolean changed = (new_enabled != old_enabled) ||
                       (fabs(cfg->min_brightness_pct - old_min) > 0.01);
    cfg->auto_brightness_enabled = new_enabled;

    if (changed && cfg->on_changed) {
        cfg->on_changed(new_enabled, cfg->min_brightness_pct, cfg->user_data);
    }

    return G_SOURCE_REMOVE;
}

static void on_config_file_changed(GFileMonitor *monitor,
                                   GFile *file,
                                   GFile *other_file,
                                   GFileMonitorEvent event_type,
                                   gpointer user_data)
{
    (void)monitor;
    (void)file;
    (void)other_file;
    ConfigManager *cfg = (ConfigManager *)user_data;

    if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
        event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
        event_type == G_FILE_MONITOR_EVENT_CREATED ||
        event_type == G_FILE_MONITOR_EVENT_DELETED ||
        event_type == G_FILE_MONITOR_EVENT_MOVED_IN ||
        event_type == G_FILE_MONITOR_EVENT_RENAMED) {
        if (cfg->reload_timer_id != 0)
            g_source_remove(cfg->reload_timer_id);
        cfg->reload_timer_id = g_timeout_add(80, on_config_reload_timeout, cfg);
    }
}

static void on_gsettings_ambient_changed(GSettings *settings,
                                         const gchar *key,
                                         gpointer user_data)
{
    (void)settings;
    (void)key;
    ConfigManager *cfg = (ConfigManager *)user_data;

    int state = evaluate_auto_brightness_enabled(cfg);
    if (state >= 0) {
        cfg->auto_brightness_enabled = (state == 1);
        if (cfg->on_changed)
            cfg->on_changed(cfg->auto_brightness_enabled, cfg->min_brightness_pct, cfg->user_data);
    }
}

static void init_de_gsettings_monitor(ConfigManager *cfg)
{
    if (cfg->is_kde_session)
        return;

    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source)
        return;

    const gchar *xdg = g_getenv("XDG_CURRENT_DESKTOP");
    const char *schemas[] = {
        (xdg && (g_strstr_len(xdg, -1, "Cinnamon") || g_strstr_len(xdg, -1, "cinnamon") ||
                 g_strstr_len(xdg, -1, "X-Cinnamon")))
            ? "org.cinnamon.settings-daemon.plugins.power"
            : "org.gnome.settings-daemon.plugins.power",
        "org.gnome.settings-daemon.plugins.power",
        "org.cinnamon.settings-daemon.plugins.power"
    };

    for (size_t i = 0; i < G_N_ELEMENTS(schemas); i++) {
        GSettingsSchema *schema = g_settings_schema_source_lookup(source, schemas[i], TRUE);
        if (schema) {
            if (g_settings_schema_has_key(schema, "ambient-enabled")) {
                cfg->de_power_settings = g_settings_new(schemas[i]);
                g_signal_connect(cfg->de_power_settings,
                                 "changed::ambient-enabled",
                                 G_CALLBACK(on_gsettings_ambient_changed),
                                 cfg);
                g_settings_schema_unref(schema);
                break;
            }
            g_settings_schema_unref(schema);
        }
    }
}

void config_mgr_init(ConfigManager *cfg,
                     double initial_min_brightness,
                     gboolean cli_set,
                     ConfigChangedCb on_changed,
                     gpointer user_data)
{
    memset(cfg, 0, sizeof(ConfigManager));
    cfg->on_changed = on_changed;
    cfg->user_data = user_data;
    cfg->min_brightness_pct = initial_min_brightness;
    cfg->min_brightness_cli_set = cli_set;
    cfg->is_kde_session = detect_kde_environment();

    cfg->kwin_config_path = g_build_filename(g_get_user_config_dir(), "kwinoutputconfig.json", NULL);
    cfg->user_config_path = g_build_filename(g_get_user_config_dir(), "galaxybook-autobrightness.conf", NULL);
    cfg->sys_config_path = g_strdup("/etc/galaxybook-autobrightness.conf");

    init_de_gsettings_monitor(cfg);
    reload_min_brightness_setting(cfg);

    int initial = evaluate_auto_brightness_enabled(cfg);
    cfg->auto_brightness_enabled = (initial != 0);

    GFile *kwin_file = g_file_new_for_path(cfg->kwin_config_path);
    cfg->kwin_config_monitor = g_file_monitor_file(kwin_file, G_FILE_MONITOR_NONE, NULL, NULL);
    if (cfg->kwin_config_monitor) {
        g_signal_connect(cfg->kwin_config_monitor, "changed", G_CALLBACK(on_config_file_changed), cfg);
    }
    g_object_unref(kwin_file);

    GFile *user_file = g_file_new_for_path(cfg->user_config_path);
    cfg->user_config_monitor = g_file_monitor_file(user_file, G_FILE_MONITOR_NONE, NULL, NULL);
    if (cfg->user_config_monitor) {
        g_signal_connect(cfg->user_config_monitor, "changed", G_CALLBACK(on_config_file_changed), cfg);
    }
    g_object_unref(user_file);

    GFile *sys_file = g_file_new_for_path(cfg->sys_config_path);
    cfg->sys_config_monitor = g_file_monitor_file(sys_file, G_FILE_MONITOR_NONE, NULL, NULL);
    if (cfg->sys_config_monitor) {
        g_signal_connect(cfg->sys_config_monitor, "changed", G_CALLBACK(on_config_file_changed), cfg);
    }
    g_object_unref(sys_file);
}

void config_mgr_cleanup(ConfigManager *cfg)
{
    if (cfg->reload_timer_id != 0) {
        g_source_remove(cfg->reload_timer_id);
        cfg->reload_timer_id = 0;
    }

    if (cfg->kwin_config_monitor) {
        g_file_monitor_cancel(cfg->kwin_config_monitor);
        g_object_unref(cfg->kwin_config_monitor);
        cfg->kwin_config_monitor = NULL;
    }

    if (cfg->user_config_monitor) {
        g_file_monitor_cancel(cfg->user_config_monitor);
        g_object_unref(cfg->user_config_monitor);
        cfg->user_config_monitor = NULL;
    }

    if (cfg->sys_config_monitor) {
        g_file_monitor_cancel(cfg->sys_config_monitor);
        g_object_unref(cfg->sys_config_monitor);
        cfg->sys_config_monitor = NULL;
    }

    if (cfg->de_power_settings) {
        g_object_unref(cfg->de_power_settings);
        cfg->de_power_settings = NULL;
    }

    g_free(cfg->kwin_config_path);
    g_free(cfg->user_config_path);
    g_free(cfg->sys_config_path);
}

