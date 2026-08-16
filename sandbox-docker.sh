#!/bin/bash

# --- Paths Configuration ---
BASE_DIR="/labs"
KERNEL_DIR="$BASE_DIR/linux-2.6.39"
BUSYBOX_DIR="$BASE_DIR/busybox-1.35.0"
ROOTFS_DIR="$BASE_DIR/rootfs"
INITRD_IMG="$BASE_DIR/initrd_busybox.img"

# --- Structural Verification Engine ---
verify_and_build_structures() {
    # 1. Always guarantee compilation dependencies exist in the new instance
    if ! dpkg -s gcc-multilib >/dev/null 2>&1; then
        echo "--> System update: Installing needed dev headers..."
        apt-get update && apt-get install -y gcc-multilib
    fi

    # 2. Rebuild missing rootfs structure if a fresh git clone deleted it
    if [ ! -d "$ROOTFS_DIR" ] || [ ! -d "$ROOTFS_DIR/dev" ]; then
        echo "--> Restoring missing rootfs staging folders..."
        mkdir -p "$ROOTFS_DIR"/{dev,proc,sys,etc,root,bin,sbin,usr}
    fi

    # 3. Always guarantee core virtual communications files exist
    if [ ! -c "$ROOTFS_DIR/dev/console" ]; then
        mknod -m 600 "$ROOTFS_DIR/dev/console" c 5 1 2>/dev/null || true
    fi
    if [ ! -c "$ROOTFS_DIR/dev/null" ]; then
        mknod -m 666 "$ROOTFS_DIR/dev/null" c 1 3 2>/dev/null || true
    fi

    # 4. Enforce base initialization script availability
    if [ ! -f "$ROOTFS_DIR/init" ]; then
        cat << 'RINIT' > "$ROOTFS_DIR/init"
#!/bin/sh
mount -t proc none /proc
mount -t sysfs none /sys
echo "=================================================="
echo " SUCCESS! Your Linux Kernel 2.6.39 System Is Alive! "
echo "=================================================="
exec /bin/sh
RINIT
        chmod +x "$ROOTFS_DIR/init"
    fi

    # 5. Fix missing BusyBox configurations if cloned fresh
    if [ ! -f "$BUSYBOX_DIR/.config" ]; then
        echo "--> Reconfiguring BusyBox compiler..."
        cd "$BUSYBOX_DIR" && make defconfig
        sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
        sed -i 's/CONFIG_EXTRA_CFLAGS=""/CONFIG_EXTRA_CFLAGS="-m32 -march=i386"/' .config
        sed -i 's/CONFIG_EXTRA_LDFLAGS=""/CONFIG_EXTRA_LDFLAGS="-m32"/' .config
    fi

    # 6. Fix missing Kernel configurations or hardcoded paths from the old workspace
    if [ ! -f "$KERNEL_DIR/.config" ] || [ ! -f "$KERNEL_DIR/scripts/basic/Makefile" ]; then
        echo "--> Re-initializing Kernel layout definitions..."
        cd "$KERNEL_DIR"
        make ARCH=i386 mrproper >/dev/null 2>&1
        make ARCH=i386 defconfig
        
        echo "--> Re-applying code architecture patches..."
        sed -i '1i #include <limits.h>' scripts/mod/sumversion.c 2>/dev/null
        sed -i 's|if (labs(utsname()->version|if (0|g' init/version.c 2>/dev/null
        sed -i 's|extern long syscall_trace_enter|extern __attribute__((regparm(0))) long syscall_trace_enter|g' arch/x86/include/asm/ptrace.h 2>/dev/null
        sed -i 's|extern void syscall_trace_leave|extern __attribute__((regparm(0))) void syscall_trace_leave|g' arch/x86/include/asm/ptrace.h 2>/dev/null
        sed -i 's/asmregparm long syscall_trace_enter/__attribute__((regparm(0))) long syscall_trace_enter/g' arch/x86/kernel/ptrace.c 2>/dev/null
        sed -i 's/asmregparm void syscall_trace_leave/__attribute__((regparm(0))) void syscall_trace_leave/g' arch/x86/kernel/ptrace.c 2>/dev/null
    fi
}

show_menu() {
    echo "=================================================="
    echo "       LINUX 2.6.39 SANDBOX AUTOMATION SCRIPT     "
    echo "=================================================="
    echo "0) First time setup (just after git clone)"
    echo "1) Rebuild & Install Userspace (BusyBox)"
    echo "2) Recompile Kernel Only"
    echo "3) Recreate initrd.img Only (Fast Pack)"
    echo "4) Run QEMU Environment"
    echo "5) Run Full Pipeline (Compile All + Pack + Boot)"
    echo "6) Erase the last hybernation state on swap device"
    echo "7) Exit Sandbox Script"
    echo "=================================================="
}

pack_initrd() {
    echo "--> Packaging fresh initrd.img..."
    cd "$ROOTFS_DIR"
    find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 > "$INITRD_IMG"
    echo "--> Done! Image size: $(ls -lh $INITRD_IMG | awk '{print $5}')"
}

run_qemu() {
    echo "--> Launching QEMU..."
    cd "$KERNEL_DIR"
    qemu-system-i386 \
      -kernel arch/x86/boot/bzImage \
      -initrd "$INITRD_IMG" \
      -hda ../disk.img \
      -hdb ../swap.img \
      -append "console=ttyS0 root=/dev/ram0 resume=/dev/sdb no_console_suspend debug ignore_loglevel loglevel=8 earlyprintk=ttyS0,115200 initcall_debug" \
      -nographic \
      -serial stdio \
      -S \
      -gdb tcp::1234 \
      -monitor telnet:127.0.0.1:1235,server,nowait \
      -m 256
}

while true; do
    show_menu
    read -p "Select an action [1-7]: " choice
    case $choice in
        0)  verify_and_build_structures
            echo "Done the first time setup."
            ;;
        1)
            echo "--> Compiling 32-bit Static BusyBox..."
            cd "$BUSYBOX_DIR" && make -j$(nproc) && make install
            cp -av "$BUSYBOX_DIR/_install/"* "$ROOTFS_DIR/"
            pack_initrd
            ;;
        2)
            echo "--> Compiling Kernel changes..."
            cd "$KERNEL_DIR" && make ARCH=i386 -j$(nproc)
            ;;
        3)
            pack_initrd
            ;;
        4)
            run_qemu
            exit 0
            ;;
        5)
            echo "--> Running FULL Sandbox Compilation and Pack pipeline..."
            verify_and_build_structures
            cd "$BUSYBOX_DIR" && make -j$(nproc) && make install
            cp -av "$BUSYBOX_DIR/_install/"* "$ROOTFS_DIR/"
            cd "$KERNEL_DIR" && make ARCH=i386 -j$(nproc)
            pack_initrd
            run_qemu
            exit 0
            ;;
	6)
	    echo '--> Erasing the last hybernation state...'
	    dd if=/dev/zero of=swap.img bs=1M count=95
	    ;;
        7)
            echo "Exiting script. Happy hacking!"
            exit 0
            ;;
        *)
            echo "Invalid selection. Please use options 1-6."
            ;;
    esac
    echo ""
done

