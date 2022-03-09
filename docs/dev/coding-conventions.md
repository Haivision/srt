# PREFACE

This document describes a set of conventions that need to be honored during
development of this project. Note that not necessarily the whole code
currently follows all these conventions - but this is the case only of
the older code that wasn't yet fixed. These conventions should be followed
in any newly written code.

These conventions are important for two reasons:

1. If something can be written using various different variants as allowed
by the language syntax, only one should be used overall in the code because
otherwise readers get easily confused.

2. Readability is most important when doing source code analysis for fixing
bugs and finding the best way of adding a new feature. This is then essential
for minimizing the time spent on that effort.

There could be cases that sometimes it is required to break the convention, but
if so, it should be an exception with a reasonable explanation that it supports
the above principles better. If it doesn't contribute to the code this way, the
convention shall be followed.


# The language standard

The C language, where it is applicable, is expected to be supported as C90
standard (ISO/IEC 9899:1990, AKA ANSI C). This concerns both the subprojects
(haicrypt) and examples written in C, as well as the C API of the SRT library.

The SRT library sources (sources for libsrt) written in C++ should use the
C++03 standard (ISO/IEC 14992:2003, which is C++98 with minor description fixes).
This is required to support development projects with toolchains using very old
compilers that do not support newer standards.

The application support files (or library) as well as support and development
applications, as well as C++ examples, should use C++11 standard (ISO/IEC
14882:2011 - this may change to a higher standard in future).


# Technical syntax variants

Detailed configuration for formatting should be provided as a configuration for
the `clang-format` tool, although there are several things worth highlighting:


## 1. Braces (curly brackets)

The convention used in the code in most cases is that the open brace should
start in the new line, indented to the exact column as the keyword starting the
statement, for which the braces were used, or to the indent column used in the
previous line in case of free-form blocks.

Exceptions:

* Brace-enclosed initializers should use open brace at the end of line
* Expressions that can fit in one line:
    * include both open and closed brace
    * are allowed only with initializers and function body (including lambda)


## 2. Symbolic type modifiers

The pointer (`*`) and reference (`&`) modifiers placed after the type should
immediately follow the type name without a space, and they themselves should be
followed by a space. Also there should be no space between the symbol name when
defining an array and the open bracket, nor inside the brackets.

Example:
```
int fn(const char* name, int* len);
int f2(int ra[], size_t& size);
```

## 3. Binding multiple expressions

This concerns two things:

* Multi-declaration
* Multi-modification

Multi-declaration is a declaration like:

```
int a, b;
```

Multi-declarations are allowed only in case when the type name doesn't have
any symbolic modifiers (pointer, reference, array), and even in this case they
are discouraged. Use rather `typedef` in case when declaring multiple variables
by one declaration was necessary to not repeat a long type name.

Multi-modifications are instruction expressions with:

* Multi-assignment - like `a = b = c;`
* More than one modification in one instruction caused by operators

It is allowed to make an expression with postfix incrementation and
decrementation operators only if the "previous value" is directly
assigned to another variable. It is not allowed to use this in any
other kind of expressions.

```
list<Data>::iterator x = i++; // OK
int y = a + b++; // WRONG.
```

Note that modifying and reading the same object is considered undefined
behavior in C++ anyway.


## 4. Spaces around symbol characters

Most of the binary operators require spaces around themselves (note:
not unary operators using the same symbol characters).

Spaces are usually not used with parentheses (notably with function call
expressions as well as declarations). Usually, however, spaces are
placed on the external parts of the parentheses of expressions, but
this is usually due to spaces around operators:

```
// spaces only here-|-|--|-|
int a = numberRows() * (1 + col); // <- not for functions, but expr
```

A space is required before open parantheses in the following cases:

1. When passing parameters to a local variable constructor:

```
// space here ----\
Condition cc_write (cg_write);    // <- variable construction
```
Note that this doesn't concern a pure constructor call - this is allowed to
look exactly like a function call:

```
// no space here-\
Acquire(Condition(cg_write));     // <- function call with constructor-call
```

2. When a parenthesis is a part of a keyword-based statement:

```
// space here \
            if (good) ...
         while (stillGood()) ...
           for (int i = 0; i < 10; ++i) ...
        sizeof (int)
```

Symbols around which spaces are never used are:

