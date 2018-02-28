#include "shmqueue.h"
#include <iostream>
using namespace std;

int main() 
{
    ShmQueue Queue;
    Queue.Init("testname", 10240);

    auto i = 0;
    for (; i < 10; i++) 
    {
        string buff = std::to_string(i);
        Queue.Push(buff.data(), buff.size());
    }
    cout << "push " << i << " elements" << endl;
    return 0;
}
