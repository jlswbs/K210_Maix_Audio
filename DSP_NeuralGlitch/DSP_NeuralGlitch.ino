// Neural cellular automaton glitch generator //

#include "timer.h"

#define SAMPLE_RATE 44100
#define TIMER_RATE 1000000000 / SAMPLE_RATE
#define PWM_RATE 1000000
#define AUDIO_PIN_L 6
#define AUDIO_PIN_R 8
#define BUFFER_SIZE 512

const int RADIUS = 8;
const int INPUT_SIZE = RADIUS * 2 + 1;
const int HIDDEN_SIZE = 10;
const int ITERATIONS = 6;

float audio_buffer[BUFFER_SIZE];
float memory_buffer[BUFFER_SIZE];
volatile int playback_ptr = 0;
volatile bool buffer_ready = false;

float w1[INPUT_SIZE][HIDDEN_SIZE];
float b1[HIDDEN_SIZE];
float w2[HIDDEN_SIZE];
float b2 = 0.0f;

float grain_phase = 0.0f;
float grain_position = 0.0f;
float grain_duration = 0.05f;
float grain_envelope = 0.0f;
float noise_state = 0.0f;

float randomf(float min, float max) { return min + (max - min) * ((float)rand() / (float)RAND_MAX); }

void neuralGlitchInit() {

  for (int i = 0; i < INPUT_SIZE; i++) {
    for (int j = 0; j < HIDDEN_SIZE; j++) {
      w1[i][j] = randomf(-1.0f, 1.0f);
    }
  }

  for (int j = 0; j < HIDDEN_SIZE; j++) {
    b1[j] = randomf(-0.5f, 0.5f);
    w2[j] = randomf(-1.0f, 1.0f);
  }

  memset(audio_buffer, 0, sizeof(audio_buffer));
  memset(memory_buffer, 0, sizeof(memory_buffer));

}

void mutate(float amount) {

  for (int i = 0; i < INPUT_SIZE; i++) {
    for (int j = 0; j < HIDDEN_SIZE; j++) {
      w1[i][j] += randomf(-amount, amount);

      if (w1[i][j] > 1.0f) w1[i][j] = 1.0f;
      if (w1[i][j] < -1.0f) w1[i][j] = -1.0f;
    }
  }

  for (int j = 0; j < HIDDEN_SIZE; j++) {
    w2[j] += randomf(-amount, amount);

    if (w2[j] > 1.0f) w2[j] = 1.0f;
    if (w2[j] < -1.0f) w2[j] = -1.0f;
  }

  b2 += randomf(-amount, amount);

  if (b2 > 1.0f) b2 = 1.0f;
  if (b2 < -1.0f) b2 = -1.0f;

}

void generateGlitchNoise() {

  float grain_samples[BUFFER_SIZE];

  float grain_freq = randomf(50.0f, 2000.0f);
  float grain_type = randomf(0.0f, 1.0f);
  float grain_density = randomf(0.3f, 0.9f);

  float phase_step = (2.0f * PI * grain_freq) / (float)SAMPLE_RATE;
  float local_phase = 0.0f;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float sample = 0.0f;

    if (grain_type < 0.33f) {
      sample = sinf(local_phase);
    } else if (grain_type < 0.66f) {
      sample = (sinf(local_phase) > 0.0f) ? 0.8f : -0.8f;
    } else {
      sample = randomf(-0.7f, 0.7f);
    }

    float envelope = 1.0f;
    float t = (float)i / (float)BUFFER_SIZE;

    if (t < 0.1f) {
      envelope = t / 0.1f;
    } else if (t > grain_density) {
      envelope = 1.0f - ((t - grain_density) / (1.0f - grain_density));
      envelope = envelope * envelope;
    }

    sample *= envelope;

    if (randomf(0.0f, 1.0f) < 0.05f) {
      sample *= 1.5f;

      if (sample > 0.9f) sample = 0.9f;
      if (sample < -0.9f) sample = -0.9f;
    }

    grain_samples[i] = sample;

    local_phase += phase_step;

    if (local_phase >= 2.0f * PI) {
      local_phase -= 2.0f * PI;
    }
  }

  memcpy(audio_buffer, grain_samples, sizeof(audio_buffer));

}

