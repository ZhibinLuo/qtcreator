/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "ResolveExpression.h"
#include "LookupContext.h"
#include "Overview.h"
#include "GenTemplateInstance.h"

#include <Control.h>
#include <AST.h>
#include <Scope.h>
#include <Names.h>
#include <Symbols.h>
#include <Literals.h>
#include <CoreTypes.h>
#include <TypeVisitor.h>
#include <NameVisitor.h>

#include <QtCore/QList>
#include <QtCore/QtDebug>

using namespace CPlusPlus;

namespace {

template <typename _Tp>
static QList<_Tp> removeDuplicates(const QList<_Tp> &results)
{
    QList<_Tp> uniqueList;
    QSet<_Tp> processed;
    foreach (const _Tp &r, results) {
        if (processed.contains(r))
            continue;

        processed.insert(r);
        uniqueList.append(r);
    }

    return uniqueList;
}

} // end of anonymous namespace

/////////////////////////////////////////////////////////////////////
// ResolveExpression
/////////////////////////////////////////////////////////////////////
ResolveExpression::ResolveExpression(Symbol *lastVisibleSymbol, const LookupContext &context)
    : ASTVisitor(context.expressionDocument()->translationUnit()),
      _lastVisibleSymbol(lastVisibleSymbol),
      _context(context),
      sem(context.expressionDocument()->translationUnit())
{
    if (! lastVisibleSymbol)
        lastVisibleSymbol = context.thisDocument()->globalNamespace();
    _scope = lastVisibleSymbol->scope();
}

ResolveExpression::ResolveExpression(Scope *scope, const LookupContext &context)
    : ASTVisitor(context.expressionDocument()->translationUnit()),
      _lastVisibleSymbol(0),
      _scope(scope),
      _context(context),
      sem(context.expressionDocument()->translationUnit())
{ }

ResolveExpression::~ResolveExpression()
{ }

QList<LookupItem> ResolveExpression::operator()(ExpressionAST *ast)
{
    const QList<LookupItem> previousResults = switchResults(QList<LookupItem>());
    accept(ast);
    return removeDuplicates(switchResults(previousResults));
}

QList<LookupItem>
ResolveExpression::switchResults(const QList<LookupItem> &results)
{
    const QList<LookupItem> previousResults = _results;
    _results = results;
    return previousResults;
}

void ResolveExpression::addResults(const QList<LookupItem> &results)
{
    foreach (const LookupItem r, results)
        addResult(r);
}

void ResolveExpression::addResult(const FullySpecifiedType &ty, Symbol *symbol)
{
    if (! symbol) {
        if (_scope)
            symbol = _scope->owner();

        else
            symbol = _context.thisDocument()->globalNamespace();
    }

    return addResult(LookupItem(ty, symbol));
}

void ResolveExpression::addResult(const LookupItem &r)
{
    Q_ASSERT(r.lastVisibleSymbol() != 0);

    if (! _results.contains(r))
        _results.append(r);
}

bool ResolveExpression::visit(BinaryExpressionAST *ast)
{
    if (tokenKind(ast->binary_op_token) == T_COMMA && ast->right_expression && ast->right_expression->asQtMethod() != 0) {

        if (ast->left_expression && ast->left_expression->asQtMethod() != 0)
            thisObject();
        else
            accept(ast->left_expression);

        QtMethodAST *qtMethod = ast->right_expression->asQtMethod();
        if (DeclaratorAST *d = qtMethod->declarator) {
            if (d->core_declarator) {
                if (DeclaratorIdAST *declaratorId = d->core_declarator->asDeclaratorId())
                    if (NameAST *nameAST = declaratorId->name)
                        _results = resolveMemberExpression(_results, T_ARROW, nameAST->name);
            }
        }

        return false;
    }

    accept(ast->left_expression);
    return false;
}

bool ResolveExpression::visit(CastExpressionAST *ast)
{
    addResult(sem.check(ast->type_id, _context.expressionDocument()->globalSymbols()));
    return false;
}

