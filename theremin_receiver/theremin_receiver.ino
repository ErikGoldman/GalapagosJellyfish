#include <Adafruit_NeoPixel.h>

#define NEOPIXEL_PIN 6

#define NORMALIZED_MAX_INPUT 2000

#define SCREENSAVER_BREAK_INPUT_THRESHOLD 50
#define SCREENSAVER_IDLE_TIME (10*1000)
#define NEOPIXEL_UPDATE_MIN_MS 20

// lol, I dunno...
#define SATURATION_VALUE 150

#define STATUS_LED_PIN 13

#define NUM_SAMPLES 5

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
  
  neopixels.begin();
}

void loop() {
  static bool isStatusLEDLit = false;
  
  long currMs = millis();

  // get the averages from the Arduino sensors to smooth the data a bit  
  double valAvgs[2];
  for (int i=0; i < 2; i++) {
    valAvgs[i] = getValAverage(i);   
  }
  
  Serial.print(currMs);
  Serial.print(":\t");
  Serial.print(valAvgs[0]);
  Serial.print("\t");
  Serial.println(valAvgs[1]);
  
  if (valAvgs[0] >= SCREENSAVER_BREAK_INPUT_THRESHOLD ||
      valAvgs[1] >= SCREENSAVER_BREAK_INPUT_THRESHOLD) {
    lastTimeWithNonZeroData = currMs;
  }
  
  if (currMs - lastTimeWithNonZeroData > SCREENSAVER_IDLE_TIME) {
    showScreenSaver();
  } else {  
    // take 0 --> NORMALIZED_MAX_INPUT and rescale to 0 --> 255
    int scaledValues[2];
    for (int i=0; i < 2; i++) {
      scaledValues[i] = max(0, min((valAvgs[i] / NORMALIZED_MAX_INPUT)*255, 255));
    }
    
    // convert input values --> HSV --> RGB
    uint8_t r, g, b;
    HSVtoRGB(scaledValues[0], SATURATION_VALUE, scaledValues[1], &r, &g, &b);
    setAllNeopixels(neopixels.Color(r, g, b));
  }
  
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

// shows a fun demo that cycles through colors
// used to do something interesting if no one is playing the theremin
void showScreenSaver() {
  const static int H_STEP_TIME = 20, S_STEP_TIME = 30, V_STEP_TIME = 50;
  
  unsigned long ms = millis();
  
  uint8_t h = (ms / H_STEP_TIME) % 255,
          s = (ms / S_STEP_TIME) % 200 + 55,
          v = ((ms / V_STEP_TIME) % 105) + 150;          
  uint8_t r, g, b;
  
  HSVtoRGB(h, s, v, &r, &g, &b);
  
  Serial.print("Showing screensaver (");
  Serial.print(r);
  Serial.print(" ");
  Serial.print(g);
  Serial.print(" ");
  Serial.print(b);
  Serial.println(")");
  
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

double getValAverage(int valIndex) {
  double sum = 0;
  for (int i=0; i < NUM_SAMPLES; i++) {
    sum += lastVals[valIndex][i];
  }
  return sum / NUM_SAMPLES;
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
