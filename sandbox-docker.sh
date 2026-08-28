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

    # 5. Fix missing BusyBox configurations if cloned fresh (Updated for x86_64)
    if [ ! -f "$BUSYBOX_DIR/.config" ]; then
        echo "--> Reconfiguring BusyBox compiler for x86_64..."
        cd "$BUSYBOX_DIR" && make distclean && make ARCH=x86_64defconfig
        sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
        # Cleaned out -m32 and i386 flags to compile as native 64-bit
        sed -i 's/CONFIG_EXTRA_CFLAGS=""/CONFIG_EXTRA_CFLAGS=""/' .config
        sed -i 's/CONFIG_EXTRA_LDFLAGS=""/CONFIG_EXTRA_LDFLAGS=""/' .config
    fi

    # 6. Fix missing Kernel configurations or hardcoded paths from the old workspace (Updated for x86_64)
    if [ ! -f "$KERNEL_DIR/.config" ] || [ ! -f "$KERNEL_DIR/scripts/basic/Makefile" ]; then
        echo "--> Re-initializing Kernel layout definitions for x86_64..."
        cd "$KERNEL_DIR"
        make ARCH=x86_64 mrproper >/dev/null 2>&1
        make ARCH=x86_64 defconfig

        echo "--> Re-applying code architecture patches..."
        sed -i '1i #include <limits.h>' scripts/mod/sumversion.c 2>/dev/null
        sed -i 's|if (labs(utsname()->version|if (0|g' init/version.c 2>/dev/null
        
        # NOTE: Dropped the 32-bit specific __attribute__((regparm(0))) ptrace modifications.
        # x86_64 strictly passes arguments via registers (rdi, rsi, rdx...) by default, 
        # making the old x86_32 stack-forcing regparm patches obsolete and prone to compilation errors.
    fi
}



show_menu() {
    echo "=================================================="
    echo "       LINUX 2.6.39 SANDBOX AUTOMATION SCRIPT     "
    echo "=================================================="
    echo "0) First time setup (just after git clone)"
    echo "1) Rebuild & Install Userspace (BusyBox)"
    echo "2) make menufoncig"
    echo "3) Recompile Kernel Only"
    echo "4) Recreate initrd.img Only (Fast Pack)"
    echo "5) Run QEMU Environment"
    echo "6) Run Full Pipeline (Compile All + Pack + Boot)"
    echo "7) Erase the last hybernation state on swap device"
    echo "8) Exit Sandbox Script"
    echo "=================================================="
}

pack_initrd() {
    echo "--> Packaging fresh initrd.img..."
    cd "$ROOTFS_DIR"
    find . -print0 | cpio --null -ov --format=newc 2>/dev/null | gzip -9 > "$INITRD_IMG"
    find "$KERNEL_DIR" -name "*.ko" -exec cp {} "$ROOTFS_DIR"/lib/modules/ \;
    echo "--> Done! Image size: $(ls -lh $INITRD_IMG | awk '{print $5}')"
}

run_qemu() {
    echo "--> Launching QEMU..."
    cd "$KERNEL_DIR"
    qemu-system-x86_64 \
      -machine pc \
      -smp 4 \
      -kernel arch/x86/boot/bzImage \
      -initrd "$INITRD_IMG" \
      -hda ../disk.img \
      -hdb ../swap.img \
      -append "console=ttyS0 root=/dev/ram0 numa=on numa=fake=4 apic=bigsmp resume=/dev/sdb no_console_suspend debug ignore_loglevel loglevel=8 earlyprintk=ttyS0,115200 initcall_debug root=/dev/sda1 console=ttyS0 init=/bin/sh" \
      -nographic \
      -serial stdio \
      -monitor telnet:127.0.0.1:1235,server,nowait \
      -m 2G \
      -numa node,nodeid=0,cpus=0,mem=512 \
      -numa node,nodeid=1,cpus=1,mem=512 \
      -numa node,nodeid=2,cpus=2,mem=512 \
      -numa node,nodeid=3,cpus=3,mem=512
      #-S \
      #-gdb tcp::1234

}

while true; do
    show_menu
    read -p "Select an action [1-7]: " choice
    case $choice in
        0)  verify_and_build_structures
            echo "Done the first time setup."
            ;;
        1)
            # Updated to explicitly print 64-bit compilation target
            echo "--> Compiling 64-bit Static BusyBox..."
            cd "$BUSYBOX_DIR" && make ARCH=x86_64 -j$(nproc) && make install
            cp -av "$BUSYBOX_DIR/_install/"* "$ROOTFS_DIR/"
            ;;
        2)
            cd "$KERNEL_DIR" && make ARCH=x86_64 menuconfig
            ;;
        3)
            echo "--> Compiling Kernel changes..."
            cd "$KERNEL_DIR" && make ARCH=x86_64 -j$(nproc)
            cd "$KERNEL_DIR" && make ARCH=x86_64 INSTALL_MOD_PATH="$ROOTFS_DIR" modules_install
            ;;
        4)
            pack_initrd
            ;;
        5)
            run_qemu
            exit 0
            ;;
        6)
            echo "--> Running FULL Sandbox Compilation and Pack pipeline..."
            verify_and_build_structures
            cd "$BUSYBOX_DIR" && make -j$(nproc) && make install
            cp -av "$BUSYBOX_DIR/_install/"* "$ROOTFS_DIR/"
            cd "$KERNEL_DIR" && make ARCH=x86_64 -j$(nproc)
            cd "$KERNEL_DIR" && make ARCH=x86_64 INSTALL_MOD_PATH="$ROOTFS_DIR" modules_install
            pack_initrd
            run_qemu
            exit 0
            ;;
        7)
            echo '--> Erasing the last hibernation state...'
            dd if=/dev/zero of=swap.img bs=1M count=95
            ;;
        8)
            echo "Exiting script. Happy hacking!"
            exit 0
            ;;
        *)
            # Fixed the prompt notification mismatch to explicitly include all options [0-8]
            echo "Invalid selection. Please use options 0-8."
            ;;
    esac
    echo ""
done
