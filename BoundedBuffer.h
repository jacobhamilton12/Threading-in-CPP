#ifndef BoundedBuffer_h
#define BoundedBuffer_h

#include <iostream>
#include <queue>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
using namespace std;

class BoundedBuffer
{
private:
  	int cap;
  	queue<vector<char>> q;

	/* mutexto protect the queue from simultaneous producer accesses
	or simultaneous consumer accesses */
	mutex mtx;
	bool closed = false;
	bool more = true;
	
	/* condition that tells the consumers that some data is there */
	condition_variable data_available;
	/* condition that tells the producers that there is some slot available */
	condition_variable slot_available;

public:
	BoundedBuffer(int _cap):cap(_cap){
		
	}
	~BoundedBuffer(){
		
	}

	void push(vector<char> data){
		unique_lock<mutex>lock(mtx);
		slot_available.wait(lock, [&](){return q.size() < cap || closed;});
		if(closed) return;
		q.push(data);
		lock.unlock();
		data_available.notify_one();
	}

	vector<char> pop(){
		unique_lock<mutex>lck(mtx);
		data_available.wait(lck, [&]{return q.size() > 0;});
		vector<char> temp = q.front();
		q.pop(); 
		lck.unlock();
		slot_available.notify_one();
		return temp;
	}
	int size(){
		mtx.lock();
		int size = q.size();
		mtx.unlock();
		return size;
	}
	bool hasMore(){
		return more;
	}
	bool setMore(bool _more){
		more = _more;
	}
};

#endif /* BoundedBuffer_ */
