cd /home/ec2-user/virtuoso-opensource
. ./.profile
cd /home/ec2-user/virtuoso-opensource/binsrc/tests/tpc-h
export WO_START=1
export DSN=1201
export PORT=1201
./run.sh 1000 7 3
cp report?.txt /home/tpch1000c
