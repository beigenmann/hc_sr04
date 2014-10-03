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

#include "hc_sr04.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benno Eigenmann");
MODULE_DESCRIPTION("HC SR04 Driver for Raspberry Pi");

static DECLARE_WAIT_QUEUE_HEAD( queue);
static dev_t first; // Global variable for the first device number
static struct cdev c_dev; // Global variable for the character device structure
static struct class *cl; // Global variable for the device class
static char flag = 'n';
struct timespec end_time;
struct timespec start_time;
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
	printk(KERN_INFO "Driver: read()\n");
	flag = 'n';
	gpio_set_value(TRIGGER, HIGH);
	udelay(10);
	gpio_set_value(TRIGGER, LOW);
	start_time = current_kernel_time();
	wait_event_interruptible_timeout(queue, flag != 'n',100);
	//timeeleapsed = end_time.tv_nsec - start_time.tv_nsec;
	end_time = timespec_sub(end_time,start_time);
	printk(KERN_INFO "End Driver: read()\n");
	if(flag == 'y') {
		ret = snprintf(buf, len, "ALT %zd\n", end_time.tv_nsec);
	}
	else {
		ret = snprintf(buf, len, "Timed out\n");
	}
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
 * The interrupt service routine called on button presses
 */
static irqreturn_t echo_isr(int irq, void *data) {
	end_time = current_kernel_time();
	flag = 'y';
	wake_up_interruptible(&queue);
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
		status = request_irq(irq_num, echo_isr, IRQF_TRIGGER_RISING | IRQF_DISABLED, "ECHO", NULL);

		if(status) {
			printk(KERN_ERR "Unable to request IRQ %d: %d\n", irq_num, status);
			return -1;
		}
	}

	printk(KERN_INFO "hc_sr04:  registered");
	return 0; // Non-zero return means that the module couldn't be loaded.
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

	printk(KERN_INFO "hc_sr04: unregistered\n");
}

module_init( hc_sr04_init);
module_exit( hc_sr04_cleanup);
