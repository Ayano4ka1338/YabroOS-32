//Physical memory manager - (c) Ayano4ka1338, 2026

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PAGE_SIZE 0x1000ULL
#define MAX_REGIONS 64
#define MAX_RESERVED_PAGES 65536
#define MAX_PAGE_TABLES 65536
#define PAGE_TABLE_HASH_SIZE 131072
#define LIMINE_MEMMAP_USABLE 0
#define PMM_MIN_PHYS 0x01000000ULL
#define PTE_P 0x001ULL
#define PTE_W 0x002ULL
#define PTE_PS (1ULL << 7)
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define PMM_TRACK_LIMIT (1ULL << 32)
#define PMM_TRACK_PAGES (PMM_TRACK_LIMIT / PAGE_SIZE)
#define PMM_TRACK_WORDS ((PMM_TRACK_PAGES + 63) / 64)

struct limine_memmap_entry { uint64_t base, length, type; } __attribute__((packed));
struct pmm_region { uint64_t base, end, next, free_head; };

static struct pmm_region regions[MAX_REGIONS];
static uint64_t reserved_pages[MAX_RESERVED_PAGES];

static uint64_t seen_page_tables[PAGE_TABLE_HASH_SIZE];
static size_t seen_page_table_count;
static size_t region_count, region_index, reserved_count;
static uint64_t g_hhdm, g_free_pages;
static uint64_t g_kernel_base, g_kernel_end;
static uint64_t g_total_pages;

static uint64_t page_state[PMM_TRACK_WORDS];

static uint64_t reserved_state[PMM_TRACK_WORDS];

static uint16_t page_refs[PMM_TRACK_PAGES];

void *pmm_phys_to_virt(uint64_t phys);
void pmm_free_page(uint64_t phys);
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

static inline bool tracked(uint64_t phys) {
	return phys < PMM_TRACK_LIMIT;
}
static inline bool page_is_allocated(uint64_t phys) {
	if (!tracked(phys)) return true;
	uint64_t n = phys / PAGE_SIZE;
	return (page_state[n >> 6] >> (n & 63)) & 1ULL;
}
static inline void page_set_allocated(uint64_t phys, bool allocated) {
	if (!tracked(phys)) return;
	uint64_t n = phys / PAGE_SIZE;
	uint64_t mask = 1ULL << (n & 63);
	if (allocated) page_state[n >> 6] |= mask;
	else page_state[n >> 6] &= ~mask;
}
static inline bool page_is_reserved_fast(uint64_t phys) {
	if (!tracked(phys)) return false;
	uint64_t n = phys / PAGE_SIZE;
	return (reserved_state[n >> 6] >> (n & 63)) & 1ULL;
}
static inline void page_set_reserved(uint64_t phys) {
	if (!tracked(phys)) return;
	uint64_t n = phys / PAGE_SIZE;
	reserved_state[n >> 6] |= 1ULL << (n & 63);
}

static bool phys_in_usable_region(uint64_t phys) {
	for (size_t r = 0; r < region_count; ++r)
		if (phys >= regions[r].base && phys < regions[r].end) return true;
	return false;
}

static bool reserve_page(uint64_t phys) {
	phys = page_of(phys);
	if (!phys || phys >= (1ULL << 52)) return false;
	if (page_is_reserved_fast(phys)) return false;
	if (reserved_count >= MAX_RESERVED_PAGES) return false;
	reserved_pages[reserved_count++] = phys;
	page_set_reserved(phys);

	if (g_ready && phys_in_usable_region(phys) && !page_is_allocated(phys)) {
		page_set_allocated(phys, true);
		if (g_free_pages) --g_free_pages;
	}
	return true;
}

void pmm_reserve_range(uint64_t base, uint64_t length) {
	if (!length) return;
	uint64_t start = page_of(base);
	uint64_t end = align_up(base + length);
	if (end < base) return;
	for (uint64_t p = start; p < end; p += PAGE_SIZE) {
		reserve_page(p);
		if (p > UINT64_MAX - PAGE_SIZE) break;
	}
}

