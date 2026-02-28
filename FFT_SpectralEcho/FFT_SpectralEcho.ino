// Spectral echo FFT hardware //

#include "timer.h"
#include "fft.h"

#define FFT_N           512
#define SAMPLE_RATE     44100
#define TIMER_RATE      1000000000/SAMPLE_RATE
#define PWM_RATE        1000000
#define AUDIO_PIN_L     6
#define AUDIO_PIN_R     8

#define BINS    128

static int16_t audio_out[FFT_N];
volatile int sample_ptr = 0;

fft_data_t fft_in_data[FFT_N] __attribute__((aligned(64)));
fft_data_t fft_out_data[FFT_N] __attribute__((aligned(64)));

int audio_callback(void *ctx) {

    int16_t s = audio_out[sample_ptr];
    float duty = (float)(s + 32768) / 65536.0f;

    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_0, PWM_RATE, (double)duty);
    pwm_set_frequency(PWM_DEVICE_1, PWM_CHANNEL_1, PWM_RATE, (double)duty);

    sample_ptr++;
    if (sample_ptr >= FFT_N) sample_ptr = 0;
    return 0;

}

void setup() {

    srand(read_cycle());

    sysctl_clock_enable(SYSCTL_CLOCK_FFT);
    sysctl_reset(SYSCTL_RESET_FFT);
    
    dmac_init();

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

}

void loop() {
    
    for (int i = 0; i < FFT_N / 2; i++) {

        float feedback = 0.96f; 
        float angle = 0.02f + ((float)i * 0.001f); 
        float s = sinf(angle);
        float c = cosf(angle);

        int16_t r1 = fft_in_data[i].R1;
        int16_t i1 = fft_in_data[i].I1;
        int16_t r2 = fft_in_data[i].R2;
        int16_t i2 = fft_in_data[i].I2;

        fft_in_data[i].R1 = (int16_t)((r1 * c - i1 * s) * feedback);
        fft_in_data[i].I1 = (int16_t)((i1 * c + r1 * s) * feedback);
        
        fft_in_data[i].R2 = (int16_t)((r2 * c - i2 * s) * feedback);
        fft_in_data[i].I2 = (int16_t)((i2 * c + r2 * s) * feedback);

    }

    if (rand() % 5 == 0) {

        for (int j = 0; j < 2; j++) {
            int target_bin = 8 + (rand() % BINS);
            int16_t amp = 5000 / (1 + (target_bin / 20));

            if (target_bin % 2 == 0) {
                fft_in_data[target_bin / 2].R1 += amp;
            } else {
                fft_in_data[target_bin / 2].R2 += amp;
            }
        }

    }

    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, FFT_DIR_BACKWARD, (uint64_t *)fft_in_data, FFT_N, (uint64_t *)fft_out_data);

    for (int i = 0; i < FFT_N / 2; i++) {
        audio_out[2 * i]     = fft_out_data[i].R1;
        audio_out[2 * i + 1] = fft_out_data[i].R2;
    }

    delay(30);

}