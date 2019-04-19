#include<stdio.h>
#include<pthread.h>
#include<stdlib.h>
#include<unistd.h>
#include<errno.h>
#include<signal.h>

/*
每个线程都可以独立地设置信号掩码
pthread版本的sigprocmask为
int pthread_sigmask(int how,const sigset_t* newmask,sigset_t* oldmask);

进程中所有线程共享该进程的信号，所以线程库将根据线程掩码决定把信号发送给哪个具体的线程
因此  在每个子进程中单独设置信号掩码 就很容易导致逻辑错误
所有线程共享信号处理函数 也就是说  在一个线程中设置了某个信号的信号处理函数后，它将覆盖其他
线程为同一信号设置的信号处理函数。
因此  最好用一个专门的线程来处理所有的信号
1.在主线程创建出其他子线程之前就调用pthread_sigmask来设置号信号掩码，所有新创建的子线程将自动
继承这个信号掩码。这样之后，所有线程都不会响应被屏蔽的信号
2.在某个线程中调用sigwait来等待信号并处理之
*/

#define handler_error_en(en,msg) \
	do {errno = en;perror(msg); exit(EXIT_FAILURE);}while(0)

static void* sig_thread(void* arg){
	sigset_t* set = (sigset_t*)arg;
	int s,sig;
	while(1){
		s = sigwait(set,&sig);
		if(s != 0)
			handler_error_en(s,"sigwait");
		printf("Signal handling thread got signal %d\n",sig);
	}

}
int main()
{
	pthread_t tid;
	sigset_t set;

	/*第一个步骤  在主线程中设置信号掩码*/
	sigemptyset(&set);
	sigaddset(&set,SIGQUIT);  //ctrl+\触发
	sigaddset(&set,SIGUSR1);
	int s = pthread_sigmask(SIG_BLOCK,&set,NULL);
	if(s != 0)
		handler_error_en(s,"pthread_sigmask");
	s = pthread_create(&tid,NULL,&sig_thread,(void*)&set);
	if(s != 0)
		handler_error_en(s,"pthread_create");
	pause();
}
