define(){ IFS='\n' read -r -d '' ${1} || true; }
declare -A pids
redirection=( "> out" "2> err" "< /dev/null" )

define HELP <<'EOF'
Script for starting DARE
usage  : $0 [options]
options: --app                # app to run
         [--scount=INT]       # server count [default 3]
         [--ccount=INT]       # client count [default 1]
         [--rcount=INT]       # request count [default 10000]
EOF

usage () {
    echo -e "$HELP"
}

ErrorAndExit () {
  echo "ERROR: $1"
  exit 1
}

StartDare() {
    for ((i=0; i<$1; ++i));
    do
        config_dare=( "server_type=start" "server_idx=$i" "group_size=$1" "config_path=${DAREDIR}/target/nodes.local.cfg" "dare_log_file=$PWD/srv${i}.log" "mgid=$DGID" "LD_PRELOAD=${DAREDIR}/target/interpose.so" )
        cmd=( "ssh" "$USER@${servers[$i]}" "${config_dare[@]}" "nohup" "${run_dare}" "${redirection[@]}" "&" "echo \$!" )
        pids[${servers[$i]}]=$("${cmd[@]}")
        echo "StartDare COMMAND: "${cmd[@]}
    done
    echo -e "\n\tinitial servers: ${!pids[@]}"
    echo -e "\t...and their PIDs: ${pids[@]}"
}

StopDare() {
    for i in "${!pids[@]}"
    do
        cmd=( "ssh" "$USER@$i" "kill -2" "${pids[$i]}" )
        echo "Executing: ${cmd[@]}"
        $("${cmd[@]}")
    done
}

FindLeader() {
    leader=""
    max_idx=-1
    max_term=""
 
    for ((i=0; i<${server_count}; ++i)); do
        srv=${servers[$i]}
        # look for the latest [T<term>] LEADER 
        cmd=( "ssh" "$USER@$srv" "grep -r \"] LEADER\"" "$PWD/srv${i}.log" )
        #echo ${cmd[@]}
        grep_out=$("${cmd[@]}")
        if [[ -z $grep_out ]]; then
            continue
        fi
        terms=($(echo $grep_out | awk '{print $2}'))
        for j in "${terms[@]}"; do
           term=`echo $j | awk -F'T' '{print $2}' | awk -F']' '{print $1}'`
           if [[ $term -gt $max_term ]]; then 
                max_term=$term
                leader=$srv
                leader_idx=$i
           fi
        done
    done
    echo "Leader: p${leader_idx} ($leader)"
}

port=8888
StartBenchmark() {
    if [[ "$APP" == "ssdb" ]]; then
        run_loop=( "${DAREDIR}/apps/ssdb/ssdb-master/tools/ssdb-bench" "$leader" "$port" "$request_count" "$client_count")
    elif [[ "$APP" == "redis" ]]; then
        run_loop=( "${DAREDIR}/apps/redis/install/bin/redis-benchmark" "-t set,get" "-h $leader" "-p $port" "-n $request_count" "-c $client_count")
    fi
    
    cmd=( "ssh" "$USER@${client}" "${run_loop[@]}" ">" "clt.log")
    $("${cmd[@]}")
}

DAREDIR=$PWD/..
run_dare=""
server_count=3
APP=""
client_count=1
request_count=10000
for arg in "$@"
do
    case ${arg} in
    --help|-help|-h)
        usage
        exit 1
        ;;
    --scount=*)
        server_count=`echo $arg | sed -e 's/--scount=//'`
        server_count=`eval echo ${server_count}`    # tilde and variable expansion
        ;;
    --app=*)
        APP=`echo $arg | sed -e 's/--app=//'`
        APP=`eval echo ${APP}`    # tilde and variable expansion
        ;;
    --ccount=*)
        client_count=`echo $arg | sed -e 's/--ccount=//'`
        client_count=`eval echo ${client_count}`    # tilde and variable expansion
        ;;
    --rcount=*)
        request_count=`echo $arg | sed -e 's/--rcount=//'`
        request_count=`eval echo ${request_count}`    # tilde and variable expansion
        ;;
    esac
done

if [[ "x$APP" == "x" ]]; then
    ErrorAndExit "No app defined: --app"
elif [[ "$APP" == "ssdb" ]]; then
    run_dare="${DAREDIR}/apps/ssdb/ssdb-master/ssdb-server ${DAREDIR}/apps/ssdb/ssdb-master/ssdb.conf"
elif [[ "$APP" == "redis" ]]; then
    run_dare="${DAREDIR}/apps/redis/install/bin/redis-server --port $port"
elif [[ "$APP" == "memcached" ]]; then
    run_dare="${DAREDIR}/apps/memcached/install/bin/memcached -p $port"
fi


# list of allocated nodes, e.g., nodes=(n112002 n112001 n111902)
nodes=(10.22.1.3 10.22.1.4 10.22.1.5 10.22.1.6 10.22.1.7 10.22.1.8 10.22.1.9 202.45.128.159)
node_count=${#nodes[@]}
echo "Allocated ${node_count} nodes:" > nodes
for ((i=0; i<${node_count}; ++i)); do
    echo "$i:${nodes[$i]}" >> nodes
done

if [ $server_count -le 0 ]; then
    ErrorAndExit "0 < #servers; --scount"
fi

client=${nodes[-2]}
echo ">>> client: ${client}"

for ((i=0; i<${server_count}; ++i)); do
    servers[${i}]=${nodes[$i]}
done
echo ">>> ${server_count} servers: ${servers[@]}"

DGID="ff0e::ffff:e101:101"

########################################################################

echo -ne "Starting $server_count servers...\n"
StartDare $server_count
echo "done"

sleep 10
#note: wait for leader election
FindLeader
StartBenchmark

sleep 0.2
StopDare

########################################################################