bool ResolveExpression::visit(ConditionAST *)
{
    // nothing to do.
    return false;
}

bool ResolveExpression::visit(ConditionalExpressionAST *ast)
{
    if (ast->left_expression)
        accept(ast->left_expression);

    else if (ast->right_expression)
        accept(ast->right_expression);

    return false;
}

bool ResolveExpression::visit(CppCastExpressionAST *ast)
{
    addResult(sem.check(ast->type_id, _context.expressionDocument()->globalSymbols()));
    return false;
}

bool ResolveExpression::visit(DeleteExpressionAST *)
{
    FullySpecifiedType ty(control()->voidType());
    addResult(ty);
    return false;
}

bool ResolveExpression::visit(ArrayInitializerAST *)
{
    // nothing to do.
    return false;
}

bool ResolveExpression::visit(NewExpressionAST *ast)
{
    if (ast->new_type_id) {
        Scope *scope = _context.expressionDocument()->globalSymbols();
        FullySpecifiedType ty = sem.check(ast->new_type_id->type_specifier_list, scope);
        ty = sem.check(ast->new_type_id->ptr_operator_list, ty, scope);
        FullySpecifiedType ptrTy(control()->pointerType(ty));
        addResult(ptrTy);
    }
    // nothing to do.
    return false;
}

bool ResolveExpression::visit(TypeidExpressionAST *)
{
    const Name *std_type_info[2];
    std_type_info[0] = control()->nameId(control()->findOrInsertIdentifier("std"));
    std_type_info[1] = control()->nameId(control()->findOrInsertIdentifier("type_info"));

    const Name *q = control()->qualifiedNameId(std_type_info, 2, /*global=*/ true);
    FullySpecifiedType ty(control()->namedType(q));
    addResult(ty);

    return false;
}

bool ResolveExpression::visit(TypenameCallExpressionAST *)
{
    // nothing to do
    return false;
}

bool ResolveExpression::visit(TypeConstructorCallAST *)
{
    // nothing to do.
    return false;
}

bool ResolveExpression::visit(PostfixExpressionAST *ast)
{
    accept(ast->base_expression);

    for (PostfixListAST *it = ast->postfix_expression_list; it; it = it->next) {
        accept(it->value);
    }

    return false;
}

bool ResolveExpression::visit(SizeofExpressionAST *)
{
    FullySpecifiedType ty(control()->integerType(IntegerType::Int));
    ty.setUnsigned(true);
    addResult(ty);
    return false;
}

bool ResolveExpression::visit(NumericLiteralAST *ast)
{
    Type *type = 0;
    const NumericLiteral *literal = numericLiteral(ast->literal_token);

    if (literal->isChar())
        type = control()->integerType(IntegerType::Char);
    else if (literal->isWideChar())
        type = control()->integerType(IntegerType::WideChar);
    else if (literal->isInt())
        type = control()->integerType(IntegerType::Int);
    else if (literal->isLong())
        type = control()->integerType(IntegerType::Long);
    else if (literal->isLongLong())
        type = control()->integerType(IntegerType::LongLong);
    else if (literal->isFloat())
        type = control()->floatType(FloatType::Float);
    else if (literal->isDouble())
        type = control()->floatType(FloatType::Double);
    else if (literal->isLongDouble())
        type = control()->floatType(FloatType::LongDouble);
    else
        type = control()->integerType(IntegerType::Int);

    FullySpecifiedType ty(type);
    if (literal->isUnsigned())
        ty.setUnsigned(true);

    addResult(ty);
    return false;
}

bool ResolveExpression::visit(BoolLiteralAST *)
{
    FullySpecifiedType ty(control()->integerType(IntegerType::Bool));
    addResult(ty);
    return false;
}

bool ResolveExpression::visit(ThisExpressionAST *)
{
    thisObject();
    return false;
}

