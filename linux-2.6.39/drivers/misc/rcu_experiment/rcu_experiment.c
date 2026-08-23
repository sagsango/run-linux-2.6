#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("AI Collaborator");
MODULE_DESCRIPTION("Linux 2.6.39 RCU Exploration Kernel Module");
MODULE_VERSION("1.0");

#define NUM_READERS 3

/* Shared data structure protected by RCU */
struct my_shared_data {
    int value;
    struct rcu_head rcu;
};

/* Global pointer to the RCU-protected structure */
static struct my_shared_data __rcu *global_data;

/* Task references */
static struct task_struct *reader_threads[NUM_READERS];
static struct task_struct *updater_thread;
static struct task_struct *call_rcu_updater_thread;

static int stop_threads = 0;

/* Reader Kthread Function */
static int reader_func(void *data)
{
    long id = (long)data;

    while (!kthread_should_stop() && !stop_threads) {
        struct my_shared_data *local_ptr;

        /* Enter RCU Read-Side Critical Section */
        rcu_read_lock();

        /* Safely fetch the shared pointer */
        local_ptr = rcu_dereference(global_data);
        if (local_ptr) {
            pr_info("[RCU Reader %ld] Read shared value: %d\n", id, local_ptr->value);
        } else {
            pr_info("[RCU Reader %ld] Shared data is NULL\n", id);
        }

        /* Exit RCU Read-Side Critical Section */
        rcu_read_unlock();

        /* Sleep a bit to avoid overwhelming the log */
        msleep(500);
    }
    return 0;
}

/* Callback function invoked by call_rcu() after a grace period */
static void my_rcu_reclaim_callback(struct rcu_head *rh)
{
    struct my_shared_data *old_data = container_of(rh, struct my_shared_data, rcu);
    pr_info("[call_rcu Callback] Grace period elapsed. Reclaiming memory for value: %d\n", old_data->value);
    kfree(old_data);
}

/* Updater Kthread using traditional synchronize_rcu() */
static int updater_sync_func(void *data)
{
    int counter = 100;

    while (!kthread_should_stop() && !stop_threads) {
        struct my_shared_data *new_data;
        struct my_shared_data *old_data;

        new_data = kmalloc(sizeof(*new_data), GFP_KERNEL);
        if (!new_data) {
            pr_err("[RCU Sync Updater] Memory allocation failed\n");
            msleep(1000);
            continue;
        }

        new_data->value = counter++;
        pr_info("[RCU Sync Updater] Preparing new structure with value: %d\n", new_data->value);

        /* Step 1: Fetch old pointer */
        old_data = rcu_dereference_protected(global_data, 1);

        /* Step 2: Assign new pointer to global visibility */
        rcu_assign_pointer(global_data, new_data);

        /* Step 3: Wait for all pre-existing readers to finish */
        pr_info("[RCU Sync Updater] Blocking on synchronize_rcu()...\n");
        synchronize_rcu();
        pr_info("[RCU Sync Updater] synchronize_rcu() done! Grace period complete.\n");

        /* Step 4: Reclaim the memory safe from concurrent read access */
        if (old_data) {
            pr_info("[RCU Sync Updater] Freeing old structure with value: %d\n", old_data->value);
            kfree(old_data);
        }

        msleep(3000);
    }
    return 0;
}

/* Updater Kthread using asynchronous call_rcu() */
static int updater_async_func(void *data)
{
    int counter = 500;

    while (!kthread_should_stop() && !stop_threads) {
        struct my_shared_data *new_data;
        struct my_shared_data *old_data;

        new_data = kmalloc(sizeof(*new_data), GFP_KERNEL);
        if (!new_data) {
            pr_err("[RCU Async Updater] Memory allocation failed\n");
            msleep(1000);
            continue;
        }

        new_data->value = counter++;
        pr_info("[RCU Async Updater] Preparing new structure with value: %d\n", new_data->value);

        old_data = rcu_dereference_protected(global_data, 1);
        rcu_assign_pointer(global_data, new_data);

        /* Asynchronously register a callback; does not block the thread */
        if (old_data) {
            pr_info("[RCU Async Updater] Registering asynchronous callback via call_rcu()\n");
            call_rcu(&old_data->rcu, my_rcu_reclaim_callback);
        }

        msleep(4500);
    }
    return 0;
}

static int __init rcu_experiment_init(void)
{
    long i;
    struct my_shared_data *initial_data;

    pr_info("[RCU Module] Initializing RCU 2.6.39 Experiment Module\n");

    /* Allocate initial shared structure */
    initial_data = kmalloc(sizeof(*initial_data), GFP_KERNEL);
    if (!initial_data)
        return -ENOMEM;
    initial_data->value = 10;
    RCU_INIT_POINTER(global_data, initial_data);

    /* Spawn Reader Threads */
    for (i = 0; i < NUM_READERS; i++) {
        reader_threads[i] = kthread_run(reader_func, (void *)i, "rcu_reader_%ld", i);
        if (IS_ERR(reader_threads[i])) {
            pr_err("[RCU Module] Failed to spawn reader thread %ld\n", i);
            stop_threads = 1;
            return PTR_ERR(reader_threads[i]);
        }
    }

    /* Spawn Synchronous Updater Thread */
    updater_thread = kthread_run(updater_sync_func, NULL, "rcu_sync_updater");
    if (IS_ERR(updater_thread)) {
        pr_err("[RCU Module] Failed to spawn sync updater thread\n");
        stop_threads = 1;
        return PTR_ERR(updater_thread);
    }

    /* Spawn Asynchronous Updater Thread */
    call_rcu_updater_thread = kthread_run(updater_async_func, NULL, "rcu_async_updater");
    if (IS_ERR(call_rcu_updater_thread)) {
        pr_err("[RCU Module] Failed to spawn async updater thread\n");
        stop_threads = 1;
        return PTR_ERR(call_rcu_updater_thread);
    }

    return 0;
}

static void __exit rcu_experiment_exit(void)
{
    int i;
    struct my_shared_data *final_data;

    pr_info("[RCU Module] Exiting Module. Stopping threads...\n");
    stop_threads = 1;

    /* Stop all kthreads */
    if (updater_thread && !IS_ERR(updater_thread))
        kthread_stop(updater_thread);
        
    if (call_rcu_updater_thread && !IS_ERR(call_rcu_updater_thread))
        kthread_stop(call_rcu_updater_thread);

    for (i = 0; i < NUM_READERS; i++) {
        if (reader_threads[i] && !IS_ERR(reader_threads[i]))
            kthread_stop(reader_threads[i]);
    }

    /* Wait for any pending call_rcu() callbacks to finish execution */
    pr_info("[RCU Module] Waiting for outstanding callbacks via rcu_barrier()...\n");
    rcu_barrier();
    pr_info("[RCU Module] rcu_barrier() complete.\n");

    /* Free remaining global data allocation */
    final_data = rcu_dereference_protected(global_data, 1);
    if (final_data) {
        kfree(final_data);
    }

    pr_info("[RCU Module] Module safely unloaded\n");
}

module_init(rcu_experiment_init);
module_exit(rcu_experiment_exit);
