server: main.cpp ./threadpool/pool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h ./userdata/redis.h ./userdata/redis.cpp
	g++ -o server main.cpp ./threadpool/pool.h ./http/http_conn.cpp ./http/http_conn.h ./lock/lock.h ./userdata/redis.h ./userdata/redis.cpp -lpthread -lhiredis -std=c++11

clean:
	rm  -r server
