#ifdef __cplusplus
extern "C" {
#endif
    
extern solver_int32_default FORCESNLPsolver_normal_adtool2forces(FORCESNLPsolver_normal_float *x,        /* primal vars                                         */
										 FORCESNLPsolver_normal_float *y,        /* eq. constraint multiplers                           */
										 FORCESNLPsolver_normal_float *l,        /* ineq. constraint multipliers                        */
										 FORCESNLPsolver_normal_float *p,        /* parameters                                          */
										 FORCESNLPsolver_normal_float *f,        /* objective function (scalar)                         */
										 FORCESNLPsolver_normal_float *nabla_f,  /* gradient of objective function                      */
										 FORCESNLPsolver_normal_float *c,        /* dynamics                                            */
										 FORCESNLPsolver_normal_float *nabla_c,  /* Jacobian of the dynamics (column major)             */
										 FORCESNLPsolver_normal_float *h,        /* inequality constraints                              */
										 FORCESNLPsolver_normal_float *nabla_h,  /* Jacobian of inequality constraints (column major)   */
										 FORCESNLPsolver_normal_float *hess,     /* Hessian (column major)                              */
										 solver_int32_default stage,     /* stage number (0 indexed)                            */
										 solver_int32_default iteration, /* iteration number of solver                          */
										 solver_int32_default threadID  /* Id of caller thread */);

#ifdef __cplusplus
} /* extern "C" */
#endif
