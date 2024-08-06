use kpc::{Kpc, KpcClass, KpcMode};

fn main() {
    let mut config = vec![None; 10];

    // Set up counter #2 to count core cycles.
    config[0] = Some((0x02, KpcMode::A64_EL0));
    // Set up counter #5 to count L1d cache misses.
    config[5] = Some((0xa3, KpcMode::A64_EL0));

    let kpc = Kpc::open().unwrap();

    kpc.set_config(KpcClass::FIXED | KpcClass::CONFIGURABLE, &config);
    kpc.force_all_counters_set();
    kpc.set_counting(KpcClass::FIXED | KpcClass::CONFIGURABLE);
    kpc.set_thread_counting(KpcClass::FIXED | KpcClass::CONFIGURABLE);
}
