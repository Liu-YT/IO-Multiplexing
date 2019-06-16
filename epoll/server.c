#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/types.h>

#define IPADDRESS   "127.0.0.1"
#define PORT        6666
#define MAXSIZE     1024
#define LISTENQ     5
#define FDSIZE      1000
#define EPOLLEVENTS 100


// 创建套接字并进行绑定
int socket_bind(const char *ip, int port);

// IO多路复用epoll
void do_epoll(int listenfd);

// 事件处理函数
void handle_events(int epollfd, struct epoll_event *events, int num, int listenfd, char *buf);

// 处理接收到的连接
void handle_accept(int epollfd, int listenfd);

// 读处理
void do_read(int epollfd, int fd, char *buf);

// 写处理 
void do_write(int epollfd, int fd, char *buf);

// 添加事件 
void add_event(int epollfd, int fd, int state);

// 修改事件 
void modify_event(int epollfd, int fd, int state);

// 删除事件
void delete_event(int epollfd, int fd, int state);

int main(int argc, char *argv[]) {
    int listenfd = socket_bind(IPADDRESS, PORT);
    listen(listenfd, LISTENQ);
    do_epoll(listenfd);
    return 0;
}

// 创建套接字并进行绑定
int socket_bind(const char *ip, int port) {
    int listenfd;
    struct sockaddr_in server_addr;
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd == -1) {
        perror("socket error:");
        exit(1);
    }

    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &server_addr.sin_addr);
    server_addr.sin_port = htons(port);
    if(bind(listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind error:");
        exit(2);
    }
    return listenfd;
}

// IO多路复用epoll
void do_epoll(int listenfd) {
    int epollfd;
    struct epoll_event events[EPOLLEVENTS];
    int ret;
    char buf[MAXSIZE];
    memset(buf, 0, MAXSIZE);

    // 创建一个描述符
    epollfd = epoll_create(FDSIZE);

    // 添加监听描述符事件
    add_event(epollfd, listenfd, EPOLLIN);

    while(1) {
        // 获取已经准备好的描述符事件
        ret = epoll_wait(epollfd, events, EPOLLEVENTS, -1);
        handle_events(epollfd, events, ret, listenfd, buf);
    }
    close(epollfd);
}

// 事件处理函数
void handle_events(int epollfd, struct epoll_event *events, int num, int listenfd, char *buf) {
    int i, fd;
    for(i = 0; i < num; ++i) {
        fd = events[i].data.fd;
        // 根据描述符的类型和事件类型进行处理
        if((fd == listenfd) && (events[i].events & EPOLLIN)) handle_accept(epollfd, listenfd);
        else if(events[i].events & EPOLLIN) do_read(epollfd, fd, buf);
        else    do_write(epollfd, fd, buf);
    }
}

// 处理接收到的连接
void handle_accept(int epollfd, int listenfd) {
    int clientfd;
    struct sockaddr_in client_addr;
    socklen_t client_addrlen;
    clientfd = accept(listenfd, (struct sockaddr*)&client_addr, &client_addrlen);
    if(clientfd == -1)  perror("accept error:");
    else {
        printf("accept a new client: %s:%d\n", inet_ntoa(client_addr.sin_addr), client_addr.sin_port);
        // 添加一个客户描述符和事件
        add_event(epollfd, clientfd, EPOLLIN);
    }
}

// 读处理
void do_read(int epollfd, int fd, char *buf) {
    int nRead = read(fd, buf, MAXSIZE);
    if(nRead == -1) {
        perror("read error:");
        close(fd);
        delete_event(epollfd, fd, EPOLLIN);
    }
    else if(nRead == 0) {
        fprintf(stderr, "client close.\n");
        close(fd);
        delete_event(epollfd, fd, EPOLLIN);
    }
    else {
        printf("read msg: %s\n", buf);
        modify_event(epollfd, fd, EPOLLOUT);
    }
}

// 写处理 
void do_write(int epollfd, int fd, char *buf) {
    int nWrite = write(fd, buf, strlen(buf));
    if(nWrite == -1) {
        perror("write error:");
        close(fd);
        delete_event(epollfd, fd, EPOLLIN);
    }
    else modify_event(epollfd, fd, EPOLLIN);

    bzero(buf, MAXSIZE);
}

// 添加事件 
void add_event(int epollfd, int fd, int state) {
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
}

// 修改事件 
void modify_event(int epollfd, int fd, int state) {
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
}

// 删除事件
void delete_event(int epollfd, int fd, int state) {
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &ev);
}