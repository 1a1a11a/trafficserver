tsxs -I../../include/ -o echttp.so -c Protocol.c util.c net.c ec.c # TxnSM.c 
# -I../../ -I../../iocore/eventsystem/ 
cp echttp.so $HOME/CDN/ATSDebug/libexec/trafficserver 
cp echttp.so $HOME/CDN/ATSRelease/libexec/trafficserver 
cp echttp.so $HOME/atsTest/libexec/trafficserver 
# ls -lhtr $HOME/CDN/ATS/libexec/trafficserver 2>/dev/null
# ls -lhtr $HOME/atsTest/libexec/trafficserver 2>/dev/null