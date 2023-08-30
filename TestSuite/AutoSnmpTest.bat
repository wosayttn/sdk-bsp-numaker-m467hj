set ipaddr=192.168.0.59

SnmpGet.exe /?

SnmpGet.exe -r:%ipaddr% -v:2c -c:"public" -o:.1.3.6.1.2.1.1.1.0

SnmpGet.exe -r:%ipaddr% -v:3 -o:.1.3.6.1.2.1.1.1.0 -sn:md5des -ap:MD5 -aw:nuvoton_no1 -pp:DES -pw:nuvoton_no1
SnmpGet.exe -r:%ipaddr% -v:3 -o:.1.3.6.1.2.1.1.1.0 -sn:md5aes -ap:MD5 -aw:nuvoton_no1 -pp:AES128 -pw:nuvoton_no1
SnmpGet.exe -r:%ipaddr% -v:3 -o:.1.3.6.1.2.1.1.1.0 -sn:shades -ap:SHA -aw:nuvoton_no1 -pp:DES -pw:nuvoton_no1
SnmpGet.exe -r:%ipaddr% -v:3 -o:.1.3.6.1.2.1.1.1.0 -sn:shaaes -ap:SHA -aw:nuvoton_no1 -pp:AES128 -pw:nuvoton_no1

pause