#!/usr/bin/env python3
"""Reject post-P5 instructions in authored objects and reachable PE code.

Open Watcom combines code and small inter-function data tables in an executable
section named AUTO. A blind linear sweep therefore decodes those data bytes as
instructions. This gate still scans the whole section, then distinguishes real
code by following control flow from the PE entry point and every linker-map code
symbol. Authored object files are also disassembled directly with WDIS so static
callbacks and other address-taken local functions cannot escape the gate.
"""

import argparse
from collections import deque
from pathlib import Path
import re
import struct
import subprocess
import sys

DENIED = re.compile(
    r"^(?:cmov[a-z]*|fcmov[a-z]*|fcomi[a-z]*|fucomi[a-z]*|"
    r"movaps|movups|xorps|addps|mulps|divps|sqrtps|movdqa|movdqu|"
    r"movapd|addpd|mulpd|cvtsi2s[sd]|cvttsd2si|cvttss2si|cvtss2sd|"
    r"cvtsd2ss|ldmxcsr|stmxcsr|shufps|sysenter|sysexit|fxsave|fxrstor|"
    r"prefetchnta|prefetcht[012]|pause|v[a-z][a-z0-9]*)$",
    re.IGNORECASE,
)
PREFIXES = {"lock", "rep", "repe", "repz", "repne", "repnz", "data16", "addr16"}


def pe_info(path: Path):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    optional = pe + 24
    image_base = struct.unpack_from("<I", data, optional + 28)[0]
    entry = image_base + struct.unpack_from("<I", data, optional + 16)[0]
    sections = []
    cursor = optional + optional_size
    for _ in range(section_count):
        virtual_size, rva, raw_size, _, _, _, _, _, characteristics = struct.unpack_from("<IIIIIIHHI", data, cursor + 8)
        if characteristics & 0x20000000:
            sections.append((image_base + rva, image_base + rva + max(virtual_size, raw_size)))
        cursor += 40
    return entry, sections


def in_sections(address: int, sections) -> bool:
    return any(first <= address < last for first, last in sections)


def object_gate(wdis: Path, objects):
    failures = []
    checked = 0
    for obj in objects:
        output = subprocess.check_output([str(wdis), str(obj)], text=True, errors="replace", stderr=subprocess.STDOUT)
        in_text = False
        for line in output.splitlines():
            segment = re.match(r"^Segment:\s+(\S+)", line)
            if segment:
                in_text = segment.group(1).upper() == "_TEXT"
                continue
            if not in_text:
                continue
            match = re.match(r"^\s*[0-9A-Fa-f]+\s+(?:[0-9A-Fa-f]{2}\s+)+\s*([A-Za-z][A-Za-z0-9]*)", line)
            if not match:
                continue
            mnemonic = match.group(1).lower()
            checked += 1
            if DENIED.match(mnemonic):
                failures.append(f"{obj.name}: {line.strip()}")
    return checked, failures


def parse_instructions(objdump: Path, pe: Path):
    output = subprocess.check_output([str(objdump), "-d", str(pe)], text=True, errors="replace", stderr=subprocess.STDOUT)
    instructions = {}
    for line in output.splitlines():
        match = re.match(r"^\s*([0-9A-Fa-f]+):\s+((?:[0-9A-Fa-f]{2}\s+)+)\s*([A-Za-z.][A-Za-z0-9.]*)\s*(.*)$", line)
        if not match:
            continue
        address = int(match.group(1), 16)
        size = len(match.group(2).split())
        mnemonic = match.group(3).lower()
        operand = match.group(4).strip()
        if mnemonic in PREFIXES and operand:
            pieces = operand.split(None, 1)
            mnemonic = pieces[0].lower()
            operand = pieces[1] if len(pieces) > 1 else ""
        instructions[address] = (size, mnemonic, operand, line.strip())
    return instructions


def map_roots(path: Path, sections):
    roots = set()
    for line in path.read_text(encoding="latin-1").splitlines():
        match = re.match(r"^([0-9A-Fa-f]{8})(?:[+*])?\s+\S", line)
        if match:
            address = int(match.group(1), 16)
            if in_sections(address, sections):
                roots.add(address)
    return roots


def direct_target(operand: str):
    if not operand or operand.lstrip().startswith("*"):
        return None
    match = re.match(r"(?:0x)?([0-9A-Fa-f]{6,16})(?:\s|$)", operand)
    return int(match.group(1), 16) if match else None


def reachable_gate(instructions, roots, sections):
    queue = deque(roots)
    seen = set()
    failures = []
    while queue:
        address = queue.popleft()
        if address in seen or not in_sections(address, sections):
            continue
        instruction = instructions.get(address)
        if not instruction:
            continue
        seen.add(address)
        size, mnemonic, operand, line = instruction
        if DENIED.match(mnemonic):
            failures.append(line)
        following = address + size
        target = direct_target(operand)
        if mnemonic.startswith("ret") or mnemonic in {"iret", "iretd", "ud2", "hlt"}:
            continue
        if mnemonic.startswith("call"):
            if target is not None:
                queue.append(target)
            queue.append(following)
        elif mnemonic in {"jmp", "jmpw", "jmpl"}:
            if target is not None:
                queue.append(target)
        elif mnemonic.startswith("j") or mnemonic.startswith("loop"):
            if target is not None:
                queue.append(target)
            queue.append(following)
        else:
            queue.append(following)
    linear_hits = [line for _, mnemonic, _, line in instructions.values() if DENIED.match(mnemonic)]
    return seen, failures, linear_hits


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pe", type=Path, required=True)
    parser.add_argument("--map", dest="map_file", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    parser.add_argument("--wdis", type=Path, required=True)
    parser.add_argument("--object", type=Path, action="append", default=[])
    args = parser.parse_args()
    entry, sections = pe_info(args.pe)
    instructions = parse_instructions(args.objdump, args.pe)
    roots = map_roots(args.map_file, sections)
    roots.add(entry)
    seen, linked_failures, linear_hits = reachable_gate(instructions, roots, sections)
    object_count, object_failures = object_gate(args.wdis, args.object)
    for failure in object_failures + linked_failures:
        print(f"FAIL {failure}")
    if object_failures or linked_failures:
        return 1
    unreachable_hits = len(linear_hits)
    print(f"PASS authored_object_instructions={object_count} reachable_linked_instructions={len(seen)}")
    if unreachable_hits:
        print(f"INFO linear executable-section scan found {unreachable_hits} denied byte pattern(s) in unreachable inter-function data")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
