/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Samsung Galaxy Book 12 (SM-W720) Ambient Light Sensor & Auto-Brightness Daemon
 * Multi-Desktop & Multi-Distro Native C Implementation using GIO / GLib.
 *
 * Supported Brightness Control Backends (auto-detected at runtime):
 *  1. KDE Plasma 6 (`org.kde.ScreenBrightness.Display`):
 *     Targets strictly the built-in panel (`IsInternal == true`) with
 *     `SetBrightness(val, SuppressIndicator=1)`. External monitors are never touched.
 *  2. KDE Plasma 5 (`org.kde.Solid.PowerManagement.Actions.BrightnessControl`):
 *     Fallback when `org.kde.Solid.PowerManagement` is active but the Plasma 6
 *     per-display interface is absent (e.g. Debian 12 / Ubuntu LTS with Plasma 5).
 *  3. Universal Linux (`systemd-logind` + `/sys/class/backlight/`):
 *     Works across GNOME, XFCE, Cinnamon, MATE, LXQt, Cosmic, Sway, Hyprland, i3, etc.
 *     Reads range from `/sys/class/backlight/intel_backlight` and applies brightness
 *     via `org.freedesktop.login1.Session.SetBrightness("backlight", dev, val)` on
 *     the system bus (strictly internal backlight, silent, zero OSD popups).
 *
 * Supported "Automatic Brightness" Setting Sources (monitored live):
 *  - KDE Plasma (KWin): `~/.config/kwinoutputconfig.json` (`"automaticBrightness"` on `eDP*`)
 *  - GNOME / Budgie: GSettings `org.gnome.settings-daemon.plugins.power` (`ambient-enabled`)
 *  - Cinnamon: GSettings `org.cinnamon.settings-daemon.plugins.power` (`ambient-enabled`)
 *  - Universal config override: `~/.config/galaxybook-autobrightness.conf` / `/etc/galaxybook-autobrightness.conf`
 *    (`enabled=true|false`, `min_brightness=<0-100>`)
 */

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

/* KDE Plasma 5 legacy interface (used only when Plasma 6 interface is absent) */
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

/*
 * Base calibrated photometric curve for Samsung AMOLED panel.
 * Lower boundary at 0 lux is 0.0% (clamped at runtime by configurable min_brightness_pct).
 */
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

typedef struct {
    GMainLoop *loop;
    GDBusConnection *system_bus;
    GDBusConnection *session_bus;

    /* Sensor state */
    guint sensor_watch_id;
    guint sensor_sub_id;
    gboolean sensor_present;
    gboolean claimed;

    /* Brightness backend state */
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

    /* Config monitors and settings */
    gboolean is_kde_session;
    gchar *kwin_config_path;
    GFileMonitor *kwin_config_monitor;
    gchar *user_config_path;
    GFileMonitor *user_config_monitor;
    gchar *sys_config_path;
    GFileMonitor *sys_config_monitor;
    guint config_reload_timer_id;
    GSettings *de_power_settings;
    gboolean auto_brightness_enabled;
    double min_brightness_pct;
    gboolean min_brightness_cli_set;

    /* Smooth transition state */
    guint timer_id;
    gint32 brightness_max;
    double current_lux;
    double target_pct;
    double current_pct;
    double step_rate_pct;
    guint step_interval_ms;
} DaemonContext;

static DaemonContext ctx;

static void select_best_backend(void);
static void claim_sensor(void);
static void release_sensor(void);
static void fetch_initial_light_level(void);
static void reload_config_settings(void);
static void start_transition(void);

static double lux_to_target_pct(double lux)
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

    if (pct < ctx.min_brightness_pct)
        pct = ctx.min_brightness_pct;
    if (pct > 100.0)
        pct = 100.0;

    return pct;
}

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
    return FALSE;
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

static int parse_kwin_auto_brightness_json(const char *json, gsize len)
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
                internal_enabled = stack[depth].has_auto_br ? stack[depth].auto_br_val : FALSE;
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

    if (found_internal)
        return internal_enabled ? 1 : 0;

    return 1;
}

static int parse_config_file(const gchar *path, int *out_enabled, double *out_min_brightness)
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

static void reload_config_settings(void)
{
    int user_enabled = -2;
    double cfg_min = -1.0;

    parse_config_file(ctx.sys_config_path, &user_enabled, &cfg_min);
    parse_config_file(ctx.user_config_path, &user_enabled, &cfg_min);

    if (!ctx.min_brightness_cli_set && cfg_min >= 0.0) {
        if (fabs(ctx.min_brightness_pct - cfg_min) > 0.01) {
            ctx.min_brightness_pct = cfg_min;
            if (ctx.claimed && ctx.auto_brightness_enabled) {
                double new_target = lux_to_target_pct(ctx.current_lux);
                if (fabs(new_target - ctx.target_pct) >= 0.5) {
                    ctx.target_pct = new_target;
                    start_transition();
                }
            }
        }
    }
}

