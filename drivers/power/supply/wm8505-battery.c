// SPDX-License-Identifier: GPL-2.0-only
/*
 * WonderMedia WM8505 Battery Driver
 *
 * Copyright (C) 2026 Logan Russell <me@lrussell.net>
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/devm-helpers.h>
#include <linux/gpio/consumer.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/workqueue.h>

#define WM8505_BATTERY_DISCHARGE_MS	250
#define WM8505_BATTERY_RISE_TIMEOUT_US	1000000
#define WM8505_BATTERY_SAMPLES		3
#define WM8505_BATTERY_AVG_WINDOW	4
#define WM8505_BATTERY_POLL		(5 * HZ)
#define WM8505_BATTERY_SAMPLE_INTERVAL	(60 * HZ)
#define WM8505_BATTERY_TEMP_C		25	/* No temp sensor; the OCV table's reference */

struct wm8505_battery {
	struct power_supply *psy;
	struct power_supply_battery_info *info;
	struct delayed_work work;
	struct mutex lock;	/* Protects the state fields */
	struct gpio_desc *sense;
	struct gpio_desc *done;
	struct gpio_desc *alarm;
	struct gpio_desc *alarm_en;
	u32 tau_us;
	u32 threshold_uv;
	unsigned long next_sample;
	int uv_win[WM8505_BATTERY_AVG_WINDOW];
	int uv_count;
	int uv_index;
	int status;
	int capacity;		/* Reported, -1 = unknown */
	int sample_cap;		/* Last measured, -1 = none */
	int voltage_uv;		/* Last measured, -1 = none */
	int ocv_uv;		/* Windowed average, -1 = none */
	bool present;
	bool alarm_on;
};

/*
 * wm8505_battery_rise_us - Time the sense-pin rise past its input threshold
 *
 * The board has no ADC. Battery voltage is measured by holding an RC network
 * on the battery rail discharged through the sense GPIO, releasing it, and
 * timing how long the pin takes to charge past its input threshold:
 * V = Vth / (1 - e^(-t/tau)). A pin that never rises means no battery is fitted.
 */
static int wm8505_battery_rise_us(struct wm8505_battery *bat)
{
	ktime_t start, timeout;
	int ret;

	ret = gpiod_direction_output(bat->sense, 0);
	if (ret)
		return ret;
	msleep(WM8505_BATTERY_DISCHARGE_MS);

	start = ktime_get();
	ret = gpiod_direction_input(bat->sense);
	if (ret)
		return ret;
	timeout = ktime_add_us(start, WM8505_BATTERY_RISE_TIMEOUT_US);

	while (!gpiod_get_value_cansleep(bat->sense)) {
		if (ktime_after(ktime_get(), timeout))
			return -ETIMEDOUT;
		usleep_range(200, 500);
	}

	return ktime_us_delta(ktime_get(), start);
}

/*
 * wm8505_battery_rise_to_uv - Convert a rise time to open-circuit microvolts
 */
static int wm8505_battery_rise_to_uv(struct wm8505_battery *bat, u32 t_us)
{
	u64 x, x2, x3, x4, e, denom, uv;

	/* Q16 Taylor series for e^(-x); exact over the operating range */
	x = min_t(u64, div64_u64((u64)t_us << 16, bat->tau_us), BIT(16));
	x2 = (x * x) >> 16;
	x3 = (x2 * x) >> 16;
	x4 = (x3 * x) >> 16;
	e = BIT(16) - x + x2 / 2 - x3 / 6 + x4 / 24;

	denom = max_t(u64, BIT(16) - e, 1);
	uv = div64_u64((u64)bat->threshold_uv << 16, denom);

	return min_t(u64, uv, INT_MAX);
}

/*
 * wm8505_battery_measure - Take a median-filtered voltage sample
 */
static int wm8505_battery_measure(struct wm8505_battery *bat, int *uv)
{
	int t[WM8505_BATTERY_SAMPLES];
	int i, j, n = 0, ret;

	for (i = 0; i < WM8505_BATTERY_SAMPLES; i++) {
		ret = wm8505_battery_rise_us(bat);
		if (ret == -ETIMEDOUT)
			continue;
		if (ret < 0)
			return ret;
		for (j = n; j > 0 && t[j - 1] > ret; j--)
			t[j] = t[j - 1];
		t[j] = ret;
		n++;
	}
	if (!n)
		return -ENODEV;

	*uv = wm8505_battery_rise_to_uv(bat, t[n / 2]);

	return 0;
}

/*
 * wm8505_battery_work - Poll charge status and sample the battery
 */
