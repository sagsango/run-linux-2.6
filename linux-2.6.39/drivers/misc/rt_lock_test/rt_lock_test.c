/*
 * RT mutex PI + lock dependency deadlock experiment
 *
 * Linux 2.6.39
 *
 * 16 threads
 * 16 RT mutexes
 *
 * Phase 1:
 *
 *     T0  -> L0
 *     T1  -> L1
 *     ...
 *     T15 -> L15
 *
 * Phase 2:
 *
 *     T0  -> L1
 *     T1  -> L2
 *     ...
 *     T14 -> L15
 *     T15 -> L0
 *
 * This creates:
 *
 *     T0 -> L1 -> T1 -> L2 -> ... -> T15 -> L0 -> T0
 *
 * and therefore a circular wait.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/rtmutex.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/timex.h>

#define NR_THREADS      16
#define NR_LOCKS        16

#define RT_TEST_DIR     "rt_lock_test"
#define RT_TEST_FILE    "control"

struct rt_test_thread {
        int id;
        struct task_struct *task;
};

static struct rt_mutex locks[NR_LOCKS];
static struct lock_class_key lock_keys[NR_LOCKS];

static struct rt_test_thread threads[NR_THREADS];

/*
 * Number of threads that have successfully acquired
 * their first lock.
 */
static atomic_t first_lock_count = ATOMIC_INIT(0);

/*
 * Every thread waits here until all 16 threads have
 * acquired their first lock.
 */
static DECLARE_WAIT_QUEUE_HEAD(first_lock_wq);

/*
 * Start gate.
 *
 * Threads are created first, but wait here until the
 * user writes "1" to debugfs.
 */
static DECLARE_COMPLETION(start_completion);

static struct dentry *debugfs_dir;
static struct dentry *debugfs_control;

static atomic_t simulation_started = ATOMIC_INIT(0);


/*
 * ------------------------------------------------------------
 * RT priority
 * ------------------------------------------------------------
 *
 * T0  -> priority 10
 * T1  -> priority 20
 * ...
 * T15 -> priority 160
 *
 * Higher numerical RT priority means higher priority.
 */
static int rt_test_priority(int id)
{
        return 10 + id * 2;
}


/*
 * ------------------------------------------------------------
 * Set the current kernel thread to SCHED_FIFO
 * ------------------------------------------------------------
 */
static void rt_test_set_priority(int id)
{
        struct sched_param param;

        param.sched_priority = rt_test_priority(id);

        /*
         * sched_setscheduler() operates on current when
         * called from the kernel thread itself.
         */
        sched_setscheduler(current, SCHED_FIFO, &param);


        pr_info("rt_lock_test set: T%d priority=%d, "
		"pid[%d] prio[%d, %d, %d]\n",
                id, param.sched_priority,
		current->pid,
		current->prio,
		current->static_prio,
		current->normal_prio);

}


/*
 * ------------------------------------------------------------
 * Wait until all threads have acquired their first lock.
 * ------------------------------------------------------------
 */
static void rt_test_wait_for_all_first_locks(void)
{
        wait_event(first_lock_wq,
                   atomic_read(&first_lock_count) == NR_THREADS);
}


/*
 * ------------------------------------------------------------
 * Thread
 * ------------------------------------------------------------
 */
static int rt_test_thread(void *data)
{
        struct rt_test_thread *t;
        int id;
        int next_lock;

        t = data;
        id = t->id;

        /*
         * Set different RT priority.
         */
        rt_test_set_priority(id);

        /*
         * Wait until userspace writes:
         *
         *     echo 1 > control
         */
        wait_for_completion(&start_completion);

        if (kthread_should_stop())
                return 0;

        /*
         * ------------------------------------------------
         * PHASE 1
         *
         * Each thread acquires its own lock.
         * ------------------------------------------------
         */

        pr_info("rt_lock_test: T%d trying L%d\n",
                id, id);

        rt_mutex_lock(&locks[id]);

        pr_info("rt_lock_test: T%d acquired L%d\n",
                id, id);

        /*
         * Tell the other threads that our first lock
         * has been acquired.
         */
        atomic_inc(&first_lock_count);

        /*
         * Wake everyone that might now be able to
         * satisfy the barrier.
         */
        wake_up_all(&first_lock_wq);

        /*
         * ------------------------------------------------
         * PHASE 2
         *
         * Wait until ALL 16 threads own their first lock.
         * ------------------------------------------------
         */

        rt_test_wait_for_all_first_locks();

        /*
         * Now calculate:
         *
         * T0  -> L1
         * T1  -> L2
         * ...
         * T15 -> L0
         */
        next_lock = (id + 1) % NR_LOCKS;

        pr_info("rt_lock_test: T%d owns L%d, "
                "trying L%d\n",
                id, id, next_lock);

        /*
         * ------------------------------------------------
         *
         * INTENTIONAL DEADLOCK
         *
         * ------------------------------------------------
         */
        rt_mutex_lock(&locks[next_lock]);

        /*
         * Normally this should never be reached.
         */
        pr_info("rt_lock_test: T%d acquired L%d "
                "(unexpected)\n",
                id, next_lock);

        /*
         * We only get here if the deadlock does not happen.
         */
        rt_mutex_unlock(&locks[next_lock]);
        rt_mutex_unlock(&locks[id]);

        return 0;
}


/*
 * ------------------------------------------------------------
 * Start simulation
 * ------------------------------------------------------------
 */
