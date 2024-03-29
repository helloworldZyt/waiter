/******************************************************************
 * 	> File name:	waiter.c
 * 	> Author:	
 * 	> Created time:	
******************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <net/if.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>

#include "debug.h"

#define LEN128		128
#define LEN512		512
#define LEN1K		1024
#define LEN2K		2028
#define LEN10K		10240

char * log_promat(char *prombuf) {
	memset(prombuf, 0, LEN128);
	char log_ts_pre[64] = "";
	struct tm janustmresult;
	struct timeval tv = {0};
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &janustmresult);
	strftime(log_ts_pre, sizeof(log_ts_pre),
				"%Y-%m-%d %H:%M:%S", &janustmresult);
	snprintf(prombuf, LEN128-1, "%s.%03d [%d]", log_ts_pre, (int)tv.tv_usec/1000, (int)syscall(SYS_gettid));
	return prombuf;
}

static char dumpcharset[] = "0123456789abcdef";
int util_is_print(char val) {
	if ((val >= 'a' && val <= 'z') || (val >= 'A' && val <= 'Z')) {
		return 1;
	} else if ((val >= '0' && val <= '9')) {
		return 1;
	} else if (val == '_' || val == '*' || val == '&' || val == '-' ||
		val == '+') {
		return 1;
	}
	return 0;
}
void util_dump_buf(char *buf, int len) {
	char prom[LEN128] = {0};
	if (buf && len > 0 && len < 512) {
		char outbuf[1024] = {0};
		char outbufvis[1024] = {0};
		int i = 0, oi = 0, ovi = 0;
		uint8_t val = 0;
		uint8_t hval = 0;
		uint8_t lval = 0;
		for (i = 0, oi = 0; i < len && oi < 1024; i++) {
			val = (uint8_t)buf[i];
			lval = val & 0xf;
			hval = val & 0xf0;
			hval >>= 4;

			outbuf[oi++] = dumpcharset[hval];
			outbuf[oi++] = dumpcharset[lval];
			outbuf[oi++] = ' ';
			if (util_is_print(buf[i])) {
				outbufvis[ovi++] = buf[i];
			} else {
				outbufvis[ovi++] = '.';
			}
		}
		
		LOGDBG("[%s]dump_buf %s\n",log_promat(prom), outbuf);
		LOGDBG("[%s]dump_buf %s\n",log_promat(prom), outbufvis);
	}
	return;
}

int connect_to(const char *ip, int port) {
	int clifd = 0;
	struct sockaddr_in dstaddr;

	clifd = socket(AF_INET, SOCK_STREAM, 0);
	if (clifd < 0) {
		LOGERR("create socket");
		return -1;
	}
	int clifml = AF_INET;
	dstaddr.sin_family = AF_INET;
	dstaddr.sin_port = htons(port);
	dstaddr.sin_addr.s_addr = inet_addr(ip);

	size_t addrlen = (clifml == AF_INET)?sizeof(dstaddr):sizeof(dstaddr);

	if (connect(clifd, (struct sockaddr *)&dstaddr, sizeof(dstaddr)) < 0) {
		LOGERR("connect to");
		close(clifd);
		return -1;
	}
	return clifd;
}

int wait_on(const char *ip, int port) {
	int clifd = 0;
	struct sockaddr_in dstaddr;

	clifd = socket(AF_INET, SOCK_STREAM, 0);
	if (clifd < 0) {
		LOGERR("create socket");
		return -1;
	}
	int clifml = AF_INET;
	dstaddr.sin_family = AF_INET;
	dstaddr.sin_port = htons(port);
	dstaddr.sin_addr.s_addr = inet_addr(ip);

	size_t addrlen = (clifml == AF_INET)?sizeof(dstaddr):sizeof(dstaddr);
	struct sockaddr *addrp = (struct sockaddr *)&dstaddr;
	if (bind(clifd, addrp, addrlen) < 0) {
		LOGERR("bind on");
		close(clifd);
		return -1;
	}

	if (listen(clifd, 50) < 0) {
		LOGERR("wait on");
		close(clifd);
		return -1;
	}
	return clifd;
}

void setnonblocking(int sockfd) {
	int opts;
	opts = fcntl(sockfd, F_GETFL);
	if (opts < 0) {
		return;
	}
	opts = opts | O_NONBLOCK;
	if (fcntl(sockfd, F_SETFL, opts) < 0) {
		return;
	}
	return;
}
/*********************************************************************
 * The waiter vvvvvvvvvvvvvv start vvvvvvvvvvvvvv
**********************************************************************/
#define MAX_EVENTS 10
//#define EXIT_FAILURE 0

