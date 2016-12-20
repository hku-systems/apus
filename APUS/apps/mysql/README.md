command to run:  
`sysbench --mysql-user=root --test=oltp --oltp-table-size=2000000 --oltp-table-name=sbtest --mysql-table-engine=InnoDB --mysql-engine-trx=yes --mysql-db=sysbench_db --mysql-socket=/var/run/mysqld/mysqld.sock prepare` 

`sudo ./run`  
  
`sysbench --mysql-host=202.45.128.160 --mysql-user=root --mysql-port=3306 --num-threads=1 --max-requests=100 --test=oltp --oltp-table-size=2000000 --oltp-table-name=sbtest --mysql-engine-trx=yes --mysql-db=sysbench_db --oltp-test-mode=complex --mysql-table-engine=InnoDB --oltp-index-updates=200 --oltp-non-index-updates=200 run`

