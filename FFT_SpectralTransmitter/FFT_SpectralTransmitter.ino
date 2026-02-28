// Spectral transmitter FFT hardware //

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

    static uint32_t sand_tick = 0;
    sand_tick++;

    for (int i = 0; i < FFT_N / 2; i++) {

        fft_in_data[i].R1 = 0;
        fft_in_data[i].I1 = 0;
        fft_in_data[i].R2 = 0;
        fft_in_data[i].I2 = 0;

    }

    int density = (sand_tick % 10) + 2;

    for (int j = 0; j < density; j++) {

        int grain_bin = rand() % (FFT_N / 2);
        int16_t amp = 15000 + (rand() % 17000);

        fft_in_data[grain_bin].R1 = (rand() % 2) ? amp : -amp;
        fft_in_data[grain_bin].I1 = (rand() % 2) ? amp : -amp;

        if(grain_bin < (FFT_N/2 - 1)) {
            fft_in_data[grain_bin+1].R1 = -fft_in_data[grain_bin].R1 >> 1;
        }

    }

    for (int i = 0; i < 10; i++) {
        int noise_idx = rand() % (FFT_N / 2);
        fft_in_data[noise_idx].R2 = rand() % 500; 
    }

    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, FFT_DIR_BACKWARD, (uint64_t *)fft_in_data, FFT_N, (uint64_t *)fft_out_data);

    for (int i = 0; i < FFT_N / 2; i++) {

        int16_t s1 = fft_out_data[i].R1;
        int16_t s2 = fft_out_data[i].R2;
        uint16_t mask = (sand_tick & 0x07) ? 0xFFFF : 0x0000;

        audio_out[2 * i]     = (s1 & mask) ^ (rand() % 128); 
        audio_out[2 * i + 1] = (s2 & mask);

    }

    int speed = 2 + (rand() % 15);
    if (sand_tick % 50 == 0) speed = 80;
    
    delay(speed);

}