#define R_PIN 6
#define G_PIN 5
#define B_PIN 10

#define NORMALIZED_MAX_INPUT 2000

#define STATUS_LED_PIN 13

#define NUM_SAMPLES 5

int lastVals[2][NUM_SAMPLES];
uint8_t lastPos[2];

void setup() {
  pinMode(R_PIN, OUTPUT);
  pinMode(G_PIN, OUTPUT);
  pinMode(B_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  
  Serial.begin(38400);
  
  for (int i=0; i < 2; i++) {
    for (int j=0; j < NUM_SAMPLES; j++) {
      lastVals[i][j] = 0;
    }
  }
  lastPos[0] = lastPos[1] = 0;
}

uint8_t R=0, G=40, B=40;

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

long cnt = 0;
void loop() {
  digitalWrite(STATUS_LED_PIN, HIGH);
  
  double valAvgs[2];
  for (int i=0; i < 2; i++) {
    valAvgs[i] = getValAverage(i);   
  }
  
  Serial.print(valAvgs[0]);
  Serial.print(" ");
  Serial.println(valAvgs[1]);    

  // take 0 --> NORMALIZED_MAX_INPUT and rescale to 0 --> 255 for the lights
  for (int i=0; i < 2; i++) {
    valAvgs[i] = min((valAvgs[i] / NORMALIZED_MAX_INPUT)*255, 255);
  }
  analogWrite(R_PIN, (int)valAvgs[0]);
  analogWrite(G_PIN, (int)valAvgs[1]);
  analogWrite(B_PIN, 45);
  
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
        cnt++;
        
        break;
      }    
    }
    digitalWrite(STATUS_LED_PIN, LOW);
  }
}
