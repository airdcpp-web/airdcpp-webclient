sudo setcap cap_net_bind_service=+ep `readlink -f \`which airdcppd\``

adchppd -d
screen -dm -S democlient bash -c 'airdcppd -c=/home/airdcpp/airdcpp-demo/ --web-resources=/home/airdcpp/airdcpp-webui/demo/; exec bash'
screen -dm -S shareclient bash -c 'airdcppd -c=/home/airdcpp/airdcpp-share/; exec bash'


# screen -dm -S express bash -c 'sh /home/airdcpp/airdcpp-webui/scripts/start_demo.sh; exec bash'

