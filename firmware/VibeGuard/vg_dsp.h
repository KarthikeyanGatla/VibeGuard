// Hann window + 256-point radix-2 FFT, written out so it runs the same on the ESP32 and the host.
#pragma once
#include <math.h>
#include <stdint.h>
#include "vg_config.h"

namespace vg {

class Fft {
 public:
  Fft() {
    for (int i = 0; i < N_FFT; i++) hann_[i] = 0.5f - 0.5f * cosf(2.0f * kPi * i / N_FFT);  // periodic Hann
    for (int i = 0; i < N_FFT / 2; i++) {
      cos_[i] = cosf(2.0f * kPi * i / N_FFT);
      sin_[i] = sinf(2.0f * kPi * i / N_FFT);
    }
    int bits = 0;
    while ((1 << bits) < N_FFT) bits++;
    for (int i = 0; i < N_FFT; i++) {
      int r = 0;
      for (int b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b);
      rev_[i] = (uint16_t)r;
    }
  }

  // Sum of the window; a sine of amplitude A gives |X| = A * WINDOW_SUM / 2 at its bin.
  static constexpr float WINDOW_SUM = N_FFT / 2.0f;

  // x: N_FFT samples with DC removed. power: N_BINS values of |X[k]|^2.
  void powerSpectrum(const float* x, float* power) {
    for (int i = 0; i < N_FFT; i++) {
      re_[rev_[i]] = x[i] * hann_[i];
      im_[rev_[i]] = 0.0f;
    }
    for (int size = 2; size <= N_FFT; size <<= 1) {
      int half = size >> 1, step = N_FFT / size;
      for (int i = 0; i < N_FFT; i += size) {
        for (int j = 0; j < half; j++) {
          float wr = cos_[j * step], wi = -sin_[j * step];
          int a = i + j, b = a + half;
          float tr = wr * re_[b] - wi * im_[b];
          float ti = wr * im_[b] + wi * re_[b];
          re_[b] = re_[a] - tr;
          im_[b] = im_[a] - ti;
          re_[a] += tr;
          im_[a] += ti;
        }
      }
    }
    for (int k = 0; k < N_BINS; k++) power[k] = re_[k] * re_[k] + im_[k] * im_[k];
  }

 private:
  float hann_[N_FFT], cos_[N_FFT / 2], sin_[N_FFT / 2], re_[N_FFT], im_[N_FFT];
  uint16_t rev_[N_FFT];
};

}  // namespace vg
