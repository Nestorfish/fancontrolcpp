#ifndef LIB_PIDFILE_H
#define LIB_PIDFILE_H
#include <boost/filesystem.hpp>
namespace bfs = boost::filesystem;

class pidfile {
 public:
  explicit pidfile(bfs::path pid_filepath);
  ~pidfile();

 private:
  bfs::path pid_filepath;
};

#endif  // LIB_PIDFILE_H
