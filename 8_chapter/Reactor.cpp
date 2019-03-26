#include<iostream>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<signal.h>
#include<sys/epoll.h>
#include<string.h>
#include<stdlib.h>
#include<pthread.h>
#include<time.h>
#include<sstream>
#include<iomanip>
#include<errno.h>
#include<queue>
using namespace std;

#define LISTENMAX 128
#define WORK_THREAD_NUM  10
#define MAXSIZE   200000
/*
Reactor模式：主线程(I/O处理单元)只负责监听文件描述符上是否有事件发生，有的话立即
             将该事件通知给工作线程(逻辑单元)。也就是说主线程只负责监听事件，数据
			 读写，接受新的连接以及处理客户端请求都在工作线程中完成。
			 一般用同步I/O模型实现
epoll_wait为例
1.在主线程中往epoll内核事件表中注册socket读就绪事件
2.主线程调用epoll_wait等待socket有数据可读
3.当socket有数据可读时，epoll_wait通知主线程，主线程将可读事件放入请求队列
4.睡眠在请求队列上的某个工作线程被唤醒，它从socket读取数据，并处理客户端请求，
然后往epoll内核事件表上注册该socket的写就绪事件
5.主线程调用epoll_wait等待socket可写
6.当socket可写时，epoll_wait通知主线程，主线程将socket可写事件放入请求队列
7.睡眠在请求队列上的某个工作线程被唤醒，它往socket上写入服务器处理客户端请求的结果
*/
bool g_exit = false;
int  g_epollfd;
int  g_sockfd;
pthread_cond_t  g_acceptcond;
pthread_mutex_t g_acceptmutex;
pthread_cond_t  g_datacond;
pthread_mutex_t g_datamutex;
queue<int> client_request_queue;

void prog_exit(int signo){
	cout<<endl;
	cout<<"recv signal:"<<signo<<" to exit"<<endl;
	g_exit = true;

	epoll_ctl(g_epollfd,EPOLL_CTL_DEL,g_sockfd,NULL);

	close(g_sockfd);
	close(g_epollfd);

	pthread_cond_destroy(&g_acceptcond);
	pthread_mutex_destroy(&g_acceptmutex);

	pthread_cond_destroy(&g_datacond);
	pthread_mutex_destroy(&g_datamutex);

	cout<<"exit succeed"<<endl;
}


int Init_net(short port){	
	//创建监听socket
	int sockfd = socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0);
	if(sockfd < 0){
		cout<<"socket error"<<endl;
		return -1;
	}

	//设置socket选项　消除TIME_WAIT期间地址无法被重用问题
	int optval = 1;
	if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,(char*)&optval,sizeof(optval)) == -1){
		cout<<"setsocket error"<<endl;	
		return -1;	
	}
	if(setsockopt(sockfd,SOL_SOCKET,SO_REUSEPORT,(char*)&optval,sizeof(optval)) == -1){
		cout<<"setsocket error"<<endl;	
		return -1;
	}

	//绑定
	struct sockaddr_in sockaddr;
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(port);
	sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if(bind(sockfd,(struct sockaddr*)&sockaddr,sizeof(sockaddr)) == -1){
		cout<<"bind error"<<endl;	
		return -1;
	}

	//监听
	if(listen(sockfd,LISTENMAX) == -1){
		cout<<"listen error"<<endl;	
		return -1;
	}
	return sockfd;
}

bool daemonize(){


	//进程变成守护进程
	//创建子进程  关闭父进程	
	pid_t pid = fork();
	if(pid < 0){
		cout<<"fork error"<<endl;
		return false;
	}
	else if(pid > 0){
		//父进程
		exit(0);
	}
	

	//子进程   pid = 0
	//创建新的会话
	//之前父子进程在同一个session中，父进程是会话的领头进程
    //如果父进程退出，子进程会成为孤儿进程
	//执行setsid()之后，子进程将重新获得一个新的会话id
	//这时候父进程退出  不会影响到子进程
	pid_t sid = setsid();
	if(sid < 0){
		cout<<"setsid error"<<endl;
		return false;
	}

	//切换工作目录
	if(chdir("/") < 0){
		cout<<"chdir error"<<endl;
		return false;
	}

	//重定向标准输入、输出、错误

	//或者先把标准输入、输出、错误关闭  然后open定位"/dev/null"
	//再或者 先打开"/dev/null"  再把标准输入、输出、错误关闭 然后调用dup三次
	int fd = open("/dev/null",O_RDWR);
	if(fd != -1){
		dup2(fd,STDIN_FILENO); //返回第一个不小于STDIN_FILENO的值
		dup2(fd,STDOUT_FILENO);
		dup2(fd,STDERR_FILENO);
	}
	if(fd > 2)
		close(fd);
	return true;
}

