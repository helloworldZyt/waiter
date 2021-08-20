#TARGET_s_lib=librtmpclient.a
#TARGET_d_lib=librtmpclient.so
TARGET_exec=go


#TARGET=$(TARGET_s_lib)
#TARGET=$(TARGET_d_lib)
#TARGET=$(TARGET_s_lib) $(TARGET_d_lib)
TARGET=$(TARGET_exec)

SRC_FILE= waiter.c
CFLAGS=-I. -pthread
obj_src=waiter.o

all:$(TARGET)


%.o:%.cpp
	$(CC) -fpic -c $< -o $@ -pthread 


%.o:%.c
	$(CC) -fpic -c $< -o $@ -pthread 


$(TARGET_s_lib):$(obj_src)
	rm -f $@
	ar cr $@ $^


$(TARGET_d_lib):$(obj_src)
	rm -f $@
	g++ -shared -o $@ $^


$(TARGET_exec):$(obj_src)
	gcc -o $@ $^ $(CFLAGS) -ggdb -pthread


install:
	rm -f ../../lib/$(TARGET_s_lib)
	cp -f $(TARGET_s_lib)    ../../lib/


clean:
	$(RM) *.o $(TARGET)


