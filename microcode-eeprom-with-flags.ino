/**
 * This sketch programs the microcode EEPROMs for the 8-bit breadboard computer
 * It includes support for a flags register with carry and zero flags
 * See this video for more: https://youtu.be/Zg1NdPKoosU
 */
#define SHIFT_DATA 2
#define SHIFT_CLK 3
#define SHIFT_LATCH 4
#define EEPROM_D0 5
#define EEPROM_D7 12
#define WRITE_EN 13

#define EEPROM_SIZE 4096

#define HLT 0b1000000000000000  // Halt clock
#define MI  0b0100000000000000  // Memory address register in
#define RI  0b0010000000000000  // RAM data in
#define RO  0b0001000000000000  // RAM data out
#define IO  0b0000100000000000  // Instruction register out
#define II  0b0000010000000000  // Instruction register in
#define AI  0b0000001000000000  // A register in
#define AO  0b0000000100000000  // A register out


#define EO  0b0000000010000000  // ALU out
#define SU  0b0000000001000000  // ALU subtract
#define BI  0b0000000000100000  // B register in
#define OI  0b0000000000010000  // Output register in
#define CE  0b0000000000001000  // Program counter enable
#define CO  0b0000000000000100  // Program counter out
#define J   0b0000000000000010  // Jump (program counter in)
#define FI  0b0000000000000001  // Flags in


//SF = 0, OF = 0
#define F_S0O0_Z0C0 0
#define F_S0O0_Z0C1 1
#define F_S0O0_Z1C0 2
#define F_S0O0_Z1C1 3

// No Sign
#define F_S0O1_Z0C0 4
#define F_S0O1_Z0C1 5
#define F_S0O1_Z1C0 6
#define F_S0O1_Z1C1 7

//No Overflow
#define F_S1O0_Z0C0 8
#define F_S1O0_Z0C1 9
#define F_S1O0_Z1C0 10
#define F_S1O0_Z1C1 11

//All
#define F_S1O1_Z0C0 12
#define F_S1O1_Z0C1 13
#define F_S1O1_Z1C0 14
#define F_S1O1_Z1C1 15


#define JC  0b0111
#define JZ  0b1000
#define JS  0b1001
#define JO  0b1010

uint16_t UCODE_TEMPLATE[16][8] = {
  { MI|CO,  RO|II|CE,  0,      0,      0,           0, 0, 0 },   // 0000 - NOP
  { MI|CO,  RO|II|CE,  IO|MI,  RO|AI,  0,           0, 0, 0 },   // 0001 - LDA
  { MI|CO,  RO|II|CE,  IO|MI,  RO|BI,  EO|AI|FI,    0, 0, 0 },   // 0010 - ADD
  { MI|CO,  RO|II|CE,  IO|MI,  RO|BI,  EO|AI|SU|FI, 0, 0, 0 },   // 0011 - SUB
  { MI|CO,  RO|II|CE,  IO|MI,  AO|RI,  0,           0, 0, 0 },   // 0100 - STA
  { MI|CO,  RO|II|CE,  IO|AI,  0,      0,           0, 0, 0 },   // 0101 - LDI
  { MI|CO,  RO|II|CE,  IO|J,   0,      0,           0, 0, 0 },   // 0110 - JMP
  { MI|CO,  RO|II|CE,  0,      0,      0,           0, 0, 0 },   // 0111 - JC
  { MI|CO,  RO|II|CE,  0,      0,      0,           0, 0, 0 },   // 1000 - JZ
  { MI|CO,  RO|II|CE,  0,      0,      0,           0, 0, 0 },   // 1001 - JS
  { MI|CO,  RO|II|CE,  0,      0,      0,           0, 0, 0 },   // 1010 - JO
  { MI|CO,  RO|II|CE,  0,      0,      0,           0, 0, 0 },   // 1011
  { MI|CO,  RO|II|CE,  0,      0,      0,           0, 0, 0 },   // 1100
  { MI|CO,  RO|II|CE,  0,      0,      0,           0, 0, 0 },   // 1101
  { MI|CO,  RO|II|CE,  AO|OI,  0,      0,           0, 0, 0 },   // 1110 - OUT
  { MI|CO,  RO|II|CE,  HLT,    0,      0,           0, 0, 0 },   // 1111 - HLT
};

