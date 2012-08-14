
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <iostream>
#include <alloca.h>
#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>

#include "node_gmp.h"

#define ENSURE(a,t) do { if(!(a)->IsObject() || !(a)->ToObject()->ObjectProtoToString()->Equals(String::New("[object " #t "]"))) return ThrowException(Exception::TypeError(String::New("bad argument"))); } while(0)

static int inside_gmp_call = 0;
#define rewire(symbol, func, block) do { \
  void **sym = (void **)dlsym(RTLD_DEFAULT, #symbol); \
  if(!sym) block \
  else (*sym) = (void *)func; \
} while(0)

#define evil_try_catch(code, msg) do { \
  assert(inside_gmp_call == 0); \
  if(sigsetjmp(gmp_sucks_env,0) == 0) { \
    inside_gmp_call = 1; \
    code \
    inside_gmp_call = 0; \
  } else { \
    inside_gmp_call = 0; \
    return ThrowException(Exception::Error(String::New(msg))); \
  } \
} while(0)

extern "C" {
  static sigjmp_buf gmp_sucks_env;
  static struct sigaction old_actions[32] = { { 0 } };
  extern void __gmp_invalid_operation();

  /* rewrite the default allocators */
  extern void *__gmp_default_allocate(size_t);
  extern void *__gmp_default_reallocate(void *, size_t, size_t);
  extern void  __gmp_default_free(void *, size_t);
  void node_gmp_replace_abort_cxx() {
    siglongjmp(gmp_sucks_env, 1);
  }
  void *node_gmp_replace_allocate(size_t s) {
    void *answer = __gmp_default_allocate(s);
    if(answer == NULL) node_gmp_replace_abort_cxx();
    return answer;
  }
  void *node_gmp_replace_reallocate(void *p, size_t o, size_t n) {
    void *answer = __gmp_default_reallocate(p,o,n);
    if(answer == NULL) node_gmp_replace_abort_cxx();
    return answer;
  }
  void node_gmp_replace_free(void *p, size_t s) {
    __gmp_default_free(p, s);
  }
  extern void replace_abort() {
    node_gmp_replace_abort_cxx();
  }
  void node_gmp_sigthrow(int sig, siginfo_t *info, void *c) {
    if(inside_gmp_call) {
      siglongjmp(gmp_sucks_env, 1);
    }
    if(old_actions[sig].sa_handler != NULL &&
       old_actions[sig].sa_handler != SIG_IGN) {
      if(old_actions[sig].sa_handler == SIG_DFL) {
        struct sigaction new_action = { 0 };
        new_action.sa_handler = SIG_DFL;
        sigaction(sig, &new_action, NULL);
        raise(sig);
      }
      else {
        old_actions[sig].sa_sigaction(sig, info, c);
      }
    }
  }
  void rewire_signal(int sig) {
    struct sigaction new_action;
    if(sigaction(sig, NULL, &old_actions[sig]) == 0) {
      new_action = old_actions[sig];
      new_action.sa_sigaction = node_gmp_sigthrow;
      sigaction(sig, &new_action, NULL);
    }
    else {
      fprintf(stderr, "sigaction failed (gmp is evil): %s\n", strerror(errno));
    }
  }

#define abort_replace(f) abort_replace_func(#f, f)
  int abort_replace_func(const char *name, void *f) {
    void ** code = (void **)f;
    void *abrt_code = dlsym(RTLD_DEFAULT, "dyld_stub_abort");
    int psize, offset;
    unsigned long long icrutch, startpoint;

    startpoint = (unsigned long long int) code;
    if(!abrt_code) abrt_code = dlsym(RTLD_DEFAULT, "abort");
    if(!abrt_code) abrt_code = (void *)abort;
    if(!abrt_code) {
      fprintf(stderr, "abort_replace_func: Could not find abort symbol\n");
      return -1;
    }
    psize = getpagesize();
    while(1) {
      icrutch = (unsigned long long int) code;
      offset = (icrutch % psize);
      if(icrutch/psize > startpoint/psize) {
        return -1;
      }
      if(*code == (void *)abrt_code) break;
      code++;
    }
    if(mprotect((void *)(icrutch - offset), psize,
                PROT_READ|PROT_WRITE|PROT_EXEC) == 0) {
      fprintf(stderr, "rewiring abort @ %p\n", code);
      *code = (void *)replace_abort;
      mprotect((void *)(icrutch - offset), psize, PROT_READ|PROT_EXEC);
      return 0;
    }
    perror("mprotect");
    return -1;
  }
}

