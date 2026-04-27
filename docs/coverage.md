# Feature Coverage Map — midi2_cpp v0.1.0 → MIDI 2.0 specs

Maps every public midi2_cpp API to the corresponding MIDI 2.0 spec section
and the backing midi2 C99 v0.3.0 function. For the cross-comparison vs
AM_MIDI2.0Lib / ni-midi2 / cmidi2, see the design spec §4.

## Zero external dependencies

`midi2_cpp` depends on nothing outside its own source tree:

- midi2 C99 vendored stb-style at `src/midi2.{h,c}` (one source of truth,
  versioned together).
- No git submodules — `git clone` is the install.
- No USB stack pulled in (TinyUSB, native USB, PIO-USB, libDaisy USBMidi
  are all caller-supplied).
- No `<Arduino.h>`, `pico/time.h`, `esp_timer.h`, or any platform header
  in the library translation units.
- No clock or RNG dependency — caller injects through public hooks.

Anywhere C++17 + CMake reach, the library compiles and self-tests with
no network access after the initial clone.

## Platform contract

The caller wires five injection points; everything else is library
responsibility.

| API | Caller responsibility | Library responsibility |
|---|---|---|
| `Device::setWriteFn(WriteFn)` | forward UMP to the platform's USB MIDI write | call this for every `sendXxx` and JR heartbeat |
| `Device::feedRx(words, count)` | drain UMP from the platform's USB MIDI RX callback | parse, reassemble SysEx, dispatch to `onXxx` |
| `Device::setNowFn(NowFn)` | return monotonic ms (`millis`, `time_us_64()/1000`, `esp_timer_get_time()/1000`) | trigger JR heartbeat at the configured interval |
| `Device::setMounted(bool)` / `setAltSetting(uint8_t)` | inform USB enumeration state | use as a precondition for sends (alt 1 = MIDI 2.0) |
| `CI::setRngFn(RngFn)` | provide entropy (`random`, `get_rand_32`, `esp_random`) | seed MUID, regenerate on collision, NAK Invalidate MUID |

When a hook is left unset the affected feature degrades gracefully (no
transport, frozen MUID, no heartbeat). The library never aborts and never
calls a platform symbol it did not receive.

## UMP transport (M2-104)

### MT 0x0 Utility (§7.2)

| midi2_cpp | Spec | midi2 C99 backing |
|---|---|---|
| `Device::sendNoop` | §7.2.1 | inline build (1-word w/ MT=0, status=0) |
| `Device::sendJRClock` | §7.2.2.2 | `midi2_msg_jr_clock` |
| `Device::sendJRTimestamp` | §7.2.2.3 | `midi2_msg_jr_timestamp` |
| `Device::sendDctpq` | §7.2.4 | `midi2_msg_dctpq` |
| `Device::sendDeltaClockstamp` | §7.2.5 | `midi2_msg_delta_clockstamp` |
| `Device::enableJRHeartbeat(ms=500)` | §7.2.2.3 | timer in `Device::task()` |
| `Device::onNoop`, `onJRClock`, `onJRTimestamp`, `onDctpq`, `onDeltaClockstamp` | dispatch | `midi2_dispatch.on_*` |

### MT 0x1 System (§7.6)

| midi2_cpp | Spec status byte | midi2 C99 backing |
|---|---|---|
| `Device::sendTuneRequest` | 0xF6 | `midi2_msg_system_tune_request` |
| `Device::sendClock` | 0xF8 | `midi2_msg_system_timing_clock` |
| `Device::sendStart` | 0xFA | `midi2_msg_system_start` |
| `Device::sendContinue` | 0xFB | `midi2_msg_system_continue` |
| `Device::sendStop` | 0xFC | `midi2_msg_system_stop` |
| `Device::sendActiveSensing` | 0xFE | `midi2_msg_system_active_sensing` |
| `Device::sendSystemReset` | 0xFF | `midi2_msg_system_reset` |
| `Device::sendMTC(timeCode)` | 0xF1 | `midi2_msg_system_mtc` |
| `Device::sendSongSelect(song)` | 0xF3 | `midi2_msg_system_song_select` |
| `Device::sendSongPosition(beats14)` | 0xF2 | `midi2_msg_system_song_position` |
| `Device::sendSystemGeneric(status, d1, d2)` | escape hatch | inline build |
| `Device::onSystem` | dispatch | `midi2_dispatch.on_system` |

### MT 0x2 MIDI 1.0 Channel Voice (§7.3)

