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
#include <math.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define LED_SW   7
#define ENC_A  2
#define ENC_B  3
#define LED_Red 14
#define LED_Green 15

volatile byte pos;
volatile int  enc_count;
boolean    sw = false;
int mode = 0;
int mode_counter[2];
int focuserPosition, targetPos, apValue, offset, apAddr, x, y;
String targetStr, apStr, gStr;
boolean IsMoving, IsFirstConnect;
char inStr[6];

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
  attachInterrupt(0, ENC_READ, CHANGE);
  attachInterrupt(1, ENC_READ, CHANGE);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  // テキストサイズを設定
  display.setTextSize(3);
  // テキスト色を設定
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println("EF-LensFocuser");
  display.display();
  delay(1000);
  mode = 0;
  apAddr = 0;
  focuserPosition = 5000;
  SPI.begin();
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV128);
  SPI.setDataMode(SPI_MODE3);
  digitalWrite(12, HIGH);
  InitLens();
  digitalWrite(LED_Red, HIGH);
  digitalWrite(LED_Green, LOW);

  apValue=EEPROM.read(apAddr);
  Serial.begin(9600);
  Serial.println(apValue);
  // nothing to do inside the setup
}

void loop() {
  int sw_count;
  short counter_now;
  sw_count = 0;
  while (digitalRead(LED_SW) == LOW) {
    sw_count++;
    if (sw_count > 50) {
      if (mode == 1) {    // 新モードはフォーカス制御
        digitalWrite(LED_Red, HIGH);
        digitalWrite(LED_Green, LOW);
      } else {            // 新モードは絞り制御
        digitalWrite(LED_Green, HIGH);
        digitalWrite(LED_Red, LOW);
      }
    }
    delay(10);
  }
  delay(100);
  if (sw_count > 50) {
    if (mode == 0) {
      mode = 1;   // 絞りモード
      digitalWrite(LED_Green, HIGH);
      digitalWrite(LED_Red, LOW);
    } else {
      mode = 0;   // フォーカスモード
      digitalWrite(LED_Red, HIGH);
      digitalWrite(LED_Green, LOW);
    }
  }
  if (sw_count != 0 && (sw_count < 50) ) {
    if  (mode == 0 ) { // Send command to LENS　フォーカス
      targetPos =  mode_counter[mode] ;
      offset = mode_counter[mode] ;
      x = highByte(offset);
      y = lowByte(offset);
      InitLens();
      IsMoving = true;
      Serial.print(offset); Serial.print(",");
      Serial.print(x); Serial.print(",");
      Serial.println(y);
      SPI.transfer(68);       delay(30);
      SPI.transfer(x);        delay(30);
      SPI.transfer(y);        delay(30);
      SPI.transfer(0);        delay(100);
      IsMoving = false;
      focuserPosition += offset;    // Update focuser position
    } else {              // 絞り
      apValue = mode_counter[mode] % 20;
      if (apValue != EEPROM.read(apAddr))
      {
        InitLens();
        Serial.println("AP");
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

  counter_now = ENC_COUNT(mode_counter[mode]);
  if (mode_counter[mode] != counter_now)
  {
    mode_counter[mode] = counter_now;
  }
  disp_update();
}

void disp_update() {
  display.clearDisplay();
  display.setCursor(0, 10);
    switch (mode) {
    case 0:
      display.print("F:");
      display.println( mode_counter[mode] );
      display.print("P:");
      display.println( focuserPosition);
      break;
    case 1:
      display.print("F:");
      display.println( focuserPosition );
      display.print("A:");
      display.println(mode_counter[mode]);
      break;
  }
  display.display();
}
