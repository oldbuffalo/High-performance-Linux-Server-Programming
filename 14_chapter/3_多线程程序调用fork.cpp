#include<stdio.h>
#include<unistd.h>
#include<pthread.h>
#include<wait.h>
#include<stdlib.h>

/*
需要注意的是：
在一个多线程程序的某个线程调用了fork，那么新创建的子进程将只有一个执行线程，它是调用fork的那个线程的
完整复制，并且子进程将自动继承父进程中互斥锁的状态，但是不清楚是加锁还是解锁状态，这就可能导致死锁。
*/


pthread_mutex_t mutex;


void* another(void* arg){
	printf("in child thread,lock the mutex\n");
	pthread_mutex_lock(&mutex);
	sleep(5); //父进程的子线程占有互斥锁5秒
	pthread_mutex_unlock(&mutex);
}

int main()
{
	pthread_mutex_init(&mutex,NULL);

	pthread_t tid;
	pthread_create(&tid,NULL,another,NULL);
	sleep(1);  //确保在fork之前  子线程已经开始运行并获得互斥变量mutex
	pthread_t pid = fork();
	if(pid < 0){
		pthread_join(tid,NULL);
		pthread_mutex_destroy(&mutex);
		return 1;
	}
	else if(pid  == 0){
		//子进程
		printf("I am in the child process,want to get the lock\n");
		//子进程继承父进程互斥锁mutex的状态，该互斥锁处于上锁的状态(父进程的子线程占有着)
		//因此  下面这句加锁操作会一直阻塞  尽管从逻辑上来说它是不应该阻塞的
		pthread_mutex_lock(&mutex);
		printf("I can not run to here\n");
		pthread_mutex_unlock(&mutex);
		exit(0);
	}
	else{
		wait(NULL);
	}
	
	pthread_join(tid,NULL);
	pthread_mutex_destroy(&mutex);
	return 0;
}
