################################################################################
#
# y2-rsync-compat: rsync >= 3.4.3 unconditionally includes <linux/openat2.h> (kernel headers 5.6+),
# but the Y2 toolchain uses 3.4 headers to match the stock kernel. Provide a local shim so it
# builds; at runtime openat2 returns ENOSYS on 3.4 and rsync falls back to its per-component walk.
# Not a package: only a hook on rsync.
#
################################################################################

define Y2_RSYNC_OPENAT2_SHIM
	mkdir -p $(@D)/linux
	printf '%s\n' \
		'#ifndef Y2_LINUX_OPENAT2_H' \
		'#define Y2_LINUX_OPENAT2_H' \
		'#include <linux/types.h>' \
		'struct open_how { __u64 flags; __u64 mode; __u64 resolve; };' \
		'#define RESOLVE_NO_XDEV 0x01' \
		'#define RESOLVE_NO_MAGICLINKS 0x02' \
		'#define RESOLVE_NO_SYMLINKS 0x04' \
		'#define RESOLVE_BENEATH 0x08' \
		'#define RESOLVE_IN_ROOT 0x10' \
		'#ifndef SYS_openat2' \
		'#define SYS_openat2 437' \
		'#endif' \
		'#endif' > $(@D)/linux/openat2.h
endef
RSYNC_PRE_CONFIGURE_HOOKS += Y2_RSYNC_OPENAT2_SHIM
