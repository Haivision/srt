#!/bin/bash

# Only as example!
gcc -I./include -L./lib test-c-client.c -lsrt -o test-c-client -lssl -lcrypto -lpthread -lstdc++
gcc -I./include -L./lib test-c-server.c -lsrt -o test-c-server -lssl -lcrypto -lpthread -lstdc++

# execute server: ./test-c-server 0.0.0.0 15012
# execyte client: ./test-c-client 127.0.0.1 15012

