<!-- TOC -->

- [select 源码概述](#select-%E6%BA%90%E7%A0%81%E6%A6%82%E8%BF%B0)
  - [`select()`内核入口](#select%E5%86%85%E6%A0%B8%E5%85%A5%E5%8F%A3)
  - [`do_select()`的循环体](#doselect%E7%9A%84%E5%BE%AA%E7%8E%AF%E4%BD%93)
  - [`struct file_operations`设备驱动的操作函数](#struct-fileoperations%E8%AE%BE%E5%A4%87%E9%A9%B1%E5%8A%A8%E7%9A%84%E6%93%8D%E4%BD%9C%E5%87%BD%E6%95%B0)
  - [`poll_wait`与设备的等待队列](#pollwait%E4%B8%8E%E8%AE%BE%E5%A4%87%E7%9A%84%E7%AD%89%E5%BE%85%E9%98%9F%E5%88%97)
  - [等待队列的删除](#%E7%AD%89%E5%BE%85%E9%98%9F%E5%88%97%E7%9A%84%E5%88%A0%E9%99%A4)
  - [细节补充](#%E7%BB%86%E8%8A%82%E8%A1%A5%E5%85%85)
  - [`select` 缺点](#select-%E7%BC%BA%E7%82%B9)
  - [参考链接](#%E5%8F%82%E8%80%83%E9%93%BE%E6%8E%A5)

<!-- /TOC -->
# select 源码概述

> [Linux源码](https://github.com/torvalds/linux)

**`select()`函数原型**
```c
int select(int nfds, 
        fd_set *restrict readfds, 
        fd_set *restrict writefds, 
        fd_set *restrict errorfds, 
        struct timeval *restrict timeout);
```

## `select()`内核入口
`select` 是个系统调用，其进入内核态以后就调用`core_sys_select()` 函数
`fs/select.c`文件
```c
SYSCALL_DEFINE5(select, int, n, fd_set __user *, inp, fd_set __user *, outp,
		fd_set __user *, exp, struct timeval __user *, tvp)
{
    return kern_select(n, inp, outp, exp, tvp);
}
```
```c
static int kern_select(int n, fd_set __user *inp, fd_set __user *outp,
		       fd_set __user *exp, struct timeval __user *tvp)
{
    struct timespec64 end_time, *to = NULL;
    struct timeval tv;
    int ret;

    if (tvp) {
        if (copy_from_user(&tv, tvp, sizeof(tv)))
            return -EFAULT;

        to = &end_time;
        if (poll_select_set_timeout(to,
                tv.tv_sec + (tv.tv_usec / USEC_PER_SEC),
                (tv.tv_usec % USEC_PER_SEC) * NSEC_PER_USEC))
            return -EINVAL;
    }

    ret = core_sys_select(n, inp, outp, exp, to);
    ret = poll_select_copy_remaining(&end_time, tvp, PT_TIMEVAL, ret);

    return ret;
}
```
```c
/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
int core_sys_select(int n, fd_set __user *inp, fd_set __user *outp,
			   fd_set __user *exp, struct timespec64 *end_time)
{
    fd_set_bits fds;

    // ...

    fds.in      = bits;
	fds.out     = bits +   size;
	fds.ex      = bits + 2*size;
	fds.res_in  = bits + 3*size;
	fds.res_out = bits + 4*size;
	fds.res_ex  = bits + 5*size;

    if ((ret = get_fd_set(n, inp, fds.in)) ||
        (ret = get_fd_set(n, outp, fds.out)) ||
        (ret = get_fd_set(n, exp, fds.ex)))
        goto out;
    zero_fd_set(n, fds.res_in);
    zero_fd_set(n, fds.res_out);
    zero_fd_set(n, fds.res_ex);

    ret = do_select(n, &fds, end_time);

    // ...
}
```

`core_sys_select`函数的功能有以下几点
* 参数检查。对 n 进行判断，n 的最大值就是当前进程所能打开的最大文件数量，一般情况下为 1024
* 用一个结构体保存用户传进来的参数，`fd_set_bits`结构，定义如下
    ```c
    /*
    * Scalable version of the fd_set.
    */

    typedef struct {
        unsigned long *in, *out, *ex;
        unsigned long *res_in, *res_out, *res_ex;
    } fd_set_bits;
    ```
    `in`,`out`,`ex`分别保存用户注册的感兴趣事件，而`res_in`，`res_out`，`res_ex`，分别保存这个文件描述符上的用户感兴趣的事件的已发生事件，**当返回时会把`res_in`,`res_out`,`res_ex`中的值赋给`in`,`out`,`ex`，所以这里就有从用户空间到内核空间，内核空间到用户空间的拷贝问题，而且每次调用`select`时都会发生大量的重复来回拷贝问题，造成效率上的问题**。
    `fd_set`和`bits`组装好之后（保存了用户感兴趣的事件）就要做具体的事情了，那就是监听多个文件描述符，它把`fd_set_bits`当做参数调用`do_select()`函数做具体的文件描述符上的监听工作

## `do_select()`的循环体

`do_select()`实质上是一个大的循环体，对每一个主程序要求监听的设备`fd`（`File Descriptor`）做一次`struct file`里的`struct file_operations`结构体里的`poll`操作。

```c
static int do_select(int n, fd_set_bits *fds, struct timespec64 *end_time)
{
    // ...
    for (;;) {
        unsigned long *rinp, *routp, *rexp, *inp, *outp, *exp;
        bool can_busy_loop = false;

        inp = fds->in; outp = fds->out; exp = fds->ex;
        rinp = fds->res_in; routp = fds->res_out; rexp = fds->res_ex;

        for (i = 0; i < n; ++rinp, ++routp, ++rexp) {
            unsigned long in, out, ex, all_bits, bit = 1, j;
            unsigned long res_in = 0, res_out = 0, res_ex = 0;
            __poll_t mask;

            in = *inp++; out = *outp++; ex = *exp++;
            all_bits = in | out | ex;
            if (all_bits == 0) {
                i += BITS_PER_LONG;
                continue;
            }

            for (j = 0; j < BITS_PER_LONG; ++j, ++i, bit <<= 1) {
                struct fd f;
                if (i >= n)
                    break;
                if (!(bit & all_bits))
                    continue;
                f = fdget(i);
                if (f.file) {
                    wait_key_set(wait, in, out, bit,
                                busy_flag);

                    // 对每个fd进行 I/O 事件检测
                    mask = vfs_poll(f.file, wait);

                    fdput(f);
                    if ((mask & POLLIN_SET) && (in & bit)) {
                        res_in |= bit;
                        retval++;
                        wait->_qproc = NULL;
                    }
                    if ((mask & POLLOUT_SET) && (out & bit)) {
                        res_out |= bit;
                        retval++;
                        wait->_qproc = NULL;
                    }
                    if ((mask & POLLEX_SET) && (ex & bit)) {
                        res_ex |= bit;
                        retval++;
                        wait->_qproc = NULL;
                    }
                    /* got something, stop busy polling */
                    if (retval) {
                        can_busy_loop = false;
                        busy_flag = 0;

                    /*
                        * only remember a returned
                        * POLL_BUSY_LOOP if we asked for it
                        */
                    } else if (busy_flag & mask)
                        can_busy_loop = true;

                }
            }
            if (res_in)
                *rinp = res_in;
            if (res_out)
                *routp = res_out;
            if (res_ex)
                *rexp = res_ex;
            cond_resched();
        }
        wait->_qproc = NULL;
        // 退出循环体
        if (retval || timed_out || signal_pending(current))
            break;
        if (table.error) {
            retval = table.error;
            break;
        }

        /* only if found POLL_BUSY_LOOP sockets && not out of time */
        if (can_busy_loop && !need_resched()) {
            if (!busy_start) {
                busy_start = busy_loop_current_time();
                continue;
            }
            if (!busy_loop_timeout(busy_start))
                continue;
        }
        busy_flag = 0;

        /*
            * If this is the first loop and we have a timeout
            * given, then we convert to ktime_t and set the to
            * pointer to the expiry value.
            */
        if (end_time && !to) {
            expire = timespec64_to_ktime(*end_time);
            to = &expire;
        }
        // 进入休眠
        if (!poll_schedule_timeout(&table, TASK_INTERRUPTIBLE,
                        to, slack))
            timed_out = 1;
    }

    poll_freewait(&table);

    return retval;
}
```
```c
static inline __poll_t vfs_poll(struct file *file, struct poll_table_struct *pt)
{
    if (unlikely(!file->f_op->poll))
        return DEFAULT_POLLMASK;
    return file->f_op->poll(file, pt);
}
```
```c
struct file {
    // ...
    const struct file_operations	*f_op;
    // ...
}
```
`vfs_poll`返回当前设备`fd`的状态（比如是否可读可写），依据这个状态，`do_select`接着将会作出不同的动作
* 如果设备`fd`的状态与主程序的感兴趣的I/O事件匹配，则记录下来，`do_select()`退出循环体，并把结果返回给上层主程序
* 如果不匹配，`do_select()`发现`timeout`已经到了或者进程有`signal`信号打断，也会退出循环，只是返回空的结果给上层应用

但如果`do_select()`发现当前没有事件发生，又还没到`timeout`，更没`signal`打扰，内核会在这个循环体里面永远地轮询下去吗？**`select()`把全部`fd`检测一轮之后如果没有可用I/O事件，会调用`poll_schedule_timeout`让当前进程去休眠一段时间，等待`fd`设备或定时器来唤醒自己，然后再继续循环体看看哪些fd可用，以此提高效率。**
```c
static int poll_schedule_timeout(struct poll_wqueues *pwq, int state,
			  ktime_t *expires, unsigned long slack)
{
    int rc = -EINTR;

    set_current_state(state);
    if (!pwq->triggered)
        rc = schedule_hrtimeout_range(expires, slack, HRTIMER_MODE_ABS);
    __set_current_state(TASK_RUNNING);

    /*
        * Prepare for the next iteration.
        *
        * The following smp_store_mb() serves two purposes.  First, it's
        * the counterpart rmb of the wmb in pollwake() such that data
        * written before wake up is always visible after wake up.
        * Second, the full barrier guarantees that triggered clearing
        * doesn't pass event check of the next iteration.  Note that
        * this problem doesn't exist for the first iteration as
        * add_wait_queue() has full barrier semantics.
        */
    smp_store_mb(pwq->triggered, 0);

    return rc;
}
```

## `struct file_operations`设备驱动的操作函数
设备发现I/O事件时会唤醒主程序进程？ 每个设备fd的等待队列在哪？我们什么时候把当前进程添加到它们的等待队列里去了？
```c
mask = vfs_poll(f.file, wait);
```
```c
static inline __poll_t vfs_poll(struct file *file, struct poll_table_struct *pt)
{
    if (unlikely(!file->f_op->poll))
        return DEFAULT_POLLMASK;
    return file->f_op->poll(file, pt);
}
```
就是由上面这行代码完成相关的功能。 不过在此之前，我们得先了解一下系统内核与文件设备的驱动程序之间耦合框架的设计。

上文对每个设备的操作`file->f_op->poll`，是一个针对每个文件设备特定的内核函数，区别于我们平时用的系统调用`poll()`。并且，这个操作是`select()` `poll()` `epoll()`背后实现的共同基础。
> Support for any of these calls requires support from the device driver. This support (for all three calls, `select()` `poll()` and `epoll()`) is provided through the driver’s poll method.

Linux的设计很灵活，它并不知道每个具体的文件设备是怎么操作的（怎么打开，怎么读写），但内核让每个设备拥有一个`struct file_operations`结构体，这个结构体里定义了各种用于操作设备的函数指针，指向操作每个文件设备的驱动程序实现的具体操作函数，即设备驱动的回调函数（callback）。
```c
struct file_operations {
    // ...

    // select()轮询设备fd的操作函数
    __poll_t (*poll) (struct file *, struct poll_table_struct *);
	
    //...
} __randomize_layout;
```

> The device method is in charge of these two steps:
> 1. Call poll_wait() on one or more wait queues that could indicate a change in the poll status. If no file descriptors are currently available for I/O, the kernel causes the process to wait on the wait queues for all file descriptors passed to the system call.
> 2. Return a bit mask describing the operations (if any) that could be immediately performed without blocking.
当`poll`函数返回时，会给出一个文件是否可读写的标志，应用程序根据不同的标志读写相应的文件，实现非阻塞的读写。这些系统调用功能相同: 允许进程来决定它是否可读或写一个或多个文件而不阻塞。这些调用也可阻塞进程直到任何一个给定集合的文件描述符可用来读或写。这些调用都需要来自设备驱动中`poll`方法的支持，`poll`返回不同的标志，告诉主进程文件是否可以读写
这个`file->f_op->poll`对文件设备的操作
1. 在一个或多个可指示查询状态变化的等待队列上调用`poll_wait`。如果没有文件描述符可用来执行 I/O, 内核使这个进程在等待队列上等待所有的传递给系统调用的文件描述符. 驱动通过调用函数`poll_wait`增加一个等待队列到`poll_table`结构
    ```c
    static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
    {
        if (p && p->_qproc && wait_address)
            p->_qproc(filp, wait_address, p);
    }
    ```
    ```c
    /*
    * Do not touch the structure directly, use the access functions
    * poll_does_not_wait() and poll_requested_events() instead.
    */
   typedef struct poll_table_struct {
    poll_queue_proc _qproc;
    __poll_t _key;
   } poll_table;
    ```
2. 检测文件设备的当前状态，返回表示是否能对设备进行无阻塞读写访问的掩码

## `poll_wait`与设备的等待队列
```c
static inline void poll_wait(struct file * filp, wait_queue_head_t * wait_address, poll_table *p)
{
	if (p && p->_qproc && wait_address)
		p->_qproc(filp, wait_address, p);
}

/*
 * Do not touch the structure directly, use the access functions
 * poll_does_not_wait() and poll_requested_events() instead.
 */
typedef struct poll_table_struct {
	poll_queue_proc _qproc;
	unsigned long _key;
} poll_table;

/* 
 * structures and helpers for f_op->poll implementations
 */
typedef void (*poll_queue_proc)(struct file *, wait_queue_head_t *, struct poll_table_struct *);
```
可以看到，`poll_wait()`其实只是调用了`struct poll_table_struct`结构里面绑定的函数指针。而`struct poll_table_struct`里的函数指针，是在`do_select()`初始化的。
```c
int do_select(int n, fd_set_bits *fds, struct timespec *end_time)
{
    // ...
    poll_table *wait;
    int retval, i, timed_out = 0;
    u64 slack = 0;
    __poll_t busy_flag = net_busy_loop_on() ? POLL_BUSY_LOOP : 0;
    unsigned long busy_start = 0;

    rcu_read_lock();
    retval = max_select_fd(n, fds);
    rcu_read_unlock();

    if (retval < 0)
        return retval;
    n = retval;

    poll_initwait(&table);
    wait = &table.pt;
    // ...
}
```
```c
void poll_initwait(struct poll_wqueues *pwq)
{
    init_poll_funcptr(&pwq->pt, __pollwait);
    pwq->polling_task = current;
    pwq->triggered = 0;
    pwq->error = 0;
    pwq->table = NULL;
    pwq->inline_index = 0;
}
EXPORT_SYMBOL(poll_initwait);
```
```c
static inline void init_poll_funcptr(poll_table *pt, poll_queue_proc qproc)
{
    pt->_qproc = qproc;
    pt->_key   = ~(__poll_t)0; /* all events enabled */
}
```
可以发现，`poll_wait`函数其实真正调用的是`__pollwait`函数
```c
/* Add a new entry */
static void __pollwait(struct file *filp, wait_queue_head_t *wait_address,
				poll_table *p)
{
    struct poll_wqueues *pwq = container_of(p, struct poll_wqueues, pt);
    struct poll_table_entry *entry = poll_get_entry(pwq);
    if (!entry)
        return;
    entry->filp = get_file(filp);
    entry->wait_address = wait_address;
    entry->key = p->_key;
    init_waitqueue_func_entry(&entry->wait, pollwake);
    entry->wait.private = pwq;
    // 把当前进程装到设备的等待队列
    add_wait_queue(wait_address, &entry->wait);
}
```
`add_wait_queue()`把当前进程添加到设备的等待队列`wait_queue_head_t`中去。
```c
void add_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry)
{
    unsigned long flags;

    wq_entry->flags &= ~WQ_FLAG_EXCLUSIVE;
    spin_lock_irqsave(&wq_head->lock, flags);
    __add_wait_queue(wq_head, wq_entry);
    spin_unlock_irqrestore(&wq_head->lock, flags);
}
EXPORT_SYMBOL(add_wait_queue);
```
```c
static inline void __add_wait_queue(struct wait_queue_head *wq_head, struct wait_queue_entry *wq_entry)
{
    list_add(&wq_entry->entry, &wq_head->head);
}
```
```c
/**
 * Insert a new element after the given list head. The new element does not
 * need to be initialised as empty list.
 * The list changes from:
 *      head → some element → ...
 * to
 *      head → new element → older element → ...
 *
 * Example:
 * struct foo *newfoo = malloc(...);
 * list_add(&newfoo->entry, &bar->list_of_foos);
 *
 * @param entry The new element to prepend to the list.
 * @param head The existing list.
 */
static inline void
list_add(struct list_head *entry, struct list_head *head)
{
    __list_add(entry, head, head->next);
}
```

## 等待队列的删除
等待队列入口项的添加和删除主要是由`poll_initwait(&table)`和`poll_freewait(&table)`完成。`poll_initwait(&table)`完成初始化`struct poll_wqueues table`的工作，而`poll_freewait(&table)`负责清理这个结构体。这里需要注意的是等待队列中的`wait_queue_t`并不是在唤醒函数`poll_wake`从队列中删除的，而是最后由`poll_freewait(&table)`集中处理的.


## 细节补充

* `fd_set`实质是一个`unsigned long`数组，里面的每一个`long`整值得每一位逗代表一个文件，其中置位为1的位标识用户要监听的文件
    ```c
    typedef __kernel_fd_set		fd_set;
    ```
    ```c
    /*
    * This macro may have been defined in <gnu/types.h>. But we always
    * use the one here.
    */
    #undef __FD_SETSIZE
    #define __FD_SETSIZE	1024

    typedef struct {
    unsigned long fds_bits[__FD_SETSIZE / (8 * sizeof(long))];
    } __kernel_fd_set;
    ```
    `select()`能同时监听的`fd`很少，只有1024个

* 所谓的文件描述符`fd`(`File Descriptor`)，大家也知道它其实只是一个表意的整数值，更深入地说，它是每个进程的`file`数组的下标
    ```c
    struct fd {
        struct file *file;
        unsigned int flags;
    };
    ```
* `wait_queue_head_t`就是一个进程（task）的队列
    ```c
    struct __wait_queue_head {
        spinlock_t		lock;
        struct list_head	task_list;
    };
    typedef struct __wait_queue_head wait_queue_head_t;
    ```

## `select` 缺点
* 支持的最大文件描述符的数量为本进程支持的最大文件数量，当有几万个文件描述符时，不能够支持，即使通过内核微调调整这个值，效率也不高
* 即使有一个文件描述符上有事件到达，也需要返回，但是这个肯定要循环调用它监听，所以每次调用都需要大量重复的从用户空间到内核空间，从内核空间到用户空间的拷贝，而且每次都是全部拷贝，效率低下
* 每次调用都要重复的开辟`entry`将其加入相应文件的等待队列，这里开销太大

## 参考链接
* [select，poll，epoll实现分析—结合内核源代码](https://www.linuxidc.com/Linux/2012-05/59873.htm)
* [大话 Select、Poll、Epoll](https://cloud.tencent.com/developer/article/1005481)
* [Linux设备驱动程序学习](http://blog.chinaunix.net/uid-20543672-id-94299.html)
* [select()/poll() 的内核实现](http://janfan.cn/chinese/2015/01/05/select-poll-impl-inside-the-kernel.html)