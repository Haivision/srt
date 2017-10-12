#ifndef WIN32
   #include <cstdlib>
   #include <netdb.h>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
#endif
#include <fstream>
#include <iostream>
#include <string>
#include <cstring>
#include <srt.h>

using namespace std;

#ifndef WIN32
void* sendfile(void*);
#else
DWORD WINAPI sendfile(LPVOID);
#endif

int main(int argc, char* argv[])
{
   //usage: sendfile [server_port]
   if ((2 < argc) || ((2 == argc) && (0 == atoi(argv[1]))))
   {
      cout << "usage: sendfile [server_port]" << endl;
      return 0;
   }

   // use this function to initialize the UDT library
   srt_startup();

   srt_setloglevel(logging::LogLevel::debug);

   addrinfo hints;
   addrinfo* res;

   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;

   string service("9000");
   if (2 == argc)
      service = argv[1];

   if (0 != getaddrinfo(NULL, service.c_str(), &hints, &res))
   {
      cout << "illegal port number or port is busy.\n" << endl;
      return 0;
   }

   SRTSOCKET serv = srt_socket(res->ai_family, res->ai_socktype, res->ai_protocol);

   // SRT requires that third argument is always SOCK_DGRAM. The Stream API is set by an option,
   // although there's also lots of other options to be set, for which there's a convenience option,
   // SRTO_TRANSTYPE.
   SRT_TRANSTYPE tt = SRTT_FILE;
   srt_setsockopt(serv, 0, SRTO_TRANSTYPE, &tt, sizeof tt);

   // Windows UDP issue
   // For better performance, modify HKLM\System\CurrentControlSet\Services\Afd\Parameters\FastSendDatagramThreshold
#ifdef WIN32
   int mss = 1052;
   srt_setsockopt(serv, 0, SRTO_MSS, &mss, sizeof(int));
#endif

   //int64_t maxbw = 5000000;
   //srt_setsockopt(serv, 0, SRTO_MAXBW, &maxbw, sizeof maxbw);

   if (SRT_ERROR == srt_bind(serv, res->ai_addr, res->ai_addrlen))
   {
      cout << "bind: " << srt_getlasterror_str() << endl;
      return 0;
   }

   freeaddrinfo(res);

   cout << "server is ready at port: " << service << endl;

   srt_listen(serv, 10);

   sockaddr_storage clientaddr;
   int addrlen = sizeof(clientaddr);

   SRTSOCKET fhandle;

   while (true)
   {
      if (SRT_INVALID_SOCK == (fhandle = srt_accept(serv, (sockaddr*)&clientaddr, &addrlen)))
      {
         cout << "accept: " << srt_getlasterror_str() << endl;
         return 0;
      }

      char clienthost[NI_MAXHOST];
      char clientservice[NI_MAXSERV];
      getnameinfo((sockaddr *)&clientaddr, addrlen, clienthost, sizeof(clienthost), clientservice, sizeof(clientservice), NI_NUMERICHOST|NI_NUMERICSERV);
      cout << "new connection: " << clienthost << ":" << clientservice << endl;

      #ifndef WIN32
         pthread_t filethread;
         pthread_create(&filethread, NULL, sendfile, new SRTSOCKET(fhandle));
         pthread_detach(filethread);
      #else
         CreateThread(NULL, 0, sendfile, new SRTSOCKET(fhandle), 0, NULL);
      #endif
   }

   srt_close(serv);

   // use this function to release the UDT library
   srt_cleanup();

   return 0;
}

#ifndef WIN32
void* sendfile(void* usocket)
#else
DWORD WINAPI sendfile(LPVOID usocket)
#endif
{
   SRTSOCKET fhandle = *(SRTSOCKET*)usocket;
   delete (SRTSOCKET*)usocket;

   // aquiring file name information from client
   char file[1024];
   int len;

   if (SRT_ERROR == srt_recv(fhandle, (char*)&len, sizeof(int)))
   {
      cout << "recv: " << srt_getlasterror_str() << endl;
      return 0;
   }

   if (SRT_ERROR == srt_recv(fhandle, file, len))
   {
      cout << "recv: " << srt_getlasterror_str() << endl;
      return 0;
   }
   file[len] = '\0';

   // open the file (only to check the size)
   fstream ifs(file, ios::in | ios::binary);

   ifs.seekg(0, ios::end);
   int64_t size = ifs.tellg();
   //ifs.seekg(0, ios::beg);
   ifs.close();

   // send file size information
   if (SRT_ERROR == srt_send(fhandle, (char*)&size, sizeof(int64_t)))
   {
      cout << "send: " << srt_getlasterror_str() << endl;
      return 0;
   }

   SRT_TRACEBSTATS trace;
   srt_bstats(fhandle, &trace, true);

   // send the file
   int64_t offset = 0;
   if (SRT_ERROR == srt_sendfile(fhandle, file, &offset, size, SRT_DEFAULT_SENDFILE_BLOCK))
   {
      cout << "sendfile: " << srt_getlasterror_str() << endl;
      return 0;
   }

   srt_bstats(fhandle, &trace, true);
   cout << "speed = " << trace.mbpsSendRate << "Mbits/sec" << endl;
   int losspercent = 100*trace.pktSndLossTotal/trace.pktSent;
   cout << "loss = " << trace.pktSndLossTotal << "pkt (" << losspercent << "%)\n";

   srt_close(fhandle);

   //ifs.close();

   #ifndef WIN32
      return NULL;
   #else
      return 0;
   #endif
}
