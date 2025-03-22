# Compile grug.so first: gcc grug/grug.c -shared -fPIC -o grug/grug.so

import ctypes
import json
import sys

class grug_error(ctypes.Structure):
    _fields_ = [
        ("path", ctypes.c_char * 4096),
        ("msg", ctypes.c_char * 420),
        ("has_changed", ctypes.c_bool),
        ("line_number", ctypes.c_int),
        ("grug_c_line_number", ctypes.c_int),
    ]

dll = ctypes.PyDLL("grug/grug.so")

def print_error(fn_name):
    error = grug_error.in_dll(dll, "grug_error")

    print(f"{error.path.decode()}:{error.line_number}: {error.msg.decode()} (detected by grug.c:{error.grug_c_line_number})", file=sys.stderr)

    raise SystemExit(f"{fn_name}() failed")

# Apply updated JSON to m60-gun.grug
grug_generate_file_from_json = dll.grug_generate_file_from_json
grug_generate_file_from_json.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
if grug_generate_file_from_json(b"dump.json", b"mods/vanilla/m60/m60-gun.grug"):
    print_error("grug_generate_file_from_json")
