//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

#include "IntegratedBC.h"

// MOOSE includes
#include "Assembly.h"
#include "SubProblem.h"
#include "SystemBase.h"
#include "MooseVariableFE.h"
#include "MooseVariableScalar.h"

#include "libmesh/quadrature.h"

defineLegacyParams(IntegratedBC);

InputParameters
IntegratedBC::validParams()
{
  InputParameters params = IntegratedBCBase::validParams();
  return params;
}

IntegratedBC::IntegratedBC(const InputParameters & parameters)
  : IntegratedBCBase(parameters),
    MooseVariableInterface<Real>(this,
                                 false,
                                 "variable",
                                 Moose::VarKindType::VAR_NONLINEAR,
                                 Moose::VarFieldType::VAR_FIELD_STANDARD),
    _var(*mooseVariable()),
    _normals(_assembly.normals()),
    _phi(_assembly.phiFace(_var)),
    _grad_phi(_assembly.gradPhiFace(_var)),
    _test(_var.phiFace()),
    _grad_test(_var.gradPhiFace()),
    _u(_is_implicit ? _var.sln() : _var.slnOld()),
    _grad_u(_is_implicit ? _var.gradSln() : _var.gradSlnOld())
{
  addMooseVariableDependency(mooseVariable());

  _save_in.resize(_save_in_strings.size());
  _diag_save_in.resize(_diag_save_in_strings.size());

  for (unsigned int i = 0; i < _save_in_strings.size(); i++)
  {
    MooseVariable * var = &_subproblem.getStandardVariable(_tid, _save_in_strings[i]);

    if (var->feType() != _var.feType())
      paramError(
          "save_in",
          "saved-in auxiliary variable is incompatible with the object's nonlinear variable: ",
          moose::internal::incompatVarMsg(*var, _var));
    _save_in[i] = var;
    var->sys().addVariableToZeroOnResidual(_save_in_strings[i]);
    addMooseVariableDependency(var);
  }

  _has_save_in = _save_in.size() > 0;

  for (unsigned int i = 0; i < _diag_save_in_strings.size(); i++)
  {
    MooseVariable * var = &_subproblem.getStandardVariable(_tid, _diag_save_in_strings[i]);

    if (var->feType() != _var.feType())
      paramError(
          "diag_save_in",
          "saved-in auxiliary variable is incompatible with the object's nonlinear variable: ",
          moose::internal::incompatVarMsg(*var, _var));

    _diag_save_in[i] = var;
    var->sys().addVariableToZeroOnJacobian(_diag_save_in_strings[i]);
    addMooseVariableDependency(var);
  }

  _has_diag_save_in = _diag_save_in.size() > 0;
}

void
IntegratedBC::computeResidual()
{
  prepareVectorTag(_assembly, _var.number());

  for (_qp = 0; _qp < _qrule->n_points(); _qp++)
    for (_i = 0; _i < _test.size(); _i++)
      _local_re(_i) += _JxW[_qp] * _coord[_qp] * computeQpResidual();

  accumulateTaggedLocalResidual();

  if (_has_save_in)
  {
    Threads::spin_mutex::scoped_lock lock(Threads::spin_mtx);
    for (unsigned int i = 0; i < _save_in.size(); i++)
      _save_in[i]->sys().solution().add_vector(_local_re, _save_in[i]->dofIndices());
  }
}

void
IntegratedBC::computeJacobian()
{
  prepareMatrixTag(_assembly, _var.number(), _var.number());

  for (_qp = 0; _qp < _qrule->n_points(); _qp++)
    for (_i = 0; _i < _test.size(); _i++)
      for (_j = 0; _j < _phi.size(); _j++)
        _local_ke(_i, _j) += _JxW[_qp] * _coord[_qp] * computeQpJacobian();

  accumulateTaggedLocalMatrix();

  if (_has_diag_save_in)
  {
    unsigned int rows = _local_ke.m();
    DenseVector<Number> diag(rows);
    for (unsigned int i = 0; i < rows; i++)
      diag(i) = _local_ke(i, i);

    Threads::spin_mutex::scoped_lock lock(Threads::spin_mtx);
    for (unsigned int i = 0; i < _diag_save_in.size(); i++)
      _diag_save_in[i]->sys().solution().add_vector(diag, _diag_save_in[i]->dofIndices());
  }
}

void
IntegratedBC::computeJacobianBlock(MooseVariableFEBase & jvar)
{
  computeJacobianBlock(jvar.number());
}

void
IntegratedBC::computeJacobianBlock(unsigned int jvar)
{
  if (jvar == _var.number())
  {
    computeJacobian();
    return;
  }

  prepareMatrixTag(_assembly, _var.number(), jvar);

  // This (undisplaced) jvar could potentially yield the wrong phi size if this object is acting
  // on the displaced mesh
  auto phi_size = _sys.getVariable(_tid, jvar).dofIndices().size();

  for (_qp = 0; _qp < _qrule->n_points(); _qp++)
    for (_i = 0; _i < _test.size(); _i++)
      for (_j = 0; _j < phi_size; _j++)
        _local_ke(_i, _j) += _JxW[_qp] * _coord[_qp] * computeQpOffDiagJacobian(jvar);

  accumulateTaggedLocalMatrix();
}

void
IntegratedBC::computeJacobianBlockScalar(unsigned int jvar)
{
  prepareMatrixTag(_assembly, _var.number(), jvar);

  MooseVariableScalar & jv = _sys.getScalarVariable(_tid, jvar);
  for (_qp = 0; _qp < _qrule->n_points(); _qp++)
    for (_i = 0; _i < _test.size(); _i++)
      for (_j = 0; _j < jv.order(); _j++)
        _local_ke(_i, _j) += _JxW[_qp] * _coord[_qp] * computeQpOffDiagJacobian(jvar);

  accumulateTaggedLocalMatrix();
}
