CFLAGS  += -Wall -Wextra -pedantic -Wno-format -std=gnu11
LDFLAGS += -lpthread -ludev -lhidapi-hidraw

ifeq ($(DEBUG), 1)
	CFLAGS += -O0 -g
	LDFLAGS += -g
else
	CFLAGS += -O2
	LDFLAGS += -s
endif

TARGET = uinputdriver
OBJS = uinputdriver.o

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

all: $(TARGET)

clean:
	rm -f $(TARGET)
	rm -f $(OBJS)

.PHONY: all clean
