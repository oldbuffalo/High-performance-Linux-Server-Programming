#include<stdio.h>
#include<time.h>
#include<sys/epoll.h>
#include<errno.h>
/*
linux下的3组IO复用都有超时参数 因此它们不仅能统一处理信号和IO事件 也能统一处理定时事件
由于I/O复用系统调用可能在超时时间到期之前返回(有I/O事件发生),所以我们要利用他们来定时
就需要不断更新定时参数以反应剩余的时间
*/


#define MAX_EVENT_SIZE 1024
#define TIMEOUT 5000

int main()
{
	time_t start = time(NULL);
	time_t end = time(NULL);	
	int timeout = TIMEOUT;
	int epollfd = epoll_create(5);
	epoll_event events[MAX_EVENT_SIZE];
	while(1){
		printf("the timeout is now %d mil-seconds\n",timeout);
		start = time(NULL);
		int ready = epoll_wait(epollfd,events,MAX_EVENT_SIZE,timeout);
		if(ready < 0 && errno != EINTR){
			printf("epoll call fail\n");
			break;
		}
		if(ready == 0){
			//返回0说明超时时间到  此时便可以处理定时任务 并重置定时任务
			timeout = TIMEOUT;
			continue;
		}
		//返回值大于0 则本次epoll_wait调用持续的时间是(end-start)*1000ms,需要将定时时间TIMEOUT减去这段时间
		//来获得下次epoll_wait调用的超时参数
		end = time(NULL);
		timeout = (end-start)*1000;
		//重新计算后的timeout有可能等于0,说明本次epoll_wait返回的时候不仅有文件描述符就绪,而且定时时间刚好到
		if(timeout <= 0)
			timeout = TIMEOUT;
		//handle connections
	}



	return 0;
}
