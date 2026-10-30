//Core C kernel services. - (c) Ayano4ka1338, 2026

#include <stdint.h>
extern int vmm_debug_cr3_va(uint64_t cr3_phys, uint64_t va, const char *tag);
extern void vmm_debug_cr3_bytes(uint64_t cr3_phys, uint64_t va, const char *tag);
extern void vmm_debug_cr3_words(uint64_t cr3_phys, uint64_t va, const char *tag);
#include <stddef.h>
#include <stdbool.h>
#include "vfs.h"

#ifndef YABRO_DEBUG_PF
#define YABRO_DEBUG_PF 0
#endif
#ifndef YABRO_DEBUG_SCHED
#define YABRO_DEBUG_SCHED 0
#endif
extern const char *vfs_boot_path(const struct vfs_node *n);

static int kernel_strncmp(const char *a, const char *b, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		unsigned char x = (unsigned char)a[i];
		unsigned char y = (unsigned char)b[i];
		if (x != y) return x < y ? -1 : 1;
		if (x == 0) return 0;
	}
	return 0;
}

#define PAGE_SIZE 0x1000ULL
#define PAGE_MASK (~(PAGE_SIZE - 1ULL))
#define PTE_P 0x001ULL
#define PTE_W 0x002ULL
#define PTE_U 0x004ULL
#define PTE_COW (1ULL << 9)
#define PTE_PS 0x080ULL
#define PTE_NX (1ULL << 63)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

extern int vmm_user_cow_fault(uint64_t cr3, uint64_t virt);
extern int64_t graphics_fb_info_kernel(uint64_t *out);
extern int64_t graphics_fb_map(uint64_t virt, uint64_t off, uint64_t len);
extern int64_t graphics_fb_unmap(uint64_t virt, uint64_t size);
extern uint64_t graphics_fb_size(void);
extern uint64_t graphics_fb_phys(void);

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
static inline void outw(uint16_t port, uint16_t data) {
	__asm__ volatile ("outw %0, %1" : : "a" (data), "dN" (port));
}
static inline void io_wait(void) { outb(0x80, 0); }

extern void *pmm_phys_to_virt(uint64_t phys);

static uint8_t acpi_checksum(const uint8_t *p, uint32_t len) {
	uint8_t sum = 0;
	for (uint32_t i = 0; i < len; ++i) sum = (uint8_t)(sum + p[i]);
	return sum;
}

static uint32_t acpi_u32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t acpi_u64(const uint8_t *p) {
	return (uint64_t)acpi_u32(p) | ((uint64_t)acpi_u32(p + 4) << 32);
}

static void *acpi_phys(uint64_t phys) {
	return pmm_phys_to_virt(phys);
}

static int acpi_sdt_ok(uint64_t phys, const char sig[4], uint32_t *len_out) {
	uint8_t *h = (uint8_t *)acpi_phys(phys);
	if (!h) return 0;
	if (sig && (h[0] != (uint8_t)sig[0] || h[1] != (uint8_t)sig[1] ||
	            h[2] != (uint8_t)sig[2] || h[3] != (uint8_t)sig[3])) return 0;
	uint32_t len = acpi_u32(h + 4);
	if (len < 36 || len > (16U * 1024U * 1024U)) return 0;
	uint8_t *t = (uint8_t *)acpi_phys(phys);
	if (!t || acpi_checksum(t, len) != 0) return 0;
	if (len_out) *len_out = len;
	return 1;
}

static uint64_t acpi_find_rsdp(void) {

	uint16_t *bda = (uint16_t *)acpi_phys(0x40E);
	if (bda) {
		uint64_t ebda = (uint64_t)(*bda) << 4;
		if (ebda >= 0x400 && ebda < 0xA0000) {
			uint64_t end = ebda + 1024;
			for (uint64_t p = ebda; p + 20 <= end; p += 16) {
				uint8_t *r = (uint8_t *)acpi_phys(p);
				if (!r || r[0]!='R'||r[1]!='S'||r[2]!='D'||r[3]!=' '||
				    r[4]!='P'||r[5]!='T'||r[6]!='R'||r[7]!=' ') continue;
				if (acpi_checksum(r, 20) == 0) return p;
				}
		}
	}
	for (uint64_t p = 0xE0000; p + 20 <= 0x100000; p += 16) {
		uint8_t *r = (uint8_t *)acpi_phys(p);
		if (!r || r[0]!='R'||r[1]!='S'||r[2]!='D'||r[3]!=' '||
		    r[4]!='P'||r[5]!='T'||r[6]!='R'||r[7]!=' ') continue;
		if (acpi_checksum(r, 20) != 0) continue;
		if (r[15] >= 2) {
			uint32_t len = acpi_u32(r + 20);
			if (len < 36 || len > 4096) continue;
			uint8_t *rr = (uint8_t *)acpi_phys(p);
			if (!rr || acpi_checksum(rr, len) != 0) continue;
		}
		return p;
	}
	return 0;
}

static uint64_t acpi_find_fadt(void) {
	uint64_t rsdp_phys = acpi_find_rsdp();
	if (!rsdp_phys) return 0;
	uint8_t *r = (uint8_t *)acpi_phys(rsdp_phys);
	if (!r) return 0;

	uint64_t root_phys = 0;
	int xsdt = (r[15] >= 2);
	if (xsdt) root_phys = acpi_u64(r + 24);
	else root_phys = (uint64_t)acpi_u32(r + 16);
	if (!root_phys) return 0;

	uint32_t len = 0;
	if (!acpi_sdt_ok(root_phys, xsdt ? "XSDT" : "RSDT", &len)) return 0;
	uint8_t *root = (uint8_t *)acpi_phys(root_phys);
	uint32_t entries = (len - 36) / (xsdt ? 8 : 4);
	for (uint32_t i = 0; i < entries; ++i) {
		uint64_t p = xsdt ? acpi_u64(root + 36 + i * 8)
		                  : (uint64_t)acpi_u32(root + 36 + i * 4);
		if (p && acpi_sdt_ok(p, "FACP", NULL)) return p;
	}
	return 0;
}

static int acpi_aml_integer(const uint8_t *p, uint32_t left,
                            uint64_t *value, uint32_t *used) {
	if (!p || !left || !value || !used) return 0;
	switch (p[0]) {
	case 0x00: *value = 0; *used = 1; return 1;             
	case 0x01: *value = 1; *used = 1; return 1;            
	case 0x0A:
		if (left < 2) return 0;
		*value = p[1]; *used = 2; return 1;
	case 0x0B:
		if (left < 3) return 0;
		*value = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
		*used = 3; return 1;
	case 0x0C:
		if (left < 5) return 0;
		*value = acpi_u32(p + 1); *used = 5; return 1;
	case 0x0E:
		if (left < 9) return 0;
		*value = acpi_u64(p + 1); *used = 9; return 1;
	default:
		return 0;
	}
}

static int acpi_get_s5(uint64_t dsdt_phys, uint32_t dsdt_len,
                       uint16_t *slp_typa, uint16_t *slp_typb) {
	uint8_t *d = (uint8_t *)acpi_phys(dsdt_phys);
	if (!d || dsdt_len < 40) return 0;

	for (uint32_t i = 36; i + 8 < dsdt_len; ++i) {
		if (d[i] != '_' || d[i+1] != 'S' || d[i+2] != '5' || d[i+3] != '_')
			continue;
		uint32_t p = i + 4;
		if (p >= dsdt_len || d[p] != 0x12) continue;                
		p++;
		if (p >= dsdt_len) continue;

		uint8_t lead = d[p];
		uint32_t follow = (lead >> 6) & 3;
		uint32_t pkg_len = lead & 0x3F;
		if (p + 1 + follow > dsdt_len) continue;
		if (follow) {
			pkg_len = lead & 0x0F;
			for (uint32_t n = 0; n < follow; ++n)
				pkg_len |= (uint32_t)d[p + 1 + n] << (4 + 8*n);
		}
		(void)pkg_len;
		p += 1 + follow;
		if (p >= dsdt_len) continue;
		uint8_t elements = d[p++];
		if (elements < 2) continue;

		uint64_t a=0,b=0; uint32_t used=0;
		if (!acpi_aml_integer(d+p, dsdt_len-p, &a, &used)) continue;
		p += used;
		if (!acpi_aml_integer(d+p, dsdt_len-p, &b, &used)) continue;
		*slp_typa = (uint16_t)a;
		*slp_typb = (uint16_t)b;
		return 1;
	}
	return 0;
}

static int acpi_shutdown(void) {
	uint64_t fadt_phys = acpi_find_fadt();
	if (!fadt_phys) return 0;
	uint8_t *f = (uint8_t *)acpi_phys(fadt_phys);
	uint32_t flen = 0;
	if (!f || !acpi_sdt_ok(fadt_phys, "FACP", &flen) || flen < 116) return 0;

	uint32_t pm1a = acpi_u32(f + 64);
	uint32_t pm1b = acpi_u32(f + 68);
	uint16_t slp_a=0, slp_b=0;

	uint64_t dsdt = (uint64_t)acpi_u32(f + 40);
	if (flen >= 148) {
		uint64_t xdsdt = acpi_u64(f + 140);
		if (xdsdt) dsdt = xdsdt;
	}
	if (!dsdt) return 0;
	uint32_t dl=0;
	if (!acpi_sdt_ok(dsdt, "DSDT", &dl)) return 0;
	if (!acpi_get_s5(dsdt, dl, &slp_a, &slp_b)) return 0;

	uint16_t val_a = (uint16_t)((slp_a << 10) | (1u << 13));
	uint16_t val_b = (uint16_t)((slp_b << 10) | (1u << 13));

	uint8_t pm1_len = f[89];
	int did = 0;
	if (pm1a && pm1_len >= 2) { outw((uint16_t)pm1a, val_a); did = 1; }
	if (pm1b && pm1_len >= 2) { outw((uint16_t)pm1b, val_b); did = 1; }
	return did;
}

__attribute__((noreturn)) void system_reboot_hw(void) {
	__asm__ volatile ("cli" ::: "memory");
	for (uint32_t i = 0; i < 1000000U; ++i) {
		if (!(inb(0x64) & 0x02)) {
			outb(0x64, 0xFE);
			for (volatile uint32_t wait = 0; wait < 1000000U; ++wait)
				__asm__ volatile ("pause");
			break;
		}
		__asm__ volatile ("pause");
	}

	outb(0xCF9, 0x02);
	io_wait();
	outb(0xCF9, 0x06);
	for (volatile uint32_t wait = 0; wait < 1000000U; ++wait)
		__asm__ volatile ("pause");

	struct idtr { uint16_t limit; uint64_t base; } __attribute__((packed));
	struct idtr null_idt = { 0, 0 };
	__asm__ volatile ("lidt %0; int3" : : "m" (null_idt) : "memory");
	for (;;) __asm__ volatile ("cli; hlt" ::: "memory");
}

static int64_t sys_system_reboot(void) {
	system_reboot_hw();
}

int64_t sys_system_shutdown(void) {

	__asm__ volatile ("cli" ::: "memory");

	if (acpi_shutdown()) {
		for (;;) __asm__ volatile ("hlt");
	}

	outw(0x604, 0x2000);
	outw(0xB004, 0x2000);
	for (;;) __asm__ volatile ("hlt");
	return 0;
}

extern void console_fb_cls(void);
extern void bsod_page_fault(uint64_t rip, uint64_t cr3, uint64_t cr2, uint64_t error) __attribute__((noreturn));

static inline void debugcon_putc(char c) {
	outb(0xE9, (uint8_t)c);
}
static inline void debugcon_puts(const char *s) {
	if (!s) return;
	while (*s) debugcon_putc(*s++);
}
static inline void debug_hex64(const char *prefix, uint64_t value) {
	static const char hex[] = "0123456789abcdef";
	if (prefix) debugcon_puts(prefix);
	debugcon_puts("0x");
	for (int i = 15; i >= 0; --i)
		debugcon_putc(hex[(value >> (i * 4)) & 0xF]);
	debugcon_putc('\n');
}

volatile uint64_t user_exec_pending = 0;

void exec_debug_iret_frame(const uint64_t *frame);
extern int vmm_user_translate_cr3(uint64_t cr3, uint64_t virt, uint64_t *phys_out, uint64_t *pte_out);
extern uint64_t syscall_user_cr3;

volatile uint64_t pf_ist_entry = 0;
volatile uint64_t pf_hw_error = 0;
volatile uint64_t pf_hw_rip = 0;
volatile uint64_t pf_hw_cs = 0;
volatile uint64_t pf_hw_rflags = 0;
volatile uint64_t pf_hw_rsp = 0;
static volatile uint64_t pf_exc_regs[15];
volatile uint64_t pf_hw_ss = 0;
volatile uint64_t pf_hw_cr2 = 0;

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
	if (gdt[4].access != 0xFA || gdt[4].granularity != 0xAF ||
	    gdt[5].access != 0xF2 || gdt[5].granularity != 0xCF) {
		debugcon_puts("[GDT-CHECK] user CS/SS descriptor mismatch\n");
	} else {
		debugcon_puts("[GDT-CHECK] user CS=0x23 SS=0x2b OK\n");
	}
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
static volatile int framebuffer_console_active = 1;
static int framebuffer_users = 0;
extern void console_fb_set_cursor(uint64_t col, uint64_t row);
extern uint64_t console_fb_get_cursor(void);
extern int32_t console_fb_get_size(uint16_t *rows, uint16_t *cols,
                                    uint16_t *xpixel, uint16_t *ypixel);

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
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void keyboard_isr_stub(void);
extern void mouse_isr_stub(void);
extern void timer_isr_stub(void);
extern void syscall_isr_stub(void);

void init_idt(void) {
	uint64_t base = (uint64_t)&idt;
	idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
	idtp.base = base;

	/* Install every architectural exception gate. Missing gates can turn a
	 * recoverable user fault into a double/triple fault and reset the machine. */
	set_id_gate(0,  (uint64_t)isr0,  0x08, 0x8E, 0);
	set_id_gate(1,  (uint64_t)isr1,  0x08, 0x8E, 0);
	set_id_gate(2,  (uint64_t)isr2,  0x08, 0x8E, 0);
	set_id_gate(3,  (uint64_t)isr3,  0x08, 0x8E, 0);
	set_id_gate(4,  (uint64_t)isr4,  0x08, 0x8E, 0);
	set_id_gate(5,  (uint64_t)isr5,  0x08, 0x8E, 0);
	set_id_gate(6,  (uint64_t)isr6,  0x08, 0x8E, 1);
	set_id_gate(7,  (uint64_t)isr7,  0x08, 0x8E, 0);
	set_id_gate(8,  (uint64_t)isr8,  0x08, 0x8E, 2); /* dedicated double-fault IST */
	set_id_gate(9,  (uint64_t)isr9,  0x08, 0x8E, 0);
	set_id_gate(10, (uint64_t)isr10, 0x08, 0x8E, 1);
	set_id_gate(11, (uint64_t)isr11, 0x08, 0x8E, 1);
	set_id_gate(12, (uint64_t)isr12, 0x08, 0x8E, 1);
	set_id_gate(13, (uint64_t)isr13, 0x08, 0x8E, 1);
	set_id_gate(14, (uint64_t)isr14, 0x08, 0x8E, 3);
	set_id_gate(15, (uint64_t)isr15, 0x08, 0x8E, 0);
	set_id_gate(16, (uint64_t)isr16, 0x08, 0x8E, 0);
	set_id_gate(17, (uint64_t)isr17, 0x08, 0x8E, 0);
	set_id_gate(18, (uint64_t)isr18, 0x08, 0x8E, 0);
	set_id_gate(19, (uint64_t)isr19, 0x08, 0x8E, 0);
	set_id_gate(20, (uint64_t)isr20, 0x08, 0x8E, 0);
	set_id_gate(21, (uint64_t)isr21, 0x08, 0x8E, 0);
	set_id_gate(22, (uint64_t)isr22, 0x08, 0x8E, 0);
	set_id_gate(23, (uint64_t)isr23, 0x08, 0x8E, 0);
	set_id_gate(24, (uint64_t)isr24, 0x08, 0x8E, 0);
	set_id_gate(25, (uint64_t)isr25, 0x08, 0x8E, 0);
	set_id_gate(26, (uint64_t)isr26, 0x08, 0x8E, 0);
	set_id_gate(27, (uint64_t)isr27, 0x08, 0x8E, 0);
	set_id_gate(28, (uint64_t)isr28, 0x08, 0x8E, 0);
	set_id_gate(29, (uint64_t)isr29, 0x08, 0x8E, 0);
	set_id_gate(30, (uint64_t)isr30, 0x08, 0x8E, 0);
	set_id_gate(31, (uint64_t)isr31, 0x08, 0x8E, 0);
	set_id_gate(0x20, (uint64_t)timer_isr_stub, 0x08, 0x8E, 0);
	set_id_gate(0x21, (uint64_t)keyboard_isr_stub, 0x08, 0x8E, 0);
	set_id_gate(0x2C, (uint64_t)mouse_isr_stub, 0x08, 0x8E, 0);
	set_id_gate(0x80, (uint64_t)syscall_isr_stub, 0x08, 0xEE, 0);

	__asm__ volatile ("lidt %0" : : "m" (idtp) : "memory");
}

static int task_lazy_page_fault(uint64_t cr3, uint64_t addr, uint64_t error_code);
static void exception_terminate_user_process(uint64_t num, uint64_t error_code, uint64_t rip);
extern void console_fb_cursor_blink_tick(void);
extern void console_fb_cursor_suspend(void);
static void pf_hw_frame_audit(void);
static void pf_watch_user_va(uint64_t cr3, const char *tag, uint64_t va);
extern int vmm_user_validate_page_cr3(uint64_t cr3, uint64_t virt, int require_write, int require_exec);

void isr_handler(uint64_t num, uint64_t error_code, uint64_t rip, uint64_t cs, uint64_t rflags) {
	/* NMI is asynchronous, not a fault caused by the current user program. */
	if (num == 2) {
		debugcon_puts("[NMI] non-maskable interrupt received\n");
		return;
	}

	/* First try the existing demand-mapping and COW paths. If a user fault is
	 * not recoverable, terminate only that process and switch back to its parent
	 * (or to another runnable task) instead of halting the whole machine. */
	if ((cs & 3ULL) == 3ULL) {
		if (num == 14) {
			uint64_t user_cr3 = 0, user_addr = 0;
			__asm__ volatile ("mov %%cr3, %0" : "=r"(user_cr3));
			__asm__ volatile ("mov %%cr2, %0" : "=r"(user_addr));
			if (!(error_code & 0x1) && task_lazy_page_fault(user_cr3, user_addr, error_code)) return;
			if ((error_code & 0x7) == 0x7 && vmm_user_cow_fault(user_cr3, user_addr) == 0) return;
		}
		exception_terminate_user_process(num, error_code, rip);
		return;
	}

	if (num == 14) pf_hw_frame_audit();
	if (num == 6) {
		uint64_t ud_cr3 = 0, cr0 = 0, cr4 = 0;
		__asm__ volatile ("mov %%cr3, %0" : "=r"(ud_cr3));
		__asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
		__asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
		debugcon_puts("[UD] Invalid opcode (#UD), not a page fault\n");
		debug_hex64("[UD] RIP=", rip);
		debug_hex64("[UD] CR3=", ud_cr3);
		debug_hex64("[UD] CR0=", cr0);
		debug_hex64("[UD] CR4=", cr4);
		(void)vmm_debug_cr3_bytes(ud_cr3, rip, "[UD-CODE] VA=");
	}
	if (num == 13) {
		uint64_t gp_cr3=0, fs=0; uint32_t lo=0, hi=0;
		__asm__ volatile ("mov %%cr3, %0" : "=r"(gp_cr3));
		__asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100));
		fs=((uint64_t)hi<<32)|lo;
		debug_hex64("[GP-USER] rip=0x", rip); debug_hex64(" rsp=0x", pf_hw_rsp);
		debug_hex64(" cr3=0x", gp_cr3); debug_hex64(" fs=0x", fs); debugcon_puts("\n");
		(void)vmm_debug_cr3_bytes(gp_cr3,rip,"[GP-CODE] VA=0x");
		(void)vmm_debug_cr3_bytes(gp_cr3,fs+0x28,"[GP-TLS-GUARD] VA=0x");
		(void)vmm_debug_cr3_words(gp_cr3,(pf_hw_rsp>=0x40)?(pf_hw_rsp-0x40):pf_hw_rsp,"[GP-STACK-BEFORE] VA=0x");
		(void)vmm_debug_cr3_words(gp_cr3,pf_hw_rsp,"[GP-STACK-AT-RSP] VA=0x");

		uint64_t guard=0, guard_pa=0, guard_pte=0;
		if (fs && vmm_user_translate_cr3(gp_cr3, fs+0x28, &guard_pa, &guard_pte)==0) {
			uint8_t *gp=(uint8_t*)pmm_phys_to_virt(guard_pa);
			if (gp) {
				uint64_t goff=(fs+0x28)&(PAGE_SIZE-1);
				for (int bi=0;bi<8;bi++) ((uint8_t*)&guard)[bi]=gp[goff+bi];
			}
		}
		debug_hex64("[GP-PROBE3] guard=0x", guard);
		uint64_t scan_start = pf_hw_rsp >= 0x100 ? pf_hw_rsp - 0x100 : pf_hw_rsp;
		uint64_t scan_end = pf_hw_rsp + 0x180;
		const uint64_t gp_user_limit=0x0000800000000000ULL;
		for (uint64_t a=scan_start; a+8<=scan_end; a+=8) {
			uint64_t phys=0,pte=0;
			if (vmm_user_translate_cr3(gp_cr3,a,&phys,&pte)!=0) continue;
			uint64_t off=a&(PAGE_SIZE-1);
			if (off>PAGE_SIZE-8) continue;
			uint8_t *q=(uint8_t*)pmm_phys_to_virt(phys);
			if(!q) continue;
			uint64_t w=0;
			for(int bi=0;bi<8;bi++) w |= ((uint64_t)q[off+bi])<<(8*bi);
			if (guard && w==guard) {
				debug_hex64("[GP-PROBE3-GUARD] slot=0x",a);
				debug_hex64(" rel=0x",(uint64_t)(a-pf_hw_rsp));
				debugcon_puts("\n");
			}
			if (w && w<gp_user_limit && w>PAGE_SIZE && vmm_user_validate_page_cr3(gp_cr3,w&PAGE_MASK,0,1)==0) {
				debug_hex64("[GP-PROBE3-EXEC] slot=0x",a);
				debug_hex64(" value=0x",w);
				debug_hex64(" rel=0x",(uint64_t)(a-pf_hw_rsp));
				debugcon_puts("\n");
				(void)vmm_debug_cr3_bytes(gp_cr3,w,"[GP-PROBE3-CODE] VA=0x");
			}
		}
	}

	if (num == 14 && !(error_code & 0x1)) {
		uint64_t lazy_cr3=0, lazy_addr=0;
		__asm__ volatile ("mov %%cr3, %0" : "=r"(lazy_cr3));
		__asm__ volatile ("mov %%cr2, %0" : "=r"(lazy_addr));
		if (task_lazy_page_fault(lazy_cr3, lazy_addr, error_code)) return;
	}
	if (num == 14 && (error_code & 0x7) == 0x7) {
		uint64_t cow_cr3=0,cow_addr=0;
		__asm__ volatile ("mov %%cr3, %0" : "=r"(cow_cr3));
		__asm__ volatile ("mov %%cr2, %0" : "=r"(cow_addr));
		if (vmm_user_cow_fault(cow_cr3, cow_addr) == 0) return;
	}
	if (num == 14) {
		uint64_t bsod_cr2 = 0, bsod_cr3 = 0;
		__asm__ volatile ("mov %%cr2, %0" : "=r"(bsod_cr2));
		__asm__ volatile ("mov %%cr3, %0" : "=r"(bsod_cr3));

		debug_hex64("[PF-NONCOW] cr3=0x", bsod_cr3);
		debug_hex64("[PF-NONCOW] cr2=0x", bsod_cr2);
		debug_hex64("[PF-NONCOW] rip=0x", rip);
		debug_hex64("[PF-NONCOW] rsp=0x", pf_hw_rsp);
		(void)vmm_debug_cr3_va(bsod_cr3, bsod_cr2 & PAGE_MASK, "[PF-NONCOW-CR2] PML4E=");
		(void)vmm_debug_cr3_va(bsod_cr3, rip & PAGE_MASK, "[PF-NONCOW-RIP] PML4E=");
		vmm_debug_cr3_bytes(bsod_cr3, rip, "[PF-NONCOW-CODE] VA=0x");
		vmm_debug_cr3_words(bsod_cr3, pf_hw_rsp, "[PF-NONCOW-STACK] VA=0x");
		pf_watch_user_va(bsod_cr3, "[PF-WATCH-CR2] va=0x", bsod_cr2);
		pf_watch_user_va(bsod_cr3, "[PF-WATCH-RIP] va=0x", rip);
		pf_watch_user_va(bsod_cr3, "[PF-WATCH-RDI] va=0x", pf_exc_regs[9]);
		pf_watch_user_va(bsod_cr3, "[PF-WATCH-RSI] va=0x", pf_exc_regs[10]);
		pf_watch_user_va(bsod_cr3, "[PF-WATCH-RBP] va=0x", pf_exc_regs[8]);
		debug_hex64("[PF-WATCH-RCX] value=0x", pf_exc_regs[12]); debugcon_puts("\n");
		bsod_page_fault(rip, bsod_cr3, bsod_cr2, error_code);
	}
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
	if (num == 14) {
		debugcon_puts("[PF-WALK] fault CR3=0x");
		for (int i = 15; i >= 0; --i) { uint8_t nibble = (cr3 >> (i * 4)) & 0xF; debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10); }
		debugcon_putc('\n');

		(void)vmm_debug_cr3_va(cr3, cr2 & PAGE_MASK, "[PF-CHILD-CR2] PML4E=");
		(void)vmm_debug_cr3_va(cr3, rip & PAGE_MASK, "[PF-CHILD-RIP] PML4E=");
		(void)vmm_debug_cr3_va(0x104d000ULL, cr2 & PAGE_MASK, "[PF-PARENT-CR2] PML4E=");
		(void)vmm_debug_cr3_va(0x104d000ULL, rip & PAGE_MASK, "[PF-PARENT-RIP] PML4E=");
		vmm_debug_cr3_bytes(cr3, rip, "[PF-CHILD-RIP-BYTES] VA=0x");
		vmm_debug_cr3_bytes(0x104d000ULL, rip, "[PF-PARENT-RIP-BYTES] VA=0x");
		vmm_debug_cr3_words(cr3, pf_hw_rsp, "[PF-CHILD-STACK] VA=0x");
		vmm_debug_cr3_words(0x104d000ULL, pf_hw_rsp, "[PF-PARENT-STACK] VA=0x");
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
	if (num == 14) {
		debugcon_puts("[PF] P="); debugcon_putc((error_code & 1) ? '1' : '0');
		debugcon_puts(" W="); debugcon_putc((error_code & 2) ? '1' : '0');
		debugcon_puts(" U="); debugcon_putc((error_code & 4) ? '1' : '0');
		debugcon_puts(" RSVD="); debugcon_putc((error_code & 8) ? '1' : '0');
		debugcon_puts(" IF="); debugcon_putc((error_code & 16) ? '1' : '0');
		uint64_t active_cr3 = 0;
		__asm__ volatile ("mov %%cr3, %0" : "=r"(active_cr3));
		debugcon_puts(" active_cr3=0x");
		for (int i = 15; i >= 0; --i) {
			uint8_t nibble = (active_cr3 >> (i*4)) & 0xF;
			debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
		}
		debugcon_puts(" syscall_cr3=0x");
		for (int i = 15; i >= 0; --i) {
			uint8_t nibble = (syscall_user_cr3 >> (i*4)) & 0xF;
			debugcon_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
		}
		debugcon_puts("\\n");
	}
	while(1) __asm__ volatile ("cli; hlt");
}

void exception_regs_audit(const uint64_t *r) {
	if (!r) return;

	for (int i = 0; i < 15; i++) pf_exc_regs[i] = r[i];
#if YABRO_DEBUG_PF

	debug_hex64("[EXC-REGS] R15=0x", r[0]);
	debug_hex64(" R14=0x", r[1]);
	debug_hex64(" R13=0x", r[2]);
	debug_hex64(" R12=0x", r[3]);
	debug_hex64(" R11=0x", r[4]);
	debug_hex64(" R10=0x", r[5]);
	debug_hex64(" R9=0x",  r[6]);
	debug_hex64(" R8=0x",  r[7]);
	debug_hex64(" RBP=0x", r[8]);
	debug_hex64(" RDI=0x", r[9]);
	debug_hex64(" RSI=0x", r[10]);
	debug_hex64(" RDX=0x", r[11]);
	debug_hex64(" RCX=0x", r[12]);
	debug_hex64(" RBX=0x", r[13]);
	debug_hex64(" RAX=0x", r[14]);
	debugcon_puts("\n");
#endif
}

void exception_raw_audit(const uint64_t *sp) {
#if YABRO_DEBUG_PF
	if (!sp) return;
	debug_hex64("[EXC-RAW] q0(vector)=0x", sp[0]);
	debug_hex64("[EXC-RAW] q1(error)=0x", sp[1]);
	debug_hex64("[EXC-RAW] q2(RIP)=0x", sp[2]);
	debug_hex64("[EXC-RAW] q3(CS)=0x", sp[3]);
	debug_hex64("[EXC-RAW] q4(RFLAGS)=0x", sp[4]);
	debug_hex64("[EXC-RAW] q5(RSP)=0x", sp[5]);
	debug_hex64("[EXC-RAW] q6(SS)=0x", sp[6]);
#else
	(void)sp;
#endif
}

static void pf_hw_frame_audit(void) {
#if YABRO_DEBUG_PF
	debug_hex64("[PF-HW] ist_entry=0x", pf_ist_entry);
	debug_hex64("[PF-HW] error=0x", pf_hw_error);
	debug_hex64("[PF-HW] rip=0x", pf_hw_rip);
	debug_hex64("[PF-HW] cs=0x", pf_hw_cs);
	debug_hex64("[PF-HW] rflags=0x", pf_hw_rflags);
	debug_hex64("[PF-HW] rsp=0x", pf_hw_rsp);
	debug_hex64("[PF-HW] ss=0x", pf_hw_ss);
	debug_hex64("[PF-HW] cr2=0x", pf_hw_cr2);
#endif
}

static void pf_watch_user_va(uint64_t cr3, const char *tag, uint64_t va) {
	uint64_t phys = 0, pte = 0;
	int rc = vmm_user_translate_cr3(cr3, va & PAGE_MASK, &phys, &pte);
	debug_hex64(tag, va);
	debug_hex64(" rc=0x", (uint64_t)(uint32_t)rc);
	debug_hex64(" pte=0x", pte);
	debug_hex64(" phys=0x", phys);
	debugcon_puts("\n");
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
".global isr2\n"
"isr2:\n"
"    pushq $0\n"
"    pushq $2\n"
"    jmp isr_common\n"
".global isr3\n"
"isr3:\n"
"    pushq $0\n"
"    pushq $3\n"
"    jmp isr_common\n"
".global isr5\n"
"isr5:\n"
"    pushq $0\n"
"    pushq $5\n"
"    jmp isr_common\n"
".global isr7\n"
"isr7:\n"
"    pushq $0\n"
"    pushq $7\n"
"    jmp isr_common\n"
".global isr9\n"
"isr9:\n"
"    pushq $0\n"
"    pushq $9\n"
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
"    pushq %rax\n"
"    movq 40(%rsp), %rax\n"
"    movq %rax, pf_hw_rsp(%rip)\n"
"    popq %rax\n"
"    pushq $13\n"
"    jmp isr_common\n"
".global isr14\n"
"isr14:\n"
"    pushq %rax\n"
"    movq 8(%rsp), %rax\n"
"    movq %rax, pf_hw_error(%rip)\n"
"    movq 16(%rsp), %rax\n"
"    movq %rax, pf_hw_rip(%rip)\n"
"    movq 24(%rsp), %rax\n"
"    movq %rax, pf_hw_cs(%rip)\n"
"    movq 32(%rsp), %rax\n"
"    movq %rax, pf_hw_rflags(%rip)\n"
"    movq 40(%rsp), %rax\n"
"    movq %rax, pf_hw_rsp(%rip)\n"
"    movq 48(%rsp), %rax\n"
"    movq %rax, pf_hw_ss(%rip)\n"
"    movq %rsp, %rax\n"
"    movq %rax, pf_ist_entry(%rip)\n"
"    movq %cr2, %rax\n"
"    movq %rax, pf_hw_cr2(%rip)\n"
"    popq %rax\n"
"    pushq $14\n"
"    jmp isr_common\n"
".global isr15\n"
"isr15:\n"
"    pushq $0\n"
"    pushq $15\n"
"    jmp isr_common\n"
".global isr16\n"
"isr16:\n"
"    pushq $0\n"
"    pushq $16\n"
"    jmp isr_common\n"
".global isr17\n"
"isr17:\n"
"    pushq $17\n"
"    jmp isr_common\n"
".global isr18\n"
"isr18:\n"
"    pushq $0\n"
"    pushq $18\n"
"    jmp isr_common\n"
".global isr19\n"
"isr19:\n"
"    pushq $0\n"
"    pushq $19\n"
"    jmp isr_common\n"
".global isr20\n"
"isr20:\n"
"    pushq $0\n"
"    pushq $20\n"
"    jmp isr_common\n"
".global isr21\n"
"isr21:\n"
"    pushq $21\n"
"    jmp isr_common\n"
".global isr22\n"
"isr22:\n"
"    pushq $0\n"
"    pushq $22\n"
"    jmp isr_common\n"
".global isr23\n"
"isr23:\n"
"    pushq $0\n"
"    pushq $23\n"
"    jmp isr_common\n"
".global isr24\n"
"isr24:\n"
"    pushq $0\n"
"    pushq $24\n"
"    jmp isr_common\n"
".global isr25\n"
"isr25:\n"
"    pushq $0\n"
"    pushq $25\n"
"    jmp isr_common\n"
".global isr26\n"
"isr26:\n"
"    pushq $0\n"
"    pushq $26\n"
"    jmp isr_common\n"
".global isr27\n"
"isr27:\n"
"    pushq $0\n"
"    pushq $27\n"
"    jmp isr_common\n"
".global isr28\n"
"isr28:\n"
"    pushq $0\n"
"    pushq $28\n"
"    jmp isr_common\n"
".global isr29\n"
"isr29:\n"
"    pushq $29\n"
"    jmp isr_common\n"
".global isr30\n"
"isr30:\n"
"    pushq $30\n"
"    jmp isr_common\n"
".global isr31\n"
"isr31:\n"
"    pushq $0\n"
"    pushq $31\n"
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
"    call exception_regs_audit\n"
"    movq %rsp, %rdi\n"
"    addq $120, %rdi\n"
"    call exception_raw_audit\n"
"    movq 120(%rsp), %rdi\n"
"    movq 128(%rsp), %rsi\n"
"    movq 136(%rsp), %rdx\n"
"    movq 144(%rsp), %rcx\n"
"    movq 152(%rsp), %r8\n"
"    call isr_handler\n"
"    cmpq $0, task_switch_pending(%rip)\n"
"    jne isr_switch_task\n"
"    cmpq $0, user_exit_pending(%rip)\n"
"    jne isr_return_to_shell\n"
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
"isr_return_to_shell:\n"
"    movq $0, user_exit_pending(%rip)\n"
"    movq kernel_cr3(%rip), %rax\n"
"    movq %rax, %cr3\n"
"    movq user_return_rsp(%rip), %rsp\n"
"    sti\n"
"    retq\n"
"isr_switch_task:\n"
"    movq $0, task_switch_pending(%rip)\n"
"    movq task_return_ctx(%rip), %r15\n"
"    testq %r15, %r15\n"
"    jz isr_switch_fatal\n"
"    subq $40, %rsp\n"
"    movq 128(%r15), %rax\n"
"    movq %rax, 0(%rsp)\n"
"    movq $0x23, %rax\n"
"    movq %rax, 8(%rsp)\n"
"    movq 136(%r15), %rax\n"
"    movq %rax, 16(%rsp)\n"
"    movq 120(%r15), %rax\n"
"    movq %rax, 24(%rsp)\n"
"    movq $0x2b, %rax\n"
"    movq %rax, 32(%rsp)\n"
"    movq 152(%r15), %rax\n"
"    movq %rax, %r10\n"
"    shrq $32, %r10\n"
"    movl %r10d, %edx\n"
"    movl $0xC0000100, %ecx\n"
"    wrmsr\n"
"    movq 160(%r15), %rax\n"
"    movq %rax, %r10\n"
"    shrq $32, %r10\n"
"    movl %r10d, %edx\n"
"    movl $0xC0000101, %ecx\n"
"    wrmsr\n"
"    movq 144(%r15), %r14\n"
"    movq %r14, %rdi\n"
"    subq $8, %rsp\n"
"    call vmm_user_activate\n"
"    addq $8, %rsp\n"
"    movq 144(%r15), %r14\n"
"    movq %r14, %cr3\n"
"    movq 0(%r15), %rax\n"
"    movq 8(%r15), %rbx\n"
"    movq 16(%r15), %rcx\n"
"    movq 24(%r15), %rdx\n"
"    movq 32(%r15), %rsi\n"
"    movq 40(%r15), %rdi\n"
"    movq 48(%r15), %rbp\n"
"    movq 56(%r15), %r8\n"
"    movq 64(%r15), %r9\n"
"    movq 72(%r15), %r10\n"
"    movq 80(%r15), %r11\n"
"    movq 88(%r15), %r12\n"
"    movq 96(%r15), %r13\n"
"    movq 104(%r15), %r14\n"
"    movq 112(%r15), %r15\n"
"    iretq\n"
"isr_switch_fatal:\n"
"    cli\n"
"    hlt\n"
"    jmp isr_switch_fatal\n"
);

#define EV_SYN 0
#define EV_KEY 1
#define EV_REL 2
#define EV_REP 20
#define SYN_REPORT 0
#define REL_X 0
#define REL_Y 1
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
struct input_event_k { int64_t tv_sec; int64_t tv_usec; uint16_t type; uint16_t code; int32_t value; } __attribute__((packed));
_Static_assert(sizeof(struct input_event_k) == 24, "Linux x86_64 input_event ABI must be 24 bytes");
struct input_id_k { uint16_t bustype, vendor, product, version; };

#define INPUT_EVENT_READ_MAX 64
#define INPUT_EVENT_CAP 256
static volatile struct input_event_k input_event_queue[INPUT_EVENT_CAP];

static volatile uint64_t keyboard_event_seq=0;

static volatile uint16_t keyboard_hotkey_trace_mask=0;

static volatile uint8_t keyboard_raw_scancode_trace_mask[32];
static volatile uint8_t keyboard_x_scancode_trace_count=0;
static volatile uint8_t keyboard_x_event_trace_count=0;

static volatile uint8_t keyboard_key_state[128];

static volatile uint32_t keyboard_repeat_delay_ms=250;
static volatile uint32_t keyboard_repeat_period_ms=33;
static void input_push_event(uint16_t type,uint16_t code,int32_t value);
static int keyboard_key_is_down(uint16_t code) {
	return code < (sizeof(keyboard_key_state)*8u) &&
	       (keyboard_key_state[code>>3] & (uint8_t)(1u<<(code&7u))) != 0;
}
static void keyboard_emit_key(uint16_t code, int down) {
	int32_t value = down ? (keyboard_key_is_down(code) ? 2 : 1) : 0;
	input_push_event(EV_KEY,code,value);
	input_push_event(EV_SYN,SYN_REPORT,0);
}
#define MOUSE_EVENT_CAP 256
static volatile struct input_event_k mouse_event_queue[MOUSE_EVENT_CAP];

static volatile uint64_t mouse_event_seq=0;
static volatile uint64_t mouse_irq12_hits=0;
static volatile uint64_t mouse_irq_bytes=0;
static volatile uint64_t mouse_poll_ticks=0;
static volatile uint64_t mouse_poll_bytes=0;
static volatile uint64_t mouse_sync_rejects=0;
static volatile uint64_t mouse_bad_packets=0;
static volatile uint64_t mouse_event_pushes=0;
static volatile uint8_t mouse_irq_logged=0;
static volatile uint8_t keyboard_irq_logged=0;
static volatile uint8_t keyboard_scancode_logged=0;
static volatile uint8_t keyboard_event_queued_logged=0;
static volatile uint8_t keyboard_poll_logged=0;
static volatile uint8_t evdev_keyboard_read_logged=0;

static volatile uint8_t evdev_ctrl_read_trace_mask=0;
static volatile uint8_t evdev_keyboard_read_enter_logged=0;
static volatile uint8_t evdev_mouse_read_enter_logged=0;
static volatile uint8_t evdev_keyboard_empty_logged=0;
static volatile uint8_t evdev_mouse_empty_logged=0;
static volatile uint8_t evdev_epoll_keyboard_add_logged=0;
static volatile uint8_t evdev_epoll_mouse_add_logged=0;
static volatile uint8_t evdev_epoll_keyboard_mod_logged=0;
static volatile uint8_t evdev_epoll_mouse_mod_logged=0;
static volatile uint8_t evdev_epoll_keyboard_ready_logged=0;
static volatile uint8_t evdev_epoll_mouse_ready_logged=0;
static volatile uint8_t evdev_epoll_keyboard_masked_logged=0;
static volatile uint8_t evdev_epoll_mouse_masked_logged=0;
static volatile uint8_t evdev_keyboard_poll_ready_logged=0;
static volatile uint8_t evdev_mouse_poll_ready_logged=0;
static volatile uint8_t evdev_keyboard_ioctl_logged=0;
static volatile uint8_t evdev_poll_keyboard_logged=0;
static volatile uint8_t evdev_poll_mouse_logged=0;
static volatile uint64_t keyboard_irq_scancodes=0;
static volatile uint64_t keyboard_poll_ticks=0;
static volatile uint64_t keyboard_poll_bytes=0;
static volatile uint64_t keyboard_read_calls=0;
static volatile uint64_t keyboard_read_events=0;
static volatile uint8_t mouse_packet_logged=0;
static volatile uint8_t mouse_event_logged=0;
static volatile uint8_t mouse_motion_logged=0;
static volatile uint8_t mouse_poll_path_logged=0;
static volatile uint8_t evdev_mouse_read_logged=0;
static volatile uint8_t evdev_keyboard_open_logged=0;
static volatile uint8_t evdev_mouse_open_logged=0;
static volatile uint64_t mouse_read_calls=0;
static volatile uint64_t mouse_read_ready=0;
static volatile uint64_t mouse_read_success=0;
static volatile uint64_t mouse_read_copy_fail=0;
static volatile uint64_t mouse_poll_ready=0;
static volatile uint64_t mouse_status_obf_seen=0;
static volatile uint64_t mouse_status_aux_seen=0;
static volatile uint64_t mouse_last_status=0;
static volatile uint64_t mouse_init_stage=0;
static volatile uint64_t mouse_init_ok=0;
static volatile uint8_t mouse_controller_config=0;
static volatile uint8_t mouse_last_byte=0;
static volatile uint8_t mouse_last_source=0;
static void input_event_stamp(struct input_event_k *e){ uint64_t ns=kernel_monotonic_ns(); e->tv_sec=(int64_t)(ns/1000000000ULL); e->tv_usec=(int64_t)((ns%1000000000ULL)/1000ULL); }
static int task_wake_io_waiters(void);
static void input_push_event(uint16_t type,uint16_t code,int32_t value){

	if(type==EV_KEY && code < (sizeof(keyboard_key_state)*8u)) {
		uint8_t mask=(uint8_t)(1u<<(code&7u));
		if(value==0) keyboard_key_state[code>>3] &= (uint8_t)~mask;
		else keyboard_key_state[code>>3] |= mask;
	}
	struct input_event_k e={0,0,type,code,value}; input_event_stamp(&e);
	input_event_queue[keyboard_event_seq % INPUT_EVENT_CAP]=e;
	keyboard_event_seq++;
	if(type==EV_KEY && code==45 && (value==0 || value==1) && keyboard_x_event_trace_count<16) {
		keyboard_x_event_trace_count++;
		debugcon_puts("[KEYBOARD X EV_KEY]");
		debug_hex64(" code=0x",(uint64_t)code);
		debug_hex64(" value=0x",(uint64_t)(int64_t)value);
		debug_hex64(" seq=0x",keyboard_event_seq);
		debugcon_puts("\n");
	}
	if(type==EV_KEY && (value==0 || value==1)) {
		int trace_base=-1;
		if(code==29) trace_base=0;                         
		else if(code==97) trace_base=2;                     
		else if(code==56) trace_base=4;                   
		else if(code==100) trace_base=6;                   
		else if(code==45) trace_base=8;             
		if(trace_base>=0) {
			unsigned bit=(unsigned)trace_base+(value==0?1u:0u);
			uint16_t flag=(uint16_t)(1u<<bit);
			if(!(keyboard_hotkey_trace_mask&flag)) {
				keyboard_hotkey_trace_mask|=flag;
				debugcon_puts("[KEYBOARD HOTKEY EVENT]");
				debug_hex64(" code=0x",(uint64_t)code);
				debug_hex64(" value=0x",(uint64_t)(int64_t)value);
				debug_hex64(" seq=0x",keyboard_event_seq);
				debugcon_puts("\n");
			}
		}
	}
	if(type==EV_KEY && !keyboard_event_queued_logged) {
		debugcon_puts("[KEYBOARD EVENT] first EV_KEY queued to event0\n");
		keyboard_event_queued_logged=1;
	}
	task_wake_io_waiters();
}
static void mouse_push_event(uint16_t type,uint16_t code,int32_t value){
	struct input_event_k e={0,0,type,code,value};
	input_event_stamp(&e);
	mouse_event_queue[mouse_event_seq % MOUSE_EVENT_CAP]=e;
	mouse_event_seq++;
	mouse_event_pushes++;
	if(!mouse_event_logged) { debugcon_puts("[MOUSE EVENT] first event queued to event1\n"); mouse_event_logged=1; }

}
#define KEY_QUEUE_CAP 128
static volatile uint8_t key_queue[KEY_QUEUE_CAP];
static volatile uint8_t key_head = 0;
static volatile uint8_t key_tail = 0;

static uint8_t console_tty_pending_bytes[4];
static uint8_t console_tty_pending_pos = 0;
static uint8_t console_tty_pending_len = 0;
static int console_tty_pending_pid = -1;
uint8_t shift_pressed = 0;
uint8_t get_key(void);
uint8_t shift_left_pressed = 0;
uint8_t shift_right_pressed = 0;
uint8_t caps_lock = 0;
static volatile uint8_t ctrl_left_pressed = 0;
static volatile uint8_t ctrl_right_pressed = 0;

static int console_fg_pgrp = 0;
static int console_shell_pgrp = 0;
static int console_tty_pgrp_signal(int sig);
static int console_tty_read_ready_for(int pid, int pgrp) {
	if (console_tty_pending_pos < console_tty_pending_len && console_tty_pending_pid == pid) return 1;
	if (key_head != key_tail && (console_fg_pgrp <= 0 || pgrp == console_fg_pgrp)) return 1;
	return 0;
}

static void keyboard_process_scancode(uint8_t scancode) {
	if(!keyboard_scancode_logged) {
		debugcon_puts("[KEYBOARD EVENT] first PS/2 scancode decoded\n");
		keyboard_scancode_logged=1;
	}
	static uint8_t e0_prefix = 0;

	uint8_t trace_mask=(uint8_t)(1u<<(scancode&7u));
	uint8_t trace_index=(uint8_t)(scancode>>3);
	if(!(keyboard_raw_scancode_trace_mask[trace_index]&trace_mask)) {
		keyboard_raw_scancode_trace_mask[trace_index]|=trace_mask;
		debugcon_puts("[KEYBOARD RAW SCANCODE]");
		debug_hex64(" byte=0x",(uint64_t)scancode);
		debug_hex64(" e0_prefix=0x",(uint64_t)e0_prefix);
		debugcon_puts("\n");
	}

	if((scancode==0x2D || scancode==0xAD) && keyboard_x_scancode_trace_count<16) {
		keyboard_x_scancode_trace_count++;
		debugcon_puts("[KEYBOARD RAW X SCANCODE]");
		debug_hex64(" byte=0x",(uint64_t)scancode);
		debug_hex64(" e0_prefix=0x",(uint64_t)e0_prefix);
		debug_hex64(" release=0x",(uint64_t)((scancode&0x80u)!=0));
		debugcon_puts("\n");
	}
	if(scancode == 0xE0){ e0_prefix=1; goto done; }
	if(e0_prefix){
		e0_prefix=0; uint8_t rel=(scancode&0x80)!=0, b=scancode&0x7f; uint16_t kc=0;
		if(b==0x1D) ctrl_right_pressed = (uint8_t)!rel;
		switch(b){case 0x1C:kc=96;break;case 0x1D:kc=97;break;case 0x35:kc=98;break;case 0x38:kc=100;break;case 0x47:kc=102;break;case 0x48:kc=103;break;case 0x49:kc=104;break;case 0x4B:kc=105;break;case 0x4D:kc=106;break;case 0x4F:kc=107;break;case 0x50:kc=108;break;case 0x51:kc=109;break;case 0x52:kc=110;break;case 0x53:kc=111;break;case 0x5B:kc=125;break;case 0x5C:kc=126;break;case 0x5D:kc=127;break;default:goto done;}
		keyboard_emit_key(kc,!rel);

		if(!rel) {
			uint8_t tty_key = 0;
			switch(b){
				case 0x48: tty_key=0x80; break;         
				case 0x50: tty_key=0x81; break;           
				case 0x4B: tty_key=0x82; break;           
				case 0x4D: tty_key=0x83; break;            
				case 0x47: tty_key=0x85; break;           
				case 0x4F: tty_key=0x86; break;          
				case 0x53: tty_key=0x87; break;             
				case 0x49: tty_key=0x88; break;              
				case 0x51: tty_key=0x89; break;                
				case 0x52: tty_key=0x8A; break;             
				default: break;
			}
			if(tty_key){
				uint8_t next=(uint8_t)((key_tail+1u)%KEY_QUEUE_CAP);
				if(next!=key_head){ key_queue[key_tail]=tty_key; key_tail=next; task_wake_io_waiters(); }
			}
		}
		goto done;
	}

	if (scancode == 0x1D) {
		ctrl_left_pressed = 1;
		keyboard_emit_key(29,1);
		goto done;
	}
	if (scancode == 0x9D) {
		ctrl_left_pressed = 0;
		keyboard_emit_key(29,0);
		goto done;
	}

	if (scancode == 0x2A) {
		keyboard_emit_key(42,1);
		shift_left_pressed = 1;
		shift_pressed = 1;
		goto done;
	}
	if (scancode == 0x36) {
		keyboard_emit_key(54,1);
		shift_right_pressed = 1;
		shift_pressed = 1;
		goto done;
	}
	if (scancode == 0xAA) {
		keyboard_emit_key(42,0);
		shift_left_pressed = 0;
		shift_pressed = shift_right_pressed;
		goto done;
	}
	if (scancode == 0xB6) {
		keyboard_emit_key(54,0);
		shift_right_pressed = 0;
		shift_pressed = shift_left_pressed;
		goto done;
	}
	if (scancode == 0x3A) {

		if(!keyboard_key_is_down(58)) caps_lock = caps_lock ? 0 : 1;
		keyboard_emit_key(58,1);
		goto done;
	}

	if (scancode & 0x80) {
		uint8_t base=(uint8_t)(scancode & 0x7f);
		if(base!=0x2A && base!=0x36) keyboard_emit_key(base,0);
		goto done;
	}

	uint8_t key = 0;
	if (scancode == 0x01) key = 27;
	else if (scancode == 0x39) key = ' ';
	else key = scancode;

	if (key != 0) {
		keyboard_emit_key(scancode,1);
		uint8_t queued_key = scancode;
		if ((ctrl_left_pressed || ctrl_right_pressed) && scancode == 0x2E) {

			if (!framebuffer_console_active) goto done;
			int fg = console_fg_pgrp;
			int foreground_job = fg > 0 && console_shell_pgrp > 0 && fg != console_shell_pgrp;
			if (foreground_job) {
				(void)console_tty_pgrp_signal(2             );
				goto done;
			}
			queued_key = 0x84;                                                  
		} else if ((ctrl_left_pressed || ctrl_right_pressed) && framebuffer_console_active) {
			int fg = console_fg_pgrp;
			int foreground_job = fg > 0 && console_shell_pgrp > 0 && fg != console_shell_pgrp;
			if (foreground_job) {
				char letter = 0;
				switch (scancode) {
					case 0x10: letter='q'; break; case 0x11: letter='w'; break;
					case 0x12: letter='e'; break; case 0x13: letter='r'; break;
					case 0x14: letter='t'; break; case 0x15: letter='y'; break;
					case 0x16: letter='u'; break; case 0x17: letter='i'; break;
					case 0x18: letter='o'; break; case 0x19: letter='p'; break;
					case 0x1E: letter='a'; break; case 0x1F: letter='s'; break;
					case 0x20: letter='d'; break; case 0x21: letter='f'; break;
					case 0x22: letter='g'; break; case 0x23: letter='h'; break;
					case 0x24: letter='j'; break; case 0x25: letter='k'; break;
					case 0x26: letter='l'; break; case 0x2C: letter='z'; break;
					case 0x2D: letter='x'; break; case 0x2E: letter='c'; break;
					case 0x2F: letter='v'; break; case 0x30: letter='b'; break;
					case 0x31: letter='n'; break; case 0x32: letter='m'; break;
					default: break;
				}
				if (letter) queued_key = (uint8_t)(0xA0 + letter - 'a');
			}
		}
		uint8_t next = (uint8_t)((key_tail + 1u) % KEY_QUEUE_CAP);
		if (next != key_head) {
			key_queue[key_tail] = queued_key;
			key_tail = next;
			task_wake_io_waiters();
		}
	}
done:
	return;
}

void keyboard_handler(void) {
	if(!keyboard_irq_logged) {
		debugcon_puts("[KEYBOARD IRQ] first IRQ1 received\n");
		keyboard_irq_logged=1;
	}

	uint8_t st = inb(0x64);
	if (!(st & 0x01) || (st & 0x20)) {
		outb(PIC1_COMMAND, 0x20);
		return;
	}
	uint8_t scancode = inb(0x60);
	keyboard_irq_scancodes++;
	keyboard_process_scancode(scancode);
	outb(PIC1_COMMAND, 0x20);
}

static int ps2_wait_input_clear(void) {
	for (uint32_t i=0;i<100000;i++) if (!(inb(0x64)&0x02)) return 1;
	return 0;
}
static int ps2_wait_output(void) {
	for (uint32_t i=0;i<100000;i++) if (inb(0x64)&0x01) return 1;
	return 0;
}
static void ps2_flush_output(void) {

	for (uint32_t i=0;i<64;i++) {
		if (!(inb(0x64)&0x01)) break;
		(void)inb(0x60);
	}
}
static int ps2_write_cmd(uint8_t cmd) {
	if (!ps2_wait_input_clear()) return 0;
	outb(0x64,cmd); return 1;
}
static int ps2_write_data(uint8_t data) {
	if (!ps2_wait_input_clear()) return 0;
	outb(0x60,data); return 1;
}

static int ps2_keyboard_init(void) {
	uint8_t config=0, ack=0;
	ps2_flush_output();
	if (!ps2_write_cmd(0xAD)) return 0;                                              
	if (!ps2_write_cmd(0x20) || !ps2_wait_output()) goto enable_port_on_error;
	config=inb(0x60);
	config |= 0x41;                                                            
	config &= (uint8_t)~0x10;                                         
	if (!ps2_write_cmd(0x60) || !ps2_write_data(config)) goto enable_port_on_error;
	if (!ps2_write_cmd(0xAE)) return 0;                                 
	if (!ps2_write_data(0xF4) || !ps2_wait_output()) return 0;                      
	ack=inb(0x60);
	return ack==0xFA;

enable_port_on_error:
	(void)ps2_write_cmd(0xAE);
	return 0;
}

static void ps2_keyboard_poll(void) {
	keyboard_poll_ticks++;
	for (uint32_t drain=0; drain<16; drain++) {
		uint8_t st=inb(0x64);
		if (!(st & 0x01) || (st & 0x20)) break;                              
		uint8_t scancode=inb(0x60);
		keyboard_poll_bytes++;
		if(!keyboard_poll_logged) {
			debugcon_puts("[KEYBOARD POLL] first PS/2 scancode drained from controller\n");
			keyboard_poll_logged=1;
		}
		keyboard_process_scancode(scancode);
	}
}

static int ps2_mouse_write(uint8_t data) {
	if (!ps2_write_cmd(0xD4)) return 0;
	if (!ps2_write_data(data)) return 0;
	if (!ps2_wait_output()) return 0;
	return inb(0x60)==0xFA;
}
static int ps2_mouse_read(uint8_t *out) {
	if (!out || !ps2_wait_output()) return 0;
	*out=inb(0x60); return 1;
}
static int ps2_mouse_init(void) {
	uint8_t cmd=0;
	mouse_init_stage=1; mouse_init_ok=0;

	ps2_flush_output();
	if (!ps2_write_cmd(0xA8)) return 0;                                      
	ps2_flush_output();
	mouse_init_stage=2;
	if (!ps2_write_cmd(0x20) || !ps2_wait_output()) return 0;
	cmd=inb(0x60);
	mouse_controller_config=cmd;
	cmd |= 0x02;                                                        
	cmd &= (uint8_t)~0x20;                                                  
	if (!ps2_write_cmd(0x60) || !ps2_write_data(cmd)) return 0;
	mouse_controller_config=cmd;
	mouse_init_stage=3;

	mouse_init_stage=4;
	if (!ps2_mouse_write(0xF5)) return 0;
	mouse_init_stage=5;
	if (!ps2_mouse_write(0xF6)) return 0;                                
	ps2_flush_output();
	mouse_init_stage=6;
	if (!ps2_mouse_write(0xF4)) return 0;                                    
	mouse_init_ok=1;
	return 1;
}
static uint8_t mouse_packet[3];
static uint8_t mouse_packet_pos=0;
static uint8_t mouse_buttons=0;
static uint64_t mouse_packets=0;
static void ps2_mouse_poll(void);

static void mouse_process_byte(uint8_t b, uint8_t source) {
	mouse_last_byte=b;
	mouse_last_source=source;
	if(source==1) {
		mouse_poll_bytes++;
		if(!mouse_poll_path_logged) {
			debugcon_puts("[MOUSE PATH] PIT polling fallback delivered bytes; IRQ12 has not fired\n");
			mouse_poll_path_logged=1;
		}
	} else if(source==2) mouse_irq_bytes++;
	if (mouse_packet_pos==0 && !(b&0x08)) {
		mouse_sync_rejects++;

		return;
	}
	mouse_packet[mouse_packet_pos++]=b;
	if(mouse_packet_pos<3) return;
	mouse_packet_pos=0;
	if(!mouse_packet_logged) { debugcon_puts("[MOUSE PACKET] first PS/2 packet decoded\n"); mouse_packet_logged=1; }
	uint8_t flags=mouse_packet[0], buttons=flags&0x07;
	if(flags&0xC0) {
		mouse_bad_packets++;
		mouse_push_event(EV_SYN,SYN_REPORT,0);
		task_wake_io_waiters();
		return;
	}
	int32_t dx=(int8_t)mouse_packet[1];
	int32_t dy=-(int8_t)mouse_packet[2];
	if(!mouse_motion_logged && (dx || dy)) {
		debugcon_puts("[MOUSE MOTION] dx="); debug_hex64("", (uint64_t)(int64_t)dx);
		debugcon_puts("[MOUSE MOTION] dy="); debug_hex64("", (uint64_t)(int64_t)dy);
		debugcon_puts("[MOUSE MOTION] buttons="); debug_hex64("", (uint64_t)buttons);
		mouse_motion_logged=1;
	}
	if(dx) mouse_push_event(EV_REL,REL_X,dx);
	if(dy) mouse_push_event(EV_REL,REL_Y,dy);
	for(uint8_t bit=0;bit<3;bit++) if((buttons^mouse_buttons)&(1u<<bit))
		mouse_push_event(EV_KEY,(uint16_t)(BTN_LEFT+bit), (buttons&(1u<<bit))?1:0);
	mouse_buttons=buttons;
	mouse_packets++;

	mouse_push_event(EV_SYN,SYN_REPORT,0);
	task_wake_io_waiters();
}

static void ps2_mouse_poll(void) {
	if(mouse_irq12_hits) return;

	mouse_poll_ticks++;
	for (uint32_t drain=0; drain<8; drain++) {
		uint8_t st=inb(0x64);
		mouse_last_status=st;
		if(st&0x01) mouse_status_obf_seen++;
		if(st&0x20) mouse_status_aux_seen++;
		if (!(st&0x01) || !(st&0x20)) break;
		mouse_process_byte(inb(0x60),1);
	}
}

void mouse_handler(void) {
	mouse_irq12_hits++;
	if(!mouse_irq_logged) { debugcon_puts("[MOUSE IRQ] first IRQ12 received\n"); mouse_irq_logged=1; }

	for (uint32_t drain=0; drain<8; drain++) {
		uint8_t st=inb(0x64);
		mouse_last_status=st;
		if(st&0x01) mouse_status_obf_seen++;
		if(st&0x20) mouse_status_aux_seen++;
		if (!(st&0x01) || !(st&0x20)) break;
		mouse_process_byte(inb(0x60),2);
	}
	outb(PIC2_COMMAND,0x20); outb(PIC1_COMMAND,0x20);
}

__asm__ (
".global mouse_isr_stub\n"
"mouse_isr_stub:\n"
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
"    movq %rsp, %r12\n"
"    andq $-16, %rsp\n"
"    call mouse_handler\n"
"    movq %r12, %rsp\n"
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
"    iretq\n"
);

__asm__ (
".global keyboard_isr_stub\n"
"keyboard_isr_stub:\n"
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
"    movq %rsp, %r12\n"
"    andq $-16, %rsp\n"
"    call keyboard_handler\n"
"    movq %r12, %rsp\n"
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
"    iretq\n"
);

static int ps2_mouse_init(void);

static void pit_init(uint32_t hz) {
	if (hz < 20) hz = 20;
	if (hz > 1000) hz = 1000;
	uint32_t div = 1193182u / hz;
	if (div < 1) div = 1;
	outb(0x43, 0x36);                                       
	outb(0x40, (uint8_t)(div & 0xff));
	outb(0x40, (uint8_t)((div >> 8) & 0xff));
	debug_hex64("[PIT] hz=0x", (uint64_t)hz);
	debug_hex64(" div=0x", (uint64_t)div);
	debugcon_puts("\n");
}

void setup_interrupts(void) {
	__asm__ volatile ("cli" ::: "memory");
	init_gdt();
	cpu_hardening_init();
	init_syscall_msrs();
	pic_remap();
	init_idt();
	pit_init(100);

	outb(PIC1_DATA, 0xF8);
	outb(PIC2_DATA, 0xEF);
	if (!ps2_keyboard_init()) debugcon_puts("[INPUT] PS/2 keyboard init warning\n");
	else debugcon_puts("[INPUT] PS/2 keyboard initialized\n");
	if (!ps2_mouse_init()) debugcon_puts("[INPUT] PS/2 mouse init failed\n");
	else debugcon_puts("[INPUT] PS/2 mouse initialized\n");
	io_wait();

	__asm__ volatile ("sti" ::: "memory");
}

uint8_t get_key(void) {
	if (key_head == key_tail) return 0;
	uint8_t key = key_queue[key_head];
	key_head = (uint8_t)((key_head + 1u) % KEY_QUEUE_CAP);
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

#define MAX_FILES 512
#define MAX_TASKS 64                                                                
#define MAX_FD_TABLES 32                                                                                            
#ifndef EMFILE
#define EMFILE 24
#endif
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define O_APPEND 1024

struct input_open_state;
struct file_desc {
	int in_use;
	int type;
	int flags;
	uint64_t offset;
	uint64_t size;
	uint8_t *data;
	const char *name;
	char boot_path[VFS_PATH_MAX];
	void *private_data;
	struct vfs_node *vnode;
	uint64_t *shared_offset;

	struct input_open_state *input_state;
	uint64_t input_seq;
};

struct input_open_state { int used; uint32_t refs; uint32_t kind; uint64_t seq; };
#define INPUT_OPEN_STATE_CAP (MAX_TASKS * MAX_FILES)
static struct input_open_state input_open_states[INPUT_OPEN_STATE_CAP];
static struct input_open_state *input_state_alloc(uint32_t kind, uint64_t seq) {
	for(int i=0;i<INPUT_OPEN_STATE_CAP;i++) if(!input_open_states[i].used) {
		input_open_states[i].used=1; input_open_states[i].refs=1;
		input_open_states[i].kind=kind; input_open_states[i].seq=seq; return &input_open_states[i];
	}
	return NULL;
}
static void input_state_retain(struct input_open_state *st) {
	if(st && st->used && st->refs<UINT32_MAX) st->refs++;
}
static void input_state_release(struct input_open_state *st) {
	if(!st || !st->used) return;
	if(st->refs>1) { st->refs--; return; }
	st->refs=0; st->used=0; st->kind=0; st->seq=0;
}
static uint64_t input_fd_seq(const struct file_desc *f) {
	return (f && f->input_state && f->input_state->used) ? f->input_state->seq : (f ? f->input_seq : 0);
}
static void input_fd_seq_set(struct file_desc *f, uint64_t seq) {
	if(!f) return;
	f->input_seq=seq;
	if(f->input_state && f->input_state->used) f->input_state->seq=seq;
}

struct open_desc_state { uint64_t offset; uint32_t refs; int used; };
static struct open_desc_state open_descs[MAX_FILES];
static uint64_t *alloc_open_offset(void) { for (int i=0;i<MAX_FILES;i++) if(!open_descs[i].used){open_descs[i].used=1;open_descs[i].refs=1;open_descs[i].offset=0;return &open_descs[i].offset;} return NULL; }
static void retain_open_offset(uint64_t *p) { if(!p)return; struct open_desc_state *s=(struct open_desc_state*)((uint8_t*)p - __builtin_offsetof(struct open_desc_state, offset)); s->refs++; }
static void release_open_offset(uint64_t *p) { if(!p)return; struct open_desc_state *s=(struct open_desc_state*)((uint8_t*)p - __builtin_offsetof(struct open_desc_state, offset)); if(s->refs && --s->refs==0)s->used=0; }
static void fd_drop_resources(struct file_desc *f);
static int64_t sys_dup3(int oldfd,int newfd,int flags);
static uint64_t fd_offset_get(struct file_desc *f){ return f->shared_offset ? *f->shared_offset : f->offset; }
static void fd_offset_set(struct file_desc *f,uint64_t v){ f->offset=v; if(f->shared_offset)*f->shared_offset=v; }
static void fd_set_boot_path(struct file_desc *f,const char *path){
	if(!f)return;
	size_t i=0;
	if(path){ while(path[i] && i+1<sizeof(f->boot_path)){ f->boot_path[i]=path[i]; i++; } }
	f->boot_path[i]=0;
}

static int fd_uses_bootfs(const struct file_desc *f) {
	if (!f) return 0;
	if (f->vnode) return vfs_is_boot_file(f->vnode);
	return f->boot_path[0] != 0;
}
static const char *fd_bootfs_path(const struct file_desc *f) {
	if (!f) return NULL;
	if (f->vnode && vfs_is_boot_file(f->vnode)) {
		const char *vp = vfs_boot_path(f->vnode);
		if (vp && vp[0]) return vp;
	}
	return f->boot_path[0] ? f->boot_path : NULL;
}

#define FD_FILE 1
#define FD_PIPE_R 2
#define FD_PIPE_W 3
#define FD_PSEUDO 4
#define FD_SOCKET 5
#define FD_EVENTFD 6
#define FD_TIMERFD 7
#define FD_PTY_MASTER 8
#define FD_PTY_SLAVE 9
#define FD_EPOLL 10
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
#define DEV_FB 7
#ifndef YABRO_DEBUG_FB
#define YABRO_DEBUG_FB 0
#endif

static int fb_trace_allowed(void){
#if YABRO_DEBUG_FB
 static unsigned budget=16;
 if(!budget) return 0;
 --budget;
 return 1;
#else
 return 0;
#endif
}
#define DEV_INPUT 8
#define DEV_INPUT_MOUSE 9
static uint64_t keyboard_event_oldest_seq(void){
	return keyboard_event_seq>INPUT_EVENT_CAP ? keyboard_event_seq-INPUT_EVENT_CAP : 0;
}
static void keyboard_fd_sync(struct file_desc *f){
	if(!f || f->type!=FD_PSEUDO || (uintptr_t)f->private_data!=DEV_INPUT)return;
	uint64_t oldest=keyboard_event_oldest_seq();
	uint64_t seq=input_fd_seq(f);
	if(seq<oldest) input_fd_seq_set(f,oldest);
}
static int keyboard_fd_ready(struct file_desc *f){
	if(!f || f->type!=FD_PSEUDO || (uintptr_t)f->private_data!=DEV_INPUT)return 0;
	keyboard_fd_sync(f);
	return input_fd_seq(f)<keyboard_event_seq;
}
static int keyboard_pop_event_fd(struct file_desc *f, struct input_event_k *out){
	if(!keyboard_fd_ready(f))return 0;
	uint64_t seq=input_fd_seq(f);
	*out=input_event_queue[seq % INPUT_EVENT_CAP];
	input_fd_seq_set(f,seq+1);
	return 1;
}
static uint64_t mouse_event_oldest_seq(void){
	return mouse_event_seq>MOUSE_EVENT_CAP ? mouse_event_seq-MOUSE_EVENT_CAP : 0;
}
static void mouse_fd_sync(struct file_desc *f){
	if(!f || f->type!=FD_PSEUDO || (uintptr_t)f->private_data!=DEV_INPUT_MOUSE)return;
	uint64_t oldest=mouse_event_oldest_seq();
	uint64_t seq=input_fd_seq(f);
	if(seq<oldest) input_fd_seq_set(f,oldest);
}
static int mouse_fd_ready(struct file_desc *f){
	if(!f || f->type!=FD_PSEUDO || (uintptr_t)f->private_data!=DEV_INPUT_MOUSE)return 0;
	mouse_fd_sync(f);
	return input_fd_seq(f)<mouse_event_seq;
}
static int mouse_pop_event_fd(struct file_desc *f, struct input_event_k *out){
	if(!mouse_fd_ready(f))return 0;
	uint64_t seq=input_fd_seq(f);
	*out=mouse_event_queue[seq % MOUSE_EVENT_CAP];
	input_fd_seq_set(f,seq+1);
	return 1;
}
#define FBIOGET_VSCREENINFO 0x4600ULL
#define FBIOPUT_VSCREENINFO 0x4601ULL
#define FBIOGET_FSCREENINFO 0x4602ULL
#define FBIOPAN_DISPLAY 0x4606ULL
#define FBIOBLANK 0x4611ULL
#define FBIOGET_VBLANK 0x4612ULL
#define FBIO_WAITFORVSYNC 0x4620ULL
#define FBIOGETCMAP 0x4604ULL
#define FBIOPUTCMAP 0x4605ULL

#define FBIOGET_INFO 0x46f0ULL

struct fb_bitfield_k { uint32_t offset, length, msb_right; };
struct fb_var_screeninfo_k {
	uint32_t xres,yres,xres_virtual,yres_virtual,xoffset,yoffset,bits_per_pixel,grayscale;
	struct fb_bitfield_k red,green,blue,transp;
	uint32_t nonstd,activate,height,width,accel_flags;
	uint32_t pixclock,left_margin,right_margin,upper_margin,lower_margin,hsync_len,vsync_len;
	uint32_t sync,vmode,rotate,colorspace;
	uint32_t reserved[4];
};
struct fb_cmap_k {
	uint32_t start;
	uint32_t len;
	uint64_t red;
	uint64_t green;
	uint64_t blue;
	uint64_t transp;
};
struct fb_fix_screeninfo_k {
	char id[16];
	unsigned long smem_start;
	uint32_t smem_len;
	uint32_t type,type_aux,visual;
	uint16_t xpanstep,ypanstep,ywrapstep;
	uint32_t line_length;
	unsigned long mmio_start;
	uint32_t mmio_len,accel;
	uint16_t capabilities;
	uint16_t reserved[2];
};
#define EVIOCGVERSION 0x80044501ULL
#define EVIOCGID 0x80084502ULL
#define EVIOCGNAME(n) (0x80004506ULL | (((uint64_t)(n) & 0x3fffULL) << 16))
#define EVIOCGBIT(ev,n) (0x80000000ULL | (((uint64_t)(n) & 0x3fffULL) << 16) | (0x45ULL << 8) | (0x20ULL + ((uint64_t)(ev) & 0x1fULL)))
#define EV_SYN 0
#define EV_KEY 1
#define EV_REL 2
#define REL_X 0
#define REL_Y 1
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111
#define BTN_MIDDLE 0x112
#define BUS_I8042 0x11
#define INPUT_PROP_POINTER 0
#define EV_MAX 0x1f
#define KEY_MAX 0x2ff
#define EVIOCGVERSION 0x80044501ULL
#define EVIOCGREP 0x80084503ULL
#define EVIOCSREP 0x40084503ULL
#define EVIOCGPHYS(n) (0x80004507ULL | (((uint64_t)(n) & 0x3fffULL) << 16))
#define EVIOCGUNIQ(n) (0x80004508ULL | (((uint64_t)(n) & 0x3fffULL) << 16))
#define EVIOCGPROP(n) (0x80004509ULL | (((uint64_t)(n) & 0x3fffULL) << 16))
#define EVIOCGKEY(n) (0x80004518ULL | (((uint64_t)(n) & 0x3fffULL) << 16))
#define EVIOCGLED(n) (0x80004519ULL | (((uint64_t)(n) & 0x3fffULL) << 16))
#define EVIOCGSND(n) (0x8000451aULL | (((uint64_t)(n) & 0x3fffULL) << 16))
#define EVIOCGSW(n)  (0x8000451bULL | (((uint64_t)(n) & 0x3fffULL) << 16))
#define EVIOCGABS(abs) (0x80184540ULL + ((uint64_t)(abs) & 0x3fULL))
#define EVIOCGRAB 0x40044590ULL
#define EVIOCSCLOCKID 0x400445a0ULL
#define PSEUDO_PROC_FILE 100
#define PSEUDO_PROC_DIR 101
#define PSEUDO_DEV_DIR 102
#define PSEUDO_SYS_DIR 103
#define PSEUDO_PROC_ROOT 104
#define PSEUDO_PROC_SELF 105
#define PSEUDO_PROC_FD 108
#define PSEUDO_DEV_FILE 106
#define PSEUDO_SYS_FILE 107

#define PTY_CAP 65536
#define TIOCGPTN 0x80045430ULL
#define TIOCSPTLCK 0x40045431ULL
#define EACCES_K 13
#define EBUSY 16
#define ENXIO 6
#define EACCES EACCES_K
#define PSEUDO_PTMX 110
#define PSEUDO_PTS_DIR 111
#define PSEUDO_PTS_SLAVE 112

typedef struct {
	uint8_t master_to_slave[PTY_CAP];
	uint8_t slave_to_master[PTY_CAP];
	uint32_t m2s_r, m2s_w, m2s_count;
	uint32_t s2m_r, s2m_w, s2m_count;
	int master_open, slave_open, unlocked;
	uint32_t master_refs, slave_refs;
	uint16_t rows, cols, xpixel, ypixel;
	uint8_t termios[60];
	uint8_t canon[PTY_CAP];
	uint32_t canon_count;
	int fg_pgrp;
	int shell_pgrp;
	int session;
	int output_stopped;
	uint32_t index;
} pty_state_k;

#define PTY_COUNT 8
static pty_state_k pty_slots[PTY_COUNT];
static int pty_index_from_path(const char *path) {
	static const char prefix[] = "/dev/pts/";
	if (!path) return -1;
	for (int i=0; i<9; ++i) if (path[i] != prefix[i]) return -1;
	if (path[9] < '0' || path[9] > '9') return -1;
	int n = 0;
	for (int i=9; path[i]>='0' && path[i]<='9'; ++i) {
		n = n*10 + (path[i]-'0');
		if (n >= PTY_COUNT) return -1;
	}
	return path[9] && path[10] == 0 ? n : -1;
}
static pty_state_k *pty_debug_state(const struct file_desc *f) {
	if (f) {
		uintptr_t raw=(uintptr_t)f->private_data;
		for (int i=0; i<PTY_COUNT; ++i)
			if (raw==(uintptr_t)&pty_slots[i]) return &pty_slots[i];
	}
	return &pty_slots[0];
}
static int pty_canon_available(pty_state_k *p);
static int pty_master_read_ready(const pty_state_k *p);
static int pty_slave_read_ready(const pty_state_k *p);
static int pty_canon_ready_bytes(const pty_state_k *p);

static uint8_t pty_dbg_master_open, pty_dbg_master_ioctl, pty_dbg_unlock;
static uint8_t pty_dbg_master_open_busy, pty_dbg_xterm_exec, pty_dbg_slave_write_done;
static uint8_t pty_dbg_slave_open, pty_dbg_slave_reject, pty_dbg_ysh_exec;
static uint8_t pty_dbg_slave_write, pty_dbg_master_read, pty_dbg_master_data;
static uint8_t pty_dbg_select_master, pty_dbg_poll_master;

static uint32_t pty_dbg_control_input_mask=0;
static uint8_t pty_dbg_ptmx_open_count, pty_dbg_pts_open_count;
static uint8_t pty_dbg_ioctl_count;
static uint8_t pty_dbg_dup_lowfd;
static void pty_dbg(const char *tag, int fd, const struct file_desc *f) {
    pty_state_k *p=pty_debug_state(f);
    debugcon_puts(tag);
    debug_hex64(" fd=0x", (uint64_t)(uint32_t)fd);
    if (f) {
        debug_hex64(" in_use=0x", (uint64_t)(uint32_t)f->in_use);
        debug_hex64(" type=0x", (uint64_t)(uint32_t)f->type);
        debug_hex64(" flags=0x", (uint64_t)(uint32_t)f->flags);
    }
    debug_hex64(" pty=0x", (uint64_t)p->index);
    debug_hex64(" master_open=0x", (uint64_t)(uint32_t)p->master_open);
    debug_hex64(" slave_open=0x", (uint64_t)(uint32_t)p->slave_open);
    debug_hex64(" unlocked=0x", (uint64_t)(uint32_t)p->unlocked);
    debug_hex64(" m2s=0x", p->m2s_count);
    debug_hex64(" s2m=0x", p->s2m_count);
    debug_hex64(" master_refs=0x", p->master_refs);
    debug_hex64(" slave_refs=0x", p->slave_refs);
    debugcon_puts("\n");
}

static const uint8_t pty_default_termios[60] = {
    0x00,0x05,0x00,0x00,                                        
    0x05,0x00,0x00,0x00,                                         
    0xbf,0x00,0x00,0x00,                                                                
    0x3b,0x8a,0x00,0x00,                                                               
    0x00,                                        
    0x03,0x1c,0x7f,0x15,0x04,0x00,0x01,0x00,
    0x11,0x13,0x1a,0x00,0x12,0x0f,0x17,0x16,0x00,
};
static void pty_reset_termios(pty_state_k *p) {
    if (!p) return;
    for (size_t i=0;i<sizeof(p->termios);i++) p->termios[i]=pty_default_termios[i];
}

static void pty_init_once(void) {
	static int done;
	if (done) return;
	done = 1;
	for (int i=0; i<PTY_COUNT; ++i) {
		pty_state_k *p=&pty_slots[i];
		for (size_t b=0; b<sizeof(*p); ++b) ((uint8_t*)p)[b]=0;
		p->index=(uint32_t)i;
		p->rows=30; p->cols=80; p->xpixel=640; p->ypixel=480;
		pty_reset_termios(p);
	}
}

static int pty_ring_write(uint8_t *buf, uint32_t *w, uint32_t *count, const uint8_t *src, uint64_t n) {
	uint64_t done=0;
	while (done<n && *count<PTY_CAP) {
		buf[*w]=src[done++]; *w=(*w+1)%PTY_CAP; (*count)++;
	}
	return (int)done;
}
static int pty_ring_read(uint8_t *buf, uint32_t *r, uint32_t *count, uint8_t *dst, uint64_t n) {
	uint64_t done=0;
	while (done<n && *count) {
		dst[done++]=buf[*r]; *r=(*r+1)%PTY_CAP; (*count)--;
	}
	return (int)done;
}
#define PIPE_CAP 65536
#define SOCK_CAP 16384
#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0x80000
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0x800
#endif
#ifndef SOL_SOCKET
#define SOL_SOCKET 1
#endif
#ifndef SO_ERROR
#define SO_ERROR 4
#endif
#ifndef SO_SNDBUF
#define SO_SNDBUF 7
#endif
#ifndef SO_RCVBUF
#define SO_RCVBUF 8
#endif
#ifndef SO_TYPE
#define SO_TYPE 3
#endif
#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 9
#endif
#ifndef SO_REUSEADDR
#define SO_REUSEADDR 2
#endif
#ifndef FIONREAD
#define FIONREAD 0x541B
#endif
#ifndef FIONBIO
#define FIONBIO 0x5421
#endif
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif
struct unix_socket_state {
	uint8_t data[2][SOCK_CAP];
	uint32_t rpos[2], wpos[2], count[2];
	uint8_t closed[2];
	uint32_t refs;
	uint32_t side_refs[2];
	uint8_t listener;
	uint8_t bound;
	uint8_t connected;
	uint8_t pending_count;
	uint8_t domain;
	uint8_t protocol;
	int pending_fd[4];
	char path[108];
	uint32_t netlink_pid;
	uint32_t netlink_groups;
};

#define UNIX_SOCKETS_MAX 32
static struct unix_socket_state unix_sockets[UNIX_SOCKETS_MAX];
struct pipe_state {
	uint8_t data[PIPE_CAP];
	uint32_t rpos, wpos, count;
	uint32_t readers, writers;
	uint8_t debug_write_logged, debug_read_logged;
	uint8_t debug_write_enter_logged, debug_writev_enter_logged, debug_read_enter_logged;
	uint8_t debug_readv_block_logged;
};
static struct pipe_state pipes[8];
static uint8_t fd0_read_diag_logged[MAX_TASKS];
struct eventfd_state_k { uint64_t value; uint32_t flags; };
struct timerfd_state_k { int clockid; uint32_t flags; uint64_t interval_ns; uint64_t next_ns; uint64_t expirations; };
static struct eventfd_state_k eventfds[8];
static struct timerfd_state_k timerfds[8];

int current_task = 0;

static struct file_desc task_fd_tables[MAX_FD_TABLES][MAX_FILES];
static int16_t task_fd_table_id[MAX_TASKS];
static uint16_t task_fd_table_refs[MAX_FD_TABLES];
static struct file_desc *task_files_for_task(int ti) {
    if (ti < 0 || ti >= MAX_TASKS) return NULL;
    int id = task_fd_table_id[ti];
    if (id < 0 || id >= MAX_FD_TABLES) return NULL;
    return task_fd_tables[id];
}
static struct file_desc *current_files(void) {
    struct file_desc *table = task_files_for_task(current_task);

    return table ? table : task_fd_tables[0];
}
#define files (current_files())
static int fd_is_pty(int fd) {
	return fd>=0 && fd<MAX_FILES && files[fd].in_use &&
		   (files[fd].type==FD_PTY_MASTER || files[fd].type==FD_PTY_SLAVE);
}

static int fd_is_console_stream(int fd) {
	if(fd<0 || fd>=MAX_FILES || !files[fd].in_use || files[fd].type!=FD_PSEUDO) return 0;
	uintptr_t dev=(uintptr_t)files[fd].private_data;
	return dev==(uintptr_t)DEV_CONSOLE || dev==(uintptr_t)DEV_TTY;
}
static pty_state_k *pty_fd_state(struct file_desc *f) {
	return (f && fd_is_pty((int)(f-files))) ? (pty_state_k*)f->private_data : NULL;
}
static uint8_t __attribute__((section(".data"))) test_file_data[] = "Hello from test file!\n";
static uint8_t pseudo_buf[MAX_FILES][4096];
static char current_exe[128] = "/HELLO.ELF";
extern uint64_t pmm_free_count(void);
extern uint64_t pmm_total_count(void);
extern uint64_t bootfs_file_size(const char *path);
extern int bootfs_path_is_dir(const char *path);
extern size_t bootfs_read_root_file(const char *path, uint64_t offset, uint8_t *dst, size_t len);

#define KYRONIX_PIE_BASE   0x0000000000400000ULL
#define KYRONIX_INTERP_BASE 0x00007f0000000000ULL
#define ELF_INTERP_PATH_MAX 256

static uint32_t pseudo_rand_state = 0x13579bdfU;
extern int64_t sys_getpid(void);
extern int64_t sys_getppid(void);
static void rtc_now(uint64_t *sec, uint64_t *nsec);
static int pseudo_eq(const char *a, const char *b) { while (*a && *b && *a==*b) { ++a; ++b; } return *a==*b; }
static char path_fold_ascii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c; }
static int pseudo_eq_ci(const char *a, const char *b) { while (*a && *b && path_fold_ascii(*a)==path_fold_ascii(*b)) { ++a; ++b; } return *a==*b; }
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

#define YABRO_PIT_TICK_NS 10000000ULL                           
static volatile uint64_t timer_ticks_global = 0;
static volatile uint64_t timer_tick_tsc = 0;
static uint64_t monotonic_boot_tsc = 0;
static uint64_t monotonic_tsc_hz = 1000000000ULL;
static int monotonic_ready = 0;

static uint64_t read_tsc(void) {
	uint32_t lo, hi;
	__asm__ volatile("lfence\nrdtsc\nlfence" : "=a"(lo), "=d"(hi) :: "memory");
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
	return 1000000000ULL;
}

static void monotonic_init(void) {
	monotonic_tsc_hz=cpuid_tsc_hz();
	monotonic_boot_tsc=read_tsc();
	monotonic_ready=1;
}

uint64_t kernel_monotonic_ns(void) {
	if (!monotonic_ready) monotonic_init();

	uint64_t ticks, base_tsc;

	do {
		ticks = timer_ticks_global;
		base_tsc = ticks ? timer_tick_tsc : monotonic_boot_tsc;
	} while (ticks != timer_ticks_global);
	if (ticks > UINT64_MAX / YABRO_PIT_TICK_NS) return UINT64_MAX;
	uint64_t base_ns = ticks * YABRO_PIT_TICK_NS;

	uint64_t now_tsc = read_tsc();
	uint64_t fraction_ns = 0;
	if (base_tsc && now_tsc >= base_tsc) {
		uint64_t delta = now_tsc - base_tsc;
		__uint128_t estimate = ((__uint128_t)delta * 1000000000ULL) /
						(monotonic_tsc_hz ? monotonic_tsc_hz : 1000000000ULL);
		fraction_ns = estimate >= YABRO_PIT_TICK_NS ?
			YABRO_PIT_TICK_NS - 1 : (uint64_t)estimate;
	}
	return UINT64_MAX - base_ns < fraction_ns ? UINT64_MAX : base_ns + fraction_ns;
}

static void cpu_brand_string(char *out, size_t cap) {
	if (!out || cap == 0) return;
	for (size_t i=0;i<cap;i++) out[i]=0;

	uint32_t max_extended=0, b=0, c=0, d=0;
	__asm__ volatile("cpuid" : "=a"(max_extended), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000U), "c"(0));
	if (max_extended < 0x80000004U) {
		const char *fallback="Unknown x86 CPU"; size_t i=0; while(fallback[i]&&i+1<cap){out[i]=fallback[i];i++;} out[i]=0; return;
	}
	uint32_t regs[12];
	for(uint32_t leaf=0;leaf<3;leaf++){ uint32_t eax,ebx,ecx,edx; uint32_t fn=0x80000002U+leaf; __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(fn)); regs[leaf*4+0]=eax; regs[leaf*4+1]=ebx; regs[leaf*4+2]=ecx; regs[leaf*4+3]=edx; }
	const char *src=(const char*)regs; size_t len=48; if(len>=cap)len=cap-1;
	for(size_t i=0;i<len;i++) out[i]=src[i];
	out[len]=0;
	while(len && out[len-1]==' ') out[--len]=0;
}

static void cpu_vendor_string(char *out, size_t cap) {
	if (!out || cap == 0) return;
	uint32_t eax,ebx,ecx,edx;
	__asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0), "c"(0));
	uint32_t regs[3]={ebx,edx,ecx}; const char *src=(const char*)regs; size_t i=0;
	while(src[i] && i+1<cap && i<12){out[i]=src[i];i++;} out[i]=0;
}

static void cpu_cpuid_leaf1(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
	uint32_t a,b,c,d;
	__asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
	if (eax) *eax=a; if (ebx) *ebx=b; if (ecx) *ecx=c; if (edx) *edx=d;
}
static uint32_t cpu_max_basic_leaf(void) {
	uint32_t eax,ebx,ecx,edx;
	__asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0), "c"(0));
	return eax;
}
static size_t append_flag(char *dst, size_t cap, size_t n, const char *name) {
	if (!name || !*name) return n;
	n=pseudo_append(dst,cap,n,name);
	return pseudo_append(dst,cap,n," ");
}
static size_t cpu_append_flags(char *dst, size_t cap, size_t n) {
	uint32_t maxleaf=cpu_max_basic_leaf(), eax,ebx,ecx,edx;
	if (maxleaf < 1) return n;
	cpu_cpuid_leaf1(&eax,&ebx,&ecx,&edx);
	if (edx&(1u<<4))  n=append_flag(dst,cap,n,"tsc");
	if (edx&(1u<<23)) n=append_flag(dst,cap,n,"mmx");
	if (edx&(1u<<25)) n=append_flag(dst,cap,n,"sse");
	if (edx&(1u<<26)) n=append_flag(dst,cap,n,"sse2");
	if (ecx&(1u<<0))  n=append_flag(dst,cap,n,"sse3");
	if (ecx&(1u<<9))  n=append_flag(dst,cap,n,"ssse3");
	if (ecx&(1u<<19)) n=append_flag(dst,cap,n,"sse4_1");
	if (ecx&(1u<<20)) n=append_flag(dst,cap,n,"sse4_2");
	if (ecx&(1u<<28)) n=append_flag(dst,cap,n,"avx");
	if (ecx&(1u<<30)) n=append_flag(dst,cap,n,"rdrand");
	n=pseudo_append(dst,cap,n,"");
	return n;
}
uint64_t kernel_uptime_ns(void);

static size_t pseudo_make_proc(char *dst, size_t cap, const char *path) {
	size_t n=0; if (!cap) return 0; dst[0]=0;
	if (pseudo_eq(path,"/proc/cpuinfo")) {
		char brand[64],vendor[32]; uint32_t eax=0,ebx=0,ecx=0,edx=0;
		cpu_brand_string(brand,sizeof(brand)); cpu_vendor_string(vendor,sizeof(vendor)); cpu_cpuid_leaf1(&eax,&ebx,&ecx,&edx);
		uint32_t base_family=(eax>>8)&0xf, ext_family=(eax>>20)&0xff; uint32_t family=base_family; if (base_family==0xf) family+=ext_family;
		uint32_t model=((eax>>4)&0xf) | (((eax>>16)&0xf)<<4);
		uint32_t stepping=eax&0xf;
		uint32_t logical=(ebx>>16)&0xff; if (!logical) logical=1;
		n=pseudo_append(dst,cap,n,"processor\t: "); n=pseudo_append_u64(dst,cap,n,0);
		n=pseudo_append(dst,cap,n,"\nvendor_id\t: "); n=pseudo_append(dst,cap,n,vendor);
		n=pseudo_append(dst,cap,n,"\ncpu family\t: "); n=pseudo_append_u64(dst,cap,n,family);
		n=pseudo_append(dst,cap,n,"\nmodel\t\t: "); n=pseudo_append_u64(dst,cap,n,model);
		n=pseudo_append(dst,cap,n,"\nmodel name\t: "); n=pseudo_append(dst,cap,n,brand);
		n=pseudo_append(dst,cap,n,"\nstepping\t: "); n=pseudo_append_u64(dst,cap,n,stepping);
		n=pseudo_append(dst,cap,n,"\ncpu count\t: "); n=pseudo_append_u64(dst,cap,n,logical);
		n=pseudo_append(dst,cap,n,"\nflags\t\t: "); n=cpu_append_flags(dst,cap,n);
		n=pseudo_append(dst,cap,n,"\n\n");
	} else if (pseudo_eq(path,"/proc/meminfo")) {
		uint64_t freep=pmm_free_count(), totalp=pmm_total_count(), total=totalp*4096ULL, freeb=freep*4096ULL;
		n=pseudo_append(dst,cap,n,"MemTotal:       "); n=pseudo_append_u64(dst,cap,n,total/1024); n=pseudo_append(dst,cap,n," kB\nMemFree:        "); n=pseudo_append_u64(dst,cap,n,freeb/1024); n=pseudo_append(dst,cap,n," kB\nMemAvailable:   "); n=pseudo_append_u64(dst,cap,n,freeb/1024); n=pseudo_append(dst,cap,n," kB\nBuffers:        0 kB\nCached:         0 kB\n");
	} else if (pseudo_eq(path,"/proc/uptime")) {
		uint64_t up_ns=kernel_uptime_ns();
		uint64_t up_sec=up_ns/1000000000ULL, up_frac=(up_ns%1000000000ULL)/10000000ULL;
		n=pseudo_append_u64(dst,cap,n,up_sec);
		if(n+3<cap){dst[n++]='.';dst[n++]=(char)('0'+up_frac/10);dst[n++]=(char)('0'+up_frac%10);}
		n=pseudo_append(dst,cap,n," 0.00\n");
	} else if (pseudo_eq(path,"/proc/version")) {
		n=pseudo_append(dst,cap,n,"YabroOS-32 version 0.1 (x86_64) #1 PREEMPT\n");
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
	if (pseudo_eq(p,"/dev/ptmx")) { *type=FD_PTY_MASTER; *kind=PSEUDO_PTMX; return 1; }
	if (pseudo_eq(p,"/dev/pts")) { *type=FD_PSEUDO; *kind=PSEUDO_PTS_DIR; return 1; }
	if (pty_index_from_path(p)>=0) { *type=FD_PTY_SLAVE; *kind=PSEUDO_PTS_SLAVE; return 1; }
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
		case VFS_NODE_DEV_FB: *kind=DEV_FB; return 1;
		case VFS_NODE_DEV_INPUT: *kind=DEV_INPUT; return 1;
		case VFS_NODE_DEV_INPUT_MOUSE: *kind=DEV_INPUT_MOUSE; return 1;
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
static int pseudo_is_dir_kind(int k) { return k==PSEUDO_PROC_ROOT || k==PSEUDO_PROC_SELF || k==PSEUDO_PROC_FD || k==PSEUDO_DEV_DIR || k==PSEUDO_SYS_DIR || k==PSEUDO_PTS_DIR; }
static int pseudo_fill(struct file_desc *f, const char *path, int kind) {
	f->type=FD_PSEUDO; f->private_data=(void*)(uintptr_t)kind;
	f->vnode=NULL; f->offset=0; f->size=0; f->data=NULL;

	if (kind != DEV_FB && kind != DEV_INPUT && kind != DEV_INPUT_MOUSE &&
	    kind != DEV_NULL && kind != DEV_ZERO && kind != DEV_CONSOLE &&
	    kind != DEV_TTY && kind != DEV_RANDOM && kind != DEV_URANDOM &&
	    kind != PSEUDO_PTMX && kind != PSEUDO_PTS_SLAVE) {
		f->vnode=vfs_lookup(path);
	}
	if (kind==PSEUDO_PTMX) {
		pty_init_once();
		pty_state_k *p=NULL;
		for (int i=0; i<PTY_COUNT; ++i) {
			pty_state_k *candidate=&pty_slots[i];
			if (!candidate->master_open && !candidate->slave_open &&
			    candidate->master_refs==0 && candidate->slave_refs==0) {
				p=candidate; break;
			}
		}
		if (!p) {
			if (!pty_dbg_master_open_busy) { pty_dbg("[PTYDBG MASTER_OPEN_NO_SLOT]", (int)(f-files), f); pty_dbg_master_open_busy=1; }
			return -EMFILE;
		}
		p->master_open=1; p->slave_open=0; p->unlocked=0;
		p->master_refs=1; p->slave_refs=0;
		p->m2s_r=p->m2s_w=p->m2s_count=0;
		p->s2m_r=p->s2m_w=p->s2m_count=0;
		p->canon_count=0; p->output_stopped=0;
		p->fg_pgrp=0; p->shell_pgrp=0; p->session=0;
		p->rows=30; p->cols=80; p->xpixel=640; p->ypixel=480;
		pty_reset_termios(p);
		f->type=FD_PTY_MASTER; f->private_data=p; f->vnode=NULL;
        if (!pty_dbg_master_open) { pty_dbg("[PTYDBG MASTER_OPEN]", (int)(f-files), f); pty_dbg_master_open=1; }
		return 0;
	}
	if (kind==PSEUDO_PTS_SLAVE) {
		pty_init_once();
		int pty_index=pty_index_from_path(path);
		if (pty_index<0 || pty_index>=PTY_COUNT) return -ENXIO;
		pty_state_k *p=&pty_slots[pty_index];
        if (!p->master_open) {
            if (!pty_dbg_slave_reject) { pty_dbg("[PTYDBG SLAVE_REJECT_NO_MASTER]", (int)(f-files), f); pty_dbg_slave_reject=1; }
            return -ENXIO;
        }
        if (!p->unlocked) {
            if (!pty_dbg_slave_reject) { pty_dbg("[PTYDBG SLAVE_REJECT_LOCKED]", (int)(f-files), f); pty_dbg_slave_reject=1; }
            return -EACCES;
        }

		if (p->slave_refs == UINT32_MAX) return -EMFILE;
		p->slave_open=1;
		p->slave_refs++;
		f->type=FD_PTY_SLAVE; f->private_data=p; f->vnode=NULL;
        if (!pty_dbg_slave_open) { pty_dbg("[PTYDBG SLAVE_OPEN]", (int)(f-files), f); pty_dbg_slave_open=1; }
		return 0;
	}
	if (kind==PSEUDO_PROC_FILE) { f->data=pseudo_buf[(f-files)%MAX_FILES]; f->size=pseudo_make_proc((char*)f->data,4096,path); }
	else if (kind==PSEUDO_SYS_FILE) { static const uint8_t s[]="0.0.1-alpha-yabroos-32\n"; f->data=(uint8_t*)s; f->size=sizeof(s)-1; }
	else if (kind==DEV_FB) { f->size=graphics_fb_size(); }
	else if (kind==DEV_INPUT || kind==DEV_INPUT_MOUSE) {
		f->size=0;
		uint64_t initial_seq=(kind==DEV_INPUT_MOUSE)?mouse_event_seq:keyboard_event_seq;
		f->input_state=input_state_alloc((uint32_t)kind,initial_seq);
		if(!f->input_state) return -EMFILE;
		f->input_seq=initial_seq;
		if(kind==DEV_INPUT_MOUSE) {
			if(!evdev_mouse_open_logged) { debugcon_puts("[EVDEV OPEN FD] /dev/input/event1\n"); evdev_mouse_open_logged=1; }
		} else if(!evdev_keyboard_open_logged) {
			debugcon_puts("[EVDEV OPEN FD] /dev/input/event0\n");
			evdev_keyboard_open_logged=1;
		}
	}
	return 0;
}

void init_files(void) {
	monotonic_init();
	vfs_init();
	vfs_debug_dump();
	for (int ti = 0; ti < MAX_TASKS; ti++) task_fd_table_id[ti] = -1;
	for (int ti = 0; ti < MAX_FD_TABLES; ti++) {
        task_fd_table_refs[ti] = 0;
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
			task_fd_tables[ti][i].input_state = NULL;
			task_fd_tables[ti][i].input_seq = 0;
			task_fd_tables[ti][i].boot_path[0] = 0;
		}
	}

    task_fd_table_id[0] = 0;
    task_fd_table_refs[0] = 1;
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
	for (int i = 0; i < MAX_FILES; i++) {
		if (!files[i].in_use) return i;
	}
	return -1;
}

#define USER_LIMIT 0x0000800000000000ULL

#define USER_MMAP_MIN 0x0000000010000000ULL

static int user_mmap_range_ok(uint64_t addr, uint64_t len) {
	if (!len || addr < USER_MMAP_MIN || addr >= USER_LIMIT) return 0;
	return len <= USER_LIMIT - addr;
}

static int mmap_watch_range(uint64_t addr, uint64_t len) {
	const uint64_t lo = 0x00007f0000000000ULL;
	const uint64_t hi = 0x0000800000000000ULL;
	if (!len || addr >= hi) return 0;
	if (addr + len < addr) return 0;
	return addr + len > lo;
}

static void mmap_watch_log(const char *tag, uint64_t addr, uint64_t len, uint64_t prot, uint64_t flags, int fd, uint64_t off) {
	if (!mmap_watch_range(addr, len)) return;
	debugcon_puts(tag);
	debug_hex64(" pid=0x", (uint64_t)current_task);
	debug_hex64(" addr=0x", addr);
	debug_hex64(" len=0x", len);
	debug_hex64(" prot=0x", prot);
	debug_hex64(" flags=0x", flags);
	debug_hex64(" fd=0x", (uint64_t)(int64_t)fd);
	debug_hex64(" off=0x", off);
	debugcon_puts("\n");
}

static int user_any_mapping_range_ok(uint64_t addr, uint64_t len) {
	if (!len || addr >= USER_LIMIT) return 0;
	return len <= USER_LIMIT - addr;
}

extern int vmm_user_translate(uint64_t virt, uint64_t *phys_out, uint64_t *pte_out);

static int user_mmap_range_free(uint64_t addr, uint64_t len) {
	if (!user_mmap_range_ok(addr, len)) return 0;
	uint64_t first = addr & PAGE_MASK;
	uint64_t last = (addr + len - 1) & PAGE_MASK;
	for (uint64_t va = first;; va += PAGE_SIZE) {
		uint64_t phys = 0;
		if (vmm_user_translate(va, &phys, NULL) == 0) return 0;
		if (va == last) break;
		if (va > USER_LIMIT - PAGE_SIZE) return 0;
	}
	return 1;
}

static int user_mmap_find_gap(uint64_t start, uint64_t size, uint64_t heap_end, uint64_t *base_out) {
	if (!base_out || !size) return 0;
	uint64_t base = (start + PAGE_SIZE - 1) & PAGE_MASK;
	for (;;) {
		if (base < USER_MMAP_MIN) base = USER_MMAP_MIN;
		if (!user_mmap_range_ok(base, size)) return 0;

		uint64_t hb = 0x0000000000500000ULL;
		if (heap_end > hb) {
			uint64_t hp = (heap_end + PAGE_SIZE - 1) & PAGE_MASK;
			if (base < hp && base + size > hb) {
				base = hp;
				continue;
			}
		}

		if (user_mmap_range_free(base, size)) {
			*base_out = base;
			return 1;
		}

		uint64_t probe = base;
		uint64_t last = (base + size - 1) & PAGE_MASK;
		for (;;) {
			uint64_t phys = 0;
			if (vmm_user_translate(probe, &phys, NULL) == 0) break;
			if (probe == last) break;
			probe += PAGE_SIZE;
		}
		if (probe > USER_LIMIT - PAGE_SIZE) return 0;
		base = probe + PAGE_SIZE;
	}
}

#define USER_HEAP_BASE 0x0000000000500000ULL
#define USER_HEAP_MAX  0x000000000F000000ULL
#define E2BIG 7

static uint64_t heap_end = USER_HEAP_BASE;
static uint64_t heap_base = USER_HEAP_BASE;
static uint64_t fs_base, gs_base;
static int user_addr_range_ok(uint64_t addr, uint64_t len) {
	if (!len || addr >= USER_LIMIT) return 0;
	if (len > USER_LIMIT - addr) return 0;
	return 1;
}

static int user_prot_wx(uint64_t prot) {
	return (prot & 2ULL) && (prot & 4ULL);
}

#define USER_STACK_TOP 0x00007FFFFFFFF000ULL

#define USER_STACK_SIZE 0x00040000ULL

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

__attribute__((noreturn)) void bsod_reboot(void) {
	system_reboot_hw();
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
extern int vmm_user_space_destroy(uint64_t cr3);
extern int vmm_user_activate(uint64_t cr3);
extern uint64_t vmm_user_cr3(void);
extern int vmm_user_map(uint64_t virt, uint64_t phys, uint64_t flags);
extern int vmm_user_translate(uint64_t virt, uint64_t *phys_out, uint64_t *pte_out);
extern int vmm_user_translate_cr3(uint64_t cr3, uint64_t virt, uint64_t *phys_out, uint64_t *pte_out);
extern int vmm_user_validate_page_cr3(uint64_t cr3, uint64_t virt, int require_write, int require_exec);
extern int vmm_user_range_ok(uint64_t virt, uint64_t len, int write);
extern int vmm_user_unmap(uint64_t virt, uint64_t *phys_out);
extern int vmm_user_protect(uint64_t virt, uint64_t flags);
extern void vmm_user_flush_tlb(void);

static uint64_t user_lookup_pte(uint64_t cr3, uint64_t va) {
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

	uint64_t active_cr3 = get_cr3();
	if (!kernel_cr3) kernel_cr3 = active_cr3;
	user_cr3 = vmm_user_cr3();
	debug_hex64("[VMM] Active CR3=0x", active_cr3);
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

		uint64_t existing_phys = 0, existing_pte = 0;
		if (vmm_user_translate(va, &existing_phys, &existing_pte) == 0 &&
		    (existing_pte & PTE_P)) {
			uint64_t merged = 0;
			if ((existing_pte & PTE_W) || (p_flags & 2)) merged |= PTE_W;

			if ((existing_pte & PTE_NX) && !(p_flags & 1)) merged |= PTE_NX;
			if (vmm_user_protect(va, merged) != 0) {
				debugcon_puts("[ELF] cannot merge PT_LOAD page permissions\n");
				return -1;
			}
			continue;
		}
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

int graphics_copy_to_user(uint64_t dst_va, const void *src, uint64_t len) {
	if (syscall_user_cr3 == 0) return -14;
	return copy_to_user(syscall_user_cr3, dst_va, (const uint8_t *)src, len);
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

#define KERNEL_STACK_SIZE (128 * 1024)

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

static const char *syscall_trace_name(uint64_t nr) {
	switch (nr) {
	case 0: return "read"; case 1: return "write"; case 2: return "open";
	case 3: return "close"; case 4: return "stat"; case 5: return "fstat";
	case 7: return "poll"; case 270: return "pselect6"; case 271: return "ppoll"; case 9: return "mmap"; case 10: return "mprotect";
	case 11: return "munmap"; case 12: return "brk"; case 13: return "rt_sigaction"; case 28: return "madvise";
	case 14: return "rt_sigprocmask"; case 16: return "ioctl"; case 17: return "pread64";
	case 19: return "readv"; case 20: return "writev"; case 21: return "access";
	case 22: return "pipe"; case 23: return "select"; case 24: return "sched_yield";
	case 32: return "dup"; case 33: return "dup2"; case 39: return "getpid";
	case 41: return "socket"; case 42: return "connect"; case 43: return "accept";
	case 44: return "sendto"; case 45: return "recvfrom"; case 46: return "sendmsg"; case 47: return "recvmsg"; case 48: return "shutdown";
	case 49: return "bind"; case 50: return "listen"; case 51: return "getsockname"; case 53: return "socketpair";
	case 54: return "setsockopt"; case 55: return "getsockopt"; case 59: return "execve"; case 60: return "exit";
	case 168: return "cls"; case 169: return "shutdown"; case 63: return "uname"; case 72: return "fcntl"; case 79: return "getcwd";
	case 80: return "chdir"; case 82: return "rename"; case 83: return "mkdir";
	case 84: return "rmdir"; case 86: return "link"; case 87: return "unlink";
	case 89: return "readlink"; case 96: return "gettimeofday"; case 109: return "setpgid";
	case 112: return "setsid"; case 121: return "getpgid"; case 122: return "getpgrp"; case 131: return "sigaltstack";
	case 217: return "getdents64"; case 292: return "dup3"; default: return "unknown";
	}
}

static int syscall_trace_interesting(uint64_t nr) {
	switch (nr) {
	case 2: case 3: case 4: case 5: case 7: case 9: case 10: case 11:
	case 12: case 13: case 14: case 16: case 21: case 22: case 23: case 28:
	case 32: case 270: case 271: case 33: case 41: case 42: case 43: case 44: case 45:
	case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55: case 60: case 63:
	case 168: case 169: case 72: case 79: case 80: case 82: case 83: case 84: case 86: case 87:
	case 89: case 96: case 109: case 112: case 121: case 122: case 131: case 217: case 292:
		return 1;
	default: return 0;
	}
}

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
typedef struct { sighandler_t sa_handler; unsigned long sa_flags; void (*sa_restorer)(void); uint64_t sa_mask; } sigaction_t;

#define MAX_LAZY_USER_MMAPS 128
typedef struct {
	uint64_t start, end, prot;
	int used;
} lazy_user_mmap_t;

typedef struct {
	int used;
	int state;
	int pid, ppid, tgid;
	int pgrp, session, controlling_tty;
	int exit_status;
	uint64_t tid_address;
	uint64_t futex_addr;
	uint64_t futex_cr3;
	uint64_t futex_deadline_ns;
	int pipe_wait_fd;
	uint64_t pipe_wait_buf;
	uint64_t pipe_wait_count;
	int io_wait_kind;
	int io_wait_valid;
	uint64_t io_wait_nr;
	uint64_t io_wait_deadline_ns;
	int io_resume_valid;
	uint64_t io_resume_nr;
	uint64_t io_resume_rip;
	uint64_t io_resume_start_ns;
	uint64_t io_resume_deadline_ns;

	uint64_t heap_base;
	uint64_t heap_end;

	uint64_t mmap_next;

	lazy_user_mmap_t lazy_mmaps[MAX_LAZY_USER_MMAPS];

	int fb_map_count;

	int nice;
	uint64_t switches;
	uint64_t cpu_ticks;

	int wait4_active;
	int wait4_pid;
	uint64_t wait4_status_ptr;
	int wait4_reap_pid;
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

enum { LAZY_MMAP_REMOVE = 0, LAZY_MMAP_SET_PROT = 1, LAZY_MMAP_REPLACE = 2 };

static int task_lazy_mmap_transform(task_t *t, uint64_t start, uint64_t end,
                                    int operation, uint64_t prot) {
	if (!t || start >= end) return -22;
	lazy_user_mmap_t out[MAX_LAZY_USER_MMAPS];
	int count=0;
	for (int i=0; i<MAX_LAZY_USER_MMAPS; ++i) {
		lazy_user_mmap_t m=t->lazy_mmaps[i];
		if (!m.used || m.start >= m.end) continue;
		if (m.end <= start || m.start >= end) {
			if (count >= MAX_LAZY_USER_MMAPS) return -12;
			out[count++]=m;
			continue;
		}
		if (m.start < start) {
			if (count >= MAX_LAZY_USER_MMAPS) return -12;
			out[count++]=(lazy_user_mmap_t){m.start,start,m.prot,1};
		}
		uint64_t os=m.start>start?m.start:start;
		uint64_t oe=m.end<end?m.end:end;
		if (operation == LAZY_MMAP_SET_PROT && os < oe) {
			if (count >= MAX_LAZY_USER_MMAPS) return -12;
			out[count++]=(lazy_user_mmap_t){os,oe,prot,1};
		}
		if (m.end > end) {
			if (count >= MAX_LAZY_USER_MMAPS) return -12;
			out[count++]=(lazy_user_mmap_t){end,m.end,m.prot,1};
		}
	}
	if (operation == LAZY_MMAP_REPLACE) {
		if (count >= MAX_LAZY_USER_MMAPS) return -12;
		out[count++]=(lazy_user_mmap_t){start,end,prot,1};
	}
	for (int i=0; i<MAX_LAZY_USER_MMAPS; ++i) {
		t->lazy_mmaps[i] = (i<count) ? out[i] : (lazy_user_mmap_t){0,0,0,0};
	}
	return 0;
}

static int task_lazy_mmap_transform_shared(task_t *t, uint64_t start, uint64_t end,
                                           int operation, uint64_t prot) {
	if (!t) return -22;
	uint64_t cr3=t->ctx.cr3 & PAGE_MASK;
	for (int i=0; i<MAX_TASKS; ++i) {
		if (!tasks[i].used) continue;
		if (&tasks[i] != t && (!cr3 || (tasks[i].ctx.cr3 & PAGE_MASK) != cr3)) continue;
		int rc=task_lazy_mmap_transform(&tasks[i],start,end,operation,prot);
		if (rc<0) return rc;
	}
	return 0;
}

static int task_lazy_mmap_protection(task_t *t, uint64_t addr, uint64_t *prot_out) {
	if (!t) return 0;
	for (int i=0; i<MAX_LAZY_USER_MMAPS; ++i) {
		lazy_user_mmap_t *m=&t->lazy_mmaps[i];
		if (m->used && addr >= m->start && addr < m->end) {
			if (prot_out) *prot_out=m->prot;
			return 1;
		}
	}
	return 0;
}

static int task_lazy_mmap_range_covered(task_t *t, uint64_t start, uint64_t end) {
	if (!t || start >= end) return 0;
	uint64_t cursor = start;
	while (cursor < end) {
		uint64_t next = cursor;
		for (int i=0; i<MAX_LAZY_USER_MMAPS; ++i) {
			lazy_user_mmap_t *m = &t->lazy_mmaps[i];
			if (m->used && m->start <= cursor && m->end > next)
				next = m->end < end ? m->end : end;
		}
		if (next == cursor) return 0;
		cursor = next;
	}
	return 1;
}

static int task_lazy_page_fault(uint64_t cr3, uint64_t addr, uint64_t error_code) {
	if (current_task < 0 || current_task >= MAX_TASKS) return 0;
	task_t *t=&tasks[current_task];
	if (!t->used || !t->ctx.cr3 || (t->ctx.cr3 & PAGE_MASK) != (cr3 & PAGE_MASK)) return 0;

	if (vmm_user_activate(cr3) != 0) return 0;
	uint64_t page=addr & PAGE_MASK, prot=0;
	if (!task_lazy_mmap_protection(t,addr,&prot) || prot==0) return 0;
	if ((error_code & 0x2) && !(prot & 0x2)) return 0;            
	if ((error_code & 0x10) && !(prot & 0x4)) return 0;              
	if (!(error_code & (0x2|0x10)) && !(prot & (0x1|0x2))) return 0;           
	uint64_t existing=0;
	if (vmm_user_translate_cr3(cr3,page,&existing,NULL)==0) return 0;
	uint64_t phys=pmm_alloc_page();
	if (!phys) return 0;
	uint8_t *bytes=(uint8_t*)pmm_phys_to_virt(phys);
	if (!bytes) { pmm_free_page(phys); return 0; }
	for (size_t i=0; i<PAGE_SIZE; ++i) bytes[i]=0;
	uint64_t flags=((prot & 0x2)?PTE_W:0) | ((prot & 0x4)?0:PTE_NX);
	if (vmm_user_map(page,phys,flags)!=0) { pmm_free_page(phys); return 0; }
	vmm_user_flush_tlb();
	return 1;
}

static void fd_trace_slot(const char *tag, int fd, const struct file_desc *d) {
	debugcon_puts(tag);
	debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid);
	debug_hex64(" table=0x", (uint64_t)(uint16_t)task_fd_table_id[current_task]);
	debug_hex64(" fd=0x", (uint64_t)(int64_t)fd);
	debug_hex64(" in_use=0x", d ? (uint64_t)d->in_use : 0);
	debug_hex64(" type=0x", d ? (uint64_t)d->type : 0);
	debug_hex64(" kind=0x", (d && d->type==FD_PSEUDO) ? (uint64_t)(uintptr_t)d->private_data : 0);
	debug_hex64(" private=0x", d ? (uint64_t)(uintptr_t)d->private_data : 0);
	debugcon_puts("\n");
}
static int next_pid = 100;
volatile int task_switch_pending = 0;
volatile int task_switch_deferred = 0;
static volatile int task_switch_target = 0;
task_context_t *task_return_ctx = NULL;

static int user_copy_from_cr3(uint64_t cr3, uint8_t *dst, uint64_t src, uint64_t len);

void syscall_task_switch_audit(const task_context_t *ctx, uint64_t actual_cr3) {
	int target = -1;
	if (ctx) {
		for (int i = 0; i < MAX_TASKS; ++i) {
			if (&tasks[i].ctx == ctx) { target = i; break; }
		}
	}
	#if YABRO_DEBUG_SCHED
	if (target >= 0 && target < MAX_TASKS && tasks[target].used) {
		if (tasks[target].pid == 0x64 || tasks[target].pid == 0x65 || tasks[target].pid == 0x68) {
			debug_hex64("[TASK-IRET] target_pid=0x", (uint64_t)tasks[target].pid);
			debug_hex64(" state=0x", (uint64_t)tasks[target].state);
			debug_hex64(" current_pid=0x", (uint64_t)tasks[current_task].pid);
			debug_hex64(" ctx=0x", (uint64_t)ctx);
			debug_hex64(" rip=0x", ctx->rip);
			debug_hex64(" rsp=0x", ctx->rsp);
			debug_hex64(" rflags=0x", ctx->rflags);
			debug_hex64(" cr3=0x", ctx->cr3);
			debug_hex64(" rax=0x", ctx->rax);
			debug_hex64(" rbx=0x", ctx->rbx);
			debug_hex64(" rcx=0x", ctx->rcx);
			debug_hex64(" rdx=0x", ctx->rdx);
			debug_hex64(" rsi=0x", ctx->rsi);
			debug_hex64(" rdi=0x", ctx->rdi);
			debug_hex64(" rbp=0x", ctx->rbp);
			debug_hex64(" r8=0x", ctx->r8);
			debug_hex64(" r9=0x", ctx->r9);
			debug_hex64(" r10=0x", ctx->r10);
			debug_hex64(" r11=0x", ctx->r11);
			debug_hex64(" r12=0x", ctx->r12);
			debug_hex64(" r13=0x", ctx->r13);
			debug_hex64(" r14=0x", ctx->r14);
			debug_hex64(" r15=0x", ctx->r15);
			debug_hex64(" fs=0x", ctx->fs_base);
			debug_hex64(" gs=0x", ctx->gs_base);
			debugcon_puts("\n");
			uint64_t words[8] = {0};
			uint64_t dump_sp = ctx->rsp >= 0x20 ? ctx->rsp - 0x20 : ctx->rsp;
			if (user_copy_from_cr3(ctx->cr3, (uint8_t*)words, dump_sp, sizeof(words)) == 0) {
				debug_hex64("[TASK-STACK] sp=0x", dump_sp);
				for (int i = 0; i < 8; ++i) {
					debug_hex64(" w=0x", words[i]);
				}
				debugcon_puts("\n");
			} else {
				debugcon_puts("[TASK-STACK] copy-failed\n");
			}
		}
	}
	#endif

#if defined(YABRO_DEBUG_CR3) && YABRO_DEBUG_CR3
	debug_hex64("[CR3-TASK] ctx=0x", (uint64_t)ctx);
	debug_hex64("[CR3-TASK] actual=0x", actual_cr3);
	debug_hex64("[CR3-TASK] ctx_cr3=0x", ctx ? ctx->cr3 : 0);
	debug_hex64("[CR3-TASK] ctx_rip=0x", ctx ? ctx->rip : 0);
	debug_hex64("[CR3-TASK] ctx_rsp=0x", ctx ? ctx->rsp : 0);
	debug_hex64("[CR3-TASK] current_task=0x", (uint64_t)(uint32_t)current_task);
	debug_hex64("[CR3-TASK] target=0x", (uint64_t)(uint32_t)task_switch_target);
#else
	(void)ctx;
	(void)actual_cr3;
#endif
}

static uint64_t fs_base, gs_base;

static void user_heap_reset_for_image(uint64_t image_end) {
	uint64_t base = USER_HEAP_BASE;
	if (image_end > base) base = (image_end + PAGE_SIZE - 1) & PAGE_MASK;
	if (base < USER_HEAP_BASE) base = USER_HEAP_BASE;
	if (base >= USER_HEAP_MAX) base = USER_HEAP_MAX - PAGE_SIZE;
	if (current_task >= 0 && current_task < MAX_TASKS && tasks[current_task].used) {
		tasks[current_task].heap_base = base;
		tasks[current_task].heap_end = base;
	}

	heap_base = base;
	heap_end = base;
	debug_hex64("[BRK] image_end=0x", image_end);
	debug_hex64("[BRK] heap_base=0x", base);
}

static uint64_t task_read_fs_base(void) {
	uint32_t lo, hi;
	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100));
	uint64_t v = ((uint64_t)hi << 32) | lo;
	fs_base = v;
	return v;
}
static uint64_t task_read_gs_base(void) {
	uint32_t lo, hi;
	__asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000101));
	uint64_t v = ((uint64_t)hi << 32) | lo;
	gs_base = v;
	return v;
}

static struct unix_socket_state *socket_state(struct file_desc *f);
static int socket_side(struct file_desc *f);
static void socket_fd_drop_ref(struct file_desc *f);
static struct epoll_state_k *epoll_state_for_file(struct file_desc *f);
static void epoll_release_file(struct file_desc *f);
static void epoll_retain_file(struct file_desc *f);

static void fd_mark_closed(struct file_desc *f) {
	if (!f) return;
	if(f->input_state) input_state_release(f->input_state);
	f->input_state=NULL;
	f->in_use = 0;
	f->type = 0;
	f->flags = 0;
	f->offset = 0;
	f->size = 0;
	f->data = NULL;
	f->name = NULL;
	f->boot_path[0] = 0;
	f->private_data = NULL;
	f->vnode = NULL;
	f->shared_offset = NULL;
	f->input_seq = 0;
}
static void task_fd_release(struct file_desc *f) {
	if (!f || !f->in_use) return;
	if (f->type==FD_EPOLL) epoll_release_file(f);
	if (f->shared_offset) release_open_offset(f->shared_offset);
	fd_mark_closed(f);
}
static void task_fd_table_release(task_t *t);

static int task_cr3_is_shared(const task_t *victim) {
	if (!victim || !victim->ctx.cr3) return 0;
	for (int i = 0; i < MAX_TASKS; ++i) {
		const task_t *other = &tasks[i];
		if (other == victim || !other->used) continue;
		if ((other->ctx.cr3 & PAGE_MASK) == (victim->ctx.cr3 & PAGE_MASK)) return 1;
	}
	return 0;
}

static void task_reap_resources(task_t *t) {
	if (!t || !t->used) return;
	task_fd_table_release(t);
	if (t->ctx.cr3 && t->ctx.cr3 != tasks[0].ctx.cr3) {
		if (task_cr3_is_shared(t)) {
			debug_hex64("[TASK-REAP] keep shared CR3=0x", t->ctx.cr3);
			debugcon_puts("\n");
		} else {
			(void)vmm_user_space_destroy(t->ctx.cr3);
		}
	}
	t->ctx.cr3 = 0;
}

static void task_fd_table_release(task_t *t) {
    if (!t) return;
    int ti = (int)(t - tasks);
    if (ti < 0 || ti >= MAX_TASKS) return;
    int table = task_fd_table_id[ti];
    if (table < 0 || table >= MAX_FD_TABLES) {
        task_fd_table_id[ti] = -1;
        return;
    }

    task_fd_table_id[ti] = -1;
    if (!task_fd_table_refs[table]) return;
    if (--task_fd_table_refs[table] != 0) {

        if (ti == 0) { task_fd_table_id[0] = 0; task_fd_table_refs[0]++; }
        return;
    }

    for (int fd = 0; fd < MAX_FILES; fd++) {
        struct file_desc *f = &task_fd_tables[table][fd];
        if (!f->in_use) continue;
        if (f->type == FD_PSEUDO && ((uintptr_t)f->private_data==DEV_INPUT || (uintptr_t)f->private_data==DEV_INPUT_MOUSE)) {
            debugcon_puts("[FD TABLE RELEASE INPUT]");
            debug_hex64(" task_pid=0x", (uint64_t)t->pid);
            debug_hex64(" table=0x", (uint64_t)table);
            debug_hex64(" fd=0x", (uint64_t)fd);
            debug_hex64(" kind=0x", (uint64_t)(uintptr_t)f->private_data);
            debugcon_puts("\n");
        }
        if (f->type == FD_PIPE_R || f->type == FD_PIPE_W) {
            struct pipe_state *p = (struct pipe_state *)f->private_data;
            if (p) {
                if (f->type == FD_PIPE_R && p->readers) p->readers--;
                if (f->type == FD_PIPE_W && p->writers) p->writers--;
            }
        } else if (f->type == FD_PTY_MASTER || f->type == FD_PTY_SLAVE) {
            pty_state_k *p = (pty_state_k *)f->private_data;
            if (p) {
                if (f->type == FD_PTY_MASTER) {
                    if (p->master_refs) p->master_refs--;
                    if (!p->master_refs) p->master_open = 0;
                } else {
                    if (p->slave_refs) p->slave_refs--;
                    if (!p->slave_refs) p->slave_open = 0;
                }
            }
        } else if (f->type == FD_SOCKET) {
            socket_fd_drop_ref(f);
        }
        task_fd_release(f);
    }

    if (table == 0) { task_fd_table_id[0] = 0; task_fd_table_refs[0] = 1; }
}

static int task_pick_next(void);
static void task_account_switch(int next);
static int user_copy_to_cr3(uint64_t cr3, uint64_t dst, const uint8_t *src, uint64_t len);

static int task_fd_table_clone(task_t *parent, task_t *child, int share_files) {
    int pi = (int)(parent - tasks);
    int ci = (int)(child - tasks);
    if (pi < 0 || pi >= MAX_TASKS || ci < 0 || ci >= MAX_TASKS) return -1;
    int source = task_fd_table_id[pi];
    if (source < 0 || source >= MAX_FD_TABLES || !task_fd_table_refs[source]) return -1;

    if (share_files) {
        task_fd_table_id[ci] = (int16_t)source;
        task_fd_table_refs[source]++;
        return 0;
    }

    int destination = -1;
    for (int i = 0; i < MAX_FD_TABLES; i++) {
        if (i != 0 && !task_fd_table_refs[i]) { destination = i; break; }
    }
    if (destination < 0) return -1;

    task_fd_table_id[ci] = (int16_t)destination;
    task_fd_table_refs[destination] = 1;
    struct file_desc *src_table = task_fd_tables[source];
    struct file_desc *dst_table = task_fd_tables[destination];
    for (int fd = 0; fd < MAX_FILES; fd++) {
        struct file_desc *src = &src_table[fd];
        struct file_desc *dst = &dst_table[fd];
        *dst = *src;
        if (!src->in_use) continue;
        if (dst->input_state) input_state_retain(dst->input_state);
        if (dst->type==FD_EPOLL) epoll_retain_file(dst);
        if (dst->shared_offset) retain_open_offset(dst->shared_offset);
        if (dst->type == FD_PIPE_R) {
            struct pipe_state *p = (struct pipe_state *)dst->private_data;
            if (p) p->readers++;
        } else if (dst->type == FD_PIPE_W) {
            struct pipe_state *p = (struct pipe_state *)dst->private_data;
            if (p) p->writers++;
        } else if (dst->type == FD_PTY_MASTER) {
            pty_state_k *p = (pty_state_k *)dst->private_data;
            if (p) { p->master_refs++; p->master_open=1; }
        } else if (dst->type == FD_PTY_SLAVE) {
            pty_state_k *p = (pty_state_k *)dst->private_data;
            if (p) { p->slave_refs++; p->slave_open=1; }
        } else if (dst->type == FD_SOCKET) {
            struct unix_socket_state *sock = socket_state(dst);
            if (sock) {
                int side = socket_side(dst);
                sock->refs++;
                if (side >= 0 && side < 2) {

                    sock->side_refs[side]++;
                }
            }
        }
    }
    return 0;
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

uint64_t kernel_uptime_ns(void) {
	const uint64_t ns_per_tick = 10000000ULL;                 
	uint64_t ticks = timer_ticks_global;
	if (ticks > UINT64_MAX / ns_per_tick) return UINT64_MAX;
	return ticks * ns_per_tick;
}

static void task_wake_timed_io_waiters(void);
task_context_t *timer_preempt_from_user(uint64_t *irq_stack) {

	outb(PIC1_COMMAND, 0x20);
	timer_ticks_global++;
	timer_tick_tsc = read_tsc();

	ps2_keyboard_poll();
	ps2_mouse_poll();
	task_wake_timed_io_waiters();

	if (framebuffer_console_active && (timer_ticks_global % 50u)==0u)
		console_fb_cursor_blink_tick();

	if (!irq_stack) return NULL;

	uint64_t cs = irq_stack[16];
	if ((cs & 0x3u) != 0x3u) return NULL;                                 
	if (current_task < 0 || current_task >= MAX_TASKS || !tasks[current_task].used) return NULL;

	task_t *cur = &tasks[current_task];
	cur->ctx.rax = irq_stack[0];
	cur->ctx.rbx = irq_stack[1];
	cur->ctx.rcx = irq_stack[2];
	cur->ctx.rdx = irq_stack[3];
	cur->ctx.rsi = irq_stack[4];
	cur->ctx.rdi = irq_stack[5];
	cur->ctx.rbp = irq_stack[6];
	cur->ctx.r8  = irq_stack[7];
	cur->ctx.r9  = irq_stack[8];
	cur->ctx.r10 = irq_stack[9];
	cur->ctx.r11 = irq_stack[10];
	cur->ctx.r12 = irq_stack[11];
	cur->ctx.r13 = irq_stack[12];
	cur->ctx.r14 = irq_stack[13];
	cur->ctx.r15 = irq_stack[14];
	cur->ctx.rip = irq_stack[15];
	cur->ctx.rflags = irq_stack[17];
	cur->ctx.rsp = irq_stack[18];
	__asm__ volatile ("mov %%cr3, %0" : "=r"(cur->ctx.cr3));
	cur->ctx.fs_base = task_read_fs_base();
	cur->ctx.gs_base = task_read_gs_base();
	cur->cpu_ticks++;

	const uint64_t sigint_bit = 1ULL << 1;
	if ((cur->sig_pending[0] & sigint_bit) && !(cur->sig_blocked[0] & sigint_bit)) {
		uintptr_t handler = (uintptr_t)cur->sig_actions[2].sa_handler;
		if (handler == 1) {
			cur->sig_pending[0] &= ~sigint_bit;              
		} else if (handler == 0 && cur->pid > 1) {
			cur->sig_pending[0] &= ~sigint_bit;
			cur->exit_status = 130;
			cur->state = TASK_ZOMBIE;

			if (cur->fb_map_count > 0) {
				if (framebuffer_users >= cur->fb_map_count) framebuffer_users -= cur->fb_map_count;
				else framebuffer_users = 0;
				cur->fb_map_count = 0;
			}
			task_fd_table_release(cur);
			for (int pi=0; pi<MAX_TASKS; ++pi) {
				task_t *parent = &tasks[pi];
				if (!parent->used || parent->pid != cur->ppid) continue;
				parent->sig_pending[0] |= 1ULL << 16;              
				if (parent->state == TASK_BLOCKED && parent->wait4_active &&
				    (parent->wait4_pid <= 0 || parent->wait4_pid == cur->pid)) {
					int wait_status = (cur->exit_status & 0xff) << 8;
					if (!parent->wait4_status_ptr ||
					    user_copy_to_cr3(parent->ctx.cr3, parent->wait4_status_ptr,
					                    (const uint8_t *)&wait_status, sizeof(wait_status)) == 0) {
						parent->ctx.rax = (uint64_t)cur->pid;
						parent->wait4_active = 0;
						parent->wait4_pid = -1;
						parent->wait4_status_ptr = 0;
						parent->wait4_reap_pid = cur->pid;
						parent->state = TASK_RUNNABLE;
					}
				}
				break;
			}
		}
	}

	if (cur->state != TASK_ZOMBIE) cur->state = TASK_RUNNABLE;
	int next = task_pick_next();
	if (next < 0) {
		if (cur->state == TASK_ZOMBIE) {

			if (tasks[0].used && tasks[0].state != TASK_ZOMBIE) next = 0;
		}
		if (next < 0) {
			cur->state = TASK_RUNNING;
			return NULL;
		}
	}

	tasks[next].state = TASK_RUNNING;
	current_task = next;
	task_account_switch(next);
	(void)vmm_user_activate(tasks[next].ctx.cr3);
	task_switch_target = next;
#if YABRO_DEBUG_SCHED
	if ((timer_ticks_global & 0x3fu) == 1u) {
		debug_hex64("[TIMER-SWITCH] from=0x", (uint64_t)cur->pid);
		debug_hex64(" to=0x", (uint64_t)tasks[next].pid);
		debugcon_puts("\n");
	}
#endif
	return &tasks[next].ctx;
}
#define ENOSYS 38
#define EPERM 1
#define EBADF  9
#define EINVAL 22
#define EFAULT 14
#define ENOMEM 12
static void task_init_root(void) {
	if (!tasks[0].used) { tasks[0].used=1; tasks[0].state=TASK_RUNNING; tasks[0].pid=1;
		tasks[0].ppid=0; tasks[0].tgid=1; tasks[0].pgrp=1; tasks[0].session=1; tasks[0].controlling_tty=-1; tasks[0].cwd=vfs_root(); tasks[0].pipe_wait_fd=-1; tasks[0].pipe_wait_buf=0; tasks[0].pipe_wait_count=0;
		tasks[0].wait4_active=0; tasks[0].wait4_pid=-1; tasks[0].wait4_status_ptr=0; tasks[0].wait4_reap_pid=-1; tasks[0].io_wait_kind=0; tasks[0].io_wait_valid=0; tasks[0].io_wait_nr=0; tasks[0].io_wait_deadline_ns=0; tasks[0].io_resume_valid=0; tasks[0].io_resume_nr=0; tasks[0].io_resume_rip=0; tasks[0].io_resume_start_ns=0; tasks[0].io_resume_deadline_ns=0;
		tasks[0].heap_base=USER_HEAP_BASE; tasks[0].heap_end=USER_HEAP_BASE; tasks[0].mmap_next=USER_MMAP_MIN; tasks[0].fb_map_count=0;
		tasks[0].nice=0; tasks[0].switches=0; tasks[0].cpu_ticks=0; }
}

static int task_pick_next(void) {
	int best=-1, best_nice=1000000;
	uint64_t best_switches=UINT64_MAX;
	for (int n=1;n<=MAX_TASKS;n++) {
		int i=(current_task+n)%MAX_TASKS;
		if (!tasks[i].used || tasks[i].state!=TASK_RUNNABLE) continue;
		if (tasks[i].nice < best_nice ||
		    (tasks[i].nice == best_nice && tasks[i].switches < best_switches)) {
			best=i; best_nice=tasks[i].nice; best_switches=tasks[i].switches;
		}
	}
	return best;
}

static void task_account_switch(int next) {
	if (next < 0 || next >= MAX_TASKS || !tasks[next].used) return;
	tasks[next].switches++;
}

static int task_set_nice(int pid, int nice) {
	if (nice < -20) nice = -20;
	if (nice > 19) nice = 19;
	for (int i=0;i<MAX_TASKS;i++) {
		if (!tasks[i].used || tasks[i].pid != pid) continue;
		tasks[i].nice=nice;
		return 0;
	}
	return -3;
}

#define ENOENT 2
#define EEXIST 17
#define EROFS 30
#define ENOTEMPTY 39
#define ENAMETOOLONG 36
#define EMFILE 24
#define ESPIPE 29
#define EPIPE 32
#define ENOTTY 25
#define ENFILE 23
#define ERANGE 34
#define EAGAIN 11
#define ECHILD 10
#define ESRCH 3
#define ENOTDIR 20
#define EISDIR 21
#define ETIMEDOUT 110
#define ENOSPC 28
#define EIO 5
#define EAFNOSUPPORT 97
#define EADDRINUSE 98
#define ENOTSOCK 88
#define ENOPROTOOPT 92

#define IO_WAIT_NONE   0
#define IO_WAIT_POLL   1
#define IO_WAIT_SELECT 2
#define IO_WAIT_EPOLL  3
#define IO_WAIT_SOCKET_READ  4
#define IO_WAIT_SOCKET_WRITE 5
#define IO_WAIT_SOCKET_ACCEPT 6
#define IO_WAIT_NANOSLEEP     7
#define IO_WAIT_PTY_READ      8
#define IO_WAIT_PTY_DRAIN     9
#define IO_WAIT_TTY           10

#if YABRO_DEBUG_SCHED
static int io_wait_dbg_budget = 64;
#endif

static int task_pty_read_ready(const task_t *t) {
    if (!t || !t->used) return 0;
    int ti=(int)(t-tasks);
    if (ti<0 || ti>=MAX_TASKS) return 0;
    int fd=(int)t->ctx.rdi;
    if (fd<0 || fd>=MAX_FILES) return 1;
    struct file_desc *table=task_files_for_task(ti);
    if (!table || !table[fd].in_use) return 1;                             
    struct file_desc *f=&table[fd];
    if (f->type!=FD_PTY_MASTER && f->type!=FD_PTY_SLAVE) return 1;
    pty_state_k *p=(pty_state_k*)f->private_data;
    if (!p) return 1;
    if (f->type==FD_PTY_MASTER) return pty_master_read_ready(p);
    return pty_slave_read_ready(p);
}

static int task_pty_drain_ready(const task_t *t) {
    if (!t || !t->used) return 0;
    int ti=(int)(t-tasks);
    if (ti<0 || ti>=MAX_TASKS) return 0;
    int fd=(int)t->ctx.rdi;
    if (fd<0 || fd>=MAX_FILES) return 1;
    struct file_desc *table=task_files_for_task(ti);
    if (!table || !table[fd].in_use) return 1;
    struct file_desc *f=&table[fd];
    if (f->type!=FD_PTY_MASTER && f->type!=FD_PTY_SLAVE) return 1;
    pty_state_k *p=(pty_state_k*)f->private_data;
    return !p || p->s2m_count==0 || !p->master_open;
}

static int task_socket_wait_ready(const task_t *t) {
	if (!t || !t->used) return 0;
	int ti = (int)(t - tasks);
	if (ti < 0 || ti >= MAX_TASKS) return 0;

	int fd = (int)t->ctx.rdi;
	if (fd < 0 || fd >= MAX_FILES) return 0;
	struct file_desc *other_table = task_files_for_task(ti);
	if (!other_table) return 0;
	struct file_desc *ft = &other_table[fd];
	if (!ft->in_use || ft->type != FD_SOCKET) return 0;
	struct unix_socket_state *s = socket_state(ft);
	if (!s) return 0;
	int side = socket_side(ft);
	if (side < 0 || side > 1) return 0;

	switch (t->io_wait_kind) {
	case IO_WAIT_SOCKET_READ:
		return s->count[side] != 0 || s->closed[side ^ 1];
	case IO_WAIT_SOCKET_WRITE:
		return s->closed[side ^ 1] || s->count[side ^ 1] < SOCK_CAP;
	case IO_WAIT_SOCKET_ACCEPT:
		return s->listener && s->pending_count != 0;
	default:
		return 1;
	}
}

static int task_wake_io_waiters(void) {
	int woke_socket_read = -1;
	for (int i=0;i<MAX_TASKS;i++) {
		task_t *t=&tasks[i];
		if (!t->used || t->state!=TASK_BLOCKED || t->io_wait_kind==IO_WAIT_NONE) continue;

		if (t->io_wait_kind==IO_WAIT_NANOSLEEP) continue;

		if ((t->io_wait_kind==IO_WAIT_SOCKET_READ ||
		     t->io_wait_kind==IO_WAIT_SOCKET_WRITE ||
		     t->io_wait_kind==IO_WAIT_SOCKET_ACCEPT) &&
		    !task_socket_wait_ready(t)) continue;
		if (t->io_wait_kind==IO_WAIT_PTY_READ && !task_pty_read_ready(t)) continue;
        if (t->io_wait_kind==IO_WAIT_PTY_DRAIN && !task_pty_drain_ready(t)) continue;
        if (t->io_wait_kind==IO_WAIT_TTY) {
            if (key_head==key_tail) continue;
            if (console_fg_pgrp>0 && t->pgrp!=console_fg_pgrp) continue;
        }

		if (!t->io_wait_valid) {
			t->state=TASK_RUNNABLE;
			t->io_wait_kind=IO_WAIT_NONE;
			t->io_wait_valid=0;
			t->io_wait_nr=0;
			t->io_wait_deadline_ns=0;
			continue;
		}
		t->ctx.rax = t->io_wait_nr;
#if YABRO_DEBUG_SCHED
		if (io_wait_dbg_budget > 0) {
			debug_hex64("[IO-WAKE] pid=0x", (uint64_t)t->pid);
			debug_hex64(" kind=0x", (uint64_t)t->io_wait_kind);
			debugcon_puts("\n");
			io_wait_dbg_budget--;
		}
#endif
		int wake_kind = t->io_wait_kind;
		t->io_wait_kind=IO_WAIT_NONE;
		t->io_wait_valid=0;
		t->io_wait_nr=0;
		t->io_wait_deadline_ns=0;
		if (wake_kind == IO_WAIT_SOCKET_READ && woke_socket_read < 0) woke_socket_read = i;
		t->state=TASK_RUNNABLE;
	}
	return woke_socket_read;
}

static void task_wake_timed_io_waiters(void) {
	uint64_t now=kernel_monotonic_ns();
	for(int i=0;i<MAX_TASKS;i++) {
		task_t *t=&tasks[i];
		if(!t->used || t->state!=TASK_BLOCKED) continue;

		if(t->futex_addr && t->futex_deadline_ns && now>=t->futex_deadline_ns) {
			t->futex_addr=0; t->futex_cr3=0; t->futex_deadline_ns=0;
			t->ctx.rax=-ETIMEDOUT; t->state=TASK_RUNNABLE;
			continue;
		}
		if(t->io_wait_kind==IO_WAIT_NONE) continue;
		uint64_t deadline=t->io_wait_deadline_ns;
		if(!deadline || now<deadline) continue;
		t->ctx.rax=t->io_wait_nr;
		t->io_wait_kind=IO_WAIT_NONE;
		t->io_wait_valid=0;
		t->io_wait_nr=0;
		t->io_wait_deadline_ns=0;
		t->state=TASK_RUNNABLE;
	}
}

void task_commit_deferred_switch(task_context_t *next) {
	if (!next || current_task < 0 || current_task >= MAX_TASKS) return;
	int ni = -1;
	for (int i = 0; i < MAX_TASKS; i++) {
		if (&tasks[i].ctx == next) { ni = i; break; }
	}
	if (ni < 0 || !tasks[ni].used || ni == current_task) return;
	task_t *cur = &tasks[current_task];
	if (cur->state == TASK_RUNNING) cur->state = TASK_RUNNABLE;
	tasks[ni].state = TASK_RUNNING;
	current_task = ni;
	task_account_switch(ni);
#if YABRO_DEBUG_SCHED
	if (tasks[ni].pid == 0x64 || tasks[ni].pid == 0x65 || tasks[ni].pid == 0x68) {
		debug_hex64("[DEFER-COMMIT] target_pid=0x", (uint64_t)tasks[ni].pid);
		debug_hex64(" rip=0x", tasks[ni].ctx.rip);
		debug_hex64(" rsp=0x", tasks[ni].ctx.rsp);
		debug_hex64(" cr3=0x", tasks[ni].ctx.cr3);
		debug_hex64(" rax=0x", tasks[ni].ctx.rax);
		debugcon_puts("\n");
	}
#endif
}

static void kernel_wait_tick(void);

static int task_block_io_until(syscall_frame_t *f, int kind, uint64_t nr, uint64_t deadline_ns) {
	if (!f || current_task<0 || current_task>=MAX_TASKS) return -EINVAL;
	task_t *t=&tasks[current_task];
	task_save_from_frame(t,f);
	t->io_wait_kind=kind;
	t->io_wait_valid=1;
	t->io_wait_nr=nr;
	t->io_wait_deadline_ns=deadline_ns;
	t->ctx.rax=nr;
	if (t->ctx.rip >= 2) t->ctx.rip -= 2;
	t->state=TASK_BLOCKED;
#if YABRO_DEBUG_SCHED
	if (io_wait_dbg_budget > 0) {
		debug_hex64("[IO-BLOCK] pid=0x", (uint64_t)t->pid);
		debug_hex64(" kind=0x", (uint64_t)kind);
		debug_hex64(" nr=0x", nr);
		debugcon_puts("\n");
		io_wait_dbg_budget--;
	}
#endif

	int next=task_pick_next();
	if (next<0) {
		for (;;) {
			if (t->state != TASK_BLOCKED) {

				int alt = task_pick_next();
				if (alt >= 0) {
					tasks[alt].state=TASK_RUNNING;
					current_task=alt;
					task_account_switch(alt);
					(void)vmm_user_activate(tasks[alt].ctx.cr3);
					task_switch_target=alt;
					task_switch_pending=1;
					task_return_ctx=&tasks[alt].ctx;
					return 0;
				}

				t->state=TASK_RUNNING;
				current_task=(int)(t-tasks);
				task_switch_target=current_task;
				task_switch_pending=1;
				task_return_ctx=&t->ctx;
				return 0;
			}
			kernel_wait_tick();
		}
	}
#if YABRO_DEBUG_SCHED
	if (t->pid == 0x64 || t->pid == 0x65 || t->pid == 0x68 || tasks[next].pid == 0x64 || tasks[next].pid == 0x65 || tasks[next].pid == 0x68) {
		debug_hex64("[TASK-BLOCK-SWITCH] from_pid=0x", (uint64_t)t->pid);
		debug_hex64(" from_state=0x", (uint64_t)t->state);
		debug_hex64(" to_pid=0x", (uint64_t)tasks[next].pid);
		debug_hex64(" to_state=0x", (uint64_t)tasks[next].state);
		debug_hex64(" to_rip=0x", tasks[next].ctx.rip);
		debug_hex64(" to_rsp=0x", tasks[next].ctx.rsp);
		debug_hex64(" to_cr3=0x", tasks[next].ctx.cr3);
		debug_hex64(" to_rax=0x", tasks[next].ctx.rax);
		debugcon_puts("\n");
	}
#endif
	tasks[next].state=TASK_RUNNING;
	current_task=next;
	task_account_switch(next);
	(void)vmm_user_activate(tasks[next].ctx.cr3);
	task_switch_target=next;
	task_switch_pending=1;
	task_return_ctx=&tasks[next].ctx;
	return 0;
}

static int task_block_io(syscall_frame_t *f, int kind, uint64_t nr) {
	return task_block_io_until(f,kind,nr,0);
}
static int task_io_resume_matches(const syscall_frame_t *f, uint64_t nr) {
	if(!f || current_task<0 || current_task>=MAX_TASKS) return 0;
	task_t *t=&tasks[current_task];
	return t->io_resume_valid && t->io_resume_nr==nr && t->io_resume_rip==f->user_rip;
}
static void task_io_resume_store(const syscall_frame_t *f, uint64_t nr, uint64_t start_ns, uint64_t deadline_ns) {
	if(!f || current_task<0 || current_task>=MAX_TASKS) return;
	task_t *t=&tasks[current_task]; t->io_resume_valid=1; t->io_resume_nr=nr;
	t->io_resume_rip=f->user_rip; t->io_resume_start_ns=start_ns; t->io_resume_deadline_ns=deadline_ns;
}
static void task_io_resume_clear(const syscall_frame_t *f, uint64_t nr) {
	if(!task_io_resume_matches(f,nr)) return;
	task_t *t=&tasks[current_task]; t->io_resume_valid=0; t->io_resume_nr=0;
	t->io_resume_rip=0; t->io_resume_start_ns=0; t->io_resume_deadline_ns=0;
}
static uint64_t monotonic_deadline_after_ms(uint64_t start_ns, int timeout_ms) {
	if(timeout_ms<0) return 0;
	uint64_t span=(uint64_t)timeout_ms*1000000ULL;
	return UINT64_MAX-start_ns<span ? UINT64_MAX : start_ns+span;
}

int64_t sys_exit_group(int status);
int64_t sys_exit(int status);
#define TIOCSCTTY 0x540E
#define TIOCSPGRP 0x5410
#define TIOCGPGRP 0x540F
#define ICANON 0x00000002ULL
#define ECHO 0x00000008ULL
#define ISIG 0x00000001ULL

#define INLCR 0x00000040U
#define IGNCR 0x00000080U
#define ICRNL 0x00000100U
#define IXANY 0x00000800U
#define IXON  0x00000400U
#define OPOST 0x00000001U
#define ONLCR 0x00000004U
#define ECHONL 0x00000040U
#define ECHOE  0x00000010U
#define ECHOK  0x00000020U
#define NOFLSH 0x00000080U
#define ECHOCTL 0x00000200U
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VSTART 8
#define VSTOP 9
#define VSUSP 10
#define VEOL 11
#define VEOL2 16
#define NCCS 19
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TCFLSH  0x540B
#define TCXONC  0x540A
#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3
#define TCIFLUSH 0
#define TCOFLUSH 1
#define TCIOFLUSH 2
#define SIGQUIT 3
#define SIGTSTP 20
#define SIGWINCH 28
#define SIGCHLD 17
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define TERMIOS_K_SIZE 60
#define SIGINT 2
static uint8_t console_termios_k[TERMIOS_K_SIZE];                                  
static int sig_pending_set(task_t*t,int s);
static int sig_is_blocked(task_t*t,int s);
static void sig_clear_pending(task_t*t,int s);

/*
 * A terminal resize sends SIGWINCH to the foreground process group.  Linux
 * treats the default SIGWINCH action (and SIGCHLD) as ignored; ignored or
 * blocked signals must not interrupt a thread sleeping in read()/poll().
 * Otherwise a harmless resize turns a pending terminal read into EINTR and
 * some programs leave their input loop.
 */
static void tty_queue_signal(task_t *t, int sig) {
    if (!t || sig <= 0 || sig > 64) return;
    sighandler_t handler = t->sig_actions[sig].sa_handler;
    if (handler == SIG_IGN ||
        (handler == SIG_DFL && (sig == SIGCHLD || sig == SIGWINCH))) {
        sig_clear_pending(t, sig);
        return;
    }
    (void)sig_pending_set(t,sig);
    if (sig_is_blocked(t, sig)) return;
    if (t->state==TASK_BLOCKED && t->io_wait_kind!=IO_WAIT_NONE && t->io_wait_valid) {

        if (t->ctx.rip <= UINT64_MAX-2) t->ctx.rip += 2;
        t->ctx.rax=(uint64_t)(int64_t)-4;
        t->io_wait_kind=IO_WAIT_NONE;
        t->io_wait_valid=0;
        t->io_wait_nr=0;
        t->io_wait_deadline_ns=0;
        t->io_resume_valid=0;
        t->io_resume_nr=0;
        t->state=TASK_RUNNABLE;
    } else if (t->state==TASK_BLOCKED && t->wait4_active) {

        t->wait4_active=0;
        t->wait4_pid=-1;
        t->wait4_status_ptr=0;
        t->ctx.rax=(uint64_t)(int64_t)-4;            
        t->state=TASK_RUNNABLE;
    } else if (t->state==TASK_BLOCKED && t->pipe_wait_fd>=0) {

        t->pipe_wait_fd=-1;
        t->pipe_wait_buf=0;
        t->pipe_wait_count=0;
        t->ctx.rax=(uint64_t)(int64_t)-4;            
        t->state=TASK_RUNNABLE;
    } else if (t->state==TASK_BLOCKED && t->futex_addr) {

        t->futex_addr=0;
        t->futex_cr3=0;
        t->futex_deadline_ns=0;
        t->ctx.rax=(uint64_t)(int64_t)-4;            
        t->state=TASK_RUNNABLE;
    }
}

static uint64_t tty_iflag(const uint8_t *t){ uint32_t v=0; __builtin_memcpy(&v,t+0,4); return v; }
static uint64_t tty_oflag(const uint8_t *t){ uint32_t v=0; __builtin_memcpy(&v,t+4,4); return v; }
static uint64_t tty_lflag(const uint8_t *t){ uint32_t v=0; __builtin_memcpy(&v,t+12,4); return v; }
static uint8_t tty_cc(const uint8_t *t, int i){ return (i>=0 && i<NCCS)?t[17+i]:0; }
static int tty_cc_match(const uint8_t *t,int i,uint8_t c){ uint8_t v=tty_cc(t,i); return v!=0 && c==v; }
static void tty_default_termios(uint8_t *t){ for(int i=0;i<TERMIOS_K_SIZE;i++)t[i]=0; uint32_t iflag=0,oflag=0,lflag=ICANON|ECHO|ISIG; __builtin_memcpy(t+0,&iflag,4); __builtin_memcpy(t+4,&oflag,4); __builtin_memcpy(t+12,&lflag,4); t[17+VINTR]=3; t[17+VQUIT]=28; t[17+VERASE]=127; t[17+VKILL]=21; t[17+VEOF]=4; t[17+5]=0; t[17+6]=1; t[17+VSUSP]=26; }
static int tty_pgrp_signal(pty_state_k *p, int sig){
    if(!p)return-EIO;
    int pg=p->fg_pgrp;
    if(!pg)return-ESRCH;
    int sent=0;
    for(int i=0;i<MAX_TASKS;i++) {
        if(!tasks[i].used || tasks[i].pgrp!=pg) continue;
        if(p->session>0 && tasks[i].session!=p->session) continue;
        tty_queue_signal(&tasks[i],sig); sent++;
    }
    return sent?0:-ESRCH;
}
static int console_tty_pgrp_signal(int sig){
    int pg=console_fg_pgrp;
    if(pg<=0) pg=tasks[current_task].pgrp;
    int session=tasks[current_task].session, sent=0;
    if(pg<=0)return-ESRCH;
    for(int i=0;i<MAX_TASKS;i++){
        if(tasks[i].used && tasks[i].pgrp==pg && tasks[i].session==session){
            tty_queue_signal(&tasks[i],sig); sent++;
        }
    }
    return sent?0:-ESRCH;
}
static uint64_t pty_output_write(pty_state_k *p,const uint8_t *src,uint64_t n);
static int pty_canon_available(pty_state_k *p){
    if(!p||!p->canon_count)return 0;
    uint8_t veol=tty_cc(p->termios,VEOL),veol2=tty_cc(p->termios,VEOL2);
    for(uint32_t i=0;i<p->canon_count;i++){
        uint8_t c=p->canon[i];
        if(c=='\n'||tty_cc_match(p->termios,VEOF,c)||(veol&&c==veol)||(veol2&&c==veol2))return 1;
    }
    return 0;
}
static int pty_master_read_ready(const pty_state_k *p){
    return p && ((!p->output_stopped && p->s2m_count!=0) || !p->slave_open);
}
static int pty_slave_read_ready(const pty_state_k *p){
    if(!p)return 1;
    if(((uint32_t)p->termios[12]|((uint32_t)p->termios[13]<<8)|
        ((uint32_t)p->termios[14]<<16)|((uint32_t)p->termios[15]<<24))&ICANON)
        return pty_canon_available((pty_state_k*)p)||!p->master_open;
    return p->m2s_count!=0||!p->master_open;
}
static int pty_canon_ready_bytes(const pty_state_k *p){
    if(!p)return 0;
    uint32_t n=0,ready=0;
    uint8_t veof=tty_cc(p->termios,VEOF),veol=tty_cc(p->termios,VEOL),veol2=tty_cc(p->termios,VEOL2);
    for(uint32_t i=0;i<p->canon_count;i++){
        uint8_t c=p->canon[i];
        if(veof&&c==veof){ready=n;break;}
        n++;
        if(c=='\n'||(veol&&c==veol)||(veol2&&c==veol2))ready=n;
    }
    return (int)ready;
}
static void pty_flush_input(pty_state_k *p){
    if(!p)return; p->m2s_r=p->m2s_w=p->m2s_count=0; p->canon_count=0;
}
static void pty_flush_output(pty_state_k *p){
    if(!p)return; p->s2m_r=p->s2m_w=p->s2m_count=0;
}
static void pty_echo_char(pty_state_k *p,uint8_t c,uint32_t lflag){
    if(!p||!p->master_open)return;
    if(!(lflag&ECHO)){ if(c=='\n'&&(lflag&ECHONL)){(void)pty_output_write(p,&c,1);} return; }
    if((lflag&ECHOCTL)&&((c<0x20&&c!='\n'&&c!='\t')||c==0x7f)){
        uint8_t vis[2]={'^',(uint8_t)(c==0x7f?'?':(c^0x40))};
        (void)pty_output_write(p,vis,2);
    }else (void)pty_output_write(p,&c,1);
}

static uint64_t pty_output_write(pty_state_k *p,const uint8_t *src,uint64_t n){
    if(!p||!src)return 0;
    uint32_t oflag=(uint32_t)tty_oflag(p->termios);
    uint64_t done=0;
    while(done<n){
        uint8_t c=src[done];
        if((oflag&OPOST)&&(oflag&ONLCR)&&c=='\n'){
            if(PTY_CAP-p->s2m_count<2)break;
            const uint8_t crlf[2]={'\r','\n'};
            if(pty_ring_write(p->slave_to_master,&p->s2m_w,&p->s2m_count,crlf,2)!=2)break;
        }else{
            if(p->s2m_count>=PTY_CAP)break;
            if(pty_ring_write(p->slave_to_master,&p->s2m_w,&p->s2m_count,&c,1)!=1)break;
        }
        done++;
    }
    return done;
}

static int pty_canon_feed(pty_state_k *p,const uint8_t *src,uint64_t n){
    if(!p||!src)return-EIO;
    uint32_t iflag=(uint32_t)tty_iflag(p->termios),lflag=(uint32_t)tty_lflag(p->termios);
    for(uint64_t i=0;i<n;i++){
        uint8_t c=src[i];

        if(c>0 && c<0x20 && c!='\r' && c!='\n' && c!='\t'){
            uint32_t bit=(uint32_t)1u<<c;
            if(!(pty_dbg_control_input_mask&bit)){
                pty_dbg_control_input_mask|=bit; debugcon_puts("[PTYDBG CONTROL INPUT]");
                debug_hex64(" byte=0x",(uint64_t)c); debug_hex64(" lflag=0x",(uint64_t)lflag);
                debug_hex64(" fg_pgrp=0x",(uint64_t)(uint32_t)p->fg_pgrp);
                debug_hex64(" shell_pgrp=0x",(uint64_t)(uint32_t)p->shell_pgrp); debugcon_puts("\n");
            }
        }
        if(c=='\r'){ if(iflag&IGNCR)continue; if(iflag&ICRNL)c='\n'; }
        else if(c=='\n'&&(iflag&INLCR))c='\r';

        if(iflag&IXON){
            if(tty_cc_match(p->termios,VSTOP,c)){p->output_stopped=1;continue;}
            if(tty_cc_match(p->termios,VSTART,c)){p->output_stopped=0;task_wake_io_waiters();continue;}
            if(p->output_stopped&&(iflag&IXANY)){p->output_stopped=0;task_wake_io_waiters();}
        }

        if(lflag&ISIG){
            int sig=0;
            if(tty_cc_match(p->termios,VINTR,c))sig=SIGINT;
            else if(tty_cc_match(p->termios,VQUIT,c))sig=SIGQUIT;
            else if(tty_cc_match(p->termios,VSUSP,c))sig=SIGTSTP;
            if(sig){
                if(!(lflag&NOFLSH)){pty_flush_input(p);pty_flush_output(p);p->output_stopped=0;}
                pty_echo_char(p,c,lflag);
                (void)tty_pgrp_signal(p,sig);
                if((lflag&ECHO)&&(lflag&ECHOCTL)){const uint8_t nl='\n';(void)pty_output_write(p,&nl,1);}
                continue;
            }
        }

        if(lflag&ICANON){
            if(tty_cc_match(p->termios,VERASE,c)){
                if(p->canon_count){
                    p->canon_count--;
                    if(lflag&ECHO){
                        if(lflag&ECHOE){const uint8_t e[3]={'\b',' ','\b'};(void)pty_output_write(p,e,3);}
                        else pty_echo_char(p,c,lflag);
                    }
                }else if(lflag&ECHO)pty_echo_char(p,c,lflag);
                continue;
            }
            if(tty_cc_match(p->termios,VKILL,c)){
                p->canon_count=0;
                if(lflag&ECHOK){const uint8_t nl='\n';(void)pty_output_write(p,&nl,1);}
                else if(lflag&ECHO)pty_echo_char(p,c,lflag);
                continue;
            }
            if(tty_cc_match(p->termios,VEOF,c)){
                if(p->canon_count>=PTY_CAP)return (int)i;
                p->canon[p->canon_count++]=c; continue;
            }
            if(p->canon_count>=PTY_CAP)return (int)i;
            p->canon[p->canon_count++]=c;
            pty_echo_char(p,c,lflag);
        }else{
            if(p->m2s_count>=PTY_CAP)return (int)i;
            (void)pty_ring_write(p->master_to_slave,&p->m2s_w,&p->m2s_count,&c,1);
            pty_echo_char(p,c,lflag);
        }
    }
    return (int)n;
}

int64_t sys_write(syscall_frame_t *sf, int fd, const void *buf, uint64_t count);
int64_t sys_read(syscall_frame_t *sf, int fd, void *buf, uint64_t count);
static int is_xkm_fd(const struct file_desc *f);
static void xkm_audit_bytes(const char *tag, const uint8_t *p, uint64_t n);
static void xkm_audit_user(const char *tag, uint64_t u, uint64_t n);
static void xkm_audit_header(const char *tag, const uint8_t *p, uint64_t n);
static void xkm_audit_toc(const char *tag, const uint8_t *p, uint64_t n);
int64_t sys_open(const char *path, int flags, int mode);
int64_t sys_mkdir(const char *path, int mode); int64_t sys_rename(const char *oldpath, const char *newpath); int64_t sys_link(const char *oldpath, const char *newpath);
static int resolve_process_path(const char *in, char *out, size_t cap);
int64_t sys_unlink(const char *path);
int64_t sys_rmdir(const char *path);
int64_t sys_getdents64(int fd, void *dirent, uint64_t count);
int64_t sys_readlink(const char *path, char *buf, uint64_t size);
int64_t sys_readlinkat(int dirfd, const char *path, char *buf, uint64_t size);
int64_t sys_close(int fd);
int64_t sys_epoll_pwait(syscall_frame_t *f, int epfd, void *uevents, int maxevents, int timeout, const void *sigmask, uint64_t sigsetsize);
int64_t sys_stat(const char *path, void *st);
int64_t sys_fstat(int fd, void *st);
int64_t sys_lseek(int fd, int64_t off, int whence);
int64_t sys_pread64(int fd, void *buf, uint64_t count, uint64_t off);
int64_t sys_readv(syscall_frame_t *sf, int fd, const void *iov, int iovcnt);
int64_t sys_writev(syscall_frame_t *sf, int fd, const void *iov, int iovcnt);
int64_t sys_access(const char *path, int mode);
int64_t sys_pipe(int *fds);
int64_t sys_dup(int oldfd);
int64_t sys_dup2(int oldfd, int newfd);
int64_t sys_fcntl(int fd, int cmd, uint64_t arg);
int64_t sys_sendmsg(syscall_frame_t *sf, int sockfd, const void *umsg, int flags);
int64_t sys_recvmsg(syscall_frame_t *sf, int sockfd, void *umsg, int flags);
int64_t sys_ioctl(syscall_frame_t *sf, int fd, uint64_t req, uint64_t arg);
int64_t sys_mmap(uint64_t addr, uint64_t len, uint64_t prot, uint64_t flags, int fd, uint64_t off);
extern uint64_t input_read_key(void);
int64_t sys_input_wait_key(void);
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
int64_t sys_setuid(uint32_t uid);
int64_t sys_setgid(uint32_t gid);
int64_t sys_setreuid(uint32_t ruid, uint32_t euid);
int64_t sys_setregid(uint32_t rgid, uint32_t egid);
int64_t sys_setresuid(uint32_t ruid, uint32_t euid, uint32_t suid);
int64_t sys_setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid);
int64_t sys_chown(const char *path, uint32_t uid, uint32_t gid);
int64_t sys_fchown(int fd, uint32_t uid, uint32_t gid);
int64_t sys_lchown(const char *path, uint32_t uid, uint32_t gid);
int64_t sys_fchownat(int dirfd, const char *path, uint32_t uid, uint32_t gid, int flags);
int64_t sys_set_tid_address(int *tidptr);
int64_t sys_arch_prctl(uint64_t code, uint64_t addr);
int64_t sys_uname(void *buf);
int64_t sys_gettimeofday(void *tv, void *tz);
int64_t sys_clock_gettime(int id, void *tp);
int64_t sys_clock_getres(int clkid, void *tp);
int64_t sys_getrandom(void *buf, uint64_t len, uint32_t flags);
int64_t sys_getcwd(char *buf, uint64_t size);
int64_t sys_chdir(const char *path);
int64_t sys_poll(syscall_frame_t *f, void *fds, uint64_t nfds, int timeout);
int64_t sys_ppoll(syscall_frame_t *f, void *fds, uint64_t nfds, const void *timeout_ts, const void *sigmask);
int64_t sys_select(syscall_frame_t *f, int nfds, void *rfds, void *wfds, void *efds, void *tv);
int64_t sys_pselect6(syscall_frame_t *f, int nfds, void *rfds, void *wfds, void *efds, const void *ts, const void *sigmask);
int64_t sys_sysinfo(void *ui);
int64_t sys_lstat(const char *path, void *st);
int64_t sys_openat(int dirfd, const char *path, int flags, int mode);
int64_t sys_newfstatat(int dirfd, const char *path, void *st, int flags);
int64_t sys_pipe2(int *fds, int flags);
int64_t sys_faccessat(int dirfd, const char *path, int mode);
int64_t sys_chmod(const char *path, uint32_t mode);
int64_t sys_fchmod(int fd, uint32_t mode);
int64_t sys_fchmodat(int dirfd, const char *path, uint32_t mode);
int64_t sys_getrlimit(int resource, void *rlim);
int64_t sys_prlimit64(int pid, int resource, const void *new_lim, void *old_lim);
int64_t sys_umask(uint64_t mask);
int64_t sys_prctl(int option, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_epoll_create1(int flags);
int64_t sys_epoll_ctl(int epfd, int op, int fd, void *event);
int64_t sys_epoll_wait(syscall_frame_t *f, int epfd, void *events, int maxevents, int timeout);
int64_t sys_ftruncate(int fd, uint64_t len);
int64_t sys_clone(syscall_frame_t *f, uint64_t flags, uint64_t child_stack, uint64_t ptid, uint64_t ctid, uint64_t tls);
int64_t sys_fork(syscall_frame_t *f);
int64_t sys_wait4(syscall_frame_t *f, int pid, int *status, int options, void *rusage);
int64_t sys_sched_yield(syscall_frame_t *f);
int64_t sys_getpriority(int which, int who);
int64_t sys_setpriority(int which, int who, int prio);
int64_t sys_execve(const char *path, const char *const *argv, const char *const *envp);
int64_t sys_futex(syscall_frame_t *f, uint32_t *uaddr, int op, uint32_t val, const void *timeout,
				  uint32_t *uaddr2, uint32_t val3);
int64_t sys_nanosleep(syscall_frame_t *f, const void *req, void *rem);
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
typedef struct sockaddr_nl {
	unsigned short nl_family;
	unsigned short nl_pad;
	uint32_t nl_pid;
	uint32_t nl_groups;
} sockaddr_nl_t;

static int user_copy_from(uint8_t *dst, uint64_t src, uint64_t len);
static int user_copy_to(uint64_t dst, const uint8_t *src, uint64_t len);

static int64_t socket_alloc_endpoint(struct unix_socket_state *s, int side) {
	int fd=find_free_fd(); if(fd<0)return-EMFILE;
	files[fd].in_use=1; files[fd].type=FD_SOCKET; files[fd].flags=0;
	files[fd].offset=0; files[fd].size=0; files[fd].data=NULL; files[fd].name=NULL; files[fd].boot_path[0]=0;
	files[fd].vnode=NULL; files[fd].shared_offset=NULL;
	files[fd].private_data=(void*)(((uintptr_t)s) | (uintptr_t)(side & 1));
	s->refs++;
	s->side_refs[side & 1]++;
	return fd;
}
static struct unix_socket_state *socket_state(struct file_desc *f){ return (struct unix_socket_state *)(f->private_data ? ((uintptr_t)f->private_data & ~1ULL) : 0); }
static int socket_side(struct file_desc *f){ return (int)((uintptr_t)f->private_data & 1ULL); }
static void socket_fd_drop_ref(struct file_desc *f) {
	if (!f || f->type != FD_SOCKET) return;
	struct unix_socket_state *s = socket_state(f);
	if (!s) return;
	int side = socket_side(f);
	if (side >= 0 && side < 2 && s->side_refs[side]) {
		s->side_refs[side]--;
		if (!s->side_refs[side]) {
			s->closed[side] = 1;
			debug_hex64("[X11 CLOSE-SIDE] side=0x", (uint64_t)side);
			debug_hex64(" refs=0x", (uint64_t)s->refs);
			debugcon_puts("\n");
		}
	}
	if (s->refs) s->refs--;
	if (!s->refs) {
		s->listener = 0;
		s->pending_count = 0;
	}
}

int64_t sys_socket(int domain, int type, int protocol) {
	int base_type = type & 0xf;
	int want_nonblock = (type & SOCK_NONBLOCK) != 0;

	if(domain==1 && base_type==SOCK_STREAM) {
		static int si;
		struct unix_socket_state *s=NULL;
		for(int k=0;k<UNIX_SOCKETS_MAX;k++){ int j=(si+k)%UNIX_SOCKETS_MAX; if(!unix_sockets[j].refs){ s=&unix_sockets[j]; si=(j+1)%UNIX_SOCKETS_MAX; break; } }
		if(!s) return -EMFILE;
		__builtin_memset(s, 0, sizeof(*s));
		s->domain=1;
		int fd = (int)socket_alloc_endpoint(s,0);
		if (fd >= 0 && want_nonblock) files[fd].flags |= SOCK_NONBLOCK;
		return fd;
	}

	if(domain==16 && base_type==2 && protocol==15) {
		static int ni;
		struct unix_socket_state *s=NULL;
		for(int k=0;k<UNIX_SOCKETS_MAX;k++){ int j=(ni+k)%UNIX_SOCKETS_MAX; if(!unix_sockets[j].refs){ s=&unix_sockets[j]; ni=(j+1)%UNIX_SOCKETS_MAX; break; } }
		if(!s) return -EMFILE;
		__builtin_memset(s, 0, sizeof(*s));
		s->domain=16; s->protocol=15;
		return socket_alloc_endpoint(s,0);
	}
	return -EAFNOSUPPORT;
}
int64_t sys_socketpair(int domain, int type, int protocol, int *sv) {
	(void)protocol;
	if(domain!=1 || (type & 0xf)!=1)return-EAFNOSUPPORT;
	if(!sv)return-EFAULT;
	static int si;
	struct unix_socket_state *s=&unix_sockets[si++%UNIX_SOCKETS_MAX];
	if(s->refs)return-EMFILE;
	__builtin_memset(s, 0, sizeof(*s));
	int a=socket_alloc_endpoint(s,0); if(a<0)return a;
	int b=socket_alloc_endpoint(s,1); if(b<0){files[a].in_use=0;files[a].private_data=NULL;s->refs=0;s->side_refs[0]=0;return b;}
	if(user_copy_to((uint64_t)sv,(uint8_t*)&a,sizeof(a)) || user_copy_to((uint64_t)sv+4,(uint8_t*)&b,sizeof(b))){files[a].in_use=files[b].in_use=0;s->refs=0;return-EFAULT;}
    debugcon_puts("[SOCKETPAIR CREATE]");
    debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid);
    debug_hex64(" table=0x", (uint64_t)(uint16_t)task_fd_table_id[current_task]);
    debug_hex64(" a=0x", (uint64_t)a); debug_hex64(" b=0x", (uint64_t)b);
    debug_hex64(" domain=0x", (uint64_t)(unsigned)domain); debug_hex64(" type=0x", (uint64_t)(unsigned)type);
    debugcon_puts("\n");
	return 0;
}
static int socket_valid_fd(int fd){ return fd>=0&&fd<MAX_FILES&&files[fd].in_use&&files[fd].type==FD_SOCKET; }
static int socket_un_copy_path(const sockaddr_t *addr, int addrlen, char *out, size_t cap){
	if(!addr||!out||cap<2)return-EFAULT;
	if(addrlen < (int)(sizeof(unsigned short)+1) || addrlen > (int)sizeof(sockaddr_un_t))return-EINVAL;
	sockaddr_un_t u; if(user_copy_from((uint8_t*)&u,(uint64_t)addr,sizeof(u)))return-EFAULT;
	if(u.sun_family!=1)return-EAFNOSUPPORT;

	if(u.sun_path[0]==0){
		size_t n=1;
		while(n<sizeof(u.sun_path) && u.sun_path[n])n++;
		if(n<=1 || n+1>=cap)return-EINVAL;
		out[0]='@';
		for(size_t i=1;i<n;i++)out[i]=u.sun_path[i];
		out[n]=0;
		return 0;
	}
	size_t n=0; while(n<sizeof(u.sun_path) && u.sun_path[n])n++;
	if(n==0 || n>=cap)return-EINVAL;
	for(size_t i=0;i<n;i++)out[i]=u.sun_path[i]; out[n]=0; return 0;
}
static int socket_find_listener(const char *path){
	for(int i=0;i<UNIX_SOCKETS_MAX;i++) if(unix_sockets[i].refs && unix_sockets[i].listener && unix_sockets[i].bound && pseudo_eq(unix_sockets[i].path,path)) return i;
	return -1;
}
int64_t sys_bind(int sockfd, const sockaddr_t *addr, int addrlen) {
	if(!socket_valid_fd(sockfd)) return -EBADF;
	struct unix_socket_state *s = socket_state(&files[sockfd]);
	if(!s) return -ENOTCONN;
	if(s->bound || s->connected) return -EINVAL;

	if(s->domain==16 && s->protocol==15) {
		if(!addr || addrlen < (int)sizeof(sockaddr_nl_t)) return -EINVAL;
		sockaddr_nl_t nl;
		if(user_copy_from((uint8_t*)&nl,(uint64_t)addr,sizeof(nl))) return -EFAULT;
		if(nl.nl_family != 16) return -EAFNOSUPPORT;
		s->netlink_pid = nl.nl_pid;
		s->netlink_groups = nl.nl_groups;
		s->bound = 1;
		return 0;
	}

	char path[108];
	int r = socket_un_copy_path(addr, addrlen, path, sizeof(path));
	if(r < 0) return r;

	for(int i=0;i<UNIX_SOCKETS_MAX;i++) {
		if(unix_sockets[i].refs && unix_sockets[i].bound &&
		   pseudo_eq(unix_sockets[i].path, path))
			return -EADDRINUSE;
	}

	struct vfs_node *vn = NULL;
	int is_abstract = (path[0] == '@');
	if(!is_abstract){

		vn = vfs_lookup(path);
		if(vn) {
			if(vfs_type(vn) != VFS_NODE_SOCKET) return -EADDRINUSE;
		} else {
			vn = vfs_create_socket(path, 0777);
			if(!vn) return -EIO;
		}

		files[sockfd].vnode = vn;
	} else {

		files[sockfd].vnode = NULL;
	}

	size_t n = 0;
	while(path[n] && n < sizeof(s->path)-1) { s->path[n] = path[n]; n++; }
	s->path[n] = 0;
	s->bound = 1;
	if(!kernel_strncmp(path,"/tmp/.X11-unix/",15)) {
		debugcon_puts("[X11 BIND OK] fd=0x"); debug_hex64("0x",(uint64_t)sockfd);
		debugcon_puts(" path="); debugcon_puts(path); debugcon_puts("\n");
	}
	return 0;
}
int64_t sys_getsockname(int sockfd, sockaddr_t *addr, int *addrlen) {
	if(!socket_valid_fd(sockfd)) return -EBADF;
	if(!addrlen) return -EFAULT;
	struct unix_socket_state *s=socket_state(&files[sockfd]);
	if(!s) return -ENOTCONN;
	int cap=0;
	if(user_copy_from((uint8_t*)&cap,(uint64_t)addrlen,sizeof(cap))) return -EFAULT;
	if(cap<0) return -EINVAL;
	if(s->domain==16 && s->protocol==15) {
		if(cap < (int)sizeof(sockaddr_nl_t) || !addr) return -EINVAL;
		sockaddr_nl_t nl={0};
		nl.nl_family=16; nl.nl_pid=s->netlink_pid; nl.nl_groups=s->netlink_groups;
		if(user_copy_to((uint64_t)addr,(const uint8_t*)&nl,sizeof(nl))) return -EFAULT;
		int n=(int)sizeof(nl);
		if(user_copy_to((uint64_t)addrlen,(const uint8_t*)&n,sizeof(n))) return -EFAULT;
		return 0;
	}
	if(s->domain==1) {
		int n=0;
		if(s->path[0]) { n=(int)(sizeof(unsigned short)); for(size_t i=0;i<sizeof(s->path)&&s->path[i];i++) n++; n++; }
		if(!addr || cap<n) return -EINVAL;
		sockaddr_un_t u={0}; u.sun_family=1;
		for(size_t i=0;i+1<sizeof(u.sun_path)&&s->path[i];i++) u.sun_path[i]=s->path[i];
		if(user_copy_to((uint64_t)addr,(const uint8_t*)&u,(uint64_t)(n?n:sizeof(unsigned short)))) return -EFAULT;
		if(user_copy_to((uint64_t)addrlen,(const uint8_t*)&n,sizeof(n))) return -EFAULT;
		return 0;
	}
	return -EAFNOSUPPORT;
}
int64_t sys_getpeername(int sockfd, sockaddr_t *addr, int *addrlen) {
	if(!socket_valid_fd(sockfd)) return -EBADF;
	if(!addrlen) return -EFAULT;
	struct unix_socket_state *s=socket_state(&files[sockfd]);
	if(!s || !s->connected) return -ENOTCONN;
	int cap=0; if(user_copy_from((uint8_t*)&cap,(uint64_t)addrlen,sizeof(cap))) return -EFAULT;
	if(!addr || cap < (int)sizeof(unsigned short)+1) return -EINVAL;
	sockaddr_un_t u={0}; u.sun_family=1;
	if(s->path[0]) for(size_t i=0;i+1<sizeof(u.sun_path)&&s->path[i];i++) u.sun_path[i]=s->path[i];
	int n=(int)sizeof(unsigned short); while(n-(int)sizeof(unsigned short)<(int)sizeof(u.sun_path)-1 && u.sun_path[n-sizeof(unsigned short)]) n++; n++;
	if(n>(int)sizeof(u)) n=(int)sizeof(u); if(cap<n) return -EINVAL;
	if(user_copy_to((uint64_t)addr,(const uint8_t*)&u,(uint64_t)n)) return -EFAULT;
	if(user_copy_to((uint64_t)addrlen,(const uint8_t*)&n,sizeof(n))) return -EFAULT; return 0;
}

int64_t sys_listen(int sockfd, int backlog) {
	if(!socket_valid_fd(sockfd))return-EBADF; if(backlog<0)return-EINVAL;
	struct unix_socket_state *s=socket_state(&files[sockfd]); if(!s)return-ENOTCONN; if(s->connected)return-EINVAL; if(!s->bound)return-EINVAL;
	s->listener=1; s->pending_count=0; for(int i=0;i<4;i++)s->pending_fd[i]=-1;
	if(s->path[0] && (!kernel_strncmp(s->path,"/tmp/.X11-unix/",15))) {
		debugcon_puts("[X11 LISTEN OK] fd=0x"); debug_hex64("0x",(uint64_t)sockfd);
		debugcon_puts(" path="); debugcon_puts(s->path); debugcon_puts("\n");
	}
	return 0;
}

static int x11_dbg_sock_budget = 32;
static void x11_sock_dbg(const char *tag, int fd, int n) {
    if (x11_dbg_sock_budget <= 0) return;
    x11_dbg_sock_budget--;
    debugcon_puts(tag);
    debug_hex64(" fd=0x", (uint64_t)fd);
    debug_hex64(" n=0x", (uint64_t)n);
    debugcon_puts("\n");
}
static void x11_sock_path_dbg(const char *tag, const char *path) {
    if (x11_dbg_sock_budget <= 0) return;
    x11_dbg_sock_budget--;
    debugcon_puts(tag);
    debugcon_puts(" ");
    debugcon_puts(path ? path : "<null>");
    debugcon_puts("\n");
}

int64_t sys_connect(int sockfd, const sockaddr_t *addr, int addrlen) {
	x11_sock_dbg("[X11 CONNECT]", sockfd, addrlen);
	if(!socket_valid_fd(sockfd))return-EBADF;
	struct unix_socket_state *old=socket_state(&files[sockfd]); if(!old)return-ENOTCONN;
	char path[108]; int r=socket_un_copy_path(addr,addrlen,path,sizeof(path)); if(r<0){ debug_hex64("[X11 CONNECT] copy_path rc=0x", (uint64_t)(int64_t)r); debugcon_puts("\n"); return r; }
	x11_sock_path_dbg("[X11 CONNECT PATH]", path);
	int li=socket_find_listener(path);
	debug_hex64("[X11 CONNECT LISTENER] index=0x", (uint64_t)(int64_t)li); debugcon_puts("\n");
	if(li<0)return-ECONNREFUSED;
	struct unix_socket_state *ls=&unix_sockets[li]; if(ls->pending_count>=4)return-EAGAIN;
	static int si; int ni=-1; for(int k=0;k<UNIX_SOCKETS_MAX;k++){int j=(si+k)%UNIX_SOCKETS_MAX;if(j!=li&&!unix_sockets[j].refs){ni=j;si=(j+1)%UNIX_SOCKETS_MAX;break;}} if(ni<0)return-EMFILE;
	struct unix_socket_state *ns=&unix_sockets[ni];
	__builtin_memset(ns, 0, sizeof(*ns));

	int ns_index = (int)(ns - unix_sockets);

	socket_fd_drop_ref(&files[sockfd]);
	files[sockfd].in_use=1;
	files[sockfd].type=FD_SOCKET;
	files[sockfd].flags=0;
	files[sockfd].offset=0;
	files[sockfd].size=0;
	files[sockfd].data=NULL;
	files[sockfd].name=NULL;
	files[sockfd].boot_path[0]=0;
	files[sockfd].vnode=NULL;
	files[sockfd].shared_offset=NULL;
	files[sockfd].private_data=(void*)ns;
	ns->refs=2;
	ns->side_refs[0]=1;
	ns->side_refs[1]=1;
	ns->connected=1;
	ls->pending_fd[ls->pending_count++]=ns_index;
	task_wake_io_waiters();
	debug_hex64("[X11 CONNECT OK] pending=0x", (uint64_t)ls->pending_count); debugcon_puts("\n");
	return 0;
}
int64_t sys_accept(syscall_frame_t *sf, int sockfd, sockaddr_t *addr, int *addrlen) {
	x11_sock_dbg("[X11 ACCEPT]", sockfd, 0);
	if(!socket_valid_fd(sockfd))return-EBADF;
	struct unix_socket_state *ls=socket_state(&files[sockfd]);
	if(!ls||!ls->listener)return-EINVAL;
	if(ls->pending_count==0){
		if(files[sockfd].flags&0x800)return-EAGAIN;

		if(task_block_io(sf, IO_WAIT_SOCKET_ACCEPT, 43)==0) return 0;
		return -EAGAIN;
	}

	int si=ls->pending_fd[0];
	if(si<0 || si>=UNIX_SOCKETS_MAX || unix_sockets[si].refs==0) return -EINVAL;
	struct unix_socket_state *s=&unix_sockets[si];

	int fd=find_free_fd();
	if(fd<0) { debug_hex64("[X11 ACCEPT FAIL] rc=0x", (uint64_t)(int64_t)-EMFILE); debugcon_puts("\n"); return -EMFILE; }
	files[fd].in_use=1; files[fd].type=FD_SOCKET; files[fd].flags=0;
	files[fd].offset=0; files[fd].size=0; files[fd].data=NULL; files[fd].name=NULL; files[fd].boot_path[0]=0;
	files[fd].vnode=NULL; files[fd].shared_offset=NULL;
	files[fd].private_data=(void*)(((uintptr_t)s)|1ULL);
	debug_hex64("[X11 ACCEPT OK] fd=0x", (uint64_t)(int64_t)fd); debugcon_puts("\n");

	if(addr && addrlen){
		int al=0;
		if(user_copy_to((uint64_t)addrlen,(uint8_t*)&al,sizeof(al))) return -EFAULT;
	}

	for(int i=1;i<ls->pending_count;i++)
		ls->pending_fd[i-1]=ls->pending_fd[i];
	ls->pending_count--;

	s->connected=1;
	return fd;
}
int64_t sys_accept4(syscall_frame_t *sf, int sockfd, sockaddr_t *addr, int *addrlen, int flags) {
	(void)flags;
	return sys_accept(sf, sockfd, addr, addrlen);
}
int64_t sys_sendto(syscall_frame_t *sf, int sockfd, const void *buf, int len, int flags, const sockaddr_t *dest_addr, int addrlen, uint64_t wait_nr) {
	if (!sf) return -EFAULT;
	x11_sock_dbg("[X11 WRITE]", sockfd, len);
	(void)flags;(void)dest_addr;(void)addrlen;
	if(sockfd<0||sockfd>=MAX_FILES||!files[sockfd].in_use||files[sockfd].type!=FD_SOCKET)return-EBADF;
	if(len<0)return-EINVAL; struct unix_socket_state*s=socket_state(&files[sockfd]); int side=socket_side(&files[sockfd]), peer=side^1;
	if(!s||s->refs<2)return-ENOTCONN; if(s->closed[peer])return-EPIPE; if(!len)return 0;
	for(;;){
		uint32_t room=SOCK_CAP-s->count[peer];
		if(room)break;
		if(files[sockfd].flags&0x800)return-EAGAIN;
		if(s->closed[peer])return-EPIPE;

		if (task_block_io(sf, IO_WAIT_SOCKET_WRITE, wait_nr) == 0) return 0;
		return -EAGAIN;
	}
	uint32_t room=SOCK_CAP-s->count[peer];
	uint32_t n=(uint32_t)len<room?(uint32_t)len:room; uint8_t tmp[256]; uint32_t done=0;
	while(done<n){uint32_t k=n-done;if(k>sizeof(tmp))k=sizeof(tmp);if(user_copy_from(tmp,(uint64_t)buf+done,k))return done?done:-EFAULT;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
		if(done==0 && k>=4){
			uint8_t op=tmp[0];

			if(op==2 || op==8 || op==12 || op==24 || op==42 || op==43 ||
			   op==53 || op==54 || op==55 || op==60 || op==61 || op==72 || op==93){
				debug_hex64("[X11 WM/CORE TX] pid=0x",(uint64_t)tasks[current_task].pid);
				debug_hex64(" fd=0x",(uint64_t)sockfd);
				debug_hex64(" op=0x",(uint64_t)op);
				debug_hex64(" b1=0x",(uint64_t)tmp[1]);
				debug_hex64(" len16=0x",(uint64_t)tmp[2]|((uint64_t)tmp[3]<<8));
				debugcon_puts("\n");
			}
		}
#endif
		for(uint32_t i=0;i<k;i++){s->data[peer][s->wpos[peer]]=tmp[i];s->wpos[peer]=(s->wpos[peer]+1)%SOCK_CAP;s->count[peer]++;}done+=k;}

	int woke = task_wake_io_waiters();
	if (woke >= 0 && woke != current_task) {

		sf->rax = (uint64_t)(int64_t)done;
		task_save_from_frame(&tasks[current_task], sf);
		task_return_ctx = &tasks[woke].ctx;
		task_switch_target = woke;
		task_switch_deferred = 1;
		task_switch_pending = 1;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
		if (tasks[woke].pid == 0x64 || tasks[woke].pid == 0x65 || tasks[woke].pid == 0x68) {
			debug_hex64("[SOCK-DEFER-SAVE] writer_pid=0x", (uint64_t)tasks[current_task].pid);
			debug_hex64(" rip=0x", tasks[current_task].ctx.rip);
			debug_hex64(" rsp=0x", tasks[current_task].ctx.rsp);
			debug_hex64(" rax=0x", tasks[current_task].ctx.rax);
			debug_hex64(" target_pid=0x", (uint64_t)tasks[woke].pid);
			debug_hex64(" target_rip=0x", tasks[woke].ctx.rip);
			debug_hex64(" target_rsp=0x", tasks[woke].ctx.rsp);
			debug_hex64(" target_cr3=0x", tasks[woke].ctx.cr3);
			debugcon_puts("\n");
		}
#endif
	}
	return done;
}
int64_t sys_recvfrom(syscall_frame_t *sf, int sockfd, void *buf, int len, int flags, sockaddr_t *src_addr, int *addrlen, uint64_t wait_nr) {
	if (!sf) return -EFAULT;
	x11_sock_dbg("[X11 READ]", sockfd, len);
	(void)flags;(void)src_addr;(void)addrlen;
	if(sockfd<0||sockfd>=MAX_FILES||!files[sockfd].in_use||files[sockfd].type!=FD_SOCKET)return-EBADF;
	if(len<0)return-EINVAL; struct unix_socket_state*s=socket_state(&files[sockfd]); int side=socket_side(&files[sockfd]);
	if(!s)return-ENOTCONN; if(!len)return 0;
	for(;;){
		if(s->count[side])break;
		if(s->closed[side^1])return 0;
		if(files[sockfd].flags&0x800)return-EAGAIN;

		if (task_block_io(sf, IO_WAIT_SOCKET_READ, wait_nr) == 0) return 0;
		return -EAGAIN;
	}
	uint32_t n=(uint32_t)len<s->count[side]?(uint32_t)len:s->count[side]; uint8_t tmp[256]; uint32_t done=0;
	while(done<n){uint32_t k=n-done;if(k>sizeof(tmp))k=sizeof(tmp);for(uint32_t i=0;i<k;i++){tmp[i]=s->data[side][s->rpos[side]];s->rpos[side]=(s->rpos[side]+1)%SOCK_CAP;s->count[side]--;}if(user_copy_to((uint64_t)buf+done,tmp,k))return done?done:-EFAULT;done+=k;}
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
	if (x11_dbg_sock_budget > 0) {
		debug_hex64("[X11 READ RET] pid=0x", (uint64_t)tasks[current_task].pid);
		debug_hex64(" fd=0x", (uint64_t)sockfd);
		debug_hex64(" ret=0x", (uint64_t)done);
		debug_hex64(" remain=0x", (uint64_t)s->count[side]);
		debugcon_puts("\n");
	}
	if ((done==8 || done==12 || done>=128) && (uint64_t)tasks[current_task].pid != 0) {
		debug_hex64("[X11 READ WATCH] pid=0x", (uint64_t)tasks[current_task].pid);
		debug_hex64(" fd=0x", (uint64_t)sockfd);
		debug_hex64(" ret=0x", (uint64_t)done);
		debug_hex64(" remain=0x", (uint64_t)s->count[side]);
		debugcon_puts("\n");
	}
#endif

	task_wake_io_waiters();
	return done;
}

typedef struct {
    uint64_t iov_base;
    uint64_t iov_len;
} yabro_iovec_k;

typedef struct {
    uint64_t msg_name;
    uint32_t msg_namelen;
    uint32_t __pad0;
    uint64_t msg_iov;
    uint64_t msg_iovlen;
    uint64_t msg_control;
    uint64_t msg_controllen;
    uint32_t msg_flags;
    uint32_t __pad1;
} yabro_msghdr_k;

static int socket_msg_copy_in(const yabro_msghdr_k *mh, yabro_iovec_k *iov) {
    if (!mh || !iov || mh->msg_iovlen > 64) return -EINVAL;
    if (mh->msg_iovlen == 0) return 0;
    if (!mh->msg_iov) return -EFAULT;
    if (user_copy_from((uint8_t *)iov, mh->msg_iov, mh->msg_iovlen * sizeof(iov[0])))
        return -EFAULT;
    for (uint64_t i=0; i<mh->msg_iovlen; ++i) {
        if (iov[i].iov_len > (1ULL<<20) || iov[i].iov_base >= USER_LIMIT)
            return -EFAULT;
    }
    return 0;
}

int64_t sys_recvmsg(syscall_frame_t *sf, int sockfd, void *umsg, int flags) {
    if (sockfd < 0 || sockfd >= MAX_FILES || !files[sockfd].in_use || files[sockfd].type != FD_SOCKET)
        return -EBADF;
    struct unix_socket_state *s = socket_state(&files[sockfd]);
    int side = socket_side(&files[sockfd]);
    if (!s) return -ENOTCONN;

    yabro_msghdr_k mh;
    if (!umsg || user_copy_from((uint8_t *)&mh, (uint64_t)umsg, sizeof(mh))) return -EFAULT;
    yabro_iovec_k iov[64];
    int irc = socket_msg_copy_in(&mh, iov);
    if (irc < 0) return irc;

    uint64_t total = 0;
    for (uint64_t i=0; i<mh.msg_iovlen; ++i) {
        if (total > (1ULL<<24) - iov[i].iov_len) return -EINVAL;
        total += iov[i].iov_len;
    }
    if (!total) return 0;

    if (!s->count[side]) {
        if (s->closed[side ^ 1]) return 0;
        if ((files[sockfd].flags & SOCK_NONBLOCK) || (flags & MSG_DONTWAIT)) return -EAGAIN;
        if (task_block_io(sf, IO_WAIT_SOCKET_READ, 47) == 0) return 0;
        return -EAGAIN;
    }

    uint64_t done = 0;
    for (uint64_t i=0; i<mh.msg_iovlen && done<total; ++i) {
        uint64_t off = 0;
        while (off < iov[i].iov_len && s->count[side]) {
            uint64_t n = iov[i].iov_len - off;
            if (n > s->count[side]) n = s->count[side];
            if (n > 256) n = 256;
            uint8_t tmp[256];
            for (uint64_t j=0; j<n; ++j) {
                tmp[j] = s->data[side][s->rpos[side]];
                s->rpos[side] = (s->rpos[side] + 1) % SOCK_CAP;
                s->count[side]--;
            }
            if (user_copy_to(iov[i].iov_base + off, tmp, n)) return done ? (int64_t)done : -EFAULT;
            off += n; done += n;
        }
    }

    mh.msg_flags = 0;
    if (mh.msg_control && mh.msg_controllen) {

        uint64_t zero = 0;
        (void)zero;
        mh.msg_controllen = 0;
    }
    if (user_copy_to((uint64_t)umsg, (const uint8_t *)&mh, sizeof(mh))) return -EFAULT;
    task_wake_io_waiters();
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
    debug_hex64("[X11 RECVMSG] fd=0x", (uint64_t)sockfd);
    debug_hex64(" len=0x", done); debugcon_puts("\n");
#endif
    return (int64_t)done;
}

int64_t sys_sendmsg(syscall_frame_t *sf, int sockfd, const void *umsg, int flags) {
    if (sockfd < 0 || sockfd >= MAX_FILES || !files[sockfd].in_use || files[sockfd].type != FD_SOCKET)
        return -EBADF;
    struct unix_socket_state *s = socket_state(&files[sockfd]);
    int side = socket_side(&files[sockfd]), peer = side ^ 1;
    if (!s || s->refs < 2) return -ENOTCONN;
    yabro_msghdr_k mh;
    if (!umsg || user_copy_from((uint8_t *)&mh, (uint64_t)umsg, sizeof(mh))) return -EFAULT;
    yabro_iovec_k iov[64];
    int irc = socket_msg_copy_in(&mh, iov);
    if (irc < 0) return irc;

    uint64_t total = 0;
    for (uint64_t i=0; i<mh.msg_iovlen; ++i) {
        if (total > (1ULL<<24) - iov[i].iov_len) return -EINVAL;
        total += iov[i].iov_len;
    }
    if (!total) return 0;
    if (s->closed[peer]) return -EPIPE;

    while ((uint64_t)(SOCK_CAP - s->count[peer]) < total) {
        if ((files[sockfd].flags & SOCK_NONBLOCK) || (flags & MSG_DONTWAIT)) return -EAGAIN;
        if (task_block_io(sf, IO_WAIT_SOCKET_WRITE, 46) == 0) return 0;
        return -EAGAIN;
    }

    uint64_t sent = 0;
    for (uint64_t i=0; i<mh.msg_iovlen; ++i) {
        uint64_t off = 0;
        while (off < iov[i].iov_len) {
            uint64_t n = iov[i].iov_len - off;
            if (n > 256) n = 256;
            uint8_t tmp[256];
            if (user_copy_from(tmp, iov[i].iov_base + off, n)) return sent ? (int64_t)sent : -EFAULT;
            for (uint64_t j=0; j<n; ++j) {
                s->data[peer][s->wpos[peer]] = tmp[j];
                s->wpos[peer] = (s->wpos[peer] + 1) % SOCK_CAP;
                s->count[peer]++;
            }
            off += n; sent += n;
        }
    }
    int woke = task_wake_io_waiters();
    if (woke >= 0 && woke != current_task) {
        sf->rax = (uint64_t)(int64_t)sent;
        task_save_from_frame(&tasks[current_task], sf);
        task_return_ctx = &tasks[woke].ctx;
        task_switch_target = woke;
        task_switch_deferred = 1;
        task_switch_pending = 1;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
        if (tasks[woke].pid == 0x64 || tasks[woke].pid == 0x65 || tasks[woke].pid == 0x68) {
            debug_hex64("[SOCK-DEFER-SAVE] writer_pid=0x", (uint64_t)tasks[current_task].pid);
            debug_hex64(" rip=0x", tasks[current_task].ctx.rip);
            debug_hex64(" rsp=0x", tasks[current_task].ctx.rsp);
            debug_hex64(" rax=0x", tasks[current_task].ctx.rax);
            debug_hex64(" target_pid=0x", (uint64_t)tasks[woke].pid);
            debug_hex64(" target_rip=0x", tasks[woke].ctx.rip);
            debug_hex64(" target_rsp=0x", tasks[woke].ctx.rsp);
            debug_hex64(" target_cr3=0x", tasks[woke].ctx.cr3);
            debugcon_puts("\n");
        }
#endif
    }
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
    debug_hex64("[X11 SENDMSG] fd=0x", (uint64_t)sockfd);
    debug_hex64(" len=0x", sent); debugcon_puts("\n");
#endif
    return (int64_t)sent;
}

int64_t sys_shutdown(int sockfd, int how) {
	if(sockfd<0||sockfd>=MAX_FILES||!files[sockfd].in_use||files[sockfd].type!=FD_SOCKET)return-EBADF;
	if(how<0||how>2)return-EINVAL; struct unix_socket_state*s=socket_state(&files[sockfd]); if(!s)return-ENOTCONN; if(how==1||how==2)s->closed[socket_side(&files[sockfd])]=1; task_wake_io_waiters(); return 0;
}
int64_t sys_setsockopt(int sockfd, int level, int optname, const void *optval, int optlen) {
    if (!socket_valid_fd(sockfd)) return -EBADF;
    if (level != SOL_SOCKET) return -ENOPROTOOPT;
    if (optlen < 0) return -EINVAL;
    switch (optname) {
        case SO_REUSEADDR:
        case SO_KEEPALIVE:
        case SO_SNDBUF:
        case SO_RCVBUF:
            return 0;
        default:
            return -ENOPROTOOPT;
    }
}
int64_t sys_getsockopt(int sockfd, int level, int optname, void *optval, int *optlen) {
    if (!socket_valid_fd(sockfd)) return -EBADF;
    if (level != SOL_SOCKET) return -ENOPROTOOPT;
    if (!optlen) return -EFAULT;
    int olen = 0;
    if (user_copy_from((uint8_t *)&olen, (uint64_t)optlen, sizeof(olen))) return -EFAULT;
    if (olen < 0) return -EINVAL;
    int32_t value = 0;
    switch (optname) {
        case SO_ERROR: value = 0; break;
        case SO_TYPE: value = SOCK_STREAM; break;
        case SO_SNDBUF: value = SOCK_CAP; break;
        case SO_RCVBUF: value = SOCK_CAP; break;
        case SO_KEEPALIVE: value = 0; break;
        default: return -ENOPROTOOPT;
    }
    if (olen > 0) {
        int copy_len = olen < (int)sizeof(value) ? olen : (int)sizeof(value);
        if (!optval || user_copy_to((uint64_t)optval, (const uint8_t *)&value, (uint64_t)copy_len)) return -EFAULT;
        olen = copy_len;
    } else {
        olen = sizeof(value);
    }
    return user_copy_to((uint64_t)optlen, (const uint8_t *)&olen, sizeof(olen)) ? -EFAULT : 0;
}

static int user_copy_from(uint8_t *dst, uint64_t src, uint64_t len);
static int user_copy_to(uint64_t dst, const uint8_t *src, uint64_t len);
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SIGUSR1 10
#define SIGTERM 15
static int sig_valid(int s){return s>0&&s<=64;}
static uint64_t sig_bit(int s){return 1ULL<<((s-1)&63);} static int sig_word(int s){return (s-1)>>6;}
static int sig_pending_set(task_t*t,int s){if(!sig_valid(s))return-EINVAL;t->sig_pending[sig_word(s)]|=sig_bit(s);return 0;}
static int sig_is_pending(task_t*t,int s){return (t->sig_pending[sig_word(s)]&sig_bit(s))!=0;}
static int sig_is_blocked(task_t*t,int s){return (t->sig_blocked[sig_word(s)]&sig_bit(s))!=0;}
static void sig_clear_pending(task_t*t,int s){t->sig_pending[sig_word(s)]&=~sig_bit(s);}
static int sig_next_deliverable(task_t*t){for(int s=1;s<=64;s++)if(sig_is_pending(t,s)&&!sig_is_blocked(t,s))return s;return 0;}
int64_t sys_rt_sigaction(int sig,const sigaction_t*act,sigaction_t*oldact,uint64_t sz){
	task_init_root();
	if(!sig_valid(sig)||sig==9||sz!=8)return-EINVAL;
	task_t*t=&tasks[current_task];
	if(oldact){
		if(!user_range_ok((uint64_t)oldact,sizeof(sigaction_t),1))return-EFAULT;
		if(user_copy_to((uint64_t)oldact,(const uint8_t*)&t->sig_actions[sig],sizeof(sigaction_t)))return-EFAULT;
	}
	if(act){
		if(!user_range_ok((uint64_t)act,sizeof(sigaction_t),0))return-EFAULT;
		sigaction_t a;
		if(user_copy_from((uint8_t*)&a,(uint64_t)act,sizeof(a)))return-EFAULT;
		t->sig_actions[sig]=a;
	}
	return 0;
}
int64_t sys_rt_sigprocmask(int how,const sigset_t*set,sigset_t*oldset,uint64_t sz){
	task_init_root();
	if(sz!=8||how<0||how>2)return-EINVAL;
	task_t*t=&tasks[current_task];
	uint64_t old[1]={t->sig_blocked[0]};
	if(oldset){if(!user_range_ok((uint64_t)oldset,8,1))return-EFAULT;if(user_copy_to((uint64_t)oldset,(const uint8_t*)old,8))return-EFAULT;}
	if(set){uint64_t in=0;if(!user_range_ok((uint64_t)set,8,0))return-EFAULT;if(user_copy_from((uint8_t*)&in,(uint64_t)set,8))return-EFAULT;if(how==SIG_BLOCK)t->sig_blocked[0]|=in;else if(how==SIG_UNBLOCK)t->sig_blocked[0]&=~in;else t->sig_blocked[0]=in;}
	return 0;
}
int64_t sys_rt_sigpending(sigset_t*set,uint64_t sz){
	task_init_root();
	if(!set||sz!=8||!user_range_ok((uint64_t)set,8,1))return-EFAULT;
	uint64_t p=tasks[current_task].sig_pending[0];
	return user_copy_to((uint64_t)set,(const uint8_t*)&p,8)?-EFAULT:0;
}

typedef struct { uint64_t ss_sp; int32_t ss_flags; int32_t _pad; uint64_t ss_size; } yabro_stack_t;
int64_t sys_sigaltstack(const yabro_stack_t *ss,const yabro_stack_t *old){
	if(ss && !user_range_ok((uint64_t)ss,sizeof(*ss),0))return-EFAULT;
	if(old){
		if(!user_range_ok((uint64_t)old,sizeof(*old),1))return-EFAULT;
		yabro_stack_t cur={0,0,0,0};
		if(user_copy_to((uint64_t)old,(const uint8_t*)&cur,sizeof(cur)))return-EFAULT;
	}
	return 0;
}

int64_t sys_madvise(uint64_t addr,uint64_t len,int advice){
	(void)addr; (void)len; (void)advice;
	return 0;
}

int64_t sys_kill(int pid,int sig){task_init_root();if((!sig_valid(sig))&&sig!=0)return-EINVAL;for(int i=0;i<MAX_TASKS;i++)if(tasks[i].used){if((pid>0&&(tasks[i].pid==pid||tasks[i].tgid==pid))||(pid==0&&tasks[i].tgid==tasks[current_task].tgid)){if(sig==0)return 0;return sig_pending_set(&tasks[i],sig);}}return-ESRCH;}
int64_t sys_tgkill(int tgid,int tid,int sig){task_init_root();if((!sig_valid(sig))&&sig!=0)return-EINVAL;for(int i=0;i<MAX_TASKS;i++)if(tasks[i].used&&tasks[i].tgid==tgid&&tasks[i].pid==tid){if(sig==0)return 0;return sig_pending_set(&tasks[i],sig);}return-ESRCH;}
static void deliver_pending_signal_to_task(task_t *t, syscall_frame_t *f){
	if(!t || !f || t->sig_active)return;
	int sig=sig_next_deliverable(t);
	if(!sig)return;
	sighandler_t h=t->sig_actions[sig].sa_handler;
	if(h==SIG_IGN){sig_clear_pending(t,sig);return;}
	if(h==SIG_DFL && (sig==SIGCHLD || sig==SIGWINCH)){sig_clear_pending(t,sig);return;}
	if(h==SIG_DFL || (uint64_t)h>=USER_LIMIT){

		if(sig!=SIGINT) sig_clear_pending(t,sig);
		t->exit_status=128+sig; t->state=TASK_ZOMBIE; return;
	}

	uint64_t rest=(uint64_t)t->sig_actions[sig].sa_restorer;
	if(!rest) return;
	uint64_t rsp=f->user_rsp;
	if(rsp<8 || !user_range_ok(rsp-8,8,1))return;

	t->sig_saved_frame=*f;
	t->sig_saved_valid=1;
	t->sig_active=1;
	if(user_copy_to(rsp-8,(const uint8_t*)&rest,8)){
		t->sig_saved_valid=0; t->sig_active=0; return;
	}
	sig_clear_pending(t,sig);
	f->user_rsp=rsp-8;
	f->user_rip=(uint64_t)h;
	f->rdi=(uint64_t)sig;
	f->rax=0;
}
static void deliver_pending_signal(syscall_frame_t*f){deliver_pending_signal_to_task(&tasks[current_task],f);}
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

static int user_copy_from_cr3(uint64_t cr3, uint8_t *dst, uint64_t src, uint64_t len) {
	if (!cr3 || !dst) return -EFAULT;
	while (len) {
		uint64_t phys, pte;
		uint64_t page = src & PAGE_MASK, off = src & (PAGE_SIZE-1);
		uint64_t n = PAGE_SIZE - off; if (n > len) n = len;
		if (vmm_user_translate_cr3(cr3, page, &phys, &pte) != 0 || !(pte & PTE_U)) return -EFAULT;
		uint8_t *sp = (uint8_t*)pmm_phys_to_virt(phys);
		if (!sp) return -EFAULT;
		for (uint64_t i=0;i<n;i++) dst[i]=sp[off+i];
		dst += n; src += n; len -= n;
	}
	return 0;
}

static int user_copy_to_cr3(uint64_t cr3, uint64_t dst, const uint8_t *src, uint64_t len) {
	if (!cr3 || (!src && len)) return -EFAULT;
	while (len) {
		uint64_t phys, pte;
		uint64_t page = dst & PAGE_MASK, off = dst & (PAGE_SIZE-1);
		uint64_t n = PAGE_SIZE - off; if (n > len) n = len;
		if (vmm_user_translate_cr3(cr3, page, &phys, &pte) != 0 || !(pte & PTE_U)) return -EFAULT;
		if (!(pte & PTE_W)) {

			if (!(pte & PTE_COW) || vmm_user_cow_fault(cr3, page) != 0) return -EFAULT;
			if (vmm_user_translate_cr3(cr3, page, &phys, &pte) != 0 ||
			    !(pte & PTE_U) || !(pte & PTE_W)) return -EFAULT;
		}
		uint8_t *dp = (uint8_t*)pmm_phys_to_virt(phys);
		if (!dp) return -EFAULT;
		for (uint64_t i=0;i<n;i++) dp[off+i]=src[i];
		dst += n; src += n; len -= n;
	}
	return 0;
}

static int user_copy_from(uint8_t *dst, uint64_t src, uint64_t len) {
	uint64_t cr3 = syscall_user_cr3;
	if (!cr3) cr3 = vmm_user_cr3();
	return user_copy_from_cr3(cr3, dst, src, len);
}

static int user_copy_to(uint64_t dst, const uint8_t *src, uint64_t len) {
	uint64_t cr3 = syscall_user_cr3;
	if (!cr3) cr3 = vmm_user_cr3();
	return user_copy_to_cr3(cr3, dst, src, len);
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
	struct file_desc *d=&files[fd];
	if(fd==0 && current_task>=0 && current_task<MAX_TASKS && !fd0_read_diag_logged[current_task]) {
		fd_trace_slot("[FD0 READ]", fd, d); debug_hex64(" count=0x", count); debugcon_puts("\n");
		fd0_read_diag_logged[current_task]=1;
	}
	if(d->type!=FD_PIPE_R)return sys_read(f,fd,buf,count);
	struct pipe_state *p=(struct pipe_state*)d->private_data; if(!count)return 0;
	if(p && !p->debug_read_enter_logged) {
		debugcon_puts("[PIPE READ ENTER]"); debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid);
		debug_hex64(" fd=0x", (uint64_t)fd); debug_hex64(" pipe=0x", (uint64_t)(uintptr_t)p);
		debug_hex64(" count=0x", count); debug_hex64(" queued=0x", (uint64_t)p->count);
		debug_hex64(" writers=0x", (uint64_t)p->writers); debug_hex64(" flags=0x", d->flags);
		debugcon_puts("\n"); p->debug_read_enter_logged=1;
	}
	if(p->count) return sys_read(f,fd,buf,count);
	if(!p->writers) return 0;
	if(d->flags & 0x800) return -EAGAIN;
	task_t *t=&tasks[current_task]; task_save_from_frame(t,f); t->pipe_wait_fd=fd; t->pipe_wait_buf=(uint64_t)buf; t->pipe_wait_count=count; t->state=TASK_BLOCKED;
	int next=task_pick_next(); if(next<0){t->state=TASK_RUNNING;t->pipe_wait_fd=-1;t->pipe_wait_buf=0;t->pipe_wait_count=0;return-EAGAIN;}
	tasks[next].state=TASK_RUNNING; current_task=next; task_account_switch(next); (void)vmm_user_activate(tasks[next].ctx.cr3); task_switch_target=next; task_switch_pending=1; task_return_ctx=&tasks[next].ctx;
	return 0;
}

int64_t sys_getpgrp(void);
int64_t sys_getpgid(int pid);
int64_t sys_setpgid(int pid,int pgid);
int64_t sys_setsid(void);

void syscall_dispatch(syscall_frame_t *f) {
	task_t *dispatch_task = &tasks[current_task];
	if (f && f->user_cr3) {
		(void)vmm_user_activate(f->user_cr3);
		if (current_task >= 0 && current_task < MAX_TASKS && dispatch_task->used)
			dispatch_task->ctx.cr3 = f->user_cr3;
	}
	if (dispatch_task->wait4_reap_pid > 0) {
		for (int ri=0; ri<MAX_TASKS; ++ri) {
			if (tasks[ri].used && tasks[ri].pid == dispatch_task->wait4_reap_pid &&
				tasks[ri].ppid == dispatch_task->pid && tasks[ri].state == TASK_ZOMBIE) {
				task_reap_resources(&tasks[ri]);
				tasks[ri].used = 0;
				tasks[ri].state = TASK_UNUSED;
				break;
			}
		}
		dispatch_task->wait4_reap_pid = -1;
	}
	uint64_t nr=f->rax,a1=f->rdi,a2=f->rsi,a3=f->rdx,a4=f->r10,a5=f->r8,a6=f->r9;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
	uint64_t syscall_trace_seq = musl_syscall_trace_count;
	int syscall_trace_this = syscall_trace_interesting(nr);
	if (syscall_trace_this && syscall_trace_seq < 8192) {
		debugcon_puts("[SYSCALL] #"); debug_hex64("", syscall_trace_seq);
		debugcon_puts(" nr="); debug_hex64("", nr);
		debugcon_puts(" "); debugcon_puts(syscall_trace_name(nr));
		debugcon_puts(" a1="); debug_hex64("", a1);
		debugcon_puts(" a2="); debug_hex64("", a2);
		debugcon_puts(" a3="); debug_hex64("", a3);
		debugcon_puts(" a4="); debug_hex64("", a4);
		debugcon_puts(" a5="); debug_hex64("", a5);
		debugcon_puts(" a6="); debug_hex64("", a6);
		debugcon_puts("\n");
	}
	musl_syscall_trace_count++;
#endif
	int64_t ret=-ENOSYS;

	if ((nr == 1 || nr == 44) && f && current_task >= 0 && current_task < MAX_TASKS && tasks[current_task].used) {

		task_save_from_frame(&tasks[current_task], f);
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
		debug_hex64("[SOCK-CONTEXT-PRE] pid=0x", (uint64_t)tasks[current_task].pid);
		debug_hex64(" sf=0x", (uint64_t)(uintptr_t)f);
		debug_hex64(" rip=0x", tasks[current_task].ctx.rip);
		debug_hex64(" rsp=0x", tasks[current_task].ctx.rsp);
		debug_hex64(" cr3=0x", tasks[current_task].ctx.cr3);
		debugcon_puts("\n");
#endif
	} else if (nr == 1 || nr == 44) {
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
		debug_hex64("[SOCK-CONTEXT-PRE-NULL] nr=0x", nr);
		debug_hex64(" f=0x", (uint64_t)(uintptr_t)f);
		debug_hex64(" current=0x", (uint64_t)(int64_t)current_task);
		debugcon_puts("\n");
#endif
	}
	switch(nr) {
	  case 168: if (framebuffer_console_active) console_fb_cls(); ret=0; break;

	  case 454:
		if (framebuffer_users == 0) {
			framebuffer_console_active = 1;
			console_fb_cls();
		} else {
			debug_hex64("[FB-OWNERSHIP] restore deferred; active users=0x", (uint64_t)framebuffer_users); debugcon_puts("\n");
		}
		debugcon_puts("[FB-OWNERSHIP] framebuffer restore request handled\n");
		ret=0;
		break;
	  case 169:

		if (a1 == 0 && a2 == 0) {
			ret = sys_system_shutdown();
		} else if (a1 != 0xfee1deadULL || a2 != 0x28121969ULL) {
			ret = -22;             
		} else if (a3 == 0x01234567ULL) {
			ret = sys_system_reboot();
		} else if (a3 == 0x4321fedcULL) {
			ret = sys_system_shutdown();
		} else {
			ret = -22;
		}
		break;
	  case 0: ret=sys_pipe_read_dispatch(f,(int)a1,(void*)a2,a3); break;
	  case 1: ret=sys_write(f,(int)a1,(const void*)a2,a3); break;
	  case 2:
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
    debugcon_puts("[OPEN-DISPATCH] before sys_open\n");
#endif
    ret=sys_open((const char*)a1,(int)a2,(int)a3);
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
    debugcon_puts("[OPEN-DISPATCH] after sys_open ret=0x");
    debug_hex64("", (uint64_t)ret);
    debugcon_puts("\n");
#endif
    break;
	  case 3: ret=sys_close((int)a1); break;
	  case 4: ret=sys_stat((const char*)a1,(void*)a2); break;
	  case 5: ret=sys_fstat((int)a1,(void*)a2); break;
	  case 7: ret=sys_poll(f,(void*)a1,a2,(int)a3); break;
	  case 8: ret=sys_lseek((int)a1,(int64_t)a2,(int)a3); break;
	  case 9: ret=sys_mmap(a1,a2,a3,a4,(int)a5,a6); break;
	  case 10: ret=sys_mprotect(a1,a2,a3); break;
	  case 11: ret=sys_munmap(a1,a2); break;
	  case 12: ret=sys_brk(a1); break;
	  case 13: ret=sys_rt_sigaction((int)a1,(void*)a2,(void*)a3,a4); break;
	  case 28: ret=sys_madvise(a1,a2,(int)a3); break;
	  case 14: ret=sys_rt_sigprocmask((int)a1,(void*)a2,(void*)a3,a4); break;
	  case 16: ret=sys_ioctl(f,(int)a1,a2,a3); break;
	  case 17: ret=sys_pread64((int)a1,(void*)a2,a3,a4); break;
	  case 19: ret=sys_readv(f,(int)a1,(const void*)a2,(int)a3); break;
	  case 20: ret=sys_writev(f,(int)a1,(const void*)a2,(int)a3); break;
	  case 21: ret=sys_access((const char*)a1,(int)a2); break;
	  case 22: ret=sys_pipe((int*)a1); break;
	  case 23: ret=sys_select(f,(int)a1,(void*)a2,(void*)a3,(void*)a4,(void*)a5); break;
          case 270: ret=sys_pselect6(f,(int)a1,(void*)a2,(void*)a3,(void*)a4,(const void*)a5,(const void*)a6); break;
          case 271: ret=sys_ppoll(f,(void*)a1,a2,(const void*)a3,(const void*)a4); break;
	  case 24: ret=sys_sched_yield(f); break;
	  case 140: ret=sys_getpriority((int)a1,(int)a2); break;
	  case 141: ret=sys_setpriority((int)a1,(int)a2,(int)a3); break;
	  case 32: ret=sys_dup((int)a1); break;
	  case 33: ret=sys_dup2((int)a1,(int)a2); break;
	  case 292: ret=sys_dup3((int)a1,(int)a2,(int)a3); break;
	  case 39: ret=sys_getpid(); break;
	  case 41: ret=sys_socket((int)a1,(int)a2,(int)a3); break;
	  case 42: ret=sys_connect((int)a1,(void*)a2,(int)a3); break;
	  case 43: ret=sys_accept(f,(int)a1,(void*)a2,(int*)a3); break;
  case 288: ret=sys_accept4(f,(int)a1,(void*)a2,(int*)a3,(int)a4); break;
	  case 44: ret=sys_sendto(f,(int)a1,(void*)a2,(int)a3,(int)a4,(void*)a5,(int)a6,44); break;
	  case 45: ret=sys_recvfrom(f,(int)a1,(void*)a2,(int)a3,(int)a4,(void*)a5,(int*)a6,45); break;
	  case 46: ret=sys_sendmsg(f,(int)a1,(const void*)a2,(int)a3); break;
	  case 47: ret=sys_recvmsg(f,(int)a1,(void*)a2,(int)a3); break;
	  case 48: ret=sys_shutdown((int)a1,(int)a2); break;
	  case 49: ret=sys_bind((int)a1,(void*)a2,(int)a3); break;
	  case 50: ret=sys_listen((int)a1,(int)a2); break;
	  case 51: ret=sys_getsockname((int)a1,(void*)a2,(int*)a3); break;
	  case 52: ret=sys_getpeername((int)a1,(void*)a2,(int*)a3); break;
	  case 109: ret=sys_setpgid((int)a1,(int)a2); break;
	  case 112: ret=sys_setsid(); break;
	  case 121: ret=sys_getpgid((int)a1); break;
	  case 122: ret=sys_getpgrp(); break;
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
	  case 86: ret=sys_link((const char*)a1,(const char*)a2); break;
	  case 87: ret=sys_unlink((const char*)a1); break;
	  case 89: ret=sys_readlink((const char*)a1,(char*)a2,a3); break;
	  case 217: ret=sys_getdents64((int)a1,(void*)a2,a3); break;
	  case 96: ret=sys_gettimeofday((void*)a1,(void*)a2); break;
	  case 99: ret=sys_sysinfo((void*)a1); break;
	  case 90: ret=sys_chmod((const char*)a1,(uint32_t)a2); break;
  case 91: ret=sys_fchmod((int)a1,(uint32_t)a2); break;
  case 92: ret=sys_chown((const char*)a1,(uint32_t)a2,(uint32_t)a3); break;
	  case 93: ret=sys_fchown((int)a1,(uint32_t)a2,(uint32_t)a3); break;
	  case 94: ret=sys_lchown((const char*)a1,(uint32_t)a2,(uint32_t)a3); break;
	  case 102: ret=sys_getuid(); break;
	  case 104: ret=sys_getgid(); break;
	  case 105: ret=sys_setuid((uint32_t)a1); break;
	  case 106: ret=sys_setgid((uint32_t)a1); break;
	  case 107: ret=sys_geteuid(); break;
	  case 108: ret=sys_getegid(); break;
	  case 113: ret=sys_setreuid((uint32_t)a1,(uint32_t)a2); break;
	  case 114: ret=sys_setregid((uint32_t)a1,(uint32_t)a2); break;
	  case 117: ret=sys_setresuid((uint32_t)a1,(uint32_t)a2,(uint32_t)a3); break;
	  case 119: ret=sys_setresgid((uint32_t)a1,(uint32_t)a2,(uint32_t)a3); break;
	  case 110: ret=sys_getppid(); break;
	  case 158: ret=sys_arch_prctl(a1,a2); break;
	  case 186: ret=sys_gettid(); break;
	  case 201: { uint64_t s,ns; rtc_now(&s,&ns); ret=(int64_t)s; } break;
	  case 202: ret=sys_futex(f,(uint32_t*)a1,(int)a2,(uint32_t)a3,(const void*)a4,(uint32_t*)a5,(uint32_t)a6); break;
	  case 218: ret=sys_set_tid_address((int*)a1); break;
	  case 62: ret=sys_kill((int)a1,(int)a2); break;
	  case 127: ret=sys_rt_sigpending((sigset_t*)a1,a2); break;
	  case 131: ret=sys_sigaltstack((const yabro_stack_t*)a1,(const yabro_stack_t*)a2); break;
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
	  case 232: ret=sys_epoll_wait(f,(int)a1,(void*)a2,(int)a3,(int)a4); break;
	  case 281: ret=sys_epoll_pwait(f,(int)a1,(void*)a2,(int)a3,(int)a4,(const void*)a5,a6); break;
	  case 233: ret=sys_epoll_ctl((int)a1,(int)a2,(int)a3,(void*)a4); break;
	  case 234: ret=sys_tgkill((int)a1,(int)a2,(int)a3); break;
	  case 257: ret=sys_openat((int)a1,(const char*)a2,(int)a3,(int)a4); break;
	  case 260: ret=sys_fchownat((int)a1,(const char*)a2,(uint32_t)a3,(uint32_t)a4,(int)a5); break;
	  case 262: ret=sys_newfstatat((int)a1,(const char*)a2,(void*)a3,(int)a4); break;
	  case 267: ret=sys_readlinkat((int)a1,(const char*)a2,(char*)a3,a4); break;
  case 268: ret=sys_fchmodat((int)a1,(const char*)a2,(uint32_t)a3); break;
	  case 269: ret=sys_faccessat((int)a1,(const char*)a2,(int)a3); break;
	  case 289: ret=sys_eventfd2((uint32_t)a1,(int)a2); break;
	  case 291: ret=sys_epoll_create1((int)a1); break;
	  case 293: ret=sys_pipe2((int*)a1,(int)a2); break;
	  case 302: ret=sys_prlimit64((int)a1,(int)a2,(const void*)a3,(void*)a4); break;
	  case 325: ret=sys_timerfd_create((int)a1,(int)a2); break;
	  case 326: ret=sys_timerfd_settime((int)a1,(int)a2,(void*)a3,(void*)a4); break;
	  case 334: ret=-ENOSYS; break;
	  case 35: ret=sys_nanosleep(f,(const void*)a1,(void*)a2); break;
	  case 78: ret=sys_getdents64((int)a1,(void*)a2,a3); break;
	  case 203: ret=sys_sched_setaffinity((int)a1,a2,(const void*)a3); break;
	  case 204: ret=sys_sched_getaffinity((int)a1,a2,(void*)a3); break;
	  case 273: ret=sys_set_robust_list((void*)a1,a2); break;
	  case 274: ret=sys_get_robust_list((int)a1,(void*)a2,(void*)a3); break;
	  case 309: ret=sys_getcpu((unsigned*)a1,(unsigned*)a2,(void*)a3); break;
	  case 318: ret=sys_getrandom((void*)a1,a2,(uint32_t)a3); break;
	  case 452: ret=(int64_t)input_read_key(); break;
	  case 453: ret=sys_input_wait_key(); break;
	  default: ret=-ENOSYS; break;
	}
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
	if (syscall_trace_this && syscall_trace_seq < 8192) {
		debugcon_puts("[SYSCALL-DISPATCH] after switch nr=0x");
		debug_hex64("", nr);
		debugcon_puts(" ret=0x");
		debug_hex64("", (uint64_t)ret);
		debugcon_puts("\n");
	}
#endif
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
	if (syscall_trace_this && syscall_trace_seq < 8192) {
		debugcon_puts("[SYSCALL-RET] #"); debug_hex64("", syscall_trace_seq);
		debugcon_puts(" nr="); debug_hex64("", nr);
		debugcon_puts(" "); debugcon_puts(syscall_trace_name(nr));
		debugcon_puts(" ret="); debug_hex64("", (uint64_t)ret);
		debugcon_puts("\n");
	}
#endif
	f->rax=ret;

	if (!user_exec_pending) {

		if (nr != 15) {
			deliver_pending_signal(f);
		}

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
        if(!p->debug_read_logged){uint64_t first=0;for(uint64_t j=0;j<n&&j<8;j++)first|=(uint64_t)tmp[j]<<(j*8);debugcon_puts("[PIPE READ FIRST] pid=");debug_hex64("",(uint64_t)t->pid);debugcon_puts(" fd=");debug_hex64("",(uint64_t)t->pipe_wait_fd);debugcon_puts(" n=");debug_hex64("",n);debug_hex64(" bytes0_7=",first);debugcon_puts("\n");p->debug_read_logged=1;}
		if(user_copy_to_cr3(t->ctx.cr3,t->pipe_wait_buf+done,tmp,n)) { t->ctx.rax = done ? (int64_t)done : -EFAULT; t->pipe_wait_fd=-1; t->pipe_wait_buf=0; t->pipe_wait_count=0; return -1; }
		done += n;
	}
	t->ctx.rax=(int64_t)done; t->pipe_wait_fd=-1; t->pipe_wait_buf=0; t->pipe_wait_count=0; t->io_wait_kind=IO_WAIT_NONE; t->io_wait_valid=0; t->io_wait_nr=0; t->state=TASK_RUNNABLE;
	return (int)done;
}
static uint32_t pipe_live_readers(struct pipe_state *p);
static uint32_t pipe_live_writers(struct pipe_state *p);
static int pipe_has_readers(struct pipe_state *p);

static void pipe_wake_readers(struct pipe_state *p) {
	if(!p) return;
	for(int i=0;i<MAX_TASKS;i++) {
		task_t *t=&tasks[i]; if(!t->used || t->state!=TASK_BLOCKED || t->pipe_wait_fd<0) continue;
		struct file_desc *other_table = task_files_for_task(i);
		if(t->pipe_wait_fd>=MAX_FILES || !other_table || !other_table[t->pipe_wait_fd].in_use || other_table[t->pipe_wait_fd].private_data!=p) continue;
		pipe_read_into_task(t,p);
	}
}

int64_t sys_write(syscall_frame_t *sf, int fd, const void *buf, uint64_t count) {
	if(fd>=0&&fd<MAX_FILES&&files[fd].in_use&&files[fd].type==FD_SOCKET) return sys_sendto(sf,fd,buf,(int)count,0,NULL,0,1);

	if (fd<0 || fd>=MAX_FILES) { if(fd!=1&&fd!=2) return -EBADF; }
	if (count>(1ULL<<20) || (!buf && count)) return -EINVAL;
	if (!count) return 0;
	if (fd>=0 && fd<MAX_FILES && files[fd].in_use && files[fd].type==FD_PIPE_W) {
		struct pipe_state *tp=(struct pipe_state*)files[fd].private_data;
		if(tp && !tp->debug_write_enter_logged) {
			debugcon_puts("[PIPE WRITE ENTER]"); debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid);
			debug_hex64(" fd=0x", (uint64_t)fd); debug_hex64(" pipe=0x", (uint64_t)(uintptr_t)tp);
			debug_hex64(" buf=0x", (uint64_t)(uintptr_t)buf); debug_hex64(" count=0x", count);
			debug_hex64(" readers=0x", (uint64_t)tp->readers); debug_hex64(" writers=0x", (uint64_t)tp->writers);
			debugcon_puts("\n"); tp->debug_write_enter_logged=1;
		}
	}
	if (!user_range_ok((uint64_t)buf,count,0)) {
		if (fd>=0 && fd<MAX_FILES && files[fd].in_use && files[fd].type==FD_PIPE_W)
			debugcon_puts("[PIPE WRITE BAD USER RANGE]\n");
		return -EFAULT;
	}

	if ((fd==1 || fd==2) && fd_is_console_stream(fd)) {
		uint64_t va=(uint64_t)buf, rem=count;
		while(rem){ uint64_t phys,pte; uint64_t off=va&(PAGE_SIZE-1); uint64_t n=PAGE_SIZE-off; if(n>rem)n=rem;
			if(vmm_user_translate(va&PAGE_MASK,&phys,&pte)||!(pte&PTE_U)) return -EFAULT;
			uint8_t *src=(uint8_t*)pmm_phys_to_virt(phys); if(!src)return -EFAULT;
			for(uint64_t i=0;i<n;i++){
				uint8_t ch=src[off+i];
				console_putc((char)ch);
				if (framebuffer_console_active) console_fb_putc(ch);
			}
			va+=n; rem-=n;
		}
		return count;
	}
	if(fd>=MAX_FILES || !files[fd].in_use) return -EBADF;
	struct file_desc *f=&files[fd];
	if(f->type==FD_PTY_MASTER || f->type==FD_PTY_SLAVE){

		pty_state_k *p=(pty_state_k*)f->private_data;
		if(!p) return -EIO;
        if (f->type==FD_PTY_SLAVE && !pty_dbg_slave_write) { pty_dbg("[PTYDBG SLAVE_WRITE]", fd, f); debug_hex64(" requested=0x", count); debugcon_puts("\n"); pty_dbg_slave_write=1; }
		if(f->type==FD_PTY_MASTER) { if(!p->slave_open) return -EPIPE; }
		else { if(!p->master_open) return -EPIPE; }
		uint8_t tmp[256]; uint64_t done=0;
		while(done<count){
			uint64_t n=count-done; if(n>sizeof(tmp)) n=sizeof(tmp);
			if(user_copy_from(tmp,(uint64_t)buf+done,n)) return done ? (int64_t)done : -EFAULT;
			int w;
			if(f->type==FD_PTY_MASTER) {
                w=pty_canon_feed(p,tmp,n);
            } else w=(int)pty_output_write(p,tmp,n);
			done+=(uint64_t)w;
			if((uint64_t)w<n) break;
		}
        if (f->type==FD_PTY_SLAVE && !pty_dbg_slave_write_done) {
            pty_dbg("[PTYDBG SLAVE_WRITE_DONE]", fd, f);
            debug_hex64(" written=0x", done);
            debugcon_puts("\n");
            pty_dbg_slave_write_done=1;
        }
        if (done) task_wake_io_waiters();
		return (int64_t)done;
	}
	if(f->type==FD_PSEUDO){
		int k=(int)(uintptr_t)f->private_data;
		if(k==DEV_NULL) return (int64_t)count;
		if(k==DEV_CONSOLE || k==DEV_TTY){ uint64_t va=(uint64_t)buf, rem=count; while(rem){ uint64_t phys,pte,off=va&(PAGE_SIZE-1),n=PAGE_SIZE-off; if(n>rem)n=rem; if(vmm_user_translate(va&PAGE_MASK,&phys,&pte)||!(pte&PTE_U))return-EFAULT; uint8_t*src=(uint8_t*)pmm_phys_to_virt(phys); if(!src)return-EFAULT; for(uint64_t i=0;i<n;i++){console_putc((char)src[off+i]); if (framebuffer_console_active) console_fb_putc(src[off+i]);} va+=n; rem-=n;} return count; }
		return -EBADF;
	}
	if(f->type==FD_EVENTFD){
		if(count!=8)return-EINVAL; uint64_t v; if(user_copy_from((uint8_t*)&v,(uint64_t)buf,8))return-EFAULT;
		struct eventfd_state_k*e=(struct eventfd_state_k*)f->private_data; if(v==UINT64_MAX)return-EINVAL; if(UINT64_MAX-e->value<v)return (f->flags&0x800)?-EAGAIN:-EAGAIN; e->value+=v; return 8;
	}
	if(f->type==FD_TIMERFD)return-EINVAL;
	if(f->type==FD_PIPE_W){ struct pipe_state *p=(struct pipe_state*)f->private_data; p->readers=pipe_live_readers(p); if(!pipe_has_readers(p)) return -EPIPE; if(!count) return 0; uint64_t done=0; while(done<count){ if(p->count==PIPE_CAP){ if(f->flags & 0x800) return done?done:-EAGAIN; break; } uint8_t tmp[256]; uint64_t n=count-done; if(n>sizeof(tmp))n=sizeof(tmp); if(n>PIPE_CAP-p->count)n=PIPE_CAP-p->count; if(user_copy_from(tmp,(uint64_t)buf+done,n))return done?done:-EFAULT;
		if(!p->debug_write_logged){uint64_t first=0;for(uint64_t j=0;j<n&&j<8;j++)first|=(uint64_t)tmp[j]<<(j*8);debugcon_puts("[PIPE WRITE FIRST] pid=");debug_hex64("",(uint64_t)tasks[current_task].pid);debugcon_puts(" fd=");debug_hex64("",(uint64_t)fd);debugcon_puts(" n=");debug_hex64("",n);debug_hex64(" bytes0_7=",first);debugcon_puts("\n");p->debug_write_logged=1;}
		for(uint64_t i=0;i<n;i++){p->data[p->wpos]=tmp[i];p->wpos=(p->wpos+1)%PIPE_CAP;p->count++;} done+=n; pipe_wake_readers(p); if(done<count && p->count==PIPE_CAP) break; } return done; }
	if(f->type!=FD_FILE) return -EBADF;
	if(f->vnode && vfs_file_is_regular(f->vnode)) {
		if((f->flags & 3) == 0) return -EBADF;
		uint64_t off=fd_offset_get(f);
		uint8_t tmp[256]; uint64_t done=0;
		while(done<count) {
			uint64_t n=count-done; if(n>sizeof(tmp)) n=sizeof(tmp);
			if(user_copy_from(tmp,(uint64_t)buf+done,n)) return done?done:-EFAULT;
			int64_t w;
			if(f->flags & O_APPEND) {
				off = vfs_file_size(f->vnode);
				w = vfs_file_write(f->vnode,off,tmp,n);
			} else {
				w = vfs_file_write(f->vnode,off,tmp,n);
			}
			if(w < 0) return done ? (int64_t)done : w;
			off += (uint64_t)w;
			done += (uint64_t)w;
			if((uint64_t)w < n) break;
		}
		fd_offset_set(f,off); f->size=vfs_file_size(f->vnode); return done;
	}
	return -EBADF;
}

int64_t sys_read(syscall_frame_t *sf, int fd, void *buf, uint64_t count) {
	if(fd>=0&&fd<MAX_FILES&&files[fd].in_use&&files[fd].type==FD_SOCKET) return sys_recvfrom(sf,fd,buf,(int)count,0,NULL,NULL,0);

	if(fd<0 || fd>=MAX_FILES || !files[fd].in_use) return -EBADF;
	if(!buf && count)return-EINVAL; if(count && !user_range_ok((uint64_t)buf,count,1))return-EFAULT;
	struct file_desc *f=&files[fd];
	if(f->type==FD_PTY_MASTER || f->type==FD_PTY_SLAVE){

		pty_state_k *p=(pty_state_k*)f->private_data;
		if(!p) return -EIO;
        if (f->type==FD_PTY_MASTER && !pty_dbg_master_read) { pty_dbg("[PTYDBG MASTER_READ_ENTER]", fd, f); debug_hex64(" requested=0x", count); debugcon_puts("\n"); pty_dbg_master_read=1; }
		if(!count) return 0;
		uint8_t tmp[256];
		uint64_t n=count<sizeof(tmp)?count:sizeof(tmp);
		int got;
		if(f->type==FD_PTY_MASTER){
			if(!p->slave_open && !p->s2m_count) return -EPIPE;
			if(!p->s2m_count || p->output_stopped) {
                if (f->flags & 0x800) return -EAGAIN;                 
                if (task_block_io(sf,IO_WAIT_PTY_READ,0)==0) return 0;
                return -EAGAIN;
            }
			got=pty_ring_read(p->slave_to_master,&p->s2m_r,&p->s2m_count,tmp,n);
		} else {
			if(!p->master_open && !p->m2s_count && !p->canon_count) return -EPIPE;
			if(((uint32_t)tty_lflag(p->termios)) & ICANON) {
				if(!pty_canon_available(p) && p->master_open) {
                    if (f->flags & 0x800) return -EAGAIN;
                    if (task_block_io(sf,IO_WAIT_PTY_READ,0)==0) return 0;
                    return -EAGAIN;
                }
                uint32_t consume=0, deliver=0;
                uint8_t veof=tty_cc(p->termios,VEOF),veol=tty_cc(p->termios,VEOL),veol2=tty_cc(p->termios,VEOL2);
                while(consume<p->canon_count && deliver<n){
                    uint8_t c=p->canon[consume++];
                    if(veof&&c==veof)break;                                                     
                    tmp[deliver++]=c;
                    if(c=='\n'||(veol&&c==veol)||(veol2&&c==veol2))break;
                }
                for(uint32_t i=consume;i<p->canon_count;i++)p->canon[i-consume]=p->canon[i];
                p->canon_count-=consume;
                got=(int)deliver;
			} else {
				if(!p->m2s_count) {
                    if (!p->master_open) return -EPIPE;
                    if (f->flags & 0x800) return -EAGAIN;
                    if (task_block_io(sf,IO_WAIT_PTY_READ,0)==0) return 0;
                    return -EAGAIN;
                }
				got=pty_ring_read(p->master_to_slave,&p->m2s_r,&p->m2s_count,tmp,n);
			}
		}
		if(got && user_copy_to((uint64_t)buf,tmp,(uint64_t)got)) return -EFAULT;
        if (f->type==FD_PTY_MASTER && got>0 && !pty_dbg_master_data) { pty_dbg("[PTYDBG MASTER_READ_DATA]", fd, f); debug_hex64(" bytes=0x", (uint64_t)got); debugcon_puts("\n"); pty_dbg_master_data=1; }
        if (got) task_wake_io_waiters();
		return got;
	}
	if(f->type==FD_PSEUDO){
		int k=(int)(uintptr_t)f->private_data;
		if(k==DEV_NULL) return 0;
		if(k==DEV_INPUT || k==DEV_INPUT_MOUSE) {
            volatile uint8_t *enter_logged = (k==DEV_INPUT_MOUSE) ? (uint8_t*)&evdev_mouse_read_enter_logged : (uint8_t*)&evdev_keyboard_read_enter_logged;
            if(!*enter_logged) {
                debugcon_puts("[EVDEV READ ENTER]");
                debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid);
                debug_hex64(" table=0x", (uint64_t)(uint16_t)task_fd_table_id[current_task]);
                debug_hex64(" fd=0x", (uint64_t)fd); debug_hex64(" kind=0x", (uint64_t)k);
                debug_hex64(" count=0x", count); debug_hex64(" flags=0x", f->flags);
                debug_hex64(" key_seq=0x", keyboard_event_seq);
                debug_hex64(" mouse_seq=0x", mouse_event_seq); debug_hex64(" input_seq=0x", input_fd_seq(f));
                debugcon_puts("\n"); *enter_logged=1;
            }
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
			if(k==DEV_INPUT_MOUSE) debugcon_puts("[EVDEV OPEN] event1\n");
#endif
			if(!count) return 0;
			if(count < sizeof(struct input_event_k)) return -EINVAL;

			struct input_event_k events[INPUT_EVENT_READ_MAX];
			uint64_t max_events=count/sizeof(struct input_event_k);
			if(max_events>INPUT_EVENT_READ_MAX) max_events=INPUT_EVENT_READ_MAX;
			uint64_t got=0;
			if(k==DEV_INPUT_MOUSE) mouse_read_calls++;
			else keyboard_read_calls++;
			while(got<max_events) {
				int ok=(k==DEV_INPUT)?keyboard_pop_event_fd(f,&events[got]):mouse_pop_event_fd(f,&events[got]);
				if(!ok) break;
				got++;
			}
			if(!got){
                volatile uint8_t *empty_logged = (k==DEV_INPUT_MOUSE) ? (uint8_t*)&evdev_mouse_empty_logged : (uint8_t*)&evdev_keyboard_empty_logged;
                if(!*empty_logged) {
                    debugcon_puts("[EVDEV READ EMPTY]"); debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid);
                    debug_hex64(" fd=0x", (uint64_t)fd); debug_hex64(" kind=0x", (uint64_t)k);
                    debug_hex64(" flags=0x", f->flags); debug_hex64(" key_seq=0x", keyboard_event_seq); debug_hex64(" mouse_seq=0x", mouse_event_seq);
                    debug_hex64(" input_seq=0x", input_fd_seq(f)); debugcon_puts("\n"); *empty_logged=1;
                }
				if(f->flags & 0x800) return -EAGAIN;
				if(task_block_io(sf,IO_WAIT_POLL,0)==0) return 0;
				return -EAGAIN;
			}
			uint64_t bytes=got*sizeof(struct input_event_k);
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL

            debugcon_puts(k==DEV_INPUT_MOUSE?"[EVDEV MOUSE READ RETURN]":"[EVDEV KEY READ RETURN]");
            debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid); debug_hex64(" fd=0x", (uint64_t)fd);
            debug_hex64(" events=0x", got); debug_hex64(" bytes=0x", bytes);
            if(got){debug_hex64(" first_type=0x", (uint64_t)events[0].type);debug_hex64(" first_code=0x", (uint64_t)events[0].code);debug_hex64(" first_value=0x", (uint64_t)(int64_t)events[0].value);} debugcon_puts("\n");
#endif
			if(user_copy_to((uint64_t)buf,(const uint8_t*)events,bytes)) {
				if(k==DEV_INPUT_MOUSE) mouse_read_copy_fail++;
				return -EFAULT;
			}
			if(k==DEV_INPUT) {
				keyboard_read_events+=got;

                for(uint64_t i=0;i<got;i++) {
                    if(events[i].type==EV_KEY &&
                       (events[i].code==29 || events[i].code==97) &&
                       (events[i].value==0 || events[i].value==1)) {
                        unsigned base=(events[i].code==29)?0u:2u;
                        unsigned bit_index=base+(events[i].value==0?1u:0u);
                        uint8_t bit=(uint8_t)(1u<<bit_index);
                        if(!(evdev_ctrl_read_trace_mask&bit)) {
                            evdev_ctrl_read_trace_mask|=bit;
                            debugcon_puts("[EVDEV CTRL READ]");
                            debug_hex64(" pid=0x",(uint64_t)tasks[current_task].pid);
                            debug_hex64(" code=0x",(uint64_t)events[i].code);
                            debug_hex64(" value=0x",(uint64_t)(int64_t)events[i].value);
                            debugcon_puts("\n");
                        }
                    }
                }
				for(uint64_t i=0;i<got;i++) {
					if(events[i].type==EV_KEY && !evdev_keyboard_read_logged) {
						debugcon_puts("[EVDEV KEY READ] first EV_KEY read from event0");
						debug_hex64(" pid=0x",(uint64_t)tasks[current_task].pid);
						debug_hex64(" fd=0x",(uint64_t)fd);
						debug_hex64(" code=0x",(uint64_t)events[i].code);
						debug_hex64(" value=0x",(uint64_t)(int64_t)events[i].value);
						debugcon_puts("\n");
						evdev_keyboard_read_logged=1;
						break;
					}
				}
			}
			if(k==DEV_INPUT_MOUSE) {
				mouse_read_ready+=got;
				mouse_read_success+=got;
				for(uint64_t i=0;i<got;i++) {
					struct input_event_k *e=&events[i];
					if(!evdev_mouse_read_logged && (e->type==EV_REL || e->type==EV_KEY)) {
						debugcon_puts("[EVDEV READ EVENT] type="); debug_hex64("", (uint64_t)e->type);
						debugcon_puts("[EVDEV READ EVENT] code="); debug_hex64("", (uint64_t)e->code);
						debugcon_puts("[EVDEV READ EVENT] value="); debug_hex64("", (uint64_t)(int64_t)e->value);
						evdev_mouse_read_logged=1;
						break;
					}
				}
			}
			return (int64_t)bytes;
		}
		if(k==DEV_TTY) {
			if(!count) return 0;
			int this_pid = (current_task >= 0 && current_task < MAX_TASKS) ? tasks[current_task].pid : -1;
			if(console_tty_pending_pos < console_tty_pending_len && console_tty_pending_pid == this_pid) {
				uint8_t pending = console_tty_pending_bytes[console_tty_pending_pos++];
				if(console_tty_pending_pos >= console_tty_pending_len) {
					console_tty_pending_pos = console_tty_pending_len = 0;
					console_tty_pending_pid = -1;
				}
				return user_copy_to((uint64_t)buf,&pending,1)==0 ? 1 : -EFAULT;
			}
			if(console_tty_pending_pos < console_tty_pending_len && console_tty_pending_pid != this_pid) {
				console_tty_pending_pos = console_tty_pending_len = 0;
				console_tty_pending_pid = -1;
			}
			if(key_head==key_tail) {
				if(f->flags & 0x800) return -EAGAIN;
				if(task_block_io(sf,IO_WAIT_TTY,0)==0) return 0;
				return -EAGAIN;
			}
			uint8_t sc=get_key(); uint8_t ch=0;
			int foreground_app = console_fg_pgrp > 0 && console_shell_pgrp > 0 &&
				console_fg_pgrp != console_shell_pgrp && tasks[current_task].pgrp == console_fg_pgrp;

			if(sc >= 0xA0 && sc <= 0xB9) {
				ch = (uint8_t)(sc - 0xA0 + 1);                            
				return user_copy_to((uint64_t)buf,&ch,1)==0 ? 1 : -EFAULT;
			}
			if((sc >= 0x80 && sc <= 0x83) || (sc >= 0x85 && sc <= 0x8A)) {
				if(!foreground_app) {
					if(user_copy_to((uint64_t)buf,&sc,1)==0) return 1;
					return -EFAULT;
				}
				uint8_t seq[3] = { '[', 0, 0 };
				uint8_t seq_len = 2;
				switch(sc) {
					case 0x80: seq[1]='A'; break;         
					case 0x81: seq[1]='B'; break;           
					case 0x82: seq[1]='D'; break;           
					case 0x83: seq[1]='C'; break;            
					case 0x85: seq[1]='H'; break;           
					case 0x86: seq[1]='F'; break;          
					case 0x87: seq[1]='3'; seq[2]='~'; seq_len=3; break;             
					case 0x88: seq[1]='5'; seq[2]='~'; seq_len=3; break;           
					case 0x89: seq[1]='6'; seq[2]='~'; seq_len=3; break;           
					case 0x8A: seq[1]='2'; seq[2]='~'; seq_len=3; break;             
					default: break;
				}
				console_tty_pending_bytes[0] = seq[0];
				console_tty_pending_bytes[1] = seq[1];
				console_tty_pending_bytes[2] = seq[2];
				console_tty_pending_pos = 0;
				console_tty_pending_len = seq_len;
				console_tty_pending_pid = this_pid;
				ch = 27;
				return user_copy_to((uint64_t)buf,&ch,1)==0 ? 1 : -EFAULT;
			}
			static const uint8_t normal[58] = {
				0, 27, '1','2','3','4','5','6','7','8','9','0','-','=',8,9,
				'q','w','e','r','t','y','u','i','o','p','[',']','\r',0,
				'a','s','d','f','g','h','j','k','l',';','\'', '`',0,'\\',
				'z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
			};
			static const uint8_t shifted[58] = {
				0, 27, '!','@','#','$','%','^','&','*','(',')','_','+',8,9,
				'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
				'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
				'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
			};
			if(sc == 0x84) {
                ch = 3;                                    
                int fg=console_fg_pgrp;
                int raw_foreground_child=(fg>0 && console_shell_pgrp>0 && fg!=console_shell_pgrp);
                if (((tty_lflag(console_termios_k) & ISIG) &&
                     ch == tty_cc(console_termios_k, VINTR)) || raw_foreground_child) {

                    (void)console_tty_pgrp_signal(SIGINT);
                    return -EAGAIN;
                }
			} else if(sc < 58) {
				int alpha = ((sc>=0x10 && sc<=0x19) ||
							 (sc>=0x1e && sc<=0x26) ||
							 (sc>=0x2c && sc<=0x32));
				int use_shift = shift_pressed ? 1 : 0;

				if(alpha && caps_lock) use_shift = !use_shift;
				ch = use_shift ? shifted[sc] : normal[sc];
			}
			if(foreground_app && sc==0x0E) ch=127;                                 

			if(ch == '\r') {
				uint32_t iflag = (uint32_t)tty_iflag(console_termios_k);
				if(iflag & IGNCR) ch = 0;
				else if(iflag & ICRNL) ch = '\n';
			} else if(ch == '\n' && ((uint32_t)tty_iflag(console_termios_k) & INLCR)) {
				ch = '\r';
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
	if(f->type==FD_PIPE_R){ struct pipe_state *p=(struct pipe_state*)f->private_data; if(!count)return 0; if(!p->count){ if(!p->writers)return 0; if(f->flags & 0x800) return -EAGAIN; return -EAGAIN; } uint8_t tmp[256]; uint64_t done=0; while(done<count && p->count){ uint64_t n=count-done;if(n>sizeof(tmp))n=sizeof(tmp); if(n>p->count)n=p->count; for(uint64_t i=0;i<n;i++){tmp[i]=p->data[p->rpos];p->rpos=(p->rpos+1)%PIPE_CAP;p->count--;}
		if(!p->debug_read_logged){uint64_t first=0;for(uint64_t j=0;j<n&&j<8;j++)first|=(uint64_t)tmp[j]<<(j*8);debugcon_puts("[PIPE READ FIRST] pid=");debug_hex64("",(uint64_t)tasks[current_task].pid);debugcon_puts(" fd=");debug_hex64("",(uint64_t)fd);debugcon_puts(" n=");debug_hex64("",n);debug_hex64(" bytes0_7=",first);debugcon_puts("\n");p->debug_read_logged=1;}
		if(user_copy_to((uint64_t)buf+done,tmp,n))return done?done:-EFAULT;done+=n;} return done; }
	if(f->type!=FD_FILE)return-EBADF;
	if(fd_uses_bootfs(f)) {
		uint64_t cur=fd_offset_get(f);
		uint64_t size=f->vnode ? vfs_file_size(f->vnode) : f->size;
		const char *bp=fd_bootfs_path(f);
		if(!bp) return -EIO;
		if(cur>=size) return 0;
		uint64_t want=count<size-cur?count:size-cur;
		uint64_t done=0;
		uint8_t tmp[4096];
		while(done<want) {
			uint64_t n=want-done;
			if(n>sizeof(tmp)) n=sizeof(tmp);
			size_t got=bootfs_read_root_file(bp,cur+done,tmp,(size_t)n);
			if(got==0) return done ? (int64_t)done : -EIO;
			if(user_copy_to((uint64_t)buf+done,tmp,got))
				return done ? (int64_t)done : -EFAULT;
			done += got;
			if(got != n) break;
		}
		fd_offset_set(f,cur+done);
		return (int64_t)done;
	}
	uint64_t size=(f->vnode&&vfs_file_is_regular(f->vnode))?vfs_file_size(f->vnode):f->size;
	uint8_t *data=(f->vnode&&vfs_file_is_regular(f->vnode))?vfs_file_data(f->vnode):f->data;
	uint64_t cur=fd_offset_get(f); uint64_t avail=cur<size?size-cur:0, n=count<avail?count:avail;
	if(n && data && user_copy_to((uint64_t)buf,data+cur,n))return-EFAULT;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
	if(n && is_xkm_fd(f) && cur==0) xkm_audit_bytes("[XKM-READ]",data+cur,n);
#endif
	fd_offset_set(f,cur+n); f->size=size; return n;
}

static int64_t sys_open_resolved(const char *raw, const char *pth, int flags, int mode) {
	(void)mode;
	int input_path_trace = pseudo_eq(raw,"/dev/input/event0") || pseudo_eq(raw,"/dev/input/event1") || pseudo_eq(pth,"/dev/input/event0") || pseudo_eq(pth,"/dev/input/event1") || pseudo_eq(raw,"event0") || pseudo_eq(raw,"event1");
	if(input_path_trace) { debugcon_puts("[INPUT OPEN REQ] raw="); debugcon_puts(raw); debugcon_puts("\n[INPUT OPEN PATH] resolved="); debugcon_puts(pth); debugcon_puts("\n[INPUT OPEN FLAGS]"); debug_hex64("", (uint64_t)(unsigned)flags); }
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
	if ((kernel_strncmp(raw, "/usr/share/X11/xkb", 18) == 0) || (kernel_strncmp(raw, "/var/lib/xkb", 12) == 0)) {
		debugcon_puts("[XKB-OPEN] "); debugcon_puts(raw); debugcon_puts(" flags=0x"); debug_hex64("", (uint64_t)(unsigned)flags); debugcon_puts("\n");
	}
	if (raw[0]=='/' && (!kernel_strncmp(raw,"/lib/",5) || !kernel_strncmp(raw,"/usr/lib/",9) || !kernel_strncmp(raw,"/etc/ld-musl",12))) {
		debugcon_puts("[OPEN-AUDIT] "); debugcon_puts(raw); debugcon_puts(" -> "); debugcon_puts(pth); debugcon_puts(" flags=0x"); debug_hex64("", (uint64_t)(unsigned)flags); debugcon_puts("\n");
	}
#endif
	int fd=find_free_fd();
	if(input_path_trace && fd>=0) {
		debugcon_puts("[INPUT FDALLOC]");
		debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid);
		debug_hex64(" table=0x", (uint64_t)(uint16_t)task_fd_table_id[current_task]);
		debug_hex64(" fd=0x", (uint64_t)fd);
		debug_hex64(" prior_in_use=0x", (uint64_t)files[fd].in_use);
		debug_hex64(" prior_type=0x", (uint64_t)files[fd].type);
		debugcon_puts("\n");
	}
	if(fd<0 || fd>=MAX_FILES) {
		if ((kernel_strncmp(raw, "/usr/share/X11/xkb", 18) == 0) ||
		    (kernel_strncmp(raw, "/var/lib/xkb", 12) == 0)) {
			debugcon_puts("[XKB-OPEN-FAIL] EMFILE "); debugcon_puts(raw);
			debugcon_puts("\n");
		}
		return-EMFILE;
	}
	int typ=0,kind=0;
	int input_pseudo_found = pseudo_lookup(pth,&typ,&kind);
	if(input_path_trace) { debugcon_puts("[INPUT OPEN LOOKUP] pseudo="); debug_hex64("", (uint64_t)input_pseudo_found); debugcon_puts("[INPUT OPEN KIND] kind="); debug_hex64("", (uint64_t)kind); }
	if(input_pseudo_found) {
		if(kind==DEV_FB && fb_trace_allowed()) { debugcon_puts("[FB-OPEN] "); debugcon_puts(pth); debugcon_puts(" flags=0x"); debug_hex64("", (uint64_t)(unsigned)flags); debugcon_puts("\n"); }
		files[fd].in_use=1; files[fd].flags=flags; files[fd].name=NULL;
		int pr=pseudo_fill(&files[fd],pth,kind);
		if(pr<0){ if(input_path_trace){debugcon_puts("[INPUT OPEN RESULT] pseudo_fill error=");debug_hex64("",(uint64_t)pr);} fd_mark_closed(&files[fd]); return pr; }
		fd_set_boot_path(&files[fd], pth);
		files[fd].size = pseudo_is_dir_kind(kind) ? 0 : files[fd].size;
        if(input_path_trace) {
            fd_trace_slot("[INPUT OPEN FINAL SLOT]", fd, &files[fd]);
            if(kind==DEV_INPUT_MOUSE) {
                debugcon_puts("[INPUT OPEN MOUSE SEQ]"); debug_hex64(" seq=0x", files[fd].input_seq);
                debug_hex64(" current=0x", mouse_event_seq); debugcon_puts("\n");
            } else if(kind==DEV_INPUT) {
                debugcon_puts("[INPUT OPEN KEY QUEUE]"); debug_hex64(" seq=0x", keyboard_event_seq);
                debug_hex64(" cursor=0x", files[fd].input_seq); debugcon_puts("\n");
            }
            debugcon_puts("[INPUT OPEN PID]"); debug_hex64("", (uint64_t)tasks[current_task].pid);
            debugcon_puts("[INPUT OPEN TABLE]"); debug_hex64("", (uint64_t)(uint16_t)task_fd_table_id[current_task]);
            debugcon_puts("[INPUT OPEN RESULT] fd=");debug_hex64("",(uint64_t)fd);
        }
		return fd;
	}
	struct vfs_node *vn=vfs_lookup(pth);
	if (raw[0]=='/' && (!kernel_strncmp(raw,"/lib/",5) || !kernel_strncmp(raw,"/usr/lib/",9) || !kernel_strncmp(raw,"/etc/ld-musl",12))) {
		debugcon_puts("[OPEN-AUDIT] vfs_lookup="); debug_hex64("0x", (uint64_t)(uintptr_t)vn); debugcon_puts(" bootfs_size="); debug_hex64("0x", bootfs_file_size(pth)); debugcon_puts("\n");
	}
	if(!vn && (flags & 0x40)) vn=vfs_create_file(pth,(flags & 0x200)!=0);
	if(!vn && !(flags & 0x40) && !(flags & 3)) {
		uint64_t bsz=bootfs_file_size(pth);
		if(bsz>0) {
			files[fd].in_use=1; files[fd].type=FD_FILE; files[fd].flags=flags; files[fd].offset=0;
			files[fd].shared_offset=alloc_open_offset();
			if(!files[fd].shared_offset){files[fd].in_use=0;return-EMFILE;}
			files[fd].data=NULL; files[fd].size=bsz; files[fd].name=NULL; files[fd].vnode=NULL;
			fd_set_boot_path(&files[fd],pth);
			if (raw[0]=='/' && (!kernel_strncmp(raw,"/lib/",5) || !kernel_strncmp(raw,"/usr/lib/",9) || !kernel_strncmp(raw,"/etc/ld-musl",12))) { debugcon_puts("[OPEN-AUDIT] BOOTFS fd="); debug_hex64("0x",(uint64_t)fd); debugcon_puts(" size="); debug_hex64("0x",files[fd].size); debugcon_puts("\n"); }
			return fd;
		}
	}
	if(vn && vfs_is_dir(vn)) {
		files[fd].in_use=1; files[fd].type=FD_PSEUDO; files[fd].flags=flags; files[fd].private_data=(void*)(uintptr_t)PSEUDO_DEV_DIR; files[fd].vnode=vn; files[fd].offset=0; files[fd].size=0; files[fd].data=NULL; files[fd].name=vn->name; fd_set_boot_path(&files[fd], pth); return fd;
	}
	if(vn && vfs_is_boot_file(vn)) {
		if(flags & 3) return -EROFS;
		files[fd].in_use=1; files[fd].type=FD_FILE; files[fd].flags=flags; files[fd].offset=0;
		files[fd].shared_offset=alloc_open_offset(); if(!files[fd].shared_offset){files[fd].in_use=0;return-EMFILE;}
		files[fd].data=NULL; files[fd].size=vfs_file_size(vn); files[fd].name=vn->name; files[fd].vnode=vn; fd_set_boot_path(&files[fd],vfs_boot_path(vn)); return fd;
	}
	if(vn && vfs_file_is_regular(vn)) {
		if((flags & 0x200) && vfs_file_truncate(vn,0)<0) return -EIO;
		files[fd].in_use=1; files[fd].type=FD_FILE; files[fd].flags=flags; files[fd].offset=(flags & O_APPEND)?vn->size:0;
		files[fd].shared_offset=alloc_open_offset(); if(!files[fd].shared_offset){fd_mark_closed(&files[fd]);return-EMFILE;}
		*files[fd].shared_offset=files[fd].offset; files[fd].data=vn->data; files[fd].size=vn->size; files[fd].name=vn->name; files[fd].vnode=vn;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
		if(is_xkm_fd(&files[fd])){
			debugcon_puts("[XKM-OPEN] path=");debugcon_puts(pth);debugcon_puts(" fd=0x");debug_hex64("",(uint64_t)fd);debug_hex64(" size=0x",vn->size);debugcon_puts("\n");
			xkm_audit_header("[XKM-OPEN-HEADER]",vfs_file_data(vn),vfs_file_size(vn));
			xkm_audit_toc("[XKM-OPEN-TOC]",vfs_file_data(vn),vfs_file_size(vn));
			xkm_audit_bytes("[XKM-OPEN-DATA]",vfs_file_data(vn),vfs_file_size(vn));
		}
#endif
		return fd;
	}
	if (raw[0]=='/' && (!kernel_strncmp(raw,"/lib/",5) || !kernel_strncmp(raw,"/usr/lib/",9) || !kernel_strncmp(raw,"/etc/ld-musl",12))) { debugcon_puts("[OPEN-AUDIT] FAIL ENOENT "); debugcon_puts(raw); debugcon_puts("\n"); }
	if(input_path_trace) debugcon_puts("[INPUT OPEN RESULT] failed ENOENT\n");
	return -ENOENT;
}
int64_t sys_open(const char *path, int flags, int mode) {
	char raw[128], pth[128];
	if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw))) return -EFAULT;
    int ptmx_req = pseudo_eq(raw, "/dev/ptmx");
    int pts_req = pty_index_from_path(raw) >= 0;
    if ((ptmx_req && pty_dbg_ptmx_open_count < 12) || (pts_req && pty_dbg_pts_open_count < 12)) {
        debugcon_puts("[PTYDBG OPEN_REQ] path="); debugcon_puts(raw);
        debug_hex64(" flags=0x", (uint64_t)(uint32_t)flags); debugcon_puts("\n");
        if (ptmx_req) pty_dbg_ptmx_open_count++; else pty_dbg_pts_open_count++;
    }
	if(resolve_process_path(raw,pth,sizeof(pth))<0) {
        if (ptmx_req || pts_req) { debugcon_puts("[PTYDBG OPEN_RESULT] path="); debugcon_puts(raw); debugcon_puts(" rc=-36 (resolve path failed)\n"); }
        return -ENAMETOOLONG;
    }
    int64_t rc = sys_open_resolved(raw,pth,flags,mode);
    if (ptmx_req || pts_req) {
        debugcon_puts("[PTYDBG OPEN_RESULT] path="); debugcon_puts(raw);
        debug_hex64(" rc=0x", (uint64_t)rc); debugcon_puts("\n");
    }
	return rc;
}
static uint32_t pipe_live_readers(struct pipe_state *p){
	uint32_t n=0;

	for(int table=0;table<MAX_FD_TABLES;table++) if(task_fd_table_refs[table])
		for(int i=0;i<MAX_FILES;i++){struct file_desc *d=&task_fd_tables[table][i];if(d->in_use&&d->private_data==p&&d->type==FD_PIPE_R)n++;}
	return n;
}
static uint32_t pipe_live_writers(struct pipe_state *p){
	uint32_t n=0;
	for(int table=0;table<MAX_FD_TABLES;table++) if(task_fd_table_refs[table])
		for(int i=0;i<MAX_FILES;i++){struct file_desc *d=&task_fd_tables[table][i];if(d->in_use&&d->private_data==p&&d->type==FD_PIPE_W)n++;}
	return n;
}
static int pipe_has_readers(struct pipe_state *p){ return pipe_live_readers(p)!=0; }
static int pipe_has_writers(struct pipe_state *p){ return pipe_live_writers(p)!=0; }
int64_t sys_close(int fd){if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF; struct file_desc*f=&files[fd];
	if(f->type==FD_PIPE_R || f->type==FD_PIPE_W ||
	   (f->type==FD_PSEUDO && ((uintptr_t)f->private_data==DEV_INPUT || (uintptr_t)f->private_data==DEV_INPUT_MOUSE)))
		fd_trace_slot("[FD CLOSE]", fd, f);
	if(f->type==FD_EPOLL){task_fd_release(f);return 0;}
	if(f->type==FD_PTY_MASTER || f->type==FD_PTY_SLAVE){ pty_state_k *p=(pty_state_k*)f->private_data; if(p){ if(f->type==FD_PTY_MASTER){if(p->master_refs)p->master_refs--;if(!p->master_refs)p->master_open=0;} else {if(p->slave_refs)p->slave_refs--;if(!p->slave_refs){p->slave_open=0;p->output_stopped=0;}} } fd_drop_resources(f); fd_mark_closed(f); task_wake_io_waiters(); return 0; }
 struct pipe_state *p=(f->type==FD_PIPE_R||f->type==FD_PIPE_W)?(struct pipe_state*)f->private_data:NULL; if(f->type==FD_PIPE_R){if(p->readers)p->readers--; fd_drop_resources(f); fd_mark_closed(f); p->readers=pipe_live_readers(p); for(int i=0;i<MAX_TASKS;i++){task_t*t=&tasks[i];if(t->used&&t->state==TASK_BLOCKED&&t->pipe_wait_fd==fd){t->ctx.rax=0;t->pipe_wait_fd=-1;t->pipe_wait_buf=0;t->pipe_wait_count=0;t->state=TASK_RUNNABLE;}} return 0;} if(f->type==FD_PIPE_W){if(p->writers)p->writers--; fd_drop_resources(f); fd_mark_closed(f); p->writers=pipe_live_writers(p); pipe_wake_readers(p); return 0;} if(f->type==FD_SOCKET){
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
    debugcon_puts("[X11 CLOSE] pid=0x"); debug_hex64("", (uint64_t)tasks[current_task].pid);
    debugcon_puts(" fd=0x"); debug_hex64("", (uint64_t)fd); debugcon_puts("\n");
#endif
    socket_fd_drop_ref(f); task_wake_io_waiters(); fd_drop_resources(f); fd_mark_closed(f);return 0;
} fd_drop_resources(f); fd_mark_closed(f);return 0;}

struct linux_stat {
	uint64_t st_dev, st_ino, st_nlink;
	uint32_t st_mode, st_uid, st_gid, __pad0;
	uint64_t st_rdev, st_size, st_blksize, st_blocks;
	uint64_t st_atime, st_atime_nsec, st_mtime, st_mtime_nsec, st_ctime, st_ctime_nsec;
	long __unused[3];
};
static int fill_stat(struct linux_stat *st,uint64_t ino,uint64_t nlink,uint64_t size,uint32_t mode){
	for(size_t i=0;i<sizeof(*st);i++)((uint8_t*)st)[i]=0;
	st->st_dev=1; st->st_ino=ino?ino:1; st->st_nlink=nlink?nlink:1;
	st->st_mode=mode; st->st_uid=0; st->st_gid=0; st->st_size=size;
	st->st_blksize=4096; st->st_blocks=(size+511)/512;
	return 0;
}
static uint64_t input_dev_rdev(int kind){

	return kind==DEV_INPUT_MOUSE ? ((13ULL<<8)|65ULL) : ((13ULL<<8)|64ULL);
}
static int64_t sys_stat_resolved(const char *p, void *ust) {
	int typ=0,kind=0; struct linux_stat st;
	if(pseudo_lookup(p,&typ,&kind)){
		uint32_t mode;
		if(pseudo_is_dir_kind(kind)) mode=0040755;
		else if(kind>=DEV_NULL && kind<=DEV_URANDOM) mode=0020666;
		else if(kind==DEV_FB || kind==DEV_INPUT || kind==DEV_INPUT_MOUSE) mode=0020666;
		else mode=0100444;
		fill_stat(&st,100,1,0,mode); st.st_ino=100;
		if(kind==DEV_FB) { st.st_size=graphics_fb_size(); st.st_rdev=(7ULL<<8)|0ULL; }
		if(kind==DEV_INPUT || kind==DEV_INPUT_MOUSE) st.st_rdev=input_dev_rdev(kind);
		if(!pseudo_is_dir_kind(kind)&&kind==PSEUDO_PROC_FILE) { char tmp[4096]; st.st_size=pseudo_make_proc(tmp,sizeof(tmp),p); }
		if(user_copy_to((uint64_t)ust,(uint8_t*)&st,sizeof(st)))return-EFAULT; return 0;
	}
	struct vfs_node *vn=vfs_lookup(p);
	if(vn) {
		uint32_t mode=vfs_mode(vn);
		uint64_t size=(vfs_file_is_regular(vn)||vfs_is_boot_file(vn))?vfs_file_size(vn):0;
		fill_stat(&st,vfs_ino(vn),vn->nlink?vn->nlink:1,size,mode);
		st.st_ino=vfs_ino(vn);
		if(user_copy_to((uint64_t)ust,(uint8_t*)&st,sizeof(st)))return-EFAULT;
		return 0;
	}
	{
		uint64_t bsz=bootfs_file_size(p);
		if(bsz>0){ fill_stat(&st,1,1,bsz,0100444); st.st_ino=1; if(user_copy_to((uint64_t)ust,(uint8_t*)&st,sizeof(st)))return-EFAULT; return 0; }
	}
	return-ENOENT;
}
int64_t sys_stat(const char*path,void*ust){
	char raw[128],p[128];
	if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw))) return -EFAULT;
	if(resolve_process_path(raw,p,sizeof(p))<0) return -ENAMETOOLONG;
	return sys_stat_resolved(p,ust);
}
int64_t sys_fstat(int fd,void*ust){
	if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF;
	struct linux_stat st;
	struct file_desc *f=&files[fd];
	uint64_t ino=1,nlink=1,size=f->size;
	uint32_t mode=0100644;

	if(f->type==FD_PSEUDO && (uintptr_t)f->private_data==DEV_FB){
		mode=0020666;
		ino=100;
		nlink=1;
		size=graphics_fb_size();
		st.st_rdev=(7ULL<<8)|0ULL;
	} else if(f->type==FD_PSEUDO && ((uintptr_t)f->private_data==DEV_INPUT || (uintptr_t)f->private_data==DEV_INPUT_MOUSE)){
		mode=0020666;
		ino=100;
		nlink=1;
		st.st_rdev=input_dev_rdev((int)(uintptr_t)f->private_data);
	} else if(f->vnode){
		ino=vfs_ino(f->vnode);
		nlink=f->vnode->nlink?f->vnode->nlink:1;
		size=(vfs_file_is_regular(f->vnode)||vfs_is_boot_file(f->vnode))?vfs_file_size(f->vnode):0;
		mode=vfs_mode(f->vnode);
	} else if(f->type==FD_PSEUDO && ((uintptr_t)f->private_data==DEV_FB||(uintptr_t)f->private_data==DEV_INPUT||(uintptr_t)f->private_data==DEV_INPUT_MOUSE)) {
		mode=0020666;
	}
	fill_stat(&st,ino,nlink,size,mode);
	if(f->type==FD_PSEUDO && (uintptr_t)f->private_data==DEV_FB){
		st.st_rdev=(7ULL<<8)|0ULL;
	} else if(f->type==FD_PSEUDO && ((uintptr_t)f->private_data==DEV_INPUT || (uintptr_t)f->private_data==DEV_INPUT_MOUSE)){
		st.st_rdev=input_dev_rdev((int)(uintptr_t)f->private_data);
	}
	if(f->type==FD_PSEUDO && (uintptr_t)f->private_data==DEV_FB){
		debug_hex64("[FB-FSTAT] fd=0x", (uint64_t)fd);
		debug_hex64(" size=0x", size);
		debug_hex64(" rdev=0x", st.st_rdev); debugcon_puts("\n");
	}
	if(user_copy_to((uint64_t)ust,(uint8_t*)&st,sizeof(st)))return-EFAULT;
	return 0;
}
static int is_xkm_fd(const struct file_desc *f){
	if(!f || !f->vnode || !vfs_file_is_regular(f->vnode)) return 0;
	if(!f->vnode->name[0]) return 0;
	return kernel_strncmp(f->vnode->name,"server-0.xkm",12)==0 && f->vnode->name[12]==0;
}
static void xkm_audit_bytes(const char *tag, const uint8_t *p, uint64_t n){
	if(!p) return;
	debugcon_puts(tag); debugcon_puts(" size=0x"); debug_hex64("",n); debugcon_puts(" first64=");
	uint64_t m=n<64?n:64; static const char hx[]="0123456789abcdef"; char q[3];
	for(uint64_t i=0;i<m;i++){q[0]=hx[(p[i]>>4)&15];q[1]=hx[p[i]&15];q[2]=0;debugcon_puts(q);} debugcon_puts("\n");
}
static void xkm_audit_user(const char *tag, uint64_t u, uint64_t n){
	uint8_t tmp[64]; uint64_t m=n<sizeof(tmp)?n:sizeof(tmp);
	if(!m || user_copy_from(tmp,u,m)) return;
	xkm_audit_bytes(tag,tmp,m);
}
static void xkm_audit_header(const char *tag, const uint8_t *p, uint64_t n){
	if(!p || n<12) return;
	debugcon_puts(tag);
	debug_hex64(" version=0x",p[0]);
	debug_hex64(" magic=0x",((uint64_t)p[3]<<16)|((uint64_t)p[2]<<8)|p[1]);
	debug_hex64(" type=0x",p[4]);
	debug_hex64(" min_kc=0x",p[5]);
	debug_hex64(" max_kc=0x",p[6]);
	debug_hex64(" num_toc=0x",p[7]);
	debug_hex64(" present=0x",(uint64_t)p[8]|((uint64_t)p[9]<<8));
	debug_hex64(" pad=0x",(uint64_t)p[10]|((uint64_t)p[11]<<8));
	debugcon_puts("\n");
}
static uint16_t xkm_le16(const uint8_t *p){
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static void xkm_audit_toc(const char *tag, const uint8_t *p, uint64_t n){
	if(!p || n<12) return;
	uint64_t count=p[7];
	uint64_t table_end=12+count*8;
	debugcon_puts(tag);
	debug_hex64(" file_size=0x",n);
	debug_hex64(" entries=0x",count);
	debug_hex64(" table_end=0x",table_end);
	debugcon_puts("\n");
	if(count>16 || table_end>n){
		debugcon_puts("[XKM-TOC-INVALID] count/range exceeds file\n");
		return;
	}
	for(uint64_t i=0;i<count;i++){
		const uint8_t *e=p+12+i*8;
		uint16_t type=xkm_le16(e), format=xkm_le16(e+2);
		uint16_t size=xkm_le16(e+4), off=xkm_le16(e+6);
		uint64_t end=(uint64_t)off+(uint64_t)size;
		int range_ok=(uint64_t)off>=table_end && end<=n && end>=(uint64_t)off;
		debugcon_puts("[XKM-TOC-ENTRY]");
		debug_hex64(" i=0x",i); debug_hex64(" type=0x",type);
		debug_hex64(" format=0x",format); debug_hex64(" size=0x",size);
		debug_hex64(" offset=0x",off); debug_hex64(" end=0x",end);
		debug_hex64(" range_ok=0x",(uint64_t)range_ok);
		debugcon_puts("\n");
		if(!range_ok){debugcon_puts("[XKM-TOC-RANGE-ERROR]\n");continue;}
		const uint8_t *h=p+off;
		uint16_t ht=xkm_le16(h), hf=xkm_le16(h+2);
		uint16_t hs=xkm_le16(h+4), ho=xkm_le16(h+6);
		int match=(ht==type && hf==format && hs==size && ho==off);
		debugcon_puts("[XKM-SECTION-HEADER]");
		debug_hex64(" i=0x",i); debug_hex64(" type=0x",ht);
		debug_hex64(" format=0x",hf); debug_hex64(" size=0x",hs);
		debug_hex64(" offset=0x",ho); debug_hex64(" match=0x",(uint64_t)match);
		debugcon_puts("\n");
	}
}
int64_t sys_lseek(int fd,int64_t off,int whence){
	if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF;
	struct file_desc*f=&files[fd]; if(f->type!=FD_FILE)return-ESPIPE;
	uint64_t cur=fd_offset_get(f); uint64_t size=f->vnode?vfs_file_size(f->vnode):f->size;
	int64_t base=whence==0?0:(whence==1?(int64_t)cur:(whence==2?(int64_t)size:INT64_MIN));
	if(base==INT64_MIN)return-EINVAL;
	if((off>0 && base>INT64_MAX-off)||(off<0 && base<INT64_MIN-off))return-EINVAL;
	int64_t n=base+off; if(n<0)return-EINVAL;
	fd_offset_set(f,(uint64_t)n);
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
	if(is_xkm_fd(f)){debug_hex64("[XKM-LSEEK] fd=0x",(uint64_t)fd);debug_hex64(" cur=0x",cur);debug_hex64(" off=0x",(uint64_t)off);debug_hex64(" whence=0x",(uint64_t)whence);debug_hex64(" new=0x",(uint64_t)n);debugcon_puts("\n");}
#endif
	return n;
}
int64_t sys_pread64(int fd,void*buf,uint64_t count,uint64_t off){
	if(fd<0||fd>=MAX_FILES||!files[fd].in_use)return-EBADF;
	struct file_desc*f=&files[fd];
	if(f->type!=FD_FILE)return-ESPIPE;
	if(fd_uses_bootfs(f)){
		const char *bp=fd_bootfs_path(f);
		if(!bp)return-EIO;
		if(off>=f->size)return 0;
		uint64_t want=count<f->size-off?count:f->size-off;
		uint64_t done=0;
		uint8_t tmp[4096];
		while(done<want){
			uint64_t n=want-done;
			if(n>sizeof(tmp))n=sizeof(tmp);
			size_t got=bootfs_read_root_file(bp,off+done,tmp,(size_t)n);
			if(!got)return done?(int64_t)done:-EIO;
			if(user_copy_to((uint64_t)buf+done,tmp,got))return done?(int64_t)done:-EFAULT;
			done+=got;
			if(got!=n)break;
		}
		return (int64_t)done;
	}
	uint64_t size=(f->vnode&&vfs_file_is_regular(f->vnode))?vfs_file_size(f->vnode):f->size;
	const uint8_t *data=(f->vnode&&vfs_file_is_regular(f->vnode))?vfs_file_data(f->vnode):f->data;
	if(off>=size)return 0;
	uint64_t n=count<size-off?count:size-off;
	if(n && data && user_copy_to((uint64_t)buf,data+off,n)) return -EFAULT;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
	if(n && is_xkm_fd(f) && off==0) xkm_audit_bytes("[XKM-PREAD]",data+off,n);
#endif
	return n;
}
struct iovec_k { uint64_t base,len; };
int64_t sys_readv(syscall_frame_t *sf, int fd, const void *uiov, int cnt) {
    if (cnt < 0 || cnt > 1024) return -EINVAL;

    struct iovec_k v;
    int64_t total = 0;
    int xkm = fd >= 0 && fd < MAX_FILES && files[fd].in_use &&
              is_xkm_fd(&files[fd]);
    uint8_t xkm_prefix[12];
    uint64_t xkm_prefix_len = 0;
    int xkm_prefix_logged = 0;

    for (int i = 0; i < cnt; i++) {
        if (user_copy_from((uint8_t *)&v, (uint64_t)uiov + i * sizeof(v), sizeof(v)))
            return -EFAULT;

#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
        if (xkm) {
            debug_hex64("[XKM-READV] iov=0x", (uint64_t)i);
            debug_hex64(" base=0x", v.base);
            debug_hex64(" len=0x", v.len);
            debug_hex64(" cur=0x", fd_offset_get(&files[fd]));
            debugcon_puts("\n");
        }
#endif

        int64_t n;
        if (fd >= 0 && fd < MAX_FILES && files[fd].in_use &&
            files[fd].type == FD_PIPE_R) {
            struct pipe_state *pipe = (struct pipe_state *)files[fd].private_data;

            if (pipe && v.len && !pipe->count && pipe->writers &&
                !(files[fd].flags & 0x800) && total == 0) {
                if (!pipe->debug_readv_block_logged) {
                    debugcon_puts("[PIPE READV BLOCK]");
                    debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid);
                    debug_hex64(" fd=0x", (uint64_t)fd);
                    debug_hex64(" pipe=0x", (uint64_t)(uintptr_t)pipe);
                    debug_hex64(" writers=0x", (uint64_t)pipe->writers);
                    debugcon_puts("\n");
                    pipe->debug_readv_block_logged = 1;
                }
                n = sys_pipe_read_dispatch(sf, fd, (void *)v.base, v.len);
            } else {
                n = sys_read(sf, fd, (void *)v.base, v.len);
            }
        } else {
            n = sys_read(sf, fd, (void *)v.base, v.len);
        }

#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
        if (xkm && n > 0 && !xkm_prefix_logged && xkm_prefix_len < sizeof(xkm_prefix)) {
            uint64_t take = (uint64_t)n;
            if (take > sizeof(xkm_prefix) - xkm_prefix_len)
                take = sizeof(xkm_prefix) - xkm_prefix_len;
            if (take && !user_copy_from(xkm_prefix + xkm_prefix_len, v.base, take)) {
                xkm_prefix_len += take;
                if (xkm_prefix_len == sizeof(xkm_prefix)) {
                    xkm_audit_header("[XKM-READV-HEADER]", xkm_prefix, sizeof(xkm_prefix));
                    xkm_audit_bytes("[XKM-READV-PREFIX]", xkm_prefix, sizeof(xkm_prefix));
                    xkm_prefix_logged = 1;
                }
            }
        }
        if (xkm && i == 0 && n > 0 && fd_offset_get(&files[fd]) >= (uint64_t)n)
            xkm_audit_user("[XKM-READV-DATA]", v.base, (uint64_t)n);
        if (xkm) {
            debug_hex64("[XKM-READV] iov=0x", (uint64_t)i);
            debug_hex64(" got=0x", (uint64_t)n);
            debug_hex64(" total=0x", (uint64_t)(total + (n > 0 ? n : 0)));
            debugcon_puts("\n");
        }
#endif

        if (n < 0) return total ? total : n;
        total += n;
        if ((uint64_t)n < v.len) break;
    }

#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
    if (xkm) {
        debug_hex64("[XKM-READV-DONE] fd=0x", (uint64_t)fd);
        debug_hex64(" total=0x", (uint64_t)total);
        debug_hex64(" cur=0x", fd_offset_get(&files[fd]));
        debugcon_puts("\n");
    }
#endif
    return total;
}
int64_t sys_writev(syscall_frame_t *sf,int fd,const void*uiov,int cnt){
 if(cnt<0||cnt>1024)return-EINVAL;
 if(fd>=0&&fd<MAX_FILES&&files[fd].in_use&&files[fd].type==FD_PIPE_W){
  struct pipe_state *p=(struct pipe_state*)files[fd].private_data;
  if(p&&!p->debug_writev_enter_logged){debugcon_puts("[PIPE WRITEV ENTER]");debug_hex64(" pid=0x",(uint64_t)tasks[current_task].pid);debug_hex64(" fd=0x",(uint64_t)fd);debug_hex64(" pipe=0x",(uint64_t)(uintptr_t)p);debug_hex64(" iov=0x",(uint64_t)(uintptr_t)uiov);debug_hex64(" cnt=0x",(uint64_t)(int64_t)cnt);debugcon_puts("\n");p->debug_writev_enter_logged=1;}
 }
 struct iovec_k v; int64_t total=0;
 int xkm=(fd>=0&&fd<MAX_FILES&&files[fd].in_use&&is_xkm_fd(&files[fd]));
 for(int i=0;i<cnt;i++){
  if(user_copy_from((uint8_t*)&v,(uint64_t)uiov+i*sizeof(v),sizeof(v)))return-EFAULT;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
  if(xkm){debug_hex64("[XKM-WRITEV] iov=0x",(uint64_t)i);debug_hex64(" base=0x",v.base);debug_hex64(" len=0x",v.len);debug_hex64(" cur=0x",fd_offset_get(&files[fd]));debugcon_puts("\n");}
  if(xkm && i==0) xkm_audit_user("[XKM-WRITEV-DATA]",v.base,v.len);
#endif
  int64_t n=sys_write(sf,fd,(void*)v.base,v.len);
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
  if(xkm){debug_hex64("[XKM-WRITEV] iov=0x",(uint64_t)i);debug_hex64(" got=0x",(uint64_t)n);debug_hex64(" total=0x",(uint64_t)(total+(n>0?n:0)));debugcon_puts("\n");}
#endif
  if(n<0)return total?total:n; total+=n; if((uint64_t)n<v.len)break;
 }
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
 if(xkm){debug_hex64("[XKM-WRITEV-DONE] fd=0x",(uint64_t)fd);debug_hex64(" total=0x",(uint64_t)total);debug_hex64(" cur=0x",fd_offset_get(&files[fd]));debug_hex64(" size=0x",vfs_file_size(files[fd].vnode));debugcon_puts("\n");}
#endif
 return total;
}
static int64_t sys_access_resolved(const char *p,int mode){(void)mode;int t=0,k=0;if(pseudo_lookup(p,&t,&k))return 0;if(vfs_lookup(p))return 0;if(bootfs_file_size(p)>0)return 0;const char *tp=p;if(tp[0]=='/')tp++;return (tp[0]=='T'&&tp[1]=='E'&&tp[2]=='S'&&tp[3]=='T'&&tp[4]=='.')?0:-ENOENT;}
int64_t sys_access(const char*path,int mode){char raw[128],p[128];if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT;if(resolve_process_path(raw,p,sizeof(p))<0)return-ENAMETOOLONG;return sys_access_resolved(p,mode);}
int64_t sys_pipe(int *ufds){
	if(!ufds)return-EFAULT;
	int a=find_free_fd(),b=-1;
	if(a<0)return-EMFILE;
	for(int i=a+1;i<MAX_FILES;i++)if(!files[i].in_use){b=i;break;}
	if(b<0)return-EMFILE;

	struct pipe_state *p=NULL;
	for(int i=0;i<8;i++){
		if(pipes[i].readers==0 && pipes[i].writers==0 && pipes[i].count==0){
			p=&pipes[i]; break;
		}
	}
	if(!p)return-EMFILE;

	p->rpos=0; p->wpos=0; p->count=0; p->readers=1; p->writers=1;
    p->debug_write_logged=0; p->debug_read_logged=0;
    p->debug_write_enter_logged=0; p->debug_writev_enter_logged=0; p->debug_read_enter_logged=0; p->debug_readv_block_logged=0;
	files[a].in_use=1; files[a].type=FD_PIPE_R; files[a].flags=0;
	files[a].offset=0; files[a].private_data=p;
	files[b].in_use=1; files[b].type=FD_PIPE_W; files[b].flags=0;
	files[b].offset=0; files[b].private_data=p;
	debugcon_puts("[PIPE CREATE]");
	debug_hex64(" pid=0x", (uint64_t)tasks[current_task].pid);
	debug_hex64(" table=0x", (uint64_t)(uint16_t)task_fd_table_id[current_task]);
	debug_hex64(" rfd=0x", (uint64_t)a); debug_hex64(" wfd=0x", (uint64_t)b);
	debug_hex64(" pipe=0x", (uint64_t)(uintptr_t)p); debugcon_puts("\n");

	if(user_copy_to((uint64_t)ufds,(uint8_t*)&a,sizeof(a)) ||
	   user_copy_to((uint64_t)ufds+4,(uint8_t*)&b,sizeof(b))){
		files[a].in_use=files[b].in_use=0;
		files[a].private_data=files[b].private_data=NULL;
		p->readers=0; p->writers=0; p->count=0;
		return-EFAULT;
	}
	return 0;
}
static void fd_copy_scalar(struct file_desc *dst, const struct file_desc *src) {
	dst->in_use = src->in_use; dst->type = src->type; dst->flags = src->flags;
	dst->offset = src->offset; dst->size = src->size; dst->data = src->data;
	dst->name = src->name; dst->private_data = src->private_data; dst->vnode = src->vnode;
	dst->shared_offset = src->shared_offset;
	for (size_t i=0; i<sizeof(dst->boot_path); ++i) dst->boot_path[i] = src->boot_path[i];
	dst->input_state = src->input_state;
	dst->input_seq = src->input_seq;
	if(dst->shared_offset) retain_open_offset(dst->shared_offset);
	if(dst->input_state) input_state_retain(dst->input_state);
	if(dst->in_use && dst->type==FD_EPOLL) epoll_retain_file(dst);
}
static void fd_drop_resources(struct file_desc *f){ if(f->shared_offset){release_open_offset(f->shared_offset);f->shared_offset=NULL;} }

static void fd_retain_endpoint_ref(struct file_desc *f) {
	if(!f || !f->in_use) return;
	if(f->type==FD_PIPE_R) {
		struct pipe_state *p=(struct pipe_state*)f->private_data; if(p)p->readers++;
	} else if(f->type==FD_PIPE_W) {
		struct pipe_state *p=(struct pipe_state*)f->private_data; if(p)p->writers++;
	} else if(f->type==FD_PTY_MASTER || f->type==FD_PTY_SLAVE) {
		pty_state_k *p=(pty_state_k*)f->private_data;
		if(p && f->type==FD_PTY_MASTER) { p->master_refs++; p->master_open=1; }
		if(p && f->type==FD_PTY_SLAVE) { p->slave_refs++; p->slave_open=1; }
	} else if(f->type==FD_SOCKET) {
		struct unix_socket_state *sock=socket_state(f); int side=socket_side(f);
		if(sock) { sock->refs++; if(side>=0 && side<2) sock->side_refs[side]++; }
	}
}
static int64_t sys_dup_internal(int oldfd,int minfd,int cloexec){
	if(oldfd<0||oldfd>=MAX_FILES||!files[oldfd].in_use)return-EBADF;
	int n=-1; for(int i=minfd;i<MAX_FILES;i++) if(!files[i].in_use){n=i;break;} if(n<0)return-EMFILE;
	fd_copy_scalar(&files[n],&files[oldfd]);
	files[n].flags &= ~0x80000; if(cloexec) files[n].flags |= 0x80000;
	fd_retain_endpoint_ref(&files[n]);
	return n;
}

int64_t sys_dup(int oldfd){
    int64_t n=sys_dup_internal(oldfd,0,0);
    if (!pty_dbg_dup_lowfd && n>=0 && oldfd>=0 && oldfd<MAX_FILES &&
        files[oldfd].in_use &&
        (files[oldfd].type==FD_PTY_MASTER || files[oldfd].type==FD_PTY_SLAVE)) {
        debugcon_puts("[FD DUP LOWEST] pid=0x"); debug_hex64("", (uint64_t)tasks[current_task].pid);
        debug_hex64(" old=0x", (uint64_t)oldfd); debug_hex64(" new=0x", (uint64_t)n);
        debug_hex64(" type=0x", (uint64_t)files[n].type); debugcon_puts("\n");
        pty_dbg_dup_lowfd=1;
    }
    return n;
}
int64_t sys_dup2(int oldfd,int newfd){
	if(oldfd<0||oldfd>=MAX_FILES||!files[oldfd].in_use||newfd<0||newfd>=MAX_FILES)return-EBADF;
	if(oldfd==newfd)return newfd;
	int pipe_remap=(files[oldfd].type==FD_PIPE_R||files[oldfd].type==FD_PIPE_W||
		(newfd>=0&&files[newfd].in_use&&(files[newfd].type==FD_PIPE_R||files[newfd].type==FD_PIPE_W)));
    int pty_remap=(files[oldfd].type==FD_PTY_MASTER||files[oldfd].type==FD_PTY_SLAVE||
        (files[newfd].in_use&&(files[newfd].type==FD_PTY_MASTER||files[newfd].type==FD_PTY_SLAVE)));
	if(pipe_remap || pty_remap || newfd==0) { fd_trace_slot("[FD DUP2 SRC]", oldfd, &files[oldfd]); fd_trace_slot("[FD DUP2 DST BEFORE]", newfd, &files[newfd]); }
	if(files[newfd].in_use)sys_close(newfd);
	fd_copy_scalar(&files[newfd],&files[oldfd]); files[newfd].flags &= ~0x80000;
	if(pipe_remap || pty_remap || newfd==0) fd_trace_slot("[FD DUP2 DST AFTER]", newfd, &files[newfd]);
	fd_retain_endpoint_ref(&files[newfd]);
	return newfd;
}
int64_t sys_dup3(int oldfd,int newfd,int flags){
	if(flags & ~0x80000)return-EINVAL;
	if(oldfd<0||oldfd>=MAX_FILES||!files[oldfd].in_use||newfd<0||newfd>=MAX_FILES)return-EBADF;
	if(oldfd==newfd)return-EINVAL;
    int pipe_remap=(files[oldfd].type==FD_PIPE_R||files[oldfd].type==FD_PIPE_W||files[oldfd].type==FD_SOCKET ||
        (files[newfd].in_use && (files[newfd].type==FD_PIPE_R||files[newfd].type==FD_PIPE_W||files[newfd].type==FD_SOCKET)));
    int pty_remap=(files[oldfd].type==FD_PTY_MASTER||files[oldfd].type==FD_PTY_SLAVE||
        (files[newfd].in_use&&(files[newfd].type==FD_PTY_MASTER||files[newfd].type==FD_PTY_SLAVE)));
    if(pipe_remap || pty_remap || newfd==0) { fd_trace_slot("[FD DUP3 SRC]", oldfd, &files[oldfd]); fd_trace_slot("[FD DUP3 DST BEFORE]", newfd, &files[newfd]); }
	if(files[newfd].in_use)sys_close(newfd);
	fd_copy_scalar(&files[newfd],&files[oldfd]); files[newfd].flags &= ~0x80000; files[newfd].flags |= (flags & 0x80000);
    if(pipe_remap || pty_remap || newfd==0) fd_trace_slot("[FD DUP3 DST AFTER]", newfd, &files[newfd]);
	fd_retain_endpoint_ref(&files[newfd]);
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
#define TIOCSWINSZ 0x5414
struct winsize_k {uint16_t rows,cols,xpixel,ypixel;};
#define TCGETS 0x5401
#define TCSETS 0x5402
static uint8_t console_termios_k[TERMIOS_K_SIZE] = {
    [0]=0x00, [1]=0x05,                            
    [8]=0x03, [10]=0x1c,                                             
    [12]=0x7f, [13]=0x15, [14]=0x04,                             
    [17+VINTR]=3, [17+VQUIT]=28, [17+VERASE]=127,
    [17+VKILL]=21, [17+VEOF]=4, [17+5]=0, [17+6]=1,
    [17+VSUSP]=26
};
static int fd_is_terminal(int fd){
	return fd>=0 && fd<MAX_FILES && files[fd].in_use && files[fd].type==FD_PSEUDO &&
		   ((int)(uintptr_t)files[fd].private_data==DEV_CONSOLE ||
			(int)(uintptr_t)files[fd].private_data==DEV_TTY) ||
		   files[fd].type==FD_PTY_MASTER || files[fd].type==FD_PTY_SLAVE;
}

int64_t sys_ioctl(syscall_frame_t *sf,int fd,uint64_t req,uint64_t arg){

	req = (uint64_t)(uint32_t)req;
	if(fd<0||fd>=MAX_FILES||!files[fd].in_use) return -EBADF;
	struct file_desc *f=&files[fd];
	if (req == FIONREAD) {
        int avail = 0;
        if (f->type == FD_SOCKET) {
            struct unix_socket_state *s = socket_state(f);
            int side = socket_side(f);
            if (!s) return -ENOTCONN;
            avail = (int)s->count[side];
        } else if (f->type == FD_PIPE_R) {
            struct pipe_state *p = (struct pipe_state *)f->private_data;
            avail = p ? (int)p->count : 0;
        } else if (f->type == FD_PTY_MASTER || f->type == FD_PTY_SLAVE) {
            pty_state_k *p = (pty_state_k *)f->private_data;
            if (!p) return -EIO;
            avail = f->type == FD_PTY_MASTER ? (p->output_stopped ? 0 : (int)p->s2m_count) :
                ((((uint32_t)tty_lflag(p->termios)) & ICANON) ? pty_canon_ready_bytes(p) : (int)p->m2s_count);
        } else {
            return -ENOTTY;
        }
        if (!arg || user_copy_to(arg, (const uint8_t *)&avail, sizeof(avail))) return -EFAULT;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
        if (f->type == FD_SOCKET) {
            debugcon_puts("[X11 FIONREAD] pid=0x"); debug_hex64("", (uint64_t)tasks[current_task].pid);
            debugcon_puts(" fd=0x"); debug_hex64("", (uint64_t)fd);
            debugcon_puts(" avail=0x"); debug_hex64("", (uint64_t)(uint32_t)avail); debugcon_puts("\n");
        }
#endif
        return 0;
    }
    if (req == FIONBIO) {
        int on = 0;
        if (!arg || user_copy_from((uint8_t *)&on, arg, sizeof(on))) return -EFAULT;
        if (f->type == FD_SOCKET || f->type == FD_PIPE_R || f->type == FD_PIPE_W ||
            f->type == FD_PTY_MASTER || f->type == FD_PTY_SLAVE) {
            if (on) f->flags |= SOCK_NONBLOCK; else f->flags &= ~((uint64_t)SOCK_NONBLOCK);
            return 0;
        }
        return -ENOTTY;
    }

	if(req==TIOCGWINSZ && f->type==FD_FILE) return -ENOTTY;
	if(f->type==FD_PSEUDO && (uintptr_t)f->private_data==DEV_FB){
		int trace_fb_ioctl=fb_trace_allowed();
		uint64_t vals[6];
		if(graphics_fb_info_kernel(vals)!=0) return -EIO;
		uint32_t w=(uint32_t)vals[0], h=(uint32_t)vals[1], pitch=(uint32_t)vals[2], bpp=(uint32_t)vals[3];
		if(trace_fb_ioctl){
			debugcon_puts("[FBTRACE] ioctl req=0x"); debug_hex64("", req); debugcon_puts(" fd=0x"); debug_hex64("", (uint64_t)fd); debugcon_puts("\n");
			debug_hex64("[FB-IOCTL] width=0x", w); debug_hex64(" height=0x", h); debug_hex64(" pitch=0x", pitch); debug_hex64(" bpp=0x", bpp); debug_hex64(" size=0x", vals[4]); debug_hex64(" phys=0x", graphics_fb_phys()); debugcon_puts("\n");
		}
		if(req==FBIOGET_VSCREENINFO){
			struct fb_var_screeninfo_k v; uint8_t *q=(uint8_t*)&v;
			for(size_t i=0;i<sizeof(v);i++)q[i]=0;
			v.xres=v.xres_virtual=w; v.yres=v.yres_virtual=h; v.bits_per_pixel=bpp;
			if(bpp==32){ v.red.offset=16; v.red.length=8; v.green.offset=8; v.green.length=8; v.blue.offset=0; v.blue.length=8; v.transp.offset=24; v.transp.length=8; }
			else if(bpp==24){ v.red.offset=16; v.red.length=8; v.green.offset=8; v.green.length=8; v.blue.offset=0; v.blue.length=8; }
			else if(bpp==16){ v.red.offset=11; v.red.length=5; v.green.offset=5; v.green.length=6; v.blue.offset=0; v.blue.length=5; }
			return user_copy_to(arg,(const uint8_t*)&v,sizeof(v)) ? -EFAULT : 0;
		}
		if(req==FBIOGET_FSCREENINFO){
			struct fb_fix_screeninfo_k fsi; uint8_t *q=(uint8_t*)&fsi;
			for(size_t i=0;i<sizeof(fsi);i++)q[i]=0;
			const char id[]="YabroOS-fb0"; for(size_t i=0;i<sizeof(id)-1 && i<sizeof(fsi.id);i++)fsi.id[i]=id[i];
			fsi.smem_start=(unsigned long)vals[5]; fsi.smem_len=(uint32_t)vals[4]; fsi.type=0; fsi.type_aux=0; fsi.visual=(bpp==1)?1:2; fsi.xpanstep=0; fsi.ypanstep=0; fsi.ywrapstep=0; fsi.line_length=pitch; fsi.mmio_start=0; fsi.mmio_len=0; fsi.accel=0; fsi.capabilities=0;
			fsi.smem_start=(unsigned long)graphics_fb_phys();
			return user_copy_to(arg,(const uint8_t*)&fsi,sizeof(fsi)) ? -EFAULT : 0;
		}
		if(req==FBIOPUT_VSCREENINFO){
			struct fb_var_screeninfo_k v;

			if(user_copy_from((uint8_t*)&v,arg,sizeof(v))) return 0;
			v.xres=w; v.yres=h; v.xres_virtual=w; v.yres_virtual=h;
			v.xoffset=0; v.yoffset=0; v.bits_per_pixel=bpp;
			(void)user_copy_to(arg,(const uint8_t*)&v,sizeof(v));
			return 0;
		}
		if(req==FBIOPAN_DISPLAY){
			struct fb_var_screeninfo_k v;
			if(user_copy_from((uint8_t*)&v,arg,sizeof(v))) return 0;
			v.xoffset=0; v.yoffset=0; v.xres=w; v.yres=h;
			v.xres_virtual=w; v.yres_virtual=h; v.bits_per_pixel=bpp;
			(void)user_copy_to(arg,(const uint8_t*)&v,sizeof(v));
			return 0;
		}
		if(req==FBIOBLANK){
			uint32_t mode=0;
			if(user_copy_from((uint8_t*)&mode,arg,sizeof(mode))) return -EFAULT;
			return (mode>4)?-EINVAL:0;
		}
		if(req==FBIOGETCMAP){
			struct fb_cmap_k cmap;
			if(user_copy_from((uint8_t*)&cmap,arg,sizeof(cmap))) return -EFAULT;

			cmap.start=0;
			return user_copy_to(arg,(const uint8_t*)&cmap,sizeof(cmap))?-EFAULT:0;
		}
		if(req==FBIOPUTCMAP){

			return 0;
		}
		if(req==FBIOGET_VBLANK){
			uint32_t v=0;
			return user_copy_to(arg,(const uint8_t*)&v,sizeof(v))?-EFAULT:0;
		}
		if(req==FBIO_WAITFORVSYNC) return 0;
		if(req==FBIOGET_INFO) return user_copy_to(arg,(const uint8_t*)vals,sizeof(vals)) ? -EFAULT : 0;
		return -ENOTTY;
	}
	if(f->type==FD_PSEUDO && ((uintptr_t)f->private_data==DEV_INPUT || (uintptr_t)f->private_data==DEV_INPUT_MOUSE)){
		const int mouse=((uintptr_t)f->private_data==DEV_INPUT_MOUSE);
        if(!mouse && !evdev_keyboard_ioctl_logged) {
            debugcon_puts("[EVDEV KEY IOCTL FIRST]"); debug_hex64(" pid=0x",(uint64_t)tasks[current_task].pid);
            debug_hex64(" fd=0x",(uint64_t)fd); debug_hex64(" req=0x",req); debugcon_puts("\n"); evdev_keyboard_ioctl_logged=1;
        }
		if(mouse){ debugcon_puts("[EVDEV IOCTL] req=0x"); debug_hex64("",req); debugcon_puts("\n"); }
		const uint64_t ureq=req;
    		const uint8_t nr=(uint8_t)(ureq & 0xffULL);
		const uint64_t size=(ureq>>16)&0x3fffULL;

		if(req==EVIOCGVERSION){ uint32_t v=0x00010001; return user_copy_to(arg,(const uint8_t*)&v,sizeof(v))?-EFAULT:0; }
		if(req==EVIOCGID){ struct input_id_k id={BUS_I8042,0x0002,(uint16_t)(mouse?0x0001:0x0001),0x0000}; return user_copy_to(arg,(const uint8_t*)&id,sizeof(id))?-EFAULT:0; }
		if(nr==0x06){
			uint64_t n=(ureq>>16)&0x3fffULL;
			const char *name=mouse?"YabroOS PS/2 Mouse":"YabroOS PS/2 Keyboard";
			uint64_t name_len=0; while(name[name_len]) name_len++;
			uint64_t m=0;
			if(n) m=name_len<(n-1)?name_len:(n-1);
			if(m && user_copy_to(arg,(const uint8_t*)name,m))return-EFAULT;
			if(n){ char z=0; if(user_copy_to(arg+m,(const uint8_t*)&z,1))return-EFAULT; }
			return (int64_t)m;
		}

		if(nr==0x07){
			const char *phys=mouse?"i8042/serio0/input0":"i8042/serio0/input1";
			uint64_t plen=0; while(phys[plen]) plen++;
			uint64_t copy=size<plen+1?size:plen+1;
			if(copy && user_copy_to(arg,(const uint8_t*)phys,copy)) return -EFAULT;
			return (int64_t)(copy?copy-1:0);
		}
		if(nr==0x08) return -ENOENT;
		if(nr==0x09){
			if(size==0) return 0;
			uint8_t prop[8]; for(size_t i=0;i<sizeof(prop);i++)prop[i]=0;
			if(mouse) prop[INPUT_PROP_POINTER>>3] |= (uint8_t)(1u<<(INPUT_PROP_POINTER&7));
			uint64_t copy=size<sizeof(prop)?size:sizeof(prop);
			if(user_copy_to(arg,prop,copy))return-EFAULT;
			return (int64_t)copy;
		}

		if(nr>=0x20 && nr<=0x3f){
			uint64_t ev=(uint64_t)(nr-0x20);
			uint8_t bits[128]; for(size_t i=0;i<sizeof(bits);i++)bits[i]=0;
			if(ev==0){

				bits[EV_SYN>>3] |= (uint8_t)(1u<<(EV_SYN&7));
				bits[EV_KEY>>3] |= (uint8_t)(1u<<(EV_KEY&7));
				if(mouse) bits[EV_REL>>3] |= (uint8_t)(1u<<(EV_REL&7));
				else bits[EV_REP>>3] |= (uint8_t)(1u<<(EV_REP&7));
			} else if(ev==EV_KEY){
				if(mouse){
					const uint16_t c[]={BTN_LEFT,BTN_RIGHT,BTN_MIDDLE};
					for(size_t j=0;j<sizeof(c)/sizeof(c[0]);j++) bits[c[j]>>3] |= (uint8_t)(1u<<(c[j]&7));
				} else {
					for(uint16_t c=1;c<=88;c++) bits[c>>3] |= (uint8_t)(1u<<(c&7));

					static const uint16_t extra_keys[]={96,97,98,100,102,103,104,105,106,107,108,109,110,111,125,126,127};
					for(size_t j=0;j<sizeof(extra_keys)/sizeof(extra_keys[0]);j++) {
						uint16_t c=extra_keys[j]; bits[c>>3] |= (uint8_t)(1u<<(c&7));
					}
				}
			} else if(ev==EV_REL && mouse){
				bits[REL_X>>3] |= (uint8_t)(1u<<(REL_X&7));
				bits[REL_Y>>3] |= (uint8_t)(1u<<(REL_Y&7));
			} else if(ev==EV_REP && !mouse){

				bits[0] |= 0x03;
			}
			uint64_t copy=size<sizeof(bits)?size:sizeof(bits);
			if(copy && user_copy_to(arg,bits,copy))return-EFAULT;
			return (int64_t)copy;
		}

		if(nr==0x18 || nr==0x19 || nr==0x1a || nr==0x1b){
			uint8_t bits[128]; for(size_t i=0;i<sizeof(bits);i++)bits[i]=0;
			if(nr==0x18 && mouse){
				bits[BTN_LEFT>>3] |= (uint8_t)(((mouse_buttons>>0)&1u)<<(BTN_LEFT&7));
				bits[BTN_RIGHT>>3] |= (uint8_t)(((mouse_buttons>>1)&1u)<<(BTN_RIGHT&7));
				bits[BTN_MIDDLE>>3] |= (uint8_t)(((mouse_buttons>>2)&1u)<<(BTN_MIDDLE&7));
			} else if(nr==0x18) {
				for(size_t i=0;i<sizeof(bits);i++) bits[i]=keyboard_key_state[i];
			}
			uint64_t copy=size<sizeof(bits)?size:sizeof(bits); if(copy&&user_copy_to(arg,bits,copy))return-EFAULT; return (int64_t)copy;
		}

		if(nr>=0x40 && nr<=0x7f) return -EINVAL;

		if(req==EVIOCGRAB || req==EVIOCSCLOCKID) return 0;
		if(req==EVIOCGREP){
			uint32_t rep[2]={keyboard_repeat_delay_ms,keyboard_repeat_period_ms};
			return user_copy_to(arg,(const uint8_t*)rep,sizeof(rep))?-EFAULT:0;
		}
		if(req==EVIOCSREP){
			if(mouse) return -EINVAL;
			uint32_t rep[2];
			if(user_copy_from((uint8_t*)rep,arg,sizeof(rep))) return -EFAULT;

			keyboard_repeat_delay_ms=rep[0]?rep[0]:250;
			keyboard_repeat_period_ms=rep[1]?rep[1]:33;
			return 0;
		}
		return -ENOTTY;
	}
	if(f->type==FD_PTY_MASTER || f->type==FD_PTY_SLAVE){
		pty_state_k *p=(pty_state_k*)f->private_data; if(!p)return-EIO;
        if (pty_dbg_ioctl_count < 24) {
            debugcon_puts("[PTYDBG IOCTL] "); debug_hex64(" fd=0x", (uint64_t)(uint32_t)fd);
            debug_hex64(" req=0x", req); debug_hex64(" type=0x", (uint64_t)(uint32_t)f->type);
            debugcon_puts("\n"); pty_dbg_ioctl_count++;
        }
		if(req==TIOCGPTN){
            uint32_t n=p->index; int rc=user_copy_to(arg,(const uint8_t*)&n,sizeof(n))?-EFAULT:0;
            if (!pty_dbg_master_ioctl) { pty_dbg("[PTYDBG TIOCGPTN]", fd, f); pty_dbg_master_ioctl=1; }
            return rc;
        }
		if(req==TIOCSPTLCK){
            uint32_t lock=0; if(user_copy_from((uint8_t*)&lock,arg,sizeof(lock)))return-EFAULT; p->unlocked=(lock==0);
            if (!pty_dbg_unlock) { pty_dbg("[PTYDBG TIOCSPTLCK]", fd, f); debug_hex64(" lock_arg=0x", lock); debugcon_puts("\n"); pty_dbg_unlock=1; }
            return 0;
        }
		if(req==TIOCGWINSZ){ struct winsize_k w={p->rows,p->cols,p->xpixel,p->ypixel}; return user_copy_to(arg,(const uint8_t*)&w,sizeof(w))?-EFAULT:0; }

        if(req==TIOCSWINSZ){
            struct winsize_k w;
            if(user_copy_from((uint8_t*)&w,arg,sizeof(w))) return -EFAULT;
            int changed=(p->rows!=w.rows || p->cols!=w.cols ||
                         p->xpixel!=w.xpixel || p->ypixel!=w.ypixel);
            p->rows=w.rows; p->cols=w.cols; p->xpixel=w.xpixel; p->ypixel=w.ypixel;

            if(changed && p->fg_pgrp>0) (void)tty_pgrp_signal(p,SIGWINCH);
            return 0;
        }
		if(req==TCGETS) return user_copy_to(arg,p->termios,60)?-EFAULT:0;
        if(req==TCSETSW || req==TCSETSF){

            if(p->s2m_count && p->master_open){
                if(sf && task_block_io(sf,IO_WAIT_PTY_DRAIN,16)==0)return 0;
                return -EAGAIN;
            }
        }
        if(req==TCSETS || req==TCSETSW || req==TCSETSF){
            uint8_t nt[60]; if(user_copy_from(nt,arg,sizeof(nt)))return-EFAULT;
            for(size_t i=0;i<sizeof(p->termios);i++)p->termios[i]=nt[i];
            if(req==TCSETSF)pty_flush_input(p);
            return 0;
        }
        if(req==TCFLSH){
            int which=(int)arg;
            if(which<TCIFLUSH || which>TCIOFLUSH)return-EINVAL;
            if(which==TCIFLUSH || which==TCIOFLUSH)pty_flush_input(p);
            if(which==TCOFLUSH || which==TCIOFLUSH)pty_flush_output(p);
            task_wake_io_waiters(); return 0;
        }
        if(req==TCXONC){
            int action=(int)arg;
            if(action==TCOOFF)p->output_stopped=1;
            else if(action==TCOON){p->output_stopped=0;task_wake_io_waiters();}
            else if(action==TCIOFF || action==TCION){
                uint8_t c=tty_cc(p->termios,action==TCIOFF?VSTOP:VSTART);
                if(!c)return 0;
                if(pty_output_write(p,&c,1)!=1)return-EAGAIN;
                task_wake_io_waiters();
            } else return-EINVAL;
            return 0;
        }
		if(req==TIOCSCTTY){ if(f->type!=FD_PTY_SLAVE)return-ENOTTY; task_t*t=&tasks[current_task]; if(t->session!=t->pid)return-EPERM; if(t->controlling_tty!=-1 && t->controlling_tty!=0)return-EPERM; t->controlling_tty=0; p->fg_pgrp=t->pgrp; if(p->shell_pgrp<=0)p->shell_pgrp=t->pgrp; if(p->session<=0)p->session=t->session; return 0; }
		if(req==TIOCGPGRP){ int pg=p->fg_pgrp; return user_copy_to(arg,(const uint8_t*)&pg,sizeof(pg))?-EFAULT:0; }
		if(req==TIOCSPGRP){ int pg=0; if(f->type!=FD_PTY_SLAVE)return-ENOTTY; if(user_copy_from((uint8_t*)&pg,arg,sizeof(pg)))return-EFAULT; if(pg<=0)return-EINVAL; int ok=0; for(int i=0;i<MAX_TASKS;i++)if(tasks[i].used&&tasks[i].pgrp==pg&&tasks[i].session==tasks[current_task].session){ok=1;break;} if(!ok)return-EPERM; if(p->shell_pgrp<=0)p->shell_pgrp=tasks[current_task].pgrp>0?tasks[current_task].pgrp:pg; if(p->session<=0)p->session=tasks[current_task].session; p->fg_pgrp=pg; return 0; }
		return -ENOTTY;
	}
	if(req==TIOCGWINSZ && (fd==1||fd==2||fd_is_terminal(fd))){
		struct winsize_k w={30,80,640,480};
		(void)console_fb_get_size(&w.rows,&w.cols,&w.xpixel,&w.ypixel);
		return user_copy_to(arg,(uint8_t*)&w,sizeof(w))?-EFAULT:0;
	}

    if(fd_is_terminal(fd) && req==TIOCGPGRP){
        task_init_root();
        int pg=console_fg_pgrp>0?console_fg_pgrp:tasks[current_task].pgrp;
        return user_copy_to(arg,(const uint8_t*)&pg,sizeof(pg))?-EFAULT:0;
    }
    if(fd_is_terminal(fd) && req==TIOCSPGRP){
        task_init_root();
        int pg=0;
        if(user_copy_from((uint8_t*)&pg,arg,sizeof(pg)))return-EFAULT;
        if(pg<=0)return-EINVAL;
        int ok=0;
        for(int i=0;i<MAX_TASKS;i++){
            if(tasks[i].used && tasks[i].pgrp==pg &&
               tasks[i].session==tasks[current_task].session){ok=1;break;}
        }
        if(!ok)return-EPERM;
        if (console_shell_pgrp<=0) console_shell_pgrp=tasks[current_task].pgrp>0?tasks[current_task].pgrp:pg;
        console_fg_pgrp=pg;
        return 0;
    }
	if(!fd_is_terminal(fd)) return -ENOTTY;
	if(req==TCGETS) return user_copy_to(arg,console_termios_k,TERMIOS_K_SIZE)?-EFAULT:0;
	if(req==TCSETS) return user_copy_from(console_termios_k,arg,TERMIOS_K_SIZE)?-EFAULT:0;
	return -ENOTTY;
}

#define PROT_READ 1ULL
#define PROT_WRITE 2ULL
#define PROT_EXEC 4ULL
#define MAP_SHARED 0x1ULL
#define MAP_FIXED 0x10ULL
#define MAP_ANON 0x20ULL
#define MAP_PRIVATE 0x2ULL
static uint64_t prot_to_pte(uint64_t prot){
	uint64_t f=0;
	if(prot&PROT_WRITE)f|=PTE_W;
	if(!(prot&PROT_EXEC))f|=PTE_NX;
	return f;
}

static int kernel_file_read_at(int fd, uint64_t off, uint8_t *dst, size_t len) {
	if (fd < 0 || fd >= MAX_FILES || !files[fd].in_use || !dst) return -EBADF;
	struct file_desc *f = &files[fd];
	if (f->type != FD_FILE) return -EBADF;
	if (off >= f->size || !len) return 0;
	size_t want = len;
	if ((uint64_t)want > f->size - off) want = (size_t)(f->size - off);

	if (fd_uses_bootfs(f)) {
		const char *bp = fd_bootfs_path(f);
		if (!bp) return -EIO;
		return (int)bootfs_read_root_file(bp, off, dst, want);
	}
	if (f->vnode && vfs_file_is_regular(f->vnode)) {
		uint64_t size = vfs_file_size(f->vnode);
		if (off >= size) return 0;
		if ((uint64_t)want > size - off) want = (size_t)(size - off);
		const uint8_t *src = vfs_file_data(f->vnode) + off;
		for (size_t i=0; i<want; i++) dst[i]=src[i];
		return (int)want;
	}
	if (f->data) {
		for (size_t i=0; i<want; i++) dst[i]=f->data[off+i];
		return (int)want;
	}
	return -EIO;
}

static uint64_t task_shared_mmap_cursor(task_t *t) {
	if (!t) return USER_MMAP_MIN;
	uint64_t cr3=t->ctx.cr3 & PAGE_MASK;
	uint64_t high=USER_MMAP_MIN;
	for (int i=0;i<MAX_TASKS;i++) {
		if (!tasks[i].used) continue;
		if (&tasks[i] != t && (!cr3 || (tasks[i].ctx.cr3 & PAGE_MASK) != cr3)) continue;
		if (tasks[i].mmap_next > high) high=tasks[i].mmap_next;
	}
	for (int i=0;i<MAX_TASKS;i++) {
		if (!tasks[i].used) continue;
		if (&tasks[i] != t && (!cr3 || (tasks[i].ctx.cr3 & PAGE_MASK) != cr3)) continue;
		tasks[i].mmap_next=high;
	}
	return high;
}
static void task_shared_mmap_cursor_advance(task_t *t, uint64_t next) {
	if (!t) return;
	uint64_t high=task_shared_mmap_cursor(t);
	if (next > high) high=next;
	uint64_t cr3=t->ctx.cr3 & PAGE_MASK;
	for (int i=0;i<MAX_TASKS;i++) {
		if (!tasks[i].used) continue;
		if (&tasks[i] != t && (!cr3 || (tasks[i].ctx.cr3 & PAGE_MASK) != cr3)) continue;
		tasks[i].mmap_next=high;
	}
}

int64_t sys_mmap(uint64_t addr,uint64_t len,uint64_t prot,uint64_t flags,int fd,uint64_t off){
	task_init_root();
	task_t *t = &tasks[current_task];
	if (!t->ctx.cr3 || vmm_user_activate(t->ctx.cr3) != 0) {
		debugcon_puts("[MMAP] invalid current task CR3\n");
		debug_hex64("[MMAP] task cr3=0x", t->ctx.cr3);
		return -EFAULT;
	}
	uint64_t mmap_cursor = task_shared_mmap_cursor(t);
	if (mmap_cursor < USER_MMAP_MIN) mmap_cursor = USER_MMAP_MIN;
	if(!len || len>(1ULL<<30))return-EINVAL;
	if((off & (PAGE_SIZE-1)) != 0) return-EINVAL;
	if(user_prot_wx(prot))return-EACCES_K;
	if(!(flags&MAP_ANON)&&fd<0)return-EBADF;
	if((flags & MAP_FIXED) && (addr & (PAGE_SIZE-1)))return-EINVAL;
	if(!(flags&MAP_ANON) && fd>=0 && fd<MAX_FILES && (!files[fd].in_use ||
	   (files[fd].type!=FD_FILE && !(files[fd].type==FD_PSEUDO &&
		 (uintptr_t)files[fd].private_data==DEV_FB))))return-EBADF;
	if(!(flags&MAP_ANON) && fd>=0 && fd<MAX_FILES && files[fd].in_use &&
	   files[fd].type==FD_PSEUDO && (uintptr_t)files[fd].private_data==DEV_FB) {
		uint64_t fbsize=graphics_fb_size();
		uint64_t phys=graphics_fb_phys();
		uint64_t maplen=(len+PAGE_SIZE-1)&PAGE_MASK;

		uint64_t fb_phys_offset=phys & (PAGE_SIZE-1);
		uint64_t fb_remaining=(off<fbsize)?fb_phys_offset+fbsize-off:0;
		uint64_t fb_map_limit=(fb_remaining+PAGE_SIZE-1)&PAGE_MASK;

		if(fb_trace_allowed()) {
			debugcon_puts("[FB-MMAP] fd=0x"); debug_hex64("", (uint64_t)fd);
			debug_hex64(" addr=0x", addr); debug_hex64(" off=0x", off);
			debug_hex64(" len=0x", len); debug_hex64(" prot=0x", prot);
			debug_hex64(" flags=0x", flags); debug_hex64(" fbsize=0x", fbsize);
			debug_hex64(" phys=0x", phys); debugcon_puts("\n");
		}
		if((off & (PAGE_SIZE-1)) != 0 || (prot & PROT_EXEC) ||
		   !(flags & (MAP_SHARED|MAP_PRIVATE)) || !fbsize || off >= fbsize ||
		   maplen == 0 || maplen > fb_map_limit) {
			if(fb_trace_allowed()) debugcon_puts("[FB-MMAP-RET] EINVAL\n");
			return -EINVAL;
		}
		uint64_t base=(addr ? ((addr+PAGE_SIZE-1)&PAGE_MASK) : ((mmap_cursor+PAGE_SIZE-1)&PAGE_MASK));
		if(!user_mmap_range_ok(base,maplen)) { if(fb_trace_allowed()) debugcon_puts("[FB-MMAP-RET] ENOMEM\n"); return -ENOMEM; }

		int64_t mr=graphics_fb_map(base,off,len);
		if(mr<0) { if(fb_trace_allowed()) { debug_hex64("[FB-MMAP-RET] graphics_fb_map=0x", (uint64_t)(-mr)); debugcon_puts("\n"); } return -EIO; }

		framebuffer_users++;
		t->fb_map_count++;
		framebuffer_console_active = 0;
		console_fb_cursor_suspend();
		if(base+maplen>mmap_cursor) task_shared_mmap_cursor_advance(t,base+maplen);
		if(fb_trace_allowed()) {
			debugcon_puts("[FB-OWNERSHIP] framebuffer mapped by userspace; console FB disabled\n");
			debug_hex64("[FB-OWNERSHIP] users=0x", (uint64_t)framebuffer_users); debugcon_puts("\n");
			debugcon_puts("[FBTRACE] mmap framebuffer OK base=0x");
			debug_hex64("", base);
			debug_hex64(" size=0x", maplen);
			debug_hex64(" phys=0x", phys);
			debug_hex64(" off=0x", off);
			debugcon_puts("\n");
		}
		return (int64_t)base;
	}
	if (len > UINT64_MAX - (PAGE_SIZE - 1)) return -EINVAL;
	uint64_t size=(len+PAGE_SIZE-1)&PAGE_MASK;
	if(size==0 || size>USER_LIMIT-USER_MMAP_MIN)return-ENOMEM;

	uint64_t base;
	if (flags & MAP_FIXED) {

		base = addr;
		if (!user_any_mapping_range_ok(base, size)) {
			debug_hex64("[MMAP] MAP_FIXED bad addr=0x", base);
			debug_hex64("[MMAP] size=0x", size);
			return -EINVAL;
		}
	} else {
		if (mmap_cursor > UINT64_MAX - (PAGE_SIZE - 1)) return -ENOMEM;
		if (!user_mmap_find_gap(mmap_cursor, size, t->heap_end, &base)) {
			debug_hex64("[MMAP] no free gap base=0x", mmap_cursor);
			debug_hex64("[MMAP] size=0x", size);
			return -ENOMEM;
		}
	}

	if ((flags & MAP_ANON) && prot == 0) {
		if (flags & MAP_FIXED) {
			for (uint64_t va=base; va<base+size; va+=PAGE_SIZE) {
				uint64_t old_phys=0;
				if (vmm_user_unmap(va,&old_phys)==0 && old_phys) pmm_free_page(old_phys);
			}
		}
		int lrc=task_lazy_mmap_transform_shared(t,base,base+size,LAZY_MMAP_REPLACE,prot);
		if (lrc<0) return lrc;
		if (base+size>mmap_cursor) task_shared_mmap_cursor_advance(t,base+size);
		vmm_user_flush_tlb();
		mmap_watch_log("[MMAP-LAZY]",base,size,prot,flags,fd,off);
		return (int64_t)base;
	}
	if (flags & MAP_FIXED) {
		if (task_lazy_mmap_transform_shared(t,base,base+size,LAZY_MMAP_REMOVE,0)<0) return -ENOMEM;
	}
	uint64_t mapped=0;
	for(uint64_t va=base;va<base+size;va+=PAGE_SIZE){
		uint64_t phys=pmm_alloc_page();
		if(!phys){
			debugcon_puts("[MMAP-ENOMEM] physical pages exhausted\n");
			debug_hex64("[MMAP-ENOMEM] request_bytes=0x", size);
			debug_hex64("[MMAP-ENOMEM] free_pages=0x", pmm_free_count());
			debug_hex64("[MMAP-ENOMEM] mmap_base=0x", base);
			debug_hex64("[MMAP-ENOMEM] flags=0x", flags);
			debug_hex64("[MMAP-ENOMEM] prot=0x", prot);
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
		if(flags & MAP_FIXED){
			uint64_t old_phys=0;
			if(vmm_user_unmap(va,&old_phys)==0 && old_phys)
				pmm_free_page(old_phys);
		}
		for(size_t i=0;i<PAGE_SIZE;i++) p[i]=0;
		if(!(flags&MAP_ANON) && fd>=0){
			uint64_t file_off=off+(va-base);
			if ((va == base) && files[fd].in_use) {
				debugcon_puts("[MMAP-FILE] fd=0x");
				debug_hex64("", (uint64_t)fd);
				debugcon_puts(" base=0x");
				debug_hex64("", base);
				debugcon_puts(" off=0x");
				debug_hex64("", off);
				debugcon_puts(" size=0x");
				debug_hex64("", size);
				debugcon_puts(" fsize=0x");
				debug_hex64("", files[fd].size);
				debugcon_puts("\n");
			}
			size_t page_bytes=PAGE_SIZE;
			if(file_off>=files[fd].size) page_bytes=0;
			else if((uint64_t)page_bytes>files[fd].size-file_off)
				page_bytes=(size_t)(files[fd].size-file_off);
			if(page_bytes){
				int got=kernel_file_read_at(fd,file_off,p,page_bytes);
				if ((va == base) && files[fd].in_use && got > 0) {
					debugcon_puts("[MMAP-FILE] first16=");
					for (int di=0; di<16 && di<got; di++) {
						static const char hx[] = "0123456789abcdef";
						char q[2];
						q[0]=hx[(p[di]>>4)&15]; q[1]=hx[p[di]&15];
						debugcon_puts(q);
					}
					debugcon_puts("\n");
				}
				if(got<0 || (size_t)got!=page_bytes){
					pmm_free_page(phys);
					for(uint64_t undo=base;undo<base+mapped;undo+=PAGE_SIZE){
						uint64_t old_phys=0;
						if(vmm_user_unmap(undo,&old_phys)==0 && old_phys) pmm_free_page(old_phys);
					}
					return-EIO;
				}
			}
		}
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
	if (base + size > mmap_cursor)
		task_shared_mmap_cursor_advance(t,base+size);
	vmm_user_flush_tlb();
	mmap_watch_log("[MMAP-WATCH]", base, size, prot, flags, fd, off);
	return base;
}

int64_t sys_munmap(uint64_t addr,uint64_t len){
	task_init_root();
	task_t *t = &tasks[current_task];
	if (!t->ctx.cr3 || vmm_user_activate(t->ctx.cr3) != 0) return -EFAULT;
	if(addr&(PAGE_SIZE-1)||!len)return-EINVAL;
	uint64_t size=(len+PAGE_SIZE-1)&PAGE_MASK;
	uint64_t fs = task_read_fs_base();
	uint64_t gs = task_read_gs_base();
	if ((fs && fs >= addr && fs - addr < size) ||
	    (gs && gs >= addr && gs - addr < size)) {
		debug_hex64("[MMAP-SAFE] refusing munmap containing FS/GS base addr=0x", addr);
		debug_hex64("[MMAP-SAFE] size=0x", size);
		debug_hex64("[MMAP-SAFE] fs=0x", fs);
		debug_hex64("[MMAP-SAFE] gs=0x", gs);
		return -EINVAL;
	}
	if(size==0 || !user_addr_range_ok(addr,size))return-EINVAL;
	if(!user_any_mapping_range_ok(addr,size))return-EINVAL;
	mmap_watch_log("[MUNMAP-WATCH]", addr, size, 0, 0, -1, 0);

	{
		uint64_t fbsize=graphics_fb_size();
		uint64_t fbphys=graphics_fb_phys() & PAGE_MASK;
		uint64_t first_phys=0;
		uint64_t fb_end=fbphys + ((fbsize + (graphics_fb_phys() & (PAGE_SIZE-1)) +
								   PAGE_SIZE-1) & PAGE_MASK);
		if(fbsize && vmm_user_translate(addr,&first_phys,NULL)==0 &&
		   first_phys>=fbphys && first_phys<fb_end) {
			int64_t ur = graphics_fb_unmap(addr,size);
			if (ur == 0 && t->fb_map_count > 0) {
				t->fb_map_count--;
				if (framebuffer_users > 0) framebuffer_users--;

			}
			return ur;
		}
	}

	if (task_lazy_mmap_transform_shared(t,addr,addr+size,LAZY_MMAP_REMOVE,0)<0) return -ENOMEM;
	for(uint64_t va=addr;va<addr+size;va+=PAGE_SIZE){
		uint64_t phys=0;

		if(vmm_user_unmap(va,&phys)==0 && phys)pmm_free_page(phys);
	}
	vmm_user_flush_tlb();
	return 0;
}

int64_t sys_mprotect(uint64_t addr,uint64_t len,uint64_t prot){
	task_init_root();
	task_t *t = &tasks[current_task];
	if (!t->ctx.cr3 || vmm_user_activate(t->ctx.cr3) != 0) return -EFAULT;
	if(addr&(PAGE_SIZE-1)||!len)return-EINVAL;
	if(user_prot_wx(prot))return-EACCES_K;
	uint64_t size=(len+PAGE_SIZE-1)&PAGE_MASK;
	if(size==0 || !user_addr_range_ok(addr,size))return-EINVAL;
	if(!user_any_mapping_range_ok(addr,size))return-EINVAL;
	if (task_lazy_mmap_transform_shared(t,addr,addr+size,LAZY_MMAP_SET_PROT,prot)<0) return -ENOMEM;
	for(uint64_t va=addr;va<addr+size;va+=PAGE_SIZE) {
		uint64_t phys=0;
		if (vmm_user_translate(va,&phys,NULL)!=0) {

			if (!task_lazy_mmap_protection(t,va,NULL)) return -EFAULT;
			if (prot == 0) continue;
			uint64_t new_phys=pmm_alloc_page();
			if (!new_phys) return -ENOMEM;
			uint8_t *page_bytes=(uint8_t *)pmm_phys_to_virt(new_phys);
			if (!page_bytes) { pmm_free_page(new_phys); return -ENOMEM; }
			for (size_t i=0;i<PAGE_SIZE;i++) page_bytes[i]=0;
			if (vmm_user_map(va,new_phys,prot_to_pte(prot))!=0) {
				pmm_free_page(new_phys);
				return -ENOMEM;
			}
			continue;
		}
		if (prot == 0) {

			uint64_t old_phys=0;
			if (vmm_user_unmap(va,&old_phys)==0 && old_phys) pmm_free_page(old_phys);
			continue;
		}
		if(vmm_user_protect(va,prot_to_pte(prot)))return-EFAULT;
	}
	vmm_user_flush_tlb();
	return 0;
}

#define MREMAP_MAYMOVE 1ULL
#define MREMAP_FIXED 2ULL
#define MREMAP_DONTUNMAP 4ULL

static int user_mapping_range_free_any(uint64_t addr, uint64_t len) {
	if (!len || addr >= USER_LIMIT || len > USER_LIMIT - addr) return 0;
	uint64_t first = addr & PAGE_MASK;
	uint64_t last = (addr + len - 1) & PAGE_MASK;
	for (uint64_t va = first;; va += PAGE_SIZE) {
		uint64_t phys = 0;
		if (vmm_user_translate(va, &phys, NULL) == 0) return 0;
		if (va == last) break;
		if (va > USER_LIMIT - PAGE_SIZE) return 0;
	}
	return 1;
}

static uint64_t mremap_pte_to_prot(uint64_t pte) {
	uint64_t prot = PROT_READ;
	if (pte & PTE_W) prot |= PROT_WRITE;
	if (!(pte & PTE_NX)) prot |= PROT_EXEC;
	return prot;
}

int64_t sys_mremap(uint64_t old_addr,uint64_t old_size,uint64_t new_size,uint64_t flags,uint64_t new_addr){
	task_init_root();
	task_t *t = &tasks[current_task];
	if (!t->ctx.cr3 || vmm_user_activate(t->ctx.cr3) != 0) return-EFAULT;
	if(!old_size||!new_size)return-EINVAL;
	if(old_addr&(PAGE_SIZE-1))return-EINVAL;
	if((old_size > USER_LIMIT) || (new_size > USER_LIMIT))return-EINVAL;
	if(flags & ~(MREMAP_MAYMOVE|MREMAP_FIXED|MREMAP_DONTUNMAP))return-EINVAL;
	if((flags & MREMAP_FIXED) && !(flags & MREMAP_MAYMOVE))return-EINVAL;
	if(flags & MREMAP_DONTUNMAP)return-EINVAL;
	uint64_t old_map_size=(old_size+PAGE_SIZE-1)&PAGE_MASK;
	uint64_t new_map_size=(new_size+PAGE_SIZE-1)&PAGE_MASK;
	if(!old_map_size || !new_map_size)return-EINVAL;
	if(!user_any_mapping_range_ok(old_addr,old_map_size))return-EINVAL;
	if(old_addr>=USER_LIMIT)return-EINVAL;
	if(new_map_size > USER_LIMIT) return-EINVAL;

	uint64_t first_phys=0, first_pte=0;
	for(uint64_t va=old_addr; va<old_addr+old_map_size; va+=PAGE_SIZE){
		uint64_t phys=0,pte=0;
		if(vmm_user_translate(va,&phys,&pte)!=0) return-EFAULT;
		if(va==old_addr){ first_phys=phys; first_pte=pte; }
	}
	uint64_t prot=mremap_pte_to_prot(first_pte);

	if (old_map_size==new_map_size) return old_addr;

	if(new_map_size<old_map_size){
		uint64_t tail=old_map_size-new_map_size;
		if(sys_munmap(old_addr+new_map_size,tail)<0)return-EFAULT;
		return old_addr;
	}

	if(!(flags & MREMAP_MAYMOVE)){
		uint64_t grow_addr=old_addr+old_map_size;
		uint64_t grow_len=new_map_size-old_map_size;
		if(!user_mapping_range_free_any(grow_addr,grow_len))return-ENOMEM;
		uint64_t mapped=0;
		for(uint64_t va=grow_addr;va<grow_addr+grow_len;va+=PAGE_SIZE){
			uint64_t phys=pmm_alloc_page();
			if(!phys){
				for(uint64_t undo=grow_addr;undo<grow_addr+mapped;undo+=PAGE_SIZE){uint64_t op=0;if(vmm_user_unmap(undo,&op)==0&&op)pmm_free_page(op);}
				return-ENOMEM;
			}
			uint8_t *pg=(uint8_t*)pmm_phys_to_virt(phys);
			if(!pg){pmm_free_page(phys);for(uint64_t undo=grow_addr;undo<grow_addr+mapped;undo+=PAGE_SIZE){uint64_t op=0;if(vmm_user_unmap(undo,&op)==0&&op)pmm_free_page(op);}return-ENOMEM;}
			for(uint64_t i=0;i<PAGE_SIZE;i++)pg[i]=0;
			if(vmm_user_map(va,phys,prot_to_pte(prot))!=0){pmm_free_page(phys);for(uint64_t undo=grow_addr;undo<grow_addr+mapped;undo+=PAGE_SIZE){uint64_t op=0;if(vmm_user_unmap(undo,&op)==0&&op)pmm_free_page(op);}return-ENOMEM;}
			mapped+=PAGE_SIZE;
		}
		vmm_user_flush_tlb();
		return old_addr;
	}

	uint64_t target=0;
	if(flags & MREMAP_FIXED){
		target=new_addr;
		if(target&(PAGE_SIZE-1) || !user_any_mapping_range_ok(target,new_map_size))return-EINVAL;
		if(target==old_addr)return-EINVAL;
		if(target < old_addr+old_map_size && old_addr < target+new_map_size)return-EINVAL;

		for(uint64_t va=target;va<target+new_map_size;va+=PAGE_SIZE){
			uint64_t op=0;
			if(vmm_user_unmap(va,&op)==0 && op)pmm_free_page(op);
		}
	}else{
		int64_t n=sys_mmap(0,new_map_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
		if(n<0)return n;
		target=(uint64_t)n;
	}

	uint64_t copy=old_size<new_size?old_size:new_size;
	uint8_t tmp[256];
	for(uint64_t o=0;o<copy;o+=sizeof(tmp)){
		uint64_t q=copy-o;if(q>sizeof(tmp))q=sizeof(tmp);
		if(user_copy_from(tmp,old_addr+o,q)||user_copy_to(target+o,tmp,q)){
			sys_munmap(target,new_map_size);
			return-EFAULT;
		}
	}
	if((prot & (PROT_WRITE|PROT_EXEC)) != (PROT_WRITE|PROT_EXEC)){
		if(sys_mprotect(target,new_map_size,prot)<0){sys_munmap(target,new_map_size);return-EFAULT;}
	}
	if(!(flags & MREMAP_DONTUNMAP)){
		if(sys_munmap(old_addr,old_map_size)<0){sys_munmap(target,new_map_size);return-EFAULT;}
	}
	return target;
}

static void task_shared_heap_sync(task_t *t, uint64_t base, uint64_t end) {
	if (!t) return;
	uint64_t cr3=t->ctx.cr3 & PAGE_MASK;
	for (int i=0;i<MAX_TASKS;i++) {
		if (!tasks[i].used) continue;
		if (&tasks[i] != t && (!cr3 || (tasks[i].ctx.cr3 & PAGE_MASK) != cr3)) continue;
		tasks[i].heap_base=base;
		tasks[i].heap_end=end;
	}
}

int64_t sys_brk(uint64_t addr){
	task_init_root();
	task_t *t = &tasks[current_task];
	if (!t->ctx.cr3 || vmm_user_activate(t->ctx.cr3) != 0) return -EFAULT;

	uint64_t base = t->heap_base ? t->heap_base : USER_HEAP_BASE;
	uint64_t old  = t->heap_end ? t->heap_end : base;
	uint64_t cr3=t->ctx.cr3 & PAGE_MASK;
	for (int i=0;i<MAX_TASKS;i++) {
		if (!tasks[i].used) continue;
		if (&tasks[i] != t && (!cr3 || (tasks[i].ctx.cr3 & PAGE_MASK) != cr3)) continue;
		if (tasks[i].heap_base) base=tasks[i].heap_base;
		if (tasks[i].heap_end > old) old=tasks[i].heap_end;
	}
	if (old < base) old=base;
	task_shared_heap_sync(t,base,old);

	if (addr == 0) {
		debug_hex64("[BRK] query heap_base=0x", base);
		debug_hex64("[BRK] query heap_end=0x", old);
		return (int64_t)old;
	}

	if (addr < base || addr >= USER_HEAP_MAX)
		return (int64_t)old;

	if (addr > old) {
		uint64_t a=(old+PAGE_SIZE-1)&PAGE_MASK;
		uint64_t b=(addr+PAGE_SIZE-1)&PAGE_MASK;
		if(b>a && !user_addr_range_ok(a,b-a)) return (int64_t)old;

		uint64_t mapped_start=a;
		for(;a<b;a+=PAGE_SIZE){
			uint64_t existing_phys=0, existing_pte=0;
			if(vmm_user_translate(a,&existing_phys,&existing_pte)==0){
				debug_hex64("[BRK] refusing to overwrite mapped VA=0x",a);
				debug_hex64("[BRK] existing PA=0x",existing_phys&PAGE_MASK);
				debug_hex64("[BRK] existing PTE=0x",existing_pte);
				goto brk_rollback;
			}

			uint64_t phys=pmm_alloc_page();
			if(!phys){
				debugcon_puts("[BRK-ENOMEM] physical pages exhausted\n");
				debug_hex64("[BRK-ENOMEM] old_brk=0x",old);
				debug_hex64("[BRK-ENOMEM] new_brk=0x",addr);
				debug_hex64("[BRK-ENOMEM] grow_bytes=0x",addr>old?addr-old:0);
				debug_hex64("[BRK-ENOMEM] free_pages=0x",pmm_free_count());
				goto brk_rollback;
			}
			uint8_t *p=(uint8_t*)pmm_phys_to_virt(phys);
			if(!p){pmm_free_page(phys);goto brk_rollback;}
			for(size_t i=0;i<PAGE_SIZE;i++) p[i]=0;

			if(vmm_user_map(a,phys,PTE_W|PTE_NX)){
				pmm_free_page(phys);
				goto brk_rollback;
			}

			uint64_t mapped_phys=0,mapped_pte=0;
			if(vmm_user_translate(a,&mapped_phys,&mapped_pte)!=0 ||
			   (mapped_pte&(PTE_P|PTE_U|PTE_W))!=(PTE_P|PTE_U|PTE_W)){
				debug_hex64("[BRK] map audit FAILED VA=0x",a);
				debug_hex64("[BRK] requested PA=0x",phys);
				debug_hex64("[BRK] observed PA=0x",mapped_phys&PAGE_MASK);
				debug_hex64("[BRK] observed PTE=0x",mapped_pte);
				uint64_t rollback_phys=0;
				if(vmm_user_unmap(a,&rollback_phys)==0&&rollback_phys) pmm_free_page(rollback_phys);
				else pmm_free_page(phys);
				goto brk_rollback;
			}
		}

		task_shared_heap_sync(t,base,addr);
		heap_base=base; heap_end=addr;
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
		debug_hex64("[BRK] grow old=0x",old);
		debug_hex64("[BRK] grow new=0x",addr);
#endif
		return (int64_t)addr;

brk_rollback:
		for(uint64_t va=mapped_start;va<a;va+=PAGE_SIZE){
			uint64_t rollback_phys=0;
			if(vmm_user_unmap(va,&rollback_phys)==0&&rollback_phys) pmm_free_page(rollback_phys);
		}
		return (int64_t)old;
	}

	if(addr<old){
		uint64_t a=(addr+PAGE_SIZE-1)&PAGE_MASK;
		uint64_t b=(old+PAGE_SIZE-1)&PAGE_MASK;
		for(;a<b;a+=PAGE_SIZE){
			uint64_t phys;
			if(vmm_user_unmap(a,&phys)==0&&phys) pmm_free_page(phys);
		}
	}

	task_shared_heap_sync(t,base,addr);
	heap_base=base; heap_end=addr;
	return (int64_t)addr;
}

static int current_pid=1,current_ppid=0,current_tid=1; static int *tid_addr;
int64_t sys_getpgrp(void){ task_init_root(); return tasks[current_task].pgrp; }
int64_t sys_getpgid(int pid){ task_init_root(); if(pid==0)pid=tasks[current_task].pid; for(int i=0;i<MAX_TASKS;i++)if(tasks[i].used&&tasks[i].pid==pid)return tasks[i].pgrp; return -3; }
int64_t sys_setpgid(int pid,int pgid){ task_init_root(); if(pid==0)pid=tasks[current_task].pid; if(pgid==0)pgid=pid; for(int i=0;i<MAX_TASKS;i++)if(tasks[i].used&&tasks[i].pid==pid){ if(tasks[i].session!=tasks[current_task].session)return-EPERM; tasks[i].pgrp=pgid; return 0; } return-ESRCH; }
int64_t sys_setsid(void){ task_init_root(); task_t*t=&tasks[current_task]; if(t->pgrp==t->pid)return-EPERM; t->session=t->pid; t->pgrp=t->pid; t->controlling_tty=-1; return t->session; }
int64_t sys_getpid(void){
	task_init_root();
	return (int64_t)(tasks[current_task].tgid ? tasks[current_task].tgid : tasks[current_task].pid);
}
int64_t sys_getppid(void){ task_init_root(); return (int64_t)tasks[current_task].ppid; }
int64_t sys_gettid(void){ task_init_root(); return (int64_t)tasks[current_task].pid; }
int64_t sys_getuid(void){return 0;} int64_t sys_geteuid(void){return 0;} int64_t sys_getgid(void){return 0;} int64_t sys_getegid(void){return 0;}

int64_t sys_setuid(uint32_t uid){ return uid==0 ? 0 : -EPERM; }
int64_t sys_setgid(uint32_t gid){ return gid==0 ? 0 : -EPERM; }
#define YABRO_ID_UNCHANGED UINT32_MAX
static int identity_request_supported(uint32_t a,uint32_t b,uint32_t c){
    return (a==0 || a==YABRO_ID_UNCHANGED) &&
           (b==0 || b==YABRO_ID_UNCHANGED) &&
           (c==0 || c==YABRO_ID_UNCHANGED);
}
int64_t sys_setreuid(uint32_t ruid,uint32_t euid){
    return identity_request_supported(ruid,euid,YABRO_ID_UNCHANGED)?0:-EPERM;
}
int64_t sys_setregid(uint32_t rgid,uint32_t egid){
    return identity_request_supported(rgid,egid,YABRO_ID_UNCHANGED)?0:-EPERM;
}
int64_t sys_setresuid(uint32_t ruid,uint32_t euid,uint32_t suid){
    return identity_request_supported(ruid,euid,suid)?0:-EPERM;
}
int64_t sys_setresgid(uint32_t rgid,uint32_t egid,uint32_t sgid){
    return identity_request_supported(rgid,egid,sgid)?0:-EPERM;
}
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
	if(code==ARCH_SET_FS || code==ARCH_SET_GS){
		debug_hex64("[ARCH_PRCTL] code=0x", code);
		debug_hex64("[ARCH_PRCTL] addr=0x", addr);
	}
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
static uint32_t rand_state=0x13579bdf;
int64_t sys_getrandom(void *buf, uint64_t len, uint32_t flags)
{
	(void)flags;

	if (len > (1ULL << 20))
		return -EINVAL;

	uint8_t tmp[64];
	uint64_t done = 0;

	while (done < len) {
		uint64_t n = len - done;
		if (n > sizeof(tmp))
			n = sizeof(tmp);

		for (uint64_t i = 0; i < n; i++) {
			pseudo_rand_state ^= pseudo_rand_state << 13;
			pseudo_rand_state ^= pseudo_rand_state >> 17;
			pseudo_rand_state ^= pseudo_rand_state << 5;
			tmp[i] = (uint8_t)pseudo_rand_state;
		}

		if (user_copy_to((uint64_t)buf + done, tmp, n))
			return -EFAULT;

		done += n;
	}

	return (int64_t)len;
}
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
	if(!n && bootfs_path_is_dir(abs)>0) {
		if(vfs_ensure_dir(abs)<0) return-ENOENT;
		n=vfs_lookup(abs);
	}
	if(!n)return-ENOENT;
	if(!vfs_is_dir(n))return-ENOTDIR;
	tasks[current_task].cwd=n;
	return 0;
}
struct pollfd_k{int fd;short events,revents;};
#define POLLIN 1
#define POLLPRI 2
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32
static void kernel_wait_tick(void) { __asm__ volatile("sti; hlt; cli" ::: "memory"); }
static uint64_t timerfd_monotonic_deadline(int fd, uint64_t mono_now) {
	if(fd<0 || fd>=MAX_FILES || !files[fd].in_use || files[fd].type!=FD_TIMERFD) return 0;
	struct timerfd_state_k *t=(struct timerfd_state_k*)files[fd].private_data;
	if(!t || !t->next_ns || t->expirations) return 0;
	uint64_t clock_now=clock_now_ns_simple(t->clockid);
	uint64_t delta=t->next_ns>clock_now ? t->next_ns-clock_now : 0;
	return UINT64_MAX-mono_now<delta ? UINT64_MAX : mono_now+delta;
}
int64_t sys_poll(syscall_frame_t *sf,void*ufds,uint64_t nfds,int timeout){
	if(nfds>MAX_FILES)return-EINVAL;
	uint64_t call_nr=(sf && sf->rax==271)?271:7;
	int resumed=task_io_resume_matches(sf,call_nr);
	uint64_t start_ns=resumed ? tasks[current_task].io_resume_start_ns : kernel_monotonic_ns();
	uint64_t deadline_ns=resumed ? tasks[current_task].io_resume_deadline_ns : monotonic_deadline_after_ms(start_ns,timeout);
	for(;;){
		uint64_t mono_now=kernel_monotonic_ns();
		uint64_t next_timer_deadline=0;
		int ready=0; struct pollfd_k p;
		for(uint64_t i=0;i<nfds;i++){
			if(user_copy_from((uint8_t*)&p,(uint64_t)ufds+i*sizeof(p),sizeof(p)))return-EFAULT;
			p.revents=0;
			if((p.fd==1||p.fd==2) && fd_is_console_stream(p.fd)){
				if(p.events&POLLOUT)p.revents|=POLLOUT;
			}else if(p.fd>=0&&p.fd<MAX_FILES&&files[p.fd].in_use){
				struct file_desc *f=&files[p.fd];
                if(f->type==FD_PSEUDO && ((uintptr_t)f->private_data==DEV_INPUT || (uintptr_t)f->private_data==DEV_INPUT_MOUSE)) {
                    int pmouse=((uintptr_t)f->private_data==DEV_INPUT_MOUSE);
                    volatile uint8_t *plog=pmouse?&evdev_poll_mouse_logged:&evdev_poll_keyboard_logged;
                    if(!*plog) {
                        debugcon_puts(pmouse?"[EVDEV POLL FIRST MOUSE]":"[EVDEV POLL FIRST KEY]");
                        debug_hex64(" pid=0x",(uint64_t)tasks[current_task].pid);debug_hex64(" fd=0x",(uint64_t)p.fd);
                        debug_hex64(" events=0x",(uint64_t)(uint16_t)p.events);debug_hex64(" flags=0x",f->flags);

                        debug_hex64(" mouse_seq=0x",mouse_event_seq);debug_hex64(" input_seq=0x",f->input_seq);debugcon_puts("\n"); *plog=1;
                    }
                }
				if(f->type==FD_FILE) {
					if(p.events&POLLIN)p.revents|=POLLIN;                                           
					if((p.events&POLLOUT) && (f->flags&3))p.revents|=POLLOUT;
				}
				if(f->type==FD_EVENTFD) {
					struct eventfd_state_k *ef=(struct eventfd_state_k*)f->private_data;
					if(ef && (p.events&POLLIN) && ef->value)p.revents|=POLLIN;
					if(ef && (p.events&POLLOUT) && ef->value<UINT64_MAX-1)p.revents|=POLLOUT;
				}
				if(f->type==FD_TIMERFD) {
					struct timerfd_state_k *tf=(struct timerfd_state_k*)f->private_data;
					if(tf && (p.events&POLLIN)) {
						uint64_t clock_now=clock_now_ns_simple(tf->clockid);
						if(tf->expirations || (tf->next_ns && clock_now>=tf->next_ns)) p.revents|=POLLIN;
						else { uint64_t td=timerfd_monotonic_deadline(p.fd,mono_now); if(td && (!next_timer_deadline || td<next_timer_deadline)) next_timer_deadline=td; }
					}
				}
				if(f->type==FD_PSEUDO && (uintptr_t)f->private_data==DEV_TTY &&
				   (p.events&POLLIN) && console_tty_read_ready_for(tasks[current_task].pid,tasks[current_task].pgrp))
					p.revents|=POLLIN;
				if(f->type==FD_PSEUDO && (p.events&POLLIN) &&
				   (((uintptr_t)f->private_data==DEV_INPUT&&keyboard_fd_ready(f))||
					((uintptr_t)f->private_data==DEV_INPUT_MOUSE&&mouse_fd_ready(f)))) {
                    int pmouse=((uintptr_t)f->private_data==DEV_INPUT_MOUSE);
                    volatile uint8_t *ready_logged=pmouse?&evdev_mouse_poll_ready_logged:&evdev_keyboard_poll_ready_logged;
                    if(!*ready_logged) {
                        debugcon_puts(pmouse?"[EVDEV POLL READY MOUSE]":"[EVDEV POLL READY KEY]");
                        debug_hex64(" pid=0x",(uint64_t)tasks[current_task].pid);
                        debug_hex64(" fd=0x",(uint64_t)p.fd);
                        debug_hex64(" events=0x",(uint64_t)(uint16_t)p.events);
                        debug_hex64(" mouse_seq=0x",mouse_event_seq);
                        debug_hex64(" input_seq=0x",f->input_seq);

                        debugcon_puts("\n"); *ready_logged=1;
                    }
					p.revents|=POLLIN;
					if((uintptr_t)f->private_data==DEV_INPUT_MOUSE) mouse_poll_ready++;
				}
				if(f->type==FD_SOCKET){
					struct unix_socket_state *ss=socket_state(f);
					if(ss){
						int side=socket_side(f);
						if(ss->listener){
							if((p.events&POLLIN)&&ss->pending_count)p.revents|=POLLIN;
						}else if(ss->refs){
							int peer=side^1;
							if((p.events&POLLIN)&&ss->count[side])p.revents|=POLLIN;
							if((p.events&POLLOUT)&&!ss->closed[peer]&&ss->count[peer]<SOCK_CAP)p.revents|=POLLOUT;
							if(ss->closed[peer])p.revents|=0x010;
							if(ss->closed[peer]&&(p.events&0x008))p.revents|=0x008;
						}
					}
				} else if(f->type==FD_PIPE_R){
					struct pipe_state *pp=(struct pipe_state*)f->private_data;
					if((p.events&POLLIN)&&pp->count)p.revents|=POLLIN;
					if(!pipe_has_writers(pp))p.revents|=0x010;
				} else if(f->type==FD_PIPE_W){
					struct pipe_state *pp=(struct pipe_state*)f->private_data;
					if(!pipe_has_readers(pp))p.revents|=0x008;
					else if(p.events&POLLOUT&&pp->count<PIPE_CAP)p.revents|=POLLOUT;
				} else if(f->type==FD_PTY_MASTER || f->type==FD_PTY_SLAVE) {
					pty_state_k *pp=(pty_state_k*)f->private_data;
					if(pp && f->type==FD_PTY_MASTER) {
						if((p.events&POLLIN) && pty_master_read_ready(pp))p.revents|=POLLIN;
						if((p.events&POLLOUT) && pp->slave_open && pp->m2s_count<PTY_CAP)p.revents|=POLLOUT;
                        if (!pty_dbg_poll_master) { pty_dbg("[PTYDBG POLL_MASTER]", p.fd, f); debug_hex64(" events=0x", (uint64_t)(uint16_t)p.events); debug_hex64(" revents=0x", (uint64_t)(uint16_t)p.revents); debugcon_puts("\n"); pty_dbg_poll_master=1; }
						if(!pp->slave_open)p.revents|=POLLHUP;
					} else if(pp) {
						if((p.events&POLLIN) && pty_slave_read_ready(pp))p.revents|=POLLIN;
						if((p.events&POLLOUT) && pp->master_open && pp->s2m_count<PTY_CAP)p.revents|=POLLOUT;
						if(!pp->master_open)p.revents|=POLLHUP;
					}
				}
			} else if(p.fd >= 0) {

				p.revents = POLLNVAL;
			}
			if(p.revents)ready++;
			if(user_copy_to((uint64_t)ufds+i*sizeof(p),(uint8_t*)&p,sizeof(p)))return-EFAULT;
		}
		if(ready) { task_io_resume_clear(sf,call_nr); return ready; }
		if(timeout==0) { task_io_resume_clear(sf,call_nr); return 0; }
		uint64_t now=kernel_monotonic_ns();
		if(deadline_ns && now>=deadline_ns) { task_io_resume_clear(sf,call_nr); return 0; }
		uint64_t wait_deadline=deadline_ns;
		if(next_timer_deadline && (!wait_deadline || next_timer_deadline<wait_deadline)) wait_deadline=next_timer_deadline;
		task_io_resume_store(sf,call_nr,start_ns,deadline_ns);
		if(task_block_io_until(sf,IO_WAIT_POLL,call_nr,wait_deadline)==0)return 0;
		task_io_resume_clear(sf,call_nr); return -EAGAIN;
	}
}
int64_t sys_ppoll(syscall_frame_t *sf, void *ufds, uint64_t nfds, const void *timeout_ts, const void *sigmask) {
    (void)sigmask;
    int timeout = -1;
    if(timeout_ts){
        struct { int64_t tv_sec; int64_t tv_nsec; } ts;
        if(user_copy_from((uint8_t*)&ts,(uint64_t)timeout_ts,sizeof(ts))) return -EFAULT;
        if(ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL) return -EINVAL;
        uint64_t ms=(uint64_t)ts.tv_sec*1000ULL + (uint64_t)(ts.tv_nsec + 999999LL)/1000000ULL;
        timeout = ms > 0x7fffffffULL ? 0x7fffffff : (int)ms;
    }
    return sys_poll(sf,ufds,nfds,timeout);
}

static int epoll_fd_ready(int fd);

static int select_fd_state(int fd, int want_r, int want_w, int want_x,
                           int *rr, int *ww, int *xx) {
	*rr=*ww=*xx=0;
	if(fd<0 || fd>=MAX_FILES || !files[fd].in_use) return -EBADF;
	struct file_desc *f=&files[fd];
	if((fd==1 || fd==2) && fd_is_console_stream(fd)) { if(want_w) *ww=1; return 0; }
	switch(f->type) {
	case FD_FILE:

		if(want_r) *rr=1;
		if(want_w) *ww=1;
		break;
	case FD_PSEUDO: {
		uintptr_t k=(uintptr_t)f->private_data;
		if(want_r && (k==DEV_NULL || k==DEV_ZERO || k==DEV_RANDOM || k==DEV_URANDOM ||
		              (k==DEV_TTY && console_tty_read_ready_for(tasks[current_task].pid,tasks[current_task].pgrp)) ||
		              (k==DEV_INPUT && keyboard_fd_ready(f)) ||
		              (k==DEV_INPUT_MOUSE && mouse_fd_ready(f)))) *rr=1;
		if(want_w && (k==DEV_NULL || k==DEV_ZERO || k==DEV_RANDOM || k==DEV_URANDOM || k==DEV_CONSOLE || k==DEV_TTY)) *ww=1;
		break;
	}
	case FD_SOCKET: {
		struct unix_socket_state *ss=socket_state(f);
		if(!ss) return -EBADF;
		int side=socket_side(f);
		if(ss->listener) { if(want_r && ss->pending_count) *rr=1; }
		else if(ss->refs) {
			int peer=side^1;
			if(want_r && (ss->count[side] || ss->closed[peer])) *rr=1;
			if(want_w && !ss->closed[peer] && ss->count[peer]<SOCK_CAP) *ww=1;
		}
		break;
	}
	case FD_PIPE_R: {
		struct pipe_state *pp=(struct pipe_state*)f->private_data;
		if(!pp) return -EBADF;
		if(want_r && (pp->count || !pipe_has_writers(pp))) *rr=1;
		break;
	}
	case FD_PIPE_W: {
		struct pipe_state *pp=(struct pipe_state*)f->private_data;
		if(!pp) return -EBADF;
		if(want_w && pipe_has_readers(pp) && pp->count<PIPE_CAP) *ww=1;
		break;
	}
	case FD_EVENTFD: {
		struct eventfd_state_k *ef=(struct eventfd_state_k*)f->private_data;
		if(!ef) return -EBADF;
		if(want_r && ef->value) *rr=1;
		if(want_w && ef->value<UINT64_MAX-1) *ww=1;
		break;
	}
	case FD_TIMERFD: {
		struct timerfd_state_k *tf=(struct timerfd_state_k*)f->private_data;
		if(!tf) return -EBADF;
		uint64_t now=clock_now_ns_simple(tf->clockid);
		if(want_r && (tf->expirations || (tf->next_ns && now>=tf->next_ns))) *rr=1;
		break;
	}
	case FD_PTY_MASTER: {
		pty_state_k *pp=(pty_state_k*)f->private_data;
		if(!pp) return -EBADF;
		if(want_r && pty_master_read_ready(pp)) *rr=1;
		if(want_w && pp->slave_open && pp->m2s_count<PTY_CAP) *ww=1;
        if (!pty_dbg_select_master) { pty_dbg("[PTYDBG SELECT_MASTER]", fd, f); debug_hex64(" want_r=0x", (uint64_t)want_r); debug_hex64(" want_w=0x", (uint64_t)want_w); debug_hex64(" readable=0x", (uint64_t)*rr); debugcon_puts("\n"); pty_dbg_select_master=1; }
		break;
	}
	case FD_PTY_SLAVE: {
		pty_state_k *pp=(pty_state_k*)f->private_data;
		if(!pp) return -EBADF;
		if(want_r && pty_slave_read_ready(pp)) *rr=1;
		if(want_w && pp->master_open && pp->s2m_count<PTY_CAP) *ww=1;
		break;
	}
	case FD_EPOLL:

		if(want_r && epoll_fd_ready(fd)) *rr=1;
		break;
	default:
		break;
	}
	(void)want_x;                                                             
	return 0;
}

static uint64_t select_timeout_to_ns(const void *utv, int pselect, int *err) {
	*err=0;
	if(!utv) return 0;                                                           
	int64_t sec, sub;
	if(pselect) {
		struct { int64_t tv_sec, tv_nsec; } ts;
		if(user_copy_from((uint8_t*)&ts,(uint64_t)utv,sizeof(ts))) { *err=-EFAULT; return 0; }
		sec=ts.tv_sec; sub=ts.tv_nsec;
		if(sec<0 || sub<0 || sub>=1000000000LL) { *err=-EINVAL; return 0; }
	} else {
		struct timeval_k tv;
		if(user_copy_from((uint8_t*)&tv,(uint64_t)utv,sizeof(tv))) { *err=-EFAULT; return 0; }
		sec=tv.tv_sec; sub=tv.tv_usec;
		if(sec<0 || sub<0 || sub>=1000000LL) { *err=-EINVAL; return 0; }
		sub*=1000LL;
	}
	uint64_t s=(uint64_t)sec, ns=(uint64_t)sub;
	if(s>UINT64_MAX/1000000000ULL) return UINT64_MAX;
	uint64_t dur=s*1000000000ULL;
	return UINT64_MAX-dur<ns ? UINT64_MAX : dur+ns;
}

static int select_copy_sets(uint64_t ur, uint64_t uw, uint64_t ue, uint64_t bytes,
                            const uint8_t *rout, const uint8_t *wout, const uint8_t *eout) {
	if(bytes && ur && user_copy_to(ur,rout,bytes)) return -EFAULT;
	if(bytes && uw && user_copy_to(uw,wout,bytes)) return -EFAULT;
	if(bytes && ue && user_copy_to(ue,eout,bytes)) return -EFAULT;
	return 0;
}

static int select_update_timeval(uint64_t utv, uint64_t deadline_ns) {
	if(!utv) return 0;
	uint64_t now=kernel_monotonic_ns(), rem=deadline_ns>now?deadline_ns-now:0;
	struct timeval_k tv;
	tv.tv_sec=(int64_t)(rem/1000000000ULL);
	tv.tv_usec=(int64_t)((rem%1000000000ULL)/1000ULL);
	return user_copy_to(utv,(const uint8_t*)&tv,sizeof(tv)) ? -EFAULT : 0;
}

int64_t sys_pselect6(syscall_frame_t *sf, int nfds, void *ur, void *uw, void *ue, const void *uts, const void *sigmask) {
	(void)sigmask;
	return sys_select(sf,nfds,ur,uw,ue,(void*)uts);
}

int64_t sys_select(syscall_frame_t *sf,int nfds,void*ur,void*uw,void*ue,void*utv){
	if(nfds<0||nfds>1024)return-EINVAL;
	uint64_t call_nr=(sf && sf->rax==270)?270:23;
	int is_pselect=(call_nr==270);
	uint64_t bytes=(uint64_t)((nfds+7)/8);
	uint8_t rin[128]={0},win[128]={0},ein[128]={0};
	uint8_t rout[128]={0},wout[128]={0},eout[128]={0};
	if(bytes>sizeof(rin))return-EINVAL;
	int resumed=task_io_resume_matches(sf,call_nr);
	uint64_t start_ns=resumed?tasks[current_task].io_resume_start_ns:kernel_monotonic_ns();
	uint64_t deadline_ns=0;
	if(resumed) deadline_ns=tasks[current_task].io_resume_deadline_ns;
	else if(utv) {
		int terr=0; uint64_t dur=select_timeout_to_ns(utv,is_pselect,&terr);
		if(terr) return terr;
		deadline_ns=UINT64_MAX-start_ns<dur?UINT64_MAX:start_ns+dur;
	}
	for(;;) {
		for(uint64_t i=0;i<sizeof(rin);i++) { rin[i]=win[i]=ein[i]=rout[i]=wout[i]=eout[i]=0; }
		if(ur&&bytes&&user_copy_from(rin,(uint64_t)ur,bytes)) { task_io_resume_clear(sf,call_nr); return-EFAULT; }
		if(uw&&bytes&&user_copy_from(win,(uint64_t)uw,bytes)) { task_io_resume_clear(sf,call_nr); return-EFAULT; }
		if(ue&&bytes&&user_copy_from(ein,(uint64_t)ue,bytes)) { task_io_resume_clear(sf,call_nr); return-EFAULT; }
		uint64_t ready=0;
		for(int fd=0;fd<nfds;fd++) {
			uint8_t mask=(uint8_t)(1u<<(fd%8));
			int want_r=ur&&(rin[fd/8]&mask), want_w=uw&&(win[fd/8]&mask), want_x=ue&&(ein[fd/8]&mask);
			if(!want_r&&!want_w&&!want_x) continue;
			int rr=0,ww=0,xx=0;
			int rc=select_fd_state(fd,want_r,want_w,want_x,&rr,&ww,&xx);
			if(rc<0) { task_io_resume_clear(sf,call_nr); return rc; }
			if(rr) { rout[fd/8]|=mask; ready++; }
			if(ww) { wout[fd/8]|=mask; ready++; }
			if(xx) { eout[fd/8]|=mask; ready++; }
		}
		if(ready) {
			int rc=select_copy_sets((uint64_t)ur,(uint64_t)uw,(uint64_t)ue,bytes,rout,wout,eout);
			if(!is_pselect && utv) { int tr=select_update_timeval((uint64_t)utv,deadline_ns); if(!rc) rc=tr; }
			task_io_resume_clear(sf,call_nr);
			return rc<0?rc:(int64_t)ready;
		}
		uint64_t now=kernel_monotonic_ns();
		if(utv && now>=deadline_ns) {
			int rc=select_copy_sets((uint64_t)ur,(uint64_t)uw,(uint64_t)ue,bytes,rout,wout,eout);
			if(!is_pselect) { int tr=select_update_timeval((uint64_t)utv,deadline_ns); if(!rc) rc=tr; }
			task_io_resume_clear(sf,call_nr);
			return rc<0?rc:0;
		}
		task_io_resume_store(sf,call_nr,start_ns,deadline_ns);
		if(task_block_io_until(sf,IO_WAIT_SELECT,call_nr,deadline_ns)==0) return 0;
		task_io_resume_clear(sf,call_nr);
		return-EAGAIN;
	}
}
struct sysinfo_k{long uptime;uint64_t loads[3],totalram,freeram,sharedram,bufferram,totalhigh,freehigh;uint32_t mem_unit,_pad[3];};
int64_t sys_sysinfo(void*ui){struct sysinfo_k x;for(size_t i=0;i<sizeof(x);i++)((uint8_t*)&x)[i]=0;x.uptime=(long)(kernel_uptime_ns()/1000000000ULL);x.totalram=pmm_total_count()*4096ULL;x.freeram=pmm_free_count()*4096ULL;x.mem_unit=1;return user_copy_to((uint64_t)ui,(uint8_t*)&x,sizeof(x))?-EFAULT:0;}

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
static int64_t sys_readlink_resolved(const char *p,char *buf,uint64_t size){const char*t=NULL;struct vfs_node*n=vfs_lookup(p);if(n&&vfs_type(n)==VFS_NODE_SYMLINK)t=vfs_link_target(n);if(!t&&(pseudo_eq(p,"/proc/self/exe")||pseudo_eq(p,"/proc/1/exe")))t=current_exe;if(!t)return-ENOENT;uint64_t nbytes=0;while(t[nbytes]&&nbytes<size)nbytes++;if(nbytes&&user_copy_to((uint64_t)buf,(const uint8_t*)t,nbytes))return-EFAULT;return nbytes;}
int64_t sys_readlink(const char *path,char *buf,uint64_t size){char raw[128],p[128];if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT;if(resolve_process_path(raw,p,sizeof(p))<0)return-ENAMETOOLONG;return sys_readlink_resolved(p,buf,size);}

int64_t sys_mkdir(const char *path, int mode) {
	char raw[128],p[128]; if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT; if(resolve_process_path(raw,p,sizeof(p))<0)return-ENAMETOOLONG;
	struct vfs_node *existing=vfs_lookup(p);
	if(existing){
		if(!vfs_is_dir(existing)) return -EEXIST;

		uint32_t want=(uint32_t)mode & 07777;
		uint32_t have=vfs_mode(existing) & 07777;
		if(have==want) return 0;

		if(!kernel_strncmp(p,"/tmp/.X11-unix",14) && want==01777){
			existing->mode=(existing->mode & ~07777u)|01777u;
			return 0;
		}
		return -EINVAL;
	}
	if(!vfs_mkdir(p,(uint32_t)mode)) return -ENOENT;
	return 0;
}
int64_t sys_link(const char *oldpath, const char *newpath) {
	char ro[128],rn[128],ao[128],an[128];
	if(user_copy_cstr(ro,(uint64_t)oldpath,sizeof(ro)))return-EFAULT;
	if(user_copy_cstr(rn,(uint64_t)newpath,sizeof(rn)))return-EFAULT;
	if(resolve_process_path(ro,ao,sizeof(ao))<0||resolve_process_path(rn,an,sizeof(an))<0)return-ENAMETOOLONG;
	struct vfs_node *src=vfs_lookup(ao);
	if(!src)return-ENOENT;
	if(vfs_is_dir(src))return-EISDIR;
	if(vfs_lookup(an))return-EEXIST;
	int rc=vfs_link(ao,an);
	return rc==0?0:(rc==-2?-EEXIST:-EIO);
}

int64_t sys_unlink(const char *path) {
	char raw[128],p[128];
	if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw))) return -EFAULT;
	if(resolve_process_path(raw,p,sizeof(p))<0) return -ENAMETOOLONG;

	struct vfs_node *n = vfs_lookup(p);
	if(!n) {
		for(int i=0;i<UNIX_SOCKETS_MAX;i++) {
			if(unix_sockets[i].refs && unix_sockets[i].bound &&
			   pseudo_eq(unix_sockets[i].path,p)) {
				unix_sockets[i].bound = 0;
				unix_sockets[i].path[0] = 0;
				return 0;
			}
		}
		return -ENOENT;
	}
	if(vfs_is_dir(n)) return -EISDIR;

	int is_socket = (vfs_type(n) == VFS_NODE_SOCKET);
	if(vfs_unlink(p)) return -EIO;
	if(is_socket) {
		for(int i=0;i<UNIX_SOCKETS_MAX;i++) {
			if(unix_sockets[i].refs && unix_sockets[i].bound &&
			   pseudo_eq(unix_sockets[i].path,p)) {
				unix_sockets[i].bound = 0;
				unix_sockets[i].path[0] = 0;
			}
		}
	}
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

static int at_is_input_endpoint(const char *p) {
	return p && (pseudo_eq(p,"/dev/input/event0") || pseudo_eq(p,"/dev/input/event1") ||
	             pseudo_eq(p,"event0") || pseudo_eq(p,"event1"));
}
static void trace_at_input_path(const char *tag, int dirfd, const char *raw, const char *resolved, int64_t result) {
	if (at_is_input_endpoint(raw) || at_is_input_endpoint(resolved) ||
	    (raw && (pseudo_eq(raw,"/dev/input") || pseudo_eq(raw,"input")))) {
		debugcon_puts(tag);
		debugcon_puts(" dirfd="); debug_hex64("", (uint64_t)(int64_t)dirfd);
		debugcon_puts(" raw="); debugcon_puts(raw ? raw : "(null)");
		debugcon_puts(" resolved="); debugcon_puts(resolved ? resolved : "(unresolved)");
		debugcon_puts(" rc="); debug_hex64("", (uint64_t)result);
		debugcon_puts("\n");
	}
}
static int resolve_at_path(int dirfd, const char *upath, char raw[128], char resolved[128]) {
	raw[0] = 0;
	resolved[0] = 0;
	if (user_copy_cstr(raw, (uint64_t)upath, 128)) return -EFAULT;
	if (!raw[0]) return -ENOENT;
	if (raw[0] == '/' || dirfd == -100) {
		int r = resolve_process_path(raw, resolved, 128);
		return r < 0 ? r : 0;
	}
	if (dirfd < 0 || dirfd >= MAX_FILES || !files[dirfd].in_use) return -EBADF;
	struct file_desc *d = &files[dirfd];
	if (!d->vnode || !vfs_is_dir(d->vnode)) return -ENOTDIR;
	const char *base = d->boot_path;
	if (!base[0]) return -ENOENT;
	char joined[256];
	size_t bn=0, pn=0;
	while (base[bn]) { if (bn+1 >= sizeof(joined)) return -ENAMETOOLONG; joined[bn]=base[bn]; bn++; }
	if (bn == 0) { joined[bn++]='/'; }
	if (bn > 1 && joined[bn-1] != '/') joined[bn++]='/';
	while (raw[pn]) { if (bn+1 >= sizeof(joined)) return -ENAMETOOLONG; joined[bn++]=raw[pn++]; }
	joined[bn]=0;
	return normalize_abs_path(joined, resolved, 128);
}
int64_t sys_openat(int dirfd, const char *path, int flags, int mode) {
	char raw[128], p[128];
	int r = resolve_at_path(dirfd, path, raw, p);
	trace_at_input_path("[OPENAT INPUT]", dirfd, raw, r < 0 ? NULL : p, r);
	if (r < 0) return r;
	return sys_open_resolved(raw, p, flags, mode);
}
int64_t sys_newfstatat(int dirfd, const char *path, void *st, int flags) {

	char raw[128], p[128];
	if (user_copy_cstr(raw, (uint64_t)path, sizeof(raw))) return -EFAULT;
	if (!raw[0] && (flags & 0x1000)) return sys_fstat(dirfd, st);
	int r = resolve_at_path(dirfd, path, raw, p);
	trace_at_input_path("[FSTATAT INPUT]", dirfd, raw, r < 0 ? NULL : p, r);
	if (r < 0) return r;
	return sys_stat_resolved(p, st);
}
int64_t sys_faccessat(int dirfd, const char *path, int mode) {
	char raw[128], p[128];
	int r = resolve_at_path(dirfd, path, raw, p);
	trace_at_input_path("[FACCESSAT INPUT]", dirfd, raw, r < 0 ? NULL : p, r);
	if (r < 0) return r;
	return sys_access_resolved(p, mode);
}

static int chmod_mode_supported(uint32_t mode){ return (mode & ~07777u)==0; }
static int resolved_path_exists(const char *path){
    int type=0,kind=0;
    return pseudo_lookup(path,&type,&kind) || vfs_lookup(path) || bootfs_file_size(path)>0;
}
int64_t sys_chmod(const char *path,uint32_t mode){
    if(!chmod_mode_supported(mode))return-EINVAL;
    char raw[128],resolved[128];
    if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT;
    if(resolve_process_path(raw,resolved,sizeof(resolved))<0)return-ENAMETOOLONG;
    return resolved_path_exists(resolved)?0:-ENOENT;
}
int64_t sys_fchmod(int fd,uint32_t mode){
    if(!chmod_mode_supported(mode))return-EINVAL;
    if(fd<0 || fd>=MAX_FILES || !files[fd].in_use)return-EBADF;
    return 0;
}
int64_t sys_fchmodat(int dirfd,const char *path,uint32_t mode){
    if(!chmod_mode_supported(mode))return-EINVAL;
    char raw[128],resolved[128];
    int r=resolve_at_path(dirfd,path,raw,resolved);
    if(r<0)return r;
    return resolved_path_exists(resolved)?0:-ENOENT;
}
static int ownership_ids_supported(uint32_t uid,uint32_t gid){
    return (uid==0 || uid==YABRO_ID_UNCHANGED) &&
           (gid==0 || gid==YABRO_ID_UNCHANGED);
}
int64_t sys_fchown(int fd,uint32_t uid,uint32_t gid){
    if(!ownership_ids_supported(uid,gid))return-EPERM;
    if(fd<0 || fd>=MAX_FILES || !files[fd].in_use)return-EBADF;
    return 0;
}
int64_t sys_fchownat(int dirfd,const char *path,uint32_t uid,uint32_t gid,int flags){
    if(!ownership_ids_supported(uid,gid))return-EPERM;
    if(flags & ~(0x0100|0x1000))return-EINVAL;                                          
    char raw[128], resolved[128];
    if(user_copy_cstr(raw,(uint64_t)path,sizeof(raw)))return-EFAULT;
    if(!raw[0] && (flags&0x1000))return sys_fchown(dirfd,uid,gid);
    int r=resolve_at_path(dirfd,path,raw,resolved);
    if(r<0)return r;
    int type=0,kind=0;
    if(pseudo_lookup(resolved,&type,&kind) || vfs_lookup(resolved) || bootfs_file_size(resolved)>0)
        return 0;
    return-ENOENT;
}
int64_t sys_chown(const char *path,uint32_t uid,uint32_t gid){
    return sys_fchownat(-100,path,uid,gid,0);
}
int64_t sys_lchown(const char *path,uint32_t uid,uint32_t gid){
    return sys_chown(path,uid,gid);
}
int64_t sys_readlinkat(int dirfd,const char *path,char *buf,uint64_t size) {
	char raw[128], p[128];
	int r = resolve_at_path(dirfd, path, raw, p);
	trace_at_input_path("[READLINKAT INPUT]", dirfd, raw, r < 0 ? NULL : p, r);
	if (r < 0) return r;
	return sys_readlink_resolved(p, buf, size);
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
	if (pid != 0 && pid != current_pid) return -3;
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

#define EPOLLIN  0x001
#define EPOLLPRI 0x002
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3
#define YABRO_EPOLL_WATCH_MAX 128
#define YABRO_EPOLL_INSTANCES 4
struct epoll_watch_k { int used; int fd; uint32_t events; uint64_t data; };

struct epoll_state_k { int used; uint32_t refs; struct epoll_watch_k w[YABRO_EPOLL_WATCH_MAX]; };
static struct epoll_state_k epolls[YABRO_EPOLL_INSTANCES];
static struct epoll_state_k *epoll_state_for_file(struct file_desc *f) {
	return (f && f->type==FD_EPOLL) ? (struct epoll_state_k*)f->private_data : NULL;
}
static void epoll_retain_file(struct file_desc *f) {
	struct epoll_state_k *e=epoll_state_for_file(f);
	if(e && e->used && e->refs<UINT32_MAX) e->refs++;
}
static void epoll_release_file(struct file_desc *f) {
	struct epoll_state_k *e=epoll_state_for_file(f);
	if(!e || !e->used) return;
	if(e->refs>1) { e->refs--; return; }
	e->refs=0; e->used=0;
	for(int i=0;i<YABRO_EPOLL_WATCH_MAX;i++) e->w[i].used=0;
}
static int epoll_is_ready(int fd, uint32_t events, uint32_t *revents) {
	*revents = 0;
	if (fd == 1 || fd == 2) { if (events & EPOLLOUT) *revents |= EPOLLOUT; return *revents != 0; }
	if (fd < 0 || fd >= MAX_FILES || !files[fd].in_use) return 0;
	if (files[fd].type == FD_FILE) {
		if (events & EPOLLIN) *revents |= EPOLLIN;                      
		if ((events & EPOLLOUT) && (files[fd].flags & 3)) *revents |= EPOLLOUT;
	}
	if (files[fd].type == FD_PIPE_R) {
		struct pipe_state *p=(struct pipe_state*)files[fd].private_data;
		if (p && (p->count || !pipe_has_writers(p)) && (events & EPOLLIN)) *revents |= EPOLLIN;
		if (p && !pipe_has_writers(p)) *revents |= EPOLLHUP;
	}
	if (files[fd].type == FD_PIPE_W) {
		struct pipe_state *p=(struct pipe_state*)files[fd].private_data;
		if (p && !pipe_has_readers(p)) *revents |= EPOLLERR;
		else if (p && (events & EPOLLOUT) && p->count < PIPE_CAP) *revents |= EPOLLOUT;
	}

	if (files[fd].type == FD_PSEUDO) {
        uintptr_t k = (uintptr_t)files[fd].private_data;
        if (k == DEV_TTY && (events & EPOLLIN) &&
            console_tty_read_ready_for(tasks[current_task].pid,tasks[current_task].pgrp)) {
            *revents |= EPOLLIN;
        }
        if (k == DEV_INPUT || k == DEV_INPUT_MOUSE) {
            int input_ready = (k == DEV_INPUT) ?
                keyboard_fd_ready(&files[fd]) : mouse_fd_ready(&files[fd]);
            volatile uint8_t *ready_logged = (k == DEV_INPUT_MOUSE) ?
                &evdev_epoll_mouse_ready_logged : &evdev_epoll_keyboard_ready_logged;
            volatile uint8_t *masked_logged = (k == DEV_INPUT_MOUSE) ?
                &evdev_epoll_mouse_masked_logged : &evdev_epoll_keyboard_masked_logged;
            if (input_ready && !(events & EPOLLIN) && !*masked_logged) {
                debugcon_puts(k == DEV_INPUT_MOUSE ? "[EVDEV EPOLL MASK BLOCKS MOUSE]" : "[EVDEV EPOLL MASK BLOCKS KEY]");
                debug_hex64(" fd=0x", (uint64_t)fd);
                debug_hex64(" events=0x", (uint64_t)events);
                debug_hex64(" mouse_seq=0x", mouse_event_seq);
                debug_hex64(" input_seq=0x", input_fd_seq(&files[fd]));

                debugcon_puts("\n"); *masked_logged = 1;
            }
            if (input_ready && (events & EPOLLIN)) {
                *revents |= EPOLLIN;
                if (!*ready_logged) {
                    debugcon_puts(k == DEV_INPUT_MOUSE ? "[EVDEV EPOLL READY MOUSE]" : "[EVDEV EPOLL READY KEY]");
                    debug_hex64(" fd=0x", (uint64_t)fd);
                    debug_hex64(" events=0x", (uint64_t)events);
                    debug_hex64(" mouse_seq=0x", mouse_event_seq);
                    debug_hex64(" input_seq=0x", input_fd_seq(&files[fd]));
                    debugcon_puts("\n"); *ready_logged = 1;
                }
            }
        }
    }
	if (files[fd].type == FD_PTY_MASTER && (events & EPOLLIN)) {
		pty_state_k *p = (pty_state_k*)files[fd].private_data;
		if (p && pty_master_read_ready(p)) *revents |= EPOLLIN;
	}
	if (files[fd].type == FD_PTY_SLAVE && (events & EPOLLIN)) {
		pty_state_k *p = (pty_state_k*)files[fd].private_data;
		if (p && pty_slave_read_ready(p)) *revents |= EPOLLIN;
	}
	if (files[fd].type == FD_EVENTFD && (events & EPOLLIN) && ((struct eventfd_state_k*)files[fd].private_data)->value) *revents |= EPOLLIN;
	if (files[fd].type == FD_EVENTFD && (events & EPOLLOUT) && ((struct eventfd_state_k*)files[fd].private_data)->value < UINT64_MAX-1) *revents |= EPOLLOUT;
	if (files[fd].type == FD_TIMERFD) { struct timerfd_state_k*t=(struct timerfd_state_k*)files[fd].private_data; uint64_t now=clock_now_ns_simple(t->clockid); if(t->next_ns&&now>=t->next_ns&&(events&EPOLLIN))*revents|=EPOLLIN; }
	if (files[fd].type == FD_SOCKET) {
		struct unix_socket_state *ss=socket_state(&files[fd]);
		if (ss) {
			int side=socket_side(&files[fd]);
			if (ss->listener) {
				if ((events & EPOLLIN) && ss->pending_count) *revents |= EPOLLIN;
			} else if (ss->refs) {
				int peer=side^1;
				if ((events & EPOLLIN) && ss->count[side]) *revents |= EPOLLIN;
				if ((events & EPOLLOUT) && !ss->closed[peer] && ss->count[peer] < SOCK_CAP) *revents |= EPOLLOUT;
				if (ss->closed[peer]) *revents |= EPOLLHUP;
				if (ss->closed[peer] && (events & EPOLLERR)) *revents |= EPOLLERR;
			}
		}
	}
	return *revents != 0;
}
static int epoll_fd_ready(int fd) {
	if(fd<0 || fd>=MAX_FILES || !files[fd].in_use || files[fd].type!=FD_EPOLL) return 0;
	struct epoll_state_k *e=(struct epoll_state_k*)files[fd].private_data;
	if(!e || !e->used) return 0;
	for(int i=0;i<YABRO_EPOLL_WATCH_MAX;i++) if(e->w[i].used) {
		uint32_t revents=0;
		if(epoll_is_ready(e->w[i].fd,e->w[i].events,&revents)) return 1;
	}
	return 0;
}

int64_t sys_epoll_create1(int flags) {

	if(flags & ~0x80000) return -EINVAL;
	int fd = find_free_fd();
	if (fd < 0) return -EMFILE;
	for (int i=0;i<YABRO_EPOLL_INSTANCES;i++) {
		if (!epolls[i].used) {
			epolls[i].used = 1;
			epolls[i].refs = 1;
			for (int j=0;j<YABRO_EPOLL_WATCH_MAX;j++) epolls[i].w[j].used = 0;
			files[fd].in_use=1; files[fd].type=FD_EPOLL; files[fd].flags=flags;
			files[fd].private_data=&epolls[i];
			debugcon_puts("[EPOLL CREATE] fd=0x"); debug_hex64("",(uint64_t)fd); debugcon_puts("\n");
			return fd;
		}
	}
	return -EMFILE;
}
struct epoll_event_k { uint32_t events; uint64_t data; } __attribute__((packed));
_Static_assert(sizeof(struct epoll_event_k) == 12, "epoll_event ABI must be 12 bytes on x86_64");
int64_t sys_epoll_ctl(int epfd, int op, int fd, void *uevent) {
	if(epfd<0||epfd>=MAX_FILES||!files[epfd].in_use||files[epfd].type!=FD_EPOLL) return -EBADF;
	if(fd<0||fd>=MAX_FILES+3) return -EBADF;
	struct epoll_state_k *e=(struct epoll_state_k*)files[epfd].private_data;
	if(!e || !e->used) return -EBADF;
	struct epoll_event_k ev;
	if(op!=EPOLL_CTL_DEL && user_copy_from((uint8_t*)&ev,(uint64_t)uevent,sizeof(ev))) return -EFAULT;
	int slot=-1;
	for(int i=0;i<YABRO_EPOLL_WATCH_MAX;i++) if(e->w[i].used && e->w[i].fd==fd) {slot=i;break;}
	if(op==EPOLL_CTL_DEL){
		if(slot<0)return-ENOENT;
		e->w[slot].used=0;
		return 0;
	}
	if(op==EPOLL_CTL_ADD){
		if(slot>=0)return-EEXIST;
		for(int i=0;i<YABRO_EPOLL_WATCH_MAX;i++) if(!e->w[i].used){slot=i;break;}
		if(slot<0){ debugcon_puts("[EPOLL ADD FULL]\n"); return-ENOSPC; }
		e->w[slot].used=1; e->w[slot].fd=fd; e->w[slot].events=ev.events; e->w[slot].data=ev.data;
        if(fd>=0 && fd<MAX_FILES && files[fd].in_use && files[fd].type==FD_PSEUDO &&
           (uintptr_t)files[fd].private_data==DEV_INPUT && !evdev_epoll_keyboard_add_logged) {
            debugcon_puts("[EVDEV EPOLL ADD KEY]");debug_hex64(" pid=0x",(uint64_t)tasks[current_task].pid);debug_hex64(" epfd=0x",(uint64_t)epfd);debug_hex64(" fd=0x",(uint64_t)fd);debug_hex64(" events=0x",(uint64_t)ev.events);debug_hex64(" data=0x",ev.data);debugcon_puts("\n");evdev_epoll_keyboard_add_logged=1;
        }
        if(fd>=0 && fd<MAX_FILES && files[fd].in_use && files[fd].type==FD_PSEUDO &&
           (uintptr_t)files[fd].private_data==DEV_INPUT_MOUSE && !evdev_epoll_mouse_add_logged) {
            debugcon_puts("[EVDEV EPOLL ADD MOUSE]");debug_hex64(" pid=0x",(uint64_t)tasks[current_task].pid);debug_hex64(" epfd=0x",(uint64_t)epfd);debug_hex64(" fd=0x",(uint64_t)fd);debug_hex64(" events=0x",(uint64_t)ev.events);debug_hex64(" data=0x",ev.data);debugcon_puts("\n");evdev_epoll_mouse_add_logged=1;
        }
		return 0;
	}
	if(op==EPOLL_CTL_MOD){
		if(slot<0)return-ENOENT;
        if(fd>=0 && fd<MAX_FILES && files[fd].in_use && files[fd].type==FD_PSEUDO) {
            uintptr_t k=(uintptr_t)files[fd].private_data;
            volatile uint8_t *logged = k==DEV_INPUT ? &evdev_epoll_keyboard_mod_logged :
                                      k==DEV_INPUT_MOUSE ? &evdev_epoll_mouse_mod_logged : NULL;
            if(logged && !*logged) {
                debugcon_puts(k==DEV_INPUT_MOUSE?"[EVDEV EPOLL MOD MOUSE]":"[EVDEV EPOLL MOD KEY]");
                debug_hex64(" pid=0x",(uint64_t)tasks[current_task].pid);
                debug_hex64(" epfd=0x",(uint64_t)epfd);
                debug_hex64(" fd=0x",(uint64_t)fd);
                debug_hex64(" old_events=0x",(uint64_t)e->w[slot].events);
                debug_hex64(" new_events=0x",(uint64_t)ev.events);
                debug_hex64(" data=0x",ev.data); debugcon_puts("\n"); *logged=1;
            }
        }
		e->w[slot].events=ev.events;e->w[slot].data=ev.data;return 0;
	}
	return -EINVAL;
}
int64_t sys_epoll_wait(syscall_frame_t *sf, int epfd, void *uevents, int maxevents, int timeout) {
	if(epfd<0||epfd>=MAX_FILES||!files[epfd].in_use||files[epfd].type!=FD_EPOLL)return-EBADF;

	#define YABRO_EPOLL_MAX_EVENTS 256
	if(maxevents<=0||maxevents>YABRO_EPOLL_MAX_EVENTS)return-EINVAL;
	struct epoll_state_k *e=(struct epoll_state_k*)files[epfd].private_data;
	if(!e || !e->used) return -EBADF;
	uint64_t call_nr=(sf && sf->rax==281)?281:232;
	int resumed=task_io_resume_matches(sf,call_nr);
	uint64_t start_ns=resumed ? tasks[current_task].io_resume_start_ns : kernel_monotonic_ns();
	uint64_t deadline_ns=resumed ? tasks[current_task].io_resume_deadline_ns : monotonic_deadline_after_ms(start_ns,timeout);
	for(;;){
		struct epoll_event_k out[YABRO_EPOLL_WATCH_MAX]; int n=0;
		uint64_t mono_now=kernel_monotonic_ns();
		uint64_t next_timer_deadline=0;
		for(int i=0;i<YABRO_EPOLL_WATCH_MAX&&n<maxevents;i++) if(e->w[i].used){
			int wfd=e->w[i].fd;
			if(wfd>=0 && wfd<MAX_FILES && files[wfd].in_use && files[wfd].type==FD_TIMERFD && (e->w[i].events&EPOLLIN)) {
				uint64_t td=timerfd_monotonic_deadline(wfd,mono_now);
				if(td && (!next_timer_deadline || td<next_timer_deadline)) next_timer_deadline=td;
			}
			uint32_t rev;
			if(epoll_is_ready(wfd,e->w[i].events,&rev)){out[n].events=rev;out[n].data=e->w[i].data;n++;}
		}
		if(n){
			task_io_resume_clear(sf,call_nr);
#if defined(YABRO_DEBUG_SYSCALL) && YABRO_DEBUG_SYSCALL
			if(current_task>=0&&current_task<MAX_TASKS&&tasks[current_task].used){
				debug_hex64("[EPOLL-COPY] pid=0x",(uint64_t)tasks[current_task].pid);
				debug_hex64(" uevents=0x",(uint64_t)uevents);
				debug_hex64(" bytes=0x",(uint64_t)n*sizeof(out[0]));
				debug_hex64(" rsp=0x",tasks[current_task].ctx.rsp); debugcon_puts("\n");
			}
#endif
			if(user_copy_to((uint64_t)uevents,(const uint8_t*)out,(uint64_t)n*sizeof(out[0])))return-EFAULT;
			return n;
		}
		if(timeout==0){task_io_resume_clear(sf,call_nr);return 0;}
		uint64_t now=kernel_monotonic_ns();
		if(deadline_ns && now>=deadline_ns){task_io_resume_clear(sf,call_nr);return 0;}
		uint64_t wait_deadline=deadline_ns;
		if(next_timer_deadline && (!wait_deadline || next_timer_deadline<wait_deadline)) wait_deadline=next_timer_deadline;

		task_io_resume_store(sf,call_nr,start_ns,deadline_ns);
		if(task_block_io_until(sf,IO_WAIT_EPOLL,call_nr,wait_deadline)==0)return 0;
		task_io_resume_clear(sf,call_nr);
		return -EAGAIN;
	}
}
int64_t sys_epoll_pwait(syscall_frame_t *sf, int epfd, void *uevents, int maxevents, int timeout, const void *sigmask, uint64_t sigsetsize) {
	(void)sigmask; (void)sigsetsize;
	return sys_epoll_wait(sf,epfd,uevents,maxevents,timeout);
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

		uint64_t deadline=0;
		if (timeout) {
			struct { int64_t tv_sec; int64_t tv_nsec; } ts;
			if (!user_range_ok((uint64_t)timeout,sizeof(ts),0) ||
				user_copy_from((uint8_t*)&ts,(uint64_t)timeout,sizeof(ts))) return -EFAULT;
			if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL) return -EINVAL;
			if (ts.tv_sec == 0 && ts.tv_nsec == 0) return -ETIMEDOUT;
			uint64_t sec=(uint64_t)ts.tv_sec, nsec=(uint64_t)ts.tv_nsec;
			if (sec > (UINT64_MAX-nsec)/1000000000ULL) return -EINVAL;
			uint64_t duration=sec*1000000000ULL+nsec;
			uint64_t now=kernel_monotonic_ns();
			if (duration > UINT64_MAX-now) return -EINVAL;
			deadline=now+duration;
		}

		task_t *t=&tasks[current_task];
		task_save_from_frame(t, f);
		t->futex_addr=(uint64_t)uaddr;
		t->futex_cr3=t->ctx.cr3 & PAGE_MASK;
		t->futex_deadline_ns=deadline;
		t->ctx.rax=0;
		if (task_switch_from_current_blocked() != 0) {
			t->futex_addr=0; t->futex_cr3=0; t->futex_deadline_ns=0;
			t->state=TASK_RUNNING;
			return -EAGAIN;
		}
		return 0;
	}
	if (cmd == FUTEX_WAKE) {
		if (!user_range_ok((uint64_t)uaddr, sizeof(uint32_t), 0)) return -EFAULT;
		uint32_t woke=0;
		uint64_t cr3=tasks[current_task].ctx.cr3 & PAGE_MASK;
		for(int i=0;i<MAX_TASKS && woke<val;i++) {
			task_t *t=&tasks[i];
			if(!t->used || t->state!=TASK_BLOCKED || t->futex_addr!=(uint64_t)uaddr ||
			   t->futex_cr3!=cr3) continue;
			t->futex_addr=0; t->futex_cr3=0; t->futex_deadline_ns=0;
			t->ctx.rax=0;
			t->state=TASK_RUNNABLE;
			woke++;
		}
		return (int64_t)woke;
	}
	return -ENOSYS;
}
int64_t sys_nanosleep(syscall_frame_t *f, const void *req, void *rem) {
	struct { int64_t tv_sec; int64_t tv_nsec; } ts;
	if (!req || user_copy_from((uint8_t*)&ts, (uint64_t)req, sizeof(ts))) return -EFAULT;
	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL) return -EINVAL;

	uint64_t span_sec=(uint64_t)ts.tv_sec;
	uint64_t span_nsec=(uint64_t)ts.tv_nsec;
	if (span_sec==0 && span_nsec==0) return 0;
	if (span_sec > (UINT64_MAX-span_nsec)/1000000000ULL) return -EINVAL;
	uint64_t duration_ns=span_sec*1000000000ULL+span_nsec;
	const uint64_t call_nr=35;
	int resumed=task_io_resume_matches(f,call_nr);
	uint64_t now=kernel_monotonic_ns();
	uint64_t start_ns=resumed ? tasks[current_task].io_resume_start_ns : now;
	uint64_t deadline_ns;
	if (resumed) deadline_ns=tasks[current_task].io_resume_deadline_ns;
	else {
		if (duration_ns > UINT64_MAX-now) return -EINVAL;
		deadline_ns=now+duration_ns;
	}
	if (now>=deadline_ns) {
		task_io_resume_clear(f,call_nr);
		if (rem) { struct { int64_t tv_sec; int64_t tv_nsec; } z={0,0};
			if (user_copy_to((uint64_t)rem,(const uint8_t*)&z,sizeof(z))) return -EFAULT; }
		return 0;
	}

	task_io_resume_store(f,call_nr,start_ns,deadline_ns);
	if (task_block_io_until(f,IO_WAIT_NANOSLEEP,call_nr,deadline_ns)==0) return 0;
	task_io_resume_clear(f,call_nr);
	return -EAGAIN;
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
	if (pid != 0 && pid != 1 && pid != (int)sys_getpid()) return -3;
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
		if (!w->used || w->state != TASK_BLOCKED || w->futex_addr != t->tid_address ||
		    (w->futex_cr3 & PAGE_MASK) != (t->ctx.cr3 & PAGE_MASK)) continue;
		w->futex_addr = 0;
		w->futex_cr3 = 0;
		w->futex_deadline_ns = 0;
		w->ctx.rax = 0;
		w->state = TASK_RUNNABLE;
	}
	t->tid_address = 0;
}

int64_t sys_exit(int status) {
	task_init_root();
    debugcon_puts("[TASK-EXIT] pid=0x"); debug_hex64("", (uint64_t)tasks[current_task].pid);
    debugcon_puts(" status=0x"); debug_hex64("", (uint64_t)(uint32_t)status); debugcon_puts("\n");
	if (current_task == 0) {

		task_fd_table_release(&tasks[0]);
		user_exit_status = status;
		user_exit_pending = 1;
		return 0;
	}

	task_t *t = &tasks[current_task];

	if (t->fb_map_count > 0) {
		if (framebuffer_users >= t->fb_map_count) framebuffer_users -= t->fb_map_count;
		else framebuffer_users = 0;
		t->fb_map_count = 0;

	}
	task_clear_child_tid(t);
	task_fd_table_release(t);
	t->exit_status = status;
	t->state = TASK_ZOMBIE;

	if (t->ppid > 0) {
		for (int i=0; i<MAX_TASKS; i++) {
			task_t *p = &tasks[i];
			if (p->used && p->pid == t->ppid) {
				sig_pending_set(p, SIGCHLD);

				break;
			}
		}
	}

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
			debugcon_puts("[SIGDBG] wake wait4 parent pid=0x"); debug_hex64("", (uint64_t)p->pid); debug_hex64(" child=0x", (uint64_t)t->pid); debug_hex64(" pending0=0x", p->sig_pending[0]); debugcon_puts("\n");
			p->wait4_active = 0;
			p->wait4_pid = -1;
			p->wait4_status_ptr = 0;
			p->wait4_reap_pid = t->pid;
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
	task_account_switch(next);
	(void)vmm_user_activate(tasks[next].ctx.cr3);
	task_switch_target = next;
	task_switch_pending = 1;
	task_return_ctx = &tasks[next].ctx;
	return 0;
}

static int exception_signal_for_vector(uint64_t vector) {
	switch (vector) {
	case 0:  return 8;  /* SIGFPE: divide error */
	case 1:  return 5;  /* SIGTRAP: debug exception */
	case 3:  return 5;  /* SIGTRAP: breakpoint */
	case 4:  return 5;  /* SIGTRAP: overflow trap */
	case 6:  return 4;  /* SIGILL: invalid opcode */
	case 7:  return 8;  /* SIGFPE: device-not-available */
	case 11: return 7;  /* SIGBUS: segment not present */
	case 17: return 7;  /* SIGBUS: alignment check */
	case 18: return 7;  /* SIGBUS: machine check */
	case 16: case 19: return 8; /* floating-point / SIMD exception */
	default: return 11; /* SIGSEGV: invalid memory/segment state */
	}
}

static void exception_terminate_user_process(uint64_t num, uint64_t error_code, uint64_t rip) {
	if (current_task < 0 || current_task >= MAX_TASKS) current_task = 0;
	task_init_root();
	if (!tasks[current_task].used) current_task = 0;
	int pid = tasks[current_task].pid;
	int signal = exception_signal_for_vector(num);
	debugcon_puts("[USER-FAULT] process fault; terminating only the user task\n");
	debug_hex64("[USER-FAULT] pid=", (uint64_t)(uint32_t)pid);
	debug_hex64("[USER-FAULT] vector=", num);
	debug_hex64("[USER-FAULT] error=", error_code);
	debug_hex64("[USER-FAULT] rip=", rip);
	debug_hex64("[USER-FAULT] signal=", (uint64_t)(uint32_t)signal);
	(void)sys_exit(128 + signal);
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

int64_t sys_input_wait_key(void) {

	for (;;) {
		uint8_t key = get_key();
		if (key) return (int64_t)key;
		__asm__ volatile("sti; hlt; cli" ::: "memory");
	}
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
	current_task=next; task_account_switch(next); (void)vmm_user_activate(tasks[next].ctx.cr3); task_switch_target=next; task_switch_pending=1; task_return_ctx=&tasks[next].ctx;
	return 0;
}

int64_t sys_getpriority(int which, int who) {
	task_init_root();
	if (which != 0) return -EINVAL;                   
	if (who == 0) who = tasks[current_task].pid;
	for (int i=0;i<MAX_TASKS;i++) if (tasks[i].used && tasks[i].pid == who) return tasks[i].nice + 20;
	return -3;
}

int64_t sys_setpriority(int which, int who, int prio) {
	task_init_root();
	if (which != 0) return -EINVAL;
	if (who == 0) who = tasks[current_task].pid;
	return task_set_nice(who, prio - 20);
}

#define CLONE_VFORK 0x00004000ULL
#define CLONE_SYSVSEM 0x00040000ULL
#define CLONE_DETACHED 0x00400000ULL
#define CLONE_SIGNAL_MASK 0x000000ffULL

static int task_clone_stack_top_writable(task_t *t, uint64_t stack_top) {
	if (!t || !stack_top || stack_top >= USER_LIMIT) return 0;
	uint64_t last_byte = stack_top - 1;
	if (user_range_ok(last_byte, 1, 1)) return 1;
	uint64_t prot = 0;
	return task_lazy_mmap_protection(t, last_byte, &prot) && (prot & PROT_WRITE) != 0;
}

int64_t sys_clone(syscall_frame_t *f, uint64_t flags, uint64_t child_stack, uint64_t ptid, uint64_t ctid, uint64_t tls) {
	task_init_root();
	if ((flags & CLONE_SETTLS) && tls >= USER_LIMIT) {
		debugcon_puts("[CLONE] invalid TLS=0x"); debug_hex64("", tls); debugcon_puts("\n");
		return -EFAULT;
	}

	const uint64_t allowed=CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|CLONE_SETTLS|CLONE_PARENT_SETTID|CLONE_CHILD_CLEARTID|CLONE_VFORK|CLONE_SYSVSEM|CLONE_DETACHED|CLONE_SIGNAL_MASK;
	if ((flags & ~allowed) != 0) {
		debugcon_puts("[CLONE] rejected flags=0x"); debug_hex64("", flags);
		return -EINVAL;
	}
	if (!(flags & CLONE_VM)) return -ENOSYS;

	if (child_stack && !task_clone_stack_top_writable(&tasks[current_task], child_stack)) {
		debugcon_puts("[CLONE] invalid stack top=0x"); debug_hex64("", child_stack); debugcon_puts("\n");
		return -EFAULT;
	}
	if ((flags & CLONE_PARENT_SETTID) && (!ptid || !user_range_ok(ptid,sizeof(int),1))) return -EFAULT;
	if ((flags & CLONE_CHILD_CLEARTID) && (!ctid || !user_range_ok(ctid,sizeof(int),1))) return -EFAULT;
	int slot=-1; for(int i=0;i<MAX_TASKS;i++) if(!tasks[i].used){slot=i;break;}
	if(slot<0)return-ENOMEM;
	task_t *parent=&tasks[current_task], *child=&tasks[slot];
	task_save_from_frame(parent,f);
	child->used=1; child->state=TASK_RUNNABLE; child->pid=next_pid++; child->ppid=parent->pid;
	child->tgid=(flags&CLONE_THREAD)?parent->tgid:child->pid; child->pgrp=parent->pgrp; child->session=parent->session; child->controlling_tty=parent->controlling_tty; child->exit_status=0; child->tid_address=(flags&CLONE_CHILD_CLEARTID)?(uint64_t)ctid:0; child->futex_addr=0; child->futex_cr3=0; child->futex_deadline_ns=0; child->pipe_wait_fd=-1; child->pipe_wait_buf=0; child->pipe_wait_count=0; child->io_wait_kind=IO_WAIT_NONE; child->io_wait_valid=0; child->io_wait_nr=0; child->io_wait_deadline_ns=0; child->io_resume_valid=0; child->io_resume_nr=0; child->io_resume_rip=0; child->io_resume_start_ns=0; child->io_resume_deadline_ns=0;
	child->wait4_active=0; child->wait4_pid=-1; child->wait4_status_ptr=0; child->wait4_reap_pid=-1;
	child->heap_base=parent->heap_base; child->heap_end=parent->heap_end; child->mmap_next=parent->mmap_next; child->fb_map_count=0;
	for (int lm=0; lm<MAX_LAZY_USER_MMAPS; ++lm) child->lazy_mmaps[lm]=parent->lazy_mmaps[lm];
	child->nice=parent->nice; child->switches=0; child->cpu_ticks=0;
	child->ctx=parent->ctx; child->ctx.rax=0;
	debug_hex64(" flags=0x", flags);
	debug_hex64(" tls_arg=0x", tls);
	debug_hex64(" inherited_fs=0x", child->ctx.fs_base);
	debug_hex64(" inherited_gs=0x", child->ctx.gs_base);
	child->sig_pending[0]=child->sig_pending[1]=0;
	child->sig_blocked[0]=parent->sig_blocked[0]; child->sig_blocked[1]=parent->sig_blocked[1];
	for (int si=1; si<=64; ++si) child->sig_actions[si]=parent->sig_actions[si];
	child->sig_saved_valid=0; child->sig_active=0; child->cwd=parent->cwd;
	if (task_fd_table_clone(parent, child, (flags & CLONE_FILES) != 0) < 0) { child->used = 0; return -EMFILE; }
	if (flags & CLONE_SETTLS) child->ctx.fs_base=tls;

	if (child_stack) {

		child->ctx.rsp = child_stack;
		child->ctx.rbp = 0;
		debugcon_puts("[CLONE] using musl child stack rsp=0x");
		debug_hex64("", child->ctx.rsp);
	}
	if((flags & CLONE_PARENT_SETTID) && ptid && user_copy_to(ptid,(const uint8_t*)&child->pid,sizeof(int))) { task_fd_table_release(child); child->used=0; return-EFAULT; }
	if((flags & CLONE_CHILD_CLEARTID) && ctid && user_copy_to(ctid,(const uint8_t*)&child->pid,sizeof(int))) { task_fd_table_release(child); child->used=0; return-EFAULT; }
	parent->ctx.rax=child->pid;
	if (flags & CLONE_THREAD) child->tgid=parent->tgid;

	return child->pid;
}

int64_t sys_fork(syscall_frame_t *f) {

	const uint64_t flags = CLONE_FS | CLONE_SIGHAND;
	int64_t pid = sys_clone(f, CLONE_VM | flags, 0, 0, 0, 0);
	if (pid < 0) return pid;
	task_t *parent=&tasks[current_task], *child=NULL;
	for (int i=0;i<MAX_TASKS;i++) if(tasks[i].used && tasks[i].pid==(int)pid){ child=&tasks[i]; break; }
	if (!child) return -3;

	child->state = TASK_BLOCKED;
	uint64_t child_cr3=0;
	if (vmm_user_space_clone(parent->ctx.cr3,&child_cr3)!=0) {

		task_reap_resources(child);
		child->used=0; child->state=TASK_UNUSED;
		return -ENOMEM;
	}
	child->ctx.cr3=child_cr3;
	child->state = TASK_RUNNABLE;

	vmm_user_flush_tlb();
	return pid;
}

static int exec_target_valid(uint64_t cr3, uint64_t entry, uint64_t stack) {
	uint64_t phys = 0, pte = 0;

	if (!entry || !stack || entry >= USER_LIMIT || stack > USER_STACK_TOP || stack < 16) {
		debugcon_puts("[EXEC] Invalid entry/stack/limit\n");
		return 0;
	}

	if (vmm_user_translate_cr3(cr3, entry & PAGE_MASK, &phys, &pte) != 0 ||
	    vmm_user_validate_page_cr3(cr3, entry & PAGE_MASK, 0, 1) != 0) {
		debugcon_puts("[EXEC] Entry translate/U-S validation FAILED\n");
		return 0;
	}
	debug_hex64("[EXEC] Entry PTE=0x", pte);
	if (!(pte & PTE_P)) { debugcon_puts("[EXEC] ERROR: Entry not present\n"); return 0; }
	if (!(pte & PTE_U)) { debugcon_puts("[EXEC] ERROR: Entry not user-accessible!\n"); return 0; }
	if (pte & PTE_NX)   { debugcon_puts("[EXEC] ERROR: Entry not executable!\n"); return 0; }

	if (vmm_user_translate_cr3(cr3, (stack - 1) & PAGE_MASK, &phys, &pte) != 0 ||
	    vmm_user_validate_page_cr3(cr3, (stack - 1) & PAGE_MASK, 1, 0) != 0) {
		debugcon_puts("[EXEC] Stack translate/U-S validation FAILED\n");
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

	if (!syscall_user_cr3 || vmm_user_activate(syscall_user_cr3) != 0) {
		debugcon_puts("[EXEC] cannot activate caller CR3\n");
		return -EFAULT;
	}
	debug_hex64("[EXEC] caller CR3=0x", syscall_user_cr3);
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

	if (!new_cr3 || !exec_target_valid(new_cr3, entry, stack)) {
		debugcon_puts("[V12.24] validation FAILED\n");
		return -ENOMEM;
	}

	task_init_root();
	if (current_task < 0 || current_task >= MAX_TASKS || !tasks[current_task].used) {
		debugcon_puts("[V12.24] ERROR: invalid current task\n");
		return -3;
	}

	task_t *cur = &tasks[current_task];

	uint64_t old_cr3 = cur->ctx.cr3 & PAGE_MASK;
	uint64_t replacement_cr3 = new_cr3 & PAGE_MASK;
	if (old_cr3 && old_cr3 != replacement_cr3 && !task_cr3_is_shared(cur)) {
		debug_hex64("[EXEC-RECLAIM] destroying old CR3=0x", old_cr3);
		if (vmm_user_space_destroy(old_cr3) != 0) {
			debug_hex64("[EXEC-RECLAIM] failed to destroy old CR3=0x", old_cr3);
		}
	}

	for (int fd=0; fd<MAX_FILES; fd++) {
		if (files[fd].in_use && (files[fd].flags & 0x80000)) sys_close(fd);
	}

	cur->mmap_next = USER_MMAP_MIN;
	for (int lm=0; lm<MAX_LAZY_USER_MMAPS; ++lm) cur->lazy_mmaps[lm]=(lazy_user_mmap_t){0,0,0,0};
	cur->ctx.rax = 0;
	cur->ctx.rsp = stack;
	cur->ctx.rip = entry;
	cur->ctx.rflags = 0x202;
	cur->ctx.cr3 = new_cr3;

	cur->ctx.fs_base = 0;
	cur->ctx.gs_base = 0;
	fs_base = 0;
	gs_base = 0;

	syscall_user_cr3 = new_cr3;
	syscall_user_rip = entry;
	syscall_user_rsp = stack;
	syscall_user_rflags = 0x202;
	syscall_saved_rax = 0;
	user_exec_pending = 1;


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
		task_reap_resources(t);
		t->used=0; t->state=TASK_UNUSED;

		sig_pending_set(cur, SIGCHLD);
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
	const char*names[]={"YabroOS-32","yabroos-32","0.1","x86_64","localdomain"};
	uint8_t out[390];for(size_t i=0;i<390;i++)out[i]=0;
	for(size_t n=0;n<sizeof(names)/sizeof(names[0]);n++)for(size_t j=0;names[n][j]&&j<64;j++)out[n*65+j]=(uint8_t)names[n][j];
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
#define PT_INTERP 3
#define PT_PHDR 6
#define ELF_MAGIC1 0x7F
#define ELF_MAGIC2 'E'
#define ELF_MAGIC3 'L'
#define ELF_MAGIC4 'F'

static int init_user_stack(uint64_t cr3, const Elf64_Ehdr *ehdr,
						   const Elf64_Phdr *phdrs, uint16_t phnum,
						   size_t file_size, const char *exec_path,
						   const char *const *argv, size_t argc,
						   const char *const *envp, size_t envc,
						   uint64_t load_bias, uint64_t entry_override,
						   uint64_t interp_base,
						   uint64_t *stack_out) {
	(void)cr3;

	uint64_t phdr_va = 0;

	for (uint16_t i = 0; i < phnum; ++i) {
		const Elf64_Phdr *ph = &phdrs[i];
		if (ph->p_type == 6 ) {
			phdr_va = ph->p_vaddr + load_bias;
			break;
		}
	}

	if (!phdr_va) {
		for (uint16_t i = 0; i < phnum; ++i) {
			const Elf64_Phdr *ph = &phdrs[i];
			if (ph->p_type != PT_LOAD) continue;
			if (ehdr->e_phoff >= ph->p_offset &&
				ehdr->e_phoff < ph->p_offset + ph->p_filesz) {
				phdr_va = ph->p_vaddr + (ehdr->e_phoff - ph->p_offset) + load_bias;
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

	const size_t ARG_STRING_MAX = 256;
	uint64_t str_cursor = random_va;
	for (size_t i = 0; i < argc; ++i) {
		const char *src = argv ? argv[i] : NULL;
		if (!src) return -1;
		size_t len = 0;
		while (len + 1 < ARG_STRING_MAX && src[len]) ++len;
		if (src[len]) {
			debugcon_puts("[ELF] argv string exceeds 255 bytes\n");
			return -1;
		}
		str_cursor = (str_cursor - (uint64_t)(len + 1)) & ~0xFULL;
		if (copy_to_user(user_cr3, str_cursor, (const uint8_t *)src, len + 1) != 0) return -1;
		argv_va[i] = str_cursor;
	}
	for (size_t i = 0; i < envc; ++i) {
		const char *src = envp ? envp[i] : NULL;
		if (!src) return -1;
		size_t len = 0;
		while (len + 1 < ARG_STRING_MAX && src[len]) ++len;
		if (src[len]) {
			debugcon_puts("[ELF] env string exceeds 255 bytes\n");
			return -1;
		}
		str_cursor = (str_cursor - (uint64_t)(len + 1)) & ~0xFULL;
		if (copy_to_user(user_cr3, str_cursor, (const uint8_t *)src, len + 1) != 0) return -1;
		envp_va[i] = str_cursor;
	}

	uint64_t auxv[32];
	size_t an = 0;
	if (phdr_va) { auxv[an++] = 3; auxv[an++] = phdr_va; }
	auxv[an++] = 4; auxv[an++] = sizeof(Elf64_Phdr);
	auxv[an++] = 5; auxv[an++] = phnum;
	auxv[an++] = 6; auxv[an++] = PAGE_SIZE;
	auxv[an++] = 9; auxv[an++] = entry_override ? entry_override : (ehdr->e_entry + load_bias);
	if (interp_base) { auxv[an++] = 7; auxv[an++] = interp_base; }
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


	*stack_out = sp;
	return 0;
}

static int load_bootfs_elf_image(const char *path, uint64_t bias,
								 uint64_t *entry_out) {
	if (!path || !path[0] || !entry_out) return -1;

	uint64_t file_size = bootfs_file_size(path);
	if (file_size < sizeof(Elf64_Ehdr) || file_size > (64ULL << 20)) {
		debugcon_puts("[ELF/INTERP] invalid interpreter size\n");
		return -1;
	}

	uint8_t hdrbuf[4096];
	size_t got = bootfs_read_root_file(path, 0, hdrbuf, sizeof(hdrbuf));
	if (got < sizeof(Elf64_Ehdr)) return -1;

	const Elf64_Ehdr *eh = (const Elf64_Ehdr *)hdrbuf;
	if (eh->e_ident[0] != ELF_MAGIC1 || eh->e_ident[1] != ELF_MAGIC2 ||
		eh->e_ident[2] != ELF_MAGIC3 || eh->e_ident[3] != ELF_MAGIC4 ||
		eh->e_ident[4] != 2 || eh->e_ident[5] != 1 ||
		eh->e_machine != 0x3E || (eh->e_type != 2 && eh->e_type != 3) ||
		eh->e_phentsize < sizeof(Elf64_Phdr) || eh->e_phnum == 0)
		return -1;

	uint64_t ph_bytes = (uint64_t)eh->e_phnum * eh->e_phentsize;
	if (eh->e_phoff > file_size || ph_bytes > file_size - eh->e_phoff ||
		eh->e_phoff > sizeof(hdrbuf) || ph_bytes > sizeof(hdrbuf) - eh->e_phoff) {

		debugcon_puts("[ELF/INTERP] program headers too large\n");
		return -1;
	}

	const Elf64_Phdr *phdrs = (const Elf64_Phdr *)(hdrbuf + eh->e_phoff);
	uint64_t image_entry = eh->e_entry + bias;
	int load_count = 0, entry_exec = 0;

	for (uint16_t i = 0; i < eh->e_phnum; ++i) {
		const Elf64_Phdr *ph = (const Elf64_Phdr *)((const uint8_t *)phdrs +
								(uint64_t)i * eh->e_phentsize);
		if (ph->p_type != PT_LOAD) continue;
		++load_count;
		if (!ph->p_memsz || ph->p_filesz > ph->p_memsz ||
			ph->p_offset > file_size || ph->p_filesz > file_size - ph->p_offset)
			return -1;
		if (ph->p_vaddr > UINT64_MAX - ph->p_memsz) return -1;

		uint64_t va = ph->p_vaddr + bias;
		uint64_t end = va + ph->p_memsz;
		if (va < ph->p_vaddr || end < va || va >= USER_LIMIT || end > USER_LIMIT)
			return -1;

		uint64_t map_start = va & PAGE_MASK;
		uint64_t map_end = (end + PAGE_SIZE - 1) & PAGE_MASK;
		if (map_end <= map_start || map_user_range(user_cr3, map_start, map_end, ph->p_flags) != 0)
			return -1;

		uint64_t copied = 0;
		while (copied < ph->p_filesz) {
			uint8_t pagebuf[PAGE_SIZE];
			uint64_t chunk = ph->p_filesz - copied;
			if (chunk > PAGE_SIZE) chunk = PAGE_SIZE;
			size_t n = bootfs_read_root_file(path, ph->p_offset + copied, pagebuf, (size_t)chunk);
			if (n != chunk) return -1;
			if (copy_to_user_loader(user_cr3, va + copied, pagebuf, chunk) != 0) return -1;
			copied += chunk;
		}
		if (ph->p_memsz > ph->p_filesz &&
			zero_user_loader(user_cr3, va + ph->p_filesz, ph->p_memsz - ph->p_filesz) != 0)
			return -1;

		if ((ph->p_flags & 1) && image_entry >= va && image_entry < end)
			entry_exec = 1;
	}

	if (!load_count || !entry_exec || image_entry >= USER_LIMIT) return -1;
	*entry_out = image_entry;
	return 0;
}

typedef int64_t (*elf_read_at_fn)(void *ctx, uint64_t offset, uint8_t *dst, uint64_t len);

static int elf_reader_read_exact(void *ctx, elf_read_at_fn reader, uint64_t file_size,
                                 uint64_t offset, uint8_t *dst, uint64_t len) {
    if (!reader || (!dst && len) || offset > file_size || len > file_size - offset)
        return -1;
    int64_t got = reader(ctx, offset, dst, len);
    return got == (int64_t)len ? 0 : -1;
}

struct elf_memory_reader { const uint8_t *data; uint64_t size; };
static int64_t elf_memory_reader_read(void *opaque, uint64_t offset, uint8_t *dst, uint64_t len) {
    struct elf_memory_reader *m = (struct elf_memory_reader *)opaque;
    if (!m || (!dst && len) || offset > m->size || len > m->size - offset || len > INT64_MAX)
        return -1;
    for (uint64_t i = 0; i < len; ++i) dst[i] = m->data[offset + i];
    return (int64_t)len;
}

uint64_t load_elf_with_args_reader(void *reader_ctx, elf_read_at_fn reader, uint64_t size,
                                   const char *exec_path,
                                   const char *const *argv, size_t argc,
                                   const char *const *envp, size_t envc,
                                   uint64_t *stack_top) {
    if (!reader || !stack_top || size < sizeof(Elf64_Ehdr)) return 0;
    Elf64_Ehdr ehdr_storage;
    if (elf_reader_read_exact(reader_ctx, reader, size, 0,
                              (uint8_t *)&ehdr_storage, sizeof(ehdr_storage)) != 0) {
        debugcon_puts("[ELF] cannot read ELF header\n");
        return 0;
    }
    const Elf64_Ehdr *ehdr = &ehdr_storage;

	if (ehdr->e_ident[0] != ELF_MAGIC1 || ehdr->e_ident[1] != ELF_MAGIC2 ||
		ehdr->e_ident[2] != ELF_MAGIC3 || ehdr->e_ident[3] != ELF_MAGIC4 ||
		ehdr->e_ident[4] != 2 || ehdr->e_ident[5] != 1 ||
		(ehdr->e_type != 2 && ehdr->e_type != 3) || ehdr->e_machine != 0x3E ||
		ehdr->e_version != 1 || ehdr->e_ehsize < sizeof(Elf64_Ehdr)) {
		debugcon_puts("[ELF] Invalid ELF64 x86_64 executable\n");
		return 0;
	}
    if (ehdr->e_phentsize < sizeof(Elf64_Phdr) || ehdr->e_phnum == 0 || ehdr->e_phnum > 128) {
        debugcon_puts("[ELF] Invalid program header table\n");
        return 0;
    }
    uint64_t ph_bytes = (uint64_t)ehdr->e_phnum * ehdr->e_phentsize;
    if (ehdr->e_phoff > size || ph_bytes > size - ehdr->e_phoff) {
        debugcon_puts("[ELF] Program headers outside file\n");
        return 0;
    }
    Elf64_Phdr phdr_storage[128];
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        uint64_t off = ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize;
        if (elf_reader_read_exact(reader_ctx, reader, size, off,
                                  (uint8_t *)&phdr_storage[i], sizeof(Elf64_Phdr)) != 0) {
            debugcon_puts("[ELF] cannot read program header\n");
            return 0;
        }
    }
    const Elf64_Phdr *phdrs = phdr_storage;

    if (create_user_space() != 0) {
        debugcon_puts("[ELF] Cannot create user CR3\n");
        return 0;
    }

	uint64_t load_bias = 0;
	if (ehdr->e_type == 3) {
		load_bias = KYRONIX_PIE_BASE;
	}

	uint64_t image_entry = ehdr->e_entry + load_bias;
	if (image_entry < ehdr->e_entry) {
		debugcon_puts("[ELF] entry/load-bias overflow\n");
		return 0;
	}

	int load_count = 0;
	int entry_exec = 0;
	uint64_t image_end = 0;

	for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
		const Elf64_Phdr *ph = &phdrs[i];
		if (ph->p_type != PT_LOAD) continue;
		++load_count;

		if (ph->p_memsz == 0) {
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

		uint64_t load_vaddr = ph->p_vaddr + load_bias;
		uint64_t end = load_vaddr + ph->p_memsz;
		if (load_vaddr < ph->p_vaddr || load_vaddr >= USER_LIMIT || end > USER_LIMIT || end < load_vaddr) {
			debugcon_puts("[ELF] PT_LOAD outside user address space\n");
			return 0;
		}
		if (end > image_end) image_end = end;

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

		uint64_t map_start = load_vaddr & PAGE_MASK;
		uint64_t map_end = (end + PAGE_SIZE - 1) & PAGE_MASK;
		if (map_end <= map_start || map_user_range(user_cr3, map_start, map_end, ph->p_flags) != 0) {
			debugcon_puts("[ELF] Failed to allocate/map PT_LOAD\n");
			return 0;
		}

        if (ph->p_filesz) {

            uint8_t elf_chunk[4096];
            uint64_t copied = 0;
            while (copied < ph->p_filesz) {
                uint64_t chunk = ph->p_filesz - copied;
                if (chunk > sizeof(elf_chunk)) chunk = sizeof(elf_chunk);
                if (elf_reader_read_exact(reader_ctx, reader, size,
                                          ph->p_offset + copied, elf_chunk, chunk) != 0) {
                    debugcon_puts("[ELF] PT_LOAD source read failed\n");
                    return 0;
                }
                int rc = copy_to_user_loader(user_cr3, load_vaddr + copied, elf_chunk, chunk);
                if (rc) {
                    debugcon_puts("[ELF] PT_LOAD file copy failed rc=0x");
                    debug_hex64("", (uint64_t)(uint32_t)rc);
                    return 0;
                }
                copied += chunk;
            }
        }
		if (ph->p_memsz > ph->p_filesz) {
			int rc = zero_user_loader(user_cr3, load_vaddr + ph->p_filesz,
									  ph->p_memsz - ph->p_filesz);
			if (rc) {
				debugcon_puts("[ELF] PT_LOAD BSS zero failed rc=0x");
				debug_hex64("", (uint64_t)(uint32_t)rc);
				return 0;
			}
		}

		if ((ph->p_flags & 1) && image_entry >= load_vaddr &&
			image_entry < end)
			entry_exec = 1;
	}

	if (!load_count || !entry_exec || image_entry >= USER_LIMIT) {
		debugcon_puts("[ELF] Invalid/no executable entry\n");
		return 0;
	}

	user_heap_reset_for_image(image_end);

	char interp_path[ELF_INTERP_PATH_MAX];
	interp_path[0] = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        const Elf64_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_INTERP) continue;
        if (!ph->p_filesz || ph->p_filesz >= sizeof(interp_path) ||
            ph->p_offset > size || ph->p_filesz > size - ph->p_offset) {
            debugcon_puts("[ELF] invalid PT_INTERP\n");
            return 0;
        }
        if (elf_reader_read_exact(reader_ctx, reader, size, ph->p_offset,
                                  (uint8_t *)interp_path, ph->p_filesz) != 0) {
            debugcon_puts("[ELF] PT_INTERP read failed\n");
            return 0;
        }
        interp_path[ph->p_filesz - 1] = 0;
        break;
    }

	uint64_t interp_entry = 0;
	uint64_t interp_base = 0;
	if (interp_path[0]) {
		debugcon_puts(interp_path);
		debugcon_puts("\n");
		interp_base = KYRONIX_INTERP_BASE;
		if (load_bootfs_elf_image(interp_path, interp_base, &interp_entry) != 0) {
			debugcon_puts("[ELF] interpreter load failed\n");
			return 0;
		}
	}

	if (map_user_stack(user_cr3) != 0) {
		debugcon_puts("[ELF] Failed to allocate user stack\n");
		return 0;
	}

	if (init_user_stack(user_cr3, ehdr, phdrs, ehdr->e_phnum, size, exec_path,
						argv, argc, envp, envc, load_bias,
						image_entry, interp_base, stack_top) != 0) {
		debugcon_puts("[ELF] Failed to initialize user process stack\n");
		return 0;
	}

	for (int j = 15; j >= 0; --j) {
		uint8_t n = (user_cr3 >> (j * 4)) & 0xF;
		debugcon_putc(n < 10 ? '0' + n : 'a' + n - 10);
	}
	debugcon_puts("\n");

	if (interp_entry) {
		image_entry = interp_entry;
	}
	return image_entry;
}

uint64_t load_elf_with_args(const uint8_t *data, size_t size, const char *exec_path,
                            const char *const *argv, size_t argc,
                            const char *const *envp, size_t envc,
                            uint64_t *stack_top) {
    if (!data) return 0;
    struct elf_memory_reader memory = { data, (uint64_t)size };
    return load_elf_with_args_reader(&memory, elf_memory_reader_read,
                                     (uint64_t)size, exec_path,
                                     argv, argc, envp, envc, stack_top);
}

void syscall_init(void) {

	if (!kernel_cr3) {
		kernel_cr3 = get_cr3();
	}
	kernel_rsp = (uint64_t)kernel_stack + KERNEL_STACK_SIZE;
	user_rsp = 0;

	tss.rsp0 = kernel_rsp;
	debugcon_puts("\n");
	init_syscall_msrs();

	init_files();
	task_init_root();
}

uint64_t load_elf(const uint8_t *data, size_t size, const char *exec_path, uint64_t *stack_top) {
	return load_elf_with_args(data, size, exec_path, NULL, 0, NULL, 0, stack_top);
}
