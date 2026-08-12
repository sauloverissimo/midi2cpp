/*
 * promicro-midi1-gabarito: deterministic MIDI 1.0 stress sender.
 *
 * Pure USB MIDI 1.0 class device (no alt 1), so a MIDI 2.0 host bridge is
 * forced through its MIDI 1.0 uplift path. Every stress message carries a
 * sequence number; the PC side proves nothing was lost, reordered or
 * fabricated.
 *
 * Cycle layout (repeats forever, ~3 s idle between cycles):
 *   marker  CC119=1 ch15            cycle start
 *   Phase A 4096 NoteOn/Off pairs   seq: note = seq>>6 (0..63),
 *                                   on-vel = (seq&0x3F)+64, off-vel = seq&0x7F
 *   Phase B 512 numbered CCs        controller = 20+(seq>>7), value = seq&0x7F
 *   Phase C pathological wire cases (raw USB-MIDI event packets):
 *     C1 32-byte numbered SysEx with F8 clock injected midway
 *     C2 stray F7 with no SysEx open
 *     C3 F0 restart: F0 11 12 F0 31 32 33 34 F7
 *     C4 F6 injected inside an open SysEx
 *     C5 sentinel NoteOn 96 vel 96 / off (stream still aligned after C1-C4)
 *     C6 CV status interrupts an open SysEx, no F7 ever (AM lib finding 1)
 *     C7 same with a late F7 (payload-leak variant)
 *   marker  CC119=2 ch15            cycle end
 */
#include <MIDIUSB.h>

static void tx(uint8_t cin, uint8_t b0, uint8_t b1, uint8_t b2) {
  midiEventPacket_t p = {cin, b0, b1, b2};
  MidiUSB.sendMIDI(p);
}

static void noteOn(uint8_t n, uint8_t v)  { tx(0x09, 0x90, n, v); }
static void noteOff(uint8_t n, uint8_t v) { tx(0x08, 0x80, n, v); }
static void cc(uint8_t ch, uint8_t c, uint8_t v) { tx(0x0B, 0xB0 | ch, c, v); }

static void marker(uint8_t v) { cc(15, 119, v); MidiUSB.flush(); delay(5); }

static void phaseA(void) {
  for (uint16_t seq = 0; seq < 4096; seq++) {
    uint8_t note = (seq >> 6) & 0x7F;
    noteOn(note, (seq & 0x3F) + 64);
    noteOff(note, seq & 0x7F);
    if ((seq & 0x0F) == 0x0F) MidiUSB.flush();
    /* pacing: ~4k msg/s, fast enough to stress, slow enough for the
     * PC rawmidi->seq chain not to drop the front of the burst */
    delayMicroseconds(500);
  }
  MidiUSB.flush();
}

static void phaseB(void) {
  for (uint16_t seq = 0; seq < 512; seq++) {
    cc(0, 20 + (seq >> 7), seq & 0x7F);
    if ((seq & 0x0F) == 0x0F) MidiUSB.flush();
    delayMicroseconds(500);
  }
  MidiUSB.flush();
}

static void phaseC(void) {
  uint8_t i;
  /* C1: SysEx F0 + 32 numbered bytes + F7, with an F8 clock injected after
   * the 11th data byte (bytes 0x00..0x0A sent, then the clock). */
  tx(0x04, 0xF0, 0x00, 0x01);
  tx(0x04, 0x02, 0x03, 0x04);
  tx(0x04, 0x05, 0x06, 0x07);
  tx(0x04, 0x08, 0x09, 0x0A);
  tx(0x0F, 0xF8, 0x00, 0x00);          /* clock mid-SysEx */
  for (i = 0x0B; i + 2 < 0x20; i += 3)
    tx(0x04, i, i + 1, i + 2);         /* data 0x0B..0x1F, 7 packets of 3 */
  tx(0x05, 0xF7, 0x00, 0x00);          /* SysEx end, lone F7 */
  MidiUSB.flush(); delay(5);

  /* C2: stray F7, no SysEx open */
  tx(0x05, 0xF7, 0x00, 0x00);
  MidiUSB.flush(); delay(5);

  /* C3: F0 restart */
  tx(0x04, 0xF0, 0x11, 0x12);
  tx(0x04, 0xF0, 0x31, 0x32);
  tx(0x07, 0x33, 0x34, 0xF7);
  MidiUSB.flush(); delay(5);

  /* C4: F6 inside an open SysEx */
  tx(0x04, 0xF0, 0x41, 0x42);
  tx(0x05, 0xF6, 0x00, 0x00);
  MidiUSB.flush(); delay(5);

  /* C5: sentinel proves the stream is still aligned */
  noteOn(96, 96);
  noteOff(96, 0x40);
  MidiUSB.flush(); delay(5);

  /* C6: CV status interrupts an open SysEx (no F7 ever): the SysEx must
   * close at the 0x90 and BOTH notes must pass through. */
  tx(0x04, 0xF0, 0x21, 0x22);
  noteOn(0x3C, 0x7F);
  noteOff(0x3C, 0x40);
  noteOn(0x3E, 0x7F);
  noteOff(0x3E, 0x40);
  MidiUSB.flush(); delay(5);

  /* C7: same, but a late F7 follows; it must NOT swallow the notes nor
   * leak their bytes into the SysEx payload. */
  tx(0x04, 0xF0, 0x51, 0x52);
  noteOn(0x40, 0x7F);
  tx(0x05, 0xF7, 0x00, 0x00);
  noteOff(0x40, 0x40);
  MidiUSB.flush();
}

void setup() { pinMode(LED_BUILTIN, OUTPUT); delay(2000); }

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  marker(1);
  phaseA();
  phaseB();
  phaseC();
  marker(2);
  digitalWrite(LED_BUILTIN, LOW);
  delay(3000);
}
