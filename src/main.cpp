#include <FastLED.h>
#include <arduinoFFT.h>

#define LED_PIN 6
#define NUM_LEDS 64
#define SAMPLE_RATE 44100
// #define BUFFER_SIZE 128
#define NUM_BANDS 8

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  Serial.begin(9600);

  // Set up audio input with ADC on Arduino Mega
  ADCSRA |= 0b00000111;     // ADC prescaler of 128
  ADMUX |= 0b01100000;      // AVCC reference voltage, right adjust result
  ADCSRB &= 0b11111000;     // Free running mode
  ADCSRA |= 0b01000000;     // Start ADC conversions
}

void loop() {
  // Read audio data from ADC and perform FFT
  uint16_t BUFFER_SIZE = 128;
  double audioBuffer[BUFFER_SIZE];
  arduinoFFT FFT = arduinoFFT(); /* Create FFT object */

  for (int i = 0; i < BUFFER_SIZE; i++) {
    while (!(ADCSRA & (1 << ADIF)));   // Wait for ADC conversion to complete
    audioBuffer[i] = ADC;               // Store audio sample in buffer
    ADCSRA |= 0b01000000;              // Start next ADC conversion
  }

  // Perform FFT on audio buffer
  FFT.Windowing(audioBuffer,BUFFER_SIZE,FFT_WIN_TYP_HAMMING,FFT_FORWARD);
  // FFT.Windowing(audioBuffer, BUFFER_SIZE, FFT_WIN_TYP_HAMMING);
   double vReal[BUFFER_SIZE];
  double vImag[BUFFER_SIZE];
  FFT.Compute(vReal,vImag,BUFFER_SIZE,FFT_FORWARD);
  // FFT.Compute(audioBuffer, BUFFER_SIZE, FFT_FORWARD);
 
  // FFT.GetResults(vReal, vImag);
 

  // Calculate frequency band amplitudes
  int bandWidth = BUFFER_SIZE / NUM_BANDS;
  double bandAmplitudes[NUM_BANDS] = { 0.0 };

  for (int i = 0; i < BUFFER_SIZE / 2; i++) {
    int bandIndex = i / bandWidth;
    bandAmplitudes[bandIndex] += sqrt(vReal[i] * vReal[i] + vImag[i] * vImag[i]);
  }

  // Normalize band amplitudes
  double maxAmplitude = 0.0;

  for (int i = 0; i < NUM_BANDS; i++) {
    if (bandAmplitudes[i] > maxAmplitude) {
      maxAmplitude = bandAmplitudes[i];
    }
  }

  for (int i = 0; i < NUM_BANDS; i++) {
    bandAmplitudes[i] = map(bandAmplitudes[i], 0, maxAmplitude, 0, 255);
  }

  // Update LED matrix based on frequency band amplitudes
  int ledIndex = 0;

  for (int band = 0; band < NUM_BANDS; band++) {
    int bandWidth = NUM_LEDS / NUM_BANDS;
    int ledBrightness = bandAmplitudes[band];
    Serial.print("Frequency Band: ");
    Serial.println(band);
    Serial.print("Amplitdue value: ");
    Serial.println(ledBrightness);
    for (int i = 0; i < bandWidth; i++) {
      leds[ledIndex++] = CRGB(ledBrightness, 10, 10);   // Set LED color (red)
    }
  }

  FastLED.show();   // Update LED matrix
}


// #include <FastLED.h>
// #include <arduinoFFT.h>

// #define NUM_LEDS 64
// #define LED_PIN 6
// #define LED_TYPE WS2812B
// #define COLOR_ORDER GRB
// CRGB leds[NUM_LEDS];

// #define AUDIO_INPUT A0

// #define SAMPLES 128  // Number of samples to take for FFT (must be a power of 2)
// #define SAMPLING_FREQUENCY 10000  // Set the sampling frequency to 10kHz
// #define NUM_BANDS 4  // Number of frequency bands you want to analyze and animate
// #define BAND_WIDTH (SAMPLING_FREQUENCY / 2) / NUM_BANDS