bool SetNoBlock(int clientfd){
	int oldflag = fcntl(clientfd,F_GETFL,0);
	int newflag = oldflag | O_NONBLOCK;
	if(fcntl(clientfd,F_SETFL,newflag) == -1){
		cout<<"set blockfd error"<<endl;
		return false;
	}
	return true;
}

void* accept_thread_func(void* arg){
	
	while(!g_exit){
		pthread_mutex_lock(&g_acceptmutex);
		pthread_cond_wait(&g_acceptcond,&g_acceptmutex);

		struct sockaddr_in clientaddr;
		socklen_t nlen = sizeof(clientaddr);
		int clientfd = accept(g_sockfd,(struct sockaddr*)&clientaddr,&nlen);

		pthread_mutex_unlock(&g_acceptmutex);

		if(clientfd == -1)
			continue;
		cout<<"new client connect:"
		    <<inet_ntoa(clientaddr.sin_addr)<<":"
			<<ntohs(clientaddr.sin_port)<<endl;

		//将新的socket设置成非阻塞
		if(!SetNoBlock(clientfd))
			continue;
		//注册到epoll内核事件表  采用ET
		struct epoll_event tmp;
		tmp.events = EPOLLIN|EPOLLHUP|EPOLLET;
		tmp.data.fd = clientfd;

		if(epoll_ctl(g_epollfd,EPOLL_CTL_ADD,clientfd,&tmp)== -1){
			cout<<"epoll_ctl add clientfd error"<<endl;
		}
	}
	
	return 0;
}


void realease_client(int clientfd){
	if(epoll_ctl(g_epollfd,EPOLL_CTL_DEL,clientfd,NULL) == -1){
		cout<<"realease client error"<<endl;
	}

	close(clientfd);

}


void* work_thread_func(void* arg){
	
	while(!g_exit){
		int clientfd;
		pthread_mutex_lock(&g_datamutex);
		while(client_request_queue.empty())  //任务队列为空 
			pthread_cond_wait(&g_datacond,&g_datamutex);
		clientfd = client_request_queue.front();
		client_request_queue.pop();
		pthread_mutex_unlock(&g_datamutex);

		//读取数据
		char buf[1024];
		bool iserror = false;
		string clientmsg;
		while(1){
			memset(buf,0,sizeof(buf));
			int nread = read(clientfd,buf,sizeof(buf));
			if(nread < 0){
				/*对于非阻塞IO 下面条件满足代表数据全部读取完毕*/
				if(errno == EAGAIN || errno == EWOULDBLOCK)
					break;
				else{
					cout<<"recv error,client disconnected,fd"
					    <<clientfd<<endl;
					realease_client(clientfd);
					iserror = true;
					break;
				}
			}
			else if(nread == 0){
				cout<<"client disconnect,fd:"<<clientfd<<endl;
				realease_client(clientfd);
				iserror = true;
				break;
			}
			clientmsg += buf;
		}
		
		if(iserror)  //发生错误了 就不用继续了
			continue;
		
		//处理业务逻辑
		cout<<"client msg:"<<clientmsg;

		//将消息加上时间戳返回
		time_t now = time(NULL);
		struct tm* nowstr = localtime(&now);
		ostringstream ostimestr; //字符串输出流  
	    ostimestr << "[" << nowstr->tm_year + 1900 << "-"   
     	<<setw(2) << setfill('0')
		<< nowstr->tm_mon + 1 << "-"   
		<< setw(2) << setfill('0')
		<< nowstr->tm_mday << " "  	
		<< setw(2) << setfill('0')
		<< nowstr->tm_hour << ":"   
		<< setw(2) << setfill('0')
		<< nowstr->tm_min << ":"   
		<< setw(2) << setfill('0')
		<< nowstr->tm_sec << "]server reply: ";  

		//在结尾加上时间
		clientmsg.insert(0,ostimestr.str());


		//发送给客户端
		while(!clientmsg.empty()){
			int nsend = write(clientfd,clientmsg.c_str(),clientmsg.length());

			if(nsend < 0){
				if(errno == EWOULDBLOCK){
					sleep(10);
					continue;
				}
			    else{
					cout<<"send error,fd =" <<clientfd<<endl;
					realease_client(clientfd);
					break;
				}
			}
			cout<<"send:"<<clientmsg<<endl;
			clientmsg.erase(0,nsend);
		}
	}

	return 0;
}



