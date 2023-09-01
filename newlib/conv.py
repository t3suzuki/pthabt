import sys
import subprocess
import re

d = dict()
total = 0
for line in sys.stdin:
    arr = line.split()
    loc = arr[-2]
    if d.get(loc):
        d[loc] += 1
    else:
        d[loc] = 1
    total += 1
#    print(line)
sorted_d = sorted(d.items(), key=lambda item: item[1], reverse=True)


try:
    bintext = subprocess.check_output(['objdump', '-Cd', './bfs'])
except:
    print "Error"


def mycheck(bintext, loc):
    targets = loc.split("+")
    pattern_func = re.compile('^(\w+) <({})\('.format(targets[0]))
    pattern_insn = None
    for line in bintext.splitlines():
        if pattern_func:
            m = re.search(pattern_func, line)
            if m:
                func_addr = m.group(1)
                insn_addr = int(func_addr, 16) + int(targets[1], 16)
                pattern_insn = re.compile('({:x}):'.format(insn_addr))
                pattern_func = None
                #print(line)
                #print(hex(insn_addr))
        if pattern_insn:
            m = re.search(pattern_insn, line)
            if m:
                print(line)
            
            
for item in sorted_d:
    ratio = item[1] / float(total)
    if ratio < 0.05:
        break
    mycheck(bintext, item[0])
#    print(item)



