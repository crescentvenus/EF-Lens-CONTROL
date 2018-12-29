/*
   ロータリーエンコーダの参照コード
   https://jumbleat.com/2016/12/17/encoder_1/
*/
/*
   EFレンズの制御関連参考リンク
   ASCOM EF Lens Controller
   http://www.indilib.org/media/kunena/attachments/3728/ascom_efEN.pdf
   EFレンズから信号線の引き出の参照資料
   How to Move Canon EF Lenses Yosuke Bando
   http://web.media.mit.edu/~bandy/invariant/move_lens.pdf
   Canon　EFレンズのArduino制御
   http://otobs.org/hiki/?EOS_model
   Technical aspects of the Canon EF lens mount
   http://www.eflens.com/lens_articles/ef_lens_mount.html
*/
#include <SPI.h>
#include <EEPROM.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define LED_SW   7
#define CS 10   // Emurate CS of SPI for DEBUG
#define F1 9    // Function SW
#define ENC_A  2
#define ENC_B  3
#define LED_Red 14
#define LED_Green 15

volatile byte pos;
volatile int  enc_count;
boolean    sw = false;
boolean  real_mode = false;

int mode = 0;
int mode_counter[2];
int focuserPosition,  apValue, offset, apAddr, fpAddr, fpValue, x, y;
boolean  IsFirstConnect;

void InitLens()
{
  SPI.transfer(0x0A);
  delay(30);
  SPI.transfer(0x00);
  delay(30);
  SPI.transfer(0x0A);
  delay(30);
  SPI.transfer(0x00);
  delay(30);
}

int ENC_COUNT(int incoming) {
  static int enc_old = enc_count;
  int val_change = enc_count - enc_old;

  if (val_change != 0)
  {
    incoming += val_change;
    enc_old   = enc_count;
  }
  return incoming;
}

void ENC_READ() {
  byte cur = (!digitalRead(ENC_B) << 1) + !digitalRead(ENC_A);
  byte old = pos & B00000011;
  byte dir = (pos & B00110000) >> 4;

  if (cur == 3) cur = 2;
  else if (cur == 2) cur = 3;

  if (cur != old)
  {
    if (dir == 0)
    {
      if (cur == 1 || cur == 3) dir = cur;
    } else {
      if (cur == 0)
      {
        if (dir == 1 && old == 3) enc_count++;
        else if (dir == 3 && old == 1) enc_count--;
        dir = 0;
      }
    }
    pos = (dir << 4) + (old << 2) + cur;
  }
}

void setup() {
  digitalWrite(13, LOW); // SPI Clock PIN
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(LED_Red, OUTPUT);
  pinMode(LED_Green, OUTPUT);
  pinMode(LED_SW, INPUT_PULLUP);
  pinMode(CS, OUTPUT);
  pinMode(F1, INPUT_PULLUP);

  attachInterrupt(0, ENC_READ, CHANGE);
  attachInterrupt(1, ENC_READ, CHANGE);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  // Font size
  display.setTextSize(3);
  // Font color
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println("EF-LensFocuser");
  display.display();
  delay(1000);
  mode = 0;   // focus control mode
  apAddr = 0; // 1 byte memory for aperture value
  fpAddr = 1; // 2 byte memory for focus position
  focuserPosition = 5000;
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV128);
  SPI.setDataMode(SPI_MODE3);
  digitalWrite(12, HIGH);
  digitalWrite(LED_Red, HIGH);
  digitalWrite(LED_Green, LOW);
  InitLens();
  SPI.transfer(0x05); // Focus Max
  delay(1000);
  apValue = EEPROM.read(apAddr);
  fpValue =  EEPROM.read(fpAddr) * 256 + EEPROM.read(fpAddr + 1); // focus position
  offset = fpValue - focuserPosition;
  proc_lens(offset);    // Move focus tot last position

  Serial.begin(9600);
  Serial.print("FP:");
  Serial.println(fpValue);
  Serial.print("AP:");
  Serial.println(apValue);
  display.clearDisplay();
  display.setCursor(0, 10);
  display.print("F:");
  display.println(fpValue);
  display.print("A:");
  display.println(apValue);
  display.display();
  delay(1000);
}

