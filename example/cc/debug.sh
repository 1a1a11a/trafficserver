killall -9 gdb; killall -9 trafiic_server;

rm ectcp.so 2>/dev/null
$HOME/CDN/ATSDebug/bin/tsxs -v -I../../include/ -o ectcp.so -c Protocol.c TxnSM.c
cp ectcp.so $HOME/CDN/ATSDebug/libexec/trafficserver

rm ectcp.so 2>/dev/null
$HOME/CDN/ATSRelease/bin/tsxs -I../../include/ -o ectcp.so -c Protocol.c TxnSM.c
cp ectcp.so $HOME/CDN/ATSRelease/libexec/trafficserver

gdb -ex r $HOME/CDN/ATSDebug/bin/traffic_server;
