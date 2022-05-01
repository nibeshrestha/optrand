#!/bin/bash

shopt -s extglob

IP_FILE=${1:-"ip.txt"}
NUM_NODES=${2:-5}

IPS=()
while IFS= read -r line; do
  IPS+=($line)
done < "$IP_FILE"

rsync -avrz \
	-e "ssh -i ~/.ssh/optrand_key" \
	"${IP_FILE}" \
	"ubuntu@${IPS[0]}:~/optrand/ip.txt"

ssh -i ~/.ssh/optrand_key \
	ubuntu@"${IPS[0]}" \
	"cd optrand && \
	(./pvss-setup --num ${NUM_NODES} || true) && \
	python scripts/gen_conf.py --iter 1 --prefix hotstuff --ips ip.txt\
	"

rsync -avzr \
	-e "ssh -i ~/.ssh/optrand_key" \
	"ubuntu@${IPS[0]}:~/optrand/pvss-setup.dat" \
	"ubuntu@${IPS[0]}:~/optrand/hotstuff.conf" \
	"ubuntu@${IPS[0]}:~/optrand/hotstuff-sec*" \
	"ubuntu@${IPS[0]}:~/optrand/pvss-sec*" \
	.

for ip in ${IPS[@]}
do 
	rsync -avzr \
		-e "ssh -i ~/.ssh/optrand_key" \
		"pvss-setup.dat" \
		"hotstuff.conf" \
		./hotstuff-sec* \
		./pvss-sec* \
		"ubuntu@${ip}:~/optrand/" &
done

wait
