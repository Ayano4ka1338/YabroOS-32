// Virtual memory manager - (c) Ayano4ka1338, 2026

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PAGE_SIZE 0x1000ULL
#define PAGE_MASK (~0xFFFULL)
#define PTE_P 0x001ULL
#define PTE_W 0x002ULL
#define PTE_U 0x004ULL
#define PTE_PS (1ULL << 7)
#define PTE_NX (1ULL << 63)
#define PTE_PWT 0x008ULL
#define PTE_PCD 0x010ULL
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define USER_TOP 0x0000800000000000ULL

extern uint64_t pmm_alloc_page(void);
extern void pmm_free_page(uint64_t);
extern void *pmm_phys_to_virt(uint64_t);
extern uint64_t pmm_hhdm(void);
extern uint64_t graphics_fb_phys(void);
extern uint64_t graphics_fb_size(void);
static inline void debugcon_putc(char c) {
	__asm__ volatile ("outb %0, %1" : : "a" ((uint8_t)c), "dN" ((uint16_t)0xE9));
}
static inline void debugcon_puts(const char *s) {
	if (!s) return;
	while (*s) debugcon_putc(*s++);
}
static inline void debug_hex(const char *s, uint64_t v) {
	static const char hex[] = "0123456789abcdef";
	if (s) debugcon_puts(s);
	debugcon_puts("0x");
	for (int i = 15; i >= 0; --i)
		debugcon_putc(hex[(v >> (i * 4)) & 0xF]);
	debugcon_putc('\n');
}

struct vmm_space { uint64_t pml4_phys; };
static struct vmm_space g_user;
static bool g_user_valid;

static inline uint64_t idx4(uint64_t v) { return (v >> 39) & 0x1ff; }
static inline uint64_t idx3(uint64_t v) { return (v >> 30) & 0x1ff; }
static inline uint64_t idx2(uint64_t v) { return (v >> 21) & 0x1ff; }
static inline uint64_t idx1(uint64_t v) { return (v >> 12) & 0x1ff; }
static inline bool canonical_user(uint64_t va) { return va < USER_TOP; }

static inline uint64_t *table(uint64_t phys)
{
	if (!phys || (phys & (PAGE_SIZE - 1))) return NULL;
	return (uint64_t *)pmm_phys_to_virt(phys & PTE_ADDR_MASK);
}

static void debug_hhdm_mapping(uint64_t phys)
{
	uint64_t cr3;
	__asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
	cr3 &= PAGE_MASK;
	uint64_t va = pmm_hhdm() + phys;
	debug_hex("[VMM] HHDM VA=0x", va);

	uint64_t *pml4 = table(cr3);
	if (!pml4) { debugcon_puts("[VMM] HHDM walk: PML4 unavailable\n"); return; }
	uint64_t e4 = pml4[idx4(va)];
	debug_hex("[VMM] HHDM PML4E=0x", e4);
	if (!(e4 & PTE_P)) return;

	uint64_t *pdpt = table(e4 & PTE_ADDR_MASK);
	if (!pdpt) return;
	uint64_t e3 = pdpt[idx3(va)];
	debug_hex("[VMM] HHDM PDPTE=0x", e3);
	if (!(e3 & PTE_P)) return;
	if (e3 & PTE_PS) { debugcon_puts("[VMM] HHDM: 1GiB mapping\n"); return; }

	uint64_t *pd = table(e3 & PTE_ADDR_MASK);
	if (!pd) return;
	uint64_t e2 = pd[idx2(va)];
	debug_hex("[VMM] HHDM PDE=0x", e2);
	if (!(e2 & PTE_P)) return;
	if (e2 & PTE_PS) { debugcon_puts("[VMM] HHDM: 2MiB mapping\n"); return; }

	uint64_t *pt = table(e2 & PTE_ADDR_MASK);
	if (!pt) return;
	uint64_t e1 = pt[idx1(va)];
	debug_hex("[VMM] HHDM PTE=0x", e1);
	if (!(e1 & PTE_P)) return;
	debugcon_puts((e1 & PTE_W) ? "[VMM] HHDM writable\n" : "[VMM] HHDM READ-ONLY\n");
}

static bool zero_page(uint64_t phys) {
	uint64_t *p = table(phys);
	if (!p) return false;
	volatile uint64_t *vp = (volatile uint64_t *)p;
	for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i) vp[i] = 0;
	return true;
}

