tsxs -I ../../include/ -o echttp.so -c Protocol.c TxnSM.c util.c
cp echttp.so $HOME/CDN/ATS/libexec/trafficserver 2>/dev/null
cp echttp.so $HOME/atsTest/libexec/trafficserver 2>/dev/null
ls -lhtr $HOME/CDN/ATS/libexec/trafficserver 2>/dev/null
ls -lhtr $HOME/atsTest/libexec/trafficserver 2>/dev/null