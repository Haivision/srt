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

Parts of the project written in C (haicrypt module and the C API for SRT) use
the C90 standard (ISO/IEC 9899:1990), AKA "ANSI C".

The SRT library is written in C++03 standard (ISO/IEC 14882:2003), which is
equivalent to C++98.

Any applications and support tools use at least C++17 standard (ISO/IEC
14882:2017).


# Technical syntax variants

Detailed configuration for formatting should be provided as a configuration for
the `clang-format` tool, although there are several things worth highlighting:

## 1. Braces (curly brackets) and indents

The convention used in the code in most cases is that the open brace should
start in the new line, indented to the exact column as the keyword starting the
statement, for which the braces were used, or to the indent column used in the
previous line in case of free-form blocks:

```
for (int i = 0; i < 10; ++i)
{
    if (interactive())
        request_input();

	{
		scoped_lock lk (m_Mutex);
    	process(i);
	}
}
```

Exceptions:

* Brace-enclosed initializers should use open brace at the end of line
* Expressions that can fit in one line:
    * include both open and closed brace
    * are allowed only with initializers and function body (including lambda)

Indentation rules:

1. No tab (HT) characters are allowed anywhere in the source. Any alignment and
indentation should be done exclusively using spaces.

2. A single indent level is 4 space characters.

3. A single instruction underlying the structural statement, as well as the
whole block is indented by one indent level. Exceptions:

   * In the `switch` statement, case labels are aligned to braces' column

   * Indent layout in the brace-enclosed initializers must have at least one
level of indentation, but it can be also laid out freely if needed

4. Indentation of the broken-down expression (conditional or function call)
should count for at least 2 levels towards the column beginning the
instruction. In case when it contains argument passing, it should be preferred
to keep a single column alignment for all arguments starting in a new line.

```
int result = very_long_function_name(some_longer_expression(),
        another_expression()); // <-- 2 indent levels
```

(See also 5. Long expression breakdown.)


## 2. Symbolic type modifiers

The type modifiers: pointer and reference (`*` and `&`) should follow the type
name with no space, and a space should be directly after the modifier.

No space before the `[]` symbol nor inside the brackets.

Spaces inside the `<...>` template declarations are used only if the contents
enclose the nested template declaration. This is required in C++03 in case when
closing angle bracket is to be typed twice.

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

No spaces used on the outer side of bracket-like symbols `()` `[]` or `<>`,
with some exceptions shown below. Spaces are placed on the external parts of
the parentheses of expressions, but this is usually due to spaces around
operators:

```
// spaces only here-|-|--|-|
int a = numberRows() * (1 + col); // <- not for functions, but expr
```

A space is required before open parentheses in the following cases:

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
        switch (x)