void loop() {
  int sw_count;
  int tmp, last_offset;
  short counter_now;
  digitalWrite(CS, HIGH);
  sw_count = 0;
  while (digitalRead(LED_SW) == LOW) {
    sw_count++;
    if (sw_count > 50) {
      if (mode == 1) {    // forcus control mode
        digitalWrite(LED_Red, HIGH);
        digitalWrite(LED_Green, LOW);
      } else {            // aperture control mode
        digitalWrite(LED_Green, HIGH);
        digitalWrite(LED_Red, LOW);
      }
    }
    if (sw_count > 200) {
      digitalWrite(LED_Green, HIGH);
      digitalWrite(LED_Red, HIGH);
    }
    delay(10);
  }
  delay(100);
  if (sw_count > 50 && sw_count < 200) {
    mode == 0 ? mode = 1 : mode = 0;
  }
  if (mode == 1) {
    digitalWrite(LED_Green, HIGH);
    digitalWrite(LED_Red, LOW);
  } else {
    digitalWrite(LED_Red, HIGH);
    digitalWrite(LED_Green, LOW);
  }
  /*
    if (sw_count != 0) {
    Serial.print("mode: ");
    Serial.print(mode);
    Serial.print(", real: ");
    Serial.println(real_mode);
    }
  */
  if (sw_count > 200) {
    real_mode = !real_mode;
    if (real_mode) {
      Serial.print("mode: ");
      Serial.println(mode);
      last_offset = mode_counter[mode];
    } else {
      mode_counter[mode] = last_offset;
    }
  }
  if (sw_count != 0 && (sw_count < 50) ) {
    proc_lens(tmp);
  }

  counter_now = ENC_COUNT(mode_counter[mode]);
  tmp = counter_now - mode_counter[mode];   // encoder counter state
  if (mode_counter[mode] != counter_now)
  {
    tmp > 0 ? 1 : -1;
    if (real_mode) {
      mode_counter[mode] += tmp;
      proc_lens(tmp);
    } else {
      mode_counter[mode] = counter_now;
    }
  }
  if (sw_count != 0 || tmp != 0) disp_update(tmp, last_offset);

}

void proc_lens(int tmp) {
  int ap;
  digitalWrite(CS, LOW);
  if  (mode == 0 ) { // Send command to LENS　フォーカス
    if (real_mode) {
      offset = tmp;
    } else {
      offset = mode_counter[mode] ;
    }
    x = highByte(focuserPosition);
    y = lowByte(focuserPosition);
    EEPROM.write(fpAddr, x);      // write to EEPROM last focus position
    EEPROM.write(fpAddr + 1, y);
    x = highByte(offset);
    y = lowByte(offset);
    Serial.print("FP:");
    Serial.print(offset);
    Serial.print(", ");
    Serial.println(focuserPosition);
    InitLens();
    SPI.transfer(0x44);       delay(30);
    SPI.transfer(x);        delay(30);
    SPI.transfer(y);        delay(30);
    SPI.transfer(0);        delay(100);
    focuserPosition += offset;
  } else {              // 絞り
    apValue = mode_counter[mode] ;
    ap = (apValue - EEPROM.read(apAddr)) * 3;
    Serial.print("APvalue:");
    Serial.print(apValue);
    Serial.print(",SET ap:");
    Serial.println(ap);
    if (apValue != EEPROM.read(apAddr))
    {
      InitLens();
      SPI.transfer(0x07);          delay(10);
      SPI.transfer(0x13);          delay(10);
      SPI.transfer((apValue - EEPROM.read(apAddr)) * 3);
      delay(100);
      SPI.transfer(0x08);          delay(10);
      SPI.transfer(0x00);          delay(10);
      EEPROM.write(apAddr, apValue);
    }
  }
}

void disp_update(int tmp, int last_offset) {
  char sep;
  display.clearDisplay();
  display.setCursor(0, 10);
  if (real_mode) {    // Update when encoder rotated
    sep = '>';
    switch (mode) {
      case 0:
        (tmp > 0) ? sep = '>' : sep = '<';
        display.print("F");
        display.print(sep);
        display.println(  last_offset );
        display.print("P");
        display.print(sep);
        display.println( focuserPosition);
        break;
      case 1:
        display.print("F");
        display.print(sep);
        display.println( focuserPosition );
        display.print("A");
        display.print(sep);
        display.println(mode_counter[mode]);
        break;
    }
  } else {        // Update when switch pushed
    sep = ':';
    switch (mode) {
      case 0:
        display.print("F");
        display.print(sep);
        display.println( mode_counter[mode] );
        display.print("P");
        display.print(sep);
        display.println( focuserPosition);
        break;
      case 1:
        display.print("F");
        display.print(sep);
        display.println( focuserPosition );
        display.print("A");
        display.print(sep);
        display.println(mode_counter[mode]);
        break;
    }
  }
  display.display();
}
