import os
import subprocess
# import pdb; pdb.set_trace()
# first compile then run
binary = 'build/Garnet_standalone/gem5.opt'
os.system("scons -j15 {}".format(binary))

bench_caps=[ "BIT_ROTATION", "SHUFFLE", "TRANSPOSE" ]
bench=[ "bit_rotation", "shuffle", "transpose" ]


# bench_caps=[ "BIT_ROTATION" ]
# bench=[ "bit_rotation" ]

routing_algorithm=["TABLE", "XY", "RAND", "ADAPT_RAND", "WestFirst"]

num_cores = [64, 256]
num_rows = [8, 16]

# num_cores = [64]
# num_rows = [8]

os.system('rm -rf ./results')
os.system('mkdir results')

out_dir = './results'
cycles = 100000
vnet = 0
tr = 1
vc_ = 4
rout_ = 3

for c in range(len(num_cores)):
	for b in range(len(bench)):
		print ("cores: {2:d} b: {0:s} vc-{1:d}".format(bench_caps[b], vc_, num_cores[c]))
		pkt_lat = 0
		injection_rate = 0.02
		while(pkt_lat < 100.00):
			############# gem5 command-line ############
			os.system("{0:s} -d {1:s}/{2:d}/{4:s}/{3:s}/vc-{5:d}/inj-{6:1.2f} configs/example/garnet_synth_traffic.py --network=garnet2.0 --num-cpus={2:d} --num-dirs={2:d} --topology=Mesh_XY --mesh-rows={7:d} --interswap=1 --whenToSwap=1 --whichToSwap=1 --sim-cycles={8:d} --injectionrate={6:1.2f} --vcs-per-vnet={5:d} --no-is-swap=1 --occupancy-swap=0 --inj-vnet=0 --synthetic={9:s} --routing-algorithm={10:d} ".format(binary, out_dir, num_cores[c],  bench_caps[b], routing_algorithm[rout_], vc_, injection_rate, num_rows[c], cycles, bench[b], rout_ ))


			# convert float to string with required precision
			inj_rate="{:1.2f}".format(injection_rate)


			############ gem5 output-directory ##############
			output_dir=("{0:s}/{1:d}/{3:s}/{2:s}/vc-{4:d}/inj-{5:1.2f}".format(out_dir, num_cores[c],  bench_caps[b], routing_algorithm[rout_], vc_, injection_rate))

			packet_latency = subprocess.check_output("grep -nri average_packet_latency  {0:s}  | sed 's/.*system.ruby.network.average_packet_latency\s*//'".format(output_dir), shell=True)
			# print packet_latency
			pkt_lat = float(packet_latency)

			print ("injection_rate={1:1.2f} \t Packet Latency: {0:f} ".format(pkt_lat, injection_rate))
			injection_rate+=0.02


############### Extract results here ###############
for c in range(len(num_cores)):
	for b in range(len(bench)):
		print ("cores: {} benchmark: {} vc-{}".format(num_cores[c], bench_caps[b], vc_))
		pkt_lat = 0
		injection_rate = 0.02
		while (pkt_lat < 200.00):

			output_dir=("{0:s}/{1:d}/{3:s}/{2:s}/vc-{4:d}/inj-{5:1.2f}".format(out_dir, num_cores[c],  bench_caps[b], routing_algorithm[rout_], vc_, injection_rate))

			if(os.path.exists(output_dir)):

				packet_latency = subprocess.check_output("grep -nri average_packet_latency  {0:s}  | sed 's/.*system.ruby.network.average_packet_latency\s*//'".format(output_dir), shell=True)

				pkt_lat = float(packet_latency)

				print ("injection_rate={1:1.2f} \t Packet Latency: {0:f} ".format(pkt_lat, injection_rate))
				injection_rate+=0.02

			else:
				pkt_lat = 1000
