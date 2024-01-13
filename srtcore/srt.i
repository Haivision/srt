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
   Volodymyr Shkolka - Cinegy GmbH
 *****************************************************************************/

/*
Automatic generatation of bindings via SWIG (http://www.swig.org)
Install swig via the following (or use instructions from the link above):
   sudo apt install swig / nuget install swigwintools / download latest from swig.org   
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
%include <stdint.i>
%include <typemaps.i>

#if defined(SWIGCSHARP)
// Push anything with an argument name 'buf' back to being an array (e.g. csharp defaults this type to string, which is not ideal here)
%apply unsigned char INOUT[]  { 
    char* buf,
    const char* buf
    }

%apply int INOUT[]  { const SRTSOCKET listeners[]}
%apply int INOUT[]  { const int* fara}

// ---- Marshal Pointers to IntPtr
%apply void *VOID_INT_PTR { 
    void *,
    SRT_LOG_HANDLER_FN *,       // Delegate
    srt_listen_callback_fn*,    // Delegate
    srt_connect_callback_fn*,   // Delegate
    int *,
    byte *,
    size_t *,
    int64_t *,
    SRT_SOCKOPT_CONFIG*
    }
    
// ---- REF int mapping
%typemap(cstype) int * "ref int"
%typemap(csin,
         pre="var pin_$csinput = GCHandle.Alloc($csinput, GCHandleType.Pinned);",
         post="
            $csinput = (int)pin_$csinput.Target;
            pin_$csinput.Free();"
        ) int *
         "pin_$csinput.AddrOfPinnedObject()"

// ---- REF size_t mapping
%typemap(cstype) size_t * "ref ulong"
%typemap(csin,
         pre="var pin_$csinput = GCHandle.Alloc($csinput, GCHandleType.Pinned);",
         post="
            $csinput = (ulong)pin_$csinput.Target;
            pin_$csinput.Free();"
        ) size_t *
         "pin_$csinput.AddrOfPinnedObject()"

// ---- OVERRIDE SIGNATURE (struct sockaddr* addr, int* addrlen)
%typemap(in) (struct sockaddr*, int*) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = (struct sockaddr *)args->Arg1;
    $2 = (int *)args->Arg2;
}
%typemap(cstype) (struct sockaddr*, int*) "out IPEndPoint"
%typemap(imtype) (struct sockaddr*, int*) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            var sock_$csinput = new SocketAddress(AddressFamily.InterNetwork);
            var content_$csinput = sock_$csinput.GetInternalBuffer(); 
            int content_len_$csinput = sock_$csinput.Size; 
            var pin_content_$csinput = GCHandle.Alloc(content_$csinput, GCHandleType.Pinned);
            var pin_content_len_$csinput = GCHandle.Alloc(content_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_content_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_content_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="    
            sock_$csinput.SetInternalSize(content_len_$csinput);
            $csinput = sock_$csinput.ToIPEndPoint();
            pin_content_$csinput.Free();
            pin_content_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (struct sockaddr*, int*)
         "input_$csinput"

// Apply mappings for different functions with the same signature (struct sockaddr*, int*)
%apply (struct sockaddr*, int*) 
{ 
    (struct sockaddr* addr, int* addrlen),
    (struct sockaddr* name, int* namelen)
}

// ---- OVERRIDE SIGNATURE (const struct sockaddr addr, int addrlen)
%typemap(in) (const struct sockaddr*, int) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = (struct sockaddr *)args->Arg1;
    $2 = *args->Arg2;
}
%typemap(cstype) (const struct sockaddr*, int) "IPEndPoint"
%typemap(imtype) (const struct sockaddr*, int) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            var sock_$csinput = $csinput.Serialize();
            var content_$csinput = sock_$csinput.GetInternalBuffer(); 
            int content_len_$csinput = sock_$csinput.Size; 
            var pin_content_$csinput = GCHandle.Alloc(content_$csinput, GCHandleType.Pinned);
            var pin_content_len_$csinput = GCHandle.Alloc(content_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_content_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_content_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            pin_content_$csinput.Free();
            pin_content_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (const struct sockaddr*, int)
         "input_$csinput"

// Apply mappings for different functions with the same signature (const struct sockaddr*, int)
%apply (const struct sockaddr*, int) 
{ 
    (const struct sockaddr* name, int namelen),
    (const struct sockaddr* local_name, int local_namelen),
    (const struct sockaddr* remote_name, int remote_namelen),
    (const struct sockaddr* target, int len),
    (const struct sockaddr* adr, int namelen)
}

// ---- OVERRIDE SIGNATURE (const struct sockaddr* /*nullable*/)
%typemap(cstype) (const struct sockaddr*) "IPEndPoint"
%typemap(imtype) (const struct sockaddr*) "global::System.IntPtr"
%typemap(csin,
         pre="
            var input_$csinput = IntPtr.Zero;
            GCHandle? pin_content_$csinput = null;
            if($csinput != null)
            {
                var sock_$csinput = $csinput.Serialize();
                var content_$csinput = sock_$csinput.GetInternalBuffer(); 
                pin_content_$csinput = GCHandle.Alloc(content_$csinput, GCHandleType.Pinned);
                input_$csinput = pin_content_$csinput.Value.AddrOfPinnedObject();
            }",
         post="pin_content_$csinput?.Free();"
        ) (const struct sockaddr*)
         "input_$csinput"

// ---- REF int64_t mapping
%typemap(cstype) (int64_t *) "ref long"
%typemap(csin,
         pre="var pin_$csinput = GCHandle.Alloc($csinput, GCHandleType.Pinned);",
         post="
            $csinput = (long)pin_$csinput.Target;
            pin_$csinput.Free();"
        ) int64_t *
         "pin_$csinput.AddrOfPinnedObject()"

// ---- SRT_LOG_HANDLER_FN to Delegate mapping
%typemap(cstype) (SRT_LOG_HANDLER_FN *) "SrtLogHandlerDelegate"
%typemap(csin,
         pre="
            GCKeeper.Keep(nameof(SrtLogHandlerDelegate), $csinput);
            var delegatePtr_$csinput = Marshal.GetFunctionPointerForDelegate($csinput);"
        ) SRT_LOG_HANDLER_FN*
         "delegatePtr_$csinput"