void ResolveExpression::thisObject()
{
    Scope *scope = _scope;
    for (; scope; scope = scope->enclosingScope()) {
        if (scope->isFunctionScope()) {
            Function *fun = scope->owner()->asFunction();
            if (Scope *cscope = scope->enclosingClassScope()) {
                Class *klass = cscope->owner()->asClass();
                FullySpecifiedType classTy(control()->namedType(klass->name()));
                FullySpecifiedType ptrTy(control()->pointerType(classTy));
                addResult(ptrTy, fun);
                break;
            } else if (const QualifiedNameId *q = fun->name()->asQualifiedNameId()) {
                const Name *nestedNameSpecifier = 0;
                if (q->nameCount() == 1 && q->isGlobal())
                    nestedNameSpecifier = q->nameAt(0);
                else
                    nestedNameSpecifier = control()->qualifiedNameId(q->names(), q->nameCount() - 1);
                FullySpecifiedType classTy(control()->namedType(nestedNameSpecifier));
                FullySpecifiedType ptrTy(control()->pointerType(classTy));
                addResult(ptrTy, fun);
                break;
            }
        }
    }
}

bool ResolveExpression::visit(CompoundExpressionAST *ast)
{
    CompoundStatementAST *cStmt = ast->statement;
    if (cStmt && cStmt->statement_list) {
        accept(cStmt->statement_list->lastValue());
    }
    return false;
}

bool ResolveExpression::visit(NestedExpressionAST *ast)
{
    accept(ast->expression);
    return false;
}

bool ResolveExpression::visit(StringLiteralAST *)
{
    FullySpecifiedType charTy = control()->integerType(IntegerType::Char);
    charTy.setConst(true);
    FullySpecifiedType ty(control()->pointerType(charTy));
    addResult(ty);
    return false;
}

bool ResolveExpression::visit(ThrowExpressionAST *)
{
    return false;
}

bool ResolveExpression::visit(TypeIdAST *)
{
    return false;
}

bool ResolveExpression::visit(UnaryExpressionAST *ast)
{
    accept(ast->expression);
    unsigned unaryOp = tokenKind(ast->unary_op_token);
    if (unaryOp == T_AMPER) {
        QMutableListIterator<LookupItem > it(_results);
        while (it.hasNext()) {
            LookupItem p = it.next();
            FullySpecifiedType ty = p.type();
            ty.setType(control()->pointerType(ty));
            p.setType(ty);
            it.setValue(p);
        }
    } else if (unaryOp == T_STAR) {
        QMutableListIterator<LookupItem > it(_results);
        while (it.hasNext()) {
            LookupItem p = it.next();
            if (PointerType *ptrTy = p.type()->asPointerType()) {
                p.setType(ptrTy->elementType());
                it.setValue(p);
            } else {
                it.remove();
            }
        }
    }
    return false;
}

bool ResolveExpression::visit(CompoundLiteralAST *ast)
{
    accept(ast->type_id);
    return false;
}

bool ResolveExpression::visit(QualifiedNameAST *ast)
{
    if (const Name *name = ast->name) {
        const QList<Symbol *> candidates = _context.lookup(name, _scope);

        foreach (Symbol *candidate, candidates)
            addResult(candidate->type(), candidate);
    }

    return false;
}

bool ResolveExpression::visit(SimpleNameAST *ast)
{
    QList<Symbol *> symbols = _context.lookup(ast->name, _scope);
    foreach (Symbol *symbol, symbols)
        addResult(symbol->type(), symbol);

    return false;
}

bool ResolveExpression::visit(TemplateIdAST *ast)
{
    const QList<Symbol *> symbols = _context.lookup(ast->name, _scope);
    foreach (Symbol *symbol, symbols)
        addResult(symbol->type(), symbol);

    return false;
}

bool ResolveExpression::visit(DestructorNameAST *)
{
    FullySpecifiedType ty(control()->voidType());
    addResult(ty);
    return false;
}

bool ResolveExpression::visit(OperatorFunctionIdAST *)
{
    return false;
}

bool ResolveExpression::visit(ConversionFunctionIdAST *)
{
    return false;
}

