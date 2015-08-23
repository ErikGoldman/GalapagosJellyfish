#include <Adafruit_NeoPixel.h>

#define NEOPIXEL_PIN 11

#define NORMALIZED_MAX_INPUT 2000

#define SCREENSAVER_BREAK_INPUT_THRESHOLD 0.1
#define SCREENSAVER_IDLE_TIME (10*1000)
#define NEOPIXEL_UPDATE_MIN_MS 20

// lol, I dunno...
#define SATURATION_VALUE 150

#define STATUS_LED_PIN 13

#define NUM_SAMPLES 5

/******** Sine wave parameters ********/
#define PI2 6.283185 // 2*PI saves calculation later
#define AMP 127 // Scaling factor for sine wave
#define OFFSET 128 // Offset shifts wave to all >0 values

/******** Lookup table ********/
#define LENGTH 256 // Length of the wave lookup table
byte wave_tables[2][256];
const int WAVE_TABLE_LENGTHS[2] = {256, 128};
const static int LONG_WAVE_TABLE = 0, SHORT_WAVE_TABLE = 1;
byte *currWaveTable;
int currWaveTableLength;

Adafruit_NeoPixel neopixels = Adafruit_NeoPixel(60, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

int lastVals[2][NUM_SAMPLES];
uint8_t lastPos[2];

unsigned long lastTimeWithNonZeroData;

void setup() {
  pinMode(NEOPIXEL_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  
  Serial.begin(38400);
  
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
     float v = (AMP*sin((PI2/WAVE_TABLE_LENGTHS[LONG_WAVE_TABLE])*i)); // Compute value
     wave_tables[LONG_WAVE_TABLE][i] = int(v+OFFSET); // Store value as integer
   }
   for (int i=0; i < WAVE_TABLE_LENGTHS[SHORT_WAVE_TABLE]; i++) { // Step across wave table
     float v = (AMP*sin((PI2/WAVE_TABLE_LENGTHS[SHORT_WAVE_TABLE])*i)); // Compute value
     wave_tables[SHORT_WAVE_TABLE][i] = int(v+OFFSET); // Store value as integer
   }
   
   currWaveTable = (byte*)wave_tables[0];
   currWaveTableLength = WAVE_TABLE_LENGTHS[0];
  
   pinMode(9, OUTPUT); // Make timer’s PWM pin an output
   noInterrupts();
   {
     /****Set timer1 for 8-bit fast PWM output ****/
     TCCR1B = (1 << CS10); // Set prescaler to full 16MHz
     TCCR1A |= (1 << COM1A1); // Pin low when TCNT1=OCR1A
     TCCR1A |= (1 << WGM10); // Use 8-bit fast PWM mode
     TCCR1B |= (1 << WGM12);
    
    /******** Set up timer2 to call ISR ********/
     TCCR2A = 0; // No options in control register A
     TCCR2B = (1 << CS21); // Set prescaler to divide by 8
     TIMSK2 = 0; // don't call ISR for now
   }
   interrupts();
   
   setIsPlayingTone(false);
  
   neopixels.begin();
}

void loop() {
  static bool isStatusLEDLit = false;
  
  long currMs = millis();

  // get the averages from the Arduino sensors to smooth the data a bit  
  double thereminValues[2];
  for (int i=0; i < 2; i++) {
    thereminValues[i] = undoNonlinearityAndAverage(i);
  }
  
  Serial.print(currMs);
  Serial.print(":\t");
  Serial.print(thereminValues[0]);
  Serial.print("\t");
  Serial.print(thereminValues[1]);
  Serial.print("\t");
  
  if (thereminValues[0] >= SCREENSAVER_BREAK_INPUT_THRESHOLD ||
      thereminValues[1] >= SCREENSAVER_BREAK_INPUT_THRESHOLD) {
    lastTimeWithNonZeroData = currMs;
  }
  
  if (currMs - lastTimeWithNonZeroData > SCREENSAVER_IDLE_TIME) {
    showScreenSaver();
  } else {
    playTheremin(thereminValues[0], thereminValues[1]);
  }
  
  Serial.println("");
  
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
        digitalWrite(STATUS_LED_PIN, isStatusLEDLit ? HIGH : LOW);
        isStatusLEDLit = !isStatusLEDLit; 
        
        int inputVal = serialReadInt();  
        lastVals[valIndex][lastPos[valIndex]] = inputVal;
        lastPos[valIndex] = (lastPos[valIndex]+1) % NUM_SAMPLES;        
        break;
      }    
    }
  }
}

