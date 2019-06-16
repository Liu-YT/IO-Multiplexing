#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

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
#define EPOLLEVENTS 20


void handle_connection(int sockfd);

// 事件处理函数
void handle_events(int epollfd, struct epoll_event *events, int num, int sockfd, char *buf);

// 读处理
void do_read(int epollfd, int fd, int sockfd, char *buf);

// 写处理 
void do_write(int epollfd, int fd, int sockfd, char *buf);

// 添加事件 
void add_event(int epollfd, int fd, int state);

// 修改事件 
void modify_event(int epollfd, int fd, int state);

// 删除事件
void delete_event(int epollfd, int fd, int state);

int count = 0;

int main(int argc, char *argv) {
    int connfd;
    struct sockaddr_in client;

    if(argc < 2) {
        printf("Uasge: client [server IP address]\n");
		return -1;
	}

	connfd = socket(AF_INET, SOCK_STREAM, 0);

    if(connfd < 0) {
		perror("socket");
		return -2;
	}

    client.sin_family = AF_INET;
	client.sin_port = htons(PORT);
    inet_pton(AF_INET, IPADDRESS, &client.sin_addr);
	if(connect(connfd, (struct sockaddr*)&client, sizeof(client)) < 0) {
		perror("connect");
		return -3;
	}

    // 处理连接描述符
    handle_connection(connfd);
    close(connfd);
    return 0;
}



void handle_connection(int sockfd) {
    char buf[MAXSIZE];
    int ret;
    struct epoll_event events[EPOLLEVENTS];
    int epollfd = epoll_create(FDSIZE);
    add_event(epollfd, STDIN_FILENO, EPOLLIN);
    while(1) {
        ret = epoll_wait(epollfd, events, EPOLLEVENTS, -1);
        handle_events(epollfd, events, ret, sockfd, buf);
    }
    close(epollfd);
}


// 事件处理函数
void handle_events(int epollfd, struct epoll_event *events, int num, int sockfd, char *buf) {
    int i, fd;
    for(i = 0; i < num; ++i) {
        fd = events[i].data.fd;
        // 根据描述符的类型和事件类型进行处理
        if(events[i].events & EPOLLIN) do_read(epollfd, fd, sockfd, buf);
        else    do_write(epollfd, fd, sockfd, buf);
    }
}

// 读处理
void do_read(int epollfd, int fd, int sockfd, char *buf) {
    int nRead = read(fd, buf, MAXSIZE);
    if(nRead == -1) {
        perror("read error:");
        close(fd);
    }
    else if(nRead == 0) {
        fprintf(stderr, "server close.\n");
        close(fd);
    }
    else {
        if(fd == STDIN_FILENO)
            add_event(epollfd, sockfd, EPOLLOUT);
        else {
            delete_event(epollfd, sockfd, EPOLLIN);
            add_event(epollfd, STDOUT_FILENO, EPOLLOUT);
        }
    }
}

// 写处理 
void do_write(int epollfd, int fd, int sockfd, char *buf) {
    int nWrite = write(fd, buf, strlen(buf));
    char temp[100];
    buf[strlen(buf) - 1] = '\0';
    if(nWrite == -1) {
        perror("write error:");
        close(fd);
    }
    else {
        if(fd == STDOUT_FILENO)
            delete_event(epollfd, fd, EPOLLIN);
        else
            modify_event(epollfd, fd, EPOLLIN);
    }
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