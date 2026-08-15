#dd if=/dev/zero of=swap.img bs=1M count=128
#dd if=/dev/zero of=swap.img bs=1M count=128
dd if=/dev/zero of=swap.img bs=1M count=95



# Initialize the swap area on the second drive
mkswap /dev/hdb

# Enable the swap space
swapon /dev/hdb

