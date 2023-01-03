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
#include <string>
#include <sstream>
#include <vector>
#include <iterator>
#include <algorithm>
#include <srt.h>

using namespace std;

string ShowChar(char in)
{
    if (in >= 32 && in < 127)
        return string(1, in);

    ostringstream os;
    os << "<" << hex << uppercase << int(in) << ">";
    return os.str();
}

int main(int argc, char* argv[])
{
   if ((argc != 4) || (0 == atoi(argv[2])))
   {
      cout << "usage: sendmsg server_ip server_port source_filename" << endl;
      return -1;
   }

   // Use this function to initialize the UDT library
   srt_startup();

   srt_setloglevel(srt_logging::LogLevel::debug);

   struct addrinfo hints, *peer;

   memset(&hints, 0, sizeof(struct addrinfo));
   hints.ai_flags = AI_PASSIVE;
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_DGRAM;

   SRTSOCKET fhandle = srt_create_socket();
   // SRT requires that third argument is always SOCK_DGRAM. The Stream API is set by an option,
   // although there's also lots of other options to be set, for which there's a convenience option,
   // SRTO_TRANSTYPE.
   SRT_TRANSTYPE tt = SRTT_FILE;
   srt_setsockopt(fhandle, 0, SRTO_TRANSTYPE, &tt, sizeof tt);

   bool message_mode = true;
   srt_setsockopt(fhandle, 0, SRTO_MESSAGEAPI, &message_mode, sizeof message_mode);

   if (0 != getaddrinfo(argv[1], argv[2], &hints, &peer))
   {
      cout << "incorrect server/peer address. " << argv[1] << ":" << argv[2] << endl;
      return -1;
   }

   // Connect to the server, implicit bind.
   if (SRT_ERROR == srt_connect(fhandle, peer->ai_addr, peer->ai_addrlen))
   {
       int rej = srt_getrejectreason(fhandle);
       cout << "connect: " << srt_getlasterror_str() << ":" << srt_rejectreason_str(rej) << endl;
       return -1;
   }

   freeaddrinfo(peer);

   string source_fname = argv[3];

   bool use_filelist = false;
   if (source_fname[0] == '+')
   {
       use_filelist = true;
       source_fname = source_fname.substr(1);
   }

   istream* pin;
   ifstream fin;
   if (source_fname == "-")
   {
       pin = &cin;
   }
   else
   {
       fin.open(source_fname.c_str());
       pin = &fin;
   }

   // The syntax is:
   //
   // 1. If the first character is +, it's followed by TTL in milliseconds.
   // 2. Otherwise the first number is the ID, followed by a space, to be filled in first 4 bytes.
   // 3. Rest of the characters, up to the end of line, should be put into a solid block and sent at once.

   int status = 0;

   int ordinal = 1;
   int lpos = 0;

   for (;;)
   {
       string line;
       int32_t id = 0;
       int ttl = -1;

       getline(*pin, (line));
       if (pin->eof())
           break;

       // Interpret
       if (line.size() < 2)
           continue;

       if (use_filelist)
       {
           // The line should contain [+TTL] FILENAME
           char fname[1024] = "";
           if (line[0] == '+')
           {
               int nparsed = sscanf(line.c_str(), "+%d %n%1000s", &ttl, &lpos, fname);
               if (nparsed != 2)
               {
                   cout << "ERROR: syntax error in input (" << nparsed << " parsed pos=" << lpos << ")\n";
                   status = SRT_ERROR;
                   break;
               }
               line = fname;
           }

           ifstream ifile (line);

           id = ordinal;
           ++ordinal;

           if (!ifile.good())
           {
               cout << "ERROR: file '" << line << "' cannot be read, skipping\n";
               continue;
           }

           line = string(istreambuf_iterator<char>(ifile), istreambuf_iterator<char>());
       }
       else
       {
           int lpos = 0;

           int nparsed = 0;
           if (line[0] == '+')
           {
               nparsed = sscanf(line.c_str(), "+%d %d %n%*s", &ttl, &id, &lpos);
               if (nparsed != 2)
               {
                   cout << "ERROR: syntax error in input (" << nparsed << " parsed pos=" << lpos << ")\n";
                   status = SRT_ERROR;
                   break;
               }
           }
           else
           {
               nparsed = sscanf(line.c_str(), "%d %n%*s", &id, &lpos);
               if (nparsed != 1)
               {
                   cout << "ERROR: syntax error in input (" << nparsed << " parsed pos=" << lpos << ")\n";
                   status = SRT_ERROR;
                   break;
               }
           }
       }

       union
       {
           char chars[4];
           int32_t intval;
       } first4;
       first4.intval = htonl(id);

       vector<char> input;
       copy(first4.chars, first4.chars+4, back_inserter(input));
       copy(line.begin() + lpos, line.end(), back_inserter(input));

       // CHECK CODE
       // cout << "WILL SEND: TTL=" << ttl << " ";
       // transform(input.begin(), input.end(),
       //         ostream_iterator<string>(cout), ShowChar);
       // cout << endl;

       int nsnd = srt_sendmsg(fhandle, &input[0], input.size(), ttl, false);
       if (nsnd == SRT_ERROR)
       {
           cout << "SRT ERROR: " << srt_getlasterror_str() << endl;
               status = SRT_ERROR;
           break;
       }
   }

   srt_close(fhandle);

   // Signal to the SRT library to clean up all allocated sockets and resources.
   srt_cleanup();

   return status;
}
