#ifndef _WIN32
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

#ifndef _WIN32
void* sendfile(void*);
#else
DWORD WINAPI sendfile(LPVOID);
#endif

int main(int argc, char* argv[])
{
   if ((2 < argc) || ((2 == argc) && (0 == atoi(argv[1]))))
   {
      cout << "usage: sendfile [server_port]" << endl;
      return 0;
   }

   // Initialize the SRT library.
   srt_startup();

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

   SRTSOCKET serv = srt_create_socket();

   // SRT requires that third argument is always SOCK_DGRAM. The Stream API is set by an option,
   // although there's also lots of other options to be set, for which there's a convenience option,
   // SRTO_TRANSTYPE.
   SRT_TRANSTYPE tt = SRTT_FILE;
   srt_setsockopt(serv, 0, SRTO_TRANSTYPE, &tt, sizeof tt);

   // Windows UDP issue
   // For better performance, modify HKLM\System\CurrentControlSet\Services\Afd\Parameters\FastSendDatagramThreshold
#ifdef _WIN32
   int mss = 1052;
   srt_setsockopt(serv, 0, SRTO_MSS, &mss, sizeof(int));
#endif

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

   // Accept multiple client connections.
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

#ifndef _WIN32
      pthread_t filethread;
      pthread_create(&filethread, NULL, sendfile, new SRTSOCKET(fhandle));
      pthread_detach(filethread);
#else
      CreateThread(NULL, 0, sendfile, new SRTSOCKET(fhandle), 0, NULL);
#endif
   }

   srt_close(serv);

   // Signal to the SRT library to clean up all allocated sockets and resources.
   srt_cleanup();

   return 0;
}

#ifndef _WIN32
void* sendfile(void* usocket)
#else
DWORD WINAPI sendfile(LPVOID usocket)
#endif
{
   SRTSOCKET fhandle = *(SRTSOCKET*)usocket;
   delete (SRTSOCKET*)usocket;

   // Acquiring file name information from client.
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

   // Open the file only to know its size.
   fstream ifs(file, ios::in | ios::binary);
   ifs.seekg(0, ios::end);
   const int64_t size = ifs.tellg();
   ifs.close();

   // Send file size.
   if (SRT_ERROR == srt_send(fhandle, (char*)&size, sizeof(int64_t)))
   {
      cout << "send: " << srt_getlasterror_str() << endl;
      return 0;
   }

   SRT_TRACEBSTATS trace;
   srt_bstats(fhandle, &trace, true);

   // Send the file itself.
   int64_t offset = 0;
   if (SRT_ERROR == srt_sendfile(fhandle, file, &offset, size, SRT_DEFAULT_SENDFILE_BLOCK))
   {
      cout << "sendfile: " << srt_getlasterror_str() << endl;
      return 0;
   }

   srt_bstats(fhandle, &trace, true);
   cout << "speed = " << trace.mbpsSendRate << "Mbits/sec" << endl;
   const int64_t losspercent = 100 * trace.pktSndLossTotal / trace.pktSent;
   cout << "network loss = " << trace.pktSndLossTotal << "pkts (" << losspercent << "%)\n";

   srt_close(fhandle);

   #ifndef _WIN32
      return NULL;
   #else
      return 0;
   #endif
}