static uint64_t *new_user_table(uint64_t *entry)
{
	uint64_t phys = pmm_alloc_page();
	if (!phys) return NULL;

	uint64_t *virt = table(phys);
	if (!virt) {
		pmm_free_page(phys);
		return NULL;
	}

	for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i)
		((volatile uint64_t *)virt)[i] = 0;

	*entry = (phys & PTE_ADDR_MASK) | PTE_P | PTE_W | PTE_U;
	return virt;
}

static uint64_t *ensure_user_child(uint64_t *parent, uint64_t idx)
{
	uint64_t e = parent[idx];

	if (!(e & PTE_P) || (e & PTE_PS))
		return new_user_table(&parent[idx]);

	uint64_t *child = table(e & PTE_ADDR_MASK);
	if (!child)
		return new_user_table(&parent[idx]);
	uint64_t wanted = (e & PTE_ADDR_MASK) | PTE_P | PTE_W | PTE_U;
	if ((e & (PTE_W | PTE_U)) != (PTE_W | PTE_U))
		parent[idx] = wanted;

	return child;
}

#define VMM_VGA_PHYS 0x000B8000ULL

static uint64_t *ensure_kernel_child(uint64_t *parent, uint64_t idx)
{
	uint64_t e = parent[idx];
	if (!(e & PTE_P) || (e & PTE_PS)) {
		uint64_t phys = pmm_alloc_page();
		if (!phys) return NULL;
		uint64_t *child = table(phys);
		if (!child) {
			pmm_free_page(phys);
			return NULL;
		}
		for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i)
			((volatile uint64_t *)child)[i] = 0;
		parent[idx] = (phys & PTE_ADDR_MASK) | PTE_P | PTE_W;
		return child;
	}
	return table(e & PTE_ADDR_MASK);
}

static int map_kernel_phys_in_user_cr3(uint64_t virt, uint64_t phys, uint64_t flags)
{
	if (!g_user_valid || (virt & 0xfff) || (phys & 0xfff)) return -1;

	if ((virt >> 48) != 0xFFFFULL) return -1;

	uint64_t *pml4 = table(g_user.pml4_phys);
	if (!pml4) return -1;

	uint64_t *pdpt = ensure_kernel_child(pml4, idx4(virt));
	if (!pdpt) return -1;
	uint64_t *pd = ensure_kernel_child(pdpt, idx3(virt));
	if (!pd) return -1;
	uint64_t *pt = ensure_kernel_child(pd, idx2(virt));
	if (!pt) return -1;

	pt[idx1(virt)] = (phys & PTE_ADDR_MASK) | PTE_P |
					 (flags & (PTE_W | PTE_NX | PTE_PWT | PTE_PCD));
	return 0;
}

static void ensure_vga_mapping(void)
{
	uint64_t va = pmm_hhdm() + VMM_VGA_PHYS;
	if ((va >> 48) != 0xFFFFULL) {
		debugcon_puts("[VMM] VGA HHDM address is non-canonical\n");
		return;
	}
	if (map_kernel_phys_in_user_cr3(va & PAGE_MASK,
									 VMM_VGA_PHYS & PAGE_MASK,
									 PTE_W | PTE_NX) == 0) {
		debugcon_puts("[VMM] VGA kernel mapping installed\n");
		debug_hex("[VMM] VGA VA=0x", va & PAGE_MASK);
	} else {
		debugcon_puts("[VMM] VGA kernel mapping FAILED\n");
	}
}

int vmm_user_space_create(void)
{
	uint64_t current_cr3;
	__asm__ volatile ("mov %%cr3, %0" : "=r"(current_cr3));
	current_cr3 &= PAGE_MASK;

	debugcon_puts("[VMM] create: current CR3\n");
	if (!current_cr3 || current_cr3 > PTE_ADDR_MASK) {
		debugcon_puts("[VMM] create: invalid CR3 physical address\n");
		return -1;
	}

	uint64_t *src = table(current_cr3);
	if (!src) {
		debugcon_puts("[VMM] create: invalid current PML4\n");
		return -1;
	}

	uint64_t pml4_phys = pmm_alloc_page();
	if (!pml4_phys) {
		debugcon_puts("[VMM] create: PML4 allocation failed\n");
		return -1;
	}

	debugcon_puts("[VMM] create: PML4 allocated\n");

	uint64_t *dst = table(pml4_phys);
	if (!dst) {
		pmm_free_page(pml4_phys);
		debugcon_puts("[VMM] create: PML4 HHDM mapping failed\n");
		return -1;
	}

	debugcon_puts("[VMM] create: PML4 mapped\n");
	for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i)
		((volatile uint64_t *)dst)[i] = 0;

	debugcon_puts("[VMM] create: PML4 zeroed\n");

	debugcon_puts("[VMM] create: copy kernel half\n");
	for (size_t i = 256; i < 512; ++i)
		dst[i] = src[i];
	debugcon_puts("[VMM] create: kernel half copied\n");

	g_user.pml4_phys = pml4_phys;
	g_user_valid = true;

	ensure_vga_mapping();

	return 0;
}

