git clone https://github.com/nibeshrestha/optrand.git

cd optrand

bash docker-setup.sh

sudo cp /usr/local/lib/libJerasure.* /usr/lib
sudo cp /usr/local/lib/libgf_complete.* /usr/lib

git checkout master
git pull 
cmake -DCMAKE_BUILD_TYPE=Release -DHOTSTUFF_PROTO_LOG=OFF . 

make
