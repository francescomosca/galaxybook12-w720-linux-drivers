/* SPDX-License-Identifier: GPL-2.0-only */
#include "common.h"
#include "curve.h"
#include "config.h"
#include "brightness.h"
#include "sensor.h"

typedef struct {
    GMainLoop *loop;
    GDBusConnection *system_bus;
    GDBusConnection *session_bus;

    ConfigManager config_mgr;
    BrightnessManager brightness_mgr;
    SensorManager sensor_mgr;

    guint timer_id;
    double current_lux;
    double target_pct;
    double current_pct;
    double step_rate_pct;
    guint step_interval_ms;
} DaemonContext;

static DaemonContext ctx;

static void stop_transition(void)
{
    if (ctx.timer_id != 0) {
        g_source_remove(ctx.timer_id);
        ctx.timer_id = 0;
    }
}

static gboolean on_transition_step(gpointer user_data)
{
    (void)user_data;

    if (!ctx.config_mgr.auto_brightness_enabled || !brightness_mgr_is_ready(&ctx.brightness_mgr)) {
        ctx.timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    double diff = ctx.target_pct - ctx.current_pct;

    if (fabs(diff) <= ctx.step_rate_pct) {
        ctx.current_pct = ctx.target_pct;
        brightness_mgr_apply(&ctx.brightness_mgr, ctx.current_pct);
        ctx.timer_id = 0;
        return G_SOURCE_REMOVE;
    }

    if (diff > 0.0)
        ctx.current_pct += ctx.step_rate_pct;
    else
        ctx.current_pct -= ctx.step_rate_pct;

    brightness_mgr_apply(&ctx.brightness_mgr, ctx.current_pct);
    return G_SOURCE_CONTINUE;
}

static void start_transition(void)
{
    if (!ctx.config_mgr.auto_brightness_enabled || !brightness_mgr_is_ready(&ctx.brightness_mgr))
        return;

    if (ctx.timer_id == 0) {
        ctx.timer_id = g_timeout_add(ctx.step_interval_ms, on_transition_step, NULL);
    }
}

static void on_light_level_updated(double lux, gpointer user_data)
{
    (void)user_data;

    if (!ctx.config_mgr.auto_brightness_enabled)
        return;

    ctx.current_lux = lux;
    double new_target = lux_to_target_pct(lux, ctx.config_mgr.min_brightness_pct);

    if (fabs(new_target - ctx.target_pct) >= 1.0) {
        if (ctx.timer_id == 0) {
            brightness_mgr_sync(&ctx.brightness_mgr, FALSE);
            ctx.current_pct = ctx.brightness_mgr.current_pct;
        }
        ctx.target_pct = new_target;
        start_transition();
    }
}

static void on_config_changed(gboolean enabled, double min_brightness, gpointer user_data)
{
    (void)user_data;

    if (!enabled) {
        stop_transition();
        sensor_mgr_release(&ctx.sensor_mgr);
    } else {
        brightness_mgr_select_backend(&ctx.brightness_mgr);
        brightness_mgr_sync(&ctx.brightness_mgr, TRUE);
        ctx.current_pct = ctx.brightness_mgr.current_pct;

        sensor_mgr_claim(&ctx.sensor_mgr);
        double new_target = lux_to_target_pct(ctx.current_lux, min_brightness);
        if (fabs(new_target - ctx.target_pct) >= 0.5) {
            ctx.target_pct = new_target;
            start_transition();
        }
    }
}

static void on_display_changed(gpointer user_data)
{
    (void)user_data;

    if (ctx.config_mgr.auto_brightness_enabled && ctx.sensor_mgr.claimed &&
        brightness_mgr_is_ready(&ctx.brightness_mgr)) {
        sensor_mgr_fetch_light_level(&ctx.sensor_mgr);
    }
}

static void on_sensor_appeared(gpointer user_data)
{
    (void)user_data;

    if (ctx.config_mgr.auto_brightness_enabled)
        sensor_mgr_claim(&ctx.sensor_mgr);
}

static void on_sensor_vanished(gpointer user_data)
{
    (void)user_data;
    stop_transition();
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
    double initial_min_brightness = 0.0;
    gboolean min_brightness_cli_set = FALSE;

    ctx.step_interval_ms = 50;
    ctx.step_rate_pct = 0.8;

    /* Check environment variable override */
    const char *env_min = g_getenv("MIN_BRIGHTNESS");
    if (!env_min)
        env_min = g_getenv("GALAXYBOOK_MIN_BRIGHTNESS");
    if (env_min) {
        char *endptr = NULL;
        double d = g_ascii_strtod(env_min, &endptr);
        if (endptr && endptr != env_min && d >= 0.0 && d <= 100.0) {
            initial_min_brightness = d;
        }
    }

    /* Parse command-line options */
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--min-brightness") == 0) && i + 1 < argc) {
            char *endptr = NULL;
            double d = g_ascii_strtod(argv[i + 1], &endptr);
            if (endptr && endptr != argv[i + 1] && d >= 0.0 && d <= 100.0) {
                initial_min_brightness = d;
                min_brightness_cli_set = TRUE;
            }
            i++;
        } else if (strncmp(argv[i], "--min-brightness=", 17) == 0) {
            const char *val_str = argv[i] + 17;
            char *endptr = NULL;
            double d = g_ascii_strtod(val_str, &endptr);
            if (endptr && endptr != val_str && d >= 0.0 && d <= 100.0) {
                initial_min_brightness = d;
                min_brightness_cli_set = TRUE;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [OPTIONS]\n", argv[0]);
            printf("Options:\n");
            printf("  -m, --min-brightness <0-100>  Set minimum auto-brightness percentage (default: 0)\n");
            printf("  -h, --help                     Show this help message\n");
            return 0;
        }
    }

    ctx.current_pct = initial_min_brightness;
    ctx.target_pct = initial_min_brightness;

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

    brightness_mgr_init(&ctx.brightness_mgr,
                        ctx.system_bus,
                        ctx.session_bus,
                        on_display_changed,
                        NULL);

    config_mgr_init(&ctx.config_mgr,
                    initial_min_brightness,
                    min_brightness_cli_set,
                    on_config_changed,
                    NULL);

    sensor_mgr_init(&ctx.sensor_mgr,
                    ctx.system_bus,
                    on_light_level_updated,
                    on_sensor_appeared,
                    on_sensor_vanished,
                    NULL);

    ctx.current_pct = ctx.brightness_mgr.current_pct;
    ctx.target_pct = ctx.current_pct;

    if (ctx.config_mgr.auto_brightness_enabled) {
        sensor_mgr_claim(&ctx.sensor_mgr);
    }

    ctx.loop = g_main_loop_new(NULL, FALSE);

    /* Safe GLib Unix signal handlers */
    g_unix_signal_add(SIGINT, on_unix_signal, ctx.loop);
    g_unix_signal_add(SIGTERM, on_unix_signal, ctx.loop);

    g_main_loop_run(ctx.loop);

    stop_transition();

    sensor_mgr_cleanup(&ctx.sensor_mgr);
    config_mgr_cleanup(&ctx.config_mgr);
    brightness_mgr_cleanup(&ctx.brightness_mgr);

    if (ctx.system_bus)
        g_object_unref(ctx.system_bus);
    if (ctx.session_bus)
        g_object_unref(ctx.session_bus);
    if (ctx.loop)
        g_main_loop_unref(ctx.loop);

    return 0;
}

