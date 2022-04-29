FILE="${1:-/dev/stdin}"
IPS=()

PVSS_CTXT_FILE="/Users/mastran/docker/clion-volume/optrand/pvss-setup.dat"
HS_CONF_FILE="/Users/mastran/docker/clion-volume/optrand/hotstuff.conf"
HS_SEC_FILE="/Users/mastran/docker/clion-volume/optrand/hotstuff-sec${ITER}.conf"

while IFS= read -r line; do
  IPS+=($line)
done < $FILE

ITER=0
for ip in "${IPS[@]}"
do
    echo $ip
    scp -oStrictHostKeyChecking=accept-new $PVSS_CTXT_FILE ubuntu@$ip:~/optrand/ &
    scp -oStrictHostKeyChecking=accept-new $HS_CONF_FILE ubuntu@$ip:~/optrand/ &
    scp -oStrictHostKeyChecking=accept-new /Users/mastran/docker/clion-volume/optrand/hotstuff-sec${ITER}.conf ubuntu@$ip:~/optrand/ &
    scp -oStrictHostKeyChecking=accept-new /Users/mastran/docker/clion-volume/optrand/pvss-sec${ITER}.conf ubuntu@$ip:~/optrand/ &

    ITER=$(expr $ITER + 1)
done

wait

ITER=0
for ip in "${IPS[@]}"
do
    echo "starting server in " $ip
    ssh -oStrictHostKeyChecking=accept-new ubuntu@$ip "cd optrand; ./examples/hotstuff-app --conf ./hotstuff-sec${ITER}.conf > log${ITER}  2>&1" &
    ITER=$(expr $ITER + 1)
done

wait
