//Core Rust kernel services. - (c) Ayano4ka1338, 2026

#![no_std]
#![no_main]

use core::panic::PanicInfo;
use core::arch::asm;
use core::ptr;
use core::slice;
use core::cmp;
use core::ffi::CStr;

extern "C" {
	fn setup_interrupts();
	fn get_key() -> u8;
	fn get_rtc_time(buf: *mut u8, bufsize: usize);
	static shift_pressed: u8;
	static caps_lock: u8;
	fn syscall_init();
	fn init_fpu_sse();
	fn enter_usermode(rip: u64, rsp: u64, cr3: u64);
	fn exec_enter_usermode(rip: u64, rsp: u64, cr3: u64);
	fn load_elf(data: *const u8, size: usize, exec_path: *const u8, stack_top: *mut u64) -> u64;
	fn load_elf_with_args(data: *const u8, size: usize, exec_path: *const u8,
						  argv: *const *const u8, argc: usize,
						  envp: *const *const u8, envc: usize, stack_top: *mut u64) -> u64;
	fn vmm_user_cr3() -> u64;
	fn vmm_user_map(virt: u64, phys: u64, flags: u64) -> i32;
	fn vmm_user_unmap(virt: u64, phys_out: *mut u64) -> i32;
	fn pmm_init(entries: *mut *mut LimineMemmapEntry, count: u64, hhdm: u64, kernel_base: u64, kernel_end: u64);
	fn pmm_reserve_range(base: u64, length: u64);
	fn pmm_hhdm_selftest() -> i32;
	fn vfs_list_dir(path: *const u8, out: *mut u8, cap: usize) -> usize;
	fn vfs_read_file(path: *const u8, out: *mut u8, cap: usize) -> usize;
	fn vfs_import_boot_file(name: *const u8, size: u64) -> i32;
	fn vfs_mkdir(path: *const u8, mode: u32) -> *mut u8;
}

#[repr(C)]
pub struct LimineFramebufferResponse {
	revision: u64,
	framebuffer_count: u64,
	framebuffers: *mut *mut LimineFramebuffer,
}

#[repr(C)]
pub struct LimineFramebuffer {
	address: *mut u8,
	width: u64,
	height: u64,
	pitch: u64,
	bpp: u16,
	memory_model: u8,
	red_mask_size: u8,
	red_mask_shift: u8,
	green_mask_size: u8,
	green_mask_shift: u8,
	blue_mask_size: u8,
	blue_mask_shift: u8,
	unused: [u8; 7],
	edid_size: u64,
	edid: *mut u8,
	mode_count: u64,
	modes: *mut *mut u8,
}

#[repr(C)]
pub struct LimineFramebufferRequest {
	id: [u64; 4],
	revision: u64,
	response: *mut LimineFramebufferResponse,
}

#[repr(C)]
pub struct LimineFile {
	revision: u64,
	address: *mut u8,
	size: u64,
	path: *mut u8,
	cmdline: *mut u8,
	media_type: u32,
	unused: u32,
	tftp_ip: u32,
	tftp_port: u32,
	partition_index: u32,
	mbr_disk_id: u32,
	gpt_disk_uuid: [u8; 16],
	gpt_part_uuid: [u8; 16],
	part_uuid: [u8; 16],
}

#[repr(C)]
pub struct LimineModuleResponse {
	revision: u64,
	module_count: u64,
	modules: *mut *mut LimineFile,
}

#[repr(C)]
pub struct LimineModuleRequest {
	id: [u64; 4],
	revision: u64,
	response: *mut LimineModuleResponse,
}

#[used]
#[link_section = ".limine_requests"]
pub static BASE_REVISION: [u64; 3] = [0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 1];

#[used]
#[link_section = ".limine_requests"]
pub static mut FRAMEBUFFER_REQUEST: LimineFramebufferRequest = LimineFramebufferRequest {
	id: [0xc7b1dd30df4c8b88, 0x0a82e883a194f07b, 0x9d5827dcd881dd75, 0xa3148604f6fab11b],
	revision: 0,
	response: ptr::null_mut(),
};

#[used]
#[link_section = ".limine_requests"]
pub static mut MODULE_REQUEST: LimineModuleRequest = LimineModuleRequest {
	id: [0xc7b1dd30df4c8b88, 0x0a82e883a194f07b, 0x3e7e279702be32af, 0xca1c4f3bd1280cee],
	revision: 0,
	response: ptr::null_mut(),
};

#[repr(C)]
pub struct LimineKernelAddressResponse {
	revision: u64,
	physical_base: u64,
	virtual_base: u64,
}

#[repr(C)]
pub struct LimineKernelAddressRequest {
	id: [u64; 4],
	revision: u64,
	response: *mut LimineKernelAddressResponse,
}

#[used]
#[link_section = ".limine_requests"]
pub static mut KERNEL_ADDRESS_REQUEST: LimineKernelAddressRequest = LimineKernelAddressRequest {
	id: [0xc7b1dd30df4c8b88, 0x0a82e883a194f07b, 0x71ba76863cc55f63, 0xb2644a48c516a487],
	revision: 0,
	response: ptr::null_mut(),
};

#[repr(C)]
pub struct LimineHhdmResponse {
	revision: u64,
	offset: u64,
}

#[repr(C)]
pub struct LimineHhdmRequest {
	id: [u64; 4],
	revision: u64,
	response: *mut LimineHhdmResponse,
}

#[repr(C)]
pub struct LimineMemmapEntry {
	base: u64,
	length: u64,
	type_: u64,
}

#[repr(C)]
pub struct LimineMemmapResponse {
	revision: u64,
	entry_count: u64,
	entries: *mut *mut LimineMemmapEntry,
}

#[repr(C)]
pub struct LimineMemmapRequest {
	id: [u64; 4],
	revision: u64,
	response: *mut LimineMemmapResponse,
}

#[used]
#[link_section = ".limine_requests"]
pub static mut HHDM_REQUEST: LimineHhdmRequest = LimineHhdmRequest {
	id: [0xc7b1dd30df4c8b88, 0x0a82e883a194f07b, 0x48dcf1cb8ad2b852, 0x63984e959a98244b],
	revision: 0,
	response: ptr::null_mut(),
};

#[used]
#[link_section = ".limine_requests"]
pub static mut MEMMAP_REQUEST: LimineMemmapRequest = LimineMemmapRequest {
	id: [0xc7b1dd30df4c8b88, 0x0a82e883a194f07b, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62],
	revision: 0,
	response: ptr::null_mut(),
};

#[no_mangle]
pub extern "C" fn limine_hhdm_offset() -> u64 {
	unsafe {
		if HHDM_REQUEST.response.is_null() { 0 } else { (*HHDM_REQUEST.response).offset }
	}
}

#[inline(always)]
unsafe fn debugcon_byte(b: u8) {
	asm!("out dx, al", in("dx") 0xE9u16, in("al") b, options(nomem, nostack, preserves_flags));
}

unsafe fn debugcon_str(s: &[u8]) {
	for &b in s { debugcon_byte(b); }
}

unsafe fn debugcon_hex(mut v: u64) {
	const HEX: &[u8; 16] = b"0123456789abcdef";
	let mut buf = [b'0'; 16];
	for i in (0..16).rev() {
		buf[i] = HEX[(v & 0xf) as usize];
		v >>= 4;
	}
	debugcon_str(&buf);
}



