# env.sh — source me: paths and a P helper for the Windows capture scripts.
T=/home/deby/projects/gate-automation/app/tools/motion-capture
SPX=/tmp/claude-1000/-home-deby-projects-gate-automation/2ee2d9ba-0769-44f3-a6a1-47be00738d3e/scratchpad/out
mkdir -p "$SPX"
WS='W:\home\deby\projects\gate-automation\app\tools\motion-capture'
SP='W:\tmp\claude-1000\-home-deby-projects-gate-automation\2ee2d9ba-0769-44f3-a6a1-47be00738d3e\scratchpad\out'
export OUT_DIR=$SPX
P(){ (cd /mnt/c && powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$WS\\$1" "${@:2}" 2>&1 | tail -1); }
# The demo backend (restarted if WSL restarted under us).
ensure_demo(){
  if ! curl -s -m 2 -o /dev/null http://127.0.0.1:8080/api/status; then
    (cd /home/deby/projects/gate-automation && setsid scripts/demo-stack.sh --hardened > "$SPX/demo-stack.log" 2>&1 &)
    for i in $(seq 1 60); do sleep 2; curl -s -m 2 -o /dev/null http://127.0.0.1:8080/api/status && break; done
  fi
}
