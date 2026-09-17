# Extended BenEater 8Bit Computer

**Extended** because this project takes Ben Eater's classic
[8-bit breadboard computer](https://eater.net/8bit) and extends it beyond the original design —
adding a **Sign** and **Overflow** flag, two new conditional-jump instructions (**JS** / **JO**),
a larger **AT28C256** EEPROM, and (coming soon) custom **KiCad** hardware. This repository currently
holds the **microcode programmer** — the Arduino sketch that burns the CPU's control logic into the
EEPROMs — with the rest of the extended design to follow.

An Arduino sketch that burns the **microcode** for an 8-bit breadboard computer (the
[Ben Eater](https://eater.net/8bit) design) into a pair of parallel EEPROMs.

This is an **extended** version of Ben Eater's
[`microcode-eeprom-with-flags`](https://github.com/beneater/eeprom-programmer/blob/master/microcode-eeprom-with-flags/microcode-eeprom-with-flags.ino)
programmer. Where the original supports two condition flags (**Carry** and **Zero**) and two
conditional jumps, this version adds a **Sign flag (SF)** and an **Overflow flag (OF)** and two new
conditional-jump instructions to go with them — **JS (Jump if Sign)** and **JO (Jump if Overflow)**.

Because the flags register grows from 2 bits to 4 bits, the microcode table quadruples in size. That
has two practical consequences that drive the rest of this document:

1. The generated table no longer fits in an Arduino Uno/Nano's SRAM — you need an **Arduino Mega**.
2. The address space grows past the original AT28C16's capacity — you need a larger EEPROM, the
   **AT28C256**.

---

## The two new jump instructions

The original CPU can branch on only two conditions:

- **JC** — Jump if **Carry** flag is set
- **JZ** — Jump if **Zero** flag is set

This version adds signed-arithmetic branching by introducing two more flags and two more jumps:

- **JS** — Jump if **Sign** flag is set (result is negative, i.e. bit 7 = 1)
- **JO** — Jump if **Overflow** flag is set (signed overflow occurred)

Together, `JC`/`JZ`/`JS`/`JO` let programs make **signed** comparisons and branches, not just
unsigned ones.

### How a conditional jump is implemented

Every instruction begins with the same two-step **fetch** microsequence:

```
step 0:  MI | CO        ; put program counter on the bus -> memory address register
step 1:  RO | II | CE    ; read RAM -> instruction register, and increment PC
```

For an **unconditional** `JMP`, step 2 is `IO | J` (put the instruction's operand on the bus and
load it into the program counter).

For a **conditional** jump, step 2 is `0` (a no-op) **by default**, and is *patched* to `IO | J`
**only in the flag-state copies of the table where the condition is true**. In other words, the
"jump happens" behaviour is baked into the microcode itself, selected by the current flags at
runtime via the EEPROM address lines. `initUCode()` does this patching for all 16 flag combinations:

```c
// Example: in every flag state where the Carry flag is set,
// enable the jump for the JC instruction.
ucode[F_S0O0_Z0C1][JC][2] = IO | J;
...
```

---

## How this differs from the original Ben Eater code

| | Ben Eater original | **This version** |
|---|---|---|
| Condition flags | 2 — Carry (CF), Zero (ZF) | 4 — Sign (SF), Overflow (OF), Zero (ZF), Carry (CF) |
| Conditional jumps | `JC`, `JZ` | `JC`, `JZ`, **`JS`**, **`JO`** |
| Flag combinations | 4 | 16 |
| Microcode array | `uint16_t ucode[4][16][8]` (~1 KB) | `uint16_t ucode[16][16][8]` (~4 KB) |
| Address bus width | 10 bits → 1024 addresses | 12 bits → 4096 addresses |
| Target EEPROM | AT28C16 (2K × 8) | **AT28C256 (32K × 8)** |
| Programming board | Arduino Uno / Nano | **Arduino Mega (2560)** |

Everything else — the shift-register address driver, the read/write routines, the serial hex dump,
and the base instruction set (NOP, LDA, ADD, SUB, STA, LDI, JMP, OUT, HLT) — is functionally the
same as the original.

---

## Why the AT28C256 EEPROM?

The microcode address is built from four fields. Widening the flags field from 2 bits to 4 bits
pushes the total address width from **10 bits (1024 bytes)** to **12 bits (4096 bytes)**.

The original **AT28C16** has only **11 address lines (A0–A10) = 2048 bytes**, which cannot address
4096 locations. The **AT28C256** has **15 address lines (A0–A14) = 32768 bytes**, which is far more
than enough. Only the low 12 address lines are used here; the upper address lines are tied to a
fixed level, so the microcode simply lives in one 4 KB window of the larger chip.

As in the original design, **two** EEPROMs are programmed in parallel — one holds the **low byte** and
one holds the **high byte** of each 16-bit control word (see the memory layout below).

---

## Why an Arduino Mega is required

The sketch builds the entire microcode table **in RAM** before writing it out:

```c
uint16_t ucode[16][16][8];   // 16 flags × 16 instructions × 8 steps × 2 bytes = 4096 bytes
uint16_t UCODE_TEMPLATE[16][8]; // + 256 bytes
```

That's roughly **4.3 KB of static data**, before the stack and everything else. An Arduino
Uno/Nano (ATmega328P) has only **2 KB of SRAM**, so the program will not fit and will fail or behave
erratically. An **Arduino Mega 2560** has **8 KB of SRAM**, which comfortably holds the table.

*(The original 2-flag version's `ucode[4][16][8]` is only ~1 KB and does fit an Uno — which is why
the original could use one.)*

---

## Memory & instruction layout inside the EEPROM

### Address decomposition

Each EEPROM address (12 bits, `A0`–`A11`) is decoded into four fields:

```
 A11 A10  A9  A8 | A7 | A6 A5 A4 A3 | A2 A1 A0
  SF  OF  ZF  CF | bs |  instruction | step
 \_____________/ |    | \__________/ \______/
     flags (4)   | (1)|    (4 bits)   (3 bits)
                byte_sel
```

```c
int flags       = (address & 0b111100000000) >> 8;  // A8–A11 : SF OF ZF CF
int byte_sel    = (address & 0b000010000000) >> 7;  // A7     : selects high vs low byte
int instruction = (address & 0b000001111000) >> 3;  // A3–A6 : opcode (0–15)
int step        = (address & 0b000000000111);       // A0–A2 : microstep (0–7)
```

- **flags (4 bits)** — the live contents of the flags register (`SF OF ZF CF`), fed from the CPU's
  flags outputs into the EEPROM address lines. This is what makes the *same* opcode behave
  differently depending on condition flags.
- **byte_sel (1 bit)** — which of the two parallel EEPROMs is being addressed. `byte_sel = 1`
  → the **low** 8 bits of the control word; `byte_sel = 0` → the **high** 8 bits.
- **instruction (4 bits)** — the 4-bit opcode.
- **step (3 bits)** — the microstep counter (0–7) within the instruction.

The two EEPROMs together output a **16-bit control word** each cycle:

```c
if (byte_sel) writeEEPROM(address, ucode[flags][instruction][step]);        // low byte  -> EEPROM #1
else          writeEEPROM(address, ucode[flags][instruction][step] >> 8);   // high byte -> EEPROM #2
```

### Control word bits

Each microstep is a 16-bit word; each bit enables one control line on the CPU:

| Bit | Signal | Meaning |
|-----|--------|---------|
| `HLT` | Halt | Halt the clock |
| `MI`  | Memory Address Register In | Latch bus → MAR |
| `RI`  | RAM In | Write bus → RAM |
| `RO`  | RAM Out | RAM → bus |
| `IO`  | Instruction Register Out | IR operand → bus |
| `II`  | Instruction Register In | Bus → IR |
| `AI`  | A Register In | Bus → A |
| `AO`  | A Register Out | A → bus |
| `EO`  | Sum Out | ALU result → bus |
| `SU`  | Subtract | ALU subtract mode |
| `BI`  | B Register In | Bus → B |
| `OI`  | Output Register In | Bus → output display |
| `CE`  | Counter Enable | Increment program counter |
| `CO`  | Counter Out | PC → bus |
| `J`   | Jump | Bus → program counter |
| `FI`  | Flags In | Latch ALU flags → flags register |

### Instruction set

| Opcode | Mnemonic | Description |
|--------|----------|-------------|
| `0000` | NOP | No operation |
| `0001` | LDA | Load A from memory |
| `0010` | ADD | Add memory to A (updates flags) |
| `0011` | SUB | Subtract memory from A (updates flags) |
| `0100` | STA | Store A to memory |
| `0101` | LDI | Load immediate into A |
| `0110` | JMP | Unconditional jump |
| `0111` | JC  | Jump if Carry |
| `1000` | JZ  | Jump if Zero |
| `1001` | **JS** | **Jump if Sign** *(new)* |
| `1010` | **JO** | **Jump if Overflow** *(new)* |
| `1011`–`1101` | — | Unused |
| `1110` | OUT | Output A to display |
| `1111` | HLT | Halt |

---

## Hardware / wiring

Pin assignments (Arduino Mega numbering), matching the `#define`s at the top of the sketch:

| Arduino pin | Purpose |
|-------------|---------|
| `2` | `SHIFT_DATA` — serial data into the address shift registers |
| `3` | `SHIFT_CLK` — shift-register clock |
| `4` | `SHIFT_LATCH` — shift-register output latch |
| `5`–`12` | `EEPROM_D0`–`EEPROM_D7` — EEPROM data bus (bidirectional) |
| `13` | `WRITE_EN` — EEPROM active-low write enable |

Two daisy-chained **74HC595** shift registers drive the EEPROM's 15 address lines plus the
active-low **Output Enable** (the top bit of the high address byte in `setAddress()`).

---

## Building & uploading

You need the microcode table to fit in SRAM, so **compile for the Arduino Mega 2560.**

Using [`arduino-cli`](https://arduino.github.io/arduino-cli/):

```bash
# Verify it compiles for the Mega
arduino-cli compile --fqbn arduino:avr:mega microcode-eeprom-with-flags.ino

# Upload to the connected Mega (replace COM3 with your port)
arduino-cli compile --fqbn arduino:avr:mega --upload -p COM3 microcode-eeprom-with-flags.ino

# Watch the programming progress + hex dump
arduino-cli monitor -p COM3 -c baudrate=57600
```

Or open `microcode-eeprom-with-flags.ino` in the **Arduino IDE**, select
**Tools → Board → Arduino Mega 2560**, and use Verify / Upload / Serial Monitor (57600 baud).

### What you'll see

On reset the sketch:

1. Builds the microcode table in RAM (`initUCode()`).
2. Writes all 4096 addresses, printing a `.` every 64 bytes.
3. Reads the EEPROM back and prints a hex dump so you can verify the contents.

Program **each EEPROM one at a time**, swapping the chip between runs — one becomes the high-byte
chip and one the low-byte chip, exactly as in Ben Eater's original workflow.

## Serial Output: 
```c
Programming EEPROM................................................................ done
Reading EEPROM
000:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
010:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
020:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
030:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
040:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
050:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
060:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
070:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
080:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
090:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
0a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
0b0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
0c0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
0d0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
0e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
0f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
100:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
110:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
120:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
130:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
140:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
150:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
160:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
170:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
180:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
190:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
1a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
1b0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
1c0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
1d0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
1e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
1f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
200:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
210:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
220:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
230:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
240:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
250:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
260:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
270:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
280:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
290:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
2a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
2b0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
2c0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
2d0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
2e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
2f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
300:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
310:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
320:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
330:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
340:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
350:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
360:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
370:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
380:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
390:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
3a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
3b0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
3c0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
3d0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
3e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
3f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
400:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
410:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
420:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
430:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
440:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
450:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
460:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
470:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
480:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
490:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
4a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
4b0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
4c0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
4d0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
4e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
4f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
500:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
510:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
520:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
530:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
540:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
550:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
560:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
570:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
580:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
590:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
5a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
5b0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
5c0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
5d0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
5e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
5f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
600:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
610:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
620:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
630:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
640:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
650:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
660:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
670:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
680:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
690:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
6a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
6b0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
6c0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
6d0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
6e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
6f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
700:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
710:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
720:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
730:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
740:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
750:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
760:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
770:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
780:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
790:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
7a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
7b0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
7c0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
7d0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
7e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
7f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
800:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
810:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
820:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
830:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
840:  40 14 00 00 00 00 00 00   40 14 08 00 00 00 00 00
850:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
860:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
870:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
880:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
890:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
8a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
8b0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
8c0:  04 08 00 00 00 00 00 00   04 08 02 00 00 00 00 00
8d0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
8e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
8f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
900:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
910:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
920:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
930:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
940:  40 14 00 00 00 00 00 00   40 14 08 00 00 00 00 00
950:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
960:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
970:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
980:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
990:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
9a0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
9b0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
9c0:  04 08 00 00 00 00 00 00   04 08 02 00 00 00 00 00
9d0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
9e0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
9f0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
a00:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
a10:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
a20:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
a30:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
a40:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
a50:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
a60:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
a70:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
a80:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
a90:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
aa0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
ab0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
ac0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
ad0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
ae0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
af0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
b00:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
b10:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
b20:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
b30:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
b40:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
b50:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
b60:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
b70:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
b80:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
b90:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
ba0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
bb0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
bc0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
bd0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
be0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
bf0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
c00:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
c10:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
c20:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
c30:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
c40:  40 14 00 00 00 00 00 00   40 14 08 00 00 00 00 00
c50:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
c60:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
c70:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
c80:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
c90:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
ca0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
cb0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
cc0:  04 08 00 00 00 00 00 00   04 08 02 00 00 00 00 00
cd0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
ce0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
cf0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
d00:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
d10:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
d20:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
d30:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
d40:  40 14 00 00 00 00 00 00   40 14 08 00 00 00 00 00
d50:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
d60:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
d70:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
d80:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
d90:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
da0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
db0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
dc0:  04 08 00 00 00 00 00 00   04 08 02 00 00 00 00 00
dd0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
de0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
df0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
e00:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
e10:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
e20:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
e30:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
e40:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
e50:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
e60:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
e70:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
e80:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
e90:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
ea0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
eb0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
ec0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
ed0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
ee0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
ef0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00
f00:  40 14 00 00 00 00 00 00   40 14 48 12 00 00 00 00
f10:  40 14 48 10 02 00 00 00   40 14 48 10 02 00 00 00
f20:  40 14 48 21 00 00 00 00   40 14 0a 00 00 00 00 00
f30:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
f40:  40 14 08 00 00 00 00 00   40 14 08 00 00 00 00 00
f50:  40 14 08 00 00 00 00 00   40 14 00 00 00 00 00 00
f60:  40 14 00 00 00 00 00 00   40 14 00 00 00 00 00 00
f70:  40 14 01 00 00 00 00 00   40 14 80 00 00 00 00 00
f80:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
f90:  04 08 00 20 81 00 00 00   04 08 00 20 c1 00 00 00
fa0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
fb0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
fc0:  04 08 02 00 00 00 00 00   04 08 02 00 00 00 00 00
fd0:  04 08 02 00 00 00 00 00   04 08 00 00 00 00 00 00
fe0:  04 08 00 00 00 00 00 00   04 08 00 00 00 00 00 00
ff0:  04 08 10 00 00 00 00 00   04 08 00 00 00 00 00 00

```
---

## Credits

- Based on Ben Eater's 8-bit breadboard computer and his original
  [`eeprom-programmer`](https://github.com/beneater/eeprom-programmer) sketches.
- Flags-register concept and video: <https://youtu.be/Zg1NdPKoosU>

This extended version adds the Sign and Overflow flags and the `JS` / `JO` conditional jumps, and
targets the AT28C256 EEPROM with an Arduino Mega.