| midi2_cpp | MIDI 1.0 status | midi2 C99 backing |
|---|---|---|
| `Device::sendNoteOn1` | 0x90 | `midi2_msg_from_midi1` |
| `Device::sendNoteOff1` | 0x80 | `midi2_msg_from_midi1` |
| `Device::sendCC1` | 0xB0 | `midi2_msg_from_midi1` |
| `Device::sendProgram1` | 0xC0 | `midi2_msg_from_midi1` |
| `Device::sendPitchBend1(value14)` | 0xE0 | `midi2_msg_from_midi1` |
| `Device::sendChannelPressure1` | 0xD0 | `midi2_msg_from_midi1` |
| `Device::sendPolyPressure1` | 0xA0 | `midi2_msg_from_midi1` |
| `Device::setUpscaleMt2(true)` | dispatch flag | `midi2_dispatch.upscale_mt2` |
| `Device::onNoteOn1` ... `onPolyPressure1` | dispatch | `midi2_dispatch.on_cv1_*` |

### MT 0x3 SysEx7 (§7.7)

| midi2_cpp | Spec | midi2 C99 backing |
|---|---|---|
| `Device::sendSysEx7(data, len)` | §7.7 (auto-fragment) | `midi2_proc_send_sysex7` |
| `Device::onSysEx7` | reassembly | `midi2_proc.on_sysex7` |

### MT 0x4 MIDI 2.0 Channel Voice (§7.4) — CORE

| midi2_cpp | Spec | midi2 C99 backing |
|---|---|---|
| `Device::sendNoteOn(group, ch, note, vel16, attrType, attrData)` | §7.4.1 | `midi2_msg_note_on` |
| `Device::sendNoteOff(...)` | §7.4.2 | `midi2_msg_note_off` |
| `Device::sendPolyPressure(ch, note, val32)` | §7.4.3 | `midi2_msg_poly_pressure` |
| `Device::sendCC(ch, idx, val32)` | §7.4.6 | `midi2_msg_cc` |
| `Device::sendRpn(ch, msb, lsb, val32)` | §7.4.7 | `midi2_msg_rpn` |
| `Device::sendNrpn(ch, msb, lsb, val32)` | §7.4.8 | `midi2_msg_nrpn` |
| `Device::sendRelRpn(...)` | §7.4.9 | `midi2_msg_rel_rpn` |
| `Device::sendRelNrpn(...)` | §7.4.10 | `midi2_msg_rel_nrpn` |
| `Device::sendProgram(ch, prog, msb, lsb, bankValid)` | §7.4.11 | `midi2_msg_program` |
| `Device::sendChannelPressure(ch, val32)` | §7.4.4 | `midi2_msg_chan_pressure` |
| `Device::sendPitchBend(ch, val32)` | §7.4.5 | `midi2_msg_pitch_bend` |
| `Device::sendPerNotePitchBend(ch, note, val)` | §7.4.12 | `midi2_msg_per_note_pb` |
| `Device::sendRegPerNoteController(...)` | §7.4.13 | `midi2_msg_reg_per_note_ctrl` |
| `Device::sendAsnPerNoteController(...)` | §7.4.14 | `midi2_msg_asn_per_note_ctrl` |
| `Device::sendPerNoteManagement(ch, note, detach, reset)` | §7.4.15 | `midi2_msg_per_note_mgmt` |
| `Device::onNoteOn` ... `onPerNoteManagement` (15 callbacks) | dispatch | `midi2_dispatch.on_*` |

### MT 0x5 SysEx8 (§7.8)

| midi2_cpp | Spec | midi2 C99 backing |
|---|---|---|
| `Device::sendSysEx8(group, streamId, data, len)` | §7.8 (auto-fragment) | `midi2_proc_send_sysex8` |
| `Device::onSysEx8` | reassembly | `midi2_proc.on_sysex8` |

### MT 0xD Flex Data (§7.5)

| midi2_cpp | Spec | midi2 C99 backing |
|---|---|---|
| `Device::sendTempo(tenNsPerQuarter)` | §7.5.4 | `midi2_msg_tempo` |
| `Device::sendTimeSignature(num, denom)` | §7.5.5 | `midi2_msg_time_sig` |
| `Device::sendMetronome(...)` | §7.5.6 | `midi2_msg_metronome` |
| `Device::sendKeySignature(sf, minor)` | §7.5.7 | `midi2_msg_key_sig` |
| `Device::sendChordName(ChordDescriptor)` | §7.5.8 | `midi2_msg_chord_name` (21 args) |
| `Device::sendFlexText(bank, status, text)` | §7.5.9-10 | `midi2_msg_flex_text` |
| `Device::onTempo`, `onTimeSignature`, `onMetronome`, `onKeySignature`, `onChord`, `onFlexText` | dispatch | `midi2_dispatch.on_*` |

