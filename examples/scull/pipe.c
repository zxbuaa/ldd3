/*
 * pipe.c -- fifo driver for scull
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
 */
 
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/kernel.h>	/* printk(), min() */
#include <linux/sched.h>
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>

#include "scull.h"		/* local definitions */

struct scull_pipe {
        wait_queue_head_t inq, outq;       /* read and write queues */
        char *buffer, *end;                /* begin of buf, end of buf */
        int buffersize;                    /* used in pointer arithmetic */
        char *rp, *wp;                     /* where to read, where to write */
        int nreaders, nwriters;            /* number of openings for r/w */
        struct fasync_struct *async_queue; /* asynchronous readers */
        struct mutex lock;                 /* mutual exclusion lock */
        struct cdev cdev;                  /* Char device structure */
};

/* parameters */
static int scull_p_nr_devs = SCULL_P_NR_DEVS;	/* number of pipe devices */
int scull_p_buffer =  SCULL_P_BUFFER;	/* buffer size */
dev_t scull_p_devno;			/* Our first device number */

module_param(scull_p_nr_devs, int, 0);	/* FIXME check perms */
module_param(scull_p_buffer, int, 0);

static struct scull_pipe *scull_p_devices;

static int scull_p_fasync(int fd, struct file *filp, int mode);
static int spacefree(struct scull_pipe *dev);
/*
 * Open and close
 */


static int scull_p_open(struct inode *inode, struct file *filp)
{
	struct scull_pipe *dev;

	dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
	filp->private_data = dev;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	if (!dev->buffer) {
		/* allocate the buffer */
		dev->buffer = kmalloc(scull_p_buffer, GFP_KERNEL);
		if (!dev->buffer) {
			mutex_unlock(&dev->lock);
			return -ENOMEM;
		}
	}
	dev->buffersize = scull_p_buffer;
	dev->end = dev->buffer + dev->buffersize;
	if (!(dev->nreaders || dev->nwriters)) /* only reset rp and wp when 1st open */
		dev->rp = dev->wp = dev->buffer; /* rd and wr from the beginning */

	/* use f_mode,not  f_flags: it's cleaner (fs/open.c tells why) */
	if (filp->f_mode & FMODE_READ)
		dev->nreaders++;
	if (filp->f_mode & FMODE_WRITE)
		dev->nwriters++;
	mutex_unlock(&dev->lock);

	return nonseekable_open(inode, filp);
}



static int scull_p_release(struct inode *inode, struct file *filp)
{
	struct scull_pipe *dev = filp->private_data;

	/* remove this filp from the asynchronously notified filp's */
	scull_p_fasync(-1, filp, 0);
	mutex_lock(&dev->lock);
	if (filp->f_mode & FMODE_READ)
		dev->nreaders--;
	if (filp->f_mode & FMODE_WRITE)
		dev->nwriters--;
	if (!(dev->nreaders || dev->nwriters)) {
		kfree(dev->buffer);
		/* clear all fields or /proc/scullpipe might give wrong information */
		dev->end = dev->buffer = NULL;
		dev->buffersize = 0;
	}
	mutex_unlock(&dev->lock);
	return 0;
}


/*
 * Data management: read and write
 */

static ssize_t scull_p_read (struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct scull_pipe *dev = filp->private_data;
	size_t part1, part2, tmp, copied = 0;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

	while (dev->rp == dev->wp) { /* nothing to read */
		mutex_unlock(&dev->lock); /* release the lock */
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		PDEBUG("\"%s\" reading: going to sleep\n", current->comm);
		if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		/* otherwise loop, but first reacquire the lock */
		if (mutex_lock_interruptible(&dev->lock))
			return -ERESTARTSYS;
	}
	/* ok, data is there, return something */
	if (dev->wp > dev->rp) {
		part1 = (size_t)(dev->wp - dev->rp);
		part2 = 0;
	} else {
		part1 = (size_t)(dev->end - dev->rp);
		part2 = (size_t)(dev->wp - dev->buffer);
	}
	tmp = min(count, part1);
	PDEBUG("Part1: going to read %li bytes from %p to %p\n", (long)tmp, dev->rp, buf);
	if (copy_to_user(buf, dev->rp, tmp)) {
		mutex_unlock(&dev->lock);
		return -EFAULT;
	}
	dev->rp += tmp;
	if (dev->rp == dev->end)
		dev->rp = dev->buffer; /* wrapped */
	buf += tmp;
	count -= tmp;
	copied += tmp;
	if (count && part2) {
		tmp = min(count, part2);
		PDEBUG("Part2: going to read %li bytes from %p to %p\n", (long)tmp, dev->rp, buf);
		if (copy_to_user(buf, dev->rp, tmp))
			goto copy_part2_fail;
		dev->rp += tmp;
		buf += tmp;
		count -= tmp;
		copied += tmp;
	}

copy_part2_fail:
	mutex_unlock(&dev->lock);

	/* finally, awake any writers and return */
	wake_up_interruptible(&dev->outq);
	PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)copied);
	return copied;
}

