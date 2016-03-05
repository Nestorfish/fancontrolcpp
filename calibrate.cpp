#include <unistd.h>
#include <stdexcept>
#include <iostream>
#include <string>
#include <list>
#include <utility>
#include <vector>
#include <algorithm>
#include "lib/fancontroller.h"

/*
 * TODO:
 * External configuration or argv?
 * Read hwmon max T° value
 * Find how to give value_history an iteror derived from history std::list
 */

class value_history {
 private:
  std::list<long> history;

 public:
  explicit value_history(int samples) :history(samples, 0) {}

  void push(long val) {
    history.pop_front();
    history.push_back(val);
  }

  double range_relative() {
    long min = *std::min_element(history.begin(), history.end());
    long max = *std::max_element(history.begin(), history.end());
    return static_cast<double>(max - min) / static_cast<double>(min);
  }

  long mean() {
    long sum = 0;
    for (std::list<long>::const_iterator it = history.begin();
        it != history.end();
        ++it) {
      sum += *it;
    }
    return static_cast<long>(sum / history.size());
  }
};


void find_min_start(fancontroller * fc, int samples, int interval) {
  long pwm;

  std::cout << "Stopping fan" << std::endl;
  fc->stop_fan();

  sleep(interval);

  std::cout << "Upward from " << fc->get_min_stop()
            << " to max PWM or fan start" << std::endl;

  pwm = fc->get_min_stop();
  while (pwm < fc->get_max_pwm()) {
    std::cout << pwm << std::endl;
    fc->set_fan_pwm(pwm);
    sleep(interval);

    if (fc->read_temperature() >= fc->get_max_temp()) {
      throw std::runtime_error("Temperature too high!");
    }

    if (fc->read_fan_speed()) {
      std::cout << "Fan started, starting validation" << std::endl;
      fc->set_fan_pwm(fc->get_min_stop());
      bool ok = true;
      for (int i = 0; i < samples; ++i) {
        sleep(interval);
        if (!fc->read_fan_speed()) {
          ok = false;
          break;
        }
      }
      if (ok) {
        std::cout << "OK" << std::endl;
        fc->set_min_start(pwm);
        return;
      }
      std::cout << "Not OK, continuing" << std::endl;
    }

    pwm += 1;
  }

  throw std::runtime_error("No fan start detected!");
}


// min_stop, min_speed, and min_temp (informative)
void find_min_stop_min_speed_min_temp(fancontroller * fc,
                                      int samples,
                                      int interval,
                                      double precision) {
  // Speed, T°
  std::vector<std::pair<long, long> > values;

  std::cout << "Downward from " << fc->get_max_pwm()
            << " to fan stop" << std::endl;

  long pwm = fc->get_max_pwm();
  bool ok = false;

  while (pwm > fc->get_min_pwm() && !ok) {
    std::cout << pwm << "\t";
    value_history fan_speed_history(samples);
    value_history temperature_history(samples);

    fc->set_fan_pwm(pwm);

    while (!sleep(interval)) {
      long fan_speed = fc->read_fan_speed();
      fan_speed_history.push(fan_speed);
      long temperature = fc->read_temperature();
      temperature_history.push(temperature);

      if (temperature >= fc->get_max_temp()) {
        throw std::runtime_error("Temperature too high!");
      }

      if (!fan_speed) {
        std::cout << "Fan stopped" << std::endl;
        ok = true;

        fc->set_min_stop(pwm + 1);

        std::vector<std::pair<long, long> >::reverse_iterator
          it = values.rbegin();
        fc->set_min_speed(it->first);
        fc->set_min_temp(it->second);

        break;
      }

      if (fan_speed_history.range_relative() < precision &&
          fan_speed_history.range_relative() < precision) {
        long mean_fan_speed = fan_speed_history.mean();
        long mean_temperature = temperature_history.mean();
        values.push_back(std::pair<long, long>(mean_fan_speed,
                                               mean_temperature));
        std::cout << mean_fan_speed << "\t" << mean_temperature << std::endl;
        break;
      }
    }

    pwm -= 1;
  }

  if (!ok) {
    std::cout << "No fan stop detected, setting min_stop to min_pwm + 1,\n" <<
            "min_speed and min_temp to current ones." << std::endl;
    fc->set_min_stop(fc->get_min_pwm() + 1);
    fc->set_min_speed(fc->read_fan_speed());
    fc->set_min_temp(fc->read_temperature());
  }
}

void calibrate(fancontroller * fc,
    int samples, int interval, double precision) {
  find_min_stop_min_speed_min_temp(fc, samples, interval, precision);
  find_min_start(fc, samples, interval);
}


int main() {
  const std::string PATH_SYS("/sys/");
  const std::string PATH_HWMON("class/hwmon");
  const std::string PATH_DEVICE("devices/");

  const std::string driver_name("f71882fg.2560");
  // TODO: add function to find which hwmon to use from driver name
  //       by resolving symlinks from PATH_HWMON
  const std::string hwmon_path("platform/f71882fg.2560/hwmon/hwmon1/");
  // resolve device from hwmon_path
  const std::string hwmon_device("platform/f71882fg.2560/");

  const std::string pwm_ctrl =
    PATH_SYS + PATH_DEVICE + hwmon_device + "pwm2";
  const std::string fan_sensor =
    PATH_SYS + PATH_DEVICE + hwmon_device + "fan2_input";
  const std::string temp_sensor =
    PATH_SYS + PATH_DEVICE + hwmon_device + "temp1_input";
  long min_temp  = 0;
  long max_temp  = 85000;
  long min_start = 0;
  long min_stop  = 0;
  long min_speed = 0;
  long min_pwm   = 0;
  long max_pwm   = 255;
  fancontroller fc(pwm_ctrl, fan_sensor, temp_sensor,
                   min_temp, max_temp,
                   min_start, min_stop, min_speed,
                   min_pwm, max_pwm);

  int samples = 15;
  int interval = 1;
  double precision = 0.015;  // 1.5%

  calibrate(&fc, samples, interval, precision);

  std::cout << "Calibration report" <<
     "\nmin_temp:  " << fc.get_min_temp() <<
     "\nmax_temp:  " << fc.get_max_temp() <<
     "\nmin_start: " << fc.get_min_start() <<
     "\nmin_stop:  " << fc.get_min_stop() <<
     "\nmin_speed: " << fc.get_min_speed() <<
     "\nmin_pwm:   " << fc.get_min_pwm() <<
     "\nmax_pwm:   " << fc.get_max_pwm() <<
     std::endl;

  return 0;
}