* Square brackets: `[]`
* Field dereference operators `.` and `->`
* All unary operators (such as `*` `&` `++` etc.)



# Conditionals

There has been previously used a convention for conditional inversion
(AKA *Yoda conditions*) and some examples of it can still be found in
the source files. This should be no longer followed. More about that
is discussed below in (EXPLANATIONS: (1)).

Every condition must contain parameters in the logical sense representing
the terms in this order:

```
TESTED_VALUE COMPARISON_OPERATOR PATTERN_VALUE
```

When you think that the roles of the expressions are equivalent and it's
hard to select which is tested and which is pattern, then it possibly
doesn't matter, although still try to form the expression such a way
that would best declare your intentions.

Inversion is allowed only in one case: when a function call is expected
to return some integer (or symbolic) value of special meaning and therefore the
logical meaning of the whole operation is composed out of the function name
together with a special return value. 

Examples:

1. The `strcmp` function is an example where you don't know exactly
what is going to be checked in the conditional expression using it,
unless you know from upside to which value the result is compared.

Consider:

```
//Will compare... .  .  .  .  .  .  .  .  .  .  .  .  .  for equality
   if (strcmp(get_string_from_somewhere(), another_string) == 0)
```

Against:

```
//For equality will compare: (shorter: "check if equal:")
      if (0 ==      strcmp(get_string_from_somewhere(), another_string))
```

2. A POSIX call that returns -1 as an error report can be more visible
that the following condition is an error handler (or, conversely, **isn't**
an error handler and is executed only on success), if the function name
and the return value are close to one another. Consider:

```
// If bind() operation  .  .  .  .  .  .  .  .  failed
   if (bind(sock, (sockaddr*)&sa, sizeof sa) == -1)
```

Against:

```
// If failed bind() operation . . . . .
   if (-1 == bind(sock, (sockaddr*)&sa, sizeof sa))
```

This is important as during the review you usually focus on under which
exactly condition the following block is executed, less on what exactly
is passed to the function. It saves time and effort of the reviewers
if both things are collected in one place rather than dispersed
throughout the whole line.


# Constness

The `const` modifier should be used everywhere where applicable, that is:

1. When defining a class's method that is not going to modify
the object's state (SEE BELOW!), this one should be declared as `const`.

2. When a class contains some utility objects (such as counters or
mutexes) that do not contribute to the object's state as such, these
fields should have a `mutable` modifier (meaning: a need to modify
such an object shall not be an excuse for not having `const` in
an otherwise not-state-modifying function). For example, a class that
protects an internal data with a mutex should have a `mutable` mutex
and therefore a method that reads the value, but requires a mutex to
be locked, can still be const, even though it needs to perform a
mutable operation on a mutex.

3. A local variable that is declared as a value-shortcut to be used
in further evaluation, but its value is never intended to be changed,
shall be declared `const`.

4. When some object, passed by pointer or reference and the function,
is not intended to be modified, it must be a `const` pointer or reference.


# Passing parameters by pointer or reference

Reference parameters in functions use a very specific convention, a
further explanation is provided below in (EXPLANATIONS (2)).

Note that these rules apply only to cases of passing a function a non-const
reference or pointer, and only if it is intended to modify the underlying
object.

This splits into the following parts:

1. Function declaration: just use the pointer or reference type.

2. Function definition: every parameter that is of mutable reference
type must have the `w_` prefix. In case of pointers, the prefix is `pw_`,
or if the pointer is intended to get passed a raw array to fill, the
prefix is `aw_`.

3. When a function is called, and it gets parameters passed by mutable
pointer or reference, the expression, that results in the actual pointer
or reference to be passed to the function, must have extra parentheses
around itself. This embraces almost all cases, including:
    * passing a variable to a function
    * passing a pointer to an array to be filled by the function
    * passing variable or array to an external library's function
    * assigning a pointer to a field in a structure to be filled at a call

This rule is not in force only for left-side parameters for operators, nor in
case of the reference-initialization.

Examples:

Calls:

```
int bytespeed = getSpeed((packetspeed));  // packetspeed will be filled

//Definition:

int getSpeed(int& w_packetspeed)
{
	w_packetspeed = packets()/time(); // <--- here is the "function-external" variable written

	return (avg_pkt_size()*packet())/time();
}

getData((packet.m_pcData), (data_size)); // filling an array and size, too

char* res = fgets((data), size, stream); // standard library also fills

msghdr mh; // external structure

mh.msg_iov = (data); // will fill this, when called
...
recvmsg(sock, (mh), 0); // mh will be filled and ALSO ATTACHED OBJECTS.

```


