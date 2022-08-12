/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/delay.h>

#include "richtek/rt-flashlight.h"
#include "mtk_charger.h"

#include "flashlight-core.h"
#include "flashlight-dt.h"
#ifdef OPLUS_FEATURE_CAMERA_COMMON
/* 2021/02/19, lishaoyang@Camera.Tunning, add for 20001&20200 torch duty 20190803*/
#include<soc/oppo/oppo_project.h>
#endif
/* device tree should be defined in flashlight-dt.h */
#ifndef MOSS_DTNAME
#define MOSS_DTNAME "mediatek,flashlights_moss"
#endif

#define MOSS_NAME "flashlights-moss"

/* define channel, level */
#define MOSS_CHANNEL_NUM 2
#define MOSS_CHANNEL_CH1 0
#define MOSS_CHANNEL_CH2 1
#define MOSS_CHANNEL_ALL 2

#define MOSS_NONE (-1)
#define MOSS_DISABLE 0
#define MOSS_ENABLE 1
#define MOSS_ENABLE_TORCH 1
#define MOSS_ENABLE_FLASH 2

#define MOSS_LEVEL_NUM 32
#define MOSS_LEVEL_TORCH 16
#define MOSS_LEVEL_FLASH MOSS_LEVEL_NUM
#define MOSS_WDT_TIMEOUT 1248 /* ms */
#define MOSS_HW_TIMEOUT 400 /* ms */

/* define mutex, work queue and timer */
static DEFINE_MUTEX(moss_mutex);
static struct work_struct moss_work_ch1;
static struct work_struct moss_work_ch2;
static struct hrtimer moss_timer_ch1;
static struct hrtimer moss_timer_ch2;
static unsigned int moss_timeout_ms[MOSS_CHANNEL_NUM];

/* define usage count */
static int use_count;
static int fd_use_count;

/* define RTK flashlight device */
static struct flashlight_device *flashlight_dev_ch1;
static struct flashlight_device *flashlight_dev_ch2;
#define RT_FLED_DEVICE_CH1  "mt-flash-led1"
#define RT_FLED_DEVICE_CH2  "mt-flash-led2"

/* define charger consumer */
static struct charger_consumer *flashlight_charger_consumer;
#define CHARGER_SUPPLY_NAME "charger_port1"

/* is decrease voltage */
static int is_decrease_voltage;

/* platform data */
struct moss_platform_data {
	int channel_num;
	struct flashlight_device_id *dev_id;
};


/******************************************************************************
 * moss operations
 *****************************************************************************/
static const int moss_current[MOSS_LEVEL_NUM] = {
	  25,   50,  75, 100, 125, 150, 175,  200,  225,  250,
	 275,  300, 325, 350, 375, 400, 450,  500,  550,  600,
	 650,  700, 750, 800, 850, 900, 950, 1000, 1050, 1100,
	1150, 1200
};

static const unsigned char moss_torch_level[MOSS_LEVEL_TORCH] = {
	0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12,
	0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E
};

/* 0x00~0x74 6.25mA/step 0x75~0xB1 12.5mA/step */
static const unsigned char moss_strobe_level[MOSS_LEVEL_FLASH] = {
	0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24,
	0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x44, 0x4C, 0x54, 0x5C,
	0x64, 0x6C, 0x74, 0x78, 0x7C, 0x80, 0x84, 0x88, 0x8C, 0x90,
	0x94, 0x98
};

#ifdef OPLUS_FEATURE_CAMERA_COMMON
/* 2021/02/19, lishaoyang@Camera.Tunning, add for 20001&20200 torch level */
static const unsigned char moss_torch_level_19165[MOSS_LEVEL_TORCH] = {
    0x00, 0x02, 0x04, 0x07, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12,
    0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E
};

static const unsigned char moss_torch_level_19131[MOSS_LEVEL_TORCH] = {
    0x00, 0x02, 0x06, 0x07, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12,
    0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E
};

static const unsigned char moss_torch_level_20001[MOSS_LEVEL_TORCH] = {
    0x00, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12,
    0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E
};
static const unsigned char moss_torch_level_20075[MOSS_LEVEL_TORCH] = {
    0x00, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0C, 0x0E, 0x10, 0x12,
    0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E
};
static const unsigned char moss_torch_level_moss[MOSS_LEVEL_TORCH] = {
    0x00, 0x02, 0x04, 0x05, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12,
    0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E
};
#endif
static int moss_decouple_mode;
static int moss_en_ch1;
static int moss_en_ch2;
static int moss_level_ch1;
static int moss_level_ch2;

