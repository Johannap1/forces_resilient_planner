% FORCESNLPsolver_final : A fast customized optimization solver.
% 
% Copyright (C) 2013-2022 EMBOTECH AG [info@embotech.com]. All rights reserved.
% 
% 
% This software is intended for simulation and testing purposes only. 
% Use of this software for any commercial purpose is prohibited.
% 
% This program is distributed in the hope that it will be useful.
% EMBOTECH makes NO WARRANTIES with respect to the use of the software 
% without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
% PARTICULAR PURPOSE. 
% 
% EMBOTECH shall not have any liability for any damage arising from the use
% of the software.
% 
% This Agreement shall exclusively be governed by and interpreted in 
% accordance with the laws of Switzerland, excluding its principles
% of conflict of laws. The Courts of Zurich-City shall have exclusive 
% jurisdiction in case of any dispute.
% 
% [OUTPUTS] = FORCESNLPsolver_final(INPUTS) solves an optimization problem where:
% Inputs:
% - xinit - matrix of size [9x1]
% - x0 - matrix of size [340x1]
% - all_parameters - matrix of size [2600x1]
% - num_of_threads - scalar
% Outputs:
% - outputs - column vector of length 340
function [outputs] = FORCESNLPsolver_final(xinit, x0, all_parameters, num_of_threads)
    
    [output, ~, ~] = FORCESNLPsolver_finalBuildable.forcesCall(xinit, x0, all_parameters, num_of_threads);
    outputs = coder.nullcopy(zeros(340,1));
    outputs(1:17) = output.x01;
    outputs(18:34) = output.x02;
    outputs(35:51) = output.x03;
    outputs(52:68) = output.x04;
    outputs(69:85) = output.x05;
    outputs(86:102) = output.x06;
    outputs(103:119) = output.x07;
    outputs(120:136) = output.x08;
    outputs(137:153) = output.x09;
    outputs(154:170) = output.x10;
    outputs(171:187) = output.x11;
    outputs(188:204) = output.x12;
    outputs(205:221) = output.x13;
    outputs(222:238) = output.x14;
    outputs(239:255) = output.x15;
    outputs(256:272) = output.x16;
    outputs(273:289) = output.x17;
    outputs(290:306) = output.x18;
    outputs(307:323) = output.x19;
    outputs(324:340) = output.x20;
end
