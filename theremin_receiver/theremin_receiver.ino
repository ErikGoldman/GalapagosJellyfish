#include <Adafruit_NeoPixel.h>
#include <Wire.h>

#define AUDIO_PIN    9
#define NEOPIXEL_PIN 11

//#define DEBUG 1

#define NORMALIZED_MAX_INPUT 5000

#define SCREENSAVER_BREAK_INPUT_THRESHOLD 0.1
#define SCREENSAVER_IDLE_TIME (10*1000)
#define NEOPIXEL_UPDATE_MIN_MS 20

#define VOLUME_MUTE_CUTOFF 0.2

// lol, I dunno...
#define SATURATION_VALUE 150

#define STATUS_LED_PIN 13

#define NUM_SAMPLES 5

unsigned int cnt = 0;

/******** Sine wave parameters ********/
#define PI2 6.283185 // 2*PI saves calculation later
#define AMP 127 // Scaling factor for sine wave
#define OFFSET 128 // Offset shifts wave to all >0 values

/******** Lookup table ********/
#define LENGTH 64 // Length of the wave lookup table
byte wave_tables[2][LENGTH];
const int WAVE_TABLE_LENGTHS[2] = {LENGTH, LENGTH/2};
const static int LONG_WAVE_TABLE = 0, SHORT_WAVE_TABLE = 1;
byte *currWaveTable;
int currWaveTableLength;

int currToneValue = 0;

