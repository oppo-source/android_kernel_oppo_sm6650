/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2022 TsingTeng MicroSystem Co., Ltd.
 */

#ifndef _TMS_NFC_H_
#define _TMS_NFC_H_

/*********** PART0: Head files ***********/
#include <linux/i2c.h>

#include "../tms_common.h"
/*********** PART1: Define Area ***********/
#ifdef TMS_MOUDLE
#undef TMS_MOUDLE
#define TMS_MOUDLE               "Nfc"
#endif
#define NFC_VERSION               TMS_VERSION ".010201"
#define NCI_HDR_LEN               (3)
#define HEAD_PAYLOAD_BYTE         (2)
#define MAX_NCI_PAYLOAD_LEN       (255)
#define MAX_NCI_BUFFER_SIZE       (NCI_HDR_LEN + MAX_NCI_PAYLOAD_LEN)
#define WAKEUP_SRC_TIMEOUT        (2000)
#define RETRY_TIMES               (3)

/*********** PART2: Struct Area ***********/
enum nfc_ioctl_request_ese_table {
    REQUEST_ESE_POWER_ON = 0, /* eSE POWER ON */
    REQUEST_ESE_POWER_OFF,    /* eSE POWER OFF */
    REQUEST_ESE_POWER_STATE,  /* eSE GET POWER STATE */
};

struct nfc_info {
    bool                   irq_enable;
    bool                   ven_enable;       /* store VEN state */
    bool                   release_read;
    bool                   irq_wake_up;
    unsigned int           open_dev_count;
    struct i2c_client      *client;
    struct device          *i2c_dev;         /* Used for i2c->dev */
    struct dev_register    dev;
    struct hw_resource     hw_res;
    struct clk             *clk;
    struct clk             *clk_parent;
    struct clk             *clk_enable;
    struct tms_info        *tms;             /* tms common data */
    struct mutex           read_mutex;
    struct mutex           write_mutex;
    struct mutex           open_dev_mutex;
    spinlock_t             irq_enable_slock;
    wait_queue_head_t      read_wq;

};

/*********** PART3: Function or variables for other files ***********/
struct nfc_info *nfc_data_alloc(struct device *dev, struct nfc_info *nfc);
void nfc_data_free(struct device *dev, struct nfc_info *nfc);
int nfc_common_info_init(struct nfc_info *nfc);
int nfc_ioctl_set_ese_state(struct nfc_info *nfc, unsigned long arg);
int nfc_ioctl_get_ese_state(struct nfc_info *nfc, unsigned long arg);
void nfc_gpio_release(struct nfc_info *nfc);
struct nfc_info *nfc_get_data(struct inode *inode);
void nfc_disable_irq(struct nfc_info *nfc);
void nfc_enable_irq(struct nfc_info *nfc);
void nfc_power_control(struct nfc_info *nfc, bool state);
void nfc_fw_download_control(struct nfc_info *nfc, bool state);
void nfc_hard_reset(struct nfc_info *nfc);
int nfc_irq_register(struct nfc_info *nfc);
void nfc_jump_fw(struct i2c_client *client, unsigned int irq_gpio);
int nfc_enable_rf_clk(struct nfc_info *nfc);
void nfc_disable_rf_clk(struct nfc_info *nfc);
#endif /* _TMS_NFC_H_ */
