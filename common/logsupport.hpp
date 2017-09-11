#ifndef INC__LOGSUPPORT_HPP
#define INC__LOGSUPPORT_HPP

#include "../srtcore/udt.h"
#include "../srtcore/logging_api.h"

logging::LogLevel::type SrtParseLogLevel(std::string level);
std::set<logging::LogFA> SrtParseLogFA(std::string fa);

UDT_API extern std::map<std::string, int> srt_level_names;

namespace logging { struct LogConfig;  }
UDT_API extern logging::LogConfig srt_logger_config;


#endif
