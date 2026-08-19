//Core C kernel services. - (c) Ayano4ka1338, 2026
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "vfs.h"

#define PAGE_SIZE 0x1000ULL
#define PAGE_MASK (~(PAGE_SIZE - 1ULL))
#define PTE_P 0x001ULL
#define PTE_W 0x002ULL
#define PTE_U 0x004ULL
#define PTE_PS 0x080ULL
#define PTE_NX (1ULL << 63)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

struct timespec_k {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile ("inb %1, %0" : "=a" (result) : "dN" (port));
    return result;
}
static inline void outb(uint16_t port, uint8_t data) {
    __asm__ volatile ("outb %0, %1" : : "a" (data), "dN" (port));
}
static inline void io_wait(void) { outb(0x80, 0); }
static inline void debugcon_putc(char c) { (void)c; }
static inline void debugcon_puts(const char *s) { (void)s; }
static inline void debug_hex64(const char *prefix, uint64_t value) { (void)prefix; (void)value; }

volatile uint64_t user_exec_pending = 0;

void exec_debug_iret_frame(const uint64_t *frame);
extern int vmm_debug_cr3_va(uint64_t cr3_phys, uint64_t va, const char *tag);
extern uint64_t syscall_user_cr3;

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt[8];
static struct gdt_ptr gp;

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct tss64 tss __attribute__((aligned(16)));
static uint8_t tss_ist_stack[16384] __attribute__((aligned(16)));
static uint8_t tss_df_stack[16384] __attribute__((aligned(16)));
static uint8_t tss_pf_stack[16384] __attribute__((aligned(16)));

static void set_tss_descriptor(int index, uint64_t base, uint32_t limit) {
    uint64_t lo = 0;
    lo |= (limit & 0xFFFFULL);
    lo |= (base & 0xFFFFFFULL) << 16;
    lo |= 0x89ULL << 40;
    lo |= ((limit >> 16) & 0xFULL) << 48;
    lo |= ((base >> 24) & 0xFFULL) << 56;
    *(uint64_t*)&gdt[index] = lo;
    *(uint64_t*)&gdt[index + 1] = (base >> 32);
}

void init_gdt() {
    gdt[0] = (struct gdt_entry){0};
    gdt[1] = (struct gdt_entry){0xFFFF, 0, 0, 0x9A, 0xAF, 0};
    gdt[2] = (struct gdt_entry){0xFFFF, 0, 0, 0x92, 0xCF, 0};
    gdt[3] = (struct gdt_entry){0xFFFF, 0, 0, 0xF2, 0xCF, 0};
    gdt[4] = (struct gdt_entry){0xFFFF, 0, 0, 0xFA, 0xAF, 0};
    gdt[5] = (struct gdt_entry){0xFFFF, 0, 0, 0xF2, 0xCF, 0};
    gdt[6] = (struct gdt_entry){0};
    gdt[7] = (struct gdt_entry){0};

    tss.rsp0 = (uint64_t)tss_ist_stack + sizeof(tss_ist_stack);
    tss.ist1 = (uint64_t)tss_ist_stack + sizeof(tss_ist_stack);
    tss.ist2 = (uint64_t)tss_df_stack + sizeof(tss_df_stack);
    tss.ist3 = (uint64_t)tss_pf_stack + sizeof(tss_pf_stack);
    tss.iomap_base = sizeof(tss);
    set_tss_descriptor(6, (uint64_t)&tss, sizeof(tss) - 1);

    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint64_t)gdt;
    __asm__ volatile ("lgdt %0" : : "m" (gp));
    __asm__ volatile (
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "retfq\n"
        "1:\n"
        "mov $0x30, %%ax\n"
        "ltr %%ax\n"
        : : : "rax", "memory"
    );
}

void init_fpu_sse(void) {
    uint64_t cr0, cr4;

    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0) :: "memory");

    cr0 &= ~(1ULL << 2);

    cr0 &= ~(1ULL << 3);
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0) : "memory");

    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4) :: "memory");

    cr4 |= (1ULL << 9) | (1ULL << 10);
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4) : "memory");

    __asm__ volatile ("fninit" ::: "memory");
}

#define IA32_STAR   0xC0000081u
#define IA32_LSTAR  0xC0000082u
#define IA32_FMASK  0xC0000084u
#define IA32_EFER   0xC0000080u
#define EFER_SCE    (1ULL << 0)

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr) : "memory");
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value, hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}
static void cpu_hardening_init(void) {
    uint32_t eax, ebx, ecx, edx, max_basic, max_ext;
    eax = 0; ecx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    max_basic = eax;

    uint64_t cr4;
    __asm__ volatile("mov %%cr4,%0" : "=r"(cr4));
    if (max_basic >= 7) {
        eax = 7; ecx = 0;
        __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
        if (ebx & (1u << 7)) {
            cr4 |= (1ULL << 20);
            __asm__ volatile("mov %0,%%cr4" :: "r"(cr4) : "memory");
            debugcon_puts("[HARDEN] SMEP=ON\n");
        } else {
            debugcon_puts("[HARDEN] SMEP=unsupported\n");
        }
    } else {
        debugcon_puts("[HARDEN] SMEP=unsupported\n");
    }

    eax = 0x80000000u; ecx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    max_ext = eax;
    if (max_ext >= 0x80000001u) {
        eax = 0x80000001u; ecx = 0;
        __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
        if (edx & (1u << 20)) {
            uint64_t efer = rdmsr(IA32_EFER);
            wrmsr(IA32_EFER, efer | (1ULL << 11));
            debugcon_puts("[HARDEN] NXE=ON\n");
        } else {
            debugcon_puts("[HARDEN] NX=unsupported\n");
        }
    } else {
        debugcon_puts("[HARDEN] NX=unsupported\n");
    }
}

extern void syscall_entry_asm(void);
extern void console_fb_putc(uint8_t ch);
extern void console_fb_set_cursor(uint64_t col, uint64_t row);
extern uint64_t console_fb_get_cursor(void);

static void init_syscall_msrs(void) {
    uint64_t star = ((uint64_t)0x13 << 48) | ((uint64_t)0x08 << 32);
    uint64_t efer = rdmsr(IA32_EFER);
    wrmsr(IA32_EFER, efer | EFER_SCE);
    wrmsr(IA32_STAR, star);
    wrmsr(IA32_LSTAR, (uint64_t)syscall_entry_asm);
    wrmsr(IA32_FMASK, (1ULL << 9) | (1ULL << 8) | (1ULL << 10));
}

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1

void pic_remap(void) {
    outb(PIC1_COMMAND, 0x11); io_wait();
    outb(PIC2_COMMAND, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 4); io_wait();
    outb(PIC2_DATA, 2); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

struct idt_entry {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t ist;
    uint8_t attributes;
    uint16_t isr_mid;
    uint32_t isr_high;
    uint32_t reserved;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct idt_entry idt[256];
struct idtr idtp;

void set_id_gate(int num, uint64_t base, uint16_t sel, uint8_t flags, uint8_t ist) {
    idt[num].isr_low = base & 0xFFFF;
    idt[num].kernel_cs = sel;
    idt[num].ist = ist & 0x7;
    idt[num].attributes = flags;
    idt[num].isr_mid = (base >> 16) & 0xFFFF;
    idt[num].isr_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].reserved = 0;
}

extern void isr0(void);
extern void isr1(void);
extern void isr4(void);
extern void isr6(void);
extern void isr8(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void keyboard_isr_stub(void);
extern void syscall_isr_stub(void);

void init_idt(void) {
    uint64_t base = (uint64_t)&idt;
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = base;

    set_id_gate(0, (uint64_t)isr0, 0x08, 0x8E, 0);
    set_id_gate(1, (uint64_t)isr1, 0x08, 0x8E, 0);
    set_id_gate(4, (uint64_t)isr4, 0x08, 0x8E, 0);
    set_id_gate(6, (uint64_t)isr6, 0x08, 0x8E, 1);
    set_id_gate(8, (uint64_t)isr8, 0x08, 0x8E, 1);
    set_id_gate(10, (uint64_t)isr10, 0x08, 0x8E, 1);
    set_id_gate(11, (uint64_t)isr11, 0x08, 0x8E, 1);
    set_id_gate(12, (uint64_t)isr12, 0x08, 0x8E, 1);
    set_id_gate(13, (uint64_t)isr13, 0x08, 0x8E, 1);
    set_id_gate(14, (uint64_t)isr14, 0x08, 0x8E, 3);
    set_id_gate(0x21, (uint64_t)keyboard_isr_stub, 0x08, 0x8E, 0);
    set_id_gate(0x80, (uint64_t)syscall_isr_stub, 0x08, 0xEE, 0);

    __asm__ volatile ("lidt %0" : : "m" (idtp) : "memory");
}

void isr_handler(uint64_t num, uint64_t error_code, uint64_t rip, uint64_t cs, uint64_t rflags) {
    uint64_t cr2 = 0, cr3 = 0;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));

    debugcon_puts("[EXCEPTION] Vector=0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (num >> (i * 4)) & 0xF;
        debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    debugcon_puts(" Error=0x");

    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (error_code >> (i*4)) & 0xF;
        debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }

    debugcon_puts(" CR2=0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (cr2 >> (i*4)) & 0xF;
        debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    debugcon_puts(" CR3=0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (cr3 >> (i*4)) & 0xF;
        debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    debugcon_puts(" RIP=0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (rip >> (i*4)) & 0xF;
        debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    debugcon_puts(" CS=0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (cs >> (i*4)) & 0xF;
        debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    debugcon_puts(" RFLAGS=0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (rflags >> (i*4)) & 0xF;
        debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    debugcon_puts("\n");
    while(1) __asm__ volatile ("cli; hlt");
}

void exception_raw_audit(const uint64_t *sp) {
    if (!sp) return;
    debug_hex64("[EXC-RAW] q0(vector)=0x", sp[0]);
    debug_hex64("[EXC-RAW] q1(error)=0x", sp[1]);
    debug_hex64("[EXC-RAW] q2(RIP)=0x", sp[2]);
    debug_hex64("[EXC-RAW] q3(CS)=0x", sp[3]);
    debug_hex64("[EXC-RAW] q4(RFLAGS)=0x", sp[4]);
    debug_hex64("[EXC-RAW] q5(RSP)=0x", sp[5]);
    debug_hex64("[EXC-RAW] q6(SS)=0x", sp[6]);
}

__asm__ (
".global isr0\n"
"isr0:\n"
"    pushq $0\n"
"    pushq $0\n"
"    jmp isr_common\n"
".global isr1\n"
"isr1:\n"
"    pushq $0\n"
"    pushq $1\n"
"    jmp isr_common\n"
".global isr4\n"
"isr4:\n"
"    pushq $0\n"
"    pushq $4\n"
"    jmp isr_common\n"
".global isr6\n"
"isr6:\n"
"    pushq $0\n"
"    pushq $6\n"
"    jmp isr_common\n"
".global isr8\n"
"isr8:\n"
"    pushq $8\n"
"    jmp isr_common\n"
".global isr10\n"
"isr10:\n"
"    pushq $10\n"
"    jmp isr_common\n"
".global isr11\n"
"isr11:\n"
"    pushq $11\n"
"    jmp isr_common\n"
".global isr12\n"
"isr12:\n"
"    pushq $12\n"
"    jmp isr_common\n"
".global isr13\n"
"isr13:\n"
"    pushq $13\n"
"    jmp isr_common\n"
".global isr14\n"
"isr14:\n"
"    pushq $14\n"
"    jmp isr_common\n"
"isr_common:\n"
"    cld\n"
"    pushq %rax\n"
"    pushq %rbx\n"
"    pushq %rcx\n"
"    pushq %rdx\n"
"    pushq %rsi\n"
"    pushq %rdi\n"
"    pushq %rbp\n"
"    pushq %r8\n"
"    pushq %r9\n"
"    pushq %r10\n"
"    pushq %r11\n"
"    pushq %r12\n"
"    pushq %r13\n"
"    pushq %r14\n"
"    pushq %r15\n"
"    movq %rsp, %rdi\n"
"    addq $120, %rdi\n"
"    call exception_raw_audit\n"
"    movq 120(%rsp), %rdi\n"
"    movq 128(%rsp), %rsi\n"
"    movq 136(%rsp), %rdx\n"
"    movq 144(%rsp), %rcx\n"
"    movq 152(%rsp), %r8\n"
"    call isr_handler\n"
"    popq %r15\n"
"    popq %r14\n"
"    popq %r13\n"
"    popq %r12\n"
"    popq %r11\n"
"    popq %r10\n"
"    popq %r9\n"
"    popq %r8\n"
"    popq %rbp\n"
"    popq %rdi\n"
"    popq %rsi\n"
"    popq %rdx\n"
"    popq %rcx\n"
"    popq %rbx\n"
"    popq %rax\n"
"    addq $16, %rsp\n"
"    iretq\n"
);

static volatile uint8_t key_buffer = 0;
uint8_t shift_pressed = 0;
uint8_t shift_left_pressed = 0;
uint8_t shift_right_pressed = 0;
uint8_t caps_lock = 0;

void keyboard_handler(void) {
    uint8_t scancode = inb(0x60);

    if (scancode == 0x2A) {
        shift_left_pressed = 1;
        shift_pressed = 1;
        goto eoi;
    }
    if (scancode == 0x36) {
        shift_right_pressed = 1;
        shift_pressed = 1;
        goto eoi;
    }
    if (scancode == 0xAA) {
        shift_left_pressed = 0;
        shift_pressed = shift_right_pressed;
        goto eoi;
    }
    if (scancode == 0xB6) {
        shift_right_pressed = 0;
        shift_pressed = shift_left_pressed;
        goto eoi;
    }
    if (scancode == 0x3A) {
        caps_lock = caps_lock ? 0 : 1;
        goto eoi;
    }

    if (scancode & 0x80) {
        goto eoi;
    }

    uint8_t key = 0;
    if (scancode == 0x01) key = 27;
    else if (scancode == 0x39) key = ' ';
    else key = scancode;

    if (key != 0) {
        key_buffer = scancode;
    }
eoi:
    outb(PIC1_COMMAND, 0x20);
}

__asm__ (
".global keyboard_isr_stub\n"
"keyboard_isr_stub:\n"
"    push %rax\n"
"    push %rcx\n"
"    push %rdx\n"
"    push %r8\n"
"    push %r9\n"
"    push %r10\n"
"    push %r11\n"
"    call keyboard_handler\n"
"    pop %r11\n"
"    pop %r10\n"
"    pop %r9\n"
"    pop %r8\n"
"    pop %rdx\n"
"    pop %rcx\n"
"    pop %rax\n"
"    iretq\n"
);

void setup_interrupts(void) {
    __asm__ volatile ("cli" ::: "memory");
    init_gdt();
    cpu_hardening_init();
    init_syscall_msrs();
    pic_remap();
    init_idt();

    outb(PIC1_DATA, 0xFD);
    outb(PIC2_DATA, 0xFF);
    io_wait();

    __asm__ volatile ("sti" ::: "memory");
}

uint8_t get_key(void) {
    uint8_t key = key_buffer;
    key_buffer = 0;
    return key;
}

static inline uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg | 0x80);
    io_wait();
    uint8_t val = inb(0x71);
    outb(0x70, reg & 0x7F);
    return val;
}
static void cmos_wait_update(void) {
    int timeout = 10000;
    while ((cmos_read(0x0A) & 0x80) && timeout--) {
        io_wait();
    }
    cmos_read(0x0C);
}
void get_rtc_time(char *buf, size_t bufsize) {
    __asm__ volatile ("cli");
    cmos_wait_update();
    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour   = cmos_read(0x04);
    uint8_t status_b = cmos_read(0x0B);
    if (!(status_b & 0x04)) {
        second = (second & 0x0F) + ((second >> 4) * 10);
        minute = (minute & 0x0F) + ((minute >> 4) * 10);
        hour   = ((hour & 0x0F) + (((hour & 0x70) >> 4) * 10)) | (hour & 0x80);
    }
    if (!(status_b & 0x02)) {
        if (hour & 0x80) {
            hour = ((hour & 0x7F) + 12) % 24;
        } else if (hour == 12) {
            hour = 0;
        }
    }
    if (bufsize >= 9) {
        buf[0] = '0' + (hour / 10);
        buf[1] = '0' + (hour % 10);
        buf[2] = ':';
        buf[3] = '0' + (minute / 10);
        buf[4] = '0' + (minute % 10);
        buf[5] = ':';
        buf[6] = '0' + (second / 10);
        buf[7] = '0' + (second % 10);
        buf[8] = '\0';
    }
    __asm__ volatile ("sti");
}

#define MAX_FILES 32
#define MAX_TASKS 8

struct file_desc {
    int in_use;
    int type;
    int flags;
    uint64_t offset;
    uint64_t size;
    uint8_t *data;
    const char *name;
    void *private_data;
    struct vfs_node *vnode;
    uint64_t *shared_offset;
};

struct open_desc_state { uint64_t offset; uint32_t refs; int used; };
static struct open_desc_state open_descs[MAX_FILES];
static uint64_t *alloc_open_offset(void) { for (int i=0;i<MAX_FILES;i++) if(!open_descs[i].used){open_descs[i].used=1;open_descs[i].refs=1;open_descs[i].offset=0;return &open_descs[i].offset;} return NULL; }
static void retain_open_offset(uint64_t *p) { if(!p)return; struct open_desc_state *s=(struct open_desc_state*)((uint8_t*)p - __builtin_offsetof(struct open_desc_state, offset)); s->refs++; }
static void release_open_offset(uint64_t *p) { if(!p)return; struct open_desc_state *s=(struct open_desc_state*)((uint8_t*)p - __builtin_offsetof(struct open_desc_state, offset)); if(s->refs && --s->refs==0)s->used=0; }
static void fd_drop_resources(struct file_desc *f);
static int64_t sys_dup3(int oldfd,int newfd,int flags);
static uint64_t fd_offset_get(struct file_desc *f){ return f->shared_offset ? *f->shared_offset : f->offset; }
static void fd_offset_set(struct file_desc *f,uint64_t v){ f->offset=v; if(f->shared_offset)*f->shared_offset=v; }

#define FD_FILE 1
#define FD_PIPE_R 2
#define FD_PIPE_W 3
#define FD_PSEUDO 4
#define FD_SOCKET 5
#define FD_EVENTFD 6
#define FD_TIMERFD 7
#define EAFNOSUPPORT 97
#define EOPNOTSUPP 95
#define EADDRINUSE 98
#define ECONNREFUSED 111
#define ENOTCONN 107
#define DEV_NULL 1
#define DEV_ZERO 2
#define DEV_CONSOLE 3
#define DEV_TTY 4
#define DEV_RANDOM 5
#define DEV_URANDOM 6
#define PSEUDO_PROC_FILE 100
#define PSEUDO_PROC_DIR 101
#define PSEUDO_DEV_DIR 102
#define PSEUDO_SYS_DIR 103
#define PSEUDO_PROC_ROOT 104
#define PSEUDO_PROC_SELF 105
#define PSEUDO_PROC_FD 108
#define PSEUDO_DEV_FILE 106
#define PSEUDO_SYS_FILE 107

#define PIPE_CAP 4096
#define SOCK_CAP 4096
struct unix_socket_state {
    uint8_t data[2][SOCK_CAP];
    uint32_t rpos[2], wpos[2], count[2];
    uint8_t closed[2];
    uint32_t refs;
    uint8_t listener;
    uint8_t bound;
    uint8_t connected;
    uint8_t pending_count;
    int pending_fd[4];
    char path[108];
};
static struct unix_socket_state unix_sockets[8];
struct pipe_state {
    uint8_t data[PIPE_CAP];
    uint32_t rpos, wpos, count;
    uint32_t readers, writers;
};
static struct pipe_state pipes[8];
struct eventfd_state_k { uint64_t value; uint32_t flags; };
struct timerfd_state_k { int clockid; uint32_t flags; uint64_t interval_ns; uint64_t next_ns; uint64_t expirations; };
static struct eventfd_state_k eventfds[8];
static struct timerfd_state_k timerfds[8];

static int current_task = 0;
static struct file_desc task_fd_tables[MAX_TASKS][MAX_FILES];
static struct file_desc *current_files(void) { return task_fd_tables[current_task]; }
#define files (current_files())
static uint8_t __attribute__((section(".data"))) test_file_data[] = "Hello from test file!\n";
static uint8_t pseudo_buf[MAX_FILES][4096];
static char current_exe[128] = "/HELLO.ELF";
extern uint64_t pmm_free_count(void);
extern uint64_t bootfs_file_size(const char *path);
extern size_t bootfs_read_root_file(const char *path, uint64_t offset, uint8_t *dst, size_t len);

static uint32_t pseudo_rand_state = 0x13579bdfU;
extern int64_t sys_getpid(void);
extern int64_t sys_getppid(void);
static void rtc_now(uint64_t *sec, uint64_t *nsec);
static int pseudo_eq(const char *a, const char *b) { while (*a && *b && *a==*b) { ++a; ++b; } return *a==*b; }
static size_t pseudo_append(char *dst, size_t cap, size_t n, const char *s) {
    while (*s && n + 1 < cap) dst[n++] = *s++;
    if (cap) dst[n < cap ? n : cap - 1] = 0;
    return n;
}
static size_t pseudo_append_u64(char *dst, size_t cap, size_t n, uint64_t v) {
    char b[24]; size_t i=0;
    if (!v) { if (n+1<cap) dst[n++]='0'; return n; }
    while (v && i<sizeof(b)) { b[i++]=(char)('0'+(v%10)); v/=10; }
    while (i && n+1<cap) dst[n++]=b[--i];
    return n;
}
static uint64_t monotonic_boot_tsc = 0;
static uint64_t monotonic_boot_rtc_sec = 0;
static uint64_t monotonic_tsc_hz = 1000000000ULL;
static int monotonic_ready = 0;

static uint64_t read_tsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t cpuid_tsc_hz(void) {
    uint32_t a,b,c,d,maxleaf;
    __asm__ volatile("cpuid" : "=a"(maxleaf), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    if (maxleaf >= 0x15) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x15), "c"(0));
        if (a && b && c) {
            uint64_t hz=((uint64_t)c*(uint64_t)b)/(uint64_t)a;
            if(hz>=100000000ULL&&hz<=10000000000ULL)return hz;
        }
    }
    if (maxleaf >= 0x16) {
        __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x16), "c"(0));
        if (a) {
            uint64_t hz=(uint64_t)a*1000000ULL;
            if(hz>=100000000ULL&&hz<=10000000000ULL)return hz;
        }
    }
    return 0;
}

static void monotonic_init(void) {
    uint64_t s,n;
    rtc_now(&s,&n);
    monotonic_boot_rtc_sec=s;
    monotonic_tsc_hz=cpuid_tsc_hz();
    if(!monotonic_tsc_hz) monotonic_tsc_hz=1000000000ULL;
    monotonic_boot_tsc=read_tsc();
    monotonic_ready=1;
}

uint64_t kernel_monotonic_ns(void) {
    if(!monotonic_ready) monotonic_init();
    uint64_t now_sec, now_nsec;
    rtc_now(&now_sec,&now_nsec);
    (void)now_nsec;
    if(now_sec < monotonic_boot_rtc_sec) return 0;
    uint64_t sec=now_sec-monotonic_boot_rtc_sec;
    uint64_t frac=0;

    if(sec==0 && monotonic_tsc_hz) {
        uint64_t delta=read_tsc()-monotonic_boot_tsc;
        frac=(delta%monotonic_tsc_hz)*1000000000ULL/monotonic_tsc_hz;
        if(frac>999999999ULL) frac=999999999ULL;
    }
    return sec*1000000000ULL+frac;
}

