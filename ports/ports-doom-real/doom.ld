ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5) FILEHDR PHDRS;
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x0000000001dff000;

    .text : ALIGN(0x1000)
    {
        *(.text*)
    } :text

    . = ALIGN(0x1000);
    .rodata : ALIGN(0x1000)
    {
        *(.rodata*)
        *(.eh_frame*)
    } :rodata

    . = ALIGN(0x1000);
    .data : ALIGN(0x1000)
    {
        *(.data*)
    } :data

    .bss : ALIGN(0x1000)
    {
        *(COMMON)
        *(.bss*)
        . = ALIGN(0x1000);
    } :data

    /DISCARD/ :
    {
        *(.note.GNU-stack)
        *(.comment)
    }
}