uint64_t vmm_user_cr3(void)
{
	return g_user_valid ? g_user.pml4_phys : 0;
}

static uint64_t clone_user_level(uint64_t src_phys, int level, int *err)
{
	uint64_t dst_phys = pmm_alloc_page();
	if (!dst_phys) { *err = -1; return 0; }
	uint64_t *src = table(src_phys), *dst = table(dst_phys);
	if (!src || !dst) { *err = -1; return 0; }
	for (size_t i=0;i<512;i++) dst[i]=0;

	for (size_t i=0;i<512;i++) {
		uint64_t e=src[i];
		if (!(e&PTE_P)) continue;
		if (e&PTE_PS) { *err=-2; return 0; }
		if (level==1) {
			uint64_t np=pmm_alloc_page();
			if (!np) { *err=-1; return 0; }
			uint8_t *sp=(uint8_t*)pmm_phys_to_virt(e&PTE_ADDR_MASK);
			uint8_t *dp=(uint8_t*)pmm_phys_to_virt(np);
			if (!sp || !dp) { *err=-1; return 0; }
			for (size_t j=0;j<PAGE_SIZE;j++) dp[j]=sp[j];
			dst[i]=(np&PTE_ADDR_MASK)|(e&(~PTE_ADDR_MASK));
		} else {
			uint64_t np=clone_user_level(e&PTE_ADDR_MASK, level-1, err);
			if (!np) return 0;
			dst[i]=(np&PTE_ADDR_MASK)|(e&(~PTE_ADDR_MASK));
		}
	}
	return dst_phys;
}

int vmm_user_space_clone(uint64_t src_cr3, uint64_t *dst_cr3_out)
{
	if (!src_cr3 || !dst_cr3_out) return -1;
	uint64_t *src=table(src_cr3&PTE_ADDR_MASK);
	if (!src) return -1;
	uint64_t dst_phys=pmm_alloc_page();
	if (!dst_phys) return -1;
	uint64_t *dst=table(dst_phys);
	if (!dst) return -1;
	for (size_t i=0;i<512;i++) dst[i]=0;

	for (size_t i=256;i<512;i++) dst[i]=src[i];

	int err=0;
	for (size_t i=0;i<256;i++) {
		uint64_t e=src[i];
		if (!(e&PTE_P)) continue;
		if (e&PTE_PS) { err=-2; break; }
		uint64_t np=clone_user_level(e&PTE_ADDR_MASK,3,&err);
		if (!np) break;
		dst[i]=(np&PTE_ADDR_MASK)|(e&(~PTE_ADDR_MASK));
	}
	if (err) return err;
	*dst_cr3_out=dst_phys;
	return 0;
}

static void destroy_user_level(uint64_t phys, int level)
{
	uint64_t *tabp = table(phys & PAGE_MASK);
	if (!tabp) return;

	for (size_t i = 0; i < 512; ++i) {
		uint64_t e = tabp[i];
		if (!(e & PTE_P)) continue;
		if (level > 1 && !(e & PTE_PS)) {
			uint64_t child = e & PTE_ADDR_MASK;
			destroy_user_level(child, level - 1);
			pmm_free_page(child);
		} else if (level == 1) {
			uint64_t page = e & PTE_ADDR_MASK;


			uint64_t fb = graphics_fb_phys() & PAGE_MASK;
			uint64_t fbsize = graphics_fb_size();
			uint64_t fbend = fb + ((fbsize + PAGE_SIZE - 1) & PAGE_MASK);
			if (!fbsize || page < fb || page >= fbend)
				pmm_free_page(page);
		}
		tabp[i] = 0;
	}
}

