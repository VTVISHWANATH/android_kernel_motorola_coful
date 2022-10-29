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
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>

#include <mt-plat/mtk_boot_common.h>
#include "ccci_debug.h"
#include "ccci_config.h"
#include "ccci_modem.h"
#include "ccci_swtp.h"
#include "ccci_fsm.h"

const struct of_device_id swtp_of_match[] = {
	{ .compatible = SWTP_COMPATIBLE_DEVICE_ID, },
	{ .compatible = SWTP1_COMPATIBLE_DEVICE_ID,},
#ifdef KYOTO_SWTP_CUST
	// modify by wt.changtingting for swtp start
	{ .compatible = SWTP2_COMPATIBLE_DEVICE_ID, },
	{ .compatible = SWTP3_COMPATIBLE_DEVICE_ID, },
	// modify by wt.changtingting for swtp end
#endif
	{},
};

#ifdef KYOTO_SWTP_CUST
// modify by wt.changtingting for swtp start
static const char irq_name[][16] = {
	"swtp0-eint",
	"swtp1-eint",
	"swtp2-eint",
	"swtp3-eint"
};
// modify by wt.changtingting for swtp end
#endif

#define SWTP_MAX_SUPPORT_MD 1
struct swtp_t swtp_data[SWTP_MAX_SUPPORT_MD];
static const char rf_name[] = "RF_cable";
// modify by wt.changtingting for swtp start
#define MAX_RETRY_CNT 30
// modify by wt.changtingting for swtp end

#if defined(CORFU_SWTP_GPIO_STATE_CUST) || defined(COFUD_SWTP_GPIO_STATE_CUST) || defined(COFUL_SWTP_GPIO_STATE_CUST) || defined(MILAN_SWTP_GPIO_STATE_CUST)
static u16 swtp_gpio_pin = 0;
static ssize_t swtp_gpio_state_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	int ret;
	ret = gpio_get_value_cansleep(swtp_gpio_pin);
	//CCCI_LEGACY_ERR_LOG(-1, SYS,"%s,ret:%d,gpio:%d\n", __func__, ret, swtp_gpio_pin);
	return sprintf(buf, "%d\n", ret);
}

static CLASS_ATTR_RO(swtp_gpio_state);

static struct class swtp_class[] = {
    {
        .name    = "swtp",
        .owner   = THIS_MODULE,
    },
    {
        .name    = "swtp1",
        .owner   = THIS_MODULE,
    }
};
#endif

static int swtp_send_tx_power(struct swtp_t *swtp)
{
	unsigned long flags;
	int power_mode, ret = 0;

	if (swtp == NULL) {
		CCCI_LEGACY_ERR_LOG(-1, SYS, "%s:swtp is null\n", __func__);
		return -1;
	}

	spin_lock_irqsave(&swtp->spinlock, flags);

	ret = exec_ccci_kern_func_by_md_id(swtp->md_id, ID_UPDATE_TX_POWER,
		(char *)&swtp->tx_power_mode, sizeof(swtp->tx_power_mode));
	power_mode = swtp->tx_power_mode;
	spin_unlock_irqrestore(&swtp->spinlock, flags);

	if (ret != 0)
		CCCI_LEGACY_ERR_LOG(swtp->md_id, SYS,
			"%s to MD%d,state=%d,ret=%d\n",
			__func__, swtp->md_id + 1, power_mode, ret);

	return ret;
}

