/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Samsung Galaxy Book 12 (SM-W720) Ambient Light Sensor & Auto-Brightness Daemon
 * Native C implementation using GIO D-Bus.
 *
 * - Claims light sensor from net.hadess.SensorProxy (ClaimLight)
 * - Automatically watches net.hadess.SensorProxy presence (re-claims on daemon restart)
 * - Listens for LightLevel updates on D-Bus PropertiesChanged
 * - Applies natural AMOLED photometric curve (lux <= 4 -> 30%)
 * - Smoothly transitions brightness via setBrightnessSilent on KDE PowerDevil
 * - Non-blocking asynchronous D-Bus calls to prevent stuttering
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>

#define SENSOR_BUS_NAME         "net.hadess.SensorProxy"
#define SENSOR_OBJECT_PATH      "/net/hadess/SensorProxy"
#define SENSOR_INTERFACE        "net.hadess.SensorProxy"

#define POWER_BUS_NAME          "org.kde.Solid.PowerManagement"
#define POWER_OBJECT_PATH       "/org/kde/Solid/PowerManagement/Actions/BrightnessControl"
#define POWER_INTERFACE         "org.kde.Solid.PowerManagement.Actions.BrightnessControl"

typedef struct {
    double lux;
    double pct;
} CurvePoint;

static const CurvePoint ALS_CURVE[] = {
    { 0.0,    30.0 },
    { 4.0,    30.0 },
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
    guint sensor_watch_id;
    guint sub_id;
    guint timer_id;
    gboolean claimed;

    gint32 brightness_max;
    double current_lux;
    double target_pct;
    double current_pct;
    double step_rate_pct;
    guint step_interval_ms;
} DaemonContext;

static DaemonContext ctx;

static double lux_to_target_pct(double lux)
{
    if (lux <= ALS_CURVE[0].lux)
        return ALS_CURVE[0].pct;
    if (lux >= ALS_CURVE[ALS_CURVE_SIZE - 1].lux)
        return ALS_CURVE[ALS_CURVE_SIZE - 1].pct;

    for (size_t i = 0; i < ALS_CURVE_SIZE - 1; i++) {
        double x0 = ALS_CURVE[i].lux;
        double y0 = ALS_CURVE[i].pct;
        double x1 = ALS_CURVE[i + 1].lux;
        double y1 = ALS_CURVE[i + 1].pct;

        if (lux >= x0 && lux <= x1) {
            return y0 + (lux - x0) * (y1 - y0) / (x1 - x0);
        }
    }
    return 50.0;
}

static void on_brightness_call_done(GObject *source_object,
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

static void apply_brightness_silent(double pct)
{
    if (!ctx.session_bus)
        return;

    gint32 max_val = (ctx.brightness_max > 0) ? ctx.brightness_max : 10000;
    gint32 val = (gint32)round((pct / 100.0) * max_val);

    g_dbus_connection_call(
        ctx.session_bus,
        POWER_BUS_NAME,
        POWER_OBJECT_PATH,
        POWER_INTERFACE,
        "setBrightnessSilent",
        g_variant_new("(i)", val),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        500,
        NULL,
        on_brightness_call_done,
        NULL
    );
}

static gboolean on_transition_step(gpointer user_data)
{
    (void)user_data;
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
    if (ctx.timer_id == 0) {
        ctx.timer_id = g_timeout_add(ctx.step_interval_ms, on_transition_step, NULL);
    }
}

static void on_properties_changed(GDBusConnection *connection,
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
    if (!ctx.system_bus)
        return;

    GVariant *res = g_dbus_connection_call_sync(
        ctx.system_bus,
        SENSOR_BUS_NAME,
        SENSOR_OBJECT_PATH,
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", SENSOR_INTERFACE, "LightLevel"),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        NULL,
        NULL
    );

    if (res) {
        GVariant *inner = NULL;
        g_variant_get(res, "(v)", &inner);
        if (inner) {
            ctx.current_lux = g_variant_get_double(inner);
            ctx.target_pct = lux_to_target_pct(ctx.current_lux);
            start_transition();
            g_variant_unref(inner);
        }
        g_variant_unref(res);
    }
}

static void on_sensor_proxy_appeared(GDBusConnection *connection,
                                     const gchar *name,
                                     const gchar *name_owner,
                                     gpointer user_data)
{
    (void)name;
    (void)name_owner;
    (void)user_data;

    GError *error = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        connection,
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

    if (ctx.sub_id == 0) {
        ctx.sub_id = g_dbus_connection_signal_subscribe(
            connection,
            SENSOR_BUS_NAME,
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            SENSOR_OBJECT_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_properties_changed,
            NULL,
            NULL
        );
    }

    fetch_initial_light_level();
}

static void on_sensor_proxy_vanished(GDBusConnection *connection,
                                     const gchar *name,
                                     gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)user_data;

    ctx.claimed = FALSE;
    if (ctx.sub_id != 0) {
        g_dbus_connection_signal_unsubscribe(ctx.system_bus, ctx.sub_id);
        ctx.sub_id = 0;
    }
}

static void release_sensor(void)
{
    if (!ctx.claimed || !ctx.system_bus)
        return;

    g_dbus_connection_call_sync(
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
    ctx.claimed = FALSE;
}

static void init_powerdevil_brightness(void)
{
    if (!ctx.session_bus)
        return;

    /* Query brightnessMax */
    GVariant *res_max = g_dbus_connection_call_sync(
        ctx.session_bus,
        POWER_BUS_NAME,
        POWER_OBJECT_PATH,
        POWER_INTERFACE,
        "brightnessMax",
        NULL,
        NULL,
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

    /* Query current brightness to prevent initial abrupt jump */
    GVariant *res_cur = g_dbus_connection_call_sync(
        ctx.session_bus,
        POWER_BUS_NAME,
        POWER_OBJECT_PATH,
        POWER_INTERFACE,
        "brightness",
        NULL,
        NULL,
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
        ctx.target_pct = ctx.current_pct;
    }
}

static gboolean on_unix_signal(gpointer user_data)
{
    GMainLoop *loop = (GMainLoop *)user_data;
    if (loop && g_main_loop_is_running(loop))
        g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}

int main(void)
{
    ctx.current_pct = 30.0;
    ctx.target_pct = 30.0;
    ctx.step_interval_ms = 50;
    ctx.step_rate_pct = 0.8;
    ctx.brightness_max = 10000;

    GError *error = NULL;
    ctx.system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error) {
        g_printerr("Failed to connect to system bus: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    ctx.session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (error) {
        g_printerr("Failed to connect to session bus: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    init_powerdevil_brightness();

    ctx.loop = g_main_loop_new(NULL, FALSE);

    /* Safe GLib Unix signal handlers */
    g_unix_signal_add(SIGINT, on_unix_signal, ctx.loop);
    g_unix_signal_add(SIGTERM, on_unix_signal, ctx.loop);

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

    if (ctx.timer_id) {
        g_source_remove(ctx.timer_id);
        ctx.timer_id = 0;
    }

    if (ctx.sensor_watch_id) {
        g_bus_unwatch_name(ctx.sensor_watch_id);
        ctx.sensor_watch_id = 0;
    }

    if (ctx.sub_id) {
        g_dbus_connection_signal_unsubscribe(ctx.system_bus, ctx.sub_id);
        ctx.sub_id = 0;
    }

    release_sensor();

    if (ctx.system_bus)
        g_object_unref(ctx.system_bus);
    if (ctx.session_bus)
        g_object_unref(ctx.session_bus);
    if (ctx.loop)
        g_main_loop_unref(ctx.loop);

    return 0;
}


