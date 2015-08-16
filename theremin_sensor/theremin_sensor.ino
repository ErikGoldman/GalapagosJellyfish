/* Theremin Sensor: Gatherer
 *
 * Theremin with TTL Oscillator 4MHz
 * Timer1 for freauency measurement
 * Timer2 for gate time
 * connect Oscillator on digital pin 5
 * connect Speaker with 1K Resistor in series on pin 8

 * KHM 2008 /  Martin Nawrath
 * Kunsthochschule fuer Medien Koeln
 * Academy of Media Arts Cologne

 */
#include <Stdio.h>
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))

//! Macro that clears all Timer/Counter1 interrupt flags.
#define CLEAR_ALL_TIMER1_INT_FLAGS    (TIFR1 = TIFR1)

#define DEBUG 0
#define NORMALIZED_MAX 2000.0

int pinLed = 13;                 // LED connected to digital pin 13
int pinFreq = 5;

const int SERIAL_BUFFER_SIZE = sizeof(int);

bool has_header_one = false, has_header_two = false, has_header_three = false, has_gotten_serial_data = false;
int serial_in_buffer[SERIAL_BUFFER_SIZE];
int serial_in_buffer_position = 0;
int other_theremin_last_val = 0;

void setup()
{
  pinMode(pinLed, OUTPUT);      // sets the digital pin as output
  pinMode(pinFreq, INPUT);

  Serial.begin(38400); // output
  memset(serial_in_buffer, 0, sizeof(int)*SERIAL_BUFFER_SIZE);

  // hardware counter setup ( refer atmega168.pdf chapter 16-bit counter1)
  TCCR1A=0;                   // reset timer/counter1 control register A
  TCCR1B=0;                   // reset timer/counter1 control register A
  TCNT1=0;                    // counter value = 0
  // set timer/counter1 hardware as counter , counts events on pin T1 ( arduino pin 5)
  // normal mode, wgm10 .. wgm13 = 0
  sbi (TCCR1B ,CS10);         // External clock source on T1 pin. Clock on rising edge.
  sbi (TCCR1B ,CS11);
  sbi (TCCR1B ,CS12);

  // timer2 setup / is used for frequency measurement gatetime generation
  // timer 2 presaler set to 256 / timer 2 clock = 16Mhz / 256 = 62500 Hz
  cbi (TCCR2B ,CS20);
  sbi (TCCR2B ,CS21);
  sbi (TCCR2B ,CS22);

  //set timer2 to CTC Mode
  cbi (TCCR2A ,WGM20);
  sbi (TCCR2A ,WGM21);
  cbi (TCCR2B ,WGM22);
  OCR2A = 124;                  // CTC at top of OCR2A / timer2 interrupt when coun value reaches OCR2A value

  // interrupt control

  sbi (TIMSK2,OCIE2A);          // enable Timer2 Interrupt

}

volatile byte i_tics;
volatile byte f_ready ;
volatile byte mlt ;
unsigned int ww;

int cal;
int cal_max;

char st1[32];
long freq_in;
long freq_zero;
long freq_cal;

long largest_value_seen = 1;

unsigned int dds;
int32_t tune = 0;

int cnt=0;

void dbg_print(const char *str) {
  #ifdef DEBUG
  Serial.print(str);
  #endif
}

void dbg_print(int val) {
  #ifdef DEBUG
  Serial.print(val);
  #endif
}

int readByteOptimistically() {
  if (Serial.available()) {
    return Serial.read();
  }
  return -1;
}

void writeInt(int toWrite) {    
  uint8_t *ptr = (uint8_t*)&toWrite;
  for (int i=0; i < sizeof(toWrite); i++) {
    Serial.write(ptr[i]);
  }
}

int bufferToInt() {
   int32_t data = 0;
   for (int i=0; i < 4; i++) {
      int b = serial_in_buffer[i];
      data |= (b << (i * 8));
   }
   return data;
}

void resetBuffer() {
  has_header_one = false;
  has_header_two = false;
  has_header_three = false;
  serial_in_buffer_position = 0;
}

