#!/bin/bash
#
# Copyright (c) 2016 HLRS, University of Stuttgart. All rights reserved.
# 
# Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
# 
# Author(s): Marius Poke <marius.poke@inf.ethz.ch>
#            Nakul Vyas <mailnakul@gmail.com>
#

define(){ IFS='\n' read -r -d '' ${1} || true; }
declare -A pids
redirection=( "> out" "2> err" "< /dev/null" )

define HELP <<'EOF'
Script for starting DARE's latency benchmark
usage  : $0 [options]
options: --dare=DIR                 # path to DARE bin and lib
         [--scount=INT]             # server count [default 3]
         [--op=put|get]             # operation type [default put]
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
    # The client starts directly (not through ssh); thus, since we ran in interactive mode, 
    # the client must be on the local node; the servers are on the other nodes
    for ((i=0, j=0; i<$1; j++)); do
        if [[ "x${nodes[$j]}" == "x$HOSTNAME" ]]; then
            continue
        fi
        run_dare=( "${DAREDIR}/bin/srv_test" "-l $PWD/srv${i}.log" "-n ${nodes[$j]}" "-s $1" "-i $i" "-m $DGID" )
        cmd=( "ssh" "$USER@${nodes[$j]}" "nohup" "${run_dare[@]}" "${redirection[@]}" "&" "echo \$!" )
        pids[${nodes[$j]}]=$("${cmd[@]}")
        echo -e "COMMAND: "${cmd[@]}
        i=$((i+1))
    done
    echo -e "\tinitial nodes: ${!pids[@]}"
    echo -e "\t...and their PIDs: ${pids[@]}"
}

StopDare() {
    for i in "${!pids[@]}"
    do
        cmd=( "ssh" "$USER@$i" "kill -s SIGINT" "${pids[$i]}" )
        echo "Executing: ${cmd[@]}"
        $("${cmd[@]}")
    done
}

DAREDIR=""
OPCODE="put"
server_count=3

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
    --dare=*)
        DAREDIR=`echo $arg | sed -e 's/--dare=//'`
        DAREDIR=`eval echo ${DAREDIR}`    # tilde and variable expansion
        ForceAbsolutePath "--dare" "${DAREDIR}"
        ;;
    --op=*)
        OPCODE=`echo $arg | sed -e 's/--op=//'`
        OPCODE=`eval echo ${OPCODE}`    # tilde and variable expansion
        ;;
    esac
done

if [[ "x$DAREDIR" == "x" ]]; then
    ErrorAndExit "No DARE folder defined: --dare."
fi

# list of allocated nodes, e.g., nodes=(n112002 n112001 n111902)
nodes=(`cat $PBS_NODEFILE | tr ' ' '\n' | awk '!u[$0]++'`)
node_count=${#nodes[@]}
echo "Allocated ${node_count} nodes:" > nodes
for ((i=0; i<${node_count}; ++i)); do
    echo "$i:${nodes[$i]}" >> nodes
done

if [ $server_count -gt $(($node_count-1)) -o $server_count -le 0 ] ; then
    ErrorAndExit "0 < #servers <= ${node_count}; --scount"
fi

# OP code
if [ "x$OPCODE" != "xput" -a "x$OPCODE" != "xget" ]; then
    ErrorAndExit "Wrong operation type: --op."
fi
mkdir -p data
data_file="$PWD/data/trace_lat_${OPCODE}_g${server_count}.data"
trace_file="$PWD/data/trace_lat_${OPCODE}_g${server_count}.trace"
rm -f *.log

# Handle SIGINT to ensure a clean exit
trap 'echo -ne "Stop all servers..." && StopDare && echo "done" && exit 1' INT

# mckey program is used to generate a dgid that provides the required multicast address
echo 'executing mckey, please wait ...'
MCKEY_M=`ip addr show ib0 | grep 'inet ' | cut -d: -f2 | awk '{ print $2}'|cut -d/ -f1`
mckey -m $MCKEY_M > mckey_dump &
mckey -m $MCKEY_M -s > /dev/null
DGID=`cat mckey_dump | grep 'dgid'| cut -d " " -f4`
echo 'Extraction of dgid from mckey finished ... '


########################################################################

echo "Starting $server_count servers..."
StartDare $server_count
echo "done!"

sleep 5

#sleep 8  
#note:this sleep statement here is cause of some problems;hence, comented out    

cmd=( "rm -f $trace_file" )
${cmd[@]}

cmd=( "${DAREDIR}/bin/kvs_trace" "--trace" "--${OPCODE}" "-o $trace_file" )
echo "Executing: ${cmd[@]}"
${cmd[@]}

cmd=( "${DAREDIR}/bin/clt_test" "--rtrace" "-t $trace_file" "-o $data_file" "-l clt.log" "-m $DGID")
echo "Executing: ${cmd[@]}"
${cmd[@]}

StopDare

########################################################################


