//! pymergetic.util.lock — module tests (`__tests__.rs`).

use super::*;
use crate::util::mod_test::case;

unsafe extern "C" fn case_locks_out_a_second_try_lock() -> i32 {
    case(|| {
        let m = Mutex::new(0i32);
        let guard = m.lock();
        assert!(m.try_lock().is_none());
        drop(guard);
        assert!(m.try_lock().is_some());
    })
}

unsafe extern "C" fn case_guard_mutates_through_deref() -> i32 {
    case(|| {
        let m = Mutex::new(alloc::vec![1, 2, 3]);
        m.lock().push(4);
        assert_eq!(*m.lock(), alloc::vec![1, 2, 3, 4]);
    })
}

crate::PM_MOD_TEST_RS!(
    "pymergetic.util.lock",
    "locks_out_a_second_try_lock",
    case_locks_out_a_second_try_lock
);
crate::PM_MOD_TEST_RS!(
    "pymergetic.util.lock",
    "guard_mutates_through_deref",
    case_guard_mutates_through_deref
);

#[test]
fn locks_out_a_second_try_lock() {
    assert_eq!(unsafe { case_locks_out_a_second_try_lock() }, 0);
}

#[test]
fn guard_mutates_through_deref() {
    assert_eq!(unsafe { case_guard_mutates_through_deref() }, 0);
}
