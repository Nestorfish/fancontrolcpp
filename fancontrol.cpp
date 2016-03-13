#include <unistd.h>
#include <systemd/sd-daemon.h>
#include <csignal>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <string>
#include <boost/program_options.hpp>

#include "lib/pidfile.h"
#include "lib/fancontroller.h"

/*
 * TODO:
 * Daemonize
 * Check/implement hysteresis for fan stop/start on low temperatures
 */

namespace bpo = boost::program_options;

static bool shutdown_request = false;
static bool verbose = false;

static void signal_handler(int signum) {
  switch (signum) {
    case SIGINT:
    case SIGTERM:
      if (shutdown_request) {
        sd_notify(0, "STATUS=Shutting down\n"
            "STOPPING=1");
        exit(0);
      } else {
        shutdown_request = true;
      }
      break;
    case SIGHUP:
      // Should reload conf...
      break;
  }
}

class pwm_computer {
 public:
  explicit pwm_computer(const fancontroller * const fc);
  virtual ~pwm_computer() {}
  long pwm_for(long temperature) const;
  virtual long calculate(long temperature) const = 0;
 protected:
  const long double min_temperature, max_temperature,
                    min_pwm, min_stop, max_pwm;
};
pwm_computer::pwm_computer(const fancontroller * const fc)
  : min_temperature(static_cast<long double>(fc->get_min_temp())),
    max_temperature(static_cast<long double>(fc->get_max_temp())),
    min_pwm(static_cast<long double>(fc->get_min_pwm())),
    min_stop(static_cast<long double>(fc->get_min_stop())),
    max_pwm(static_cast<long double>(fc->get_max_pwm())) {}
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
  explicit linear_pwm_computer(const fancontroller * const fc);
  ~linear_pwm_computer() {}
  long calculate(long temperature) const;
 private:
  const long double a, b;
};
linear_pwm_computer::linear_pwm_computer(const fancontroller * const fc)
  : pwm_computer(fc),
    a((max_pwm - min_stop) / (max_temperature - min_temperature)),
    b((min_stop + max_pwm - a * (min_temperature + max_temperature)) / 2.0L) {}
long linear_pwm_computer::calculate(long temperature) const {
  return static_cast<long>(a * static_cast<long double>(temperature) + b);
}

class quadratic_pwm_computer : public pwm_computer {
 public:
  explicit quadratic_pwm_computer(const fancontroller * const fc);
  ~quadratic_pwm_computer() {}
  long calculate(long temperature) const;
 private:
  const long double a, b, c;
};
quadratic_pwm_computer::quadratic_pwm_computer(const fancontroller * const fc)
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

