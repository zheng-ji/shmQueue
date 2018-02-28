#include "shmqueue.h"
#include <iostream>
using namespace std;

int main() 
{
    ShmQueue Queue;
    Queue.Init("testname", 10240);
    cout << endl;
    
    for ( ; false == Queue.Empty(); Queue.PopFront())
    {
        const char * buff = nullptr;    
        size_t buffLen = 0;
        const bool ret = Queue.Front(buff, buffLen);
        
        cout << "element: " << buff << " len: " << buffLen << endl;
        if ( false == ret )
        {
            cout << "sorry" << endl;
            break;
        }
    }
    return 0;
}
