#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<assert.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<string.h>

/*
压力测试是考验服务器性能的很重要的一个环节
压力测试的程序有很多实现方式，比如I/O复用方式，多线程、多进程并发编程方式，以及这些方式的结合使用。
不过，单纯的I/O复用方式的施压程度是最高的，因为线程和进程的调用本身也是要占用一定CPU时间。
使用epoll来实现一个通用的服务器压力测试程序
*/

//每个客户连接不停地向服务器发送这个请求 
static const char* request = "GET http://localhost/index.html HTTP/1.1\r\nConnection: keep-alive\r\n\r\nxxx";

int setnoblocking(int fd){
	int old_option = fcntl(fd,F_GETFL);
	int new_option = old_option | O_NONBLOCK;
	fcntl(fd,F_SETFL,new_option);
	return new_option;
}

void addfd(int epollfd,int fd){
	epoll_event event;
	event.data.fd = fd;
	event.events = EPOLLOUT|EPOLLET|EPOLLERR;  //一开始注册的是 POLLOUT事件 为了能一上来发出请求
	epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
	setnoblocking(fd);
}

//向服务器写入len字节的数据
bool write_nbytes(int sockfd,const char* buf,int len){
	int bytes_write = 0;
	printf("write out %d bytes to socket %d\n",len,sockfd);
	while(1){
		bytes_write = send(sockfd,buf,len,0);
		if(bytes_write == -1)
			return false;
		else if(bytes_write == 0)
			return false;
		len -= bytes_write;
		buf = buf+bytes_write;
		if(len <= 0)
			return true;
	}

}


//从服务器读取数据 
bool read_once(int sockfd,char* buf,int len){
	int bytes_read = 0;
	memset(buf,'\0',len);
	bytes_read = recv(sockfd,buf,len,0);
	if(bytes_read == -1)
		return false;
	else if(bytes_read == 0)
		return false;
	printf("read in %d bytes from socket %d with content: %s\n",bytes_read,sockfd,buf);

	return true;
}

//向服务器发起num个TCP连接 可以通过改变num来调整测试压力 
void start_conn(int epollfd,int num,const char* ip,int port){
	struct sockaddr_in addr;
	bzero(&addr,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	inet_aton(ip,&addr.sin_addr);

	for(int i = 0;i<num;i++){
		sleep(1);
		int sockfd = socket(AF_INET,SOCK_STREAM,0);
		printf("create 1 sock\n");
		if(sockfd < 0)
			continue;
		if(connect(sockfd,(struct sockaddr*)&addr,sizeof(addr)) == 0){
			printf("build connection %d\n",i);
			addfd(epollfd,sockfd);
		}

	}

}

void close_conn(int epollfd,int sockfd){
	epoll_ctl(epollfd,EPOLL_CTL_DEL,sockfd,0);
	close(sockfd);
}

int main(int argc,char* argv[]){
	assert(argc == 4); //传入ip port 连接数 
	int epollfd = epoll_create(5);
	start_conn(epollfd,atoi(argv[3]),argv[1],atoi(argv[2]));
	epoll_event events[10000];
	char buf[2048];
	while(1){
		int ready = epoll_wait(epollfd,events,10000,2000);
		for(int i = 0;i<ready;i++){
			int sockfd = events[i].data.fd;
			if(events[i].events & EPOLLIN){
				if(!read_once(sockfd,buf,2048))
					close_conn(epollfd,sockfd);
				struct epoll_event event;
				event.events = EPOLLOUT|EPOLLET|EPOLLERR;
				event.data.fd = sockfd;
				epoll_ctl(epollfd,EPOLL_CTL_MOD,sockfd,&event);
			}
			else if(events[i].events & EPOLLOUT){
				if(!write_nbytes(sockfd,request,strlen(request)))
					close_conn(epollfd,sockfd);
				struct epoll_event event;
				event.events = EPOLLIN|EPOLLET|EPOLLERR;
				event.data.fd = sockfd;
				epoll_ctl(epollfd,EPOLL_CTL_MOD,sockfd,&event);
			}
			else if(events[i].events & EPOLLERR)
				close_conn(epollfd,sockfd);
		}
	}


}
