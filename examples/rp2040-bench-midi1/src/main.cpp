// rp2040-bench-midi1, MIDI 1.0 bytestream to UMP conformance bench.
//
// Feeds a battery of malformed MIDI 1.0 byte vectors (drawn from real-world
// failure reports: truncated SysEx, driver padding, orphan data bytes,
// interleaved Real-Time) into the midi2 core converter and checks every
// emitted UMP word against the expected sequence derived from M2-104-UM.
// Results print on the USB serial console; no MIDI wiring is involved, the
// vectors live in flash.
//
// Build with -DWITH_AM_MIDI2=ON to also run each vector through
// AM_MIDI2.0Lib's bytestreamToUMP (fetched at configure time) and print the
// two conversions side by side. Word streams can differ legitimately in
// SysEx packet partitioning; the printout leaves the comparison to the
// reader, the self-check verdict applies to the midi2 output only.

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"

extern "C" {
#include "midi2.h"
}

#ifdef WITH_AM_MIDI2
#include <new>
#include "bytestreamToUMP.h"
#endif

struct Vector {
    const char*     name;
    const uint8_t*  bytes;
    size_t          nbytes;
    const uint32_t* expect;
    size_t          nexpect;
};

#define VEC(id, ...) static const uint8_t id[] = {__VA_ARGS__}
#define EXP(id, ...) static const uint32_t id[] = {__VA_ARGS__}

// Leading data bytes and driver padding (AM_MIDI2.0Lib issue #39 family)
VEC(v01, 0x00);
static const uint32_t e01[] = {}; // no output
VEC(v02, 0xF8, 0,0,0,0,0,0,0,0,0,0,0);
EXP(e02, 0x10F80000);
VEC(v03, 0x3C, 0x40);
static const uint32_t e03[] = {};
VEC(v04, 0x3C, 0x40, 0x90, 0x3C, 0x7F);
EXP(e04, 0x20903C7F);

// Undefined System Common passes alone, next message intact (issue #16)
VEC(v05, 0xF4, 0x90, 0x3C, 0x7F);
EXP(e05, 0x10F40000, 0x20903C7F);

// Real-Time interleaved inside a SysEx keeps its wire position (issue #24)
VEC(v06, 0xF0, 0x0A,0x0B,0x0C,0x0D,0x0F, 0xF8, 0x2A,0x2B,0x2C,0x2D,0x2E,0x2F, 0xF7);
EXP(e06, 0x30150A0B, 0x0C0D0F00, 0x10F80000,
         0x30262A2B, 0x2C2D2E2F, 0x30300000, 0x00000000);

// Duplicate F7 is absorbed (issue #36)
VEC(v07, 0xF0, 0x21,0x22,0x23,0x24,0x25, 0xF7, 0xF7);
EXP(e07, 0x30052122, 0x23242500);

// A second F0 closes the previous message, no payload merge
VEC(v08, 0xF0, 0x11, 0x12, 0xF0, 0x31, 0x32, 0x33, 0x34, 0xF7);
EXP(e08, 0x30021112, 0x00000000, 0x30043132, 0x33340000);

// A Channel Voice status terminates an open SysEx; notes must survive
VEC(v09, 0xF0, 0x11, 0x22, 0x90, 0x3C, 0x7F, 0x90, 0x3E, 0x7F);
EXP(e09, 0x30021122, 0x00000000, 0x20903C7F, 0x20903E7F);
VEC(v10, 0xF0, 1,2,3,4,5,6,7,8, 0x90, 0x3C, 0x7F);
EXP(e10, 0x30160102, 0x03040506, 0x30320708, 0x00000000, 0x20903C7F);

// Tune Request terminates the SysEx and follows the closing packet
VEC(v11, 0xF0, 0x11, 0xF6);
EXP(e11, 0x30011100, 0x00000000, 0x10F60000);

// Running Status baseline and System Common leaving no Running Status
VEC(v12, 0x90, 0x3C, 0x7F, 0x3E, 0x00, 0x80, 0x3C, 0x40);
EXP(e12, 0x20903C7F, 0x20903E00, 0x20803C40);
VEC(v13, 0xF2, 0x40, 0x20, 0x41, 0x21);
EXP(e13, 0x10F24020);

