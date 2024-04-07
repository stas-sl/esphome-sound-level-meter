#include "i2s.h"

namespace esphome {
namespace i2s {

static const char *const TAG = "i2s";

void I2SComponent::set_ws_pin(InternalGPIOPin *ws_pin) { this->ws_pin_ = ws_pin; }
void I2SComponent::set_bck_pin(InternalGPIOPin *bck_pin) { this->bck_pin_ = bck_pin; }
void I2SComponent::set_din_pin(InternalGPIOPin *din_pin) { this->din_pin_ = din_pin; }
void I2SComponent::set_dout_pin(InternalGPIOPin *dout_pin) { this->dout_pin_ = dout_pin; }
void I2SComponent::set_sample_rate(uint32_t sample_rate) { this->sample_rate_ = sample_rate; }
uint32_t I2SComponent::get_sample_rate() const { return this->sample_rate_; }
void I2SComponent::set_bits_per_sample(uint8_t bits_per_sample) { this->bits_per_sample_ = bits_per_sample; }
uint8_t I2SComponent::get_bits_per_sample() const { return this->bits_per_sample_; }
void I2SComponent::set_dma_buf_count(int dma_buf_count) { this->dma_buf_count_ = dma_buf_count; }
int I2SComponent::get_dma_buf_count() const { return this->dma_buf_count_; }
void I2SComponent::set_dma_buf_len(int dma_buf_len) { this->dma_buf_len_ = dma_buf_len; }
int I2SComponent::get_dma_buf_len() const { return this->dma_buf_len_; }
void I2SComponent::set_use_apll(bool use_apll) { this->use_apll_ = use_apll; }
bool I2SComponent::get_use_apll() const { return this->use_apll_; }
void I2SComponent::set_bits_shift(uint8_t bits_shift) { this->bits_shift_ = bits_shift; }
uint8_t I2SComponent::get_bits_shift() const { return this->bits_shift_; }
float I2SComponent::get_setup_priority() const { return setup_priority::BUS; }
void I2SComponent::set_channel(i2s_channel_fmt_t channel) { this->channel_ = channel; }

void I2SComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "I2S %d:", this->port_num_);
  LOG_PIN("  WS Pin: ", this->ws_pin_);
  LOG_PIN("  BCK Pin: ", this->bck_pin_);
  LOG_PIN("  DIN Pin: ", this->din_pin_);
  LOG_PIN("  DOUT Pin: ", this->dout_pin_);
  ESP_LOGCONFIG(TAG, "  Sample Rate: %u", this->sample_rate_);
  ESP_LOGCONFIG(TAG, "  Bits Per Sample: %u", this->bits_per_sample_);
  ESP_LOGCONFIG(TAG, "  DMA Buf Count: %u", this->dma_buf_count_);
  ESP_LOGCONFIG(TAG, "  DMA Buf Len: %u", this->dma_buf_len_);
  ESP_LOGCONFIG(TAG, "  Use APLL: %s", YESNO(this->use_apll_));
  ESP_LOGCONFIG(TAG, "  Bits Shift: %u", this->bits_shift_);
  ESP_LOGCONFIG(TAG, "  Channel: %s",
                this->channel_ == I2S_CHANNEL_FMT_ONLY_RIGHT  ? "right"
                : this->channel_ == I2S_CHANNEL_FMT_ONLY_LEFT ? "left"
                                                              : "invalid");
}

bool I2SComponent::read(uint8_t *data, size_t len, size_t *bytes_read, TickType_t ticks_to_wait) {
  esp_err_t err = i2s_read(i2s_port_t(this->port_num_), data, len, bytes_read, ticks_to_wait);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "i2s_read failed: %s", esp_err_to_name(err));
    return false;
  }

  return true;
}

bool I2SComponent::read_samples(int32_t *data, size_t num_samples, size_t *samples_read, TickType_t ticks_to_wait) {
  if (this->bits_per_sample_ <= 16) {
    ESP_LOGE(TAG,
             "read_samples: int32 output data pointer should be used with 24 or 32 bit sampling, but current "
             "bits_per_samples is %u",
             this->bits_per_sample_);
    return false;
  }
  size_t bytes_read;
  bool ok = this->read(reinterpret_cast<uint8_t *>(data), num_samples * sizeof(int32_t), &bytes_read, ticks_to_wait);
  if (ok) {
    *samples_read = bytes_read / sizeof(int32_t);
    if (this->bits_shift_ > 0)
      for (int i = 0; i < *samples_read; i++)
        data[i] >>= this->bits_shift_;
    return ok;
  } else {
    return false;
  }
}

