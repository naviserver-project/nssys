#!/usr/bin/tclsh

load /usr/local/lib/tclsys.so

# Send HTTP over UDP
puts [ns_sysudp 127.0.0.255 80 "GET / HTTP/1.0\r\n\r\n"]

# Setting channel on video device
set fd [open /dev/video0 r+]
# Calculate and set frequency for channel 80 NTSC cable
set freq [expr 559250*16/1000]
ns_sysioctl $fd VIDIOCSFREQ [binary format i $freq]
close $fd
