#!/bin/sh
# Rut bang Gantt (CPU, process, [slot bat dau, slot ket thuc)) tu output cua ./os.
# Dung: ./os sched > out.txt; sh tests/gantt.sh out.txt
awk '
/^Time slot/                 { t = $3 }
/Dispatched process/         { c = $2; sub(":", "", c); cur[c] = $5; start[c] = t }
/Put process|has finished/   { c = $2; sub(":", "", c)
                               if (c in cur) {
                                       printf "CPU %s  P%-3s [%3d, %3d)\n", c, cur[c], start[c], t
                                       delete cur[c]
                               } }
' "${1:-/dev/stdin}" | sort -k2,2n -k5,5n
