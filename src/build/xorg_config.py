#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

def matches(pattern: str, line: str) -> bool:
    return re.match(pattern, line, re.IGNORECASE) is not None

def identifier(line: str) -> str:
    match = re.match(r'^\s*Identifier\s+(.+?)\s*$', line, re.IGNORECASE)
    if not match:
        return ""
    return match.group(1).strip().strip('"')

def screen_id(lines: list[str]) -> str:
    in_screen = False
    for line in lines:
        if matches(r'^\s*Section\s+"Screen"', line):
            in_screen = True
            continue
        if in_screen and matches(r'^\s*Identifier\s+', line):
            return identifier(line) or "Screen0"
        if in_screen and matches(r'^\s*EndSection', line):
            in_screen = False
    return "Screen0"

def transform_layout(lines: list[str], chosen_screen: str) -> list[str]:
    out: list[str] = []
    in_files = in_module = in_flags = in_layout = skip_layout = False
    saw_files = saw_module = saw_flags = saw_layout = False
    modulepath_written = evdev_load_written = False
    flags_option = flags_enable_option = flags_allow_empty_option = flags_xkb_disable_option = False
    screen_seen = False

    for line in lines:
        if in_files:
            if matches(r'^\s*ModulePath\s+', line):
                if not modulepath_written:
                    out.append('    ModulePath "/usr/lib/xorg/modules"')
                modulepath_written = True
                continue
            if matches(r'^\s*EndSection', line):
                if not modulepath_written:
                    out.append('    ModulePath "/usr/lib/xorg/modules"')
                    modulepath_written = True
                out.append(line)
                in_files = False
                continue
            out.append(line)
            continue

        if in_module:
            if matches(r'^\s*Load\s+"Evdev"', line):
                if not evdev_load_written:
                    out.append('    Load "evdev"')
                evdev_load_written = True
                continue
            if matches(r'^\s*EndSection', line):
                if not evdev_load_written:
                    out.append('    Load "evdev"')
                    evdev_load_written = True
                out.append(line)
                in_module = False
                continue
            out.append(line)
            continue

        if skip_layout:
            if matches(r'^\s*EndSection', line):
                skip_layout = False
            continue

        if in_flags:
            options = [
                (r'^\s*Option\s+"AutoAddDevices"', '    Option "AutoAddDevices" "false"', 'flags_option'),
                (r'^\s*Option\s+"AutoEnableDevices"', '    Option "AutoEnableDevices" "true"', 'flags_enable_option'),
                (r'^\s*Option\s+"AllowEmptyInput"', '    Option "AllowEmptyInput" "false"', 'flags_allow_empty_option'),
                (r'^\s*Option\s+"XkbDisable"', '    Option "XkbDisable" "false"', 'flags_xkb_disable_option'),
            ]
            handled = False
            for pattern, replacement, attr in options:
                if matches(pattern, line):
                    current_value = {'flags_option': flags_option, 'flags_enable_option': flags_enable_option, 'flags_allow_empty_option': flags_allow_empty_option, 'flags_xkb_disable_option': flags_xkb_disable_option}[attr]
                    if not current_value:
                        out.append(replacement)
                    if attr == 'flags_option': flags_option = True
                    elif attr == 'flags_enable_option': flags_enable_option = True
                    elif attr == 'flags_allow_empty_option': flags_allow_empty_option = True
                    else: flags_xkb_disable_option = True
                    handled = True
                    break
            if handled:
                continue
            if matches(r'^\s*EndSection', line):
                if not flags_option: out.append('    Option "AutoAddDevices" "false"')
                if not flags_enable_option: out.append('    Option "AutoEnableDevices" "true"')
                if not flags_allow_empty_option: out.append('    Option "AllowEmptyInput" "false"')
                if not flags_xkb_disable_option: out.append('    Option "XkbDisable" "false"')
                out.append(line)
                in_flags = False
                continue
            out.append(line)
            continue

        if in_layout:
            if matches(r'^\s*EndSection', line):
                if not screen_seen:
                    out.append(f'    Screen 0 "{chosen_screen}"')
                out.append('    InputDevice "YabroKeyboard" "CoreKeyboard"')
                out.append('    InputDevice "YabroMouse" "CorePointer"')
                out.append('')
                out.append(line)
                in_layout = False
                continue
            if matches(r'^\s*Identifier\s+', line) or matches(r'^\s*InputDevice\s+', line):
                continue
            if matches(r'^\s*Screen\s+', line):
                screen_seen = True
            out.append(line)
            continue

        if matches(r'^\s*Section\s+"Files"', line):
            in_files = saw_files = True
            out.append(line)
            continue
        if matches(r'^\s*Section\s+"Module"', line):
            in_module = saw_module = True
            out.append(line)
            continue
        if matches(r'^\s*Section\s+"ServerFlags"', line):
            in_flags = saw_flags = True
            flags_option = flags_enable_option = flags_allow_empty_option = flags_xkb_disable_option = False
            out.append(line)
            continue
        if matches(r'^\s*Section\s+"ServerLayout"', line):
            if not saw_layout:
                saw_layout = True
                in_layout = True
                screen_seen = False
                out.append(line)
                out.append('    Identifier "YabroLayout"')
            else:
                skip_layout = True
            continue
        out.append(line)

    if not saw_files:
        out.extend(['', 'Section "Files"', '    ModulePath "/usr/lib/xorg/modules"', 'EndSection'])
    if not saw_module:
        out.extend(['', 'Section "Module"', '    Load "evdev"', 'EndSection'])
    if not saw_flags:
        out.extend(['', 'Section "ServerFlags"', '    Option "AutoAddDevices" "false"', '    Option "AutoEnableDevices" "true"', '    Option "AllowEmptyInput" "false"', '    Option "XkbDisable" "false"', 'EndSection'])
    if not saw_layout:
        out.extend(['', 'Section "ServerLayout"', '    Identifier "YabroLayout"', f'    Screen 0 "{chosen_screen}"', '    InputDevice "YabroKeyboard" "CoreKeyboard"', '    InputDevice "YabroMouse" "CorePointer"', 'EndSection'])
    return out

def strip_yabro_input(lines: list[str]) -> list[str]:
    out: list[str] = []
    index = 0
    while index < len(lines):
        if not matches(r'^\s*Section\s+"InputDevice"(?:\s|$)', lines[index]):
            out.append(lines[index])
            index += 1
            continue
        start = index
        section: list[str] = [lines[index]]
        index += 1
        while index < len(lines):
            section.append(lines[index])
            ended = matches(r'^\s*EndSection(?:\s|$)', lines[index])
            index += 1
            if ended:
                break
        device_id = ""
        for line in section:
            if matches(r'^\s*Identifier\s+"', line):
                device_id = identifier(line)
                break
        if device_id not in ("YabroMouse", "YabroKeyboard"):
            out.extend(section)
    return out

def read_lines(path: str) -> list[str]:
    return Path(path).read_text(encoding='utf-8', errors='replace').splitlines()

def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print('usage: xorg_config.py screen-id FILE | layout SCREEN_ID FILE | strip-input FILE', file=sys.stderr)
        return 2
    command = argv[1]
    if command == 'screen-id' and len(argv) == 3:
        print(screen_id(read_lines(argv[2])))
    elif command == 'layout' and len(argv) == 4:
        print('\n'.join(transform_layout(read_lines(argv[3]), argv[2])))
    elif command == 'strip-input' and len(argv) == 3:
        print('\n'.join(strip_yabro_input(read_lines(argv[2]))))
    else:
        print('invalid xorg_config.py arguments', file=sys.stderr)
        return 2
    return 0

if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
