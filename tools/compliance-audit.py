#!/usr/bin/env python3
"""Static compliance audit for midi2cpp device recipes.

Checks every device recipe under examples/ against the requirements that the
official MIDI 2.0 Workbench (midi2-dev) enforces on the wire, plus the project
identity conventions, without needing hardware:

  - MIDI-CI bootstrap present (ci.begin)
  - DeviceInfo: valid JSON object with manufacturerId[3] / familyId[2] /
    modelId[2] / versionId[4] matching the MIDI-CI identity bytes, plus
    manufacturer / family / model / version strings (M2-105 DeviceInfo schema)
  - ChannelList: pure JSON array of {title, channel>=1} (M2-105 ChannelList)
  - ProgramList: JSON array of {title, bankPC[3]} (M2-105 ProgramList)
  - Custom resources: X- prefix, published through an app-registered
    ResourceList whose entry carries a schema with a title
  - Profile IDs: no unregistered 0x7D profiles at Function Block scope
  - tusb_config: CFG_TUD_MIDI2_EP_NAME ("<Board> MIDI 2.0") and a
    fleet-unique CFG_TUD_MIDI2_PRODUCT_ID
  - usb_descriptors: Manufacturer string, Product string equal to the
    DeviceInfo model (identity coherence across USB / UMP Stream / MIDI-CI)
  - Group Terminal Block callback present and bidirectional (TinyUSB recipes)
  - README: identification present, Spec coverage section present

Exit code 0 when clean; 1 when any finding remains. Run from the repo root:

    python3 tools/compliance-audit.py

This is the STATIC half of the compliance pair. The dynamic half,
tools/compliance-wire-test.py, exercises a real device over native UMP
(Discovery, Set Profile On/Off, MIDI Message Report) the way the Workbench
does on the wire.
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "examples")
MANUFACTURER = "midi2.diy"

# recipe -> (main file, tusb_config or None, usb_descriptors or None)
DEVICES = {
 "teensy41-midi2": ("teensy41-midi2.ino", None, None),
 "teensy41-control-surface": ("teensy41-control-surface.ino", None, None),
 "daisyseed-midi2": ("src/main.cpp", None, None),
 "rp2040-midi2": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "waveshare-rp2040-midi2": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "waveshare-rp2350-usb-a-midi2": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "rp2350-pico2-midi2": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "t-picoc3-device-midi2": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "weact-STM32F411CEU6-blackpill-device-midi2": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "nrf52840-promicro-midi2": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "ra4m1-weact-device-midi2": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "xiao-samd21-midi2": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "rp2040-promicro-ump-test-bench": ("src/main.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "esp32-p4-devkit-usb-midi2": ("idf/main/main.cpp", "idf/main/tusb_config.h", "idf/components/tinyusb/usb_descriptors.c"),
 "esp32-s3-devkitc-usb-midi2": ("idf/main/main.cpp", "idf/main/tusb_config.h", "idf/components/tinyusb/usb_descriptors.c"),
 "arduino-nano-esp32-midi2": ("idf/main/main.cpp", "idf/main/tusb_config.h", "idf/components/tinyusb/usb_descriptors.c"),
 "t-display-s3-midi2": ("idf/main/main.cpp", "idf/main/tusb_config.h", "idf/components/tinyusb/usb_descriptors.c"),
}

# bridge recipe -> (main file, extra glue file or None, tusb_config, usb_descriptors)
# The device face of a bridge is a USB MIDI 2.0 device: identity and category
# rules apply to it exactly as they do to the device recipes above.
BRIDGES = {
 "adafruit-feather-rp2040-bridge-midi2": ("src/main.cpp", "src/feather_bridge.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "waveshare-rp2350-usb-a-bridge-midi2": ("src/main.cpp", "src/feather_bridge.cpp", "src/tusb_config.h", "src/usb_descriptors.c"),
 "esp32-p4-devkit-bridge-midi2": ("idf/main/main.cpp", "idf/main/esp32_p4_devkit_bridge.cpp", "idf/main/tusb_config.h", "idf/components/tinyusb/usb_descriptors.c"),
 "esp32-p4-devkit-bridge2-midi2": ("idf/main/main.cpp", "idf/main/esp32_p4_devkit_bridge2.cpp", "idf/main/tusb_config.h", "idf/components/tinyusb/usb_descriptors.c"),
}

# host recipe -> glue file holding the m2host bring-up
HOSTS = {
 "adafruit-feather-rp2040-host-midi2": "src/feather_host.cpp",
 "daisyseed-host-midi2": "src/daisyseed_host_midi2.cpp",
 "esp32-p4-devkit-host-midi2": "idf/main/esp32_p4_devkit_host.cpp",
 "esp32-s3-devkitc-host-midi2": "pio/src/main.cpp",
 "t-display-s3-shield-host-midi2": "pio/src/main.cpp",
 "teensy41-host-midi2": "src/teensy41_host_midi2.cpp",
}

PE_BODY_MAX = 448   # midi2 lib reply body cap (sized to the advertised 512 maxSysex)


def read(path):
    try:
        with open(path) as fh:
            return fh.read()
    except OSError:
        return None


def c_literal_json(text, anchor):
    """Concatenate adjacent C string literals following `anchor` until the
    JSON value they spell out balances. Returns the decoded JSON string or
    None. Works both at a registration site (addPropertyStatic("Name", "{...")
    and at a declaration site (static const char kName[] = "{...")."""
    i = text.find(anchor)
    if i < 0:
        return None
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', text[i:i + 1600])
    s = ""
    started = False
    for lit in lits:
        u = lit.encode().decode("unicode_escape")
        if not started:
            if not u.lstrip().startswith(("{", "[")):
                continue            # skip the resource-name literal itself
            started = True
        s += u
        st = s.strip()
        if st.startswith("{") and st.endswith("}") and st.count("{") == st.count("}"):
            return st
        if st.startswith("[") and st.endswith("]") and st.count("[") == st.count("]"):
            return st
    return None


def resource_json(text, name, decl_hint=None):
    """Find a resource's JSON either at its registration or at a declaration
    constant (e.g. kDeviceInfo[])."""
    # Declaration site first (authoritative); registration site as fallback.
    for anchor in filter(None, (decl_hint, f'"{name}"')):
        j = c_literal_json(text, anchor)
        if j:
            return j
    return None


def audit(name, spec, product_ids, model_ids):
    mainf, tusbf, descf = spec
    d = os.path.join(ROOT, name)
    t = read(os.path.join(d, mainf))
    F = []
    if t is None:
        return [f"main file missing: {mainf}"]

    if "ci.begin" not in t:
        F.append("no ci.begin (MIDI-CI bootstrap missing)")

    model = ""
    di = resource_json(t, "DeviceInfo", "kDeviceInfo[]")
    if not di:
        F.append("DeviceInfo missing")
    else:
        try:
            j = json.loads(di)
            if not isinstance(j, dict):
                raise ValueError("DeviceInfo must be a JSON object")
            km = re.search(r'kModelId\s*=\s*0x([0-9A-Fa-f]+)', t)
            if km:
                mid = int(km.group(1), 16)
                want_model = [mid & 0x7F, (mid >> 7) & 0x7F]
                if j.get("modelId") != want_model:
                    F.append(f"DeviceInfo modelId={j.get('modelId')} != kModelId bytes {want_model}")
                model_ids.setdefault(mid, []).append(name)
            for key, want in (("manufacturerId", [125, 0, 0]), ("familyId", [1, 0]),
                              ("modelId", None), ("versionId", [0, 0, 4, 0])):
                if key not in j:
                    F.append(f"DeviceInfo missing {key}")
                elif want and j[key] != want:
                    F.append(f"DeviceInfo {key}={j[key]} (expected {want})")
            if j.get("manufacturer") != MANUFACTURER:
                F.append(f"DeviceInfo manufacturer={j.get('manufacturer')!r}")
            if not j.get("family"):
                F.append("DeviceInfo family empty")
            model = j.get("model", "")
            if "MIDI 2.0" not in model:
                F.append(f"DeviceInfo model lacks 'MIDI 2.0': {model!r}")
            if j.get("version") not in ("0.0.1", "0.1"):
                F.append(f"DeviceInfo version={j.get('version')!r}")
            if len(di) > PE_BODY_MAX:
                F.append(f"DeviceInfo {len(di)}B exceeds PE body cap {PE_BODY_MAX}")
        except ValueError as e:
            F.append(f"DeviceInfo invalid JSON: {e}")

    cl = resource_json(t, "ChannelList", "kChannelList[]")
    if not cl:
        m = re.search(r'ChannelList"?,\s*\n?\s*\[\]\(\) -> const char\* \{ return "((?:[^"\\]|\\.)*)"', t)
        cl = m.group(1).encode().decode("unicode_escape") if m else None
    if not cl:
        F.append("ChannelList missing")
    else:
        try:
            j = json.loads(cl)
            if not isinstance(j, list):
                F.append("ChannelList is not a pure array")
            elif not all("title" in e and isinstance(e.get("channel"), int) and e["channel"] >= 1 for e in j):
                F.append("ChannelList entries need title + channel >= 1")
        except ValueError as e:
            F.append(f"ChannelList invalid JSON: {e}")

    pl = resource_json(t, "ProgramList", "kProgramList[]")
    if not pl:
        F.append("ProgramList missing")
    else:
        try:
            j = json.loads(pl)
            ok = isinstance(j, list) and all(
                "title" in e and isinstance(e.get("bankPC"), list) and len(e["bankPC"]) == 3 for e in j)
            if not ok:
                F.append("ProgramList entries need title + bankPC[3]")
        except ValueError as e:
            F.append(f"ProgramList invalid JSON: {e}")

    customs = set(re.findall(r'addProperty(?:Static)?\("(X-[A-Za-z0-9-]+)"', t))
    plain = {n for n in re.findall(r'addProperty(?:Static)?\("([A-Za-z0-9-]+)"', t)
             if not n.startswith("X-")} \
        - {"DeviceInfo", "ChannelList", "ProgramList", "ResourceList"}
    if plain:
        F.append(f"custom resource without X- prefix: {sorted(plain)}")
    if customs:
        rl = resource_json(t, "ResourceList")
        if not rl:
            F.append("X- resource present but no app ResourceList override")
        else:
            try:
                j = json.loads(rl)
                names = {e.get("resource") for e in j}
                for c in customs:
                    entry = next((e for e in j if e.get("resource") == c), None)
                    if not entry:
                        F.append(f"{c} not listed in ResourceList")
                    elif "schema" not in entry:
                        F.append(f"{c} ResourceList entry lacks schema")
                    elif "title" not in entry["schema"]:
                        F.append(f"{c} schema lacks title")
                for r in ("DeviceInfo", "ChannelList", "ProgramList"):
                    if r not in names:
                        F.append(f"ResourceList override omits {r}")
            except ValueError as e:
                F.append(f"ResourceList invalid JSON: {e}")

    if "addProfile" not in t:
        F.append("no Profile registered (ci_cat advertises Profile Configuration)")
    if "setMidiReport" not in t:
        F.append("no setMidiReport (ci_cat advertises Process Inquiry)")
    for m in re.findall(r'kProfile\w*\[5\]\s*=\s*\{([^}]+)\}', t):
        if m.split(",")[0].strip() == "0x7D":
            F.append("profile 0x7D at FB scope (use GM 1 0x7E or none)")

    if tusbf:
        tc = read(os.path.join(d, tusbf)) or ""
        ep = re.search(r'CFG_TUD_MIDI2_EP_NAME\s+"([^"]+)"', tc)
        pid = re.search(r'CFG_TUD_MIDI2_PRODUCT_ID\s+"([^"]+)"', tc)
        if not ep:
            F.append("CFG_TUD_MIDI2_EP_NAME missing")
        elif "MIDI 2.0" not in ep.group(1):
            F.append(f"EP name lacks 'MIDI 2.0': {ep.group(1)!r}")
        if not pid:
            F.append("CFG_TUD_MIDI2_PRODUCT_ID missing")
        else:
            product_ids.setdefault(pid.group(1), []).append(name)

        glue_files = glob.glob(os.path.join(d, "src/*.cpp")) + glob.glob(os.path.join(d, "idf/main/*.cpp"))
        glue = " ".join(filter(None, (read(p) for p in glue_files)))
        if "tud_midi2_gtb_desc_cb" not in glue:
            F.append("tud_midi2_gtb_desc_cb missing (FB info falls back to defaults)")
        elif "MIDI2_GTB_BIDIRECTIONAL" not in glue:
            F.append("GTB is not bidirectional")

    if descf:
        dc = read(os.path.join(d, descf)) or ""
        if f'"{MANUFACTURER}"' not in dc:
            F.append(f"usb_descriptors Manufacturer is not {MANUFACTURER!r}")
        prod = re.search(r'"([^"]+)",\s*/\* 2: Product', dc)
        if prod and model and prod.group(1) != model:
            F.append(f"USB Product {prod.group(1)!r} != DeviceInfo model {model!r}")

    rd = read(os.path.join(d, "README.md")) or ""
    if not re.search(r'## Spec [Cc]overage', rd):
        F.append("README lacks a Spec coverage section")
    if MANUFACTURER not in rd:
        F.append(f"README never mentions {MANUFACTURER}")
    return F


def audit_bridge(name, spec, product_ids, model_ids):
    mainf, gluef, tusbf, descf = spec
    d = os.path.join(ROOT, name)
    t = read(os.path.join(d, mainf)) or ""
    glue = t + " " + (read(os.path.join(d, gluef)) or "")
    F = []
    if not t:
        return [f"main file missing: {mainf}"]

    # Device-face USB identity: same fleet conventions as the device recipes.
    tc = read(os.path.join(d, tusbf)) or ""
    ep = re.search(r'CFG_TUD_MIDI2_EP_NAME\s+"([^"]+)"', tc)
    pid = re.search(r'CFG_TUD_MIDI2_PRODUCT_ID\s+"([^"]+)"', tc)
    if not ep:
        F.append("CFG_TUD_MIDI2_EP_NAME missing")
    elif "MIDI 2.0" not in ep.group(1):
        F.append(f"EP name lacks 'MIDI 2.0': {ep.group(1)!r}")
    if not pid:
        F.append("CFG_TUD_MIDI2_PRODUCT_ID missing")
    else:
        product_ids.setdefault(pid.group(1), []).append(name)

    dc = read(os.path.join(d, descf)) or ""
    if f'"{MANUFACTURER}"' not in dc:
        F.append(f"usb_descriptors Manufacturer is not {MANUFACTURER!r}")

    # FB/endpoint identity on the wire, by any of the three valid paths:
    # driver GTB callback (#3738), an app-level Stream responder, or the
    # m2bridge composed responder (lib-owned).
    uses_m2bridge = bool(re.search(r'\bBridge\s+\w+|midi2::Bridge|m2bridge', t))
    app_responder = "onEndpointDiscovery" in glue and "onFbDiscovery" in glue
    if "tud_midi2_gtb_desc_cb" in glue:
        if "MIDI2_GTB_BIDIRECTIONAL" not in glue:
            F.append("GTB is not bidirectional")
    elif not app_responder and not uses_m2bridge:
        F.append("no FB identity path (gtb_desc_cb, app Stream responder or m2bridge)")

    # MIDI-CI face (direct m2ci or composed via m2bridge). ci.begin defaults
    # to ciCat 0x1C (Profile | PE | Process Inquiry): every advertised
    # category must be backed, exactly as on the device recipes.
    has_ci = "ci.begin" in t or re.search(r'\bBridge\s+\w+|midi2::Bridge|m2bridge', t)
    if has_ci:
        km = re.search(r'kModel\s*=\s*0x([0-9A-Fa-f]+)', t)
        if km:
            model_ids.setdefault(int(km.group(1), 16), []).append(name)
        else:
            F.append("kModel not found (fleet-unique model id unverifiable)")
        if not resource_json(t, "DeviceInfo", "kDeviceInfo[]"):
            F.append("MIDI-CI face without DeviceInfo (PE advertised by default ciCat)")
        if "addProfile" not in t:
            F.append("MIDI-CI face without a Profile (ciCat advertises Profile Configuration)")
        if "setMidiReport" not in t:
            F.append("MIDI-CI face without setMidiReport (ciCat advertises Process Inquiry)")

    # Forwarding TX must not drop silently on a full FIFO: the write result
    # feeds a bounded retry or a surfaced drop counter.
    m = re.search(r'^\s*tud_midi2_n_ump_write\([^;]*\);\s*$', glue, re.M)
    if m:
        F.append("device-face forward ignores tud_midi2_n_ump_write result (silent drop on full FIFO)")
    return F


def audit_host(name, gluef):
    d = os.path.join(ROOT, name)
    t = read(os.path.join(d, gluef))
    if t is None:
        return [f"glue file missing: {gluef}"]
    F = []
    # Initiator MUID entropy: rng must be installed before the SAME object's
    # begin() so the boot MUID seeds from it (same rule as the device fleet).
    m = re.search(r'(\w+)(?:\.|->)setRngFn', t)
    if not m:
        F.append("no setRngFn (initiator MUID falls back to a fixed seed)")
    else:
        obj = m.group(1)
        beg = re.search(rf'\b{re.escape(obj)}(?:\.|->)begin\(', t)
        if beg and beg.start() < m.start():
            F.append("setRngFn installed after begin() (boot MUID misses the rng)")
    return F


def main():
    product_ids = {}
    model_ids = {}
    total = 0
    width = max(len(n) for n in list(DEVICES) + list(BRIDGES) + list(HOSTS)) + 2
    for name, spec in DEVICES.items():
        findings = audit(name, spec, product_ids, model_ids)
        total += len(findings)
        print(f"{name:<{width}} {'OK' if not findings else f'{len(findings)} finding(s)'}")
        for f in findings:
            print(f"    - {f}")
    for name, spec in BRIDGES.items():
        findings = audit_bridge(name, spec, product_ids, model_ids)
        total += len(findings)
        print(f"{name:<{width}} {'OK' if not findings else f'{len(findings)} finding(s)'}")
        for f in findings:
            print(f"    - {f}")
    for name, gluef in HOSTS.items():
        findings = audit_host(name, gluef)
        total += len(findings)
        print(f"{name:<{width}} {'OK' if not findings else f'{len(findings)} finding(s)'}")
        for f in findings:
            print(f"    - {f}")
    dups = {k: v for k, v in product_ids.items() if len(v) > 1}
    if dups:
        total += len(dups)
        print(f"\nduplicate CFG_TUD_MIDI2_PRODUCT_ID: {dups}")
    mdups = {k: v for k, v in model_ids.items() if len(v) > 1}
    if mdups:
        total += len(mdups)
        print(f"\nduplicate MIDI-CI modelId (Workbench caches by mfr+family+model): {mdups}")
    print(f"\ntotal findings: {total}")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
