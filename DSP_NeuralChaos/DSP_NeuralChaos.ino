// Neural cellular automaton chaos generator //

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

const float CHAOS_UPDATE = 0.9f;
const float CHAOS_FEEDBACK = 0.98f;
const float TANH_LIMIT = 0.99f;

float audio_buffer[BUFFER_SIZE];
float memory_buffer[BUFFER_SIZE];
volatile int playback_ptr = 0;
volatile bool buffer_ready = false;

float w1[INPUT_SIZE][HIDDEN_SIZE];
float b1[HIDDEN_SIZE];
float w2[HIDDEN_SIZE];
float b2 = 0.0f;

float divergence_rate = 0.0f;
float lyapunov_estimate = 0.0f;
int overflow_counter = 0;

float randomf(float min, float max) { return min + (max - min) * ((float)rand() / (float)RAND_MAX); }

float cubic_activation(float x) { return (x * 1.2f) - (0.2f * x * x * x); }

float cubic_tanh_safe(float x) {

    float cubed = x * x * x;

    if (fabsf(x) > 5.0f) {
        return tanhf(x) * 0.9f;
    }

    return cubed;

}

void neuralChaosInit() {

    for (int i = 0; i < INPUT_SIZE; i++) {
        for (int j = 0; j < HIDDEN_SIZE; j++) {
            w1[i][j] = randomf(-1.0f, 1.0f);
        }
    }

    for (int j = 0; j < HIDDEN_SIZE; j++) {
        b1[j] = randomf(-0.8f, 0.8f);
        w2[j] = randomf(-1.2f, 1.2f);
    }

    b2 = randomf(-0.3f, 0.3f);

    for (int i = 0; i < BUFFER_SIZE; i++) {
        audio_buffer[i] = randomf(-0.1f, 0.1f);
        memory_buffer[i] = audio_buffer[i];
    }

}

void mutate(float amount) {

    float chaos_mutation = amount * 1.5f;
    float decay = 0.99f; 

    for (int i = 0; i < INPUT_SIZE; i++) {
        for (int j = 0; j < HIDDEN_SIZE; j++) {
            w1[i][j] = (w1[i][j] * decay) + randomf(-chaos_mutation, chaos_mutation);
        }
    }

    for (int j = 0; j < HIDDEN_SIZE; j++) {
        w2[j] = (w2[j] * decay) + randomf(-chaos_mutation, chaos_mutation);
        b1[j] = (b1[j] * decay) + randomf(-amount, amount);
    }

    b2 = (b2 * decay) + randomf(-amount, amount);

}

void processChaosBlock(float& divergence) {

    for (int i = 0; i < BUFFER_SIZE; i++) {
        audio_buffer[i] = audio_buffer[i] * 0.5f + memory_buffer[i] * 0.5f;
    }

    float ping_pong_buffer[BUFFER_SIZE];
    float* src = audio_buffer;
    float* dst = ping_pong_buffer;

    float max_val = 0.0f;

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

            float raw_step = src[i] + update * CHAOS_UPDATE;
            dst[i] = tanhf(raw_step * 0.2f) * 5.0f; 

            if (fabsf(dst[i]) > max_val) {
                max_val = fabsf(dst[i]);
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
        memory_buffer[i] = audio_buffer[i] * CHAOS_FEEDBACK;
    }

    float sum_abs = 0.0f;
    float sum_sq = 0.0f;
    float sum_diff = 0.0f;
    float zero_crossings = 0.0f;

    for (int i = 0; i < BUFFER_SIZE; i++) {
        float val = audio_buffer[i];

        sum_abs += fabsf(val);
        sum_sq += val * val;

        if (i > 0) {
            sum_diff += fabsf(val - audio_buffer[i - 1]);

            if (val * audio_buffer[i - 1] < 0.0f) {
                zero_crossings += 1.0f;
            }
        }
    }

    divergence = (max_val > 1.0f) ? (max_val - 1.0f) : 0.0f;

    if (divergence > 2.0f) divergence = 2.0f;

    divergence = divergence / 2.0f;

    static float last_max = 0.0f;

    if (last_max > 0.0f && max_val > 0.0f) {
        float ratio = max_val / last_max;

        if (ratio > 0.1f && ratio < 10.0f) {
            lyapunov_estimate = logf(ratio) * 0.1f;

            if (lyapunov_estimate > 0.5f) lyapunov_estimate = 0.5f;
            if (lyapunov_estimate < -0.5f) lyapunov_estimate = -0.5f;
        }
    }

    last_max = max_val;

    if (max_val > 3.0f) {
        overflow_counter++;
    }

    overflow_counter = overflow_counter * 0.95f;

}

int audio_callback(void *ctx) {

    float sample = audio_buffer[playback_ptr];
    float compressed = tanhf(sample * 0.5f);
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

    static int evolution_counter = 0;

    if (buffer_ready) {

        float divergence = 0.0f;
        processChaosBlock(divergence);
        evolution_counter++;

        if (evolution_counter >= 16) {
            evolution_counter = 0;
            float mutation_strength = 0.01f + (divergence * 0.03f);
            mutate(mutation_strength);
        }

        buffer_ready = false;
    }

}