typedef struct waiter_attribute {
	pthread_t thread_id;
	char *ip;
	int port;
} WaiterAttri, SenderAttri;

typedef struct gust_attribute {
	pthread_t thread_id;
	int num;
	int confd;
	char ip[LEN128];
	int port;
} GuestAttri;

void *guest_running(void *arg) {
	if (NULL == arg) {
		LOGERR("running param error!\n");
		return NULL;
	}
	GuestAttri *guest = (GuestAttri *)arg;
	char rcvbufA[LEN2K], rcvbufB[LEN2K], rcvbuf[LEN2K];
	struct pollfd fds[5] = {0};
	ssize_t rcvlen = 0;
	int num = 0, i = 0, ret;

	char prom[LEN128] = {0};
	int broken_flag = 0;
	LOGDBG("[%s] new guest\n", log_promat(prom)); 
	for(;;) {
		num	= 0;
		memset(fds, 0, sizeof(fds));
		fds[num].fd = guest->confd;
		fds[num].events = POLLIN | POLLERR | POLLHUP;
		fds[num].revents = 0;
		num++;

		ret = poll(fds, num, 10000);
		if (0 > ret) {
			if (errno == EINTR) {
				LOGDBG("[%s]Got an EINTR (%s), ignoring...\n", log_promat(prom), strerror(errno)); 
				continue;
			} else {
				break;
			}
		} else if (0 == ret) {
			LOGDBG("[%s]wait guest timeout!\n", log_promat(prom)); 
			continue;
		}
		LOGDBG("##################### data start %d %s ######################\n", ret, log_promat(prom)); 
		for(i = 0; i < num; i++) {
			if (fds[i].revents & (POLLERR)) {
				broken_flag = 1;
				LOGDBG("[%s]Got poll POLLERR %d (%s)\n", log_promat(prom), errno, strerror(errno)); 
				break;
			} else if (fds[i].revents & POLLIN) {
				if (fds[i].fd == guest->confd) {
					rcvlen = recv(guest->confd, rcvbuf, sizeof(rcvbuf), 0);
					util_dump_buf(rcvbuf, rcvlen);
					if (rcvlen == 0) {
						broken_flag = 1;
						LOGDBG("[%s]Got recvlen 0 %d (%s)\n", log_promat(prom), errno, strerror(errno)); 
						break;
					}
					send(guest->confd, rcvbuf, rcvlen, 0);
				} else {
					broken_flag = 1;
					LOGDBG("[%s]Got poll unknown fd %d (%s)\n", log_promat(prom), errno, strerror(errno)); 
					break;
				}
			} else if (fds[i].revents & (POLLHUP)) {
				broken_flag = 1;
				LOGDBG("[%s]Got poll POLLHUP %d (%s)\n", log_promat(prom), errno, strerror(errno)); 
				break;
			} else if (fds[i].revents & (POLLNVAL)) {
				broken_flag = 1;
				LOGDBG("[%s]Got poll POLLNVAL %d (%s)\n", log_promat(prom), errno, strerror(errno)); 
				break;
			} else {
				broken_flag = 1;
				LOGDBG("[%s]Got poll POLLNVAL %d (%s) %p\n", log_promat(prom), errno, strerror(errno), fds[i].revents); 
				break;
			}
		}
		LOGDBG("##################### data end %s ######################\n", log_promat(prom)); 
		usleep(10000);
		if (broken_flag != 0) {
			break;
		}
	}
	return NULL;
}

