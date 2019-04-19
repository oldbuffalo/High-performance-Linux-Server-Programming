#include<stdio.h>
#include<pthread.h>
#include<unistd.h>
/*
模拟死锁
死锁：拥有资源的进程又想申请别的资源，导致程序无法向前推进
死锁条件：
1.互斥
2.保持等待           自己拥有  还还有别的资源
3.不可剥夺 
4.循环等待           互相索取对方的资源
*/


int a;
int b;
pthread_mutex_t mutex_a;
pthread_mutex_t mutex_b;

void* another(void* arg){

	pthread_mutex_lock(&mutex_b);
	printf("in child thread,got mutex b,waiting for mutex a\n");
	sleep(5);
	++b;
	pthread_mutex_lock(&mutex_a);
	b += a++;
	pthread_mutex_unlock(&mutex_a);
	pthread_mutex_unlock(&mutex_b);
	pthread_exit(NULL);
}

int main()
{
	pthread_t tid;
	pthread_mutex_init(&mutex_a,NULL);
	pthread_mutex_init(&mutex_b,NULL);

	pthread_create(&tid,NULL,another,NULL);

	pthread_mutex_lock(&mutex_a);
	printf("in parent thread,got mutex a,waiting for mutex b\n");
	sleep(5);  //确保每个线程都占有一个锁
	++a;
	pthread_mutex_lock(&mutex_b);
	a += b++;
	pthread_mutex_unlock(&mutex_b);
	pthread_mutex_unlock(&mutex_a);


	pthread_join(tid,NULL);
	pthread_mutex_destroy(&mutex_a);
	pthread_mutex_destroy(&mutex_b);


	return 0;
}
