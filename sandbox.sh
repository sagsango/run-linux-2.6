#!/bin/bash

# --- Paths Configuration ---
BASE_DIR="/labs"
KERNEL_DIR="$BASE_DIR/linux-2.6.39"
BUSYBOX_DIR="$BASE_DIR/busybox-1.35.0"
ROOTFS_DIR="$BASE_DIR/rootfs"
INITRD_IMG="$BASE_DIR/initrd_busybox.img"

show_menu() {
    echo "=================================================="
    echo "       LINUX 2.6.39 SANDBOX AUTOMATION SCRIPT     "
    echo "=================================================="
    echo "1) Rebuild & Install Userspace (BusyBox)"
    echo "2) Recompile Kernel Only"
    echo "3) Recreate initrd.img Only (Fast Pack)"
    echo "4) Run QEMU Environment"
    echo "5) Run Full Pipeline (Compile All + Pack + Boot)"
    echo "6) Exit Sandbox Script"
    echo "=================================================="
}

pack_initrd() {
    echo "--> Packaging fresh initrd.img..."
    if [ ! -d "$ROOTFS_DIR" ]; then
        echo "Error: rootfs directory missing. Run Busybox installation first."
        return 1
    fi
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
      -append "console=ttyS0 quiet root=/dev/ram0" \
      -nographic \
      -m 256
}

while true; do
    show_menu
    read -p "Select an action [1-6]: " choice
    case $choice in
        1)
            echo "--> Compiling 32-bit Static BusyBox..."
            cd "$BUSYBOX_DIR"
            make -j$(nproc) && make install
            echo "--> Synchronizing staging rootfs directory..."
            mkdir -p "$ROOTFS_DIR"
            cp -av "$BUSYBOX_DIR/_install/"* "$ROOTFS_DIR/"
            mkdir -p "$ROOTFS_DIR"/{dev,proc,sys,etc,root}
            [ ! -c "$ROOTFS_DIR/dev/console" ] && mknod -m 600 "$ROOTFS_DIR/dev/console" c 5 1
            [ ! -c "$ROOTFS_DIR/dev/null" ] && mknod -m 666 "$ROOTFS_DIR/dev/null" c 1 3
            chmod +x "$ROOTFS_DIR/init" 2>/dev/null || true
            ;;
        2)
            echo "--> Compiling Kernel changes..."
            cd "$KERNEL_DIR"
            make ARCH=i386 -j$(nproc)
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
            cd "$BUSYBOX_DIR" && make -j$(nproc) && make install
            mkdir -p "$ROOTFS_DIR" && cp -av "$BUSYBOX_DIR/_install/"* "$ROOTFS_DIR/"
            cd "$KERNEL_DIR" && make ARCH=i386 -j$(nproc)
            pack_initrd
            run_qemu
	    exit 0
            ;;
        6)
            echo "Exiting script. Happy hacking!"
            exit 0
            ;;
        *)
            echo "Invalid selection. Please use options 1-6."
            ;;
    esac
    echo ""
done
