/*
 *      main.c                  (C) 2022-2023, Aurélien Croc (AP²C)
 *
 *   This program is free software; you can redistribute it and/or modify it under
 *   the terms of the GNU General Public License as published by the Free Software
 *   Foundation; version 2 of the License.
 * 
 *   This program is distributed in the hope that it will be useful, but WITHOUT
 *   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *   details.
 *   
 *   You should have received a copy of the GNU General Public License along with
 *   this program; If not, see <http://www.gnu.org/licenses/>.
 */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>
#include <libudev.h>
#include "brightness.h"
#include "dpAux.h"

#define AUX_PATH "/dev/drm_dp_aux0"
#define BACKLIGHT_PATH "/sys/class/backlight/intel_backlight"
#define BRIGHTNESS_PATH BACKLIGHT_PATH "/brightness"
#define MAX_BRIGHTNESS_PATH BACKLIGHT_PATH "/max_brightness"
#define BL_POWER_PATH BACKLIGHT_PATH "/bl_power"

struct {
    pthread_mutex_t         lock;
    displayBrightness_t*    dp;
    aux_t                   aux;
    int                     maxBrightness;
    int                     colorProfile;
    int                     lastValidBrightness;
} sigData;

static volatile sig_atomic_t brightnessDirty = 0;

static int isScreenOn(void)
{
    FILE *f;
    char buf[32];

    if ((f = fopen(BL_POWER_PATH, "r")) != NULL) {
        if (fgets(buf, sizeof(buf), f) != NULL) {
            int power = strtol(buf, NULL, 10);
            fclose(f);
            if (power != 0)
                return 0;
        } else {
            fclose(f);
        }
    }

    const char *dpmsPaths[] = {
        "/sys/class/drm/card1-eDP-1/dpms",
        "/sys/class/drm/card0-eDP-1/dpms",
        NULL
    };

    for (int i = 0; dpmsPaths[i] != NULL; i++) {
        if ((f = fopen(dpmsPaths[i], "r")) != NULL) {
            if (fgets(buf, sizeof(buf), f) != NULL) {
                fclose(f);
                if (strncmp(buf, "On", 2) == 0)
                    return 1;
                return 0;
            }
            fclose(f);
        }
    }

    return 1;
}

int applyBrightnessAndColorProfile(int forceReopen, int applyProfile)
{
    FILE *handle;
    int ret = -1;

    if (pthread_mutex_lock(&sigData.lock) != 0)
        return -1;

    if (!isScreenOn()) {
        pthread_mutex_unlock(&sigData.lock);
        return 0;
    }

    if ((handle = fopen(BRIGHTNESS_PATH, "r")) != NULL) {
        char buffer[256];
        int rawBrightness, scaledBrightness;

        if (fgets(buffer, sizeof(buffer), handle) != NULL) {
            fclose(handle);
            rawBrightness = strtol(buffer, NULL, 10);

            if (rawBrightness <= 0) {
                pthread_mutex_unlock(&sigData.lock);
                return 0;
            }

            scaledBrightness = rawBrightness * 100 / sigData.maxBrightness;
            if (scaledBrightness < 1)
                scaledBrightness = 1;
            if (scaledBrightness > 100)
                scaledBrightness = 100;
            sigData.lastValidBrightness = scaledBrightness;

            if (forceReopen || sigData.aux < 0) {
                dpAuxClose(sigData.aux);
                sigData.aux = dpAuxOpen(AUX_PATH);
            }

            if (sigData.aux >= 0 && accessParaAuxRegs(sigData.aux) == 0) {
                setBrightness(sigData.aux, sigData.dp, scaledBrightness);
                if (applyProfile)
                    setColorProfile(sigData.aux, sigData.dp, sigData.colorProfile);
                ret = 0;
            }
        } else {
            fclose(handle);
        }
    }
    pthread_mutex_unlock(&sigData.lock);
    return ret;
}

static void restoreDisplayOnWake(void)
{
    for (int attempt = 0; attempt < 10; attempt++) {
        if (!isScreenOn())
            return;
        if (applyBrightnessAndColorProfile(1, 1) == 0)
            return;
        usleep(100000);
    }
}

void sigIOHandler(int sig)
{
    (void)sig;
    brightnessDirty = 1;
}

