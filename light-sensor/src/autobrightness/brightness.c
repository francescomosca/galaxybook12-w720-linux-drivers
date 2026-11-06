/* SPDX-License-Identifier: GPL-2.0-only */
#include "brightness.h"

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

static void discover_sysfs_backlight(BrightnessManager *mgr)
{
    g_free(mgr->sysfs_backlight_name);
    g_free(mgr->sysfs_backlight_dir);
    mgr->sysfs_backlight_name = NULL;
    mgr->sysfs_backlight_dir = NULL;

    if (g_file_test("/sys/class/backlight/intel_backlight/max_brightness", G_FILE_TEST_EXISTS)) {
        mgr->sysfs_backlight_name = g_strdup("intel_backlight");
        mgr->sysfs_backlight_dir = g_strdup("/sys/class/backlight/intel_backlight");
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
            mgr->sysfs_backlight_name = g_strdup(entry);
            mgr->sysfs_backlight_dir = candidate;
            g_free(max_file);
            break;
        }
        g_free(max_file);
        g_free(candidate);
    }
    g_dir_close(dir);
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

static gboolean discover_kde_internal_display(BrightnessManager *mgr)
{
    g_free(mgr->internal_display_name);
    g_free(mgr->internal_display_path);
    mgr->internal_display_name = NULL;
    mgr->internal_display_path = NULL;

    if (!mgr->session_bus)
        return FALSE;

    GVariant *res = g_dbus_connection_call_sync(
        mgr->session_bus,
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
            if (get_dbus_property_bool(mgr->session_bus,
                                       SCREEN_BUS_NAME,
                                       path,
                                       SCREEN_DISP_INTERFACE,
                                       "IsInternal",
                                       &is_internal) && is_internal) {
                mgr->internal_display_name = g_strdup(disp_name);
                mgr->internal_display_path = path;
                break;
            }
            g_free(path);
        }
    }
    g_variant_unref(inner);

    return (mgr->internal_display_path != NULL);
}

void brightness_mgr_select_backend(BrightnessManager *mgr)
{
    if (mgr->kde_screen_bus_present) {
        mgr->backend = BACKEND_KDE_PER_DISPLAY;
        discover_kde_internal_display(mgr);
    } else if (mgr->kde_power_bus_present) {
        mgr->backend = BACKEND_KDE_LEGACY;
    } else {
        mgr->backend = BACKEND_SYSFS_LOGIND;
        if (!mgr->sysfs_backlight_name)
            discover_sysfs_backlight(mgr);
    }

    if (brightness_mgr_is_ready(mgr)) {
        brightness_mgr_sync(mgr, TRUE);
    }
}

gboolean brightness_mgr_is_ready(const BrightnessManager *mgr)
{
    if (mgr->backend == BACKEND_KDE_PER_DISPLAY)
        return (mgr->internal_display_path != NULL);
    if (mgr->backend == BACKEND_KDE_LEGACY)
        return mgr->kde_power_bus_present;
    if (mgr->backend == BACKEND_SYSFS_LOGIND)
        return (mgr->sysfs_backlight_name != NULL);
    return FALSE;
}

