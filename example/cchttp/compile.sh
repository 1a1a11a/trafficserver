tsxs -I ../../include/ -I../../ -I../../iocore/eventsystem/ -o echttp.so -c Protocol.c util.c net.c ec.c # TxnSM.c 
cp echttp.so $HOME/CDN/ATSDebug/libexec/trafficserver 2>/dev/null
cp echttp.so $HOME/atsTest/libexec/trafficserver 2>/dev/null
# ls -lhtr $HOME/CDN/ATS/libexec/trafficserver 2>/dev/null
# ls -lhtr $HOME/atsTest/libexec/trafficserver 2>/dev/null