// ---- srt_listen_callback_fn to Delegate mapping
%typemap(cstype) (srt_listen_callback_fn *) "SrtListenCallbackDelegate"
%typemap(csin,
         pre="
            GCKeeper.Keep($\"{nameof(SrtListenCallbackDelegate)}-{lsn}\", hook_fn);
            var delegatePtr_$csinput = Marshal.GetFunctionPointerForDelegate($csinput);"
        ) srt_listen_callback_fn*
         "delegatePtr_$csinput"
         
%typemap(cstype) (srt_connect_callback_fn *) "SrtConnectCallbackDelegate"
%typemap(csin,
         pre="
            GCKeeper.Keep($\"{nameof(SrtConnectCallbackDelegate)}-{clr}\", hook_fn);
            var delegatePtr_$csinput = Marshal.GetFunctionPointerForDelegate($csinput);"
        ) srt_connect_callback_fn*
         "delegatePtr_$csinput"

// ---- Nullable SRT_EPOLL_OPT handling
%typemap(cstype) (const int* events) "SRT_EPOLL_OPT?"
%typemap(csin,
         pre="    
            GCHandle? pin_$csinput = null;
            IntPtr pinAddr_$csinput = IntPtr.Zero;
            if($csinput.HasValue)
            {
                uint opt_$csinput = (uint)$csinput.Value; 
                pin_$csinput = GCHandle.Alloc(opt_$csinput, GCHandleType.Pinned);
                pinAddr_$csinput = pin_$csinput.Value.AddrOfPinnedObject();
            }
         ",
         post="pin_$csinput?.Free();"
        ) const int* events
         "pinAddr_$csinput"

// --- Map srt_getlasterror return type to SRT_ERRNO
%typemap(csbase) SRT_ERRNO "int"
%typemap(cstype) int srt_getlasterror "SRT_ERRNO"
%typemap(csout) int srt_getlasterror { return (SRT_ERRNO)$imcall; }

// ------------------------------------------------------------
// ------------------------- SRTSOCKET ------------------------
// ------------------------------------------------------------

// --- Map all SRTSOCKET to artificial SRTSOCKET structure 
%typemap(cstype) SRTSOCKET "SRTSOCKET"

// --- Map srt_create_socket return type to SRTSOCKET
%typemap(cstype) int srt_create_socket "SRTSOCKET"

// --- Map srt_accept return type to SRTSOCKET
%typemap(cstype) int srt_accept "SRTSOCKET"

// ------------------------------------------------------------
// ------------------------- SYSSOCKET ------------------------
// ------------------------------------------------------------

// --- Map all SYSSOCKET to artificial SYSSOCKET structure 
%typemap(cstype) SYSSOCKET "SYSSOCKET"

// ------------------------------------------------------------
// ------------------------- EPOLL ----------------------------
// ------------------------------------------------------------
// --- Map all int eid to artificial EPOLL structure 
%typemap(cstype) int eid "EPOLL"

// --- Map srt_epoll_create return type to EPOLL
%typemap(cstype) int srt_epoll_create "EPOLL"

// srt_epoll_wait method parameters transformations
%typemap(in) (SRTSOCKET* , int*) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = (int*)args->Arg2;
}
%typemap(cstype) (SRTSOCKET* , int*) "SRTSOCKET[]"
%typemap(imtype) (SRTSOCKET* , int*) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            GCHandle? pin_array_$csinput = null;
            GCHandle? pin_array_len_$csinput = null;
            if($csinput != null)
            {
                Array.Clear($csinput, 0, $csinput.Length);
                var array_len_$csinput = $csinput.Length; 
                pin_array_$csinput = GCHandle.Alloc($csinput, GCHandleType.Pinned);
                pin_array_len_$csinput = GCHandle.Alloc(array_len_$csinput, GCHandleType.Pinned);
            }
            
            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_array_$csinput?.AddrOfPinnedObject() ?? IntPtr.Zero,
                Arg2 = pin_array_len_$csinput?.AddrOfPinnedObject() ?? IntPtr.Zero
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            pin_array_$csinput?.Free();
            pin_array_len_$csinput?.Free();
            transitive_$csinput.Dispose();"
        ) (SRTSOCKET* , int*)
         "input_$csinput"

// Apply mappings for different functions with the same signature (SRTSOCKET* , int*)
%apply (SRTSOCKET* , int*) 
{ 
    (SRTSOCKET* readfds, int* rnum),
    (SRTSOCKET* writefds, int* wnum)
}

%typemap(in) (SYSSOCKET* , int*) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = (int*)args->Arg2;
}
%typemap(cstype) (SYSSOCKET* , int*) "SYSSOCKET[]"
%typemap(imtype) (SYSSOCKET* , int*) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            GCHandle? pin_array_$csinput = null;
            GCHandle? pin_array_len_$csinput = null;
            if($csinput != null)
            {
                Array.Clear($csinput, 0, $csinput.Length);
                var array_len_$csinput = $csinput.Length; 
                pin_array_$csinput = GCHandle.Alloc($csinput, GCHandleType.Pinned);
                pin_array_len_$csinput = GCHandle.Alloc(array_len_$csinput, GCHandleType.Pinned);
            }
            
            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_array_$csinput?.AddrOfPinnedObject() ?? IntPtr.Zero,
                Arg2 = pin_array_len_$csinput?.AddrOfPinnedObject() ?? IntPtr.Zero
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            pin_array_$csinput?.Free();
            pin_array_len_$csinput?.Free();
            transitive_$csinput.Dispose();"
        ) (SYSSOCKET* , int*)
         "input_$csinput"

// Apply mappings for different functions with the same signature (SYSSOCKET* , int*)
%apply (SYSSOCKET* , int*) 
{ 
    (SYSSOCKET* lrfds, int* lrnum), 
    (SYSSOCKET* lwfds, int* lwnum)
}

// srt_epoll_uwait method parameters transformations
/*
%inline
{ 
    int srt_getepoll_event_size()
    { 
        return sizeof(SRT_EPOLL_EVENT);
    }
}
*/

//%ignore srt_epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
%ignore SRT_EPOLL_EVENT_STR;
//int srt_epoll_uwait(int eid, SRT_EPOLL_EVENT1* fdsSet, int fdsSize, int64_t msTimeOut);

%typemap(in) (SRT_EPOLL_EVENT* , int) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = (int)args->Arg2;
}
%typemap(cstype) (SRT_EPOLL_EVENT* , int) "SRT_EPOLL_EVENT[]"
%typemap(imtype) (SRT_EPOLL_EVENT* , int) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            GCHandle? pin_array_$csinput = null;
            int array_len_$csinput = 0;
            if($csinput != null)
            {
                array_len_$csinput = $csinput.Length; 
                pin_array_$csinput = GCHandle.Alloc($csinput, GCHandleType.Pinned);
            }
            
            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_array_$csinput?.AddrOfPinnedObject() ?? IntPtr.Zero,
                Arg2 = new IntPtr(array_len_$csinput)
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            pin_array_$csinput?.Free();
            transitive_$csinput.Dispose();"
        ) (SRT_EPOLL_EVENT* , int)
         "input_$csinput"

