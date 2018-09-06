#ifndef _WIN32
   #include <arpa/inet.h>
   #include <netdb.h>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
#endif
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <srt.h>

using namespace std;

int main(int argc, char* argv[])
{
   if ((argc != 5) || (0 == atoi(argv[2])))
   {
      cout << "usage: recvfile server_ip server_port remote_filename local_filename" << endl;
      return -1;
   }

   // use this function to initialize the UDT library
   srt_startup();

   srt_setloglevel(logging::LogLevel::debug);

   struct addrinfo hints, *peer;

   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;

   SRTSOCKET fhandle = srt_socket(hints.ai_family, hints.ai_socktype, hints.ai_protocol);
   // SRT requires that third argument is always SOCK_DGRAM. The Stream API is set by an option,
   // although there's also lots of other options to be set, for which there's a convenience option,
   // SRTO_TRANSTYPE.
   SRT_TRANSTYPE tt = SRTT_FILE;
   srt_setsockopt(fhandle, 0, SRTO_TRANSTYPE, &tt, sizeof tt);

   if (0 != getaddrinfo(argv[1], argv[2], &hints, &peer))
   {
      cout << "incorrect server/peer address. " << argv[1] << ":" << argv[2] << endl;
      return -1;
   }

   // connect to the server, implict bind
   if (SRT_ERROR == srt_connect(fhandle, peer->ai_addr, peer->ai_addrlen))
   {
      cout << "connect: " << srt_getlasterror_str() << endl;
      return -1;
   }

   freeaddrinfo(peer);


   // send name information of the requested file
   int len = strlen(argv[3]);

   if (SRT_ERROR == srt_send(fhandle, (char*)&len, sizeof(int)))
   {
      cout << "send: " << srt_getlasterror_str() << endl;
      return -1;
   }

   if (SRT_ERROR == srt_send(fhandle, argv[3], len))
   {
      cout << "send: " << srt_getlasterror_str() << endl;
      return -1;
   }

   // get size information
   int64_t size;

   if (SRT_ERROR == srt_recv(fhandle, (char*)&size, sizeof(int64_t)))
   {
      cout << "send: " << srt_getlasterror_str() << endl;
      return -1;
   }

   if (size < 0)
   {
      cout << "no such file " << argv[3] << " on the server\n";
      return -1;
   }

   // receive the file
   //fstream ofs(argv[4], ios::out | ios::binary | ios::trunc);
   int64_t recvsize; 
   int64_t offset = 0;

   SRT_TRACEBSTATS trace;
   srt_bstats(fhandle, &trace, true);

   if (SRT_ERROR == (recvsize = srt_recvfile(fhandle, argv[4], &offset, size, SRT_DEFAULT_RECVFILE_BLOCK)))
   {
      cout << "recvfile: " << srt_getlasterror_str() << endl;
      return -1;
   }

   srt_bstats(fhandle, &trace, true);

   cout << "speed = " << trace.mbpsRecvRate << "Mbits/sec" << endl;
   int losspercent = 100*trace.pktRcvLossTotal/trace.pktRecv;
   cout << "loss = " << trace.pktRcvLossTotal << "pkt (" << losspercent << "%)\n";

   srt_close(fhandle);

   //ofs.close();

   // use this function to release the UDT library
   srt_cleanup();

   return 0;
}
