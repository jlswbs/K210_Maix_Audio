// Spectral cosmoptera glitch FFT hardware //

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

void generate_base_signal() {

    for (int i = 0; i < FFT_N/2; i++) {
        fft_in_data[i].R1 = 0;
        fft_in_data[i].I1 = 0;
        fft_in_data[i].R2 = 0;
        fft_in_data[i].I2 = 0;
    }
    
    int num_clusters = 3 + (rand() % 4);
    
    for (int c = 0; c < num_clusters; c++) {
        int center_bin = rand() % (FFT_N/2 - 20) + 10;
        int width = 3 + (rand() % 8);
        float amp = 5000 + (rand() % 8000);
        
        for (int w = -width; w <= width; w++) {
            int bin = center_bin + w;
            if (bin >= 0 && bin < FFT_N/2) {
                float distance_amp = amp * (1.0f - abs(w)/(float)width * 0.7f);
                
                if (bin % 2 == 0) {
                    fft_in_data[bin/2].R1 += (int16_t)distance_amp;
                    fft_in_data[bin/2].I1 += (int16_t)(distance_amp * 0.2f);
                } else {
                    fft_in_data[bin/2].R2 += (int16_t)distance_amp;
                    fft_in_data[bin/2].I2 += (int16_t)(distance_amp * 0.2f);
                }
            }
        }
    }

}

float black_hole_pos = 0.5f;
float black_hole_mass = 1.0f;
float accretion_disk[FFT_N/2];

void black_hole_processing() {

    for (int i = 0; i < FFT_N/2; i++) {
        float normalized_pos = (float)i / (FFT_N/2);
        float distance_from_horizon = abs(normalized_pos - black_hole_pos);
        
        if (distance_from_horizon < 0.1f) {
            float curvature = 1.0f - (distance_from_horizon / 0.1f);
            int stretched_bins = (int)(curvature * 10);
            
            for (int s = 1; s <= stretched_bins; s++) {
                if (i + s < FFT_N/2) {
                    accretion_disk[i] += abs(fft_in_data[i + s].R1) * (1.0f - s/10.0f);
                    fft_in_data[i + s].R1 *= 0.9f;
                }
            }
            
            if (accretion_disk[i] > 1000) {
                fft_in_data[i].R1 += (int16_t)(accretion_disk[i] * 0.5f);
                fft_in_data[i].I1 += (int16_t)(accretion_disk[i] * 0.3f);
                accretion_disk[i] *= 0.8f;
            }
        }
        
        if (distance_from_horizon > 0.1f && distance_from_horizon < 0.5f) {
            float red_shift = 1.0f + (0.5f - distance_from_horizon) * black_hole_mass;
            float real = fft_in_data[i].R1;
            float imag = fft_in_data[i].I1;
            fft_in_data[i].R1 = (int16_t)(real * cos(red_shift) - imag * sin(red_shift));
            fft_in_data[i].I1 = (int16_t)(imag * cos(red_shift) + real * sin(red_shift));
        }
    }
    
    for (int i = 0; i < FFT_N/2; i++) {
        if (accretion_disk[i] > 500) {
            if (rand() % 100 < 10) {
                int jet_bin = i + (rand() % 20 - 10);
                if (jet_bin >= 0 && jet_bin < FFT_N/2) {
                    fft_in_data[jet_bin].R1 += (int16_t)(accretion_disk[i] * 0.3f);
                    accretion_disk[i] *= 0.7f;
                }
            }
        }
    }

}

float crystal_seeds[16];
int crystal_lattice[FFT_N/2];
float temperature = 0.5f;

void crystal_growth() {

    if (rand() % 100 < 5) {
        int new_seed = rand() % (FFT_N/2);
        crystal_seeds[new_seed % 16] = (float)new_seed / (FFT_N/2);
        crystal_lattice[new_seed] = 1;
    }
    
    for (int i = 0; i < FFT_N/2; i++) {
        if (crystal_lattice[i] > 0) {
            for (int s = -2; s <= 2; s++) {
                if (s == 0) continue;
                int neighbor = i + s;
                if (neighbor >= 0 && neighbor < FFT_N/2) {
                    if (rand() % 100 < (int)(temperature * 30)) {
                        crystal_lattice[neighbor] = 1;
                        
                        if (fft_in_data[i].R1 != 0) {
                            float harmonic_ratio = (float)neighbor / i;
                            if (abs(harmonic_ratio - 2.0) < 0.1f ||
                                abs(harmonic_ratio - 1.5) < 0.1f ||
                                abs(harmonic_ratio - 1.333) < 0.1f) {
                                
                                fft_in_data[neighbor].R1 += 
                                    fft_in_data[i].R1 * 0.5f * temperature;
                            }
                        }
                    }
                }
            }
        }
    }
    
    temperature += (rand() % 100 - 50) / 500.0f;
    if (temperature < 0.1f) temperature = 0.1f;
    if (temperature > 1.0f) temperature = 1.0f;
    
    if (temperature > 0.8f) {
        for (int i = 0; i < FFT_N/2; i++) {
            if (crystal_lattice[i] && rand() % 100 < 20) {
                crystal_lattice[i] = 0;
                fft_in_data[i].R1 += (int16_t)(rand() % 5000 - 2500);
            }
        }
    }

}

