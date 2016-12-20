#!/bin/sh

AIM="guard_dev"
cd $AIM
sh ./sync_so.sh
cd ../

rm -rf $AIM/.db
rm -rf $AIM/*.log
rsync -aP --delete  ./$AIM hkucs-poweredge-r430-1:/tmp/
rsync -aP --delete  ./$AIM hkucs-poweredge-r430-2:/tmp/
rsync -aP --delete  ./$AIM hkucs-poweredge-r430-3:/tmp/
rsync -a ./$AIM/node_id.0 hkucs-poweredge-r430-1:/tmp/$AIM/node_id
rsync -a ./$AIM/node_id.1 hkucs-poweredge-r430-2:/tmp/$AIM/node_id
rsync -a ./$AIM/node_id.2 hkucs-poweredge-r430-3:/tmp/$AIM/node_id