#ifndef DISABLE_SWTP_FACTORY
static int swtp_switch_state(int irq, struct swtp_t *swtp)
{
	unsigned long flags;
	int i;

	if (swtp == NULL) {
		CCCI_LEGACY_ERR_LOG(-1, SYS, "%s:data is null\n", __func__);
		return -1;
	}

	spin_lock_irqsave(&swtp->spinlock, flags);
	for (i = 0; i < MAX_PIN_NUM; i++) {
		if (swtp->irq[i] == irq)
			break;
	}
	if (i == MAX_PIN_NUM) {
		spin_unlock_irqrestore(&swtp->spinlock, flags);
		CCCI_LEGACY_ERR_LOG(-1, SYS,
			"%s:can't find match irq\n", __func__);
		return -1;
	}

	if (swtp->eint_type[i] == IRQ_TYPE_LEVEL_LOW) {
		irq_set_irq_type(swtp->irq[i], IRQ_TYPE_LEVEL_HIGH);
		swtp->eint_type[i] = IRQ_TYPE_LEVEL_HIGH;
	} else {
		irq_set_irq_type(swtp->irq[i], IRQ_TYPE_LEVEL_LOW);
		swtp->eint_type[i] = IRQ_TYPE_LEVEL_LOW;
	}

	if (swtp->gpio_state[i] == SWTP_EINT_PIN_PLUG_IN)
		swtp->gpio_state[i] = SWTP_EINT_PIN_PLUG_OUT;
	else
		swtp->gpio_state[i] = SWTP_EINT_PIN_PLUG_IN;

	swtp->tx_power_mode = SWTP_NO_TX_POWER;
#ifdef KYOTO_SWTP_CUST
	// modify by wt.changtingting for swtp start
	/* show gpio state */
	if ((swtp->gpio_state[0] == SWTP_EINT_PIN_PLUG_IN)&&(swtp->gpio_state[1] == SWTP_EINT_PIN_PLUG_IN)&&(swtp->gpio_state[2] == SWTP_EINT_PIN_PLUG_OUT)&&(swtp->gpio_state[3] == SWTP_EINT_PIN_PLUG_OUT)) {
		swtp->tx_power_mode = SWTP_DO_TX_POWER;
		CCCI_LEGACY_ERR_LOG(swtp->md_id, SYS,
			"--------SWTP_DO_TX_POWER----------%s>>tx_power_mode = %d,gpio_state:Ant0=%d, Ant1=%d, Ant5=%d, Ant4=%d\n",
			__func__, swtp->tx_power_mode, swtp->gpio_state[0], swtp->gpio_state[1], swtp->gpio_state[2], swtp->gpio_state[3]);
	} else {
		swtp->tx_power_mode = SWTP_NO_TX_POWER;
		CCCI_LEGACY_ERR_LOG(swtp->md_id, SYS,
			"--------SWTP_NO_TX_POWER----------%s>>tx_power_mode = %d,gpio_state:Ant0=%d, Ant1=%d, Ant5=%d, Ant4=%d\n",
			__func__, swtp->tx_power_mode, swtp->gpio_state[0], swtp->gpio_state[1], swtp->gpio_state[2], swtp->gpio_state[3]);
	}
        // modify by wt.changtingting for swtp end
#else
	for (i = 0; i < MAX_PIN_NUM; i++) {
		if (swtp->gpio_state[i] == SWTP_EINT_PIN_PLUG_IN) {
			swtp->tx_power_mode = SWTP_DO_TX_POWER;
			break;
		}
	}
#endif
	inject_pin_status_event(swtp->tx_power_mode, rf_name);
	spin_unlock_irqrestore(&swtp->spinlock, flags);

	return swtp->tx_power_mode;
}
#endif
static void swtp_send_tx_power_state(struct swtp_t *swtp)
{
	int ret = 0;

	if (!swtp) {
		CCCI_LEGACY_ERR_LOG(-1, SYS,
			"%s:swtp is null\n", __func__);
		return;
	}

	if (swtp->md_id == 0) {
		ret = swtp_send_tx_power(swtp);
		if (ret < 0) {
			CCCI_LEGACY_ERR_LOG(swtp->md_id, SYS,
				"%s send tx power failed, ret=%d, schedule delayed work\n",
				__func__, ret);
			schedule_delayed_work(&swtp->delayed_work, 5 * HZ);
		}
	} else
		CCCI_LEGACY_ERR_LOG(swtp->md_id, SYS,
			"%s:md is no support\n", __func__);

}
#ifndef DISABLE_SWTP_FACTORY
static irqreturn_t swtp_irq_handler(int irq, void *data)
{
	struct swtp_t *swtp = (struct swtp_t *)data;
	int ret = 0;

	ret = swtp_switch_state(irq, swtp);
	if (ret < 0) {
		CCCI_LEGACY_ERR_LOG(swtp->md_id, SYS,
			"%s swtp_switch_state failed in irq, ret=%d\n",
			__func__, ret);
	} else
		swtp_send_tx_power_state(swtp);

	return IRQ_HANDLED;
}
#endif