// Apply mappings for different functions with the same signature (SRT_EPOLL_EVENT* , int)
%apply (SRT_EPOLL_EVENT* , int) 
{ 
    (SRT_EPOLL_EVENT* fdsSet, int fdsSize)
}

// Rework srt_epoll_set flags type
%inline{ typedef enum SRT_EPOLL_FLAGS SRT_EPOLL_FLAGS_TRANSIENT; }
%ignore srt_epoll_set(int eid, int32_t flags);
int32_t srt_epoll_set(int eid, SRT_EPOLL_FLAGS_TRANSIENT flags);

// ------------------------------------------------------------
// ------------------------- LOGGING --------------------------
// ------------------------------------------------------------
// --- Map srt_setlogflags member to artificial LogFlag structure 
// --- structure itself defined in csharp module imports below
// Type mappings for IM wrapper INT -> managed struct
%inline{ typedef int LogFlag; }

// Force C# to use LogFlag structure instead original INT
%typemap(cstype) LogFlag "LogFlag"

// Redefine C# method representation
%ignore srt_setlogflags(int flags);
void srt_setlogflags(LogFlag flags);

// Forward constants from C side to C# srt module
const LogFlag SRT_LOGF_DISABLE_TIME = SRT_LOGF_DISABLE_TIME;
const LogFlag SRT_LOGF_DISABLE_THREADNAME = SRT_LOGF_DISABLE_THREADNAME;
const LogFlag SRT_LOGF_DISABLE_SEVERITY = SRT_LOGF_DISABLE_SEVERITY;
const LogFlag SRT_LOGF_DISABLE_EOL = SRT_LOGF_DISABLE_EOL;

// --- Map srt_setloglevel member to artificial LogLevel structure 
// --- structure itself defined in csharp module imports below
// Type mappings for IM wrapper INT -> managed struct
%inline{ typedef int LogLevel; }

// Force C# to use LogLevel structure instead original INT
%typemap(cstype) LogLevel "LogLevel"

// Redefine C# method representation
%ignore srt_setloglevel(int ll);
void srt_setloglevel(LogLevel logLevel);

// Forward constants from C side to C# srt module
const LogLevel LOG_DEBUG = LOG_DEBUG;
const LogLevel LOG_NOTICE = LOG_NOTICE;
const LogLevel LOG_WARNING = LOG_WARNING;
const LogLevel LOG_ERR = LOG_ERR;
const LogLevel LOG_CRIT = LOG_CRIT;

// --- Map srt_addlogfa/srt_dellogfa/srt_resetlogfa members to artificial LogFunctionalArea structure 
// --- structure itself defined in csharp module imports below
// Type mappings for IM wrapper INT -> managed struct
%inline{ typedef int LogFunctionalArea; }

// Force C# to use LogFunctionalArea structure instead original INT
%typemap(cstype) LogFunctionalArea "LogFunctionalArea"

// Redefine C# method representation
%ignore srt_addlogfa(int fa);
void srt_addlogfa(LogFunctionalArea functionalArea);

%ignore srt_dellogfa(int fa);
void srt_dellogfa(LogFunctionalArea functionalArea);

// OVERRIDE SIGNATURE (const int* fara, size_t fara_size)
%typemap(in) (const int* fara, size_t fara_size) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = (int const *)args->Arg1;
    $2 = (size_t)*args->Arg2;
}

%typemap(cstype) (const int* fara, size_t fara_size) "params LogFunctionalArea[]"
%typemap(imtype) (const int* fara, size_t fara_size) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            var array_$csinput = $csinput.Select(x => (int)x).ToArray();
            int array_len_$csinput = array_$csinput.Length; 

            var pin_array_$csinput = GCHandle.Alloc(array_$csinput, GCHandleType.Pinned);
            var pin_array_len_$csinput = GCHandle.Alloc(array_len_$csinput, GCHandleType.Pinned);
            
            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_array_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_array_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            pin_array_$csinput.Free();
            pin_array_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (const int* fara, size_t fara_size)
         "input_$csinput"

