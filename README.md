# fs-anemometer

The firestorm shield anemometer. This requires the latest firestorm kernel. You can get a binary [here](https://github.com/SoftwareDefinedBuildings/pecs-fw/tree/master/kernel)

The easiest way to compile and program this is to use our docker image:

```
#some ubuntu versions will require this to let container access USB
sudo service apparmor stop

docker pull r.cal-sdb.org/fsa
docker run --privileged -it r.cal-sdb.org/fsa
# you will drop into a byobu shell
git pull
make && make install #install the anemometer firmware
sload tail #attach stdout from the firestorm
```

