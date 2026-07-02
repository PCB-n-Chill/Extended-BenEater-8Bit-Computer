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

---

## Credits

- Based on Ben Eater's 8-bit breadboard computer and his original
  [`eeprom-programmer`](https://github.com/beneater/eeprom-programmer) sketches.
- Flags-register concept and video: <https://youtu.be/Zg1NdPKoosU>

This extended version adds the Sign and Overflow flags and the `JS` / `JO` conditional jumps, and
targets the AT28C256 EEPROM with an Arduino Mega.