static int moss_is_charger_ready(void)
{
	if (flashlight_is_ready(flashlight_dev_ch1) &&
			flashlight_is_ready(flashlight_dev_ch2))
		return FLASHLIGHT_CHARGER_READY;
	else
		return FLASHLIGHT_CHARGER_NOT_READY;
}

static int moss_is_torch(int level)
{
	if (level >= MOSS_LEVEL_TORCH)
		return -1;

	return 0;
}

#if 0
static int moss_is_torch_by_timeout(int timeout)
{
	if (!timeout)
		return 0;

	if (timeout >= MOSS_WDT_TIMEOUT)
		return 0;

	return -1;
}
#endif

static int moss_verify_level(int level)
{
	if (level < 0)
		level = 0;
	else if (level >= MOSS_LEVEL_NUM)
		level = MOSS_LEVEL_NUM - 1;

	return level;
}

/* flashlight enable function */
static int moss_enable(void)
{
	int ret = 0;
	enum flashlight_mode mode = FLASHLIGHT_MODE_TORCH;

	if (!flashlight_dev_ch1 || !flashlight_dev_ch2) {
		pr_info("Failed to enable since no flashlight device.\n");
		return -1;
	}

	/* set flash mode if any channel is flash mode */
	if ((moss_en_ch1 == MOSS_ENABLE_FLASH)
			|| (moss_en_ch2 == MOSS_ENABLE_FLASH))
		mode = FLASHLIGHT_MODE_FLASH;

	pr_debug("enable(%d,%d), mode:%d.\n",
		moss_en_ch1, moss_en_ch2, mode);

	/* enable channel 1 and channel 2 */
	if (moss_decouple_mode == FLASHLIGHT_SCENARIO_COUPLE &&
			moss_en_ch1 != MOSS_DISABLE &&
			moss_en_ch2 != MOSS_DISABLE) {
		pr_info("dual flash mode\n");
		if (mode == FLASHLIGHT_MODE_TORCH)
			ret |= flashlight_set_mode(
				flashlight_dev_ch1, FLASHLIGHT_MODE_DUAL_TORCH);
		else
			ret |= flashlight_set_mode(
				flashlight_dev_ch1, FLASHLIGHT_MODE_DUAL_FLASH);
	} else {
		if (moss_en_ch1)
			ret |= flashlight_set_mode(
				flashlight_dev_ch1, mode);
		else if (moss_decouple_mode == FLASHLIGHT_SCENARIO_COUPLE)
			ret |= flashlight_set_mode(
				flashlight_dev_ch1, FLASHLIGHT_MODE_OFF);
		if (moss_en_ch2)
			ret |= flashlight_set_mode(
				flashlight_dev_ch2, mode);
		else if (moss_decouple_mode == FLASHLIGHT_SCENARIO_COUPLE)
			ret |= flashlight_set_mode(
				flashlight_dev_ch2, FLASHLIGHT_MODE_OFF);
	}
	if (ret < 0)
		pr_info("Failed to enable.\n");

	return ret;
}

/* flashlight disable function */
static int moss_disable_ch1(void)
{
	int ret = 0;

	pr_debug("disable_ch1.\n");

	if (!flashlight_dev_ch1) {
		pr_info("Failed to disable since no flashlight device.\n");
		return -1;
	}

	ret |= flashlight_set_mode(flashlight_dev_ch1, FLASHLIGHT_MODE_OFF);

	if (ret < 0)
		pr_info("Failed to disable.\n");

	return ret;
}

static int moss_disable_ch2(void)
{
	int ret = 0;

	pr_debug("disable_ch2.\n");

	if (!flashlight_dev_ch2) {
		pr_info("Failed to disable since no flashlight device.\n");
		return -1;
	}

	ret |= flashlight_set_mode(flashlight_dev_ch2, FLASHLIGHT_MODE_OFF);

	if (ret < 0)
		pr_info("Failed to disable.\n");

	return ret;
}