static bool is_reserved(uint64_t phys) {
	return page_is_reserved_fast(page_of(phys));
}

static bool phys_is_described(uint64_t phys,
                              struct limine_memmap_entry **entries,
                              uint64_t count) {
	for (uint64_t i = 0; i < count; ++i) {
		struct limine_memmap_entry *e = entries ? entries[i] : NULL;
		if (!e || !e->length || e->base > UINT64_MAX - e->length) continue;
		if (phys >= e->base && phys - e->base < e->length) return true;
	}
	return false;
}

static bool page_table_seen(uint64_t phys) {
	if (seen_page_table_count >= MAX_PAGE_TABLES) return true;

	uint64_t key = phys >> 12;
	size_t slot = (size_t)((key * 11400714819323198485ULL) &
	                       (PAGE_TABLE_HASH_SIZE - 1));
	for (size_t probes = 0; probes < PAGE_TABLE_HASH_SIZE; ++probes) {
		uint64_t old = seen_page_tables[slot];
		if (old == phys) return true;
		if (old == 0) {
			seen_page_tables[slot] = phys;
			++seen_page_table_count;
			return false;
		}
		slot = (slot + 1) & (PAGE_TABLE_HASH_SIZE - 1);
	}
	return true;                                                              
}

static void reserve_page_table_tree(uint64_t table_phys, unsigned level,
                                    struct limine_memmap_entry **entries,
                                    uint64_t count) {
	table_phys = page_of(table_phys);
	if (!table_phys || table_phys > PTE_ADDR_MASK || level == 0) return;
	if (!phys_is_described(table_phys, entries, count)) return;
	if (page_table_seen(table_phys)) return;
	(void)reserve_page(table_phys);
	uint64_t *table = (uint64_t *)pmm_phys_to_virt(table_phys);
	if (!table) return;
	for (size_t i = 0; i < 512; ++i) {
		uint64_t e = table[i];
		if (!(e & PTE_P) || level == 1) continue;

		if ((level == 3 || level == 2) && (e & PTE_PS)) continue;
		uint64_t child = e & PTE_ADDR_MASK;
		if (child && phys_is_described(child, entries, count))
			reserve_page_table_tree(child, level - 1, entries, count);
	}
}

static void reserve_active_page_tables(struct limine_memmap_entry **entries,
                                       uint64_t count) {
	uint64_t cr3;
	__asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
	seen_page_table_count = 0;
	for (size_t i = 0; i < PAGE_TABLE_HASH_SIZE; ++i) seen_page_tables[i] = 0;
	reserve_page_table_tree(cr3, 4, entries, count);
}

void pmm_init(struct limine_memmap_entry **entries, uint64_t count, uint64_t hhdm, uint64_t kernel_base, uint64_t kernel_end) {
	region_count = region_index = reserved_count = 0;
	g_free_pages = 0;
	g_total_pages = 0;
	for (size_t i = 0; i < PMM_TRACK_WORDS; ++i) {
		page_state[i] = 0;
		reserved_state[i] = 0;
	}
	for (size_t i = 0; i < PMM_TRACK_PAGES; ++i) page_refs[i] = 0;
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
		/* The bitmap/refcount arrays only track physical memory below 4 GiB. */
		if (end > PMM_TRACK_LIMIT) end = PMM_TRACK_LIMIT;
		if (start >= end) continue;
		regions[region_count++] = (struct pmm_region){start, end, start, 0};
		g_free_pages += (end - start) / PAGE_SIZE;
		g_total_pages += (end - start) / PAGE_SIZE;
	}

	if (g_kernel_end > g_kernel_base && g_kernel_end - g_kernel_base < (1ULL << 40)) {
		for (uint64_t p = g_kernel_base; p < g_kernel_end; p += PAGE_SIZE)
			reserve_page(p);
	}

	reserve_active_page_tables(entries, count);

	for (size_t i = 0; i < reserved_count; ++i) {
		uint64_t rp = reserved_pages[i];
		if (rp < PMM_TRACK_LIMIT) page_set_allocated(rp, true);
		for (size_t r = 0; r < region_count; ++r) {
			if (rp >= regions[r].base && rp < regions[r].end) {
				if (g_free_pages) --g_free_pages;
				break;
			}
		}
	}

	g_ready = region_count != 0;
}