static const Vector kVectors[] = {
    {"leading data byte alone",            v01, sizeof v01, e01, 0},
    {"F8 + 11 padding bytes",              v02, sizeof v02, e02, 1},
    {"two leading data bytes",             v03, sizeof v03, e03, 0},
    {"orphan bytes then real Note On",     v04, sizeof v04, e04, 1},
    {"undefined System Common F4",         v05, sizeof v05, e05, 2},
    {"F8 inside SysEx (wire order)",       v06, sizeof v06, e06, 7},
    {"duplicate F7",                       v07, sizeof v07, e07, 2},
    {"F0 restart with data buffered",      v08, sizeof v08, e08, 4},
    {"CV status ends short SysEx",         v09, sizeof v09, e09, 4},
    {"CV status ends streamed SysEx",      v10, sizeof v10, e10, 5},
    {"F6 inside SysEx",                    v11, sizeof v11, e11, 3},
    {"running status baseline",            v12, sizeof v12, e12, 3},
    {"F2 leaves no Running Status",        v13, sizeof v13, e13, 1},
};

#define MAXW 32

static size_t run_midi2(const uint8_t* bytes, size_t n, uint32_t* out, size_t cap) {
    midi2_conv_state s;
    memset(&s, 0xCD, sizeof s);   // poisoned storage: init must not care
    midi2_conv_init(&s, 0);
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (midi2_conv_feed(&s, bytes[i])) {
            do {
                for (uint8_t w = 0; w < s.ump_words && count < cap; w++)
                    out[count++] = s.ump[w];
            } while (midi2_conv_next(&s));
        }
    }
    return count;
}

#ifdef WITH_AM_MIDI2
static size_t run_am(const uint8_t* bytes, size_t n, uint32_t* out, size_t cap) {
    alignas(bytestreamToUMP) unsigned char storage[sizeof(bytestreamToUMP)];
    memset(storage, 0xCD, sizeof storage);   // same poisoned-storage discipline
    bytestreamToUMP* c = new (storage) bytestreamToUMP();
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        c->bytestreamParse(bytes[i]);
        while (c->availableUMP() && count < cap) out[count++] = c->readUMP();
    }
    c->~bytestreamToUMP();
    return count;
}
#endif

static void print_words(const char* tag, const uint32_t* w, size_t n) {
    printf("  %-14s:", tag);
    if (n == 0) printf(" (none)");
    for (size_t i = 0; i < n; i++) printf(" %08lX", (unsigned long)w[i]);
    printf("\n");
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    for (;;) {
        int pass = 0, fail = 0;
        printf("\n=== rp2040-bench-midi1: MIDI 1.0 bytestream to UMP conformance ===\n");
        printf("midi2 core self-check against M2-104-UM derived expectations\n");
#ifdef WITH_AM_MIDI2
        printf("side-by-side with AM_MIDI2.0Lib bytestreamToUMP\n");
#endif
        for (size_t v = 0; v < sizeof kVectors / sizeof kVectors[0]; v++) {
            const Vector& t = kVectors[v];
            uint32_t got[MAXW];
            size_t n = run_midi2(t.bytes, t.nbytes, got, MAXW);
            bool ok = (n == t.nexpect) && (memcmp(got, t.expect, n * 4) == 0);
            printf("%s %s\n", ok ? "PASS" : "FAIL", t.name);
            printf("  bytes         :");
            for (size_t i = 0; i < t.nbytes; i++) printf(" %02X", t.bytes[i]);
            printf("\n");
            print_words("midi2", got, n);
            if (!ok) print_words("expected", t.expect, t.nexpect);
#ifdef WITH_AM_MIDI2
            uint32_t am[MAXW];
            size_t na = run_am(t.bytes, t.nbytes, am, MAXW);
            print_words("AM_MIDI2.0Lib", am, na);
            if (na != n || memcmp(am, got, n * 4) != 0)
                printf("  note          : conversions differ, compare the words above\n");
#endif
            ok ? pass++ : fail++;
        }
        printf("=== midi2 self-check: %d pass, %d fail ===\n", pass, fail);
        sleep_ms(5000);
    }
}
