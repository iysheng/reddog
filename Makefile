CROSS_COMPILE=
TARGET=reddog_client reddog_server
LDFLAGS+=-lpthread
CFLAGS+=-g
#$(info TRAGTE=$(TARGET))

all:$(TARGET)

$(TARGET):$(addsuffix .o,$(TARGET))

%:%.o
	${CROSS_COMPILE}gcc $(LDFLAGS) $< -o $@
	cp -fv $@ ../test_reddog
%.o:%.c
	${CROSS_COMPILE}gcc -c $< -o $@

.PHONY:clean
clean:
	rm -fr $(TARGET) $(addsuffix .o,$(TARGET))
