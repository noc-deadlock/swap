# README #

gem5 code repositories for SWAP: Synchronized Weaving of Adjacent Packets for Network Deadlock Prevention

### Design Description ###
Section-3 of the paper describes the design

Mayank Parasar‬, Natalie Enright Jerger, Paul V. Gratz, Joshua San Miguel, Tushar Krishna

In Proc of 52nd Annual IEEE/ACM International Symposium on Microarchitecture, MICRO-52, 2019

Paper:
   * https://mayank-parasar.github.io/website/papers/swap_micro2019.pdf

### How to build ###
Under gem5/
* scons -j15 scons/Garnet_standalone/gem5.opt

### How to run ###

* See gem5/run_script_synthetic.sh
* To run: ./run_script_synthetic.sh
* A handy (approx.) script for saturation throughtput gem5/sat_thrpt.py
* To run: python sat_thrpt.py
 
### Developer ###

* Mayank Parasar (mayankparasar@gmail.com)