int main(int argc,char* argv[])
{
	char ch;
	short port = 0;
	bool isdaemon = false;
	//getopt  命令行解析函数   p:d  代表-p  -d选项  -p后面必须有参数
	//实现传递-p port来设置程序的监听端口号，传递-d来使程序以守护进程运行在后台。
	while((ch = getopt(argc,argv,"p:d")) != -1){
		switch(ch){
			case 'p':
				port = atol(optarg);
				cout<<"set port = "<<port<<endl;
				break;
			case 'd':
				cout<<"daemon"<<endl;
				isdaemon = true;
				break;
		}
	}
	if(isdaemon) //Linux有库函数完成这个功能 int daemon(int,int)
		if(!daemonize()){ 
			cout<<"damonize error!"<<endl;
			return -1;
		}
	if(port == 0)
		port = 12345;
	
	g_sockfd = Init_net(port);
	if(g_sockfd < 0){
		cout<<"Init Net error!"<<endl;
		return -1;
	}

	//往epoll内核时间表注册监听socket读事件
	//EPOLLIN  EPOLLRDHUP
	g_epollfd = epoll_create(MAXSIZE);

	struct epoll_event tmp;
	tmp.events = EPOLLIN | EPOLLRDHUP | EPOLLET ;
	tmp.data.fd = g_sockfd;
	if(epoll_ctl(g_epollfd,EPOLL_CTL_ADD,g_sockfd,&tmp) == -1){
		cout<<"epoll_ctl error"<<endl;
		return -1;
	}
	

	//设置信号处理函数
	//SIGPIPE SIGINT SIGTERM
	//往一个读端关闭的管道或者socket连接中写数据会引发SIGPIPE
	//SIGPIPE默认处理动作是结束进程，不希望因为错误的写操作结束进程
	signal(SIGPIPE,SIG_IGN);
	signal(SIGINT,prog_exit);//键入ctrl+C 中断进程
	signal(SIGTERM,prog_exit); //收到kill命令 中断进程

	//初始化锁和条件变量
	//连接请求的条件变量和锁
	//客户端请求(非连接)的条件变量和锁
	//请求队列的锁  队列用链表
	pthread_cond_init(&g_acceptcond,NULL);
	pthread_mutex_init(&g_acceptmutex,NULL);

	pthread_cond_init(&g_datacond,NULL);
	pthread_mutex_init(&g_datamutex,NULL);


	//创建连接请求处理线程 和  工作线程
	pthread_t pid;
	pthread_create(&pid,NULL,accept_thread_func,NULL);

	for(int i = 0;i<WORK_THREAD_NUM;i++)
		pthread_create(&pid,NULL,work_thread_func,NULL);


	//循环 调用epoll_wait  监听文件描述符上的事件
	struct epoll_event ent[MAXSIZE]; //接受返回事件
	memset(&ent,0,sizeof(ent));

	while(!g_exit){

		//循环取出事件
		int ready = epoll_wait(g_epollfd,ent,MAXSIZE,10);
		if(ready == 0)
			continue;
		else if(ready < 0){
			cout<<"epoll_wait error"<<endl;
			continue;
		}
		cout<<"ready:"<<ready<<endl;
		for(int i = 0;i<ready;i++){
			//如果是连接请求   释放连接请求的条件变量
			if(ent[i].data.fd == g_sockfd){
				pthread_cond_signal(&g_acceptcond);
			}else{
			//如果是数据请求 先将请求加入到请求队列 然后释放条件变量
				pthread_mutex_lock(&g_datamutex);
				client_request_queue.push(ent[i].data.fd);
				pthread_mutex_unlock(&g_datamutex);
				pthread_cond_signal(&g_datacond);
			}
		}
	}

	return 0;
}
