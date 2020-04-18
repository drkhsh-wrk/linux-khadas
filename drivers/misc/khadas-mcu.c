/*
 * Khadas Edge MCU control driver
 *
 * Written by: Nick <nick@khadas.com>
 *
 * Copyright (C) 2019 Wesion Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/ctype.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>



/* Device registers */
#define MCU_WOL_REG		0x21
#define MCU_RST_REG             0x2c
#define MCU_LAN_MAC_ID  0x6
#define MCU_AGEING_TEST	0x35
#define MCU_LAN_MAC_SWITCH  0x2d


struct mcu_data {
	struct i2c_client *client;
	struct class *wol_class;
	int wol_enable;
};

struct mcu_data *g_mcu_data;
int ageing_test_flag = 0;
extern void set_test(int flag);
extern void realtek_enable_wol(int enable, bool is_shutdown);
extern int StringToHex(char *str, unsigned char *out, unsigned int *outlen);
extern void HexToAscii(unsigned char *pHex, unsigned char *pAscii, int nLen);

void mcu_enable_wol(int enable, bool is_shutdown)
{
	realtek_enable_wol(enable, is_shutdown);
}

static int i2c_master_reg8_send(const struct i2c_client *client,
		const char reg, const char *buf, int count)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	int ret;
	char *tx_buf = kzalloc(count + 1, GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;
	tx_buf[0] = reg;
	memcpy(tx_buf+1, buf, count);

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = count + 1;
	msg.buf = (char *)tx_buf;

	ret = i2c_transfer(adap, &msg, 1);
	kfree(tx_buf);
	return (ret == 1) ? count : ret;
}

static int i2c_master_reg8_recv(const struct i2c_client *client,
		const char reg, char *buf, int count)
{
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	char reg_buf = reg;

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 1;
	msgs[0].buf = &reg_buf;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = count;
	msgs[1].buf = (char *)buf;

	ret = i2c_transfer(adap, msgs, 2);

	return (ret == 2) ? count : ret;
}

static int mcu_i2c_read_regs(struct i2c_client *client,
		u8 reg, u8 buf[], unsigned len)
{
	int ret;
	ret = i2c_master_reg8_recv(client, reg, buf, len);
	return ret;
}

static int mcu_i2c_write_regs(struct i2c_client *client,
		u8 reg, u8 const buf[], __u16 len)
{
	int ret;
	ret = i2c_master_reg8_send(client, reg, buf, (int)len);
	return ret;
}

static ssize_t store_test(struct class *cls, struct class_attribute *attr,
		        const char *buf, size_t count)
{
	int flag;

	if (kstrtoint(buf, 0, &flag))
		return -EINVAL;

	set_test(flag);
	return count;
}

static ssize_t store_rst_mcu(struct class *cls, struct class_attribute *attr,
		        const char *buf, size_t count)
{
	u8 reg[2];
	int ret;
	int rst;

	if (kstrtoint(buf, 0, &rst))
		return -EINVAL;

	reg[0] = rst;
	ret = mcu_i2c_write_regs(g_mcu_data->client, MCU_RST_REG, reg, 1);
	if (ret < 0) {
		printk("rst mcu err\n");
		return ret;
	}
	return count;
}

static ssize_t store_wol_enable(struct class *cls, struct class_attribute *attr,
		        const char *buf, size_t count)
{
	u8 reg[2];
	int ret;
	int enable;
	int state;

	if (kstrtoint(buf, 0, &enable))
		return -EINVAL;

	ret = mcu_i2c_read_regs(g_mcu_data->client, MCU_WOL_REG, reg, 1);
	if (ret < 0) {
		printk("write wol state err\n");
		return ret;
	}
	state = (int)reg[0];
	reg[0] = enable | (state & 0x02);
	ret = mcu_i2c_write_regs(g_mcu_data->client, MCU_WOL_REG, reg, 1);
	if (ret < 0) {
		printk("write wol state err\n");
		return ret;
	}

	g_mcu_data->wol_enable = reg[0];
	mcu_enable_wol(g_mcu_data->wol_enable, false);

	printk("write wol state: %d\n", g_mcu_data->wol_enable);
	return count;
}