static size_t pseudo_make_proc(char *dst, size_t cap, const char *path) {
    size_t n=0; if (!cap) return 0; dst[0]=0;
    if (pseudo_eq(path,"/proc/cpuinfo")) {
        n=pseudo_append(dst,cap,n,"processor\t: 0\nvendor_id\t: YabroOS-32\ncpu family\t: 6\nmodel name\t: YabroOS-32 x86_64\nflags\t\t: syscall nx sse2\n\n");
    } else if (pseudo_eq(path,"/proc/meminfo")) {
        uint64_t freep=pmm_free_count(), total=64ULL*1024ULL*1024ULL, freeb=freep*4096ULL;
        n=pseudo_append(dst,cap,n,"MemTotal:       "); n=pseudo_append_u64(dst,cap,n,total/1024); n=pseudo_append(dst,cap,n," kB\nMemFree:        "); n=pseudo_append_u64(dst,cap,n,freeb/1024); n=pseudo_append(dst,cap,n," kB\nMemAvailable:   "); n=pseudo_append_u64(dst,cap,n,freeb/1024); n=pseudo_append(dst,cap,n," kB\nBuffers:        0 kB\nCached:         0 kB\n");
    } else if (pseudo_eq(path,"/proc/uptime")) {
        uint64_t up_ns=kernel_monotonic_ns();
        uint64_t up_sec=up_ns/1000000000ULL, up_frac=(up_ns%1000000000ULL)/10000000ULL;
        n=pseudo_append_u64(dst,cap,n,up_sec);
        if(n+3<cap){dst[n++]='.';dst[n++]=(char)('0'+up_frac/10);dst[n++]=(char)('0'+up_frac%10);}
        n=pseudo_append(dst,cap,n," 0.00\n");
    } else if (pseudo_eq(path,"/proc/version")) {
        n=pseudo_append(dst,cap,n,"YabroOS-32 version 0.0.1-alpha (x86_64) #1 PREEMPT\n");
    } else if (pseudo_eq(path,"/proc/self/status") || pseudo_eq(path,"/proc/1/status")) {
        n=pseudo_append(dst,cap,n,"Name:\tYabroOS-32\nState:\tR (running)\nPid:\t"); n=pseudo_append_u64(dst,cap,n,(uint64_t)sys_getpid()); n=pseudo_append(dst,cap,n,"\nPPid:\t"); n=pseudo_append_u64(dst,cap,n,(uint64_t)sys_getppid()); n=pseudo_append(dst,cap,n,"\nTgid:\t"); n=pseudo_append_u64(dst,cap,n,(uint64_t)sys_getpid()); n=pseudo_append(dst,cap,n,"\nUid:\t0\t0\t0\t0\nGid:\t0\t0\t0\t0\nThreads:\t1\n");
    } else if (pseudo_eq(path,"/proc/self/cmdline") || pseudo_eq(path,"/proc/1/cmdline")) {
        const char *e=current_exe; const char *slash=e; while (*e) { if (*e=='/') slash=e+1; e++; } n=pseudo_append(dst,cap,n,slash); if (n+1<cap) dst[n++]=0;
    } else if (pseudo_eq(path,"/proc/self/maps") || pseudo_eq(path,"/proc/1/maps")) {
        n=pseudo_append(dst,cap,n,"00400000-00401000 r-xp 00000000 00:00 1\t"); n=pseudo_append(dst,cap,n,current_exe); n=pseudo_append(dst,cap,n,"\n007ff000-00800000 rw-p 00000000 00:00 0\t[stack]\n");
    } else return 0;
    return n;
}
static int pseudo_lookup(const char *p, int *type, int *kind) {
    if (!p || !type || !kind) return 0; *type=0; *kind=0;
    struct vfs_node *n=vfs_lookup(p);
    if (n) { *type=FD_PSEUDO; switch(vfs_type(n)) {
        case VFS_NODE_DIR:
            if (pseudo_eq(p,"/proc")) *kind=PSEUDO_PROC_ROOT;
            else if (pseudo_eq(p,"/proc/self") || pseudo_eq(p,"/proc/1")) *kind=PSEUDO_PROC_SELF;
            else if (pseudo_eq(p,"/proc/self/fd") || pseudo_eq(p,"/proc/1/fd")) *kind=PSEUDO_PROC_FD;
            else if (pseudo_eq(p,"/dev")) *kind=PSEUDO_DEV_DIR; else return 0; return 1;
        case VFS_NODE_PROC_FILE: *kind=PSEUDO_PROC_FILE; return 1;
        case VFS_NODE_DEV_NULL: *kind=DEV_NULL; return 1;
        case VFS_NODE_DEV_ZERO: *kind=DEV_ZERO; return 1;
        case VFS_NODE_DEV_CONSOLE: *kind=DEV_CONSOLE; return 1;
        case VFS_NODE_DEV_TTY: *kind=DEV_TTY; return 1;
        case VFS_NODE_DEV_RANDOM: *kind=DEV_RANDOM; return 1;
        case VFS_NODE_DEV_URANDOM: *kind=DEV_URANDOM; return 1;
        case VFS_NODE_SYMLINK:
            if (pseudo_eq(p,"/proc/self/exe") || pseudo_eq(p,"/proc/1/exe")) { *kind=PSEUDO_PROC_FILE; return 1; }
            if (pseudo_eq(p,"/dev/stdin")) { *kind=DEV_TTY; return 1; }
            if (pseudo_eq(p,"/dev/stdout") || pseudo_eq(p,"/dev/stderr")) { *kind=DEV_CONSOLE; return 1; }
            return 0;
        default: return 0; }}
    if (pseudo_eq(p,"/sys")) { *type=FD_PSEUDO; *kind=PSEUDO_SYS_DIR; return 1; }
    if (pseudo_eq(p,"/sys/kernel/osrelease")) { *type=FD_PSEUDO; *kind=PSEUDO_SYS_FILE; return 1; }
    return 0;
}
static int pseudo_is_dir_kind(int k) { return k==PSEUDO_PROC_ROOT || k==PSEUDO_PROC_SELF || k==PSEUDO_PROC_FD || k==PSEUDO_DEV_DIR || k==PSEUDO_SYS_DIR; }
static int pseudo_fill(struct file_desc *f, const char *path, int kind) {
    f->type=FD_PSEUDO; f->private_data=(void*)(uintptr_t)kind; f->vnode=vfs_lookup(path); f->offset=0; f->size=0; f->data=NULL;
    if (kind==PSEUDO_PROC_FILE) { f->data=pseudo_buf[(f-files)%MAX_FILES]; f->size=pseudo_make_proc((char*)f->data,4096,path); }
    else if (kind==PSEUDO_SYS_FILE) { static const uint8_t s[]="0.0.1-alpha-yabroos-32\n"; f->data=(uint8_t*)s; f->size=sizeof(s)-1; }
    return 0;
}

void init_files(void) {
    monotonic_init();
    vfs_init();
    vfs_debug_dump();
    for (int ti = 0; ti < MAX_TASKS; ti++) {
        for (int i = 0; i < MAX_FILES; i++) {
            task_fd_tables[ti][i].in_use = 0;
            task_fd_tables[ti][i].type = 0;
            task_fd_tables[ti][i].flags = 0;
            task_fd_tables[ti][i].offset = 0;
            task_fd_tables[ti][i].size = 0;
            task_fd_tables[ti][i].data = NULL;
            task_fd_tables[ti][i].name = NULL;
            task_fd_tables[ti][i].private_data = NULL;
            task_fd_tables[ti][i].vnode = NULL;
            task_fd_tables[ti][i].shared_offset = NULL;
        }
    }
    for (int i = 0; i < MAX_FILES; i++) {
        open_descs[i].used = 0; open_descs[i].refs = 0; open_descs[i].offset = 0;
    }

    files[0].in_use = 1;
    files[0].type = FD_PSEUDO;
    files[0].private_data = (void *)(uintptr_t)DEV_TTY;

    files[1].in_use = 1;
    files[1].type = FD_PSEUDO;
    files[1].private_data = (void *)(uintptr_t)DEV_CONSOLE;

    files[2].in_use = 1;
    files[2].type = FD_PSEUDO;
    files[2].private_data = (void *)(uintptr_t)DEV_CONSOLE;

    debugcon_puts("[V10] stdio fds initialized: 0=tty 1=console 2=console\\n");
}

int find_free_fd(void) {
    for (int i = 3; i < MAX_FILES; i++) {
        if (!files[i].in_use) return i;
    }
    return -1;
}

#define USER_LIMIT 0x0000800000000000ULL

#define USER_MMAP_MIN 0x0000000010000000ULL
#define USER_MMAP_MAX 0x0000700000000000ULL
#define USER_HEAP_BASE 0x0000000000500000ULL
#define USER_HEAP_MAX  0x000000000F000000ULL
#define EACCES_K 13
#define E2BIG 7

static int user_addr_range_ok(uint64_t addr, uint64_t len) {
    if (!len || addr >= USER_LIMIT) return 0;
    if (len > USER_LIMIT - addr) return 0;
    return 1;
}

static int user_prot_wx(uint64_t prot) {
    return (prot & 2ULL) && (prot & 4ULL);
}

#define USER_STACK_TOP 0x0000000000800000ULL
#define USER_STACK_SIZE 0x00010000ULL

extern uint64_t pmm_hhdm(void);
extern uint64_t pmm_alloc_page(void);
extern void pmm_free_page(uint64_t phys);
extern void *pmm_phys_to_virt(uint64_t phys);

#define COM1 0x3F8
#define VGA_PHYS 0x000B8000ULL
#define VGA_COLS 80
#define VGA_ROWS 25
static uint16_t *console_vga;
static uint16_t console_cursor;
static bool console_serial_ready;

static void console_serial_init(void) {
    if (console_serial_ready) return;
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
    console_serial_ready = true;
}

static void console_serial_putc(char c) {
    console_serial_init();
    while ((inb(COM1 + 5) & 0x20) == 0) { }
    outb(COM1, (uint8_t)c);
}

static void console_vga_init(void) {
    if (console_vga) return;
    console_vga = (uint16_t *)pmm_phys_to_virt(VGA_PHYS);
    if (!console_vga) return;
    for (uint64_t i = 0; i < (uint64_t)VGA_COLS * VGA_ROWS; ++i)
        console_vga[i] = (uint16_t)(0x07 << 8) | ' ';
    console_cursor = 0;
}

static void console_vga_scroll(void) {
    if (!console_vga) return;
    for (uint64_t row = 1; row < VGA_ROWS; ++row)
        for (uint64_t col = 0; col < VGA_COLS; ++col)
            console_vga[(row - 1) * VGA_COLS + col] = console_vga[row * VGA_COLS + col];
    for (uint64_t col = 0; col < VGA_COLS; ++col)
        console_vga[(VGA_ROWS - 1) * VGA_COLS + col] = (uint16_t)(0x07 << 8) | ' ';
    console_cursor = (VGA_ROWS - 1) * VGA_COLS;
}

static void console_vga_putc(char c) {
    console_vga_init();
    if (!console_vga) return;
    uint16_t row = console_cursor / VGA_COLS;
    uint16_t col = console_cursor % VGA_COLS;
    if (c == '\r') { console_cursor = row * VGA_COLS; return; }
    if (c == '\n') {
        console_cursor = (row + 1) * VGA_COLS;
        if (console_cursor >= VGA_COLS * VGA_ROWS) console_vga_scroll();
        return;
    }
    if (c == '\b') {
        if (col) { --console_cursor; console_vga[console_cursor] = (uint16_t)(0x07 << 8) | ' '; }
        return;
    }
    console_vga[console_cursor++] = (uint16_t)(0x07 << 8) | (uint8_t)c;
    if (console_cursor >= VGA_COLS * VGA_ROWS) console_vga_scroll();
}

static void console_putc(char c) {

    debugcon_putc(c);
    console_serial_putc(c);
}

static uint64_t user_cr3 = 0;
uint64_t kernel_cr3 = 0;

static uint64_t get_cr3(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PAGE_MASK;
}

static inline void invlpg(uint64_t addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(addr) : "memory");
}

static inline void load_cr3(uint64_t cr3) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(cr3 & PAGE_MASK) : "memory");
}

static inline uint8_t *phys_bytes(uint64_t phys) {
    uint64_t hhdm = pmm_hhdm();
    return hhdm ? (uint8_t *)(hhdm + phys) : (uint8_t *)phys;
}

static inline uint64_t *phys_table(uint64_t phys) {
    return (uint64_t *)phys_bytes(phys);
}

static uint64_t alloc_zero_page(void) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    debug_hex64("[VMM] zero page phys=0x", phys);
    uint8_t *p = phys_bytes(phys);
    debug_hex64("[VMM] zero page VA=0x", (uint64_t)p);
    if (!p) { debugcon_puts("[VMM] zero page HHDM pointer NULL\n"); return 0; }
    debugcon_puts("[VMM] zero page write begin\n");
    for (size_t i = 0; i < PAGE_SIZE; ++i) p[i] = 0;
    debugcon_puts("[VMM] zero page write OK\n");
    return phys;
}

extern int vmm_user_space_create(void);
extern int vmm_user_space_clone(uint64_t src_cr3, uint64_t *dst_cr3_out);
extern int vmm_user_activate(uint64_t cr3);
extern uint64_t vmm_user_cr3(void);
extern int vmm_user_map(uint64_t virt, uint64_t phys, uint64_t flags);
extern int vmm_user_translate(uint64_t virt, uint64_t *phys_out, uint64_t *pte_out);
extern int vmm_user_range_ok(uint64_t virt, uint64_t len, int write);
extern int vmm_user_unmap(uint64_t virt, uint64_t *phys_out);
extern int vmm_user_protect(uint64_t virt, uint64_t flags);

static uint64_t user_lookup_pte(uint64_t cr3, uint64_t va) {
    (void)cr3;
    uint64_t phys = 0, pte = 0;
    if (vmm_user_translate(va, &phys, &pte) != 0) return 0;
    return pte;
}

static int user_range_ok(uint64_t va, uint64_t len, int write) {
    return vmm_user_range_ok(va, len, write);
}

static int create_user_space(void) {
    __asm__ volatile ("cli" ::: "memory");
    debugcon_puts("[VMM] Creating user space via dedicated VMM...\n");
    if (vmm_user_space_create() != 0) {
        debugcon_puts("[VMM] user space creation failed\n");
        return -1;
    }
    kernel_cr3 = get_cr3();
    user_cr3 = vmm_user_cr3();
    debug_hex64("[VMM] Kernel CR3=0x", kernel_cr3);
    debug_hex64("[VMM] User CR3=0x", user_cr3);
    return user_cr3 ? 0 : -1;
}

static int map_user_range(uint64_t cr3, uint64_t start, uint64_t end, uint32_t p_flags) {
    (void)cr3;
    if (end <= start || end > USER_LIMIT) return -1;
    uint64_t first = start & PAGE_MASK;
    uint64_t last = (end + PAGE_SIZE - 1) & PAGE_MASK;
    uint64_t flags = 0;
    if (p_flags & 2) flags |= PTE_W;
    if (!(p_flags & 1)) flags |= PTE_NX;

    for (uint64_t va = first; va < last; va += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return -1;
        if (vmm_user_map(va, phys, flags) != 0) {
            debugcon_puts("[ELF] vmm_user_map failed\n");
            return -1;
        }
        uint8_t *p = (uint8_t *)pmm_phys_to_virt(phys);
        if (!p) return -1;
        for (size_t i = 0; i < PAGE_SIZE; ++i) p[i] = 0;
    }
    return 0;
}

static int map_user_stack(uint64_t cr3) {
    (void)cr3;
    uint64_t base = USER_STACK_TOP - USER_STACK_SIZE;

    for (uint64_t va = base; va < USER_STACK_TOP; va += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return -1;
        if (vmm_user_map(va, phys, PTE_W | PTE_NX) != 0) {
            debugcon_puts("[ELF] user stack map failed\n");
            return -1;
        }
        uint8_t *p = (uint8_t *)pmm_phys_to_virt(phys);
        if (!p) return -1;
        for (size_t i = 0; i < PAGE_SIZE; ++i) p[i] = 0;
    }
    return 0;
}

static int copy_to_user(uint64_t cr3, uint64_t dst_va, const uint8_t *src, uint64_t len) {

    if (cr3 == 0 || cr3 != vmm_user_cr3()) return -14;
    if (!src && len) return -14;
    while (len) {
        uint64_t page = dst_va & PAGE_MASK;
        uint64_t off = dst_va & (PAGE_SIZE - 1);
        uint64_t n = PAGE_SIZE - off;
        if (n > len) n = len;
        uint64_t phys = 0, pte = 0;
        if (vmm_user_translate(page, &phys, &pte) != 0) return -14;
        if (!(pte & PTE_U) || !(pte & PTE_W)) return -14;
        uint8_t *dst = (uint8_t *)pmm_phys_to_virt(phys);
        if (!dst) return -14;
        dst += off;
        for (uint64_t i = 0; i < n; ++i) dst[i] = src[i];
        dst_va += n;
        src += n;
        len -= n;
    }
    return 0;
}

static int copy_to_user_loader(uint64_t cr3, uint64_t dst_va, const uint8_t *src, uint64_t len) {
    if (cr3 == 0 || cr3 != vmm_user_cr3()) return -14;
    if (!src && len) return -14;
    while (len) {
        uint64_t page = dst_va & PAGE_MASK;
        uint64_t off = dst_va & (PAGE_SIZE - 1);
        uint64_t n = PAGE_SIZE - off;
        if (n > len) n = len;
        uint64_t phys = 0, pte = 0;
        if (vmm_user_translate(page, &phys, &pte) != 0) return -14;
        if (!(pte & PTE_U) || !(pte & PTE_P)) return -14;
        uint8_t *dst = (uint8_t *)pmm_phys_to_virt(phys);
        if (!dst) return -14;
        dst += off;
        for (uint64_t i = 0; i < n; ++i) dst[i] = src[i];
        dst_va += n;
        src += n;
        len -= n;
    }
    return 0;
}

static int zero_user_loader(uint64_t cr3, uint64_t dst_va, uint64_t len) {
    if (cr3 == 0 || cr3 != vmm_user_cr3()) return -14;
    while (len) {
        uint64_t page = dst_va & PAGE_MASK;
        uint64_t off = dst_va & (PAGE_SIZE - 1);
        uint64_t n = PAGE_SIZE - off;
        if (n > len) n = len;
        uint64_t phys = 0, pte = 0;
        if (vmm_user_translate(page, &phys, &pte) != 0) return -14;
        if (!(pte & PTE_U) || !(pte & PTE_P)) return -14;
        uint8_t *dst = (uint8_t *)pmm_phys_to_virt(phys);
        if (!dst) return -14;
        dst += off;
        for (uint64_t i = 0; i < n; ++i) dst[i] = 0;
        dst_va += n;
        len -= n;
    }
    return 0;
}

#define KERNEL_STACK_SIZE 16384
static uint8_t kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(4096)));
uint64_t kernel_rsp;
uint64_t user_rsp;
uint64_t syscall_user_rsp;
uint64_t syscall_user_cr3;
uint64_t syscall_saved_rax;
uint64_t syscall_saved_r12;
uint64_t syscall_saved_r13;
uint64_t syscall_user_rip;
uint64_t syscall_user_rflags;

uint64_t syscall_return_frame[5] __attribute__((aligned(16)));
uint64_t user_return_rsp;
volatile uint64_t user_exit_pending;
volatile int64_t user_exit_status;
static volatile uint64_t musl_syscall_trace_count;

void exec_debug_iret_frame(const uint64_t *frame) { (void)frame; }

extern void enter_usermode(uint64_t rip, uint64_t rsp, uint64_t cr3) __attribute__((noreturn));
extern void exec_enter_usermode(uint64_t rip, uint64_t rsp, uint64_t cr3) __attribute__((noreturn));
extern void syscall_handler(void);
extern int kernel_execve_file(const char *path, uint64_t *entry_out, uint64_t *stack_out);

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t user_rip, user_rflags, user_rsp, user_cr3;
} syscall_frame_t;

void syscall_return_audit(const syscall_frame_t *f) { (void)f; }

_Static_assert(__builtin_offsetof(syscall_frame_t, user_rip) == 120, "syscall user rip offset");
_Static_assert(__builtin_offsetof(syscall_frame_t, user_rflags) == 128, "syscall user rflags offset");
_Static_assert(__builtin_offsetof(syscall_frame_t, user_rsp) == 136, "syscall user rsp offset");
_Static_assert(__builtin_offsetof(syscall_frame_t, user_cr3) == 144, "syscall user cr3 offset");

#define TASK_UNUSED 0
#define TASK_RUNNABLE 1
#define TASK_RUNNING 2
#define TASK_BLOCKED 3
#define TASK_ZOMBIE 4
#define CLONE_VM 0x00000100ULL
#define CLONE_FS 0x00000200ULL
#define CLONE_FILES 0x00000400ULL
#define CLONE_SIGHAND 0x00000800ULL
#define CLONE_THREAD 0x00010000ULL
#define CLONE_SETTLS 0x00080000ULL
#define CLONE_PARENT_SETTID 0x00100000ULL
#define CLONE_CHILD_CLEARTID 0x00200000ULL

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rsp, rip, rflags, cr3;
    uint64_t fs_base, gs_base;
} task_context_t;

_Static_assert(__builtin_offsetof(task_context_t, rsp) == 120, "task rsp offset");
_Static_assert(__builtin_offsetof(task_context_t, rip) == 128, "task rip offset");
_Static_assert(__builtin_offsetof(task_context_t, rflags) == 136, "task rflags offset");
_Static_assert(__builtin_offsetof(task_context_t, cr3) == 144, "task cr3 offset");

typedef struct { uint64_t sig[16]; } sigset_t;
typedef void (*sighandler_t)(int);
typedef struct { sighandler_t sa_handler; unsigned long sa_flags; void (*sa_restorer)(void); sigset_t sa_mask; } sigaction_t;

typedef struct {
    int used;
    int state;
    int pid, ppid, tgid;
    int exit_status;
    uint64_t tid_address;
    uint64_t futex_addr;
    int pipe_wait_fd;
    uint64_t pipe_wait_buf;
    uint64_t pipe_wait_count;

    int wait4_active;
    int wait4_pid;
    uint64_t wait4_status_ptr;
    task_context_t ctx;
    uint64_t sig_pending[2];
    uint64_t sig_blocked[2];
    sigaction_t sig_actions[65];
    syscall_frame_t sig_saved_frame;
    int sig_saved_valid;
    int sig_active;
    struct vfs_node *cwd;
} task_t;

static task_t tasks[MAX_TASKS];
static int next_pid = 100;
volatile int task_switch_pending = 0;
static volatile int task_switch_target = 0;
task_context_t *task_return_ctx = NULL;
static uint64_t fs_base, gs_base;

static uint64_t task_read_fs_base(void) {
    if (current_task >= 0 && current_task < MAX_TASKS && tasks[current_task].used)
        return tasks[current_task].ctx.fs_base;
    return fs_base;
}
static uint64_t task_read_gs_base(void) {
    if (current_task >= 0 && current_task < MAX_TASKS && tasks[current_task].used)
        return tasks[current_task].ctx.gs_base;
    return gs_base;
}

static void task_fd_release(struct file_desc *f) {
    if (!f || !f->in_use) return;
    if (f->shared_offset) release_open_offset(f->shared_offset);
    f->shared_offset = NULL;
    f->in_use = 0;
    f->private_data = NULL;
}

static void task_fd_table_release(task_t *t) {
    if (!t) return;
    int ti = (int)(t - tasks);
    for (int fd = 0; fd < MAX_FILES; fd++) {
        struct file_desc *f = &task_fd_tables[ti][fd];
        if (!f->in_use) continue;
        if (f->type == FD_PIPE_R || f->type == FD_PIPE_W) {
            struct pipe_state *p = (struct pipe_state *)f->private_data;
            if (p) {
                if (f->type == FD_PIPE_R && p->readers) p->readers--;
                if (f->type == FD_PIPE_W && p->writers) p->writers--;
            }
        }
        task_fd_release(f);
    }
}

static void task_fd_table_clone(task_t *parent, task_t *child) {
    int pi = (int)(parent - tasks);
    int ci = (int)(child - tasks);
    for (int fd = 0; fd < MAX_FILES; fd++) {
        struct file_desc *src = &task_fd_tables[pi][fd];
        struct file_desc *dst = &task_fd_tables[ci][fd];
        *dst = *src;
        if (!src->in_use) continue;
        if (dst->shared_offset) retain_open_offset(dst->shared_offset);
        if (dst->type == FD_PIPE_R) {
            struct pipe_state *p = (struct pipe_state *)dst->private_data;
            if (p) p->readers++;
        } else if (dst->type == FD_PIPE_W) {
            struct pipe_state *p = (struct pipe_state *)dst->private_data;
            if (p) p->writers++;
        }
    }
}

