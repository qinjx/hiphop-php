/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010- Facebook, Inc. (http://www.facebook.com)         |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#include <compiler/statement/class_variable.h>
#include <compiler/analysis/analysis_result.h>
#include <compiler/expression/modifier_expression.h>
#include <compiler/expression/expression_list.h>
#include <compiler/analysis/block_scope.h>
#include <compiler/analysis/class_scope.h>
#include <compiler/analysis/variable_table.h>
#include <compiler/expression/simple_variable.h>
#include <compiler/expression/assignment_expression.h>
#include <compiler/option.h>

using namespace HPHP;

///////////////////////////////////////////////////////////////////////////////
// constructors/destructors

ClassVariable::ClassVariable
(STATEMENT_CONSTRUCTOR_PARAMETERS,
 ModifierExpressionPtr modifiers, ExpressionListPtr declaration)
  : Statement(STATEMENT_CONSTRUCTOR_PARAMETER_VALUES(ClassVariable)),
    m_modifiers(modifiers), m_declaration(declaration) {
}

StatementPtr ClassVariable::clone() {
  ClassVariablePtr stmt(new ClassVariable(*this));
  stmt->m_modifiers = Clone(m_modifiers);
  stmt->m_declaration = Clone(m_declaration);
  return stmt;
}

///////////////////////////////////////////////////////////////////////////////
// parser functions

static bool isEquivRedecl(const std::string &name,
                          ExpressionPtr exp,
                          ModifierExpressionPtr modif,
                          Symbol * symbol) {
  assert(exp);
  assert(modif);
  assert(symbol);
  if (symbol->getName()     != name                 ||
      symbol->isProtected() != modif->isProtected() ||
      symbol->isPrivate()   != modif->isPrivate()   ||
      symbol->isPublic()    != modif->isPublic()    ||
      symbol->isStatic()    != modif->isStatic())
    return false;

  ExpressionPtr symDeclExp =
    dynamic_pointer_cast<Expression>(symbol->getDeclaration());
  if (!exp) return !symDeclExp;
  return exp->equals(symDeclExp);
}

void ClassVariable::onParseRecur(AnalysisResultConstPtr ar,
                                 ClassScopePtr scope) {
  ModifierExpressionPtr modifiers =
    scope->setModifiers(m_modifiers);

  if (m_modifiers->isAbstract()) {
    parseTimeFatal(Compiler::InvalidAttribute,
                   "Properties cannot be declared abstract");
  }

  if (m_modifiers->isFinal()) {
    parseTimeFatal(Compiler::InvalidAttribute,
                   "Properties cannot be declared final");
  }

  for (int i = 0; i < m_declaration->getCount(); i++) {
    VariableTablePtr variables = scope->getVariables();
    ExpressionPtr exp = (*m_declaration)[i];
    if (exp->is(Expression::KindOfAssignmentExpression)) {
      AssignmentExpressionPtr assignment =
        dynamic_pointer_cast<AssignmentExpression>(exp);
      ExpressionPtr var = assignment->getVariable();
      const std::string &name =
        dynamic_pointer_cast<SimpleVariable>(var)->getName();
      if (variables->isPresent(name)) {
        exp->parseTimeFatal(Compiler::DeclaredVariableTwice,
                            "Cannot redeclare %s::$%s",
                            scope->getOriginalName().c_str(), name.c_str());
      } else {
        assignment->onParseRecur(ar, scope);
      }
    } else {
      const std::string &name =
        dynamic_pointer_cast<SimpleVariable>(exp)->getName();
      if (variables->isPresent(name)) {
        exp->parseTimeFatal(Compiler::DeclaredVariableTwice,
                            "Cannot redeclare %s::$%s",
                            scope->getOriginalName().c_str(), name.c_str());
      } else {
        variables->add(name, Type::Null, false, ar, exp, m_modifiers);
      }
    }
  }

  scope->setModifiers(modifiers);
}

///////////////////////////////////////////////////////////////////////////////
// static analysis functions

void ClassVariable::analyzeProgram(AnalysisResultPtr ar) {
  m_declaration->analyzeProgram(ar);
  AnalysisResult::Phase phase = ar->getPhase();
  if (phase != AnalysisResult::AnalyzeAll) {
    return;
  }
  ClassScopePtr scope = getClassScope();
  for (int i = 0; i < m_declaration->getCount(); i++) {
    ExpressionPtr exp = (*m_declaration)[i];
    bool error;
    if (exp->is(Expression::KindOfAssignmentExpression)) {
      AssignmentExpressionPtr assignment =
        dynamic_pointer_cast<AssignmentExpression>(exp);
      SimpleVariablePtr var =
        dynamic_pointer_cast<SimpleVariable>(assignment->getVariable());
      ExpressionPtr value = assignment->getValue();
      scope->getVariables()->setClassInitVal(var->getName(), value);
      error = scope->getVariables()->markOverride(ar, var->getName());
    } else {
      SimpleVariablePtr var =
        dynamic_pointer_cast<SimpleVariable>(exp);
      error = scope->getVariables()->markOverride(ar, var->getName());
      scope->getVariables()->setClassInitVal(var->getName(),
                                             makeConstant(ar, "null"));
    }
    if (error) {
      Compiler::Error(Compiler::InvalidOverride, exp);
    }
  }
}

void ClassVariable::addTraitPropsToScope(AnalysisResultPtr ar,
                                         ClassScopePtr scope) {
  ModifierExpressionPtr modifiers = scope->setModifiers(m_modifiers);
  VariableTablePtr variables = scope->getVariables();

  for (int i = 0; i < m_declaration->getCount(); i++) {
    ExpressionPtr exp = (*m_declaration)[i];

    SimpleVariablePtr var;
    ExpressionPtr value;
    if (exp->is(Expression::KindOfAssignmentExpression)) {
      AssignmentExpressionPtr assignment =
        dynamic_pointer_cast<AssignmentExpression>(exp);
      var = dynamic_pointer_cast<SimpleVariable>(assignment->getVariable());
      value = assignment->getValue();
    } else {
      var = dynamic_pointer_cast<SimpleVariable>(exp);
      value = makeConstant(ar, "null");
    }

    const string &name = var->getName();
    Symbol *sym;
    ClassScopePtr prevScope = variables->isPresent(name) ? scope :
      scope->getVariables()->findParent(ar, name, sym);

    if (prevScope &&
        !isEquivRedecl(name, exp, m_modifiers,
                       prevScope->getVariables()->getSymbol(name))) {
      Compiler::Error(Compiler::DeclaredVariableTwice, exp);
      m_declaration->removeElement(i--);
    } else {
      if (prevScope != scope) { // Property is new or override, so add it
        variables->add(name, Type::Variant, false, ar, exp, m_modifiers);
        variables->getSymbol(name)->setValue(exp);
        variables->setClassInitVal(name, value);
        variables->markOverride(ar, name);
      } else {
        m_declaration->removeElement(i--);
      }
    }
  }
  scope->setModifiers(modifiers);
}

ConstructPtr ClassVariable::getNthKid(int n) const {
  switch (n) {
    case 0:
      return m_modifiers;
    case 1:
      return m_declaration;
    default:
      assert(false);
      break;
  }
  return ConstructPtr();
}

int ClassVariable::getKidCount() const {
  return 2;
}

void ClassVariable::setNthKid(int n, ConstructPtr cp) {
  switch (n) {
    case 0:
      m_modifiers = boost::dynamic_pointer_cast<ModifierExpression>(cp);
      break;
    case 1:
      m_declaration = boost::dynamic_pointer_cast<ExpressionList>(cp);
      break;
    default:
      assert(false);
      break;
  }
}

StatementPtr ClassVariable::preOptimize(AnalysisResultConstPtr ar) {
  ClassScopePtr scope = getClassScope();
  for (int i = 0; i < m_declaration->getCount(); i++) {
    ExpressionPtr exp = (*m_declaration)[i];
    if (exp->is(Expression::KindOfAssignmentExpression)) {
      AssignmentExpressionPtr assignment =
        dynamic_pointer_cast<AssignmentExpression>(exp);
      SimpleVariablePtr var =
        dynamic_pointer_cast<SimpleVariable>(assignment->getVariable());
      ExpressionPtr value = assignment->getValue();
      scope->getVariables()->setClassInitVal(var->getName(), value);
    }
  }
  return StatementPtr();
}

void ClassVariable::inferTypes(AnalysisResultPtr ar) {
  assert(getScope().get() == getClassScope().get());
  IMPLEMENT_INFER_AND_CHECK_ASSERT(getScope());

  // assignments will ignore the passed in type,
  // but we need to ensure that Null is applied to
  // the simple variables.
  m_declaration->inferAndCheck(ar, Type::Null, false);

  if (m_modifiers->isStatic()) {
    ClassScopePtr scope = getClassScope();
    for (int i = 0; i < m_declaration->getCount(); i++) {
      ExpressionPtr exp = (*m_declaration)[i];
      SimpleVariablePtr var;
      if (exp->is(Expression::KindOfAssignmentExpression)) {
        AssignmentExpressionPtr assignment =
          dynamic_pointer_cast<AssignmentExpression>(exp);
        ExpressionPtr value = assignment->getValue();
        if (value->containsDynamicConstant(ar)) {
          scope->getVariables()->
            setAttribute(VariableTable::ContainsDynamicStatic);
        }
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// code generation functions

void ClassVariable::outputPHP(CodeGenerator &cg, AnalysisResultPtr ar) {
  m_modifiers->outputPHP(cg, ar);
  cg_printf(" ");
  m_declaration->outputPHP(cg, ar);
  cg_printf(";\n");
}