static int moss_disable_all(void)
{
	int ret = 0;

	pr_debug("disable_ch1.\n");

	if (!flashlight_dev_ch1) {
		pr_info("Failed to disable since no flashlight device.\n");
		return -1;
	}

	ret |= flashlight_set_mode(flashlight_dev_ch1,
		FLASHLIGHT_MODE_DUAL_OFF);

	if (ret < 0)
		pr_info("Failed to disable.\n");

	return ret;
}

static int moss_disable(int channel)
{
	int ret = 0;

	if (channel == MOSS_CHANNEL_CH1)
		ret = moss_disable_ch1();
	else if (channel == MOSS_CHANNEL_CH2)
		ret = moss_disable_ch2();
	else if (channel == MOSS_CHANNEL_ALL)
		ret = moss_disable_all();
	else {
		pr_info("Error channel\n");
		return -1;
	}

	return ret;
}

/* set flashlight level */
static int moss_set_level_ch1(int level)
{
	level = moss_verify_level(level);
	moss_level_ch1 = level;

	if (!flashlight_dev_ch1) {
		pr_info("Failed to set ht level since no flashlight device.\n");
		return -1;
	}

	/* set brightness level */
	#ifdef OPLUS_FEATURE_CAMERA_COMMON
	/* 2021/02/19, lishaoyang@Camera.Tunning, add for 20001&20200 torch duty*/
	if (!moss_is_torch(level)) {
		if (is_project(19165)) {
			flashlight_set_torch_brightness(
				flashlight_dev_ch1, moss_torch_level_19165[level]);
		} else if (is_project(19131) || is_project(19132) || is_project(19133) || is_project(19420)
			|| is_project(20041) || is_project(20042)) {
			flashlight_set_torch_brightness(
				flashlight_dev_ch1, moss_torch_level_19131[level]);
                } else if (is_project(20001) || is_project(20002) || is_project(20003) || is_project(20200)){
                        flashlight_set_torch_brightness(
                                flashlight_dev_ch1, moss_torch_level_20001[level]);
		} else if (is_project(20075) || is_project(20076)) {
			flashlight_set_torch_brightness(
				flashlight_dev_ch1, moss_torch_level_20075[level]);
		} else if (is_project(0x2169E) || is_project(0x2169F) || is_project(0x216C9) || is_project(0x216CA)
                        || is_project(21711) || is_project(21712)){
			flashlight_set_torch_brightness(
				flashlight_dev_ch1, moss_torch_level_moss[level]);
		} else {
			flashlight_set_torch_brightness(
				flashlight_dev_ch1, moss_torch_level[level]);
		}
	}
	#else
	if (!moss_is_torch(level))
		flashlight_set_torch_brightness(
				flashlight_dev_ch1, moss_torch_level[level]);
	#endif
	flashlight_set_strobe_brightness(
			flashlight_dev_ch1, moss_strobe_level[level]);

	return 0;
}

static int moss_set_level_ch2(int level)
{
	level = moss_verify_level(level);
	moss_level_ch2 = level;

	if (!flashlight_dev_ch2) {
		pr_info("Failed to set lt level since no flashlight device.\n");
		return -1;
	}

	/* set brightness level */
	#ifdef OPLUS_FEATURE_CAMERA_COMMON
	/* 2021/02/19, lishaoyang@Camera.Tunning, add for 19165 torch duty*/
	if (!moss_is_torch(level)) {
		if (is_project(19165)) {
			flashlight_set_torch_brightness(
				flashlight_dev_ch2, moss_torch_level_19165[level]);
		} else {
			flashlight_set_torch_brightness(
				flashlight_dev_ch2, moss_torch_level[level]);
		}
	}
	#else
	if (!moss_is_torch(level))
		flashlight_set_torch_brightness(
				flashlight_dev_ch2, moss_torch_level[level]);
	#endif
	flashlight_set_strobe_brightness(
			flashlight_dev_ch2, moss_strobe_level[level]);

	return 0;
}

