"""Single source of truth for the shader-manifest binary schema.

The native reader mirrors these values in platform/src/gx/vk_shader_manifest.h;
tools/tests/test_manifest_schema.py asserts that both stay in sync.

Record layout (little endian, no padding):

    header : magic u32, schema u32, build u32, count u32, headerCrc u32
    record : uid[UID_WORDS] u32, key[5] u32, flags u8,
             sourceLen u16, source[sourceLen] u8, recordCrc u32

Every CRC is FNV-1a 32 over the bytes that precede it.
"""

MAGIC_PTSU = 0x55535450
SCHEMA_VERSION = 3
BUILD_VERSION = 20261020
UID_WORDS = 100
KEY_WORDS = 5
MAX_SOURCE_BYTES = 512
HEADER_SIZE = 5 * 4
HEADER_CRC_BYTES = 4 * 4

assert UID_WORDS * 4 == 400, "ShaderUid must stay 400 bytes (100 words)"