static void task_save_from_frame(task_t *t, const syscall_frame_t *f) {
    t->ctx.rax=f->rax; t->ctx.rbx=f->rbx; t->ctx.rcx=f->rcx; t->ctx.rdx=f->rdx;
    t->ctx.rsi=f->rsi; t->ctx.rdi=f->rdi; t->ctx.rbp=f->rbp; t->ctx.r8=f->r8;
    t->ctx.r9=f->r9; t->ctx.r10=f->r10; t->ctx.r11=f->r11; t->ctx.r12=f->r12;
    t->ctx.r13=f->r13; t->ctx.r14=f->r14; t->ctx.r15=f->r15;
    t->ctx.rsp=f->user_rsp; t->ctx.rip=f->user_rip;
    t->ctx.rflags=f->user_rflags; t->ctx.cr3=f->user_cr3;
    t->ctx.fs_base=task_read_fs_base(); t->ctx.gs_base=task_read_gs_base();
}

static void task_init_root(void) {
    if (!tasks[0].used) { tasks[0].used=1; tasks[0].state=TASK_RUNNING; tasks[0].pid=1;
        tasks[0].ppid=0; tasks[0].tgid=1; tasks[0].cwd=vfs_root(); tasks[0].pipe_wait_fd=-1; tasks[0].pipe_wait_buf=0; tasks[0].pipe_wait_count=0;
        tasks[0].wait4_active=0; tasks[0].wait4_pid=-1; tasks[0].wait4_status_ptr=0; }
}

static int task_pick_next(void) {
    for (int n=1;n<MAX_TASKS;n++) { int i=(current_task+n)%MAX_TASKS;
        if (tasks[i].used && tasks[i].state==TASK_RUNNABLE) return i; }
    return -1;
}

#define ENOSYS 38
#define EBADF  9
#define EINVAL 22
#define EFAULT 14
#define ENOMEM 12
#define ENOENT 2
#define EEXIST 17
#define EROFS 30
#define ENOTEMPTY 39
#define ENAMETOOLONG 36
#define EMFILE 24
#define ESPIPE 29
#define EPIPE 32
#define ENOTTY 25
#define ERANGE 34
#define EAGAIN 11
#define ECHILD 10
#define ESRCH 3
#define ENOTDIR 20
#define EISDIR 21
#define ETIMEDOUT 110
#define ENOSPC 28
#define EIO 5

int64_t sys_exit_group(int status);
int64_t sys_exit(int status);
int64_t sys_write(int fd, const void *buf, uint64_t count);
int64_t sys_read(int fd, void *buf, uint64_t count);
int64_t sys_open(const char *path, int flags, int mode);
int64_t sys_mkdir(const char *path, int mode); int64_t sys_rename(const char *oldpath, const char *newpath);
static int resolve_process_path(const char *in, char *out, size_t cap);
int64_t sys_unlink(const char *path);
int64_t sys_rmdir(const char *path);
int64_t sys_getdents64(int fd, void *dirent, uint64_t count);
int64_t sys_readlink(const char *path, char *buf, uint64_t size);
int64_t sys_readlinkat(int dirfd, const char *path, char *buf, uint64_t size);
int64_t sys_close(int fd);
int64_t sys_stat(const char *path, void *st);
int64_t sys_fstat(int fd, void *st);
int64_t sys_lseek(int fd, int64_t off, int whence);
int64_t sys_pread64(int fd, void *buf, uint64_t count, uint64_t off);
int64_t sys_readv(int fd, const void *iov, int iovcnt);
int64_t sys_writev(int fd, const void *iov, int iovcnt);
int64_t sys_access(const char *path, int mode);
int64_t sys_pipe(int *fds);
int64_t sys_dup(int oldfd);
int64_t sys_dup2(int oldfd, int newfd);
int64_t sys_fcntl(int fd, int cmd, uint64_t arg);
int64_t sys_ioctl(int fd, uint64_t req, uint64_t arg);
int64_t sys_mmap(uint64_t addr, uint64_t len, uint64_t prot, uint64_t flags, int fd, uint64_t off);
int64_t sys_munmap(uint64_t addr, uint64_t len);
int64_t sys_mprotect(uint64_t addr, uint64_t len, uint64_t prot);
int64_t sys_mremap(uint64_t old_addr, uint64_t old_size, uint64_t new_size, uint64_t flags, uint64_t new_addr);
int64_t sys_brk(uint64_t addr);
int64_t sys_getpid(void);
int64_t sys_getppid(void);
int64_t sys_gettid(void);
int64_t sys_getuid(void);
int64_t sys_geteuid(void);
int64_t sys_getgid(void);
int64_t sys_getegid(void);
int64_t sys_set_tid_address(int *tidptr);
int64_t sys_arch_prctl(uint64_t code, uint64_t addr);
int64_t sys_uname(void *buf);
int64_t sys_gettimeofday(void *tv, void *tz);
int64_t sys_clock_gettime(int id, void *tp);
int64_t sys_clock_getres(int clkid, void *tp);
int64_t sys_getrandom(void *buf, uint64_t len, uint32_t flags);
int64_t sys_getcwd(char *buf, uint64_t size);
int64_t sys_chdir(const char *path);
int64_t sys_poll(void *fds, uint64_t nfds, int timeout);
int64_t sys_select(int nfds, void *rfds, void *wfds, void *efds, void *tv);
int64_t sys_sysinfo(void *ui);
int64_t sys_lstat(const char *path, void *st);
int64_t sys_openat(int dirfd, const char *path, int flags, int mode);
int64_t sys_newfstatat(int dirfd, const char *path, void *st, int flags);
int64_t sys_pipe2(int *fds, int flags);
int64_t sys_faccessat(int dirfd, const char *path, int mode);
int64_t sys_getrlimit(int resource, void *rlim);
int64_t sys_prlimit64(int pid, int resource, const void *new_lim, void *old_lim);
int64_t sys_umask(uint64_t mask);
int64_t sys_prctl(int option, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_epoll_create1(int flags);
int64_t sys_epoll_ctl(int epfd, int op, int fd, void *event);
int64_t sys_epoll_wait(int epfd, void *events, int maxevents, int timeout);
int64_t sys_ftruncate(int fd, uint64_t len);
int64_t sys_clone(syscall_frame_t *f, uint64_t flags, uint64_t child_stack, uint64_t ptid, uint64_t ctid, uint64_t tls);
int64_t sys_fork(syscall_frame_t *f);
int64_t sys_wait4(syscall_frame_t *f, int pid, int *status, int options, void *rusage);
int64_t sys_sched_yield(syscall_frame_t *f);
int64_t sys_execve(const char *path, const char *const *argv, const char *const *envp);
int64_t sys_futex(syscall_frame_t *f, uint32_t *uaddr, int op, uint32_t val, const void *timeout, uint32_t *uaddr2, uint32_t val3);
int64_t sys_nanosleep(const void *req, void *rem);
int64_t sys_sched_setaffinity(int pid, uint64_t cpusetsize, const void *mask);
int64_t sys_sched_getaffinity(int pid, uint64_t cpusetsize, void *mask);
int64_t sys_set_robust_list(void *head, uint64_t len);
int64_t sys_get_robust_list(int pid, void **head_ptr, uint64_t *len_ptr);
int64_t sys_getcpu(unsigned *cpu, unsigned *node, void *tcache);
int64_t sys_rt_sigreturn(syscall_frame_t *f);
int64_t sys_rt_sigpending(sigset_t *set, uint64_t sigsetsize);
int64_t sys_kill(int pid, int sig);
int64_t sys_tgkill(int tgid, int tid, int sig);

static uint64_t musl_robust_head = 0;
static uint64_t musl_robust_len = 0;

typedef struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
} sockaddr_t;

typedef struct sockaddr_in {
    short sin_family;
    unsigned short sin_port;
    unsigned long sin_addr;
    char sin_zero[8];
} sockaddr_in_t;

typedef struct sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
} sockaddr_un_t;

static int user_copy_from(uint8_t *dst, uint64_t src, uint64_t len);
static int user_copy_to(uint64_t dst, const uint8_t *src, uint64_t len);

static int64_t socket_alloc_endpoint(struct unix_socket_state *s, int side) {
    int fd=find_free_fd(); if(fd<0)return-EMFILE;
    files[fd].in_use=1; files[fd].type=FD_SOCKET; files[fd].flags=0;
    files[fd].offset=0; files[fd].size=0; files[fd].data=NULL; files[fd].name=NULL;
    files[fd].vnode=NULL; files[fd].shared_offset=NULL;
    files[fd].private_data=(void*)(((uintptr_t)s) | (uintptr_t)(side & 1));
    s->refs++;
    return fd;
}
static struct unix_socket_state *socket_state(struct file_desc *f){ return (struct unix_socket_state *)(f->private_data ? ((uintptr_t)f->private_data & ~1ULL) : 0); }
static int socket_side(struct file_desc *f){ return (int)((uintptr_t)f->private_data & 1ULL); }

int64_t sys_socket(int domain, int type, int protocol) {
    (void)protocol;
    if(domain!=1 || (type & 0xf)!=1) return -EAFNOSUPPORT;
    static int si;
    struct unix_socket_state *s=NULL;
    for(int k=0;k<8;k++){ int j=(si+k)%8; if(!unix_sockets[j].refs){ s=&unix_sockets[j]; si=(j+1)%8; break; } }
    if(!s) return -EMFILE;
    *s=(struct unix_socket_state){0};
    return socket_alloc_endpoint(s,0);
}
int64_t sys_socketpair(int domain, int type, int protocol, int *sv) {
    (void)protocol;
    if(domain!=1 || (type & 0xf)!=1)return-EAFNOSUPPORT;
    if(!sv)return-EFAULT;
    static int si;
    struct unix_socket_state *s=&unix_sockets[si++%8];
    if(s->refs)return-EMFILE;
    *s=(struct unix_socket_state){0};
    int a=socket_alloc_endpoint(s,0); if(a<0)return a;
    int b=socket_alloc_endpoint(s,1); if(b<0){files[a].in_use=0;s->refs=0;return b;}
    if(user_copy_to((uint64_t)sv,(uint8_t*)&a,sizeof(a)) || user_copy_to((uint64_t)sv+4,(uint8_t*)&b,sizeof(b))){files[a].in_use=files[b].in_use=0;s->refs=0;return-EFAULT;}
    return 0;
}
static int socket_valid_fd(int fd){ return fd>=0&&fd<MAX_FILES&&files[fd].in_use&&files[fd].type==FD_SOCKET; }
static int socket_un_copy_path(const sockaddr_t *addr, int addrlen, char *out, size_t cap){
    if(!addr||!out||cap<2)return-EFAULT;
    if(addrlen < (int)(sizeof(unsigned short)+1) || addrlen > (int)sizeof(sockaddr_un_t))return-EINVAL;
    sockaddr_un_t u; if(user_copy_from((uint8_t*)&u,(uint64_t)addr,sizeof(u)))return-EFAULT;
    if(u.sun_family!=1)return-EAFNOSUPPORT;
    size_t n=0; while(n<sizeof(u.sun_path) && u.sun_path[n])n++;
    if(n==0 || n>=cap)return-EINVAL;
    for(size_t i=0;i<n;i++)out[i]=u.sun_path[i]; out[n]=0; return 0;
}
static int socket_find_listener(const char *path){
    for(int i=0;i<8;i++) if(unix_sockets[i].refs && unix_sockets[i].listener && unix_sockets[i].bound && pseudo_eq(unix_sockets[i].path,path)) return i;
    return -1;
}
int64_t sys_bind(int sockfd, const sockaddr_t *addr, int addrlen) {
    if(!socket_valid_fd(sockfd))return-EBADF;
    struct unix_socket_state *s=socket_state(&files[sockfd]); if(!s)return-ENOTCONN;
    if(s->bound || s->connected)return-EINVAL;
    char path[108]; int r=socket_un_copy_path(addr,addrlen,path,sizeof(path)); if(r<0)return r;
    if(socket_find_listener(path)>=0)return-EADDRINUSE;
    size_t n=0; while(path[n]&&n<sizeof(s->path)-1){s->path[n]=path[n];n++;} s->path[n]=0; s->bound=1; return 0;
}
int64_t sys_listen(int sockfd, int backlog) {
    if(!socket_valid_fd(sockfd))return-EBADF; if(backlog<0)return-EINVAL;
    struct unix_socket_state *s=socket_state(&files[sockfd]); if(!s)return-ENOTCONN; if(s->connected)return-EINVAL; if(!s->bound)return-EINVAL;
    s->listener=1; s->pending_count=0; for(int i=0;i<4;i++)s->pending_fd[i]=-1; return 0;
}
int64_t sys_connect(int sockfd, const sockaddr_t *addr, int addrlen) {
    if(!socket_valid_fd(sockfd))return-EBADF;
    struct unix_socket_state *old=socket_state(&files[sockfd]); if(!old)return-ENOTCONN;
    char path[108]; int r=socket_un_copy_path(addr,addrlen,path,sizeof(path)); if(r<0)return r;
    int li=socket_find_listener(path); if(li<0)return-ECONNREFUSED;
    struct unix_socket_state *ls=&unix_sockets[li]; if(ls->pending_count>=4)return-EAGAIN;
    static int si; int ni=-1; for(int k=0;k<8;k++){int j=(si+k)%8;if(j!=li&&!unix_sockets[j].refs){ni=j;si=(j+1)%8;break;}} if(ni<0)return-EMFILE;
    struct unix_socket_state *ns=&unix_sockets[ni]; *ns=(struct unix_socket_state){0};
    int afd=find_free_fd(); if(afd<0){*ns=(struct unix_socket_state){0};return-EMFILE;}
    files[afd].in_use=1; files[afd].type=FD_SOCKET; files[afd].flags=0; files[afd].offset=0; files[afd].size=0; files[afd].data=NULL; files[afd].name=NULL; files[afd].vnode=NULL; files[afd].shared_offset=NULL;
    files[afd].private_data=(void*)(((uintptr_t)ns)|1ULL); ns->refs=1;
    old->refs=0; *old=(struct unix_socket_state){0};
    files[sockfd].private_data=(void*)ns; ns->refs=2; ns->connected=1;
    ls->pending_fd[ls->pending_count++]=afd; return 0;
}
int64_t sys_accept(int sockfd, sockaddr_t *addr, int *addrlen) {
    if(!socket_valid_fd(sockfd))return-EBADF;
    struct unix_socket_state *ls=socket_state(&files[sockfd]); if(!ls||!ls->listener)return-EINVAL;
    if(ls->pending_count==0)return-EAGAIN;
    int fd=ls->pending_fd[0]; for(int i=1;i<ls->pending_count;i++)ls->pending_fd[i-1]=ls->pending_fd[i]; ls->pending_count--;
    if(addr && addrlen){ int al=0; if(user_copy_to((uint64_t)addrlen,(uint8_t*)&al,sizeof(al)))return-EFAULT; }
    return fd;
}
int64_t sys_sendto(int sockfd, const void *buf, int len, int flags, const sockaddr_t *dest_addr, int addrlen) {
    (void)dest_addr;(void)addrlen;
    if(sockfd<0||sockfd>=MAX_FILES||!files[sockfd].in_use||files[sockfd].type!=FD_SOCKET)return-EBADF;
    if(len<0)return-EINVAL; struct unix_socket_state*s=socket_state(&files[sockfd]); int side=socket_side(&files[sockfd]), peer=side^1;
    if(!s||s->refs<2)return-ENOTCONN; if(s->closed[peer])return-EPIPE; if(!len)return 0;
    uint32_t room=SOCK_CAP-s->count[peer]; if(!room)return (files[sockfd].flags&0x800)?-EAGAIN:0;
    uint32_t n=(uint32_t)len<room?(uint32_t)len:room; uint8_t tmp[256]; uint32_t done=0;
    while(done<n){uint32_t k=n-done;if(k>sizeof(tmp))k=sizeof(tmp);if(user_copy_from(tmp,(uint64_t)buf+done,k))return done?done:-EFAULT;for(uint32_t i=0;i<k;i++){s->data[peer][s->wpos[peer]]=tmp[i];s->wpos[peer]=(s->wpos[peer]+1)%SOCK_CAP;s->count[peer]++;}done+=k;}
    return done;
}
int64_t sys_recvfrom(int sockfd, void *buf, int len, int flags, sockaddr_t *src_addr, int *addrlen) {
    (void)flags;(void)src_addr;(void)addrlen;
    if(sockfd<0||sockfd>=MAX_FILES||!files[sockfd].in_use||files[sockfd].type!=FD_SOCKET)return-EBADF;
    if(len<0)return-EINVAL; struct unix_socket_state*s=socket_state(&files[sockfd]); int side=socket_side(&files[sockfd]);
    if(!s)return-ENOTCONN; if(!len)return 0; if(!s->count[side]){if(s->closed[side^1])return 0;return(files[sockfd].flags&0x800)?-EAGAIN:-EAGAIN;}
    uint32_t n=(uint32_t)len<s->count[side]?(uint32_t)len:s->count[side]; uint8_t tmp[256]; uint32_t done=0;
    while(done<n){uint32_t k=n-done;if(k>sizeof(tmp))k=sizeof(tmp);for(uint32_t i=0;i<k;i++){tmp[i]=s->data[side][s->rpos[side]];s->rpos[side]=(s->rpos[side]+1)%SOCK_CAP;s->count[side]--;}if(user_copy_to((uint64_t)buf+done,tmp,k))return done?done:-EFAULT;done+=k;}
    return done;
}
int64_t sys_shutdown(int sockfd, int how) {
    if(sockfd<0||sockfd>=MAX_FILES||!files[sockfd].in_use||files[sockfd].type!=FD_SOCKET)return-EBADF;
    if(how<0||how>2)return-EINVAL; struct unix_socket_state*s=socket_state(&files[sockfd]); if(!s)return-ENOTCONN; if(how==1||how==2)s->closed[socket_side(&files[sockfd])]=1; return 0;
}
int64_t sys_setsockopt(int sockfd, int level, int optname, const void *optval, int optlen) { (void)level;(void)optname;(void)optval;(void)optlen; if(sockfd<0||sockfd>=MAX_FILES||!files[sockfd].in_use||files[sockfd].type!=FD_SOCKET)return-EBADF; return 0; }
int64_t sys_getsockopt(int sockfd, int level, int optname, void *optval, int *optlen) { (void)level;(void)optname;(void)optval;(void)optlen; if(sockfd<0||sockfd>=MAX_FILES||!files[sockfd].in_use||files[sockfd].type!=FD_SOCKET)return-EBADF; return 0; }

static int user_copy_from(uint8_t *dst, uint64_t src, uint64_t len);
static int user_copy_to(uint64_t dst, const uint8_t *src, uint64_t len);
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIGCHLD 17
#define SIGUSR1 10
#define SIGTERM 15
#define SIGINT 2
static int sig_valid(int s){return s>0&&s<=64;}
static uint64_t sig_bit(int s){return 1ULL<<((s-1)&63);} static int sig_word(int s){return (s-1)>>6;}
static int sig_pending_set(task_t*t,int s){if(!sig_valid(s))return-EINVAL;t->sig_pending[sig_word(s)]|=sig_bit(s);return 0;}
static int sig_is_pending(task_t*t,int s){return (t->sig_pending[sig_word(s)]&sig_bit(s))!=0;}
static int sig_is_blocked(task_t*t,int s){return (t->sig_blocked[sig_word(s)]&sig_bit(s))!=0;}
static void sig_clear_pending(task_t*t,int s){t->sig_pending[sig_word(s)]&=~sig_bit(s);}
static int sig_next_deliverable(task_t*t){for(int s=1;s<=64;s++)if(sig_is_pending(t,s)&&!sig_is_blocked(t,s))return s;return 0;}
int64_t sys_rt_sigaction(int sig,const sigaction_t*act,sigaction_t*oldact,uint64_t sz){task_init_root();if(!sig_valid(sig)||sig==9||sz!=sizeof(sigset_t))return-EINVAL;task_t*t=&tasks[current_task];if(oldact){if(!user_range_ok((uint64_t)oldact,sizeof(sigaction_t),1))return-EFAULT;if(user_copy_to((uint64_t)oldact,(const uint8_t*)&t->sig_actions[sig],sizeof(sigaction_t)))return-EFAULT;}if(act){if(!user_range_ok((uint64_t)act,sizeof(sigaction_t),0))return-EFAULT;sigaction_t a;if(user_copy_from((uint8_t*)&a,(uint64_t)act,sizeof(a)))return-EFAULT;t->sig_actions[sig]=a;}return 0;}
int64_t sys_rt_sigprocmask(int how,const sigset_t*set,sigset_t*oldset,uint64_t sz){task_init_root();if(sz!=sizeof(sigset_t)||how<0||how>2)return-EINVAL;task_t*t=&tasks[current_task];sigset_t old={{t->sig_blocked[0],t->sig_blocked[1]}};if(oldset){if(!user_range_ok((uint64_t)oldset,sizeof(old),1))return-EFAULT;if(user_copy_to((uint64_t)oldset,(const uint8_t*)&old,sizeof(old)))return-EFAULT;}if(set){if(!user_range_ok((uint64_t)set,sizeof(*set),0))return-EFAULT;sigset_t in;if(user_copy_from((uint8_t*)&in,(uint64_t)set,sizeof(in)))return-EFAULT;if(how==SIG_BLOCK){t->sig_blocked[0]|=in.sig[0];t->sig_blocked[1]|=in.sig[1];}else if(how==SIG_UNBLOCK){t->sig_blocked[0]&=~in.sig[0];t->sig_blocked[1]&=~in.sig[1];}else{t->sig_blocked[0]=in.sig[0];t->sig_blocked[1]=in.sig[1];}}return 0;}
int64_t sys_rt_sigpending(sigset_t*set,uint64_t sz){task_init_root();if(!set||sz!=sizeof(sigset_t)||!user_range_ok((uint64_t)set,sizeof(sigset_t),1))return-EFAULT;sigset_t p={{tasks[current_task].sig_pending[0],tasks[current_task].sig_pending[1]}};return user_copy_to((uint64_t)set,(const uint8_t*)&p,sizeof(p))?-EFAULT:0;}
int64_t sys_kill(int pid,int sig){task_init_root();if((!sig_valid(sig))&&sig!=0)return-EINVAL;for(int i=0;i<MAX_TASKS;i++)if(tasks[i].used){if((pid>0&&(tasks[i].pid==pid||tasks[i].tgid==pid))||(pid==0&&tasks[i].tgid==tasks[current_task].tgid)){if(sig==0)return 0;return sig_pending_set(&tasks[i],sig);}}return-ESRCH;}
int64_t sys_tgkill(int tgid,int tid,int sig){task_init_root();if((!sig_valid(sig))&&sig!=0)return-EINVAL;for(int i=0;i<MAX_TASKS;i++)if(tasks[i].used&&tasks[i].tgid==tgid&&tasks[i].pid==tid){if(sig==0)return 0;return sig_pending_set(&tasks[i],sig);}return-ESRCH;}
static void deliver_pending_signal(syscall_frame_t*f){task_t*t=&tasks[current_task];if(t->sig_active)return;int sig=sig_next_deliverable(t);if(!sig)return;sighandler_t h=t->sig_actions[sig].sa_handler;if(h==SIG_IGN){sig_clear_pending(t,sig);return;}if(h==SIG_DFL||(uint64_t)h>=USER_LIMIT||!t->sig_actions[sig].sa_restorer){sig_clear_pending(t,sig);t->exit_status=128+sig;t->state=TASK_ZOMBIE;return;}uint64_t rsp=f->user_rsp;if(rsp<8||!user_range_ok(rsp-8,8,1))return;t->sig_saved_frame=*f;t->sig_saved_valid=1;t->sig_active=1;sig_clear_pending(t,sig);uint64_t rest=(uint64_t)t->sig_actions[sig].sa_restorer;if(user_copy_to(rsp-8,(const uint8_t*)&rest,8)){t->sig_saved_valid=0;t->sig_active=0;return;}f->user_rsp=rsp-8;f->user_rip=(uint64_t)h;f->rdi=(uint64_t)sig;f->rax=0;}
int64_t sys_rt_sigreturn(syscall_frame_t*f){task_init_root();task_t*t=&tasks[current_task];if(!t->sig_active||!t->sig_saved_valid)return-EINVAL;syscall_frame_t x=t->sig_saved_frame;t->sig_saved_valid=0;t->sig_active=0;*f=x;return 0;}

static uint64_t clock_now_ns_simple(int clockid) {
    if(clockid==1||clockid==4)return kernel_monotonic_ns();
    uint64_t sec=0,nsec=0; rtc_now(&sec,&nsec); return sec*1000000000ULL+nsec;
}

