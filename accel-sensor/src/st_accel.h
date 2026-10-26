/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef ST_ACCEL_H
#define ST_ACCEL_H

#include <linux/types.h>
#include <linux/iio/common/st_sensors.h>

#define LIS2HH12_ACCEL_DEV_NAME "lis2hh12"

#ifdef CONFIG_IIO_BUFFER
int st_accel_allocate_ring(struct iio_dev *indio_dev);
int st_accel_trig_set_state(struct iio_trigger *trig, bool state);
#define ST_ACCEL_TRIGGER_SET_STATE (&st_accel_trig_set_state)
#else /* CONFIG_IIO_BUFFER */
static inline int st_accel_allocate_ring(struct iio_dev *indio_dev) { return 0; }
#define ST_ACCEL_TRIGGER_SET_STATE NULL
#endif /* CONFIG_IIO_BUFFER */

int st_accel_common_probe(struct iio_dev *indio_dev);
void st_accel_common_remove(struct iio_dev *indio_dev);
const struct st_sensor_settings *st_accel_get_settings(const char *name);

#endif
