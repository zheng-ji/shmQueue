#pragma once
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>
#include <algorithm>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <type_traits>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

struct ShmQueueStat
{
	int64_t in;
	int64_t out;
	uint64_t capacity;
	int64_t inuse_bytes;
};


//single producer single consumer safe queue, not general multi-thread-safe
class ShmQueue 
{
public:
	//shmName , example: "mmuxdiamond_queue_1"
	bool Init(const std::string & shmName, size_t shmSize);
	
	bool Push(const char * buff, size_t buffLen);
	
	bool Front(const char *& buff, size_t & buffLen) const;
	bool PopFront();
	
	bool Empty() const;
	
	void GetStat(ShmQueueStat & s);
	
	ShmQueue();
	~ShmQueue();
	
	uint64_t m_next_pop_time {0};
	uint64_t m_next_stat_report_time {0};

private:
	struct QueueMeta 
	{
		volatile unsigned int head;
		volatile unsigned int tail;
		
		unsigned int capacity;
		unsigned int createTime;
		
		volatile int64_t in;
		volatile int64_t out;
		volatile int64_t inuse_bytes;
	};
	
	int OpenShm(int flags, size_t size) ;
	bool Allocate(unsigned int n, unsigned int & begin, unsigned int & newTail) const;
	bool GetFront() const;
	
	//when internal shm data is corrupted, drop all elements! reset shm data
	void EmergencyReset() const;

private:
    std::string m_shm_name;
    QueueMeta * m_meta;

    char * m_base;
    size_t m_shm_size;
};
