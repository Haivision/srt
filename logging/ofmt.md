# Introduction

OFMT is the output formatting helper tool cooperating with the standard
IOSTREAM library and providing the "on-demand tagged API". This allows to
provide configuration for formatting values in isolation, or as stateous
for a local scope, and additionally wrapper classes providing additional API.

Example:

```
int x = 16, y = 10;
cout << fmt(x, fmtc().hex()) << " " << y << endl;
```

So it prints:
``
10 10
``

# Basic usage

USAGE:

1. Using iostream style:

```
ofmt_refs sout(cout);
sout << "Value: " << v << " (" << fmt(v, fmtc().hex().width(2).fillzero()) << ")\n";
```

NOTE 1: You can use the `fmt` function also directly with ostream-based
objects, you just need to include `"ofmt_iostream.h"` and note that the stream
wrapper classes (ofmt_bufs and ofmt_refs) always pass string values
directly (bypassing the formatting facility). If you use ostream directly, you
can force this bypassing by using `fmt_rawstr()` for strings every time.

NOTE 2: When passing a string literal, consider using `"Value"_SV` (C++11 only)
or `OFMT_SV("Value")`. Unfortunately C++ doesn't distinguish between `"Value"`
and `char v[20] = "Value"`; both here contain `"Value\0"`, but `sizeof()` for
them returns 6 and 20 respectively, as it's the reserved size, not string size.
Although the compiler should expand `strlen()` in place for literals, note it
might not do it with turned off optimizations.

2. Using variadic style (C++11 only):

```
ofmt_refs sout(cout);
sout.print("Value: ", v, " (", fmt(v, fmtc().hex().width(2).fillzero()), ")\n");
```

or

```
ofmt_refs sout(cout);
sout.printl("Value: ", v, " (", fmt(v, fmtc().hex().width(2).fillzero()), ")");
```

or

```
ofprintl(cout, "Value: ", v, " (", fmt(v, fmtc().hex().width(2).fillzero()), ")");
```

The following versions for the inline fmt* calls are available:

* `fmt(value)` - uses the default formatting (for `ofmt*stream` classes, all
values other than string holders pass through it automatically)

* `fmt(value, fmtc()...)` - uses configuration-specific isolated formatting

* `fmtx(value)` - sends the value using the current formatting of the stream

* `fmtx(value, fmtc()...)` - like above, but fmtc object modifies the stream's
state for the value, on top of the current settings

* `fmtm(value, manip1, manip2...)` - isolated formatting with iostream manips

* `fmt_if(value, s1, s2)` - passes s1 if `value == true` and s2 otherwise


# Motivation

The iostream library has many advantages over the old C's printf function, but
the design has flaws: the formatting settings can be only changed as a stream
state (through methods or inline manipulators). For example, once you send
`hex` manipulators to the stream, all integer values are printed in hex until
you change it back to `dec` - including other far away instructions and other
functions. Only the `width` parameter is reset to 0 after printing a value of
type for which `operator<<` is defined in the standard library. To work this
around you'd have to save and restore stream state by yourself, there is even
no facility to do some "push and pop settings" for the stream. This approach
may be desired sometimes, but mostly it is a drawback, of which users are often
unaware, especially accustomed to printf, where default formatting settings
are always the same, or some settings are always specified explicitly.

To fix this problem, you can use the so-called On-Demand Tagged API. That is,
instead of specifying the configuration by changing the stream's state, you can
choose to just apply a format specification for a single value using the
supplied `fmt` function, while the stream state remains untouched:

```
cout << fmt(x, fmtc().hex()) << " " << y; // y printed as dec
```

The `fmtc` structure is provided for the following reasons:

* Allows for C++03 compatibility (infinite call chain)
* Does not pollute the namespace with freestanding manipulator names
* The settings philosophy is made closer to the one of printf tags
* Provides an ability to save the configuration in a single object:

```
fmtc hex04 = fmtc().hex().fillzero().width(4);
cout << fmt(a, hex04) << ":" << fmt(b, hex04);
```

You can also use iostream manipulators directly. Note that the version for
C++03 supports only up to 2 manipulators.

```
cout << fmtm(x, hex) << " " << y; // y printed as dec
```

This solution is compatible with C++98/C++03 version, but if you compile
in C++11 mode, some more advanced API is inaccessible.

# Formatting flags

The following configuration items are available in `fmtc` type:

* `width(int field_length)`: set minimum field length to `field_length`
* `precision(int prec)`: floating-point precision
* `fill(char c)`: character to fill unused space of minimum width
* `fillzero()`: same as `.fill('0').internal()`
* `left()`, `right()`, `internal()`: alignment control inside wide field
* `dec()`, `hex()`, `oct()`: set numeric base system (lowercase)
* `uhex()`, `uoct()`: uppercase version of the above (for oct only prefix)
* `fhex()`, `ufhex()`: floating-point hex format (C++11 only)
* `general()`, `ugeneral()`: floating-point value-dependent fixed or scientific selection
* `fixed()`: floating-point fixed format
* `exp()`, `scientific()`: floating-point scientific format (exponental)
* `uexp()`, `uscientific()`: floating-point scientific format (exponental) with uppercase E
* `showbase()`: use `0` prefix for oct and `0x` for hex (`0X` if `uhex`)
* `showpos()`: prefix positive numbers with `+`
* `showpoint()`: add decimal point always, even if fraction part is 0

