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
#define EDP_DPMS_PATH "/sys/class/drm/card1-eDP-1/dpms"

struct {
    pthread_mutex_t         lock;
    displayBrightness_t*    dp;
    aux_t                   aux;
    int                     maxBrightness;
    int                     colorProfile;
} sigData;

/*
LOCK = threading.Lock()
def handler(aux, displayInfo, cc5State, maxBrightness):
    if not LOCK.acquire(blocking=False) :
        return

    try :
        with open(BRIGHTNESS_PATH) as infile:
            brightness = int(infile.readline())
            infile.close()
            aux.reopen()
            sendParaAux(aux)
            setBrightness(aux, displayInfo, cc5State, int(brightness * 101 / maxBrightness))
    except Exception as e:
        print('exception: %s' % e)
    finally:
        LOCK.release()
*/

void applyBrightnessAndColorProfile(int forceReopen)
{
    FILE *handle;

    if (pthread_mutex_lock(&sigData.lock) != 0)
        return;

    if ((handle = fopen(BRIGHTNESS_PATH, "r")) != NULL) {
        char buffer[256];
        int brightness;

        fgets((char *)&buffer, sizeof(buffer), handle);
        fclose(handle);
        brightness = strtol((char *)&buffer, (char **)NULL, 10);

        if (forceReopen) {
            dpAuxClose(sigData.aux);
            sigData.aux = dpAuxOpen(AUX_PATH);
        }
        accessParaAuxRegs(sigData.aux);

        setBrightness(sigData.aux, sigData.dp, 
            brightness * 101 / sigData.maxBrightness);
        setColorProfile(sigData.aux, sigData.dp, sigData.colorProfile);
    }
    pthread_mutex_unlock(&sigData.lock);
}

void sigIOHandler(int sig)
{
    (void)sig;
    applyBrightnessAndColorProfile(1);
}

static int isScreenOn(void)
{
    FILE *f = fopen(EDP_DPMS_PATH, "r");
    if (!f)
        return 1;
    char buf[32];
    if (fgets(buf, sizeof(buf), f) != NULL) {
        fclose(f);
        if (strncmp(buf, "On", 2) == 0)
            return 1;
        return 0;
    }
    fclose(f);
    return 1;
}

void watch(aux_t aux, displayBrightness_t *dp, int colorProfile)
{
    int fd, maxBrightness;
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
    fgets((char *)&buffer, sizeof(buffer), handle);
    fclose(handle);

    // Prepare the signal data
    pthread_mutex_init(&sigData.lock, NULL);
    sigData.dp = dp;
    sigData.aux = aux;
    sigData.maxBrightness = strtol((char *)&buffer, (char **)NULL, 10);
    sigData.colorProfile = colorProfile;

    // Set the signal handler
    signal(SIGIO, sigIOHandler);

    // Set initial brightness and color profile
    applyBrightnessAndColorProfile(1);

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

    // Set up udev monitor for DRM display events (screen turn on / modeset / resume)
    udev = udev_new();
    if (udev) {
        mon = udev_monitor_new_from_netlink(udev, "udev");
        if (mon) {
            udev_monitor_filter_add_match_subsystem_devtype(mon, "drm", NULL);
            udev_monitor_enable_receiving(mon);
            udev_fd = udev_monitor_get_fd(mon);
        }
    }

    wasScreenOn = isScreenOn();
    lastTS = time(NULL);

    while (1) {
        struct pollfd pfd;
        int pollRet = 0;

        if (udev_fd >= 0) {
            pfd.fd = udev_fd;
            pfd.events = POLLIN;
            // Poll for up to 1500ms
            pollRet = poll(&pfd, 1, 1500);
        } else {
            sleep(2);
        }

        newTS = time(NULL);

        // 1. Detect resume from system suspend (time jump)
        if (newTS - lastTS > 4) {
            usleep(200000); // 200ms grace period for DP-AUX hardware ready
            applyBrightnessAndColorProfile(1);
        }
        lastTS = newTS;

        // 2. Detect DRM udev events (e.g. DPMS On/Off or modeset)
        if (pollRet > 0 && (pfd.revents & POLLIN)) {
            struct udev_device *dev;
            int drmEvent = 0;
            while ((dev = udev_monitor_receive_device(mon)) != NULL) {
                drmEvent = 1;
                udev_device_unref(dev);
            }
            if (drmEvent) {
                int currentlyOn = isScreenOn();
                if (currentlyOn) {
                    usleep(150000); // Wait 150ms for AMOLED panel to power up
                    applyBrightnessAndColorProfile(1);
                }
                wasScreenOn = currentlyOn;
            }
        }

        // 3. Periodic DPMS state transition check (fallback)
        int currentScreenState = isScreenOn();
        if (!wasScreenOn && currentScreenState) {
            // Screen was off and is now turned back on
            usleep(150000);
            applyBrightnessAndColorProfile(1);
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
        printf("     brightness: the brightness value between 1 and 101\n");
        printf("     color profile: the color profile to use between 0 and 6 "
            "[3 by default]\n");
        return 0;
    }
    
    // Open the DP AUX channel
    if (!(aux = dpAuxOpen(AUX_PATH))) {
        fprintf(stderr, "Cannot open DP AUX device\n");
        return 1;
    }

    if (!(dp = initBrightness(aux, colorProfile))) {
        fprintf(stderr, "Cannot initialize the display\n");
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
    setColorProfile(aux, dp, colorProfile);
    setBrightness(aux, dp, brightness);


    free(dp);
    dpAuxClose(aux);

    return 0;
}


/* vim: set expandtab tabstop=4 shiftwidth=4 smarttab tw=80 cin fenc=utf8 : */

