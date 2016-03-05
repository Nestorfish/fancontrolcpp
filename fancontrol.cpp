#include <unistd.h>
#include <csignal>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <string>
#include <boost/program_options.hpp>

#include "lib/fancontroller.h"

/*
 * TODO:
 * Daemonize
 */

namespace bpo = boost::program_options;

static bool sigint = false;
static void signal_handler(int signum) {
  switch (signum) {
    case SIGINT:
      if (sigint) {
        exit(0);
      } else {
        sigint = true;
      }
      break;
  }
}

class pwm_computer {
 public:
  explicit pwm_computer(const fancontroller &fc);
  virtual ~pwm_computer() {}
  long pwm_for(long temperature) const;
  virtual long calculate(long temperature) const = 0;
 protected:
  const long double min_temperature, max_temperature,
                    min_pwm, min_stop, max_pwm;
};
pwm_computer::pwm_computer(const fancontroller &fc)
  : min_temperature(static_cast<long double>(fc.get_min_temp())),
    max_temperature(static_cast<long double>(fc.get_max_temp())),
    min_pwm(static_cast<long double>(fc.get_min_pwm())),
    min_stop(static_cast<long double>(fc.get_min_stop())),
    max_pwm(static_cast<long double>(fc.get_max_pwm())) {}
long pwm_computer::pwm_for(long temperature) const {
  if (temperature < min_temperature)  // TODO: add hysteresis
    return static_cast<double>(min_pwm);
  else if (temperature > max_temperature)
    return static_cast<double>(max_pwm);
  else
    return calculate(temperature);
}

class linear_pwm_computer : public pwm_computer {
 public:
  explicit linear_pwm_computer(const fancontroller &fc);
  ~linear_pwm_computer() {}
  long calculate(long temperature) const;
 private:
  const long double a, b;
};
linear_pwm_computer::linear_pwm_computer(const fancontroller &fc)
  : pwm_computer(fc),
    a((max_pwm - min_stop) / (max_temperature - min_temperature)),
    b((min_stop + max_pwm - a * (min_temperature + max_temperature)) / 2.0L) {}
long linear_pwm_computer::calculate(long temperature) const {
  return static_cast<long>(a * static_cast<long double>(temperature) + b);
}

class quadratic_pwm_computer : public pwm_computer {
 public:
  explicit quadratic_pwm_computer(const fancontroller &fc);
  ~quadratic_pwm_computer() {}
  long calculate(long temperature) const;
 private:
  const long double a, b, c;
};
quadratic_pwm_computer::quadratic_pwm_computer(const fancontroller &fc)
  : pwm_computer(fc),
    a((max_pwm - min_stop) / pow(max_temperature - min_temperature, 2.0L)),
    b(-2.0L * min_temperature * a),
    c((min_stop + max_pwm +
       a * (pow(min_temperature + max_temperature, 2.0L) -
            2.0L * pow(max_temperature, 2.0L))) / 2.0L) {}
long quadratic_pwm_computer::calculate(long temperature) const {
  return static_cast<long>(
      a * std::pow(static_cast<long double>(temperature), 2.0L)
    + b * static_cast<long double>(temperature)
    + c);
}

static void update(fancontroller * fc, const pwm_computer * compute) {
  long temp  = fc->read_temperature();
  long cur_pwm = fc->read_fan_pwm();
  long cur_fan_speed = fc->read_fan_speed();

  // Compute regular new PWM value
  long computed_pwm = compute->pwm_for(temp);
#if defined(MY_DEBUG)
  std::cout << "Computed: " << computed_pwm;
#endif

  // Filter it
  // progressive and growing increase, unlimited decrease
  long new_pwm = fc->get_min_start();
  static long up_step(0);
  if (computed_pwm > cur_pwm) {
    up_step += 1;
    new_pwm = cur_pwm + up_step;
    if (new_pwm >= computed_pwm) {
      new_pwm = computed_pwm;
      up_step = 0;
    }
  } else {
    new_pwm = computed_pwm;
    up_step = 0;
  }
#if defined(MY_DEBUG)
  std::cout << ", Filtered: " << new_pwm;
#endif

  // Ensure fan start if necessary
  if (!cur_fan_speed && new_pwm < fc->get_min_stop()) {
    new_pwm = 0;
    up_step *= 2;  // Because new_pwm will not be applied but reset to 0
#if defined(MY_DEBUG)
    std::cout << ", Zeroed";
#endif
  }
  if (new_pwm && !cur_fan_speed) {
    std::cout << "Starting fan" << std::endl;
    fc->start_fan();
  }

  // Apply
#if defined(MY_DEBUG)
  std::cout << ", Applying" << std::endl;
#endif
  fc->set_fan_pwm(new_pwm);
}

