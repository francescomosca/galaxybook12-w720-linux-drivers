/* SPDX-License-Identifier: GPL-2.0-only */
#include "sensor.h"

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
    SensorManager *sm = (SensorManager *)user_data;

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
            if (sm->on_light_level)
                sm->on_light_level(lux, sm->user_data);
        }
        g_variant_unref(val);
    }

    if (changed_props) g_variant_iter_free(changed_props);
    if (invalidated_props) g_variant_iter_free(invalidated_props);
}

void sensor_mgr_fetch_light_level(SensorManager *sm)
{
    if (!sm->system_bus || !sm->claimed)
        return;

    GVariant *res = g_dbus_connection_call_sync(
        sm->system_bus,
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
                double lux = g_variant_get_double(inner);
                if (sm->on_light_level)
                    sm->on_light_level(lux, sm->user_data);
            }
            g_variant_unref(inner);
        }
        g_variant_unref(res);
    }
}

void sensor_mgr_claim(SensorManager *sm)
{
    if (!sm->system_bus || !sm->sensor_present || sm->claimed)
        return;

    GError *error = NULL;
    GVariant *res = g_dbus_connection_call_sync(
        sm->system_bus,
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

    sm->claimed = TRUE;

    if (sm->sensor_sub_id == 0) {
        sm->sensor_sub_id = g_dbus_connection_signal_subscribe(
            sm->system_bus,
            SENSOR_BUS_NAME,
            "org.freedesktop.DBus.Properties",
            "PropertiesChanged",
            SENSOR_OBJECT_PATH,
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            on_sensor_properties_changed,
            sm,
            NULL
        );
    }

    sensor_mgr_fetch_light_level(sm);
}

void sensor_mgr_release(SensorManager *sm)
{
    if (sm->sensor_sub_id != 0 && sm->system_bus) {
        g_dbus_connection_signal_unsubscribe(sm->system_bus, sm->sensor_sub_id);
        sm->sensor_sub_id = 0;
    }

    if (!sm->claimed || !sm->system_bus)
        return;

    GVariant *res = g_dbus_connection_call_sync(
        sm->system_bus,
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

    sm->claimed = FALSE;
}

static void on_sensor_proxy_appeared(GDBusConnection *connection,
                                     const gchar *name,
                                     const gchar *name_owner,
                                     gpointer user_data)
{
    (void)connection;
    (void)name;
    (void)name_owner;
    SensorManager *sm = (SensorManager *)user_data;

    sm->sensor_present = TRUE;
    if (sm->on_sensor_appeared)
        sm->on_sensor_appeared(sm->user_data);
}

static void on_sensor_proxy_vanished(GDBusConnection *connection,
                                     const gchar *name,
                                     gpointer user_data)
{
    (void)connection;
    (void)name;
    SensorManager *sm = (SensorManager *)user_data;

    sm->sensor_present = FALSE;
    sm->claimed = FALSE;

    if (sm->sensor_sub_id != 0 && sm->system_bus) {
        g_dbus_connection_signal_unsubscribe(sm->system_bus, sm->sensor_sub_id);
        sm->sensor_sub_id = 0;
    }

    if (sm->on_sensor_vanished)
        sm->on_sensor_vanished(sm->user_data);
}

void sensor_mgr_init(SensorManager *sm,
                     GDBusConnection *system_bus,
                     SensorLightLevelCb on_light_level,
                     void (*on_sensor_appeared)(gpointer user_data),
                     void (*on_sensor_vanished)(gpointer user_data),
                     gpointer user_data)
{
    memset(sm, 0, sizeof(SensorManager));
    sm->system_bus = system_bus;
    sm->on_light_level = on_light_level;
    sm->on_sensor_appeared = on_sensor_appeared;
    sm->on_sensor_vanished = on_sensor_vanished;
    sm->user_data = user_data;

    sm->sensor_watch_id = g_bus_watch_name_on_connection(
        sm->system_bus,
        SENSOR_BUS_NAME,
        G_BUS_NAME_WATCHER_FLAGS_NONE,
        on_sensor_proxy_appeared,
        on_sensor_proxy_vanished,
        sm,
        NULL
    );
}

void sensor_mgr_cleanup(SensorManager *sm)
{
    if (sm->sensor_watch_id != 0)
        g_bus_unwatch_name(sm->sensor_watch_id);

    sensor_mgr_release(sm);
}

