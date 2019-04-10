#ifndef TIME_WHEEL_TIMER
#define TIME_WHEEL_TIMER

/*
基于排序链表的定时器容器存在一个问题   添加定时器效率很低
时间轮采用类似hash表的结构
时间轮就像一个轮子,虚拟一个指针指向轮子上的一个槽,它以恒定的速度顺时针转动,每转动一步就
指向下一个槽,每次转动称为一个滴答(tick).每转动一步的时间称为时间轮的槽间隔si,实际上为心搏
时间.每个槽上都有一个链表,它们的定时时间相差N*si的整数倍
假设现在指针指向槽cs,要添加一个定时时间位ti的定时器,则该定时器将被插入槽ts对应的链表中
ts = (cs + (ti/si)) % N;
对时间轮而言  要提高定时精度  si要足够小  要提高执行效率  N要足够大

添加定时器的时间  O(1)
删除定时器的时间  O(1)
执行一个定时器    O(N)  实际要比O(N)快的多  因为时间轮将所有的定时器散列到不同的链表上
*/

#include<time.h>
#include<stdio.h>
#include<netinet/in.h>

#define BUF_SIZE 1024

class tw_timer;

//绑定socket和定时器
struct client_data{
	sockaddr_in address;
	int sockfd;
	char buf[BUF_SIZE];
	tw_timer* timer;
};

//定时器类
class tw_timer{
public:
	int rotation;   //记录定时器在时间轮转多少圈后生效
	int time_slot;  //记录定时器属于时间轮上哪个槽
	void (*cb_func)(client_data* );   //定时器回调函数
	client_data* user_data;   //客户数据
	tw_timer* next; //指向下一个定时器
	tw_timer* prev; //指向上一个定时器
public:
	tw_timer(int rot,int ts):rotation(rot),time_slot(ts),next(NULL),prev(NULL){}
};

//时间轮类  管理定时器的类
class time_wheel{
private:
	static const int N = 60;  //时间轮上槽的数目
	static const int SI = 1;  //每1s时间轮转动一次  槽间隔为1s
	tw_timer* slots[N];  //时间轮的槽,其中每个元素指向一个定时器链表 链表无序
	int cur_slot;       //时间轮的当前槽
public:
	time_wheel():cur_slot(0){
		for(int i = 0;i<N;i++){
			slots[i] = NULL;  //初始化每个槽的头结点
		}

	}
	~time_wheel(){
		//遍历每个槽  并销毁其中的定时器
		for(int i = 0;i<N;i++){
			tw_timer* tmp = slots[i];
			while(tmp){
				slots[i] = tmp->next;
				delete tmp;
				tmp = slots[i];
			}

		}

	}

	//根据定时器timeout创建一个定时器,并把它插入合适的槽中
	tw_timer* add_timer(int timeout){
		if(timeout < 0)
			return NULL;
		int ticks = 0;
		//根据待插入定时器的超时值计算它将在时间轮上转动多少个滴答之后被触发
		if(timeout < SI)
			ticks = 1;
		else
			ticks = timeout / SI;
		
		//计算待插入的定时器在时间轮转动多少圈后被触发
		int rotation = ticks / N;
		//计算待插入的定时器应该被插入哪个槽中
		int ts = (cur_slot + (ticks % N))%N;

		//创建新的定时器  它在时间轮上转动rotation之后被触发 且位于第ts槽中
		tw_timer* timer = new tw_timer(rotation,ts);
		
		if(!slots[ts]){
			//当前槽中没有定时器
			printf("add timer,rotation is %d,ts is %d,cur_slot is %d\n",rotation,ts,cur_slot);
			slots[ts] = timer;
		}
		else{
			//头插
			timer->next = slots[ts];
			slots[ts]->prev = timer;
			slots[ts] = timer;
		}
		return timer;
	}
	//删除目标定时器timer
	void del_timer(tw_timer* timer){
		if(!timer)
			return;

		int ts = timer->time_slot;   //定时器所在的槽
		
		//slots[ts]是目标定时器所在槽的头结点
		if(slots[ts] == timer){
			slots[ts] = slots[ts]->next;
			if(slots[ts])
				slots[ts]->prev = NULL;
			delete timer;
		}
		else{
			timer->prev->next = timer->next;  //timer的上一个连下一个
			if(timer->next)
				timer->next->prev = timer->prev;
			delete timer;
		}

	}
	//SI时间到后,调用该函数  时间轮向前滚动一个槽的间隔
	void tick(){
		tw_timer* tmp = slots[cur_slot];  //拿到时间轮上当前槽的头结点
		printf("current slot is %d\n",cur_slot);

		while(tmp){
			printf("tick the timer once\n");
			//如果定时器的rotation值大于0 则它在这一轮不起作用
			if(tmp->rotation > 0){
				tmp->rotation--;
				tmp = tmp->next;
			}
			else{
				//定时器到期  执行定时任务 然后删除该定时器
				tmp->cb_func(tmp->user_data);
				if(tmp == slots[cur_slot]){
					printf("delete header in cur_slot\n");
					slots[cur_slot] = tmp->next;
					if(slots[cur_slot])
						slots[cur_slot]->prev= NULL;
					delete tmp;
					tmp = slots[cur_slot];
				}
				else{
					tmp->prev->next = tmp->next;
					if(tmp->next)
						tmp->next->prev = tmp->prev;
					tw_timer* tmp2 = tmp->next;
					delete tmp;
					tmp = tmp2;
				}

			}
		}
		//更新时间轮的当前槽 以反应时间轮的转动
		cur_slot = (cur_slot+1)%N;
	}
};

#endif 