### MT 0xF UMP Stream (§7.1)

| midi2_cpp | Spec | midi2 C99 backing |
|---|---|---|
| `Device::sendDeviceIdentity(mfrId[3], family, model, version)` | §7.1.6 | `midi2_proc_send_device_identity` |
| `Device::sendEndpointNameUpdate(name)` | §7.1.4 | `midi2_proc_send_endpoint_name` |
| `Device::sendProductInstanceIdUpdate(id)` | §7.1.5 | `midi2_proc_send_product_id` |
| `Device::sendFbNameUpdate(fbIdx, name)` | §7.1.9 | `midi2_proc_send_fb_name` |
| `Device::sendStartOfClip()` | §7.1.10 | `midi2_msg_stream_start_of_clip` |
| `Device::sendEndOfClip()` | §7.1.11 | `midi2_msg_stream_end_of_clip` |
| `Device::onEndpointDiscovery`, `onEndpointInfo`, `onDeviceIdentity`, `onStreamText`, `onFbName`, `onStreamConfigRequest`, `onStreamConfigNotify`, `onFbDiscovery`, `onFbInfo`, `onClip` | dispatch | `midi2_dispatch.on_*` |

## MIDI-CI v1.2 (M2-101)

### Discovery (§5.5–5.8)

| midi2_cpp | Spec sub-ID | midi2 C99 backing |
|---|---|---|
| `CI::sendDiscoveryInquiry()` | 0x70 | `midi2_ci_build_discovery` + `midi2_proc_send_sysex7` |
| `CI::onDiscovery` (auto-respond + notify) | dispatch + responder | `midi2_ci_dispatch.on_discovery` + `midi2_ci_process_sysex` |
| `CI::onDiscoveryReply` | 0x71 | `midi2_ci_dispatch.on_discovery_reply` |

### ACK / NAK / Invalidate / Endpoint Info (§5.9, §6, §6.4)

| midi2_cpp | Spec sub-ID | midi2 C99 backing |
|---|---|---|
| `CI::onAck` | 0x7D | `midi2_ci_dispatch.on_ack` |
| `CI::onNak` | 0x7F | `midi2_ci_dispatch.on_nak` |
| `CI::onInvalidateMuid` (auto-regen MUID) | 0x7E | `midi2_ci_dispatch.on_invalidate_muid` |
| `CI::onEndpointInfo` | 0x72 | `midi2_ci_dispatch.on_endpoint_info` |
| `CI::onEndpointInfoReply` | 0x73 | `midi2_ci_dispatch.on_endpoint_info_reply` |

### Profile Configuration (§7)

| midi2_cpp | Spec sub-ID | midi2 C99 backing |
|---|---|---|
| `CI::addProfile(id, alwaysOn=false)` | registry | `midi2_ci_add_profile` |
| `CI::removeProfile(id)` | registry | `midi2_ci_remove_profile` |
| `CI::onProfileInquiry` (auto-respond) | 0x20 | `midi2_ci_dispatch.on_profile_inquiry` + responder |
| `CI::onProfileEnable(id, channels)` | 0x22 | `midi2_ci_dispatch.on_set_profile_on` |
| `CI::onProfileDisable(id, channels)` | 0x23 | `midi2_ci_dispatch.on_set_profile_off` |
| `CI::onProfileAdded(id)` | 0x26 | `midi2_ci_dispatch.on_profile_added` |
| `CI::onProfileRemoved(id)` | 0x27 | `midi2_ci_dispatch.on_profile_removed` |
| `CI::onProfileDetailsInquiry(id, target)` | 0x28 | `midi2_ci_dispatch.on_profile_details` |
| `CI::onProfileSpecificData(id, data, len)` | 0x2F | `midi2_ci_dispatch.on_profile_specific_data` |

### Property Exchange (§8, M2-103, M2-105)

| midi2_cpp | Spec sub-ID | midi2 C99 backing |
|---|---|---|
| `CI::addProperty(name, getter, setter=nullptr)` | registry | `midi2_ci_add_property_dynamic` |
| `CI::addPropertyStatic(name, value)` | registry | `midi2_ci_add_property_static` |
| `CI::setPropertySubscribable(name, true)` | flag | `midi2_ci_pe_set_subscribable` |
| `CI::notifyPropertyChanged(name)` (fan-out) | 0x3F | `midi2_ci_notify_property_changed` |
| `CI::subscriberCount()` | telemetry | `midi2_ci_get_subscriber_count` |
| `CI::removeProperty(name)` | registry (mirrors shift) | `midi2_ci_remove_property` |
| `CI::onPECapability(maxSimul, peMajor, peMinor)` | 0x30 | `midi2_ci_dispatch.on_pe_capability` |
| `CI::onPEGet(header, headerLen)` | 0x34 | `midi2_ci_dispatch.on_pe_get` |
| `CI::onPESet(header, headerLen, body, bodyLen)` | 0x36 | `midi2_ci_dispatch.on_pe_set` |
| `CI::onPESubscribe(header, headerLen)` | 0x38 | `midi2_ci_dispatch.on_pe_subscribe` |
| `CI::onPENotify(header, headerLen, body, bodyLen)` | 0x3F | `midi2_ci_dispatch.on_pe_notify` |
| Auto-reply PE Capability + PE Get | Appendix E | `midi2_ci_process_sysex` |