int64_t sys_eventfd2(uint32_t initval, int flags) {
    int fd=find_free_fd(); if(fd<0)return-EMFILE;
    for(int i=0;i<8;i++) if(eventfds[i].flags==0 && eventfds[i].value==0) {
        eventfds[i].value=initval; eventfds[i].flags=(uint32_t)(flags|1u);
        files[fd].in_use=1; files[fd].type=FD_EVENTFD; files[fd].flags=flags; files[fd].private_data=&eventfds[i];
        return fd;
    }
    return -EMFILE;
}

int64_t sys_timerfd_create(int clockid, int flags) {
    if(clockid!=0 && clockid!=1)return-EINVAL;
    int fd=find_free_fd(); if(fd<0)return-EMFILE;
    for(int i=0;i<8;i++) if(timerfds[i].flags==0 && timerfds[i].next_ns==0 && timerfds[i].expirations==0) {
        timerfds[i].clockid=clockid; timerfds[i].flags=1; timerfds[i].interval_ns=0; timerfds[i].next_ns=0; timerfds[i].expirations=0;
        files[fd].in_use=1; files[fd].type=FD_TIMERFD; files[fd].flags=flags; files[fd].private_data=&timerfds[i];
        return fd;
    }
    return -EMFILE;
}

struct itimerspec_k { struct timespec_k it_interval; struct timespec_k it_value; };
int64_t sys_timerfd_settime(int fd, int flags, const struct itimerspec_k *new_value, struct itimerspec_k *old_value) {
    (void)flags;
    if(fd<0||fd>=MAX_FILES||!files[fd].in_use||files[fd].type!=FD_TIMERFD)return-EBADF;
    if(!new_value)return-EFAULT;
    struct itimerspec_k n;
    if(user_copy_from((uint8_t*)&n,(uint64_t)new_value,sizeof(n)))return-EFAULT;
    struct timerfd_state_k*t=(struct timerfd_state_k*)files[fd].private_data;
    if(old_value){ struct itimerspec_k o={{0,0},{0,0}}; if(t->next_ns){uint64_t now=clock_now_ns_simple(t->clockid);uint64_t rem=t->next_ns>now?t->next_ns-now:0;o.it_value.tv_sec=(int64_t)(rem/1000000000ULL);o.it_value.tv_nsec=(int64_t)(rem%1000000000ULL);} o.it_interval.tv_sec=(int64_t)(t->interval_ns/1000000000ULL);o.it_interval.tv_nsec=(int64_t)(t->interval_ns%1000000000ULL);if(user_copy_to((uint64_t)old_value,(const uint8_t*)&o,sizeof(o)))return-EFAULT; }
    if(n.it_value.tv_sec<0||n.it_value.tv_nsec<0||n.it_value.tv_nsec>=1000000000LL||n.it_interval.tv_sec<0||n.it_interval.tv_nsec<0||n.it_interval.tv_nsec>=1000000000LL)return-EINVAL;
    uint64_t value=(uint64_t)n.it_value.tv_sec*1000000000ULL+(uint64_t)n.it_value.tv_nsec;
    uint64_t interval=(uint64_t)n.it_interval.tv_sec*1000000000ULL+(uint64_t)n.it_interval.tv_nsec;
    t->interval_ns=interval;t->expirations=0;t->next_ns=value?clock_now_ns_simple(t->clockid)+value:0;return 0;
}

static int user_copy_from(uint8_t *dst, uint64_t src, uint64_t len) {
    while (len) {
        uint64_t phys, pte;
        uint64_t page = src & PAGE_MASK, off = src & (PAGE_SIZE-1);
        uint64_t n = PAGE_SIZE - off; if (n > len) n = len;
        if (vmm_user_translate(page, &phys, &pte) != 0 || !(pte & PTE_U)) return -EFAULT;
        uint8_t *sp = (uint8_t*)pmm_phys_to_virt(phys);
        if (!sp) return -EFAULT;
        for (uint64_t i=0;i<n;i++) dst[i]=sp[off+i];
        dst += n; src += n; len -= n;
    }
    return 0;
}

static int user_copy_to(uint64_t dst, const uint8_t *src, uint64_t len) {
    while (len) {
        uint64_t phys, pte;
        uint64_t page = dst & PAGE_MASK, off = dst & (PAGE_SIZE-1);
        uint64_t n = PAGE_SIZE - off; if (n > len) n = len;
        if (vmm_user_translate(page, &phys, &pte) != 0 || !(pte & PTE_U) || !(pte & PTE_W)) return -EFAULT;
        uint8_t *dp = (uint8_t*)pmm_phys_to_virt(phys);
        if (!dp) return -EFAULT;
        for (uint64_t i=0;i<n;i++) dp[off+i]=src[i];
        dst += n; src += n; len -= n;
    }
    return 0;
}

static int user_copy_cstr(char *dst, uint64_t src, uint64_t cap) {
    if (!dst || !cap) return -EINVAL;
    for (uint64_t i=0;i+1<cap;i++) {
        if (user_copy_from((uint8_t*)&dst[i], src+i, 1)) return -EFAULT;
        if (!dst[i]) return 0;
    }
    dst[cap-1]=0; return -ENAMETOOLONG;
}

static void rtc_now(uint64_t *sec, uint64_t *nsec) {
    uint8_t sec0,min,hour,day,mon,yr;
    outb(0x70,0x00); sec0=inb(0x71);
    outb(0x70,0x02); min=inb(0x71);
    outb(0x70,0x04); hour=inb(0x71);
    outb(0x70,0x07); day=inb(0x71);
    outb(0x70,0x08); mon=inb(0x71);
    outb(0x70,0x09); yr=inb(0x71);
    outb(0x70,0x0B); uint8_t b=inb(0x71);
    if (!(b&4)) { sec0=(sec0&15)+((sec0>>4)*10); min=(min&15)+((min>>4)*10); hour=(hour&15)+((hour>>4)*10); day=(day&15)+((day>>4)*10); mon=(mon&15)+((mon>>4)*10); yr=(yr&15)+((yr>>4)*10); }
    if (!(b&2)) { if (hour&0x80) hour=((hour&0x7f)+12)%24; else if(hour==12) hour=0; }
    uint64_t y=2000+yr, days=0; for(uint64_t yy=1970; yy<y; yy++) days += ((yy%4==0 && yy%100!=0)||yy%400==0)?366:365;
    static const uint16_t md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(uint8_t m=1;m<mon;m++) days += md[m-1] + (m==2 && ((y%4==0&&y%100!=0)||y%400==0));
    days += day-1; *sec=days*86400ULL + hour*3600ULL + min*60ULL + sec0; *nsec=0;
}

static int64_t sys_pipe_read_dispatch(syscall_frame_t *f, int fd, void *buf, uint64_t count) {
    if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF;
    struct file_desc *d=&files[fd]; if(d->type!=FD_PIPE_R)return sys_read(fd,buf,count);
    struct pipe_state *p=(struct pipe_state*)d->private_data; if(!count)return 0;
    if(p->count) return sys_read(fd,buf,count);
    if(!p->writers) return 0;
    if(d->flags & 0x800) return -EAGAIN;
    task_t *t=&tasks[current_task]; task_save_from_frame(t,f); t->pipe_wait_fd=fd; t->pipe_wait_buf=(uint64_t)buf; t->pipe_wait_count=count; t->state=TASK_BLOCKED;
    int next=task_pick_next(); if(next<0){t->state=TASK_RUNNING;t->pipe_wait_fd=-1;t->pipe_wait_buf=0;t->pipe_wait_count=0;return-EAGAIN;}
    tasks[next].state=TASK_RUNNING; current_task=next; (void)vmm_user_activate(tasks[next].ctx.cr3); task_switch_target=next; task_switch_pending=1; task_return_ctx=&tasks[next].ctx;
    return 0;
}

void syscall_dispatch(syscall_frame_t *f) {

    if (f && f->user_cr3) (void)vmm_user_activate(f->user_cr3);
    uint64_t nr=f->rax,a1=f->rdi,a2=f->rsi,a3=f->rdx,a4=f->r10,a5=f->r8,a6=f->r9;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
    if (musl_syscall_trace_count < 64) {
        debug_hex64("[SYSCALL] nr=0x", nr);
        musl_syscall_trace_count++;
    }
#endif
    int64_t ret=-ENOSYS;
    switch(nr) {
      case 0: ret=sys_pipe_read_dispatch(f,(int)a1,(void*)a2,a3); break;
      case 1: ret=sys_write((int)a1,(const void*)a2,a3); break;
      case 2: ret=sys_open((const char*)a1,(int)a2,(int)a3); break;
      case 3: ret=sys_close((int)a1); break;
      case 4: ret=sys_stat((const char*)a1,(void*)a2); break;
      case 5: ret=sys_fstat((int)a1,(void*)a2); break;
      case 7: ret=sys_poll((void*)a1,a2,(int)a3); break;
      case 8: ret=sys_lseek((int)a1,(int64_t)a2,(int)a3); break;
      case 9: ret=sys_mmap(a1,a2,a3,a4,(int)a5,a6); break;
      case 10: ret=sys_mprotect(a1,a2,a3); break;
      case 11: ret=sys_munmap(a1,a2); break;
      case 12: ret=sys_brk(a1); break;
      case 13: ret=sys_rt_sigaction((int)a1,(void*)a2,(void*)a3,a4); break;
      case 14: ret=sys_rt_sigprocmask((int)a1,(void*)a2,(void*)a3,a4); break;
      case 16: ret=sys_ioctl((int)a1,a2,a3); break;
      case 17: ret=sys_pread64((int)a1,(void*)a2,a3,a4); break;
      case 19: ret=sys_readv((int)a1,(const void*)a2,(int)a3); break;
      case 20: ret=sys_writev((int)a1,(const void*)a2,(int)a3); break;
      case 21: ret=sys_access((const char*)a1,(int)a2); break;
      case 22: ret=sys_pipe((int*)a1); break;
      case 23: ret=sys_select((int)a1,(void*)a2,(void*)a3,(void*)a4,(void*)a5); break;
      case 24: ret=sys_sched_yield(f); break;
      case 32: ret=sys_dup((int)a1); break;
      case 33: ret=sys_dup2((int)a1,(int)a2); break;
      case 292: ret=sys_dup3((int)a1,(int)a2,(int)a3); break;
      case 39: ret=sys_getpid(); break;
      case 41: ret=sys_socket((int)a1,(int)a2,(int)a3); break;
      case 42: ret=sys_connect((int)a1,(void*)a2,(int)a3); break;
      case 43: ret=sys_accept((int)a1,(void*)a2,(int*)a3); break;
      case 44: ret=sys_sendto((int)a1,(void*)a2,(int)a3,(int)a4,(void*)a5,(int)a6); break;
      case 45: ret=sys_recvfrom((int)a1,(void*)a2,(int)a3,(int)a4,(void*)a5,(int*)a6); break;
      case 48: ret=sys_shutdown((int)a1,(int)a2); break;
      case 49: ret=sys_bind((int)a1,(void*)a2,(int)a3); break;
      case 50: ret=sys_listen((int)a1,(int)a2); break;
      case 53: ret=sys_socketpair((int)a1,(int)a2,(int)a3,(int*)a4); break;
      case 54: ret=sys_setsockopt((int)a1,(int)a2,(int)a3,(void*)a4,(int)a5); break;
      case 55: ret=sys_getsockopt((int)a1,(int)a2,(int)a3,(void*)a4,(int*)a5); break;
      case 60: ret=sys_exit((int)a1); break;
      case 63: ret=sys_uname((void*)a1); break;
      case 72: ret=sys_fcntl((int)a1,(int)a2,a3); break;
      case 79: ret=sys_getcwd((char*)a1,a2); break;
      case 80: ret=sys_chdir((const char*)a1); break;
      case 82: ret=sys_rename((const char*)a1,(const char*)a2); break;
      case 83: ret=sys_mkdir((const char*)a1,(int)a2); break;
      case 84: ret=sys_rmdir((const char*)a1); break;
      case 87: ret=sys_unlink((const char*)a1); break;
      case 89: ret=sys_readlink((const char*)a1,(char*)a2,a3); break;
      case 217: ret=sys_getdents64((int)a1,(void*)a2,a3); break;
      case 96: ret=sys_gettimeofday((void*)a1,(void*)a2); break;
      case 99: ret=sys_sysinfo((void*)a1); break;
      case 102: ret=sys_getuid(); break;
      case 104: ret=sys_getgid(); break;
      case 107: ret=sys_geteuid(); break;
      case 108: ret=sys_getegid(); break;
      case 110: ret=sys_getppid(); break;
      case 158: ret=sys_arch_prctl(a1,a2); break;
      case 186: ret=sys_gettid(); break;
      case 201: { uint64_t s,ns; rtc_now(&s,&ns); ret=(int64_t)s; } break;
      case 202: ret=sys_futex(f,(uint32_t*)a1,(int)a2,(uint32_t)a3,(const void*)a4,(uint32_t*)a5,(uint32_t)a6); break;
      case 218: ret=sys_set_tid_address((int*)a1); break;
      case 62: ret=sys_kill((int)a1,(int)a2); break;
      case 127: ret=sys_rt_sigpending((sigset_t*)a1,a2); break;
      case 228: ret=sys_clock_gettime((int)a1,(void*)a2); break;
      case 229: ret=sys_clock_getres((int)a1,(void*)a2); break;
      case 231: ret=sys_exit_group((int)a1); break;
      case 6: ret=sys_lstat((const char*)a1,(void*)a2); break;
      case 15: ret=sys_rt_sigreturn(f); break;
      case 56: ret=sys_clone(f,(uint64_t)a1,a2,a3,a4,a5); break;
      case 57: ret=sys_fork(f); break;
      case 59: ret=sys_execve((const char*)a1,(const char*const*)a2,(const char*const*)a3); break;
      case 61: ret=sys_wait4(f,(int)a1,(int*)a2,(int)a3,(void*)a4); break;
      case 74: ret=0; break;
      case 75: ret=0; break;
      case 77: ret=sys_ftruncate((int)a1,a2); break;
      case 95: ret=sys_umask(a1); break;
      case 97: ret=sys_getrlimit((int)a1,(void*)a2); break;
      case 157: ret=sys_prctl((int)a1,a2,a3,a4,a5); break;
      case 219: ret=-ENOSYS; break;
      case 232: ret=sys_epoll_wait((int)a1,(void*)a2,(int)a3,(int)a4); break;
      case 233: ret=sys_epoll_ctl((int)a1,(int)a2,(int)a3,(void*)a4); break;
      case 234: ret=sys_tgkill((int)a1,(int)a2,(int)a3); break;
      case 257: ret=sys_openat((int)a1,(const char*)a2,(int)a3,(int)a4); break;
      case 262: ret=sys_newfstatat((int)a1,(const char*)a2,(void*)a3,(int)a4); break;
      case 267: ret=sys_readlinkat((int)a1,(const char*)a2,(char*)a3,a4); break;
      case 269: ret=sys_faccessat((int)a1,(const char*)a2,(int)a3); break;
      case 289: ret=sys_eventfd2((uint32_t)a1,(int)a2); break;
      case 291: ret=sys_epoll_create1((int)a1); break;
      case 293: ret=sys_pipe2((int*)a1,(int)a2); break;
      case 302: ret=sys_prlimit64((int)a1,(int)a2,(const void*)a3,(void*)a4); break;
      case 325: ret=sys_timerfd_create((int)a1,(int)a2); break;
      case 326: ret=sys_timerfd_settime((int)a1,(int)a2,(void*)a3,(void*)a4); break;
      case 334: ret=-ENOSYS; break;
      case 35: ret=sys_nanosleep((const void*)a1,(void*)a2); break;
      case 78: ret=sys_getdents64((int)a1,(void*)a2,a3); break;
      case 203: ret=sys_sched_setaffinity((int)a1,a2,(const void*)a3); break;
      case 204: ret=sys_sched_getaffinity((int)a1,a2,(void*)a3); break;
      case 273: ret=sys_set_robust_list((void*)a1,a2); break;
      case 274: ret=sys_get_robust_list((int)a1,(void*)a2,(void*)a3); break;
      case 309: ret=sys_getcpu((unsigned*)a1,(unsigned*)a2,(void*)a3); break;
      case 318: ret=sys_getrandom((void*)a1,a2,(uint32_t)a3); break;
      default: ret=-ENOSYS; break;
    }
    f->rax=ret;

    if (!user_exec_pending) {

        if (nr != 15)
            deliver_pending_signal(f);

        syscall_user_rip = f->user_rip;
        syscall_user_rsp = f->user_rsp;
        syscall_user_rflags = f->user_rflags;
        syscall_user_cr3 = f->user_cr3;
    }

    syscall_saved_rax=(uint64_t)f->rax;
}

static int pipe_read_into_task(task_t *t, struct pipe_state *p) {
    if (!t || !p || t->pipe_wait_fd < 0 || !t->pipe_wait_buf || !t->pipe_wait_count) return 0;
    uint64_t done=0; uint8_t tmp[256];
    while (done<t->pipe_wait_count && p->count) {
        uint64_t n=t->pipe_wait_count-done; if(n>sizeof(tmp)) n=sizeof(tmp); if(n>p->count)n=p->count;
        for(uint64_t i=0;i<n;i++){tmp[i]=p->data[p->rpos];p->rpos=(p->rpos+1)%PIPE_CAP;p->count--;}
        if(user_copy_to(t->pipe_wait_buf+done,tmp,n)) { t->ctx.rax = done ? (int64_t)done : -EFAULT; t->pipe_wait_fd=-1; t->pipe_wait_buf=0; t->pipe_wait_count=0; return -1; }
        done += n;
    }
    t->ctx.rax=(int64_t)done; t->pipe_wait_fd=-1; t->pipe_wait_buf=0; t->pipe_wait_count=0; t->state=TASK_RUNNABLE;
    return (int)done;
}
static uint32_t pipe_live_readers(struct pipe_state *p);
static uint32_t pipe_live_writers(struct pipe_state *p);
static int pipe_has_readers(struct pipe_state *p);

static void pipe_wake_readers(struct pipe_state *p) {
    if(!p) return;
    for(int i=0;i<MAX_TASKS;i++) {
        task_t *t=&tasks[i]; if(!t->used || t->state!=TASK_BLOCKED || t->pipe_wait_fd<0) continue;
        if(t->pipe_wait_fd>=MAX_FILES || !task_fd_tables[i][t->pipe_wait_fd].in_use || task_fd_tables[i][t->pipe_wait_fd].private_data!=p) continue;
        pipe_read_into_task(t,p);
    }
}

int64_t sys_write(int fd, const void *buf, uint64_t count) {
    if(fd>=0&&fd<MAX_FILES&&files[fd].in_use&&files[fd].type==FD_SOCKET) return sys_sendto(fd,buf,(int)count,0,NULL,0);

    if (fd<0 || fd>=MAX_FILES) { if(fd!=1&&fd!=2) return -EBADF; }
    if (count>(1ULL<<20) || (!buf && count)) return -EINVAL;
    if (!count) return 0;
    if (!user_range_ok((uint64_t)buf,count,0)) return -EFAULT;
    if ((fd==1 || fd==2) && (!files[fd].in_use || files[fd].type==FD_PSEUDO)) {
        uint64_t va=(uint64_t)buf, rem=count;
        while(rem){ uint64_t phys,pte; uint64_t off=va&(PAGE_SIZE-1); uint64_t n=PAGE_SIZE-off; if(n>rem)n=rem;
            if(vmm_user_translate(va&PAGE_MASK,&phys,&pte)||!(pte&PTE_U)) return -EFAULT;
            uint8_t *src=(uint8_t*)pmm_phys_to_virt(phys); if(!src)return -EFAULT;
            for(uint64_t i=0;i<n;i++){
                uint8_t ch=src[off+i];
                console_putc((char)ch);
                console_fb_putc(ch);
            }
            va+=n; rem-=n;
        }
        return count;
    }
    if(fd>=MAX_FILES || !files[fd].in_use) return -EBADF;
    struct file_desc *f=&files[fd];
    if(f->type==FD_PSEUDO){
        int k=(int)(uintptr_t)f->private_data;
        if(k==DEV_NULL) return (int64_t)count;
        if(k==DEV_CONSOLE || k==DEV_TTY){ uint64_t va=(uint64_t)buf, rem=count; while(rem){ uint64_t phys,pte,off=va&(PAGE_SIZE-1),n=PAGE_SIZE-off; if(n>rem)n=rem; if(vmm_user_translate(va&PAGE_MASK,&phys,&pte)||!(pte&PTE_U))return-EFAULT; uint8_t*src=(uint8_t*)pmm_phys_to_virt(phys); if(!src)return-EFAULT; for(uint64_t i=0;i<n;i++){console_putc((char)src[off+i]); console_fb_putc(src[off+i]);} va+=n; rem-=n;} return count; }
        return -EBADF;
    }
    if(f->type==FD_EVENTFD){
        if(count!=8)return-EINVAL; uint64_t v; if(user_copy_from((uint8_t*)&v,(uint64_t)buf,8))return-EFAULT;
        struct eventfd_state_k*e=(struct eventfd_state_k*)f->private_data; if(v==UINT64_MAX)return-EINVAL; if(UINT64_MAX-e->value<v)return (f->flags&0x800)?-EAGAIN:-EAGAIN; e->value+=v; return 8;
    }
    if(f->type==FD_TIMERFD)return-EINVAL;
    if(f->type==FD_PIPE_W){ struct pipe_state *p=(struct pipe_state*)f->private_data; p->readers=pipe_live_readers(p); if(!pipe_has_readers(p)) return -EPIPE; if(!count) return 0; uint64_t done=0; while(done<count){ if(p->count==PIPE_CAP){ if(f->flags & 0x800) return done?done:-EAGAIN; break; } uint8_t tmp[256]; uint64_t n=count-done; if(n>sizeof(tmp))n=sizeof(tmp); if(n>PIPE_CAP-p->count)n=PIPE_CAP-p->count; if(user_copy_from(tmp,(uint64_t)buf+done,n))return done?done:-EFAULT; for(uint64_t i=0;i<n;i++){p->data[p->wpos]=tmp[i];p->wpos=(p->wpos+1)%PIPE_CAP;p->count++;} done+=n; pipe_wake_readers(p); if(done<count && p->count==PIPE_CAP) break; } return done; }
    if(f->type!=FD_FILE) return -EBADF;
    if(f->vnode && vfs_file_is_regular(f->vnode)) {
        if((f->flags & 3) == 0) return -EBADF;
        uint64_t off=fd_offset_get(f);
        uint8_t tmp[256]; uint64_t done=0;
        while(done<count) { uint64_t n=count-done; if(n>sizeof(tmp)) n=sizeof(tmp);
            if(user_copy_from(tmp,(uint64_t)buf+done,n)) return done?done:-EFAULT;
            uint64_t w=vfs_file_write(f->vnode,off,tmp,n); if(!w) break;
            off+=w; done+=w;
        }
        fd_offset_set(f,off); f->size=vfs_file_size(f->vnode); return done;
    }
    return -EBADF;
}

