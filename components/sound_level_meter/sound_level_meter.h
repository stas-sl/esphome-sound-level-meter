#pragma onve

#include <mutex>
#include <algorithm>
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2s/i2s.h"

namespace esphome {
namespace sound_level_meter {
class SensorGroup;
class SoundLevelMeterSensor;
class Filter;

class SoundLevelMeter : public Component {
  friend class SoundLevelMeterSensor;

 public:
  void set_update_interval(uint32_t update_interval);
  uint32_t get_update_interval();
  void set_buffer_size(uint32_t buffer_size);
  uint32_t get_buffer_size();
  uint32_t get_sample_rate();
  void set_i2s(i2s::I2SComponent *i2s);
  void add_group(SensorGroup *group);
  void set_warmup_interval(uint32_t warmup_interval);
  void set_task_stack_size(uint32_t task_stack_size);
  void set_task_priority(uint8_t task_priority);
  void set_task_core(uint8_t task_core);
  void set_mic_sensitivity(optional<float> mic_sensitivity);
  optional<float> get_mic_sensitivity();
  void set_mic_sensitivity_ref(optional<float> mic_sensitivity_ref);
  optional<float> get_mic_sensitivity_ref();
  void set_offset(optional<float> offset);
  optional<float> get_offset();
  virtual void setup() override;
  virtual void loop() override;
  virtual void dump_config() override;

 protected:
  i2s::I2SComponent *i2s_{nullptr};
  std::vector<SensorGroup *> groups_;
  size_t buffer_size_{256};
  uint32_t warmup_interval_{500};
  uint32_t task_stack_size_{1024};
  uint8_t task_priority_{1};
  uint8_t task_core_{1};
  optional<float> mic_sensitivity_{};
  optional<float> mic_sensitivity_ref_{};
  optional<float> offset_{};
  std::queue<std::function<void()>> defer_queue_;
  std::mutex defer_mutex_;
  uint32_t update_interval_{60000};

  static void task(void *param);
  // epshome's scheduler is not thred safe, so we have to use custom thread safe implementation
  // to execute sensor updates in main loop
  void defer(std::function<void()> &&f);
};

class SensorGroup {
 public:
  void set_parent(SoundLevelMeter *parent);
  void add_sensor(SoundLevelMeterSensor *sensor);
  void add_group(SensorGroup *group);
  void add_filter(Filter *filter);
  void process(std::vector<float> &buffer);
  void dump_config(const char *prefix);

 protected:
  SoundLevelMeter *parent_{nullptr};
  std::vector<SensorGroup *> groups_;
  std::vector<SoundLevelMeterSensor *> sensors_;
  std::vector<Filter *> filters_;
};

class SoundLevelMeterSensor : public sensor::Sensor {
 public:
  void set_parent(SoundLevelMeter *parent);
  void set_update_interval(uint32_t update_interval);
  virtual void process(std::vector<float> &buffer) = 0;
  void defer_publish_state(float state);

 protected:
  SoundLevelMeter *parent_{nullptr};
  uint32_t update_samples_{0};
  float adjust_dB(float dB, bool is_rms = true);
};

class SoundLevelMeterSensorEq : public SoundLevelMeterSensor {
 public:
  virtual void process(std::vector<float> &buffer) override;

 protected:
  double sum_{0.f};
  uint32_t count_{0};
};

class SoundLevelMeterSensorMax : public SoundLevelMeterSensor {
 public:
  void set_window_size(uint32_t window_size);
  virtual void process(std::vector<float> &buffer) override;

 protected:
  uint32_t window_samples_{0};
  float sum_{0.f};
  float max_{std::numeric_limits<float>::min()};
  uint32_t count_sum_{0}, count_max_{0};
};

class SoundLevelMeterSensorMin : public SoundLevelMeterSensor {
 public:
  void set_window_size(uint32_t window_size);
  virtual void process(std::vector<float> &buffer) override;

 protected:
  uint32_t window_samples_{0};
  float sum_{0.f};
  float min_{std::numeric_limits<float>::max()};
  uint32_t count_sum_{0}, count_min_{0};
};

class SoundLevelMeterSensorPeak : public SoundLevelMeterSensor {
 public:
  virtual void process(std::vector<float> &buffer) override;

 protected:
  float peak_{0.f};
  uint32_t count_{0};
};

class Filter {
 public:
  virtual void process(std::vector<float> &data) = 0;
};

class SOS_Filter : public Filter {
 public:
  SOS_Filter(std::initializer_list<std::initializer_list<float>> &&coeffs);
  virtual void process(std::vector<float> &data) override;

 protected:
  std::vector<std::array<float, 5>> coeffs_;  // {b0, b1, b2, a1, a2}
  std::vector<std::array<float, 2>> state_;
};
}  // namespace sound_level_meter
}  // namespace esphome