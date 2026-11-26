#!/bin/bash
# Chay toan bo cau hinh trong input/ bang ./os, luu log vao tests/results/,
# in bang tom tat va giai thich tung [INSTRUCTION-ERROR] (lenh nao gay loi).
#
# Dung (tu thu muc goc repo, tren Linux/WSL):
#   make all && bash tests/run_teacher_tests.sh            # tat ca cau hinh
#   bash tests/run_teacher_tests.sh sched_0 os_dma_demo     # chi vai cau hinh
#
# Bien moi truong: BIN=./os-mm de chay ban khong DMA, TIMEOUT=giay (mac dinh 120).

BIN=${BIN:-./os}
TIMEOUT=${TIMEOUT:-120}
OUT=tests/results

if [ ! -x "$BIN" ]; then
	echo "Khong thay $BIN. Hay chay 'make all' (hoac 'make os-mm' va BIN=./os-mm) truoc." >&2
	exit 1
fi
mkdir -p "$OUT"

if [ $# -gt 0 ]; then
	configs="$*"
else
	configs=$(ls input | grep -v '^proc$')
fi

printf "%-28s %-4s %-9s %-9s %-7s %-6s %s\n" CONFIG RC PROCESS FINISHED ERRORS SWAPS DMA_DONE
for c in $configs; do
	log="$OUT/$c.log"
	timeout "$TIMEOUT" "$BIN" "$c" > "$log" 2>&1
	rc=$?
	np=$(head -1 "input/$c" | awk '{print $3}')
	printf "%-28s %-4s %-9s %-9s %-7s %-6s %s\n" "$c" "$rc" "$np" \
		"$(grep -c 'has finished' "$log")" "$(grep -c 'INSTRUCTION-ERROR' "$log")" \
		"$(grep -c 'SWAP-' "$log")" "$(grep -c 'DMA-DONE' "$log")"
done

echo
echo "Chi tiet [INSTRUCTION-ERROR] (so lan | chuong trinh | lenh | ma loi):"
for c in $configs; do
	log="$OUT/$c.log"
	grep -q INSTRUCTION-ERROR "$log" || continue
	declare -A prog=()
	while read -r path pid; do prog[$pid]=$path; done < <(
		sed -n 's/.*Loaded a process at \(input\/proc\/[^,]*\), PID: *\([0-9]*\).*/\1 \2/p' "$log")
	sed -n 's/.*PID=\([0-9]*\) PC=\([0-9]*\) opcode=[0-9]* rc=\(-*[0-9]*\).*/\1 \2 \3/p' "$log" |
	while read -r pid pc rc; do
		p=${prog[$pid]}
		echo "$(basename "$p") | $(sed -n "$((pc + 2))p" "$p" | tr -d '\r') | rc=$rc"
	done | sort | uniq -c | sed "s/^/  $c: /"
	unset prog
done
echo
echo "Log day du: $OUT/<config>.log   Gantt: sh tests/gantt.sh $OUT/<config>.log"