int vmm_user_space_destroy(uint64_t cr3)
{
	if (!cr3) return -1;
	uint64_t pml4_phys = cr3 & PAGE_MASK;
	uint64_t *pml4 = table(pml4_phys);
	if (!pml4) return -1;



	for (size_t i = 0; i < 256; ++i) {
		uint64_t e = pml4[i];
		if (!(e & PTE_P) || (e & PTE_PS)) continue;
		uint64_t child = e & PTE_ADDR_MASK;
		destroy_user_level(child, 3);
		pmm_free_page(child);
		pml4[i] = 0;
	}
	pmm_free_page(pml4_phys);
	return 0;
}

int vmm_user_activate(uint64_t cr3)
{
	cr3 &= PAGE_MASK;
	if (!cr3 || !table(cr3)) return -1;
	g_user.pml4_phys=cr3;
	g_user_valid=true;
	return 0;
}

extern uint64_t kernel_cr3;
void vmm_user_flush_tlb(void)
{
	if (!g_user_valid || !g_user.pml4_phys || !kernel_cr3) return;
	uint64_t user = g_user.pml4_phys & PAGE_MASK;
	uint64_t kern = kernel_cr3 & PAGE_MASK;
	uint64_t current;
	__asm__ volatile ("mov %%cr3, %0" : "=r"(current));
	current &= PAGE_MASK;
	if (current == user) {
		__asm__ volatile ("mov %0, %%cr3" :: "r"(user) : "memory");
		return;
	}
	__asm__ volatile ("mov %0, %%cr3" :: "r"(user) : "memory");
	__asm__ volatile ("mov %0, %%cr3" :: "r"(kern) : "memory");
}

int vmm_user_map(uint64_t virt, uint64_t phys, uint64_t flags)
{
	if (!g_user_valid || !canonical_user(virt) || (virt & 0xfff) || (phys & 0xfff))
		return -1;

	uint64_t *pml4 = table(g_user.pml4_phys);
	if (!pml4) return -1;

	uint64_t *pdpt = ensure_user_child(pml4, idx4(virt));
	if (!pdpt) return -1;
	uint64_t *pd = ensure_user_child(pdpt, idx3(virt));
	if (!pd) return -1;
	uint64_t *pt = ensure_user_child(pd, idx2(virt));
	if (!pt) return -1;

	uint64_t *leaf = &pt[idx1(virt)];
	uint64_t new_pte = (phys & PTE_ADDR_MASK) | PTE_P | PTE_U |
					  (flags & (PTE_W | PTE_NX | PTE_PWT | PTE_PCD));
	*leaf = new_pte;
	uint64_t current_cr3;
	__asm__ volatile ("mov %%cr3, %0" : "=r"(current_cr3));
	if ((current_cr3 & PAGE_MASK) == (g_user.pml4_phys & PAGE_MASK))
		__asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");

	return 0;
}

int vmm_user_translate(uint64_t virt, uint64_t *phys_out, uint64_t *pte_out)
{
	if (!g_user_valid || !canonical_user(virt)) return -1;

	uint64_t *pml4 = table(g_user.pml4_phys);
	if (!pml4) return -1;
	uint64_t e4 = pml4[idx4(virt)];
	if (!(e4 & PTE_P) || (e4 & PTE_PS)) return -1;

	uint64_t *pdpt = table(e4 & PTE_ADDR_MASK);
	if (!pdpt) return -1;
	uint64_t e3 = pdpt[idx3(virt)];
	if (!(e3 & PTE_P) || (e3 & PTE_PS)) return -1;

	uint64_t *pd = table(e3 & PTE_ADDR_MASK);
	if (!pd) return -1;
	uint64_t e2 = pd[idx2(virt)];
	if (!(e2 & PTE_P) || (e2 & PTE_PS)) return -1;

	uint64_t *pt = table(e2 & PTE_ADDR_MASK);
	if (!pt) return -1;
	uint64_t pte = pt[idx1(virt)];
	if (!(pte & PTE_P)) return -1;

	if (phys_out) *phys_out = (pte & PTE_ADDR_MASK) | (virt & 0xfff);
	if (pte_out) *pte_out = pte;
	return 0;
}

