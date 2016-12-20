var config = { _id : "replicaset", members : [ {_id : 0, host : '127.0.0.1:26017', priority : 2}, {_id : 1, host : '127.0.0.1:26018', priority : 1}, {_id : 2, host : '127.0.0.1:26019', priority : 1}, ] }
rs.initiate(config);  
