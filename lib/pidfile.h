#ifndef LIB_PIDFILE_H
#define LIB_PIDFILE_H
#include <boost/filesystem.hpp>
namespace bfs = boost::filesystem;

class pidfile {
 public:
  explicit pidfile(bfs::path pid_filepath);
  ~pidfile();
  pid_t get_pid() const;

 private:
  bfs::path pid_filepath;
  pid_t pid;
};

#endif  // LIB_PIDFILE_H
