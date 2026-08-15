
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
# sudo docker run -it   --device=/dev/kvm   --device=/dev/net/tun   --cap-add=NET_ADMIN   -v "$(pwd)":/labs   qemu-kernel26:latest
sudo docker run -it -p 1234:1234  --device=/dev/kvm   --device=/dev/net/tun   --cap-add=NET_ADMIN   -v "$(pwd)":/labs  qemu-kernel26:folsom-ready


# How to poweroff the vm
poweroff -f


# commit the current coainternaier in the image (after install pkg etc)
~ ❱❱❱ sudo docker ps
[sudo] password for sagar:
CONTAINER ID   IMAGE                  COMMAND       CREATED              STATUS              PORTS     NAMES
6f96689c32d5   qemu-kernel26:latest   "/bin/bash"   About a minute ago   Up About a minute             tender_leakey
~ ❱❱❱ sudo docker images
                                                                                   i Info →   U  In Use
IMAGE                  ID             DISK USAGE   CONTENT SIZE   EXTRA
qemu-kernel26:latest   717173f18c8f        535MB          137MB    U
~ ❱❱❱ sudo docker commit 6f96689c32d5 qemu-kernel26:latest
sha256:c6022483d0439c2ff4e89785e204230d41d9ec2cb9ef9ce1b99b8dce4dfffb1c
~ ❱❱❱ sudo docker images
                                                                                   i Info →   U  In Use
IMAGE                  ID             DISK USAGE   CONTENT SIZE   EXTRA
qemu-kernel26:latest   c6022483d043        612MB          162MB
~ ❱❱❱


# Upload docker image to DockerHub
run-linux-2.6 ❱❱❱ sudo docker login
[sudo] password for sagar:

USING WEB-BASED LOGIN

i Info → To sign in with credentials on the command line, use 'docker login -u <username>'


Your one-time device confirmation code is: SDCW-NXKQ
Press ENTER to open your browser or submit your device code here: https://login.docker.com/activate

Waiting for authentication in the browser…

WARNING! Your credentials are stored unencrypted in '/root/.docker/config.json'.
Configure a credential helper to remove this warning. See
https://docs.docker.com/go/credential-store/

Login Succeeded
run-linux-2.6 ❱❱❱ sudo docker tag qemu-kernel26:latest sagsango002/ubuntu_14.04_for_kernel_2.6.39:latest
run-linux-2.6 ❱❱❱ sudo docker push  sagsango002/ubuntu_14.04_for_kernel_2.6.39:latest
The push refers to repository [docker.io/sagsango002/ubuntu_14.04_for_kernel_2.6.39]
3c3e2bed3d58: Pushed
2e6e20c8e2e6: Pushed
0551a797c01d: Pushed
512123a864da: Pushed
f84e2f9b086c: Pushed
abc8fd01e3a8: Pushed
714f7889ca92: Pushed
run-linux-2.6 ❱❱❱


# pull my docker image
sudo docker pull sagsango002/ubuntu_14.04_for_kernel_2.6.39:latest
