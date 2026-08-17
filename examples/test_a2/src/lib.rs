#![no_std]

#[no_mangle]
pub extern "C" fn a2_rs_ping() -> i32 {
    1
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
