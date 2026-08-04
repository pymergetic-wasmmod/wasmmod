#![no_std]

#[no_mangle]
pub extern "C" fn c_rs_tag() -> i32 {
    5
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
