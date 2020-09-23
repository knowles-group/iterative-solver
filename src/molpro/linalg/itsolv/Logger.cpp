#include "Logger.h"
#include <iostream>
#include <map>

namespace molpro {
namespace linalg {
namespace itsolv {
namespace {
std::map<Logger::Level, std::string> log_level_names{
    {Logger::Trace, "Trace"}, {Logger::Debug, "Debug"}, {Logger::Info, "Info"},  {Logger::DataDump, "DataDump"},
    {Logger::Warn, "Warn"},   {Logger::Error, "Error"}, {Logger::Fatal, "Fatal"}};

} // namespace

void Logger::msg(const std::string& message, Level log_lvl) {
  auto print_message = [&log_lvl, &message]() {
    std::cout << log_level_names[log_lvl] << ": " << message << std::endl;
  };
  if (log_lvl == Trace || log_lvl == Debug || log_lvl == Info) {
    if (log_lvl <= max_trace_level) {
      print_message();
    }
  } else if (log_lvl == Warn || log_lvl == Error) {
    if (log_lvl <= max_warn_level) {
      print_message();
    }
  } else if (log_lvl == DataDump && data_dump) {
    print_message();
  } else if (log_lvl == Fatal) {
    std::cerr << log_level_names[log_lvl] << ": " << message << std::endl;
  }
}

} // namespace itsolv
} // namespace linalg
} // namespace molpro