/* Wait for space for writing; caller must hold device mutex.  On
 * error the mutex will be released before returning. */
static int scull_getwritespace(struct scull_pipe *dev, struct file *filp)
{
	while (spacefree(dev) == 0) { /* full */
		DEFINE_WAIT(wait);
		
		mutex_unlock(&dev->lock);
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		PDEBUG("\"%s\" writing: going to sleep\n",current->comm);
		prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
		if (spacefree(dev) == 0)
			schedule();
		finish_wait(&dev->outq, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		if (mutex_lock_interruptible(&dev->lock))
			return -ERESTARTSYS;
	}
	return 0;
}	

/* How much space is free? */
static int spacefree(struct scull_pipe *dev)
{
	if (dev->rp == dev->wp)
		return dev->buffersize - 1;
	return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

static ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct scull_pipe *dev = filp->private_data;
	size_t part1, part2, tmp, copied = 0;
	int result;

	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;

	/* Make sure there's space to write */
	result = scull_getwritespace(dev, filp);
	if (result)
		return result; /* scull_getwritespace called mutex_unlock(&dev->lock) */

	/* ok, space is there, accept something */
	count = min(count, (size_t)spacefree(dev));
	if (dev->wp >= dev->rp) {
		part1 = (size_t)(dev->end - dev->wp);
		part2 = (size_t)(dev->rp - dev->buffer);
	} else {
		part1 = (size_t)(dev->rp - dev->wp);
		part2 = 0;
	}
	tmp = min(count, part1);
	PDEBUG("Part1: going to write %li bytes to %p from %p\n", (long)tmp, dev->wp, buf);
	if (copy_from_user(dev->wp, buf, tmp)) {
		mutex_unlock(&dev->lock);
		return -EFAULT;
	}
	dev->wp += tmp;
	if (dev->wp == dev->end)
		dev->wp = dev->buffer; /* wrapped */
	buf += tmp;
	count -= tmp;
	copied += tmp;
	if (count && part2) {
		tmp = min(count, part2);
		PDEBUG("Part2: going to write %li bytes to %p from %p\n", (long)tmp, dev->wp, buf);
		if (copy_from_user(dev->wp, buf, tmp))
			goto copy_part2_fail;
		dev->wp += tmp;
		buf += tmp;
		count -= tmp;
		copied += tmp;
	}

copy_part2_fail:
	mutex_unlock(&dev->lock);

	/* finally, awake any reader */
	wake_up_interruptible(&dev->inq);  /* blocked in read() and select() */

	/* and signal asynchronous readers, explained late in chapter 5 */
	if (dev->async_queue)
		kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
	PDEBUG("\"%s\" did write %li bytes\n",current->comm, (long)copied);
	return copied;
}

static unsigned int scull_p_poll(struct file *filp, poll_table *wait)
{
	struct scull_pipe *dev = filp->private_data;
	unsigned int mask = 0;

	/*
	 * The buffer is circular; it is considered full
	 * if "wp" is right behind "rp" and empty if the
	 * two are equal.
	 */
	mutex_lock(&dev->lock);
	poll_wait(filp, &dev->inq,  wait);
	poll_wait(filp, &dev->outq, wait);
	if (dev->rp != dev->wp)
		mask |= POLLIN | POLLRDNORM;	/* readable */
	if (spacefree(dev))
		mask |= POLLOUT | POLLWRNORM;	/* writable */
	mutex_unlock(&dev->lock);
	return mask;
}





static int scull_p_fasync(int fd, struct file *filp, int mode)
{
	struct scull_pipe *dev = filp->private_data;

	return fasync_helper(fd, filp, mode, &dev->async_queue);
}



#ifdef SCULL_DEBUG
static void *scull_p_seq_start(struct seq_file *s, loff_t *pos)
{
	if (*pos >= scull_p_nr_devs)
		return NULL;
	return scull_p_devices + *pos;
}

static void *scull_p_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	(*pos)++;
	if (*pos >= scull_p_nr_devs)
		return NULL;
	return scull_p_devices + *pos;
}