void brightness_mgr_sync(BrightnessManager *mgr, gboolean reset_target)
{
    (void)reset_target;

    if (mgr->backend == BACKEND_KDE_PER_DISPLAY) {
        if (!mgr->session_bus || !mgr->internal_display_path)
            return;

        gint32 max_val = 0;
        if (get_dbus_property_int32(mgr->session_bus,
                                    SCREEN_BUS_NAME,
                                    mgr->internal_display_path,
                                    SCREEN_DISP_INTERFACE,
                                    "MaxBrightness",
                                    &max_val) && max_val > 0) {
            mgr->brightness_max = max_val;
        }
        if (mgr->brightness_max <= 0)
            mgr->brightness_max = 10000;

        gint32 cur_val = 0;
        if (get_dbus_property_int32(mgr->session_bus,
                                    SCREEN_BUS_NAME,
                                    mgr->internal_display_path,
                                    SCREEN_DISP_INTERFACE,
                                    "Brightness",
                                    &cur_val)) {
            mgr->current_pct = ((double)cur_val / (double)mgr->brightness_max) * 100.0;
        }
    } else if (mgr->backend == BACKEND_KDE_LEGACY) {
        if (!mgr->session_bus)
            return;

        GVariant *res_max = g_dbus_connection_call_sync(
            mgr->session_bus,
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
            g_variant_get(res_max, "(i)", &mgr->brightness_max);
            g_variant_unref(res_max);
        }
        if (mgr->brightness_max <= 0)
            mgr->brightness_max = 10000;

        GVariant *res_cur = g_dbus_connection_call_sync(
            mgr->session_bus,
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
            mgr->current_pct = ((double)cur_val / (double)mgr->brightness_max) * 100.0;
        }
    } else if (mgr->backend == BACKEND_SYSFS_LOGIND) {
        if (!mgr->sysfs_backlight_dir)
            return;

        gint32 max_val = 0;
        if (read_sysfs_int(mgr->sysfs_backlight_dir, "max_brightness", &max_val) && max_val > 0)
            mgr->brightness_max = max_val;
        if (mgr->brightness_max <= 0)
            mgr->brightness_max = 10000;

        gint32 cur_val = 0;
        if (read_sysfs_int(mgr->sysfs_backlight_dir, "brightness", &cur_val)) {
            mgr->current_pct = ((double)cur_val / (double)mgr->brightness_max) * 100.0;
        }
    }
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

    BrightnessManager *mgr = (BrightnessManager *)g_object_get_data(source_object, "mgr_ptr");
    if (mgr && mgr->system_bus && mgr->sysfs_backlight_name) {
        g_dbus_connection_call(
            mgr->system_bus,
            LOGIN1_BUS_NAME,
            LOGIN1_SESSION_SELF,
            LOGIN1_SESSION_IFACE,
            "SetBrightness",
            g_variant_new("(ssu)", "backlight", mgr->sysfs_backlight_name, val),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            500,
            NULL,
            on_dbus_call_done,
            NULL
        );
    }
}

