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
#include <vector>
#include <srt.h>
#include <assert.h>             // assert

using namespace std;

int main(int argc, char* argv[])
{
   vector<string> args (argv + 1, argv + argc);

   string service;

   // Very simple options parsing:
   // 1. Walk through the parameters first;
   //    any parameter not with -dash is a free argument.
   // 2. Since the first -arg there are -opt val pairs.
   // 3. Remember the position of the first option that splits
   //    between free arguments and options - `optpos`
   // 4. Default arguments are allowed and recognized by
   //    presence in order.

   // Current usage:
   // recvlive [listen-port] [options...]
   //
   // Current options:
   // -echo quiet: Do not print reports from the activities during transmission

   size_t optpos = 0;
   for (size_t i = 0; i < args.size(); ++i)
   {
       if (args[i][0] == '-')
       {
           optpos = i;
           break;
       }
   }

   bool error = false;
   bool opt_quiet = false;

   // Simplified option syntax, check parity
   if (args.size() - optpos % 2)
   {
       cout << "Options should be specified in pairs as -option value\n";
       error = true;
   }
   else
   {
       if (optpos > 0)
       {
           service = args[0];
       }
       else if (optpos > 1)
       {
           cout << "Too many arguments\n";
           error = true;
       }

       if (service != "" && atoi(service.c_str()) == 0)
       {
           cout << "Invalid port specification: " << service << endl;
           error = true;
       }
       else
       {
           service = "9000";
       }

       for (size_t i = optpos; i + 1 < args.size(); i += 2)
       {
           if (args[i] == "-echo")
           {
               string val = args[i+1];
               if (val == "quiet")
                   opt_quiet = true;
           }
       }
   }

#define IFLOUD(arg) if (!opt_quiet) { arg ; }

   if (error)
   {
       // usage: recvlive [server_port] [options...]
      cout << "usage: recvlive [server_port] [-option value...]" << endl;
      return 1;
   }

   // use this function to initialize the UDT library
   srt_startup();

   srt_setloglevel(LOG_DEBUG);

   addrinfo hints;
   addrinfo* res;

   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;


   if (0 != getaddrinfo(NULL, service.c_str(), &hints, &res))
   {
      cout << "illegal port number or port is busy.\n";
      return 0;
   }

   SRTSOCKET sfd = srt_create_socket();
   if (SRT_INVALID_SOCK == sfd)
   {
      cout << "srt_socket: " << srt_getlasterror_str() << endl;
      return 0;
   }

   // SRT requires that third argument is always SOCK_DGRAM. The Stream API is set by an option,
   // although there's also lots of other options to be set, for which there's a convenience option,
   // SRTO_TRANSTYPE.
   // SRT_TRANSTYPE tt = SRTT_LIVE;
   // if (SRT_ERROR == srt_setsockopt(sfd, 0, SRTO_TRANSTYPE, &tt, sizeof tt))
   // {
   //    cout << "srt_setsockopt: " << srt_getlasterror_str() << endl;
   //    return 0;
   // }

   bool no = false;
   if (SRT_ERROR == srt_setsockopt(sfd, 0, SRTO_RCVSYN, &no, sizeof no))
   {
      cout << "srt_setsockopt: " << srt_getlasterror_str() << endl;
      return 0;
   }

   // Test the deprecated option feature here:
   //srt_setsockopt(sfd, 0, SRTO_STRICTENC, &no, sizeof no);

   // Windows UDP issue
   // For better performance, modify HKLM\System\CurrentControlSet\Services\Afd\Parameters\FastSendDatagramThreshold
#ifdef WIN32
   int mss = 1052;
   srt_setsockopt(sfd, 0, SRTO_MSS, &mss, sizeof(int));
#endif

   // int64_t maxbw = 5000000;
   // srt_setsockopt(sfd, 0, SRTO_MAXBW, &maxbw, sizeof maxbw);

   if (SRT_ERROR == srt_bind(sfd, res->ai_addr, res->ai_addrlen))
   {
      cout << "srt_bind: " << srt_getlasterror_str() << endl;
      return 0;
   }

   freeaddrinfo(res);

   IFLOUD(cout << "server is ready at port: " << service << endl);

   if (SRT_ERROR == srt_listen(sfd, 10))
   {
      cout << "srt_listen: " << srt_getlasterror_str() << endl;
      return 0;
   }

   int epid = srt_epoll_create();
   if (epid < 0)
   {
      cout << "srt_epoll_create: " << srt_getlasterror_str() << endl;
      return 0;
   }

   int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
   if (SRT_ERROR == srt_epoll_add_usock(epid, sfd, &events))
   {
      cout << "srt_epoll_add_usock: " << srt_getlasterror_str() << endl;
      return 0;
   }

   const int srtrfdslenmax = 100;
   SRTSOCKET srtrfds[srtrfdslenmax];
   char data[1500];

   // the event loop
   while (true)
   {
      int srtrfdslen = srtrfdslenmax;
      int n = srt_epoll_wait(epid, &srtrfds[0], &srtrfdslen, 0, 0, 100, 0, 0, 0, 0);
      assert(n <= srtrfdslen);
      for (int i = 0; i < n; i++)
      {
         SRTSOCKET s = srtrfds[i];
         SRT_SOCKSTATUS status = srt_getsockstate(s);
         if ((status == SRTS_BROKEN) ||
             (status == SRTS_NONEXIST) ||
             (status == SRTS_CLOSED))
         {
            cout << "source disconnected. status=" << status << endl;
            srt_close(s);
            continue;
         }
         else if (s == sfd)
         {
            assert(status == SRTS_LISTENING);

            SRTSOCKET fhandle;
            sockaddr_storage clientaddr;
            int addrlen = sizeof(clientaddr);

            fhandle = srt_accept(sfd, (sockaddr*)&clientaddr, &addrlen);
            if (SRT_INVALID_SOCK == fhandle)
            {
               cout << "srt_accept: " << srt_getlasterror_str() << endl;
               return 0;
            }

            char clienthost[NI_MAXHOST];
            char clientservice[NI_MAXSERV];
            getnameinfo((sockaddr *)&clientaddr, addrlen,
                        clienthost, sizeof(clienthost),
                        clientservice, sizeof(clientservice), NI_NUMERICHOST|NI_NUMERICSERV);
            cout << "new connection: " << clienthost << ":" << clientservice << endl;

            int events = SRT_EPOLL_IN | SRT_EPOLL_ERR;
            if (SRT_ERROR == srt_epoll_add_usock(epid, fhandle, &events))
            {
               IFLOUD(cout << "srt_epoll_add_usock: " << srt_getlasterror_str() << endl);
               return 0;
            }
         }
         else
         {
            while (true)
            {
               int ret = srt_recvmsg(s, data, sizeof(data));
               if (SRT_ERROR == ret)
               {
                  // EAGAIN for SRT READING
                  if (SRT_EASYNCRCV != srt_getlasterror(NULL))
                  {
                     cout << "srt_recvmsg: " << srt_getlasterror_str() << endl;
                     return 0;
                  }
                  break;
               }
               // cout << ret << " bytes received" << endl;
            }
         }
      }
   }

   srt_close(sfd);

   srt_epoll_release(epid);

   // use this function to release the UDT library
   srt_cleanup();

   return 0;
}

// Local Variables:
// c-file-style: "ellemtel"
// c-basic-offset: 3
// compile-command: "g++ -Wall -O2 -std=c++11 -I.. -I../srtcore -o recvlive recvlive.cpp -L.. -lsrt -lpthread -L/usr/local/opt/openssl/lib -lssl -lcrypto"
// End:
