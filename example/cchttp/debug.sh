killall -9 gdb; killall -9 trafiic_server; 

rm echttp.so 2>/dev/null
$HOME/CDN/ATSDebug/bin/tsxs -v -lisal -I../../include/ -o echttp.so -c Protocol.c util.c net.c ec.c transform.c # TxnSM.c 
cp echttp.so $HOME/CDN/ATSDebug/libexec/trafficserver 

rm echttp.so 2>/dev/null
$HOME/CDN/ATSRelease/bin/tsxs -lisal -I../../include/ -o echttp.so -c Protocol.c util.c net.c ec.c transform.c
cp echttp.so $HOME/CDN/ATSRelease/libexec/trafficserver 

gdb -ex r $HOME/CDN/ATSDebug/bin/traffic_server; 
