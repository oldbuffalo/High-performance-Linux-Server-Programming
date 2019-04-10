#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<signal.h>
#include<sys/epoll.h>
//#include"other_lst_timer.h"
#include"lst_timer.h"

typedef struct sockaddr SA;

#define MAX_EVENT_SIZE 1024
#define FD_LIMIT 65535
#define TIMESLOT 5
static int epollfd;
static int pipefd[2];
static sort_timer_list timer_lst;   //管理定时器的链表

int setnonblocking(int fd){
	int old_option = fcntl(fd,F_GETFL);
	int new_option = new_option | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_option);
	return old_option;
}

void addfd(int epollfd,int fd){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
	setnonblocking(fd);
}

void sig_handler(int signo){
	int save_errno = errno;
	int msg = signo;
	send(pipefd[1],(char*)&msg,1,0);
	errno = save_errno;
}

void addsig(int signo){
	struct sigaction sa;
	sa.sa_handler = sig_handler;
	sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	int ret = sigaction(signo,&sa,NULL);
	assert(ret != -1);
}

void timer_handler(){
	//定时处理任务  实际上就是调用tick函数
	timer_lst.tick();
	//因为一次alarm调用只会引起一次SIGALRM信号 所以要重新定时 以不断触发SIGALRM信号
	alarm(TIMESLOT);
}


//定时器回调函数  删除非活动连接socket上的注册事件  并关闭之
void cb_func(client_data* user_data){
	assert(user_data);
	epoll_ctl(epollfd,EPOLL_CTL_DEL,user_data->sockfd,0);
	close(user_data->sockfd);
	printf("close fd %d\n",user_data->sockfd);
}

int main(int argc,char* argv[])
{
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		return -1;
	}

	const char* ip = argv[1];
	int port = atoi(argv[2]);

	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip,&serveraddr.sin_addr);

	int listenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(listenfd >= 0);

	int reuse = 1;
	int ret = setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
	assert(ret != -1);

	ret = bind(listenfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);

	ret = listen(listenfd,5);
	assert(ret != -1);
	
	epoll_event events[MAX_EVENT_SIZE];
	epollfd = epoll_create(5);
	assert(epollfd >= 0);

	addfd(epollfd,listenfd);

	//管道用于信号处理函数和主进程之间通信  传递信号值 
	//信号处理函数往pipefd[1]写入信号值  主进程监听pipefd[0]上的读事件
	ret = socketpair(AF_UNIX,SOCK_STREAM,0,pipefd);
	assert(ret != -1);
	setnonblocking(pipefd[1]);
	addfd(epollfd,pipefd[0]);
	
	//设置信号捕捉函数
	addsig(SIGALRM);
	addsig(SIGTERM);

	client_data* users = new client_data[FD_LIMIT];   //用户数据数组
	bool timeout = false; //标记是否有定时任务需要处理
	alarm(TIMESLOT);  //定时

	bool stop_server = false;	
	while(!stop_server){
		int ready = epoll_wait(epollfd,events,MAX_EVENT_SIZE,-1);
		if(ready < 0 && errno != EINTR){
			printf("epoll call fail\n");
			break;
		}
		
		for(int i = 0;i<ready;i++){
			int sockfd = events[i].data.fd;
			//处理新到的客户连接
			if(sockfd == listenfd){
				struct sockaddr_in clientaddr;
				socklen_t nlen = sizeof(clientaddr);
				int confd = accept(listenfd,(SA*)&clientaddr,&nlen);
				assert(confd >= 0);
				addfd(epollfd,confd);
				printf("new connectin ip:%s port:%d\n",inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port));
				users[confd].address = clientaddr;
				users[confd].sockfd = confd;
				//创建定时器  设置其回调函数与超时时间 然后绑定定时器与用户数据 最后挂到链表上
				util_timer* timer = new util_timer;
				timer->user_data = &users[confd];
				timer->cb_func = cb_func;
				time_t cur = time(NULL);
				timer->timeout = cur + 3*TIMESLOT;
				users[confd].timer = timer;
				timer_lst.add_timer(timer);
			
			}
			else if(sockfd == pipefd[0] && events[i].events & EPOLLIN){
				//处理信号
				int sig;
				char signals[1024];
				ret = recv(pipefd[0],signals,sizeof(signals),0);
				if(ret == -1){
					//处理错误
					continue;
				}
				else if(ret == 0)
					continue;
				else{
					//正确接收到信号处理函数通过管道传递的信号值
					for(int i = 0;i<ret;i++){
						switch(signals[i]){
							case SIGALRM:
								//用timeout变量标记有定时任务需要处理,但不立即处理定时任务,这是因为
								//定时任务的优先级不是很高,优先处理其他更重要的任务
								timeout = true;
								break;
							case SIGTERM:
								stop_server = true;
								break;
						}

					}
	
				}
			}
			else if(events[i].events & EPOLLIN){
				//处理客户连接上接收到的数据
				memset(users[sockfd].buf,'\0',BUF_SIZE);
				ret = recv(sockfd,users[sockfd].buf,BUF_SIZE-1,0);
				printf("get %d bytes of normal data:%s\n",ret,users[sockfd].buf);
				util_timer* timer = users[sockfd].timer;
				if(ret < 0){
					if(errno  != EAGAIN){
						//发生读错误  关闭连接  移除其对应的定时器
						cb_func(&users[sockfd]);
						if(timer)
							timer_lst.del_timer(timer);
					}
				}
				else if(ret == 0){
					//如果对方已经关闭连接 本方也关闭连接  并移除对应的定时器
					cb_func(&users[sockfd]);
					if(timer)
						timer_lst.del_timer(timer);
				}
				else{
					//如果某个客户连接上有数据可读 就要调整该连接上对应的定时器 以延迟该连接被关闭的事件
					if(timer){
						time_t cur = time(NULL);
						timer->timeout = cur + 3*TIMESLOT;  //每次都等3*TIMESLOT时间
						printf("adjust timer once\n");
						timer_lst.add_timer(timer);
					}

				}
			}
		}//for
		//最后处理定时事件  因为I/O事件有更高的优先级  这样做导致定时任务不能精确地按照预期的时间执行
		if(timeout){
			timer_handler();
			timeout = false;
		}

	}//while
	
	close(listenfd);
	close(pipefd[0]);
	close(pipefd[1]);
	delete []users;
	return 0;
}