using namespace v8;
using namespace node;

#define GETARG(arg,t)                                                            \
  if (arg->IsNumber() || arg->IsString()) {                                      \
    String::Utf8Value val(arg->ToString());                                      \
    char * num = t;                                                              \
    try {                                                                        \
      if(arg->IsNumber()) i = (double)arg->NumberValue();                        \
      else i = num;                                                              \
    } catch (std::invalid_argument err) {                                        \
      node_gmp_replace_abort_cxx();                                              \
    }                                                                            \
  } else if (!(arg->IsUndefined() || arg->IsNull())) {                           \
    node_gmp_replace_abort_cxx();                                                \
  }


Handle<Value>
GInt::New(const Arguments &args) {
  HandleScope scope;

  mpz_class i = 0;

  ENSURE(args.This(), GInt);

  evil_try_catch({ GETARG(args[0], strtok(*val, ".")); }, "bad argument");

  GInt *g = new GInt(i);
  g->Wrap(args.This());
  return args.This();
}


GInt::GInt(mpz_class val): ObjectWrap() {
  val_ = val;
}

GInt::~GInt(){

}


Handle<Value>
GInt::Add(const Arguments &args) {
  HandleScope scope;
  mpz_class i = 0;

  ENSURE(args.This(), GInt);
  GInt *self = ObjectWrap::Unwrap<GInt>(args.This());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GInt);
    GInt *other = ObjectWrap::Unwrap<GInt>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], strtok(*val, ".")); }, "bad argument");

  evil_try_catch({ self->val_ += i; }, "gmp abort");
  return args.This();
}

Handle<Value>
GInt::Sub(const Arguments &args) {
  HandleScope scope;

  mpz_class i = 0;

  ENSURE(args.This(), GInt);
  GInt *self = ObjectWrap::Unwrap<GInt>(args.This());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GInt);
    GInt *other = ObjectWrap::Unwrap<GInt>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], strtok(*val, ".")); }, "bad argument");

  evil_try_catch({ self->val_ -= i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GInt::Mul(const Arguments &args) {
  HandleScope scope;

  mpz_class i = 0;

  ENSURE(args.This(), GInt);
  GInt *self = ObjectWrap::Unwrap<GInt>(args.This());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GInt);
    GInt *other = ObjectWrap::Unwrap<GInt>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], strtok(*val, ".")); }, "bad argument");

  evil_try_catch({ self->val_ *= i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GInt::Div(const Arguments &args) {
  HandleScope scope;

  mpz_class i = 0;

  if(args[0]->IsObject()) {
    ENSURE(args[0], GInt);
    GInt *other = ObjectWrap::Unwrap<GInt>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], strtok(*val, ".")); }, "bad argument");

  ENSURE(args.This(), GInt);
  GInt *self = ObjectWrap::Unwrap<GInt>(args.This());
  evil_try_catch({ self->val_ /= i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GInt::Pow(const Arguments &args) {
  HandleScope scope;

  if (!args[0]->IsNumber()) {
    return ThrowException(Exception::TypeError(String::New("exponent must be an int")));
  }

  ENSURE(args.This(), GInt);
  GInt *self = ObjectWrap::Unwrap<GInt>(args.This());

  evil_try_catch({
    inside_gmp_call = 1;
    mpz_t c;
    mpz_init(c);

    mpz_pow_ui(c, self->val_.get_mpz_t(), (long)args[0]->Int32Value());

    self->val_ = mpz_class(c);

    mpz_clear(c);
  }, "gmp abort");

  return args.This();
}

Handle<Value>
GInt::Cmp(const Arguments &args) {
  HandleScope scope;

  mpz_class i = 0;
  int result = 0;

  ENSURE(args.This(), GInt);
  GInt *self = ObjectWrap::Unwrap<GInt>(args.This());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GInt);
    GInt *other = ObjectWrap::Unwrap<GInt>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], strtok(*val, ".")); }, "bad argument");

  evil_try_catch({ if(self->val_ < i) result = 1;
                   else if(self->val_ > i) result = -1; }, "gmp abort");

  Local<Number> val = Number::New(result);
  return scope.Close(val);
}

