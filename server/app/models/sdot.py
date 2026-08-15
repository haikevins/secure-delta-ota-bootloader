from __future__ import annotations

from dataclasses import dataclass
import hashlib
import struct
import zlib

SDOT_MAGIC = 0x544F4453
SDOT_FORMAT_VERSION = 1
SDOT_FIXED_HEADER_SIZE = 120
SDOT_EXTENSION_MAGIC = 0x31584353
SDOT_EXTENSION_VERSION = 1
SDOT_EXTENSION_SIZE = 20
SDOT_HEADER_SIZE = 140
SDOT_SIGNATURE_SIZE = 64

SDOT_IMAGE_FULL = 1
SDOT_IMAGE_DELTA = 2
SDOT_HASH_SHA256 = 1
SDOT_SIGNATURE_ECDSA_P256 = 1
APPLICATION_ADDRESS = 0x08006000

_FIXED = struct.Struct("<IHHIIIIIIIII32s32sIHHHH")
_EXT = struct.Struct("<IHHIII")

assert _FIXED.size == SDOT_FIXED_HEADER_SIZE
assert _EXT.size == SDOT_EXTENSION_SIZE


@dataclass(frozen=True)
class SdotInfo:
    image_type: int
    product_id: int
    hardware_revision: int
    base_version: int
    target_version: int
    payload_size: int
    target_size: int
    target_address: int
    base_sha256: str
    target_sha256: str
    payload_crc32: int
    key_id: int
    base_size: int
    target_crc32: int
    signature_size: int
    container_size: int
    container_crc32: int
    container_sha256: str

    @property
    def kind(self) -> str:
        return "delta" if self.image_type == SDOT_IMAGE_DELTA else "full"


def parse_sdot(data: bytes) -> SdotInfo:
    if len(data) < SDOT_HEADER_SIZE + SDOT_SIGNATURE_SIZE:
        raise ValueError("SDOT container is too short")

    fixed = _FIXED.unpack_from(data, 0)
    ext = _EXT.unpack_from(data, SDOT_FIXED_HEADER_SIZE)

    (
        magic,
        format_version,
        header_size,
        product_id,
        hardware_revision,
        image_type,
        _flags,
        base_version,
        target_version,
        payload_size,
        target_size,
        target_address,
        base_hash,
        target_hash,
        payload_crc32,
        hash_algorithm,
        signature_algorithm,
        signature_size,
        _reserved,
    ) = fixed

    if magic != SDOT_MAGIC:
        raise ValueError("invalid SDOT magic")
    if format_version != SDOT_FORMAT_VERSION:
        raise ValueError("unsupported SDOT format version")
    if header_size != SDOT_HEADER_SIZE:
        raise ValueError("unsupported SDOT header size")
    if ext[0] != SDOT_EXTENSION_MAGIC:
        raise ValueError("invalid SCX1 extension magic")
    if ext[1] != SDOT_EXTENSION_VERSION or ext[2] != SDOT_EXTENSION_SIZE:
        raise ValueError("unsupported SCX1 extension")
    if hash_algorithm != SDOT_HASH_SHA256:
        raise ValueError("unsupported SDOT hash algorithm")
    if signature_algorithm != SDOT_SIGNATURE_ECDSA_P256:
        raise ValueError("unsupported SDOT signature algorithm")
    if signature_size != SDOT_SIGNATURE_SIZE:
        raise ValueError("unsupported SDOT signature size")
    if image_type not in (SDOT_IMAGE_FULL, SDOT_IMAGE_DELTA):
        raise ValueError("unsupported SDOT image type")
    if target_version <= 0:
        raise ValueError("SDOT target version must be positive")
    if image_type == SDOT_IMAGE_FULL and base_version != 0:
        raise ValueError("full SDOT must have base_version=0")
    if image_type == SDOT_IMAGE_DELTA:
        if base_version <= 0 or target_version <= base_version:
            raise ValueError("delta SDOT requires 0 < base_version < target_version")
    if target_address != APPLICATION_ADDRESS:
        raise ValueError("SDOT target address mismatch")

    total = header_size + payload_size + signature_size
    if total != len(data):
        raise ValueError("SDOT total length mismatch")

    payload = data[header_size:header_size + payload_size]
    if (zlib.crc32(payload) & 0xFFFFFFFF) != payload_crc32:
        raise ValueError("SDOT payload CRC32 mismatch")

    key_id = ext[3]
    base_size = ext[4]
    target_crc32 = ext[5]
    if key_id == 0:
        raise ValueError("SDOT key_id must be nonzero")
    if image_type == SDOT_IMAGE_FULL and base_size != 0:
        raise ValueError("full SDOT must have base_size=0")
    if image_type == SDOT_IMAGE_DELTA and base_size < 8:
        raise ValueError("delta SDOT base_size is invalid")

    return SdotInfo(
        image_type=image_type,
        product_id=product_id,
        hardware_revision=hardware_revision,
        base_version=base_version,
        target_version=target_version,
        payload_size=payload_size,
        target_size=target_size,
        target_address=target_address,
        base_sha256=base_hash.hex(),
        target_sha256=target_hash.hex(),
        payload_crc32=payload_crc32,
        key_id=key_id,
        base_size=base_size,
        target_crc32=target_crc32,
        signature_size=signature_size,
        container_size=len(data),
        container_crc32=zlib.crc32(data) & 0xFFFFFFFF,
        container_sha256=hashlib.sha256(data).hexdigest(),
    )