static int evaluate_auto_brightness_enabled(void)
{
    int user_enabled = -2;
    parse_config_file(ctx.sys_config_path, &user_enabled, NULL);
    parse_config_file(ctx.user_config_path, &user_enabled, NULL);
    if (user_enabled == 0 || user_enabled == 1)
        return user_enabled;

    if ((ctx.is_kde_session || ctx.kde_screen_bus_present || ctx.kde_power_bus_present) &&
        ctx.kwin_config_path) {
        gchar *contents = NULL;
        gsize len = 0;
        GError *error = NULL;

        if (!g_file_get_contents(ctx.kwin_config_path, &contents, &len, &error)) {
            if (error) {
                gboolean not_found = g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT);
                g_error_free(error);
                if (!not_found)
                    return -1;
            }
        } else {
            int res = parse_kwin_auto_brightness_json(contents, len);
            g_free(contents);
            return res;
        }
    }

    if (ctx.de_power_settings) {
        return g_settings_get_boolean(ctx.de_power_settings, "ambient-enabled") ? 1 : 0;
    }

    return 1;
}

static gboolean read_sysfs_int(const gchar *dir, const gchar *file, gint32 *out_val)
{
    if (!dir)
        return FALSE;

    gchar *path = g_build_filename(dir, file, NULL);
    gchar *contents = NULL;
    gboolean ok = FALSE;

    if (g_file_get_contents(path, &contents, NULL, NULL) && contents) {
        *out_val = (gint32)strtol(contents, NULL, 10);
        ok = TRUE;
    }
    g_free(contents);
    g_free(path);
    return ok;
}

static void discover_sysfs_backlight(void)
{
    g_free(ctx.sysfs_backlight_name);
    g_free(ctx.sysfs_backlight_dir);
    ctx.sysfs_backlight_name = NULL;
    ctx.sysfs_backlight_dir = NULL;

    if (g_file_test("/sys/class/backlight/intel_backlight/max_brightness", G_FILE_TEST_EXISTS)) {
        ctx.sysfs_backlight_name = g_strdup("intel_backlight");
        ctx.sysfs_backlight_dir = g_strdup("/sys/class/backlight/intel_backlight");
        return;
    }

    GDir *dir = g_dir_open("/sys/class/backlight", 0, NULL);
    if (!dir)
        return;

    const gchar *entry = NULL;
    while ((entry = g_dir_read_name(dir)) != NULL) {
        if (*entry == '.')
            continue;
        gchar *candidate = g_build_filename("/sys/class/backlight", entry, NULL);
        gchar *max_file = g_build_filename(candidate, "max_brightness", NULL);
        if (g_file_test(max_file, G_FILE_TEST_EXISTS)) {
            ctx.sysfs_backlight_name = g_strdup(entry);
            ctx.sysfs_backlight_dir = candidate;
            g_free(max_file);
            break;
        }
        g_free(max_file);
        g_free(candidate);
    }
    g_dir_close(dir);
}

static void on_dbus_call_done(GObject *source_object,
                              GAsyncResult *res,
                              gpointer user_data)
{
    (void)user_data;
    GError *error = NULL;
    GVariant *ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source_object), res, &error);
    if (error) {
        g_error_free(error);
    } else if (ret) {
        g_variant_unref(ret);
    }
}

static void on_logind_auto_done(GObject *source_object,
                                GAsyncResult *res,
                                gpointer user_data)
{
    guint32 val = GPOINTER_TO_UINT(user_data);
    GError *error = NULL;
    GVariant *ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source_object), res, &error);
    if (!error) {
        if (ret)
            g_variant_unref(ret);
        return;
    }
    g_error_free(error);

    if (ctx.system_bus && ctx.sysfs_backlight_name) {
        g_dbus_connection_call(
            ctx.system_bus,
            LOGIN1_BUS_NAME,
            LOGIN1_SESSION_SELF,
            LOGIN1_SESSION_IFACE,
            "SetBrightness",
            g_variant_new("(ssu)", "backlight", ctx.sysfs_backlight_name, val),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            500,
            NULL,
            on_dbus_call_done,
            NULL
        );
    }
}

