#include <sys/types.h>
#include <sys/stat.h>

#include "shmqueue.h"

using namespace std;

ShmQueue::ShmQueue() : m_meta(NULL), m_base(NULL){ }

ShmQueue::~ShmQueue() 
{
	::munmap(m_meta, m_shm_size);
	m_meta = NULL;
	m_base = NULL;
}

int ShmQueue::OpenShm(int flags, size_t size) 
{
	const int fd = shm_open(m_shm_name.c_str(), flags, 00777);

	if ( fd == -1 ) 
	{
		return -1;
	}
	
	if ( size != 0 ) 
	{
	    // align size
	    const size_t pageSize = sysconf(_SC_PAGE_SIZE);

		if ( size % pageSize != 0 )
		{
			size = ((size + pageSize) / pageSize) * pageSize;
		}
	}

	m_shm_size = size;
	
	// resize
	struct stat buffer;
	
	if ( ::fstat(fd, &buffer) == -1 ) 
	{
		return -2;
	} 
	else if ( m_shm_size != buffer.st_size ) 
	{
		if ( m_shm_size == 0 ) 
		{
			m_shm_size = buffer.st_size;
		} 
		else if ( ::ftruncate(fd, m_shm_size) == -1 ) 
		{
			return -3;
		}
	}
	
	void * addr = mmap(0, m_shm_size, PROT_READ | PROT_WRITE , MAP_SHARED, fd, 0);
	
	::close(fd);
	
	if ( addr == MAP_FAILED ) 
	{
		return -4;
	}

	m_meta = (QueueMeta *) addr;
	m_base = (char *) addr + sizeof(QueueMeta);

	return 0;
}


bool ShmQueue::Init(const string & shmName, size_t shmSize ) 
{
	m_shm_name = shmName;
	
	const bool exists = (::access( ("/dev/shm/"+m_shm_name).c_str(), F_OK) == 0);
	
	//maybe wrong permission.
	if (false == exists) 
	{
		const int ret = ::shm_unlink(m_shm_name.c_str());

		printf("ERR: unlink shm queue %s ret %d errno %s ", m_shm_name.c_str(), ret, strerror(errno));
	}
	
	const int oflag = exists ? O_RDWR : (O_RDWR|O_CREAT) ;
	
	const int openRet = OpenShm(oflag, shmSize);
	
	if ( 0 != openRet )
	{
		printf("ERR: open shm queue %s failed! ret %d ", m_shm_name.c_str(), openRet);
		
		return false;
	}
	
	if ( false == exists ) 
	{
		//when manager process start, clear all previous elements in queue.
		memset(m_meta, 0, sizeof(QueueMeta));

		m_meta->capacity = m_shm_size - sizeof(QueueMeta);
		m_meta->head = m_meta->tail = 0;
		m_meta->in = m_meta->out = 0;
		m_meta->createTime = (unsigned int) time(0);
	}
	
	printf("open ShmQueue %s success. head %u tail %u "
			"capacity %u createTime %u in %ld out %ld",
			m_shm_name.c_str(), m_meta->head, m_meta->tail,
			m_meta->capacity, m_meta->createTime, m_meta->in, m_meta->out);
	
	return true;
}

const static unsigned RECORD_HEAD_MAGIC = 0x48524d53; // 'S' 'M' 'R' 'H';

struct RecordHead 
{
	unsigned magic {RECORD_HEAD_MAGIC};
	bool isEndOfQueue : 1;
	unsigned int payloadLength : 31;
	
	RecordHead() : isEndOfQueue(false), payloadLength(0) { }
};

bool ShmQueue::Push(const char * buff, size_t buffLen) 
{
	unsigned int begin = 0;
	unsigned int newTail = 0;
	
	if ( false == Allocate(sizeof(RecordHead) + buffLen, begin, newTail) ) 
	{
		return false;
	}
	
	RecordHead * r = (RecordHead *)(m_base + begin);
	
	r->magic = RECORD_HEAD_MAGIC;
	r->isEndOfQueue = false;
	r->payloadLength = buffLen;
	
	char * toPtr = m_base + begin + sizeof(RecordHead);
	
	memcpy(toPtr, buff, buffLen);
	
	m_meta->in++;
	m_meta->inuse_bytes += buffLen + sizeof(*r);
	m_meta->tail = newTail;
	
	return true;
}

bool ShmQueue::PopFront() 
{
	if ( !GetFront() ) 
	{
		return false;
	}
	
	RecordHead const * h = (RecordHead *)(m_base + m_meta->head);

	m_meta->head += sizeof(RecordHead) + h->payloadLength;
	m_meta->out++;
	m_meta->inuse_bytes -= sizeof(*h) + h->payloadLength;
	
	return true;
}

