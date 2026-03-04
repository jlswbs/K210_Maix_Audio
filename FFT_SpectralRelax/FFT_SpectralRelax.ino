// Spectral relax FFT hardware //

#include "timer.h"
#include "fft.h"

#define FFT_N           512
#define SAMPLE_RATE     44100
#define TIMER_RATE      1000000000/SAMPLE_RATE
#define PWM_RATE        1000000
#define AUDIO_PIN_L     6
#define AUDIO_PIN_R     8


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

    static int silence_count = 0;
    int16_t check_sample = audio_out[rand() % FFT_N];
    if (abs(check_sample) < 100) silence_count++; else silence_count = 0;

    if (silence_count > 5) {
        fft_in_data[rand() % (FFT_N/2)].R1 = rand() % 12000;
        silence_count = 0;
    }

    float dna_mod = (float)check_sample / 32768.0f;

    for (int i = 0; i < FFT_N / 2; i++) {

        float angle = 0.02f + (dna_mod * 0.01f);
        float s = sinf(angle);
        float c = cosf(angle);

        float r = (float)fft_in_data[i].R1;
        float im = (float)fft_in_data[i].I1;

        float next_r = (r * c - im * s) * 0.995f;
        float next_im = (im * c + r * s) * 0.995f;

        if (i > 0) {
            next_r += fft_in_data[i-1].R1 * 0.01f;
        }

        fft_in_data[i].R1 = (int16_t)next_r;
        fft_in_data[i].I1 = (int16_t)next_im;

    }

    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, FFT_DIR_BACKWARD, (uint64_t *)fft_in_data, FFT_N, (uint64_t *)fft_out_data);

    for (int i = 0; i < FFT_N / 2; i++) {

        audio_out[2 * i]     = fft_out_data[i].R1;
        audio_out[2 * i + 1] = fft_out_data[i].R2;
        fft_in_data[i].I2 = (int16_t)(fft_out_data[i].R2 * 0.05f);

    }

    delay(5);

}