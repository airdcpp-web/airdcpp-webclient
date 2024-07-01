adchppd -d
screen -dm -S democlient bash -c 'airdcppd -c=/home/airdcpp/airdcpp-demo/; exec bash'
screen -dm -S shareclient bash -c 'airdcppd -c=/home/airdcpp/airdcpp-share/; exec bash'


