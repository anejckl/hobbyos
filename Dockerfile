FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install cross-compiler toolchain + GRUB tools + NASM + QEMU
RUN apt-get update && apt-get install -y \
    nasm \
    grub-pc-bin \
    grub-common \
    xorriso \
    mtools \
    gcc \
    binutils \
    make \
    qemu-system-x86 \
    e2fsprogs \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /hobbyos