int64_t sys_read(int fd, void *buf, uint64_t count) {
    if(fd>=0&&fd<MAX_FILES&&files[fd].in_use&&files[fd].type==FD_SOCKET) return sys_recvfrom(fd,buf,(int)count,0,NULL,NULL);

    if(fd<0 || fd>=MAX_FILES || !files[fd].in_use) return -EBADF;
    if(!buf && count)return-EINVAL; if(count && !user_range_ok((uint64_t)buf,count,1))return-EFAULT;
    struct file_desc *f=&files[fd];
    if(f->type==FD_PSEUDO){
        int k=(int)(uintptr_t)f->private_data;
        if(k==DEV_NULL) return 0;
        if(k==DEV_TTY) {
            if(!count) return 0;
            if(key_buffer==0) return -EAGAIN;
            uint8_t sc=key_buffer; key_buffer=0; uint8_t ch=0;

            static const uint8_t normal[58] = {
                0, 27, '1','2','3','4','5','6','7','8','9','0','-','=',8,9,
                'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
                'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\',
                'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
            };
            static const uint8_t shifted[58] = {
                0, 27, '!','@','#','$','%','^','&','*','(',')','_','+',8,9,
                'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
                'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
                'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
            };
            if(sc < 58) {
                int alpha = ((sc>=0x10 && sc<=0x19) ||
                             (sc>=0x1e && sc<=0x26) ||
                             (sc>=0x2c && sc<=0x32));
                int use_shift = shift_pressed ? 1 : 0;

                if(alpha && caps_lock) use_shift = !use_shift;
                ch = use_shift ? shifted[sc] : normal[sc];
            }
            if(ch && user_copy_to((uint64_t)buf,&ch,1)==0) return 1;
            return -EAGAIN;
        }
        if(k==DEV_CONSOLE) return 0;
        if(k==DEV_ZERO){ uint8_t z[128]={0}; uint64_t done=0; while(done<count){uint64_t n=count-done; if(n>sizeof(z))n=sizeof(z); if(user_copy_to((uint64_t)buf+done,z,n))return-EFAULT; done+=n;} return count; }
        if(k==DEV_RANDOM || k==DEV_URANDOM){ uint64_t done=0; while(done<count){uint64_t n=count-done; if(n>64)n=64; uint8_t r[64]; for(uint64_t i=0;i<n;i++){pseudo_rand_state^=pseudo_rand_state<<13;pseudo_rand_state^=pseudo_rand_state>>17;pseudo_rand_state^=pseudo_rand_state<<5;r[i]=(uint8_t)pseudo_rand_state;} if(user_copy_to((uint64_t)buf+done,r,n))return-EFAULT; done+=n;} return count; }
        if(k==PSEUDO_PROC_FILE && f->vnode) {
            char proc_path[128]; size_t pn=0;
            if (f->vnode->name[0]=='c' && f->vnode->name[1]=='p') {
                const char *q="/proc/cpuinfo"; while(q[pn] && pn+1<sizeof(proc_path)){proc_path[pn]=q[pn];pn++;}
            } else if (f->vnode->name[0]=='m' && f->vnode->name[1]=='e') {
                const char *q="/proc/meminfo"; while(q[pn] && pn+1<sizeof(proc_path)){proc_path[pn]=q[pn];pn++;}
            } else if (f->vnode->name[0]=='u' && f->vnode->name[1]=='p') {
                const char *q="/proc/uptime"; while(q[pn] && pn+1<sizeof(proc_path)){proc_path[pn]=q[pn];pn++;}
            } else if (f->vnode->name[0]=='v' && f->vnode->name[1]=='e') {
                const char *q="/proc/version"; while(q[pn] && pn+1<sizeof(proc_path)){proc_path[pn]=q[pn];pn++;}
            }
            if(pn){proc_path[pn]=0; f->size=pseudo_make_proc((char*)f->data,4096,proc_path);}
        }
        uint64_t cur=fd_offset_get(f); uint64_t avail=cur<f->size?f->size-cur:0, n=count<avail?count:avail; if(n && user_copy_to((uint64_t)buf,f->data+cur,n))return-EFAULT; fd_offset_set(f,cur+n); return n;
    }
    if(f->type==FD_EVENTFD){
        if(count!=8)return-EINVAL; struct eventfd_state_k*e=(struct eventfd_state_k*)f->private_data; if(e->value==0)return (f->flags&0x800)?-EAGAIN:-EAGAIN; uint64_t v=e->value; e->value=0; if(user_copy_to((uint64_t)buf,(const uint8_t*)&v,8))return-EFAULT; return 8;
    }
    if(f->type==FD_TIMERFD){
        if(count!=8)return-EINVAL; struct timerfd_state_k*t=(struct timerfd_state_k*)f->private_data; uint64_t now=clock_now_ns_simple(t->clockid);
        if(t->next_ns && now>=t->next_ns){ if(t->interval_ns){uint64_t missed=(now-t->next_ns)/t->interval_ns+1;t->expirations+=missed;t->next_ns+=missed*t->interval_ns;} else {t->expirations=1;t->next_ns=0;} }
        if(!t->expirations)return (f->flags&0x800)?-EAGAIN:-EAGAIN; uint64_t v=t->expirations;t->expirations=0;if(user_copy_to((uint64_t)buf,(const uint8_t*)&v,8))return-EFAULT;return 8;
    }
    if(f->type==FD_PIPE_R){ struct pipe_state *p=(struct pipe_state*)f->private_data; if(!count)return 0; if(!p->count){ if(!p->writers)return 0; if(f->flags & 0x800) return -EAGAIN; return -EAGAIN; } uint8_t tmp[256]; uint64_t done=0; while(done<count && p->count){ uint64_t n=count-done;if(n>sizeof(tmp))n=sizeof(tmp); if(n>p->count)n=p->count; for(uint64_t i=0;i<n;i++){tmp[i]=p->data[p->rpos];p->rpos=(p->rpos+1)%PIPE_CAP;p->count--;} if(user_copy_to((uint64_t)buf+done,tmp,n))return done?done:-EFAULT;done+=n;} return done; }
    if(f->type!=FD_FILE)return-EBADF;
    if(f->vnode && vfs_is_boot_file(f->vnode)) {
        uint64_t cur=fd_offset_get(f), size=vfs_file_size(f->vnode);
        if(cur>=size) return 0;
        uint64_t n=count<size-cur?count:size-cur; if(n>256)n=256;
        uint8_t tmp[256]; size_t got=bootfs_read_root_file(vfs_node_name(f->vnode),cur,tmp,(size_t)n);
        if(got && user_copy_to((uint64_t)buf,tmp,got)) return -EFAULT;
        fd_offset_set(f,cur+got); return (int64_t)got;
    }
    uint64_t size=(f->vnode&&vfs_file_is_regular(f->vnode))?vfs_file_size(f->vnode):f->size;
    uint8_t *data=(f->vnode&&vfs_file_is_regular(f->vnode))?vfs_file_data(f->vnode):f->data;
    uint64_t cur=fd_offset_get(f); uint64_t avail=cur<size?size-cur:0, n=count<avail?count:avail; if(n && user_copy_to((uint64_t)buf,data+cur,n))return-EFAULT; fd_offset_set(f,cur+n); f->size=size; return n;
}

int64_t sys_open(const char *path, int flags, int mode) {
    (void)mode; char raw[128], pth[128]; if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT; if(resolve_process_path(raw,pth,sizeof(pth))<0)return-ENAMETOOLONG;
    int fd=find_free_fd(); if(fd<0)return-EMFILE;
    int typ=0,kind=0;
    if(pseudo_lookup(pth,&typ,&kind)) {
        files[fd].in_use=1; files[fd].flags=flags; files[fd].name=pth[1]?pth+1:pth;
        pseudo_fill(&files[fd],pth,kind); files[fd].size = pseudo_is_dir_kind(kind) ? 0 : files[fd].size;
        return fd;
    }
    struct vfs_node *vn=vfs_lookup(pth);
    if(!vn && (flags & 0x40)) vn=vfs_create_file(pth,(flags & 0x200)!=0);
    if(vn && vfs_is_dir(vn)) {
        files[fd].in_use=1; files[fd].type=FD_PSEUDO; files[fd].flags=flags; files[fd].private_data=(void*)(uintptr_t)PSEUDO_DEV_DIR; files[fd].vnode=vn; files[fd].offset=0; files[fd].size=0; files[fd].data=NULL; files[fd].name=vn->name; return fd;
    }
    if(vn && vfs_is_boot_file(vn)) {
        if(flags & 3) return -EROFS;
        files[fd].in_use=1; files[fd].type=FD_FILE; files[fd].flags=flags; files[fd].offset=0;
        files[fd].shared_offset=alloc_open_offset(); if(!files[fd].shared_offset){files[fd].in_use=0;return-EMFILE;}
        files[fd].data=NULL; files[fd].size=vfs_file_size(vn); files[fd].name=vn->name; files[fd].vnode=vn; return fd;
    }
    if(vn && vfs_file_is_regular(vn)) {
        if((flags & 0x200) && vfs_file_truncate(vn,0)<0) return -EIO;
        files[fd].in_use=1; files[fd].type=FD_FILE; files[fd].flags=flags; files[fd].offset=(flags&0x400)?vn->size:0;
        files[fd].shared_offset=alloc_open_offset(); if(!files[fd].shared_offset){files[fd].in_use=0;return-EMFILE;}
        *files[fd].shared_offset=files[fd].offset; files[fd].data=vn->data; files[fd].size=vn->size; files[fd].name=vn->name; files[fd].vnode=vn; return fd;
    }
    return -ENOENT;
}
static uint32_t pipe_live_readers(struct pipe_state *p){
    uint32_t n=0;
    for(int ti=0;ti<MAX_TASKS;ti++) if(tasks[ti].used)
        for(int i=0;i<MAX_FILES;i++){struct file_desc *d=&task_fd_tables[ti][i];if(d->in_use&&d->private_data==p&&d->type==FD_PIPE_R)n++;}
    return n;
}
static uint32_t pipe_live_writers(struct pipe_state *p){
    uint32_t n=0;
    for(int ti=0;ti<MAX_TASKS;ti++) if(tasks[ti].used)
        for(int i=0;i<MAX_FILES;i++){struct file_desc *d=&task_fd_tables[ti][i];if(d->in_use&&d->private_data==p&&d->type==FD_PIPE_W)n++;}
    return n;
}
static int pipe_has_readers(struct pipe_state *p){ return pipe_live_readers(p)!=0; }
static int pipe_has_writers(struct pipe_state *p){ return pipe_live_writers(p)!=0; }
int64_t sys_close(int fd){if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF; struct file_desc*f=&files[fd]; struct pipe_state *p=(f->type==FD_PIPE_R||f->type==FD_PIPE_W)?(struct pipe_state*)f->private_data:NULL; if(f->type==FD_PIPE_R){if(p->readers)p->readers--; fd_drop_resources(f); f->in_use=0;f->private_data=NULL; p->readers=pipe_live_readers(p); for(int i=0;i<MAX_TASKS;i++){task_t*t=&tasks[i];if(t->used&&t->state==TASK_BLOCKED&&t->pipe_wait_fd==fd){t->ctx.rax=0;t->pipe_wait_fd=-1;t->pipe_wait_buf=0;t->pipe_wait_count=0;t->state=TASK_RUNNABLE;}} return 0;} if(f->type==FD_PIPE_W){if(p->writers)p->writers--; fd_drop_resources(f); f->in_use=0;f->private_data=NULL; p->writers=pipe_live_writers(p); pipe_wake_readers(p); return 0;} if(f->type==FD_SOCKET){struct unix_socket_state*s=socket_state(f);if(s){s->closed[socket_side(f)]=1;if(s->refs)s->refs--;} f->in_use=0;f->private_data=NULL;return 0;} f->in_use=0;f->private_data=NULL;return 0;}

struct linux_stat { uint64_t st_dev,st_ino,st_nlink,st_mode,st_uid,st_gid,st_rdev,st_size,st_blksize,st_blocks,st_atime,st_atime_nsec,st_mtime,st_mtime_nsec,st_ctime,st_ctime_nsec; uint32_t __unused[2]; };
static int fill_stat(struct linux_stat *st,uint64_t size,uint32_t mode){for(size_t i=0;i<sizeof(*st);i++)((uint8_t*)st)[i]=0;st->st_ino=1;st->st_nlink=1;st->st_mode=mode;st->st_uid=0;st->st_gid=0;st->st_size=size;st->st_blksize=4096;st->st_blocks=(size+511)/512;return 0;}
int64_t sys_stat(const char*path,void*ust){
    char raw[128],p[128];if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT;if(resolve_process_path(raw,p,sizeof(p))<0)return-ENAMETOOLONG;
    int typ=0,kind=0; struct linux_stat st;
    if(pseudo_lookup(p,&typ,&kind)){ fill_stat(&st,pseudo_is_dir_kind(kind)?0:0,pseudo_is_dir_kind(kind)?0040755:0100444); st.st_ino=100; if(!pseudo_is_dir_kind(kind)&&kind==PSEUDO_PROC_FILE) { char tmp[4096]; st.st_size=pseudo_make_proc(tmp,sizeof(tmp),p); } if(user_copy_to((uint64_t)ust,(uint8_t*)&st,sizeof(st)))return-EFAULT; return 0; }
    struct vfs_node *vn=vfs_lookup(p);
    if(vn) {
        uint32_t mode=vfs_mode(vn);
        uint64_t size=(vfs_file_is_regular(vn)||vfs_is_boot_file(vn))?vfs_file_size(vn):0;
        fill_stat(&st,size,mode);
        st.st_ino=vfs_ino(vn);
        if(user_copy_to((uint64_t)ust,(uint8_t*)&st,sizeof(st)))return-EFAULT;
        return 0;
    }
    return-ENOENT;
}
int64_t sys_fstat(int fd,void*ust){if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF;struct linux_stat st;fill_stat(&st,files[fd].size,0100644);if(user_copy_to((uint64_t)ust,(uint8_t*)&st,sizeof(st)))return-EFAULT;return 0;}
int64_t sys_lseek(int fd,int64_t off,int whence){if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF;struct file_desc*f=&files[fd];if(f->type!=FD_FILE)return-ESPIPE;uint64_t cur=fd_offset_get(f);int64_t base=whence==0?0:(whence==1?(int64_t)cur:(int64_t)f->size);int64_t n=base+off;if(n<0)return-EINVAL;fd_offset_set(f,(uint64_t)n);return n;}
int64_t sys_pread64(int fd,void*buf,uint64_t count,uint64_t off){if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF;struct file_desc*f=&files[fd];if(f->type!=FD_FILE)return-ESPIPE;if(off>=f->size)return 0;uint64_t n=count<f->size-off?count:f->size-off;if(n&&!user_copy_to((uint64_t)buf,f->data+off,n))return n;return n? -EFAULT:0;}
struct iovec_k { uint64_t base,len; };
int64_t sys_readv(int fd,const void*uiov,int cnt){if(cnt<0||cnt>1024)return-EINVAL;struct iovec_k v;int64_t total=0;for(int i=0;i<cnt;i++){if(user_copy_from((uint8_t*)&v,(uint64_t)uiov+i*sizeof(v),sizeof(v)))return-EFAULT;int64_t n=sys_read(fd,(void*)v.base,v.len);if(n<0)return total?total:n;total+=n;if((uint64_t)n<v.len)break;}return total;}
int64_t sys_writev(int fd,const void*uiov,int cnt){if(cnt<0||cnt>1024)return-EINVAL;struct iovec_k v;int64_t total=0;for(int i=0;i<cnt;i++){if(user_copy_from((uint8_t*)&v,(uint64_t)uiov+i*sizeof(v),sizeof(v)))return-EFAULT;int64_t n=sys_write(fd,(void*)v.base,v.len);if(n<0)return total?total:n;total+=n;if((uint64_t)n<v.len)break;}return total;}
int64_t sys_access(const char*path,int mode){(void)mode;char raw[128],p[128];if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT;if(resolve_process_path(raw,p,sizeof(p))<0)return-ENAMETOOLONG;int t=0,k=0;if(pseudo_lookup(p,&t,&k))return 0;if(vfs_lookup(p))return 0;const char *tp=p;if(tp[0]=='/')tp++;return (tp[0]=='T'&&tp[1]=='E'&&tp[2]=='S'&&tp[3]=='T'&&tp[4]=='.')?0:-ENOENT;}
int64_t sys_pipe(int *ufds){int a=find_free_fd(),b=-1;if(a<0)return-EMFILE;files[a].in_use=1;files[a].type=FD_PIPE_R;files[a].flags=0;for(int i=a+1;i<MAX_FILES;i++)if(!files[i].in_use){b=i;break;}if(b<0){files[a].in_use=0;return-EMFILE;}static int pi;struct pipe_state*p=&pipes[pi++%8];p->rpos=p->wpos=p->count=0;p->readers=1;p->writers=1;files[b].in_use=1;files[b].type=FD_PIPE_W;files[a].private_data=files[b].private_data=p;if(user_copy_to((uint64_t)ufds,(uint8_t*)&a,sizeof(a))||user_copy_to((uint64_t)ufds+4,(uint8_t*)&b,sizeof(b))){files[a].in_use=files[b].in_use=0;return-EFAULT;}return 0;}
static void fd_copy_scalar(struct file_desc *dst, const struct file_desc *src) {
    dst->in_use = src->in_use; dst->type = src->type; dst->flags = src->flags;
    dst->offset = src->offset; dst->size = src->size; dst->data = src->data;
    dst->name = src->name; dst->private_data = src->private_data; dst->vnode = src->vnode;
    dst->shared_offset = src->shared_offset;
    if(dst->shared_offset) retain_open_offset(dst->shared_offset);
}
static void fd_drop_resources(struct file_desc *f){ if(f->shared_offset){release_open_offset(f->shared_offset);f->shared_offset=NULL;} }
static int64_t sys_dup_internal(int oldfd,int minfd,int cloexec){
    if(oldfd<0||oldfd>=MAX_FILES||!files[oldfd].in_use)return-EBADF;
    int n=-1; for(int i=minfd;i<MAX_FILES;i++) if(!files[i].in_use){n=i;break;} if(n<0)return-EMFILE;
    fd_copy_scalar(&files[n],&files[oldfd]);
    files[n].flags &= ~0x80000; if(cloexec) files[n].flags |= 0x80000;
    if(files[n].type==FD_PIPE_R)((struct pipe_state*)files[n].private_data)->readers++;
    if(files[n].type==FD_PIPE_W)((struct pipe_state*)files[n].private_data)->writers++;
    return n;
}
int64_t sys_dup(int oldfd){return sys_dup_internal(oldfd,3,0);}
int64_t sys_dup2(int oldfd,int newfd){
    if(oldfd<0||oldfd>=MAX_FILES||!files[oldfd].in_use||newfd<0||newfd>=MAX_FILES)return-EBADF;
    if(oldfd==newfd)return newfd;
    if(files[newfd].in_use)sys_close(newfd);
    fd_copy_scalar(&files[newfd],&files[oldfd]); files[newfd].flags &= ~0x80000;
    if(files[newfd].type==FD_PIPE_R)((struct pipe_state*)files[newfd].private_data)->readers++;
    if(files[newfd].type==FD_PIPE_W)((struct pipe_state*)files[newfd].private_data)->writers++;
    return newfd;
}
int64_t sys_dup3(int oldfd,int newfd,int flags){
    if(flags & ~0x80000)return-EINVAL;
    if(oldfd<0||oldfd>=MAX_FILES||!files[oldfd].in_use||newfd<0||newfd>=MAX_FILES)return-EBADF;
    if(oldfd==newfd)return-EINVAL;
    if(files[newfd].in_use)sys_close(newfd);
    fd_copy_scalar(&files[newfd],&files[oldfd]); files[newfd].flags &= ~0x80000; files[newfd].flags |= (flags & 0x80000);
    if(files[newfd].type==FD_PIPE_R)((struct pipe_state*)files[newfd].private_data)->readers++;
    if(files[newfd].type==FD_PIPE_W)((struct pipe_state*)files[newfd].private_data)->writers++;
    return newfd;
}
int64_t sys_fcntl(int fd,int cmd,uint64_t arg){
    if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF;
    switch(cmd){
      case 0: return sys_dup_internal(fd,(int)arg,0);
      case 1: return (files[fd].flags & 0x80000) ? 1 : 0;
      case 2: files[fd].flags = (files[fd].flags & ~0x80000) | ((arg&1)?0x80000:0); return 0;
      case 3: return files[fd].flags;
      case 4: files[fd].flags = (files[fd].flags & 0x80000) | ((int)arg & ~0x80000); return 0;
      case 1030: return sys_dup_internal(fd,(int)arg,1);
      default: return-ENOSYS;
    }
}
#define TIOCGWINSZ 0x5413
struct winsize_k {uint16_t rows,cols,xpixel,ypixel;};
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TIOCGWINSZ 0x5413
#define TERMIOS_K_SIZE 60
static uint8_t console_termios_k[TERMIOS_K_SIZE] = {
    0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x00,0x1c,0x00,0x7f,0x15,0x04,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
static int fd_is_terminal(int fd){
    return fd>=0 && fd<MAX_FILES && files[fd].in_use && files[fd].type==FD_PSEUDO &&
           ((int)(uintptr_t)files[fd].private_data==DEV_CONSOLE ||
            (int)(uintptr_t)files[fd].private_data==DEV_TTY);
}
int64_t sys_ioctl(int fd,uint64_t req,uint64_t arg){
    if(req==TIOCGWINSZ && (fd==1||fd==2||fd_is_terminal(fd))){
        struct winsize_k w={30,80,640,480};
        return user_copy_to(arg,(uint8_t*)&w,sizeof(w))?-EFAULT:0;
    }
    if(!fd_is_terminal(fd)) return -ENOTTY;
    if(req==TCGETS) return user_copy_to(arg,console_termios_k,TERMIOS_K_SIZE)?-EFAULT:0;
    if(req==TCSETS) return user_copy_from(console_termios_k,arg,TERMIOS_K_SIZE)?-EFAULT:0;
    return -ENOTTY;
}

static uint64_t mmap_next=USER_MMAP_MIN;
#define PROT_READ 1ULL
#define PROT_WRITE 2ULL
#define PROT_EXEC 4ULL
#define MAP_FIXED 0x10ULL
#define MAP_ANON 0x20ULL
#define MAP_PRIVATE 0x2ULL
static uint64_t prot_to_pte(uint64_t prot){
    uint64_t f=0;
    if(prot&PROT_WRITE)f|=PTE_W;
    if(!(prot&PROT_EXEC))f|=PTE_NX;
    return f;
}

int64_t sys_mmap(uint64_t addr,uint64_t len,uint64_t prot,uint64_t flags,int fd,uint64_t off){
    (void)off;
    if(!len || len>(1ULL<<30))return-EINVAL;
    if(user_prot_wx(prot))return-EACCES_K;
    if(!(flags&MAP_ANON)&&fd<0)return-EBADF;
    if(flags&MAP_FIXED)return-EINVAL;
    uint64_t size=(len+PAGE_SIZE-1)&PAGE_MASK;
    if(size==0 || size>USER_MMAP_MAX-USER_MMAP_MIN)return-ENOMEM;
    uint64_t base=(mmap_next+PAGE_SIZE-1)&PAGE_MASK;
    if(base<USER_MMAP_MIN || base>USER_MMAP_MAX || size>USER_MMAP_MAX-base)return-ENOMEM;
    if(!user_addr_range_ok(base,size))return-ENOMEM;
    uint64_t mapped=0;
    for(uint64_t va=base;va<base+size;va+=PAGE_SIZE){
        uint64_t phys=pmm_alloc_page();
        if(!phys){
            for(uint64_t undo=base;undo<base+mapped;undo+=PAGE_SIZE){
                uint64_t old_phys=0;
                if(vmm_user_unmap(undo,&old_phys)==0 && old_phys) pmm_free_page(old_phys);
            }
            return-ENOMEM;
        }
        uint8_t*p=(uint8_t*)pmm_phys_to_virt(phys);
        if(!p){
            pmm_free_page(phys);
            for(uint64_t undo=base;undo<base+mapped;undo+=PAGE_SIZE){
                uint64_t old_phys=0;
                if(vmm_user_unmap(undo,&old_phys)==0 && old_phys) pmm_free_page(old_phys);
            }
            return-ENOMEM;
        }
        for(size_t i=0;i<PAGE_SIZE;i++)p[i]=0;
        if(vmm_user_map(va,phys,prot_to_pte(prot))){
            pmm_free_page(phys);
            for(uint64_t undo=base;undo<base+mapped;undo+=PAGE_SIZE){
                uint64_t old_phys=0;
                if(vmm_user_unmap(undo,&old_phys)==0 && old_phys) pmm_free_page(old_phys);
            }
            return-ENOMEM;
        }
        mapped+=PAGE_SIZE;
    }
    mmap_next=base+size;
    return base;
}

int64_t sys_munmap(uint64_t addr,uint64_t len){
    if(addr&(PAGE_SIZE-1)||!len)return-EINVAL;
    uint64_t size=(len+PAGE_SIZE-1)&PAGE_MASK;
    if(size==0 || !user_addr_range_ok(addr,size))return-EINVAL;
    if(addr<USER_MMAP_MIN || addr>=USER_MMAP_MAX || size>USER_MMAP_MAX-addr)return-EINVAL;

    for(uint64_t va=addr;va<addr+size;va+=PAGE_SIZE){
        uint64_t phys;
        if(vmm_user_translate(va,&phys,NULL)!=0) return-EINVAL;
    }

    for(uint64_t va=addr;va<addr+size;va+=PAGE_SIZE){
        uint64_t phys=0;
        if(vmm_user_unmap(va,&phys)!=0) return-EFAULT;
        if(phys)pmm_free_page(phys);
    }
    return 0;
}

int64_t sys_mprotect(uint64_t addr,uint64_t len,uint64_t prot){
    if(addr&(PAGE_SIZE-1)||!len)return-EINVAL;
    if(user_prot_wx(prot))return-EACCES_K;
    uint64_t size=(len+PAGE_SIZE-1)&PAGE_MASK;
    if(size==0 || !user_addr_range_ok(addr,size))return-EINVAL;
    if(addr<USER_MMAP_MIN || addr>=USER_MMAP_MAX || size>USER_MMAP_MAX-addr)return-EINVAL;
    for(uint64_t va=addr;va<addr+size;va+=PAGE_SIZE)
        if(vmm_user_protect(va,prot_to_pte(prot)))return-EFAULT;
    return 0;
}

int64_t sys_mremap(uint64_t old_addr,uint64_t old_size,uint64_t new_size,uint64_t flags,uint64_t new_addr){
    (void)flags;
    if(!old_size||!new_size)return-EINVAL;
    if(!user_addr_range_ok(old_addr,old_size))return-EINVAL;
    if(old_addr<USER_MMAP_MIN || old_addr>=USER_MMAP_MAX)return-EINVAL;
    if(old_size==new_size)return old_addr;
    if(new_size<old_size){
        sys_munmap(old_addr+((new_size+PAGE_SIZE-1)&PAGE_MASK),old_size-new_size);
        return old_addr;
    }
    uint64_t n=(uint64_t)sys_mmap(new_addr,new_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if((int64_t)n<0)return n;
    uint64_t copy=old_size<new_size?old_size:new_size;
    uint8_t tmp[256];
    for(uint64_t o=0;o<copy;o+=sizeof(tmp)){
        uint64_t q=copy-o;if(q>sizeof(tmp))q=sizeof(tmp);
        if(user_copy_from(tmp,old_addr+o,q)||user_copy_to(n+o,tmp,q)){
            sys_munmap(n,new_size);
            return-EFAULT;
        }
    }
    sys_munmap(old_addr,old_size);
    return n;
}

static uint64_t heap_end=USER_HEAP_BASE;
static uint64_t heap_base=USER_HEAP_BASE;
int64_t sys_brk(uint64_t addr){
    if(addr==0)return heap_end;
    if(addr<heap_base||addr>=USER_HEAP_MAX)return heap_end;
    uint64_t old=heap_end;
    if(addr>old){
        uint64_t a=(old+PAGE_SIZE-1)&PAGE_MASK;
        uint64_t b=(addr+PAGE_SIZE-1)&PAGE_MASK;
        if(b>a && !user_addr_range_ok(a,b-a))return heap_end;
        for(;a<b;a+=PAGE_SIZE){
            uint64_t phys=pmm_alloc_page();
            if(!phys)return heap_end;
            uint8_t*p=(uint8_t*)pmm_phys_to_virt(phys);
            if(!p){pmm_free_page(phys);return heap_end;}
            for(size_t i=0;i<PAGE_SIZE;i++)p[i]=0;
            if(vmm_user_map(a,phys,PTE_W|PTE_NX)){
                pmm_free_page(phys);
                return heap_end;
            }
        }
    }else if(addr<old){
        uint64_t a=(addr+PAGE_SIZE-1)&PAGE_MASK,b=(old+PAGE_SIZE-1)&PAGE_MASK;
        for(;a<b;a+=PAGE_SIZE){
            uint64_t phys;
            if(vmm_user_unmap(a,&phys)==0&&phys)pmm_free_page(phys);
        }
    }
    heap_end=addr;
    return heap_end;
}

static int current_pid=1,current_ppid=0,current_tid=1; static int *tid_addr;
int64_t sys_getpid(void){
    task_init_root();
    return (int64_t)(tasks[current_task].tgid ? tasks[current_task].tgid : tasks[current_task].pid);
}
int64_t sys_getppid(void){ task_init_root(); return (int64_t)tasks[current_task].ppid; }
int64_t sys_gettid(void){ task_init_root(); return (int64_t)tasks[current_task].pid; }
int64_t sys_getuid(void){return 0;} int64_t sys_geteuid(void){return 0;} int64_t sys_getgid(void){return 0;} int64_t sys_getegid(void){return 0;}
int64_t sys_set_tid_address(int *p) {
    task_init_root();
    if (!p || !user_range_ok((uint64_t)p, sizeof(int), 1)) return -EFAULT;
    tasks[current_task].tid_address = (uint64_t)p;
    return tasks[current_task].pid;
}
#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004
int64_t sys_arch_prctl(uint64_t code,uint64_t addr){
    uint64_t v;
    task_init_root();
    if(code==ARCH_SET_FS){
        if(addr>=USER_LIMIT)return-EFAULT;
        if(addr && vmm_user_translate(addr, NULL, NULL))return-EFAULT;
        fs_base=addr; tasks[current_task].ctx.fs_base=addr;
        v=addr; __asm__ volatile("wrmsr"::"c"(0xC0000100),"a"((uint32_t)v),"d"((uint32_t)(v>>32)));
        return 0;
    }
    if(code==ARCH_SET_GS){
        if(addr>=USER_LIMIT)return-EFAULT;
        if(addr && vmm_user_translate(addr, NULL, NULL))return-EFAULT;
        gs_base=addr; tasks[current_task].ctx.gs_base=addr;
        v=addr; __asm__ volatile("wrmsr"::"c"(0xC0000101),"a"((uint32_t)v),"d"((uint32_t)(v>>32)));
        return 0;
    }
    if(code==ARCH_GET_FS){

        if(!addr || !user_range_ok(addr, sizeof(v), 1)) return -EFAULT;

        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100));
        v = ((uint64_t)hi << 32) | lo;
        if (current_task >= 0 && current_task < MAX_TASKS && tasks[current_task].used)
            tasks[current_task].ctx.fs_base = v;
        fs_base = v;
        return user_copy_to(addr,(uint8_t*)&v,8)?-EFAULT:0;
    }
    if(code==ARCH_GET_GS){
        if(!addr || !user_range_ok(addr, sizeof(v), 1)) return -EFAULT;
        v = (current_task >= 0 && current_task < MAX_TASKS && tasks[current_task].used)
              ? tasks[current_task].ctx.gs_base : gs_base;
        return user_copy_to(addr,(uint8_t*)&v,8)?-EFAULT:0;
    }
    return-EINVAL;
}

