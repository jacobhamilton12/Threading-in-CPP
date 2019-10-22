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

void * file_function(FIFORequestChannel* chan, BoundedBuffer &buff, char* filename, FILE * outfile, mutex &m){
    //gets file size
    m.lock();
    int size = sizeof(filename)/sizeof(char);
    char * buffer = new char[sizeof(filemsg) + sizeof(filename)];
    memcpy(buffer,new filemsg(0,0), sizeof(filemsg));
    strcpy(buffer + sizeof(filemsg),filename);
    chan->cwrite(buffer, sizeof(filemsg) + 20);
    __int64_t length = *(__int64_t*)chan->cread();
    cout << length << endl;
    ftruncate(fileno(outfile), length);
    

    //adds messages to buffer
    int len = 256;
    __int64_t offset = 0;
    if(len > length)
        len = length;
    while(len > 0){
        buffer = new char[sizeof(filemsg) + sizeof(filename)];
        memcpy(buffer,new filemsg(offset,len), sizeof(filemsg));
        strcpy(buffer + sizeof(filemsg),filename);
        buff.push(vector<char>(buffer, buffer + sizeof(filemsg) + strlen(filename)));
        offset += 256;
        if(length - offset < 256){
            len = length - offset;
        }
    }
    m.unlock();
}

void * worker_function(FIFORequestChannel* chan, BoundedBuffer &buff, int work, mutex &m, HistogramCollection &hc, FILE * outfile)
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
    char* ptr;
    m.lock();
    while(buff.size() > 0){
        m.unlock();
        vector<char> msg(buff.pop());
        m.lock();
        if(((datamsg*)msg.data())->mtype == DATA_MSG){
            newchan.cwrite(msg.data(), sizeof(datamsg));
            data = *(double*)newchan.cread();
            hc.get(((datamsg*)msg.data())->person -1)->update(data);
        }else{
            long int offset = ((filemsg*)msg.data())->offset;
            newchan.cwrite(msg.data(), sizeof(filemsg) + 20);
            ptr = newchan.cread();
            fseek(outfile, offset, SEEK_SET);
            //lseek (fileno(outfile), offset, SEEK_CUR);
            fwrite(ptr, sizeof(char),strlen(ptr), outfile);
        }
        m.unlock();
        //cout << ((datamsg*)point.data())->person << endl;
        //m.unlock();
        //cout << data << endl;
        totals.at(work-1)++;
        m.lock();
        //cout << "worker: " << work << endl;
    }

    if(m.try_lock())
        m.lock();
    MESSAGE_TYPE q = QUIT_MSG;
    newchan.cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    m.unlock();
    
}
int main(int argc, char *argv[])
{
    int n = 100;    //default number of requests per "patient"
    int p = 10;     // number of patients [1,15]
    int w = 20;    //default number of worker threads
    int b = 10000; 	// default capacity of the request buffer, you should change this default
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
    FILE * outfile;
    outfile = fopen("received/out.csv", "wb");
	
    struct timeval start, end;
    gettimeofday (&start, 0);
    
    char* fname = nullptr;//{"1.csv"};
    
    // Start all threads here 
    vector<thread> wthreads; 
    vector<thread> pthreads;
    thread fthread;
    if(fname == nullptr){
        for(int i = 1; i <= p; i++){
            hc.add(new Histogram(10,-2,2));
            pthreads.push_back(thread(patient_function, ref(request_buffer), i,n));
        }
    }else{
        fthread = thread(file_function, chan, ref(request_buffer), fname, outfile, ref(mut));
    }
    cout << "Loading..." << endl;
    for(int i = 1; i <= w; i++){
        totals.push_back(0);
        wthreads.push_back(thread(worker_function, chan, ref(request_buffer),i,ref(mut),ref(hc), ref(outfile)));
    }
    


    
	// Join all threads here
    if(fthread.joinable())
        fthread.join();
    if(pthreads.size() != 0){
        for (thread & p : pthreads)
        {
            // If thread Object is Joinable then Join that thread.
            if (p.joinable())
                p.join();
        }
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

    fclose(outfile);
    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!!!" << endl;
    delete chan;
    
}
