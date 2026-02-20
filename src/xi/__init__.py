import cppyy.ll
import ctypes
import os
from pathlib import Path

# Determine current file path
current_dir = Path(__file__).resolve().parent

# If installed via pip wheel, headers are packaged directly beside __init__.py inside "xi/"
if (current_dir / "include").exists():
    include_path = current_dir / "include"
    pkg_path = current_dir / "packages"
else:
    # If running locally from the git repository, step up to the root folder
    include_path = current_dir.parent.parent / "include"
    pkg_path = current_dir.parent.parent / "packages"

cppyy.add_include_path(str(include_path))
cppyy.c_include(str(pkg_path / "monocypher" / "monocypher.c"))

cppyy.include("Xi/String.hpp")
cppyy.include("Xi/InlineArray.hpp")
cppyy.include("Xi/Array.hpp")
cppyy.include("Rho/Tunnel.hpp")
cppyy.include("Rho/Railway.hpp")

# 2. Capture the REAL C++ Class
Xi = cppyy.gbl.Xi
String = Xi.String 

# ------------------------------------------------------------------
# MONKEY PATCH LOGIC
# ------------------------------------------------------------------

_orig_init = String.__init__

def String_init(self, arg=None):
    """Augmented constructor to handle Python bytes and strings"""
    _orig_init(self) # Call C++ allocator
    if arg is None: return

    # Normalize input to bytes
    b_data = None
    if isinstance(arg, (bytes, bytearray)):
        b_data = arg
    elif isinstance(arg, str):
        b_data = arg.encode('utf-8')
    elif hasattr(arg, 'size'):
        self.concat(arg)
        return

    if b_data is not None:
        # FIX: Create a ctypes buffer from bytes to get a valid address
        # We use from_buffer_copy to ensure we have a stable memory block
        c_buf = (ctypes.c_ubyte * len(b_data)).from_buffer_copy(b_data)
        addr = ctypes.addressof(c_buf)
        # Call the C++ helper with the integer address
        self.setFromRawAddress(addr, len(b_data))

def String_bytes(self):
    """FORCED binary export. Always returns the Python 'bytes' type."""
    sz = self.size()
    if sz == 0: return b""
    # Direct memory copy from C++ data() pointer to Python bytes object
    addr = cppyy.ll.cast['uintptr_t'](self.data())
    return ctypes.string_at(addr, sz)

def String_repr(self):
    """Beautiful print output using decimal bytes"""
    try:
        # bytes(self) calls String_bytes, then we decode it
        deci = bytes(self.toDeci()).decode('utf-8')
        return f"Xi::String({self.size()})[{deci}]"
    except:
        return f"Xi::String({self.size()})[binary]"

# String patches
String.__init__  = String_init
String.__bytes__ = String_bytes
String.__str__   = lambda self: bytes(self).decode('utf-8', 'replace')
String.__repr__  = String_repr

# Apply patches to Array template proxy
# Note: These are for Python-side convenience. 
# However, we must be careful not to break TemplateProxy.__getitem__.
# Xi.Array.__getitem__ = Array_getitem # Let's avoid these for now if not needed

# ------------------------------------------------------------------
# EXPORTS
# ------------------------------------------------------------------
RailwayStation = Xi.RailwayStation
Tunnel = Xi.Tunnel
Map = Xi.Map
Packet = Xi.Packet
RawCart = Xi.RawCart