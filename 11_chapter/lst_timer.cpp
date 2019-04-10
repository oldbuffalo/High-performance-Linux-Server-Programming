#include"lst_timer.h"
#include<stdio.h>
//析构
sort_timer_list::~sort_timer_list(){ 
	//链表被销毁的时候  删除其中所有的定时器
	util_timer* temp = head;
	while(temp){
		head = temp->next;
		delete temp;
		temp = head;
	}

}

//将目标定时器timer添加到链表中
void sort_timer_list::add_timer(util_timer* timer){
	if(!timer)
		return;
	
	if(!head){
		head = tail = timer;
		return;
	}
	/*
	如果目标定时器的超时时间小于当前链表中所有定时器的超时时间,则把该定时器插入链表头部,变成链表
	新的头结点.否则就需要调用重载函数add_timer(util_timer* timer,util_timer* lst_head),把它插入到合适的
	位置,以保证链表的升序性
	*/
	if(timer->timeout < head->timeout){
		timer->next = head;
		head->prev = timer;
		head = timer;
		return;
	}
	add_timer(timer,head);
}


/*
当某个定时任务发生变化时(比如在外部重新设置了超时时间),调整对应的定时器在链表中的位置,
只考虑被调整的定时器的超时时间延长的情况,即往链表尾部移动
*/
void sort_timer_list::adjust_timer(util_timer* timer){
	if(!timer)
		return;
	util_timer* tmp = timer->next;
	//如果被调增的目标定时器处于链表尾部或者该定时器新的超时值仍然小于其下一个定时器的超时值,不用调整
	if(!tmp  || timer->timeout < tmp->timeout)
		return;
	//走到这里代表需要调增,如果目标定时器是链表的头结点,则将该定时器从链表中取出并重新插入链表
	if(timer == head){
		head = head->next;
		head->prev = NULL;
		timer->next = NULL;
		add_timer(timer,head);
	}
	else{//如果目标定时器不是链表的头结点,将该定时器从链表中取出,然后插入其原来所在位置之后的部分链表中
		//目标定时器 不是头  也不是尾  那么链表中至少有3个定时器 timer->next肯定不为空
		timer->prev->next = timer->next;
		timer->next->prev = timer->prev;
		add_timer(timer,timer->next);
	}

}

//将目标定时器timer从链表中删除
void sort_timer_list::del_timer(util_timer* timer){
	if(!timer)
		return;
	//链表中只有一个定时器
	if(head == timer && tail == timer){
		delete timer;
		head = NULL;
		tail = NULL;
		return;
	}

	//链表中至少有两个定时器,删除的是头
	if(timer == head){
		head = head->next;
		head->prev = NULL;
		delete timer;
		return;
	}

	//链表中至少有两个定时器,删除的是尾
	if(timer == tail){
		tail = tail->prev;
		tail->next = NULL;
		delete timer;
		return;
	}

	//删除的目标定时器位于链表的中间
	timer->prev->next = timer->next;
	timer->next->prev = timer->prev;
	delete timer;
}

//SIGALRM信号每次被触发就在其信号处理函数中执行一次tick函数,以处理链表上的任务
//相当于一个心搏函数,每隔一段固定的时间就执行一次,以检测并处理到期的任务
void sort_timer_list::tick(){
	if(!head)
		return;
	printf("timer tick\n");
	time_t cur = time(NULL);  //获得系统当前时间
	util_timer* tmp = head;
	//从头结点开始依次处理每个定时器,直到遇到一个尚未到期的定时器,这就是定时器的核心逻辑
	while(tmp){
		//因为每个定时器都用绝对时间作超时值,所以我们可以把定时器的超时值和系统当前时间比较判断定时器是否到期
		if(cur < tmp->timeout) //没到期
			break;
		tmp->cb_func(tmp->user_data);
		//执行完定时器中的定时任务之后,就将它从链表中删除,并重置链表头结点
		head = tmp->next;
		if(head)
			head->prev = NULL;
		delete tmp;
		tmp = head;
	}

}

//一个重载的辅助函数 被公有的add_timer函数和adjust_timer函数调用,该函数表示将目标定时器timer添加
//到节点lst_head之后的部分链表中
void sort_timer_list::add_timer(util_timer* timer,util_timer* lst_head){
	util_timer* prev = lst_head;
	util_timer* tmp = lst_head->next;
	//遍历lst_head结点之后的链表,找一个超时时间大于目标定时器的超时时间的结点,并将目标定时器插入到该结点之前
	while(tmp){
		if(timer->timeout < tmp->timeout){
			prev->next = timer;
			timer->prev = prev;
			timer->next = tmp;
			tmp->prev = timer;
			break;
		}
		prev = tmp;
		tmp = tmp->next;
	}

	//如果遍历完lst_head结点之后的部分链表,仍未找过超时时间大于目标定时器的超时时间的节点
	//就将目标定时器放在链表尾部
	if(!tmp){
		prev->next = timer;
		timer->prev = prev;
		timer->next = NULL;
		tail = timer;
	}
	
}


