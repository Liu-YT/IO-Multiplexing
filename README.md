# IO Multiplexing

select、poll、epoll

## 源码剖析
* [select源码剖析](./源码剖析/select.md)
* [poll源码剖析](./源码剖析/poll.md)
* [epoll源码剖析](./源码剖析/epoll.md) 

## 简单DEMO
* [select demo](./select)
* [poll demo](./poll)
* [epoll demo](./poll)

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