bool ResolveExpression::maybeValidPrototype(Function *funTy, unsigned actualArgumentCount) const
{
    unsigned minNumberArguments = 0;

    for (; minNumberArguments < funTy->argumentCount(); ++minNumberArguments) {
        Argument *arg = funTy->argumentAt(minNumberArguments)->asArgument();

        if (arg->hasInitializer())
            break;
    }

    if (actualArgumentCount < minNumberArguments) {
        // not enough arguments.
        return false;

    } else if (! funTy->isVariadic() && actualArgumentCount > funTy->argumentCount()) {
        // too many arguments.
        return false;
    }

    return true;
}

bool ResolveExpression::visit(CallAST *ast)
{
    const QList<LookupItem> baseResults = _results;
    _results.clear();

    // Compute the types of the actual arguments.
    int actualArgumentCount = 0;

    //QList< QList<Result> > arguments;
    for (ExpressionListAST *exprIt = ast->expression_list; exprIt; exprIt = exprIt->next) {
        //arguments.append(operator()(exprIt->expression));
        ++actualArgumentCount;
    }

    const Name *functionCallOp = control()->operatorNameId(OperatorNameId::FunctionCallOp);

    foreach (const LookupItem &result, baseResults) {
        FullySpecifiedType ty = result.type().simplified();
        Symbol *lastVisibleSymbol = result.lastVisibleSymbol();

        if (NamedType *namedTy = ty->asNamedType()) {
            if (ClassOrNamespace *b = _context.classOrNamespace(namedTy->name(), lastVisibleSymbol)) {
                foreach (Symbol *overload, b->find(functionCallOp)) {
                    if (Function *funTy = overload->type()->asFunctionType()) {
                        if (maybeValidPrototype(funTy, actualArgumentCount)) {
                            Function *proto = instantiate(namedTy->name(), funTy)->asFunctionType();
                            addResult(proto->returnType().simplified(), lastVisibleSymbol);
                        }
                    }
                }
            }

        } else if (Function *funTy = ty->asFunctionType()) {
            if (maybeValidPrototype(funTy, actualArgumentCount))
                addResult(funTy->returnType().simplified(), lastVisibleSymbol);

        } else if (Class *classTy = ty->asClassType()) {
            // Constructor call
            FullySpecifiedType ctorTy = control()->namedType(classTy->name());
            addResult(ctorTy, lastVisibleSymbol);
        }
    }

    return false;
}

bool ResolveExpression::visit(ArrayAccessAST *ast)
{
    const QList<LookupItem> baseResults = _results;
    _results.clear();

    const QList<LookupItem> indexResults = operator()(ast->expression);

    const Name *arrayAccessOp = control()->operatorNameId(OperatorNameId::ArrayAccessOp);

    foreach (const LookupItem &result, baseResults) {
        FullySpecifiedType ty = result.type().simplified();
        Symbol *lastVisibleSymbol = result.lastVisibleSymbol();

        if (PointerType *ptrTy = ty->asPointerType()) {
            addResult(ptrTy->elementType().simplified(), lastVisibleSymbol);

        } else if (ArrayType *arrTy = ty->asArrayType()) {
            addResult(arrTy->elementType().simplified(), lastVisibleSymbol);

        } else if (NamedType *namedTy = ty->asNamedType()) {
            if (ClassOrNamespace *b = _context.classOrNamespace(namedTy->name(), lastVisibleSymbol)) {
                foreach (Symbol *overload, b->find(arrayAccessOp)) {
                    if (Function *funTy = overload->type()->asFunctionType()) {
                        Function *proto = instantiate(namedTy->name(), funTy)->asFunctionType();
                        // ### TODO: check the actual arguments
                        addResult(proto->returnType().simplified(), lastVisibleSymbol);
                    }
                }

            }
        }
    }
    return false;
}

bool ResolveExpression::visit(MemberAccessAST *ast)
{
    // The candidate types for the base expression are stored in
    // _results.
    const QList<LookupItem> baseResults = _results;

    // Evaluate the expression-id that follows the access operator.
    const Name *memberName = 0;
    if (ast->member_name)
        memberName = ast->member_name->name;

    // Remember the access operator.
    const int accessOp = tokenKind(ast->access_token);

    _results = resolveMemberExpression(baseResults, accessOp, memberName);

    return false;
}

