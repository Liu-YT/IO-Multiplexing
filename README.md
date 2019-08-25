# IO Multiplexing

select、poll、epoll

## 源码剖析
* [select源码剖析](./源码剖析/select.md)
* [poll源码剖析](./源码剖析/poll.md)
* [epoll源码剖析](./源码剖析/epoll.md) 

## 简单DEMO
* [select demo](./select)
* [poll demo](./poll)
* [epoll demo](./epoll)

## 原理概述

### select
调用 select 函数
1. 从用户空间拷贝 fd_set 到内核空间
2. 注册回调函数 __pollwait
3. 遍历所有 fd， 对全部指定设备做一次 poll（这里的 poll 是一个文件操作，它有两个参数，一个是文件 fd 本身，一个是当设备尚未就绪时调用的回调函数 __pollwait，这个函数把设备自己特有的等待队列传给内核，让内核把当前的进程挂载到其中）
4. 当设备就绪时，设备就会唤醒在自己特有等待队列中的所有节点，于是当前进程就获取到了完成的信号，poll 文件操作返回的是一组标准的掩码，其中的各个位指示当前的不同的就绪状态（全0为没有任何事件触发），根据 mask 可对 fd_set 赋值
5. 如何所有设备的返回的掩码都没有显示任何的事件触发，就去掉回调函数的函数指针，进入有限时的睡眠状态，再回复和不断做 poll，直到其中一个设备有事件触发为止
6. 只要有事件触发，系统调用返回，将 fd_set 从内核空间拷贝到用户空间，回到用户态，用户可以对相关的 fd 作进一步的度或者写操作

### epoll

调用 epoll_create 时
1. 内核帮我们在 epoll 文件系统里建了个 file 结点
2. 在内核 cache 里建了个红黑树用于存储以后 epoll_ctl 传来的socket
3. 建立一个 list 链表，用于存储准备就绪的事件

调用 epoll_ctl 时
1. 把 socket 放到 epoll 文件系统里 file 对象对应的红黑树上
2. 给内核中断处理程序注册一个回调函数，告诉内核如果这个句柄的中断到了，就把它放到准备就绪 list 链表里

调用 epoll_wait 时

观察 list 链表里有没有数据，有数据就返回，没有数据就 sleep，等到 timeout 时间后即使链表没数据也返回。

## 总结
* `select`的最大文件描述符的限制是1024（其描述符最大为1024，此处非数目），可以说其是受内部实现`FD_SIZE`的限制，仅可以通过修改内核重新编译实现修改，而`poll`和`epoll`其实也是受进程的能打开的最大文件描述符数目限制，但是可以修改`ulimit -n <数目>`


## 相关基础博客
* [Linux| 网络IO模型](https://liu-yt.github.io/2019/06/12/Linux-%E7%BD%91%E7%BB%9CIO%E6%A8%A1%E5%9E%8B/)
* [Linux| IO多路复用机制](https://liu-yt.github.io/2019/06/13/Linux-IO%E5%A4%9A%E8%B7%AF%E5%A4%8D%E7%94%A8%E6%9C%BA%E5%88%B6/)


## 一些好的博客
* The Implementation of epoll
  * [The Implementation of epoll(1)](https://idndx.com/2014/09/01/the-implementation-of-epoll-1/)
  * [The Implementation of epoll(2)](https://idndx.com/2014/09/02/the-implementation-of-epoll-2/)
  * [The Implementation of epoll(3)](https://idndx.com/2014/09/22/the-implementation-of-epoll-3/)
  * [The Implementation of epoll(4)](https://idndx.com/2015/07/08/the-implementation-of-epoll-4/)
* [大话 Select、Poll、Epoll](https://cloud.tencent.com/developer/article/1005481)
* [select、poll、epoll之间的区别](https://www.cnblogs.com/aspirant/p/9166944.html)
* [linux内核select/poll，epoll实现与区别](https://www.jb51.net/article/97777.htm)
* [linux 内核poll/select/epoll实现剖析](https://watter1985.iteye.com/blog/1614039)