static bpo::variables_map parse_parameters(int argc, char **argv) {
  std::string conf_file("/etc/fancontrol_cpp");

  bpo::variables_map parameters;

  bpo::options_description cli_desc("Command-line options");
  cli_desc.add_options()
    ("help,h", "Print this help")
    ("help-conf", "Print configuration file help")
    ("foreground,F", "Stay in foreground")
    ("config-file,c", bpo::value<std::string>(&conf_file),
       "Path to configuration file");
  try {
    bpo::store(bpo::parse_command_line(argc, argv, cli_desc), parameters);
    bpo::notify(parameters);
  } catch (bpo::error & e) {
    std::cerr << e.what() << "\n" << cli_desc << std::endl;
    exit(1);
  }

  bpo::options_description file_desc("Configuration file parameters");
  file_desc.add_options()
    ("poll_interval", bpo::value<unsigned int>()->required(),
       "Polling interval")
    ("pwm_algorithm", bpo::value<std::string>()->default_value("quadratic"),
       "PWM adjusting function algorithm\n  (quadratic or linear)")
    ("pwm_ctrl", bpo::value<std::string>()->required(),
       "PWM control device")
    ("fan_sensor", bpo::value<std::string>()->required(),
       "Fan rotation speed sensor device")
    ("temp_sensor", bpo::value<std::string>()->required(),
       "Temperature sensor device")
    ("min_temp", bpo::value<long>()->required(),
       "Minimum temperature for PWM adjusting function")
    ("max_temp", bpo::value<long>()->required(),
       "Maximum temperature for PWM adjusting function")
    ("min_start", bpo::value<long>()->required(),
       "Minimum PWM value to start fan rotation when stopped")
    ("min_stop", bpo::value<long>()->required(),
       "PWM value applied at min_temp (must keep fan rotating)")
    ("min_speed", bpo::value<long>()->required(),
       "Minimum fan rotation speed to consider it started")
    ("min_pwm", bpo::value<long>()->required(),
       "Minimum allowed PWM value\n  (applied below min_temp)")
    ("max_pwm", bpo::value<long>()->required(),
       "Maximum allowed PWM value\n  (applied at and after max_temp)");
  try {
    std::cerr << "Reading parameters from " << conf_file << std::endl;
    bpo::store(bpo::parse_config_file<char>(conf_file.c_str(), file_desc),
               parameters);
    bpo::notify(parameters);
  } catch (bpo::error & e) {
    std::cerr << e.what() << "\n" << file_desc << std::endl;
    exit(1);
  }

  if (parameters.count("help")) {
    std::cout << cli_desc << std::endl;
    exit(0);
  }
  if (parameters.count("help-conf")) {
    std::cout << file_desc << std::endl;
    exit(0);
  }

  return parameters;
}

int main(int argc, char ** argv) {
  bpo::variables_map parameters = parse_parameters(argc, argv);

  unsigned int poll_interval = parameters["poll_interval"].as<unsigned int>();

  fancontroller fc(parameters["pwm_ctrl"].as<std::string>(),
                   parameters["fan_sensor"].as<std::string>(),
                   parameters["temp_sensor"].as<std::string>(),
                   parameters["min_temp"].as<long>(),
                   parameters["max_temp"].as<long>(),
                   parameters["min_start"].as<long>(),
                   parameters["min_stop"].as<long>(),
                   parameters["min_speed"].as<long>(),
                   parameters["min_pwm"].as<long>(),
                   parameters["max_pwm"].as<long>());

  pwm_computer * pwm_computer_f;
  if (parameters["pwm_algorithm"].as<std::string>() == "linear") {
      pwm_computer_f = new linear_pwm_computer(fc);
  } else if (parameters["pwm_algorithm"].as<std::string>() == "quadratic") {
      pwm_computer_f = new quadratic_pwm_computer(fc);
  } else {
      std::cerr << "Unknown PWM algorithm!" << std::endl;
      exit(1);
  }

#if defined(MY_DEBUG)
  for (long temp = parameters["min_temp"].as<long>() - 5000L;
       temp <= parameters["max_temp"].as<long>() + 5000L;
       temp += 1000L) {
    std::cout << temp << "\t"
              << pwm_computer_f->pwm_for(temp)
              << std::endl;
  }
#endif

  std::signal(SIGINT, signal_handler);

  try {
    do {
      std::cout << "Temperature: " << fc.read_temperature()
                << "  Fan speed: " << fc.read_fan_speed()
                << "  PWM value: " << fc.read_fan_pwm()
                << std::endl;
      update(&fc, pwm_computer_f);
    } while (!sleep(poll_interval) && !sigint);
  } catch (const std::runtime_error & e) {
    std::cerr << "Got error with update()!" << std::endl;
    std::cerr << e.what();
    std::cerr << "Restoring fan max speed" << std::endl;
    fc.set_full_speed();
    delete pwm_computer_f;
    return 1;
  }

  std::cerr << "Leaving." << std::endl;
  delete pwm_computer_f;

  return 0;
}