// ------------------------------------------------------------
// ------------------------- FLAGS ----------------------------
// ------------------------------------------------------------
// ---- ADD ADDITIONAL method for string flags manipulation
// -- srt_getsockflag_string method
%typemap(in) (void* stringOptVal, int* stringOptLen) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = (int*)args->Arg2;
}
%typemap(cstype) (void* stringOptVal, int* stringOptLen) "out string"
%typemap(imtype) (void* stringOptVal, int* stringOptLen) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            var value_len_$csinput = 512;
            var value_$csinput = new byte[value_len_$csinput];
            var pin_value_$csinput = GCHandle.Alloc(value_$csinput, GCHandleType.Pinned);
            var pin_value_len_$csinput = GCHandle.Alloc(value_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_value_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_value_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            $csinput = Encoding.UTF8.GetString(value_$csinput,0, (int)pin_value_len_$csinput.Target);
            pin_value_$csinput.Free();
            pin_value_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (void* stringOptVal, int* stringOptLen)
         "input_$csinput"

// -- srt_setsockflag_string method
%typemap(in) (void* stringOptVal, int stringOptLen) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = *(int*)args->Arg2;
}
%typemap(cstype) (void* stringOptVal, int stringOptLen) "string"
%typemap(imtype) (void* stringOptVal, int stringOptLen) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            var value_$csinput = Encoding.UTF8.GetBytes($csinput);
            var value_len_$csinput = value_$csinput.Length;
            var pin_value_$csinput = GCHandle.Alloc(value_$csinput, GCHandleType.Pinned);
            var pin_value_len_$csinput = GCHandle.Alloc(value_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_value_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_value_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            pin_value_$csinput.Free();
            pin_value_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (void* stringOptVal, int stringOptLen)
         "input_$csinput"

%typemap(csimports) SRT_STRING_SOCKOPT %{
   using static SRT_SOCKOPT;
%}
%inline {
    typedef enum
    {
        SRTO_STRING_BINDTODEVICE    = SRTO_BINDTODEVICE,
        SRTO_STRING_CONGESTION	    = SRTO_CONGESTION,
        SRTO_STRING_PACKETFILTER    = SRTO_PACKETFILTER,
        SRTO_STRING_PASSPHRASE	    = SRTO_PASSPHRASE,
        SRTO_STRING_STREAMID	    = SRTO_STREAMID,
    }SRT_STRING_SOCKOPT;

    int srt_getsockflag_string(SRTSOCKET u, SRT_STRING_SOCKOPT opt, void* stringOptVal, int* stringOptLen)
    { 
        return srt_getsockflag(u, (SRT_SOCKOPT)opt, stringOptVal, stringOptLen);
    }
    
    int srt_setsockflag_string(SRTSOCKET u, SRT_STRING_SOCKOPT opt, void* stringOptVal, int stringOptLen)
    { 
        return srt_setsockflag(u, (SRT_SOCKOPT)opt, stringOptVal, stringOptLen);
    }
}

// ---- ADD ADDITIONAL method for bool flags manipulation
// -- srt_getsockflag_bool method
%typemap(in) (void* boolOptVal, int* boolOptLen) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = (int*)args->Arg2;
}
%typemap(cstype) (void* boolOptVal, int* boolOptLen) "out bool"
%typemap(imtype) (void* boolOptVal, int* boolOptLen) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            byte value_$csinput = default(byte);
            var value_len_$csinput = Marshal.SizeOf(value_$csinput.GetType());

            var pin_value_$csinput = GCHandle.Alloc(value_$csinput, GCHandleType.Pinned);
            var pin_value_len_$csinput = GCHandle.Alloc(value_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_value_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_value_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            $csinput = ((byte)pin_value_$csinput.Target) == 1 ? true : false;
            pin_value_$csinput.Free();
            pin_value_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (void* boolOptVal, int* boolOptLen)
         "input_$csinput"

// -- srt_setsockflag_bool method
%typemap(in) (void* boolOptVal, int boolOptLen) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = *(int*)args->Arg2;
}
%typemap(cstype) (void* boolOptVal, int boolOptLen) "bool"
%typemap(imtype) (void* boolOptVal, int boolOptLen) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            var value_$csinput = $csinput ? (byte)1 : (byte)0;
            var value_len_$csinput = Marshal.SizeOf(value_$csinput.GetType());
            var pin_value_$csinput = GCHandle.Alloc(value_$csinput, GCHandleType.Pinned);
            var pin_value_len_$csinput = GCHandle.Alloc(value_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_value_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_value_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            pin_value_$csinput.Free();
            pin_value_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (void* boolOptVal, int boolOptLen)
         "input_$csinput"

%typemap(csimports) SRT_BOOL_SOCKOPT %{
   using static SRT_SOCKOPT;
%}

%inline {
    typedef enum
    {
        SRTO_BOOL_DRIFTTRACER = SRTO_DRIFTTRACER,
        SRTO_BOOL_ENFORCEDENCRYPTION = SRTO_ENFORCEDENCRYPTION,
        SRTO_BOOL_MESSAGEAPI = SRTO_MESSAGEAPI,
        SRTO_BOOL_NAKREPORT = SRTO_NAKREPORT,
        SRTO_BOOL_RCVSYN = SRTO_RCVSYN,
        SRTO_BOOL_RENDEZVOUS = SRTO_RENDEZVOUS,
        SRTO_BOOL_REUSEADDR = SRTO_REUSEADDR,
        SRTO_BOOL_SENDER = SRTO_SENDER,
        SRTO_BOOL_SNDSYN = SRTO_SNDSYN,
        SRTO_BOOL_TLPKTDROP = SRTO_TLPKTDROP,
        SRTO_BOOL_TSBPDMODE = SRTO_TSBPDMODE
    }SRT_BOOL_SOCKOPT;

    int srt_getsockflag_bool(SRTSOCKET u, SRT_BOOL_SOCKOPT opt, void* boolOptVal, int* boolOptLen)
    { 
        return srt_getsockflag(u, (SRT_SOCKOPT)opt, boolOptVal, boolOptLen);
    }
    
    int srt_setsockflag_bool(SRTSOCKET u, SRT_BOOL_SOCKOPT opt, void* boolOptVal, int boolOptLen)
    { 
        return srt_setsockflag(u, (SRT_SOCKOPT)opt, boolOptVal, boolOptLen);
    }
}

// ---- ADD ADDITIONAL method for long flags manipulation
// -- srt_getsockflag_long method
%typemap(in) (void* longOptVal, int* longOptLen) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = (int*)args->Arg2;
}
%typemap(cstype) (void* longOptVal, int* longOptLen) "out long"
%typemap(imtype) (void* longOptVal, int* longOptLen) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            long value_$csinput = default(long);
            var value_len_$csinput = Marshal.SizeOf(value_$csinput.GetType());

            var pin_value_$csinput = GCHandle.Alloc(value_$csinput, GCHandleType.Pinned);
            var pin_value_len_$csinput = GCHandle.Alloc(value_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_value_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_value_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            $csinput = (long)pin_value_$csinput.Target;
            pin_value_$csinput.Free();
            pin_value_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (void* longOptVal, int* longOptLen)
         "input_$csinput"

// -- srt_setsockflag_long method
%typemap(in) (void* longOptVal, int longOptLen) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = *(int*)args->Arg2;
}
%typemap(cstype) (void* longOptVal, int longOptLen) "long"
%typemap(imtype) (void* longOptVal, int longOptLen) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            var value_$csinput = $csinput;
            var value_len_$csinput = Marshal.SizeOf(value_$csinput.GetType());
            var pin_value_$csinput = GCHandle.Alloc(value_$csinput, GCHandleType.Pinned);
            var pin_value_len_$csinput = GCHandle.Alloc(value_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_value_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_value_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            pin_value_$csinput.Free();
            pin_value_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (void* longOptVal, int longOptLen)
         "input_$csinput"

%typemap(csimports) SRT_LONG_SOCKOPT %{
   using static SRT_SOCKOPT;
%}

%inline {
    typedef enum
    {
        SRTO_LONG_INPUTBW = SRTO_INPUTBW,
        SRTO_LONG_MAXBW = SRTO_MAXBW,
        SRTO_LONG_MININPUTBW = SRTO_MININPUTBW,
    }SRT_LONG_SOCKOPT;

    int srt_getsockflag_long(SRTSOCKET u, SRT_LONG_SOCKOPT opt, void* longOptVal, int* longOptLen)
    { 
        return srt_getsockflag(u, (SRT_SOCKOPT)opt, longOptVal, longOptLen);
    }
    
    int srt_setsockflag_long(SRTSOCKET u, SRT_LONG_SOCKOPT opt, void* longOptVal, int longOptLen)
    { 
        return srt_setsockflag(u, (SRT_SOCKOPT)opt, longOptVal, longOptLen);
    }
}

// ---- ADD ADDITIONAL method for int flags manipulation
// -- srt_getsockflag_int method
%typemap(in) (void* intOptVal, int* intOptLen) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = (int*)args->Arg2;
}
%typemap(cstype) (void* intOptVal, int* intOptLen) "out int"
%typemap(imtype) (void* intOptVal, int* intOptLen) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            int value_$csinput = default(int);
            var value_len_$csinput = Marshal.SizeOf(value_$csinput.GetType());

            var pin_value_$csinput = GCHandle.Alloc(value_$csinput, GCHandleType.Pinned);
            var pin_value_len_$csinput = GCHandle.Alloc(value_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_value_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_value_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            $csinput = (int)pin_value_$csinput.Target;
            pin_value_$csinput.Free();
            pin_value_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (void* intOptVal, int* intOptLen)
         "input_$csinput"

// -- srt_setsockflag_int method
%typemap(in) (void* intOptVal, int intOptLen) {
    TransitiveArguments * args = (TransitiveArguments *)$input;
    $1 = args->Arg1;
    $2 = *(int*)args->Arg2;
}
%typemap(cstype) (void* intOptVal, int intOptLen) "int"
%typemap(imtype) (void* intOptVal, int intOptLen) "global::System.Runtime.InteropServices.HandleRef"
%typemap(csin,
         pre="
            var value_$csinput = $csinput;
            var value_len_$csinput = Marshal.SizeOf(value_$csinput.GetType());
            var pin_value_$csinput = GCHandle.Alloc(value_$csinput, GCHandleType.Pinned);
            var pin_value_len_$csinput = GCHandle.Alloc(value_len_$csinput, GCHandleType.Pinned);

            var transitive_$csinput = new TransitiveArguments
            {
                Arg1 = pin_value_$csinput.AddrOfPinnedObject(),
                Arg2 = pin_value_len_$csinput.AddrOfPinnedObject()
            };
            var input_$csinput = TransitiveArguments.getCPtr(transitive_$csinput);",
         post="
            pin_value_$csinput.Free();
            pin_value_len_$csinput.Free();
            transitive_$csinput.Dispose();"
        ) (void* intOptVal, int intOptLen)
         "input_$csinput"

%typemap(csimports) SRT_INT_SOCKOPT %{
   using static SRT_SOCKOPT;
%}

%inline {
    typedef enum
    {
        SRTO_INT_CONNTIMEO = SRTO_CONNTIMEO,
        //SRTO_INT_CRYPTOMODE = SRTO_CRYPTOMODE,  //TODO: Remove when AEAD option is no longer conditional
        SRTO_INT_EVENT = SRTO_EVENT,
        SRTO_INT_FC = SRTO_FC,
        SRTO_INT_GROUPCONNECT = SRTO_GROUPCONNECT,
        SRTO_INT_GROUPMINSTABLETIMEO = SRTO_GROUPMINSTABLETIMEO,
        SRTO_INT_GROUPTYPE = SRTO_GROUPTYPE,
        SRTO_INT_IPTOS = SRTO_IPTOS,
        SRTO_INT_IPTTL = SRTO_IPTTL,
        SRTO_INT_IPV6ONLY = SRTO_IPV6ONLY,
        SRTO_INT_ISN = SRTO_ISN,
        SRTO_INT_KMPREANNOUNCE = SRTO_KMPREANNOUNCE,
        SRTO_INT_KMREFRESHRATE = SRTO_KMREFRESHRATE,
        SRTO_INT_KMSTATE = SRTO_KMSTATE,
        SRTO_INT_LATENCY = SRTO_LATENCY,
        SRTO_INT_LOSSMAXTTL = SRTO_LOSSMAXTTL,
        SRTO_INT_MINVERSION = SRTO_MINVERSION,
        SRTO_INT_MSS = SRTO_MSS,
        SRTO_INT_OHEADBW = SRTO_OHEADBW,
        SRTO_INT_PAYLOADSIZE = SRTO_PAYLOADSIZE,
        SRTO_INT_PBKEYLEN = SRTO_PBKEYLEN,
        SRTO_INT_PEERIDLETIMEO = SRTO_PEERIDLETIMEO,
        SRTO_INT_PEERLATENCY = SRTO_PEERLATENCY,
        SRTO_INT_PEERVERSION = SRTO_PEERVERSION,
        SRTO_INT_RCVBUF = SRTO_RCVBUF,
        SRTO_INT_RCVDATA = SRTO_RCVDATA,
        SRTO_INT_RCVKMSTATE = SRTO_RCVKMSTATE,
        SRTO_INT_RCVLATENCY = SRTO_RCVLATENCY,
        SRTO_INT_RCVTIMEO = SRTO_RCVTIMEO,
        SRTO_INT_RETRANSMITALGO = SRTO_RETRANSMITALGO,
        SRTO_INT_SNDBUF = SRTO_SNDBUF,
        SRTO_INT_SNDDATA = SRTO_SNDDATA,
        SRTO_INT_SNDDROPDELAY = SRTO_SNDDROPDELAY,
        SRTO_INT_SNDKMSTATE = SRTO_SNDKMSTATE,
        SRTO_INT_SNDTIMEO = SRTO_SNDTIMEO,
        SRTO_INT_STATE = SRTO_STATE,
        SRTO_INT_TRANSTYPE = SRTO_TRANSTYPE,
        SRTO_INT_UDP_RCVBUF = SRTO_UDP_RCVBUF,
        SRTO_INT_UDP_SNDBUF = SRTO_UDP_SNDBUF,
        SRTO_INT_VERSION = SRTO_VERSION,
    }SRT_INT_SOCKOPT;

    int srt_getsockflag_int(SRTSOCKET u, SRT_INT_SOCKOPT opt, void* intOptVal, int* intOptLen)
    { 
        return srt_getsockflag(u, (SRT_SOCKOPT)opt, intOptVal, intOptLen);
    }
    
    int srt_setsockflag_int(SRTSOCKET u, SRT_INT_SOCKOPT opt, void* intOptVal, int intOptLen)
    { 
        return srt_setsockflag(u, (SRT_SOCKOPT)opt, intOptVal, intOptLen);
    }
}

#if defined(SWIGWORDSIZE64)
%define PRIMITIVE_TYPEMAP(NEW_TYPE, TYPE)
%clear NEW_TYPE;
%clear NEW_TYPE *;
%clear NEW_TYPE &;
%clear const NEW_TYPE &;
%apply TYPE { NEW_TYPE };
%apply TYPE * { NEW_TYPE * };
%apply TYPE & { NEW_TYPE & };
%apply const TYPE & { const NEW_TYPE & };
%enddef // PRIMITIVE_TYPEMAP
PRIMITIVE_TYPEMAP(long int, long long);
PRIMITIVE_TYPEMAP(unsigned long int, unsigned long long);
#undef PRIMITIVE_TYPEMAP
#endif // defined(SWIGWORDSIZE64)
#endif // defined(SWIGCSHARP)

// 
// C# related configuration section, customizing binding for this language  
//
// Rebind objects from the default mappings for types and objects that are optimized for C#
//enums in C# are int by default, this override pushes this enum to the require uint format
%typemap(csbase) SRT_EPOLL_OPT "uint"
%typemap(csattributes) SRT_EPOLL_OPT "[System.Flags]"

//the SRT_ERRNO enum references itself another enum - we must import this other enum into the class file for resolution
%typemap(csimports) SRT_ERRNO %{
   using static CodeMajor;
   using static CodeMinor;
%}

%typemap(csimports) SRT_INT_SOCKOPT %{
   using static SRT_SOCKOPT;
%}

%rename(SRT_TRACEBSTATS) CBytePerfMon;

// Ignore deprecated methods
%ignore srt_rejectreason_msg;
%ignore srt_setsockopt;

// General interface definition of wrapper - due to above typemaps and code, we can now just reference the main srt.h file
%include "srt.h";

// --- C additional definitions
%inline{
    #ifndef _WIN32
        typedef unsigned char byte;
    #endif

    // Structure used for arguments transit
    typedef struct
    {
        byte *Arg1;
        byte *Arg2;
        byte *Arg3;
        byte *Arg4;
        byte *Arg5;
        byte *Arg6;
    } TransitiveArguments;
}

// add top-level code to module file
// which allows C# bindings of specific objects to be injected for easier use in C# 
%pragma(csharp) moduleimports=%{ 

using System;
using System.Linq;
using System.Text;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Reflection;
using System.Collections.Concurrent;
using static SrtSharp.SRT_SOCKOPT;

/// <summary>
/// Delegate which is used for logger callbacks
/// </summary>
/// <param name="opaque">Custom parameter</param>
/// <param name="level">Log level</param>
/// <param name="file">File name</param>
/// <param name="line">Line number</param>
/// <param name="area">Log area name</param>
/// <param name="message">Message</param>
public delegate void SrtLogHandlerDelegate(IntPtr opaque, int level, string file, int line, string area, string message);

/// <summary>
/// Callback hook delegate, which will be executed on a socket that is automatically created to handle the incoming connection on the listening socket (and is about to be returned by srt_accept), but before the connection has been accepted.
/// </summary>
/// <param name="opaque">The pointer passed as hook_opaque when registering</param>
/// <param name="ns">The freshly created socket to handle the incoming connection</param>
/// <param name="hsVersion">The handshake version (usually 5, pre-1.3 versions of SRT use 4)</param>
/// <param name="peerAddress">The address of the incoming connection</param>
/// <param name="streamId">The value set to SRTO_STREAMID option set on the peer side</param>
public delegate void SrtListenCallbackDelegate(
    IntPtr opaque, 
    int ns, 
    int hsVersion, 
    [MarshalAs(UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(SockAddrMarshaler))] IPEndPoint peerAddress, 
    string streamId);

/// <summary>
/// Callback hook delegate, which will be executed on a given u socket or all member sockets of a u group, just after a pending connection in the background has been resolved and the connection has failed. Note that this function is not guaranteed to be called if the u socket is set to blocking mode (SRTO_RCVSYN option set to true). It is guaranteed to be called when a socket is in non-blocking mode, or when you use a group.
/// </summary>
/// <param name="opaque">The pointer passed as hook_opaque when registering</param>
/// <param name="ns">The socket for which the connection process was resolved</param>
/// <param name="errorCode">The error code, same as for srt_connect for blocking mode</param>
/// <param name="peerAddress">The target address passed to srt_connect call</param>
/// <param name="token">The token value, if it was used for group connection, otherwise -1</param>
public delegate void SrtConnectCallbackDelegate(
    IntPtr opaque, 
    int ns,
    SRT_ERRNO errorCode, 
    [MarshalAs(UnmanagedType.CustomMarshaler, MarshalTypeRef = typeof(SockAddrMarshaler))]IPEndPoint peerAddress, 
    int token);

public static class MarshalExtensions
{
    private static readonly FieldInfo SocketAddressBufferField;
    private static readonly FieldInfo SocketAddressInternalSizeField;

    static MarshalExtensions()
    {
        // Internal fields of SocketAddress class are used to access the internal buffer and size
        // But starting from NET 8 internal fields were renamed, so we need to find them in a different way
        var socketAddressFields = typeof(SocketAddress).GetFields(BindingFlags.Instance | BindingFlags.NonPublic);
        SocketAddressBufferField = socketAddressFields.FirstOrDefault(f => f.FieldType == typeof(byte[]));
        SocketAddressInternalSizeField = socketAddressFields.FirstOrDefault(f => f.FieldType == typeof(int));
    }

    internal static byte[] GetInternalBuffer(this SocketAddress socketAddress)
    {
        if (SocketAddressBufferField == null)
        {
            var message = $"Unable to find the internal buffer field in the {nameof(SocketAddress)} class.";
            throw new PlatformNotSupportedException(message);
        }

        return (byte[])SocketAddressBufferField.GetValue(socketAddress);
    }
    
    internal static void SetInternalSize(this SocketAddress socketAddress, int size)
    {
        if (SocketAddressInternalSizeField == null)
        {
            var message = $"Unable to find the internal buffer size field in the {nameof(SocketAddress)} class.";
            throw new PlatformNotSupportedException(message);
        }

        SocketAddressInternalSizeField.SetValue(socketAddress, size);
    }
    
    internal static IPEndPoint ToIPEndPoint(this SocketAddress socketAddress)
    {
        IPEndPoint endPoint = new IPEndPoint(IPAddress.Any, 0);
        return (IPEndPoint)endPoint.Create(socketAddress);
    }

    public static void Validate(this int responseCode, string message)
    {
        if(responseCode != srt.SRT_ERROR) return;
        throw new InvalidOperationException($"{message} failed: {srt.srt_getlasterror_str()}");
    }
    
    public static SRTSOCKET Validate(this SRTSOCKET socket)
    {
        if(socket.IsValid) return socket;
        throw new InvalidOperationException($"Socket was not created: {srt.srt_getlasterror_str()}");
    }
}

internal class SockAddrMarshaler : ICustomMarshaler
{
    private SockAddrMarshaler(string cookies) => Cookies = cookies;
    public string Cookies { get; }

    public static ICustomMarshaler GetInstance(string cookies) => new SockAddrMarshaler(cookies);

    public void CleanUpManagedData(object managedObj) { /* Nothing GC will clean up managed object */ }
    public void CleanUpNativeData(IntPtr pNativeData) { /* Nothing */ }
    public int GetNativeDataSize() => -1;
    public IntPtr MarshalManagedToNative(object managedObj) => throw new NotSupportedException();
    public object MarshalNativeToManaged(IntPtr pNativeData)
    {
        const int DATA_OFFSET = 2;
        var family = Marshal.ReadInt16(pNativeData);
        var dataPointer = pNativeData + DATA_OFFSET;
        var socketAddress = new SocketAddress((AddressFamily) family, 16)
        {
            // Swap port bytes
            [DATA_OFFSET + 0] = Marshal.ReadByte(dataPointer + 1),
            [DATA_OFFSET + 1] = Marshal.ReadByte(dataPointer),
            // Copy address bytes
            [DATA_OFFSET + 2] = Marshal.ReadByte(dataPointer + 2),
            [DATA_OFFSET + 3] = Marshal.ReadByte(dataPointer + 3),
            [DATA_OFFSET + 4] = Marshal.ReadByte(dataPointer + 4),
            [DATA_OFFSET + 5] = Marshal.ReadByte(dataPointer + 5),
            [DATA_OFFSET + 6] = Marshal.ReadByte(dataPointer + 6),
            [DATA_OFFSET + 7] = Marshal.ReadByte(dataPointer + 7),
            [DATA_OFFSET + 8] = Marshal.ReadByte(dataPointer + 8),
            [DATA_OFFSET + 9] = Marshal.ReadByte(dataPointer + 9),
            [DATA_OFFSET + 10] = Marshal.ReadByte(dataPointer + 10),
            [DATA_OFFSET + 11] = Marshal.ReadByte(dataPointer + 11),
            [DATA_OFFSET + 12] = Marshal.ReadByte(dataPointer + 12),
            [DATA_OFFSET + 13] = Marshal.ReadByte(dataPointer + 13),
        };
                
        var endPoint = new IPEndPoint(IPAddress.Any, 0);
        return (IPEndPoint)endPoint.Create(socketAddress);
    }
}

static class GCKeeper
{
    private static readonly ConcurrentDictionary<string, object> _registry = new ConcurrentDictionary<string, object>();
    public static void Keep(string name, object value) 
    {
        _registry.AddOrUpdate(name, value, (key, existen) => value);
    }
    public static void Forget(string name)
    {
        _registry.TryRemove(name, out _);
    }
}

/// <summary>
/// Artificial structure that represents SRTSOCKET
/// </summary>
public readonly struct SRTSOCKET : IDisposable
{
    /// <summary>
    /// Not initialized socket structure
    /// </summary>
    public static readonly SRTSOCKET NotInitialized = 0;
   
    /// <summary>
    /// Check socket is created
    /// </summary>
    public bool IsCreated { get { return _value > 0; } }
    
    /// <summary>
    /// Check socket is valid
    /// </summary>
    public bool IsValid { get { return _value != -1; } }
    
    /// <summary>
    /// Get socket status
    /// </summary>
    public SRT_SOCKSTATUS Status { get { return srt.srt_getsockstate(_value); } }

    private readonly int _value;
    SRTSOCKET(int value) => _value = value;
    public void Dispose() 
    { 
         if(IsCreated) return;
          
         srt.srt_close(_value);
         // Remove any kept delegate pointers associated with an socket
         GCKeeper.Forget($"{nameof(SrtConnectCallbackDelegate)}-{_value}");
         GCKeeper.Forget($"{nameof(SrtListenCallbackDelegate)}-{_value}");
    }
    public override string ToString() => $"{_value}";
    public static implicit operator SRTSOCKET(int b) => new SRTSOCKET(b);
    public static implicit operator int(SRTSOCKET d) => d._value;
}

/// <summary>
/// Artificial structure that represents SYSSOCKET
/// </summary>
public readonly struct SYSSOCKET : IDisposable
{
    /// <summary>
    /// Not initialized socket structure
    /// </summary>
    public static readonly SYSSOCKET NotInitialized = 0;
   
    /// <summary>
    /// Check socket is created
    /// </summary>
    public bool IsCreated { get { return _value > 0; } }
    
    /// <summary>
    /// Check socket is valid
    /// </summary>
    public bool IsValid { get { return _value != -1; } }

    private readonly int _value;
    SYSSOCKET(int value) => _value = value;
    public void Dispose() 
    { 
         if(IsCreated) return;
    }
    public override string ToString() => $"{_value}";
    public static implicit operator SYSSOCKET(int b) => new SYSSOCKET(b);
    public static implicit operator int(SYSSOCKET d) => d._value;
}

/// <summary>
/// Artificial structure that represents EPOLL
/// </summary>
public readonly struct EPOLL : IDisposable
{
    /// <summary>
    /// Not initialized epoll structure
    /// </summary>
    public static readonly EPOLL NotInitialized = 0;
   
    /// <summary>
    /// Check epoll is created
    /// </summary>
    public bool IsCreated { get { return _value > 0; } }
    
    /// <summary>
    /// Check epoll is valid
    /// </summary>
    public bool IsValid { get { return _value != -1; } }

    private readonly int _value;
    EPOLL(int value) => _value = value;
    public void Dispose() 
    { 
         if(IsCreated) return;
          
         srt.srt_epoll_release(_value);
    }
    public override string ToString() => $"{_value}";
    public static implicit operator EPOLL(int b) => new EPOLL(b);
    public static implicit operator int(EPOLL d) => d._value;
}

/// <summary>
/// Artificial structure that represents arguments for srt_setlogflags(LogFlag)
/// </summary>
public readonly struct LogFlag
{
    /// <summary>
    /// Do not provide the time in the header
    /// </summary>
    public static readonly LogFlag DisableTime = srt.SRT_LOGF_DISABLE_TIME;

    /// <summary>
    /// Do not provide the thread name in the header
    /// </summary>
    public static readonly LogFlag DisableThreadName = srt.SRT_LOGF_DISABLE_THREADNAME;

    /// <summary>
    /// Do not provide severity information in the header
    /// </summary>
    public static readonly LogFlag DisableSeverity = srt.SRT_LOGF_DISABLE_SEVERITY;

    /// <summary>
    /// Do not add the end-of-line character to the log line
    /// </summary>
    public static readonly LogFlag DisableEOL = srt.SRT_LOGF_DISABLE_EOL;

    private readonly int _value;
    LogFlag(int value) => _value = value;
    public override string ToString() => $"{_value}";
    public static implicit operator LogFlag(int b) => new LogFlag(b);
    public static implicit operator int(LogFlag d) => d._value;
}

/// <summary>
/// Artificial structure that represents arguments for srt_setloglevel(LogLevel)
/// </summary>
public readonly struct LogLevel
{
    /// <summary>
    /// Highly detailed and very frequent messages
    /// </summary>
    public static readonly LogLevel Debug = srt.LOG_DEBUG;

    /// <summary>
    /// Occasionally displayed information
    /// </summary>
    public static readonly LogLevel Notice = srt.LOG_NOTICE;

    /// <summary>
    /// Unusual behavior
    /// </summary>
    public static readonly LogLevel Warning = srt.LOG_WARNING;

    /// <summary>
    /// Abnormal behavior
    /// </summary>
    public static readonly LogLevel Error = srt.LOG_ERR;

    /// <summary>
    /// Error that makes the current socket unusable
    /// </summary>
    public static readonly LogLevel Critical = srt.LOG_CRIT;

    private readonly int _value;
    LogLevel(int value) => _value = value;
    public override string ToString() => $"{_value}";
    public static implicit operator LogLevel(int b) => new LogLevel(b);
    public static implicit operator int(LogLevel d) => d._value;
}

/// <summary>
/// Artificial structure that represents arguments for srt_addlogfa/srt_dellogfa/srt_resetlogfa
/// </summary>
public readonly struct LogFunctionalArea
{
    /// <summary>
    /// gglog: General uncategorized log; for serious issues only
    /// </summary>
    public static readonly LogFunctionalArea General = srt.SRT_LOGFA_GENERAL;

    /// <summary>
    /// smlog: Socket create/open/close/configure activities
    /// </summary>
    public static readonly LogFunctionalArea SocketManagement = srt.SRT_LOGFA_SOCKMGMT;

    /// <summary>
    /// cnlog: Connection establishment and handshake
    /// </summary>
    public static readonly LogFunctionalArea Connection = srt.SRT_LOGFA_CONN;

    /// <summary>
    /// xtlog: The checkTimer and around activities
    /// </summary>
    public static readonly LogFunctionalArea XTimer = srt.SRT_LOGFA_XTIMER;

    /// <summary>
    /// tslog: The TsBPD thread
    /// </summary>
    public static readonly LogFunctionalArea TsBPD = srt.SRT_LOGFA_TSBPD;

    /// <summary>
    /// rslog: System resource allocation and management
    /// </summary>
    public static readonly LogFunctionalArea ResourceManagement = srt.SRT_LOGFA_RSRC;

    /// <summary>
    /// cclog: Congestion control module
    /// </summary>
    public static readonly LogFunctionalArea Congestion = srt.SRT_LOGFA_CONGEST;

    /// <summary>
    /// pflog: Packet filter module
    /// </summary>
    public static readonly LogFunctionalArea PacketFilter = srt.SRT_LOGFA_PFILTER;

    /// <summary>
    /// aclog: API part for socket and library management
    /// </summary>
    public static readonly LogFunctionalArea SocketApi = srt.SRT_LOGFA_API_CTRL;

    /// <summary>
    /// qclog: Queue control activities
    /// </summary>
    public static readonly LogFunctionalArea QueueControl = srt.SRT_LOGFA_QUE_CTRL;

    /// <summary>
    /// eilog: EPoll; internal update activities
    /// </summary>
    public static readonly LogFunctionalArea EPollActivities = srt.SRT_LOGFA_EPOLL_UPD;

    /// <summary>
    /// arlog: API part for receiving
    /// </summary>
    public static readonly LogFunctionalArea ReceivingApi = srt.SRT_LOGFA_API_RECV;

    /// <summary>
    /// brlog: Buffer; receiving side
    /// </summary>
    public static readonly LogFunctionalArea ReceivingBuffer = srt.SRT_LOGFA_BUF_RECV;

    /// <summary>
    /// qrlog: Queue; receiving side
    /// </summary>
    public static readonly LogFunctionalArea ReceivingQueue = srt.SRT_LOGFA_QUE_RECV;

    /// <summary>
    /// krlog: CChannel; receiving side
    /// </summary>
    public static readonly LogFunctionalArea ReceivingChannel = srt.SRT_LOGFA_CHN_RECV;

    /// <summary>
    /// grlog: Group; receiving side
    /// </summary>
    public static readonly LogFunctionalArea ReceivingGroup = srt.SRT_LOGFA_GRP_RECV;

    /// <summary>
    /// aslog: API part for sending
    /// </summary>
    public static readonly LogFunctionalArea SendingApi = srt.SRT_LOGFA_API_SEND;

    /// <summary>
    /// bslog: Buffer; sending side
    /// </summary>
    public static readonly LogFunctionalArea SendingBuffer = srt.SRT_LOGFA_BUF_SEND;

    /// <summary>
    /// qslog: Queue; sending side
    /// </summary>
    public static readonly LogFunctionalArea SendingQueue = srt.SRT_LOGFA_QUE_SEND;

    /// <summary>
    /// kslog: CChannel; sending side
    /// </summary>
    public static readonly LogFunctionalArea SendingChannel = srt.SRT_LOGFA_CHN_SEND;

    /// <summary>
    /// gslog: Group; sending side
    /// </summary>
    public static readonly LogFunctionalArea SendingGroup = srt.SRT_LOGFA_GRP_SEND;

    /// <summary>
    /// inlog: Internal activities not connected directly to a socket
    /// </summary>
    public static readonly LogFunctionalArea Internal = srt.SRT_LOGFA_INTERNAL;

    /// <summary>
    /// qmlog: Queue; management part
    /// </summary>
    public static readonly LogFunctionalArea ManagementQueue = srt.SRT_LOGFA_QUE_MGMT;

    /// <summary>
    /// kmlog: CChannel; management part
    /// </summary>
    public static readonly LogFunctionalArea ManagementChannel = srt.SRT_LOGFA_CHN_MGMT;

    /// <summary>
    /// gmlog: Group; management part
    /// </summary>
    public static readonly LogFunctionalArea ManagementGroup = srt.SRT_LOGFA_GRP_MGMT;

    /// <summary>
    /// ealog: EPoll; API part
    /// </summary>
    public static readonly LogFunctionalArea EPollApi = srt.SRT_LOGFA_EPOLL_API;

    /// <summary>
    /// hclog: Haicrypt module area
    /// </summary>
    public static readonly LogFunctionalArea HaiCrypt = srt.SRT_LOGFA_HAICRYPT;

    /// <summary>
    /// aplog: Applications
    /// </summary>
    public static readonly LogFunctionalArea Applications = srt.SRT_LOGFA_APPLOG;

    private readonly int _value;
    LogFunctionalArea(int value) => _value = value;
    public override string ToString() => $"{_value}";
    public static implicit operator LogFunctionalArea(int b) => new LogFunctionalArea(b);
    public static implicit operator int(LogFunctionalArea d) => d._value;
}

/// <summary>
/// SRT_EPOLL_EVENT structure definition
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct SRT_EPOLL_EVENT
{
    /// <summary>
    /// The user socket (SRT socket)
    /// </summary>
    public SRTSOCKET Socket;

    /// <summary>
    /// Event flags that report readiness of this socket
    /// </summary>
    public SRT_EPOLL_OPT Events;
}
%}