import sys
lvl=int(sys.argv[1])
print(f"""symbols keen.noi
run 420
tap 1
run 200
poke lives 99 w1
poke cur_level {lvl} w1
poke game_state 2 w1
poke perf_on 2 w1
run 60
poke perf_loops 0 w2\npoke perf_vbl 0 w2
""")
# alternate run right/left with jumps and shots
for k in range(10):
    d = 'right' if (k%4<2) != (len(sys.argv) > 2) else 'left'   # 2nd arg: start left
    print(f"hold {d} 1\nrun 20\nhold {d} 2\nrun 20\nhold {d}\nrun 20")
print("print perf_loops w2\nprint perf_vbl w2\nprint game_state w1\nprint lives w1\nprint cur_level w1")
