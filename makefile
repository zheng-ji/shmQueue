all:
	g++ consumer.cpp shmqueue.cpp -o consumer -std=c++11 -lrt
	g++ producer.cpp shmqueue.cpp -o producer -std=c++11 -lrt
