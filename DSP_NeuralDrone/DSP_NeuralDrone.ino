// Neural cellular automaton drone generator //

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

float frequencies[] = {73.42f, 110.0f, 146.83f, 164.81f, 196.00f, 220.00f};
volatile float current_freq = 110.0f;
float phase = 0.0f;

float randomf(float min, float max) { return min + (max - min) * ((float)rand() / (float)RAND_MAX); }

void neuralDroneInit() {

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

}

void generateSineBlock(float freq) {

  float phase_step = (2.0f * PI * freq) / (float)SAMPLE_RATE;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    audio_buffer[i] = sinf(phase);

    phase += phase_step;

    if (phase >= 2.0f * PI)
      phase -= 2.0f * PI;

    if (i < 16)
      audio_buffer[i] *= (i / 16.0f);

    if (i > BUFFER_SIZE - 16)
      audio_buffer[i] *= ((BUFFER_SIZE - i) / 16.0f);
  }

}

void processBlock(float& activity, float& chaos) {

  for (int i = 0; i < BUFFER_SIZE; i++) {
    audio_buffer[i] = audio_buffer[i] * 0.7f + memory_buffer[i] * 0.3f;
  }

  float ping_pong_buffer[BUFFER_SIZE];

  float* src = audio_buffer;
  float* dst = ping_pong_buffer;

  for (int iter = 0; iter < ITERATIONS; iter++) {

    for (int i = 0; i < BUFFER_SIZE; i++) {

      float window[INPUT_SIZE];

      for (int r = -RADIUS; r <= RADIUS; r++) {
        int idx = i + r;

        if (idx < 0)
          idx += BUFFER_SIZE;

        if (idx >= BUFFER_SIZE)
          idx -= BUFFER_SIZE;

        window[r + RADIUS] = src[idx];
      }

      float hidden_outputs[HIDDEN_SIZE];

      for (int h = 0; h < HIDDEN_SIZE; h++) {
        float sum = 0.0f;

        for (int m = 0; m < INPUT_SIZE; m++) {
          sum += window[m] * w1[m][h];
        }

        sum += b1[h];
        hidden_outputs[h] = tanhf(sum);
      }

      float update = 0.0f;

      for (int h = 0; h < HIDDEN_SIZE; h++) {
        update += hidden_outputs[h] * w2[h];
      }

      update += b2;

      dst[i] = src[i] + update * 0.35f;

      if (dst[i] > 1.0f)
        dst[i] = 1.0f;
      else if (dst[i] < -1.0f)
        dst[i] = -1.0f;
    }

    float* temp = src;
    src = dst;
    dst = temp;
  }

  if (src != audio_buffer) {
    memcpy(audio_buffer, ping_pong_buffer, sizeof(audio_buffer));
  }

  memcpy(memory_buffer, audio_buffer, sizeof(memory_buffer));

  float sum_abs = 0.0f;
  float sum_sq = 0.0f;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float val = audio_buffer[i];

    sum_abs += fabsf(val);
    sum_sq += val * val;
  }

  activity = sum_abs / (float)BUFFER_SIZE;

  float mean = sum_abs / (float)BUFFER_SIZE;
  float variance = (sum_sq / (float)BUFFER_SIZE) - (mean * mean);

  chaos = (variance > 0.0f) ? sqrtf(variance) : 0.0f;

}

int audio_callback(void *ctx) {

  float sample = audio_buffer[playback_ptr];
  float duty = 0.5f * (sample + 1.0f);

  pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_0, PWM_RATE, (float)duty);
  pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_1, PWM_RATE, (float)duty);

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

  neuralDroneInit();

}

void loop() {

  static int evolution_counter = 0;

  if (buffer_ready) {

    float activity = 0.0f;
    float chaos = 0.0f;

    generateSineBlock(current_freq);
    processBlock(activity, chaos);

    evolution_counter++;

    if (evolution_counter >= 32) {

      evolution_counter = 0;

      int freq_idx = (int)(chaos * 10.0f) % 6;
      current_freq = frequencies[freq_idx];

      mutate(0.1f);

    }

    buffer_ready = false;

  }

}