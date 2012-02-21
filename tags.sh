#!/bin/sh

find . -name '*.c' | awk '{print "\x22" $0 "\x22" }' > ./cscope.files
find . -name '*.h' | awk '{print "\x22" $0 "\x22" }' >> ./cscope.files
find /usr/include -name '*.h' | awk '{print "\x22" $0 "\x22" }' >> ./cscope.files
cscope -b -q 2&>1 /dev/null
