% The code is about the nmpc with external forces

clear
close all
clearvars -global
clc

addpath(genpath(pwd));

setup;


mpc_generator_normal;
mpc_generator_final;

% /home/johanna/uav_mpcc/src/lmpcc/lmpcc/python_forces_code/forces/forces_pro_client_v5.1.0_2022_08_16_20_17_46
% FORCESconfigureClient;
% mex -setup