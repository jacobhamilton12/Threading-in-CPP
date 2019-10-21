#include "common.h"
#include "BoundedBuffer.h"
#include "Histogram.h"
#include "common.h"
#include "HistogramCollection.h"
#include "FIFOreqchannel.h"
using namespace std;


//logging vars
vector<int> totals;

void * patient_function(BoundedBuffer &buff, int pat, int num)
{
    /* What will the patient threads do? */
    double sec = 0.0;
    char * dmsg = (char*)(new datamsg(pat,sec,1));
    char * dmsg2 = (char*)(new datamsg(pat,sec,2));
    for(int i = 0; i < num; i++){
        ((datamsg*)dmsg)->seconds += .004;
        ((datamsg*)dmsg2)->seconds += .004;
        vector<char> point(dmsg, dmsg + sizeof(datamsg));
        vector<char> point2(dmsg2, dmsg2 + sizeof(datamsg));
        buff.push(point);
        buff.push(point2);
        sec += .004;
    }
}

void * worker_function(FIFORequestChannel* chan, BoundedBuffer &buff, int work, mutex &m)
{
    /*
		Functionality of the worker threads	
    */
    //create new channel
    m.lock();
    chan->cwrite((char*)new newchannelmsg(), sizeof(newchannelmsg));
    char * newname = chan->cread();
    FIFORequestChannel newchan (newname, FIFORequestChannel::CLIENT_SIDE);
    m.unlock();
    double data;
    //cout << "heredafaf" << endl;
    while(!buff.isEmpty()){
        vector<char> point(buff.pop());
        m.lock();
        newchan.cwrite(point.data(), sizeof(datamsg));
        data = *(double*)newchan.cread();
        m.unlock();
        cout << data << endl;
        totals.at(work-1)++;
        cout << "worker: " << work << endl;
    }

    m.lock();
    MESSAGE_TYPE q = QUIT_MSG;
    newchan.cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    m.unlock();
    
}
int main(int argc, char *argv[])
{
    int n = 1000;    //default number of requests per "patient"
    int p = 10;     // number of patients [1,15]
    int w = 100;    //default number of worker threads
    int b = 1000; 	// default capacity of the request buffer, you should change this default
	int m = MAX_MESSAGE; 	// default capacity of the file buffer
    srand(time_t(NULL));
    mutex mut;
    
    int pid = fork();
    if (pid == 0){
		// modify this to pass along m
        execl ("dataserver", "dataserver", (char *)NULL);
        
    }
    
	FIFORequestChannel* chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
    BoundedBuffer request_buffer(b);
	HistogramCollection hc;
	
    struct timeval start, end;
    gettimeofday (&start, 0);



    /* Start all threads here */
    vector<thread> wthreads; 
    vector<thread> pthreads;
    for(int i = 1; i <= p; i++){
        pthreads.push_back(thread(patient_function, ref(request_buffer), p,n));
    }
    for(int i = 1; i <= w; i++){
        totals.push_back(0);
        wthreads.push_back(thread(worker_function, chan, ref(request_buffer),i,ref(mut)));
    }
    


    
	// Join all threads here
    for (thread & p : pthreads)
	{
		// If thread Object is Joinable then Join that thread.
		if (p.joinable())
			p.join();
	}
    for (thread & w : wthreads)
	{
		// If thread Object is Joinable then Join that thread.
		if (w.joinable())
			w.join();
	}
    //wthreads.clear();

    //print logging stuff
    for(int i = 0; i < totals.size(); i++){
        cout << "Worker " << i+1 << "\tTotal: " << totals.at(i) << endl;
    }


    //time counter
    gettimeofday (&end, 0);
	hc.print ();
    int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)/(int) 1e6;
    int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec)%((int) 1e6);
    cout << "Took " << secs << " seconds and " << usecs << " micor seconds" << endl;

    
    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!!!" << endl;
    delete chan;
    
}
