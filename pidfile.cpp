#include "lib/pidfile.h"

#include <unistd.h>

#include <string>
#include <stdexcept>

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
namespace bfs = boost::filesystem;

pidfile::pidfile(bfs::path pid_filepath)
  : pid_filepath(pid_filepath)
{
  pid = getpid();
  if (bfs::exists(pid_filepath)) {
    pid_t old_pid;
    bfs::ifstream(pid_filepath) >> old_pid;
    bfs::path old_comm_path("/proc/" + std::to_string(old_pid) + "/comm");
    if (bfs::exists(old_comm_path)) {
      std::string self_name;
      bfs::ifstream("/proc/self/comm") >> self_name;
      std::string old_name;
      bfs::ifstream(old_comm_path) >> old_name;
      if (self_name == old_name) {
        throw std::runtime_error("Already running!");
      }
    }
    bfs::remove(pid_filepath);
  }
  bfs::ofstream out(pid_filepath);
  out << pid;
  out.close();
}

pidfile::~pidfile() {
  bfs::remove(pid_filepath);
}

pid_t pidfile::get_pid() const {
  return pid;
}