int vmm_user_debug_walk(uint64_t virt, uint64_t *out)
{
	if (!out || !g_user_valid || !canonical_user(virt) || (virt & (PAGE_SIZE - 1)))
		return -1;

	uint64_t *pml4 = table(g_user.pml4_phys);
	if (!pml4) return -1;
	uint64_t e4 = pml4[idx4(virt)];
	if (!(e4 & PTE_P) || (e4 & PTE_PS)) return -1;

	uint64_t *pdpt = table(e4 & PTE_ADDR_MASK);
	if (!pdpt) return -1;
	uint64_t e3 = pdpt[idx3(virt)];
	if (!(e3 & PTE_P) || (e3 & PTE_PS)) return -1;

	uint64_t *pd = table(e3 & PTE_ADDR_MASK);
	if (!pd) return -1;
	uint64_t e2 = pd[idx2(virt)];
	if (!(e2 & PTE_P) || (e2 & PTE_PS)) return -1;

	uint64_t *pt = table(e2 & PTE_ADDR_MASK);
	if (!pt) return -1;
	uint64_t e1 = pt[idx1(virt)];
	if (!(e1 & PTE_P)) return -1;

	*out = (e4 & 0xffULL)
		 | ((e3 & 0xffULL) << 8)
		 | ((e2 & 0xffULL) << 16)
		 | ((e1 & 0xffULL) << 24);
	return 0;
}

int vmm_user_range_ok(uint64_t virt, uint64_t len, int write)
{
	if (!g_user_valid || !len || !canonical_user(virt) || len > USER_TOP - virt)
		return 0;

	uint64_t first = virt & PAGE_MASK;
	uint64_t last = (virt + len - 1) & PAGE_MASK;

	for (uint64_t va = first;; va += PAGE_SIZE) {
		uint64_t phys, pte;
		if (vmm_user_translate(va, &phys, &pte)) return 0;
		if (!(pte & PTE_U) || (write && !(pte & PTE_W))) return 0;
		if (va == last) break;
		if (va > USER_TOP - PAGE_SIZE) return 0;
	}
	return 1;
}

int vmm_debug_cr3_va(uint64_t cr3_phys, uint64_t va, const char *tag)
{
	(void)cr3_phys; (void)va; (void)tag;
	return 0;
}

void vmm_user_switch(void)
{
	if (!g_user_valid) return;
	uint64_t cr3 = g_user.pml4_phys;
	__asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

uint64_t vmm_current_user_pml4(void)
{
	return g_user_valid ? g_user.pml4_phys : 0;
}

int vmm_user_unmap(uint64_t virt, uint64_t *phys_out)
{
	if (!g_user_valid || !canonical_user(virt) || (virt & (PAGE_SIZE-1))) return -1;
	uint64_t *pml4=table(g_user.pml4_phys); if(!pml4)return -1;
	uint64_t e4=pml4[idx4(virt)]; if(!(e4&PTE_P)||e4&PTE_PS)return -1;
	uint64_t *pdpt=table(e4&PTE_ADDR_MASK); if(!pdpt)return -1;
	uint64_t e3=pdpt[idx3(virt)]; if(!(e3&PTE_P)||e3&PTE_PS)return -1;
	uint64_t *pd=table(e3&PTE_ADDR_MASK); if(!pd)return -1;
	uint64_t e2=pd[idx2(virt)]; if(!(e2&PTE_P)||e2&PTE_PS)return -1;
	uint64_t *pt=table(e2&PTE_ADDR_MASK); if(!pt)return -1;
	uint64_t *leaf=&pt[idx1(virt)]; uint64_t e=*leaf; if(!(e&PTE_P))return -1;
	if(phys_out)*phys_out=e&PTE_ADDR_MASK; *leaf=0; __asm__ volatile("invlpg (%0)"::"r"(virt):"memory"); return 0;
}

int vmm_user_protect(uint64_t virt, uint64_t flags)
{
	if (!g_user_valid || !canonical_user(virt) || (virt & (PAGE_SIZE-1))) return -1;
	uint64_t phys,pte; if(vmm_user_translate(virt,&phys,&pte))return -1;
	uint64_t *pml4=table(g_user.pml4_phys); if(!pml4)return -1;
	uint64_t e4=pml4[idx4(virt)]; uint64_t *pdpt=table(e4&PTE_ADDR_MASK); if(!pdpt)return -1;
	uint64_t e3=pdpt[idx3(virt)]; uint64_t *pd=table(e3&PTE_ADDR_MASK); if(!pd)return -1;
	uint64_t e2=pd[idx2(virt)]; uint64_t *pt=table(e2&PTE_ADDR_MASK); if(!pt)return -1;
	uint64_t *leaf=&pt[idx1(virt)]; *leaf=(pte&PTE_ADDR_MASK)|PTE_P|PTE_U|(flags&(PTE_W|PTE_NX)); __asm__ volatile("invlpg (%0)"::"r"(virt):"memory"); return 0;
}