Handle<Value>
GInt::ToNumber(const Arguments &args) {
  HandleScope scope;
  ENSURE(args.This(), GInt);
  GInt *self = ObjectWrap::Unwrap<GInt>(args.This());

  Local<Number> val = Number::New(self->val_.get_d());

  return scope.Close(val);
}

Handle<Value>
GInt::ToString(const Arguments &args) {
  HandleScope scope;

  ENSURE(args.This(), GInt);
  GInt *self = ObjectWrap::Unwrap<GInt>(args.This());
  Local<String> str;

  evil_try_catch({ str = String::New(self->val_.get_str(10).c_str()); },
                 "gmp abort");
  return scope.Close(str);
}

Handle<Value>
GFloat::New(const Arguments &args) {
  HandleScope scope;
  mpf_class i;
  if(args[1]->IsNumber()) i.set_prec(args[1]->Uint32Value());
  i = 0;

  evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  ENSURE(args.This(), GFloat);
  GFloat *g = new GFloat(i);
  g->Wrap(args.This());
  return args.This();
}


GFloat::GFloat(mpf_class val): ObjectWrap() {
  val_ = val;
  val_.set_prec(val.get_prec());
}

GFloat::~GFloat(){

}


Handle<Value>
GFloat::Add(const Arguments &args) {
  HandleScope scope;

  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());
  mpf_class i = 0;
  i.set_prec(self->val_.get_prec());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GFloat);
    GFloat *other = ObjectWrap::Unwrap<GFloat>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ self->val_ += i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GFloat::Sub(const Arguments &args) {
  HandleScope scope;

  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());
  mpf_class i = 0;
  i.set_prec(self->val_.get_prec());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GFloat);
    GFloat *other = ObjectWrap::Unwrap<GFloat>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ self->val_ -= i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GFloat::Mul(const Arguments &args) {
  HandleScope scope;

  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());
  mpf_class i = 0;
  i.set_prec(self->val_.get_prec());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GFloat);
    GFloat *other = ObjectWrap::Unwrap<GFloat>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ self->val_ *= i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GFloat::Div(const Arguments &args) {
  HandleScope scope;

  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());
  mpf_class i = 0;
  i.set_prec(self->val_.get_prec());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GFloat);
    GFloat *other = ObjectWrap::Unwrap<GFloat>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ self->val_ /= i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GFloat::Pow(const Arguments &args) {
  HandleScope scope;

  if (!args[0]->IsNumber()) {
    return ThrowException(Exception::TypeError(String::New("exponent must be an int")));
  }

  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());

  evil_try_catch({
    mp_bitcnt_t prec = self->val_.get_prec();
    mpf_t c;
    mpf_init2(c, prec);

    mpf_pow_ui(c, self->val_.get_mpf_t(), (double)args[0]->Int32Value());

    self->val_ = mpf_class(c);
    self->val_.set_prec(prec);
    mpf_clear(c);
  }, "gmp abort");

  return args.This();
}

