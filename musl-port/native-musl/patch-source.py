#!/usr/bin/env python3
"""Apply and verify the small YabroOS-specific musl dynamic-loader patch."""
import re, sys
from pathlib import Path
p = Path(sys.argv[1])
s = p.read_text()

func = re.search(r"static inline struct symdef find_sym2\(.*?\n\}", s, re.S)
if not func:
    raise SystemExit("ERROR: cannot locate find_sym2")

new = """static inline struct symdef find_sym2(struct dso *dso, const char *s, int need_def, int use_deps)
{
\tuint32_t h = 0, gh = gnu_hash(s), gho = gh / (8*sizeof(size_t)), *ght;
\tsize_t ghm = 1ul << gh % (8*sizeof(size_t));
\tstruct symdef def = {0};
\tstruct dso **deps = use_deps ? dso->deps : 0;
\tfor (; dso; dso = use_deps ? *deps++ : dso->next) {
\t\tSym *sym = 0;
\t\tif ((ght = dso->ghashtab)) {
\t\t\tsym = gnu_lookup_filtered(gh, ght, dso, s, gho, ghm);
\t\t\tif (!sym) sym = gnu_lookup(gh, ght, dso, s);
\t\t} else {
\t\t\tif (!h) h = sysv_hash(s);
\t\t\tsym = sysv_lookup(s, h, dso);
\t\t}
\t\t/* YABROOS direct symtab fallback */
\t\tif (!sym && dso->syms && dso->strings) {
\t\t\tuintptr_t a = (uintptr_t)dso->syms;
\t\t\tuintptr_t z = (uintptr_t)dso->strings;
\t\t\tif (z > a) {
\t\t\t\tsize_t n = (z-a) / sizeof(Sym);
\t\t\t\tif (n <= (1u<<20)) for (size_t i=0; i<n; i++) {
\t\t\t\t\tSym *cand = dso->syms + i;
\t\t\t\t\tif (!cand->st_name) continue;
\t\t\t\t\tif (dso->versym && dso->versym[i] < 0) continue;
\t\t\t\t\tif ((uintptr_t)(dso->strings + cand->st_name) >= z) continue;
\t\t\t\t\tif (strcmp(s, dso->strings + cand->st_name)) continue;
\t\t\t\t\tif (!(1<<(cand->st_info&0xf) & OK_TYPES)) continue;
\t\t\t\t\tif (!(1<<(cand->st_info>>4) & OK_BINDS)) continue;
\t\t\t\t\tsym = cand;
\t\t\t\t\tbreak;
\t\t\t\t}
\t\t\t}
\t\t}
\t\tif (!sym) continue;
\t\tif (!sym->st_shndx)
\t\t\tif (need_def || (sym->st_info&0xf) == STT_TLS || ARCH_SYM_REJECT_UND(sym)) continue;
\t\tif (!sym->st_value)
\t\t\tif ((sym->st_info&0xf) != STT_TLS) continue;
\t\tif (!(1<<(sym->st_info&0xf) & OK_TYPES)) continue;
\t\tif (!(1<<(sym->st_info>>4) & OK_BINDS)) continue;
\t\tdef.sym = sym;
\t\tdef.dso = dso;
\t\tbreak;
\t}
\treturn def;
}
"""
s = s[:func.start()] + new + s[func.end():]
checks = [
    'static inline struct symdef find_sym2',
    'dso = use_deps ? *deps++ : dso->next',
    'if (!sym) sym = gnu_lookup(gh, ght, dso, s);',
    'YABROOS direct symtab fallback',
]
missing=[x for x in checks if x not in s]
if missing: raise SystemExit('ERROR: incomplete dynlink patch: '+repr(missing))
old_identity = """
\tfor (p=head->next; p; p=p->next) {
\t\tif (p->dev == st.st_dev && p->ino == st.st_ino) {
\t\t\t/* If this library was previously loaded with a
\t\t\t * pathname but a search found the same inode,
\t\t\t * setup its shortname so it can be found by name. */
\t\t\tif (!p->shortname && pathname != name)
\t\t\t\tp->shortname = strrchr(p->name, '/')+1;
\t\t\tclose(fd);
\t\t\treturn p;
\t\t}
\t}
"""
new_identity = """
	/* YABROOS: the VFS inode identity is not reliable enough for DSO dedup.
	 * Different files can report the same (dev, ino). Since pathname is the
	 * canonical path actually opened below, deduplicate only exact paths. */
	for (p=head->next; p; p=p->next) {
		if (!strcmp(p->name, pathname)) {
			if (!p->shortname && pathname != name)
				p->shortname = strrchr(p->name, '/')+1;
			close(fd);
			return p;
		}
	}
"""
if old_identity not in s:
    raise SystemExit("ERROR: cannot locate load_library inode dedup block")
s = s.replace(old_identity, new_identity, 1)

if 'YABROOS: the VFS inode identity is not reliable enough for DSO dedup.' not in s:
    raise SystemExit('ERROR: incomplete VFS path-identity patch')

p.write_text(s)