4. The variable passed to a function by reference must be **always
initialized**, even if the designated function is going to fill in the
designated object from scratch. This is because conditions as to whether
uninitialized objects are accepted by a function may change in time
and one change here intended to be only in one call case can potentially
cause big problems in all other call cases.

5. Passing by mutable pointer is allowed only in case when you need a
variant case with a possibility to pass NULL there. If the symbol through
which the object is being passed is never intended to be NULL, always
use reference.

This convention should not be used when passing an object that is **not**
to be modified inside the call. There could be also cases when the passed
object is not intended to be written, but the rule of having const there was
needed to be broken. In that case also the rules of the prefix and extra
parentheses do not apply.


# Naming convention

Naming convention is the following:

1. Global entities should follow the "Microsoft style" (aka. `PascalCase`).
This includes class names and global functions.

2. Class's methods names should follow "Java style" (aka. `camelCase`).
There's no special requirement to highlight static methods.

3. Local variables usually use lowercase, in case when word separation
is needed, use `snake_case`.

4. Constants used anywhere among existing entities (except constant
local variables) use `SCREAMING_SNAKE_CASE`.

5. Class's fields and global variables use the "Fields' naming convention"
as described below.


# Fields' naming convention

This convention uses a special form of the Hungarian Notation. The
motivation is explained in (EXPLANATIONS (3)).

The general syntax for the field name is `[pfx][mk][mkx][name]`:

* pfx: Field prefix: `m_` for members, `s_` for static, `g_` for global
* mk: The marker (can be also empty) - lowercase only
* mkx: Optional extra marker for specific cases:
    * for fields bound to socket options: `OPT_`
* name: field name using `PascalCase`
* Optional sufix `_[unit]`: designates a unit (in specific cases)

Possible marker values (`[mk]` part):

