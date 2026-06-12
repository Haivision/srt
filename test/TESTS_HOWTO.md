# Rules for writing tests for SRT

## 1. Use automatic startup/cleanup management

Note that most of the test require SRT library to be initialized for the
time of running the test. There are two methods how you can do it:

* In a free test (`TEST` macro), declare this in the beginning:
`srt::TestInit srtinit;`

* In a fixture (`TEST_F` macro), draw your class off `srt::Test`
(instead of `testing::Test`)

In the fixture case you should also use names `setup/teardown` instead of
`SetUp/TearDown`. Both these things will properly initialize and destroy the
library resources.

## 2. Do not misuse ASSERT macros

**Be careful** where you are using `ASSERT_*` macros. In distinction to
`EXPECT_*` macros, they interrupt the testing procedure by throwing an exception.
This means that if this fires, nothing will be executed up to the end of the
current testing procedure, unless it's a destructor of some object constructed
inside the procedure.

This means, however, that if you have any resource deallocation procedures, which
must be placed there for completion regardless of the test result, the call to
`ASSERT_*` macro will skip them, which may often lead to misexecution of the
remaining tests and have them falsely failed. If this interruption is necessary,
there are the following methods you can use to prevent skipping resource cleanup:

* Do not cleanup anything in the testing procedure. Use the fixture's teardown
method for any cleaning. Remember also that it is not allowed to use `ASSERT_*`
macros in the teardown procedure, should you need to test additionally to the
cleanup.

* You can also use a local class with a destructor so that cleanups will execute
no matter what happened inside the procedure

* Last resort, keep the code that might use `ASSERT_*` macro in the try-catch
block and free the resources in the `catch` clause, then rethrow the exception.
A disadvantage of this solution is that you'll have to repeat the cleanup
procedure outside the try-catch block.

* Use `EXPECT_` macros, but still check the condition again and skip required
parts of the test that could not be done without this resource.

# Useful SRT test features

## Test command line parameters

The SRT tests support command-line parameters. They are available in test
procedures, startups, and through this you can control some execution
aspects. The gtest-specific options are being removed from the command
line by the gtest library itself; all other parameters are available for
the user. The main API access function for this is `srt::TestEnv`. This
is a fixed singleton object accessed through `srt::TestEnv::me` pointer.
These arguments are accessible through two fields:

* `TestEnv::args`: a plain vector with all the command line arguments
* `TestEnv::argmap`: a map of arguments parsed according to the option syntax

The option syntax is the following:

* `-option` : single option without argument; can be tested for presence
* `-option param1 param2 param3` : multiple parameters assigned to an option

Special markers:

* `--`: end of options
* `-/`: end of parameters for the current option

To specify free parameters after an option (and possibly its own parameters),
end the parameter list with the `-/` phrase. The `--` phrase means that the
rest of command line parameters are arguments for the last specified option,
even if they start with a dash. Note that a single dash has no special meaning.

The `TestEnv::argmap` is using option names (except the initial dash) as keys
and the value is a vector of the parameters specified after the option. Free
parameters are collected under an empty string key. For convenience you can
also use two `TestEnv` helper methods:

* `OptionPresent(name)`: returns true if the option of `name` is present in the
map (note that options without parameters have simply an empty vector assigned)

* `OptionValue(name)`: returns a string that contains all parameters for that
option separated by a space (note that the value type in the map is a vector
of strings)

## Test environment feature checks

The macro `SRTST_REQUIRE` can be used to check if particular feature of the
test environment is available. This binds to the `TestEnv::Available_FEATURE`
option if used as `SRTST_REQUIRE(FEATURE)`. This macro makes the test function
exit immediately with success. The checking function should take care of
printing appropriate information about that the test was forcefully passed.

To add more environment availability features, add more `TestEnv::Available_*`
methods. Methods must return `bool`, but may have parameters, which are passed
next to the first argument in the macro transparently. Availability can be
tested internally, or taken as a good deal basing on options, as it is
currently done with the IPv6 feature - it is declared as not available when the
test application gets the `-disable-ipv6` option.

It is unknown what future tests could require particular system features,
so this solution is open for further extensions.