const CELL_W: usize = 8;
const CELL_H: usize = 16;
const GLYPH_BYTES: usize = 16;
const FONT: [u8; 1536] = [
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

	0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x10,0x00,0x00,0x00,0x00,

	0x00,0x24,0x24,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

	0x00,0x00,0x24,0x24,0x24,0x7E,0x24,0x24,0x7E,0x24,0x24,0x24,0x00,0x00,0x00,0x00,

	0x00,0x10,0x10,0x7C,0x92,0x90,0x90,0x7C,0x12,0x12,0x92,0x7C,0x10,0x10,0x00,0x00,

	0x00,0x00,0x64,0x94,0x68,0x08,0x10,0x10,0x20,0x2C,0x52,0x4C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x18,0x24,0x24,0x18,0x30,0x4A,0x44,0x44,0x44,0x3A,0x00,0x00,0x00,0x00,

	0x00,0x10,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

	0x00,0x00,0x08,0x10,0x20,0x20,0x20,0x20,0x20,0x20,0x10,0x08,0x00,0x00,0x00,0x00,

	0x00,0x00,0x20,0x10,0x08,0x08,0x08,0x08,0x08,0x08,0x10,0x20,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x20,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x00,0x00,

	0x00,0x00,0x04,0x04,0x08,0x08,0x10,0x10,0x20,0x20,0x40,0x40,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x46,0x4A,0x52,0x62,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x08,0x18,0x28,0x08,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x02,0x04,0x08,0x10,0x20,0x40,0x7E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x02,0x1C,0x02,0x02,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x02,0x06,0x0A,0x12,0x22,0x42,0x7E,0x02,0x02,0x02,0x00,0x00,0x00,0x00,

	0x00,0x00,0x7E,0x40,0x40,0x40,0x7C,0x02,0x02,0x02,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x1C,0x20,0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x7E,0x02,0x02,0x04,0x04,0x08,0x08,0x10,0x10,0x10,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x3E,0x02,0x02,0x04,0x38,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x10,0x10,0x00,0x00,0x00,0x10,0x10,0x20,0x00,0x00,0x00,

	0x00,0x00,0x00,0x04,0x08,0x10,0x20,0x40,0x20,0x10,0x08,0x04,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x40,0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x40,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x42,0x04,0x08,0x08,0x00,0x08,0x08,0x00,0x00,0x00,0x00,

	0x00,0x00,0x7C,0x82,0x9E,0xA2,0xA2,0xA2,0xA6,0x9A,0x80,0x7E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x7C,0x42,0x42,0x42,0x7C,0x42,0x42,0x42,0x42,0x7C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x40,0x40,0x40,0x40,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x78,0x44,0x42,0x42,0x42,0x42,0x42,0x42,0x44,0x78,0x00,0x00,0x00,0x00,

	0x00,0x00,0x7E,0x40,0x40,0x40,0x78,0x40,0x40,0x40,0x40,0x7E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x7E,0x40,0x40,0x40,0x78,0x40,0x40,0x40,0x40,0x40,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x40,0x40,0x4E,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x42,0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x38,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,

	0x00,0x00,0x0E,0x04,0x04,0x04,0x04,0x04,0x04,0x44,0x44,0x38,0x00,0x00,0x00,0x00,

	0x00,0x00,0x42,0x44,0x48,0x50,0x60,0x60,0x50,0x48,0x44,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x82,0xC6,0xAA,0x92,0x82,0x82,0x82,0x82,0x82,0x82,0x00,0x00,0x00,0x00,

	0x00,0x00,0x42,0x42,0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x7C,0x40,0x40,0x40,0x40,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x4A,0x3C,0x02,0x00,0x00,0x00,

	0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x7C,0x50,0x48,0x44,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x3C,0x42,0x40,0x40,0x3C,0x02,0x02,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0xFE,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00,

	0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x24,0x24,0x24,0x18,0x18,0x00,0x00,0x00,0x00,

	0x00,0x00,0x82,0x82,0x82,0x82,0x82,0x82,0x92,0xAA,0xC6,0x82,0x00,0x00,0x00,0x00,

	0x00,0x00,0x42,0x42,0x24,0x24,0x18,0x18,0x24,0x24,0x42,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x82,0x82,0x44,0x44,0x28,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00,

	0x00,0x00,0x7E,0x02,0x02,0x04,0x08,0x10,0x20,0x40,0x40,0x7E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x38,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x38,0x00,0x00,0x00,0x00,

	0x00,0x00,0x40,0x40,0x20,0x20,0x10,0x10,0x08,0x08,0x04,0x04,0x00,0x00,0x00,0x00,

	0x00,0x00,0x38,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x38,0x00,0x00,0x00,0x00,

	0x00,0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,

	0x10,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x3C,0x02,0x3E,0x42,0x42,0x42,0x3E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x40,0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x42,0x7C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x3C,0x42,0x40,0x40,0x40,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x02,0x02,0x02,0x3E,0x42,0x42,0x42,0x42,0x42,0x3E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x3C,0x42,0x42,0x7E,0x40,0x40,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x0E,0x10,0x10,0x7C,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x3E,0x42,0x42,0x42,0x42,0x42,0x3E,0x02,0x02,0x3C,0x00,

	0x00,0x00,0x40,0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x10,0x10,0x00,0x30,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,

	0x00,0x00,0x04,0x04,0x00,0x0C,0x04,0x04,0x04,0x04,0x04,0x04,0x44,0x44,0x38,0x00,

	0x00,0x00,0x40,0x40,0x40,0x42,0x44,0x48,0x70,0x48,0x44,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x30,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x38,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0xFC,0x92,0x92,0x92,0x92,0x92,0x92,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x42,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x42,0x7C,0x40,0x40,0x40,0x00,

	0x00,0x00,0x00,0x00,0x00,0x3E,0x42,0x42,0x42,0x42,0x42,0x3E,0x02,0x02,0x02,0x00,

	0x00,0x00,0x00,0x00,0x00,0x5E,0x60,0x40,0x40,0x40,0x40,0x40,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x3E,0x40,0x40,0x3C,0x02,0x02,0x7C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x10,0x10,0x10,0x7C,0x10,0x10,0x10,0x10,0x10,0x0E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x3E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x24,0x24,0x18,0x18,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x82,0x82,0x92,0x92,0x92,0x92,0x7C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x42,0x42,0x24,0x18,0x24,0x42,0x42,0x00,0x00,0x00,0x00,

	0x00,0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x3E,0x02,0x02,0x3C,0x00,

	0x00,0x00,0x00,0x00,0x00,0x7E,0x04,0x08,0x10,0x20,0x40,0x7E,0x00,0x00,0x00,0x00,

	0x00,0x00,0x0C,0x10,0x10,0x10,0x20,0x10,0x10,0x10,0x10,0x0C,0x00,0x00,0x00,0x00,

	0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,0x00,

	0x00,0x00,0x30,0x08,0x08,0x08,0x04,0x08,0x08,0x08,0x08,0x30,0x00,0x00,0x00,0x00,

	0x00,0x62,0x92,0x8C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

	0x08,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,

];

static mut C_CONSOLE_FB: *const LimineFramebuffer = ptr::null();
static mut GFX_FB_PHYS: u64 = 0;
static mut GFX_FB_SIZE: u64 = 0;
static mut GFX_FB_WIDTH: u64 = 0;
static mut GFX_FB_HEIGHT: u64 = 0;
static mut GFX_FB_PITCH: u64 = 0;
static mut GFX_FB_BPP: u64 = 0;

static mut C_CONSOLE_FB_VIEW: LimineFramebuffer = LimineFramebuffer {
	address: ptr::null_mut(),
	width: 0,
	height: 0,
	pitch: 0,
	bpp: 0,
	memory_model: 0,
	red_mask_size: 0,
	red_mask_shift: 0,
	green_mask_size: 0,
	green_mask_shift: 0,
	blue_mask_size: 0,
	blue_mask_shift: 0,
	unused: [0; 7],
	edid_size: 0,
	edid: ptr::null_mut(),
	mode_count: 0,
	modes: ptr::null_mut(),
};
static mut C_CONSOLE_COL: usize = 0;
static mut C_CONSOLE_ROW: usize = 0;

extern "C" {
	fn graphics_copy_to_user(dst_va: u64, src: *const u8, len: u64) -> i32;
}

extern "C" {
	fn vmm_user_translate(virt: u64, phys_out: *mut u64, pte_out: *mut u64) -> i32;
	fn vmm_user_debug_walk(virt: u64, out: *mut u64) -> i32;
	fn vmm_user_flush_tlb();
}

#[no_mangle]
pub extern "C" fn graphics_fb_info_kernel(out: *mut u64) -> i64 {
	unsafe {
		if out.is_null() || GFX_FB_SIZE == 0 {
			return -19;
		}
		*out.add(0) = GFX_FB_WIDTH;
		*out.add(1) = GFX_FB_HEIGHT;
		*out.add(2) = GFX_FB_PITCH;
		*out.add(3) = GFX_FB_BPP;
		*out.add(4) = GFX_FB_SIZE;
		*out.add(5) = GFX_FB_PHYS & 0xfff;
		0
	}
}

#[no_mangle]
pub extern "C" fn graphics_fb_phys() -> u64 {
	unsafe { GFX_FB_PHYS }
}

#[no_mangle]
pub extern "C" fn graphics_fb_size() -> u64 {
	unsafe { GFX_FB_SIZE }
}

#[no_mangle]
pub extern "C" fn graphics_fb_unmap(virt: u64, size: u64) -> i64 {
	const PAGE: u64 = 0x1000;
	unsafe {
		if virt == 0 || (virt & (PAGE - 1)) != 0 || size == 0 { return -22; }

		let phys_offset = GFX_FB_PHYS & (PAGE - 1);
		let fb_pages = (GFX_FB_SIZE + phys_offset + PAGE - 1) & !(PAGE - 1);
		let requested = (size + PAGE - 1) & !(PAGE - 1);

		if requested == 0 || requested > fb_pages { return -22; }

		for i in 0..(requested / PAGE) {
			let mut old_phys = 0u64;
			if vmm_user_unmap(virt + i * PAGE, &mut old_phys) != 0 { return -14; }
		}
		vmm_user_flush_tlb();
		0
	}
}

#[no_mangle]
pub extern "C" fn graphics_fb_map(virt: u64, off: u64, len: u64) -> i64 {
	const PAGE: u64 = 0x1000;
	const USER_TOP: u64 = 0x0000_8000_0000_0000;
	unsafe {
		if C_CONSOLE_FB.is_null() || GFX_FB_SIZE == 0 ||
		   virt == 0 || (virt & (PAGE - 1)) != 0 ||
		   (off & (PAGE - 1)) != 0 || len == 0 || off >= GFX_FB_SIZE {
			return -22;
		}

		let phys_offset = GFX_FB_PHYS & (PAGE - 1);
		let phys = (GFX_FB_PHYS & !(PAGE - 1)).saturating_add(off);
		let size = (len + phys_offset + PAGE - 1) & !(PAGE - 1);

		if off.checked_add(len).map_or(true, |end| end > GFX_FB_SIZE) {
			return -22;
		}
		if virt >= USER_TOP || size > USER_TOP - virt {
			return -22;
		}

		let pages = size / PAGE;
		for i in 0..pages {
			if vmm_user_map(virt + i * PAGE, phys + i * PAGE, 0x1f) != 0 {


				for j in 0..i {
					let mut old_phys = 0u64;
					let _ = vmm_user_unmap(virt + j * PAGE, &mut old_phys);
				}
				vmm_user_flush_tlb();
				return -12;
			}
		}

		vmm_user_flush_tlb();
		size as i64
	}
}

