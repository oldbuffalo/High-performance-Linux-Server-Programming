#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include<sys/socket.h>
#include<arpa/inet.h>
#include<netinet/in.h>
#include<stdlib.h>
#include<unistd.h>
#include<fcntl.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<errno.h>
#include<assert.h>
#include<string.h>
#include<stdio.h>
#include<sys/wait.h>
#include<sys/stat.h>

/*描述一个子进程的类*/
class process{
public:
	pid_t m_pid;        //目标子进程的pid
	int m_pipefd[2];    //父进程和子进程通信用的管道
};

template<typename T>
class processpool{
private:
	static const int MAX_PROCESS_NUMBER = 16; //进程池允许的最大子进程数量
	static const int USER_PER_PROCESS = 65536; //每个子进程最多能处理的客户数量
	static const int MAX_EVENT_NUMBER = 10000; //epoll最多能处理的事件数
	int m_process_number;    //进程池中当前进程的个数
	int m_idx;    //子进程在池中的序号  从0开始
	int m_epollfd; //每一个进程都有一个epoll内核时间表
	int m_listenfd; //监听socket
	int m_stop; //子进程通过m_stop来决定是否停止运行
	process* m_sub_process; //保存所有子进程的描述信息
	static processpool<T>* m_instance; //进程池静态实例
private:
	//将构造函数定义为私有  因此只能通过后面的create静态函数来创建processpool实例
	processpool(int listenfd,int process_number = 8);
	void setup_sig_pipe(); //统一事件源
	void run_parent();
	void run_child();
public:
	//单例模式 保证程序最多创建一个processpool实例
	static processpool<T>* create(int listenfd,int process_number=8){
		if(!m_instance)
			m_instance = new processpool<T>(listenfd,process_number);
		return m_instance;
	}
	~ processpool(){
		delete []m_sub_process;
	}
	void run();   //启动进程池
};

template<typename T>
processpool<T>* processpool<T>::m_instance = NULL;

//用来处理信号的管道 以实现统一事件源  
static int sig_pipefd[2];

static int setnoblocking(int fd){
	int old_option = fcntl(fd,F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_option);
	return old_option;
}

static void addfd(int epollfd,int fd){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
	setnoblocking(fd);
}

static void removefd(int epollfd,int fd){
	epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
	close(fd);
}

static void sig_handler(int sig){
	int save_errno = errno;
	int msg = sig;
	send(sig_pipefd[1],(char*)&msg,1,0);
	errno = save_errno;
}

static void addsig(int sig,void(handler)(int),bool restart = true){
	struct sigaction sa;
	memset(&sa,'\0',sizeof(sa));
	sa.sa_handler = handler;
	if(restart)
		sa.sa_flags |= SA_RESTART;
	sigfillset(&sa.sa_mask);
	assert(sigaction(sig,&sa,NULL) != -1);
}

/*进程池构造函数  注意参数listenfd必须在进程池之前被创建*/
template<typename T>
processpool<T>::processpool(int listenfd,int process_number)
:m_listenfd(listenfd),m_process_number(process_number),m_idx(-1),m_stop(false){
	assert(process_number>0 && process_number<=MAX_PROCESS_NUMBER);

	m_sub_process = new process[process_number];
	assert(m_sub_process);
	
	/*创建process_number个子进程  并建立他们和父进程之间的管道*/
	for(int i = 0;i<process_number;i++){
		int ret = socketpair(AF_UNIX,SOCK_STREAM,0,m_sub_process[i].m_pipefd);
		assert(ret == 0);
		m_sub_process[i].m_pid = fork();
		assert(m_sub_process[i].m_pid >= 0);
		//父进程用m_pipefd[0]  子进程用m_pipefd[1]
		if(m_sub_process[i].m_pid > 0){
			//父进程  用m_pipefd[0]和子进程通信
			printf("create child pid:%d\n",m_sub_process[i].m_pid);
			close(m_sub_process[i].m_pipefd[1]);
			continue;
		}
		else{
			//子进程  跳出循环
			close(m_sub_process[i].m_pipefd[0]);
			m_idx = i;
			break;
		}
	}
}

/*统一事件源*/
template<typename T>
void processpool<T>::setup_sig_pipe(){
	//创建epoll事件监听表和信号管道
	m_epollfd = epoll_create(5);
	assert(m_epollfd != -1);

	int ret = socketpair(AF_UNIX,SOCK_STREAM,0,sig_pipefd);
	assert(ret != -1);
	//信号处理函数往1端写   因此从0端读
	setnoblocking(sig_pipefd[1]);
	addfd(m_epollfd,sig_pipefd[0]);

	addsig(SIGCHLD,sig_handler);
	addsig(SIGTERM,sig_handler);
	addsig(SIGINT,sig_handler);
	addsig(SIGPIPE,SIG_IGN);
}

/*父进程中m_idx值为-1 子进程m_idx值大于等于0,根据此 判断接下来要运行的是父进程代码还是子进程代码*/
template<typename T>
void processpool<T>::run(){
	if(m_idx != -1)
		run_child();
	else
		run_parent();
}

