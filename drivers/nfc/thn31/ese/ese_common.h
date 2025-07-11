/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2022 TsingTeng MicroSystem Co., Ltd.
 */

#ifndef _TMS_ESE_H_
#define _TMS_ESE_H_

/*********** PART0: Head files ***********/
#include <linux/spi/spi.h>

#include "../tms_common.h"
/*********** PART1: Define Area ***********/
#ifdef TMS_MOUDLE
#undef TMS_MOUDLE
#define TMS_MOUDLE               "eSE"
#endif
#define ESE_VERSION              TMS_VERSION ".010200"
/*********** PART2: Struct Area ***********/
struct ese_info {
    struct spi_device      *client;
    struct device          *spi_dev;            /* Used for spi->dev */
    struct dev_register    dev;
    struct hw_resource     hw_res;
    struct tms_info        *tms;                /* tms common data */
    struct mutex           read_mutex;
    struct mutex           write_mutex;
    wait_queue_head_t      read_wq;

};

/*********** PART3: Function or variables for other files ***********/
struct ese_info *ese_data_alloc(struct device *dev, struct ese_info *ese);
void ese_data_free(struct device *dev, struct ese_info *ese);
int ese_common_info_init(struct ese_info *ese);
void ese_gpio_release(struct ese_info *ese);
struct ese_info *ese_get_data(struct inode *inode);
void ese_hard_reset(struct ese_info *ese);
#endif /* _TMS_ESE_H_ */
