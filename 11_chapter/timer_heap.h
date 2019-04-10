#ifndef MIN_HEAP
#define MIN_HEAP

#include<iostream>
#include<netinet/in.h>
#include<time.h>
using std::exception;

#define BUF_SIZE 64

/*
用小根堆 管理定时器
之前的方案都是用固定的频率调用心搏函数tick 并在其中检测到期的定时器  然后执行定时器上的回调函数
另外的思路上  将定时器中超时时间最小的一个定时器的超时值作为心搏间隔.这样一旦心搏函数tick被调用
超时时间最小的定时器必然到期,就可以在tick函数中处理该定时器.然后从剩余的定时器中找出超时事件最小的一个
将这段最小事件设置位下一次心搏间隔
*/

class heap_timer;
//绑定socket和定时器
struct client_data{
	sockaddr_in address;
	int sockfd;
	char buf[BUF_SIZE];
	heap_timer* timer;
};

//定时器类
class heap_timer{
public:
	time_t timeout; //定时器生效的绝对时间
	void (*cb_func)(client_data*);  //定时器回调函数
	client_data* user_data; //用户数据
public:
	heap_timer(int delay){
		timeout = time(NULL)+delay;	
	}
};

//时间堆类
class time_heap{
private:
	heap_timer* *array;   //堆数组  堆数组里面存定时器对象的指针  指针数组
	int capacity;        //堆数组的容量
	int cur_size;        //堆数组当前包含元素的个数
public:
	//构造函数一  初始化一个大小为cap的空堆
	time_heap(int cap)throw (std::exception) :capacity(cap),cur_size(0){
		array = new heap_timer* [capacity];  //创建堆数组
		if(!array)
			throw std::exception();
		for(int i = 0;i<capacity;i++)
			array[i] = NULL;
	}
	//构造函数二  用已有数组来初始化堆
	//注意传入参数 应该传入一个指向指针数组的数组
	time_heap(heap_timer** init_array,int size,int cap) throw (std::exception):cur_size(size),capacity(cap){
		if(capacity < size)
			throw std::exception();
		array = new heap_timer* [capacity];  
		if(!array)
			throw std::exception();

		for(int i = 0;i<capacity;i++)
			array[i] = NULL;
		if(size != 0){
			//初始化堆数组
			for(int i = 0;i<size;i++)
				array[i] = init_array[i];

			//根据数组中的值 建立堆
			//从最后一个根节点开始建立堆   每次都和左右孩子中较小的比较 一直向下调整
			for(int i = (cur_size-1)/2;i>=0;i--)
				percolate_down(i);
		}

	}
	//销毁时间堆
	~time_heap(){
		for(int i = 0;i<cur_size;i++)
			delete array[i];

		delete []array;
	}
public:
	//添加目标定时器timer
	void add_timer(heap_timer* timer) throw (std::exception){
		if(!timer)
			return;
		//如果当前堆数组容量不够  则将其扩大1倍
		if(cur_size >= capacity)  
			resize();

		//加入堆的过程  就是一个上虑的过程
		int parent = 0;
		int child = cur_size++;  //新插入堆的元素
		while(child > 0){
			parent = (cur_size-1)/2;
			//孩子比父小 就上调
			if(timer->timeout <array[parent]->timeout)
				array[child] = array[parent];
			child = parent;
		}
		array[child] = timer;
	}

	//删除目标定时器timer
	void del_timer(heap_timer* timer){
		if(!timer)
			return;
		//仅仅将目标定时器的回调函数设置为空  即所有的延迟销毁  这将节省真正删除该定时器的开销
		//但这样做容易使得数组膨胀
		timer->cb_func = NULL;
	}

	//获得堆顶的定时器
	heap_timer* top() const{
		if(empty())
			return NULL;
		return array[0];
	}

	//删除堆顶部的定时器
	void pop_timer(){
		if(empty())
			return;
		if(array[0]){
			delete array[0];
			//将原来堆顶元素替换成数组中最后一个元素
			array[0] = array[--cur_size];
			percolate_down(0);
		}
	}

	//心搏函数
	void tick(){
		heap_timer* tmp = array[0];
		time_t cur = time(NULL);
		printf("tick\n");	
		//循环处理堆中到期的定时器
		while(!empty()){
			if(!tmp)
				break;
			if(tmp->timeout > cur)
				break;
			//否则就执行堆顶定时器中的任务
			if(array[0]->cb_func)
				array[0]->cb_func(array[0]->user_data);
			pop_timer();
			tmp = array[0];
		}
	}

	//判断堆数组是否为空
	bool empty() const{
		return cur_size == 0;
	}
private:
	//最小堆的下虑操作  它确保堆数组中以第hole个节点作为根的子树拥有最小堆的性质
	void percolate_down(int hole){
		heap_timer* tmp = array[hole];
		int child = 0;
		//用根和左右子孩子比较
		while(hole*2 + 1 < cur_size){
			//左孩子
			child = hole*2+1;
			//找出左右孩子中较小的
			if(child+1 < cur_size && array[child+1]->timeout < array[child]->timeout) 
				child++;
			if(array[child]->timeout < array[hole]->timeout)
				array[hole] = array[child];
			else
				break;
			hole = child; //继续向下调整
		}
		array[child] = tmp;

	}
	//将数组容量扩大一倍
	void resize() throw (std::exception){
		heap_timer* *temp = new heap_timer* [2*capacity];
		if(!temp)
			throw std::exception();
		for(int i = 0;i<2*capacity;i++)
			temp[i] = NULL;
		capacity = 2*capacity;
		for(int i = 0;i<cur_size;i++)
			temp[i] = array[i];
		delete []array;
		array = temp;
	}
	
};



#endif