void processBlock(float& activity, float& chaos) {

  for (int i = 0; i < BUFFER_SIZE / 4; i++) {
    audio_buffer[BUFFER_SIZE - 1 - i] = audio_buffer[i] * 0.5f;
  }

  for (int i = 0; i < BUFFER_SIZE; i++) {
    audio_buffer[i] = audio_buffer[i] * 0.6f + memory_buffer[i] * 0.4f;
  }

  float ping_pong_buffer[BUFFER_SIZE];
  float* src = audio_buffer;
  float* dst = ping_pong_buffer;

  for (int iter = 0; iter < ITERATIONS; iter++) {

    for (int i = 0; i < BUFFER_SIZE; i++) {
      float window[INPUT_SIZE];

      for (int r = -RADIUS; r <= RADIUS; r++) {
        int idx = i + r;

        if (idx < 0) idx += BUFFER_SIZE;
        if (idx >= BUFFER_SIZE) idx -= BUFFER_SIZE;

        window[r + RADIUS] = src[idx];
      }

      float hidden_outputs[HIDDEN_SIZE];

      for (int h = 0; h < HIDDEN_SIZE; h++) {
        float sum = 0.0f;

        for (int m = 0; m < INPUT_SIZE; m++) {
          sum += window[m] * w1[m][h];
        }

        sum += b1[h];

        hidden_outputs[h] = (sum > 0.0f) ? sum : sum * 0.1f;

        if (hidden_outputs[h] > 1.0f) hidden_outputs[h] = 1.0f;
        if (hidden_outputs[h] < -1.0f) hidden_outputs[h] = -1.0f;
      }

      float update = 0.0f;

      for (int h = 0; h < HIDDEN_SIZE; h++) {
        update += hidden_outputs[h] * w2[h];
      }

      update += b2;

      dst[i] = src[i] + update * 0.7f;

      if (dst[i] > 0.8f) dst[i] = 0.8f;
      else if (dst[i] < -0.8f) dst[i] = -0.8f;

      if (randomf(0.0f, 1.0f) < 0.02f) {
        dst[i] = roundf(dst[i] * 8.0f) / 8.0f;
      }
    }

    float* temp = src;
    src = dst;
    dst = temp;
  }

  if (src != audio_buffer) {
    memcpy(audio_buffer, ping_pong_buffer, sizeof(audio_buffer));
  }

  for (int i = 0; i < BUFFER_SIZE; i++) {
    memory_buffer[i] = audio_buffer[i] * 0.95f;
  }

  float sum_abs = 0.0f;
  float sum_sq = 0.0f;
  float zero_crossings = 0.0f;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float val = audio_buffer[i];

    sum_abs += fabsf(val);
    sum_sq += val * val;

    if (i > 0 && (audio_buffer[i] * audio_buffer[i - 1]) < 0.0f) {
      zero_crossings += 1.0f;
    }
  }

  activity = sum_abs / (float)BUFFER_SIZE;

  float mean = sum_abs / (float)BUFFER_SIZE;
  float variance = (sum_sq / (float)BUFFER_SIZE) - (mean * mean);

  chaos = (variance > 0.0f) ? sqrtf(variance) : 0.0f;

  activity = (activity + 0.5f) * 1.5f;

  if (activity > 1.0f) activity = 1.0f;

  chaos = (chaos + (zero_crossings / (float)BUFFER_SIZE)) * 0.7f;

  if (chaos > 1.0f) chaos = 1.0f;

}

int audio_callback(void *ctx) {

  float sample = audio_buffer[playback_ptr];
  float output = sample * 0.8f;
  static float lfo_phase = 0.0f;
  float lfo = 0.5f + 0.5f * sinf(lfo_phase);

  lfo_phase += 0.005f;

  if (lfo_phase >= 2.0f * PI) { lfo_phase -= 2.0f * PI; }

  output *= (0.7f + lfo * 0.3f);
  if (output > 0.95f) output = 0.95f;
  if (output < -0.95f) output = -0.95f;
  float duty = 0.5f * (output + 1.0f);

  pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_0, PWM_RATE, duty);
  pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_1, PWM_RATE, duty);

  playback_ptr++;

  if (playback_ptr >= BUFFER_SIZE) {
    playback_ptr = 0;
    buffer_ready = true;
  }

  return 0;

}

void setup() {

  srand(read_cycle());

  fpioa_set_function(AUDIO_PIN_L, FUNC_TIMER1_TOGGLE1);
  fpioa_set_function(AUDIO_PIN_R, FUNC_TIMER1_TOGGLE2);

  plic_init();
  sysctl_enable_irq();

  timer_init(TIMER_DEVICE_0);
  timer_set_interval(TIMER_DEVICE_0, TIMER_CHANNEL_0, TIMER_RATE);
  timer_irq_register(TIMER_DEVICE_0, TIMER_CHANNEL_0, 0, 1, audio_callback, NULL);
  timer_set_enable(TIMER_DEVICE_0, TIMER_CHANNEL_0, 1);

  pwm_init(PWM_DEVICE_1);

  pwm_set_enable(PWM_DEVICE_1, PWM_CHANNEL_0, 1);
  pwm_set_enable(PWM_DEVICE_1, PWM_CHANNEL_1, 1);

  neuralGlitchInit();

}

void loop() {

  static int evolution_counter = 0;
  static int grain_counter = 0;

  if (buffer_ready) {
    float activity = 0.0f;
    float chaos = 0.0f;

    generateGlitchNoise();
    processBlock(activity, chaos);

    evolution_counter++;
    grain_counter++;

    if (evolution_counter >= 16) {
      evolution_counter = 0;

      float mutation_strength = 0.1f + (chaos * 0.3f);

      mutate(mutation_strength);

      if (randomf(0.0f, 1.0f) < 0.1f) {
        for (int j = 0; j < HIDDEN_SIZE; j++) {
          if (randomf(0.0f, 1.0f) < 0.3f) {
            w2[j] = randomf(-1.5f, 1.5f);
          }
        }
      }
    }

    if (grain_counter >= 8) {
      grain_counter = 0;
      grain_duration = 0.03f + activity * 0.1f;
    }

    buffer_ready = false;
  }

}