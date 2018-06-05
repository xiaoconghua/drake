clear; clc; close all;
global kl_traj c_traj beta_traj psi_traj eta_traj

%% Load Rigid Body

urdf = fullfile(getDrakePath,'examples', 'HAMR-URDF', 'urdf', 'HAMRVariational_scaledV2.urdf');
kl_traj = []; 
c_traj = [];
beta_traj = []; 
psi_traj = []; 
eta_traj = []; 

% options
options.ignore_self_collisions = true;
options.collision_meshes = false;
options.z_inactive_guess_tol = 0.1;
options.use_bullet = false;

% options to change
options.dt = 1;
options.mu = 0.6;
gait = 'none';
SAVE_FLAG = 1;
ISFLOAT = true; % floating (gnd contact) or in air (not floating)

if ISFLOAT
    options.floating = ISFLOAT;
    options.collision = ISFLOAT;
    load q0.mat
%     x0 = [zeros(6,1); q0_biased(1:44); zeros(6,1); q0_biased(45:end)];
%     x0(3) = 13.04;
    x0 = zeros(100,1); x0(1:50) = q0; 
    options.terrain = RigidBodyFlatTerrain();
    
else
    options.floating = ISFLOAT;
    options.collision = ISFLOAT;
    x0 = zeros(88, 1);
    %     load q0_biased.mat
    %     x0 = q0_biased;
    
    options.terrain = [];
end

% Build robot + visualizer
hamr = HamrVariationalTSRBM(urdf, options);
hamr = hamr.setJointLimits(-Inf(hamr.getNumPositions(), 1), Inf(hamr.getNumPositions(), 1));
hamr = compile(hamr);

v = hamr.constructVisualizer();
% v.inspector(x0);

%% Build Actuators
dp.Vb = 150;
dp.Vg = 0;
%
nact = 8;
hr_actuators = HamrActuators(nact, {'FLsact', 'FLlact', 'RLsact', 'RLlact', ...
    'FRsact', 'FRlact', 'RRsact', 'RRlact'},  [1; 1; -1; -1; 1; 1; -1; -1], dp);

% % make lift's double thick
% tcfL = 2*hr_actuators.dummy_bender(1).tcf;
% for i = 1:numel(hr_actuators.dummy_bender)
%     if contains(hr_actuators.names{i}, 'lact')
%         hr_actuators.dummy_bender(i) = hr_actuators.dummy_bender(i).setCFThickness(tcfL);
%     end
% end

%% Connect system

%connections from actuators to hamr
hr_actuators = hr_actuators.setOutputFrame(hamr.getInputFrame());
connection1(1).from_output = hr_actuators.getOutputFrame();
connection1(1).to_input = hamr.getInputFrame();

% connections from hamr to actuators
hamr_out = hamr.getOutputFrame();
act_in = hr_actuators.getInputFrame();
act_in = act_in.replaceFrameNum(2, hamr_out.getFrameByName('ActuatorDeflectionandRate'));
hr_actuators = hr_actuators.setInputFrame(act_in);

connection2(1).from_output = hamr_out.getFrameByName('ActuatorDeflectionandRate');
connection2(1).to_input = act_in.getFrameByName('ActuatorDeflectionandRate');

% mimo inputs
input_select(1).system = 1;
input_select(1).input = act_in.getFrameByName('DriveVoltage');

% mimo outputs
output_select(1).system = 2;
output_select(1).output = hamr_out.getFrameByName('HamrPosition');
output_select(2).system = 2;
output_select(2).output = hamr_out.getFrameByName('HamrVelocity');
output_select(3).system = 1;
output_select(3).output = hr_actuators.getOutputFrame();

hamrWact = mimoFeedback(hr_actuators, hamr, connection1, connection2, ...
    input_select, output_select);

%% Build (open-loop) control input

fd = 0.001;         % drive frequency (Hz)
tsim = 500;

t = 0:options.dt:tsim;

