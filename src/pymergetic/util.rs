//! pymergetic.util — pep420 namespace (see `__pmm__.toml`: `pep420 =
//! true`, no `impl`). Python needs zero files for that; Rust's `mod`
//! system has no equivalent to a dynamically-merged namespace, so this
//! file exists purely as the mechanical declaration of this level's
//! children — no logic, no impl of its own, same posture as any other
//! barrel/umbrella file in this tree.
#[path = "util/mem.rs"]
pub mod mem;

#[path = "util/zlib.rs"]
pub mod zlib;

#[path = "util/mtar.rs"]
pub mod mtar;

#[path = "util/lz4.rs"]
pub mod lz4;

#[path = "util/lock.rs"]
pub mod lock;

#[path = "util/pysample.rs"]
pub mod pysample;
