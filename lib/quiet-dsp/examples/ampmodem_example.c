// 
// ampmodem_test.c
//
// Tests simple modulation/demodulation of the ampmodem (analog
// amplitude modulator/demodulator) with noise, carrier phase,
// and carrier frequency offsets.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <complex.h>

#include "liquid.h"

#define OUTPUT_FILENAME "ampmodem_example.m"

// print usage/help message
void usage()
{
    printf("ampmodem_example [options]\n");
    printf("  -h         : print usage\n");
    printf("  -f <freq>  : frequency offset,      default: 0.02\n");
    printf("  -p <phase> : phase offset,          default: -pi/4\n");
    printf("  -n <num>   : number of samples,     default: 256\n");
    printf("  -S <snr>   : SNR [dB],              default: 20\n");
    printf("  -t <type>  : AM type (dsb/usb/lsb), default: dsb\n");
    printf("  -s         : suppress the carrier,  default: off\n");
}

int main(int argc, char*argv[])
{
    // options
    float        mod_index          = 0.1f;      // modulation index (bandwidth)
    float        fc                 = 0.0f;      // AM carrier
    float        cfo                = 0.02f;     // carrier frequency offset
    float        cpo                = -0.1*M_PI; // carrier phase offset
    unsigned int num_samples        = 256;       // number of samples
    float        SNRdB              = 30.0f;     // signal-to-noise ratio [dB]
    liquid_ampmodem_type type       = LIQUID_AMPMODEM_USB;
    int          suppressed_carrier = 0;

    int dopt;
    while ((dopt = getopt(argc,argv,"hf:p:n:S:t:s")) != EOF) {
        switch (dopt) {
        case 'h': usage();                    return 0;
        case 'f': cfo = atof(optarg);         break;
        case 'p': cpo = atof(optarg);         break;
        case 'n': num_samples = atoi(optarg); break;
        case 'S': SNRdB = atof(optarg);       break;
        case 't':
            if (strcmp(optarg,"dsb")==0) {
                type = LIQUID_AMPMODEM_DSB;
            } else if (strcmp(optarg,"usb")==0) {
                type = LIQUID_AMPMODEM_USB;
            } else if (strcmp(optarg,"lsb")==0) {
                type = LIQUID_AMPMODEM_LSB;
            } else {
                fprintf(stderr,"error: %s, invalid AM type: %s\n", argv[0], optarg);
                return 1;
            }
            break;
        case 's': suppressed_carrier = 1;     break;
        default:                              return 1;
        }
    }

    // create mod/demod objects
    ampmodem mod   = ampmodem_create(mod_index, fc, type, suppressed_carrier);
    ampmodem demod = ampmodem_create(mod_index, fc, type, suppressed_carrier);
    ampmodem_print(mod);

    unsigned int i;
    float x[num_samples];
    liquid_float_complex y[num_samples];
    float z[num_samples];

    // generate 'audio' signal (filtered noise)
    iirfilt_rrrf faudio = iirfilt_rrrf_create_prototype(
        LIQUID_IIRDES_ELLIP, LIQUID_IIRDES_LOWPASS, LIQUID_IIRDES_SOS,
        5 /*order*/, 0.2f /*fc*/, 0.22f /*f0*/, 1.0f /*Ap*/, 40.0f/*As*/);
    for (i=0; i<num_samples; i++) {
        // push random sample
        iirfilt_rrrf_execute(faudio, randnf(), &x[i]);

        // clip
        x[i] = tanhf(x[i]);
    }
    iirfilt_rrrf_destroy(faudio);

    // modulate signal
    for (i=0; i<num_samples; i++)
        ampmodem_modulate(mod, x[i], &y[i]);

    // add channel impairments
    float nstd = powf(10.0f,-SNRdB/20.0f);
    for (i=0; i<num_samples; i++) {
        y[i] *= cexpf(_Complex_I*(2*M_PI*cfo*i + cpo));
        y[i] += nstd*(randnf() + _Complex_I*randnf())*M_SQRT1_2;
    }

    // demodulate signal
    for (i=0; i<num_samples; i++)
        ampmodem_demodulate(demod, y[i], &z[i]);

    // destroy objects
    ampmodem_destroy(mod);
    ampmodem_destroy(demod);

    // compute demodulation error
    unsigned int delay = (type == LIQUID_AMPMODEM_DSB) ? 0 : 18; // fixed delay
    float rmse = 0.0f;
    for (i=delay; i<num_samples; i++)
        rmse += (x[i-delay] - z[i]) * (x[i-delay] - z[i]);
    rmse = sqrtf( rmse / (float)(num_samples-delay) );
    printf("rms error : %12.8f dB\n", 10*log10f(rmse));

    // export results
    FILE * fid = fopen(OUTPUT_FILENAME,"w");
    fprintf(fid,"%% %s : auto-generated file\n", OUTPUT_FILENAME);
    fprintf(fid,"clear all\n");
    fprintf(fid,"close all\n");
    fprintf(fid,"n=%u;\n",num_samples);
    fprintf(fid,"delay=%u;\n", delay);
    for (i=0; i<num_samples; i++) {
        fprintf(fid,"x(%3u) = %12.4e;\n", i+1, x[i]);
        fprintf(fid,"y(%3u) = %12.4e + j*%12.4e;\n", i+1, crealf(y[i]), cimagf(y[i]));
        fprintf(fid,"z(%3u) = %12.4e;\n", i+1, z[i]);
    }
    // plot results
    fprintf(fid,"t=0:(n-1);\n");
    fprintf(fid,"figure('position',[100 100 800 600]);\n");
    fprintf(fid,"subplot(2,1,1);\n");
    fprintf(fid,"  plot(t,x,t-delay,z);\n");
    fprintf(fid,"  axis([-delay n -1.2 1.2]);\n");
    fprintf(fid,"  xlabel('time');\n");
    fprintf(fid,"  ylabel('signal');\n");
    fprintf(fid,"  legend('original','demodulated');\n");
    fprintf(fid,"  grid on;\n");
    // spectrum
    fprintf(fid,"subplot(2,1,2);\n");
    fprintf(fid,"  nfft=1024;\n");
    fprintf(fid,"  f=[0:(nfft-1)]/nfft - 0.5;\n");
    fprintf(fid,"  Y = 20*log10(abs(fftshift(fft(y,nfft))));\n");
    fprintf(fid,"  Y = Y - max(Y);\n");
    fprintf(fid,"  plot(f,Y);\n");
    fprintf(fid,"  axis([-0.5 0.5 -60 10]);\n");
    fprintf(fid,"  grid on;\n");
    fclose(fid);
    printf("results written to %s\n", OUTPUT_FILENAME);
    return 0;
}
