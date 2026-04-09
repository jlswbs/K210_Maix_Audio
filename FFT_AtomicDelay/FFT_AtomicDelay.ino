// Spectral atomic delay FFT hardware //

#include "timer.h"
#include "fft.h"

#define FFT_N 512
#define SAMPLE_RATE 44100
#define TIMER_RATE 1000000000/SAMPLE_RATE
#define PWM_RATE 1000000
#define AUDIO_PIN_L 6
#define AUDIO_PIN_R 8

static int16_t audio_out[FFT_N];
volatile int sample_ptr = 0;

fft_data_t fft_in_data[FFT_N] __attribute__((aligned(64)));
fft_data_t fft_out_data[FFT_N] __attribute__((aligned(64)));

typedef struct {
    fft_data_t spectrum[FFT_N/2];
    int duration_remaining;
    int total_duration;
    float envelope_step;
    float envelope_current;
    int effects_mask;
} audio_grain_t;

#define MAX_GRAINS 128
audio_grain_t grains[MAX_GRAINS];
int active_grains = 0;

#define DELAY_BUFFER_SIZE 256
#define MAX_DELAY_GRAINS 8

typedef struct {
    int active;
    int buffer_pos;
    int delay_length;
    float feedback;
    float mix;
    fft_data_t buffer[DELAY_BUFFER_SIZE][FFT_N/2];
} spectral_delay_t;

spectral_delay_t delays[MAX_DELAY_GRAINS];

void init_delays() {

    for (int d = 0; d < MAX_DELAY_GRAINS; d++) {
        delays[d].active = 0;
        delays[d].buffer_pos = 0;
        delays[d].delay_length = 50 + (rand() % 200);
        delays[d].feedback = 0.5f + (rand() % 50) / 100.0f;
        delays[d].mix = 0.3f + (rand() % 50) / 100.0f;

        for (int i = 0; i < DELAY_BUFFER_SIZE; i++) {
            for (int j = 0; j < FFT_N/2; j++) {
                delays[d].buffer[i][j].R1 = 0;
                delays[d].buffer[i][j].I1 = 0;
                delays[d].buffer[i][j].R2 = 0;
                delays[d].buffer[i][j].I2 = 0;
            }
        }
    }

}

void apply_spectral_delay(fft_data_t *spectrum) {

    static int global_delay_ptr = 0;

    for (int d = 0; d < MAX_DELAY_GRAINS; d++) {
        if (!delays[d].active) continue;

        for (int i = 0; i < FFT_N/2; i++) {
            delays[d].buffer[delays[d].buffer_pos][i] = spectrum[i];
        }

        int old_pos = (delays[d].buffer_pos - delays[d].delay_length + DELAY_BUFFER_SIZE) % DELAY_BUFFER_SIZE;

        for (int i = 0; i < FFT_N/2; i++) {
            float wet = (float)delays[d].buffer[old_pos][i].R1 * delays[d].feedback;
            float dry = (float)spectrum[i].R1 * (1.0f - delays[d].mix);

            spectrum[i].R1 = (int16_t)(dry + wet * delays[d].mix);
            spectrum[i].R2 = (int16_t)(dry + wet * delays[d].mix);
        }

        delays[d].buffer_pos = (delays[d].buffer_pos + 1) % DELAY_BUFFER_SIZE;
    }

    global_delay_ptr = (global_delay_ptr + 1) % DELAY_BUFFER_SIZE;

}

void create_random_delay() {

    for (int d = 0; d < MAX_DELAY_GRAINS; d++) {
        if (!delays[d].active) {
            delays[d].active = 1;
            delays[d].buffer_pos = 0;
            delays[d].delay_length = 20 + (rand() % 200);
            delays[d].feedback = 0.5f + (rand() % 50) / 100.0f;
            delays[d].mix = 0.3f + (rand() % 60) / 100.0f;

            for (int i = 0; i < DELAY_BUFFER_SIZE; i++) {
                for (int j = 0; j < FFT_N/2; j++) {
                    delays[d].buffer[i][j].R1 = 0;
                    delays[d].buffer[i][j].I1 = 0;
                    delays[d].buffer[i][j].R2 = 0;
                    delays[d].buffer[i][j].I2 = 0;
                }
            }
            break;
        }
    }

}

void fade_out_old_delays() {

    for (int d = 0; d < MAX_DELAY_GRAINS; d++) {
        if (delays[d].active) {
            if (rand() % 1000 < 1) {
                delays[d].feedback *= 0.95f;
                if (delays[d].feedback < 0.05f) {
                    delays[d].active = 0;
                }
            }
        }
    }

}

void generate_grain(audio_grain_t *grain) {

    for (int i = 0; i < FFT_N/2; i++) {
        grain->spectrum[i].R1 = 0;
        grain->spectrum[i].I1 = 0;
        grain->spectrum[i].R2 = 0;
        grain->spectrum[i].I2 = 0;
    }

    int num_clusters = 1 + (rand() % 5);
    for (int c = 0; c < num_clusters; c++) {
        int center_bin = rand() % (FFT_N/2 - 20) + 10;
        int width = 1 + (rand() % 4);
        float amp = 8000 + (rand() % 12000);

        for (int w = -width; w <= width; w++) {
            int bin = center_bin + w;
            if (bin >= 0 && bin < FFT_N/2) {
                float distance_amp = amp * (1.0f - abs(w)/(float)width * 0.5f);
                if (bin % 2 == 0)
                    grain->spectrum[bin/2].R1 += (int16_t)distance_amp;
                else
                    grain->spectrum[bin/2].R2 += (int16_t)distance_amp;
            }
        }
    }

    grain->total_duration = 1 + (rand() % 150);
    grain->duration_remaining = grain->total_duration;

    grain->envelope_current = 0.0f;
    grain->envelope_step = 2.0f / grain->total_duration;

    grain->effects_mask = 0;
    if (rand() % 100 < 10) grain->effects_mask |= 0x01;
    if (rand() % 100 < 15) grain->effects_mask |= 0x02;
    if (rand() % 100 < 20) grain->effects_mask |= 0x04;
    if (rand() % 100 < 25) grain->effects_mask |= 0x08;

}

