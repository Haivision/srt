#*
#* SRT - Secure, Reliable, Transport
#* Copyright (c) 2020 Haivision Systems Inc.
#* 
#* This Source Code Form is subject to the terms of the Mozilla Public
#* License, v. 2.0. If a copy of the MPL was not distributed with this
#* file, You can obtain one at http://mozilla.org/MPL/2.0/.
#* 
#*/
#
#*****************************************************************************
#written by
#  Haivision Systems Inc.
#*****************************************************************************

# This is an example configuration file.

# All variables have loggers_ prefix. All those variables can be
# then used inside the generated file pattern.

# Logger definitions.
# Comments here allowed, just only for the whole line.

# The content is line-oriented!

# Structure: { name-id display-id help comment follows } where:
# * name-id: Identifier that can be used to obtain the FA identifier (internal)
# * display-id: This FA will be displayed in the log header with the prefix (see below)
# * remaining text: a description to be placed in a commend
set loggers_table {
	external   ex   External functionality
	internal   in   Internal functionality
}

# NOTE: display-id (ex, in here) will be used in various facilities.

# This will be used to construct the variable name. The
# display-id field will be used, followed by this one.
# For example, with this suffix, the "external" FA the logger will be "exlog"
set loggers_varsuffix log

# OPTIONAL, you may want to create also a link to the general
# logger, for convenience. This time it's a full name of a variable
set loggers_generallink gglog

# This is the prefix when displaying the FA in the log header.
# For example, for 'external', the prefix will be "LF.ex"
set loggers_prefix "LF."

# The namespace where the global logger variables and the logger config will
# be defined. Use dot separation rather than ::.
# For example, this one below will be my::ns namespace.
set loggers_namespace my.ns

# Name of the config object where the loggers will be subscribed.
# This will be the function name that returns the logger config
# object as a singleton.
set loggers_configname logconfig

# Whether all loggers should be enabled or disabled by default
# Note that this doesn't touch upon the general logger.
set loggers_enabled true

# Name of the generated header and source file (.cpp and .h suffixes
# will be added to this name). May enclose the directory name.
set loggers_modulename example_logfa_file

# This contents will be pasted into the generated header and source
# file in the beginning.
set loggers_globalheader {

// This is an example HVU Logger's generated file.

}
