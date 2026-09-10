#!/bin/bash -x

cd gitview_pr
mkdir -p _build && cd _build


cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_BONDING=1 -DENABLE_PKTINFO=1 -DENABLE_MAXREXMITBW=1 ..