static int rt_test_start(void)
{
        int i;
        int ret;

        if (atomic_xchg(&simulation_started, 1)) {
                pr_info("rt_lock_test: simulation already started\n");
                return -EBUSY;
        }

        /*
         * Reset state.
         */
        atomic_set(&first_lock_count, 0);

        /*
         * Create 16 kernel threads.
         */
        for (i = 0; i < NR_THREADS; i++) {

                threads[i].id = i;

                threads[i].task =
                    kthread_create(rt_test_thread,
                                   &threads[i],
                                   "rt_lock_t%d",
                                   i);

                if (IS_ERR(threads[i].task)) {

                        ret = PTR_ERR(threads[i].task);

                        pr_err("rt_lock_test: "
                               "failed to create T%d: %d\n",
                               i, ret);

                        threads[i].task = NULL;

                        /*
                         * Stop threads already created.
                         */
                        while (--i >= 0) {
                                if (threads[i].task)
                                        kthread_stop(threads[i].task);
                        }

                        atomic_set(&simulation_started, 0);

                        return ret;
                }
        }

        /*
         * Start all threads.
         *
         * They will immediately wait on start_completion.
         */
        for (i = 0; i < NR_THREADS; i++)
                wake_up_process(threads[i].task);

        /*
         * Give the threads a chance to reach the
         * completion wait.
         */
        msleep(100);

        /*
         * Release the starting gate.
         */
        complete_all(&start_completion);

        pr_info("rt_lock_test: simulation started\n");

        return 0;
}


/*
 * ------------------------------------------------------------
 * debugfs read
 * ------------------------------------------------------------
 */
static ssize_t rt_test_read(struct file *file,
                            char __user *buf,
                            size_t count,
                            loff_t *ppos)
{
        char msg[256];
        int len;

        len = snprintf(msg, sizeof(msg),
                       "write 1 to start the simulation\n");

        return simple_read_from_buffer(buf, count, ppos,
                                       msg, len);
}


/*
 * ------------------------------------------------------------
 * debugfs write
 * ------------------------------------------------------------
 */
static ssize_t rt_test_write(struct file *file,
                             const char __user *buf,
                             size_t count,
                             loff_t *ppos)
{
        char kbuf[16];

        if (count >= sizeof(kbuf))
                return -EINVAL;

        if (copy_from_user(kbuf, buf, count))
                return -EFAULT;

        kbuf[count] = '\0';

        if (kbuf[0] == '1')
                rt_test_start();
        else
                pr_info("rt_lock_test: write 1 to start\n");

        return count;
}


static const struct file_operations rt_test_fops = {
        .owner = THIS_MODULE,
        .read  = rt_test_read,
        .write = rt_test_write,
};


/*
 * ------------------------------------------------------------
 * Module initialization
 * ------------------------------------------------------------
 */
static int __init rt_lock_test_init(void)
{
        int i;

        pr_info("rt_lock_test: initializing\n");

        /*
         * Initialize 16 RT mutexes.
         *
         * IMPORTANT:
         *
         * Every lock gets a DIFFERENT lockdep class key.
         *
         * Therefore lockdep sees:
         *
         *     L0 != L1 != L2 != ... != L15
         */
        for (i = 0; i < NR_LOCKS; i++) {

                __rt_mutex_init(&locks[i],
                                "rt_lock_test");
                               // &lock_keys[i]);

                pr_info("rt_lock_test: initialized L%d\n", i);
        }

        /*
         * Create:
         *
         * /sys/kernel/debug/rt_lock_test/
         */
        debugfs_dir = debugfs_create_dir(RT_TEST_DIR, NULL);

        if (!debugfs_dir) {
                pr_err("rt_lock_test: "
                       "failed to create debugfs directory\n");

                return -ENOMEM;
        }

        /*
         * Create:
         *
         * /sys/kernel/debug/rt_lock_test/control
         */
        debugfs_control =
            debugfs_create_file(RT_TEST_FILE,
                                 0644,
                                 debugfs_dir,
                                 NULL,
                                 &rt_test_fops);

        if (!debugfs_control) {

                debugfs_remove(debugfs_dir);
                debugfs_dir = NULL;

                pr_err("rt_lock_test: "
                       "failed to create debugfs file\n");

                return -ENOMEM;
        }

        pr_info("rt_lock_test: ready\n");
        pr_info("rt_lock_test: echo 1 > "
                "/sys/kernel/debug/%s/%s\n",
                RT_TEST_DIR, RT_TEST_FILE);

        return 0;
}


/*
 * ------------------------------------------------------------
 * Module exit
 * ------------------------------------------------------------
 *
 * WARNING:
 *
 * Once the intentional circular deadlock is created,
 * the threads are blocked forever.
 *
 * Therefore rmmod is NOT expected to succeed after
 * the simulation reaches the deadlock.
 * ------------------------------------------------------------
 */
static void __exit rt_lock_test_exit(void)
{
        int i;

        pr_info("rt_lock_test: exiting\n");

        /*
         * Remove debugfs first.
         */
        if (debugfs_control)
                debugfs_remove(debugfs_control);

        if (debugfs_dir)
                debugfs_remove(debugfs_dir);

        /*
         * Stop threads.
         *
         * WARNING:
         *
         * If the simulation has already reached:
         *
         *     rt_mutex_lock(next_lock)
         *
         * these threads are blocked in the intentional
         * deadlock and kthread_stop() cannot make them
         * execute the exit path.
         */
        for (i = 0; i < NR_THREADS; i++) {
                if (threads[i].task) {
                        kthread_stop(threads[i].task);
                        threads[i].task = NULL;
                }
        }

        for (i = 0; i < NR_LOCKS; i++)
                rt_mutex_destroy(&locks[i]);

        pr_info("rt_lock_test: unloaded\n");
}


module_init(rt_lock_test_init);
module_exit(rt_lock_test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sagar Singh");
MODULE_DESCRIPTION("Linux 2.6.39 RT mutex PI/deadlock experiment");
