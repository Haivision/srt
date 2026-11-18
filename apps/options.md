Program option handling
=======================

This is the facility to maintain the program options.

It allows to define the option syntax for the command line, and then parse it
according to the definitions.


Main facility
=============

Currently the main facility allows to get fed from the original program's
main() function arguments.

It can be also declared as global, but if you decide to use option symbols,
note that all of them should be in exactly the same scope as the main facility
object (due to pointer persistence).

To use the main facility, create a variable:

```
OptionHandler opts;
```

Once all the required definitions are done, perform:

```
opts.process(argv, argc);
```

and all options should be accessible.

The `process` method also returns a value of type OptionStatus. It's a
structure with 3 fields:

* `status` : false, if processing failed
* `error_option` : a string with the option name that failed to parse
* `error_code` : enum with the error symbol:
   * `ERR_MISSING` : too little arguments specified
   * `ERR_EXCEED` : too many arguments specified
   * `ERR_UNDEF` : this option wasn't defined

Options can be identified by the name:

* Non-option arguments are collected under the empty-string name
* Option name is the string designiting it without the initial "-"
   * Note: `--long-option` is then identified as `"-long-option"`

There are two ways, how you want to grab the options and their values:

* Free way: after processing just try to extract the option by its
name specified as string. Multiple names can be used in one call.
In this case all options expect the same kind of arguments.

* Defined way: You need to additionally define variables that will
be pinned into the handler and have their names defined, together
with expected arguments and help text. This should be done before
you call `process()`.


Syntax features
===============

From the command line arguments you can extract:

* free arguments: those that are specified without an option
* options with their assigned arguments

Which arguments are assigned to options, and what arguments are expected,
it depends on their arity specification (symbols defined in `OptionScheme`):

* `ARG_NONE`: None; any non-option arguments belong to free ones
* `ARG_VAR`: All non-option arguments up to next option are assigned
* `ARG_FIXED(N)`: Exactly N following arguments are assigned
* `ARG_OPT(N)`: Like `ARG_VAR`, but only up to N arguments

For example: if we have an option named "a" with `ARG_FIXED(1)`, this
list of arguments:

```
free arg -a one two three -a again four
```

will result in having the following mappings:

* `["a"]` : again
* `[""]` : free arg two three four

Multiple uses of the same option override the previous value, while free
arguments are collected from all positions.

Two option-like arguments are treated special way:

* `--` - ends the current argument series and makes all following
arguments non-option, even if they start with a dash

* `-/` - ends the current argument list if more optional arguments
are expected (`ARG_VAR` and `ARG_OPT`), that is, any non-option argument that
follows it is considered free

Arguments for options can be specified in two ways:

* As arguments
* Attached to the option as a single string through a separator

This second option allows to specify the argument like `-a:one`, which
would be the same as `-a one`, including multiple arguments, like
`-resolution:1920:1080`. The separator is one of: colon, comma or space,
and it can be also configured.

Note that if a user uses this method, then arguments for this option are taken
exclusively from this set and no arguments will be consumed from the next ones.
For example, if the "a" option is `ARG_VAR`, then

```
free1 free2 -a one two three f1 f2
```

You get

* `[""]`: free1 free2
* `["a"]`: one two three f1 f2

unless you block some by

```
free1 free2 -a one tro three -/ f1 f2
```

which results in

* `[""]`: free1 free2 f1 f2
* `["a"]`: one two three

and the same can be achieved by

```
free1 free2 -a:one:two:three f1 f2
```

or

```
free1 free2 -a" one two three" f1 f2
```

**UNSUPPORTED (YET)**

1. Grouped single-character options are not supported.

2. Cumulative argument options are not supported (only override).


Mode: free options
==================

Free way means that you don't provide definitions of the options. All that
this facility does then is that it grabs options from the command line and
provides you access to this. This has consequences, though:

* All option's arguments are of the same type, default is `ARG_NONE`. This
can be changed using `default_arg` method, although values can be still
specified through separators.

* No option is unknown for the facility, nor are any syntax errors checked

If you decide for this mode, all you need is to create the `OptionHandler`
type variable, and call `process` on it. Options will be then available
through this call:

```
opts.getfree(default_value, "a", "-acquire");
```

or

```
opts(default_value, "a", "-acquire");
```

Here you have two arguments for a case when the option can be `-a` or
`--acquire`. If you only define one form of an option, you can use `getfree1`
with this value, or simpler `operator[]`:

```
opts["a"]
```


Mode: option scheme
===================

To define the options' argument syntax, you can use the symbolic tags with
assigned types and help text. This should be done after declaring
`OptionHandler opts;`:

```
OptionName O_ACQUIRE (opts, OptionScheme::ARG_FIXED(1), "<n> Acquire n targets", "a", "-acquire");
OptionName O_ACQUIRE (opts, "<n> Acquire n targets", "a", "-acquire");
```

This way you define the symbol that can be assigned to multiple option forms
with also the argument specification, together with the help text. The second
line is the same thing, except that it tries to guess the argument specification
from the help text's first character:

* SPACE: This defines `ARG_NONE`
* `[` : This defines `ARG_VAR`
* `<` : This defines `ARG_FIXED(1)`, unless followed by `...>`, which is `ARG_VAR`

Note that there's no possibility to define non-1 fixed number of arguments of a
certain maximum of optional arguments.

Remaining arguments are option names, which can be multiple alternative names
for the same option.

If you use the scheme, you should be able to use `get` or `operator[]` with
the tag to obtain the value assigned to the option:

```
opts.get(O_ACQUIRE, false)
opts[O_ACQUIRE]
```

By using the schemes you have a possibility to define argument number for every
option individually, you can check at once options with different alternative
names, check the status of any syntax errors, and easily display the help text
for the option:

```
for (auto& option: opts.options())
{
    cerr << option.helpitem() << endl;
}
```


True by existence
=================

If you think that your option is only to change some boolean state to invert
the default value, you can recognize things by the fact that the option is
present; you can do it by:

```
opts.exists(OPTION);
```

Here `OPTION` can be specified either as a tag or string.


Accessing options
=================

Arguments are assigned to an option through the `std::vector<std::string>` type.
The API allows this to be further adopted to the users' needs. The accessing
methods return a proxy object, which allow having something like "overloading by
return type" trick. Types supported for this API are:

* `std::vector<std::string>` : arguments are taken as originally packed
* `std::string` : arguments are joined by a single space
* boolean : Converts the value to `bool` type, while:
   * The value is false if the option is not specified
   * "no", "false", "off" and "0" values are false
   * All other cases return true
* any integer type : the option is forced conversion through `stoi` (exceptions!)

All you need to do is to simply:

```
int speed = opts["s"];
```

The proxy object conversion is also a convenience method - you can also use it
explicitly by:

```
opts["s"].as<int>()
```

This doesn't check that the option doesn't exist, and the default value in this
case is the type's default. To use another default value, use:

```
opts["s"].as_default<int>()
```

which will do the same as automatic conversion, just explicitly.

The conversion to a number will also result in exception of `NumberError` type,
if the specified argument cannot be converted to a number.


Configuration and properties
============================

The following properties are available (methods that can accept or
return a value) on `OptionHandler` type:

* `default_arg`(RW): default option argument for options without scheme

* `separators`(W): change separator characters for glued-in
arguments

* `unknown`(R): In the schemed version, contains all options that were
not present in the scheme; such options by default grab no arguments
from the command line - the application may decide to bail out if
any such option has been specified

* `params`(R): Returns the complete map of all options

* `options`(R): Returns the option scheme array

