#pragma once

#include <mutex>
#include <condition_variable>
#include <algorithm>
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/i2s/i2s.h"

namespace esphome {
namespace sound_level_meter {
class SoundLevelMeterSensor;
class Filter;
template<typename T> class BufferStack;

class SoundLevelMeter : public Component {
  friend class SoundLevelMeterSensor;

 public:
  void set_update_interval(uint32_t update_interval);
  uint32_t get_update_interval();
  void set_buffer_size(uint32_t buffer_size);
  uint32_t get_buffer_size();
  uint32_t get_sample_rate();
  void set_i2s(i2s::I2SComponent *i2s);
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
  void set_is_high_freq(bool is_high_freq);
  void add_sensor(SoundLevelMeterSensor *sensor);
  void add_dsp_filter(Filter *dsp_filter);
  virtual void setup() override;
  virtual void loop() override;
  virtual void dump_config() override;
  void turn_on();
  void turn_off();
  void toggle();
  bool is_on();

 protected:
  i2s::I2SComponent *i2s_{nullptr};
  std::vector<Filter *> dsp_filters_;
  std::vector<SoundLevelMeterSensor *> sensors_;
  size_t buffer_size_{256};
  uint32_t warmup_interval_{500};
  uint32_t task_stack_size_{1024};
  uint8_t task_priority_{1};
  uint8_t task_core_{1};
  optional<float> mic_sensitivity_{};
  optional<float> mic_sensitivity_ref_{};
  optional<float> offset_{};
  std::deque<std::function<void()>> defer_queue_;
  std::mutex defer_mutex_;
  uint32_t update_interval_{60000};
  bool is_on_{true};
  bool is_high_freq_{false};
  std::mutex on_mutex_;
  std::condition_variable on_cv_;
  HighFrequencyLoopRequester high_freq_;

  void sort_sensors();
  void process(BufferStack<float> &buffers);
  // epshome's scheduler is not thred safe, so we have to use custom thread safe implementation
  // to execute sensor updates in main loop
  void defer(std::function<void()> &&f);
  void reset();

  static void task(void *param);
};

class SoundLevelMeterSensor : public sensor::Sensor {
  friend SoundLevelMeter;

 public:
  void set_parent(SoundLevelMeter *parent);
  void set_update_interval(uint32_t update_interval);
  void add_dsp_filter(Filter *dsp_filter);
  virtual void process(std::vector<float> &buffer) = 0;
  void defer_publish_state(float state);

 protected:
  SoundLevelMeter *parent_{nullptr};
  std::vector<Filter *> dsp_filters_;
  uint32_t update_samples_{0};
  float adjust_dB(float dB, bool is_rms = true);

  virtual void reset() = 0;
};

class SoundLevelMeterSensorEq : public SoundLevelMeterSensor {
 public:
  virtual void process(std::vector<float> &buffer) override;

 protected:
  double sum_{0.};
  uint32_t count_{0};

  virtual void reset() override;
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

  virtual void reset() override;
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

  virtual void reset() override;
};

class SoundLevelMeterSensorPeak : public SoundLevelMeterSensor {
 public:
  virtual void process(std::vector<float> &buffer) override;

 protected:
  float peak_{0.f};
  uint32_t count_{0};

  virtual void reset() override;
};

class Filter {
  friend SoundLevelMeter;

 public:
  virtual void process(std::vector<float> &data) = 0;

 protected:
  virtual void reset() = 0;
};

class SOS_Filter : public Filter {
 public:
  SOS_Filter(std::initializer_list<std::initializer_list<float>> &&coeffs);
  virtual void process(std::vector<float> &data) override;

 protected:
  std::vector<std::array<float, 5>> coeffs_;  // {b0, b1, b2, a1, a2}
  std::vector<std::array<float, 2>> state_;

  virtual void reset() override;
};

template<typename T> class BufferStack {
 public:
  BufferStack(uint32_t buffer_size);
  std::vector<T> &current();
  void push();
  void pop();
  void reset();
  operator std::vector<T> &();

 private:
  uint32_t buffer_size_;
  uint32_t max_depth_;
  uint32_t index_{0};
  std::vector<std::vector<T>> buffers_;
};

template<typename... Ts> class TurnOnAction : public Action<Ts...> {
 public:
  explicit TurnOnAction(SoundLevelMeter *sound_level_meter) : sound_level_meter_(sound_level_meter) {}

  void play(Ts... x) override { this->sound_level_meter_->turn_on(); }

 protected:
  SoundLevelMeter *sound_level_meter_;
};

template<typename... Ts> class TurnOffAction : public Action<Ts...> {
 public:
  explicit TurnOffAction(SoundLevelMeter *sound_level_meter) : sound_level_meter_(sound_level_meter) {}

  void play(Ts... x) override { this->sound_level_meter_->turn_off(); }

 protected:
  SoundLevelMeter *sound_level_meter_;
};

template<typename... Ts> class ToggleAction : public Action<Ts...> {
 public:
  explicit ToggleAction(SoundLevelMeter *sound_level_meter) : sound_level_meter_(sound_level_meter) {}

  void play(Ts... x) override { this->sound_level_meter_->toggle(); }

 protected:
  SoundLevelMeter *sound_level_meter_;
};

}  // namespace sound_level_meter
}  // namespace esphome