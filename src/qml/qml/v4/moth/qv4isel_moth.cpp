#include "qv4isel_util_p.h"
#include "qv4isel_moth_p.h"
#include "qv4vme_moth_p.h"
#include <private/qv4functionobject_p.h>
#include <private/qv4regexpobject_p.h>
#include <private/qv4debugging_p.h>
#include <private/qv4function_p.h>

using namespace QQmlJS;
using namespace QQmlJS::Moth;

namespace {

inline BinOp aluOpFunction(V4IR::AluOp op)
{
    switch (op) {
    case V4IR::OpInvalid:
        return 0;
    case V4IR::OpIfTrue:
        return 0;
    case V4IR::OpNot:
        return 0;
    case V4IR::OpUMinus:
        return 0;
    case V4IR::OpUPlus:
        return 0;
    case V4IR::OpCompl:
        return 0;
    case V4IR::OpBitAnd:
        return __qmljs_bit_and;
    case V4IR::OpBitOr:
        return __qmljs_bit_or;
    case V4IR::OpBitXor:
        return __qmljs_bit_xor;
    case V4IR::OpAdd:
        return __qmljs_add;
    case V4IR::OpSub:
        return __qmljs_sub;
    case V4IR::OpMul:
        return __qmljs_mul;
    case V4IR::OpDiv:
        return __qmljs_div;
    case V4IR::OpMod:
        return __qmljs_mod;
    case V4IR::OpLShift:
        return __qmljs_shl;
    case V4IR::OpRShift:
        return __qmljs_shr;
    case V4IR::OpURShift:
        return __qmljs_ushr;
    case V4IR::OpGt:
        return __qmljs_gt;
    case V4IR::OpLt:
        return __qmljs_lt;
    case V4IR::OpGe:
        return __qmljs_ge;
    case V4IR::OpLe:
        return __qmljs_le;
    case V4IR::OpEqual:
        return __qmljs_eq;
    case V4IR::OpNotEqual:
        return __qmljs_ne;
    case V4IR::OpStrictEqual:
        return __qmljs_se;
    case V4IR::OpStrictNotEqual:
        return __qmljs_sne;
    case V4IR::OpInstanceof:
        return __qmljs_instanceof;
    case V4IR::OpIn:
        return __qmljs_in;
    case V4IR::OpAnd:
        return 0;
    case V4IR::OpOr:
        return 0;
    default:
        assert(!"Unknown AluOp");
        return 0;
    }
};

} // anonymous namespace

InstructionSelection::InstructionSelection(QV4::ExecutionEngine *engine, V4IR::Module *module)
    : EvalInstructionSelection(engine, module)
    , _function(0)
    , _vmFunction(0)
    , _block(0)
    , _codeStart(0)
    , _codeNext(0)
    , _codeEnd(0)
{
}

InstructionSelection::~InstructionSelection()
{
}

void InstructionSelection::run(QV4::Function *vmFunction, V4IR::Function *function)
{
    V4IR::BasicBlock *block;

    QHash<V4IR::BasicBlock *, QVector<ptrdiff_t> > patches;
    QHash<V4IR::BasicBlock *, ptrdiff_t> addrs;

    int codeSize = 4096;
    uchar *codeStart = new uchar[codeSize];
    uchar *codeNext = codeStart;
    uchar *codeEnd = codeStart + codeSize;

    qSwap(_function, function);
    qSwap(_vmFunction, vmFunction);
    qSwap(block, _block);
    qSwap(patches, _patches);
    qSwap(addrs, _addrs);
    qSwap(codeStart, _codeStart);
    qSwap(codeNext, _codeNext);
    qSwap(codeEnd, _codeEnd);

    int locals = frameSize();
    assert(locals >= 0);

    Instruction::Push push;
    push.value = quint32(locals);
    addInstruction(push);

    foreach (_block, _function->basicBlocks) {
        _addrs.insert(_block, _codeNext - _codeStart);

        foreach (V4IR::Stmt *s, _block->statements)
            s->accept(this);
    }

    patchJumpAddresses();

    _vmFunction->code = VME::exec;
    _vmFunction->codeData = squeezeCode();

    qSwap(_function, function);
    qSwap(_vmFunction, vmFunction);
    qSwap(block, _block);
    qSwap(patches, _patches);
    qSwap(addrs, _addrs);
    qSwap(codeStart, _codeStart);
    qSwap(codeNext, _codeNext);
    qSwap(codeEnd, _codeEnd);

    delete[] codeStart;
}

