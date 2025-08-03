#include "sound_level_meter.h"

namespace esphome::sound_level_meter {

static constexpr const char *TAG = "sound_level_meter";

// By definition dBFS value of a full-scale sine wave equals to 0.
// Since the RMS of the full-scale sine wave is 1/sqrt(2), multiplying rms(signal) by sqrt(2)
// ensures that the formula evaluates to 0 when signal is a full-scale sine wave.
// This is equivalent to adding DBFS_OFFSET
// see: https://dsp.stackexchange.com/a/50947/65262
static constexpr float DBFS_OFFSET = 20 * log10(sqrt(2));
static constexpr uint32_t AUDIO_BUFFER_DURATION_MS = 20;

/* SoundLevelMeter */

void SoundLevelMeter::set_update_interval(uint32_t update_interval_ms) {
  this->update_interval_ms_ = update_interval_ms;
}
uint32_t SoundLevelMeter::get_update_interval() { return this->update_interval_ms_; }
void SoundLevelMeter::set_ring_buffer_size(uint32_t ring_buffer_size_ms) {
  this->ring_buffer_size_ms_ = ring_buffer_size_ms;
}
uint32_t SoundLevelMeter::get_ring_buffer_size() { return this->ring_buffer_size_ms_; }
void SoundLevelMeter::set_microphone_source(microphone::MicrophoneSource *microphone_source) {
  this->microphone_source_ = microphone_source;
}
void SoundLevelMeter::set_warmup_interval(uint32_t warmup_interval_ms) {
  this->warmup_interval_ms_ = warmup_interval_ms;
}
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
void SoundLevelMeter::set_is_auto_start(bool is_auto_start) { this->is_auto_start_ = is_auto_start; }
void SoundLevelMeter::add_sensor(SoundLevelMeterSensor *sensor) { this->sensors_.push_back(sensor); }
void SoundLevelMeter::add_dsp_filter(Filter *dsp_filter) { this->dsp_filters_.push_back(dsp_filter); }

audio::AudioStreamInfo SoundLevelMeter::get_audio_stream_info() const {
  return this->microphone_source_->get_audio_stream_info();
}

uint32_t SoundLevelMeter::ms_to_frames(uint32_t ms) {
  return this->get_audio_stream_info().get_sample_rate() * (ms / 1000.f);
}

void SoundLevelMeter::dump_config() {
  ESP_LOGCONFIG(TAG, "Sound Level Meter:");
  ESP_LOGCONFIG(TAG, "  Ring Buffer Size: %u ms)", this->ring_buffer_size_ms_);
  ESP_LOGCONFIG(TAG, "  Warmup Interval: %lu ms", this->warmup_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Task Stack Size: %lu", this->task_stack_size_);
  ESP_LOGCONFIG(TAG, "  Task Priority: %u", this->task_priority_);
  ESP_LOGCONFIG(TAG, "  Task Core: %u", this->task_core_);
  ESP_LOGCONFIG(TAG, "  High Freq: %s", YESNO(this->is_high_freq_));
  ESP_LOGCONFIG(TAG, "  Auto Start: %s", YESNO(this->is_auto_start_));
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "Sensors:");
  for (auto *s : this->sensors_)
    LOG_SENSOR("    ", "Sound Pressure Level", s);
}

void SoundLevelMeter::setup() {
  this->sort_sensors();

  this->microphone_source_->add_data_callback([this](const std::vector<uint8_t> &data) {
    auto ring_buffer = this->ring_buffer_weak_.lock();
    if (ring_buffer) {
      size_t bytes_free = ring_buffer->free();
      if (bytes_free < data.size()) {
        defer([] { ESP_LOGW(TAG, "Not enough free bytes in ring buffer to store incoming audio data."); });
      }
      ring_buffer->write((void *) data.data(), data.size());
      this->ring_buffer_stats_free_ = std::min(ring_buffer->free(), this->ring_buffer_stats_free_);
    }
  });

  if (this->is_auto_start_) {
    this->start();
  }
}

void SoundLevelMeter::loop() {
  // Process no more than 5 items per loop iteration.
  // When there are many sensors with short update intervals,
  // a large number of state updates (publish_state) may be queued.
  // Publishing state is a relatively expensive operation, so calling
  // it more than 10–20 times per iteration could trigger a warning
  // that the component is taking too long to operate. Therefore, we
  // limit the number of updates per iteration. The loop runs approximately
  // 100 times per second, so any remaining items will be processed
  // in the next iteration. Processing only one item per iteration is too
  // restrictive, as in extreme cases with many updates - say,
  // 100 per second - we might hit performance limits. Thus, we set a maximum
  // of 5 items per iteration, allowing up to 500 sensor updates per second
  // in theory, which should be more than sufficient for most scenarios.
  std::vector<std::function<void()>> tasks;
  {
    uint32_t max_items = 5;
    std::lock_guard<std::mutex> lock(this->defer_mutex_);
    for (int i = 0; i < max_items && !this->defer_queue_.empty(); i++) {
      tasks.push_back(std::move(this->defer_queue_.front()));
      this->defer_queue_.pop_front();
    }
  }
  for (auto &f : tasks) {
    f();
  }
}

void SoundLevelMeter::start() {
  if (!this->is_running_) {
    xTaskCreatePinnedToCore(SoundLevelMeter::task, "sound_level_meter", this->task_stack_size_, this,
                            this->task_priority_, &this->task_handle_, this->task_core_);
    ESP_LOGD(TAG, "Sound Level Meter started");
  }
}

void SoundLevelMeter::stop() {
  if (this->is_running() && !this->is_pending_stop_) {
    this->is_pending_stop_ = true;
    ESP_LOGD(TAG, "Sound Level Meter stopped");
  }
}

bool SoundLevelMeter::is_running() { return this->is_running_; }

void SoundLevelMeter::task(void *param) {
  SoundLevelMeter *this_ = reinterpret_cast<SoundLevelMeter *>(param);
  this_->is_running_ = true;
  {
    this_->ring_buffer_ = RingBuffer::create(this_->get_audio_stream_info().ms_to_bytes(this_->ring_buffer_size_ms_));
    this_->ring_buffer_weak_ = this_->ring_buffer_;
    BufferStack<float> buffers(this_->ms_to_frames(AUDIO_BUFFER_DURATION_MS));

    this_->reset();

    this_->microphone_source_->start();

    for (auto &s : this_->sensors_) {
      s->update_samples_ = this_->ms_to_frames(s->update_interval_ms_);
    }

    if (this_->is_high_freq_)
      this_->high_freq_.start();

    auto warmup_start = millis();
    while (millis() - warmup_start < this_->warmup_interval_ms_)
      this_->read_samples(buffers, 2 * pdMS_TO_TICKS(AUDIO_BUFFER_DURATION_MS));

    uint32_t process_time = 0, process_count = 0;
    uint64_t process_start;

    while (!this_->is_pending_stop_) {
      if (!this_->microphone_source_->is_running()) {
        if (!this_->status_has_warning()) {
          this_->status_set_warning("Microphone isn't running, can't compute statistics");
          this_->reset();
        }
        delay(AUDIO_BUFFER_DURATION_MS);
        continue;
      }

      if (this_->status_has_warning()) {
        this_->status_clear_warning();
      }

      buffers.reset();

      if (this_->read_samples(buffers, 2 * pdMS_TO_TICKS(AUDIO_BUFFER_DURATION_MS)) > 0) {
        process_start = esp_timer_get_time();

        this_->process(buffers);

        process_time += esp_timer_get_time() - process_start;
        process_count += buffers.current().size();

        if (process_count >= this_->ms_to_frames(this_->update_interval_ms_)) {
          auto cpu_util = float(process_time) / 1000 / this_->update_interval_ms_;
          auto rb_size = this_->ring_buffer_->available() + this_->ring_buffer_->free();
          auto rb_util = float(rb_size - this_->ring_buffer_stats_free_) / rb_size;
          auto core = xPortGetCoreID();
          this_->defer([cpu_util, rb_util, core]() {
            ESP_LOGD(TAG, "CPU (Core %u) Utilization: %.1f%%, Ring Buffer Utilization: %.1f%%", core, cpu_util * 100,
                     rb_util * 100);
          });
          process_time = process_count = 0;
          this_->ring_buffer_stats_free_ = SIZE_MAX;
        }
      }
    }
  }
  this_->ring_buffer_.reset();
  this_->microphone_source_->stop();

  if (this_->is_high_freq_)
    this_->high_freq_.stop();

  this_->reset();

  this_->is_running_ = false;
  this_->is_pending_stop_ = false;
  auto handle = this_->task_handle_;
  this_->task_handle_ = nullptr;
  vTaskDelete(handle);
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

size_t SoundLevelMeter::read_samples(std::vector<float> &data, TickType_t ticks_to_wait) {
  uint8_t bytes_per_sample = this->get_audio_stream_info().samples_to_bytes(1);

  size_t bytes_read = this->ring_buffer_->read(data.data(), data.size() * bytes_per_sample, ticks_to_wait);
  size_t samples_read = bytes_read / bytes_per_sample;
  if (samples_read > 0) {
    data.resize(samples_read);
    auto data_as_uint8 = reinterpret_cast<const uint8_t *>(data.data());
    for (int i = bytes_read - bytes_per_sample, j = samples_read - 1; i >= 0; i -= bytes_per_sample, j--) {
      data[j] = audio::unpack_audio_sample_to_q31(&data_as_uint8[i], bytes_per_sample) / float(INT32_MAX);
    }
  }
  return samples_read;
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
  this->set_update_interval(parent->get_update_interval());
}

void SoundLevelMeterSensor::set_update_interval(uint32_t update_interval_ms) {
  this->update_interval_ms_ = update_interval_ms;
  this->update_samples_ = this->parent_->ms_to_frames(update_interval_ms);
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

void SoundLevelMeterSensorMax::set_window_size(uint32_t window_size_ms) {
  this->window_samples_ = this->parent_->ms_to_frames(window_size_ms);
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

void SoundLevelMeterSensorMin::set_window_size(uint32_t window_size_ms) {
  this->window_samples_ = this->parent_->ms_to_frames(window_size_ms);
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

void SOS_Filter::process(std::vector<float> &data) {
  int n = data.size();
  int m = this->coeffs_.size();
  for (int j = 0; j < m; j++) {
#ifdef USE_ESP_DSP  // esp-dsp uses direct form 2
#if defined(USE_ESP32_VARIANT_ESP32)
    dsps_biquad_f32_ae32(&data[0], &data[0], data.size(), &this->coeffs_[j][0], &this->state_[j][0]);
#elif defined(USE_ESP32_VARIANT_ESP32S3)
    dsps_biquad_f32_aes3(&data[0], &data[0], data.size(), &this->coeffs_[j][0], &this->state_[j][0]);
#elif defined(USE_ESP32_VARIANT_ESP32P4)
    dsps_biquad_f32_arp4(&data[0], &data[0], data.size(), &this->coeffs_[j][0], &this->state_[j][0]);
#else
    dsps_biquad_f32_ansi(&data[0], &data[0], data.size(), &this->coeffs_[j][0], &this->state_[j][0]);
#endif
#else  // I'm using direct form 2 transposed, which should be a bit more numerically stable
    for (int i = 0; i < n; i++) {
      // y[i] = b0 * x[i] + s0
      float yi = this->coeffs_[j][0] * data[i] + this->state_[j][0];
      // s0 = b1 * x[i] - a1 * y[i] + s1
      this->state_[j][0] = this->coeffs_[j][1] * data[i] - this->coeffs_[j][3] * yi + this->state_[j][1];
      // s1 = b2 * x[i] - a2 * y[i]
      this->state_[j][1] = this->coeffs_[j][2] * data[i] - this->coeffs_[j][4] * yi;

      data[i] = yi;
    }
#endif
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

template<typename T> void BufferStack<T>::reset() {
  this->index_ = 0;
  this->current().resize(this->buffer_size_);
}

template<typename T> BufferStack<T>::operator std::vector<T> &() { return this->current(); }

}  // namespace esphome::sound_level_meter