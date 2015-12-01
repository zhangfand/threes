import subprocess
import sys

try:
    from subprocess import DEVNULL
except ImportError:
    import os
    DEVNULL = open(os.devnull, 'wb')

EXPERIMENT_TIMES = int(sys.argv[1]) if len(sys.argv) > 1 else 1

with open('result_hybrid_valid_action_smoothness.csv', 'w') as file:
    file.write("move, score, max rank\n")
    for i in range(EXPERIMENT_TIMES):
        p = subprocess.Popen(
            './bin/threes', stderr=subprocess.PIPE, stdout=DEVNULL)
        for line in p.stderr.readlines():
            file.write(line)
        print("Test #" + str(i) + " Finished.")

retval = p.wait()