QList<LookupItem>
ResolveExpression::resolveBaseExpression(const QList<LookupItem> &baseResults, int accessOp,
                                         bool *replacedDotOperator) const
{
    QList<LookupItem> results;

    if (baseResults.isEmpty())
        return results;

    LookupItem result = baseResults.first();
    FullySpecifiedType ty = result.type().simplified();
    Symbol *lastVisibleSymbol = result.lastVisibleSymbol();

    if (Function *funTy = ty->asFunctionType()) {
        if (funTy->isAmbiguous())
            ty = funTy->returnType().simplified();
    }

    if (accessOp == T_ARROW)  {
        if (NamedType *namedTy = ty->asNamedType()) {
            const Name *arrowAccessOp = control()->operatorNameId(OperatorNameId::ArrowOp);

            foreach (Symbol *s, _context.lookup(namedTy->name(), result.lastVisibleSymbol())) {
                if (PointerType *ptrTy = s->type()->asPointerType()) {
                    FullySpecifiedType elementTy = ptrTy->elementType().simplified();

                    if (elementTy->isNamedType() || elementTy->isClassType())
                        results.append(LookupItem(elementTy, lastVisibleSymbol));

                } else if (const NamedType *nt = s->type()->asNamedType()) {
                    Symbol *l = _context.lookup(nt->name(), result.lastVisibleSymbol()).first();

                    if (PointerType *ptrTy = l->type()->asPointerType()) {
                        FullySpecifiedType elementTy = ptrTy->elementType().simplified();

                        if (elementTy->isNamedType() || elementTy->isClassType())
                            results.append(LookupItem(elementTy, lastVisibleSymbol));
                    }
                }
            }

            if (ClassOrNamespace *b = _context.classOrNamespace(namedTy->name(), result.lastVisibleSymbol())) {
                foreach (Symbol *overload, b->find(arrowAccessOp)) {
                    if (Function *funTy = overload->type()->asFunctionType()) {
                        FullySpecifiedType f = instantiate(namedTy->name(), funTy);
                        FullySpecifiedType retTy = f->asFunctionType()->returnType().simplified();

                        if (PointerType *ptrTy = retTy->asPointerType()) {
                            FullySpecifiedType elementTy = ptrTy->elementType().simplified();
                            results.append(LookupItem(elementTy, overload));
                        }
                    }
                }
            }

        } else if (PointerType *ptrTy = ty->asPointerType()) {
            FullySpecifiedType elementTy = ptrTy->elementType().simplified();

            if (elementTy->isNamedType() || elementTy->isClassType())
                results.append(LookupItem(elementTy, lastVisibleSymbol));
        }
    } else if (accessOp == T_DOT) {
        if (replacedDotOperator) {
            if (PointerType *ptrTy = ty->asPointerType()) {
                *replacedDotOperator = true;
                ty = ptrTy->elementType().simplified();
            } else if (ArrayType *arrTy = ty->asArrayType()) {
                *replacedDotOperator = true;
                ty = arrTy->elementType().simplified();
            }
        }

        if (NamedType *namedTy = ty->asNamedType()) {
            const QList<Symbol *> candidates = _context.lookup(namedTy->name(), result.lastVisibleSymbol());
            foreach (Symbol *candidate, candidates) {
                if (candidate->isTypedef() && candidate->type()->isNamedType()) {
                    ty = candidate->type();
                    lastVisibleSymbol = candidate;
                    break;
                } else if (TypenameArgument *arg = candidate->asTypenameArgument()) {
                    ty = arg->type();
                    lastVisibleSymbol = candidate;
                    break;
                }
            }

            results.append(LookupItem(ty, lastVisibleSymbol));

        } else if (Function *fun = ty->asFunctionType()) {
            Scope *funScope = fun->scope();

            if (funScope && (funScope->isBlockScope() || funScope->isNamespaceScope())) {
                FullySpecifiedType retTy = fun->returnType().simplified();
                results.append(LookupItem(retTy, lastVisibleSymbol));
            }
        }
    }

    return removeDuplicates(results);
}