void brightness_mgr_apply(BrightnessManager *mgr, double pct)
{
    gint32 max_val = (mgr->brightness_max > 0) ? mgr->brightness_max : 10000;
    gint32 val = (gint32)round((pct / 100.0) * max_val);
    if (val < 1 && max_val > 0)
        val = 1;
    if (val > max_val)
        val = max_val;

    if (mgr->backend == BACKEND_KDE_PER_DISPLAY) {
        if (!mgr->session_bus || !mgr->internal_display_path)
            return;

        g_dbus_connection_call(
            mgr->session_bus,
            SCREEN_BUS_NAME,
            mgr->internal_display_path,
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
    } else if (mgr->backend == BACKEND_KDE_LEGACY) {
        if (!mgr->session_bus)
            return;

        g_dbus_connection_call(
            mgr->session_bus,
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
    } else if (mgr->backend == BACKEND_SYSFS_LOGIND) {
        if (!mgr->system_bus || !mgr->sysfs_backlight_name)
            return;

        g_object_set_data(G_OBJECT(mgr->system_bus), "mgr_ptr", mgr);

        g_dbus_connection_call(
            mgr->system_bus,
            LOGIN1_BUS_NAME,
            LOGIN1_SESSION_AUTO,
            LOGIN1_SESSION_IFACE,
            "SetBrightness",
            g_variant_new("(ssu)", "backlight", mgr->sysfs_backlight_name, (guint32)val),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            500,
            NULL,
            on_logind_auto_done,
            GUINT_TO_POINTER((guint32)val)
        );
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
    BrightnessManager *mgr = (BrightnessManager *)user_data;

    brightness_mgr_select_backend(mgr);
    if (mgr->on_display_changed)
        mgr->on_display_changed(mgr->user_data);
}

static void on_screen_brightness_appeared(GDBusConnection *connection,
                                          const gchar *name,
                                          const gchar *name_owner,
                                          gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)name_owner;
    BrightnessManager *mgr = (BrightnessManager *)user_data;

    mgr->kde_screen_bus_present = TRUE;
    brightness_mgr_select_backend(mgr);
    if (mgr->on_display_changed)
        mgr->on_display_changed(mgr->user_data);
}

static void on_screen_brightness_vanished(GDBusConnection *connection,
                                          const gchar *name,
                                          gpointer user_data)
{
    (void)connection;
    (void)name;
    BrightnessManager *mgr = (BrightnessManager *)user_data;

    mgr->kde_screen_bus_present = FALSE;
    g_free(mgr->internal_display_name);
    g_free(mgr->internal_display_path);
    mgr->internal_display_name = NULL;
    mgr->internal_display_path = NULL;
    brightness_mgr_select_backend(mgr);
}

static void on_kde5_power_appeared(GDBusConnection *connection,
                                   const gchar *name,
                                   const gchar *name_owner,
                                   gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)name_owner;
    BrightnessManager *mgr = (BrightnessManager *)user_data;

    mgr->kde_power_bus_present = TRUE;
    brightness_mgr_select_backend(mgr);
    if (mgr->on_display_changed)
        mgr->on_display_changed(mgr->user_data);
}

static void on_kde5_power_vanished(GDBusConnection *connection,
                                   const gchar *name,
                                   gpointer user_data)
{
    (void)connection;
    (void)name;
    BrightnessManager *mgr = (BrightnessManager *)user_data;

    mgr->kde_power_bus_present = FALSE;
    brightness_mgr_select_backend(mgr);
}

void brightness_mgr_init(BrightnessManager *mgr,
                         GDBusConnection *system_bus,
                         GDBusConnection *session_bus,
                         void (*on_display_changed)(gpointer user_data),
                         gpointer user_data)
{
    memset(mgr, 0, sizeof(BrightnessManager));
    mgr->system_bus = system_bus;
    mgr->session_bus = session_bus;
    mgr->on_display_changed = on_display_changed;
    mgr->user_data = user_data;
    mgr->brightness_max = 10000;
    mgr->current_pct = 30.0;

    discover_sysfs_backlight(mgr);

    if (mgr->session_bus) {
        mgr->screen_added_sub_id = g_dbus_connection_signal_subscribe(
            mgr->session_bus,
            SCREEN_BUS_NAME,
            SCREEN_MGR_INTERFACE,
            "DisplayAdded",
            SCREEN_MGR_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_screen_display_changed,
            mgr,
            NULL
        );

        mgr->screen_removed_sub_id = g_dbus_connection_signal_subscribe(
            mgr->session_bus,
            SCREEN_BUS_NAME,
            SCREEN_MGR_INTERFACE,
            "DisplayRemoved",
            SCREEN_MGR_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_screen_display_changed,
            mgr,
            NULL
        );

        mgr->screen_range_sub_id = g_dbus_connection_signal_subscribe(
            mgr->session_bus,
            SCREEN_BUS_NAME,
            SCREEN_MGR_INTERFACE,
            "BrightnessRangeChanged",
            SCREEN_MGR_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_screen_display_changed,
            mgr,
            NULL
        );

        mgr->screen_watch_id = g_bus_watch_name_on_connection(
            mgr->session_bus,
            SCREEN_BUS_NAME,
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            on_screen_brightness_appeared,
            on_screen_brightness_vanished,
            mgr,
            NULL
        );

        mgr->kde5_watch_id = g_bus_watch_name_on_connection(
            mgr->session_bus,
            KDE5_POWER_BUS_NAME,
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            on_kde5_power_appeared,
            on_kde5_power_vanished,
            mgr,
            NULL
        );
    }

    brightness_mgr_select_backend(mgr);
}

void brightness_mgr_cleanup(BrightnessManager *mgr)
{
    if (mgr->screen_watch_id != 0)
        g_bus_unwatch_name(mgr->screen_watch_id);
    if (mgr->kde5_watch_id != 0)
        g_bus_unwatch_name(mgr->kde5_watch_id);

    if (mgr->session_bus) {
        if (mgr->screen_added_sub_id != 0)
            g_dbus_connection_signal_unsubscribe(mgr->session_bus, mgr->screen_added_sub_id);
        if (mgr->screen_removed_sub_id != 0)
            g_dbus_connection_signal_unsubscribe(mgr->session_bus, mgr->screen_removed_sub_id);
        if (mgr->screen_range_sub_id != 0)
            g_dbus_connection_signal_unsubscribe(mgr->session_bus, mgr->screen_range_sub_id);
    }

    g_free(mgr->internal_display_name);
    g_free(mgr->internal_display_path);
    g_free(mgr->sysfs_backlight_name);
    g_free(mgr->sysfs_backlight_dir);
}

