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

# Dump m60.grug to dump.json
grug_dump_file_ast = dll.grug_dump_file_ast
grug_dump_file_ast.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
if grug_dump_file_ast(b"mods/vanilla/m60/m60.grug", b"dump.json"):
    print_error("grug_dump_file_ast")

# Load JSON
with open(b"dump.json", "r") as f:
    data = json.load(f)

# Double the RPM
rpm = int(data["entity"]["fields"][2]["value"]["value"])
rpm *= 2
data["entity"]["fields"][2]["value"]["value"] = str(rpm)

# Overwrite JSON
with open(b"dump.json", "w") as f:
    json.dump(data, f)

# Apply updated JSON to m60.grug
grug_apply_file_ast = dll.grug_apply_file_ast
grug_apply_file_ast.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
if grug_apply_file_ast(b"dump.json", b"mods/vanilla/m60/m60.grug"):
    print_error("grug_apply_file_ast")