static void wm8505_battery_work(struct work_struct *work)
{
	struct wm8505_battery *bat = container_of(work, struct wm8505_battery,
						  work.work);
	int status, capacity, uv = 0, sample = -1, mret = 0;
	bool alarm, changed, measured = false;

	if (!READ_ONCE(bat->psy))
		return;

	if (power_supply_am_i_supplied(bat->psy) > 0)
		status = gpiod_get_value_cansleep(bat->done) ?
			 POWER_SUPPLY_STATUS_FULL : POWER_SUPPLY_STATUS_CHARGING;
	else
		status = POWER_SUPPLY_STATUS_DISCHARGING;

	if (status == POWER_SUPPLY_STATUS_DISCHARGING &&
	    (bat->status != POWER_SUPPLY_STATUS_DISCHARGING ||
	     time_after(jiffies, bat->next_sample))) {
		bat->next_sample = jiffies + WM8505_BATTERY_SAMPLE_INTERVAL;
		if (bat->status != POWER_SUPPLY_STATUS_DISCHARGING) {
			/* New discharge, drop stale samples */
			bat->uv_count = 0;
			bat->uv_index = 0;
		}
		mret = wm8505_battery_measure(bat, &uv);
		measured = true;
	}

	alarm = bat->alarm && gpiod_get_value_cansleep(bat->alarm);

	mutex_lock(&bat->lock);

	if (status != POWER_SUPPLY_STATUS_DISCHARGING) {
		/* Charger holds the rail; battery voltage is unmeasurable */
		bat->voltage_uv = -1;
		bat->ocv_uv = -1;
	}

	if (measured) {
		bat->present = mret != -ENODEV;
		if (!mret) {
			int i, n;
			s64 avg = 0;

			bat->uv_win[bat->uv_index] = uv;
			bat->uv_index = (bat->uv_index + 1) % WM8505_BATTERY_AVG_WINDOW;
			if (bat->uv_count < WM8505_BATTERY_AVG_WINDOW)
				bat->uv_count++;
			n = bat->uv_count;
			for (i = 0; i < n; i++)
				avg += bat->uv_win[i];
			avg = div_s64(avg, n);

			sample = power_supply_batinfo_ocv2cap(bat->info, avg,
							      WM8505_BATTERY_TEMP_C);
			bat->voltage_uv = uv;
			bat->ocv_uv = avg;
		} else {
			bat->uv_count = 0;
			bat->uv_index = 0;
			bat->voltage_uv = -1;
			bat->ocv_uv = -1;
		}
		bat->sample_cap = sample;
	}

	capacity = (status == POWER_SUPPLY_STATUS_FULL) ? 100 : bat->sample_cap;

	changed = status != bat->status || capacity != bat->capacity ||
		  alarm != bat->alarm_on;
	bat->status = status;
	bat->capacity = capacity;
	bat->alarm_on = alarm;

	mutex_unlock(&bat->lock);

	if (changed)
		power_supply_changed(bat->psy);

	queue_delayed_work(system_wq, &bat->work, WM8505_BATTERY_POLL);
}

static enum power_supply_property wm8505_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

/*
 * wm8505_battery_get_property - Report a power-supply property
 */
static int wm8505_battery_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct wm8505_battery *bat = power_supply_get_drvdata(psy);
	int ret = 0;

	mutex_lock(&bat->lock);

	if (!bat->psy) {
		mutex_unlock(&bat->lock);
		return -EAGAIN;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = bat->status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = bat->present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = bat->info->technology;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (bat->voltage_uv < 0)
			ret = -ENODATA;
		else
			val->intval = bat->voltage_uv;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		if (bat->ocv_uv < 0)
			ret = -ENODATA;
		else
			val->intval = bat->ocv_uv;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (bat->capacity < 0)
			ret = -ENODATA;
		else
			val->intval = bat->capacity;
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		if (bat->alarm_on)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else if (bat->status == POWER_SUPPLY_STATUS_FULL)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (bat->capacity < 0)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
		else
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&bat->lock);

	return ret;
}

/*
 * wm8505_battery_external_power_changed - Re-poll when the supply state changes
 */
static void wm8505_battery_external_power_changed(struct power_supply *psy)
{
	struct wm8505_battery *bat = power_supply_get_drvdata(psy);

	mod_delayed_work(system_wq, &bat->work, 0);
}

static const struct power_supply_desc wm8505_battery_desc = {
	.name			= "wm8505-battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.get_property		= wm8505_battery_get_property,
	.external_power_changed	= wm8505_battery_external_power_changed,
	.properties		= wm8505_battery_props,
	.num_properties		= ARRAY_SIZE(wm8505_battery_props),
};

/*
 * wm8505_battery_probe - Set up the battery power supply
 */