uint16_t ucode[16][16][8];

void initUCode() {
  //SF = 0, OF = 0 , ZF = 0, CF = 0
  memcpy(ucode[F_S0O0_Z0C0], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));

  //SF = 0, OF = 0 , ZF = 0, CF = 1
  memcpy(ucode[F_S0O0_Z0C1], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S0O0_Z0C1][JC][2] = IO|J;

  //SF = 0, OF = 0 , ZF = 1, CF = 0
  memcpy(ucode[F_S0O0_Z1C0], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S0O0_Z1C0][JZ][2] = IO|J;

  //SF = 0, OF = 0 , ZF = 1, CF = 1
  memcpy(ucode[F_S0O0_Z1C1], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S0O0_Z1C1][JC][2] = IO|J;
  ucode[F_S0O0_Z1C1][JZ][2] = IO|J;

  /////////////////////////////////////////////////////////////////////

  //SF = 0, OF = 1 , ZF = 0, CF = 0
  memcpy(ucode[F_S0O1_Z0C0], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S0O1_Z0C0][JO][2] = IO|J;

  //SF = 0, OF = 1 , ZF = 0, CF = 1
  memcpy(ucode[F_S0O1_Z0C1], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S0O1_Z0C1][JC][2] = IO|J;
  ucode[F_S0O1_Z0C1][JO][2] = IO|J;

  //SF = 0, OF = 1 , ZF = 1, CF = 0
  memcpy(ucode[F_S0O1_Z1C0], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S0O1_Z1C0][JZ][2] = IO|J;
  ucode[F_S0O1_Z1C0][JO][2] = IO|J;

  //SF = 0, OF = 1 , ZF = 1, CF = 1
  memcpy(ucode[F_S0O1_Z1C1], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S0O1_Z1C1][JC][2] = IO|J;
  ucode[F_S0O1_Z1C1][JZ][2] = IO|J;
  ucode[F_S0O1_Z1C1][JO][2] = IO|J;

///////////////////////////////////////////////////////////////////////

  //SF = 1, OF = 0 , ZF = 0, CF = 0
  memcpy(ucode[F_S1O0_Z0C0], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S1O0_Z0C0][JS][2] = IO|J;

  //SF = 1, OF = 0 , ZF = 0, CF = 1
  memcpy(ucode[F_S1O0_Z0C1], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S1O0_Z0C1][JC][2] = IO|J;
  ucode[F_S1O0_Z0C1][JS][2] = IO|J;

  //SF = 1, OF = 0 , ZF = 1, CF = 0
  memcpy(ucode[F_S1O0_Z1C0], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S1O0_Z1C0][JZ][2] = IO|J;
  ucode[F_S1O0_Z1C0][JS][2] = IO|J;

  //SF = 1, OF = 0 , ZF = 1, CF = 1
  memcpy(ucode[F_S1O0_Z1C1], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S1O0_Z1C1][JC][2] = IO|J;
  ucode[F_S1O0_Z1C1][JZ][2] = IO|J;
  ucode[F_S1O0_Z1C1][JS][2] = IO|J;

///////////////////////////////////////////////////////////////////////

  //SF = 1, OF = 1 , ZF = 0, CF = 0
  memcpy(ucode[F_S1O1_Z0C0], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S1O1_Z0C0][JO][2] = IO|J;
  ucode[F_S1O1_Z0C0][JS][2] = IO|J;

  //SF = 1, OF = 1 , ZF = 0, CF = 1
  memcpy(ucode[F_S1O1_Z0C1], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S1O1_Z0C1][JC][2] = IO|J;
  ucode[F_S1O1_Z0C1][JO][2] = IO|J;
  ucode[F_S1O1_Z0C1][JS][2] = IO|J;

  //SF = 1, OF = 1 , ZF = 1, CF = 0
  memcpy(ucode[F_S1O1_Z1C0], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S1O1_Z1C0][JZ][2] = IO|J;
  ucode[F_S1O1_Z1C0][JO][2] = IO|J;
  ucode[F_S1O1_Z1C0][JS][2] = IO|J;

  //SF = 1, OF = 1 , ZF = 1, CF = 1
  memcpy(ucode[F_S1O1_Z1C1], UCODE_TEMPLATE, sizeof(UCODE_TEMPLATE));
  ucode[F_S1O1_Z1C1][JC][2] = IO|J;
  ucode[F_S1O1_Z1C1][JZ][2] = IO|J;
  ucode[F_S1O1_Z1C1][JO][2] = IO|J;
  ucode[F_S1O1_Z1C1][JS][2] = IO|J;
}

