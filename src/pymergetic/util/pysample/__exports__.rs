//! pymergetic.util.pysample — hand-written stand-in for what `bindgen`
//! should emit by reading __exports__.h. Delete once the real py-facegen
//! pipeline exists; this is training-scaffold, not the plan.
#![allow(non_camel_case_types)]

unsafe extern "C" {
    pub fn pm_util_pysample_hello() -> i32;
    pub fn pm_util_pysample_echo_len(data: *const u8, data_len: u32) -> i32;
}
