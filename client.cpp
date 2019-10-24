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
    char * dmsg = (char*)(new datamsg(pat,0,1));
    //char * dmsg2 = (char*)(new datamsg(pat,sec,2));
    for(int i = 0; i < num; i++){
        //cout << "333333333333" << endl;
        vector<char> point(dmsg, dmsg + sizeof(datamsg));
        //vector<char> point2(dmsg2, dmsg2 + sizeof(datamsg));
        buff.push(point);
        ((datamsg*)dmsg)->seconds += .004;
        //buff.push(point2);
    }
    buff.setMore(false);
}

void * file_function(FIFORequestChannel* chan, BoundedBuffer &buff, char* filename, FILE * outfile, mutex &m, int &bSize){
    m.lock();
    int size = sizeof(filename)/sizeof(char);
    char * buffer = new char[sizeof(filemsg) + sizeof(filename)];
    memcpy(buffer,new filemsg(0,0), sizeof(filemsg));
    strcpy(buffer + sizeof(filemsg),filename);
    chan->cwrite(buffer, sizeof(filemsg) + 20);
    __int64_t length = *(__int64_t*)chan->cread();
    //cout << length << endl;
    ftruncate(fileno(outfile), length);
    //m.unlock();

    //adds messages to buffer
    int len = 256;
    __int64_t offset = 0;
    if(len > length)
        len = length;
    while(len > 0){
        //m.lock();
        buffer = new char[sizeof(filemsg) + sizeof(filename)];
        memcpy(buffer,new filemsg(offset,len), sizeof(filemsg));
        strcpy(buffer + sizeof(filemsg),filename);
        //m.lock();
        buff.push(vector<char>(buffer, buffer + sizeof(filemsg) + strlen(filename)));
        //m.unlock();
        offset += 256;
        if(length - offset < 256){
            len = length - offset;
        }
    }
    m.unlock();
    buff.setMore(false);
}

void * worker_function(FIFORequestChannel* chan, BoundedBuffer &buff, int work, mutex &m, HistogramCollection &hc, FILE * outfile, vector<mutex*> &mutexes)
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
    double data = 0;
    char* ptr = new char[20];
    m.lock();
    while(buff.size() > 0 || buff.hasMore()){
        //cout << "hhhhhhhhhhhhhhhhhhH" << endl;
        //m.unlock();
        if(buff.size() > 0){
            vector<char> msg(buff.pop());
            m.unlock();
            if(((datamsg*)msg.data())->mtype == DATA_MSG){
                //m.lock();
                newchan.cwrite(msg.data(), sizeof(datamsg));
                data = *(double*)newchan.cread();
                //m.unlock();
                int pers = ((datamsg*)msg.data())->person -1;
                mutexes.at(pers)->lock();
                hc.get(pers)->update(data);
                mutexes.at(pers)->unlock();
            }else{
                m.lock();
                long int offset = ((filemsg*)msg.data())->offset;
                newchan.cwrite(msg.data(), sizeof(filemsg) + 20);
                ptr = newchan.cread();
                fseek(outfile, offset, SEEK_SET);
                fwrite(ptr, 1,((filemsg*)msg.data())->length, outfile);
                m.unlock();
                //fflush(outfile);
            }
            //cout << "herererer" << endl;
            totals.at(work-1)++;
            m.lock();
        }else{
            m.unlock();
        }
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
    int w = 100;    //default number of worker threads
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
    char* fname = nullptr;
    int c;
    while((c = getopt(argc, argv, "f:n:p:w:b:")) != -1){
        switch(c) {
            case 'f':
                fname = new char[20];
                if(optarg)
                    fname = optarg;
                break;
            case 'n':
                if(optarg)
                    n = atoi(optarg);
                break;
            case 'p':
                if(optarg)
                    p = atof(optarg);
                break;
            case 'w':
                if(optarg)
                    w = atoi(optarg);
                break;
            case 'b':
                if(optarg)
                    b = atoi(optarg);
                break;
            case '?':
                std::cout << "Error" << std::endl;
        }
    }
    FILE * outfile;
    char* filename;
    if(fname != nullptr){
        filename = new char[strlen(fname) + 10];
        strcpy(filename,"received/");
        strcat(filename, fname);
        outfile = fopen(filename, "w+");
    }
	
    struct timeval start, end;
    gettimeofday (&start, 0);
    
   //{"1.csv"};
    
    // Start all threads here 
    vector<thread> wthreads; 
    vector<thread> pthreads;
    vector<mutex*> mutexes;
    thread fthread;
    if(fname == nullptr){
        for(int i = 1; i <= p; i++){
            mutexes.push_back(new mutex());
            hc.add(new Histogram(10,-2,2));
            pthreads.push_back(thread(patient_function, ref(request_buffer), i,n));
        }
    }else{
        fthread = thread(file_function, chan, ref(request_buffer), fname, outfile, ref(mut), ref(b));
    }
   
    cout << "Loading..." << endl;
    for(int i = 1; i <= w; i++){
        totals.push_back(0);
        wthreads.push_back(thread(worker_function, chan, ref(request_buffer),i,ref(mut),ref(hc), ref(outfile), ref(mutexes)));
    }
    


    
	// Join all threads here
    
    if(pthreads.size() != 0){
        for (thread & p : pthreads)
        {
            // If thread Object is Joinable then Join that thread.
            if (p.joinable())
                p.join();
        }
    }
    if(fthread.joinable())
        fthread.join();
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
    if(fname != nullptr)
        fclose(outfile);
    MESSAGE_TYPE q = QUIT_MSG;
    chan->cwrite ((char *) &q, sizeof (MESSAGE_TYPE));
    cout << "All Done!!!" << endl;
    delete chan;
    
}
