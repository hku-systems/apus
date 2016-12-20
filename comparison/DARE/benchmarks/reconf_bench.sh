#!/bin/bash
#
# Copyright (c) 2016 HLRS, University of Stuttgart. All rights reserved.
# 
# Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
# 
# Author(s): Marius Poke <marius.poke@inf.ethz.ch>
#            Nakul Vyas <mailnakul@gmail.com>
#
# Brief: group reconfiguration benchmark (requires 8 nodes)

define(){ IFS='\n' read -r -d '' ${1} || true; }
declare -A pids
declare -A cpids
declare -A rounds
declare -A data_files
redirection=( "> out" "2> err" "< /dev/null" )

define HELP <<'EOF'
Script for starting DARE's group reconfiguration benchmark
usage  : $0 [options]
options: --dare=DIR           # path to DARE bin and lib
         [--op=put|get]       # operation type [default put]
         [--bsize=(8-1024)]   # blob size [default 64]   
         
EOF

usage () {
    echo -e "$HELP"
}

ErrorAndExit () {
  echo "ERROR: $1"
  exit 1
}

ForceAbsolutePath () {
  case "$2" in
    /* )
      ;;
    *)
      ErrorAndExit "Expected an absolute path for $1"
      ;;
  esac
}

StartDare() {
    for ((i=0; i<${group_size}; ++i)); do
        srv=${servers[$i]}
        run_dare=( "${DAREDIR}/bin/srv_test" "-l $PWD/srv${i}_1.log" "-n $srv" "-s ${group_size}" "-i $i" "-m $DGID" )
        cmd=( "ssh" "$USER@$srv" "nohup" "${run_dare[@]}" "${redirection[@]}" "&" "echo \$!" )
        pids[$srv]=$("${cmd[@]}")
        rounds[$srv]=2
        #echo "COMMAND: "${cmd[@]}
        echo -e "\tp$i ($srv) -- pid=${pids[$srv]}"
    done
    #echo -e "\n\tinitial servers: ${!servers[]}${!pids[@]}"
    #echo -e "\t...and their PIDs: ${pids[@]}"
}

StopDare() {
    for srv in "${!pids[@]}"; do
        cmd=( "ssh" "$USER@$srv" "kill -s SIGINT" "${pids[$srv]}" )
        echo "Executing: ${cmd[@]}"
        $("${cmd[@]}")
    done
}

FindLeader() {
    leader=""
    max_idx=-1
    max_term=""
 
    for ((i=0; i<${group_size}; ++i)); do
        srv=${servers[$i]}
        # look for the latest [T<term>] LEADER 
        cmd=( "ssh" "$USER@$srv" "grep -r \"] LEADER\"" "$PWD/srv${i}_$((rounds[$srv]-1)).log" )
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
    #echo "Leader: p${leader_idx} ($leader)"
}

RemoveLeader() {
    FindLeader
    if [[ -z $leader ]]; then
        echo -e "\n\tNo leader [$leader]"
        return 1
    fi
    #echo ${!pids[@]}
    #echo ${pids[@]}
    if [[ -z ${pids[$leader]} ]]; then
        echo -e "\n\tNo PID for the leader $leader"
        return 1
    fi
    cmd=( "ssh" "$USER@$leader" "kill -s SIGINT" "${pids[$leader]}" )
    $("${cmd[@]}")
    unset pids[$leader]
    echo -e "\tremoved p${leader_idx} ($leader)"
    #echo -e "\n\tservers after removing the leader p${leader_idx} ($leader): ${!pids[@]}"
    #echo -e "\t...and their PIDs: ${pids[@]}"
    #echo ${cmd[@]}
    maj=$(bc -l <<< "${group_size}/2.")
    if [[ ${#pids[@]} < $maj ]]; then
        ErrorAndExit "...not enough servers!"
    fi
    return 0
}

# Stop a server that is not the leader
RemoveServer() {
    FindLeader
    for ((i=0; i<${group_size}; ++i)); do
        srv=${servers[$i]}
        if [[ "x$srv" == "x$leader" ]]; then
            continue
        fi
        if [[ "x${pids[$srv]}" == "x" ]]; then
            continue
        fi
        cmd=( "ssh" "$USER@$srv" "kill -s SIGINT" "${pids[$srv]}" )
        #echo -e "\tcmd: ${cmd[@]}"
        $("${cmd[@]}")
        unset pids[$srv]
        echo -e "\tremoved p$i ($srv) -- p$leader_idx is the leader"
        #echo -e "\tservers after removing p$i ($srv): ${!pids[@]}"
        #echo -e "\t...and their PIDs: ${pids[@]}"
        #echo ${cmd[@]}
        break
    done
    maj=$(bc -l <<< "${group_size}/2.")
    if [[ ${#pids[@]} < $maj ]]; then
        ErrorAndExit "...not enough servers!"
    fi
}

AddServer() {
    if [[ ${#pids[@]} == $group_size ]]; then
        # the group is full
        group_size=$((group_size+2))
    fi
    for ((i=0; i<${group_size}; ++i)); do
        srv=${servers[$i]}
        next=0
        for j in "${!pids[@]}"; do 
            if [[ "x$srv" == "x$j" ]]; then
               next=1
               break
            fi
        done
        if [[ $next == 1 ]]; then
            continue
        fi
        break
    done
    if [[ "x${rounds[$srv]}" == "x" ]]; then
        rounds[$srv]=1
    fi
    run_dare=( "${DAREDIR}/bin/srv_test" "-l $PWD/srv${i}_${rounds[$srv]}.log" "--join" "-n $srv" "-m $DGID" )
    cmd=( "ssh" "$USER@$srv" "nohup" "${run_dare[@]}" "${redirection[@]}" "&" "echo \$!" )
    pids[$srv]=$("${cmd[@]}")
    rounds[$srv]=$((rounds[$srv] + 1))
    #echo "COMMAND: "${cmd[@]}
    echo -e "\tadded p$i ($srv)"
    #echo -e "\n\tservers after adding p$i ($srv): ${!pids[@]}"
    #echo -e "\t...and their PIDs: ${pids[@]}"
}

# Resize the group to $1
DareGroupResize() {
    if [ $1 -gt $group_size ]; then
        ErrorAndExit "To increase the group size, add a server."
    fi
    cmd=( "${DAREDIR}/bin/clt_test" "--reconf" "-s $1" "-m $DGID")
    #echo -e "\tCMD: ${cmd[@]}"
    ${cmd[@]}
    if [ $1 -lt $group_size ]; then 
        # downsize: unset pids of removed servers
        for ((i=$1; i<$group_size; ++i)); do
            unset pids[${servers[$i]}]
            echo -e "\tremoved p$i (${server[$i]})"
        done        
    fi
    group_size=$1
    #echo -e "\n\tservers after resizing: ${!pids[@]}"
    #echo -e "\t...and their PIDs: ${pids[@]}"
}

StartLoop() {
    cmd=( "rm -f $trace_file" )
    ${cmd[@]}
    echo -e "\tblobsize: $blob_size bytes"
    cmd=( "${DAREDIR}/bin/kvs_trace" "--loop" "--$OPCODE" "-s $blob_size" "-o $trace_file" )
    #echo "Executing: ${cmd[@]}"
    ${cmd[@]}
    run_loop=( "${DAREDIR}/bin/clt_test" "--loop" "-t $trace_file" "-o $data_file" "-l $PWD/clt.log" "-m $DGID")
    cmd=( "ssh" "$USER@$client" "nohup" "${run_loop[@]}" "${redirection[@]}" "&" "echo \$!" )
    #echo "COMMAND: ${cmd[@]}"
    client_pid=$("${cmd[@]}")
}

StopLoop() {
    cmd=( "ssh" "$USER@$client" "kill -s SIGINT" "$client_pid" )
    echo "Executing: ${cmd[@]}"
    $("${cmd[@]}")
}

DAREDIR=""
blob_size=64
OPCODE="put"
for arg in "$@"
do
    case ${arg} in
    --help|-help|-h)
        usage
        exit 1
        ;;
    --dare=*)
        DAREDIR=`echo $arg | sed -e 's/--dare=//'`
        DAREDIR=`eval echo ${DAREDIR}`    # tilde and variable expansion
        ForceAbsolutePath "--dare" "${DAREDIR}"
        ;;
    --op=*)
        OPCODE=`echo $arg | sed -e 's/--op=//'`
        OPCODE=`eval echo ${OPCODE}`    # tilde and variable expansion
        ;;
    --bsize=*)
        blob_size=`echo $arg | sed -e 's/--bsize=//'`
        blob_size=`eval echo ${blob_size}`    # tilde and variable expansion
        ;;
    esac
done

if [[ "x$DAREDIR" == "x" ]]; then
    ErrorAndExit "No DARE folder defined: --dare."
fi

# list of allocated nodes, e.g., nodes=(n112002 n112001 n111902)
nodes=(`cat $PBS_NODEFILE | tr ' ' '\n' | awk '!u[$0]++'`)
node_count=${#nodes[@]}
if [ $node_count -lt 8 ]; then
    ErrorAndExit "At least 8 nodes are required."
fi
echo "Allocated ${node_count} nodes:" > nodes
for ((i=0; i<${node_count}; ++i)); do
    echo "$i:${nodes[$i]}" >> nodes
done
group_size=5

client=${nodes[0]}
echo ">>> 1 client: ${client}"

for ((i=1; i<$node_count; ++i)); do
    servers[$((i-1))]=${nodes[$i]}
done
echo ">>> $(($node_count-1)) servers: ${servers[@]}"

# OP code
if [ "x$OPCODE" != "xput" -a "x$OPCODE" != "xget" ]; then
    ErrorAndExit "Wrong operation type: --op."
fi
rm -rf data
rm -f *.log
mkdir -p data
data_file="$PWD/data/loop_${OPCODE}_${blob_size}.data"
trace_file="$PWD/data/loop_${OPCODE}_${blob_size}.trace"

# Handle SIGINT to ensure a clean exit
trap 'echo -ne "Stop all servers...\n" && StopLoop && StopDare && echo "done" && exit 1' INT

# mckey program is used to generate a dgid that provides the required multicast address
echo 'executing mckey, please wait ...'
MCKEY_M=`ip addr show ib0 | grep 'inet ' | cut -d: -f2 | awk '{ print $2}'|cut -d/ -f1`
mckey -m $MCKEY_M > mckey_dump &
mckey -m $MCKEY_M -s > /dev/null
DGID=`cat mckey_dump | grep 'dgid'| cut -d " " -f4`
echo 'Extraction of dgid from mckey finished ... '

########################################################################

Stop() {
    sleep 0.5
    StopLoop
    sleep 0.2
    StopDare
    exit 1
}

Start() {
    echo -e "Starting $group_size servers..."
    StartDare
    echo "done"

    sleep 2

    echo -e "Starting client..."
    StartLoop
    echo "done"

    sleep 0.5
    
    if [[ "x$1" == "xstop" ]]; then
        Stop
    fi    
}

FailLeader() {
    echo -e "Removing the leader..."
    while true; do
        RemoveLeader
        ret=$?
        #echo "ret=$ret"
        if [ $ret -eq 0 ]; then 
            break;    
        fi
        sleep 0.05
    done
    echo "done"

    sleep 1
    
    if [[ "x$1" == "xstop" ]]; then
        Stop
    fi  
}

RecoverServer() {
    echo -e "Adding a server..."
    AddServer
    echo "done"

    sleep 0.5
    
    if [[ "x$1" == "xstop" ]]; then
        Stop
    fi
}

Upsize() {
    echo -e "Adding a server (upsize)..."
    AddServer
    echo "done"

    sleep 0.3
    
    if [[ "x$1" == "xstop" ]]; then
        Stop
    fi
}

Downsize() {
    size=$((group_size - 2))
    echo -e "Resize group from $group_size to $size..."
    DareGroupResize $size
    echo "done"

    sleep 0.3
    
    if [[ "x$1" == "xstop" ]]; then
        Stop
    fi
}

FailServer() {
    echo -e "Removing a server (non-leader)..."
    RemoveServer
    echo "done"

    sleep 0.7
    
    if [[ "x$1" == "xstop" ]]; then
        Stop
    fi
}

########################################################################

# Start DARE (size = 5)
Start

# Remove the leader (size = 6)
#FailLeader 

#sleep 1

#Stop

# Upsize (size = 6)
Upsize

# Upsize (size = 7)
Upsize 

#Stop

# Remove the leader (size = 6)
FailLeader


# Remove a server that is not the leader (size = 5)
FailServer 

# Add a server (size = 6)
RecoverServer

# Add a server (size = 7)
RecoverServer 

# Downsize (size = 5)
Downsize 

# Remove the leader (size = 4)
FailLeader 

# Add a server (size = 5)
RecoverServer

# Downsize (size = 3)
Downsize stop

########################################################################