static void apply_brightness_silent(double pct)
{
    if (!ctx.auto_brightness_enabled)
        return;

    gint32 max_val = (ctx.brightness_max > 0) ? ctx.brightness_max : 10000;
    gint32 val = (gint32)round((pct / 100.0) * max_val);
    if (val < 1 && max_val > 0)
        val = 1;
    if (val > max_val)
        val = max_val;

    if (ctx.backend == BACKEND_KDE_PER_DISPLAY) {
        if (!ctx.session_bus || !ctx.internal_display_path)
            return;

        g_dbus_connection_call(
            ctx.session_bus,
            SCREEN_BUS_NAME,
            ctx.internal_display_path,
            SCREEN_DISP_INTERFACE,
            "SetBrightness",
            g_variant_new("(iu)", val, SUPPRESS_INDICATOR_FLAG),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            500,
            NULL,
            on_dbus_call_done,
            NULL
        );
    } else if (ctx.backend == BACKEND_KDE_LEGACY) {
        if (!ctx.session_bus)
            return;

        g_dbus_connection_call(
            ctx.session_bus,
            KDE5_POWER_BUS_NAME,
            KDE5_BRIGHTNESS_PATH,
            KDE5_BRIGHTNESS_IFACE,
            "setBrightnessSilent",
            g_variant_new("(i)", val),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            500,
            NULL,
            on_dbus_call_done,
            NULL
        );
    } else if (ctx.backend == BACKEND_SYSFS_LOGIND) {
        if (!ctx.system_bus || !ctx.sysfs_backlight_name)
            return;

        g_dbus_connection_call(
            ctx.system_bus,
            LOGIN1_BUS_NAME,
            LOGIN1_SESSION_AUTO,
            LOGIN1_SESSION_IFACE,
            "SetBrightness",
            g_variant_new("(ssu)", "backlight", ctx.sysfs_backlight_name, (guint32)val),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            500,
            NULL,
            on_logind_auto_done,
            GUINT_TO_POINTER((guint32)val)
        );
    }
}

static void stop_transition(void)
{
    if (ctx.timer_id != 0) {
        g_source_remove(ctx.timer_id);
        ctx.timer_id = 0;
    }
}

static gboolean is_backend_ready(void)
{
    if (ctx.backend == BACKEND_KDE_PER_DISPLAY)
        return (ctx.internal_display_path != NULL);
    if (ctx.backend == BACKEND_KDE_LEGACY)
        return ctx.kde_power_bus_present;
    if (ctx.backend == BACKEND_SYSFS_LOGIND)
        return (ctx.sysfs_backlight_name != NULL);
    return FALSE;
}

