#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<sys/epoll.h>
#include<sys/wait.h>
#include<sys/time.h>
#include<fcntl.h>
#include<errno.h>
#include<signal.h>
#include<sys/mman.h>
#include<sys/stat.h>

typedef struct sockaddr SA;
#define BUFSIZE 1024
#define BACKLOG 128
#define USER_LIMIT  5    //最大用户数量限制
#define FD_LIMIT 65535 //文件描述符最大限制
#define PROCESS_LIMIT 65535 //进程个数最大限制
#define MAX_EVENT_SIZE 1024

/*
聊天室的多进程服务器
一个子进程处理一个客户连接
将所有客户socket连接的读缓冲设计为一段共享内存
*/

struct client_data{
	sockaddr_in address; //客户端地址
	int confd;           //socket文件描述符
	pid_t pid;          //处理这个连接的子进程的pid
	int pipefd[2];      //和父进程通信用的管道
};

static const char* shm_name = "/my_shm";
int sig_pipefd[2];
int epollfd;
int listenfd;
int shmfd;
char* share_men = NULL;  

//客户连接数组  进程用客户连接的编号来索引这个数组 即可取得相关的客户连接数据
client_data* users = NULL;

//子进程和客户连接的映射关系 用进程的pid来索引这个数组即可取得该进程所处理的客户连接的编号
int* sub_process = NULL;
int user_count; //当前客户数量
bool stop_child = false;

int setnonblocking(int fd){
	int oldoption = fcntl(fd,F_GETFL);
	int newoption = oldoption | O_NONBLOCK;
	fcntl(fd,F_SETFL,newoption);
	return oldoption;
}

void addfd(int epollfd,int fd){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
	setnonblocking(fd);
}

//统一事件源
void sig_handler(int signo){
	int save_errno = errno;
	int msg = signo;
	send(sig_pipefd[1],(char*)&msg,1,0);
	errno = save_errno;
}

void addsig(int signo,void (*handler)(int),bool restart = true){
	struct sigaction sa;
	memset(&sa,'\0',sizeof(sa));
	sa.sa_handler = handler;
	if(restart){
		//restart标记是否要重启被中断的系统调用
		sa.sa_flags |= SA_RESTART;
	}
	sigfillset(&sa.sa_mask);
	assert(sigaction(signo,&sa,NULL) != -1);

}

void del_resource(){
	close(sig_pipefd[0]);
	close(sig_pipefd[1]);
	close(listenfd);
	close(epollfd);
	shm_unlink(shm_name);
	//munmap(share_men,USER_LIMIT*BUFSIZE);
	delete []users;
	delete []sub_process;
}