void mix_grains_to_output(fft_data_t *output) {

    for (int i = 0; i < FFT_N/2; i++) {
        output[i].R1 = 0;
        output[i].I1 = 0;
        output[i].R2 = 0;
        output[i].I2 = 0;
    }

    for (int g = 0; g < active_grains; g++) {
        audio_grain_t *grain = &grains[g];
        float env = grain->envelope_current;

        for (int i = 0; i < FFT_N/2; i++) {
            output[i].R1 += (int16_t)(grain->spectrum[i].R1 * env);
            output[i].I1 += (int16_t)(grain->spectrum[i].I1 * env);
            output[i].R2 += (int16_t)(grain->spectrum[i].R2 * env);
            output[i].I2 += (int16_t)(grain->spectrum[i].I2 * env);
        }

        if (grain->duration_remaining > grain->total_duration/2)
            grain->envelope_current += grain->envelope_step;
        else
            grain->envelope_current -= grain->envelope_step;

        if (grain->envelope_current > 1.0f) grain->envelope_current = 1.0f;
        if (grain->envelope_current < 0.0f) grain->envelope_current = 0.0f;

        grain->duration_remaining--;
    }

    int write_idx = 0;
    for (int g = 0; g < active_grains; g++) {
        if (grains[g].duration_remaining > 0 && grains[g].envelope_current > 0.01f) {
            if (write_idx != g)
                grains[write_idx] = grains[g];
            write_idx++;
        }
    }
    active_grains = write_idx;

}

void apply_effects_to_grain(audio_grain_t *grain) {

    fft_data_t temp[FFT_N/2];
    for (int i = 0; i < FFT_N/2; i++)
        temp[i] = grain->spectrum[i];

    if (grain->effects_mask & 0x01) {
        float pos = (float)(rand() % 100) / 100;
        for (int i = 0; i < FFT_N/2; i++) {
            float dist = abs((float)i/(FFT_N/2) - pos);
            if (dist < 0.15f) {
                float red_shift = 1.0f + (0.3f - dist) * 0.8f;
                float real = temp[i].R1, imag = temp[i].I1;
                temp[i].R1 = (int16_t)(real * cos(red_shift) - imag * sin(red_shift));
                temp[i].I1 = (int16_t)(imag * cos(red_shift) + real * sin(red_shift));
            }
        }
    }

    if (grain->effects_mask & 0x02) {
        for (int i = 0; i < FFT_N/2 - 10; i++) {
            if (abs(temp[i].R1) > 1000 && (rand() % 100) < 15) {
                int harmonic = i * 2;
                if (harmonic < FFT_N/2)
                    temp[harmonic].R1 += temp[i].R1 * 0.3f;
            }
        }
    }

    if (grain->effects_mask & 0x04) {
        for (int i = 0; i < FFT_N/2; i++) {
            if (rand() % 200 < 3)
                temp[i].R1 += (int16_t)((rand() % 6000) - 3000);
        }
    }

    if (grain->effects_mask & 0x08) {
        static fft_data_t last_grain[FFT_N/2];
        for (int i = 0; i < FFT_N/2; i++) {
            temp[i].R1 = (temp[i].R1 + last_grain[i].R1) / 2;
            temp[i].I1 = (temp[i].I1 + last_grain[i].I1) / 2;
            last_grain[i] = temp[i];
        }
    }

    for (int i = 0; i < FFT_N/2; i++) {
        float amp = sqrt(temp[i].R1*temp[i].R1 + temp[i].I1*temp[i].I1);
        if (amp > 28000) {
            float scale = 28000 / amp;
            temp[i].R1 *= scale;
            temp[i].I1 *= scale;
        }
    }

    for (int i = 0; i < FFT_N/2; i++) grain->spectrum[i] = temp[i];

}

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

    init_delays();
    
}

void loop() {

    if (active_grains < MAX_GRAINS && (rand() % 100) < 8) {
        generate_grain(&grains[active_grains]);
        active_grains++;
    }
    
    if (rand() % 200 < 3) {
        create_random_delay();
    }
    
    fade_out_old_delays();
    
    for (int g = 0; g < active_grains; g++) {
        apply_effects_to_grain(&grains[g]);
    }
    
    fft_data_t mixed_spectrum[FFT_N/2];
    mix_grains_to_output(mixed_spectrum);
    
    apply_spectral_delay(mixed_spectrum);
    
    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, 
                           FFT_DIR_BACKWARD, (uint64_t *)mixed_spectrum, 
                           FFT_N, (uint64_t *)fft_out_data);
    
    for (int i = 0; i < FFT_N / 2; i++) {
        audio_out[2 * i]     = fft_out_data[i].R1;
        audio_out[2 * i + 1] = fft_out_data[i].R2;
    }

}