static int moss_set_level(int channel, int level)
{
	if (channel == MOSS_CHANNEL_CH1)
		moss_set_level_ch1(level);
	else if (channel == MOSS_CHANNEL_CH2)
		moss_set_level_ch2(level);
	else {
		pr_info("Error channel\n");
		return -1;
	}

	return 0;
}
int torch_enable = 0;
ssize_t enable_torch_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    return scnprintf(buf, PAGE_SIZE, "%d\n", torch_enable);
}
static int moss_operate(int channel, int enable);
ssize_t enable_torch_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
    int rc;

    rc = kstrtoint(buf, 0, &torch_enable);//echo "0" or "1"
    if (rc)
        return rc;
    if(torch_enable == 1)
    {
        pr_info("enable_torch: torch on");
        moss_operate(MOSS_CHANNEL_CH1, 0);
        moss_operate(MOSS_CHANNEL_CH2, 0);
        moss_set_level(MOSS_CHANNEL_CH1, 0x06);
        moss_set_level(MOSS_CHANNEL_CH2, 0x0);
        moss_timeout_ms[0] = 0;
        moss_operate(MOSS_CHANNEL_CH1, 1);
        moss_timeout_ms[1] = 0;
        moss_operate(MOSS_CHANNEL_CH2, 1);
    }
    else if(torch_enable == 0)
    {
        pr_info("enable_torch: torch off");
        moss_operate(MOSS_CHANNEL_CH1, 0);
        moss_operate(MOSS_CHANNEL_CH2, 0);
    }

    return count;
}
static DEVICE_ATTR(enable_torch, 0664,
    enable_torch_show,
    enable_torch_store);
static int moss_set_scenario(int scenario)
{
	/* set decouple mode */
	moss_decouple_mode = scenario & FLASHLIGHT_SCENARIO_DECOUPLE_MASK;

	/* notify charger to increase or decrease voltage */
	if (!flashlight_charger_consumer) {
		pr_info("Failed with no charger consumer handler.\n");
		return -1;
	}

	mutex_lock(&moss_mutex);
	if (scenario & FLASHLIGHT_SCENARIO_CAMERA_MASK) {
		if (!is_decrease_voltage) {
#ifdef CONFIG_MTK_CHARGER
			pr_info("Decrease voltage level.\n");
			charger_manager_enable_high_voltage_charging(
					flashlight_charger_consumer, false);
#endif
			is_decrease_voltage = 1;
		}
	} else {
		if (is_decrease_voltage) {
#ifdef CONFIG_MTK_CHARGER
			pr_info("Increase voltage level.\n");
			charger_manager_enable_high_voltage_charging(
					flashlight_charger_consumer, true);
#endif
			is_decrease_voltage = 0;
		}
	}
	mutex_unlock(&moss_mutex);

	return 0;
}

/* flashlight init */
static int moss_init(void)
{
	/* clear flashlight state */
	moss_en_ch1 = MOSS_NONE;
	moss_en_ch2 = MOSS_NONE;

	/* clear decouple mode */
	moss_decouple_mode = FLASHLIGHT_SCENARIO_COUPLE;

	/* clear charger status */
	is_decrease_voltage = 0;

	return 0;
}

/* flashlight uninit */
static int moss_uninit(void)
{
	int ret;

	/* clear flashlight state */
	moss_en_ch1 = MOSS_NONE;
	moss_en_ch2 = MOSS_NONE;

	/* clear decouple mode */
	moss_decouple_mode = FLASHLIGHT_SCENARIO_COUPLE;

	/* clear charger status */
	is_decrease_voltage = 0;

	ret = moss_disable(MOSS_CHANNEL_ALL);

	return ret;
}


/******************************************************************************
 * Timer and work queue
 *****************************************************************************/
static void moss_work_disable_ch1(struct work_struct *data)
{
	pr_debug("ht work queue callback\n");
	moss_disable(MOSS_CHANNEL_CH1);
}

static void moss_work_disable_ch2(struct work_struct *data)
{
	pr_debug("lt work queue callback\n");
	moss_disable(MOSS_CHANNEL_CH2);
}

static enum hrtimer_restart moss_timer_func_ch1(struct hrtimer *timer)
{
	schedule_work(&moss_work_ch1);
	return HRTIMER_NORESTART;
}

static enum hrtimer_restart moss_timer_func_ch2(struct hrtimer *timer)
{
	schedule_work(&moss_work_ch2);
	return HRTIMER_NORESTART;
}

static int moss_timer_start(int channel, ktime_t ktime)
{
	if (channel == MOSS_CHANNEL_CH1)
		hrtimer_start(&moss_timer_ch1, ktime, HRTIMER_MODE_REL);
	else if (channel == MOSS_CHANNEL_CH2)
		hrtimer_start(&moss_timer_ch2, ktime, HRTIMER_MODE_REL);
	else {
		pr_info("Error channel\n");
		return -1;
	}

	return 0;
}

