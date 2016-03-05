#include "lib/fancontroller.h"
#include <unistd.h>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cmath>

/*
 * TODO:
 * Resolve filenames with realpath() or boost::filesystem
 * Finish reading Documentation/sysfs-rules.txt
 */


fancontroller::fancontroller(const std::string &controller,
                             const std::string &fan_sensor,
                             const std::string &temp_sensor,
                             long min_temp, long max_temp,
                             long min_start, long min_stop, long min_speed,
                             long min_pwm, long max_pwm)
  : controller(controller), fan_sensor(fan_sensor), temp_sensor(temp_sensor),
    min_temp(min_temp), max_temp(max_temp),
    min_start(min_start), min_stop(min_stop), min_speed(min_speed),
    min_pwm(min_pwm), max_pwm(max_pwm),
    controller_enabler(controller + "_enable") {
  if (std::ofstream(controller.c_str()).fail())
    throw std::runtime_error("Could not open PWM device!");
  if (std::ifstream(fan_sensor.c_str()).fail())
    throw std::runtime_error("Could not open fan sensor device!");
  if (std::ifstream(temp_sensor.c_str()).fail())
    throw std::runtime_error("Could not open temperature sensor device!");
  write_value(controller_enabler, 1);
}

fancontroller::~fancontroller() {
  try {
    set_full_speed();
  }
  catch (...) {}
}

long fancontroller::read_value(const std::string &path) const {
#if defined(MY_DEBUG)
  std::cerr << "Reading " << path << std::endl;
#endif
  std::ifstream file(path.c_str());
  long val;
  file >> val;
  if (file.fail())
    throw std::runtime_error("Unable to read " + path + "!");
  return val;
}

void fancontroller::write_value(const std::string &path, long val) {
#if defined(MY_DEBUG)
  std::cerr << "Writing " << val << " to " << path << std::endl;
#endif
  std::ofstream file(path.c_str());
  file << val;
  if (file.fail()) {
    std::stringstream exc;
    exc << "Unable to write " << val << " to " << path << "!";
    throw std::runtime_error(exc.str());
  }
}

long fancontroller::read_temperature() const {
  return read_value(temp_sensor);
}

long fancontroller::read_fan_speed() const {
  return read_value(fan_sensor);
}

long fancontroller::read_fan_pwm() const {
  return read_value(controller);
}

void fancontroller::set_fan_pwm(long pwm) {
  write_value(controller, pwm);
}

void fancontroller::set_full_speed() {
  write_value(controller, max_pwm);
}

void fancontroller::start_fan() {
  long pwm = min_start;
  set_fan_pwm(pwm);
  sleep(1);
  while (read_fan_speed() < min_speed) {
    if (pwm < max_pwm) {
      set_fan_pwm(++pwm);
      sleep(1);
    } else {
      throw std::runtime_error("Unable to start fan!");
    }
  }
}

void fancontroller::stop_fan() {
  set_fan_pwm(0);
  while (read_fan_speed())
    sleep(1);
}
