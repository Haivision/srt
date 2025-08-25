/*
  WARNING: Generated from ../logging/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */


#include "logging.h"
#include "logger_fas.h"

namespace srt { namespace logging { 
    hvu::logging::LogConfigSingleton logger_config_si;
    hvu::logging::LogConfig& logger_config() { return logger_config_si.instance();}
    hvu::logging::Logger& gglog = logger_config().general;

    // Socket create/open/close/configure activities
    hvu::logging::Logger smlog("sockmgmt", logger_config(), true, "SRT.sm");


    // Connection establishment and handshake
    hvu::logging::Logger cnlog("conn", logger_config(), true, "SRT.cn");


    // The checkTimer and around activities
    hvu::logging::Logger xtlog("xtimer", logger_config(), true, "SRT.xt");


    // The TsBPD thread
    hvu::logging::Logger tslog("tsbpd", logger_config(), true, "SRT.ts");


    // System resource allocation and management
    hvu::logging::Logger rslog("rsrc", logger_config(), true, "SRT.rs");


    // Congestion control module
    hvu::logging::Logger cclog("congest", logger_config(), true, "SRT.cc");


    // Packet filter module
    hvu::logging::Logger pflog("pfilter", logger_config(), true, "SRT.pf");


    // API part for socket and library managmenet
    hvu::logging::Logger aclog("api_ctrl", logger_config(), true, "SRT.ac");


    // Queue control activities
    hvu::logging::Logger qclog("que_ctrl", logger_config(), true, "SRT.qc");


    // EPoll, internal update activities
    hvu::logging::Logger eilog("epoll_upd", logger_config(), true, "SRT.ei");


    // API part for receiving
    hvu::logging::Logger arlog("api_recv", logger_config(), true, "SRT.ar");


    // Buffer, receiving side
    hvu::logging::Logger brlog("buf_recv", logger_config(), true, "SRT.br");


    // Queue, receiving side
    hvu::logging::Logger qrlog("que_recv", logger_config(), true, "SRT.qr");


    // CChannel, receiving side
    hvu::logging::Logger krlog("chn_recv", logger_config(), true, "SRT.kr");


    // Group, receiving side
    hvu::logging::Logger grlog("grp_recv", logger_config(), true, "SRT.gr");


    // API part for sending
    hvu::logging::Logger aslog("api_send", logger_config(), true, "SRT.as");


    // Buffer, sending side
    hvu::logging::Logger bslog("buf_send", logger_config(), true, "SRT.bs");


    // Queue, sending side
    hvu::logging::Logger qslog("que_send", logger_config(), true, "SRT.qs");


    // CChannel, sending side
    hvu::logging::Logger kslog("chn_send", logger_config(), true, "SRT.ks");


    // Group, sending side
    hvu::logging::Logger gslog("grp_send", logger_config(), true, "SRT.gs");


    // Internal activities not connected directly to a socket
    hvu::logging::Logger inlog("internal", logger_config(), true, "SRT.in");


    // Queue, management part
    hvu::logging::Logger qmlog("que_mgmt", logger_config(), true, "SRT.qm");


    // CChannel, management part
    hvu::logging::Logger kmlog("chn_mgmt", logger_config(), true, "SRT.km");


    // Group, management part
    hvu::logging::Logger gmlog("grp_mgmt", logger_config(), true, "SRT.gm");


    // EPoll, API part
    hvu::logging::Logger ealog("epoll_api", logger_config(), true, "SRT.ea");




} }
