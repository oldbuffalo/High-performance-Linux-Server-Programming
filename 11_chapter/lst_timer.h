/*
由alarm 和 setitimer函数设置的实时闹钟一旦超时  将触发SIGALRM信号
因此 可以利用该信号的信号处理函数来处理定时任务
但是 如果要处理多个定时任务 就需要不断地触发SIGALRM信号,一般而言,SIGALRM信号按照固定的
频率生成,即由alarm或setitimer函数设置的定时周期T保持不变.

处理非活动连接
简单的定时器实现:基于升序链表的定时器  并把它应用到处理非活动连接
定时器对象至少要包含两个成员:1.超时时间  2.任务回调函数  
有时候还可能包含回调函数被执行时需要传入的参数,以及是否重启定时器等信息
如果用链表来管理所有定时器,每个定时器还要包含指向下一个定时器的指针成员
升序定时器链表将其中的定时器按照超时时间做升序排序
*/
/*
添加定时器到链表  O(n)
删除定时器        O(1)
执行定时任务      O(1)
因此添加定时器的效率偏低  需要一直比较超时时间  找到合适的插入位置
*/
#ifndef LST_TIMER
#define LST_TIMER
#include<time.h>
#include<netinet/in.h>
#define BUF_SIZE 1024

class util_timer;  //前向声明

//用户数据结构  
struct client_data{
	struct sockaddr_in address; // 客户端socket地址
	int sockfd;          //连接socket
	char buf[BUF_SIZE];  //读缓冲区
	util_timer* timer;   //定时器
};


//定时器类
class util_timer{
public:
	time_t timeout;  //任务超时时间  这里使用绝对时间
	void (*cb_func)(client_data*);   //任务回调函数
	client_data* user_data; //回调函数处理的客户数据,由定时器的执行者传递给回调函数
	util_timer* prev;  //双向链表
	util_timer* next;
public:
	util_timer():prev(NULL),next(NULL){}
};

//定时器链表  它是一个升序的双向链表  并且带有头结点和尾结点
class sort_timer_list{
private:
	util_timer* head;
	util_timer* tail;
public:
	sort_timer_list():head(NULL),tail(NULL){}
	~sort_timer_list();  //析构
	//将目标定时器timer添加到链表中
	void add_timer(util_timer* timer); 

	//当某个定时任务发生变化时,调整对应的定时器在链表中的位置
    //只考虑被调整的定时器的超时时间延长的情况,即往链表尾部移动
	void adjust_timer(util_timer* timer); 

	void del_timer(util_timer* timer); //将目标定时器timer从链表中删除


	//SIGALRM信号每次被触发就在其信号处理函数中执行一次tick函数,以处理链表上的任务
	//相当于一个心搏函数,每隔一段固定的时间就执行一次,以检测并处理到期的任务
	void tick();
private:
	//一个重载的辅助函数 被公有的add_timer函数和adjust_timer函数调用
	//该函数表示将目标定时器timer添加到节点lst_head之后的部分链表中
	void add_timer(util_timer* timer,util_timer* lst_head);
	                                         
};



#endif
