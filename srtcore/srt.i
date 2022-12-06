/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Lewis Kirkaldie - Cinegy GmbH
 *****************************************************************************/

/*
Automatic generatation of bindings via SWIG (http://www.swig.org)
Install swig via the following (or use instructions from the link above):
   sudo apt install swig / nuget install swigwintools    
Generate the bindings using:
   mkdir srtcore/bindings/csharp -p
   swig -v -csharp -namespace SrtSharp -outdir ./srtcore/bindings/csharp/ ./srtcore/srt.i
You can now reference the SrtSharp lib in your .Net Core projects.  Ensure the srtlib.so (or srt.dll / srt_swig_csharp.dll) is in the binary path of your .NetCore project.
*/

%module srt
%{
   #include "srt.h"
%}

%include <arrays_csharp.i>

//push anything with an argument name 'buf' back to being an array (e.g. csharp defaults this type to string, which is not ideal here)
%apply unsigned char INOUT[]  { char* buf}

/// 
/// C# related configration section, customizing binding for this language  
/// 

// add top-level code to module file, which allows C# bindings of specific objects to be injected for easier use in C# 

%pragma(csharp) moduleimports=%{ 
using System;
using System.Net;
using System.Runtime.InteropServices;

// sockaddr_in layout in C# - for easier creation of socket object from C# code
[StructLayout(LayoutKind.Sequential)]
public struct sockaddr_in
{
   public short sin_family;
   public ushort sin_port;
   public uint sin_addr;
   public long sin_zero;
};

public static class SocketHelper{
   public static SWIGTYPE_p_sockaddr CreateSocketAddress(string address, int port){
            var destination = IPAddress.Parse(address);
            
      var sin = new sockaddr_in()
      {
         sin_family = srt.AF_INET,
         sin_port = (ushort)IPAddress.HostToNetworkOrder((short)port),
#pragma warning disable 618
         sin_addr = (uint)destination.Address,
#pragma warning restore 618
         sin_zero = 0
      };

      var hnd = GCHandle.Alloc(sin, GCHandleType.Pinned);
      
      var socketAddress = new SWIGTYPE_p_sockaddr(hnd.AddrOfPinnedObject(), false);
      
      hnd.Free();

      return socketAddress;
   }
}
%}

/// Rebind objects from the default mappings for types and objects that are optimized for C#

//enums in C# are int by default, this override pushes this enum to the require uint format
%typemap(csbase) SRT_EPOLL_OPT "uint"

//the SRT_ERRNO enum references itself another enum - we must import this other enum into the class file for resolution
%typemap(csimports) SRT_ERRNO %{

   using static CodeMajor;
   using static CodeMinor;

%}

SWIG_CSBODY_PROXY(public, public, SWIGTYPE)
SWIG_CSBODY_TYPEWRAPPER(public, public, public, SWIGTYPE)

%typemap(ctype) SWIGTYPE "const struct sockaddr*"
%typemap(imtype, out="global::System.IntPtr") SWIGTYPE "global::System.Runtime.InteropServices.HandleRef"
%typemap(cstype) const struct sockaddr* "SWIGTYPE_p_sockaddr"

// General interface definition of wrapper - due to above typemaps and code, we can now just reference the main srt.h file

const short AF_INET = 2;


%include "srt.h";