#ifndef LIB_FANCONTROLLER_H_
#define LIB_FANCONTROLLER_H_
#include <string>

class fancontroller {
 private:
  const std::string controller;
  const std::string fan_sensor;
  const std::string temp_sensor;

  long min_temp;
  long max_temp;
  long min_start;
  long min_stop;
  long min_speed;
  long min_pwm;
  long max_pwm;

  const std::string controller_enabler;

  long read_value(const std::string & path) const;
  void write_value(const std::string & path, long val);

 public:
  fancontroller(const std::string &controller,
                const std::string &fan_sensor,
                const std::string &temp_sensor,
                long min_temp, long max_temp,
                long min_start, long min_stop, long min_speed,
                long min_pwm, long max_pwm);
  ~fancontroller();

  long get_min_temp()  const { return min_temp; }
  long get_max_temp()  const { return max_temp; }
  long get_min_start() const { return min_start;}
  long get_min_stop()  const { return min_stop; }
  long get_min_speed() const { return min_speed;}
  long get_min_pwm()   const { return min_pwm;  }
  long get_max_pwm()   const { return max_pwm;  }

  void set_min_temp(long val)  { min_temp = val; }
  void set_max_temp(long val)  { max_temp = val; }
  void set_min_start(long val) { min_start = val;}
  void set_min_stop(long val)  { min_stop = val; }
  void set_min_speed(long val) { min_speed = val;}
  void set_min_pwm(long val)   { min_pwm = val;  }
  void set_max_pwm(long val)   { max_pwm = val;  }

  long read_temperature() const;
  long read_fan_speed() const;
  long read_fan_pwm() const;

  void set_fan_pwm(long pwm);

  void set_full_speed();
  void start_fan();
  void stop_fan();
};
#endif  // LIB_FANCONTROLLER_H_