Handle<Value>
GFloat::Sqrt(const Arguments &args) {
  HandleScope scope;
  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());
  evil_try_catch({ self->val_ = sqrt(self->val_); }, "gmp abort");
  return args.This();
}

Handle<Value>
GFloat::Ceil(const Arguments &args) {
  HandleScope scope;
  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());
  evil_try_catch({ self->val_ = ceil(self->val_); }, "gmp abort");
  return args.This();
}

Handle<Value>
GFloat::Floor(const Arguments &args) {
  HandleScope scope;
  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());
  evil_try_catch({ self->val_ = floor(self->val_); }, "gmp abort");
  return args.This();
}

Handle<Value>
GFloat::Cmp(const Arguments &args) {
  HandleScope scope;

  mpf_class i = 0;
  int result = 0;

  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GFloat);
    GFloat *other = ObjectWrap::Unwrap<GFloat>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ if(self->val_ < i) result = 1;
                   else if(self->val_ > i) result = -1; }, "gmp abort");

  Local<Number> val = Number::New(result);
  return scope.Close(val);
}


Handle<Value>
GFloat::ToNumber(const Arguments &args) {
  HandleScope scope;
  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());

  Local<Number> val = Number::New(self->val_.get_d());

  return scope.Close(val);
}

Handle<Value>
GFloat::ToString(const Arguments &args) {
  HandleScope scope;
  mp_exp_t exp;
  long int i;
  ENSURE(args.This(), GFloat);
  GFloat *self = ObjectWrap::Unwrap<GFloat>(args.This());
  const char *unfixed = NULL;
  evil_try_catch({ unfixed = self->val_.get_str(exp,10).c_str(); },
                 "gmp abort");
  char *buff = (char *)alloca(abs(exp) + 2 + strlen(unfixed));
  char *cp = buff;
  if(unfixed[0] == '-') { *cp++ = '-'; unfixed++; }
  if(exp<=0) {
    *cp++ = '0'; *cp++ = '.';
    i = exp;
    while(i++<0) { *cp++ = '0'; }
  }
  i = 0;
  while(i<exp || *unfixed) {
    if(i++ == exp && exp != 0) { *cp++ = '.'; }
    if(*unfixed) *cp++ = *unfixed++;
    else *cp++ = '0';
  }
  *cp = '\0';

  Local<String> val = String::New(buff);

  return scope.Close(val);
}

Handle<Value>
GRational::New(const Arguments &args) {
  HandleScope scope;
  mpq_class i = 0;

  evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  ENSURE(args.This(), GRational);
  GRational *g = new GRational(i);
  g->Wrap(args.This());
  return args.This();
}


GRational::GRational(mpq_class val): ObjectWrap() {
  val_ = val;
}

GRational::~GRational(){

}