static gboolean on_transition_step(gpointer user_data)
{
    (void)user_data;

    if (!ctx.auto_brightness_enabled || !is_backend_ready()) {
        ctx.timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    double diff = ctx.target_pct - ctx.current_pct;

    if (fabs(diff) <= ctx.step_rate_pct) {
        ctx.current_pct = ctx.target_pct;
        apply_brightness_silent(ctx.current_pct);
        ctx.timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    if (diff > 0.0)
        ctx.current_pct += ctx.step_rate_pct;
    else
        ctx.current_pct -= ctx.step_rate_pct;

    apply_brightness_silent(ctx.current_pct);
    return G_SOURCE_CONTINUE;
}

static void start_transition(void)
{
    if (!ctx.auto_brightness_enabled || !is_backend_ready())
        return;

    if (ctx.timer_id == 0) {
        ctx.timer_id = g_timeout_add(ctx.step_interval_ms, on_transition_step, NULL);
    }
}

static gboolean get_dbus_property_bool(GDBusConnection *bus,
                                       const gchar *bus_name,
                                       const gchar *object_path,
                                       const gchar *iface,
                                       const gchar *prop_name,
                                       gboolean *out_val)
{
    GVariant *res = g_dbus_connection_call_sync(
        bus,
        bus_name,
        object_path,
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", iface, prop_name),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        500,
        NULL,
        NULL
    );
    if (!res)
        return FALSE;

    GVariant *inner = NULL;
    g_variant_get(res, "(v)", &inner);
    g_variant_unref(res);
    if (!inner)
        return FALSE;

    gboolean ok = FALSE;
    if (g_variant_is_of_type(inner, G_VARIANT_TYPE_BOOLEAN)) {
        *out_val = g_variant_get_boolean(inner);
        ok = TRUE;
    }
    g_variant_unref(inner);
    return ok;
}

static gboolean get_dbus_property_int32(GDBusConnection *bus,
                                        const gchar *bus_name,
                                        const gchar *object_path,
                                        const gchar *iface,
                                        const gchar *prop_name,
                                        gint32 *out_val)
{
    GVariant *res = g_dbus_connection_call_sync(
        bus,
        bus_name,
        object_path,
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", iface, prop_name),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        500,
        NULL,
        NULL
    );
    if (!res)
        return FALSE;

    GVariant *inner = NULL;
    g_variant_get(res, "(v)", &inner);
    g_variant_unref(res);
    if (!inner)
        return FALSE;

    gboolean ok = FALSE;
    if (g_variant_is_of_type(inner, G_VARIANT_TYPE_INT32)) {
        *out_val = g_variant_get_int32(inner);
        ok = TRUE;
    }
    g_variant_unref(inner);
    return ok;
}

static void sync_current_brightness(gboolean reset_target)
{
    if (ctx.backend == BACKEND_KDE_PER_DISPLAY) {
        if (!ctx.session_bus || !ctx.internal_display_path)
            return;

        gint32 max_val = 0;
        if (get_dbus_property_int32(ctx.session_bus,
                                    SCREEN_BUS_NAME,
                                    ctx.internal_display_path,
                                    SCREEN_DISP_INTERFACE,
                                    "MaxBrightness",
                                    &max_val) && max_val > 0) {
            ctx.brightness_max = max_val;
        }
        if (ctx.brightness_max <= 0)
            ctx.brightness_max = 10000;

        gint32 cur_val = 0;
        if (get_dbus_property_int32(ctx.session_bus,
                                    SCREEN_BUS_NAME,
                                    ctx.internal_display_path,
                                    SCREEN_DISP_INTERFACE,
                                    "Brightness",
                                    &cur_val)) {
            ctx.current_pct = ((double)cur_val / (double)ctx.brightness_max) * 100.0;
            if (reset_target)
                ctx.target_pct = ctx.current_pct;
        }
    } else if (ctx.backend == BACKEND_KDE_LEGACY) {
        if (!ctx.session_bus)
            return;

        GVariant *res_max = g_dbus_connection_call_sync(
            ctx.session_bus,
            KDE5_POWER_BUS_NAME,
            KDE5_BRIGHTNESS_PATH,
            KDE5_BRIGHTNESS_IFACE,
            "brightnessMax",
            NULL,
            G_VARIANT_TYPE("(i)"),
            G_DBUS_CALL_FLAGS_NONE,
            500,
            NULL,
            NULL
        );
        if (res_max) {
            g_variant_get(res_max, "(i)", &ctx.brightness_max);
            g_variant_unref(res_max);
        }
        if (ctx.brightness_max <= 0)
            ctx.brightness_max = 10000;

        GVariant *res_cur = g_dbus_connection_call_sync(
            ctx.session_bus,
            KDE5_POWER_BUS_NAME,
            KDE5_BRIGHTNESS_PATH,
            KDE5_BRIGHTNESS_IFACE,
            "brightness",
            NULL,
            G_VARIANT_TYPE("(i)"),
            G_DBUS_CALL_FLAGS_NONE,
            500,
            NULL,
            NULL
        );
        if (res_cur) {
            gint32 cur_val = 0;
            g_variant_get(res_cur, "(i)", &cur_val);
            g_variant_unref(res_cur);
            ctx.current_pct = ((double)cur_val / (double)ctx.brightness_max) * 100.0;
            if (reset_target)
                ctx.target_pct = ctx.current_pct;
        }
    } else if (ctx.backend == BACKEND_SYSFS_LOGIND) {
        if (!ctx.sysfs_backlight_dir)
            return;

        gint32 max_val = 0;
        if (read_sysfs_int(ctx.sysfs_backlight_dir, "max_brightness", &max_val) && max_val > 0)
            ctx.brightness_max = max_val;
        if (ctx.brightness_max <= 0)
            ctx.brightness_max = 10000;

        gint32 cur_val = 0;
        if (read_sysfs_int(ctx.sysfs_backlight_dir, "brightness", &cur_val)) {
            ctx.current_pct = ((double)cur_val / (double)ctx.brightness_max) * 100.0;
            if (reset_target)
                ctx.target_pct = ctx.current_pct;
        }
    }
}

static gboolean discover_kde_internal_display(void)
{
    g_free(ctx.internal_display_name);
    g_free(ctx.internal_display_path);
    ctx.internal_display_name = NULL;
    ctx.internal_display_path = NULL;

    if (!ctx.session_bus)
        return FALSE;

    GVariant *res = g_dbus_connection_call_sync(
        ctx.session_bus,
        SCREEN_BUS_NAME,
        SCREEN_MGR_PATH,
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", SCREEN_MGR_INTERFACE, "DisplaysDBusNames"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        NULL,
        NULL
    );

    if (!res)
        return FALSE;

    GVariant *inner = NULL;
    g_variant_get(res, "(v)", &inner);
    g_variant_unref(res);
    if (!inner)
        return FALSE;

    if (g_variant_is_of_type(inner, G_VARIANT_TYPE_STRING_ARRAY)) {
        GVariantIter iter;
        const gchar *disp_name = NULL;
        g_variant_iter_init(&iter, inner);
        while (g_variant_iter_next(&iter, "&s", &disp_name)) {
            if (!disp_name || *disp_name == '\0')
                continue;
            gchar *path = g_strdup_printf("%s/%s", SCREEN_MGR_PATH, disp_name);
            gboolean is_internal = FALSE;
            if (get_dbus_property_bool(ctx.session_bus,
                                       SCREEN_BUS_NAME,
                                       path,
                                       SCREEN_DISP_INTERFACE,
                                       "IsInternal",
                                       &is_internal) && is_internal) {
                ctx.internal_display_name = g_strdup(disp_name);
                ctx.internal_display_path = path;
                break;
            }
            g_free(path);
        }
    }
    g_variant_unref(inner);

    return (ctx.internal_display_path != NULL);
}

static void select_best_backend(void)
{
    if (ctx.kde_screen_bus_present) {
        ctx.backend = BACKEND_KDE_PER_DISPLAY;
        discover_kde_internal_display();
    } else if (ctx.kde_power_bus_present) {
        ctx.backend = BACKEND_KDE_LEGACY;
    } else {
        ctx.backend = BACKEND_SYSFS_LOGIND;
        if (!ctx.sysfs_backlight_name)
            discover_sysfs_backlight();
    }

    if (is_backend_ready()) {
        sync_current_brightness(ctx.timer_id == 0);
    } else {
        stop_transition();
    }
}

static void on_sensor_properties_changed(GDBusConnection *connection,
                                         const gchar *sender_name,
                                         const gchar *object_path,
                                         const gchar *interface_name,
                                         const gchar *signal_name,
                                         GVariant *parameters,
                                         gpointer user_data)
{
    (void)connection;
    (void)sender_name;
    (void)object_path;
    (void)interface_name;
    (void)signal_name;
    (void)user_data;

    if (!ctx.auto_brightness_enabled)
        return;

    const gchar *iface = NULL;
    GVariantIter *changed_props = NULL;
    GVariantIter *invalidated_props = NULL;

    g_variant_get(parameters, "(&sa{sv}as)", &iface, &changed_props, &invalidated_props);
    if (!g_str_equal(iface, SENSOR_INTERFACE)) {
        if (changed_props) g_variant_iter_free(changed_props);
        if (invalidated_props) g_variant_iter_free(invalidated_props);
        return;
    }

    const gchar *key;
    GVariant *val;
    while (g_variant_iter_next(changed_props, "{&sv}", &key, &val)) {
        if (g_str_equal(key, "LightLevel")) {
            double lux = g_variant_get_double(val);
            ctx.current_lux = lux;
            double new_target = lux_to_target_pct(lux);
            if (fabs(new_target - ctx.target_pct) >= 1.0) {
                if (ctx.timer_id == 0)
                    sync_current_brightness(FALSE);
                ctx.target_pct = new_target;
                start_transition();
            }
        }
        g_variant_unref(val);
    }

    if (changed_props) g_variant_iter_free(changed_props);
    if (invalidated_props) g_variant_iter_free(invalidated_props);
}

static void fetch_initial_light_level(void)
{
    if (!ctx.system_bus || !ctx.claimed || !ctx.auto_brightness_enabled)
        return;

    GVariant *res = g_dbus_connection_call_sync(
        ctx.system_bus,
        SENSOR_BUS_NAME,
        SENSOR_OBJECT_PATH,
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", SENSOR_INTERFACE, "LightLevel"),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        NULL,
        NULL
    );

    if (res) {
        GVariant *inner = NULL;
        g_variant_get(res, "(v)", &inner);
        if (inner) {
            if (g_variant_is_of_type(inner, G_VARIANT_TYPE_DOUBLE)) {
                ctx.current_lux = g_variant_get_double(inner);
                sync_current_brightness(FALSE);
                ctx.target_pct = lux_to_target_pct(ctx.current_lux);
                start_transition();
            }
            g_variant_unref(inner);
        }
        g_variant_unref(res);
    }
}

static void claim_sensor(void)
{
    if (!ctx.system_bus || !ctx.sensor_present || ctx.claimed || !ctx.auto_brightness_enabled)
        return;

    GError *error = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        ctx.system_bus,
        SENSOR_BUS_NAME,
        SENSOR_OBJECT_PATH,
        SENSOR_INTERFACE,
        "ClaimLight",
        NULL,
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        2000,
        NULL,
        &error
    );

    if (error) {
        g_printerr("Failed to claim light sensor: %s\n", error->message);
        g_error_free(error);
        return;
    }

    if (res)
        g_variant_unref(res);

    ctx.claimed = TRUE;

    if (ctx.sensor_sub_id == 0) {
        ctx.sensor_sub_id = g_dbus_connection_signal_subscribe(
            ctx.system_bus,
            SENSOR_BUS_NAME,
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            SENSOR_OBJECT_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_sensor_properties_changed,
            NULL,
            NULL
        );
    }

    fetch_initial_light_level();
}

static void release_sensor(void)
{
    if (ctx.sensor_sub_id != 0 && ctx.system_bus) {
        g_dbus_connection_signal_unsubscribe(ctx.system_bus, ctx.sensor_sub_id);
        ctx.sensor_sub_id = 0;
    }

    if (!ctx.claimed || !ctx.system_bus)
        return;

    GVariant *res = g_dbus_connection_call_sync(
        ctx.system_bus,
        SENSOR_BUS_NAME,
        SENSOR_OBJECT_PATH,
        SENSOR_INTERFACE,
        "ReleaseLight",
        NULL,
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        NULL,
        NULL
    );
    if (res)
        g_variant_unref(res);

    ctx.claimed = FALSE;
}

static void on_sensor_proxy_appeared(GDBusConnection *connection,
                                     const gchar *name,
                                     const gchar *name_owner,
                                     gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)name_owner;
    (void)user_data;

    ctx.sensor_present = TRUE;
    if (ctx.auto_brightness_enabled)
        claim_sensor();
}

