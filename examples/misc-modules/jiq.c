/*
 * jiq.c -- the just-in-queue module
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
 * $Id: jiq.c,v 1.7 2004/09/26 07:02:43 gregkh Exp $
 */
 
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fs.h>     /* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>  /* error codes */
#include <linux/workqueue.h>
#include <linux/preempt.h>
#include <linux/interrupt.h> /* tasklets */
#include <linux/wait.h>

MODULE_LICENSE("Dual BSD/GPL");

/*
 * The delay for the delayed workqueue timer file.
 */
static long delay = HZ;
module_param(delay, long, 0);

/*
 * This module is a silly one: it only embeds short code fragments
 * that show how enqueued tasks `feel' the environment
 */

static long max_count = 5;
module_param(max_count, long, 0);

/*
 * Print information about the current environment. This is called from
 * within the task queues. If the limit is reched, awake the reading
 * process.
 */
static DECLARE_WAIT_QUEUE_HEAD(jiq_wait);

/*
 * Keep track of info we need between task queue runs.
 */
static struct clientdata {
	struct delayed_work dwork;
	struct seq_file *seq_file;
	unsigned long jiffies;
	long delay;
	int count;
	bool stopped;
} jiq_data;

static void jiq_print_tasklet(unsigned long);
static DECLARE_TASKLET(jiq_tasklet, jiq_print_tasklet, (unsigned long)&jiq_data);

/*
 * Do the printing; return non-zero if the task should be rescheduled.
 */
static int jiq_print(struct clientdata *data)
{
	unsigned long j = jiffies;

	if (!data->count || data->stopped) {
		wake_up_interruptible(&jiq_wait);
		return 0;
	}
	if (data->count == max_count)
		seq_printf(data->seq_file, "    time  delta preempt   pid cpu command\n");

  	/* intr_count is only exported since 1.3.5, but 1.99.4 is needed anyways */
	seq_printf(data->seq_file, "%9li  %4li     %3i %5i %3i %s\n",
			j, j - data->jiffies,
			preempt_count(), current->pid, smp_processor_id(),
			current->comm);

	data->jiffies = j;
	data->count--;
	return 1;
}

/*
 * Call jiq_print from a work queue
 */
static void jiq_print_wq(struct work_struct *work)
{
	struct clientdata *data = container_of(work, struct clientdata, dwork.work);
    
	if (!jiq_print(data))
		return;
    
	if (data->delay)
		schedule_delayed_work(&data->dwork, data->delay);
	else
		schedule_work(&data->dwork.work);
}

static int jiqwq_seq_show(struct seq_file *m, void *v)
{
	struct clientdata *data = m->private;
	int retval;
	
	data->seq_file = m;
	data->jiffies = jiffies;      /* initial time */
	data->delay = 0;
	data->count = max_count;
	data->stopped = false;

	schedule_work(&data->dwork.work);
	retval = wait_event_interruptible(jiq_wait, !data->count);
	if (retval < 0) {
		data->stopped = true;
		cancel_work_sync(&data->dwork.work);
	}

	return retval;
}

static int jiqwqdelay_seq_show(struct seq_file *m, void *v)
{
	struct clientdata *data = m->private;
	int retval;
	
	data->seq_file = m;
	data->jiffies = jiffies;      /* initial time */
	data->delay = delay;
	data->count = max_count;
	data->stopped = false;
    
	schedule_delayed_work(&data->dwork, delay);
	retval = wait_event_interruptible(jiq_wait, !data->count);
	if (retval < 0) {
		data->stopped = true;
		cancel_delayed_work_sync(&data->dwork);
	}

	return retval;
}

/*
 * Call jiq_print from a tasklet
 */
static void jiq_print_tasklet(unsigned long ptr)
{
	if (!jiq_print((struct clientdata *)ptr))
		return;
	tasklet_schedule(&jiq_tasklet);
}

static int jiqtasklet_seq_show(struct seq_file *m, void *v)
{
	struct clientdata *data = m->private;
	int retval;

	data->seq_file = m;
	data->jiffies = jiffies;      /* initial time */
	data->count = max_count;
	data->stopped = false;

	tasklet_schedule(&jiq_tasklet);
	retval = wait_event_interruptible(jiq_wait, !data->count);
	if (retval < 0) {
		data->stopped = true;
		tasklet_kill(&jiq_tasklet);
	}

	return retval;
}

/*
 * This one, instead, tests out the timers.
 */

static struct timer_list jiq_timer;

static void jiq_timedout(unsigned long ptr)
{
	struct clientdata *data = (struct clientdata *)ptr;
	if (!jiq_print(data))
		return;
	jiq_timer.expires += data->delay;
	add_timer(&jiq_timer);
}

static int jiqtimer_seq_show(struct seq_file *m, void *v)
{
	struct clientdata *data = m->private;
	int retval;

	data->seq_file = m;
	data->jiffies = jiffies;
	data->delay = delay;
	data->count = max_count;
	data->stopped = false;

	init_timer(&jiq_timer);              /* init the timer structure */
	jiq_timer.function = jiq_timedout;
	jiq_timer.data = (unsigned long)data;
	jiq_timer.expires = jiffies + delay; /* one second */

	jiq_print(data);   /* print and go to sleep */
	add_timer(&jiq_timer);
	retval = wait_event_interruptible(jiq_wait, !data->count);
	if (retval < 0) {
		data->stopped = true;
		del_timer_sync(&jiq_timer);  /* in case a signal woke us up */
	}

	return retval;
}

static int jiq_single_open(struct inode *inode, struct file *file)
{
	unsigned char *name = file->f_path.dentry->d_iname;
	int retval = 0;

	if (!strcmp(name, "jiqwq"))
		retval = single_open(file, jiqwq_seq_show, (void *)&jiq_data);
	else if (!strcmp(name, "jiqwqdelay"))
		retval = single_open(file, jiqwqdelay_seq_show, (void *)&jiq_data);
	else if (!strcmp(name, "jiqtimer"))
		retval = single_open(file, jiqtimer_seq_show, (void *)&jiq_data);
	else if (!strcmp(name, "jiqtasklet"))
		retval = single_open(file, jiqtasklet_seq_show, (void *)&jiq_data);
	return retval;
}

static struct file_operations jiq_read_fops = {
	.owner = THIS_MODULE,
	.open = jiq_single_open,
	.release = single_release,
	.read = seq_read,
	.llseek = seq_lseek,
};

/*
 * the init/clean material
 */

static int jiq_init(void)
{

	/* this line is in jiq_init() */
	INIT_DELAYED_WORK(&jiq_data.dwork, jiq_print_wq);

	proc_create("jiqwq", 0, NULL, &jiq_read_fops);
	proc_create("jiqwqdelay", 0, NULL, &jiq_read_fops);
	proc_create("jiqtimer", 0, NULL, &jiq_read_fops);
	proc_create("jiqtasklet", 0, NULL, &jiq_read_fops);

	return 0; /* succeed */
}

static void jiq_cleanup(void)
{
	remove_proc_entry("jiqwq", NULL);
	remove_proc_entry("jiqwqdelay", NULL);
	remove_proc_entry("jiqtimer", NULL);
	remove_proc_entry("jiqtasklet", NULL);
}

module_init(jiq_init);
module_exit(jiq_cleanup);