% Vact = [0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + pi/2);            % FLswing
%     0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t - deg2rad(60));                       % FLlift
%     0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + 3*pi/2);                % RLSwing
%     0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t - deg2rad(60));                  % RLLift
%     0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + pi/2 + deg2rad(120));                % FRswing
%     0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + deg2rad(60));                       % FRlift
%     0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + 3*pi/2 - deg2rad(120));              % RRSwing
%     0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + deg2rad(60))];                      % RRLift

switch gait
    case 'TROT'
        Vact = [0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + pi/2);            % FLswing
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t);                       % FLlift
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + 3*pi/2);              % RLSwing
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t);                       % RLLift
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + pi/2);                % FRswing
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t);                       % FRlift
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + 3*pi/2);              % RRSwing
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t)];                      % RRLift
    case 'PRONK'
        Vact = [0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + pi/2);            % FLswing
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t);                       % FLlift
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + pi/2);                % RLSwing
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + pi);                  % RLLift
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t - pi/2);                % FRswing
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t + pi);                       % FRlift
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t - pi/2);                % RRSwing
            0.5*(dp.Vb-dp.Vg)*sin(2*pi*fd*t)];                 % RRLift
    otherwise
        Vact = zeros(8, numel(t));
end

% ramp
tramp = 1/fd;
ramp = t/tramp; ramp(t >= tramp) = 1;

Vact = bsxfun(@times, ramp, Vact) + 0.5*(dp.Vb - dp.Vg);
u = PPTrajectory(foh(t, Vact));
u = setOutputFrame(u, hamrWact.getInputFrame());

figure(1); clf;
plot(t, Vact(1,:), t, Vact(2,:), '--');
legend('Swing Drive', 'Lift Drive')

%% Simulate Open loop

hamr_OL = cascade(u, hamrWact);
% nQ = hamr.getNumPositions();
% x0_hat = hamr.positionConstraints(x0(1:nQ));
nQ = hamr.getManipulator().getNumPositions();
x0_hat = hamr.getManipulator().positionConstraints(x0(1:nQ));
[tf, err_str] = valuecheck(positionConstraints(hamr,x0(1:nQ)),zeros(72,1),1e-6);
tf = 1;
if tf
    disp('Valid initial condition: simulating...')
    tic;
    %     options = odeset('RelTol',1e-3,'AbsTol',1e-4);
%     x1 = hamr.update(0, x0, Vact(:,1))
    xtraj = simulate(hamr_OL, [0 tsim], x0); %options);
    tlcp = toc;
    xtraj_scaled = PPTrajectory(foh(xtraj.getBreaks()*1e-3, xtraj.eval(xtraj.getBreaks())));
    xtraj_scaled = xtraj_scaled.setOutputFrame(xtraj.getOutputFrame());
    fprintf('It took %fs to simulate %fs of realtime. \nThats %fx \n', ...
        tlcp, tsim/1000, 1000*tlcp/tsim)
    options.slider = true;
    %     xtraj.tt = xtraj.tt/1000;
    v.playback(xtraj_scaled, options);
else
    disp('invalid initial condition...')
end

%% Plotting
tt = xtraj.getBreaks();
yy = xtraj.eval(tt); 
xx = yy(1:2*nQ,:); 
uu = yy(2*nQ+1:end,:);

act_dof = hamr.getActuatedJoints();
ndof = hamr.getNumDiscStates();
title_str = {'Front Left Swing', 'Front Left Lift', ...
    'Rear Left Swing', 'Rear Left Lift', ...
    'Front Right Swing', 'Front Right Lift', ...
    'Rear Rear Swing', 'Rear Rear Lift'};

figure(2); clf; hold on;
for i = 1:numel(act_dof)
    subplot(4,2,i); hold on; title(title_str(i))
    yyaxis left; hold on; plot(tt, uu(i,:), 'b')
    %     yyaxis left; plot(tt, yy(act_dof(i), :)*1e3);
    %     yyaxis right; plot(tt, Vact(i,:));
    %     legend('Deflection', 'Force')
    yyaxis right; hold on; plot(tt, xx(act_dof(i), :)*1e3, 'r--', ...
        t, Vact(i,:) - mean(Vact(i,:)), 'r')
    %     title_str
    %     rms(yy(act_dof(i), :)*1e3)
    legend('Force', 'Deflection', 'Drive')
