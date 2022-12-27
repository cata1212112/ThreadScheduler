build: libscheduler.so

libscheduler.so: so_scheduler.o
	$(CC) -shared -o $@ $^ -lpthread
	
so_scheduler.o: so_scheduler.c
	$(CC) -fPIC -Wall -g -o $@ -c $< -lpthread
	
.PHONY: clean
clean:
	-rm -f *.o libcheduler.so