static ssize_t show_wol_enable(struct class *cls,
		        struct class_attribute *attr, char *buf)
{
	int enable;
	enable = g_mcu_data->wol_enable & 0x01;
	return sprintf(buf, "%d\n", enable);
}

static ssize_t show_mac_addr(struct class *cls,
				struct class_attribute *attr, char *buf)
{
	int ret;
	unsigned char addr_Ascii[12]={0};
	unsigned char addr[6]={0};
	int i;
	
	for(i=0; i<=5; i++){
		ret = mcu_i2c_read_regs(g_mcu_data->client, MCU_LAN_MAC_ID+i, &addr[i], 1);
		if (ret < 0) 
			printk("%s: mac address failed (%d)",__func__, ret);
		//printk("%s: mac address: %02x\n",__func__, addr[i]);
	}
	
	HexToAscii(addr,addr_Ascii,6);
	printk("mac address (%s)\n", addr_Ascii);

	return sprintf(buf, "%s\n", addr_Ascii);
}	

static ssize_t store_mac_addr(struct class *cls, struct class_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	char addr_Ascii[12]={0};
	unsigned char addr[6]={0};
	unsigned char pasd[1]={0};
	int outlen = 0;
	int i;
	//81 1
	//82 73 61 64 61 68 4B
	//81 0

	addr[0] = 1;
	mcu_i2c_write_regs(g_mcu_data->client, 0x81, addr, 1);
	pasd[0] = 0x73;
	mcu_i2c_write_regs(g_mcu_data->client, 0x82, pasd, 1);
	pasd[0] = 0x61;
	mcu_i2c_write_regs(g_mcu_data->client, 0x82, pasd, 1);
	pasd[0] = 0x64;
	mcu_i2c_write_regs(g_mcu_data->client, 0x82, pasd, 1);
	pasd[0] = 0x61;
	mcu_i2c_write_regs(g_mcu_data->client, 0x82, pasd, 1);
	pasd[0] = 0x68;
	mcu_i2c_write_regs(g_mcu_data->client, 0x82, pasd, 1);
	pasd[0] = 0x4B;
	mcu_i2c_write_regs(g_mcu_data->client, 0x82, pasd, 1);
	addr[0] = 0;
	mcu_i2c_write_regs(g_mcu_data->client, 0x81, addr, 1);
	
	sscanf(buf,"%s",addr_Ascii);

	StringToHex(addr_Ascii,addr,&outlen);
	printk("mac address: %02x:%02x:%02x:%02x:%02x:%02x\n",
			addr[0], addr[1], addr[2],
			addr[3], addr[4], addr[5]);
	for(i=0; i<=5; i++){
		ret = mcu_i2c_write_regs(g_mcu_data->client, MCU_LAN_MAC_ID+i, &addr[i], 1);
		if (ret < 0)
			printk("%s: mac address failed (%d)\n",__func__, ret);
	}
	addr[0] = 1;
	ret = mcu_i2c_write_regs(g_mcu_data->client, MCU_LAN_MAC_SWITCH, addr, 1);
	if (ret < 0)
		printk("%s: mac address failed (%d)\n",__func__, ret);	

	return count;
}

static ssize_t show_ageing_test(struct class *cls,
				struct class_attribute *attr, char *buf)
{
	int ret;
	unsigned char addr[1]={0};
	
	ret = mcu_i2c_read_regs(g_mcu_data->client, MCU_AGEING_TEST, addr, 1);
	if (ret < 0) 
		printk("%s: AGEING_TEST failed (%d)",__func__, ret);
	
	return sprintf(buf, "%d\n", addr[0]);
}	

static ssize_t store_ageing_test(struct class *cls, struct class_attribute *attr,
				const char *buf, size_t count)
{
	u8 reg[2];
	int ret;
	int enable;
	
	if (kstrtoint(buf, 0, &enable))
		return -EINVAL;
	reg[0] = enable;
	ret = mcu_i2c_write_regs(g_mcu_data->client, MCU_AGEING_TEST, reg, 1);
	if (ret < 0) {
		printk("ageing_test state err\n");
		return ret;
	}
	printk("ageing_test state: %d\n", enable);
	ageing_test_flag = 1;
	return count;
}