#[no_mangle]
pub extern "C" fn input_read_key() -> u64 {
	unsafe {
		let scancode = get_key();
		if scancode == 0 { 0 } else { scancode_to_ascii(scancode) as u64 }
	}
}

#[no_mangle]
pub extern "C" fn console_fb_set_cursor(col: u64, row: u64) {
	unsafe {
		C_CONSOLE_COL = col as usize;
		C_CONSOLE_ROW = row as usize;
	}
}

#[no_mangle]
pub extern "C" fn console_fb_get_cursor() -> u64 {
	unsafe { ((C_CONSOLE_ROW as u64) << 32) | (C_CONSOLE_COL as u64) }
}

#[no_mangle]
pub extern "C" fn console_fb_putc(ch: u8) {
	unsafe {
		if C_CONSOLE_FB.is_null() {
			return;
		}

		let fb = &*C_CONSOLE_FB;
		static mut FB_AUDIT_ONCE: bool = false;
		if !FB_AUDIT_ONCE {
			FB_AUDIT_ONCE = true;
			debugcon_str(b"[FB-AUDIT] addr=0x"); debugcon_hex(fb.address as u64);
			debugcon_str(b" width=0x"); debugcon_hex(fb.width);
			debugcon_str(b" height=0x"); debugcon_hex(fb.height);
			debugcon_str(b" pitch=0x"); debugcon_hex(fb.pitch);
			debugcon_str(b" bpp=0x"); debugcon_hex(fb.bpp as u64);
			debugcon_str(b"\n");
		}

		let fb_addr = fb.address as u64;
		if fb_addr < 0x0001_0000 {
			debugcon_str(b"[FB-AUDIT] INVALID-LOW-ADDRESS\n");
			return;
		}

		let max_cols = (fb.width as usize / CELL_W);
		let max_rows = fb.height as usize / CELL_H;
		if max_cols == 0 || max_rows == 0 {
			return;
		}

		match ch {
			b'\r' => {
				C_CONSOLE_COL = 0;
			}
			b'\n' => {
				C_CONSOLE_COL = 0;
				C_CONSOLE_ROW += 1;
			}
			8 => {
				if C_CONSOLE_COL > 0 {
					C_CONSOLE_COL -= 1;
					draw_char(fb, b' ', C_CONSOLE_COL, C_CONSOLE_ROW);
				}
			}
			_ => {
				draw_char(fb, ch, C_CONSOLE_COL, C_CONSOLE_ROW);
				C_CONSOLE_COL += 1;
				if C_CONSOLE_COL >= max_cols {
					C_CONSOLE_COL = 0;
					C_CONSOLE_ROW += 1;
				}
			}
		}

		if C_CONSOLE_ROW >= max_rows {
			scroll_screen(fb, max_cols, max_rows);
			C_CONSOLE_ROW = max_rows - 1;
		}
	}
}

unsafe fn draw_char(fb: &LimineFramebuffer, ch: u8, col: usize, row: usize) {
	let fb_ptr = fb.address as *mut u32;
	let pitch_pixels = (fb.pitch / 4) as usize;

	let start_x = col * CELL_W;
	let start_y = row * CELL_H;
	let font_idx = if ch >= 32 && ch <= 127 { ((ch - 32) as usize) * GLYPH_BYTES } else { 0 };
	for dy in 0..CELL_H {
		let font_row = FONT[font_idx + dy];
		for dx in 0..CELL_W {
			let color = if (font_row & (1 << (7 - dx))) != 0 {
				0x00_FF_FF_FF
			} else {
				0x00_00_00_00
			};
			let py = start_y + dy;
			let px = start_x + dx;
			let offset = py * pitch_pixels + px;
			fb_ptr.add(offset).write_volatile(color);
		}
	}
}

unsafe fn draw_cursor(fb: &LimineFramebuffer, col: usize, row: usize, show: bool) {
	let fb_ptr = fb.address as *mut u32;
	let pitch_pixels = (fb.pitch / 4) as usize;
	let start_x = col * CELL_W;

	let start_y = row * CELL_H + (CELL_H - 2);
	let color = if show { 0x00_FF_FF_FF } else { 0x00_00_00_00 };
	for y in 0..2 {
		for x in 0..CELL_W {
			let py = start_y + y;
			let px = start_x + x;
			let offset = py * pitch_pixels + px;
			fb_ptr.add(offset).write_volatile(color);
		}
	}
}

unsafe fn clear_framebuffer(fb: &LimineFramebuffer) {
	let pixels = (fb.width * fb.height) as usize;
	let fb_ptr = fb.address as *mut u32;
	for i in 0..pixels {
		fb_ptr.add(i).write_volatile(0x00_00_00_00);
	}
}

unsafe fn scroll_screen(fb: &LimineFramebuffer, max_cols: usize, max_rows: usize) {
	let fb_ptr = fb.address as *mut u32;
	let pitch_pixels = (fb.pitch / 4) as usize;
	let line_height = CELL_H;
	let line_width = max_cols * CELL_W;
	for row in 1..max_rows {
		let src_y = row * line_height;
		let dst_y = (row - 1) * line_height;
		for y in 0..line_height {
			let src_offset = (src_y + y) * pitch_pixels;
			let dst_offset = (dst_y + y) * pitch_pixels;
			for x in 0..line_width {
				let src_idx = src_offset + x;
				let dst_idx = dst_offset + x;
				fb_ptr.add(dst_idx).write_volatile(fb_ptr.add(src_idx).read_volatile());
			}
		}
	}
	let last_y = (max_rows - 1) * line_height;
	for y in 0..line_height {
		let offset = (last_y + y) * pitch_pixels;
		for x in 0..line_width {
			fb_ptr.add(offset + x).write_volatile(0);
		}
	}
}

fn scancode_to_ascii(scancode: u8) -> u8 {
	const NORMAL: [u8; 58] = [
		0, 27, b'1', b'2', b'3', b'4', b'5', b'6', b'7', b'8', b'9', b'0',
		b'-', b'=', 8, 9,
		b'q', b'w', b'e', b'r', b't', b'y', b'u', b'i', b'o', b'p',
		b'[', b']', 13, 0,
		b'a', b's', b'd', b'f', b'g', b'h', b'j', b'k', b'l', b';',
		b'\'', b'`', 0, b'\\',
		b'z', b'x', b'c', b'v', b'b', b'n', b'm', b',', b'.', b'/',
		0, b'*', 0, b' '
	];
	const SHIFTED: [u8; 58] = [
		0, 27, b'!', b'@', b'#', b'$', b'%', b'^', b'&', b'*', b'(', b')',
		b'_', b'+', 8, 9,
		b'Q', b'W', b'E', b'R', b'T', b'Y', b'U', b'I', b'O', b'P',
		b'{', b'}', 13, 0,
		b'A', b'S', b'D', b'F', b'G', b'H', b'J', b'K', b'L', b':',
		b'"', b'~', 0, b'|',
		b'Z', b'X', b'C', b'V', b'B', b'N', b'M', b'<', b'>', b'?',
		0, b'*', 0, b' '
	];
	if scancode as usize >= NORMAL.len() {
		return 0;
	}
	let shift = unsafe {
		let sh = shift_pressed;
		let cap = caps_lock;
		let is_alpha = match scancode {
			16..=25 | 30..=38 | 44..=50 => true,
			_ => false,
		};
		if is_alpha && cap == 1 { !sh } else { sh }
	};
	if shift != 0 { SHIFTED[scancode as usize] } else { NORMAL[scancode as usize] }
}

fn atoi(bytes: &[u8]) -> i64 {
	let mut sign = 1;
	let mut i = 0;
	if bytes.len() > 0 && bytes[0] == b'-' {
		sign = -1;
		i = 1;
	}
	let mut val: i64 = 0;
	while i < bytes.len() {
		let b = bytes[i];
		if b >= b'0' && b <= b'9' {
			val = val * 10 + (b - b'0') as i64;
		} else {
			break;
		}
		i += 1;
	}
	val * sign
}

fn trim_bytes(s: &[u8]) -> &[u8] {
	let mut start = 0;
	let mut end = s.len();
	while start < end && s[start] == b' ' { start += 1; }
	while end > start && s[end-1] == b' ' { end -= 1; }
	&s[start..end]
}

fn format_i64(mut n: i64, buf: &mut [u8; 21]) -> &[u8] {
	if n == 0 {
		buf[0] = b'0';
		return &buf[0..1];
	}
	let mut pos = 20;
	let mut negative = false;
	if n < 0 {
		negative = true;
		n = -n;
	}
	while n > 0 {
		pos -= 1;
		buf[pos] = b'0' + (n % 10) as u8;
		n /= 10;
	}
	if negative {
		pos -= 1;
		buf[pos] = b'-';
	}
	&buf[pos..]
}

