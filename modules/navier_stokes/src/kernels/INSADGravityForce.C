//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

#include "INSADGravityForce.h"
#include "INSADObjectTracker.h"
#include "FEProblemBase.h"

registerMooseObject("NavierStokesApp", INSADGravityForce);

InputParameters
INSADGravityForce::validParams()
{
  InputParameters params = ADVectorKernelValue::validParams();
  params.addClassDescription("Computes a body force due to gravity.");
  params.addRequiredParam<RealVectorValue>("gravity", "Direction of the gravity vector");
  params.addParam<MaterialPropertyName>("rho_name", "rho", "The name of the density");
  return params;
}

INSADGravityForce::INSADGravityForce(const InputParameters & parameters)
  : ADVectorKernelValue(parameters),
    _gravity_strong_residual(getADMaterialProperty<RealVectorValue>("gravity_strong_residual"))
{
  // Bypass the UserObjectInterface method because it requires a UserObjectName param which we
  // don't need
  auto & obj_tracker = const_cast<INSADObjectTracker &>(
      _fe_problem.getUserObject<INSADObjectTracker>("ins_ad_object_tracker"));
  obj_tracker.setHasGravity(true);
  obj_tracker.setGravityVector(getParam<RealVectorValue>("gravity"));
}

ADRealVectorValue
INSADGravityForce::precomputeQpResidual()
{
  return _gravity_strong_residual[_qp];
}
