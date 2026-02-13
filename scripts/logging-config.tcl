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

# Logger definitions.
# Comments here allowed, just only for the whole line.

# Structure: { name-id display-id help-comment } where:
# * name-id: Identifier that can be used to obtain the FA identifier (internal)
# * display-id: This FA will be displayed in the log header with the prefix (see below)
# * help-comment: Unused currently, visible only in this table for now.
set loggers_table {
	sockmgmt   sm   "Socket create/open/close/configure activities"
	conn       cn   "Connection establishment and handshake"
	xtimer     xt   "The checkTimer and around activities"
	tsbpd      ts   "The TsBPD thread"
	rsrc       rs   "System resource allocation and management"
	congest    cc   "Congestion control module"
	pfilter    pf   "Packet filter module"
	api_ctrl   ac   "API part for socket and library management"
	que_ctrl   qc   "Queue control activities"
	epoll_upd  ei   "EPoll, internal update activities"

	api_recv   ar   "API part for receiving"
	buf_recv   br   "Buffer, receiving side"
	que_recv   qr   "Queue, receiving side"
	chn_recv   kr   "CChannel, receiving side"
	grp_recv   gr   "Group, receiving side"

	api_send   as   "API part for sending"
	buf_send   bs   "Buffer, sending side"
	que_send   qs   "Queue, sending side"
	chn_send   ks   "CChannel, sending side"
	grp_send   gs   "Group, sending side"

	internal   in   "Internal activities not connected directly to a socket"
	que_mgmt   qm   "Queue, management part"
	chn_mgmt   km   "CChannel, management part"
	grp_mgmt   gm   "Group, management part"
	epoll_api  ea   "EPoll, API part"
}

# This is the prefix when displaying the FA in the log header.
# For example, if your display-id is "cc", this will be "${loggers_prefix}cc"
set loggers_prefix "SRT."

# The namespace where the global logger variables and the logger config will
# be defined. Use dot separation rather than ::.
set loggers_namespace srt.logging

# Name of the config object where the loggers will be subscribed.
set loggers_configname logger_config

# Whether all loggers should be enabled or disabled by default
set loggers_enabled true

# This will be used to construct the variable name. The
# display-id field will be used, followed by this one.
set loggers_varsuffix log

# OPTIONAL, you may want to create also a link to the general
# logger, for convenience.
set loggers_generallink gglog

# Name of the generated header and source file (.cpp and .h suffixes
# will be added to this name). May enclose the directory name.
set loggers_modulename srtcore/logger_fas

# This contents will be pasted into the generated header and source
# file in the beginning.
set loggers_globalheader {
 /*
  WARNING: Generated from ../logging/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */

}
