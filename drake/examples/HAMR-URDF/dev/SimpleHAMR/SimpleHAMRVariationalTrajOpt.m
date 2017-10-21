function [hamr,xtraj,utraj,ctraj,btraj,...
    psitraj,etatraj,jltraj, kltraj, straj, ...
    z,F,info,infeasible_constraint_name] = SimpleHAMRVariationalTrajOpt()

% file
urdf = fullfile(getDrakePath, 'examples', 'HAMR-URDF', 'dev', 'SimpleHAMR', 'HAMRSimple_scaled.urdf');

% options
% options.terrain = RigidBodyFlatTerrain();
options.ignore_self_collisions = true;
options.collision_meshes = false;
options.use_bullet = false;
options.floating = false;
options.collision = false;

hamr = HAMRSimpleRBM(urdf,options);
v = hamr.constructVisualizer();

% state/input dimenisons
nq = hamr.getNumPositions();
nv = hamr.getNumVelocities();
nx = nq+nv;
nu = hamr.getNumInputs();

% --- Set Input limits ---
ulim = 10;                 % set max force
umin = -ulim*ones(nu,1);
umax = ulim*ones(nu, 1);

% --- Initialize TrajOpt---
optimoptions.sweight = 1;

% ---- Initial Guess ----%

T = 100;
N = 11;
x0 = hamr.getInitialState();
q0 = x0(1:nq); v0 = 0*q0;
x1 = x0; q1 = x1(1:nq);
t_init = linspace(0,T,N);
T_span = [T T];
traj_opt = VariationalTrajectoryOptimization(hamr,N,T_span,optimoptions);

traj_init.x = PPTrajectory(foh([0 T],[x0, x1]));
traj_init.u = PPTrajectory(zoh(t_init,0.001*randn(nu,N)));
% traj_init.c = PPTrajectory(zoh(t_init,0.001*randn(traj_opt.nC,N)));
% traj_init.b = PPTrajectory(zoh(t_init,0.001*randn(traj_opt.nC*traj_opt.nD,N)));
% traj_init.psi = PPTrajectory(zoh(t_init,0.001*randn(traj_opt.nC,N)));
% traj_init.eta =  PPTrajectory(zoh(t_init,0.001*randn(traj_opt.nC*traj_opt.nD,N)));

% -- Costs ---%
traj_opt = traj_opt.addRunningCost(@running_cost_fun);
% traj_opt = traj_opt.addFinalCost(@final_cost_fun);


% -- Constraints ---%
traj_opt = traj_opt.addPositionConstraint(ConstantConstraint(q0),1);
traj_opt = traj_opt.addPositionConstraint(ConstantConstraint(q1),N);
traj_opt = traj_opt.addVelocityConstraint(ConstantConstraint(v0),1);
traj_opt = traj_opt.addTrajectoryDisplayFunction(@displayTraj);

traj_opt = traj_opt.addInputConstraint(BoundingBoxConstraint(umin, umax),1:N-1);

% Solver options
traj_opt = traj_opt.setSolver('snopt');
traj_opt = traj_opt.setSolverOptions('snopt','MajorIterationsLimit',10000);
traj_opt = traj_opt.setSolverOptions('snopt','MinorIterationsLimit',200000);
traj_opt = traj_opt.setSolverOptions('snopt','IterationsLimit',5000000);
traj_opt = traj_opt.setSolverOptions('snopt','SuperbasicsLimit',1000);
traj_opt = traj_opt.setSolverOptions('snopt','print','outputlog.txt');

disp('Solving...')
tic
[xtraj,utraj,ctraj,btraj,psitraj,etatraj,jltraj,kltraj,straj ...
    ,z,F,info,infeasible_constraint_name] = solveTraj(traj_opt,t_init,traj_init);
toc
%
    function [f,df] = running_cost_fun(h,x,u)
        R = (1/ulim)^2*eye(nu);
        g = (1/2)*u'*R*u;
        f = h*g;
        df = [g, zeros(1,nx), h*u'*R];
    end

    function [f,df] = final_cost_fun(tf,x)
        a = 1;
        f = -a*x(1);
        df = zeros(1, nx+1);
        df(2) = -a;
    end

    function displayTraj(h,x,u)
        disp('Displaying Trajectory...')
        h = h/1e3;
        ts = [0;cumsum(h)];
        for i=1:length(ts)
            v.drawWrapper(0,x(:,i));
            pause(h(1));
        end
        
    end
end