static int moss_timer_cancel(int channel)
{
	if (channel == MOSS_CHANNEL_CH1)
		hrtimer_cancel(&moss_timer_ch1);
	else if (channel == MOSS_CHANNEL_CH2)
		hrtimer_cancel(&moss_timer_ch2);
	else {
		pr_info("Error channel\n");
		return -1;
	}

	return 0;
}

/******************************************************************************
 * Flashlight operation wrapper function
 *****************************************************************************/
static int moss_operate(int channel, int enable)
{
	ktime_t ktime;
	unsigned int s;
	unsigned int ns;

	/* setup enable/disable */
	if (channel == MOSS_CHANNEL_CH1) {
		moss_en_ch1 = enable;
		if (moss_en_ch1)
			if (moss_is_torch(moss_level_ch1))
				moss_en_ch1 = MOSS_ENABLE_FLASH;
	} else if (channel == MOSS_CHANNEL_CH2) {
		moss_en_ch2 = enable;
		if (moss_en_ch2)
			if (moss_is_torch(moss_level_ch2))
				moss_en_ch2 = MOSS_ENABLE_FLASH;
	} else {
		pr_info("Error channel\n");
		return -1;
	}

        if (is_decrease_voltage == 0) {
                Oplusimgsensor_powerstate_notify(enable);
        }

	/* decouple mode */
	if (moss_decouple_mode) {
		if (channel == MOSS_CHANNEL_CH1) {
			moss_en_ch2 = MOSS_DISABLE;
			moss_timeout_ms[MOSS_CHANNEL_CH2] = 0;
		} else if (channel == MOSS_CHANNEL_CH2) {
			moss_en_ch1 = MOSS_DISABLE;
			moss_timeout_ms[MOSS_CHANNEL_CH1] = 0;
		}
	}

	pr_debug("en_ch(%d,%d), decouple:%d\n",
		moss_en_ch1, moss_en_ch2, moss_decouple_mode);

	/* operate flashlight and setup timer */
	if ((moss_en_ch1 != MOSS_NONE) && (moss_en_ch2 != MOSS_NONE)) {
		if ((moss_en_ch1 == MOSS_DISABLE) &&
				(moss_en_ch2 == MOSS_DISABLE)) {
			if (moss_decouple_mode) {
				if (channel == MOSS_CHANNEL_CH1) {
					moss_disable(MOSS_CHANNEL_CH1);
					moss_timer_cancel(MOSS_CHANNEL_CH1);
				} else if (channel == MOSS_CHANNEL_CH2) {
					moss_disable(MOSS_CHANNEL_CH2);
					moss_timer_cancel(MOSS_CHANNEL_CH2);
				}
			} else {
				moss_disable(MOSS_CHANNEL_ALL);
				moss_timer_cancel(MOSS_CHANNEL_CH1);
				moss_timer_cancel(MOSS_CHANNEL_CH2);
			}
		} else {
			if (moss_timeout_ms[MOSS_CHANNEL_CH1] &&
				moss_en_ch1 != MOSS_DISABLE) {
				s = moss_timeout_ms[MOSS_CHANNEL_CH1] /
					1000;
				ns = moss_timeout_ms[MOSS_CHANNEL_CH1] %
					1000 * 1000000;
				ktime = ktime_set(s, ns);
				moss_timer_start(MOSS_CHANNEL_CH1, ktime);
			}
			if (moss_timeout_ms[MOSS_CHANNEL_CH2] &&
				moss_en_ch2 != MOSS_DISABLE) {
				s = moss_timeout_ms[MOSS_CHANNEL_CH2] /
					1000;
				ns = moss_timeout_ms[MOSS_CHANNEL_CH2] %
					1000 * 1000000;
				ktime = ktime_set(s, ns);
				moss_timer_start(MOSS_CHANNEL_CH2, ktime);
			}
			moss_enable();
		}

		/* clear flashlight state */
		moss_en_ch1 = MOSS_NONE;
		moss_en_ch2 = MOSS_NONE;
	}

	return 0;
}

/******************************************************************************
 * Flashlight operations
 *****************************************************************************/
