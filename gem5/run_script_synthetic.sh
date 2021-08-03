#!/bin/bash

#### script for running SWAP with ligra workloads on RISCV architecture ####

is_swap=( 'is_swap-disable' 'is_swap-enable' )
routAlgo=( 'TABLE' 'XY' 'RAND' 'ADAPT_RAND' 'WestFirst' )
bench_caps=( 'UNIFORM_RANDOM' 'BIT_COMPLEMENT' 'BIT_REVERSE' 'BIT_ROTATION' 'SHUFFLE' 'TRANSPOSE' )
bench=( 'uniform_random' 'bit_complement' 'bit_reverse' 'bit_rotation' 'shuffle' 'transpose' )
d="11-17-2019"
whenToSwap=$1
occupancy_=$2
# r=3
# s_=$4
whichToSwap=1

# for b in 3
# for b in 0 1 2 3 4 5
for b in 0 3
do
for r in 1 4
do
for vc_ in 1 2 4
do
# commandline for benchmarks
for k in 0.02 0.04 0.06 0.08 0.10 0.12 0.14 0.16 0.18 0.20 0.22 0.24 0.26 0.28 0.30 0.32 0.34 0.36 0.38 0.40 0.42 0.44 0.46 0.48 0.50 0.52 0.54 0.56 0.58 0.60 0.62 0.64 0.66 0.68
	# 0.70 0.72 0.74 0.76 0.78 0.80 0.82 0.84 0.86 0.88 0.90 0.92 0.94 0.96 0.98 1.0
do
	./build/Garnet_standalone/gem5.opt -d ${out_dir}/16c/occupancy-${occupancy_}/is_swap-disable/${routAlgo[$r]}/${bench_caps[$b]}/whenToSwap-${whenToSwap}/vc-${vc_}/inj-${k} configs/example/garnet_synth_traffic.py --network=garnet2.0 --num-cpus=16 --num-dirs=16 --topology=Mesh_XY --mesh-rows=4 --interswap=1 --whenToSwap=${whenToSwap} --whichToSwap=1 --sim-cycles=100000 --injectionrate=${k} --vcs-per-vnet=${vc_} --no-is-swap=1 --occupancy-swap=${occupancy_} --inj-vnet=0 --synthetic=${bench[$b]} --routing-algorithm=${r} &
done
done
done
done