static struct class_attribute wol_class_attrs[] = {
	__ATTR(enable, 0644, show_wol_enable, store_wol_enable),
	__ATTR(test, 0644, NULL, store_test),
	__ATTR(rst_mcu, 0644, NULL, store_rst_mcu),
	__ATTR(mac_addr, 0644, show_mac_addr, store_mac_addr),
	__ATTR(ageing_test, 0644, show_ageing_test, store_ageing_test),	
};

static void create_mcu_attrs(void)
{
	int i;
	printk("%s\n",__func__);
	g_mcu_data->wol_class = class_create(THIS_MODULE, "wol");
	if (IS_ERR(g_mcu_data->wol_class)) {
		pr_err("create wol_class debug class fail\n");
		return;
	}
	for (i = 0; i < ARRAY_SIZE(wol_class_attrs); i++) {
		if (class_create_file(g_mcu_data->wol_class, &wol_class_attrs[i]))
			pr_err("create wol attribute %s fail\n", wol_class_attrs[i].attr.name);
	}
}

static int mcu_parse_dt(struct device *dev)
{
	int ret = 0;

	if (NULL == dev) return -EINVAL;

	return ret;
}

int mcu_get_wol_status(void)
{
	if (g_mcu_data)
		return g_mcu_data->wol_enable;

	return 0;
}

static int mcu_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	u8 reg[2];
	int ret;

	printk("%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	g_mcu_data = kzalloc(sizeof(struct mcu_data), GFP_KERNEL);

	if (g_mcu_data == NULL)
		return -ENOMEM;

	mcu_parse_dt(&client->dev);

	g_mcu_data->client = client;
	ret = mcu_i2c_read_regs(client, MCU_WOL_REG, reg, 1);
	if (ret < 0)
		goto exit;
	g_mcu_data->wol_enable = (int)reg[0];

	create_mcu_attrs();
	printk("%s,wol enable=%d\n",__func__ ,g_mcu_data->wol_enable);

	if (g_mcu_data->wol_enable == 3)
		mcu_enable_wol(g_mcu_data->wol_enable, false);

	reg[0] = 0x01;
	ret = mcu_i2c_write_regs(client, 0x87, reg, 1);
	if (ret < 0) {
		printk("write mcu err\n");
		goto  exit;
	}
	return 0;
exit:
	kfree(g_mcu_data);
	return ret;

}

static int mcu_remove(struct i2c_client *client)
{
	kfree(g_mcu_data);
	return 0;
}

static void mcu_shutdown(struct i2c_client *client)
{
	kfree(g_mcu_data);
}

#ifdef CONFIG_PM_SLEEP
static int mcu_suspend(struct device *dev)
{
	return 0;
}

static int mcu_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops mcu_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(mcu_suspend, mcu_resume)
};

#define MCU_PM_OPS (&(mcu_dev_pm_ops))

#endif

static const struct i2c_device_id mcu_id[] = {
	{ "khadas-mcu", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcu_id);


static struct of_device_id mcu_dt_ids[] = {
	{ .compatible = "khadas-mcu" },
	{},
};
MODULE_DEVICE_TABLE(i2c, mcu_dt_ids);

struct i2c_driver mcu_driver = {
	.driver  = {
		.name   = "khadas-mcu",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(mcu_dt_ids),
#ifdef CONFIG_PM_SLEEP
		.pm = MCU_PM_OPS,
#endif
	},
	.probe		= mcu_probe,
	.remove 	= mcu_remove,
	.shutdown = mcu_shutdown,
	.id_table	= mcu_id,
};
module_i2c_driver(mcu_driver);

MODULE_AUTHOR("Nick <nick@khadas.com>");
MODULE_DESCRIPTION("Khadas Edge MCU control driver");
MODULE_LICENSE("GPL");
