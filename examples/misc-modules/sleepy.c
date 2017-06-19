/*
 * sleepy.c -- the writers awake the readers
 *
 * Copyright (C) 2001 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2001 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: sleepy.c,v 1.7 2004/09/26 07:02:43 gregkh Exp $
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/sched.h>  /* current and everything */
#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>     /* everything... */
#include <linux/types.h>  /* size_t */
#include <linux/wait.h>

MODULE_LICENSE("Dual BSD/GPL");

static int sleepy_major = 0;

static DECLARE_WAIT_QUEUE_HEAD(wq);
static int flag = 0;

static ssize_t sleepy_read (struct file *filp, char __user *buf, size_t count, loff_t *pos)
{
	printk(KERN_DEBUG "process %i (%s) going to sleep\n",
			current->pid, current->comm);
	wait_event_interruptible(wq, flag != 0);
	flag = 0;
	printk(KERN_DEBUG "awoken %i (%s)\n", current->pid, current->comm);
	return 0; /* EOF */
}

static ssize_t sleepy_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	printk(KERN_DEBUG "process %i (%s) awakening the readers...\n",
			current->pid, current->comm);
	flag = 1;
	wake_up_interruptible(&wq);
	return count; /* succeed, to avoid retrial */
}


static struct file_operations sleepy_fops = {
	.owner = THIS_MODULE,
	.read =  sleepy_read,
	.write = sleepy_write,
};

static struct cdev *cdev; 

static int sleepy_init(void)
{
	dev_t devno;
	int result;

	/*
	 * Register your major, and accept a dynamic number
	 */
	if (sleepy_major) {
		devno = MKDEV(sleepy_major, 0);
		result = register_chrdev_region(devno, 1, "sleepy");
	} else {
		result = alloc_chrdev_region(&devno, sleepy_major, 1, "sleepy");
		sleepy_major = MAJOR(devno);
	}
	if (result < 0)
		goto register_chrdev_region_failed;

	cdev = cdev_alloc();
	if (!cdev) {
		result = -ENOMEM;
		goto cdev_alloc_failed;
	}
	cdev_init(cdev, &sleepy_fops);
	cdev->owner = sleepy_fops.owner;
	result = cdev_add(cdev, devno, 1);
	if (result < 0)
		goto cdev_add_failed;

	return 0;

cdev_add_failed:
	cdev_del(cdev);
cdev_alloc_failed:
	unregister_chrdev_region(devno, 1);
register_chrdev_region_failed:
	return result;
}

static void sleepy_cleanup(void)
{
	dev_t devno = MKDEV(sleepy_major, 0);
	cdev_del(cdev);
	unregister_chrdev_region(devno, 1);
}

module_init(sleepy_init);
module_exit(sleepy_cleanup);

