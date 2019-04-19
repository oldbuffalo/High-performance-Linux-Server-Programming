#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<exception>
#include<pthread.h>
#include<cstdio>
#include"locker.h"
using namespace std;

template<typename T>
class threadpool{
private:
	int m_thread_number;  //线程池中的线程数
	int m_max_request;    //请求队列中允许的最大请求数
	pthread_t* m_threads; //描述线程池的数组  大小为m_thread_num
	list<T*> m_workqueue; //请求队列
	locker m_queuelocker; //保护请求队列的互斥锁
	sem m_queuestat;      //是否有任务需要处理
	bool m_stop;          //是否结束线程
public:
	//第一个参数是线程池中线程的数量 第二个参数是请求队列中最多允许的、等待处理的请求的数量
	threadpool(int thread_num = 8,int max_requests = 10000);
	~threadpool();
	bool append(T* request);  //往请求队列中添加任务
	static void* worker(void* arg);  //工作线程运行函数  不断从工作队列总取出任务并执行
	void run();
};

/*
需要注意的地方：
在C++程序中pthread_create第三个参数必须指向一个静态函数
而要在一个静态函数中使用类的动态成员(成员变量和成员函数)，有两种方式实现
1.通过类的静态对象来调用。比如单例模式中，静态函数可以通过类的全局唯一实例来访问动态成员函数
2.将类的对象作为参数传递给静态函数，然后在静态函数中引用这个对象，并调用其动态方法
*/
template<typename T>
threadpool<T>::threadpool(int thread_num,int max_requests)
:m_thread_number(thread_num),m_max_request(max_requests),m_stop(false),m_threads(NULL){
	if(thread_num <= 0 || max_requests <= 0)
		throw exception();
	m_threads = new pthread_t[m_thread_number];
	if(!m_threads)
		throw exception();
	//创建m_thread_num根线程   并将它们都设置为脱离线程
	for(int i = 0;i<m_thread_number;i++){
		printf("create the %dth thread\n",i);
		if(pthread_create(m_threads+i,NULL,worker,this) != 0){
			delete []m_threads;
			throw exception();
		}

		if(pthread_detach(m_threads[i]) != 0){
			delete []m_threads;
			throw exception();
		}
	}
}

template<typename T>
threadpool<T>::~threadpool(){
	delete []m_threads;
	m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request){
	//操作工作队列时一定要加锁  因为它被所有线程共享
	m_queuelocker.lock();
	if(m_workqueue.size() > m_max_request){
		m_queuelocker.unlock();
		return false;
	}
	m_workqueue.push_back(request);
	m_queuelocker.unlock();
	m_queuestat.post();

	return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg){
	threadpool* pool = (threadpool*)arg;
	pool->run();
	return pool;
}

//类中的成员每次都需要用pool对象引用出来比较麻烦 因此单独写一个run函数作为类的成员函数
//然后在线程处理函数中调用比较方便
template<typename T>
void threadpool<T>::run(){
	while(!m_stop){
		m_queuestat.wait();  //等待信号量
		m_queuelocker.lock();
		if(m_workqueue.empty()){
			m_queuelocker.unlock();
			continue;
		}
		//双向链表 模拟队列  从尾部进  先进的先出
		T* request = m_workqueue.front();
		m_workqueue.pop_front();
		m_queuelocker.unlock();
		if(!request)
			continue;
		request->process();
	}
}

#endif
