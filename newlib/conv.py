import sys
import subprocess
import re

E9PATH="./e9patch/e9tool"
EXEFILE="./bfs"

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
    bintext = subprocess.check_output(['objdump', '-Cd', EXEFILE])
except:
    print "Error"


def mycheck(bintext, loc):
    targets = loc.split("+")
    pattern_func = re.compile('^(\w+) <({})\(.+\)>'.format(targets[0]))
    pattern_insn = None
    for line in bintext.splitlines():
        if pattern_func:
            m = re.search(pattern_func, line)
            if m:
                func_addr = m.group(1)
                insn_addr = int(func_addr, 16) + int(targets[1], 16)
                pattern_insn = re.compile('({:x}):'.format(insn_addr))
                pattern_func = None
                print(line)
                print(hex(insn_addr))
        if pattern_insn:
            m = re.search(pattern_insn, line)
            if m:
                print(line)
                pattern_op = re.compile('mov\s+\(%(\w+)\),')
                m = re.search(pattern_op, line)
                if m:
                    reg = m.group(1)
                    match_str = "addr={}".format(hex(insn_addr))
                    patch_str = "pref_yield<clean>({})@pref_yield".format(reg)
                    #print(match_str)
                    #print(patch_str)
                    subprocess.call([E9PATH, "-M", match_str, "-P", patch_str, EXEFILE])
            
            
for item in sorted_d:
    ratio = item[1] / float(total)
    #if ratio < 0.2:
    #    break
    print(ratio, item)
    mycheck(bintext, item[0])
    break


