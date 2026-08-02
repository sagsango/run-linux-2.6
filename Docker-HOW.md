
# Build the docker
sudo docker build -t qemu-kernel26:latest .



# Enter the docker
sudo docker run -it \
  --device=/dev/kvm \
  --device=/dev/net/tun \
  --cap-add=NET_ADMIN \
  -v "$(pwd)":/labs \
  qemu-kernel26:latest

# or
sudo docker run -it   --device=/dev/kvm   --device=/dev/net/tun   --cap-add=NET_ADMIN   -v "$(pwd)":/labs   qemu-kernel26:latest


# How to poweroff the vm
poweroff -f
