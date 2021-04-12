# SRT Developer's Guide

* [Development Setup](#development-setup)
  * [Installing Dependencies](#installing-dependencies)
  * [Forking SRT on GitHub](#forking-srt-on-github)
  * [Building SRT](#building-srt)
* [Project Structure](#project-structure)
* [Coding Rules](#coding-rules)
* [Submitting an Issue](#submitting-an-issue)
* [Submitting a Pull Request](#submitting-a-pull-request)
  * [Commit Message Format](#commit-message-format)
* [Generated files](#generated-files)

## Development Setup

This section describes how to set up your development environment to build and test SRT,
and explains the basic mechanics of using `git` and `cmake`.

### Installing Dependencies

Install the following dependencies on your machine:

* [Git](http://git-scm.com/): The [GitHub Guide to
  Set Up Git][git-setup] is a good source of information.

* [CMake](http://cmake.org): v2.8.12 or higher is recommended.

* [OpenSSL](http://www.openssl.org): v1.1.0 or higher. Alternatively Nettle and mbedTLS can be used.

### Forking SRT on GitHub

To contribute code to SRT, you must have a GitHub account so you can push code to your own
fork of SRT and open Pull Requests in the [GitHub Repository][github].

To create a GitHub account, follow the instructions [here](https://github.com/signup/free).
Afterwards, go ahead and [fork](http://help.github.com/forking) the
[main SRT repository][github].

### Building SRT

To build SRT, clone the source code repository and use CMake to generate system-dependent build files:

```shell
# Clone your Github repository to 'srt' folder.
git clone https://github.com/<github username>/srt.git srt

# Go to the SRT directory.
cd srt

# Add the main SRT repository as an upstream remote to your repository.
git remote add upstream "https://github.com/Haivision/srt.git"

# For macOS also export OpenSSL paths with the following commands:
# export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
# export OPENSSL_LIB_DIR=$(brew --prefix openssl)"/lib"
# export OPENSSL_INCLUDE_DIR=$(brew --prefix openssl)"/include"

# Create a directory for build artifacts.
# Note: To create a directory on windows use `md` command instead of `mkdir`.
mkdir _build && cd _build

# Generate build files, including unit tests.
cmake .. -DENABLE_UNITTESTS=ON

# Build SRT.
cmake --build ./
```

**Note.** If you are using Windows, please refer to [Building SRT for Windows](../build/build-win.md) instructions.

**Note.** Please see the following document for the build options: [SRT Build Options](../build/build-options.md).
To see the full list of make options run `cmake .. -LAH` from the `_build` folder.

**Note.** There is an alternative `configure` script provided. It is **NOT** an alternative Autotools build, but a convenience script.
It processes the usual format of `--long-options` and calls `cmake` with appropriate options in the end. This script is dependent on "tcl" package.
Please see the following document for `configure` usage: [SRT Build Options](../build/build-options.md).

The build output is in the `_build` directory. The following applications can be found there.

* `srt-live-transmit` - A sample application to transmit a live stream from source medium (UDP/SRT/`stdin`)
to the target medium (UDP/SRT/`stdout`). See [Using the `srt-live-transmit` App](../apps/srt-live-transmit.md) for more info.
* `srt-file-transmit` - A sample application to transmit files with SRT.
* `srt-tunnel` - A sample application to set up an SRT tunnel for TCP traffic. See [Using the `srt-tunnel` App](../apps/srt-tunnel.md) for more info.
* `tests-srt` - unit testing application.

## Language standard requirements

The following conventions for the language standard are used in this project:

1. The SRT library requires C++03 (also known as C++98) standard.
2. The examples (to be enabled in cmake by `-DENABLE_EXAMPLES=1`) require either C++03 or C89 standard.
3. The following require C++11 standard:
   * demo applications
   * testing applications (to be enabled in cmake by `-DENABLE_TESTING=1`)
   * unit tests (to be enabled in cmake by `-DENABLE_UNITTESTS=1`)

Note that C++11 standard will be enforced if you have enabled applications
and haven't specified the C++ standard explicitly. When you have an old compiler
that does not support C++11 and you want to compile as many parts as possible,
the simplest way is to use the following options (in cmake):

```
-DENABLE_APPS=0 -DUSE_CXX_STD=03 -DENABLE_EXAMPLES=1
```

Note also that there are several other options that, when enabled, may require
that the SRT library be compiled using C++11 standard (`-DENABLE_STDCXX_SYNC=1`
for example).

## Project Structure

The SRT installation has the following folders:

* apps - the folder contains [srt-live-transmit](../apps/srt-live-transmit.md), `srt-file-transmit` and [srt-tunnel](../apps/srt-tunnel.md) sample applications.
* *common - holds some platform-dependent code.
* *docs - contains all the documentation in the GitHub Markdown format.
* *examples - example applications (use `-DENABLE_EXAMPLES=ON` CMake build option to include in the build)
* *haicrypt - encryption-related code.
* *scripts - some scripts including CMake and TCL scripts.
* *srtcore - the main source code of the SRT library.
* *test - unit tests for the library.
* *testing - the folder contains applications used during development: `srt-test-live`, `srt-test-file`, etc. Use `-DENABLE_TESTING=ON` to include in the build.

## Coding Rules

TBD.

## Submitting an Issue

If you found an issue or have a question, please submit a (GitHub Issue)[github-issues].
Note that questions can also be asked in the SRT Alliance slack channel:
[start the conversation](https://slackin-srtalliance.azurewebsites.net/) in the `#general` or `#development` channel on [Slack](https://srtalliance.slack.com).

## Submitting a Pull Request

Create a pull request from your fork following the [GitHub Guide][github-guide-prs].

### Commit Message Format

We use a certain format for commit messages to automate the preparation of release notes.

Each commit must start with one of the following tags, identifying the main scope of the commit.
If your PR contains several distinguishable changes, it is recommended to split them into several commits,
using the described commit message format.

Please note that it is preferred to merge PRs by rebasing onto the master branch.
If a PR contains several commits, they should be in the defined format.
If your PR has several commits, and you need to update or change them, please use `git rebase` to save
the commits structure. An alternative merging strategy is squash-merging, when all commits of the PR
are squashed into a single one. In this case you can update your PR by making additional commits
and merging with master. All those secondary commits will be squashed into a single one after merging to master.

The format of the commit message is `[<tag>] <Message>`, where possible commits tags are:

* `[core]` - commit changes the core SRT library code,
* `[tests]` - commit changes or adds unit tests,
* `[build]` - commit is related tp build system,
* `[apps]`- commit mainly changes sample applications or application utilities, including testing and example applications,
* `[docs]` - commit changes or adds documentation.

## Generated files

Please note *before modifying any files* that some of them are generated. This is
indicated after the file header, or in any section of a file that needs to be replaced by 
generated code if related changes are added. The following sections require attention:

### Logging functional areas

In addition to levels (Debug, Note, Warn, Error, Fatal) the logging system has
functional areas (FA) that allow a developer to selectively turn on only
specific types of logs. For example, in this logging instruction:

```
LOGC(cclog.Note, log << "This is a note");
```

* `LOGC` is the macro, which allows for file and line information pass-through
* `cclog` is the logger variable named after the FA, here "cc" means Congestion Control
* `Note` is the log level
* The expression after the comma is the log text composition expression

The FA system allows a developer to enable or disable printing all logs assigned
to particular functional area. This allows the developer to selectively turn on
only specific areas. This is useful during testing to help minimize the impact
of logging on performance or behavior.

To add a name designating a new functional area to be used in the logs, modify the
`generate-logging-defs.tcl` script. A list of loggers is contained in the
`loggers` list at the top of the TCL file. You can insert an addition anywhere
in this list, as long as it has these three unique elements: a long name, a
short name, and an ID (e.g.` GENERAL   g  0`). The TCL file contains
a`hidden_loggers` list with additional definitions that do not always need to
be added to particular generated files. Alternative declarations for items in
this list are provided in a different way.

To add or rename one of the logger definitions:

* Modify appropriately the `loggers` list
* Run the script to generate the files
   * Note that you need to press Enter to confirm overwrite

Note that the script can have arguments, which is the list of files that have to
be generated (must be identical with the keys used in `generation` dictionary).
By default it regenerates all required files. Note also that `srt.h` is exceptionally
a file that is being only modified, that is, the current contents of the file is
preserved, and only the part replaced.

The script contains also all definitions for file generation in the following variables:

* `special`: contains a code that should be executed for particular target
* `generation`: the definition of the file contents to be generated

Both these are dictionaries, whose keys are "targets". Target names are
names of the generated files. If the name is an explicit path (contains
at least one path separator), it's the relative path towards the repository
top directory, otherwise the file will be generated in the current directory.

The `generation` dictionary should contain complete definition of all files
to be generated. Every entry is an array containing 3 elements:

* format model (can be empty, if the generated file consists only of the list of entries)
* entry format for `loggers`
* entry format for `hidden_loggers` (optional)

The 'format model' uses two variables: `globalheader`, which is the obligatory
header for all source files, and `entries` in a place where the list of functional
area (FA) entries is expected to be placed.

The entry formats should utilize the variable names as defined in the `model` variable
in the beginning, as it sees fit.

Currently generated files are:

* `srtcore/logger_default.cpp`: contains setting of all FA as enabled
* `srtcore/logger_defs.h` and `srtcore/logger_defs.cpp`: declares/defiones logger objects
* `apps/logsupport_appdefs.cpp`: Provides string-to-symbol bindings for the applications

### Build options

If you modify the `CMakeLists.txt` file and add some build options to it, remember to
generate this list of options and update appropriately the `configure-data.tcl` file.
This file should be run in the build directory and passed the `CMakeCache.txt`
file as argument. The list of options will be printed on the standard output, and
it should be the content of the `cmake_options` variable defined in `configure-data.tcl`
file.

Note that this does not mean that the contents should be blindly pasted into
the options list. Apply only the new options that you have added. The script
does its best to make sure that no option is missing. Note that some options
might be provided by an external dependent script (like `build-gmock`) and
therefore mistakenly added to the list.

[git-setup]: https://help.github.com/articles/set-up-git
[github-issues]: https://github.com/Haivision/srt/issues
[github-guide-prs]: https://docs.github.com/en/github/collaborating-with-issues-and-pull-requests/creating-a-pull-request-from-a-fork
[github]: https://github.com/Haivision/srt