(Note: fillzero() sets the `0` character as filling, with regard to the
`char` or `wchar_t` types, and also sets `internal` adjustment field.)

Note that all of them are implemented as methods that return "itself", so
you can bind settings in chain. You can also create local variable for this
type so that you can define the format specification and use in multiple `fmt`
calls:

```
fmtc phex8 = fmtc().hex().width(8).fillzero().showbase();
```

# Formatting stream utility

The general idea for the on-demand tagged API is that the stream state
remains the same and other settings only apply to the single value. But
the iostream's streams don't work this way. Therefore here are available
stream classes that should do: `ofmt_bufs` and `ofmt_refs`. They
provide the right overloads of the `operator<<`, which accept 3 types
of arguments:

* The format-value proxy object, which should print the value with
explicit format settings (returned by the helper `fmt` method)

* The string holder types (`std::string`, string view type and the raw
NUL-terminated string), which are printed directly through `write` method

* Any other type of the value, which is passed through `fmt` with one
argument and then redirected to the first one

The intermediate formatting is done by the locally created `std::stringstream`
object, which is then copied to the stream's base object by buffer-to-buffer
copy. The stream's base object is `std::stringstream` object in `ofmt_bufs`
and the referred object of `std::ostream` type in `ofmt_refs`.

Beside the "traditional" overloads for `operator<<`, they provide also the
"print" function, which uses multiple arguments ("printl" additionally adds the
end-of-line). This is only available in C++11 version.

Alternatively you can also use `ofprint` and `ofprintl` functions with any
`std::ostream` class as first argument, which correspond to the `print` and
`printl` methods respectively.


# Iostream support

This support is not provided by default, but you can use this also with
iostream classes' instances. You need to include "`ofmt_iostream.h`" for this.
It provides extra overloads for the internal type
`internal::fmt_proxy_template` so that the result of the `fmt` function can be
handled. Note that the rules as above described for `ofmtstream` do not apply
here, unless you use `fmt_rawstr()` function for every string.

This provides also a possibility to use `std::put_time` through a special
overload of the `fmt` function. Usage:

```
ofmt_bufs out;
...
typedef std::chrono::system_clock sclock;
std::time_t timenow = sclock::to_time_t(sclock::now());

out << "Timestamp: " << fmt(*std::localtime(&timenow), "%F %T");
```

(Note that in C++20 there are some easier way to get from `sclock::now()` to
`std::put_time` and you can easily use it through `fmt` instead of
`std::put_time`).

If you want to create a similar formatting specifier for your type, follow the
example in `ofmt_iostream.h` that defines `snd_time_tm` for `put_time`.
Generally what you need to do is to provide your own structure with defined
`format_send` method inside, and the configuration specification will be
transparently passed by `fmt` to this structure, while returning
`hvu::internal::fmt_proxy_template<YOUR_STRUCTURE>`.


# Stateous API

Although the stateous API is a biggest design flaw of iostream, there are
cases sometimes, when you need it - especially if you want to use a temporary
buffer for a series of data to be printed. In that case you should use
wrappers of `ofmt_bufx` or `ofmt_refx`.

The difference between `ofmt_*x` and `ofmt_*s` types is in the way how
the values are sent to the stream by default:

* For `*s` types, the default is to pass it through fmt(), which enforces
default formatting

* For `*x` types, the default is to pass it through fmtx(), which uses the
current formatting

In any case you can use fmt() of fmtx() to enforce the desired type:

For example:

```
ofmt_bufx ob;
ob.setup(fmtc().hex());
int x = 0x20, y = 0x30;

// PRINTS:20 30
ob << x << " " << y;

// PRINTS:32 30
ob << fmt(x) << " " << y;

// PRINTS:  32 30
ob << fmt(x, fmtc().width(4)) << " " << y;

// PRINTS:  20 30
ob << fmtx(x, fmtc().width(4)) << " " << y;
```

# Formatting application functions

The following functions can be used inside the stream sending instruction:

* `fmt`: Applies formatting according to the settings (or default)

* `fmtx`: The "stateous" version of fmt

* `fmtm`: The fmt version using ostream manipulators directly

* `fmts`: formats the single value (like with `fmt`) and return it as `std::string`;
the call to `fmts(value, man1)` is equivalent to calling
`ofcat(fmt(value, man1))` with just one argument

* `fmt_rawstr`: Turns a string of `std::string` or pointer-length specification
into the `internal::fmt_stringview` type, which can be directly handled by the
`operator<<` overload or `print` method of `ofmt_bufs`. This is also provided
for iostream version.

* `fmt_if`: Formats a boolean value according to the direct specification of
the value for "true" and "false" respectively

* `operator""_SV`: Turns the literal string into the value of
`internal::fmt_stringview`; allows to take the literal's size statically

* `OFMT_SV`: A preprocessor macro that does the same as `_SV` operator,
available for C++03


# Additional multi-argument functions

All these functions take multiple arguments, each of them is turned into a
formatted version and they are glued together into a single string value.

* `ofcat`: a multi-argument function where formatted versions of the arguments
are glued together and returned as `std::string`

* `ofprint`: first argument is the stream to which arguments will be written

* `ofprintl`: like `ofprint`, but `std::endl` is added after the last argument

Note that these functions operate on default settings, and modifications are
only possible on-demand through fmt* functions.


