# ESPHome Sound Level Meter

This component is heavily based on awesome work by Ivan Kostoski: [esp32-i2s-slm](https://github.com/ikostoski/esp32-i2s-slm) (his [hackaday.io project](https://hackaday.io/project/166867-esp32-i2s-slm))

Add it to your ESPHome config:

```yaml
external_components:
  - source: github://stas-sl/esphome-sound-level-meter
```

For configuration options see [example-config.yaml](example-config.yaml):

```yaml
i2s:
  bck_pin: 23
  ws_pin: 18
  din_pin: 19
  sample_rate: 48000
  bits_per_sample: 32
  dma_buf_count: 8
  dma_buf_len: 256
  use_apll: true
  bits_shift: 8

sound_level_meter:
  update_interval: 1s
  buffer_size: 1000
  warmup_interval: 500ms
  task_stack_size: 4096
  task_priority: 2
  task_core: 1
  mic_sensitivity: -26dB
  mic_sensitivity_ref: 94dB
  offset: 0dB
  groups:
    - filters:
        - type: sos
          coeffs:
            #      b0            b1             b2           a1             a2
            # INMP441:
            - [ 1.0019784  , -1.9908513  ,  0.9889158  , -1.9951786  ,  0.99518436 ]
            # A-weighting:
            - [ 0.16999495 ,  0.741029   ,  0.52548885 , -0.11321865 , -0.056549273]
            - [ 1.         , -2.00027    ,  1.0002706  , -0.03433284 , -0.79215795 ]
            - [ 1.         , -0.709303   , -0.29071867 , -1.9822421  ,  0.9822986  ]
      sensors:
        - type: eq
          name: LAeq_1s
        - type: eq
          name: LAeq_1min
          update_interval: 1min # override defaults from component
          id: LAeq
        - type: max
          name: LAmax_1s_125ms
          window_size: 125ms
        - type: min
          name: LAmin_1s_125ms
          window_size: 125ms
        - type: peak
          name: LApeak_1s
```

### Performance

In Ivan's project SOS filters are implemented using ESP32 assembler, so they are really fast. A quote from him:

> Well, now you can lower the frequency of ESP32 down to 80MHz (i.e. for battery operation) and filtering and summation of I2S data will still take less than 15% of single core processing time. At 240MHz, filtering 1/8sec worth of samples with 2 x 6th-order IIR filters takes less than 5ms.

I'm not so familiar with assembler and it is hard to understand and maintain, so I implemented filtering in regular C++. Looks like the performance is not that bad. At 80MHz filtering and summation takes ~210ms per 1s of audio (48000 samples), which is 21% of single core processing time (vs. 15% if implemented in ASM). At 240MHz same task takes 67ms (vs. 5x8=40ms in ASM).

| CPU Freq | # SOS | Sensors                        | Sample Rate | Buffer size | Time (per 1s audio) |
| -------- | ----- | ------------------------------ | ----------- | ----------- | ------------------- |
| 80MHz    | 0     | 1 Leq                          | 48000       | 1024        | 57 ms               |
| 80MHz    | 6     | 1 Leq                          | 48000       | 1024        | 204 ms              |
| 80MHz    | 6     | 1 Lmax                         | 48000       | 1024        | 211 ms              |
| 80MHz    | 6     | 1 Lpeak                        | 48000       | 1024        | 207 ms              |
| 240MHz   | 0     | 1 Leq                          | 48000       | 1024        | 18 ms               |
| 240MHz   | 6     | 1 Leq                          | 48000       | 1024        | 67 ms               |
| 240MHz   | 6     | 1 Leq, 1 Lpeak, 1 Lmax, 1 Lmin | 48000       | 1024        | 90 ms               |

### Supported platforms

Tested with ESPHome version 2023.2.0, platforms:
[x] ESP32 (Arduino v2.0.5, ESP-IDF v4.4.2)
[x] ESP32-IDF (ESP-IDF v4.4.2)

### References

1. [ESP32-I2S-SLM hackaday.io project](https://hackaday.io/project/166867-esp32-i2s-slm)
1. [Measuring Audible Noise in Real-Time hackaday.io project](https://hackaday.io/project/162059-street-sense/log/170825-measuring-audible-noise-in-real-time)
1. [What are LAeq and LAFmax?](https://www.nti-audio.com/en/support/know-how/what-are-laeq-and-lafmax)
1. [Noise measuring @ smartcitizen.me](https://docs.smartcitizen.me/Components/sensors/air/Noise)
1. [EspAudioSensor](https://revspace.nl/EspAudioSensor)
1. [Design of a digital A-weighting filter with arbitrary sample rate (dsp.stackexchange.com)](https://dsp.stackexchange.com/questions/36077/design-of-a-digital-a-weighting-filter-with-arbitrary-sample-rate)
1. [How to compute dBFS? (dsp.stackexchange.com)](https://dsp.stackexchange.com/questions/8785/how-to-compute-dbfs)
1. [Microphone Specification Explained](https://invensense.tdk.com/wp-content/uploads/2015/02/AN-1112-v1.1.pdf)
1. [esp32-i2s-slm source code](https://github.com/ikostoski/esp32-i2s-slm)
1. [DNMS source code](https://github.com/hbitter/DNMS)
1. [NoiseLevel source code](https://github.com/bertrik/NoiseLevel)
