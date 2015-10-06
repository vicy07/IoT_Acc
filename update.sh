sudo rm -rf github
mkdir github
cd ~/IoT/github
git clone https://github.com/vicy07/IoT_At_Home
cd ~/IoT
rsync -av --progress ~/IoT/github/IoT_At_Home/Gateway/  ~/IoT/Gateway --exclude config.json
cd ~/IoT/Gateway
chmod 711 smart_gateway.py
cd ~/IoT
cp github/IoT_At_Home/update.sh update.sh -f -u
chmod 711 update.sh

