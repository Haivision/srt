#!/bin/bash

gcc -I./include -L./lib test-c-client.c -lsrt -o test-c-client -lssl -lcrypto -lpthread -lstdc++
gcc -I./include -L./lib test-c-server.c -lsrt -o test-c-server -lssl -lcrypto -lpthread -lstdc++