// double vReal[SAMPLES];
// double vImag[SAMPLES];
// arduinoFFT FFT = arduinoFFT();

// int bandRanges[NUM_BANDS + 1] = {0, 16, 32, 48, 64}; // LED index ranges for each band (adjust as needed)
// float bandAmplitudes[NUM_BANDS] = {0};  // Amplitude of each band

// // Example test signal (replace this with your desired pattern)
// const int testSignal[] = {100, 200, 300, 400, 500, 400, 300, 200, 100};
// const int testSignalSize = sizeof(testSignal) / sizeof(testSignal[0]);

// // const int scaleFactor = 5; // Adjust this factor as needed
// // const int testSignal[] = {100, 200, 300, 400, 500, 400, 300, 200, 100};
// // const int testSignalSize = sizeof(testSignal) / sizeof(testSignal[0]);
// // int scaledTestSignal[testSignalSize];

// void setup() {
//   // Setup LED strip
//   FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
//   FastLED.setBrightness(100);

//   // Other setup configurations
//   pinMode(AUDIO_INPUT, INPUT);
//   Serial.begin(9600);
// }

// void loop() {
//   // Read audio input and convert to digital form
//  /*
//  int audioSample;
//   for (int i = 0; i < SAMPLES; i++) {
//     audioSample = analogRead(AUDIO_INPUT);
//     vReal[i] = audioSample;
//     vImag[i] = 0;
//     delayMicroseconds(50);
//   }
// */

// // Use the test signal instead:
// for (int i = 0; i < SAMPLES; i++) {
//   vReal[i] = testSignal[i % testSignalSize];
//   vImag[i] = 0;
// }
// // for (int i = 0; i < testSignalSize; i++) {
// //   scaledTestSignal[i] = testSignal[i] / scaleFactor;
// // }

//   // Perform FFT
//   FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
//   FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
//   FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);
  
//   // Calculate amplitude for each band
//   for (int band = 0; band < NUM_BANDS; band++) {
//     float amplitude = 0;
//     int startIndex = bandRanges[band];
//     int endIndex = bandRanges[band + 1];

//     for (int i = startIndex; i < endIndex; i++) {
//       amplitude += vReal[i];
//     }
//     bandAmplitudes[band] = amplitude/1000;
//     Serial.print("Frequency Band: ");
//     Serial.println(band);
//     Serial.print("Amplitdue value: ");
//     int x = 255 - (bandAmplitudes[band] * 255);
//     Serial.print(x);
//     Serial.println(" |Mag|");
//   }
  
//   /*
//   Remove audio normalization (optional): Since you are using a predefined test signal, there's no need to normalize the amplitudes. You can remove the normalization part of the code:
//   */
//   // Normalize amplitudes

//   // float maxAmplitude = 0;
//   // for (int band = 0; band < NUM_BANDS; band++) {
//   //   if (bandAmplitudes[band] > maxAmplitude) {
//   //     maxAmplitude = bandAmplitudes[band];
//   //   }
//   // }
//   // for (int band = 0; band < NUM_BANDS; band++) {
//   //   bandAmplitudes[band] /= maxAmplitude;
//   // }


//   // Animate LEDs based on amplitude of each band
//   for (int band = 0; band < NUM_BANDS; band++) {
//     int startIndex = bandRanges[band];
//     int endIndex = bandRanges[band + 1];

//     for (int i = startIndex; i < endIndex; i++) {
//       CRGB color = CRGB::Red; // Choose the color based on the band (adjust as needed)
//       leds[i] = color.fadeToBlackBy(255 - (bandAmplitudes[band] * 255));


//       // test
//     //   float threshold = 0.2;
//     //   if (bandAmplitudes[band] > threshold) {
//     //     leds[i] = color.fadeToBlackBy(255 - (bandAmplitudes[band] * 255));
//     //   } else {
//     //     leds[i] = CRGB::Black;
//     //   }
//      }
//   }

//   // Update LED strip
//   FastLED.show();
// }
