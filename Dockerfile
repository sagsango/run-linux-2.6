FROM ubuntu:14.04

# Update sources list safely using a different delimiter for sed to avoid forward slash errors
RUN sed -i 's|http://ubuntu.com|http://ubuntu.com|g' /etc/apt/sources.list && \
    sed -i 's|http://ubuntu.com|http://ubuntu.com|g' /etc/apt/sources.list

# Install QEMU system emulators and building prerequisites
RUN apt-get update && apt-get install -y --no-install-recommends \
    qemu-system-x86 \
    build-essential \
    libncurses5-dev \
    bc \
    bison \
    flex \
    wget \
    cpio \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /labs

CMD ["/bin/bash"]