static void swtp_tx_delayed_work(struct work_struct *work)
{
	struct swtp_t *swtp = container_of(to_delayed_work(work),
		struct swtp_t, delayed_work);
	int ret, retry_cnt = 0;

	while (retry_cnt < MAX_RETRY_CNT) {
		ret = swtp_send_tx_power(swtp);
		if (ret != 0) {
			msleep(2000);
			retry_cnt++;
		} else
			break;
	}
}

int swtp_md_tx_power_req_hdlr(int md_id, int data)
{
	struct swtp_t *swtp = NULL;
#ifdef DISABLE_SWTP_FACTORY
        unsigned long flags;
#endif
	if (md_id < 0 || md_id >= SWTP_MAX_SUPPORT_MD) {
		CCCI_LEGACY_ERR_LOG(md_id, SYS,
		"%s:md_id=%d not support\n",
		__func__, md_id);
		return -1;
	}

	swtp = &swtp_data[md_id];
#ifdef DISABLE_SWTP_FACTORY
       /*default do no tx power for special use*/
       spin_lock_irqsave(&swtp->spinlock, flags);
       swtp->tx_power_mode = SWTP_NO_TX_POWER;
       spin_unlock_irqrestore(&swtp->spinlock, flags);
#endif
	swtp_send_tx_power_state(swtp);

	return 0;
}

