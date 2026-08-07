# Writes the bytes described by a hex string in IN to the file OUT.
#
# CMake can read a file as hex but has no way to write one, and the MIME check needs fixture files
# containing exact magic bytes. `file(WRITE)` cannot emit a NUL, so the bytes go out through a
# hex-encoded intermediate that this turns back into binary.

file(READ "${IN}" _hex)
string(STRIP "${_hex}" _hex)

string(LENGTH "${_hex}" _length)
math(EXPR _bytes "${_length} / 2")

set(_out "")
math(EXPR _last "${_bytes} - 1")
foreach(_i RANGE ${_last})
    math(EXPR _at "${_i} * 2")
    string(SUBSTRING "${_hex}" ${_at} 2 _byte)
    string(APPEND _out "\\x${_byte}")
endforeach()

# The one path that emits arbitrary bytes: printf understands the escapes, CMake's file(WRITE)
# does not.
execute_process(COMMAND printf "${_out}" OUTPUT_FILE "${OUT}")