#[repr(packed)]
struct BiosParameterBlock {
	jmp_boot: [u8; 3],
	oem_name: [u8; 8],
	bytes_per_sector: u16,
	sectors_per_cluster: u8,
	reserved_sectors: u16,
	num_fats: u8,
	root_entries: u16,
	total_sectors_16: u16,
	media_descriptor: u8,
	fat_size_16: u16,
	sectors_per_track: u16,
	num_heads: u16,
	hidden_sectors: u32,
	total_sectors_32: u32,
	fat_size_32: u32,
	ext_flags: u16,
	fs_version: u16,
	root_cluster: u32,
	fs_info: u16,
	backup_boot_sector: u16,
	reserved: [u8; 12],
	drive_number: u8,
	reserved1: u8,
	boot_signature: u8,
	volume_id: u32,
	volume_label: [u8; 11],
	fs_type: [u8; 8],
}

#[repr(packed)]
#[derive(Copy, Clone)]
struct DirectoryEntry {
	name: [u8; 11],
	attributes: u8,
	reserved: u8,
	creation_time_tenths: u8,
	creation_time: u16,
	creation_date: u16,
	last_access_date: u16,
	first_cluster_high: u16,
	last_write_time: u16,
	last_write_date: u16,
	first_cluster_low: u16,
	file_size: u32,
}

#[repr(packed)]
#[derive(Copy, Clone)]
struct LongNameEntry {
	sequence: u8,
	name1: [u16; 5],
	attributes: u8,
	entry_type: u8,
	checksum: u8,
	name2: [u16; 6],
	first_cluster: u16,
	name3: [u16; 2],
}

static mut FAT32_BASE: *const u8 = core::ptr::null();
static mut FAT32_SIZE: u64 = 0;
static mut FAT32_BYTES_PER_SECTOR: u16 = 0;
static mut FAT32_SECTORS_PER_CLUSTER: u8 = 0;
static mut FAT32_RESERVED_SECTORS: u16 = 0;
static mut FAT32_NUM_FATS: u8 = 0;
static mut FAT32_FAT_SIZE: u32 = 0;
static mut FAT32_ROOT_CLUSTER: u32 = 0;
static mut FAT32_DATA_START: u64 = 0;

unsafe fn fat32_init(base: *mut u8, size: u64) {
	FAT32_BASE = base as *const u8;
	FAT32_SIZE = size;
	let bpb = &*(base as *const BiosParameterBlock);
	FAT32_BYTES_PER_SECTOR = bpb.bytes_per_sector;
	FAT32_SECTORS_PER_CLUSTER = bpb.sectors_per_cluster;
	FAT32_RESERVED_SECTORS = bpb.reserved_sectors;
	FAT32_NUM_FATS = bpb.num_fats;
	FAT32_FAT_SIZE = bpb.fat_size_32;
	FAT32_ROOT_CLUSTER = bpb.root_cluster;

	let fat_start = bpb.reserved_sectors as u64 * bpb.bytes_per_sector as u64;
	let data_start = fat_start + (bpb.num_fats as u64 * bpb.fat_size_32 as u64 * bpb.bytes_per_sector as u64);
	FAT32_DATA_START = data_start;
	if !fat_cluster_valid(FAT32_ROOT_CLUSTER) { return; }
	import_fat_root_into_vfs();
}

unsafe fn fat_cluster_valid(cluster: u32) -> bool {
	if FAT32_BASE.is_null() || FAT32_BYTES_PER_SECTOR == 0 || FAT32_SECTORS_PER_CLUSTER == 0 {
		return false;
	}
	if cluster < 2 || cluster >= 0x0FFFFFF7 {
		return false;
	}
	let cluster_bytes = FAT32_SECTORS_PER_CLUSTER as u64 * FAT32_BYTES_PER_SECTOR as u64;
	if cluster_bytes == 0 { return false; }
	let data_bytes = FAT32_SIZE.saturating_sub(FAT32_DATA_START);
	let max_clusters = data_bytes / cluster_bytes;
	(cluster as u64 - 2) < max_clusters
}

unsafe fn cluster_to_ptr(cluster: u32) -> *const u8 {
	if !fat_cluster_valid(cluster) { return core::ptr::null(); }
	let offset = FAT32_DATA_START + (cluster - 2) as u64 * FAT32_SECTORS_PER_CLUSTER as u64 * FAT32_BYTES_PER_SECTOR as u64;
	if offset >= FAT32_SIZE { return core::ptr::null(); }
	FAT32_BASE.add(offset as usize)
}

unsafe fn next_cluster(cluster: u32) -> u32 {
	if FAT32_BASE.is_null() || FAT32_BYTES_PER_SECTOR == 0 { return 0x0FFFFFFF; }
	let fat_offset = (cluster as u64) * 4;
	let fat_sector_offset = FAT32_RESERVED_SECTORS as u64 * FAT32_BYTES_PER_SECTOR as u64;
	let fat_bytes = FAT32_FAT_SIZE as u64 * FAT32_BYTES_PER_SECTOR as u64;
	if fat_offset + 4 > fat_bytes || fat_sector_offset + fat_offset + 4 > FAT32_SIZE {
		return 0x0FFFFFFF;
	}
	let ptr = FAT32_BASE.add((fat_sector_offset + fat_offset) as usize) as *const u32;
	ptr.read_unaligned() & 0x0FFFFFFF
}

fn ascii_fold(c: u8) -> u8 {
	if c >= b'a' && c <= b'z' { c - 32 } else { c }
}

unsafe fn fat_short_name(entry: &DirectoryEntry, out: &mut [u8; 260]) -> usize {
	let mut pos=0usize;
	let mut base=8usize;
	while base>0 && entry.name[base-1]==b' ' { base-=1; }
	let mut ext=3usize;
	while ext>0 && entry.name[8+ext-1]==b' ' { ext-=1; }
	if base==0 { return 0; }
	for i in 0..base { out[pos]=entry.name[i]; pos+=1; }
	if ext>0 { out[pos]=b'.'; pos+=1; for i in 0..ext { out[pos]=entry.name[8+i]; pos+=1; } }
	out[pos]=0; pos
}

unsafe fn lfn_append_entry(dst: &mut [u8; 260], e: &LongNameEntry) {
	let seq=(e.sequence & 0x1f) as usize;
	if seq==0 { return; }
	if (e.sequence & 0x40) != 0 { dst[0]=0; for i in 1..dst.len(){dst[i]=0;} }
	let base=(seq-1)*13;
	if base>=dst.len()-1 { return; }
	let mut pos=base;

	let name1 = ptr::read_unaligned(ptr::addr_of!(e.name1));
	let name2 = ptr::read_unaligned(ptr::addr_of!(e.name2));
	let name3 = ptr::read_unaligned(ptr::addr_of!(e.name3));
	let groups=[&name1[..],&name2[..],&name3[..]];
	for g in groups {
		for &u in g {
			if u==0x0000 || u==0xffff {
				if pos<dst.len(){dst[pos]=0;}
				return;
			}
			if pos>=dst.len()-1 { return; }
			dst[pos]=if u<=0x7f { u as u8 } else { b'?' };
			pos+=1;
		}
	}
}

unsafe fn name_matches(query: &[u8], candidate: &[u8;260]) -> bool {
	let mut i=0usize;
	while i<query.len() && query[i]!=0 && candidate[i]!=0 {
		if ascii_fold(query[i]) != ascii_fold(candidate[i]) { return false; }
		i+=1;
	}
	(i==query.len() || query.get(i)==Some(&0)) && candidate[i]==0
}

unsafe fn find_file_in_directory(mut cur_cluster: u32, name: &[u8]) -> Option<DirectoryEntry> {
	let mut lfn=[0u8;260];
	loop {
		let dir_ptr=cluster_to_ptr(cur_cluster);
		let entries=slice::from_raw_parts(dir_ptr as *const DirectoryEntry,
			(FAT32_SECTORS_PER_CLUSTER as usize*FAT32_BYTES_PER_SECTOR as usize)/core::mem::size_of::<DirectoryEntry>());
		for entry in entries {
			let entry_ref=&*entry;
			if entry_ref.name[0]==0x00 { return None; }
			if entry_ref.name[0]==0xE5 { lfn[0]=0; continue; }
			if entry_ref.attributes==0x0f {
				let le=&*(entry_ref as *const DirectoryEntry as *const LongNameEntry);
				lfn_append_entry(&mut lfn,le);
				continue;
			}
			if (entry_ref.attributes&0x10)!=0 { lfn[0]=0; continue; }

			let mut short=[0u8;260];
			let short_len=fat_short_name(entry_ref,&mut short);
			let short_match=short_len>0 && name_matches(name,&short);
			let long_match=lfn[0]!=0 && name_matches(name,&lfn);
			if short_match || long_match { return Some(*entry_ref); }
			lfn[0]=0;
		}
		let next=next_cluster(cur_cluster);
		if next>=0x0FFFFFF8 { break; }
		cur_cluster=next;
	}
	None
}

