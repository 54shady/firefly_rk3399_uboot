# Usage

Compile

	make ARCH=arm CROSS_COMPILE=aarch64-linux-gnu- firefly-rk3399_defconfig all O=out

Pack uboot.img

	rkthings/loaderimage --pack --uboot out/u-boot-dtb.bin out/uboot.img 0x200000

Generate idbloader.img

	out/tools/mkimage -n rk3399 -T rksd -d rkthings/rk3399_ddr_800MHz_v1.08.bin out/idbloader.img
	cat rkthings/rk3399_miniloader_v1.06.bin >> out/idbloader.img

Generate trust.img

	cd rkthings
	./trust_merger trust.ini
	mv trust.img ../out/

Flash Image(MaskRom Mode)

	rkdeveloptool db rk3399_loader_v1.08.106.bin
	rkdeveloptool wl 0x40 idbloader.img
	rkdeveloptool wl 0x4000 uboot.img
	rkdeveloptool wl 0x6000 trust.img
	rkdeveloptool rd