bool I2SComponent::read_samples(int16_t *data, size_t num_samples, size_t *samples_read, TickType_t ticks_to_wait) {
  if (this->bits_per_sample_ > 16) {
    ESP_LOGE(TAG,
             "read_samples: int16 output data pointer should be used with 8 or 16 bit sampling, but current "
             "bits_per_samples is %u",
             this->bits_per_sample_);
    return false;
  }
  size_t bytes_read;
  bool ok = this->read(reinterpret_cast<uint8_t *>(data), num_samples * sizeof(int16_t), &bytes_read, ticks_to_wait);
  if (ok) {
    *samples_read = bytes_read / sizeof(int16_t);
    if (this->bits_shift_ > 0)
      for (int i = 0; i < *samples_read; i++)
        data[i] >>= this->bits_shift_;
    return ok;
  } else {
    return false;
  }
}

bool I2SComponent::read_samples(float *data, size_t num_samples, size_t *samples_read, TickType_t ticks_to_wait) {
  uint8_t bytes_per_sample = this->bits_per_sample_ <= 16 ? 2 : 4;
  const float max_value = (1UL << (bytes_per_sample * 8 - this->bits_shift_ - 1)) - 1;
  bool ok;
  if (this->bits_per_sample_ <= 16)
    ok = this->read_samples(reinterpret_cast<int16_t *>(data), num_samples, samples_read, ticks_to_wait);
  else
    ok = this->read_samples(reinterpret_cast<int32_t *>(data), num_samples, samples_read, ticks_to_wait);
  if (ok) {
    if (this->bits_per_sample_ <= 16) {
      int16_t *data_i16 = reinterpret_cast<int16_t *>(data);
      for (int i = *samples_read - 1; i >= 0; i--)
        data[i] = data_i16[i] / max_value;
    } else {
      int32_t *data_i32 = reinterpret_cast<int32_t *>(data);
      for (int i = *samples_read - 1; i >= 0; i--)
        data[i] = data_i32[i] / max_value;
    }
    return ok;
  } else {
    return false;
  }
}

bool I2SComponent::read_samples(std::vector<float> &data, TickType_t ticks_to_wait) {
  size_t samples_read;
  bool result = this->read_samples(data.data(), data.capacity(), &samples_read, ticks_to_wait);
  data.resize(samples_read);
  return result;
}

void I2SComponent::setup() {
  static uint8_t next_port_num = 0;
  this->port_num_ = next_port_num++;

  ESP_LOGCONFIG(TAG, "Setting up I2S %u ...", this->port_num_);

  i2s_config_t i2s_config = {.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
                             .sample_rate = this->sample_rate_,
                             .bits_per_sample = i2s_bits_per_sample_t(this->bits_per_sample_),
                             .channel_format = this->channel_,
                             .communication_format = I2S_COMM_FORMAT_STAND_I2S,
                             .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
                             .dma_buf_count = this->dma_buf_count_,
                             .dma_buf_len = this->dma_buf_len_,
                             .use_apll = this->use_apll_,
                             .tx_desc_auto_clear = false,
                             .fixed_mclk = 0,
                             .mclk_multiple = I2S_MCLK_MULTIPLE_DEFAULT,
                             .bits_per_chan = i2s_bits_per_chan_t(0)};

  i2s_pin_config_t i2s_pin_config = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = this->bck_pin_->get_pin(),
      .ws_io_num = this->ws_pin_->get_pin(),
      .data_out_num = this->dout_pin_ != nullptr ? this->dout_pin_->get_pin() : I2S_PIN_NO_CHANGE,
      .data_in_num = this->din_pin_ != nullptr ? this->din_pin_->get_pin() : I2S_PIN_NO_CHANGE};

  esp_err_t err = i2s_driver_install(i2s_port_t(this->port_num_), &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "i2s_driver_install failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }

  err = i2s_set_pin(i2s_port_t(this->port_num_), &i2s_pin_config);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "i2s_set_pin failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }
}
}  // namespace i2s
}  // namespace esphome