static int moss_ioctl(unsigned int cmd, unsigned long arg)
{
	struct flashlight_dev_arg *fl_arg;
	int channel;

	fl_arg = (struct flashlight_dev_arg *)arg;
	channel = fl_arg->channel;

	/* verify channel */
	if (channel < 0 || channel >= MOSS_CHANNEL_NUM) {
		pr_info("Failed with error channel\n");
		return -EINVAL;
	}

	switch (cmd) {
	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		pr_debug("FLASH_IOC_SET_TIME_OUT_TIME_MS(%d): %d\n",
				channel, (int)fl_arg->arg);
		moss_timeout_ms[channel] = fl_arg->arg;
		break;

	case FLASH_IOC_SET_DUTY:
		pr_debug("FLASH_IOC_SET_DUTY(%d): %d\n",
				channel, (int)fl_arg->arg);
		moss_set_level(channel, fl_arg->arg);
		break;

	case FLASH_IOC_SET_SCENARIO:
		pr_debug("FLASH_IOC_SET_SCENARIO(%d): %d\n",
				channel, (int)fl_arg->arg);
		moss_set_scenario(fl_arg->arg);
		break;

	case FLASH_IOC_SET_ONOFF:
		pr_debug("FLASH_IOC_SET_ONOFF(%d): %d\n",
				channel, (int)fl_arg->arg);
		moss_operate(channel, fl_arg->arg);
		break;

	case FLASH_IOC_IS_CHARGER_READY:
		pr_debug("FLASH_IOC_IS_CHARGER_READY(%d)\n", channel);
		fl_arg->arg = moss_is_charger_ready();
		pr_debug("FLASH_IOC_IS_CHARGER_READY(%d)\n", fl_arg->arg);
		break;

	case FLASH_IOC_GET_DUTY_NUMBER:
		pr_debug("FLASH_IOC_GET_DUTY_NUMBER(%d)\n", channel);
		fl_arg->arg = MOSS_LEVEL_NUM;
		break;

	case FLASH_IOC_GET_MAX_TORCH_DUTY:
		pr_debug("FLASH_IOC_GET_MAX_TORCH_DUTY(%d)\n", channel);
		fl_arg->arg = MOSS_LEVEL_TORCH - 1;
		break;

	case FLASH_IOC_GET_DUTY_CURRENT:
		fl_arg->arg = moss_verify_level(fl_arg->arg);
		pr_debug("FLASH_IOC_GET_DUTY_CURRENT(%d): %d\n",
				channel, (int)fl_arg->arg);
		fl_arg->arg = moss_current[fl_arg->arg];
		break;

	case FLASH_IOC_GET_HW_TIMEOUT:
		pr_debug("FLASH_IOC_GET_HW_TIMEOUT(%d)\n", channel);
		fl_arg->arg = MOSS_HW_TIMEOUT;
		break;

	default:
		pr_info("No such command and arg(%d): (%d, %d)\n",
				channel, _IOC_NR(cmd), (int)fl_arg->arg);
		return -ENOTTY;
	}

	return 0;
}

static int moss_open(void)
{
	/* Move to set driver for saving power */
	mutex_lock(&moss_mutex);
	fd_use_count++;
	pr_debug("open driver: %d\n", fd_use_count);
	mutex_unlock(&moss_mutex);
	return 0;
}

static int moss_release(void)
{
	/* Move to set driver for saving power */
	mutex_lock(&moss_mutex);
	fd_use_count--;
	pr_debug("close driver: %d\n", fd_use_count);
	/* If camera NE, we need to enable pe by ourselves*/
	if (fd_use_count == 0 && is_decrease_voltage) {
#ifdef CONFIG_MTK_CHARGER
		pr_info("Increase voltage level.\n");
		charger_manager_enable_high_voltage_charging(
				flashlight_charger_consumer, true);
#endif
		is_decrease_voltage = 0;
	}
	mutex_unlock(&moss_mutex);
	return 0;
}

static int moss_set_driver(int set)
{
	int ret = 0;

	/* set chip and usage count */
	mutex_lock(&moss_mutex);
	if (set) {
		if (!use_count)
			ret = moss_init();
		use_count++;
		pr_debug("Set driver: %d\n", use_count);
	} else {
		use_count--;
		if (!use_count)
			ret = moss_uninit();
		if (use_count < 0)
			use_count = 0;
		pr_debug("Unset driver: %d\n", use_count);
	}
	mutex_unlock(&moss_mutex);

	return ret;
}