//信号处理函数:停止一个子进程
void child_term_handler(int signo){
	stop_child = true;
}
//子进程运行的函数  参数idx指出该子进程处理的客户连接的编号
//users是保存所有客户连接数据的数组 share_men指出共享内存的起始地址
int run_child(int idx,client_data* users,char* share_men){
	epoll_event events[MAX_EVENT_SIZE];
	//子进程用IO复用技术来同时监听两个文件描述符  客户连接socket和与父进程通信的管道文件
	int child_epollfd = epoll_create(5);
	printf("child process epollfd:%d\n",child_epollfd);
	assert(child_epollfd >= 0);
	int confd = users[idx].confd;
	addfd(child_epollfd,confd);
	int pipefd = users[idx].pipefd[1];
	addfd(child_epollfd,pipefd);
	printf("child process get socket:%d,pipefd[1]:%d\n",confd,pipefd);

	//子进程需要设置自己的信号处理函数
	addsig(SIGTERM,child_term_handler,false);
	int ret;
	while(!stop_child){
		int ready = epoll_wait(child_epollfd,events,MAX_EVENT_SIZE,-1);
		if(ready < 0 && errno != EINTR){
			printf("epoll call fail\n");
			break;
		}

		for(int i = 0;i<ready;i++){
			int sockfd = events[i].data.fd;
			if(sockfd == confd && events[i].events & EPOLLIN){
				//本子进程负责的客户连接有数据到达
				memset(share_men+idx*BUFSIZE,'\0',BUFSIZE);
				ret = recv(confd,share_men+idx*BUFSIZE,BUFSIZE-1,0);
				printf("child process:get data from num %d client data:%s",idx,share_men+idx*BUFSIZE);
				if(ret < 0){
					if(errno != EAGAIN)
						stop_child = true;
				}
				else if(ret == 0){
					printf("remote client close the connection\n");
					stop_child = true;
				}
				else
					send(pipefd,(char*)&idx,sizeof(idx),0);
			}
			else if(sockfd == pipefd && events[i].events &EPOLLIN){
				//主进程通知本进程(通过管道)将第client个客户的数据发到本进程负责的客户端
				int client = 0;
				//接受主进程发送来的数据  即有客户数据到达的连接的编号
				ret = recv(pipefd,(char*)&client,sizeof(client),0);
				printf("child process:client:%d get data_index:%d from main process\n",idx,client);
				if(ret < 0){
					if(errno != EAGAIN)
						stop_child = true;
				}
				else if(ret == 0)
					stop_child = true;
				else{
					send(confd,share_men+client*BUFSIZE,BUFSIZE,0);
					printf("child process send data sucessfully\n");
				}
			}
			else
				continue;
		}//for

	}//while

	printf("child process end\n");
	close(child_epollfd);
	close(confd);
	close(pipefd);
	return 0;
}
int main(int argc,char* argv[])
{
	//传入ip，port
	if(argc <= 2){
		printf("usage:%s ip port\n",argv[0]);
		exit(-1);
	}
	const char* ip =argv[1];
	int port = atoi(argv[2]);

	listenfd = socket(AF_INET,SOCK_STREAM,0);
	assert(listenfd >= 0);

	int reuse = 1;
	int ret = setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
	assert(ret != -1);

	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(port);
	inet_aton(ip ,&serveraddr.sin_addr);
	ret = bind(listenfd,(SA*)&serveraddr,sizeof(serveraddr));
	assert(ret != -1);

	ret = listen(listenfd,BACKLOG);
	assert(ret != -1);
	
	user_count = 0;
	users = new client_data[USER_LIMIT+1];
	sub_process = new int[PROCESS_LIMIT];

	for(int i = 0;i<PROCESS_LIMIT;i++)
		sub_process[i] = -1;
	epoll_event events[MAX_EVENT_SIZE];
	epollfd = epoll_create(5);
	assert(epollfd >= 0);
	addfd(epollfd,listenfd);

	ret = socketpair(AF_UNIX,SOCK_STREAM,0,sig_pipefd);
	assert(ret != -1);
	setnonblocking(sig_pipefd[1]);
	addfd(epollfd,sig_pipefd[0]);

	addsig(SIGCHLD,sig_handler);
	addsig(SIGTERM,sig_handler);
	addsig(SIGINT,sig_handler);
	addsig(SIGPIPE,SIG_IGN);

	//创建共享内存 作为所有客户socker连接的读缓存
	shmfd = shm_open(shm_name,O_CREAT|O_RDWR,0666);
//	shmfd = open(shm_name,O_CREAT|O_RDWR,0664);
	assert(shmfd >= 0);
	ret = ftruncate(shmfd,USER_LIMIT*BUFSIZE);
	assert(ret != -1);
	share_men = (char*)mmap(NULL,USER_LIMIT*BUFSIZE,PROT_READ|PROT_WRITE,MAP_SHARED,shmfd,0);
	assert(share_men != MAP_FAILED);
	close(shmfd);

	bool server_stop = false;
	bool terminate = false;

	while(!server_stop){
		int ready = epoll_wait(epollfd,events,MAX_EVENT_SIZE,-1);
		if(ready < 0 && errno != EINTR){
			printf("epoll call fail\n");
			break;
		}

		for(int i = 0;i<ready;i++){
			int sockfd = events[i].data.fd;
			if(sockfd == listenfd){
				struct sockaddr_in clientaddr;
				socklen_t nlen = sizeof(clientaddr);
				int confd = accept(listenfd,(SA*)&clientaddr,&nlen);
				printf("main process confd:%d\n",confd);
				if(confd < 0){
					printf("errno is:%d\n",errno);
					continue;
				}
				//如果请求过多  关闭新的连接
				if(user_count >= USER_LIMIT){
					const char* info = "too many users\n";
					printf("%s\n",info);
					send(confd,info,strlen(info),0);
					close(confd);
					continue;
				} 
				//接收到新连接 保存第user_count个客户连接的相关数据
				users[user_count].address = clientaddr;
				users[user_count].confd = confd;
				//在主进程和子进程之间建立双向管道 以传递必要的数据
				//父进程用pipefd[0]  子进程用piepfd[1]
				ret = socketpair(AF_UNIX,SOCK_STREAM,0,users[user_count].pipefd);
				assert(ret != -1);
				
				pid_t pid = fork();
				if(pid < 0){
					close(confd);
					break;
				}
				else if(pid == 0){
					close(epollfd);
					close(listenfd);
					close(users[user_count].pipefd[0]);  
					close(sig_pipefd[0]);
					close(sig_pipefd[1]);
					run_child(user_count,users,share_men);
					munmap((void*)share_men,USER_LIMIT*BUFSIZE);
					exit(0);
				}
				else{
					close(confd);//因为父进程关闭了confd  下次accept分配的confd的值和上一次的一样
					close(users[user_count].pipefd[1]);
					addfd(epollfd,users[user_count].pipefd[0]);
					users[user_count].pid = pid;
					//记录新的客户连接在users中的索引值 建立进程pid和该索引值间的映射关系
					sub_process[pid] = user_count;
					printf("create new process pid:%d\n",pid);
					user_count++;
					printf("new connection ip:%s port:%d fd:%d,now have %d users\n",
				       inet_ntoa(clientaddr.sin_addr),ntohs(clientaddr.sin_port),
					   confd,user_count);
				}
			}
			else if(sockfd == sig_pipefd[0] && events[i].events &EPOLLIN){
				//处理信号事件
				int sig;
				char signals[1024];
				ret = recv(sig_pipefd[0],signals,sizeof(signals),0);
				if(ret == -1)
					continue;
				else if(ret == 0)
					continue;
				else{
					for(int i = 0;i<ret;i++){
						switch(signals[i]){
							case SIGCHLD:{
								//子进程退出  表示有某个客户端关闭了连接
								pid_t pid;
								int stat;
								while((pid = waitpid(-1,&stat,WNOHANG)) > 0){
									//用子进程的pid取得被关闭的客户连接的编号
									int del_user = sub_process[pid];
									sub_process[pid] = -1;
									if(del_user < 0  || del_user > USER_LIMIT)
										continue;
									epoll_ctl(epollfd,EPOLL_CTL_DEL,users[del_user].pipefd[0],0);
									close(users[del_user].pipefd[0]);
									users[del_user] = users[--user_count];
									sub_process[users[del_user].pid] = del_user;
								}
								if(terminate && user_count == 0)
									server_stop = true;
								break;
							}//SIGCHLD
							case SIGTERM:
							case SIGINT:{
								//结束服务器程序
								printf("kill all the child now\n");
								if(user_count == 0){
									server_stop = true;
									break;
								}
								for(int i = 0;i<user_count;i++){
									int pid = users[i].pid;
									kill(pid,SIGTERM);
								}
								terminate = true;
								break;
							}
							default:
								break;
						}
					}
				}

			}
			else if(events[i].events &EPOLLIN){
				//某个子进程向父进程写入了数据
				int child = 0;
				//读取管道文件 child标记是哪个客户连接有数据到达
				ret = recv(sockfd,(char*)&child,sizeof(child),0);
				printf("main process:read client:%d accross pipe\n",child);
				if(ret == -1)
					continue;
				else if(ret == 0)
					continue;
				else{
					//向负责处理第child个客户连接的子进程之外的其他子进程发送消息
					//通知它们有客户数据要写
					for(int j = 0;j<user_count;j++){
						if(users[j].pipefd[0] != sockfd){  //每个子进程管道的值不一样
							printf("main process send id to other child accross pipe\n");
							//双向管道 可读可写
							send(users[j].pipefd[0],(char*)&child,sizeof(child),0);
						}
					}
				}

			}
		}//for
	}//while

	del_resource();
	return 0;
}
