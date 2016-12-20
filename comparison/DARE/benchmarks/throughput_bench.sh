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
declare -A cpids
declare -A data_files
redirection=( "> out" "2> err" "< /dev/null" )

define HELP <<'EOF'
Script for starting DARE's throughput benchmark
usage  : $0 [options]
options: --dare=DIR           # path to DARE bin and lib
         [--scount=INT]       # server count [default 3]
         [--ccount=INT]       # client count [default 1]   
         [--op=put|get]       # operation type [default put]
         [--bsize=(8-1024)]   # blob size [default 64]   
         [--proc=(0-100)]     # percentage of op operation [default 100]
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
    for ((i=0; i<$1; ++i));
    do
        #run_dare=( "valgrind" "--leak-check=full" "--track-origins=yes" "--log-file=$PWD/srv${i}.mem" "${DAREDIR}/bin/srv_test" "-l $PWD/srv${i}.log" "-n ${servers[$i]}" "-s $1" "-i $i" "-m $DGID" )
        run_dare=( "${DAREDIR}/bin/srv_test" "-l $PWD/srv${i}.log" "-n ${servers[$i]}" "-s $1" "-i $i" "-m $DGID" )
        cmd=( "ssh" "$USER@${servers[$i]}" "nohup" "${run_dare[@]}" "${redirection[@]}" "&" "echo \$!" )
        pids[${servers[$i]}]=$("${cmd[@]}")
        echo "COMMAND: "${cmd[@]}
    done
    echo -e "\n\tinitial servers: ${!pids[@]}"
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

StartClients() {
    # Create trace file - the same for each client
    cmd=( "rm -f $trace_file" )
    echo "Executing: ${cmd[@]}"
    ${cmd[@]}
    cmd=( "${DAREDIR}/bin/kvs_trace" "--loop" "--${OPCODE}" "-s ${blob_size}" "-o $trace_file" )
    echo "Executing: ${cmd[@]}"
    ${cmd[@]}

    for i in "${!clients[@]}"; do
        # Start client
        if [[ "x${proc}" == "x" ]]; then
            data_files[$i]="$PWD/data/loop_req_${OPCODE}_${blob_size}b_c${i}.data"
            run_loop=( "${DAREDIR}/bin/clt_test" "--loop" "-t ${trace_file}" "-o ${data_files[$i]}" "-l $PWD/clt${i}.log" "-m $DGID")
        else 
            data_files[$i]="$PWD/data/loop_req_${OPCODE}_p${proc}_${blob_size}b_c${i}.data"
            run_loop=( "${DAREDIR}/bin/clt_test" "--loop" "-t ${trace_file}" "-p $proc" "-o ${data_files[$i]}" "-l $PWD/clt${i}.log" "-m $DGID" )
        fi
        cmd=( "ssh" "$USER@${clients[$i]}" "nohup" "${run_loop[@]}" "${redirection[@]}" "&" "echo \$!" )
        cpids[${clients[$i]}]=$("${cmd[@]}")
        echo "COMMDAND: ${cmd[@]}"
        sleep 1
    done
}

StopClients() {
    #tmp=( ${!cpids[@]} )
    #IFS=$'\n' sorted_cpids=($(sort <<<"${tmp[*]}"))
    #for i in "${sorted_cpids[@]}"; do
    for i in "${clients[@]}"; do
        cmd=( "ssh" "$USER@$i" "kill -s SIGINT" "${cpids[$i]}" )
        echo "Executing: ${cmd[@]}"
        $("${cmd[@]}")
    done
}

DAREDIR=""
OPCODE="put"
server_count=3
client_count=1
blob_size=64
proc=100
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
    --ccount=*)
        client_count=`echo $arg | sed -e 's/--ccount=//'`
        client_count=`eval echo ${client_count}`    # tilde and variable expansion
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
    --proc=*)
        proc=`echo $arg | sed -e 's/--proc=//'`
        proc=`eval echo ${proc}`    # tilde and variable expansion
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

if [ $server_count -le 0 ]; then
    ErrorAndExit "0 < #servers; --scount"
fi
if [ $client_count -le 0 ]; then
    ErrorAndExit "0 < #clients; --ccount"
fi
if [ $((server_count+client_count)) -gt $node_count ] ; then
    ErrorAndExit "#servers + #clients <= ${node_count}; --scount & --ccount"
fi

for ((i=0; i<${client_count}; ++i)); do
    clients[$i]=${nodes[$i]}
done
echo ">>> ${client_count} clients: ${clients[@]}"

for ((i=${client_count}; i<$((server_count+client_count)); ++i)); do
    servers[$((i-client_count))]=${nodes[$i]}
done
echo ">>> ${server_count} servers: ${servers[@]}"

# OP code
if [ "x$OPCODE" != "xput" -a "x$OPCODE" != "xget" ]; then
    ErrorAndExit "Wrong operation type: --op."
fi
rm -rf data
rm -f *.log
mkdir -p data
trace_file="$PWD/data/loop_req_${OPCODE}_${blob_size}b.trace"

# Handle SIGINT to ensure a clean exit
trap 'echo -ne "Stop all servers..." && StopClients && StopDare && echo "done" && exit 1' INT

# mckey program is used to generate a dgid that provides the required multicast address
echo 'executing mckey, please wait ...'
MCKEY_M=`ip addr show ib0 | grep 'inet ' | cut -d: -f2 | awk '{ print $2}'|cut -d/ -f1`
mckey -m $MCKEY_M > mckey_dump &
mckey -m $MCKEY_M -s > /dev/null
DGID=`cat mckey_dump | grep 'dgid'| cut -d " " -f4`
echo 'Extraction of dgid from mckey finished ... '

########################################################################

echo -ne "Starting $server_count servers...\n"
StartDare $server_count
echo "done"

sleep 2

# Write entry in the SM
tmp_tfile="$PWD/tmp.trace"
tmp_dfile="$PWD/tmp.data"
cmd=( "${DAREDIR}/bin/kvs_trace" "--loop" "--put" "-s ${blob_size}" "-o ${tmp_tfile}" )
${cmd[@]}
cmd=( "${DAREDIR}/bin/clt_test" "--trace" "-t $tmp_tfile" "-o $tmp_dfile" "-l write.log" "-m $DGID" )
${cmd[@]}
rm ${tmp_tfile} ${tmp_dfile}

StartClients
StopClients

sleep 0.2
StopDare

########################################################################