struct timeval_k{int64_t tv_sec,tv_usec;};
int64_t sys_gettimeofday(void*tv,void*tz){(void)tz;uint64_t s,n;rtc_now(&s,&n);if(tv){struct timeval_k t={(int64_t)s,(int64_t)(n/1000)};if(user_copy_to((uint64_t)tv,(uint8_t*)&t,sizeof(t)))return-EFAULT;}return 0;}
int64_t sys_clock_gettime(int id,void*tp){if(id!=0&&id!=1&&id!=4)return-EINVAL;uint64_t ns;if(id==1||id==4)ns=kernel_monotonic_ns();else{uint64_t s,n;rtc_now(&s,&n);ns=s*1000000000ULL+n;}struct timespec_k t={(int64_t)(ns/1000000000ULL),(int64_t)(ns%1000000000ULL)};return user_copy_to((uint64_t)tp,(const uint8_t*)&t,sizeof(t))?-EFAULT:0;}
int64_t sys_clock_getres(int id,void*tp){if(id!=0&&id!=1&&id!=4)return-EINVAL;struct timespec_k t={1,0};return user_copy_to((uint64_t)tp,(uint8_t*)&t,sizeof(t))?-EFAULT:0;}
static uint32_t rand_state=0x13579bdf; int64_t sys_getrandom(void*buf,uint64_t len,uint32_t flags){(void)flags;if(len>(1<<20))return-EINVAL;uint8_t tmp[64];uint64_t done=0;while(done<len){uint64_t n=len-done;if(n>sizeof(tmp))n=sizeof(tmp);for(uint64_t i=0;i<n;i++){pseudo_rand_state^=pseudo_rand_state<<13;pseudo_rand_state^=pseudo_rand_state>>17;pseudo_rand_state^=pseudo_rand_state<<5;tmp[i]=(uint8_t)rand_state;}if(user_copy_to((uint64_t)buf+done,tmp,n))return-EFAULT;done+=n;}return len;}
static int task_cwd_path(char *out, size_t cap){
    if(!out || cap<2) return -ERANGE;
    struct vfs_node *cur=tasks[current_task].cwd;
    if(!cur) cur=vfs_root();
    char names[32][VFS_NAME_MAX+1]; int depth=0;
    while(cur && cur!=vfs_root() && depth<(int)(sizeof(names)/sizeof(names[0]))){

        if(cur->parent && cur->parent->mounted && cur->parent->mounted->root==cur)
            cur=cur->parent;
        size_t n=0;
        while(cur->name[n] && n<VFS_NAME_MAX){ names[depth][n]=cur->name[n]; n++; }
        names[depth][n]=0;
        depth++;
        cur=cur->parent;
    }
    size_t on=0;
    if(depth==0){ out[0]='/'; out[1]=0; return 1; }
    for(int i=depth-1;i>=0;i--){
        size_t n=0; while(names[i][n]) n++;
        if(on+n+1>=cap) return -ERANGE;
        out[on++]='/';
        for(size_t j=0;j<n;j++) out[on++]=names[i][j];
    }
    out[on]=0;
    return (int)on;
}

static int normalize_abs_path(const char *in, char *out, size_t cap) {
    if(!in || !out || cap < 2 || in[0] != '/') return -EINVAL;
    char parts[32][VFS_NAME_MAX+1];
    size_t depth=0, i=1;
    while(in[i]) {
        while(in[i]=='/') i++;
        if(!in[i]) break;
        char part[VFS_NAME_MAX+1]; size_t n=0;
        while(in[i] && in[i]!='/') {
            if(n >= VFS_NAME_MAX) return -ENAMETOOLONG;
            part[n++]=in[i++];
        }
        part[n]=0;
        if(!n || (n==1 && part[0]=='.')) continue;
        if(n==2 && part[0]=='.' && part[1]=='.') {
            if(depth) depth--;
            continue;
        }
        if(depth >= 32) return -ENAMETOOLONG;
        for(size_t j=0;j<=n;j++) parts[depth][j]=part[j];
        depth++;
    }
    size_t pos=0; out[pos++]='/';
    for(size_t k=0;k<depth;k++) {
        size_t n=0; while(parts[k][n]) n++;
        if(pos>1) { if(pos+1>=cap) return -ENAMETOOLONG; out[pos++]='/'; }
        if(pos+n>=cap) return -ENAMETOOLONG;
        for(size_t j=0;j<n;j++) out[pos++]=parts[k][j];
    }
    out[pos]=0;
    return 0;
}

static int resolve_process_path(const char *in, char *out, size_t cap) {
    if(!in || !out || cap<2) return -EINVAL;
    char raw[128];
    size_t n=0;
    if(in[0]=='/') {
        while(in[n] && n+1<sizeof(raw)) { raw[n]=in[n]; n++; }
        if(in[n]) return -ENAMETOOLONG;
        raw[n]=0;
    } else {
        char base[128];
        int bn=task_cwd_path(base,sizeof(base));
        if(bn<0) return bn;
        n=0;
        while(base[n] && n+1<sizeof(raw)) { raw[n]=base[n]; n++; }
        if(base[n]) return -ENAMETOOLONG;
        if(n>1) { if(n+1>=sizeof(raw)) return -ENAMETOOLONG; raw[n++]='/'; }
        else if(n==0) raw[n++]='/';
        size_t i=0;
        while(in[i] && n+1<sizeof(raw)) raw[n++]=in[i++];
        if(in[i]) return -ENAMETOOLONG;
        raw[n]=0;
    }
    return normalize_abs_path(raw,out,cap);
}

int64_t sys_getcwd(char*buf,uint64_t size){
    task_init_root();
    if(!buf||size<2)return-ERANGE;
    char out[128];
    int n=task_cwd_path(out,sizeof(out));
    if(n<0 || size<=(uint64_t)n)return-ERANGE;
    return user_copy_to((uint64_t)buf,(const uint8_t*)out,(uint64_t)n+1)?-EFAULT:(int64_t)(n+1);
}
int64_t sys_chdir(const char*path){
    task_init_root();
    char p[128]; if(user_copy_cstr(p,(uint64_t)path,sizeof(p)))return-EFAULT;
    char abs[128];
    if(p[0]=='/') {
        size_t n=0; while(p[n] && n<sizeof(abs)-1){abs[n]=p[n];n++;} abs[n]=0;
    } else {
        int base=task_cwd_path(abs,sizeof(abs));
        if(base<0)return-ERANGE;
        size_t n=(size_t)base;
        if(n>1){ if(n+1>=sizeof(abs))return-ENAMETOOLONG; abs[n++]='/'; }
        for(size_t i=0;p[i];i++){ if(n+1>=sizeof(abs))return-ENAMETOOLONG; abs[n++]=p[i]; }
        abs[n]=0;
    }
    struct vfs_node *n=vfs_lookup(abs);
    if(!n)return-ENOENT;
    if(!vfs_is_dir(n))return-ENOTDIR;
    tasks[current_task].cwd=n;
    return 0;
}
struct pollfd_k{int fd;short events,revents;};
#define POLLIN 1
#define POLLOUT 4
int64_t sys_poll(void*ufds,uint64_t nfds,int timeout){(void)timeout;if(nfds>64)return-EINVAL;int ready=0;struct pollfd_k p;for(uint64_t i=0;i<nfds;i++){if(user_copy_from((uint8_t*)&p,(uint64_t)ufds+i*sizeof(p),sizeof(p)))return-EFAULT;p.revents=0;if(p.fd==1||p.fd==2){if(p.events&POLLOUT)p.revents|=POLLOUT;}else if(p.fd>=0&&p.fd<MAX_FILES&&files[p.fd].in_use){if(files[p.fd].type==FD_FILE&&p.events&POLLIN&&files[p.fd].offset<files[p.fd].size)p.revents|=POLLIN;if(files[p.fd].type==FD_PIPE_R){struct pipe_state *pp=(struct pipe_state*)files[p.fd].private_data;if((p.events&POLLIN)&&pp->count)p.revents|=POLLIN;if(!pipe_has_writers(pp))p.revents|=0x010;} else if(files[p.fd].type==FD_PIPE_W){struct pipe_state *pp=(struct pipe_state*)files[p.fd].private_data;if(!pipe_has_readers(pp))p.revents|=0x008;else if(p.events&POLLOUT&&pp->count<PIPE_CAP)p.revents|=POLLOUT;}}if(p.revents)ready++;if(user_copy_to((uint64_t)ufds+i*sizeof(p),(uint8_t*)&p,sizeof(p)))return-EFAULT;}return ready;}
int64_t sys_select(int nfds,void*ur,void*uw,void*ue,void*utv){(void)ue;(void)utv;if(nfds<0||nfds>64)return-EINVAL;uint64_t r=0,w=0;uint8_t b[8];for(int fd=0;fd<nfds;fd++){if(ur&&user_copy_from(b,(uint64_t)ur+fd/8,1)==0&&(b[0]&(1<<(fd%8)))&&fd>=0&&fd<MAX_FILES&&files[fd].in_use&&files[fd].type==FD_FILE&&files[fd].offset<files[fd].size)r++;if(uw&&user_copy_from(b,(uint64_t)uw+fd/8,1)==0&&(b[0]&(1<<(fd%8)))&&(fd==1||fd==2))w++;}return (int64_t)(r+w);}
struct sysinfo_k{long uptime;uint64_t loads[3],totalram,freeram,sharedram,bufferram,totalhigh,freehigh;uint32_t mem_unit,_pad[3];};
int64_t sys_sysinfo(void*ui){struct sysinfo_k x;for(size_t i=0;i<sizeof(x);i++)((uint8_t*)&x)[i]=0;x.uptime=(long)(kernel_monotonic_ns()/1000000000ULL);x.totalram=64ULL*1024*1024;x.freeram=pmm_free_count()*4096ULL;x.mem_unit=1;return user_copy_to((uint64_t)ui,(uint8_t*)&x,sizeof(x))?-EFAULT:0;}

struct linux_dirent64_k { uint64_t ino, off; unsigned short reclen; unsigned char type; char name[0]; } __attribute__((packed));
static int dirent_put(uint8_t *dst,uint64_t cap,uint64_t *pos,uint64_t ino,uint8_t type,const char *name,uint64_t next){ size_t nl=0;while(name[nl])nl++; uint16_t reclen=(uint16_t)((20+nl+7)&~7); if(*pos+reclen>cap)return 0; struct linux_dirent64_k *d=(struct linux_dirent64_k*)(dst+*pos);d->ino=ino;d->off=next;d->reclen=reclen;d->type=type;for(size_t i=0;i<=nl;i++)d->name[i]=name[i];*pos+=reclen;return 1; }
int64_t sys_getdents64(int fd,void *udirent,uint64_t count){
    if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF; struct file_desc*f=&files[fd]; if(f->type!=FD_PSEUDO||!f->vnode||!vfs_is_dir(f->vnode))return-ENOTDIR;
    uint8_t tmp[1024];uint64_t cap=count<sizeof(tmp)?count:sizeof(tmp),pos=0,idx=f->offset;
    if(idx==0){if(!dirent_put(tmp,cap,&pos,vfs_ino(f->vnode),4,".",1))return 0;idx=1;}
    if(idx==1){uint64_t pino=f->vnode->parent?vfs_ino(f->vnode->parent):vfs_ino(f->vnode);if(!dirent_put(tmp,cap,&pos,pino,4,"..",2)){f->offset=1;if(pos&&user_copy_to((uint64_t)udirent,tmp,pos))return-EFAULT;return(int64_t)pos;}idx=2;}
    uint64_t child_idx=idx-2,total=vfs_child_count(f->vnode);
    for(;child_idx<total;child_idx++){struct vfs_node*n=vfs_child_at(f->vnode,child_idx);if(!n)break;uint8_t dtype=vfs_is_dir(n)?4:(vfs_type(n)==VFS_NODE_SYMLINK?10:8);uint64_t next=child_idx+3;if(!dirent_put(tmp,cap,&pos,vfs_ino(n),dtype,n->name,next))break;idx=next;}
    f->offset=idx;if(pos&&user_copy_to((uint64_t)udirent,tmp,pos))return-EFAULT;return(int64_t)pos;
}
int64_t sys_readlink(const char *path,char *buf,uint64_t size){char raw[128],p[128];if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT;if(resolve_process_path(raw,p,sizeof(p))<0)return-ENAMETOOLONG;const char*t=NULL;struct vfs_node*n=vfs_lookup(p);if(n&&vfs_type(n)==VFS_NODE_SYMLINK)t=vfs_link_target(n);if(!t&&(pseudo_eq(p,"/proc/self/exe")||pseudo_eq(p,"/proc/1/exe")))t=current_exe;if(!t)return-ENOENT;uint64_t nbytes=0;while(t[nbytes]&&nbytes<size)nbytes++;if(nbytes&&user_copy_to((uint64_t)buf,(const uint8_t*)t,nbytes))return-EFAULT;return nbytes;}
int64_t sys_readlinkat(int dirfd,const char*path,char*buf,uint64_t size){if(dirfd!=-100)return-ENOTDIR;return sys_readlink(path,buf,size);}

int64_t sys_mkdir(const char *path, int mode) {
    char raw[128],p[128]; if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT; if(resolve_process_path(raw,p,sizeof(p))<0)return-ENAMETOOLONG;
    if(vfs_lookup(p)) return -EEXIST;
    if(!vfs_mkdir(p,(uint32_t)mode)) return -ENOENT;
    return 0;
}
int64_t sys_unlink(const char *path) {
    char raw[128],p[128]; if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT; if(resolve_process_path(raw,p,sizeof(p))<0)return-ENAMETOOLONG;
    struct vfs_node *n=vfs_lookup(p);
    if(!n) return -ENOENT;
    if(vfs_is_dir(n)) return -EISDIR;
    if(vfs_unlink(p)) return -EIO;
    return 0;
}

int64_t sys_rmdir(const char *path) {
    char raw[128],p[128]; if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT; if(resolve_process_path(raw,p,sizeof(p))<0)return-ENAMETOOLONG;
    struct vfs_node *n=vfs_lookup(p);
    if(!n) return -ENOENT;
    if(!vfs_is_dir(n)) return -ENOTDIR;
    if(vfs_child_count(n)!=0) return -ENOTEMPTY;
    int rr=vfs_rmdir(p);
    if(rr==-2) return -ENOTEMPTY;
    if(rr!=0) return -EIO;
    return 0;
}

int64_t sys_rename(const char *oldpath, const char *newpath) {
    char ro[128],rn[128],ao[128],an[128];
    if(user_copy_cstr(ro,(uint64_t)oldpath,sizeof(ro)))return-EFAULT;
    if(user_copy_cstr(rn,(uint64_t)newpath,sizeof(rn)))return-EFAULT;
    if(resolve_process_path(ro,ao,sizeof(ao))<0||resolve_process_path(rn,an,sizeof(an))<0)return-ENAMETOOLONG;
    struct vfs_node *src=vfs_lookup(ao); if(!src) return -ENOENT;
    struct vfs_node *parent=vfs_lookup(an);
    if(parent && vfs_is_dir(parent)) {
        const char *base=an; for(const char *q=an;*q;q++) if(*q=='/') base=q+1;
        char target[128]; size_t n=0;
        while(an[n] && n+1<sizeof(target)) {target[n]=an[n];n++;}
        if(n>1 && target[n-1]=='/') n--;
        if(n+1+0>=sizeof(target)) return -ENAMETOOLONG;
        if(n>1) target[n++]='/';
        while(*base && n+1<sizeof(target)) target[n++]=*base++;
        target[n]=0;
        for(size_t i=0;i<=n;i++) an[i]=target[i];
    }
    if(vfs_rename(ao,an)==0) return 0;
    return -EIO;
}

int64_t sys_lstat(const char *path, void *st) { return sys_stat(path, st); }
int64_t sys_openat(int dirfd, const char *path, int flags, int mode) {

    if (dirfd != -100) return -ENOTDIR;
    return sys_open(path, flags, mode);
}
int64_t sys_newfstatat(int dirfd, const char *path, void *st, int flags) {
    (void)flags;
    if (dirfd != -100) return -ENOTDIR;
    return sys_stat(path, st);
}
int64_t sys_faccessat(int dirfd, const char *path, int mode) {
    if (dirfd != -100) return -ENOTDIR;
    return sys_access(path, mode);
}
int64_t sys_pipe2(int *fds, int flags) { int64_t r=sys_pipe(fds); if(r<0)return r; int a,b; if(user_copy_from((uint8_t*)&a,(uint64_t)fds,4)||user_copy_from((uint8_t*)&b,(uint64_t)fds+4,4))return-EFAULT; if(flags & 0x800){files[a].flags|=0x800;files[b].flags|=0x800;} if(flags & 0x80000){files[a].flags|=0x80000;files[b].flags|=0x80000;} return 0; }

struct rlimit_k { uint64_t rlim_cur, rlim_max; };
#define RLIM_INFINITY (~0ULL)
#define RLIMIT_NOFILE 7
#define RLIMIT_STACK 3
int64_t sys_getrlimit(int resource, void *rlim) {
    struct rlimit_k r;
    if (resource == RLIMIT_NOFILE) r = (struct rlimit_k){MAX_FILES, MAX_FILES};
    else if (resource == RLIMIT_STACK) r = (struct rlimit_k){USER_STACK_SIZE, USER_STACK_SIZE};
    else r = (struct rlimit_k){RLIM_INFINITY, RLIM_INFINITY};
    return user_copy_to((uint64_t)rlim, (const uint8_t*)&r, sizeof(r)) ? -EFAULT : 0;
}
int64_t sys_prlimit64(int pid, int resource, const void *new_lim, void *old_lim) {
    if (pid != 0 && pid != current_pid) return -ESRCH;
    if (new_lim) return -ENOSYS;
    return old_lim ? sys_getrlimit(resource, old_lim) : 0;
}
static uint64_t process_umask = 0022;
int64_t sys_umask(uint64_t mask) { uint64_t old = process_umask; process_umask = mask & 0777; return (int64_t)old; }

#define PR_SET_NAME 15
#define PR_GET_NAME 16
int64_t sys_prctl(int option, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    static char process_name[16] = "yabroos-32";
    if (option == PR_SET_NAME) {
        char tmp[16];
        for (int i=0;i<15;i++) { if (user_copy_from((uint8_t*)&tmp[i], a2+i, 1)) return -EFAULT; if (!tmp[i]) { for(int j=i+1;j<16;j++)tmp[j]=0; break; } }
        tmp[15]=0; for(int i=0;i<16;i++)process_name[i]=tmp[i]; return 0;
    }
    if (option == PR_GET_NAME) return user_copy_to(a2, (const uint8_t*)process_name, 16) ? -EFAULT : 0;
    return -EINVAL;
}

