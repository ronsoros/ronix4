cp $KOUT rootfs; for i in $(ls app/*.bin); do cp $i rootfs/bin/$(sed 's/.bin//g' <<< $(basename $i)); done; cd rootfs; start $KOUT bin/init