unsafe fn fat_name_from_entry(entry: &DirectoryEntry, out: &mut [u8;260]) -> usize {
	let mut lfn=[0u8;260];
	let _ = &mut lfn;
	let short_len=fat_short_name(entry,out);
	short_len
}

unsafe fn find_entry_in_directory(mut cur_cluster: u32, name: &[u8]) -> Option<DirectoryEntry> {
	let mut lfn=[0u8;260];
	loop {
		let dir_ptr=cluster_to_ptr(cur_cluster);
		let entries=slice::from_raw_parts(dir_ptr as *const DirectoryEntry,
			(FAT32_SECTORS_PER_CLUSTER as usize*FAT32_BYTES_PER_SECTOR as usize)/core::mem::size_of::<DirectoryEntry>());
		for entry in entries {
			let e=&*entry;
			if e.name[0]==0x00 { return None; }
			if e.name[0]==0xE5 { lfn[0]=0; continue; }
			if e.attributes==0x0f {
				let le=&*(e as *const DirectoryEntry as *const LongNameEntry);
				lfn_append_entry(&mut lfn,le);
				continue;
			}

			let mut short=[0u8;260];
			let short_len=fat_short_name(e,&mut short);
			let short_match=short_len>0 && name_matches(name,&short);
			let long_match=lfn[0]!=0 && name_matches(name,&lfn);
			lfn[0]=0;

			if short_match || long_match { return Some(*e); }
		}
		let next=next_cluster(cur_cluster);
		if next>=0x0FFFFFF8 { break; }
		cur_cluster=next;
	}
	None
}

unsafe fn fat_path_components(path: *const u8, out: &mut [u8;260]) -> usize {
	if path.is_null() { return 0; }
	let mut n=0usize;
	while n+1<out.len() {
		let c=*path.add(n);
		if c==0 { break; }
		out[n]=c;
		n+=1;
	}
	n
}

unsafe fn find_file_in_root(path: *const u8) -> Option<DirectoryEntry> {
	let mut raw=[0u8;260];
	let n=fat_path_components(path,&mut raw);
	if n==0 { return None; }

	let mut cur_cluster=FAT32_ROOT_CLUSTER;
	let mut pos=0usize;

	while pos<n {
		while pos<n && raw[pos]==b'/' { pos+=1; }
		if pos>=n { break; }
		let begin=pos;
		while pos<n && raw[pos]!=b'/' { pos+=1; }
		let component=&raw[begin..pos];
		if component.is_empty() { continue; }

		let entry=find_entry_in_directory(cur_cluster,component)?;
		let is_last = {
			let mut q=pos;
			while q<n && raw[q]==b'/' { q+=1; }
			q>=n
		};

		if is_last {
			if (entry.attributes & 0x10) != 0 { return None; }
			return Some(entry);
		}

		if (entry.attributes & 0x10) == 0 { return None; }
		cur_cluster=((entry.first_cluster_high as u32)<<16)|(entry.first_cluster_low as u32);
		if cur_cluster==0 { return None; }
	}
	None
}

unsafe fn get_file_size_by_name(name: &[u8]) -> usize {
	let mut p=[0u8; 260];
	if name.len()+1 > p.len() { return 0; }
	p[..name.len()].copy_from_slice(name);
	p[name.len()]=0;
	match find_file_in_root(p.as_ptr()) { Some(e)=>e.file_size as usize, None=>0 }
}

unsafe fn read_file_by_name(name: &[u8], buffer: &mut [u8]) -> usize {
	let mut p=[0u8; 260];
	if name.len()+1 > p.len() { return 0; }
	p[..name.len()].copy_from_slice(name);
	p[name.len()]=0;
	if let Some(entry) = find_file_in_root(p.as_ptr()) {
		let file_size = entry.file_size as usize;
		let first_cluster = (entry.first_cluster_high as u32) << 16 | entry.first_cluster_low as u32;
		let mut cur_cluster = first_cluster;
		let mut offset = 0;
		let cluster_size = FAT32_SECTORS_PER_CLUSTER as usize * FAT32_BYTES_PER_SECTOR as usize;
		while offset < file_size && offset < buffer.len() {
			let src = cluster_to_ptr(cur_cluster);
			let to_copy = cmp::min(cluster_size, file_size - offset);
			let dst = &mut buffer[offset..offset + to_copy];
			ptr::copy_nonoverlapping(src, dst.as_mut_ptr(), to_copy);
			offset += to_copy;
			let next = next_cluster(cur_cluster);
			if next >= 0x0FFFFFF8 {
				if offset < file_size {
					return 0;
				}
				break;
			}
			cur_cluster = next;
		}
		return offset;
	}
	0
}

unsafe fn make_fat83_name(path: *const u8) -> Option<[u8; 11]> {
	if path.is_null() { return None; }
	let mut raw=[0u8;128]; let mut n=0usize;
	while n+1<raw.len() { let c=*path.add(n); if c==0 { break; } raw[n]=c; n+=1; }
	if n==0 { return None; }
	let mut start=0usize;
	for i in 0..n { if raw[i]==b'/' { start=i+1; } }
	let file=&raw[start..n];
	let mut name=[b' ';11];
	let mut dot=None;
	for i in 0..file.len() { if file[i]==b'.' { dot=Some(i); break; } }
	let base_len=cmp::min(dot.unwrap_or(file.len()),8);
	for i in 0..base_len { let mut c=file[i]; if c>=b'a'&&c<=b'z'{c-=32;} name[i]=c; }
	if let Some(d)=dot { let ext=&file[d+1..]; let ext_len=cmp::min(ext.len(),3); for i in 0..ext_len { let mut c=ext[i]; if c>=b'a'&&c<=b'z'{c-=32;} name[8+i]=c; } }
	Some(name)
}

unsafe fn read_file_range_by_name(name: &[u8;11], offset: usize, buffer: &mut [u8]) -> usize {
	let entry=match find_file_in_directory(FAT32_ROOT_CLUSTER,name){Some(e)=>e,None=>return 0};
	let file_size=entry.file_size as usize;
	if offset>=file_size || buffer.is_empty() { return 0; }
	let want=cmp::min(buffer.len(),file_size-offset);
	let cluster_size=FAT32_SECTORS_PER_CLUSTER as usize * FAT32_BYTES_PER_SECTOR as usize;
	if cluster_size==0 { return 0; }
	let mut cur_cluster=((entry.first_cluster_high as u32)<<16)|(entry.first_cluster_low as u32);
	let mut skip=offset/cluster_size;
	while skip>0 {
		let next=next_cluster(cur_cluster);
		if next>=0x0FFFFFF8 { return 0; }
		cur_cluster=next; skip-=1;
	}
	let mut in_cluster=offset%cluster_size;
	let mut done=0usize;
	while done<want {
		let src=cluster_to_ptr(cur_cluster).add(in_cluster);
		let n=cmp::min(cluster_size-in_cluster,want-done);
		ptr::copy_nonoverlapping(src,buffer.as_mut_ptr().add(done),n);
		done+=n; in_cluster=0;
		if done<want { let next=next_cluster(cur_cluster); if next>=0x0FFFFFF8 { break; } cur_cluster=next; }
	}
	done
}

#[no_mangle]
pub unsafe extern "C" fn bootfs_file_size(path: *const u8) -> u64 {
	match find_file_in_root(path) { Some(e)=>e.file_size as u64, None=>0 }
}

unsafe fn read_file_range_by_entry(entry: &DirectoryEntry, offset: usize, buffer: &mut [u8]) -> usize {
	let file_size=entry.file_size as usize;
	if offset>=file_size || buffer.is_empty() { return 0; }
	let want=cmp::min(buffer.len(),file_size-offset);
	let cluster_size=FAT32_SECTORS_PER_CLUSTER as usize * FAT32_BYTES_PER_SECTOR as usize;
	let first=((entry.first_cluster_high as u32)<<16)|(entry.first_cluster_low as u32);
	let mut cur=first;
	let mut skip=offset/cluster_size;
	while skip>0 { let next=next_cluster(cur); if next>=0x0FFFFFF8 { return 0; } cur=next; skip-=1; }
	let mut in_cluster=offset%cluster_size;
	let mut done=0usize;
	while done<want {
		let src=cluster_to_ptr(cur).add(in_cluster);
		let n=cmp::min(cluster_size-in_cluster,want-done);
		ptr::copy_nonoverlapping(src,buffer.as_mut_ptr().add(done),n);
		done+=n; in_cluster=0;
		if done<want { let next=next_cluster(cur); if next>=0x0FFFFFF8 { break; } cur=next; }
	}
	done
}

#[no_mangle]
pub unsafe extern "C" fn bootfs_read_root_file(path: *const u8, offset: u64, dst: *mut u8, len: usize) -> usize {
	if dst.is_null() || len==0 { return 0; }
	let entry=match find_file_in_root(path){Some(e)=>e,None=>return 0};
	let buf=slice::from_raw_parts_mut(dst,len);
	read_file_range_by_entry(&entry,offset as usize,buf)
}

