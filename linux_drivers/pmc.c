#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/uaccess.h>

#include <linux/bits.h>
#include <asm/sysreg.h>

#define get_bit_offset(index, mask)     (__ffs(mask) + (index))

/* Counters */
#define SYS_IMP_APL_PMC0_EL1	sys_reg(3, 2, 15, 0, 0) // cycles
#define SYS_IMP_APL_PMC1_EL1	sys_reg(3, 2, 15, 1, 0) // instructions

/* Core PMC control register */
// PMCR0
#define SYS_IMP_APL_PMCR0_EL1	sys_reg(3, 1, 15, 0, 0)
#define PMCR0_CNT_ENABLE_0_7	GENMASK(7, 0)
#define PMCR0_IMODE		GENMASK(10, 8)
#define PMCR0_IMODE_OFF		0
#define PMCR0_IMODE_PMI		1
#define PMCR0_IMODE_AIC		2
#define PMCR0_IMODE_HALT	3
#define PMCR0_IMODE_FIQ		4
#define PMCR0_IACT		BIT(11)
#define PMCR0_PMI_ENABLE_0_7	GENMASK(19, 12)
#define PMCR0_STOP_CNT_ON_PMI	BIT(20)
#define PMCR0_CNT_GLOB_L2C_EVT	BIT(21)
#define PMCR0_DEFER_PMI_TO_ERET	BIT(22)
#define PMCR0_ALLOW_CNT_EN_EL0	BIT(30)

// PMCR1
#define SYS_IMP_APL_PMCR1_EL1	sys_reg(3, 1, 15, 1, 0)
#define PMCR1_COUNT_A64_EL0_0_7	GENMASK(15, 8)
#define PMCR1_COUNT_A64_EL1_0_7	GENMASK(23, 16)

#define FIRST_MINOR 0
#define MINOR_CNT   1

#define LOG(...) 	printk(KERN_INFO "pmc: " __VA_ARGS__)

static dev_t dev;
static struct cdev c_dev;
static struct class *cl;

static void m1_pmu_start(void) {
    u64 val;
    val = read_sysreg_s(SYS_IMP_APL_PMCR0_EL1);
    val &= ~(PMCR0_IMODE | PMCR0_IACT);
    val |= FIELD_PREP(PMCR0_IMODE, PMCR0_IMODE_FIQ);
    write_sysreg_s(val, SYS_IMP_APL_PMCR0_EL1);
    asm volatile ("isb");
}

static void m1_pmu_stop(void) {
    u64 val;
    val = read_sysreg_s(SYS_IMP_APL_PMCR0_EL1);
    val &= ~(PMCR0_IMODE | PMCR0_IACT);
    val |= FIELD_PREP(PMCR0_IMODE, PMCR0_IMODE_OFF);
    write_sysreg_s(val, SYS_IMP_APL_PMCR0_EL1);
    asm volatile ("isb");
}

static void m1_pmu_enable_pmc0_counter(void) {
    u64 val, bit;
    bit = BIT(get_bit_offset(0, PMCR0_CNT_ENABLE_0_7));
    val = read_sysreg_s(SYS_IMP_APL_PMCR0_EL1);
    val |= bit;
    write_sysreg_s(val, SYS_IMP_APL_PMCR0_EL1);
    asm volatile ("isb");
}

static void m1_pmu_disable_pmc0_counter(void) {
    u64 val, bit;
    bit = BIT(get_bit_offset(0, PMCR0_CNT_ENABLE_0_7));
    val = read_sysreg_s(SYS_IMP_APL_PMCR0_EL1);
    val &= ~bit;
    write_sysreg_s(val, SYS_IMP_APL_PMCR0_EL1);
    asm volatile ("isb");
}

static void m1_pmu_configure_pmc0(void) {
    u64 val, user_bit, kernel_bit;
    user_bit = BIT(get_bit_offset(0, PMCR1_COUNT_A64_EL0_0_7));
    kernel_bit = BIT(get_bit_offset(0, PMCR1_COUNT_A64_EL1_0_7));
    val = read_sysreg_s(SYS_IMP_APL_PMCR1_EL1);
    val |= user_bit;
    val |= kernel_bit;
    write_sysreg_s(val, SYS_IMP_APL_PMCR1_EL1);
    asm volatile ("isb");
}

static void m1_pmu_allow_user_pmc0(void) {
    u64 val;
    val = read_sysreg_s(SYS_IMP_APL_PMCR0_EL1);
    val |= PMCR0_ALLOW_CNT_EN_EL0;
    write_sysreg_s(val, SYS_IMP_APL_PMCR0_EL1);
    asm volatile ("isb");
}

MODULE_LICENSE("Dual BSD/GPL");

static int mod_open(struct inode *i, struct file *f) {
    return 0;
}

static int mod_close(struct inode *i, struct file *f) {
    return 0;
}

static long mod_ioctl(struct file *f, unsigned int cmd, unsigned long arg) {
    LOG("cmd: %d, arg: %lu\n", cmd, arg);
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = mod_open,
    .release = mod_close,
    .unlocked_ioctl = mod_ioctl,
};

static void m1_pmu_enable_and_allowuser_pmc0(void*) {
    m1_pmu_start();
    m1_pmu_enable_pmc0_counter();
    m1_pmu_configure_pmc0();
    m1_pmu_allow_user_pmc0();
}

static void m1_pmu_disable_pmc0(void*) {
    m1_pmu_stop();
    m1_pmu_disable_pmc0_counter();
}

static int mod_init(void) {

    int ret;
    struct device *dev_ret;

    if ((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, "pmc")) < 0)
        return ret;

    cdev_init(&c_dev, &fops);

    if ((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0)
        return ret;

    if (IS_ERR(cl = class_create(THIS_MODULE, "char"))) {
        cdev_del(&c_dev);
        unregister_chrdev_region(dev, MINOR_CNT);
        return PTR_ERR(cl);
    }

    if (IS_ERR(dev_ret = device_create(cl, NULL, dev, NULL, "pmc"))) {
        class_destroy(cl);
        cdev_del(&c_dev);
        unregister_chrdev_region(dev, MINOR_CNT);
        return PTR_ERR(cl);
    }

    LOG("Module loaded!\n");

    on_each_cpu(m1_pmu_enable_and_allowuser_pmc0, NULL, 0);

    return 0;
}

static void mod_exit(void) {

    device_destroy(cl, dev);
    class_destroy(cl);
    cdev_del(&c_dev);
    unregister_chrdev_region(dev, MINOR_CNT);

    LOG("Module unloaded!\n");

    on_each_cpu(m1_pmu_disable_pmc0, NULL, 0);
}

module_init(mod_init);
module_exit(mod_exit);