// theremin values are scaled 0..1
void playTheremin(double thereminOne, double thereminTwo) {
  // set the music
  setToneValue(thereminOne);
  setVolume(thereminTwo);

  // set the lights
  uint8_t r, g, b;
  HSVtoRGB(thereminOne * 255, SATURATION_VALUE, thereminOne * 255, &r, &g, &b);
  setAllNeopixels(neopixels.Color(r, g, b));
}

// shows a fun demo that cycles through colors
// used to do something interesting if no one is playing the theremin
void showScreenSaver() {
  const static int H_STEP_TIME = 20, S_STEP_TIME = 30, V_STEP_TIME = 50;
  
  unsigned long ms = millis();
  
  uint8_t h = (ms / H_STEP_TIME) % 255,
          s = (ms / S_STEP_TIME) % 200 + 55,
          v = ((ms / V_STEP_TIME) % 105) + 50;          
  uint8_t r, g, b;
  
  HSVtoRGB(h, s, v, &r, &g, &b);
  
  Serial.print("SCREENSAVER");
  setAllNeopixels(neopixels.Color(r, g, b));
}

int blockForByte() {
  while (!Serial.available()) {
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
    double value = (lastVals[valIndex][i] / NORMALIZED_MAX_INPUT);
    
    // undo non-linearity by taking the square-root
    value = pow(value, 0.5);
    
    sum += value;
  }
  return sum / NUM_SAMPLES;
}

void setIsPlayingTone(bool isPlaying) {
  static bool lastIsPlaying = false, isInitialized = false;
  
  if (isPlaying == lastIsPlaying && isInitialized) {
    return;
  }
  
  isInitialized = true;  
  lastIsPlaying = isPlaying;
  
  if (isPlaying) {
    TIMSK2 = (1 << OCIE2A); // Call ISR when TCNT2 = OCRA2
  } else {
    TIMSK2 = 0; // stop the timer ISR
    OCR1AL = 0; // stop PWM output by always outputting a 0
  }
}

// thereminValue is 0..1
void setVolume(double thereminValue) {
  // TODO: this physical theremin is busted so just say yes always =)
  setIsPlayingTone(true);
}

// thereminValue is 0..1
void setToneValue(double thereminValue) {
  int toneValue = (int)(thereminValue * 65 + 10);
  
  Serial.print("Tone: ");
  Serial.print(toneValue);
  
  if (toneValue <= 5) {
    // for now this is too fast -- error and continue
    Serial.print(" ******* TOO LOW!!! ERROR!!! ******** ");
    toneValue = 5;
  }
  
  // if tone values get below 32, use the short wave table so we don't fire the
  // time interrupt so frequently
  if (toneValue < 32) {
    currWaveTable = (byte*)wave_tables[SHORT_WAVE_TABLE];
    OCR2A = toneValue * 2;
  } else {
    currWaveTable = (byte*)wave_tables[LONG_WAVE_TABLE];
    OCR2A = toneValue;
  }
}

/******** Called every time TCNT2 = OCR2A ********/
ISR(TIMER2_COMPA_vect) { // Called when TCNT2 == OCR2A
  static byte waveIndex=0; // Points to each table entry

  // index into the correct wave table and set the PWM value
  waveIndex = waveIndex % currWaveTableLength;
  OCR1AL = currWaveTable[waveIndex++];

  //asm(“NOP;NOP”); // Fine tuning
  TCNT2 = 6; // Timing to compensate for ISR run time
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

void setAllNeopixels(uint32_t color) {
  static long lastUpdateTime = 0;
  
  long currTime = millis();
  
  if (currTime - lastUpdateTime >= NEOPIXEL_UPDATE_MIN_MS) {
    for (int i=0; i < neopixels.numPixels(); i++) {
      neopixels.setPixelColor(i, color);
    }
    neopixels.show();
    
    lastUpdateTime = currTime;
  }
}