#define FD_EPOLL 8
#define EPOLLIN  0x001
#define EPOLLOUT 0x004
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
struct epoll_watch_k { int used; int fd; uint32_t events; uint64_t data; };
struct epoll_state_k { struct epoll_watch_k w[8]; };
static struct epoll_state_k epolls[2];
static int epoll_is_ready(int fd, uint32_t events, uint32_t *revents) {
    *revents = 0;
    if (fd == 1 || fd == 2) { if (events & EPOLLOUT) *revents |= EPOLLOUT; return *revents != 0; }
    if (fd < 0 || fd >= MAX_FILES || !files[fd].in_use) return 0;
    if (files[fd].type == FD_FILE && (events & EPOLLIN) && files[fd].offset < files[fd].size) *revents |= EPOLLIN;
    if (files[fd].type == FD_PIPE_R && (events & EPOLLIN) && ((struct pipe_state*)files[fd].private_data)->count) *revents |= EPOLLIN;
    if (files[fd].type == FD_EVENTFD && (events & EPOLLIN) && ((struct eventfd_state_k*)files[fd].private_data)->value) *revents |= EPOLLIN;
    if (files[fd].type == FD_EVENTFD && (events & EPOLLOUT) && ((struct eventfd_state_k*)files[fd].private_data)->value < UINT64_MAX-1) *revents |= EPOLLOUT;
    if (files[fd].type == FD_TIMERFD) { struct timerfd_state_k*t=(struct timerfd_state_k*)files[fd].private_data; uint64_t now=clock_now_ns_simple(t->clockid); if(t->next_ns&&now>=t->next_ns&&(events&EPOLLIN))*revents|=EPOLLIN; }
    return *revents != 0;
}
int64_t sys_epoll_create1(int flags) {
    (void)flags;
    int fd = find_free_fd(); if (fd < 0) return -EMFILE;
    for (int i=0;i<2;i++) { int empty=1; for(int j=0;j<8;j++) if(epolls[i].w[j].used) empty=0; if(empty){ files[fd].in_use=1; files[fd].type=FD_EPOLL; files[fd].private_data=&epolls[i]; return fd; } }
    return -EMFILE;
}
struct epoll_event_k { uint32_t events; uint32_t pad; uint64_t data; };
int64_t sys_epoll_ctl(int epfd, int op, int fd, void *uevent) {
    if(epfd<0||epfd>=MAX_FILES||!files[epfd].in_use||files[epfd].type!=FD_EPOLL) return -EBADF;
    if(fd<0||fd>=MAX_FILES+3) return -EBADF;
    struct epoll_state_k *e=(struct epoll_state_k*)files[epfd].private_data;
    struct epoll_event_k ev;
    if(op!=EPOLL_CTL_DEL && user_copy_from((uint8_t*)&ev,(uint64_t)uevent,sizeof(ev))) return -EFAULT;
    int slot=-1;
    for(int i=0;i<8;i++) if(e->w[i].used && e->w[i].fd==fd) {slot=i;break;}
    if(op==EPOLL_CTL_DEL){if(slot<0)return-ENOENT;e->w[slot].used=0;return 0;}
    if(op==EPOLL_CTL_ADD){if(slot>=0)return-EINVAL;for(int i=0;i<8;i++)if(!e->w[i].used){slot=i;break;}if(slot<0)return-EMFILE;e->w[slot].used=1;e->w[slot].fd=fd;e->w[slot].events=ev.events;e->w[slot].data=ev.data;return 0;}
    if(op==EPOLL_CTL_MOD){if(slot<0)return-ENOENT;e->w[slot].events=ev.events;e->w[slot].data=ev.data;return 0;}
    return -EINVAL;
}
int64_t sys_epoll_wait(int epfd, void *uevents, int maxevents, int timeout) {
    (void)timeout;
    if(epfd<0||epfd>=MAX_FILES||!files[epfd].in_use||files[epfd].type!=FD_EPOLL)return-EBADF;
    if(maxevents<=0||maxevents>8)return-EINVAL;
    struct epoll_state_k *e=(struct epoll_state_k*)files[epfd].private_data;
    struct epoll_event_k out[8]; int n=0;
    for(int i=0;i<8&&n<maxevents;i++) if(e->w[i].used){uint32_t rev;if(epoll_is_ready(e->w[i].fd,e->w[i].events,&rev)){out[n].events=rev;out[n].pad=0;out[n].data=e->w[i].data;n++;}}
    if(n && user_copy_to((uint64_t)uevents,(const uint8_t*)out,(uint64_t)n*sizeof(out[0])))return-EFAULT;
    return n;
}
int64_t sys_ftruncate(int fd, uint64_t len) {
    if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF;
    if(files[fd].type!=FD_FILE)return-ESPIPE;
    if(!files[fd].vnode || !vfs_file_is_regular(files[fd].vnode)) return -EINVAL;
    if(vfs_file_truncate(files[fd].vnode,len)!=0)return-EINVAL;
    files[fd].size=len; return 0;
}

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CMD_MASK 127
static int futex_user_load(uint32_t *uaddr, uint32_t *value) {
    return user_copy_from((uint8_t*)value, (uint64_t)uaddr, sizeof(*value));
}
static int task_switch_from_current_blocked(void) {
    task_t *cur=&tasks[current_task];
    int next=task_pick_next();
    if(next<0) return -1;
    cur->state=TASK_BLOCKED;
    tasks[next].state=TASK_RUNNING;
    current_task=next;
    (void)vmm_user_activate(tasks[next].ctx.cr3);
    task_switch_target=next;
    task_switch_pending=1;
    task_return_ctx=&tasks[next].ctx;
    return 0;
}
int64_t sys_futex(syscall_frame_t *f, uint32_t *uaddr, int op, uint32_t val, const void *timeout,
                  uint32_t *uaddr2, uint32_t val3) {
    (void)uaddr2; (void)val3;
    task_init_root();
    if (!uaddr || ((uint64_t)uaddr & 3)) return -EINVAL;
    int cmd = op & FUTEX_CMD_MASK;
    if (cmd == FUTEX_WAIT) {
        uint32_t cur;
        if (futex_user_load(uaddr, &cur)) return -EFAULT;
        if (cur != val) return -EAGAIN;
        if (timeout && !user_range_ok((uint64_t)timeout, 16, 0)) return -EFAULT;
        if (timeout) return -ENOSYS;
        task_t *t=&tasks[current_task];
        task_save_from_frame(t, f);
        t->futex_addr=(uint64_t)uaddr;
        t->ctx.rax=0;
        if (task_switch_from_current_blocked() != 0) {
            t->futex_addr=0;
            t->state=TASK_RUNNING;
            return -EAGAIN;
        }
        return 0;
    }
    if (cmd == FUTEX_WAKE) {
        if (!user_range_ok((uint64_t)uaddr, sizeof(uint32_t), 0)) return -EFAULT;
        uint32_t woke=0;
        for(int i=0;i<MAX_TASKS && woke<val;i++) {
            task_t *t=&tasks[i];
            if(!t->used || t->state!=TASK_BLOCKED || t->futex_addr!=(uint64_t)uaddr) continue;
            t->futex_addr=0;
            t->ctx.rax=0;
            t->state=TASK_RUNNABLE;
            woke++;
        }
        return (int64_t)woke;
    }
    return -ENOSYS;
}

int64_t sys_nanosleep(const void *req, void *rem) {
    struct { int64_t tv_sec; int64_t tv_nsec; } ts;
    if (!req || user_copy_from((uint8_t*)&ts, (uint64_t)req, sizeof(ts))) return -EFAULT;
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL) return -EINVAL;

    if (ts.tv_sec == 0 && ts.tv_nsec == 0) return 0;
    volatile uint64_t spins = (uint64_t)ts.tv_nsec / 100 + (uint64_t)ts.tv_sec * 1000000ULL;
    if (spins > 5000000ULL) spins = 5000000ULL;
    while (spins--) __asm__ volatile("pause");
    if (rem) { struct { int64_t tv_sec; int64_t tv_nsec; } z={0,0}; (void)user_copy_to((uint64_t)rem,(const uint8_t*)&z,sizeof(z)); }
    return 0;
}

int64_t sys_sched_setaffinity(int pid, uint64_t cpusetsize, const void *mask) {
    (void)pid;
    if (!mask || cpusetsize == 0 || cpusetsize > 128) return -EINVAL;
    uint8_t tmp[128];
    return user_copy_from(tmp,(uint64_t)mask,cpusetsize) ? -EFAULT : 0;
}

int64_t sys_sched_getaffinity(int pid, uint64_t cpusetsize, void *mask) {
    (void)pid;
    if (!mask || cpusetsize == 0 || cpusetsize > 128) return -EINVAL;
    uint8_t tmp[128]={0}; tmp[0]=1;
    return user_copy_to((uint64_t)mask,tmp,cpusetsize) ? -EFAULT : (int64_t)cpusetsize;
}

int64_t sys_set_robust_list(void *head, uint64_t len) {
    if (len != 24 || !head || !user_range_ok((uint64_t)head,len,0)) return -EINVAL;
    musl_robust_head=(uint64_t)head; musl_robust_len=len; return 0;
}

int64_t sys_get_robust_list(int pid, void **head_ptr, uint64_t *len_ptr) {
    if (pid != 0 && pid != 1 && pid != (int)sys_getpid()) return -ESRCH;
    if (!head_ptr || !len_ptr) return -EFAULT;
    if (user_copy_to((uint64_t)head_ptr,(const uint8_t*)&musl_robust_head,8)) return -EFAULT;
    if (user_copy_to((uint64_t)len_ptr,(const uint8_t*)&musl_robust_len,8)) return -EFAULT;
    return 0;
}

int64_t sys_getcpu(unsigned *cpu, unsigned *node, void *tcache) {
    (void)tcache; unsigned zero=0;
    if (cpu && user_copy_to((uint64_t)cpu,(const uint8_t*)&zero,4)) return -EFAULT;
    if (node && user_copy_to((uint64_t)node,(const uint8_t*)&zero,4)) return -EFAULT;
    return 0;
}

static void task_clear_child_tid(task_t *t) {
    if (!t || !t->tid_address) return;
    uint32_t zero = 0;
    (void)user_copy_to(t->tid_address, (const uint8_t*)&zero, sizeof(zero));

    for (int i=0; i<MAX_TASKS; i++) {
        task_t *w = &tasks[i];
        if (!w->used || w->state != TASK_BLOCKED || w->futex_addr != t->tid_address) continue;
        w->futex_addr = 0;
        w->ctx.rax = 0;
        w->state = TASK_RUNNABLE;
    }
    t->tid_address = 0;
}

int64_t sys_exit(int status) {
    task_init_root();
    if (current_task == 0) {
        user_exit_status = status;
        user_exit_pending = 1;
        return 0;
    }

    task_t *t = &tasks[current_task];
    task_clear_child_tid(t);
    task_fd_table_release(t);
    t->exit_status = status;
    t->state = TASK_ZOMBIE;

    if (t->ppid > 0) {
        for (int i=0; i<MAX_TASKS; i++) {
            task_t *p = &tasks[i];
            if (!p->used || p->pid != t->ppid || p->state != TASK_BLOCKED || !p->wait4_active)
                continue;
            if (p->wait4_pid > 0 && p->wait4_pid != t->pid) continue;

            int wait_status = (t->exit_status & 0xff) << 8;

            if (vmm_user_activate(p->ctx.cr3) != 0) continue;
            if (p->wait4_status_ptr &&
                user_copy_to(p->wait4_status_ptr, (const uint8_t *)&wait_status, sizeof(wait_status)))
                continue;

            p->ctx.rax = (uint64_t)t->pid;
            p->wait4_active = 0;
            p->wait4_pid = -1;
            p->wait4_status_ptr = 0;
            t->used = 0;
            t->state = TASK_UNUSED;
            p->state = TASK_RUNNABLE;
            break;
        }
    }

    int next = -1;

    if (t->ppid > 0) {
        for (int i=0; i<MAX_TASKS; i++) {
            if (tasks[i].used && tasks[i].pid == t->ppid && tasks[i].state == TASK_RUNNABLE) {
                next = i; break;
            }
        }
    }
    if (next < 0) next = task_pick_next();
    if (next < 0) next = 0;

    tasks[next].state = TASK_RUNNING;
    current_task = next;
    (void)vmm_user_activate(tasks[next].ctx.cr3);
    task_switch_target = next;
    task_switch_pending = 1;
    task_return_ctx = &tasks[next].ctx;
    return 0;
}

int64_t sys_exit_group(int status) {
    task_init_root();

    if (current_task != 0)
        return sys_exit(status);

    debugcon_puts("\n[EXIT] User program exited with code: ");
    char buf[20]; int s = status;
    if (s < 0) { debugcon_putc('-'); s = -s; }
    int div = 1; while (div <= s/10) div *= 10;
    while (div > 0) { debugcon_putc('0' + (s/div)); s %= div; div /= 10; }
    debugcon_puts("\n");
    user_exit_status = status; user_exit_pending = 1; return 0;
}

int64_t sys_sched_yield(syscall_frame_t *f) {
    task_init_root();
    task_t *cur=&tasks[current_task];
    task_save_from_frame(cur, f);
    cur->ctx.rax=0;
    int next=task_pick_next();
    if(next<0) {

        __asm__ volatile("sti; hlt; cli" ::: "memory");
        return 0;
    }
    cur->state=TASK_RUNNABLE; tasks[next].state=TASK_RUNNING;
    current_task=next; (void)vmm_user_activate(tasks[next].ctx.cr3); task_switch_target=next; task_switch_pending=1; task_return_ctx=&tasks[next].ctx;
    return 0;
}

int64_t sys_clone(syscall_frame_t *f, uint64_t flags, uint64_t child_stack, uint64_t ptid, uint64_t ctid, uint64_t tls) {
    task_init_root();
    if ((flags & CLONE_SETTLS) && tls >= USER_LIMIT) return -EFAULT;
    const uint64_t allowed=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID;
    if ((flags & ~allowed) != 0) return -EINVAL;
    if (!(flags & CLONE_VM)) return -ENOSYS;
    if (child_stack && (!user_range_ok(child_stack-8,8,1))) return -EFAULT;
    if ((flags & CLONE_PARENT_SETTID) && (!ptid || !user_range_ok(ptid,sizeof(int),1))) return -EFAULT;
    if ((flags & CLONE_CHILD_CLEARTID) && (!ctid || !user_range_ok(ctid,sizeof(int),1))) return -EFAULT;
    int slot=-1; for(int i=0;i<MAX_TASKS;i++) if(!tasks[i].used){slot=i;break;}
    if(slot<0)return-ENOMEM;
    task_t *parent=&tasks[current_task], *child=&tasks[slot];
    task_save_from_frame(parent,f);
    child->used=1; child->state=TASK_RUNNABLE; child->pid=next_pid++; child->ppid=parent->pid;
    child->tgid=(flags&CLONE_THREAD)?parent->tgid:child->pid; child->exit_status=0; child->tid_address=(flags&CLONE_CHILD_CLEARTID)?(uint64_t)ctid:0; child->futex_addr=0; child->pipe_wait_fd=-1; child->pipe_wait_buf=0; child->pipe_wait_count=0;
    child->wait4_active=0; child->wait4_pid=-1; child->wait4_status_ptr=0;
    child->ctx=parent->ctx; child->ctx.rax=0;
    child->sig_pending[0]=child->sig_pending[1]=0;
    child->sig_blocked[0]=parent->sig_blocked[0]; child->sig_blocked[1]=parent->sig_blocked[1];
    for (int si=1; si<=64; ++si) child->sig_actions[si]=parent->sig_actions[si];
    child->sig_saved_valid=0; child->sig_active=0; child->cwd=parent->cwd;
    task_fd_table_clone(parent, child);
    if (flags & CLONE_SETTLS) child->ctx.fs_base=tls;

    if (child_stack) {
        uint64_t src_base = parent->ctx.rsp & PAGE_MASK;
        uint64_t dst_base = child_stack & PAGE_MASK;
        uint8_t stack_page[PAGE_SIZE];
        if (user_copy_from(stack_page, src_base, PAGE_SIZE) != 0 ||
            user_copy_to(dst_base, stack_page, PAGE_SIZE) != 0) {
            child->used=0; child->state=TASK_UNUSED; return -EFAULT;
        }
        int64_t delta = (int64_t)(dst_base - src_base);
        child->ctx.rsp = (uint64_t)((int64_t)parent->ctx.rsp + delta);
        if (parent->ctx.rbp >= src_base && parent->ctx.rbp < src_base + PAGE_SIZE)
            child->ctx.rbp = (uint64_t)((int64_t)parent->ctx.rbp + delta);
        else
            child->ctx.rbp = child->ctx.rsp;
    }
    if((flags & CLONE_PARENT_SETTID) && ptid && user_copy_to(ptid,(const uint8_t*)&child->pid,sizeof(int))) {child->used=0;return-EFAULT;}
    if((flags & CLONE_CHILD_CLEARTID) && ctid && user_copy_to(ctid,(const uint8_t*)&child->pid,sizeof(int))) {child->used=0;return-EFAULT;}
    parent->ctx.rax=child->pid;
    if (flags & CLONE_THREAD) child->tgid=parent->tgid;

    return child->pid;
}

int64_t sys_fork(syscall_frame_t *f) {

    const uint64_t flags = CLONE_FS | CLONE_FILES | CLONE_SIGHAND;
    int64_t pid = sys_clone(f, CLONE_VM | flags, 0, 0, 0, 0);
    if (pid < 0) return pid;
    task_t *parent=&tasks[current_task], *child=NULL;
    for (int i=0;i<MAX_TASKS;i++) if(tasks[i].used && tasks[i].pid==(int)pid){ child=&tasks[i]; break; }
    if (!child) return -ESRCH;
    uint64_t child_cr3=0;
    if (vmm_user_space_clone(parent->ctx.cr3,&child_cr3)!=0) {
        child->used=0; child->state=TASK_UNUSED;
        return -ENOMEM;
    }
    child->ctx.cr3=child_cr3;
    return pid;
}

static int exec_target_valid(uint64_t cr3, uint64_t entry, uint64_t stack) {
    (void)cr3;
    uint64_t phys = 0, pte = 0;

    if (!entry || !stack || entry >= USER_LIMIT || stack > USER_STACK_TOP || stack < 16) {
        debugcon_puts("[EXEC] Invalid entry/stack/limit\n");
        return 0;
    }

    if (vmm_user_translate(entry & PAGE_MASK, &phys, &pte) != 0) {
        debugcon_puts("[EXEC] Entry translate FAILED\n");
        return 0;
    }
    debug_hex64("[EXEC] Entry PTE=0x", pte);
    if (!(pte & PTE_P)) { debugcon_puts("[EXEC] ERROR: Entry not present\n"); return 0; }
    if (!(pte & PTE_U)) { debugcon_puts("[EXEC] ERROR: Entry not user-accessible!\n"); return 0; }
    if (pte & PTE_NX)   { debugcon_puts("[EXEC] ERROR: Entry not executable!\n"); return 0; }

    if (vmm_user_translate((stack - 1) & PAGE_MASK, &phys, &pte) != 0) {
        debugcon_puts("[EXEC] Stack translate FAILED\n");
        return 0;
    }
    debug_hex64("[EXEC] Stack PTE=0x", pte);
    if (!(pte & PTE_P)) { debugcon_puts("[EXEC] ERROR: Stack not present\n"); return 0; }
    if (!(pte & PTE_U)) { debugcon_puts("[EXEC] ERROR: Stack not user-accessible\n"); return 0; }
    if (!(pte & PTE_W)) { debugcon_puts("[EXEC] ERROR: Stack not writable\n"); return 0; }

    if (!user_range_ok(stack - 16, 16, 1)) {
        debugcon_puts("[EXEC] Stack range check FAILED\n");
        return 0;
    }
    debugcon_puts("[EXEC] Validation SUCCESS\n");
    return 1;
}

static int exec_build_user_stack(uint64_t cr3, uint64_t *stack_out,
                                  const char *const *kargv, int argc,
                                  const char *const *kenvp, int envc) {
    if (!stack_out || !cr3 || cr3 != vmm_user_cr3() ||
        argc < 0 || envc < 0 || argc > 16 || envc > 16)
        return -EINVAL;

    uint64_t argv_va[16] = {0};
    uint64_t envp_va[16] = {0};

    const uint64_t stack_base = USER_STACK_TOP - USER_STACK_SIZE;
    const uint64_t string_top = USER_STACK_TOP - 0x2000ULL;
    uint64_t sp = string_top;

    for (int i = envc - 1; i >= 0; --i) {
        if (!kenvp || !kenvp[i]) return -EFAULT;
        size_t n = 0;
        while (n < 255 && kenvp[i][n]) ++n;
        if (n == 255 && kenvp[i][n] != 0) return -E2BIG;
        if (sp < stack_base + 0x1000ULL || (uint64_t)(n + 1) > sp - (stack_base + 0x1000ULL))
            return -E2BIG;
        sp -= (uint64_t)n + 1ULL;
        int rc = copy_to_user(cr3, sp, (const uint8_t *)kenvp[i], (uint64_t)n + 1ULL);
        if (rc) {
            debugcon_puts("[V21] env string copy failed\n");
            debug_hex64("[V21] env copy rc=0x", (uint64_t)(uint32_t)rc);
            debug_hex64("[V21] env dst=0x", sp);
            return rc;
        }
        envp_va[i] = sp;
    }

    for (int i = argc - 1; i >= 0; --i) {
        if (!kargv || !kargv[i]) return -EFAULT;
        size_t n = 0;
        while (n < 255 && kargv[i][n]) ++n;
        if (n == 255 && kargv[i][n] != 0) return -E2BIG;
        if (sp < stack_base + 0x1000ULL || (uint64_t)(n + 1) > sp - (stack_base + 0x1000ULL))
            return -E2BIG;
        sp -= (uint64_t)n + 1ULL;
        int rc = copy_to_user(cr3, sp, (const uint8_t *)kargv[i], (uint64_t)n + 1ULL);
        if (rc) {
            debugcon_puts("[V21] argv string copy failed\n");
            debug_hex64("[V21] argv copy rc=0x", (uint64_t)(uint32_t)rc);
            debug_hex64("[V21] argv dst=0x", sp);
            return rc;
        }
        argv_va[i] = sp;
    }

    const uint64_t auxv[] = {
        6, PAGE_SIZE,
        9, 0,
        0, 0
    };
    const size_t aux_words = sizeof(auxv) / sizeof(auxv[0]);
    const size_t words = 1 + (size_t)argc + 1 + (size_t)envc + 1 + aux_words;
    const uint64_t bytes = (uint64_t)words * sizeof(uint64_t);

    const uint64_t vector_gap = 8ULL;
    const uint64_t vector_top = sp & ~0xFULL;
    if (vector_top < stack_base + 0x1000ULL + bytes + vector_gap) return -E2BIG;
    sp = vector_top - bytes - vector_gap;
    sp = (sp & ~0xFULL) | 8ULL;

    if (sp + bytes > vector_top - vector_gap) {
        debugcon_puts("[V21] exec stack/vector overlaps strings\n");
        return -E2BIG;
    }

    uint64_t off = sp;
    const uint64_t argc_u = (uint64_t)argc;
    const uint64_t null = 0;

#define EXEC_PUSH_U64(v) do { \
        uint64_t __v = (uint64_t)(v); \
        int __rc = copy_to_user(cr3, off, (const uint8_t *)&__v, sizeof(__v)); \
        if (__rc) { \
            debugcon_puts("[V21] stack vector copy failed\n"); \
            debug_hex64("[V21] vector rc=0x", (uint64_t)(uint32_t)__rc); \
            debug_hex64("[V21] vector dst=0x", off); \
            return __rc; \
        } \
        off += sizeof(__v); \
    } while (0)

    EXEC_PUSH_U64(argc_u);
    for (int i = 0; i < argc; ++i) EXEC_PUSH_U64(argv_va[i]);
    EXEC_PUSH_U64(null);
    for (int i = 0; i < envc; ++i) EXEC_PUSH_U64(envp_va[i]);
    EXEC_PUSH_U64(null);
    for (size_t i = 0; i < aux_words; ++i) EXEC_PUSH_U64(auxv[i]);
