# ESPHome Sound Level Meter [![CI](https://github.com/stas-sl/esphome-sound-level-meter/actions/workflows/ci.yaml/badge.svg)](https://github.com/stas-sl/esphome-sound-level-meter/actions/workflows/ci.yaml)

> [!NOTE]
> This component was originally developed a few years ago when ESPHome lacked official support for I2S audio. Since then, ESPHome has introduced its own official I2S [microphone](https://esphome.io/components/microphone/i2s_audio.html) and [sound_level](https://esphome.io/components/sensor/sound_level.html) components. For basic sound level measurements, I recommend using these official components, as they offer better compatibility with newer ESP32 variants. However, my component remains useful if you require advanced features like A/C-weighting or custom microphone equalization, which are not available in the official implementations yet.

This component was made to measure environmental noise levels (Leq, Lmin, Lmax, Lpeak) with different frequency weightings over configured time intervals. It is heavily based on awesome work by Ivan Kostoski: [esp32-i2s-slm](https://github.com/ikostoski/esp32-i2s-slm) (his [hackaday.io project](https://hackaday.io/project/166867-esp32-i2s-slm)).

<img width="488" alt="esphome sound level meter" src="https://github.com/user-attachments/assets/442a9b5d-4607-4d39-945a-9949f19904e0">

Typical weekly traffic noise recorded with a microphone located 50m from a medium traffic road:

<img width="1187" alt="image" src="https://user-images.githubusercontent.com/4602302/224789124-a86224c9-c11d-4972-a564-b042bab97bcb.png">


## Configuration

Add it to your ESPHome config:

```yaml
external_components:
  - source: github://stas-sl/esphome-sound-level-meter  # add @tag if you want to use a specific version (e.g @v1.0.0)
```

For configuration options see [minimal-example-config.yaml](configs/minimal-example-config.yaml) or [advanced-example-config.yaml](configs/advanced-example-config.yaml). I would recommend to start from minimal config, and if it works and produces reasonable values (30-100 dB SPL), then add more filters/sensors.

```yaml
i2s:
  bck_pin: 23                   # also labeled as SCK
  ws_pin: 18
  din_pin: 19                   # also labeled as SD
  sample_rate: 48000            # default: 48000

  # Common settings are bits_per_sample: 16 and bits_shift: 0,
  # which should work for most microphones. A 16-bit resolution provides
  # a 96 dB dynamic range, sufficient to cover the dynamic range of most
  # MEMS microphones.
  # Alternatively, if your mic supports 24 bit resolution, you can
  # try bits_per_sample: 32 and bits_shift: 8
  bits_per_sample: 16           # default: 16
  mclk_multiple: 256            # default: 256
  dma_buf_count: 8              # default: 8
  dma_buf_len: 256              # default: 256
  use_apll: true                # default: false

  # according to datasheet when L/R pin is connected to GND,
  # the mic should output its signal in the left channel,
  # but on some boards I've encountered the opposite, 
  # so you can try both
  channel: right                # default: right

  # right shift samples.
  # for example if mic has 24 bit resolution, and
  # i2s configured as 32 bits, then audio data will be aligned left (MSB)
  # and LSB will be padded with zeros, so you might want to shift them right by 8 bits
  bits_shift: 0                 # default: 0

sound_level_meter:
  id: sound_level_meter1

  # update_interval specifies over which interval to aggregate audio data
  # you can specify default update_interval on top level, but you can also override
  # it further by specifying it on sensor level
  update_interval: 60s           # default: 60s

  # you can disable (turn off) component by default (on boot)
  # and turn it on later when needed via sound_level_meter.turn_on/toggle actions;
  # when used with switch it might conflict/being overriden by
  # switch state restoration logic, so you have to either disable it in
  # switch config and then is_on property here will have effect, 
  # or completely rely on switch state restoration/initialization and 
  # any value set here will be ignored
  is_on: true                   # default: true

  # buffer_size is in samples (not bytes), so for float data type
  # number of bytes will be buffer_size * 4
  buffer_size: 1024             # default: 1024

  # ignore audio data at startup for this long
  warmup_interval: 500ms        # default: 500ms

  # audio processing runs in a separate task, you can change its settings below
  task_stack_size: 4096         # default: 4096
  task_priority: 2              # default: 2
  task_core: 1                  # default: 1

  # see your mic datasheet to find sensitivity and reference SPL.
  # those are used to convert dB FS to db SPL

  # if omitted, the reported values will be in dB FS units
  mic_sensitivity: -26dB        # default: empty
  mic_sensitivity_ref: 94dB     # default: empty
  # additional offset if needed
  offset: 0dB                   # default: empty

  # under dsp_filters section you can define multiple filters,
  # which can be referenced later by each sensor
  
  # for now only SOS filter type is supported, see math/filter-design.ipynb
  # to learn how to create or convert other filter types to SOS

  # note, that those coefficients are only applicable for
  # specific sample rate (48 kHz), if you will use
  # other value, then you need to update these coefficicients
  dsp_filters:
    - id: f_inmp441             # INMP441 mic eq @ 48kHz
      type: sos
      coeffs:
        #       b0          b1          b2          a1          a2          
        - [ 1.0019784 , -1.9908513, 0.9889158 , -1.9951786, 0.99518436 ]
    - id: f_a                   # A weighting @ 48kHz
      type: sos
      coeffs:
        #        b0            b1            b2            a1            a2            
        - [ 0.16999495  , 0.741029    , 0.52548885  , -0.11321865 , -0.056549273 ]
        - [ 1.          , -2.00027    , 1.0002706   , -0.03433284 , -0.79215795  ]
        - [ 1.          , -0.709303   , -0.29071867 , -1.9822421  , 0.9822986    ]
    - id: f_c                   # C weighting @ 48kHz
      type: sos
      coeffs:
        #        b0             b1             b2             a1             a2             
        - [ -0.49651518  , -0.12296628  , -0.0076134163, -0.37165618  , 0.03453208    ]
        - [ 1.           , 1.3294908    , 0.44188643   , 1.2312505    , 0.37899444    ]
        - [ 1.           , -2.          , 1.           , -1.9946145   , 0.9946217     ]
  sensors:
    # 'eq' type sensor calculates Leq (average) sound level over specified period
    - type: eq
      name: LZeq_1s
      id: LZeq_1s
      # you can override updated_interval specified on top level
      # individually per each sensor
      update_interval: 1s

      # The dsp_filters field is a list of filter IDs defined
      # in the main component's corresponding section.
      # If you're only using a single filter, you can omit the brackets.
      # Alternatively, instead of referencing an existing filter,
      # you can define a filter directly here in the same format
      # as in the main component. However, this filter won't be reusable
      # across other sensors, so it’s best to use this approach only when
      # each sensor requires its own unique filters that don't overlap with others.
      dsp_filters: [f_inmp441]

    # you can have as many sensors of same type, but with different
    # other parameters (e.g. update_interval) as needed
    - type: eq
      name: LZeq_1min
      id: LZeq_1min
      unit_of_measurement: dBZ
      # another syntax for specifying a list
      dsp_filters: 
        - f_inmp441

    # 'max' sensor type calculates Lmax with specified window_size.
    # for example, if update_interval is 60s and window_size is 1s
    # then it will calculate 60 Leq values for each second of audio data
    # and the result will be max of them
    - type: max
      name: LZmax_1s_1min
      id: LZmax_1s_1min
      window_size: 1s
      unit_of_measurement: dBZ
      # you can omit brackets, if there is only single element
      dsp_filters: f_inmp441

    # same as 'max', but 'min'
    - type: min
      name: LZmin_1s_1min
      id: LZmin_1s_1min
      window_size: 1s
      unit_of_measurement: dBZ
      # it is also possible to define filter right in place, 
      # though it would be considered as a different filter even
      # if it has the same coeffiecients as other defined filters, 
      # so previous calculations could not be reused, there
      dsp_filters:
        - type: sos
          coeffs:
            #       b0          b1          b2          a1          a2          
            - [ 1.0019784 , -1.9908513, 0.9889158 , -1.9951786, 0.99518436 ]

    # it finds max single sample over whole update_interval
    - type: peak
      name: LZpeak_1min
      id: LZpeak_1min
      unit_of_measurement: dBZ

    - type: eq
      name: LAeq_1min
      id: LAeq_1min
      unit_of_measurement: dBA
      dsp_filters: [f_inmp441, f_a]
    - type: max
      name: LAmax_1s_1min
      id: LAmax_1s_1min
      window_size: 1s
      unit_of_measurement: dBA
      dsp_filters: [f_inmp441, f_a]
    - type: min
      name: LAmin_1s_1min
      id: LAmin_1s_1min
      window_size: 1s
      unit_of_measurement: dBA
      dsp_filters: [f_inmp441, f_a]
    - type: peak
      name: LApeak_1min
      id: LApeak_1min
      unit_of_measurement: dBA
      dsp_filters: [f_inmp441, f_a]

    - type: eq
      name: LCeq_1min
      id: LCeq_1min
      unit_of_measurement: dBC
      dsp_filters: [f_inmp441, f_c]
    - type: max
      name: LCmax_1s_1min
      id: LCmax_1s_1min
      window_size: 1s
      unit_of_measurement: dBC
      dsp_filters: [f_inmp441, f_c]
    - type: min
      name: LCmin_1s_1min
      id: LCmin_1s_1min
      window_size: 1s
      unit_of_measurement: dBC
      dsp_filters: [f_inmp441, f_c]
    - type: peak
      name: LCpeak_1min
      id: LCpeak_1min
      unit_of_measurement: dBC
      dsp_filters: [f_inmp441, f_c]

# automation
# available actions:
#   - sound_level_meter.turn_on
#   - sound_level_meter.turn_off
#   - sound_level_meter.toggle
switch:
  - platform: template
    name: "Sound Level Meter Switch"
    icon: mdi:power
    # if you want is_on property on component to have effect, then set
    # restore_mode to DISABLED, or alternatively you can use other modes
    # (more on them in esphome docs), then is_on property on the component will
    # be overriden by the switch
    restore_mode: DISABLED # ALWAYS_OFF | ALWAYS_ON | RESTORE_DEFAULT_OFF | RESTORE_DEFAULT_ON
    lambda: |-
      return id(sound_level_meter1).is_on();
    turn_on_action:
      - sound_level_meter.turn_on
    turn_off_action:
      - sound_level_meter.turn_off

button:
  - platform: template
    name: "Sound Level Meter Toggle Button"
    on_press:
      - sound_level_meter.toggle: sound_level_meter1

binary_sensor:
  - platform: gpio
    pin: GPIO0
    name: "Sound Level Meter GPIO Toggle"
    on_press:
      - sound_level_meter.toggle: sound_level_meter1
```

## 10 bands spectrum analyzer

[10-bands-spectrum-analyzer-example-config.yaml](configs/10-bands-spectrum-analyzer-example-config.yaml)

While manually specifying IIR/SOS filters might not be the most user-friendly approach, it offers great flexibility. This method allows you to use any filter you need, provided you know how to customize it to meet your requirements. Originally, my intention wasn’t to go beyond standard weighting functions like A/C, however to showcase the capabilities, I created a 10-band spectrum analyzer using ten 6th-order band-pass filters, each targeting a specific frequency band - simply by writing the appropriate config file, without needing to modify the component's source code.

For real-time visualization, I'm using web server number/slider controls to display the levels of each of the 10 bands. While this might not be the intended use of the sliders and web server - since they may not be designed for such frequent updates and it pushes the ESP32 to its limits, but it works 🤪

With 10 x 6 = 60 SOS filters, the component uses about 60-70% of the CPU, and I assume the web server also consumes some CPU power to send approximately 100 messages per second. So, this is quite a CPU-intensive task. I chose 6th-order filters somewhat arbitrarily; you could experiment with lower-order filters, which might meet your needs while using less CPU power.

https://github.com/user-attachments/assets/904a2a96-0c16-4797-9587-2558cc0c70f8

While this example serves as a stress test, you could also use it to monitor different frequencies over longer time intervals with less frequent updates.

<img width="1189" src="https://github.com/user-attachments/assets/8a23774d-46f2-4162-8504-4e46bfe50a80">

## Filter design (math)

Check out [filter-design notebook](math/filter-design.ipynb) to learn how those SOS coefficients were calculated.

## Sending data to sensor.community

See [sensor-community-example-config.yaml](configs/sensor-community-example-config.yaml)

## Performance

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

## Supported platforms

Tested with ESPHome version 2025.7.2, platforms:
- [x] ESP32 (Arduino v3.1.3, ESP-IDF v5.3.2)
- [x] ESP32-IDF (ESP-IDF v5.3.2)

## Troubleshooting

Setting up I2S microphones, including correct wiring, identifying the right pins, and finding the correct setting values, can be tricky and frustrating, especially if you're doing it for the first time (and even if you're not). With numerous different boards and microphones, each with its own peculiarities, it can be a challenge. Below, I’ll summarize the best advice for troubleshooting if things don't work on the first attempt.

1. Double check your wires. I believe this is the most frequent source of mistakes. Try connecting L/R to GND or to VCC; or alternatively specify left or right channel in i2s configuration. Try different PINS, as some different boards might use some PINS for other purposes.

2. If you are receiving `-inf` values, it indicates that the input data consists entirely of zeros. This typically means you either have incorrect wiring or an incorrect PIN assignment in your i2s config.

3. I would recommend starting from [minimal-example-config.yaml](configs/minimal-example-config.yaml) and ensuring that it produces values in reasonable range (30-100 dB SPL) and it reacts accordingly if you clap or produce louder noises, before proceeding further.

4. Most microphones should work with `sample_rate: 16` and `bits_shift: 0`, but you can try other settings like `sample_rate: 32` and `bits_shift: 8` for 24 bits microphones.

5. I tested it only on ESP32/ESP32 S3, that have 2 CPU cores, so not sure how it will work on other chips.

6. If you are experiencing performance issues, set logger level to `DEBUG` and the component will print CPU utilization, which shouldn't be higher than 80%. 

7. If you've set up everything correctly, then in a quite room the lowest LAeq levels should correspond to the noise floor levels from your mic specification. For example for INMP441 it should be ~33 dBA +/- few dBs. Don't confuse this with dBZ levels, which do not have A-weighting applied and can therefore be 5-15 dB higher.

8. Sometimes, even in complete silence, LAeq values may not reach the noise floor level. This can indicate the presence of other noises caused by electromagnetic or RF interference, which may be due to the WiFi module, a poor power supply, long wires, etc. For instance, on one board, I couldn’t achieve lower than 40-45 dBA until I reduced the WiFi power by setting `output_power: 8.5 dB` in config, after which I immediately obtained the expected 33 dBA.

9. If none of the above helps, debugging may become more challenging. I personally spent plenty of hours troubleshooting I2S issues by writing specific sketches to print audio data or to record it for playback and analysis on my PC. Another option might be to try native ESPHome's i2s_audio/microphone components to ensure that your board/microphone can work together and then use those settings that worked whith this component. Or maybe even try some other examples/projects that work with i2s audio.

## References

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