static void update(fancontroller * fc, const pwm_computer * compute, const long temp_hyst) {
  long temp  = fc->read_temperature();
  long cur_pwm = fc->read_fan_pwm();
  long cur_fan_speed = fc->read_fan_speed();

  // Hysteresis
  if (!cur_fan_speed) {
    temp -= temp_hyst;
  }

  // Compute regular new PWM value
  long computed_pwm = compute->pwm_for(temp);
#if defined(MY_DEBUG)
  std::cout << "Computed: " << computed_pwm;
#endif

  // Filter it
  // progressive and growing increase, unlimited decrease
  long new_pwm = fc->get_min_start();
  if (computed_pwm > cur_pwm) {
    fc->up_step += 1;
    new_pwm = cur_pwm + fc->up_step;
    if (new_pwm >= computed_pwm) {
      new_pwm = computed_pwm;
      fc->up_step = 0;
    }
  } else {
    new_pwm = computed_pwm;
    fc->up_step = 0;
  }
#if defined(MY_DEBUG)
  std::cout << ", Filtered: " << new_pwm;
#endif

  // Would not start; zero value, but increase up_step to avoid staying for too long stopped if it should be normally running
  if (!cur_fan_speed && new_pwm < fc->get_min_stop()) {
    new_pwm = 0;
    fc->up_step *= 2;
#if defined(MY_DEBUG)
    std::cout << ", Zeroed";
#endif
  }

  // Ensure fan start if necessary
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
    ("verbose,v", "Verbose mode")
    ("config-file,c", bpo::value<std::string>(&conf_file),
       "Path to configuration file");
  try {
    bpo::store(bpo::parse_command_line(argc, argv, cli_desc), parameters);
    bpo::notify(parameters);
  } catch (bpo::error & e) {
    std::cerr << e.what() << "\n" << cli_desc << std::endl;
    sd_notifyf(0, "STATUS=Failed to parse command-line parameters: %s\n"
        "STOPPING=1",
        e.what());
    exit(1);
  }

  bpo::options_description file_desc("Configuration file parameters");
  file_desc.add_options()
    ("poll_interval", bpo::value<unsigned int>()->required(),
       "Main polling interval")
    // First instance, mandatory
    ("pwm_algorithm1", bpo::value<std::string>()->default_value("quadratic"),
       "PWM adjusting function algorithm\n  (quadratic or linear)")
    ("pwm_ctrl1", bpo::value<std::string>()->required(),
       "PWM control device")
    ("fan_sensor1", bpo::value<std::string>()->required(),
       "Fan rotation speed sensor device")
    ("temp_sensor1", bpo::value<std::string>()->required(),
       "Temperature sensor device")
    ("min_temp1", bpo::value<long>()->required(),
       "Minimum temperature for PWM adjusting function")
    ("max_temp1", bpo::value<long>()->required(),
       "Maximum temperature for PWM adjusting function")
    ("temp_hyst1", bpo::value<long>()->required(),
       "Temperature hysteresis for fan stop/start")
    ("min_start1", bpo::value<long>()->required(),
       "Minimum PWM value to start fan rotation when stopped")
    ("min_stop1", bpo::value<long>()->required(),
       "PWM value applied at min_temp (must keep fan rotating)")
    ("min_speed1", bpo::value<long>()->required(),
       "Minimum fan rotation speed to consider it started")
    ("min_pwm1", bpo::value<long>()->required(),
       "Minimum allowed PWM value\n  (applied below min_temp)")
    ("max_pwm1", bpo::value<long>()->required(),
       "Maximum allowed PWM value\n  (applied at and after max_temp)")
    // Up to 2 other instances, optional
    ("pwm_algorithm2", bpo::value<std::string>()->default_value("quadratic"),
       "PWM adjusting function algorithm\n  (quadratic or linear)")
    ("pwm_ctrl2", bpo::value<std::string>(),
       "PWM control device")
    ("fan_sensor2", bpo::value<std::string>(),
       "Fan rotation speed sensor device")
    ("temp_sensor2", bpo::value<std::string>(),
       "Temperature sensor device")
    ("min_temp2", bpo::value<long>(),
       "Minimum temperature for PWM adjusting function")
    ("max_temp2", bpo::value<long>(),
       "Maximum temperature for PWM adjusting function")
    ("temp_hyst2", bpo::value<long>(),
       "Temperature hysteresis for fan stop/start")
    ("min_start2", bpo::value<long>(),
       "Minimum PWM value to start fan rotation when stopped")
    ("min_stop2", bpo::value<long>(),
       "PWM value applied at min_temp (must keep fan rotating)")
    ("min_speed2", bpo::value<long>(),
       "Minimum fan rotation speed to consider it started")
    ("min_pwm2", bpo::value<long>(),
       "Minimum allowed PWM value\n  (applied below min_temp)")
    ("max_pwm2", bpo::value<long>(),
       "Maximum allowed PWM value\n  (applied at and after max_temp)")
    // the last...
    ("pwm_algorithm3", bpo::value<std::string>()->default_value("quadratic"),
       "PWM adjusting function algorithm\n  (quadratic or linear)")
    ("pwm_ctrl3", bpo::value<std::string>(),
       "PWM control device")
    ("fan_sensor3", bpo::value<std::string>(),
       "Fan rotation speed sensor device")
    ("temp_sensor3", bpo::value<std::string>(),
       "Temperature sensor device")
    ("min_temp3", bpo::value<long>(),
       "Minimum temperature for PWM adjusting function")
    ("max_temp3", bpo::value<long>(),
       "Maximum temperature for PWM adjusting function")
    ("temp_hyst3", bpo::value<long>(),
       "Temperature hysteresis for fan stop/start")
    ("min_start3", bpo::value<long>(),
       "Minimum PWM value to start fan rotation when stopped")
    ("min_stop3", bpo::value<long>(),
       "PWM value applied at min_temp (must keep fan rotating)")
    ("min_speed3", bpo::value<long>(),
       "Minimum fan rotation speed to consider it started")
    ("min_pwm3", bpo::value<long>(),
       "Minimum allowed PWM value\n  (applied below min_temp)")
    ("max_pwm3", bpo::value<long>(),
       "Maximum allowed PWM value\n  (applied at and after max_temp)");
  try {
    std::cerr << "Reading parameters from " << conf_file << std::endl;
    bpo::store(bpo::parse_config_file<char>(conf_file.c_str(), file_desc),
               parameters);
    bpo::notify(parameters);
  } catch (bpo::error & e) {
    std::cerr << e.what() << "\n" << file_desc << std::endl;
    sd_notifyf(0, "STATUS=Failed to parse configuration file: %s\n"
        "STOPPING=1",
        e.what());
    exit(1);
  }

  if (parameters.count("pwm_ctrl2") && !(
        parameters.count("fan_sensor2") &&
        parameters.count("temp_sensor2") &&
        parameters.count("min_temp2") &&
        parameters.count("max_temp2") &&
        parameters.count("temp_hyst2") &&
        parameters.count("min_start2") &&
        parameters.count("min_stop2") &&
        parameters.count("min_speed2") &&
        parameters.count("min_pwm2") &&
        parameters.count("max_pwm2"))
        ) {
    std::cerr << "Incomplete instance definition for pwm_ctrl2!\n";
    sd_notify(0, "STATUS=Failed to parse configuration file: Incomplete instance definition for pwm_ctrl2!\n"
        "STOPPING=1");
    exit(1);
  }

  if (parameters.count("pwm_ctrl3") && !(
        parameters.count("fan_sensor3") &&
        parameters.count("temp_sensor3") &&
        parameters.count("min_temp3") &&
        parameters.count("max_temp3") &&
        parameters.count("temp_hyst3") &&
        parameters.count("min_start3") &&
        parameters.count("min_stop3") &&
        parameters.count("min_speed3") &&
        parameters.count("min_pwm3") &&
        parameters.count("max_pwm3"))
        ) {
    std::cerr << "Incomplete instance definition for pwm_ctrl3!\n";
    sd_notify(0, "STATUS=Failed to parse configuration file: Incomplete instance definition for pwm_ctrl3!\n"
        "STOPPING=1");
    exit(1);
  }

  if (parameters.count("help")) {
    std::cout << cli_desc << std::endl;
    sd_notify(0, "STATUS=Shutting down\n"
        "STOPPING=1");
    exit(0);
  }
  if (parameters.count("help-conf")) {
    std::cout << file_desc << std::endl;
    sd_notify(0, "STATUS=Shutting down\n"
        "STOPPING=1");
    exit(0);
  }
  if (parameters.count("verbose")) {
    verbose = true;
  }

  return parameters;
}