void watch(aux_t aux, displayBrightness_t *dp, int colorProfile)
{
    int fd;
    time_t lastTS, newTS;
    char buffer[256];
    FILE *handle;
    struct udev *udev = NULL;
    struct udev_monitor *mon = NULL;
    int udev_fd = -1;
    int wasScreenOn = 1;

    // Get the maximum brightness value
    if ((handle = fopen(MAX_BRIGHTNESS_PATH, "r")) == NULL) {
        fprintf(stderr, "Error while opening " MAX_BRIGHTNESS_PATH "\n");
        return;
    }
    if (fgets(buffer, sizeof(buffer), handle) == NULL) {
        fclose(handle);
        return;
    }
    fclose(handle);

    // Prepare the signal data
    pthread_mutex_init(&sigData.lock, NULL);
    sigData.dp = dp;
    sigData.aux = aux;
    sigData.maxBrightness = strtol(buffer, NULL, 10);
    if (sigData.maxBrightness <= 0)
        sigData.maxBrightness = 5000;
    sigData.colorProfile = colorProfile;
    sigData.lastValidBrightness = 50;

    // Set the signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigIOHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGIO, &sa, NULL);

    // Set initial brightness and color profile
    applyBrightnessAndColorProfile(1, 1);

    // Open the /sys brightness directory in order to check changes
    if ((fd = open(BACKLIGHT_PATH, O_RDONLY)) == -1) {
        fprintf(stderr, "Error while opening path " BACKLIGHT_PATH "\n");
        return;
    }
    fcntl(fd, F_SETSIG, 0); 
    if (fcntl(fd, F_NOTIFY, DN_MODIFY | DN_MULTISHOT) == -1) {
        fprintf(stderr, "Cannot watch for changes on path " BACKLIGHT_PATH 
            "\n");
        return;
    }

    // Set up udev monitor for DRM and backlight events
    udev = udev_new();
    if (udev) {
        mon = udev_monitor_new_from_netlink(udev, "udev");
        if (mon) {
            udev_monitor_filter_add_match_subsystem_devtype(mon, "drm", NULL);
            udev_monitor_filter_add_match_subsystem_devtype(mon, "backlight", NULL);
            udev_monitor_enable_receiving(mon);
            udev_fd = udev_monitor_get_fd(mon);
        }
    }

    wasScreenOn = isScreenOn();
    lastTS = time(NULL);

    while (1) {
        struct pollfd pfd;
        int pollRet = 0;

        if (brightnessDirty) {
            brightnessDirty = 0;
            applyBrightnessAndColorProfile(0, 0);
        }

        if (udev_fd >= 0) {
            pfd.fd = udev_fd;
            pfd.events = POLLIN;
            pollRet = poll(&pfd, 1, 1000);
        } else {
            sleep(1);
        }

        if (brightnessDirty) {
            brightnessDirty = 0;
            applyBrightnessAndColorProfile(0, 0);
        }

        newTS = time(NULL);

        // 1. Detect resume from system suspend (time jump)
        if (newTS - lastTS > 4) {
            usleep(200000);
            restoreDisplayOnWake();
            wasScreenOn = isScreenOn();
        }
        lastTS = newTS;

        // 2. Detect DRM / backlight udev events (DPMS On/Off or modeset)
        if (pollRet > 0 && (pfd.revents & POLLIN)) {
            struct udev_device *dev;
            int drmEvent = 0;
            while ((dev = udev_monitor_receive_device(mon)) != NULL) {
                const char *subsys = udev_device_get_subsystem(dev);
                if (subsys && strcmp(subsys, "drm") == 0)
                    drmEvent = 1;
                else if (subsys && strcmp(subsys, "backlight") == 0)
                    brightnessDirty = 1;
                udev_device_unref(dev);
            }
            if (drmEvent) {
                int currentlyOn = isScreenOn();
                if (currentlyOn && !wasScreenOn) {
                    usleep(150000);
                    restoreDisplayOnWake();
                } else if (currentlyOn) {
                    usleep(50000);
                    applyBrightnessAndColorProfile(1, 1);
                }
                wasScreenOn = currentlyOn;
            }
        }

        // 3. Periodic DPMS state transition check (fallback)
        int currentScreenState = isScreenOn();
        if (!wasScreenOn && currentScreenState) {
            usleep(150000);
            restoreDisplayOnWake();
        }
        wasScreenOn = currentScreenState;
    }

    if (mon)
        udev_monitor_unref(mon);
    if (udev)
        udev_unref(udev);
}

int main(int argc, char** argv)
{
    int brightness, colorProfile=3;
    displayBrightness_t* dp;
    aux_t aux;

    // Check arguments
    if (argc < 2) {
        printf("Usage: %s watch | <brightness> [<color profile>]\n", argv[0]);
        printf("     watch: listening for sys brightness changes and update "
            "the screen brightness accordingly\n");
        printf("     brightness: the brightness value between 1 and 100\n");
        printf("     color profile: the color profile to use between 0 and 6 "
            "[3 by default]\n");
        return 0;
    }
    
    // Open the DP AUX channel
    if ((aux = dpAuxOpen(AUX_PATH)) < 0) {
        fprintf(stderr, "Cannot open DP AUX device\n");
        return 1;
    }

    if (!(dp = initBrightness(aux, 1))) {
        fprintf(stderr, "Cannot initialize the display\n");
        dpAuxClose(aux);
        return 2;
    }

    // Extract the color profile
    if (argc == 3)
        colorProfile = strtol(argv[2], (char **)NULL, 10);

    // Check whether the program has to watch for brightness update
    if (!strcmp(argv[1], "watch")) {
        watch(aux, dp, colorProfile);
        free(dp);
        dpAuxClose(aux);
        return 0;
    }

    // Otherwise set the brightness
    brightness = strtol(argv[1], (char **)NULL, 10);
    if (brightness < 1)
        brightness = 1;
    if (brightness > 100)
        brightness = 100;
    setColorProfile(aux, dp, colorProfile);
    setBrightness(aux, dp, brightness);


    free(dp);
    dpAuxClose(aux);

    return 0;
}


/* vim: set expandtab tabstop=4 shiftwidth=4 smarttab tw=80 cin fenc=utf8 : */