### Process Inquiry (§9 + Appendix F)

| midi2_cpp | Spec sub-ID | midi2 C99 backing |
|---|---|---|
| `CI::setMidiReport(msgDataControl, sys, ch, note)` | 0x42 bitmap | stored in CIState |
| `CI::onPICapability` | 0x40 | `midi2_ci_dispatch.on_pi_capability` |
| `CI::onMidiReportInquiry(msgDataCtl)` | 0x42 | `midi2_ci_dispatch.on_pi_midi_report` |

## Bit Scaling (M2-115)

| midi2_cpp (static) | Spec | midi2 C99 backing |
|---|---|---|
| `Device::scaleUp7to16(v7)` | §3.1 | `midi2_msg_scale_up_7to16` |
| `Device::scaleUp7to32(v7)` | §3.1 | `midi2_msg_scale_up_7to32` |
| `Device::scaleUp14to32(v14)` | §3.1 | `midi2_msg_scale_up_14to32` |
| `Device::scaleDown16to7(v)` | §3.2 | `midi2_msg_scale_down_16to7` |
| `Device::scaleDown32to7(v)` | §3.2 | `midi2_msg_scale_down_32to7` |
| `Device::scaleDown32to14(v)` | §3.2 | `midi2_msg_scale_down_32to14` |
| Roundtrip test | §4 | 16,640 iterations in `test_midi2_scaling.cpp` |

## Helpers (field-tested)

| midi2_cpp (static) | Use case | midi2 C99 backing |
|---|---|---|
| `Device::setUmpGroup(word0*, group)` | edit group nibble | `midi2_msg_set_group` |
| `Device::downgradeMt4ToMt2(in, count, out, outCount)` | A/B 1.0/2.0 toggle | `midi2_msg_mt4_to_mt2` |
| `Device::cableEventToUmp(cableEvent, group, ump*)` | USB MIDI 1.0 → UMP | `midi2_msg_cable_event_to_ump` |
| `Device::setGroupRemap(map[16])` | remap incoming groups | `midi2_proc.group_map[]` |
| `Device::setUpscaleMt2(bool)` | auto MT 0x2 → MT 0x4 in dispatch | `midi2_dispatch.upscale_mt2` |

## Byte Stream → UMP

| midi2_cpp | Use | midi2 C99 backing |
|---|---|---|
| `ByteStreamConverter(group)` | DIN-5 MIDI 1.0 input | `midi2_conv_state` |
| `ByteStreamConverter::feed(byte)` | byte-by-byte | `midi2_conv_feed` |
| `ByteStreamConverter::onUmp(callable)` | emitted UMP | `state->ump[]` |
| `ByteStreamConverter::reset()` | clear running status | re-init via `midi2_conv_init` |
| `ByteStreamConverter::setGroup(g)` | change UMP group | `state->group` |

## Out of scope for v0.1.0

- USB transport stack (TinyUSB / native USB / Adafruit_TinyUSB / Teensy
  USB / libDaisy USBMidi). Lives in the caller — see the Platform
  contract section above.
- Platform clock and RNG (`millis`, `esp_random`, `get_rand_32`). Caller
  injects via `setNowFn` / `setRngFn`.
- AVR Uno — out of scope by RAM budget; the library targets boards with
  at least ~6 KB free RAM after stack reservation.
- 5 Initiator-role senders (`sendEndpointInfoInquiry/Reply`, `sendAck`,
  `sendProfileDetailsReply`, `sendProfileSpecificData`) — auto-respond
  covers Receiver flows; manual senders deferred to v0.1.x
- `setMaxSysexSize` — pending upstream setter in midi2 C99
- `sendFlexText` multi-UMP fragmentation (refuses payloads > 12 bytes)
- UMP → byte stream (only the byte→UMP direction is implemented)
- MCoded7 SysEx compression
- Pitch fixed-point types (`pitch_7_9`, `pitch_7_25`)