float planck_scale = 0.01f;
float vacuum_energy[FFT_N/2][2];

void quantum_foam() {

    for (int i = 0; i < FFT_N/2; i++) {
        if (rand() % 1000 < 5) {
            float energy = (rand() % 10000) * planck_scale;
            float lifetime = 1.0f / energy;
            
            vacuum_energy[i][0] += energy;
            vacuum_energy[i][1] += lifetime;
            
            fft_in_data[i].R1 += (int16_t)(energy * 100);
            fft_in_data[i].I1 += (int16_t)(energy * 50);
        }
        
        vacuum_energy[i][1] -= 0.01f;
        if (vacuum_energy[i][1] <= 0) {
            vacuum_energy[i][0] *= 0.5f;
            vacuum_energy[i][1] = 0;
        }
    }
    
    for (int i = 1; i < FFT_N/2 - 1; i++) {
        if (vacuum_energy[i][0] > vacuum_energy[i-1][0] * 2 &&
            vacuum_energy[i][0] > vacuum_energy[i+1][0] * 2) {
            
            if (rand() % 100 < 20) {
                fft_in_data[i-1].R1 += (int16_t)(vacuum_energy[i][0] * 0.3f);
                fft_in_data[i+1].R1 += (int16_t)(vacuum_energy[i][0] * 0.3f);
                vacuum_energy[i][0] *= 0.4f;
            }
        }
    }
    
    if (rand() % 1000 < 3) {
        int entangled_a = rand() % (FFT_N/2);
        int entangled_b = rand() % (FFT_N/2);
        
        if (abs(fft_in_data[entangled_a].R1) > 1000) {
            fft_in_data[entangled_b].R1 = fft_in_data[entangled_a].R1;
            fft_in_data[entangled_b].I1 = -fft_in_data[entangled_a].I1;
        }
    }

}

#define MEMORY_DEPTH 16
fft_data_t spectral_memory[MEMORY_DEPTH][FFT_N/2];
int memory_index = 0;

void spectral_memory_engine() {

    for (int i = 0; i < FFT_N/2; i++) {
        spectral_memory[memory_index][i] = fft_in_data[i];
    }
    memory_index = (memory_index + 1) % MEMORY_DEPTH;
    
    if (rand() % 100 < 10) {
        int past = rand() % MEMORY_DEPTH;
        int blend_start = rand() % (FFT_N/2 - 20);
        int blend_length = 10 + (rand() % 30);
        
        for (int i = 0; i < blend_length; i++) {
            int pos = blend_start + i;
            if (pos < FFT_N/2) {
                float mix = (float)(rand() % 100) / 100.0f;
                fft_in_data[pos].R1 = (int16_t)(
                    fft_in_data[pos].R1 * (1.0f - mix) + 
                    spectral_memory[past][pos].R1 * mix
                );
                fft_in_data[pos].I1 = (int16_t)(
                    fft_in_data[pos].I1 * (1.0f - mix) + 
                    spectral_memory[past][pos].I1 * mix
                );
            }
        }
    }

}

void spectral_limiter() {

    float max_amp = 30000;
    
    for (int i = 0; i < FFT_N/2; i++) {
        float amp1 = sqrt(fft_in_data[i].R1 * fft_in_data[i].R1 + 
                         fft_in_data[i].I1 * fft_in_data[i].I1);
        float amp2 = sqrt(fft_in_data[i].R2 * fft_in_data[i].R2 + 
                         fft_in_data[i].I2 * fft_in_data[i].I2);
        
        if (amp1 > max_amp) {
            float scale = max_amp / amp1;
            fft_in_data[i].R1 *= scale;
            fft_in_data[i].I1 *= scale;
        }
        
        if (amp2 > max_amp) {
            float scale = max_amp / amp2;
            fft_in_data[i].R2 *= scale;
            fft_in_data[i].I2 *= scale;
        }
    }
    
    for (int i = FFT_N/4; i < FFT_N/2; i++) {
        fft_in_data[i].R1 *= 0.95f;
        fft_in_data[i].I1 *= 0.95f;
        fft_in_data[i].R2 *= 0.95f;
        fft_in_data[i].I2 *= 0.95f;
    }
    
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

    generate_base_signal();
    
    black_hole_processing();
    crystal_growth();
    quantum_foam();
    spectral_memory_engine();
    
    if (rand() % 1000 < 2) {
        black_hole_pos = (float)(rand() % 100) / 100;
        black_hole_mass = 0.5f + (rand() % 100) / 100.0f;
    }
    
    spectral_limiter();
    
    fft_complex_uint16_dma(DMAC_CHANNEL1, DMAC_CHANNEL2, 0, 
                           FFT_DIR_BACKWARD, (uint64_t *)fft_in_data, 
                           FFT_N, (uint64_t *)fft_out_data);
    
    for (int i = 0; i < FFT_N / 2; i++) {
        audio_out[2 * i]     = fft_out_data[i].R1;
        audio_out[2 * i + 1] = fft_out_data[i].R2;
    }
    
    delay(30);

}