1. Size: `z` marker defines that the variable designates a size of a
container or count of some finite elements and is using an unsigned type
designed for keeping a size (usually it's `size_t`): `m_zNumberElements` 

2. Integer type with specific size used to implement the field:

    * `ll` marker designates a signed 64-bit integer: `m_llDistance`. It is
important to highlight this when the value is dealing with others
of different size.

    * `i` marker designates a 32-bit integer type. Although usually there's
`int` type meant here, in all today compilers `int` is a 32-bit type,
even on 64-bit systems. The real type of this variable could be `int`
or `int32_t`.

    * `u` before the integer marker designates an unsigned integer type.
The integer type must always have a designation of signed or unsigned
type, while it is signed by default. It is important when used in
expressions that mix signed and unsigned integers. Note also that
if the variable is to designate a size, it better use `size_t` type
and `z` marker therefore.

    * Note that integer marker is important in case when it's not obvious
that particular field designates something for which number representation
is only one of the possible ones - for example, when it designates
a number of microseconds since epoch.

    * Note: there's no `l` marker in use, as well as there's no use of
`long` type, at least directly, see EXPLANATIONS(4).

3. A variable that designates time should have a marker that states
that it represents time or duration should have the following
markers:

    * `ts` for steady clock (monotonic)
    * `tc` for system clock
    * `td` for duration

    For cases when various different units of time are used for particular domain,
a suffix such as `_us` or `_tk` may be required to designate it, in order to
prevent mistakes with mixing incompatible units.

4. Boolean type: `b` marker declares that the field represents only
the on/off character of the designated value.

5. The `p` marker designates a pointer. The pointer is usually for
a bigger object and that one needs no futher markers. Note that this
is only when you intend to keep only a single object here, see also
p. 10.

6. The `s` marker marks a variable of type `std::string` (not a
bare array of characters).

7. The `d` marker designates the `double` (floating-point) type.
The `float` type is never used as it's completely useless.

8. The `cb` marker designates a callback (pointer to function or
some more elaborate wrapper for it).

9. The `a` and `ca` markers define a raw array (note: not any advanced C++
container). This marker is **independent** of the real type used to implement
it (note: `p` marker just because it's a pointer type, is wrong, if the field
actually designates an array). The `ca` marker is used in a special case when
the field holding it is of pointer type and the array is to be dynamically
allocated, but the size of the array doesn't change during the whole lifetime
of the object containing it. In all other cases it should be `a`.

10. The mutexes and condition variables must contain the words
`Lock` and `Cond` respectively, usually at the end. Usually they
have an empty marker, just like objects.



# EXPLANATIONS:

## 1. CONDITIONAL INVERSION (aka *Yoda conditions*)

This is a technique that had to be advantegous in mistake prevention,
however in the end it was proven to cause more harm and trouble. This
is further discussed
[here](https://sektorvanskijlen.wordpress.com/2019/05/16/conditional-inversion-very-harmful-myth/).


## 2. REFERENCE PASSING

The overall problem with reference passing is not exactly with distinction
between reference and value passing, but with the fact that the unit being
passed to a function is a variable (or object) that the function will
potentially modify. In case when you pass by value (copy), or even through
a constant pointer or reference, this doesn't matter, as the designated
value source woudn't be modified by the call.

Cases when a variable is passed as such, and it is written to by the
receiving function, is generally unobvious and very often overlooked. A
function shall better not get parameters by reference and rather return
a value that would be intended to be written into a variable (so the
assignment operation is clearly visible as modification). However sometimes
this is not enough efficient, especially if some larger objects are to be
modified. For simple types there are some extra abilities that could help here
in a form of tuples, but these are only available in C++11.

This creates a problem - when you analyze the code, at some point you
have a function call that gets this variable passed, and if you are not
aware that this variable might be written to in this call, you miss an
important point of modification and get false impression that particular
value of this variable comes from somewhere else. Or you lose time with
the need to look deeper into the code just because you have 10 various
calls with this variable, and only one of them was getting it by reference
and writing to it, but you must review all of them to be sure.

The convention should help in this analysis:

1. When you pass a variable by reference, it is visible by extra
parentheses.
2. When a variable has `w_` prefix, and it's written to, you know this is a
parameter through which this value will be effectively returned.
3. When a variable with `w_` prefix is passed to a call with extra
parentheses, you know that this is a reference pass-though.

There was previously tried a solution inspired by C# language with the
use of a `ref_t` type wrapper and a `Ref` helper function, which should
simulate the reference marking. This experiment has eventually failed
as the parameter name inside the function that receive it require extra `*`
operator to access the designated reference (or `.get()` method call), as
creating a type that can transparently designate a reference in C++ is
impossible. The goal was to provide a solution that could be verified
by the compiler, but this one brought more problems than solutions. The
convention cannot be enforced by the compiler, but at least it satisfies
all the visibility requirements.


## 3. HUNGARIAN NOTATION IN THE NAMING CONVENTION

Hungarian Notation is usually a method of embracing the type designation
in the variable name. It doesn't mandate exactly what it should be used
for, although while it could be seen usually in various project a very
strict requirement to have an obligatory data type marker, here the
purpose is slightly different - it is to make it clear about the logical
meaning of what the field defines, not exactly their declared type.
Note that datatype-related Hungarian Notation cases can still be found
in the source code.

It is important to have appropriate markers in the field names in all cases
when the meaning can be ambiguous or simply unobvious. If the name of the
variable suggests something that can only be implemented using just one kind of
type, the marker can be skipped. However in most cases it isn't clear enough
from the name, what type was used to implement it, or more in particular, what
their characteristics of use are because of both the type used to implement
it and the way how it is being used in the code. The goal of this rule is to
help prevent misuse of a field due to used unit, relationship character,
compatibility and needed translations to a different unit or character.


## 4. NO USE OF LONG TYPE

The `long` type's size differs on 32-bit and 64-bit systems and therefore it
only makes sense to use it if there's something in the hardware reflecting
this difference. There should be exclusively `int` (or `int32_t`) used for
32-bit integer and `long long` (or `int64_t`) used for 64-bit type. The
fixed-size are prefered, if you intend to have a type of certain size,
although remember that `int64_t` resolves to `long long` on 32-bit systems
and to `long` (!) on 64-bit systems (this causes confusion in case of
format strings). Still, variables of `int64_t` type should have `ll`
prefix, no matter that this type resolves to either `long long` or `long`,
depending on the platform. And still, as for today platofm definitions,
the `int` type can be safely treated as 32-bit, though in order to
highlight the fact that a fixed-size integer is meant, `int32_t` should
be used - especially if you are going to make operations on single bits.