/*
 * Output the address bits and outputEnable signal using shift registers.
 */
void setAddress(int address, bool outputEnable) {
  shiftOut(SHIFT_DATA, SHIFT_CLK, MSBFIRST, (address >> 8) | (outputEnable ? 0x00 : 0x80));
  shiftOut(SHIFT_DATA, SHIFT_CLK, MSBFIRST, address);

  digitalWrite(SHIFT_LATCH, LOW);
  digitalWrite(SHIFT_LATCH, HIGH);
  digitalWrite(SHIFT_LATCH, LOW);
}


/*
 * Read a byte from the EEPROM at the specified address.
 */
byte readEEPROM(int address) {
  for (int pin = EEPROM_D0; pin <= EEPROM_D7; pin += 1) {
    pinMode(pin, INPUT);
  }
  setAddress(address, /*outputEnable*/ true);

  byte data = 0;
  for (int pin = EEPROM_D7; pin >= EEPROM_D0; pin -= 1) {
    data = (data << 1) + digitalRead(pin);
  }
  return data;
}


/*
 * Write a byte to the EEPROM at the specified address.
 */
void writeEEPROM(int address, byte data) {
  setAddress(address, /*outputEnable*/ false);
  for (int pin = EEPROM_D0; pin <= EEPROM_D7; pin += 1) {
    pinMode(pin, OUTPUT);
  }

  for (int pin = EEPROM_D0; pin <= EEPROM_D7; pin += 1) {
    digitalWrite(pin, data & 1);
    data = data >> 1;
  }
  digitalWrite(WRITE_EN, LOW);
  delayMicroseconds(1);
  digitalWrite(WRITE_EN, HIGH);
  delay(10);
}


/*
 * Read the contents of the EEPROM and print them to the serial monitor.
 */
void printContents(int start, int length) {
  for (int base = start; base < length; base += 16) {
    byte data[16];
    for (int offset = 0; offset <= 15; offset += 1) {
      data[offset] = readEEPROM(base + offset);
    }

    char buf[80];
    sprintf(buf, "%03x:  %02x %02x %02x %02x %02x %02x %02x %02x   %02x %02x %02x %02x %02x %02x %02x %02x",
            base, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
            data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);

    Serial.println(buf);
  }
}


void setup() {
  // put your setup code here, to run once:
  initUCode();

  pinMode(SHIFT_DATA, OUTPUT);
  pinMode(SHIFT_CLK, OUTPUT);
  pinMode(SHIFT_LATCH, OUTPUT);
  digitalWrite(WRITE_EN, HIGH);
  pinMode(WRITE_EN, OUTPUT);
  Serial.begin(57600);

  // Program data bytes
  Serial.print("Programming EEPROM");

  // Program the 8 high-order bits of microcode into the first 128 bytes of EEPROM
  for (int address = 0; address < EEPROM_SIZE; address += 1) {
    int flags       = (address & 0b111100000000) >> 8;
    int byte_sel    = (address & 0b000010000000) >> 7;
    int instruction = (address & 0b000001111000) >> 3;
    int step        = (address & 0b000000000111);

    if (byte_sel) {

      //eeprom #1
      writeEEPROM(address, ucode[flags][instruction][step]);
    } else {

      //eeprom #2
      writeEEPROM(address, ucode[flags][instruction][step] >> 8);
    }

    if (address % 64 == 0) {
      Serial.print(".");
    }
  }

  Serial.println(" done");


  // Read and print out the contents of the EERPROM
  Serial.println("Reading EEPROM");
  printContents(0, EEPROM_SIZE);
}


void loop() {
  // put your main code here, to run repeatedly:

}

