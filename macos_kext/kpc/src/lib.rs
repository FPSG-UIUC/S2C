use bitflags::bitflags;
use libloading::{Error, Library, Symbol};
use ouroboros::self_referencing;

bitflags! {
    pub struct KpcClass: u32 {
        const FIXED        = 1 << 0;
        const CONFIGURABLE = 1 << 1;
        const POWER        = 1 << 2;
        const RAW_PMU      = 1 << 3;
    }

    pub struct KpcMode: u64 {
        const A32_EL0 = 1 << 16;
        const A64_EL0 = 1 << 17;
        const EL1     = 1 << 18;
        const EL3     = 1 << 19;
    }
}

#[self_referencing]
pub struct Kpc {
    library: Library,

    #[borrows(library)]
    #[not_covariant]
    kpc_force_all_ctrs_set: Symbol<'this, unsafe extern fn(i32) -> i32>,

    #[borrows(library)]
    #[not_covariant]
    kpc_set_counting: Symbol<'this, unsafe extern fn(u32) -> i32>,

    #[borrows(library)]
    #[not_covariant]
    kpc_set_thread_counting: Symbol<'this, unsafe extern fn(u32) -> i32>,

    #[borrows(library)]
    #[not_covariant]
    kpc_set_config: Symbol<'this, unsafe extern fn(u32, *mut core::ffi::c_void) -> i32>,

    #[borrows(library)]
    #[not_covariant]
    kpc_get_counter_count: Symbol<'this, unsafe extern fn(u32) -> u32>,

    #[borrows(library)]
    #[not_covariant]
    kpc_get_config_count: Symbol<'this, unsafe extern fn(u32) -> u32>,

    #[borrows(library)]
    #[not_covariant]
    kpc_get_thread_counters: Symbol<'this, unsafe extern fn(i32, u32, *mut core::ffi::c_void) -> i32>,
}

impl Kpc {
    pub fn open() -> Result<Kpc, Error> {
        let library = unsafe {
            Library::new("/System/Library/PrivateFrameworks/kperf.framework/Versions/A/kperf")?
        };

        let kpc = KpcBuilder {
            library,
            kpc_force_all_ctrs_set_builder: |library: &Library| unsafe {
                library.get(b"kpc_force_all_ctrs_set").unwrap()
            },
            kpc_set_counting_builder: |library: &Library| unsafe {
                library.get(b"kpc_set_counting").unwrap()
            },
            kpc_set_thread_counting_builder: |library: &Library| unsafe {
                library.get(b"kpc_set_thread_counting").unwrap()
            },
            kpc_set_config_builder: |library: &Library| unsafe {
                library.get(b"kpc_set_config").unwrap()
            },
            kpc_get_counter_count_builder: |library: &Library| unsafe {
                library.get(b"kpc_get_counter_count").unwrap()
            },
            kpc_get_config_count_builder: |library: &Library| unsafe {
                library.get(b"kpc_get_config_count").unwrap()
            },
            kpc_get_thread_counters_builder: |library: &Library| unsafe {
                library.get(b"kpc_get_thread_counters").unwrap()
            },
        }.build();

        Ok(kpc)
    }

    pub fn force_all_counters_set(&self) {
        self.with(|library| {
            unsafe {
                (*library.kpc_force_all_ctrs_set)(1)
            };
        });
    }

    pub fn set_counting(&self, mask: KpcClass) {
        self.with(|library| {
            unsafe {
                (*library.kpc_set_counting)(mask.bits())
            };
        });
    }

    pub fn set_thread_counting(&self, mask: KpcClass) {
        self.with(|library| {
            unsafe {
                (*library.kpc_set_thread_counting)(mask.bits())
            };
        });
    }

    pub fn set_config(
        &self,
        mask: KpcClass,
        config: &[Option<(u8, KpcMode)>],
    ) {
        let mut config: Vec<u64> = config
            .iter()
            .map(|config| match config {
                Some((event, mode)) => *event as u64 | mode.bits(),
                None => 0,
            })
            .collect();

        self.with(|library| {
            unsafe {
                (*library.kpc_set_config)(mask.bits(), config.as_mut_ptr() as *mut core::ffi::c_void);
            }
        });
    }

    pub fn get_counter_count(&self, mask: KpcClass) -> usize {
        let mut result = 0;

        self.with(|library| {
            result = unsafe {
                (*library.kpc_get_counter_count)(mask.bits()) as usize
            };
        });

        result
    }

    pub fn get_config_count(&self, mask: KpcClass) -> usize {
        let mut result = 0;

        self.with(|library| {
            result = unsafe {
                (*library.kpc_get_config_count)(mask.bits()) as usize
            };
        });

        result
    }

    pub fn get_thread_counters(&self, counters: &mut [u64]) {
        self.with(|library| {
            unsafe {
                (*library.kpc_get_thread_counters)(0, counters.len() as u32, counters.as_mut_ptr() as *mut core::ffi::c_void)
            };
        });
    }
}