static void scull_p_seq_stop(struct seq_file *s, void *v)
{
}

static int scull_p_seq_show(struct seq_file *s, void *v)
{
	struct scull_pipe *p = v;
	int i = p - scull_p_devices;

	if (!i)
		seq_printf(s, "Default buffersize is %i\n", scull_p_buffer);

	if (mutex_lock_interruptible(&p->lock))
		return -ERESTARTSYS;
	seq_printf(s, "\nDevice %i: %p\n", i, p);
	seq_printf(s, "   Buffer: %p to %p (%i bytes)\n", p->buffer, p->end, p->buffersize);
	seq_printf(s, "   rp %p   wp %p\n", p->rp, p->wp);
	seq_printf(s, "   readers %i   writers %i\n", p->nreaders, p->nwriters);
	mutex_unlock(&p->lock);
	return 0;
}

static struct seq_operations scull_p_seq_ops = {
	.start = scull_p_seq_start,
	.stop = scull_p_seq_stop,
	.next = scull_p_seq_next,
	.show = scull_p_seq_show
};

static int scull_p_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &scull_p_seq_ops);
}

static struct file_operations scull_p_proc_ops = {
	.owner = THIS_MODULE,
	.open = scull_p_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release
};

#endif



/*
 * The file operations for the pipe device
 * (some are overlayed with bare scull)
 */
struct file_operations scull_pipe_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		scull_p_read,
	.write =	scull_p_write,
	.poll =		scull_p_poll,
	.unlocked_ioctl =	scull_ioctl,
	.open =		scull_p_open,
	.release =	scull_p_release,
	.fasync =	scull_p_fasync,
};


/*
 * Set up a cdev entry.
 */
static void scull_p_setup_cdev(struct scull_pipe *dev, int index)
{
	int err, devno = scull_p_devno + index;
    
	cdev_init(&dev->cdev, &scull_pipe_fops);
	dev->cdev.owner = scull_pipe_fops.owner;
	err = cdev_add (&dev->cdev, devno, 1);
	/* Fail gracefully if need be */
	if (err)
		printk(KERN_NOTICE "Error %d adding scullpipe%d", err, index);
}

 

/*
 * Initialize the pipe devs; return how many we did.
 */
int scull_p_init(dev_t firstdev)
{
	int i, result;

	result = register_chrdev_region(firstdev, scull_p_nr_devs, "scullp");
	if (result < 0) {
		printk(KERN_NOTICE "Unable to get scullp region, error %d\n", result);
		return 0;
	}
	scull_p_devno = firstdev;
	scull_p_devices = kzalloc(scull_p_nr_devs * sizeof(struct scull_pipe), GFP_KERNEL);
	if (scull_p_devices == NULL) {
		unregister_chrdev_region(firstdev, scull_p_nr_devs);
		return 0;
	}
	for (i = 0; i < scull_p_nr_devs; i++) {
		init_waitqueue_head(&(scull_p_devices[i].inq));
		init_waitqueue_head(&(scull_p_devices[i].outq));
		mutex_init(&scull_p_devices[i].lock);
		scull_p_setup_cdev(scull_p_devices + i, i);
	}
#ifdef SCULL_DEBUG
	proc_create("scullpipe", 0, NULL, &scull_p_proc_ops);
#endif
	return scull_p_nr_devs;
}

/*
 * This is called by cleanup_module or on failure.
 * It is required to never fail, even if nothing was initialized first
 */
void scull_p_cleanup(void)
{
	int i;

#ifdef SCULL_DEBUG
	remove_proc_entry("scullpipe", NULL);
#endif

	if (!scull_p_devices)
		return; /* nothing else to release */

	for (i = 0; i < scull_p_nr_devs; i++) {
		cdev_del(&scull_p_devices[i].cdev);
		kfree(scull_p_devices[i].buffer);
	}
	kfree(scull_p_devices);
	unregister_chrdev_region(scull_p_devno, scull_p_nr_devs);
	scull_p_devices = NULL; /* pedantic */
}