bool ShmQueue::Front(const char *& buff, size_t & buffLen) const 
{
	buff = NULL;
	buffLen = 0;
	
	if (false == GetFront()) 
	{
		return false;
	}
	
	RecordHead * const h = (RecordHead *)(m_base + m_meta->head);
	buff = (const char *)(h) + sizeof(RecordHead);
	buffLen = h->payloadLength;
	
	return true;
}

bool ShmQueue::Empty() const 
{
	return m_meta->head == m_meta->tail;
}

void ShmQueue::GetStat(ShmQueueStat & s)
{
	s.in = m_meta->in;
	s.out = m_meta->out;
	s.capacity = m_meta->capacity;
	s.inuse_bytes = m_meta->inuse_bytes;
}

//allocate n bytes after m_meta->tail
bool ShmQueue::Allocate(unsigned int n, unsigned int & begin, unsigned int & newTail) const 
{
	//must not make tail==head
	begin = m_meta->tail;
	const unsigned int head_local = m_meta->head;
	
	//[0 *** tail ------ head **** capacity]
	if ( m_meta->tail < head_local ) 
	{ 
		// tail -> head
		if (m_meta->tail + n < head_local) 
		{
			//m_meta->tail += n;
			newTail = m_meta->tail + n;
	        
			return true;
	    }

		begin = -1;
	    
		return false;
	}
	
	//[0 --- head ***** tail --- capacity]
	if ( m_meta->tail + n < m_meta->capacity ) 
	{
		//m_meta->tail += n;
		newTail = m_meta->tail + n;
		
		return true;
	}
	
	if ( m_meta->tail + sizeof(RecordHead) <= m_meta->capacity ) 
	{
		RecordHead * r = (RecordHead *)(m_base + m_meta->tail);
		r->isEndOfQueue = true;
		r->payloadLength = m_meta->capacity - sizeof(RecordHead) - m_meta->tail;
	}
	
	// alloc from begin
	if ( 0 + n < head_local ) 
	{
		//m_meta->tail = n;
		newTail = n;
		begin = 0;
	    
		return true;
	}
	
	begin = -1;

	return false; // no enough space.
}

bool ShmQueue::GetFront() const 
{
	const unsigned int tailLocal = m_meta->tail;
	
	//[0 *** tail ------ head **** capacity]
	if ( tailLocal < m_meta->head )
	{
		// tail -> head
		if ( m_meta->head + sizeof(RecordHead) > m_meta->capacity ) 
		{
			m_meta->head = 0;
	    } 
		else 
		{
			RecordHead * h = (RecordHead *)(m_base + m_meta->head);

			if ( h->isEndOfQueue ) 
			{
				m_meta->head = 0;
	        } 
			else 
			{
				return true;
	        }
		}
	}
	
	if ( m_meta->head == tailLocal ) 
	{
		//empty
		return false;
	}
	
	//[0 --- head ***** tail --- capacity]
	
	if ( m_meta->head + sizeof(RecordHead) > tailLocal ) 
	{
		printf( "ERR: shm corrupted! head > capacity. %s head %u+%u > %u capacity %u ",
			m_shm_name.c_str(), m_meta->head, (unsigned) sizeof(RecordHead),
			tailLocal, m_meta->capacity);
	    
		EmergencyReset();

		return false;
	}
	
	RecordHead * const h = (RecordHead *)(m_base + m_meta->head);
	
	if ( RECORD_HEAD_MAGIC != h->magic ) 
	{
		printf( "ERR: shm corrupted! wrong magic. %s head %u+%u %u capacity %u ",
				m_shm_name.c_str(), m_meta->head, (unsigned) sizeof(RecordHead),
				tailLocal, m_meta->capacity);

		EmergencyReset();
	}
	
	if ( (m_meta->head + sizeof(RecordHead) + h->payloadLength) > tailLocal ) 
	{
		printf( "ERR: shm corrupted! payload > capacity. %s head %u+%u+%u > %u capacity %u ",
				m_shm_name.c_str(), m_meta->head, (unsigned) sizeof(RecordHead),
				h->payloadLength, tailLocal, m_meta->capacity);
	    
		EmergencyReset();
	    
		return false;
	}
	
	return true;
}

void ShmQueue::EmergencyReset() const 
{
	printf( "ERR: ShmQueue shm %s corrupted, do EmergencyReset! "
			" head %u tail %u capacity %u createTime %u in %ld out %ld",
			m_shm_name.c_str(), m_meta->head, m_meta->tail,
			m_meta->capacity, m_meta->createTime, m_meta->in, m_meta->out);

	m_meta->head = m_meta->tail;
}