static void on_sensor_proxy_vanished(GDBusConnection *connection,
                                     const gchar *name,
                                     gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)user_data;

    ctx.sensor_present = FALSE;
    ctx.claimed = FALSE;
    stop_transition();

    if (ctx.sensor_sub_id != 0 && ctx.system_bus) {
        g_dbus_connection_signal_unsubscribe(ctx.system_bus, ctx.sensor_sub_id);
        ctx.sensor_sub_id = 0;
    }
}

static void on_screen_display_changed(GDBusConnection *connection,
                                      const gchar *sender_name,
                                      const gchar *object_path,
                                      const gchar *interface_name,
                                      const gchar *signal_name,
                                      GVariant *parameters,
                                      gpointer user_data)
{
    (void)connection;
    (void)sender_name;
    (void)object_path;
    (void)interface_name;
    (void)signal_name;
    (void)parameters;
    (void)user_data;

    select_best_backend();
    if (ctx.auto_brightness_enabled && ctx.claimed && is_backend_ready())
        fetch_initial_light_level();
}

static void on_screen_brightness_appeared(GDBusConnection *connection,
                                          const gchar *name,
                                          const gchar *name_owner,
                                          gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)name_owner;
    (void)user_data;

    ctx.kde_screen_bus_present = TRUE;
    select_best_backend();
    if (ctx.auto_brightness_enabled && ctx.claimed && is_backend_ready())
        fetch_initial_light_level();
}