end

lp_b = [0, 7.540, -11.350;
    0, 7.540, -11.350;
    0, -7.540, -11.350;
    0, -7.540, -11.350];

lp_g = zeros([numel(t), size(lp_b')]);

legs = {'FLL4', 'RLL4', 'FRL4', 'RRL4'};

for j = 1:numel(tt)
    q = xx(1:ndof/2, j);
    qd = xx(ndof/2+1: ndof, j);
    kinsol = hamr.doKinematics(q, qd);
    for i = 1:size(lp_b,1)
        lp_g(j,:,i) = hamr.forwardKin(kinsol, hamr.findLinkId(legs{i}),lp_b(i,:)');
    end
end

figure(3); clf; hold on;
for i = 1:size(lp_b,1)
    %     subplot(2,2,i); hold on; title(legs{i});
    plot((lp_g(:,1,i) - mean(lp_g(:,1,i))), ...
        (lp_g(:,3,i) - mean(lp_g(:,3,i))))
    %     plot(lp_g(:,3,i)*1e3) % - mean(lp_g(:,3,i)))
    %     axis equal;
    %     axis([-2.5, 2.5, -2.5, 2.5])
end
legend(legs)

if ISFLOAT
    figure(4); clf;
    title_str = {'com x', 'com y', 'com z', 'roll', 'pitch', 'yaw'};
    for i = 1:6
        subplot(3,2,i); hold on; title(title_str(i))
        yyaxis left; hold on; plot(tt, xx(i,:))
        yyaxis right; hold on; plot(tt, xx(i+ndof/2, :))
    end
    
    contact_opt.use_bullet = false;
    phi = zeros(4, numel(tt));
    for i = 1:numel(tt)
        q = xx(1:ndof/2, i);
        qd = xx(ndof/2+1:ndof, i);
        kinsol = hamr.doKinematics(q, qd);
        phi(:,i) = hamr.contactConstraints(kinsol, false, contact_opt);
    end
    
%     cc = zeros(4, numel(c_traj)); 
%     for i = 1:numel(c_traj)
%         z_inactive_ind = find(phi(:,i+1) > options.z_inactive_guess_tol)
%         if isempty(z_inactive_ind)
%             cc(:,i) = c_traj{i};
%         else
%             cc(:,i) = NaN*ones(4,1); 
%         end       
%     end
    
    figure(5); clf; 
    for i = 1:size(phi, 1)
        subplot(2,2,i); hold on; 
        yyaxis right; plot(tt, phi(i,:)); ylabel('Leg Height')
        yyaxis left; plot(tt, c_traj(i,:)); ylabel('Force')
        plot(tt, beta_traj(4*(i-1)+(1:4), :), 'k'); 
    end
end

%% saving
savedir = '';

if SAVE_FLAG
    fname = [gait, '_var_', num2str(dp.Vb) 'V_', num2str(1e3*fd), 'Hz_', num2str(options.mu*100), '.mat'];
    save([savedir, fname], 'tt', 'xx', 'uu', 'kl_traj', 'c_traj', 'beta_traj', 'eta_traj', 'psi_traj', 'Vact');
end

%%
% posframe = xtraj_scaled.getOutputFrame().getFrameByName('HamrPosition');
% jointnames = posframe.getCoordinateNames();
%
% plotjoints = {'FLqfb2', 'RLqfb2', 'FRqfb2', 'RRqfb2';
%     'FLql3', 'RLql3', 'FRql3', 'RRql3'};
%
% figure(6); clf; hold on;
% for i = 1:size(plotjoints,2)
%     inds = find(strcmpi(jointnames, plotjoints{1,i}));
%     subplot(1,2,1); hold on;
%     plot(tt, yy(inds,:))
%     subplot(1,2,2); hold on;
%     indl = find(strcmpi(jointnames, plotjoints{2,i}))
%     plot(tt, yy(indl,:));
% end
% subplot(1,2,1); legend(plotjoints{1,:})
% subplot(1,2,2); legend(plotjoints{2,:})
%
%
