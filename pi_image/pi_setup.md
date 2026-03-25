# QNX Raspberry Pi 4 Setup Guide

This guide explains how to take our prebuilt QNX image and boot it on the Raspberry Pi 4. 

## 1. Prepare the MicroSD Card
The Raspberry Pi's internal bootloader looks for a **FAT32** formatted partition to load its initial firmware and the OS. 
* Insert your MicroSD card into your computer.
* Format the entire card to **FAT32** (If you are on Linux, you can use `gparted` or `sudo mkfs.fat -F 32 /dev/sdX1`).

## 2. Copy the Boot Files
Unlike standard Linux distributions (.img files) that require specialized flashing tools, QNX on the Pi just needs the raw firmware and the image file sitting on the root of the drive.
* Open the `prebuilt_image/` directory.
* Copy **all 5 files** directly to the root of your FAT32 MicroSD card:
  * `bcm2711-rpi-4-b.dtb` (Device Tree Blob for the Pi 4 hardware)
  * `config.txt` (Pi boot configuration, tells the Pi to load the QNX ifs)
  * `fixup4.dat` (Memory allocation firmware)
  * `start4.elf` (The Pi's GPU firmware and bootloader)
  * `ifs-rpi4.bin` (Our actual compiled QNX OS and filesystem)

## 3. Boot the Pi
1. Safely eject the MicroSD card and insert it into the Raspberry Pi 4.
2. Connect wifi network called COMPE4900E_Group7 with password qnxuser123
4. Run `ssh qnxuser@192.168.7.2` and type the password `qnxuser`. 
5. You are now ssh'd in! Run `cd /` and you can see what files are there.

---

### File Reference: `rpi4.build`
The `rpi4.build` file sitting next to the `prebuilt_image/` folder is the QNX recipe script. It dictates exactly which drivers, binaries, and libraries get packed into the `ifs-rpi4.bin` file. 

**You only need to touch `rpi4.build` if you are modifying the core OS image.** If you are just testing our compiled C++ applications, boot the prebuilt image and run the binaries directly.