static void swtp_init_delayed_work(struct work_struct *work)
{
	struct swtp_t *swtp = container_of(to_delayed_work(work),
		struct swtp_t, init_delayed_work);
	int md_id;
	int i, ret = 0;
#ifdef CONFIG_MTK_EIC
	u32 ints[2] = { 0, 0 };
#else
	u32 ints[1] = { 0 };
#endif
	u32 ints1[2] = { 0, 0 };
	struct device_node *node = NULL;

	CCCI_NORMAL_LOG(-1, SYS, "%s start\n", __func__);
	CCCI_BOOTUP_LOG(-1, SYS, "%s start\n", __func__);

	if (!swtp) {
		ret = -1;
		goto SWTP_INIT_END;
	}
	md_id = swtp->md_id;

	if (md_id < 0 || md_id >= SWTP_MAX_SUPPORT_MD) {
		ret = -2;
		CCCI_LEGACY_ERR_LOG(-1, SYS,
			"%s: invalid md_id = %d\n", __func__, md_id);
		goto SWTP_INIT_END;
	}

#ifdef KYOTO_SWTP_CUST
	// modify by wt.changtingting for swtp start
	if (ARRAY_SIZE(swtp_of_match) < MAX_PIN_NUM || ARRAY_SIZE(irq_name) < MAX_PIN_NUM) {
		ret = -5;
		CCCI_LEGACY_ERR_LOG(-1, SYS,
			"%s: invalid array count = %d(swtp_of_match), %d(irq_name)\n", __func__, ARRAY_SIZE(swtp_of_match) ,ARRAY_SIZE(irq_name));
		goto SWTP_INIT_END;
	}
	// modify by wt.changtingting for swtp end
#endif
	for (i = 0; i < MAX_PIN_NUM; i++)
		swtp_data[md_id].gpio_state[i] = SWTP_EINT_PIN_PLUG_OUT;

	for (i = 0; i < MAX_PIN_NUM; i++) {
		node = of_find_matching_node(NULL, &swtp_of_match[i]);
		if (node) {
			ret = of_property_read_u32_array(node, "debounce",
				ints, ARRAY_SIZE(ints));
			if (ret) {
				CCCI_LEGACY_ERR_LOG(md_id, SYS,
					"%s:swtp%d get debounce fail\n",
					__func__, i);
#ifdef KYOTO_SWTP_CUST
				// modify by wt.changtingting for swtp start
				// break;
				ints[0] = 512000;
				// modify by wt.changtingting for swtp end
#else
                                break;
#endif
			}

			ret = of_property_read_u32_array(node, "interrupts",
				ints1, ARRAY_SIZE(ints1));
			if (ret) {
				CCCI_LEGACY_ERR_LOG(md_id, SYS,
					"%s:swtp%d get interrupts fail\n",
					__func__, i);
				break;
			}
#ifdef CONFIG_MTK_EIC /* for chips before mt6739 */
			swtp_data[md_id].gpiopin[i] = ints[0];
			swtp_data[md_id].setdebounce[i] = ints[1];
#else /* for mt6739,and chips after mt6739 */
			swtp_data[md_id].setdebounce[i] = ints[0];
			swtp_data[md_id].gpiopin[i] =
				of_get_named_gpio(node, "deb-gpios", 0);
#endif
			gpio_set_debounce(swtp_data[md_id].gpiopin[i],
				swtp_data[md_id].setdebounce[i]);
			swtp_data[md_id].eint_type[i] = ints1[1];
			swtp_data[md_id].irq[i] = irq_of_parse_and_map(node, 0);

#if defined(CORFU_SWTP_GPIO_STATE_CUST) || defined(COFUD_SWTP_GPIO_STATE_CUST) || defined(COFUL_SWTP_GPIO_STATE_CUST) || defined(MILAN_SWTP_GPIO_STATE_CUST)
			if(swtp_data[md_id].gpiopin[i] != 0){
				swtp_gpio_pin = swtp_data[md_id].gpiopin[i];
				ret = class_register(&swtp_class[i]);
				ret = class_create_file(&swtp_class[i], &class_attr_swtp_gpio_state);
			}
#endif

#ifdef KYOTO_SWTP_CUST
			// modify by wt.changtingting for swtp start
			/*ret = request_irq(swtp_data[md_id].irq[i],
				swtp_irq_handler, IRQF_TRIGGER_NONE,
				(i == 0 ? "swtp0-eint" : "swtp1-eint"),
				&swtp_data[md_id]);*/
			ret = request_irq(swtp_data[md_id].irq[i],
				swtp_irq_handler, IRQF_TRIGGER_NONE,
				irq_name[i],
				&swtp_data[md_id]);
			// modify by wt.changtingting for swtp end
#else
#ifndef DISABLE_SWTP_FACTORY
			ret = request_irq(swtp_data[md_id].irq[i],
				swtp_irq_handler, IRQF_TRIGGER_NONE,
				(i == 0 ? "swtp0-eint" : "swtp1-eint"),
				&swtp_data[md_id]);
#endif
#endif
			if (ret) {
				CCCI_LEGACY_ERR_LOG(md_id, SYS,
					"swtp%d-eint IRQ LINE NOT AVAILABLE\n",
					i);
				break;
			}
		} else {
			CCCI_LEGACY_ERR_LOG(md_id, SYS,
				"%s:can't find swtp%d compatible node\n",
				__func__, i);
			ret = -3;
		}
	}
	register_ccci_sys_call_back(md_id, MD_SW_MD1_TX_POWER_REQ,
		swtp_md_tx_power_req_hdlr);

SWTP_INIT_END:
	CCCI_BOOTUP_LOG(md_id, SYS, "%s end: ret = %d\n", __func__, ret);
	CCCI_NORMAL_LOG(md_id, SYS, "%s end: ret = %d\n", __func__, ret);

	return;
}

int swtp_init(int md_id)
{
	/* parameter check */
	if (md_id < 0 || md_id >= SWTP_MAX_SUPPORT_MD) {
		CCCI_LEGACY_ERR_LOG(-1, SYS,
			"%s: invalid md_id = %d\n", __func__, md_id);
		return -1;
	}
	/* init woke setting */
	swtp_data[md_id].md_id = md_id;

	INIT_DELAYED_WORK(&swtp_data[md_id].init_delayed_work,
		swtp_init_delayed_work);
	/* tx work setting */
	INIT_DELAYED_WORK(&swtp_data[md_id].delayed_work,
		swtp_tx_delayed_work);
	swtp_data[md_id].tx_power_mode = SWTP_NO_TX_POWER;

	spin_lock_init(&swtp_data[md_id].spinlock);

	/* schedule init work */
	schedule_delayed_work(&swtp_data[md_id].init_delayed_work, HZ);

	CCCI_BOOTUP_LOG(md_id, SYS, "%s end, init_delayed_work scheduled\n",
		__func__);
	return 0;
}