Handle<Value>
GRational::Add(const Arguments &args) {
  HandleScope scope;

  ENSURE(args.This(), GRational);
  GRational *self = ObjectWrap::Unwrap<GRational>(args.This());
  mpq_class i = 0;

  if(args[0]->IsObject()) {
    ENSURE(args[0], GRational);
    GRational *other = ObjectWrap::Unwrap<GRational>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ self->val_ += i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GRational::Sub(const Arguments &args) {
  HandleScope scope;

  ENSURE(args.This(), GRational);
  GRational *self = ObjectWrap::Unwrap<GRational>(args.This());
  mpq_class i = 0;

  if(args[0]->IsObject()) {
    ENSURE(args[0], GRational);
    GRational *other = ObjectWrap::Unwrap<GRational>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ self->val_ -= i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GRational::Mul(const Arguments &args) {
  HandleScope scope;

  ENSURE(args.This(), GRational);
  GRational *self = ObjectWrap::Unwrap<GRational>(args.This());
  mpq_class i = 0;

  if(args[0]->IsObject()) {
    ENSURE(args[0], GRational);
    GRational *other = ObjectWrap::Unwrap<GRational>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ self->val_ *= i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GRational::Div(const Arguments &args) {
  HandleScope scope;

  ENSURE(args.This(), GRational);
  GRational *self = ObjectWrap::Unwrap<GRational>(args.This());
  mpq_class i = 0;

  if(args[0]->IsObject()) {
    ENSURE(args[0], GRational);
    GRational *other = ObjectWrap::Unwrap<GRational>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ self->val_ /= i; }, "gmp abort");

  return args.This();
}

Handle<Value>
GRational::Pow(const Arguments &args) {
  HandleScope scope;

  if (!args[0]->IsNumber()) {
    return ThrowException(Exception::TypeError(String::New("exponent must be an int")));
  }

  ENSURE(args.This(), GRational);
  GRational *self = ObjectWrap::Unwrap<GRational>(args.This());

  evil_try_catch({
    mpz_t c;
    mpz_t num;
    mpz_t den;
    mpz_init(c);
    mpz_init(num);
    mpz_init(den);
    mpq_ptr n;

    n = self->val_.get_mpq_t();

    mpq_get_den(den, n);
    mpq_get_num(num, n);
    mpz_pow_ui(c, num, (long)args[0]->Int32Value());
    mpq_set_num(n, c);

    //self->val_ = mpq_class(mpz_class(c), mpz_class(den));
    mpz_clear(c);
    mpz_clear(num);
    mpz_clear(den);
  }, "gmp abort");

  return args.This();
}

Handle<Value>
GRational::Cmp(const Arguments &args) {
  HandleScope scope;

  mpf_class i = 0;
  int result = 0;

  ENSURE(args.This(), GRational);
  GRational *self = ObjectWrap::Unwrap<GRational>(args.This());

  if(args[0]->IsObject()) {
    ENSURE(args[0], GRational);
    GRational *other = ObjectWrap::Unwrap<GRational>(args[0]->ToObject());
    i = other->val_;
  }
  else
    evil_try_catch({ GETARG(args[0], *val); }, "bad argument");

  evil_try_catch({ if(self->val_ < i) result = 1;
                   else if(self->val_ > i) result = -1; }, "gmp abort");

  Local<Number> val = Number::New(result);
  return scope.Close(val);
}

Handle<Value>
GRational::ToNumber(const Arguments &args) {
  HandleScope scope;
  ENSURE(args.This(), GRational);
  GRational *self = ObjectWrap::Unwrap<GRational>(args.This());

  self->val_.canonicalize();
  Local<Number> val = Number::New(self->val_.get_d());

  return scope.Close(val);
}

Handle<Value>
GRational::ToString(const Arguments &args) {
  HandleScope scope;
  ENSURE(args.This(), GRational);
  GRational *self = ObjectWrap::Unwrap<GRational>(args.This());
  const char *unfixed = NULL;
  self->val_.canonicalize();
  evil_try_catch({ unfixed = self->val_.get_str(10).c_str(); }, "gmp abort");
  Local<String> val = String::New(unfixed);

  return scope.Close(val);
}


void RegisterModule(Handle<Object> target) {
  target->Set(String::NewSymbol("version"), String::New(gmp_version));

  Local<FunctionTemplate> t_int = FunctionTemplate::New(GInt::New);
  t_int->InstanceTemplate()->SetInternalFieldCount(1);
  t_int->SetClassName(String::NewSymbol("GInt"));

  NODE_SET_PROTOTYPE_METHOD(t_int, "add", GInt::Add);
  NODE_SET_PROTOTYPE_METHOD(t_int, "sub", GInt::Sub);
  NODE_SET_PROTOTYPE_METHOD(t_int, "mul", GInt::Mul);
  NODE_SET_PROTOTYPE_METHOD(t_int, "div", GInt::Div);
  NODE_SET_PROTOTYPE_METHOD(t_int, "pow", GInt::Pow);
  NODE_SET_PROTOTYPE_METHOD(t_int, "cmp", GInt::Cmp);
  NODE_SET_PROTOTYPE_METHOD(t_int, "toString", GInt::ToString);
  NODE_SET_PROTOTYPE_METHOD(t_int, "toValue", GInt::ToNumber);

  target->Set(String::NewSymbol("Int"), t_int->GetFunction());

  Local<FunctionTemplate> t_float = FunctionTemplate::New(GFloat::New);
  t_float->InstanceTemplate()->SetInternalFieldCount(1);
  t_float->SetClassName(String::NewSymbol("GFloat"));

  NODE_SET_PROTOTYPE_METHOD(t_float, "add", GFloat::Add);
  NODE_SET_PROTOTYPE_METHOD(t_float, "sub", GFloat::Sub);
  NODE_SET_PROTOTYPE_METHOD(t_float, "mul", GFloat::Mul);
  NODE_SET_PROTOTYPE_METHOD(t_float, "div", GFloat::Div);
  NODE_SET_PROTOTYPE_METHOD(t_float, "pow", GFloat::Pow);
  NODE_SET_PROTOTYPE_METHOD(t_float, "cmp", GFloat::Cmp);
  NODE_SET_PROTOTYPE_METHOD(t_float, "sqrt", GFloat::Sqrt);
  NODE_SET_PROTOTYPE_METHOD(t_float, "ceil", GFloat::Ceil);
  NODE_SET_PROTOTYPE_METHOD(t_float, "floor", GFloat::Floor);
  NODE_SET_PROTOTYPE_METHOD(t_float, "toString", GFloat::ToString);
  NODE_SET_PROTOTYPE_METHOD(t_float, "toValue", GFloat::ToNumber);

  target->Set(String::NewSymbol("Float"), t_float->GetFunction());

  Local<FunctionTemplate> t_rational = FunctionTemplate::New(GRational::New);
  t_rational->InstanceTemplate()->SetInternalFieldCount(1);
  t_rational->SetClassName(String::NewSymbol("GRational"));

  NODE_SET_PROTOTYPE_METHOD(t_rational, "add", GRational::Add);
  NODE_SET_PROTOTYPE_METHOD(t_rational, "sub", GRational::Sub);
  NODE_SET_PROTOTYPE_METHOD(t_rational, "mul", GRational::Mul);
  NODE_SET_PROTOTYPE_METHOD(t_rational, "div", GRational::Div);
  NODE_SET_PROTOTYPE_METHOD(t_rational, "pow", GRational::Pow);
  NODE_SET_PROTOTYPE_METHOD(t_rational, "cmp", GRational::Cmp);
  NODE_SET_PROTOTYPE_METHOD(t_rational, "toString", GRational::ToString);
  NODE_SET_PROTOTYPE_METHOD(t_rational, "toValue", GRational::ToNumber);

  target->Set(String::NewSymbol("Rational"), t_rational->GetFunction());

  /* rewire allocators to throw on OOM */
  rewire(__gmp_allocate_func, node_gmp_replace_allocate,
         { ThrowException(Exception::Error(String::New("cannot find __gmp_allocate_func"))); });
  rewire(__gmp_reallocate_func, node_gmp_replace_reallocate,
         { ThrowException(Exception::Error(String::New("cannot find __gmp_reallocate_func"))); });
  rewire(__gmp_free_func, node_gmp_replace_free, { });

  /* The following is a best effort at fixing the FPE condition */
#ifdef gmp_initialize_abort
  gmp_initialize_abort(replace_abort);
#else
  rewire_signal(SIGABRT);
  rewire_signal(SIGFPE);
#warning sketchy abort replacements going on
  if(sizeof (unsigned long) > sizeof (int))
    abort_replace((void *)mpz_init2);
  abort_replace((void *)__gmp_invalid_operation);
#endif
}

NODE_MODULE(gmp, RegisterModule);

