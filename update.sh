rm -rf github
mkdir github
cd github
git clone https://github.com/vicy07/IoT_At_Home
rsync -av --progress github/IoT_At_Home/cgateway/ cgateway --exclude config.json
cd ..
cd cgateway
cd RF24
sudo make install
cd ~/IoT_Acc/cgateway
make
chmod 711 gw.py
chmod 711 remote
chmod 711 register.py
chmod 711 init.py
chmod 711 remote.cpp
chmod 711 launcher.sh
cd ~/IoT_Acc/
cp github/IoT_At_Home/update.sh update.sh -f -u
chmod 711 update.sh