static ssize_t moss_strobe_store(struct flashlight_arg arg)
{
	moss_set_driver(1);
	if (arg.decouple)
		moss_set_scenario(
			FLASHLIGHT_SCENARIO_CAMERA |
			FLASHLIGHT_SCENARIO_DECOUPLE);
	else
		moss_set_scenario(
			FLASHLIGHT_SCENARIO_CAMERA |
			FLASHLIGHT_SCENARIO_COUPLE);
	moss_set_level(arg.channel, arg.level);
	moss_timeout_ms[arg.channel] = 0;

	if (arg.level < 0)
		moss_operate(arg.channel, MOSS_DISABLE);
	else
		moss_operate(arg.channel, MOSS_ENABLE);

	msleep(arg.dur);
	if (arg.decouple)
		moss_set_scenario(
			FLASHLIGHT_SCENARIO_FLASHLIGHT |
			FLASHLIGHT_SCENARIO_DECOUPLE);
	else
		moss_set_scenario(
			FLASHLIGHT_SCENARIO_FLASHLIGHT |
			FLASHLIGHT_SCENARIO_COUPLE);
	moss_operate(arg.channel, MOSS_DISABLE);
	moss_set_driver(0);

	return 0;
}

static struct flashlight_operations moss_ops = {
	moss_open,
	moss_release,
	moss_ioctl,
	moss_strobe_store,
	moss_set_driver
};


/******************************************************************************
 * Platform device and driver
 *****************************************************************************/
static int moss_parse_dt(struct device *dev,
		struct moss_platform_data *pdata)
{
	struct device_node *np, *cnp;
	u32 decouple = 0;
	int i = 0;

	if (!dev || !dev->of_node || !pdata)
		return -ENODEV;

	np = dev->of_node;

	pdata->channel_num = of_get_child_count(np);
	if (!pdata->channel_num) {
		pr_info("Parse no dt, node.\n");
		return 0;
	}
	pr_info("Channel number(%d).\n", pdata->channel_num);

	if (of_property_read_u32(np, "decouple", &decouple))
		pr_info("Parse no dt, decouple.\n");

	pdata->dev_id = devm_kzalloc(dev,
			pdata->channel_num *
			sizeof(struct flashlight_device_id),
			GFP_KERNEL);
	if (!pdata->dev_id)
		return -ENOMEM;

	for_each_child_of_node(np, cnp) {
		if (of_property_read_u32(cnp, "type", &pdata->dev_id[i].type))
			goto err_node_put;
		if (of_property_read_u32(cnp, "ct", &pdata->dev_id[i].ct))
			goto err_node_put;
		if (of_property_read_u32(cnp, "part", &pdata->dev_id[i].part))
			goto err_node_put;
		snprintf(pdata->dev_id[i].name, FLASHLIGHT_NAME_SIZE,
				MOSS_NAME);
		pdata->dev_id[i].channel = i;
		pdata->dev_id[i].decouple = decouple;

		pr_info("Parse dt (type,ct,part,name,channel,decouple)=(%d,%d,%d,%s,%d,%d).\n",
				pdata->dev_id[i].type, pdata->dev_id[i].ct,
				pdata->dev_id[i].part, pdata->dev_id[i].name,
				pdata->dev_id[i].channel,
				pdata->dev_id[i].decouple);
		i++;
	}

	return 0;

err_node_put:
	of_node_put(cnp);
	return -EINVAL;
}

