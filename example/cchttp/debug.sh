killall -9 gdb; killall -9 trafiic_server; 

$HOME/CDN/ATSDebug/bin/tsxs -I../../include/ -o echttp.so -c Protocol.c util.c net.c ec.c # TxnSM.c 
cp echttp.so $HOME/CDN/ATSDebug/libexec/trafficserver 2>/dev/null
cp echttp.so $HOME/CDN/ATSRelease/libexec/trafficserver 2>/dev/null
gdb -ex r $HOME/CDN/ATSDebug/bin/traffic_server; 
