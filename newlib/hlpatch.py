import sys
import subprocess
import re
import argparse

parser = argparse.ArgumentParser(
    prog='hlpatch',
    description='Hiding latency by patching with prefetch+yield'
)

parser.add_argument(dest="exe_file", help='specify patched exec binary filename')
parser.add_argument('-p', '--perf', dest="perf_file", help='specify perf script output filename')
parser.add_argument('-c', '--column', dest="loc_col", default=-2, help='specify location column in perf script')
parser.add_argument('-e', '--e9path', dest="e9path", default="./e9patch/e9tool", help='specify e9tool path')
parser.add_argument('-v', '--verbose', dest="verbose", action='store_true')
args = parser.parse_args()

def parse_perf_script():
    if args.perf_file:
        with open(args.perf_file) as f:
            lines = f.readlines()
    else:
        lines = sys.stdin.readlines()
        
    d = dict()
    total = 0
    for line in lines:
        arr = line.split()
        loc = arr[args.loc_col]
        if d.get(loc):
            d[loc] += 1
        else:
            d[loc] = 1
        total += 1
    sorted_d = sorted(d.items(), key=lambda item: item[1], reverse=True)
    return [sorted_d, total]

def debug_print(stmt):
    if args.verbose:
        print(stmt)

def patch(exe_text, loc):
    targets = loc.split("+")
    pattern_func = re.compile('^(\w+) <({})\(.+\)>'.format(targets[0]))
    pattern_insn = None
    for line in exe_text.splitlines():
        if pattern_func:
            m = re.search(pattern_func, line)
            if m:
                func_addr = m.group(1)
                insn_addr = int(func_addr, 16) + int(targets[1], 16)
                pattern_insn = re.compile('({:x}):'.format(insn_addr))
                pattern_func = None
                debug_print("   [Found function top] " + line)
                #debug_print(hex(insn_addr))
        if pattern_insn:
            m = re.search(pattern_insn, line)
            if m:
                print("   [Found instruction] " + line)
                pattern_op = re.compile('mov\s+\(%(\w+)\),')
                m = re.search(pattern_op, line)
                if m:
                    reg = m.group(1)
                    match_str = "addr={}".format(hex(insn_addr))
                    patch_str = "pref_yield<clean>({})@pref_yield".format(reg)
                    #debug_print(match_str)
                    debug_print("   [e9patch command arg] " + patch_str)
                    subprocess.call([args.e9path, "-M", match_str, "-P", patch_str, args.exe_file])
            

debug_print("=== Parsing perf script output... ===")
sorted_d,total = parse_perf_script()
debug_print("=== Parse done! ===")

try:
    exe_text = subprocess.check_output(['objdump', '-Cd', args.exe_file])
except:
    print "Error"


debug_print("=== Patching... ===")
for item in sorted_d:
    ratio = item[1] / float(total)
    #if ratio < 0.2:
    #    break
    debug_print("  {}% {}".format(int(ratio*100), item))
    patch(exe_text, item[0])
    break
debug_print("=== Patch done! ===")


