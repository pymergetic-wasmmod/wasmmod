#![no_std]

#[no_mangle]
pub extern "C" fn d_rs_value() -> i32 {
    7
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
