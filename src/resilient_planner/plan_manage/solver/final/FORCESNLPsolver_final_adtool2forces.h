#ifdef __cplusplus
extern "C" {
#endif
    
extern solver_int32_default FORCESNLPsolver_final_adtool2forces(FORCESNLPsolver_final_float *x,        /* primal vars                                         */
										 FORCESNLPsolver_final_float *y,        /* eq. constraint multiplers                           */
										 FORCESNLPsolver_final_float *l,        /* ineq. constraint multipliers                        */
										 FORCESNLPsolver_final_float *p,        /* parameters                                          */
										 FORCESNLPsolver_final_float *f,        /* objective function (scalar)                         */
										 FORCESNLPsolver_final_float *nabla_f,  /* gradient of objective function                      */
										 FORCESNLPsolver_final_float *c,        /* dynamics                                            */
										 FORCESNLPsolver_final_float *nabla_c,  /* Jacobian of the dynamics (column major)             */
										 FORCESNLPsolver_final_float *h,        /* inequality constraints                              */
										 FORCESNLPsolver_final_float *nabla_h,  /* Jacobian of inequality constraints (column major)   */
										 FORCESNLPsolver_final_float *hess,     /* Hessian (column major)                              */
										 solver_int32_default stage,     /* stage number (0 indexed)                            */
										 solver_int32_default iteration, /* iteration number of solver                          */
										 solver_int32_default threadID  /* Id of caller thread */);

#ifdef __cplusplus
} /* extern "C" */
#endif