void *waiter_running(void *arg) {
	if (NULL == arg) {
		LOGDBG("running param error!\n");
		return NULL;
	}
	GuestAttri *guest_list[MAX_EVENTS] = {NULL};
	int guest_cnt = 0;
	WaiterAttri *waiter = (WaiterAttri *)arg;
	int waitfd = wait_on(waiter->ip, waiter->port);
	if (waitfd < 0) {
		LOGDBG("running failed at wait on!\n");
		return NULL;
	}
	int listen_sock, conn_sock, nfds, epollfd, i, n;
	struct epoll_event ev, events[MAX_EVENTS];
	/* Code to set up listening socket, 'listen_sock',
		(socket(), bind(), listen()) omitted */
	listen_sock = waitfd;

	epollfd = epoll_create1(0);
	if (epollfd == -1) {
		LOGERR("epoll_create1");
		exit(EXIT_FAILURE);
	}

	ev.events = EPOLLIN;
	ev.data.fd = listen_sock;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listen_sock, &ev) == -1) {
		LOGERR("epoll_ctl: listen_sock");
		exit(EXIT_FAILURE);
	}
	char prom[LEN128] = {0};
	LOGDBG("[%s]waiter running on %s %d, with fd %d...\n", log_promat(prom), waiter->ip, waiter->port, waitfd);
	 

	for (;;) {
		nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
		if (nfds == -1) {
			LOGERR("epoll_wait");
			break;
		}

		for (n = 0; n < nfds; ++n) {
			struct sockaddr_in guestaddr;
			int guestaddrlen = sizeof(guestaddr);
			if (events[n].data.fd == listen_sock) {
				conn_sock = accept(listen_sock, (struct sockaddr *) &guestaddr, &guestaddrlen);
				if (conn_sock == -1) {
					LOGERR("accept");
					break;
				}
				setnonblocking(conn_sock);
				// ev.events = EPOLLIN | EPOLLET;
				// ev.data.fd = conn_sock;
				// if (epoll_ctl(epollfd, EPOLL_CTL_ADD, conn_sock, &ev) == -1) {
				// 	LOGERR("epoll_ctl: conn_sock");
				// 	exit(EXIT_FAILURE);
				// }
				GuestAttri *guest = (GuestAttri *)malloc(sizeof(GuestAttri));
				if (guest) {
					guest->confd = conn_sock;
					guest->num = guest_cnt++;
					inet_ntop(guestaddr.sin_family, &(guestaddr.sin_addr), guest->ip, sizeof(guest->ip));
					guest->port= ntohs(guestaddr.sin_port);
					LOGDBG("[%s]Got new guest %s %d\n", log_promat(prom), guest->ip, guest->port); 
					pthread_create(&guest->thread_id, NULL, guest_running, (void *)guest);
					guest_list[guest_cnt-1] = guest;
				}
			} else {
				//do_use_fd(events[n].data.fd);
			}
		}
	}
	// release data
	// for (i = 0; i < guest_cnt; i++) {}
}

int waiter_new(const char *ip , int port) {
	WaiterAttri *waiter = NULL;
	char prom[LEN128] = {0};
	if (ip == NULL || port <= 0) {
		LOGDBG("Param error!\n");
		return -999;
	}
	waiter = (WaiterAttri *)malloc(sizeof(WaiterAttri));
	if (NULL == waiter) {
		LOGERR("malloc");
		return -1;
	}
	waiter->ip = strdup(ip);
	waiter->port = port;
	
	LOGDBG("[%s]new waiter on %s %d\n", log_promat(prom), ip, port); 
	waiter_running(waiter);
	return 0;
}

