ENTRY(kernel_main)

SECTIONS {
    . = 0xffffffff80000000;
    __kernel_start = .;

    .limine_requests : { KEEP(*(.limine_requests)) }
    .text : { *(.text*) . = ALIGN(0x1000); }
    .data : { *(.data*) . = ALIGN(0x1000); }
    .rodata : { *(.rodata*) . = ALIGN(0x1000); }
    .bss : { *(.bss*) *(COMMON) . = ALIGN(0x1000); }
    .got : { *(.got) . = ALIGN(0x1000); }
    .eh_frame : { *(.eh_frame) . = ALIGN(0x1000); }
    __kernel_end = .;
}
