#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pm_wakeirq.h>
#include <linux/pm.h>
#include <linux/slab.h>

struct wake_irq_dev {
	struct device *dev;
	int gpio;
	int irq;
	bool wake_enabled;
};

static irqreturn_t wake_irq_handler(int irq, void *dev_id)
{
	struct wake_irq_dev *wdev = dev_id;
	
	dev_info(wdev->dev, "Received wake-up interrupt from RK96X!\n");
	
	return IRQ_HANDLED;
}

static int wake_irq_probe(struct platform_device *pdev)
{
	struct wake_irq_dev *wdev;
	struct device *dev = &pdev->dev;
	int ret;

	wdev = devm_kzalloc(dev, sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return -ENOMEM;

	wdev->dev = dev;
	platform_set_drvdata(pdev, wdev);

	wdev->gpio = of_get_named_gpio(dev->of_node, "rk96x-wake-irq-gpios", 0);
	if (gpio_is_valid(wdev->gpio)) {
		wdev->irq = gpio_to_irq(wdev->gpio);
		if (wdev->irq < 0) {
			dev_err(dev, "Failed to get IRQ from GPIO %d: %d\n", wdev->gpio, wdev->irq);
			return wdev->irq;
		}
	} else {
		dev_err(dev, "Invalid gpio num: %d\n", wdev->gpio);
		return -ENODEV;
	}

	ret = devm_request_irq(dev, wdev->irq, wake_irq_handler,
			      IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			      "rk96x_wake_irq", wdev);
	if (ret) {
		dev_err(dev, "Failed to request IRQ %d: %d\n", wdev->irq, ret);
		return ret;
	}

	device_init_wakeup(dev, true);
	ret = dev_pm_set_wake_irq(dev, wdev->irq);
	if (ret) {
		dev_err(dev, "Failed to set wake IRQ: %d\n", ret);
		device_init_wakeup(dev, false);
		return ret;
	}

	wdev->wake_enabled = true;
	dev_info(dev, "RK96X wake IRQ driver probed successfully\n");

	return 0;
}

static int wake_irq_remove(struct platform_device *pdev)
{
	struct wake_irq_dev *wdev = platform_get_drvdata(pdev);
	
	device_init_wakeup(&pdev->dev, false);
	dev_pm_clear_wake_irq(&pdev->dev);
	wdev->wake_enabled = false;
	
	dev_info(&pdev->dev, "RK96X Wake IRQ driver removed\n");
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int wake_irq_suspend(struct device *dev)
{
	struct wake_irq_dev *wdev = dev_get_drvdata(dev);
	
	if (device_may_wakeup(dev) && wdev->wake_enabled) {
		enable_irq_wake(wdev->irq);
		dev_dbg(dev, "Wake IRQ enabled for suspend\n");
	}
	
	return 0;
}

static int wake_irq_resume(struct device *dev)
{
	struct wake_irq_dev *wdev = dev_get_drvdata(dev);
	
	if (device_may_wakeup(dev) && wdev->wake_enabled) {
		disable_irq_wake(wdev->irq);
		dev_dbg(dev, "Wake IRQ disabled after resume\n");
	}
	
	return 0;
}
#endif

static const struct dev_pm_ops wake_irq_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(wake_irq_suspend, wake_irq_resume)
};

static const struct of_device_id wake_irq_of_match[] = {
	{ .compatible = "rockchip,rk96x-wake-aov-irq" },
	{ }
};
MODULE_DEVICE_TABLE(of, wake_irq_of_match);

static struct platform_driver wake_irq_driver = {
	.probe	= wake_irq_probe,
	.remove	= wake_irq_remove,
	.driver	= {
		.name		= "rk96x-wake-irq",
		.of_match_table	= wake_irq_of_match,
		.pm		= &wake_irq_pm_ops,
	},
};
module_platform_driver(wake_irq_driver);

MODULE_DESCRIPTION("Rk96x Wake-up Interrupt Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rk96x-wake-irq");
