#pragma once

#include <vector>
#include <driver/i2s.h>
#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace i2s {

class I2SComponent : public Component {
 public:
  void set_ws_pin(InternalGPIOPin *ws_pin);
  void set_bck_pin(InternalGPIOPin *bck_pin);
  void set_din_pin(InternalGPIOPin *din_pin);
  void set_dout_pin(InternalGPIOPin *dout_pin);
  void set_sample_rate(uint32_t sample_rate);
  uint32_t get_sample_rate() const;
  void set_bits_per_sample(uint8_t bits_per_sample);
  uint8_t get_bits_per_sample() const;
  void set_dma_buf_count(int dma_buf_count);
  int get_dma_buf_count() const;
  void set_dma_buf_len(int dma_buf_len);
  int get_dma_buf_len() const;
  void set_use_apll(bool use_apll);
  bool get_use_apll() const;
  void set_bits_shift(uint8_t bits_shift);
  uint8_t get_bits_shift() const;
  bool read(uint8_t *data, size_t len, size_t *bytes_read, TickType_t ticks_to_wait = portMAX_DELAY);
  bool read_samples(int32_t *data, size_t num_samples, size_t *samples_read, TickType_t ticks_to_wait = portMAX_DELAY);
  bool read_samples(int16_t *data, size_t num_samples, size_t *samples_read, TickType_t ticks_to_wait = portMAX_DELAY);
  bool read_samples(float *data, size_t num_samples, size_t *samples_read, TickType_t ticks_to_wait = portMAX_DELAY);
  bool read_samples(std::vector<float> &data, TickType_t ticks_to_wait = portMAX_DELAY);
  void set_channel(i2s_channel_fmt_t channel);
  virtual void setup() override;
  virtual void dump_config() override;
  virtual float get_setup_priority() const override;

 protected:
  InternalGPIOPin *ws_pin_{nullptr};
  InternalGPIOPin *bck_pin_{nullptr};
  InternalGPIOPin *din_pin_{nullptr};
  InternalGPIOPin *dout_pin_{nullptr};

  uint32_t sample_rate_{48000};
  uint8_t bits_per_sample_{32};
  uint8_t port_num_{0};
  int dma_buf_count_{8};
  int dma_buf_len_{256};
  bool use_apll_{false};
  uint8_t bits_shift_{0};
  i2s_channel_fmt_t channel_{I2S_CHANNEL_FMT_ONLY_RIGHT};
};
}  // namespace i2s
}  // namespace esphome
