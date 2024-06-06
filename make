CC = gcc
CFLAGS = -Wall -g
OBJS = rtk.o
TARGET = rtk

$(TARGET): $(OBJS)
    $(CC)  -g -o $@ $^

rtk.o: rtk.c
    $(CC)  $(CFLAGS) -c rtk.c -o rtk.o

clean:
    rm -f $(TARGET) $(OBJS)
