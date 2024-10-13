#include "sound_level_meter.h"

namespace esphome {
namespace sound_level_meter {

static const char *const TAG = "sound_level_meter";

// By definition dBFS value of a full-scale sine wave equals to 0.
// Since the RMS of the full-scale sine wave is 1/sqrt(2), multiplying rms(signal) by sqrt(2)
// ensures that the formula evaluates to 0 when signal is a full-scale sine wave.
// This is equivalent to adding DBFS_OFFSET
// see: https://dsp.stackexchange.com/a/50947/65262
static constexpr float DBFS_OFFSET = 20 * log10(sqrt(2));

/* SoundLevelMeter */

void SoundLevelMeter::set_update_interval(uint32_t update_interval) { this->update_interval_ = update_interval; }
uint32_t SoundLevelMeter::get_update_interval() { return this->update_interval_; }
void SoundLevelMeter::set_buffer_size(uint32_t buffer_size) { this->buffer_size_ = buffer_size; }
uint32_t SoundLevelMeter::get_buffer_size() { return this->buffer_size_; }
uint32_t SoundLevelMeter::get_sample_rate() { return this->i2s_->get_sample_rate(); }
void SoundLevelMeter::set_i2s(i2s::I2SComponent *i2s) { this->i2s_ = i2s; }
void SoundLevelMeter::set_warmup_interval(uint32_t warmup_interval) { this->warmup_interval_ = warmup_interval; }
void SoundLevelMeter::set_task_stack_size(uint32_t task_stack_size) { this->task_stack_size_ = task_stack_size; }
void SoundLevelMeter::set_task_priority(uint8_t task_priority) { this->task_priority_ = task_priority; }
void SoundLevelMeter::set_task_core(uint8_t task_core) { this->task_core_ = task_core; }
void SoundLevelMeter::set_mic_sensitivity(optional<float> mic_sensitivity) { this->mic_sensitivity_ = mic_sensitivity; }
optional<float> SoundLevelMeter::get_mic_sensitivity() { return this->mic_sensitivity_; }
void SoundLevelMeter::set_mic_sensitivity_ref(optional<float> mic_sensitivity_ref) {
  this->mic_sensitivity_ref_ = mic_sensitivity_ref;
}
optional<float> SoundLevelMeter::get_mic_sensitivity_ref() { return this->mic_sensitivity_ref_; }
void SoundLevelMeter::set_offset(optional<float> offset) { this->offset_ = offset; }
optional<float> SoundLevelMeter::get_offset() { return this->offset_; }
void SoundLevelMeter::set_is_high_freq(bool is_high_freq) { this->is_high_freq_ = is_high_freq; }
void SoundLevelMeter::add_sensor(SoundLevelMeterSensor *sensor) { this->sensors_.push_back(sensor); }
void SoundLevelMeter::add_dsp_filter(Filter *dsp_filter) { this->dsp_filters_.push_back(dsp_filter); }

void SoundLevelMeter::dump_config() {
  ESP_LOGCONFIG(TAG, "Sound Level Meter:");
  ESP_LOGCONFIG(TAG, "  Buffer Size: %u (samples)", this->buffer_size_);
  ESP_LOGCONFIG(TAG, "  Warmup Interval: %u ms", this->warmup_interval_);
  ESP_LOGCONFIG(TAG, "  Task Stack Size: %u", this->task_stack_size_);
  ESP_LOGCONFIG(TAG, "  Task Priority: %u", this->task_priority_);
  ESP_LOGCONFIG(TAG, "  Task Core: %u", this->task_core_);
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "Sensors:");
  for (auto *s : this->sensors_)
    LOG_SENSOR("    ", "Sound Pressure Level", s);
}

void SoundLevelMeter::setup() {
  this->sort_sensors();
  xTaskCreatePinnedToCore(SoundLevelMeter::task, "sound_level_meter", this->task_stack_size_, this,
                          this->task_priority_, nullptr, this->task_core_);
}

void SoundLevelMeter::loop() {
  std::lock_guard<std::mutex> lock(this->defer_mutex_);
  if (!this->defer_queue_.empty()) {
    auto &f = this->defer_queue_.front();
    f();
    this->defer_queue_.pop_front();
  }
}

void SoundLevelMeter::turn_on() {
  std::lock_guard<std::mutex> lock(this->on_mutex_);
  this->reset();
  this->is_on_ = true;
  this->on_cv_.notify_one();
  if (this->is_high_freq_)
    this->high_freq_.start();
  ESP_LOGD(TAG, "Turned on");
}

void SoundLevelMeter::turn_off() {
  std::lock_guard<std::mutex> lock(this->on_mutex_);
  this->reset();
  this->is_on_ = false;
  this->on_cv_.notify_one();
  if (this->is_high_freq_)
    this->high_freq_.stop();
  ESP_LOGD(TAG, "Turned off");
}

void SoundLevelMeter::toggle() {
  if (this->is_on_)
    this->turn_off();
  else
    this->turn_on();
}

bool SoundLevelMeter::is_on() { return this->is_on_; }

void SoundLevelMeter::task(void *param) {
  SoundLevelMeter *this_ = reinterpret_cast<SoundLevelMeter *>(param);
  BufferStack<float> buffers(this_->buffer_size_);

  auto warmup_start = millis();
  while (millis() - warmup_start < this_->warmup_interval_)
    this_->i2s_->read_samples(buffers);
  uint32_t process_time = 0, process_count = 0;
  uint64_t process_start;
  if (this_->is_on_ && this_->is_high_freq_)
    this_->high_freq_.start();
  while (1) {
    {
      std::unique_lock<std::mutex> lock(this_->on_mutex_);
      this_->on_cv_.wait(lock, [this_] { return this_->is_on_; });
    }
    buffers.reset();
    if (this_->i2s_->read_samples(buffers)) {
      process_start = esp_timer_get_time();

      this_->process(buffers);

      process_time += esp_timer_get_time() - process_start;
      process_count += buffers.current().size();

      auto sr = this_->get_sample_rate();
      if (process_count >= sr * (this_->update_interval_ / 1000.f)) {
        auto t = uint32_t(float(process_time) / process_count * (sr / 1000.f));
        auto cpu_util = float(process_time) / 1000 / this_->update_interval_;
        ESP_LOGD(TAG, "CPU (Core %u) Utilization: %.1f %%", xPortGetCoreID(), cpu_util * 100);
        process_time = process_count = 0;
      }
    }
  }
}

// Arranging sensors in a sorted order so that those with the same
// filters (or prefix) appear consecutively. This enables more efficient
// computations by applying filters only once for each common prefix of filters
void SoundLevelMeter::sort_sensors() {
  std::sort(this->sensors_.begin(), this->sensors_.end(), [](SoundLevelMeterSensor *a, SoundLevelMeterSensor *b) {
    return std::lexicographical_compare(a->dsp_filters_.begin(), a->dsp_filters_.end(), b->dsp_filters_.begin(),
                                        b->dsp_filters_.end());
  });
}

void SoundLevelMeter::process(BufferStack<float> &buffers) {
  std::vector<Filter *> prefix;
  for (auto s : this->sensors_) {
    int i = 0, n = s->dsp_filters_.size(), m = prefix.size();
    // finding common prefx
    while (i < n && i < m && s->dsp_filters_[i] == prefix[i])
      i++;

    // discard applied filters beyond common prefix (if any)
    while (prefix.size() > i) {
      prefix.pop_back();
      buffers.pop();
    }

    // apply new filters from current sensor on top of common prefix
    for (; i < s->dsp_filters_.size(); i++) {
      auto f = s->dsp_filters_[i];
      buffers.push();
      f->process(buffers);
      prefix.push_back(f);
    }
    s->process(buffers);
  }
}

void SoundLevelMeter::defer(std::function<void()> &&f) {
  std::lock_guard<std::mutex> lock(this->defer_mutex_);
  this->defer_queue_.push_back(std::move(f));
}

void SoundLevelMeter::reset() {
  for (auto f : this->dsp_filters_)
    f->reset();
  for (auto s : this->sensors_)
    s->reset();
}

/* SoundLevelMeterSensor */

void SoundLevelMeterSensor::set_parent(SoundLevelMeter *parent) {
  this->parent_ = parent;
  this->update_samples_ = parent->get_sample_rate() * (parent->get_update_interval() / 1000.f);
}

void SoundLevelMeterSensor::set_update_interval(uint32_t update_interval) {
  this->update_samples_ = this->parent_->get_sample_rate() * (update_interval / 1000.f);
}

void SoundLevelMeterSensor::add_dsp_filter(Filter *dsp_filter) { this->dsp_filters_.push_back(dsp_filter); }

void SoundLevelMeterSensor::defer_publish_state(float state) {
  this->parent_->defer([this, state]() { this->publish_state(state); });
}

float SoundLevelMeterSensor::adjust_dB(float dB, bool is_rms) {
  // see: https://dsp.stackexchange.com/a/50947/65262
  if (is_rms)
    dB += DBFS_OFFSET;

  // see: https://invensense.tdk.com/wp-content/uploads/2015/02/AN-1112-v1.1.pdf
  // dBSPL = dBFS + mic_sensitivity_ref - mic_sensitivity
  // e.g. dBSPL = dBFS + 94 - (-26) = dBFS + 120
  if (this->parent_->get_mic_sensitivity().has_value() && this->parent_->get_mic_sensitivity_ref().has_value())
    dB += *this->parent_->get_mic_sensitivity_ref() - *this->parent_->get_mic_sensitivity();

  if (this->parent_->get_offset().has_value())
    dB += *this->parent_->get_offset();

  return dB;
}

/* SoundLevelMeterSensorEq */

void SoundLevelMeterSensorEq::process(std::vector<float> &buffer) {
  // as adding small floating point numbers with large ones might lead
  // to precision loss, we first accumulate local sum for entire buffer
  // and only in the end add it to global sum which could become quite large
  // for large accumulating periods (like 1 hour), therefore global sum (this->sum_)
  // is of type double
  float local_sum = 0;
  for (int i = 0; i < buffer.size(); i++) {
    local_sum += buffer[i] * buffer[i];
    this->count_++;
    if (this->count_ == this->update_samples_) {
      float dB = 10 * log10((sum_ + local_sum) / count_);
      dB = this->adjust_dB(dB);
      this->defer_publish_state(dB);
      this->sum_ = 0;
      this->count_ = 0;
      local_sum = 0;
    }
  }
  this->sum_ += local_sum;
}

void SoundLevelMeterSensorEq::reset() {
  this->sum_ = 0.;
  this->count_ = 0;
  this->defer_publish_state(NAN);
}

/* SoundLevelMeterSensorMax */

void SoundLevelMeterSensorMax::set_window_size(uint32_t window_size) {
  this->window_samples_ = this->parent_->get_sample_rate() * (window_size / 1000.f);
}

void SoundLevelMeterSensorMax::process(std::vector<float> &buffer) {
  for (int i = 0; i < buffer.size(); i++) {
    this->sum_ += buffer[i] * buffer[i];
    this->count_sum_++;
    if (this->count_sum_ == this->window_samples_) {
      this->max_ = std::max(this->max_, this->sum_ / this->count_sum_);
      this->sum_ = 0.f;
      this->count_sum_ = 0;
    }
    this->count_max_++;
    if (this->count_max_ == this->update_samples_) {
      float dB = 10 * log10(this->max_);
      dB = this->adjust_dB(dB);
      this->defer_publish_state(dB);
      this->max_ = std::numeric_limits<float>::min();
      this->count_max_ = 0;
    }
  }
}

void SoundLevelMeterSensorMax::reset() {
  this->sum_ = 0.f;
  this->max_ = std::numeric_limits<float>::min();
  this->count_max_ = 0;
  this->count_sum_ = 0;
  this->defer_publish_state(NAN);
}

/* SoundLevelMeterSensorMin */

void SoundLevelMeterSensorMin::set_window_size(uint32_t window_size) {
  this->window_samples_ = this->parent_->get_sample_rate() * (window_size / 1000.f);
}

void SoundLevelMeterSensorMin::process(std::vector<float> &buffer) {
  for (int i = 0; i < buffer.size(); i++) {
    this->sum_ += buffer[i] * buffer[i];
    this->count_sum_++;
    if (this->count_sum_ == this->window_samples_) {
      this->min_ = std::min(this->min_, this->sum_ / this->count_sum_);
      this->sum_ = 0.f;
      this->count_sum_ = 0;
    }
    this->count_min_++;
    if (this->count_min_ == this->update_samples_) {
      float dB = 10 * log10(this->min_);
      dB = this->adjust_dB(dB);
      this->defer_publish_state(dB);
      this->min_ = std::numeric_limits<float>::max();
      this->count_min_ = 0;
    }
  }
}

void SoundLevelMeterSensorMin::reset() {
  this->sum_ = 0.f;
  this->min_ = std::numeric_limits<float>::max();
  this->count_min_ = 0;
  this->count_sum_ = 0;
  this->defer_publish_state(NAN);
}

/* SoundLevelMeterSensorPeak */

void SoundLevelMeterSensorPeak::process(std::vector<float> &buffer) {
  for (int i = 0; i < buffer.size(); i++) {
    this->peak_ = std::max(this->peak_, abs(buffer[i]));
    this->count_++;
    if (this->count_ == this->update_samples_) {
      float dB = 20 * log10(this->peak_);
      dB = this->adjust_dB(dB, false);
      this->defer_publish_state(dB);
      this->peak_ = 0.f;
      this->count_ = 0;
    }
  }
}

void SoundLevelMeterSensorPeak::reset() {
  this->peak_ = 0.f;
  this->count_ = 0;
  this->defer_publish_state(NAN);
}

/* SOS_Filter */

SOS_Filter::SOS_Filter(std::initializer_list<std::initializer_list<float>> &&coeffs) {
  this->coeffs_.resize(coeffs.size());
  this->state_.resize(coeffs.size(), {});
  int i = 0;
  for (auto &row : coeffs)
    std::copy(row.begin(), row.end(), coeffs_[i++].begin());
}

// direct form 2 transposed
void SOS_Filter::process(std::vector<float> &data) {
  int n = data.size();
  int m = this->coeffs_.size();
  for (int j = 0; j < m; j++) {
    for (int i = 0; i < n; i++) {
      // y[i] = b0 * x[i] + s0
      float yi = this->coeffs_[j][0] * data[i] + this->state_[j][0];
      // s0 = b1 * x[i] - a1 * y[i] + s1
      this->state_[j][0] = this->coeffs_[j][1] * data[i] - this->coeffs_[j][3] * yi + this->state_[j][1];
      // s1 = b2 * x[i] - a2 * y[i]
      this->state_[j][1] = this->coeffs_[j][2] * data[i] - this->coeffs_[j][4] * yi;

      data[i] = yi;
    }
  }
}

void SOS_Filter::reset() {
  for (auto &s : this->state_)
    s = {0.f, 0.f};
}

/* BufferStack */

template<typename T> BufferStack<T>::BufferStack(uint32_t buffer_size) : buffer_size_(buffer_size) {
  this->buffers_.resize(1);
  this->buffers_[0].resize(buffer_size);
}

template<typename T> std::vector<T> &BufferStack<T>::current() { return this->buffers_[this->index_]; }

template<typename T> void BufferStack<T>::push() {
  this->index_++;
  if (this->index_ == this->buffers_.capacity()) {
    this->buffers_.reserve(this->index_ + 1);
    this->buffers_.resize(this->index_ + 1);
  }
  auto &dst = this->buffers_[this->index_];
  auto &src = this->buffers_[this->index_ - 1];
  auto n = src.size();
  dst.resize(n);
  // this is faster than assigning one vector to another, which results in element-wise copying
  memcpy(&dst[0], &src[0], n * sizeof(T));
}

template<typename T> void BufferStack<T>::pop() {
  assert(this->index_ >= 1 && "Index out of bounds");
  this->index_--;
}

template<typename T> void BufferStack<T>::reset() { this->index_ = 0; }

template<typename T> BufferStack<T>::operator std::vector<T> &() { return this->current(); }

}  // namespace sound_level_meter
}  // namespace esphome