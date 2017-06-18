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
	printk(KERN_DEBUG "process %i (%s) awakening the readers...\n",
			current->pid, current->comm);
	if (!strncmp(buf, "all", 3))
		complete_all(&comp);
	else
		complete(&comp);
	return count; /* succeed, to avoid retrial */
}


static struct file_operations complete_fops = {
	.owner = THIS_MODULE,
	.read =  complete_read,
	.write = complete_write,
};

static struct cdev *complete_cdev;
static dev_t complete_devno;

static int complete_init(void)
{
	int result;

	/*
	 * Register your major, and accept a dynamic number
	 */
	if (complete_major) {
		complete_devno = MKDEV(complete_major, 0);
		result  = register_chrdev_region(complete_devno, 1, "complete");
	} else {
		result = alloc_chrdev_region(&complete_devno, 0, 1, "complete");
		complete_major = MAJOR(complete_devno);
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
	result = cdev_add(complete_cdev, complete_devno, 1);
	if (result < 0)
		goto cdev_add_failed;

	return 0;

cdev_add_failed:
	cdev_del(complete_cdev);
cdev_alloc_failed:
	unregister_chrdev_region(complete_devno, 1);
register_chrdev_failed:
	return result;
}

static void complete_cleanup(void)
{
	cdev_del(complete_cdev);
	unregister_chrdev_region(complete_devno, 1);
}

module_init(complete_init);
module_exit(complete_cleanup);

