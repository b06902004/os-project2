cd ./ksocket
insmod ksocket.ko

cd ../master_device
insmod master_device.ko

cd ../slave_device
insmod slave_device.ko

cd ../