int sender_new(const char *ip, int port) {
	int snd_fd = connect_to(ip, port);
	return 0;
}

#define err_quit	LOGDBG

void daemonize(const char *cmd){
    int i, fd0, fd1, fd2;
    pid_t pid;
    struct rlimit rl;
    struct sigaction sa;
    /* * Clear file creation mask. */
	/* 因为我们从shell创建的daemon子进程，所以daemon子进程会继承shell的umask，
		如果不清除的话，会导致daemon进程创建文件时屏蔽某些权限 */
    umask(0);
    /* * Get maximum number of file descriptors. */
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0)
        err_quit("%s: can't get file limit", cmd);
    /* * Become a session leader to lose controlling TTY. */
	/* fork后让父进程退出，子进程获得新的pid，肯定不为进程组组长，这是setsid前提 */
    if ((pid = fork()) < 0) {
        err_quit("%s: can't fork", cmd);
	} else if (pid != 0) { /* parent */
        exit(0);
	}
	/* 调用setsid来创建新的进程会话。这使得daemon进程成为会话首进程，脱离和terminal的关联 */
    setsid();
    /* * Ensure future opens won't allocate controlling TTYs. */
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGHUP, &sa, NULL) < 0)
        err_quit("%s: can't ignore SIGHUP", cmd);
	/* 最好在这里再次fork。这样使得daemon进程不再是会话首进程，那么永远没有机会获得控制终端。
		如果这里不fork的话，会话首进程依然可能打开控制终端
	 */
    if ((pid = fork()) < 0) {
        err_quit("%s: can't fork", cmd);
	} else if (pid != 0) { /* parent */
        exit(0);
	}
    /* * Change the current working directory to the root so * we won't prevent file systems from being unmounted. */
	/* 将当前工作目录切换到根目录。父进程继承过来的当前目录可能mount在一个文件系统上，
		如果不切换到根目录，那么这个文件系统不允许unmount
	 */
    if (chdir("/") < 0)
        err_quit("%s: can't change directory to /", cmd);
    /* * Close all open file descriptors. */
    if (rl.rlim_max == RLIM_INFINITY)
        rl.rlim_max = 1024;
	/* 在子进程中关闭从父进程中继承过来的那些不需要的文件描述符。
		可以通过_SC_OPEN_MAX来判断最高文件描述符(不是很必须) */
    for (i = 0; i < rl.rlim_max; i++)
        close(i);
    /* * Attach file descriptors 0, 1, and 2 to /dev/null. */
	/*  */
    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup(0);
    fd2 = dup(0);
    /* * Initialize the log file. */
    openlog(cmd, LOG_CONS, LOG_DAEMON);
    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        syslog(LOG_ERR, "unexpected file descriptors %d %d %d",fd0, fd1, fd2);
        exit(1);
    }
}

/*********************************************************************
 * the waiter ^^^^^^^^^^^^^^^^ end ^^^^^^^^^^^^^^^^
**********************************************************************/

int main (int argc, char *argv[]) {
	const char *dsthost = "0.0.0.0";
	int dstport = 0;
	const char *comnd = "waiter";

	openlog(argv[0], 3, 7);
	syslog(LOG_INFO, "waiter start...\n");

	dstport = 554;
	if (argc > 3) {
		comnd = argv[1];
		dsthost = strdup(argv[2]);
		dstport = atoi(argv[3]);
	}
	else if (argc > 2) {
		dsthost = strdup(argv[1]);
		dstport = atoi(argv[2]);
	} else if (argc > 1) {
		dsthost = strdup(argv[1]);
	}
	if (comnd && !strcasecmp(comnd, "sender")) {
		logs_init("/tmp/waiter/sender.log", 9, NULL);
		sender_new(dsthost, dstport);
	} else {
		logs_init("/tmp/waiter/waiter.log", 9, NULL);
		daemonize(NULL);
		waiter_new(dsthost, dstport);
	}
	

	return 0;
}