template<typename T>
void processpool<T>::run_child(){
	setup_sig_pipe();
	//每个子进程都通过其在进程池中的序号值m_idx找到与父进程通信的管道
	int pipefd = m_sub_process[m_idx].m_pipefd[1];
	//子进程需要监听管道文件描述符pipefd,因为父进程将通过它来通知子进程accept新连接
	addfd(m_epollfd,pipefd);                     

	epoll_event events[MAX_EVENT_NUMBER];
	T* users = new T[USER_PER_PROCESS];
	assert(users);
	int ret = -1;

	while(!m_stop){
		int ready = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
		if(ready < 0 && errno != EINTR){
			printf("epoll fail\n");
			break;
		}
		
		for(int i = 0;i<ready;i++){
			int sockfd = events[i].data.fd;
			if(sockfd == pipefd && events[i].events & EPOLLIN){
				int client = 0;
				//从父子进程之间的管道读取数据  如果读取成功 表示有新客户连接到来
				ret = recv(sockfd,(char*)&client,sizeof(client),0);
				if((ret < 0 && errno != EAGAIN) || ret == 0)
					continue;
				else{
					struct sockaddr_in clientaddr;
					socklen_t nlen = sizeof(clientaddr);
					int confd = accept(m_listenfd,(struct sockaddr*)&clientaddr,&nlen);
					if(confd < 0){
						printf("child accept error,errno is %d\n",errno);
						continue;
					}
					//注意在子进程中m_sub_process[m_idx].m_pid的值是0
					printf("child process pid:%d get new connection\n",getpid());
					addfd(m_epollfd,confd);
					/*模板T必须实现init方法以初始化一个客户连接  直接使用confd来索引逻辑处理对象*/
					users[confd].init(m_epollfd,confd,clientaddr);
				}
			}
			else if(sockfd == sig_pipefd[0] && events[i].events & EPOLLIN){
				//处理子进程收到的信号
				int sig;
				char signals[1024];
				ret = recv(sockfd,signals,1024,0);
				if(ret <= 0)
					continue;
				for(int i = 0;i<ret;i++){
					switch(signals[i]){
						case SIGCHLD:{
							pid_t pid;
							int stat;
							while((pid = waitpid(-1,&stat,WNOHANG)) > 0)
								continue;
							break;
						}
						case SIGTERM:
						case SIGINT:
							m_stop = true;
							break;
						default:
							break;
					}

				}
			}
			else if(events[i].events &EPOLLIN){
				//其他可读数据  客户请求到来 调用逻辑处理对象的process方法处理
				users[sockfd].process();
			}
			else
				continue;
		
		}

	}

	delete []users;
	users = NULL;
	close(pipefd);
	//注意 m_listenfd应该由其创建者来关闭这个文件描述符
	close(m_epollfd);
}

template<typename T>
void processpool<T>::run_parent(){
	printf("run_parent pid:%d\n",getpid());
	setup_sig_pipe();

	//父进程监听m_listenfd
	addfd(m_epollfd,m_listenfd);

	epoll_event events[MAX_EVENT_NUMBER];
	int sub_process_counter = 0;
	
	int ret = -1;
	int new_con = 1;
	while(!m_stop){
		int ready = epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
		if(ready < 0 && errno != EINTR){
			printf("epoll fail\n");
			break;
		}
		for(int i = 0;i<ready;i++){
			int sockfd = events[i].data.fd;
			if(sockfd == m_listenfd){
				/*如果有新连接到来 就采用Round Robin方式将其分配给一个子进程处理*/
				int i = sub_process_counter;
				do{
					if(m_sub_process[i].m_pid != -1)
						break;
					i = (i+1)%m_process_number;
				}while(i != sub_process_counter);
				
				if(m_sub_process[i].m_pid == -1){
					m_stop = true;
					break;
				}
				sub_process_counter = (i+1)%m_process_number;
				send(m_sub_process[i].m_pipefd[0],(char*)&new_con,sizeof(new_con),0);
				printf("send request to child %d\n",i);
			}
			else if(sockfd == sig_pipefd[0] && events[i].events & EPOLLIN){
				//处理父进程接收到的信号
				int sig;
				char signals[1024];
				ret =recv(sockfd,signals,1024,0);
				if(ret <= 0)
					continue;
				for(int i = 0;i<ret;i++){
					switch(signals[i]){
						case SIGCHLD:{
							pid_t pid;
							int stat;
							while((pid = waitpid(-1,&stat,WNOHANG)) > 0){
								for(int i = 0;i<m_process_number;i++){
									/*如果进程池中第i个子进程退出 则主进程关闭相应的通信管道
									 并设置m_pid为-1 以标记该子进程已经退出*/
									if(m_sub_process[i].m_pid == pid){
										printf("child %d join\n",i);
										close(m_sub_process[i].m_pipefd[0]);
										m_sub_process[i].m_pid = -1;
									}
								}
							}
							/*如果所有的子进程都退出了  则父进程也退出*/
							m_stop = true;
							for(int i = 0;i<m_process_number;i++){
								if(m_sub_process[i].m_pid != -1)
									m_stop = false;
							}
							break;
						}
						case SIGTERM:
						case SIGINT:{
							/*如果父进程收到终止信号  杀死所有子进程  并等待它们全部结束*/
							printf("kill all the child now\n");
							for(int i = 0;i<m_process_number;i++){
								int pid = m_sub_process[i].m_pid;
								if(pid != -1)
									kill(pid,SIGTERM);
							}
							break;
						}
						default:
							break;

					}

				}

			}
			else
				continue;

		}//for

	}//while
	
	close(m_epollfd);
}



#endif