int processSerialByteIfAvailable() {
  int byteData = readByteOptimistically();
  if (byteData != -1) {
    if (!has_header_one && !has_header_two && !has_header_three && byteData == 0xA1) {
      has_header_one = true;
    } else if (has_header_one && !has_header_two && !has_header_three && byteData == 0xB5) {
      has_header_two = true;
    } else if (has_header_one && has_header_two && !has_header_three && byteData == 0xC1) {
      has_header_three = true;
    } else if (has_header_one && has_header_two && has_header_three) {
      serial_in_buffer[serial_in_buffer_position] = byteData;
      serial_in_buffer_position++;
      
      if (serial_in_buffer_position == SERIAL_BUFFER_SIZE) {
        resetBuffer();
        has_gotten_serial_data = true;
        other_theremin_last_val = bufferToInt();
  
        dbg_print("From other theremin: ");
        dbg_print(other_theremin_last_val);      
      }
    } else {
      resetBuffer();
    }
  }
}

void loop()
{
  cnt++;
  f_meter_start();

  int normalizedTune = (int)(((double)tune / largest_value_seen) * NORMALIZED_MAX);
  Serial.write(0xA1);
  Serial.write(0xB5);
  Serial.write(0xC1);  
  writeInt(normalizedTune);
  
  if (has_gotten_serial_data) {
    Serial.write(0xA1);
    Serial.write(0xB5);  
    Serial.write(0xC2);
    writeInt(other_theremin_last_val);
  }
  
  while (f_ready==0) {            // wait for period length end (100ms) by interrupt
    processSerialByteIfAvailable();
  }
  tune = freq_zero - freq_in;
  if (tune < 0) {
    tune = 0;
  }  
  if (tune > largest_value_seen) {
    largest_value_seen = tune;
  }
  
  // startup
  if (cnt==10) {
    freq_zero=freq_in;
    freq_cal=freq_in;
    cal_max=0;
    dbg_print("** START **");
  }

  // autocalibration
  if (cnt % 20 == 0) {   // try autocalibrate after n cycles
    dbg_print("*");
    if (cal_max <= 2) {
      freq_zero=freq_in;
      dbg_print(" calibration");
    }
    freq_cal=freq_in;
    cal_max=0;
    dbg_print("");
  }
  cal = freq_in-freq_cal;
  if ( cal < 0) cal*=-1;  // absolute value
  if (cal > cal_max) cal_max=cal;

  digitalWrite(pinLed,1);  // let LED blink
  dbg_print(cnt);
  dbg_print("  "); 

  sprintf(st1, " %04d",tune);
  dbg_print(st1);
  dbg_print("  "); 
  
  dbg_print((int)normalizedTune);
  dbg_print("  "); 

  dbg_print(freq_in);
  dbg_print("  ");

  dbg_print(freq_zero);
  dbg_print("  ");
  dbg_print(cal_max);
  dbg_print("\n");
  digitalWrite(pinLed,0);

}
//******************************************************************
void f_meter_start() {
  f_ready=0;                      // reset period measure flag
  i_tics=0;                        // reset interrupt counter
  sbi (GTCCR,PSRASY);              // reset presacler counting
  TCNT2=0;                         // timer2=0
  TCNT1=0;                         // Counter1 = 0
  cbi (TIMSK0,TOIE0);              // dissable Timer0 again // millis and delay
  sbi (TIMSK2,OCIE2A);             // enable Timer2 Interrupt
  TCCR1B = TCCR1B | 7;             //  Counter Clock source = pin T1 , start counting now
}

//******************************************************************
// Timer2 Interrupt Service is invoked by hardware Timer2 every 2ms = 500 Hz
//  16Mhz / 256 / 125 / 500 Hz
// here the gatetime generation for freq. measurement takes place: 

ISR(TIMER2_COMPA_vect) {

  if (i_tics==50) {         // multiple 2ms = gate time = 100 ms
                            // end of gate time, measurement ready
    TCCR1B = TCCR1B & ~7;   // Gate Off  / Counter T1 stopped
    cbi (TIMSK2,OCIE2A);    // disable Timer2 Interrupt
    sbi (TIMSK0,TOIE0);     // ensable Timer0 again // millis and delay
    f_ready=1;              // set global flag for end count period

                            // calculate now frequeny value
    freq_in=0x10000 * mlt;  // mukt #ovverflows by 65636
    freq_in += TCNT1;       // add counter1 value
    mlt=0;

  }
  i_tics++;                 // count number of interrupt events
  if (TIFR1 & 1) {          // if Timer/Counter 1 overflow flag
    mlt++;                  // count number of Counter1 overflows
    sbi(TIFR1,TOV1);        // clear Timer/Counter 1 overflow flag
  }

}