static int wm8505_battery_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	struct device *dev = &pdev->dev;
	struct wm8505_battery *bat;
	struct power_supply *psy;
	int table_len, ret;

	bat = devm_kzalloc(dev, sizeof(*bat), GFP_KERNEL);
	if (!bat)
		return -ENOMEM;

	bat->status = POWER_SUPPLY_STATUS_UNKNOWN;
	bat->capacity = -1;
	bat->sample_cap = -1;
	bat->voltage_uv = -1;
	bat->ocv_uv = -1;
	bat->present = true;
	mutex_init(&bat->lock);
	INIT_DELAYED_WORK(&bat->work, wm8505_battery_work);

	bat->sense = devm_gpiod_get(dev, "sense", GPIOD_IN);
	if (IS_ERR(bat->sense))
		return dev_err_probe(dev, PTR_ERR(bat->sense),
				     "Failed to request sense GPIO\n");

	bat->done = devm_gpiod_get(dev, "charge-done", GPIOD_IN);
	if (IS_ERR(bat->done))
		return dev_err_probe(dev, PTR_ERR(bat->done),
				     "Failed to request charge-done GPIO\n");

	bat->alarm = devm_gpiod_get_optional(dev, "low-battery", GPIOD_IN);
	if (IS_ERR(bat->alarm))
		return dev_err_probe(dev, PTR_ERR(bat->alarm),
				     "Failed to request low-battery GPIO\n");

	/*
	 * The comparator on the low-battery GPIO only reads correctly while
	 * this pin is held as an input; claim it purely to enable that path.
	 */
	bat->alarm_en = devm_gpiod_get_optional(dev, "alarm-enable", GPIOD_IN);
	if (IS_ERR(bat->alarm_en))
		return dev_err_probe(dev, PTR_ERR(bat->alarm_en),
				     "Failed to request alarm-enable GPIO\n");

	ret = device_property_read_u32(dev, "wm,time-constant-us", &bat->tau_us);
	if (ret || !bat->tau_us)
		return dev_err_probe(dev, ret ?: -EINVAL,
				     "Missing wm,time-constant-us property\n");

	ret = device_property_read_u32(dev, "wm,threshold-microvolt",
				       &bat->threshold_uv);
	if (ret || !bat->threshold_uv)
		return dev_err_probe(dev, ret ?: -EINVAL,
				     "Missing wm,threshold-microvolt property\n");

	psy_cfg.drv_data = bat;
	psy_cfg.fwnode = dev_fwnode(dev);
	psy = devm_power_supply_register(dev, &wm8505_battery_desc, &psy_cfg);
	if (IS_ERR(psy))
		return dev_err_probe(dev, PTR_ERR(psy),
				     "Failed to register power supply\n");

	ret = power_supply_get_battery_info(psy, &bat->info);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get battery info\n");

	if (!power_supply_find_ocv2cap_table(bat->info, WM8505_BATTERY_TEMP_C,
					     &table_len))
		return dev_err_probe(dev, -ENODEV, "Failed to find OCV capacity table\n");

	mutex_lock(&bat->lock);
	bat->psy = psy;
	mutex_unlock(&bat->lock);

	ret = devm_add_action_or_reset(dev, devm_delayed_work_drop, &bat->work);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, bat);
	queue_delayed_work(system_wq, &bat->work, 0);

	return 0;
}

/*
 * wm8505_battery_suspend - Stop the poll worker across suspend
 */
static int wm8505_battery_suspend(struct device *dev)
{
	struct wm8505_battery *bat = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&bat->work);

	return 0;
}

/*
 * wm8505_battery_resume - Force a fresh sample on resume
 */
static int wm8505_battery_resume(struct device *dev)
{
	struct wm8505_battery *bat = dev_get_drvdata(dev);

	bat->next_sample = jiffies - 1;
	mod_delayed_work(system_wq, &bat->work, 0);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(wm8505_battery_pm_ops, wm8505_battery_suspend,
				wm8505_battery_resume);

static const struct of_device_id wm8505_battery_of_match[] = {
	{ .compatible = "wm,wm8505-battery", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wm8505_battery_of_match);

static struct platform_driver wm8505_battery_driver = {
	.probe = wm8505_battery_probe,
	.driver	= {
		.name = "wm8505-battery",
		.of_match_table = wm8505_battery_of_match,
		.pm = pm_sleep_ptr(&wm8505_battery_pm_ops),
	},
};
module_platform_driver(wm8505_battery_driver);

MODULE_DESCRIPTION("WonderMedia WM8505 Battery Driver");
MODULE_AUTHOR("Logan Russell <me@lrussell.net>");
MODULE_LICENSE("GPL");
