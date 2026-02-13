Introduction
============

The iostream library has many advantages over the old C's printf function,
but the design has flaws: the formatting specification exists as a state of
the stream, which can be modified on the fly through manipulators.
For example, once you send `hex` manipulator into the stream, all integers will
be printed in hex since this time, until you change it back to `dec`.
This includes the instruction of sending to a stream happening anyhow next,
including after some instructions or in another function. The rules for
iostream manipulators only state that the `width` parameter is reset to 0,
and only after printing value of a type, for which the `operator<<` is
overloaded in the C++ standard library. All other parameters just get
changed and there's even no method to reset settings to system defaults
(you can do it by doing save-and-restore configuration, but it's on your
head as well to remember the system configuration in the beginning, which
is actually impossible in case of a library).

To fix this problem, you can use the so-called On-Demand Tagged API. That is,
instead of specifying the configuration by changing the stream's state, you can
choose to just apply a format specification for a single value using the
supplied `fmt` function:

```
cout << fmt(x, fmtc().hex()) << " " << y; // y printed as dec
```

(Note here that to compile this fragment, you need to include `ofmt_iostream.h`).

The formatting specification is using the `fmtc` structure to achieve it,
which prevents from polluting the global namespace with too many names
as well as allows to save the configuration in a variable so that it can
be specified for multiple values:

```
fmtc hex04 = fmtc().hex().fillzero().width(4);
cout << fmt(a, hex04) << ":" << fmt(b, hex04);
```

Although for a single manipulator there is prepared also a simplified version
using the iomanip manipulators:

```
cout << fmt(x, hex) << " " << y; // y printed as dec
```

(Potentially this can be extended to handle multiple iomanip formatters,
but it's not implemented yet).

This solution is compatible with C++98/C++03 version. Some alternative, more
comfortable API is provided for C++11.


Formatting flags
================

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
you can bind settings in chain:

```
	fmtc().hex().width(8).fillzero().showbase()
```

You can also create local variable for this type so that you can define
the format specification and use in multiple `fmt` calls:

```
fmtc phex8 = fmtc().hex().width(8).fillzero().showbase();
```

The `width`, `precision` and `fill` methods have a parameter and this way
they can use as well a runtime value.

Formatting stream utility
=========================

The idea for stream manipulation with on-demand tagged API is that there are
overloads for several types known as "string form", in which case this
string is directly written into the stream, with bypassing any formatting.
Everything else is passed through the `fmt` function with no config spec,
that is, it will use default formatting. The `fmt` function returns a proxy
object, which will employ std::stringstream for formatting, and then in order
to send to the output stream it will use buffer-to-buffer copy.

Hence the `ofmtbufstream` class is provided, which is a wrapper around
`std::stringstream` and should be used instead of it in order to utilize the
on-demand tagged API. Beside the "traditional" overloads for `operator<<`, it
provides also the "print" function, which uses multiple arguments ("puts"
additionally adds the end-of-line, just like the standard C "puts" function
does). This function is only available in C++11 version.


Iostream support
================

This support is not provided by default, but you can use this also with
iostream classes' instances. You need to include "`ofmt_iostream.h`" for this.
It provides extra overloads for the internal types `internal::fmt_proxy` and
`internal::fmt_simple_proxy` so that the result of the `fmt` function can be
handled. Note that the rules as above described for `ofmtstream` do not apply
here, unless you use `fmt_rawstr()` function for every string.

This provides also a possibility to use `std::put_time` through a special
overload of the `fmt` function. Usage:

```
ofmtbufstream out;
...
typedef std::chrono::system_clock sclock;
std::time_t timenow = sclock::to_time_t(sclock::now());

out << "Timestamp: " << fmt(*std::localtime(&timenow), "%F %T");
```

(Note that in C++20 there are some easier way to get from `sclock.now()` to
`std::put_time` and you can easily use it through `fmt` instead of
`std::put_time`).

If you want to create a similar formatting specifier for your type, this is
needed to be done:

1. Create your proxy type that will carry over the value and format
specification. You can also provide an overload for `fmt` function with that
type, although all you need to have is a function that will return the proxy
object by value.

2. Create an partial specialization of `hvu::internal::fmt_simple_proxy` with
this proxy type. Inside you need to provide a `sendto` method template that
will print this value in the passed stream according to the specification.

Follow the example in `ofmt_iostream.h` that defines `tm_proxy` for `put_time`.


Stateous API
============

Although the stateous API is a biggest design flaw of iostream, there are
cases sometimes, when you need it - especially if you want to use a tempoarary
buffer of `ofmtbufstream` for a series of data to be printed. In that case
you should use the following:

* `ofmtbufstream::setup()` to set the formatting specification that should
be permanent
* In case of a need for additional settings at the printed value, use `fmtx`
instead of `fmt`

The difference between `fmt` and `fmtx` is that the latter applies the
settings to the very stream, on which it operates (rather than on a temporarily
created sub-buffer as `fmt` does), which makes every setting permanent,
and all earlier settings are preserved. This way such settings as `width`
can be applied using `fmtx`, as this setting is always being reset to 0 after
printing a value of a standard type.


Additional formatting functions
===============================

The `fmt` function returns a proxy that should make the value written into the
stream with appropriate format. Besides there are also other formatting tools:

* `fmtx`: The "stateous" version of fmt

* `fmtcat`: a multi-argument function where formatted versions of the arguments
are glued together and returned as `std::string`

* `fmts`: formats the single value (like with `fmt`) and return it as `std::string`;
the call to `fmts(value, man1)` is equivalent to calling
`fmtcat(fmt(value, man1))` with just one argument

* `fmt_rawstr`: Turns a string of `std::string` or pointer-length specification
into the `internal::fmt_stringview` type, which can be directly handled by the
`operator<<` overload or `print` method of `ofmtbufstream`. This is also provided
for iostream version.