static void on_screen_brightness_vanished(GDBusConnection *connection,
                                          const gchar *name,
                                          gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)user_data;

    ctx.kde_screen_bus_present = FALSE;
    g_free(ctx.internal_display_name);
    g_free(ctx.internal_display_path);
    ctx.internal_display_name = NULL;
    ctx.internal_display_path = NULL;
    select_best_backend();
}

static void on_kde5_power_appeared(GDBusConnection *connection,
                                   const gchar *name,
                                   const gchar *name_owner,
                                   gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)name_owner;
    (void)user_data;

    ctx.kde_power_bus_present = TRUE;
    select_best_backend();
    if (ctx.auto_brightness_enabled && ctx.claimed && is_backend_ready())
        fetch_initial_light_level();
}

static void on_kde5_power_vanished(GDBusConnection *connection,
                                   const gchar *name,
                                   gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)user_data;

    ctx.kde_power_bus_present = FALSE;
    select_best_backend();
}

static void apply_auto_brightness_toggle(gboolean enabled)
{
    if (enabled == ctx.auto_brightness_enabled)
        return;

    ctx.auto_brightness_enabled = enabled;
    if (!enabled) {
        stop_transition();
        release_sensor();
    } else {
        select_best_backend();
        claim_sensor();
    }
}

static gboolean on_config_reload_timeout(gpointer user_data)
{
    (void)user_data;
    ctx.config_reload_timer_id = 0;

    reload_config_settings();

    int state = evaluate_auto_brightness_enabled();
    if (state < 0) {
        ctx.config_reload_timer_id = g_timeout_add(50, on_config_reload_timeout, NULL);
        return G_SOURCE_REMOVE;
    }

    apply_auto_brightness_toggle(state == 1);
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
    (void)user_data;

    if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
        event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
        event_type == G_FILE_MONITOR_EVENT_CREATED ||
        event_type == G_FILE_MONITOR_EVENT_DELETED ||
        event_type == G_FILE_MONITOR_EVENT_MOVED_IN ||
        event_type == G_FILE_MONITOR_EVENT_RENAMED) {
        if (ctx.config_reload_timer_id != 0)
            g_source_remove(ctx.config_reload_timer_id);
        ctx.config_reload_timer_id = g_timeout_add(80, on_config_reload_timeout, NULL);
    }
}

static void on_gsettings_ambient_changed(GSettings *settings,
                                         const gchar *key,
                                         gpointer user_data)
{
    (void)settings;
    (void)key;
    (void)user_data;

    int state = evaluate_auto_brightness_enabled();
    if (state >= 0)
        apply_auto_brightness_toggle(state == 1);
}

