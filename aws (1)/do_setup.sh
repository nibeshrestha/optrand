# Do the setup on the AWS Server

FILE="${1:-/dev/stdin}"

IPS=()

# Create Private IP files
#bash scripts/aws/get_pvt_ips.sh < $FILE \
#> $PVT_IP_FILE

while IFS= read -r line; do
  IPS+=($line)
done < $FILE

for ip in "${IPS[@]}"
do
    echo $ip
    ssh -oStrictHostKeyChecking=accept-new -t ubuntu@$ip 'bash -ls' < setup.sh &
done

wait

#for ip in "${IPS[@]}"
#do
#  ssh arch@$ip "cd optrand-rs; cat > ips_file" < $IPS_FILE
#  ssh arch@$ip "cd optrand-rs; cat > cli_ip_file" < $CLI_IPS_FILE
#done
