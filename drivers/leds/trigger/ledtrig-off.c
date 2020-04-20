/*
 * LED Kernel OFF Trigger
 *
 * Copyright 2019 Terry <terry@szwesion.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>

static int off_trig_activate(struct led_classdev *led_cdev)
{
	led_set_brightness_sync(led_cdev, LED_OFF);
	return 0;
}

static struct led_trigger off_led_trigger = {
	.name     = "off",
	.activate = off_trig_activate,
};

static int __init off_trig_init(void)
{
	return led_trigger_register(&off_led_trigger);
}

static void __exit off_trig_exit(void)
{
	led_trigger_unregister(&off_led_trigger);
}

module_init(off_trig_init);
module_exit(off_trig_exit);

MODULE_AUTHOR("Terry <terry@szwesion.com>");
MODULE_DESCRIPTION("OFF LED trigger");
MODULE_LICENSE("GPL");