static void init_de_gsettings_monitor(void)
{
    if (ctx.is_kde_session)
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
                ctx.de_power_settings = g_settings_new(schemas[i]);
                g_signal_connect(ctx.de_power_settings,
                                 "changed::ambient-enabled",
                                 G_CALLBACK(on_gsettings_ambient_changed),
                                 NULL);
                g_settings_schema_unref(schema);
                break;
            }
            g_settings_schema_unref(schema);
        }
    }
}

static void init_config_monitors(void)
{
    ctx.is_kde_session = detect_kde_environment();
    ctx.kwin_config_path = g_build_filename(g_get_user_config_dir(), "kwinoutputconfig.json", NULL);
    ctx.user_config_path = g_build_filename(g_get_user_config_dir(), "galaxybook-autobrightness.conf", NULL);
    ctx.sys_config_path = g_strdup("/etc/galaxybook-autobrightness.conf");

    init_de_gsettings_monitor();
    reload_config_settings();

    int initial = evaluate_auto_brightness_enabled();
    ctx.auto_brightness_enabled = (initial != 0);

    GFile *kwin_file = g_file_new_for_path(ctx.kwin_config_path);
    ctx.kwin_config_monitor = g_file_monitor_file(kwin_file, G_FILE_MONITOR_NONE, NULL, NULL);
    if (ctx.kwin_config_monitor) {
        g_signal_connect(ctx.kwin_config_monitor, "changed", G_CALLBACK(on_config_file_changed), NULL);
    }
    g_object_unref(kwin_file);

    GFile *user_file = g_file_new_for_path(ctx.user_config_path);
    ctx.user_config_monitor = g_file_monitor_file(user_file, G_FILE_MONITOR_NONE, NULL, NULL);
    if (ctx.user_config_monitor) {
        g_signal_connect(ctx.user_config_monitor, "changed", G_CALLBACK(on_config_file_changed), NULL);
    }
    g_object_unref(user_file);

    GFile *sys_file = g_file_new_for_path(ctx.sys_config_path);
    ctx.sys_config_monitor = g_file_monitor_file(sys_file, G_FILE_MONITOR_NONE, NULL, NULL);
    if (ctx.sys_config_monitor) {
        g_signal_connect(ctx.sys_config_monitor, "changed", G_CALLBACK(on_config_file_changed), NULL);
    }
    g_object_unref(sys_file);
}