QList<LookupItem>
ResolveExpression::resolveMemberExpression(const QList<LookupItem> &baseResults,
                                           unsigned accessOp,
                                           const Name *memberName,
                                           bool *replacedDotOperator) const
{
    QList<LookupItem> results;

    const QList<LookupItem> classObjectResults = resolveBaseExpression(baseResults, accessOp, replacedDotOperator);

    foreach (const LookupItem &r, classObjectResults) {
        FullySpecifiedType ty = r.type();

        if (Class *klass = ty->asClassType())
            results += resolveMember(memberName, klass);

        else if (NamedType *namedTy = ty->asNamedType()) {
            if (ClassOrNamespace *b = _context.classOrNamespace(namedTy->name(), r.lastVisibleSymbol())) {
                foreach (Symbol *c, b->find(memberName))
                    results.append(LookupItem(instantiate(namedTy->name(), c), c));
            }
        }
    }

    return removeDuplicates(results);
}

FullySpecifiedType ResolveExpression::instantiate(const Name *className, Symbol *candidate) const
{
    return GenTemplateInstance::instantiate(className, candidate, _context.control());
}

QList<LookupItem>
ResolveExpression::resolveMember(const Name *memberName, Class *klass,
                                 const Name *className) const
{
    QList<LookupItem> results;

    if (! klass)
        return results;

    if (! className)
        className = klass->name();

    if (! className)
        return results;

    const QList<Symbol *> candidates = _context.lookup(memberName, klass->members());

    foreach (Symbol *candidate, candidates) {
        FullySpecifiedType ty = candidate->type();
        const Name *unqualifiedNameId = className;

        if (const QualifiedNameId *q = className->asQualifiedNameId())
            unqualifiedNameId = q->unqualifiedNameId();

        if (const TemplateNameId *templId = unqualifiedNameId->asTemplateNameId())
            ty = GenTemplateInstance::instantiate(templId, candidate, _context.control());

        results.append(LookupItem(ty, candidate));
    }

    return removeDuplicates(results);
}


QList<LookupItem>
ResolveExpression::resolveMember(const Name *memberName, ObjCClass *klass) const
{
    QList<LookupItem> results;
    if (!memberName || !klass)
        return results;

    const QList<Symbol *> candidates = _context.lookup(memberName, klass->members());

    foreach (Symbol *candidate, candidates) {
        FullySpecifiedType ty = candidate->type();

        results.append(LookupItem(ty, candidate));
    }

    return removeDuplicates(results);
}

bool ResolveExpression::visit(PostIncrDecrAST *)
{
    return false;
}

bool ResolveExpression::visit(ObjCMessageExpressionAST *ast)
{
    QList<LookupItem> receiverResults = operator()(ast->receiver_expression);

    if (!receiverResults.isEmpty()) {
        LookupItem result = receiverResults.first();
        FullySpecifiedType ty = result.type().simplified();
        const Name *klassName = 0;

        if (const ObjCClass *classTy = ty->asObjCClassType()) {
            // static access, e.g.:
            // [NSObject description];
            klassName = classTy->name();
        } else if (const PointerType *ptrTy = ty->asPointerType()) {
            const FullySpecifiedType pointeeTy = ptrTy->elementType();
            if (pointeeTy && pointeeTy->isNamedType()) {
                // dynamic access, e.g.:
                // NSObject *obj = ...; [obj release];
                klassName = pointeeTy->asNamedType()->name();
            }
        }

        if (klassName&&ast->selector && ast->selector->name) {
            const QList<Symbol *> resolvedSymbols = _context.lookup(klassName, result.lastVisibleSymbol());
            foreach (Symbol *resolvedSymbol, resolvedSymbols)
                if (ObjCClass *klass = resolvedSymbol->asObjCClass())
                    _results.append(resolveMember(ast->selector->name, klass));
        }
    }

    return false;
}

