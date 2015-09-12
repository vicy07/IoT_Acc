rm -rf github
mkdir github
cd ~/IoT_At_Home/github
git clone https://github.com/vicy07/IoT_At_Home
cd ~/IoT_At_Home
rsync -av --progress ~/IoT_At_Home/github/IoT_At_Home/cgateway/  ~/IoT_At_Home/cgateway --exclude config.json
cd ~/IoT_At_Home/cgateway
cd RF24
sudo make install
cd ~/IoT_At_Home/cgateway
make
chmod 711 gw.py
chmod 711 remote
chmod 711 register.py
chmod 711 init.py
chmod 711 remote.cpp
chmod 711 launcher.sh
cd ~/IoT_At_Home
cp github/IoT_At_Home/update.sh update.sh -f -u
chmod 711 update.sh