unsafe fn import_fat_dir_into_vfs(cur_cluster: u32, prefix: &mut [u8;260], plen: usize, depth: u32) {
	if depth > 16 { return; }
	let mut cur=cur_cluster;
	let mut lfn=[0u8;260];

	loop {
		let dir_ptr=cluster_to_ptr(cur);
		if dir_ptr.is_null() { return; }
		let entries=slice::from_raw_parts(dir_ptr as *const DirectoryEntry,
			(FAT32_SECTORS_PER_CLUSTER as usize*FAT32_BYTES_PER_SECTOR as usize)/core::mem::size_of::<DirectoryEntry>());

		for entry in entries {
			let e=&*entry;
			if e.name[0]==0x00 { return; }
			if e.name[0]==0xE5 { lfn[0]=0; continue; }
			if e.attributes==0x0f {
				let le=&*(e as *const DirectoryEntry as *const LongNameEntry);
				lfn_append_entry(&mut lfn,le);
				continue;
			}

			let mut short=[0u8;260];
			let short_len=fat_short_name(e,&mut short);
			let lfn_len = if lfn[0] != 0 {
				let mut n = 0usize;
				while n < lfn.len() && lfn[n] != 0 { n += 1; }
				n
			} else { 0 };
			let name = if lfn_len != 0 { &lfn[..lfn_len] } else { &short[..short_len] };
			if name.is_empty() { lfn[0]=0; continue; }

			let is_dir=(e.attributes & 0x10)!=0;
			let is_dot = name == b"." || name == b"..";
			let is_volume = (e.attributes & 0x08) != 0;
			let is_system = (e.attributes & 0x04) != 0;
			if is_volume || is_system { lfn[0]=0; continue; }
			if !is_dot {
				let mut newlen=plen;
				if newlen>1 && prefix[newlen-1]!=b'/' {
					if newlen+1<260 { prefix[newlen]=b'/'; newlen+=1; }
				}
				if newlen+name.len()+1<260 {
					for &c in name {
						if newlen<259 { prefix[newlen]=c; newlen+=1; }
					}
					prefix[newlen]=0;

					if is_dir {
						let _=vfs_mkdir(prefix.as_ptr() as *const u8,0755);
						let child=((e.first_cluster_high as u32)<<16)|(e.first_cluster_low as u32);
						if child!=0 && fat_cluster_valid(child) {
							import_fat_dir_into_vfs(child,prefix,newlen,depth+1);
						}
					} else if e.file_size>0 {
						let _=vfs_import_boot_file(prefix.as_ptr() as *const u8,e.file_size as u64);
					}
				}
			}
			lfn[0]=0;
		}

		let next=next_cluster(cur);
		if next>=0x0FFFFFF8 { break; }
		cur=next;
	}
}

unsafe fn import_fat_root_into_vfs() {
	let mut prefix=[0u8;260];
	prefix[0]=b'/';
	prefix[1]=0;
	import_fat_dir_into_vfs(FAT32_ROOT_CLUSTER,&mut prefix,1,0);
}

#[no_mangle]
pub unsafe extern "C" fn kernel_execve_file_args(path: *const u8,
	argv: *const *const u8, argc: usize, envp: *const *const u8, envc: usize,
	entry_out: *mut u64, stack_out: *mut u64) -> i32 {
	if path.is_null() || entry_out.is_null() || stack_out.is_null() {
		return -14;
	}

	let entry=match find_file_in_root(path) { Some(e)=>e, None=>return -2 };

	static mut EXEC_IMAGE: [u8; 4 * 1024 * 1024] = [0u8; 4 * 1024 * 1024];

	let image_ptr = core::ptr::addr_of_mut!(EXEC_IMAGE) as *mut u8;
	let image = core::slice::from_raw_parts_mut(image_ptr, 4 * 1024 * 1024);

	let file_size = entry.file_size as usize;
	if file_size == 0 || file_size > image.len() {
		return -8;
	}

	let read_size = read_file_range_by_entry(&entry,0,&mut image[..file_size]);
	if read_size != file_size {
		return -8;
	}

	let size = file_size;

	let mut stack_top = 0u64;
	let entry = load_elf_with_args(image.as_ptr(), size, path, argv, argc, envp, envc, &mut stack_top);
	if entry == 0 { return -8; }
	*entry_out = entry;
	*stack_out = stack_top;
	0
}

#[no_mangle]
pub unsafe extern "C" fn kernel_execve_file(path: *const u8, entry_out: *mut u64, stack_out: *mut u64) -> i32 {
	kernel_execve_file_args(path, core::ptr::null(), 0, core::ptr::null(), 0, entry_out, stack_out)
}

unsafe fn list_root_directory(output: &mut [u8]) -> usize {
	let root_cluster = FAT32_ROOT_CLUSTER;
	let mut cur_cluster = root_cluster;
	let mut out_pos = 0usize;
	let mut lfn = [0u8; 260];

	loop {
		let dir_ptr = cluster_to_ptr(cur_cluster);
		let entries = slice::from_raw_parts(
			dir_ptr as *const DirectoryEntry,
			(FAT32_SECTORS_PER_CLUSTER as usize * FAT32_BYTES_PER_SECTOR as usize)
				/ core::mem::size_of::<DirectoryEntry>()
		);

		for entry in entries {
			let e = &*entry;
			if e.name[0] == 0x00 { return out_pos; }
			if e.name[0] == 0xE5 { lfn[0] = 0; continue; }
			if e.attributes == 0x0f {
				let le = &*(e as *const DirectoryEntry as *const LongNameEntry);
				lfn_append_entry(&mut lfn, le);
				continue;
			}

			let mut short = [0u8; 260];
			let short_len = fat_short_name(e, &mut short);
			let lfn_len = if lfn[0] != 0 {
				let mut n = 0usize;
				while n < lfn.len() && lfn[n] != 0 { n += 1; }
				n
			} else { 0 };
			let name = if lfn_len != 0 { &lfn[..lfn_len] } else { &short[..short_len] };
			if name.is_empty() || name[0] == 0 { lfn[0] = 0; continue; }
			if (e.attributes & 0x10) != 0 {

			}

			for &c in name {
				if c == 0 { break; }
				if out_pos + 2 >= output.len() { return out_pos; }
				output[out_pos] = c;
				out_pos += 1;
			}
			if out_pos + 1 >= output.len() { return out_pos; }
			output[out_pos] = if (e.attributes & 0x10) != 0 { b'/' } else { b'\n' };
			out_pos += 1;
			lfn[0] = 0;
		}

		let next = next_cluster(cur_cluster);
		if next >= 0x0FFFFFF8 { break; }
		cur_cluster = next;
	}
	out_pos
}

extern "C" {
	static __kernel_start: u8;
	static __kernel_end: u8;
}

