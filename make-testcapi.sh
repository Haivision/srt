#!/bin/bash

gcc -o testcapi apps/testcapi.c -I. -L. -L/usr/local/opt/openssl/lib/ -lhaicrypt -lhaisrt -lssl -lcrypto -lc++
gcc -o testcserver apps/testcserver.c -I. -L. -L/usr/local/opt/openssl/lib/ -lhaicrypt -lhaisrt -lssl -lcrypto -lc++