void InstructionSelection::callValue(V4IR::Temp *value, V4IR::ExprList *args, V4IR::Temp *result)
{
    Instruction::CallValue call;
    prepareCallArgs(args, call.argc, call.args);
    call.dest = getParam(value);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callProperty(V4IR::Temp *base, const QString &name, V4IR::ExprList *args, V4IR::Temp *result)
{
    // call the property on the loaded base
    Instruction::CallProperty call;
    call.base = getParam(base);
    call.name = identifier(name);
    prepareCallArgs(args, call.argc, call.args);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callSubscript(V4IR::Temp *base, V4IR::Temp *index, V4IR::ExprList *args, V4IR::Temp *result)
{
    // call the property on the loaded base
    Instruction::CallElement call;
    call.base = getParam(base);
    call.index = getParam(index);
    prepareCallArgs(args, call.argc, call.args);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::constructActivationProperty(V4IR::Name *func,
                                                       V4IR::ExprList *args,
                                                       V4IR::Temp *result)
{
    Instruction::CreateActivationProperty create;
    create.name = identifier(*func->id);
    prepareCallArgs(args, create.argc, create.args);
    create.result = getResultParam(result);
    addInstruction(create);
}

void InstructionSelection::constructProperty(V4IR::Temp *base, const QString &name, V4IR::ExprList *args, V4IR::Temp *result)
{
    Instruction::CreateProperty create;
    create.base = getParam(base);
    create.name = identifier(name);
    prepareCallArgs(args, create.argc, create.args);
    create.result = getResultParam(result);
    addInstruction(create);
}

void InstructionSelection::constructValue(V4IR::Temp *value, V4IR::ExprList *args, V4IR::Temp *result)
{
    Instruction::CreateValue create;
    create.func = getParam(value);
    prepareCallArgs(args, create.argc, create.args);
    create.result = getResultParam(result);
    addInstruction(create);
}

void InstructionSelection::loadThisObject(V4IR::Temp *temp)
{
    Instruction::LoadThis load;
    load.result = getResultParam(temp);
    addInstruction(load);
}

void InstructionSelection::loadConst(V4IR::Const *sourceConst, V4IR::Temp *targetTemp)
{
    assert(sourceConst);

    Instruction::LoadValue load;
    load.value = getParam(sourceConst);
    load.result = getResultParam(targetTemp);
    addInstruction(load);
}

void InstructionSelection::loadString(const QString &str, V4IR::Temp *targetTemp)
{
    Instruction::LoadValue load;
    load.value = Instr::Param::createValue(QV4::Value::fromString(identifier(str)));
    load.result = getResultParam(targetTemp);
    addInstruction(load);
}

void InstructionSelection::loadRegexp(V4IR::RegExp *sourceRegexp, V4IR::Temp *targetTemp)
{
    QV4::Value v = QV4::Value::fromObject(engine()->newRegExpObject(
                                            *sourceRegexp->value,
                                            sourceRegexp->flags));
    _vmFunction->generatedValues.append(v);

    Instruction::LoadValue load;
    load.value = Instr::Param::createValue(v);
    load.result = getResultParam(targetTemp);
    addInstruction(load);
}

void InstructionSelection::getActivationProperty(const V4IR::Name *name, V4IR::Temp *temp)
{
    Instruction::LoadName load;
    load.name = identifier(*name->id);
    load.result = getResultParam(temp);
    addInstruction(load);
}

void InstructionSelection::setActivationProperty(V4IR::Temp *source, const QString &targetName)
{
    Instruction::StoreName store;
    store.source = getParam(source);
    store.name = identifier(targetName);
    addInstruction(store);
}

void InstructionSelection::initClosure(V4IR::Closure *closure, V4IR::Temp *target)
{
    QV4::Function *vmFunc = vmFunction(closure->value);
    assert(vmFunc);
    Instruction::LoadClosure load;
    load.value = vmFunc;
    load.result = getResultParam(target);
    addInstruction(load);
}

void InstructionSelection::getProperty(V4IR::Temp *base, const QString &name, V4IR::Temp *target)
{
    Instruction::LoadProperty load;
    load.base = getParam(base);
    load.name = identifier(name);
    load.result = getResultParam(target);
    addInstruction(load);
}

void InstructionSelection::setProperty(V4IR::Temp *source, V4IR::Temp *targetBase, const QString &targetName)
{
    Instruction::StoreProperty store;
    store.base = getParam(targetBase);
    store.name = identifier(targetName);
    store.source = getParam(source);
    addInstruction(store);
}

void InstructionSelection::getElement(V4IR::Temp *base, V4IR::Temp *index, V4IR::Temp *target)
{
    Instruction::LoadElement load;
    load.base = getParam(base);
    load.index = getParam(index);
    load.result = getResultParam(target);
    addInstruction(load);
}

void InstructionSelection::setElement(V4IR::Temp *source, V4IR::Temp *targetBase, V4IR::Temp *targetIndex)
{
    Instruction::StoreElement store;
    store.base = getParam(targetBase);
    store.index = getParam(targetIndex);
    store.source = getParam(source);
    addInstruction(store);
}

void InstructionSelection::copyValue(V4IR::Temp *sourceTemp, V4IR::Temp *targetTemp)
{
    Instruction::MoveTemp move;
    move.source = getParam(sourceTemp);
    move.result = getResultParam(targetTemp);
    addInstruction(move);
}

void InstructionSelection::unop(V4IR::AluOp oper, V4IR::Temp *sourceTemp, V4IR::Temp *targetTemp)
{
    UnaryOpName op = 0;
    switch (oper) {
    case V4IR::OpIfTrue: assert(!"unreachable"); break;
    case V4IR::OpNot: op = __qmljs_not; break;
    case V4IR::OpUMinus: op = __qmljs_uminus; break;
    case V4IR::OpUPlus: op = __qmljs_uplus; break;
    case V4IR::OpCompl: op = __qmljs_compl; break;
    case V4IR::OpIncrement: op = __qmljs_increment; break;
    case V4IR::OpDecrement: op = __qmljs_decrement; break;
    default: assert(!"unreachable"); break;
    } // switch

    if (op) {
        Instruction::Unop unop;
        unop.alu = op;
        unop.source = getParam(sourceTemp);
        unop.result = getResultParam(targetTemp);
        addInstruction(unop);
    } else {
        qWarning("  UNOP1");
    }
}

void InstructionSelection::binop(V4IR::AluOp oper, V4IR::Temp *leftSource, V4IR::Temp *rightSource, V4IR::Temp *target)
{
    Instruction::Binop binop;
    binop.alu = aluOpFunction(oper);
    binop.lhs = getParam(leftSource);
    binop.rhs = getParam(rightSource);
    binop.result = getResultParam(target);
    addInstruction(binop);
}

void InstructionSelection::inplaceNameOp(V4IR::AluOp oper, V4IR::Temp *rightSource, const QString &targetName)
{
    InplaceBinOpName op = 0;
    switch (oper) {
    case V4IR::OpBitAnd: op = __qmljs_inplace_bit_and_name; break;
    case V4IR::OpBitOr: op = __qmljs_inplace_bit_or_name; break;
    case V4IR::OpBitXor: op = __qmljs_inplace_bit_xor_name; break;
    case V4IR::OpAdd: op = __qmljs_inplace_add_name; break;
    case V4IR::OpSub: op = __qmljs_inplace_sub_name; break;
    case V4IR::OpMul: op = __qmljs_inplace_mul_name; break;
    case V4IR::OpDiv: op = __qmljs_inplace_div_name; break;
    case V4IR::OpMod: op = __qmljs_inplace_mod_name; break;
    case V4IR::OpLShift: op = __qmljs_inplace_shl_name; break;
    case V4IR::OpRShift: op = __qmljs_inplace_shr_name; break;
    case V4IR::OpURShift: op = __qmljs_inplace_ushr_name; break;
    default: break;
    }

    if (op) {
        Instruction::InplaceNameOp ieo;
        ieo.alu = op;
        ieo.name = identifier(targetName);
        ieo.source = getParam(rightSource);
        addInstruction(ieo);
    }
}

void InstructionSelection::inplaceElementOp(V4IR::AluOp oper, V4IR::Temp *source, V4IR::Temp *targetBaseTemp, V4IR::Temp *targetIndexTemp)
{
    InplaceBinOpElement op = 0;
    switch (oper) {
    case V4IR::OpBitAnd: op = __qmljs_inplace_bit_and_element; break;
    case V4IR::OpBitOr: op = __qmljs_inplace_bit_or_element; break;
    case V4IR::OpBitXor: op = __qmljs_inplace_bit_xor_element; break;
    case V4IR::OpAdd: op = __qmljs_inplace_add_element; break;
    case V4IR::OpSub: op = __qmljs_inplace_sub_element; break;
    case V4IR::OpMul: op = __qmljs_inplace_mul_element; break;
    case V4IR::OpDiv: op = __qmljs_inplace_div_element; break;
    case V4IR::OpMod: op = __qmljs_inplace_mod_element; break;
    case V4IR::OpLShift: op = __qmljs_inplace_shl_element; break;
    case V4IR::OpRShift: op = __qmljs_inplace_shr_element; break;
    case V4IR::OpURShift: op = __qmljs_inplace_ushr_element; break;
    default: break;
    }

    Instruction::InplaceElementOp ieo;
    ieo.alu = op;
    ieo.base = getParam(targetBaseTemp);
    ieo.index = getParam(targetIndexTemp);
    ieo.source = getParam(source);
    addInstruction(ieo);
}

void InstructionSelection::inplaceMemberOp(V4IR::AluOp oper, V4IR::Temp *source, V4IR::Temp *targetBase, const QString &targetName)
{
    InplaceBinOpMember op = 0;
    switch (oper) {
    case V4IR::OpBitAnd: op = __qmljs_inplace_bit_and_member; break;
    case V4IR::OpBitOr: op = __qmljs_inplace_bit_or_member; break;
    case V4IR::OpBitXor: op = __qmljs_inplace_bit_xor_member; break;
    case V4IR::OpAdd: op = __qmljs_inplace_add_member; break;
    case V4IR::OpSub: op = __qmljs_inplace_sub_member; break;
    case V4IR::OpMul: op = __qmljs_inplace_mul_member; break;
    case V4IR::OpDiv: op = __qmljs_inplace_div_member; break;
    case V4IR::OpMod: op = __qmljs_inplace_mod_member; break;
    case V4IR::OpLShift: op = __qmljs_inplace_shl_member; break;
    case V4IR::OpRShift: op = __qmljs_inplace_shr_member; break;
    case V4IR::OpURShift: op = __qmljs_inplace_ushr_member; break;
    default: break;
    }

    Instruction::InplaceMemberOp imo;
    imo.alu = op;
    imo.base = getParam(targetBase);
    imo.member = identifier(targetName);
    imo.source = getParam(source);
    addInstruction(imo);
}

void InstructionSelection::prepareCallArgs(V4IR::ExprList *e, quint32 &argc, quint32 &args)
{
    bool singleArgIsTemp = false;
    if (e && e->next == 0 && e->expr->asTemp()) {
        // ok, only 1 argument in the call...
        const int idx = e->expr->asTemp()->index;
        // We can only pass a reference into the stack, which holds temps that
        // are not arguments (idx >= 0) nor locals (idx >= localCound).
        singleArgIsTemp = idx >= _function->locals.size() && e->expr->asTemp()->scope == 0;
    }

    if (singleArgIsTemp) {
        // We pass single arguments as references to the stack, but only if it's not a local or an argument.
        argc = 1;
        args = e->expr->asTemp()->index - _function->locals.size();
    } else if (e) {
        // We need to move all the temps into the function arg array
        int argLocation = outgoingArgumentTempStart();
        assert(argLocation >= 0);
        argc = 0;
        args = argLocation;
        while (e) {
            Instruction::MoveTemp move;
            move.source = getParam(e->expr);
            move.result = Instr::Param::createTemp(argLocation);
            addInstruction(move);
            ++argLocation;
            ++argc;
            e = e->next;
        }
    } else {
        argc = 0;
        args = 0;
    }
}

void InstructionSelection::visitJump(V4IR::Jump *s)
{
    Instruction::Jump jump;
    jump.offset = 0;
    ptrdiff_t loc = addInstruction(jump) + (((const char *)&jump.offset) - ((const char *)&jump));

    _patches[s->target].append(loc);
}

void InstructionSelection::visitCJump(V4IR::CJump *s)
{
    Instr::Param condition;
    if (V4IR::Temp *t = s->cond->asTemp()) {
        condition = getResultParam(t);
    } else if (V4IR::Binop *b = s->cond->asBinop()) {
        condition = getResultParam(0);
        Instruction::Binop binop;
        binop.alu = aluOpFunction(b->op);
        binop.lhs = getParam(b->left);
        binop.rhs = getParam(b->right);
        binop.result = condition;
        addInstruction(binop);
    } else {
        Q_UNIMPLEMENTED();
    }

    Instruction::CJump jump;
    jump.offset = 0;
    jump.condition = condition;
    ptrdiff_t trueLoc = addInstruction(jump) + (((const char *)&jump.offset) - ((const char *)&jump));
    _patches[s->iftrue].append(trueLoc);

    if (_block->index + 1 != s->iffalse->index) {
        Instruction::Jump jump;
        jump.offset = 0;
        ptrdiff_t falseLoc = addInstruction(jump) + (((const char *)&jump.offset) - ((const char *)&jump));
        _patches[s->iffalse].append(falseLoc);
    }
}

void InstructionSelection::visitRet(V4IR::Ret *s)
{
    Instruction::Ret ret;
    ret.result = getParam(s->expr);
    addInstruction(ret);
}

void InstructionSelection::visitTry(V4IR::Try *t)
{
    Instruction::EnterTry enterTry;
    enterTry.tryOffset = 0;
    enterTry.catchOffset = 0;
    enterTry.exceptionVarName = identifier(t->exceptionVarName);
    enterTry.exceptionVar = getParam(t->exceptionVar);
    ptrdiff_t enterTryLoc = addInstruction(enterTry);

    ptrdiff_t tryLoc = enterTryLoc + (((const char *)&enterTry.tryOffset) - ((const char *)&enterTry));
    _patches[t->tryBlock].append(tryLoc);

    ptrdiff_t catchLoc = enterTryLoc + (((const char *)&enterTry.catchOffset) - ((const char *)&enterTry));
    _patches[t->catchBlock].append(catchLoc);
}

void InstructionSelection::callBuiltinInvalid(V4IR::Name *func, V4IR::ExprList *args, V4IR::Temp *result)
{
    Instruction::CallActivationProperty call;
    call.name = identifier(*func->id);
    prepareCallArgs(args, call.argc, call.args);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinTypeofMember(V4IR::Temp *base, const QString &name, V4IR::Temp *result)
{
    Instruction::CallBuiltinTypeofMember call;
    call.base = getParam(base);
    call.member = identifier(name);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinTypeofSubscript(V4IR::Temp *base, V4IR::Temp *index, V4IR::Temp *result)
{
    Instruction::CallBuiltinTypeofSubscript call;
    call.base = getParam(base);
    call.index = getParam(index);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinTypeofName(const QString &name, V4IR::Temp *result)
{
    Instruction::CallBuiltinTypeofName call;
    call.name = identifier(name);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinTypeofValue(V4IR::Temp *value, V4IR::Temp *result)
{
    Instruction::CallBuiltinTypeofValue call;
    call.value = getParam(value);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinDeleteMember(V4IR::Temp *base, const QString &name, V4IR::Temp *result)
{
    Instruction::CallBuiltinDeleteMember call;
    call.base = getParam(base);
    call.member = identifier(name);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinDeleteSubscript(V4IR::Temp *base, V4IR::Temp *index, V4IR::Temp *result)
{
    Instruction::CallBuiltinDeleteSubscript call;
    call.base = getParam(base);
    call.index = getParam(index);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinDeleteName(const QString &name, V4IR::Temp *result)
{
    Instruction::CallBuiltinDeleteName call;
    call.name = identifier(name);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinDeleteValue(V4IR::Temp *result)
{
    Instruction::LoadValue load;
    load.value = Instr::Param::createValue(QV4::Value::fromBoolean(false));
    load.result = getResultParam(result);
    addInstruction(load);
}

void InstructionSelection::callBuiltinPostDecrementMember(V4IR::Temp *base, const QString &name, V4IR::Temp *result)
{
    Instruction::CallBuiltinPostDecMember call;
    call.base = getParam(base);
    call.member = identifier(name);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinPostDecrementSubscript(V4IR::Temp *base, V4IR::Temp *index, V4IR::Temp *result)
{
    Instruction::CallBuiltinPostDecSubscript call;
    call.base = getParam(base);
    call.index = getParam(index);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinPostDecrementName(const QString &name, V4IR::Temp *result)
{
    Instruction::CallBuiltinPostDecName call;
    call.name = identifier(name);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinPostDecrementValue(V4IR::Temp *value, V4IR::Temp *result)
{
    Instruction::CallBuiltinPostDecValue call;
    call.value = getParam(value);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinPostIncrementMember(V4IR::Temp *base, const QString &name, V4IR::Temp *result)
{
    Instruction::CallBuiltinPostIncMember call;
    call.base = getParam(base);
    call.member = identifier(name);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinPostIncrementSubscript(V4IR::Temp *base, V4IR::Temp *index, V4IR::Temp *result)
{
    Instruction::CallBuiltinPostIncSubscript call;
    call.base = getParam(base);
    call.index = getParam(index);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinPostIncrementName(const QString &name, V4IR::Temp *result)
{
    Instruction::CallBuiltinPostIncName call;
    call.name = identifier(name);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinPostIncrementValue(V4IR::Temp *value, V4IR::Temp *result)
{
    Instruction::CallBuiltinPostIncValue call;
    call.value = getParam(value);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinThrow(V4IR::Temp *arg)
{
    Instruction::CallBuiltinThrow call;
    call.arg = getParam(arg);
    addInstruction(call);
}

void InstructionSelection::callBuiltinFinishTry()
{
    Instruction::CallBuiltinFinishTry call;
    addInstruction(call);
}

void InstructionSelection::callBuiltinForeachIteratorObject(V4IR::Temp *arg, V4IR::Temp *result)
{
    Instruction::CallBuiltinForeachIteratorObject call;
    call.arg = getParam(arg);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinForeachNextPropertyname(V4IR::Temp *arg, V4IR::Temp *result)
{
    Instruction::CallBuiltinForeachNextPropertyName call;
    call.arg = getParam(arg);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinPushWithScope(V4IR::Temp *arg)
{
    Instruction::CallBuiltinPushScope call;
    call.arg = getParam(arg);
    addInstruction(call);
}

void InstructionSelection::callBuiltinPopScope()
{
    Instruction::CallBuiltinPopScope call;
    addInstruction(call);
}

void InstructionSelection::callBuiltinDeclareVar(bool deletable, const QString &name)
{
    Instruction::CallBuiltinDeclareVar call;
    call.isDeletable = deletable;
    call.varName = identifier(name);
    addInstruction(call);
}

void InstructionSelection::callBuiltinDefineGetterSetter(V4IR::Temp *object, const QString &name, V4IR::Temp *getter, V4IR::Temp *setter)
{
    Instruction::CallBuiltinDefineGetterSetter call;
    call.object = getParam(object);
    call.name = identifier(name);
    call.getter = getParam(getter);
    call.setter = getParam(setter);
    addInstruction(call);
}

void InstructionSelection::callBuiltinDefineProperty(V4IR::Temp *object, const QString &name, V4IR::Temp *value)
{
    Instruction::CallBuiltinDefineProperty call;
    call.object = getParam(object);
    call.name = identifier(name);
    call.value = getParam(value);
    addInstruction(call);
}

void InstructionSelection::callBuiltinDefineArray(V4IR::Temp *result, V4IR::ExprList *args)
{
    Instruction::CallBuiltinDefineArray call;
    prepareCallArgs(args, call.argc, call.args);
    call.result = getResultParam(result);
    addInstruction(call);
}

void InstructionSelection::callBuiltinDefineObjectLiteral(V4IR::Temp *result, V4IR::ExprList *args)
{
    int argLocation = outgoingArgumentTempStart();

    QV4::InternalClass *klass = engine()->emptyClass;
    V4IR::ExprList *it = args;
    while (it) {
        V4IR::Name *name = it->expr->asName();
        it = it->next;

        bool isData = it->expr->asConst()->value;
        it = it->next;
        klass = klass->addMember(identifier(*name->id), isData ? QV4::Attr_Data : QV4::Attr_Accessor);

        Instruction::MoveTemp move;
        move.source = getParam(it->expr);
        move.result = Instr::Param::createTemp(argLocation);
        addInstruction(move);
        ++argLocation;

        if (!isData) {
            it = it->next;

            Instruction::MoveTemp move;
            move.source = getParam(it->expr);
            move.result = Instr::Param::createTemp(argLocation);
            addInstruction(move);
            ++argLocation;
        }

        it = it->next;
    }

    Instruction::CallBuiltinDefineObjectLiteral call;
    call.internalClass = klass;
    call.args = outgoingArgumentTempStart();
    call.result = getResultParam(result);
    addInstruction(call);
}

ptrdiff_t InstructionSelection::addInstructionHelper(Instr::Type type, Instr &instr)
{
#ifdef MOTH_THREADED_INTERPRETER
    instr.common.code = VME::instructionJumpTable()[static_cast<int>(type)];
#else
    instr.common.instructionType = type;
#endif

    int instructionSize = Instr::size(type);
    if (_codeEnd - _codeNext < instructionSize) {
        int currSize = _codeEnd - _codeStart;
        uchar *newCode = new uchar[currSize * 2];
        ::memset(newCode + currSize, 0, currSize);
        ::memcpy(newCode, _codeStart, currSize);
        _codeNext = _codeNext - _codeStart + newCode;
        delete[] _codeStart;
        _codeStart = newCode;
        _codeEnd = _codeStart + currSize * 2;
    }

    ::memcpy(_codeNext, reinterpret_cast<const char *>(&instr), instructionSize);
    ptrdiff_t ptrOffset = _codeNext - _codeStart;
    _codeNext += instructionSize;

    return ptrOffset;
}

void InstructionSelection::patchJumpAddresses()
{
    typedef QHash<V4IR::BasicBlock *, QVector<ptrdiff_t> >::ConstIterator PatchIt;
    for (PatchIt i = _patches.begin(), ei = _patches.end(); i != ei; ++i) {
        Q_ASSERT(_addrs.contains(i.key()));
        ptrdiff_t target = _addrs.value(i.key());

        const QVector<ptrdiff_t> &patchList = i.value();
        for (int ii = 0, eii = patchList.count(); ii < eii; ++ii) {
            ptrdiff_t patch = patchList.at(ii);

            *((ptrdiff_t *)(_codeStart + patch)) = target - patch;
        }
    }

    _patches.clear();
    _addrs.clear();
}

uchar *InstructionSelection::squeezeCode() const
{
    int codeSize = _codeNext - _codeStart;
    uchar *squeezed = new uchar[codeSize];
    ::memcpy(squeezed, _codeStart, codeSize);
    return squeezed;
}

QV4::String *InstructionSelection::identifier(const QString &s)
{
    QV4::String *str = engine()->newIdentifier(s);
    _vmFunction->identifiers.append(str);
    return str;
}