Adafruit_NeoPixel neopixels = Adafruit_NeoPixel(60, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

int lastVals[2][NUM_SAMPLES];
uint8_t lastPos[2];

unsigned long lastTimeWithNonZeroData;

void debug_print(const char *str) {
  #ifdef DEBUG
  Serial.print(str);
  #else
  if (cnt % 20 == 0) {
    Serial.print(str);
  }
  #endif
}

void debug_print(double i) {
  #ifdef DEBUG
  Serial.print(i);
  #else
  if (cnt % 20 == 0) {
    Serial.print(i);
  }
  #endif
}

void setup() {
  pinMode(NEOPIXEL_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(AUDIO_PIN, OUTPUT);
  
  Serial.begin(38400);
  
  Wire.begin();  
  setVolume(0);
  
  // initialize value buffer data
  for (int i=0; i < 2; i++) {
    for (int j=0; j < NUM_SAMPLES; j++) {
      lastVals[i][j] = 0;
    }
  }
  lastPos[0] = lastPos[1] = 0;
  lastTimeWithNonZeroData = millis();
  
  /******** Setup the waveform output code ********/
  /** Populate the waveform table with a sine wave **/
  for (int i=0; i < WAVE_TABLE_LENGTHS[LONG_WAVE_TABLE]; i++) { // Step across wave table
    float major = sin((PI2/WAVE_TABLE_LENGTHS[LONG_WAVE_TABLE])*i),
          h1    = sin((PI2/WAVE_TABLE_LENGTHS[LONG_WAVE_TABLE])*i*2),
          h2    = sin((PI2/WAVE_TABLE_LENGTHS[LONG_WAVE_TABLE])*i*4);
    
    float v = (AMP/2)*major + (AMP/4) * h1 + (AMP/8) * h2; // Compute value          
    wave_tables[LONG_WAVE_TABLE][i] = int(v+OFFSET); // Store value as integer
   }
   for (int i=0; i < WAVE_TABLE_LENGTHS[SHORT_WAVE_TABLE]; i++) { // Step across wave table
     float v = (AMP*sin((PI2/WAVE_TABLE_LENGTHS[SHORT_WAVE_TABLE])*i)); // Compute value
     wave_tables[SHORT_WAVE_TABLE][i] = int(v+OFFSET); // Store value as integer
   }
   
   currWaveTable = (byte*)wave_tables[LONG_WAVE_TABLE];
   currWaveTableLength = WAVE_TABLE_LENGTHS[LONG_WAVE_TABLE];
  
   pinMode(9, OUTPUT); // Make timer’s PWM pin an output
   noInterrupts();
   {
     // ****Set timer1 for 8-bit fast PWM output ****
     TCCR1B = (1 << CS10); // Set prescaler to full 16MHz
     TCCR1A |= (1 << COM1A1); // Pin low when TCNT1=OCR1A
     TCCR1A |= (1 << WGM10); // Use 8-bit fast PWM mode
     TCCR1B |= (1 << WGM12);
     OCR1AL = 0;
   }
   interrupts();

   neopixels.begin();
}

void loop() {  
  // block for data
  while(true) {
    // wait until something changes
    if (blockForByte() == 0xA1 &&
      blockForByte() == 0xB5) {
        
      int nextByte = blockForByte();
      
      int valIndex = -1;      
      if (nextByte == 0xC1) {
        valIndex = 0;
      } else if (nextByte == 0xC2) {
        valIndex = 1;
      }
       
      if (valIndex != -1) {
        int inputVal = serialReadInt();  
        lastVals[valIndex][lastPos[valIndex]] = inputVal;
        lastPos[valIndex] = (lastPos[valIndex]+1) % NUM_SAMPLES;        
        
        gotNewData();
      }    
    }
  }
}

void gotNewData() {
  static bool isStatusLEDLit = false;
  
  long currMs = millis();

  // get the averages from the Arduino sensors to smooth the data a bit  
  double thereminValues[2];
  for (int i=0; i < 2; i++) {
    thereminValues[i] = undoNonlinearityAndAverage(i);
  }
  
  cnt++;
  
  debug_print(currMs);
  debug_print(":\t");
  debug_print(thereminValues[0]);
  debug_print("\t");
  debug_print(thereminValues[1]);
  debug_print("\t");
  
  if (thereminValues[0] >= SCREENSAVER_BREAK_INPUT_THRESHOLD ||
      thereminValues[1] >= SCREENSAVER_BREAK_INPUT_THRESHOLD) {
    lastTimeWithNonZeroData = currMs;
  }
  
  if (currMs - lastTimeWithNonZeroData > SCREENSAVER_IDLE_TIME) {
    showScreenSaver();
  } else {
    playTheremin(thereminValues[0], thereminValues[1]);
  }
  
  debug_print("\n");  
}

// theremin values are scaled 0..1
void playTheremin(double thereminOne, double thereminTwo) {
  static long lastUpdateTime = 0;
  
  long currTime = millis();
  
  // set the music
  setToneValue(thereminOne);
  setVolume(thereminTwo);
  
  // set the lights
  setAllNeopixels(thereminOne * 255, SATURATION_VALUE, thereminTwo * 255);
}

// shows a fun demo that cycles through colors
// used to do something interesting if no one is playing the theremin
void showScreenSaver() {
  const static int H_STEP_TIME = 20, S_STEP_TIME = 30, V_STEP_TIME = 50;
  
  unsigned long ms = millis();
  
  uint8_t h = (ms / H_STEP_TIME) % 255,
          s = (ms / S_STEP_TIME) % 200 + 55,
          v = ((ms / V_STEP_TIME) % 105) + 50;          

  debug_print("SCREENSAVER");
  setVolume(0);
  setAllNeopixels(h, s, v);
}

int blockForByte() {
  static uint64_t lastWriteTime = 0;
  static uint16_t waveIndex=0; // Points to each table entry 
  
  while (!Serial.available()) {
    // delay    
    for (volatile uint32_t i=0; i < currToneValue; i++) {
    }
    
    waveIndex = waveIndex % currWaveTableLength;
    OCR1AL = currWaveTable[waveIndex++];
  }
  return Serial.read();
}

int serialReadInt() {
   int data = 0;
   for (int i=0; i < 4; i++) {
      uint8_t b = blockForByte();
      data |= (b << (i * 8));
   }
   return data;
}

// returns a value 0..1 corresponding to the theremin value
double undoNonlinearityAndAverage(int valIndex) {
  double sum = 0;
  for (int i=0; i < NUM_SAMPLES; i++) {
    double value = max(min(((double)lastVals[valIndex][i] / NORMALIZED_MAX_INPUT), 1.0), 0.0);
    
    // undo non-linearity by taking the square-root
    value = pow(value, 0.5);
    
    sum += value;
  }
  return sum / NUM_SAMPLES;
}

// thereminValue is 0..1
void setVolume(double thereminValue) {
  // 0101 control code, then A0, A1, A2 = 0 (grounded)
  const static byte DEVICE_ADDRESS = 0b0101000,
                    SET_POT1       = 0b10101001;
                    
  const static int VOLUME_RANGE = 64 - 4; // set a reasonable max loudness
                    
  byte potValue = (1 << 6); // mute mode
  if (thereminValue >= VOLUME_MUTE_CUTOFF) {
    // rescale from MUTE..1 to 0..1
    thereminValue = (thereminValue - VOLUME_MUTE_CUTOFF) / (1 - VOLUME_MUTE_CUTOFF);
    // take a root for non-linearity
    thereminValue = pow(thereminValue, 0.2);
    
    potValue = max(64 - (int)(thereminValue * VOLUME_RANGE), 0);
    
    debug_print("Volume: ");
    debug_print(potValue);
  } else {
    debug_print("Volume: MUTED");
  }
  
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(byte(SET_POT1)); // DS1807 change POT1 command (from datasheet)
  Wire.write(potValue); // send value
  //Wire.write(0); // send value
  Wire.endTransmission(true); // end and release the bus  
}

// thereminValue is 0..1
void setToneValue(double thereminValue) {
  const static int MAX_TONE = 75, MIN_TONE = 1;
  
  int toneValue = max(min((MAX_TONE - (int)(thereminValue * (MAX_TONE - MIN_TONE))), MAX_TONE), MIN_TONE);
  
  debug_print("Tone: ");
  debug_print(toneValue);
  
  currToneValue = toneValue;
}

void HSVtoRGB(uint8_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
  float r2, g2, b2;
  
  _HSVtoRGB(&r2, &g2, &b2, (float)h/255.0, (float)s/255.0, (float)v/255.0);
  
  *r = (uint8_t)(r2*255);
  *g = (uint8_t)(g2*255);
  *b = (uint8_t)(b2*255);
} 

void _HSVtoRGB( float *r, float *g, float *b, float h, float s, float v ) {
  
  int i;
  float f, p, q, t;
  if( s == 0 ) {
    // achromatic (grey)
    *r = *g = *b = v;
    return;
  }

  h /= 60;			// sector 0 to 5
  i = floor( h );
  f = h - i;			// factorial part of h
  p = v * ( 1 - s );
  q = v * ( 1 - s * f );
  t = v * ( 1 - s * ( 1 - f ) );
  
  switch( i ) {
    case 0:
      *r = v;
      *g = t;
      *b = p;
      break;
    case 1:
      *r = q;
      *g = v;
      *b = p;
      break;
    case 2:
      *r = p;
      *g = v;
      *b = t;
      break;
    case 3:
      *r = p;
      *g = q;
      *b = v;
      break;
    case 4:
      *r = t;
      *g = p;
      *b = v;
      break;
    default:		// case 5:
      *r = v;
      *g = p;
      *b = q;
      break;
  }
}

void setAllNeopixels(int h, int s, int v) {
  static long lastUpdateTime = 0;
  
  long currTime = millis();
  
  if (currTime - lastUpdateTime >= NEOPIXEL_UPDATE_MIN_MS) {
    uint8_t r, g, b;
    HSVtoRGB(h, s, v, &r, &g, &b);
    int color = neopixels.Color(r, g, b);
  
    for (int i=0; i < neopixels.numPixels(); i++) {
      neopixels.setPixelColor(i, color);
    }
    neopixels.show();
    
    lastUpdateTime = currTime;
  }
}