static gboolean on_unix_signal(gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *)user_data;
    if (loop && g_main_loop_is_running(loop))
        g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv)
{
    /* Default minimum brightness limit: 0.0% (30% clamp removed) */
    ctx.min_brightness_pct = 0.0;
    ctx.min_brightness_cli_set = FALSE;
    ctx.current_pct = 30.0;
    ctx.target_pct = 30.0;
    ctx.step_interval_ms = 50;
    ctx.step_rate_pct = 0.8;
    ctx.brightness_max = 10000;
    ctx.auto_brightness_enabled = TRUE;

    /* Check environment variable override */
    const char *env_min = g_getenv("MIN_BRIGHTNESS");
    if (!env_min)
        env_min = g_getenv("GALAXYBOOK_MIN_BRIGHTNESS");
    if (env_min) {
        char *endptr = NULL;
        double d = g_ascii_strtod(env_min, &endptr);
        if (endptr && endptr != env_min && d >= 0.0 && d <= 100.0) {
            ctx.min_brightness_pct = d;
        }
    }

    /* Parse command-line options */
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--min-brightness") == 0) && i + 1 < argc) {
            char *endptr = NULL;
            double d = g_ascii_strtod(argv[i + 1], &endptr);
            if (endptr && endptr != argv[i + 1] && d >= 0.0 && d <= 100.0) {
                ctx.min_brightness_pct = d;
                ctx.min_brightness_cli_set = TRUE;
            }
            i++;
        } else if (strncmp(argv[i], "--min-brightness=", 17) == 0) {
            const char *val_str = argv[i] + 17;
            char *endptr = NULL;
            double d = g_ascii_strtod(val_str, &endptr);
            if (endptr && endptr != val_str && d >= 0.0 && d <= 100.0) {
                ctx.min_brightness_pct = d;
                ctx.min_brightness_cli_set = TRUE;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Options:\n");
            printf("  -m, --min-brightness <0-100>  Set minimum auto-brightness percentage (default: 0)\n");
            printf("  -h, --help                     Show this help message\n");
            return 0;
        }
    }

    ctx.current_pct = ctx.min_brightness_pct;
    ctx.target_pct = ctx.min_brightness_pct;

    GError *error = NULL;
    ctx.system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error) {
        g_printerr("Failed to connect to system bus: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    ctx.session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (error) {
        g_printerr("Warning: session bus unavailable (%s), using systemd-logind backend\n", error->message);
        g_error_free(error);
        error = NULL;
    }

    discover_sysfs_backlight();
    init_config_monitors();
    select_best_backend();

    ctx.loop = g_main_loop_new(NULL, FALSE);

    /* Safe GLib Unix signal handlers */
    g_unix_signal_add(SIGINT, on_unix_signal, ctx.loop);
    g_unix_signal_add(SIGTERM, on_unix_signal, ctx.loop);

    if (ctx.session_bus) {
        /* Subscribe to KDE Plasma 6 per-display hotplug and range events */
        ctx.screen_added_sub_id = g_dbus_connection_signal_subscribe(
            ctx.session_bus,
            SCREEN_BUS_NAME,
            SCREEN_MGR_INTERFACE,
            "DisplayAdded",
            SCREEN_MGR_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_screen_display_changed,
            NULL,
            NULL
        );

        ctx.screen_removed_sub_id = g_dbus_connection_signal_subscribe(
            ctx.session_bus,
            SCREEN_BUS_NAME,
            SCREEN_MGR_INTERFACE,
            "DisplayRemoved",
            SCREEN_MGR_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_screen_display_changed,
            NULL,
            NULL
        );

        ctx.screen_range_sub_id = g_dbus_connection_signal_subscribe(
            ctx.session_bus,
            SCREEN_BUS_NAME,
            SCREEN_MGR_INTERFACE,
            "BrightnessRangeChanged",
            SCREEN_MGR_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_screen_display_changed,
            NULL,
            NULL
        );

        /* Watch KDE Plasma 6 org.kde.ScreenBrightness lifecycle */
        ctx.screen_watch_id = g_bus_watch_name_on_connection(
            ctx.session_bus,
            SCREEN_BUS_NAME,
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            on_screen_brightness_appeared,
            on_screen_brightness_vanished,
            NULL,
            NULL
        );

        /* Watch KDE Plasma 5 org.kde.Solid.PowerManagement lifecycle */
        ctx.kde5_watch_id = g_bus_watch_name_on_connection(
            ctx.session_bus,
            KDE5_POWER_BUS_NAME,
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            on_kde5_power_appeared,
            on_kde5_power_vanished,
            NULL,
            NULL
        );
    }

    /* Watch net.hadess.SensorProxy lifecycle */
    ctx.sensor_watch_id = g_bus_watch_name_on_connection(
        ctx.system_bus,
        SENSOR_BUS_NAME,
        G_BUS_NAME_WATCHER_FLAGS_NONE,
        on_sensor_proxy_appeared,
        on_sensor_proxy_vanished,
        NULL,
        NULL
    );

    g_main_loop_run(ctx.loop);

    stop_transition();

    if (ctx.config_reload_timer_id != 0) {
        g_source_remove(ctx.config_reload_timer_id);
        ctx.config_reload_timer_id = 0;
    }

    if (ctx.kwin_config_monitor) {
        g_file_monitor_cancel(ctx.kwin_config_monitor);
        g_object_unref(ctx.kwin_config_monitor);
        ctx.kwin_config_monitor = NULL;
    }

    if (ctx.user_config_monitor) {
        g_file_monitor_cancel(ctx.user_config_monitor);
        g_object_unref(ctx.user_config_monitor);
        ctx.user_config_monitor = NULL;
    }

    if (ctx.sys_config_monitor) {
        g_file_monitor_cancel(ctx.sys_config_monitor);
        g_object_unref(ctx.sys_config_monitor);
        ctx.sys_config_monitor = NULL;
    }

    if (ctx.de_power_settings) {
        g_object_unref(ctx.de_power_settings);
        ctx.de_power_settings = NULL;
    }

    if (ctx.sensor_watch_id != 0)
        g_bus_unwatch_name(ctx.sensor_watch_id);
    if (ctx.screen_watch_id != 0)
        g_bus_unwatch_name(ctx.screen_watch_id);
    if (ctx.kde5_watch_id != 0)
        g_bus_unwatch_name(ctx.kde5_watch_id);

    if (ctx.session_bus) {
        if (ctx.screen_added_sub_id != 0)
            g_dbus_connection_signal_unsubscribe(ctx.session_bus, ctx.screen_added_sub_id);
        if (ctx.screen_removed_sub_id != 0)
            g_dbus_connection_signal_unsubscribe(ctx.session_bus, ctx.screen_removed_sub_id);
        if (ctx.screen_range_sub_id != 0)
            g_dbus_connection_signal_unsubscribe(ctx.session_bus, ctx.screen_range_sub_id);
    }

    release_sensor();

    g_free(ctx.internal_display_name);
    g_free(ctx.internal_display_path);
    g_free(ctx.sysfs_backlight_name);
    g_free(ctx.sysfs_backlight_dir);
    g_free(ctx.kwin_config_path);
    g_free(ctx.user_config_path);
    g_free(ctx.sys_config_path);

    if (ctx.system_bus)
        g_object_unref(ctx.system_bus);
    if (ctx.session_bus)
        g_object_unref(ctx.session_bus);
    if (ctx.loop)
        g_main_loop_unref(ctx.loop);

    return 0;
}
