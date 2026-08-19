ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(5); rodata PT_LOAD FLAGS(4); data PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x1e00000;
 .text : { *(.text*) } :text
 . = ALIGN(0x1000);
 .rodata : { *(.rodata*) } :rodata
 . = ALIGN(0x1000);
 .data : { *(.data*) } :data
 .bss : { *(.bss*) *(COMMON) } :data
}