#undef EXEC_PUSH_U64

    if (!user_range_ok(sp, bytes, 1)) {
        debugcon_puts("[V21] final exec stack range check failed\n");
        return -EFAULT;
    }

    uint64_t check = 0;
    if (user_copy_from((uint8_t *)&check, sp, 8) || check != (uint64_t)argc) {
        debugcon_puts("[V21] argc verification failed\n");
        return -EFAULT;
    }

    for (int i = 0; i < argc; ++i) {
        uint64_t p = 0;
        if (user_copy_from((uint8_t *)&p, sp + 8 + (uint64_t)i * 8, 8) || p != argv_va[i]) {
            debugcon_puts("[V21] argv pointer verification failed\n");
            return -EFAULT;
        }
    }

    for (int i = 0; i < argc; ++i) {
        size_t n = 0;
        while (n < 255 && kargv[i][n]) ++n;
        for (size_t j = 0; j <= n; ++j) {
            uint8_t got = 0;
            if (user_copy_from(&got, argv_va[i] + j, 1) || got != (uint8_t)kargv[i][j]) {
                debugcon_puts("[V21] argv string verification failed\n");
                debug_hex64("[V21] argv index=0x", (uint64_t)i);
                debug_hex64("[V21] argv va=0x", argv_va[i]);
                debug_hex64("[V21] argv byte=0x", (uint64_t)j);
                return -EFAULT;
            }
        }
    }

    uint64_t env_off = sp + 8 + (uint64_t)argc * 8 + 8;
    for (int i = 0; i < envc; ++i) {
        uint64_t p = 0;
        if (user_copy_from((uint8_t *)&p, env_off + (uint64_t)i * 8, 8) || p != envp_va[i]) {
            debugcon_puts("[V21] envp pointer verification failed\n");
            return -EFAULT;
        }
    }

    debug_hex64("[EXEC-STACK] rsp=0x", sp);
    debug_hex64("[EXEC-STACK] argc=0x", (uint64_t)argc);
    if (argc > 0) debug_hex64("[EXEC-STACK] argv0=0x", argv_va[0]);
    if (argc > 1) debug_hex64("[EXEC-STACK] argv1=0x", argv_va[1]);
    if (envc > 0) debug_hex64("[EXEC-STACK] envp0=0x", envp_va[0]);
    debugcon_puts("[EXEC-STACK] argv/envp verification: OK\n");

    *stack_out = sp;
    return 0;
}

extern int kernel_execve_file_args(const char *path, const char *const *argv, size_t argc, const char *const *envp, size_t envc, uint64_t *entry_out, uint64_t *stack_out);

int64_t sys_execve(const char *path, const char *const *argv, const char *const *envp) {
    char kargv[16][256]; char kenvp[16][256];
    const char *argv_k[16], *envp_k[16];
    int argc=0, envc=0;

    char kpath[128];
    if (user_copy_cstr(kpath, (uint64_t)path, sizeof(kpath)) != 0) return -EFAULT;

    if (argv) {
        for (; argc<16; ++argc) { uint64_t p=0; if (user_copy_from((uint8_t*)&p,(uint64_t)argv + (uint64_t)argc*8,8)) return -EFAULT; if (!p) break; if (user_copy_cstr(kargv[argc],p,sizeof(kargv[argc])) != 0) return -EFAULT; argv_k[argc]=kargv[argc]; }
        if (argc==16) return -E2BIG;
    }
    if (envp) {
        for (; envc<16; ++envc) { uint64_t p=0; if (user_copy_from((uint8_t*)&p,(uint64_t)envp + (uint64_t)envc*8,8)) return -EFAULT; if (!p) break; if (user_copy_cstr(kenvp[envc],p,sizeof(kenvp[envc])) != 0) return -EFAULT; envp_k[envc]=kenvp[envc]; }
        if (envc==16) return -E2BIG;
    }

    if (argc > 0 && kargv[0][0] == 0 && kpath[0] != 0) {
        size_t n = 0;
        while (n + 1 < sizeof(kargv[0]) && kpath[n]) {
            kargv[0][n] = kpath[n];
            ++n;
        }
        kargv[0][n] = 0;
        argv_k[0] = kargv[0];
        debugcon_puts("[V21] normalized empty argv0 to exec path\n");
    }

    uint64_t entry = 0, stack = 0;
    debugcon_puts("[V21.14] execve captured argv/envp\n");
    debug_hex64("[V21.14] argc=0x", (uint64_t)argc);
    for (int i = 0; i < argc; ++i) {
        debugcon_puts("[V21.14] argv[");
        debugcon_puts(kargv[i]);
        debugcon_puts("]\n");
    }
    debug_hex64("[V21.14] envc=0x", (uint64_t)envc);
    for (int i = 0; i < envc; ++i) {
        debugcon_puts("[V21.14] envp[");
        debugcon_puts(kenvp[i]);
        debugcon_puts("]\n");
    }
    debugcon_puts("[V12.24] execve path=");
    debugcon_puts(kpath);
    debugcon_puts("\n");

    int rc = kernel_execve_file_args(kpath, argv_k, (size_t)argc, envp_k, (size_t)envc, &entry, &stack);
    if (rc != 0) {
        debugcon_puts("[V12.24] kernel_execve_file FAILED rc=");
        debugcon_putc(rc < 0 ? '-' : '+');
        uint64_t ev=(uint64_t)(rc<0?-rc:rc); char eb[20]; int ei=0; if(!ev) eb[ei++]='0'; while(ev&&ei<(int)sizeof(eb)){eb[ei++]=(char)('0'+(ev%10));ev/=10;} while(ei) debugcon_putc(eb[--ei]);
        debugcon_putc('\n');
        return rc;
    }

    uint64_t new_cr3 = vmm_user_cr3();
    if (!new_cr3) return -ENOMEM;
    for (size_t i=0;i<sizeof(current_exe)-1 && kpath[i];i++) current_exe[i]=kpath[i], current_exe[i+1]=0;
    debugcon_puts("[EXEC] argv/envp stack built by ELF loader\n");
    debug_hex64("[V21.14] new argc=0x", (uint64_t)argc);
    debug_hex64("[V21.14] new rsp=0x", stack);
    debug_hex64("[V21.14] new cr3=0x", new_cr3);
    debugcon_puts("[V12.24] kernel_execve_file OK\n");
    debug_hex64("[V12.24] new_cr3=0x", new_cr3);
    debug_hex64("[V12.24] entry=0x", entry);
    debug_hex64("[V12.24] stack=0x", stack);
    debugcon_puts("[V12.24] validating new image...\n");

    if (!new_cr3 || !exec_target_valid(new_cr3, entry, stack)) {
        debugcon_puts("[V12.24] validation FAILED\n");
        return -ENOMEM;
    }
    debugcon_puts("[V12.24] validation OK\n");

    debugcon_puts("[V12.24] checking current task...\n");
    task_init_root();
    if (current_task < 0 || current_task >= MAX_TASKS || !tasks[current_task].used) {
        debugcon_puts("[V12.24] ERROR: invalid current task\n");
        return -ESRCH;
    }

    task_t *cur = &tasks[current_task];
    debug_hex64("[V12.24] pid=0x", (uint64_t)cur->pid);

    for (int fd=3; fd<MAX_FILES; fd++) {
        if (files[fd].in_use && (files[fd].flags & 0x80000)) sys_close(fd);
    }

    cur->ctx.rax = 0;
    cur->ctx.rsp = stack;
    cur->ctx.rip = entry;
    cur->ctx.rflags = 0x202;
    cur->ctx.cr3 = new_cr3;

    syscall_user_cr3 = new_cr3;
    syscall_user_rip = entry;
    syscall_user_rsp = stack;
    syscall_user_rflags = 0x202;
    syscall_saved_rax = 0;
    user_exec_pending = 1;

    debugcon_puts("[V12.30] task context updated\n");
    debugcon_puts("[V12.30] EXEC IMAGE COMMITTED\n");
    debug_hex64("[V12.30]   pid=0x", (uint64_t)cur->pid);
    debug_hex64("[V12.30]   cr3=0x", new_cr3);
    debug_hex64("[V12.30]   rip=0x", entry);
    debug_hex64("[V12.30]   rsp=0x", stack);
    debugcon_puts("[V12.30] QUEUED DEDICATED EXEC IRETQ\n");

    return 0;
}

int64_t sys_wait4(syscall_frame_t *f, int pid, int *status, int options, void *rusage) {
    (void)rusage;
    task_init_root();
    task_t *cur = &tasks[current_task];
    if(status && !user_range_ok((uint64_t)status,sizeof(int),1)) return -EFAULT;
    if(options & ~1) return -EINVAL;

    for(int i=0;i<MAX_TASKS;i++){
        task_t *t=&tasks[i];
        if(!t->used || t->ppid!=cur->pid) continue;
        if(pid>0 && t->pid!=pid) continue;
        if(t->state!=TASK_ZOMBIE) continue;
        int got=t->pid;
        int wait_status=(t->exit_status & 0xff) << 8;
        if(status && user_copy_to((uint64_t)status,(const uint8_t*)&wait_status,sizeof(wait_status)))
            return -EFAULT;
        t->used=0; t->state=TASK_UNUSED;
        return got;
    }

    if(options & 1) return 0;
    if(!f) return -EINVAL;

    cur->wait4_active=1;
    cur->wait4_pid=pid;
    cur->wait4_status_ptr=(uint64_t)status;
    task_save_from_frame(cur,f);
    cur->state=TASK_BLOCKED;

    int next=task_pick_next();
    if(next<0){
        cur->wait4_active=0; cur->wait4_pid=-1; cur->wait4_status_ptr=0;
        cur->state=TASK_RUNNING;
        return -EAGAIN;
    }
    tasks[next].state=TASK_RUNNING;
    current_task=next;
    (void)vmm_user_activate(tasks[next].ctx.cr3);
    task_switch_target=next;
    task_switch_pending=1;
    task_return_ctx=&tasks[next].ctx;
    return 0;
}

int64_t sys_uname(void *buf) {
    if(!buf || !user_range_ok((uint64_t)buf,390,1))return-EFAULT;
    const char*names[6]={"YabroOS-32","yabroos-32","0.0.1-alpha","x86_64","YabroOS-32","localdomain"};
    uint8_t out[390];for(size_t i=0;i<390;i++)out[i]=0;
    for(int n=0;n<6;n++)for(size_t j=0;names[n][j]&&j<64;j++)out[n*65+j]=(uint8_t)names[n][j];
    return user_copy_to((uint64_t)buf,out,sizeof(out))?-EFAULT:0;
}

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#define PT_LOAD 1
#define ELF_MAGIC1 0x7F
#define ELF_MAGIC2 'E'
#define ELF_MAGIC3 'L'
#define ELF_MAGIC4 'F'

static int init_user_stack(uint64_t cr3, const Elf64_Ehdr *ehdr,
                           const Elf64_Phdr *phdrs, uint16_t phnum,
                           size_t file_size, const char *exec_path,
                           const char *const *argv, size_t argc,
                           const char *const *envp, size_t envc,
                           uint64_t *stack_out) {
    (void)cr3;

    uint64_t phdr_va = 0;

    for (uint16_t i = 0; i < phnum; ++i) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type == 6 ) {
            phdr_va = ph->p_vaddr;
            break;
        }
    }

    if (!phdr_va) {
        for (uint16_t i = 0; i < phnum; ++i) {
            const Elf64_Phdr *ph = &phdrs[i];
            if (ph->p_type != PT_LOAD) continue;
            if (ehdr->e_phoff >= ph->p_offset &&
                ehdr->e_phoff < ph->p_offset + ph->p_filesz) {
                phdr_va = ph->p_vaddr + (ehdr->e_phoff - ph->p_offset);
                break;
            }
        }
    }

    uint8_t execfn[128];
    size_t execfn_len = 0;
    if (exec_path && exec_path[0]) {
        if (exec_path[0] != '/') execfn[execfn_len++] = '/';
        for (size_t src = 0; execfn_len + 1 < sizeof(execfn) && exec_path[src]; ++src)
            execfn[execfn_len++] = (uint8_t)exec_path[src];
    }
    if (execfn_len == 0) {
        execfn[execfn_len++] = '/';
        execfn[execfn_len++] = '?';
    }
    execfn[execfn_len] = 0;

    if (argc > 16 || envc > 16) return -1;

    const char *default_argv0 = (const char *)execfn;
    if (argc == 0) {
        argv = &default_argv0;
        argc = 1;
    }

    uint64_t execfn_va = (USER_STACK_TOP - 64ULL) & ~0xFULL;
    if (copy_to_user(user_cr3, execfn_va, execfn, execfn_len + 1ULL) != 0) return -1;

    static const uint8_t random_bytes[16] = {
        0x51,0x8b,0x2d,0x71,0x19,0xc4,0x63,0xa7,
        0x9e,0x42,0xd8,0x05,0x6c,0xf1,0x37,0xb0
    };
    uint64_t random_va = (execfn_va - 16ULL) & ~0xFULL;
    if (copy_to_user(user_cr3, random_va, random_bytes, sizeof(random_bytes)) != 0) return -1;

    uint64_t argv_va[16], envp_va[16];
    uint64_t str_cursor = random_va;
    for (size_t i = 0; i < argc; ++i) {
        const char *src = argv ? argv[i] : NULL;
        if (!src) return -1;
        size_t len = 0;
        while (len < 127 && src[len]) ++len;
        if (len == 127 && src[len]) return -1;
        str_cursor = (str_cursor - (uint64_t)(len + 1)) & ~0xFULL;
        if (copy_to_user(user_cr3, str_cursor, (const uint8_t *)src, len + 1) != 0) return -1;
        argv_va[i] = str_cursor;
    }
    for (size_t i = 0; i < envc; ++i) {
        const char *src = envp ? envp[i] : NULL;
        if (!src) return -1;
        size_t len = 0;
        while (len < 127 && src[len]) ++len;
        if (len == 127 && src[len]) return -1;
        str_cursor = (str_cursor - (uint64_t)(len + 1)) & ~0xFULL;
        if (copy_to_user(user_cr3, str_cursor, (const uint8_t *)src, len + 1) != 0) return -1;
        envp_va[i] = str_cursor;
    }

    uint64_t auxv[16];
    size_t an = 0;
    if (phdr_va) { auxv[an++] = 3; auxv[an++] = phdr_va; }
    auxv[an++] = 4; auxv[an++] = sizeof(Elf64_Phdr);
    auxv[an++] = 5; auxv[an++] = phnum;
    auxv[an++] = 6; auxv[an++] = PAGE_SIZE;
    auxv[an++] = 9; auxv[an++] = ehdr->e_entry;
    auxv[an++] = 25; auxv[an++] = random_va;
    auxv[an++] = 31; auxv[an++] = execfn_va;
    auxv[an++] = 0; auxv[an++] = 0;

    const size_t qwords = 1 + (argc + 1) + (envc + 1) + an;
    const uint64_t vector_bytes = (uint64_t)qwords * sizeof(uint64_t);

    const uint64_t vector_gap = 16ULL;
    if (str_cursor < vector_bytes + vector_gap + 8ULL) return -1;
    const uint64_t vector_limit = str_cursor - vector_gap;

    const uint64_t vector_base = vector_limit - vector_bytes;
    uint64_t sp = ((vector_base - 8ULL) & ~0xFULL) | 0x8ULL;
    if (sp + vector_bytes > vector_limit) {
        debugcon_puts("[ELF] initial user stack/vector overlap\n");
        return -1;
    }
    uint64_t argc_u = argc;
    uint64_t null = 0;
    if (copy_to_user(user_cr3, sp, (const uint8_t *)&argc_u, 8) != 0) return -1;
    uint64_t pos = sp + 8;
    for (size_t i = 0; i < argc; ++i, pos += 8)
        if (copy_to_user(user_cr3, pos, (const uint8_t *)&argv_va[i], 8) != 0) return -1;
    if (copy_to_user(user_cr3, pos, (const uint8_t *)&null, 8) != 0) return -1;
    pos += 8;
    for (size_t i = 0; i < envc; ++i, pos += 8)
        if (copy_to_user(user_cr3, pos, (const uint8_t *)&envp_va[i], 8) != 0) return -1;
    if (copy_to_user(user_cr3, pos, (const uint8_t *)&null, 8) != 0) return -1;
    pos += 8;
    if (copy_to_user(user_cr3, pos, (const uint8_t *)auxv, an * 8) != 0) return -1;

    if (!user_range_ok(sp, vector_bytes, 1) ||
        sp + vector_bytes > vector_limit) {
        debugcon_puts("[ELF] initial user stack/vector validation failed\n");
        return -1;
    }

    debug_hex64("[ELF] initial argc=0x", argc);
    debug_hex64("[ELF] initial argv=0x", sp + 8);
    debug_hex64("[ELF] initial envp=0x", sp + 8 + (argc + 1) * 8);
    debug_hex64("[ELF] initial auxv=0x", sp + 8 + (argc + 1 + envc + 1) * 8);
    debug_hex64("[ELF] initial AT_PHDR=0x", phdr_va);
    debug_hex64("[ELF] initial AT_ENTRY=0x", ehdr->e_entry);
    debug_hex64("[ELF] initial file_size=0x", (uint64_t)file_size);
    debug_hex64("[ELF] initial RSP ABI mod16=0x", sp & 0xFULL);

    *stack_out = sp;
    return 0;
}

uint64_t load_elf_with_args(const uint8_t *data, size_t size, const char *exec_path,
                            const char *const *argv, size_t argc,
                            const char *const *envp, size_t envc,
                            uint64_t *stack_top) {
    if (!data || !stack_top || size < sizeof(Elf64_Ehdr)) return 0;
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;

    if (ehdr->e_ident[0] != ELF_MAGIC1 || ehdr->e_ident[1] != ELF_MAGIC2 ||
        ehdr->e_ident[2] != ELF_MAGIC3 || ehdr->e_ident[3] != ELF_MAGIC4 ||
        ehdr->e_ident[4] != 2 || ehdr->e_ident[5] != 1 ||
        ehdr->e_type != 2 || ehdr->e_machine != 0x3E ||
        ehdr->e_version != 1 || ehdr->e_ehsize < sizeof(Elf64_Ehdr)) {
        debugcon_puts("[ELF] Invalid ELF64 x86_64 executable\n");
        return 0;
    }
    if (ehdr->e_phentsize < sizeof(Elf64_Phdr) || ehdr->e_phnum == 0) {
        debugcon_puts("[ELF] Invalid program header table\n");
        return 0;
    }
    uint64_t ph_bytes = (uint64_t)ehdr->e_phnum * ehdr->e_phentsize;
    if (ehdr->e_phoff > size || ph_bytes > size - ehdr->e_phoff) {
        debugcon_puts("[ELF] Program headers outside file\n");
        return 0;
    }

    debugcon_puts("[ELF] Creating user address space...\n");
    if (create_user_space() != 0) {
        debugcon_puts("[ELF] Cannot create user CR3\n");
        return 0;
    }

    const Elf64_Phdr *phdrs = (const Elf64_Phdr *)(data + ehdr->e_phoff);
    int load_count = 0;
    int entry_exec = 0;

    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)(data + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);
        if (ph->p_type != PT_LOAD) continue;
        ++load_count;

        if (ph->p_memsz == 0) {
            debugcon_puts("[ELF] PT_LOAD memsz=0\n");
            continue;
        }
        if (ph->p_filesz > ph->p_memsz) {
            debugcon_puts("[ELF] PT_LOAD filesz>memsz\n");
            return 0;
        }
        if (ph->p_offset > size || ph->p_filesz > size - ph->p_offset) {
            debugcon_puts("[ELF] PT_LOAD truncated file\n");
            return 0;
        }
        if (ph->p_vaddr > UINT64_MAX - ph->p_memsz) {
            debugcon_puts("[ELF] PT_LOAD address overflow\n");
            return 0;
        }

        uint64_t end = ph->p_vaddr + ph->p_memsz;
        if (ph->p_vaddr >= USER_LIMIT || end > USER_LIMIT || end < ph->p_vaddr) {
            debugcon_puts("[ELF] PT_LOAD outside user address space\n");
            return 0;
        }

        debugcon_puts("[ELF] Loading segment vaddr=0x");
        for (int j = 15; j >= 0; --j) {
            uint8_t n = (ph->p_vaddr >> (j * 4)) & 0xF;
            debugcon_putc(n < 10 ? '0' + n : 'a' + n - 10);
        }
        debugcon_puts(" memsz=0x");
        for (int j = 15; j >= 0; --j) {
            uint8_t n = (ph->p_memsz >> (j * 4)) & 0xF;
            debugcon_putc(n < 10 ? '0' + n : 'a' + n - 10);
        }
        debugcon_puts(" flags=");
        debugcon_putc((ph->p_flags & 4) ? 'R' : '-');
        debugcon_putc((ph->p_flags & 2) ? 'W' : '-');
        debugcon_putc((ph->p_flags & 1) ? 'X' : '-');
        debugcon_putc('\n');

        uint64_t map_start = ph->p_vaddr & PAGE_MASK;
        uint64_t map_end = (end + PAGE_SIZE - 1) & PAGE_MASK;
        if (map_end <= map_start || map_user_range(user_cr3, map_start, map_end, ph->p_flags) != 0) {
            debugcon_puts("[ELF] Failed to allocate/map PT_LOAD\n");
            return 0;
        }

        if (ph->p_filesz) {
            int rc = copy_to_user_loader(user_cr3, ph->p_vaddr,
                                         data + ph->p_offset, ph->p_filesz);
            if (rc) {
                debugcon_puts("[ELF] PT_LOAD file copy failed rc=0x");
                debug_hex64("", (uint64_t)(uint32_t)rc);
                return 0;
            }
        }
        if (ph->p_memsz > ph->p_filesz) {
            int rc = zero_user_loader(user_cr3, ph->p_vaddr + ph->p_filesz,
                                      ph->p_memsz - ph->p_filesz);
            if (rc) {
                debugcon_puts("[ELF] PT_LOAD BSS zero failed rc=0x");
                debug_hex64("", (uint64_t)(uint32_t)rc);
                return 0;
            }
        }

        if ((ph->p_flags & 1) && ehdr->e_entry >= ph->p_vaddr &&
            ehdr->e_entry < end)
            entry_exec = 1;
    }

    if (!load_count || !entry_exec || ehdr->e_entry >= USER_LIMIT) {
        debugcon_puts("[ELF] Invalid/no executable entry\n");
        return 0;
    }

    if (map_user_stack(user_cr3) != 0) {
        debugcon_puts("[ELF] Failed to allocate user stack\n");
        return 0;
    }

    if (init_user_stack(user_cr3, ehdr, phdrs, ehdr->e_phnum, size, exec_path,
                        argv, argc, envp, envc, stack_top) != 0) {
        debugcon_puts("[ELF] Failed to initialize user process stack\n");
        return 0;
    }

    debugcon_puts("[ELF] User CR3 prepared=0x");
    for (int j = 15; j >= 0; --j) {
        uint8_t n = (user_cr3 >> (j * 4)) & 0xF;
        debugcon_putc(n < 10 ? '0' + n : 'a' + n - 10);
    }
    debugcon_puts("\n");

    debugcon_puts("[ELF] Load complete, ready for userspace\n");
    debug_hex64("[ELF] entry=0x", ehdr->e_entry);
    debug_hex64("[ELF] stack=0x", *stack_top);
    debug_hex64("[ELF] cr3=0x", user_cr3);
    debugcon_puts("[ELF] FINAL USER ENTRY/RSP/CR3 ready\n");
    return ehdr->e_entry;
}

void syscall_init(void) {
    kernel_rsp = (uint64_t)kernel_stack + KERNEL_STACK_SIZE;
    user_rsp = 0;

    tss.rsp0 = kernel_rsp;
    debug_hex64("[V12.31] TSS.RSP0=0x", tss.rsp0);
    debug_hex64("[V12.31] kernel_rsp=0x", kernel_rsp);
    init_syscall_msrs();

    debugcon_puts("[SYSCALL] Initializing system calls...\n");

    init_files();
    task_init_root();
    debugcon_puts("[SYSCALL] System calls initialized\n");
}

uint64_t load_elf(const uint8_t *data, size_t size, const char *exec_path, uint64_t *stack_top) {
    return load_elf_with_args(data, size, exec_path, NULL, 0, NULL, 0, stack_top);
}
