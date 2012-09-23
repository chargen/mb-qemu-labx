/*
 * Tables of FDT device models and their init functions. Keyed by compatibility
 * strings, device instance names.
 */

#ifndef FDT_GENERIC_H
#define FDT_GENERIC_H

#include "qemu-common.h"
#include "sysbus.h"
#include "device_tree.h"
#include "qemu-coroutine.h"

typedef struct FDTDevOpaque {
    char *node_path;
    void *opaque;
} FDTDevOpaque;

typedef struct FDTMachineInfo {
    /* the fdt blob */
    void *fdt;
    /* irq descriptors for top level int controller */
    qemu_irq *irq_base;
    /* per-device specific opaques */
    FDTDevOpaque *dev_opaques;

    /* Base address of the root bus */
    target_phys_addr_t sysbus_base;

    /* recheck coroutine queue */
    CoQueue *cq;
} FDTMachineInfo;

/* create a new FDTMachineInfo. The client is responsible for setting irq_base.
 * the mutex fdt_mutex is locked on return. Client must call
 * fdt_init_destroy_fdti to cleanup
 */

FDTMachineInfo *fdt_init_new_fdti(void *fdt);
void fdt_init_destroy_fdti(FDTMachineInfo *fdti);

typedef int (*FDTInitFn)(char *, FDTMachineInfo *, void *);

/* associate a FDTInitFn with a FDT compatibility */

void add_to_compat_table(FDTInitFn, const char *, void *);

/* try and find a device model for a particular compatibility. If found,
 * the FDTInitFn associated with the compat will be called and 0 will
 * be returned. Returns non-zero on not found or error
 */

int fdt_init_compat(char *, FDTMachineInfo *, const char *);

/* same as above, but associates with a FDT node name (rather than compat) */

void add_to_inst_bind_table(FDTInitFn, const char *, void *);
int fdt_init_inst_bind(char *, FDTMachineInfo *, const char *);

/* Register an FDTInitFn that should always be called upon machine creation */

void add_to_force_table(FDTInitFn, const char *, void *);
int fdt_force_bind_all(FDTMachineInfo *);

void dump_compat_table(void);
void dump_inst_bind_table(void);

/* Called from FDTInitFn's to inform the framework that a dependency is
 * unresolved and the calling context needs to wait for another device to
 * instantiate first. The calling thread will suspend until a change in state
 * in the argument fdt machine is detected.
 */

void fdt_init_yield(FDTMachineInfo *);

/* set, check and get per device opaques. Keyed by fdt node_paths */

void fdt_init_set_opaque(FDTMachineInfo *fdti, const char *node_path, void *opaque);
int fdt_init_has_opaque(FDTMachineInfo *fdti, const char *node_path);
void *fdt_init_get_opaque(FDTMachineInfo *fdti, const char *node_path);

/* statically register a FDTInitFn as being associate with a compatibility */

#define fdt_register_compatibility_opaque(function, compat, n, opaque) \
static void __attribute__((constructor)) \
function ## n ## _register_imp(void) { \
    add_to_compat_table(function, compat, opaque); \
}

#define fdt_register_compatibility_n(function, compat, n) \
fdt_register_compatibility_opaque(function, compat, n, NULL)

#define fdt_register_compatibility(function, compat) \
fdt_register_compatibility_n(function, compat, 0)

#endif /* FDT_GENERIC_H */