#[no_mangle]
pub extern "C" fn kernel_main() -> ! {
	unsafe {

		setup_interrupts();

		init_fpu_sse();
		if !MEMMAP_REQUEST.response.is_null() && !HHDM_REQUEST.response.is_null() {
			let mm = &*MEMMAP_REQUEST.response;
			let hhdm = (*HHDM_REQUEST.response).offset;
			let (kernel_base, kernel_end) = if !KERNEL_ADDRESS_REQUEST.response.is_null() {
				let ka = &*KERNEL_ADDRESS_REQUEST.response;
				let start = &__kernel_start as *const u8 as u64;
				let end = &__kernel_end as *const u8 as u64;
				let size = end.saturating_sub(start);
				(ka.physical_base, ka.physical_base.saturating_add(size))
			} else {
				(0, 0)
			};
			pmm_init(mm.entries, mm.entry_count, hhdm, kernel_base, kernel_end);
			if !FRAMEBUFFER_REQUEST.response.is_null() {
				let fb_resp = &*FRAMEBUFFER_REQUEST.response;
				if fb_resp.framebuffer_count > 0 && !fb_resp.framebuffers.is_null() {
					let fb0 = &*(*fb_resp.framebuffers);
					let fb_size = fb0.pitch.saturating_mul(fb0.height);
					let hhdm = (*HHDM_REQUEST.response).offset;
					let raw = fb0.address as u64;
					let fb_phys = if raw >= hhdm && raw - hhdm <= 0x0000_ffff_ffff_ffff {
						raw - hhdm
					} else {
						raw
					};
					pmm_reserve_range(fb_phys, fb_size);
					debugcon_str(b"[PMM] framebuffer raw=0x");
					debugcon_hex(raw);
					debugcon_str(b"[PMM] reserved framebuffer phys=0x");
					debugcon_hex(fb_phys);
					debugcon_str(b"[PMM] reserved framebuffer size=0x");
					debugcon_hex(fb_size);
				}
			}
			if pmm_hhdm_selftest() != 0 {
				loop { asm!("cli; hlt"); }
			}
		}
		syscall_init();

		let framebuffer_response = &*FRAMEBUFFER_REQUEST.response;
		let fb = &**framebuffer_response.framebuffers;

		let max_cols = (fb.width as usize / CELL_W);
		let max_rows = fb.height as usize / CELL_H;

		let fb_raw = fb.address as u64;
		let hhdm = limine_hhdm_offset();
		let fb_phys = if hhdm != 0 && fb_raw >= hhdm && fb_raw - hhdm <= 0x0000_ffff_ffff_ffff {
			fb_raw - hhdm
		} else {
			fb_raw
		};
		GFX_FB_PHYS = fb_phys;
		GFX_FB_SIZE = fb.pitch.saturating_mul(fb.height);
		GFX_FB_WIDTH = fb.width;
		GFX_FB_HEIGHT = fb.height;
		GFX_FB_PITCH = fb.pitch;
		GFX_FB_BPP = fb.bpp as u64;
		let fb_virt = hhdm.saturating_add(fb_phys);

		debugcon_str(b"[FB-ADDR] raw=0x"); debugcon_hex(fb_raw);
		debugcon_str(b"[FB-ADDR] hhdm=0x"); debugcon_hex(hhdm);
		debugcon_str(b"[FB-ADDR] phys=0x"); debugcon_hex(fb_phys);
		debugcon_str(b"[FB-ADDR] kernel_va=0x"); debugcon_hex(fb_virt);

		C_CONSOLE_FB_VIEW.address = fb_virt as *mut u8;
		C_CONSOLE_FB_VIEW.width = fb.width;
		C_CONSOLE_FB_VIEW.height = fb.height;
		C_CONSOLE_FB_VIEW.pitch = fb.pitch;
		C_CONSOLE_FB_VIEW.bpp = fb.bpp;
		C_CONSOLE_FB_VIEW.memory_model = fb.memory_model;
		C_CONSOLE_FB_VIEW.red_mask_size = fb.red_mask_size;
		C_CONSOLE_FB_VIEW.red_mask_shift = fb.red_mask_shift;
		C_CONSOLE_FB_VIEW.green_mask_size = fb.green_mask_size;
		C_CONSOLE_FB_VIEW.green_mask_shift = fb.green_mask_shift;
		C_CONSOLE_FB_VIEW.blue_mask_size = fb.blue_mask_size;
		C_CONSOLE_FB_VIEW.blue_mask_shift = fb.blue_mask_shift;
		C_CONSOLE_FB_VIEW.unused = fb.unused;
		C_CONSOLE_FB_VIEW.edid_size = fb.edid_size;
		C_CONSOLE_FB_VIEW.edid = fb.edid;
		C_CONSOLE_FB_VIEW.mode_count = fb.mode_count;
		C_CONSOLE_FB_VIEW.modes = fb.modes;
		C_CONSOLE_FB = &C_CONSOLE_FB_VIEW as *const LimineFramebuffer;
		C_CONSOLE_COL = 0;
		C_CONSOLE_ROW = 0;

		let mut col = 0;
		let mut row = 0;

		clear_framebuffer(&C_CONSOLE_FB_VIEW);

		let print_str = |s: &[u8], col: &mut usize, row: &mut usize| {
			for &ch in s {
				if ch == b'\n' {
					*col = 0;
					*row += 1;
				} else {
					draw_char(fb, ch, *col, *row);
					*col += 1;
					if *col >= max_cols {
						*col = 0;
						*row += 1;
					}
				}
				if *row >= max_rows {
					scroll_screen(fb, max_cols, max_rows);
					*row = max_rows - 1;
				}
			}
		};

		let module_response_ptr = MODULE_REQUEST.response;
		let mut fat32_ok = false;
		if !module_response_ptr.is_null() {
			let module_response = &*module_response_ptr;
			let modules = slice::from_raw_parts(
				module_response.modules as *const *mut LimineFile,
				module_response.module_count as usize
			);
			for &mod_ptr in modules {
				let file = &*mod_ptr;
				let path = CStr::from_ptr(file.path as *const i8).to_bytes();
				if path == b"/boot/initrd.fat" {
					fat32_init(file.address, file.size);
					fat32_ok = true;
					let msg = b"\n[OK] FAT32 module loaded\n";
					print_str(msg, &mut col, &mut row);
					break;
				}
			}
		}
		if !fat32_ok {
			let msg = b"\n[WARN] FAT32 module not found\n";
			print_str(msg, &mut col, &mut row);
		}

		let welcome = b"Welcome to YabroOS-32 Shell\nType 'help' for available commands\n\n";
		print_str(welcome, &mut col, &mut row);
		let prompt = b"YabroOS-32> ";
		print_str(prompt, &mut col, &mut row);

		let mut buffer = [0u8; 128];
		let mut buf_idx = 0;

		draw_cursor(fb, col, row, true);

		loop {
			let scancode;
			asm!("cli");
			scancode = get_key();
			if scancode == 0 {
				asm!("sti; hlt");
				continue;
			}
			asm!("sti");

			draw_cursor(fb, col, row, false);
			let key = scancode_to_ascii(scancode);

			if key == 13 {
				draw_char(fb, b'\n', col, row);
				col = 0;
				row += 1;
				if row >= max_rows {
					scroll_screen(fb, max_cols, max_rows);
					row = max_rows - 1;
				}

				if buf_idx > 0 {
					let cmd_slice = &buffer[..buf_idx];

					if cmd_slice == b"help" {
						let help_text = b"\n=== YabroOS-32 Shell Commands ===\nhelp   - Show this help\ncls    - Clear screen\necho   - Print text\nfetch  - System information\nreboot - Reboot system\ntime   - Show RTC time\ncalc   - Simple calculator (e.g. calc 2+3)\nls     - List files\ncat    - View a file\nrun    - Run an ELF program\ngraphics - Run /GFX-TEST.ELF\n\nUserspace: run YSH.ELF to start YSH.\n\n";
						print_str(help_text, &mut col, &mut row);
					}

					else if cmd_slice == b"cls" {
						clear_framebuffer(&C_CONSOLE_FB_VIEW);
						col = 0;
						row = 0;
					}

					else if cmd_slice == b"fetch" {
						let fetch_text = b"\nYabroOS-32 Kernel v0.0.2-alpha (with ELF loader)\nRust + C Hybrid Kernel\nArchitecture: x86_64 / Limine\n\n";
						print_str(fetch_text, &mut col, &mut row);
					}

					else if cmd_slice == b"reboot" {
						let reboot_text = b"\nRebooting...\n";
						print_str(reboot_text, &mut col, &mut row);
						asm!("mov al, 0xfe; out 0x64, al");
					}

					else if cmd_slice.starts_with(b"echo ") {
						draw_char(fb, b' ', col, row);
						col += 1;
						print_str(&cmd_slice[5..], &mut col, &mut row);
						draw_char(fb, b'\n', col, row);
						col = 0;
						row += 1;
						if row >= max_rows {
							scroll_screen(fb, max_cols, max_rows);
							row = max_rows - 1;
						}
					}

					else if cmd_slice == b"time" {
						let mut time_buf = [0u8; 20];
						unsafe {
							get_rtc_time(time_buf.as_mut_ptr(), time_buf.len());
						}
						let prefix = b"\nSystem time: ";
						let suffix = b"\n\n";
						let mut msg = [0u8; 40];
						let mut pos = 0;
						for &ch in prefix {
							msg[pos] = ch;
							pos += 1;
						}
						for &ch in time_buf.iter() {
							if ch == 0 { break; }
							msg[pos] = ch;
							pos += 1;
						}
						for &ch in suffix {
							msg[pos] = ch;
							pos += 1;
						}
						print_str(&msg[..pos], &mut col, &mut row);
					}

					else if cmd_slice.starts_with(b"calc ") {
						let expr = &cmd_slice[5..];
						let mut op_pos = None;
						let mut op_char = 0u8;
						for (i, &ch) in expr.iter().enumerate() {
							if ch == b'+' || ch == b'-' || ch == b'*' || ch == b'/' {
								op_pos = Some(i);
								op_char = ch;
								break;
							}
						}
						if let Some(pos) = op_pos {
							let left = trim_bytes(&expr[..pos]);
							let right = trim_bytes(&expr[pos+1..]);
							if left.len() > 0 && right.len() > 0 {
								let a = atoi(left);
								let b = atoi(right);
								let result = match op_char {
									b'+' => a + b,
									b'-' => a - b,
									b'*' => a * b,
									b'/' => {
										if b == 0 {
											let err = b"\nError: division by zero\n\n";
											print_str(err, &mut col, &mut row);
											continue;
										} else {
											a / b
										}
									}
									_ => 0,
								};
								let mut res_msg = [0u8; 40];
								let prefix = b"\nResult: ";
								let suffix = b"\n\n";
								let mut pos = 0;
								for &ch in prefix {
									res_msg[pos] = ch;
									pos += 1;
								}
								let num_buf = &mut [0u8; 21];
								let num_slice = format_i64(result, num_buf);
								for &ch in num_slice {
									res_msg[pos] = ch;
									pos += 1;
								}
								for &ch in suffix {
									res_msg[pos] = ch;
									pos += 1;
								}
								print_str(&res_msg[..pos], &mut col, &mut row);
							} else {
								let err = b"\nInvalid expression. Use: calc number op number\n\n";
								print_str(err, &mut col, &mut row);
							}
						} else {
							let err = b"\nInvalid expression. Use: calc number op number\n\n";
							print_str(err, &mut col, &mut row);
						}
					}

					else if cmd_slice == b"ls" || cmd_slice.starts_with(b"ls ") {
						unsafe {
							let arg = if cmd_slice.len() > 3 { &cmd_slice[3..] } else { b"/" };
							let mut path = [0u8; 128];
							let mut plen = 0usize;
							if arg.is_empty() { path[0] = b'/'; plen = 1; }
							else {
								for &c in arg.iter().take(path.len() - 1) {
									path[plen] = c; plen += 1;
								}
								if plen == 0 || path[0] != b'/' {

									let mut tmp = [0u8; 128];
									let mut n = 0usize;
									tmp[n] = b'/'; n += 1;
									for &c in arg.iter().take(tmp.len() - 2) { tmp[n] = c; n += 1; }
									path = tmp; plen = n;
								}
							}
							while plen > 1 && path[plen - 1] == b'/' { plen -= 1; }
							path[plen] = 0;

							let mut out = [0u8; 4096];
							let vlen = vfs_list_dir(path.as_ptr(), out.as_mut_ptr(), out.len());
							if vlen > 0 {
								print_str(&out[..vlen], &mut col, &mut row);
							} else {
								print_str(b"No such directory\n", &mut col, &mut row);
							}
						}
					}

					else if cmd_slice.starts_with(b"cat ") {
						let filename = &cmd_slice[4..];
						let mut path = [0u8; 128];
						let mut plen = 0usize;
						if !filename.is_empty() {
							if filename[0] == b'/' {
								for &c in filename.iter().take(path.len()-1) { path[plen]=c; plen+=1; }
							} else {
								path[0]=b'/'; plen=1;
								for &c in filename.iter().take(path.len()-2) { path[plen]=c; plen+=1; }
							}
						}
						while plen > 1 && path[plen-1] == b'/' { plen -= 1; }
						path[plen]=0;
						unsafe {
							let mut file_data = [0u8; 2048];
							let vread = vfs_read_file(path.as_ptr(), file_data.as_mut_ptr(), file_data.len());
							if vread > 0 {
								print_str(&file_data[..vread], &mut col, &mut row);
								print_str(b"\n", &mut col, &mut row);
							} else {

								let mut name_buf = [b' '; 11];
								let mut name_part = filename;
								let mut ext_part = &[][..];
								if let Some(pos) = filename.iter().position(|&c| c == b'.') {
									name_part = &filename[..pos]; ext_part = &filename[pos+1..];
								}
								let name_len = cmp::min(name_part.len(), 8);
								for i in 0..name_len {
									let mut c=name_part[i]; if c>=b'a' && c<=b'z' { c-=32; }
									name_buf[i]=c;
								}
								let ext_len=cmp::min(ext_part.len(),3);
								for i in 0..ext_len {
									let mut c=ext_part[i]; if c>=b'a' && c<=b'z' { c-=32; }
									name_buf[8+i]=c;
								}
								let mut fat_data=[0u8;1024];
								let read=read_file_by_name(&name_buf,&mut fat_data);
								if read>0 { print_str(&fat_data[..read],&mut col,&mut row); print_str(b"\n",&mut col,&mut row); }
								else { print_str(b"\nFile not found\n",&mut col,&mut row); }
							}
						}
					}

					else if cmd_slice.starts_with(b"run ") {

						let command = &cmd_slice[4..];
						let mut tokens: [&[u8]; 17] = [&[]; 17];
						let mut ntok = 0usize;
						let mut i = 0usize;
						while i < command.len() && ntok < tokens.len() {
							while i < command.len() && command[i].is_ascii_whitespace() { i += 1; }
							if i >= command.len() { break; }
							let start = i;
							while i < command.len() && !command[i].is_ascii_whitespace() { i += 1; }
							tokens[ntok] = &command[start..i];
							ntok += 1;
						}
						if ntok == 0 {
							print_str(b"\nInvalid run command\n", &mut col, &mut row);
							continue;
						}
						let filename = tokens[0];
						let mut name_buf = [b' '; 11];
						let mut name_part = filename;
						let mut ext_part = &[][..];
						if let Some(pos) = filename.iter().position(|&c| c == b'.') {
							name_part = &filename[..pos];
							ext_part = &filename[pos+1..];
						}
						let name_len = cmp::min(name_part.len(), 8);
						for i in 0..name_len {
							let mut c = name_part[i];
							if c >= b'a' && c <= b'z' { c -= 32; }
							name_buf[i] = c;
						}
						let ext_len = cmp::min(ext_part.len(), 3);
						for i in 0..ext_len {
							let mut c = ext_part[i];
							if c >= b'a' && c <= b'z' { c -= 32; }
							name_buf[8 + i] = c;
						}
						unsafe {

							let mut exec_path = [0u8; 128];
							let copy_len = cmp::min(filename.len(), exec_path.len() - 1);
							exec_path[..copy_len].copy_from_slice(&filename[..copy_len]);
							exec_path[copy_len] = 0;

							let mut argv_storage = [[0u8; 96]; 8];
							let mut env_storage = [[0u8; 96]; 8];
							let mut argv_ptrs = [core::ptr::null::<u8>(); 8];
							let mut env_ptrs = [core::ptr::null::<u8>(); 8];
							let mut argc = 0usize;
							let mut envc = 0usize;

							let argv0_len = if filename.first() == Some(&b'/') {
								if copy_len >= argv_storage[0].len() { 0 } else {
									argv_storage[0][..copy_len].copy_from_slice(&filename[..copy_len]);
									copy_len
								}
							} else {
								let canonical_len = 1 + cmp::min(name_part.len(), 8)
									+ if !ext_part.is_empty() {
										1 + cmp::min(ext_part.len(), 3)
									} else { 0 };
								if canonical_len >= argv_storage[0].len() { 0 } else {
									argv_storage[0][0] = b'/';
									for j in 0..cmp::min(name_part.len(), 8) {
										let mut c = name_part[j];
										if c >= b'a' && c <= b'z' { c -= 32; }
										argv_storage[0][1 + j] = c;
									}
									let mut pos = 1 + cmp::min(name_part.len(), 8);
									if !ext_part.is_empty() {
										argv_storage[0][pos] = b'.';
										pos += 1;
										for j in 0..cmp::min(ext_part.len(), 3) {
											let mut c = ext_part[j];
											if c >= b'a' && c <= b'z' { c -= 32; }
											argv_storage[0][pos + j] = c;
										}
									}
									canonical_len
								}
							};
							if argv0_len == 0 {
								print_str(b"\nArgument too long\n", &mut col, &mut row);
								continue;
							}
							argv_storage[0][argv0_len] = 0;
							argv_ptrs[0] = argv_storage[0].as_ptr();
							argc = 1;

							for tok in tokens[1..ntok].iter() {
								if tok.is_empty() { continue; }
								if tok.iter().position(|&c| c == b'=').is_some() {
									if envc >= env_storage.len() { break; }
									let n = cmp::min(tok.len(), env_storage[envc].len() - 1);
									env_storage[envc][..n].copy_from_slice(&tok[..n]);
									env_storage[envc][n] = 0;
									env_ptrs[envc] = env_storage[envc].as_ptr();
									envc += 1;
								} else if argc < argv_storage.len() {
									let n = cmp::min(tok.len(), argv_storage[argc].len() - 1);
									argv_storage[argc][..n].copy_from_slice(&tok[..n]);
									argv_storage[argc][n] = 0;
									argv_ptrs[argc] = argv_storage[argc].as_ptr();
									argc += 1;
								}
							}

							let mut entry: u64 = 0;
							let mut stack_top: u64 = 0;
							let rc = kernel_execve_file_args(
								exec_path.as_ptr(),
								argv_ptrs.as_ptr(), argc,
								env_ptrs.as_ptr(), envc,
								&mut entry, &mut stack_top,
							);

							if rc == 0 && entry != 0 {
								let msg = b"\nRunning program...\n";
								print_str(msg, &mut col, &mut row);

								console_fb_set_cursor(col as u64, row as u64);

								exec_enter_usermode(entry, stack_top, vmm_user_cr3());

								let packed = console_fb_get_cursor();
								col = packed as u32 as usize;
								row = (packed >> 32) as usize;

								let done = b"\n[program exited]\n";
								print_str(done, &mut col, &mut row);
							} else {
								let err = b"\nInvalid ELF file\n";
								print_str(err, &mut col, &mut row);
							}
						}
					}

					else {
						let err_text = b"\nUnknown command. Type 'help'\n\n";
						print_str(err_text, &mut col, &mut row);
					}

					buf_idx = 0;
				}

				print_str(prompt, &mut col, &mut row);
			} else if key == 8 {
				if buf_idx > 0 && col > 12 {
					buf_idx -= 1;
					col -= 1;
					draw_char(fb, b' ', col, row);
				}
			} else if key >= 32 && key <= 126 {
				if buf_idx < buffer.len() - 1 {
					buffer[buf_idx] = key;
					buf_idx += 1;
					draw_char(fb, key, col, row);
					col += 1;
					if col >= max_cols {
						col = 0;
						row += 1;
						if row >= max_rows {
							scroll_screen(fb, max_cols, max_rows);
							row = max_rows - 1;
						}
					}
				}
			}

			if row >= max_rows {
				scroll_screen(fb, max_cols, max_rows);
				row = max_rows - 1;
			}

			draw_cursor(fb, col, row, true);
		}
	}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
	loop {
		unsafe { asm!("cli; hlt"); }
	}
}

