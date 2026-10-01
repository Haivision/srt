TARGETS=srt_server srt_client

all: $(TARGETS)

%: %.c
	$(CC) $< `pkg-config --cflags --libs srt` -o `basename $< .c`

clean:
	rm -f $(TARGETS)
