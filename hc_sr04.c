#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include "hc_sr04.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benno Eigenmann");
MODULE_DESCRIPTION("HC SR04 Driver for Raspberry Pi");

static DECLARE_WAIT_QUEUE_HEAD( queue);
static dev_t first; // Global variable for the first device number
static struct cdev c_dev; // Global variable for the character device structure
static struct class *cl; // Global variable for the device class
volatile unsigned long time_elapsed;
volatile unsigned long time_start;
#define BUS_PARAM_REQUIRED      1
#define HC_SR04_PARAM_COUNT         2
#define BUS_COUNT_MAX           2

static unsigned int Echo[HC_SR04_PARAM_COUNT] __initdata;
static unsigned int Trigger[HC_SR04_PARAM_COUNT] __initdata;
static unsigned int echocount __initdata;
static unsigned int triggercount __initdata;
static unsigned int _count;
static unsigned int _Echo[HC_SR04_PARAM_COUNT];
static unsigned int _Trigger[HC_SR04_PARAM_COUNT];

#define BUS_PARM_DESC \
        " config -> gpioa,gpiob"

module_param_array(Echo, uint, &echocount, 0);
MODULE_PARM_DESC(Echo, "Echo" BUS_PARM_DESC);
module_param_array(Trigger, uint, &triggercount, 0);
MODULE_PARM_DESC(Trigger, "Trigger" BUS_PARM_DESC);
static struct timer_list trigger_timer;
static int hc_sr04_open(struct inode *i, struct file *f) {
	printk(KERN_INFO "Driver: open()\n");
	return 0;
}
static int hc_sr04_close(struct inode *i, struct file *f) {
	printk(KERN_INFO "Driver: close()\n");
	return 0;
}
static ssize_t hc_sr04_read(struct file *f, char __user *buf, size_t
		len, loff_t *off) {
	ssize_t ret;
//	printk(KERN_INFO "Driver: read()\n");
//	flag = 'n';
//	gpio_set_value(TRIGGER, HIGH);
//	udelay(10);
//	gpio_set_value(TRIGGER, LOW);
//	wait_event_interruptible_timeout(queue, flag == 'y',20);
//	printk(KERN_INFO "End Driver: read()\n");
//	if(flag == 'y') {
//		ret = snprintf(buf, len, "ALT %zd\n", time_elapsed);
//	}
//	else {
//		ret = snprintf(buf, len, "Timed out\n");
//	}
	ret = snprintf(buf, len, "ALT %zd\n", time_elapsed);
	*off += ret;
	return ret;
}

static ssize_t hc_sr04_write(struct file *f, const char __user *buf,
		size_t len, loff_t *off) {
	printk(KERN_INFO "Driver: write()\n");
	return len;
}
static struct file_operations fops = { .owner = THIS_MODULE, .open =
		hc_sr04_open, .release = hc_sr04_close, .read = hc_sr04_read, .write =
		hc_sr04_write };

/*
 * Timer function called periodically
 */
static void trigger_timer_func(unsigned long data) {
	gpio_set_value(TRIGGER, HIGH);
	udelay(10);
	gpio_set_value(TRIGGER, LOW);
	//printk(KERN_INFO "%s\n", __func__);

	/* schedule next execution */
	trigger_timer.expires 8= jiffies + ((1 * HZ)/10); 		// 1 sec.
	add_timer(&trigger_timer);
}
1
/*
 * The interrupt service routine called on button presses
 */
static irqreturn_t echo_isr(int irq, void *data) {
	int value;
#ifdef DEBUGPIN
	gpio_set_value(DEBUGPIN, HIGH);
	udelay(10);
	gpio_set_value(DEBUGPIN, LOW);
#endif
	value = gpio_get_value(ECHO);
	if (value == 0) {
		time_elapsed = jiffies - time_start;
	} else {
		time_start = jiffies;
	}
	return IRQ_HANDLED;
}

static int __init hc_sr04_init(void) /* Constructor */
{
	int status,i;
	unsigned int irq_num;
	if(echocount == 0 && triggercount == 0) {
		echocount = 1;
		triggercount = 1;
		Echo[0] = ECHO;
		Trigger[0] = TRIGGER;
	}
	if(echocount != triggercount) {
		return -1;
	}
	_count = echocount;
	if (alloc_chrdev_region(&first, 0, 1, "hc_sr04") < 0) {
		return -1;
	}
	if ((cl = class_create(THIS_MODULE, "chardrv")) == NULL) {
		unregister_chrdev_region(first, 1);
		return -1;
	}
	if (device_create(cl, NULL, first, NULL, "hc_sr04") == NULL) {
		class_destroy(cl);
		unregister_chrdev_region(first, 1);
		return -1;
	}
	cdev_init(&c_dev, &fops);
	if (cdev_add(&c_dev, first, 1) == -1) {
		device_destroy(cl, first);
		class_destroy(cl);
		unregister_chrdev_region(first, 1);
		return -1;
	}
#ifdef DEBUGPIN 
	status = gpio_request_one(DEBUGPIN, GPIOF_OUT_INIT_LOW, "DEBUG");
#endif 
//	printk(KERN_INFO "Successfully requested ECHO IRQ # %d\n", ECHO);
	for(i = 0; i <_count;i++) {
		_Trigger[i] = Trigger[i];
		_Echo[i] = Echo[i];
		status = gpio_request_one(Trigger[i], GPIOF_OUT_INIT_LOW, "TRIGGER");

		if (status) {
			printk(KERN_ERR "Unable to request GPIOs TRIGGER %d: %d\n",_Trigger[i] , status);
			return status;
		}

		status = gpio_request_one(Echo[i] , GPIOF_IN, "ECHO");

		if (status) {
			printk(KERN_ERR "Unable to request GPIOs ECHO %d: %d\n",_Echo[i], status);
			return status;
		}
		irq_num = gpio_to_irq( Echo[i] );
		status = request_irq(irq_num, echo_isr, IRQF_TRIGGER_RISING |IRQF_TRIGGER_FALLING | IRQF_DISABLED, "ECHO", NULL);

		if(status) {
			printk(KERN_ERR "Unable to request IRQ %d: %d\n", irq_num, status);
			return -1;
		}
	}

	printk(KERN_INFO "hc_sr04:  registered");
	/* init timer, add timer function */
	init_timer(&trigger_timer);

	trigger_timer.function = trigger_timer_func;
	trigger_timer.data = 1L;							// initially turn LED on
	trigger_timer.expires = jiffies + ((1 * HZ)/10);// 1 sec.
	add_timer(&trigger_timer);
	return 0;// Non-zero return means that the module couldn't be loaded.
}

static void __exit hc_sr04_cleanup(void)
{
	unsigned int irq_num,i;
	cdev_del(&c_dev);
	device_destroy(cl, first);
	class_destroy(cl);
	unregister_chrdev_region(first, 1);
	for(i = 0; i <_count;i++) {
		irq_num = gpio_to_irq(_Echo[i]);
		free_irq(irq_num, NULL);
		gpio_free(_Trigger[i]);
		gpio_free(_Echo[i]);
	}
	del_timer_sync(&trigger_timer);
	printk(KERN_INFO "hc_sr04: unregistered\n");
}

module_init( hc_sr04_init);
module_exit( hc_sr04_cleanup);