static int moss_probe(struct platform_device *pdev)
{
	struct moss_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int ret;
	int i;

	pr_debug("Probe start.\n");

	/* parse dt */
	if (!pdata) {
		pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		pdev->dev.platform_data = pdata;
		ret = moss_parse_dt(&pdev->dev, pdata);
		if (ret)
			return ret;
	}

	/* init work queue */
	INIT_WORK(&moss_work_ch1, moss_work_disable_ch1);
	INIT_WORK(&moss_work_ch2, moss_work_disable_ch2);

	/* init timer */
	hrtimer_init(&moss_timer_ch1, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	moss_timer_ch1.function = moss_timer_func_ch1;
	hrtimer_init(&moss_timer_ch2, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	moss_timer_ch2.function = moss_timer_func_ch2;
	moss_timeout_ms[MOSS_CHANNEL_CH1] = 600;
	moss_timeout_ms[MOSS_CHANNEL_CH2] = 600;

	/* clear attributes */
	use_count = 0;
	fd_use_count = 0;
	is_decrease_voltage = 0;

	/* get RTK flashlight handler */
	flashlight_dev_ch1 = find_flashlight_by_name(RT_FLED_DEVICE_CH1);
	if (!flashlight_dev_ch1) {
		pr_info("Failed to get ht flashlight device.\n");
		return -EFAULT;
	}
	flashlight_dev_ch2 = find_flashlight_by_name(RT_FLED_DEVICE_CH2);
	if (!flashlight_dev_ch2) {
		pr_info("Failed to get lt flashlight device.\n");
		return -EFAULT;
	}

	/* setup strobe mode timeout */
	if (flashlight_set_strobe_timeout(flashlight_dev_ch1,
				MOSS_HW_TIMEOUT, MOSS_HW_TIMEOUT + 200) < 0)
		pr_info("Failed to set strobe timeout.\n");

	/* get charger consumer manager */
	flashlight_charger_consumer = charger_manager_get_by_name(
			&flashlight_dev_ch1->dev, CHARGER_SUPPLY_NAME);
	if (!flashlight_charger_consumer) {
		pr_info("Failed to get charger manager.\n");
		return -EFAULT;
	}

	/* register flashlight device */
	if (pdata->channel_num) {
		for (i = 0; i < pdata->channel_num; i++)
			if (flashlight_dev_register_by_device_id(
						&pdata->dev_id[i],
						&moss_ops))
				return -EFAULT;
	} else {
		if (flashlight_dev_register(MOSS_NAME, &moss_ops))
			return -EFAULT;
	}

        ret = device_create_file(&pdev->dev, &dev_attr_enable_torch);
	pr_debug("Probe done.\n");

	return 0;
}

static int moss_remove(struct platform_device *pdev)
{
	struct moss_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int i;

	pr_debug("Remove start.\n");

	pdev->dev.platform_data = NULL;

	/* unregister flashlight device */
	if (pdata && pdata->channel_num)
		for (i = 0; i < pdata->channel_num; i++)
			flashlight_dev_unregister_by_device_id(
					&pdata->dev_id[i]);
	else
		flashlight_dev_unregister(MOSS_NAME);

	/* flush work queue */
	flush_work(&moss_work_ch1);
	flush_work(&moss_work_ch2);

	/* clear RTK flashlight device */
	flashlight_dev_ch1 = NULL;
	flashlight_dev_ch2 = NULL;

	pr_debug("Remove done.\n");

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id moss_of_match[] = {
	{.compatible = MOSS_DTNAME},
	{},
};
MODULE_DEVICE_TABLE(of, moss_of_match);
#else
static struct platform_device moss_platform_device[] = {
	{
		.name = MOSS_NAME,
		.id = 0,
		.dev = {}
	},
	{}
};
MODULE_DEVICE_TABLE(platform, moss_platform_device);
#endif

static struct platform_driver moss_platform_driver = {
	.probe = moss_probe,
	.remove = moss_remove,
	.driver = {
		.name = MOSS_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = moss_of_match,
#endif
	},
};

static int __init flashlight_moss_init(void)
{
	int ret;

	pr_debug("Init start.\n");

#ifndef CONFIG_OF
	ret = platform_device_register(&moss_platform_device);
	if (ret) {
		pr_info("Failed to register platform device\n");
		return ret;
	}
#endif

	ret = platform_driver_register(&moss_platform_driver);
	if (ret) {
		pr_info("Failed to register platform driver\n");
		return ret;
	}

	pr_debug("Init done.\n");

	return 0;
}

static void __exit flashlight_moss_exit(void)
{
	pr_debug("Exit start.\n");

	platform_driver_unregister(&moss_platform_driver);

	pr_debug("Exit done.\n");
}

/* replace module_init() since conflict in kernel init process */
late_initcall(flashlight_moss_init);
module_exit(flashlight_moss_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("youjintong@vanyol.com");
MODULE_DESCRIPTION("MTK Flashlight MOSS Driver");
