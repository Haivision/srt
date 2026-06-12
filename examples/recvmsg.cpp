#ifndef WIN32
   #include <cstdlib>
   #include <netdb.h>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
#endif
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <cassert>

#include <srt.h>
#include <netinet_any.h>

using namespace std;

string ShowChar(char in)
{
    if (in >= 32 && in < 127)
        return string(1, in);

    ostringstream os;
    os << "<" << hex << uppercase << int(in) << ">";
    return os.str();
}

string CreateFilename(string fmt, int ord)
{
    ostringstream os;

    size_t pos = fmt.find('%');
    if (pos == string::npos)
        os << fmt << ord << ".out";
    else
    {
        os << fmt.substr(0, pos) << ord << fmt.substr(pos+1);
    }
    return os.str();
}

int main(int argc, char* argv[])
{
   string service("9000");
   if (argc > 1)
      service = argv[1];

   if (service == "--help")
   {
      cout << "usage: recvmsg [server_port] [filepattern]" << endl;
      return 0;
   }

   addrinfo hints;
   addrinfo* res;

   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;

   if (0 != getaddrinfo(NULL, service.c_str(), &hints, &res))
   {
      cout << "illegal port number or port is busy.\n" << endl;
      return 1;
   }

   string outfileform;
   if (argc > 2)
   {
       outfileform = argv[2];
   }

   // use this function to initialize the UDT library
   srt_startup();

   srt_setloglevel(srt_logging::LogLevel::debug);

   SRTSOCKET sfd = srt_create_socket();
   if (SRT_INVALID_SOCK == sfd)
   {
      cout << "srt_socket: " << srt_getlasterror_str() << endl;
      return 1;
   }

   int file_mode = SRTT_FILE;
   if (SRT_ERROR == srt_setsockflag(sfd, SRTO_TRANSTYPE, &file_mode, sizeof file_mode))
   {
      cout << "srt_setsockopt: " << srt_getlasterror_str() << endl;
      return 1;
   }

   bool message_mode = true;
   if (SRT_ERROR == srt_setsockflag(sfd, SRTO_MESSAGEAPI, &message_mode, sizeof message_mode))
   {
      cout << "srt_setsockopt: " << srt_getlasterror_str() << endl;
      return 1;
   }

   if (SRT_ERROR == srt_bind(sfd, res->ai_addr, res->ai_addrlen))
   {
      cout << "srt_bind: " << srt_getlasterror_str() << endl;
      return 0;
   }

   freeaddrinfo(res);

   cout << "server is ready at port: " << service << endl;

   if (SRT_ERROR == srt_listen(sfd, 10))
   {
      cout << "srt_listen: " << srt_getlasterror_str() << endl;
      return 1;
   }

   char data[4096];

   srt::sockaddr_any remote;

   int afd = srt_accept(sfd, remote.get(), &remote.len);

   if (afd == SRT_INVALID_SOCK)
   {
       cout << "srt_accept: " << srt_getlasterror_str() << endl;
       return 1;
   }

   cout << "Connection from " << remote.str() << " established\n";

   bool save_to_files = true;

   if (outfileform != "")
       save_to_files = true;

    int ordinal = 1;

   // the event loop
   while (true)
   {
       SRT_SOCKSTATUS status = srt_getsockstate(afd);
       if ((status == SRTS_BROKEN) ||
               (status == SRTS_NONEXIST) ||
               (status == SRTS_CLOSED))
       {
           cout << "source disconnected. status=" << status << endl;
           srt_close(afd);
           break;
       }

       int ret = srt_recvmsg(afd, data, sizeof(data));
       if (ret == SRT_ERROR)
       {
           cout << "srt_recvmsg: " << srt_getlasterror_str() << endl;
           break;
       }
       if (ret == 0)
       {
           cout << "EOT\n";
           break;
       }

       if (ret < 5)
       {
           cout << "WRONG MESSAGE SYNTAX\n";
           break;
       }

       if (save_to_files)
       {
           string fname = CreateFilename(outfileform, ordinal++);
           ofstream ofile(fname);

           if (!ofile.good())
           {
               cout << "ERROR: can't create file: " << fname << " - skipping message\n";
               continue;
           }

           ofile.write(data, ret);
           ofile.close();
           cout << "Written " << ret << " bytes of message to " << fname << endl;
       }
       else
       {
           union
           {
               char chars[4];
               int32_t intval;
           } first4;

           copy(data, data + 4, first4.chars);

           cout << "[" << ret << "B " << ntohl(first4.intval) << "] ";
           for (int i = 4; i < ret; ++i)
               cout << ShowChar(data[i]);
           cout << endl;
       }
   }

   srt_close(afd);
   srt_close(sfd);

   // use this function to release the UDT library
   srt_cleanup();

   return 0;
}

// Local Variables:
// c-file-style: "ellemtel"
// c-basic-offset: 3
// compile-command: "g++ -Wall -O2 -std=c++11 -I.. -I../srtcore -o recvlive recvlive.cpp -L.. -lsrt -lpthread -L/usr/local/opt/openssl/lib -lssl -lcrypto"
// End:
