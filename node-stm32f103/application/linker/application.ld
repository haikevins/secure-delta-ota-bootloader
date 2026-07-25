/* Phase 1 TODO: complete application linker script. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08006000, LENGTH = 39K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 20K
}
