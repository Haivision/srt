Contents
========

This module contains the logging system, plus dependent utilities.


Compat utilities
================

Header: `hvu_compat.h`
Source: `hvu_compat.c`

Implements the following functions:

1. `SysStrError`: replicates the functionality of the `strerror` function
portable way. This function returns a message assigned to the given system
error code.

The "portable" `strerror` function is not reentrant, and the reentrant
functions are not portable, there are 2 different versions on POSIX systems
and there's a whole new procedure on Windows. This function should cover
this functionality on all supported systems.

The version for C language requires an output buffer. An extra C++ version
returns it as a string.

The version with C interface is named `hvu_SysStrError`, everything else is
in the `hvu` namespace.

2. `SysLocalTime`: returns the `tm` structure for the local time basing on
given value that should be the number of seconds sinc epoch. This should
replicate the reentrant versions of the `localtime` function.


Thread name
===========

This is a facility that can be used to name a thread so that the name is
visible in the debugger. This name can be also extracted by the logging
system so that this name is present in the log.

The `ThreadName::set` can be used to set the current thread's name. This
can be only used in the thread handler function so that the thread can
set the name for itself.

The `ThreadName::get` can be used to read the current thread name.
The name is read into the buffer of size 64, designated as BUFSIZE static
constant.

This class can be used to make a temporary current process name change
using the RAII-RRID object: You set the name in the constructor, which
will extract the previous name, then the destructor restores the old name.
This can be used as a trick to name the thread at the time when it starts
so that the name is inherited in the newly spawned thread.

Note that the use of this class can be not always reliable because it
would have to be stated that during the life time of this object the
thread should not be switched, otherwise the name read by other facilities
could be misleading. A more reliable way is to set the thread name
in the beginning of the thread handler using `ThreadName::set`.


OFMT
====

This is a header-only library facilitating the on-demand tagged formatting.

It can be used for internal formatting only (using the wrapper over
`std::stringstream`) and with iostream as well. It provides the `fmt`
function that facilitates the "on-demand tagged API" for formatting.
Example:

```
printf("%x : %d", a, b);
```

In the bare iostream should be written as:

```
cout << hex << a << dec << " : " << b;
```

With the on-demand tagged API it has to be written as:

cout << fmt(a, fmtc().hex()) << " : " << b;

Additionally, it provides optimizations based on the fact that in
the buffer class you can bypass any formatting for strings, if you
don't use `fmt`, that is, rely on the default formatting.

```
hvu::ofmtbufstream out;

out << fmt(a, fmtc().hex()) << " : " << b;
```

Here the `" : "` part will be written to the embedded std::stringstream
using the write() method, not using operator<<, which will bypass the
formatting.


Follow the [OFMT documentation](ofmt.md) for details.


Logging system
==============

The logging system consists of two main parts:

1. Functional part
2. Generated part

The functional part is the whole logging facility and it provides one
default "general" functional area designated object that you can use
for logging. Additional (selectable) functional areas you can use through
the generated code.


Generated files
---------------

The generated part is created using the script `generate-fa-files.tcl`.
As argument it requires the configuration file. You should save this
configuration file in your project. The model for this configuration is
provided in `config-model.tcl` file.

Note that you are obliged to perform the generation in order to be able
to use this library at all, but your configuration may contain the empty
list of the FA, if you only plan to use one general FA, and all configuration
items can be reused from the model configuration.

In this file you can define your all FA entries. For every FA, beside
the description you have the identification name (to find this FA by
string name) and the name prefix. For every FA there will be created
a global variable, which's name consists of `<prefix><suffix>`, where
`<prefix>` is in this table and `<suffix>` is common for the whole
configuration defined in `loggers_varsuffix` variable.


Basic Usage
-----------

All these variables can be used then to perform logging at the given
location in the code. So, for example, when you have a FA with prefix
"fa" and the variable suffix is "log", this is to issue a log message
for level "error".

```
falog.Error("Wrong value of ", x);
```

Note that the printing call form uses the "subsequent arguments" method,
similar to the `print` function in Python or Perl (NOTE: not C++20 one).
In order to use any non-default formatting for numeric values, use the
`fmt` function from the `ofmt.h` header; see above for details.


Macros
------

This above shown call method is not recommended if you want to be able to
control the logging at compile time to be generally enabled or disabled;
additionally this method doesn't provide the file and line information,
should you need it in your logging line format. Additionally, the variadic
argument version is only available since C++11. Therefore there are two macros
added:

* `LOGP`: Sequential logging arguments specification (like `print` in Perl or Python)
* `LOGC`: Use the iostream-style `operator <<` to specify arguments

They resolve to call of the above instruction, if the `ENABLE_DEBUG`
macro is defined as 1, otherwise they resolve to nothing. Additionally
for logging that is considered "heavy" (very detailed and often occurring,
usually for debug only), there are corresponding `HLOGP` and `HLOGC`
macros enabled by `ENABLE_HEAVY_LOGGING`. The usage is:

```
LOGP(falog.Error, "Wrong value of ", x);
```
or with iostream-style (`log` is a local variable inside this instruction only):
```
LOGC(falog.Error, log << "Wrong value of " << x);
```

The second one is the only possibility for C++03/C++98. The LOGP
is defined for this standard, but it accepts only one message argument.

For convenience these enabler macros enable also the use of the following
convenience macros:

* `IF_LOGGING( STATEMENT )`
* `IF_HEAVY_LOGGING( STATEMENT )`

You can use them in order to prepare appropriate parameters that you would like
to use in the logging instruction, but they are not used outside the logging -
such as declaring a helper variable. Only a single instruction can be placed
inside the arguments.


Configuration
-------------

In order to manage your logging configuration during runtime there is provided
the configuration object. Currently it's implemented as a singleton, for which
you define the access function. Note that the singleton is using the C++ system
supported singleton (meaning, guaranteed to be thread-safely initialized).
Theoretically this is not a requirement in C++98 to be supported, but all
compilers have been supporting it since even before C++11 has been defined,
regardless of the standard requirement. You might want to check on your
compiler if the thread-safe global initialization is in force, but this thing
is only known to be unsupported on archaic gcc compilers only.

In this configuration object you can:

* set up the log selection
* find the FA id by name
* configure the C++ stream used for log printing
* configure the log handler function (instead of printing on the `cerr` stream)
* configure special format flags

The name of the type of the configuration object is `hvu::logging::LogConfig`.
The accessor function's name is defined in the logger configuration file
under the `loggers_configname` key.


Management
----------

All FA objects get their ids (unique and generated) and they can be also
accessed by these IDs as being collected in a configuration object, to which
they should be added during the creation time. The ID can be obtained by

```
falog.id()
```

IDs can be searched also by name in the configuration object and through
this object they can be also turned on or off.

Particular logs can be enabled or disabled using two categories:

1. Level. You can set the highest possible log level. All logs that
are below this level will not be printed. The levels are in this order:

* Fatal
* Error
* Warn
* Note
* Debug

These above are the method names that issue printing a log at particular
level. The corresponding level values for configuration as enum labels
of type `hvu::logging::LogLevel::type`:

* fatal
* error
* warning
* note
* debug

You can translate a string into this value using `hvu::logging::parse_level`.

Using the configuration object you can set the maximum level by

* `set_maxlevel(level)`

Note that the argument should be the value of `hvu::logging::LogLevel::type`
type, but you can use as well the `LOG_*` values from `<syslog.h>` header and
explicitly convert them into this type. See the `logging_api.h` header for
values. Values do not have 1-based resolution, but this isn't a problem if
you set the log level value to a value not present in this list; the comparison
for enabled log bases on the integer value of the `LOG_*` symbol.

For Windows there's a specific header provided with `LOG_*` symbols,
`windows_syslog.h`.


2. Functional Area. 

You can obtain the IDs from the names given by a string with names
separated by comma. All IDs are then collected in a set returned
by `hvu::logging::parse_fa`. This requires the configuraiton object
because all the names are collected there. These IDs can be then
used to turn on or off the particular functional areas.

The following functions can be used to configure the enabled FAs:

* `enable_fa(name, enabled)`: find FA by name and set it enabled or disabled
* `enable_fa(array, arraysize, enabled)`: enable or disable FAs by IDs in the array
* `setup_fa(faset)`: reset enabled FA to only those present in `faset`
* `setup_fa(faset, enabled)`: set only selected FAs enabled or disabled


Additional configuration facilities
-----------------------------------

The following methods are available in `LogConfig` object:

* `size()`

Returns the number of functional areas registered in this configuration.

* `name(id)`

Returns the name for this FA ID. Empty string is returned if ID is invalid.

* `find_id(name)`

Find the FA ID by name. The value should be the positive integer or 0.
If this name is not found, -1 is returned. Note that there is always available
a FA named "general" with ID 0.

* `set_handler(opaque, handler)`

Sets the handler function that will be called instead of the default one that
prints into the stream, where `opaque` is a `void*` pointer value to be always
passed to the handler and `handler` is the function to be called with the
following signature:

```
typedef void HVU_LOG_HANDLER_FN(void* opaque, int level, const char* file, int line, const char* area, const char* message);
```

where:

   * `opaque` is the object as passed to the `set_handler` call
   * `level` is the level value as above described
   * `file` and `line` are values passed from the macro `LOGC` or `LOGP`
   * `area` is the FA prefix, as configured
   * `message` is the log message with header

Note that the header is always present before the message text, and what
this header contains, can be configured in the flags

* `set_flags(f)`

Sets the flags that control the contents of the header; using these you can
turn off particular elements of the log text. This is useful if you want to
format the log message and provide particular parts of the message yourself.

Flags:

   * `HVU_LOGF_DISABLE_TIME`: Do not add the time when the log instruction was executed
   * `HVU_LOGF_DISABLE_THREADNAME`: Do not add the thread name to the log header
   * `HVU_LOGF_DISABLE_SEVERITY`: Do not add level marker to the log header
   * `HVU_LOGF_DISABLE_EOL`: Do not add the EOL character at the end of `message`


Development dependent parts
===========================

The logging system uses thread-related facilities for its own purpose.
It doesn't use threads, but it does use thread names, as well as mutexes
and atomics.

The header file `hvu_sync.h` provides appropriate definitions based on
the C++11 standard library.

If you want to use it with C++03, you have to provide these facilities
yourself. The SRT library contains a nice wrapper library over the POSIX
threads that provide the appropriate classes that use the same API as
the C++11 standard library - you can use it as an example. In order to
use a header with your definitions, provide its name in `HVU_EXT_INCLUDE_SYNC`
macro in the compilation command line.