```

(Note that `return` doesn't require any parentheses.)

Symbols around which spaces are never used are:

* Square and angle brackets: `[]` `<>`
* Scope operators: `.`, `->`, `::`
* All unary operators (such as `*` `&` `++` etc.)


## 5. Long expressions breakdown

In any case when a long expression must be split into multiple lines, the
following rules should be observed:

1. At least 2 levels of indent, unless a smaller indent makes the argument
fit the right column:

```
{
    int a = Statement(x,
    longer_args); // WRONG!

    int a = Statement(x,
        longer_args); // NOT acceptable (<2 indents and not aligned to column)

    f(bnd,
      another,
      yet_another); // acceptable <2 indents because fits the function call column

    int a = Statement(x,
            longer_args); // Ok, although align to the argument is preferred

    int a = Statement(x,
                      longer_args); // Preferred, but the above is still ok.
}
```

2. The symbols and operators position in the breakdown:
   * Always at the end of line: `,` and `;`
   * Binary operators are in the beginning of line
   * Unary operators shall never be in the breakdown
   * The `?:` operator parts shall be in the beginning of line
   * Binary for already aligned argument needs one more indent level

In short, if you have nested expression, the indentation must at least
follow the nesting level - the part of the expression, which is a part
of a nested expression, which's head is already indented, requires an
extra indentation level.

Short example:

```
{
    int a = Fall(a, b, // Comma at the end
                 short_aligned(x) && encoded(x)
                     && encrypted(x), // Binary, argument aligned
                 ++ncalled); // Unary, never broken down
	int ret = short_call(x, y)
            ? 0
            : -1; // Can be also in one line, but ? must be first
            
}
```

Note: column-alignment with the argument position in the line above is
preferred, but not obligatory, and it should be chosen what is the clearest in
particular case. Important is only to keep the line side of the operators and
symbols.


# Conditionals

## 1. Argument order in comparison operators

There has been previously used a convention for conditional inversion
(AKA _Yoda-conditions_) and some examples of it can still be found in
the source files. This should be no longer followed. More about that
is discussed below in (EXPLANATIONS: (1)).

Every condition must contain parameters in the logical sense representing
the terms in this order:

```
TESTED_VALUE COMPARISON_OPERATOR PATTERN_VALUE
```

When you think that the roles of the expressions are equivalent and it's
hard to select which is tested and which is pattern, then it likely doesn't
matter, although still try to form the expression such a way that would best
declare your intentions.

Inversion is allowed only in one case: when a function call is expected to
return some integer (or symbolic) value of special meaning and therefore the
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
that the following condition is an error handler (or, conversely, __isn't__
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


## 2. Non-boolean conversions

C++ allows for implicit conversion to `bool` of the pointer and integer
values - mind though that many such convention may result in a misleading
code. Observe the **logical** interpretation of the expression: it must
"sound" positive or negative according to the intention:

1. The expression is "negative", understood as "failure", "unexpected"
or "unwanted" (requiring extraordinary or exceptional handling) if:

   * It uses the `!` operator
   * It uses the `!=` comparison
   * The `==` is used to compare against -1
   * The `==` is used to compare against a constant that "sounds like failure"

The use of `==` operator shall normally express a positive condition, however
it's generally acceptable for it to sound negative if compared against:

   * -1: this is a POSIX convention, generally understood as error
   * A constant with "ERROR", "FAILURE" or "INVALID" in the name

HOWEVER: Comparison to 0 may be ambiguous. Therefore use `== 0` only for cases
when it means success. If a function returns 0 on failure, prefer implicit
conversion to bool, or compare it as not equal to erroneous value. If the
erroneous value is "any negative value", use `< 0`.

2. And it is considered "successful" or "expected" if:

   * If the expression is implicitly converted to `bool`
   * It uses the `==` operator (with any value that doesn't look like failure)

Correct examples:

1. A function returning 0 or 1 that meant boolean: implicit conversion is ok,
it is even preferred to use `!` rather than `== 0`.

2. A value that is 0 on error and any other value is successful: also implicit
conversion to `bool` is ok.

3. A pointer is "ok", if it's not NULL, so NULL converts to false: implicit
conversion allowed, like `if (!p)` or `if (p)`.

**DO NOT** use implicit boolean expression in case when it is misleading, like:

* A system function that only returns 0 and -1 with the meaning of success and
failure respectively. For example: `if (close(sock))` suggests a success. The
correct use is `if (close(sock) == -1)` or `if (-1 == close(sock))`.

* A 3-way comparison function (such as `strcmp`), which returns 0 if compared
arguments are equal: `if (!strcmp(x, y))` uses `!` operator which suggests
an unexpected situation, while the condition is positive: "x and y are equal".
The best correct usage is `if (0 == strcmp(x, y))`.

* A function that returns a `bool` type, where `true` means that it failed.
In this case implicit conversion is ok, but only if the name of the called
function clearly indicates that it checks for some negative condition, like
`if (call_failed(fn))`. Cases like `if (catchcall(fn))` still sound positive.
In this case prefer an intermediate variable: `bool passed = !catchcall(fn)`,
then `if (!passed)...`.


In all the above expressions there's required an explicit comparison, and it
is also preferred that the pattern value is on the left, which improves
clarity. For example:

```
    // The use of `!=` operator clearly states an unexpected "non-equality"
    if (0 != strcmp(a, b)) ...

    // Returns -1, which means failure
    if (-1 == fcntl(... 

    // The "INVALID" phrase clearly states that it is an error
    if (INVALID_SOCK == (s = socket(...
```


# Constness

The `const` modifier should be used everywhere where applicable, that is:

1. When defining a class's method that is not going to modify
the object's state (SEE BELOW!), this one should be declared as `const`.

2. If a class's method that only reads the object's data is not marked `const`
just because it requires exceptionally mutable access to some fields - very
often mutex objects are in this situation - then prefer to mark these
exceptional fields with `mutable` modifier, while read-only methods should
still use `const`.

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
    * passing a single variable to a function
    * passing a pointer to an array to be filled by the function
    * passing variable or array to an external library's function
    * assigning a pointer to a field in a structure to be filled at a call
	* passing any expression that resolves to a mutable pointer or reference

Clarification: passing a pointer to an object, which is intended to be modified
by the call, shall also be passed in extra parentheses, both in a situation when
it's the object to be modified and if the pointer variable itself is passed to
be written to - even though these situation are not clearly distinguished.

The only case when this rule is not in force is when the effective reference to
an object is on the left side of the assignment-type operators, or it's passed
as an initialization expression for a reference variable.

Examples:

Calls:

```
int bytespeed = getSpeed((packetspeed));  // packetspeed will be filled

//Definition:

int getSpeed(int& w_packetspeed)
{
	w_packetspeed = packets() / time(); // <--- here is the "function-external" variable written

	return (avg_pkt_size() * packet()) / time();
}

getData((packet.m_pcData), (data_size)); // filling an array and size, too

char* res = fgets((data), size, stream); // standard library also fills

msghdr mh; // external structure

mh.msg_iov = (data); // will fill this, when called
...
recvmsg(sock, (mh), 0); // mh will be filled and ALSO ATTACHED OBJECTS.

```

4. The variable passed to a function by reference must be __always
initialized__, even if the designated function is going to fill in the
designated object from scratch. This is because conditions as to whether
uninitialized objects are accepted by a function may change in time
and one change here intended to be only in one call case can potentially
cause big problems in all other call cases.

5. Passing by mutable pointer is allowed only in case when you need a
variant case with a possibility to pass NULL there. If the symbol through
which the object is being passed is never intended to be NULL, always
use reference.

This convention should not be used when passing an object that is __not__
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
local variables) use `SCREAMING_SNAKE_CASE`. Local constants shall use
this convention only if it's a shortcut to other constants; runtime
constants shall still be named like variables.

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
* Optional suffix `_[unit]`: designates a unit (in specific cases)

Possible marker values (`[mk]` part):

1. Empty. Use it always if nothing from the fixed list is appropriate.

2. Integer markers:

   * `z`: usually it's `size_t` type and designates the finite number or
integer size: `m_zNumberElements`

   * `ll` marker designates a signed 64-bit integer: `m_llDistance`. It is
important to highlight this when the value is dealing with others
of different size.

   * `i` marker designates a 32-bit integer type, usually `int` (this is
true also on 64-bit systems)

   * `u` before the integer marker designates an unsigned integer type.
Either used alone (for `unsigned int`) or with `ll` (for `uint64_t`).  This
designation is very important when used in expressions that mix signed and
unsigned integers. Note also that the `z` marker is unsigned by definition.

NOTES:

a. The integer marker is important in case when it's not obvious that
particular field designates something for which number representation is only
one of the possible ones - for example, when it designates a number of
microseconds since epoch.

b. There's no `l` marker in use, as well as there's no use of `long` type, at
least directly, see EXPLANATIONS(4).

3. A variable that designates time should have a marker that states
that it represents time or duration should have the following
markers:

    * `ts` for steady clock (monotonic)
    * `tc` for system clock
    * `td` for duration

    For cases when various different units of time are used for particular domain,
a suffix such as `_us` or `_tk` may be required to designate it, in order to
prevent mistakes with mixing incompatible units.

4. `b`: the `bool` type to represent only on/off value

5. `p`: the pointer, for single objects only (never for an array - see 9.)

6. `s`: a variable of type `std::string` (not an array of characters!).

7. `d`: designates the `double` (floating-point) type. The `float` type is
never used as it's completely useless.

8. `cb`: designates a callback (pointer to function or some more elaborate
wrapper for it).

9. `a` and `ca`: raw array (not any advanced C++ container). The type is
any kind of pointer type (including wrapped one) used as a raw array (not as
a single object). The `ca` marker is for a case of dynamically allocated array
with a lifetime constant size, and `a` in all other raw array cases.

10. The mutexes and condition variables must contain the words
`Lock` and `Cond` respectively, usually at the end. Usually they
have an empty marker, just like objects.



# EXPLANATIONS:

## 1. CONDITIONAL INVERSION (aka _Yoda conditions_)

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
value source wouldn't be modified by the call.

Cases when a variable is passed as such, and it is written to by the
receiving function, is generally unobvious and very often overlooked. The
cleanest approach is to write to variables only through the return value
from the function, and pass only values to read - but that's not always
possible or efficient enough, therefore sometimes you need to pass the
variable to which the function will be writing.

This creates a problem - when you analyze the code, at some point you
have a function call that gets this variable passed, and if you are not
aware that this variable might be written to in this call, you miss an
important point of modification and get false impression that particular
value of this variable comes from somewhere else. Or you lose time with
the need to look deeper into the code just because you have 10 various
calls with this variable, and only one of them was getting it by reference
and writing to it, but you must review all of them to be sure.

The convention should help in this analysis:

1. When you pass a variable to be modified, it is visible by extra parentheses.

2. When a variable has `w_` prefix, and it's written to, you know this is a
parameter through which this value will be effectively returned.

3. When a variable with `w_` prefix is passed to a call with extra parentheses,
you know that this is a reference pass-though.

There was previously tried a solution inspired by C# language with the
use of a `ref_t` type wrapper and a `Ref` helper function, which should
simulate the reference marking. This experiment has eventually failed;
the argument of `ref_t` type was clumsy when used inside the function -
creation of a really transparent reference type in C++ is not possible, as well
as there's no way to prevent implicit conversion from a lvalue to a reference.
The goal was to force a user to use `Ref` wrapper when passing a variable and
report error when attempting to try an implicit conversion to `ref_t` type.
But this one brought more problems than solutions.

The convention with using extra parentheses cannot be enforced by the compiler,
but at least it satisfies all the visibility requirements.


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
fixed-size are preferred, if you intend to have a type of certain size,
although remember that `int64_t` resolves to `long long` on 32-bit systems
and to `long` (!) on 64-bit systems (this causes confusion in case of
format strings). Still, variables of `int64_t` type should have `ll`
prefix, no matter that this type resolves to either `long long` or `long`,
depending on the platform. And still, as for today platofm definitions,
the `int` type can be safely treated as 32-bit, though in order to
highlight the fact that a fixed-size integer is meant, `int32_t` should
be used - especially if you are going to make operations on single bits.

