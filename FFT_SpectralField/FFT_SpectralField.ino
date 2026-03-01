// Spectral field FFT hardware //

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

    for (int i = 0; i < FFT_N / 2; i++) {

        fft_in_data[i].R1 = (int16_t)(fft_in_data[i].R1 * 0.5f);
        fft_in_data[i].I1 = (int16_t)(fft_in_data[i].I1 * 0.7f);
        
        if (abs(fft_in_data[i].R1) > 500) {
            fft_in_data[i].R1 ^= 0x00FF; 
        }

    }

    for (int j = 0; j < 5; j++) {

        int base_bin = rand() % (FFT_N / 4);
        int16_t energy = (rand() % 5000);

        if (rand() % 2 == 0) {
            fft_in_data[base_bin].R1 += energy;
            fft_in_data[base_bin].I2 -= energy / 3;
        } else {
            fft_in_data[base_bin].R2 += energy;
            if(base_bin > 0) fft_in_data[base_bin-1].I1 -= energy / 2; 
        }
    }

    for (int i = 0; i < FFT_N / 2; i++) {
        if ((i % 7) == 0) {
            fft_in_data[i].R1 = -fft_in_data[i].R2;
            fft_in_data[i].R2 = -fft_in_data[i].R1;
        }
    }

    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, FFT_DIR_BACKWARD, (uint64_t *)fft_in_data, FFT_N, (uint64_t *)fft_out_data);

    for (int i = 0; i < FFT_N / 2; i++) {

        int16_t sample1 = fft_out_data[i].R1;
        int16_t sample2 = fft_out_data[i].R2;
        
        audio_out[2 * i]     = (rand() % 100 > 68) ? 0 : sample1;
        audio_out[2 * i + 1] = sample2;

    }

    delay(20 + (rand() % 30));

}