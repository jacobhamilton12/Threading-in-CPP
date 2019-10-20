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
		unique_lock<mutex>lck(mtx);
		while(q.size()>=cap){slot_available.wait(lck);}
		q.push(data);
		data_available.notify_one();
	}

	vector<char> pop(){
		unique_lock<mutex>lck(mtx);
		while(q.size()>=cap){data_available.wait(lck);}
		vector<char> temp = q.front();
		q.pop(); 
		slot_available.notify_one();
		return temp;
	}
};

#endif /* BoundedBuffer_ */