int main(int argc, char ** argv) {
  pidfile pidfile("/run/fancontrolcpp.pid");

  bpo::variables_map parameters = parse_parameters(argc, argv);

  unsigned int poll_interval = parameters["poll_interval"].as<unsigned int>();

  long temp_hyst1 = parameters["temp_hyst1"].as<long>();
  fancontroller * fc1 = new fancontroller(parameters["pwm_ctrl1"].as<std::string>(),
      parameters["fan_sensor1"].as<std::string>(),
      parameters["temp_sensor1"].as<std::string>(),
      parameters["min_temp1"].as<long>(),
      parameters["max_temp1"].as<long>(),
      parameters["min_start1"].as<long>(),
      parameters["min_stop1"].as<long>(),
      parameters["min_speed1"].as<long>(),
      parameters["min_pwm1"].as<long>(),
      parameters["max_pwm1"].as<long>());

  pwm_computer * pwm_computer_f1;
  if (parameters["pwm_algorithm1"].as<std::string>() == "linear") {
    pwm_computer_f1 = new linear_pwm_computer(fc1);
  } else if (parameters["pwm_algorithm1"].as<std::string>() == "quadratic") {
    pwm_computer_f1 = new quadratic_pwm_computer(fc1);
  } else {
    std::cerr << "Unknown PWM algorithm for pwm_ctrl1!" << std::endl;
    sd_notify(0, "STATUS=Failed to start up: Unknown PWM algorithm for pwm_ctrl1!\n"
        "STOPPING=1");
    exit(1);
  }

  long temp_hyst2 = 0;
  fancontroller * fc2 = nullptr;
  pwm_computer * pwm_computer_f2 = nullptr;
  if (parameters.count("pwm_ctrl2")) {
    temp_hyst2 = parameters["temp_hyst2"].as<long>();
    fc2 = new fancontroller(parameters["pwm_ctrl2"].as<std::string>(),
        parameters["fan_sensor2"].as<std::string>(),
        parameters["temp_sensor2"].as<std::string>(),
        parameters["min_temp2"].as<long>(),
        parameters["max_temp2"].as<long>(),
        parameters["min_start2"].as<long>(),
        parameters["min_stop2"].as<long>(),
        parameters["min_speed2"].as<long>(),
        parameters["min_pwm2"].as<long>(),
        parameters["max_pwm2"].as<long>());

    if (parameters["pwm_algorithm2"].as<std::string>() == "linear") {
      pwm_computer_f2 = new linear_pwm_computer(fc2);
    } else if (parameters["pwm_algorithm2"].as<std::string>() == "quadratic") {
      pwm_computer_f2 = new quadratic_pwm_computer(fc2);
    } else {
      std::cerr << "Unknown PWM algorithm for pwm_ctrl2!" << std::endl;
      sd_notify(0, "STATUS=Failed to start up: Unknown PWM algorithm for pwm_ctrl2!\n"
          "STOPPING=1");
      exit(1);
    }
  }

  long temp_hyst3 = 0;
  fancontroller * fc3 = nullptr;
  pwm_computer * pwm_computer_f3 = nullptr;
  if (parameters.count("pwm_ctrl3")) {
    temp_hyst3 = parameters["temp_hyst3"].as<long>();
    fc3 = new fancontroller(parameters["pwm_ctrl3"].as<std::string>(),
        parameters["fan_sensor3"].as<std::string>(),
        parameters["temp_sensor3"].as<std::string>(),
        parameters["min_temp3"].as<long>(),
        parameters["max_temp3"].as<long>(),
        parameters["min_start3"].as<long>(),
        parameters["min_stop3"].as<long>(),
        parameters["min_speed3"].as<long>(),
        parameters["min_pwm3"].as<long>(),
        parameters["max_pwm3"].as<long>());

    if (parameters["pwm_algorithm3"].as<std::string>() == "linear") {
      pwm_computer_f3 = new linear_pwm_computer(fc3);
    } else if (parameters["pwm_algorithm3"].as<std::string>() == "quadratic") {
      pwm_computer_f3 = new quadratic_pwm_computer(fc3);
    } else {
      std::cerr << "Unknown PWM algorithm for pwm_ctrl3!" << std::endl;
      sd_notify(0, "STATUS=Failed to start up: Unknown PWM algorithm for pwm_ctrl3!\n"
          "STOPPING=1");
      exit(1);
    }
  }

#if defined(MY_DEBUG)
  for (long temp = parameters["min_temp1"].as<long>() - 5000L;
       temp <= parameters["max_temp1"].as<long>() + 5000L;
       temp += 1000L) {
    std::cout << temp << "\t"
              << pwm_computer_f1->pwm_for(temp)
              << std::endl;
  }
#endif

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGHUP, signal_handler);

  sd_notifyf(0, "READY=1\n"
      "STATUS=Entering control loop...\n"
      "MAINPID=%lu",
      (unsigned long) pidfile.get_pid());

  try {
    do {
      if (verbose) {
        std::cout << "FC1 "
                  << "Temperature: " << fc1->read_temperature()
                  << "  Fan speed: " << fc1->read_fan_speed()
                  << "  PWM value: " << fc1->read_fan_pwm()
                  << std::endl;
      }
      update(fc1, pwm_computer_f1, temp_hyst1);
      if (parameters.count("pwm_ctrl2")) {
        if (verbose) {
          std::cout << "FC2 "
                    << "Temperature: " << fc2->read_temperature()
                    << "  Fan speed: " << fc2->read_fan_speed()
                    << "  PWM value: " << fc2->read_fan_pwm()
                    << std::endl;
        }
        update(fc2, pwm_computer_f2, temp_hyst2);
      }
      if (parameters.count("pwm_ctrl3")) {
        if (verbose) {
          std::cout << "FC3 "
                    << "Temperature: " << fc3->read_temperature()
                    << "  Fan speed: " << fc3->read_fan_speed()
                    << "  PWM value: " << fc3->read_fan_pwm()
                    << std::endl;
        }
        update(fc3, pwm_computer_f3, temp_hyst3);
      }
    } while (!sleep(poll_interval) && !shutdown_request);
  } catch (const std::runtime_error & e) {
    std::cerr << "Got error with update()!" << std::endl;
    std::cerr << e.what();
    sd_notify(0, "STATUS=Got error with update()!\n"
        "STOPPING=1");
    std::cerr << "Restoring fan max speed" << std::endl;
    fc1->set_full_speed();
    delete pwm_computer_f1;
    delete fc1;
    if (parameters.count("pwm_ctrl2")) {
      fc2->set_full_speed();
      delete pwm_computer_f2;
      delete fc2;
    }
    if (parameters.count("pwm_ctrl3")) {
      fc3->set_full_speed();
      delete pwm_computer_f3;
      delete fc3;
    }
    return 1;
  }

  std::cerr << "Leaving." << std::endl;
  delete pwm_computer_f1;
  delete fc1;
  if (parameters.count("pwm_ctrl2")) {
      delete pwm_computer_f2;
      delete fc2;
  }
  if (parameters.count("pwm_ctrl3")) {
      delete pwm_computer_f3;
      delete fc3;
  }
  sd_notify(0, "STATUS=Shutting down\n"
      "STOPPING=1");
  return 0;
}
