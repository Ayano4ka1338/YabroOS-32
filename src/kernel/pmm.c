//Physical memory manager - (c) Ayano4ka1338, 2026
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 0x1000ULL
#define MAX_REGIONS 64
#define MAX_RESERVED_PAGES 4096
#define LIMINE_MEMMAP_USABLE 0
#define PMM_MIN_PHYS 0x01000000ULL
#define PTE_P 0x001ULL
#define PTE_W 0x002ULL
#define PTE_PS (1ULL << 7)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

struct limine_memmap_entry { uint64_t base, length, type; } __attribute__((packed));
struct pmm_region { uint64_t base, end, next; };

static struct pmm_region regions[MAX_REGIONS];
static uint64_t reserved_pages[MAX_RESERVED_PAGES];
static size_t region_count, region_index, reserved_count;
static uint64_t g_hhdm, g_free_pages;
static uint64_t g_kernel_base, g_kernel_end;

void *pmm_phys_to_virt(uint64_t phys);
static bool g_ready;

static inline void debugc(char c) {
    __asm__ volatile("outb %0, %1" : : "a"(c), "dN"((uint16_t)0xE9));
}

static void debugcon_puts(const char *s) { while (*s) debugc(*s++); }

static void debug_hex(const char *s, uint64_t v) {
    debugcon_puts(s);
    for (int i = 15; i >= 0; --i) {
        uint8_t n = (uint8_t)((v >> (i * 4)) & 0xF);
        debugc(n < 10 ? (char)('0' + n) : (char)('a' + n - 10));
    }
    debugc('\n');
}

static inline uint64_t align_up(uint64_t x) { return (x + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }
static inline uint64_t align_down(uint64_t x) { return x & ~(PAGE_SIZE - 1); }
static inline uint64_t page_of(uint64_t x) { return x & ~(PAGE_SIZE - 1); }

static bool reserve_page(uint64_t phys) {
    phys = page_of(phys);
    if (!phys || phys >= (1ULL << 52)) return false;
    for (size_t i = 0; i < reserved_count; ++i)
        if (reserved_pages[i] == phys) return false;
    if (reserved_count >= MAX_RESERVED_PAGES) return false;
    reserved_pages[reserved_count++] = phys;
    return true;
}

static bool is_reserved(uint64_t phys) {
    phys = page_of(phys);
    for (size_t i = 0; i < reserved_count; ++i)
        if (reserved_pages[i] == phys) return true;
    return false;
}

static void reserve_active_page_tables(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    cr3 = page_of(cr3);
    if (!cr3) return;

    reserve_page(cr3);

    debug_hex("[PMM] active CR3=0x", cr3);
    debug_hex("[PMM] reserved active root=0x", 1);
}

void pmm_init(struct limine_memmap_entry **entries, uint64_t count, uint64_t hhdm, uint64_t kernel_base, uint64_t kernel_end) {
    region_count = region_index = reserved_count = 0;
    g_free_pages = 0;
    g_hhdm = hhdm;
    g_kernel_base = align_down(kernel_base);
    g_kernel_end = align_up(kernel_end);
    g_ready = false;
    if (!entries || !hhdm) return;

    for (uint64_t i = 0; i < count && region_count < MAX_REGIONS; ++i) {
        struct limine_memmap_entry *e = entries[i];
        if (!e || e->type != LIMINE_MEMMAP_USABLE || e->length < PAGE_SIZE) continue;
        uint64_t start = e->base;
        uint64_t end = e->base + e->length;
        if (end < e->base) continue;
        start = align_up(start);
        end = align_down(end);
        if (start < PMM_MIN_PHYS) start = PMM_MIN_PHYS;
        if (start >= end) continue;
        regions[region_count++] = (struct pmm_region){start, end, start};
        g_free_pages += (end - start) / PAGE_SIZE;
    }

    if (g_kernel_end > g_kernel_base && g_kernel_end - g_kernel_base < (1ULL << 40)) {
        for (uint64_t p = g_kernel_base; p < g_kernel_end; p += PAGE_SIZE)
            reserve_page(p);
    }

    reserve_active_page_tables();

    debug_hex("[PMM] kernel physical base=0x", g_kernel_base);
    debug_hex("[PMM] kernel physical end=0x", g_kernel_end);
    debug_hex("[PMM] minimum physical address=0x", PMM_MIN_PHYS);

    for (size_t i = 0; i < reserved_count; ++i) {
        uint64_t rp = reserved_pages[i];
        for (size_t r = 0; r < region_count; ++r) {
            if (rp >= regions[r].base && rp < regions[r].end) {
                if (g_free_pages) --g_free_pages;
                break;
            }
        }
    }

    g_ready = region_count != 0;
    debug_hex("[PMM] regions=0x", region_count);
    debug_hex("[PMM] free pages=0x", g_free_pages);
}

uint64_t pmm_alloc_page(void) {
    if (!g_ready || !g_hhdm) return 0;

    for (size_t i = 0; i < region_count; ++i) {
        struct pmm_region *r = &regions[i];
        while (r->next < r->end) {
            uint64_t phys = r->next;
            r->next += PAGE_SIZE;
            if (phys < PMM_MIN_PHYS || is_reserved(phys)) continue;
            if (phys >= (1ULL << 52)) continue;
            if (phys > UINT64_MAX - g_hhdm) continue;
            if (g_free_pages) --g_free_pages;
            debug_hex("[PMM] alloc=0x", phys);
            return phys;
        }
    }
    return 0;
}

uint64_t pmm_alloc_zeroed(void) {
    uint64_t phys = pmm_alloc_page();
    if (!phys) return 0;
    volatile uint64_t *p = (volatile uint64_t *)pmm_phys_to_virt(phys);
    if (!p) return 0;
    for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i)
        p[i] = 0;
    return phys;
}

void pmm_free_page(uint64_t phys) { (void)phys; }
uint64_t pmm_free_count(void) { return g_free_pages; }
uint64_t pmm_hhdm(void) { return g_hhdm; }
void *pmm_phys_to_virt(uint64_t phys) {
    if (!g_hhdm) return NULL;
    if (phys > UINT64_MAX - g_hhdm) return NULL;
    uint64_t va = g_hhdm + phys;

    if ((va >> 48) != 0xFFFFULL) return NULL;
    return (void *)va;
}

int pmm_hhdm_selftest(void) {
    if (!g_ready || !g_hhdm) return -1;
    uint64_t phys = pmm_alloc_page();
    if (!phys) {
        debugcon_puts("[PMM] HHDM selftest: allocation failed\n");
        return -1;
    }
    volatile uint64_t *p = (volatile uint64_t *)pmm_phys_to_virt(phys);
    if (!p) {
        debugcon_puts("[PMM] HHDM selftest: translation failed\n");
        return -1;
    }
    debug_hex("[PMM] HHDM selftest phys=0x", phys);
    debug_hex("[PMM] HHDM selftest va=0x", (uint64_t)p);
    p[0] = 0x594142524f4f5348ULL;
    if (p[0] != 0x594142524f4f5348ULL) {
        debugcon_puts("[PMM] HHDM selftest: readback failed\n");
        return -1;
    }
    p[0] = 0;
    debugcon_puts("[PMM] HHDM selftest: OK\n");
    return 0;
}