uint64_t pmm_alloc_page(void) {
	if (!g_ready || !g_hhdm) return 0;

	for (size_t i = 0; i < region_count; ++i) {
		struct pmm_region *r = &regions[i];
		uint64_t p = r->next;
		while (p < r->end) {
			r->next = p + PAGE_SIZE;
			if (p >= PMM_TRACK_LIMIT || p < PMM_MIN_PHYS || is_reserved(p)) {
				p += PAGE_SIZE;
				continue;
			}
			if (page_is_allocated(p)) {
				p += PAGE_SIZE;
				continue;
			}
			page_set_allocated(p, true);
			page_refs[p / PAGE_SIZE] = 1;
			if (g_free_pages) --g_free_pages;
			return p;
		}

		for (p = r->base; p < r->end; p += PAGE_SIZE) {
			if (p >= PMM_TRACK_LIMIT || p < PMM_MIN_PHYS || is_reserved(p)) continue;
			if (!page_is_allocated(p)) {
				page_set_allocated(p, true);
				page_refs[p / PAGE_SIZE] = 1;
				r->next = p + PAGE_SIZE;
				if (g_free_pages) --g_free_pages;
				return p;
			}
		}
	}
	return 0;
}

uint64_t pmm_alloc_zeroed(void) {
	uint64_t phys = pmm_alloc_page();
	if (!phys) return 0;
	volatile uint64_t *p = (volatile uint64_t *)pmm_phys_to_virt(phys);
	if (!p) { pmm_free_page(phys); return 0; }
	for (size_t i = 0; i < PAGE_SIZE / sizeof(uint64_t); ++i)
		p[i] = 0;
	return phys;
}

void pmm_free_page(uint64_t phys) {
	phys = page_of(phys);
	if (!g_ready || !phys || phys >= (1ULL << 52) || is_reserved(phys)) return;
	if (phys >= PMM_TRACK_LIMIT) {
		return;
	}

	for (size_t i = 0; i < region_count; ++i) {
		struct pmm_region *r = &regions[i];
		if (phys < r->base || phys >= r->end) continue;

		if (!page_is_allocated(phys)) {
			return;
		}
		uint64_t idx = phys / PAGE_SIZE;
		uint16_t refs = page_refs[idx];
		if (refs > 1) {
			page_refs[idx] = (uint16_t)(refs - 1);
			return;
		}
		page_refs[idx] = 0;
		page_set_allocated(phys, false);
		++g_free_pages;
		return;
	}
}
uint16_t pmm_page_refcount(uint64_t phys) {
	phys = page_of(phys);
	if (!g_ready || !phys || phys >= PMM_TRACK_LIMIT) return 0;
	return page_refs[phys / PAGE_SIZE];
}

int pmm_page_is_reserved(uint64_t phys) {
	return is_reserved(page_of(phys)) ? 1 : 0;
}

int pmm_retain_page(uint64_t phys) {
	phys = page_of(phys);
	if (!g_ready || !phys || phys >= PMM_TRACK_LIMIT || is_reserved(phys)) return -1;
	if (!page_is_allocated(phys)) return -1;
	uint64_t idx = phys / PAGE_SIZE;
	if (page_refs[idx] == UINT16_MAX) return -1;
	if (page_refs[idx] == 0) page_refs[idx] = 1;
	else ++page_refs[idx];
	return 0;
}

uint64_t pmm_free_count(void) { return g_free_pages; }
uint64_t pmm_total_count(void) { return g_total_pages; }
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
		return -1;
	}
	volatile uint64_t *p = (volatile uint64_t *)pmm_phys_to_virt(phys);
	if (!p) {
		return -1;
	}
	p[0] = 0x594142524f4f5348ULL;
	if (p[0] != 0x594142524f4f5348ULL) {
		return -1;
	}
	p[0] = 0;
	return 0;
}
