// Neural cellular automaton subthreshold zoom generator //

#include "timer.h"

#define SAMPLE_RATE 44100
#define TIMER_RATE 1000000000 / SAMPLE_RATE
#define PWM_RATE 1000000
#define AUDIO_PIN_L 6
#define AUDIO_PIN_R 8
#define BUFFER_SIZE 1024


const int RADIUS = 12;
const int INPUT_SIZE = RADIUS * 2 + 1;
const int HIDDEN_SIZE = 12;
const int ITERATIONS = 8;

const float UPDATE = 0.1f;
const float FEEDBACK = 0.98f;
const float ZOOM_GAIN = 45.0f;

float audio_buffer[BUFFER_SIZE];
float memory_buffer[BUFFER_SIZE];
volatile int playback_ptr = 0;
volatile bool buffer_ready = false;

float w1[INPUT_SIZE][HIDDEN_SIZE];
float b1[HIDDEN_SIZE];
float w2[HIDDEN_SIZE];
float b2 = 0.0f;

float randomf(float min, float max) { return min + (max - min) * ((float)rand() / (float)RAND_MAX); }

float cubic_activation(float x) { return (x * 1.2f) - (0.2f * x * x * x); }

void neuralChaosInit() {

    for (int i = 0; i < INPUT_SIZE; i++) {
        for (int j = 0; j < HIDDEN_SIZE; j++) {
            w1[i][j] = randomf(-1.0f, 1.0f);
        }
    }
    for (int j = 0; j < HIDDEN_SIZE; j++) {
        b1[j] = randomf(-0.6f, 0.6f);
        w2[j] = randomf(-0.8f, 0.8f);
    }
    b2 = randomf(-0.15f, 0.15f);
    for (int i = 0; i < BUFFER_SIZE; i++) {
        audio_buffer[i] = randomf(-0.01f, 0.01f);
    }

}

void processChaosBlock() {

    for (int i = 0; i < BUFFER_SIZE; i++) {
        audio_buffer[i] = audio_buffer[i] * 0.5f + memory_buffer[i] * 0.5f;
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
                hidden_outputs[h] = cubic_activation(sum);
                if (isnan(hidden_outputs[h]) || isinf(hidden_outputs[h])) {
                    hidden_outputs[h] = 0.0f;
                }
            }

            float update = 0.0f;
            for (int h = 0; h < HIDDEN_SIZE; h++) {
                update += hidden_outputs[h] * w2[h];
            }
            update += b2;

            float raw_step = src[i] + update * UPDATE;
            dst[i] = tanhf(raw_step * 0.2f) * 5.0f; 
        }
        float* temp = src;
        src = dst;
        dst = temp;
    }

    if (src != audio_buffer) {
        memcpy(audio_buffer, ping_pong_buffer, sizeof(audio_buffer));
    }

    for (int i = 0; i < BUFFER_SIZE; i++) {
        memory_buffer[i] = audio_buffer[i] * FEEDBACK;
    }

}

int audio_callback(void *ctx) {

    float sample = audio_buffer[playback_ptr];
    static float last_sample = 0.0f;
    static float last_micro_delta = 0.0f;
    float micro_delta = sample - last_sample;
    last_sample = sample;
    float nano_acceleration = micro_delta - last_micro_delta;
    last_micro_delta = micro_delta;
    float zoomed_sample = nano_acceleration * ZOOM_GAIN; 
    float compressed = tanhf(zoomed_sample * 0.5f);
    float duty = 0.5f * (compressed + 1.0f);

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

    neuralChaosInit();

}

void loop() {

    if (buffer_ready) {

        processChaosBlock();
        buffer_ready = false;

    }

}