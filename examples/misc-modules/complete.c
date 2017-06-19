/*
 * complete.c -- the writers awake the readers
 *
 * Copyright (C) 2003 Alessandro Rubini and Jonathan Corbet
 * Copyright (C) 2003 O'Reilly & Associates
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 * $Id: complete.c,v 1.2 2004/09/26 07:02:43 gregkh Exp $
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/sched.h>  /* current and everything */
#include <linux/kernel.h> /* printk() */
#include <linux/fs.h>     /* everything... */
#include <linux/types.h>  /* size_t */
#include <linux/completion.h>

MODULE_LICENSE("Dual BSD/GPL");

static int complete_major = 0;

static DECLARE_COMPLETION(comp);

static ssize_t complete_read (struct file *filp, char __user *buf, size_t count, loff_t *pos)
{
	printk(KERN_DEBUG "process %i (%s) going to sleep\n",
			current->pid, current->comm);
	wait_for_completion(&comp);
	printk(KERN_DEBUG "awoken %i (%s)\n", current->pid, current->comm);
	return 0; /* EOF */
}

static ssize_t complete_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	if (!strncmp(buf, "reset", 5)) {
		printk(KERN_DEBUG "process %i (%s) reseting the completion...\n",
			current->pid, current->comm);
		init_completion(&comp);
	} else if (!strncmp(buf, "all", 3)) {
		printk(KERN_DEBUG "process %i (%s) awakening all the readers...\n",
			current->pid, current->comm);
		complete_all(&comp);
	} else {
		printk(KERN_DEBUG "process %i (%s) awakening one single reader...\n",
			current->pid, current->comm);
		complete(&comp);
	}
	return count; /* succeed, to avoid retrial */
}


static struct file_operations complete_fops = {
	.owner = THIS_MODULE,
	.read =  complete_read,
	.write = complete_write,
};

static struct cdev *complete_cdev;

static int complete_init(void)
{
	static dev_t devno;
	int result;

	/*
	 * Register your major, and accept a dynamic number
	 */
	if (complete_major) {
		devno = MKDEV(complete_major, 0);
		result  = register_chrdev_region(devno, 1, "complete");
	} else {
		result = alloc_chrdev_region(&devno, 0, 1, "complete");
		complete_major = MAJOR(devno);
	}
	if (result < 0)
		goto register_chrdev_failed;

	complete_cdev = cdev_alloc();
	if (!complete_cdev) {
		result = -ENOMEM;
		goto cdev_alloc_failed;
	}
	cdev_init(complete_cdev, &complete_fops);
	complete_cdev->owner = THIS_MODULE;
	result = cdev_add(complete_cdev, devno, 1);
	if (result < 0)
		goto cdev_add_failed;

	return 0;

cdev_add_failed:
	cdev_del(complete_cdev);
cdev_alloc_failed:
	unregister_chrdev_region(devno, 1);
register_chrdev_failed:
	return result;
}

static void complete_cleanup(void)
{
	dev_t devno = MKDEV(complete_major, 0);
	cdev_del(complete_cdev);
	unregister_chrdev_region(devno, 1);
}

module_init(complete_init);
module_exit(complete_cleanup);

