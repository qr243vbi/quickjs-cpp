#pragma once

#include <functional>
#include <map>
#include <memory>
#include <quickjs.h>
#include <stdexcept>
#include <unordered_map>
#include <vector>
namespace qjs {

class Value;

class Context;

class ContextGetter {
public:
  virtual Context getContext() const;
protected:
  virtual JSContext *context() const = 0;
};

class Array : public ContextGetter {
public:
  virtual Value get(size_t index) const = 0;
  virtual bool set(size_t index, Value value) = 0;
  virtual size_t size() const = 0;
  template <typename T> bool set(size_t index, T value);
  virtual ~Array() = default;
};

class Map : public ContextGetter {
public:
  virtual qjs::Value get(const std::string &name) const = 0;
  virtual bool set(const std::string &index, qjs::Value value) = 0;
  virtual bool drop(const std::string &index) = 0;
  virtual size_t size() const = 0;
  template <typename T> bool set(const std::string &index, T value);
  virtual ~Map() = default;
};

template <typename T>
constexpr bool is_array_or_derived = std::is_base_of<Array, T>::value;

template <typename T>
using is_not_array = std::integral_constant<bool, !is_array_or_derived<T>>;

class Function : public ContextGetter {
public:

template <typename T, 
  std::enable_if_t<(is_array_or_derived<T> ), int> = 0 > 
qjs::Value invoke(qjs::Value *th, T* xs);

qjs::Value invoke();

template <typename... T,
          std::enable_if_t<(is_not_array<T>::value && ...), int> = 0>
qjs::Value invoke(qjs::Value *th, T... xs);


  virtual ~Function() = default;
protected:
  virtual qjs::Value call(qjs::Value *, qjs::Array * p = nullptr) = 0;
};

class Callback : public Function {
public:
  Callback(JSContext *ctx) { this->ctx = ctx; }
  std::function<Value(qjs::Value *, qjs::Array *)> fn;
protected:
  virtual qjs::Value call(qjs::Value *val, qjs::Array *arr = nullptr) override;

private:
  JSContext *ctx;

protected:
  virtual JSContext *context() const override { return ctx; }
};

static JSValue js_function_trampoline(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValueConst *data);

static Array *newEmptyArray(JSContext *);

class Value : public Array, public Map, public Function {
protected:
  virtual Value call(qjs::Value *val, Array *array = nullptr) override {
    auto ctx = context();
    if (!isFunction()) {
      return Value(ctx, JS_UNDEFINED, true);
    }
    auto func = raw();
    auto size = array->size();
    JSValueConst *argv = new JSValueConst[size];

    for (int i = 0; i < size; i++) {
      argv[i] = array->get(i).raw();
    }

    JSValue result = JS_Call(ctx, func, val->raw(), array->size(), argv);
    delete[] argv;

    return Value(ctx, result, true);
  }
public:
  virtual bool drop(const std::string &val) override {
    if (isObject()) {
      auto ctx = context();
      if (JS_DeleteProperty(ctx, raw(),
                            JS_NewAtomLen(ctx, val.c_str(), val.size()),
                            0) == true) {
        return true;
      };
    }
    return false;
  }

  virtual Context getContext() const override;

  virtual size_t size() const override {
    auto ctx = context();
    if (isArray()) {
      uint32_t len;
      JSValue length_val = JS_GetPropertyStr(ctx, raw(), "length");
      JS_ToUint32(ctx, &len, length_val);
      JS_FreeValue(ctx, length_val);
      return len;
    } else if (isObject()) {
      JSPropertyEnum *props;
      uint32_t prop_count = 0;
      if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, raw(),
                                 JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) ==
          0) {
        js_free(ctx, props);
      }
      return prop_count;
    } else {
      return 0;
    }
  }

  static std::vector<qjs::Value> makeVector(JSContext *ctx, int argc,
                                            JSValueConst *argv) {
    std::vector<qjs::Value> out;
    out.reserve(argc);

    for (int i = 0; i < argc; i++) {
      out.emplace_back(ctx, JS_DupValue(ctx, argv[i]), true);
    }

    return out;
  }

  void assign(JSContext *ctx, JSValue value, bool takeOwnership) {
    free();
    ctx_ = ctx;
    if (takeOwnership) {
      value_ = value;
    } else {
      value_ = JS_DupValue(ctx, value);
    }
  }

  void assign(JSContext *ctx, double value) {
    assign(ctx, JS_NewFloat64(ctx, value), true);
  }

  void assign(JSContext *ctx, const std::string &value) {
    JSValue v = JS_NewStringLen(ctx, value.c_str(), value.size());
    assign(ctx, v, true); // consumes v
  }

  void assignArray(JSContext *ctx) { assign(ctx, JS_NewArray(ctx), true); }

  void assignObject(JSContext *ctx, JSClassID id = JS_INVALID_CLASS_ID,
                    void *opaque = nullptr) {
    if (opaque != nullptr || id != JS_INVALID_CLASS_ID) {
      assign(ctx, JS_NewObjectClass(ctx, id), true);
      JSValue val = rawdup();
      JS_SetOpaque(val, opaque);
      JS_FreeValue(ctx, val);
    } else {
      assign(ctx, JS_NewObject(ctx), true);
    }
  }

#define ASSIGN_MOVE(val)                                                       \
  {                                                                            \
    assign(val.ctx_, val.value_, false);                                       \
    val.free();                                                                \
  }

  template <
      typename T,
      std::enable_if_t<!std::is_same_v<std::decay_t<T>, Value>, int> = 0,
      std::enable_if_t<!is_array_or_derived<std::decay_t<T>>, int> = 0,
      std::enable_if_t<!std::is_same_v<std::decay_t<T>, JSValue>, int> = 0>
  Value(JSContext *ctx, T t) {
    assign(ctx, t);
  }

  void assign(const Value &val) { assign(val.ctx_, val.value_, false); }

  Value() {}

  Value(JSContext *ctx, JSValue value, bool takeOwnership) {
    assign(ctx, value, takeOwnership);
  }

  Value &operator=(Value &&val) noexcept {
    ASSIGN_MOVE(val);
    return *this;
  }

  Value &operator=(const Value &val) {
    assign(val);
    return *this;
  }

  Value(const Value &val) { assign(val); };

  Value(Value &&val) noexcept { ASSIGN_MOVE(val); }

  void setPropertyFunc(const std::string & name, 
      const qjs::Value& getter1, const qjs::Value& setter1, int flags = JS_PROP_ENUMERABLE){
    auto ctx = context();
    JSAtom atom = JS_NewAtom(ctx, name.c_str());
    JSValue getter = getter1.rawdup();
    JSValue setter = setter1.rawdup();

    JS_DefinePropertyGetSet(ctx, raw(), atom, getter, setter, flags);
    // only free atom
    JS_FreeAtom(ctx, atom);
  }

  void setPropertyFunc(const std::string & name,
    const std::function<Value(Value*)> &getter1, 
    const std::function<Value(Value*, Value*)> &setter1,
    int flags = JS_PROP_ENUMERABLE
  );

  virtual bool set(size_t index, qjs::Value value) override {
    if (isArray()) {
      auto ctx = context();
      auto val = raw();
      JS_SetPropertyUint32(ctx, val, index, value.rawdup());
      return true;
    } else {
      return false;
    }
  }

  virtual bool set(const std::string &index, qjs::Value value) override {
    if (isObject()) {
      JS_SetPropertyStr(context(), raw(), index.c_str(), value.rawdup());
      return true;
    } else {
      return false;
    }
  }

  virtual qjs::Value get(size_t index) const override {
    auto ctx = context();
    if (isArray()) {
      auto new_val = JS_GetPropertyUint32(ctx, raw(), index);
      return Value(ctx, new_val, true);
    } else {
      return Value(ctx, JS_UNDEFINED, true);
    }
  }

  template <typename T> bool set(size_t index, T value) {
    return set(index, qjs::Value(context(), value));
  }

  template <typename T> bool set(std::string index, T value) {
    return set(index, qjs::Value(context(), value));
  }

  virtual qjs::Value get(const std::string &name) const override {
    auto ctx = context();
    if (isObject()) {
      auto new_val = JS_GetPropertyStr(ctx, raw(), name.c_str());
      return Value(ctx, new_val, true);
    } else {
      return Value(ctx, JS_UNDEFINED, true);
    }
  }

  typedef JSClassID ClassID;

  void *as(ClassID classid) {
    if (classid == JS_INVALID_CLASS_ID) {
      return nullptr;
    }
    return JS_GetOpaque(value_, classid);
  }

  bool is(ClassID classid) { return as(classid) != nullptr; }

  ClassID getClassID() { return JS_GetClassID(value_); }

  template <typename T> bool is() const;

  template <typename T> T as() const;

  bool isException() const { return JS_IsException(raw()); }

  bool isUndefined() const { return JS_IsUndefined(raw()); }

  bool isNull() const { return JS_IsNull(raw()); }

  bool isBool() const { return JS_IsBool(raw()); }

  bool isNumber() const { return JS_IsNumber(raw()); }

  bool isString() const { return JS_IsString(raw()); }

  bool isObject() const { return JS_IsObject(raw()); }

  bool isFunction() const { return JS_IsFunction(context(), raw()); }

  bool isArray() const { return JS_IsArray(raw()); }

  bool isSymbol() const { return JS_VALUE_GET_TAG(raw()) == JS_TAG_SYMBOL; }

  bool isBigInt() const { return JS_VALUE_GET_TAG(raw()) == JS_TAG_BIG_INT; }

  virtual ~Value() { free(); }

  friend void throwIfException(JSContext *ctx, qjs::Value &value);
  friend class PointerArray;
  friend JSValue js_function_trampoline(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv, int magic,
                                        JSValueConst *data);

protected:
  JSValue rawdup() const { return JS_DupValue(ctx_, value_); }
  JSValue raw() const { return value_; }

  virtual JSContext *context() const override { return ctx_; }
  void free() {
    if (ctx_ != nullptr) {
      JS_FreeValue(ctx_, value_);
      ctx_ = nullptr;
    }
  }
  JSContext *ctx_ = nullptr;
  JSValue value_ = JS_UNDEFINED;
};

class VectorArray : public Array, public std::vector<Value> {
public:
  virtual ~VectorArray() override { this->clear(); }
  virtual Value get(size_t index) const override {
    return std::vector<Value>::at(index);
  };
  virtual bool set(size_t index, Value value) override {
    auto count = size();
    if (count <= index) {
      this->std::vector<Value>::reserve(index * 2);
      auto ctx = context();
      while (count < index) {
        this->std::vector<Value>::push_back(
            qjs::Value(ctx, JS_UNDEFINED, true));
        count++;
      }
      this->std::vector<Value>::push_back(value);
    } else {
      this->std::vector<Value>::at(index) = value;
    }
    return true;
  };
  virtual size_t size() const override { return std::vector<Value>::size(); };
  template <typename T> bool set(size_t index, T value) {
    return set(index, qjs::Value(context(), value));
  };
  VectorArray(JSContext *ctx) { this->ctx = ctx; }

private:
  JSContext *ctx;

protected:
  virtual JSContext *context() const override { return ctx; }
};

Array *
newEmptyArray(JSContext *context) { // NOLINT(misc-definitions-in-headers)
  return new VectorArray(context);
}

template <typename T> bool Array::set(size_t index, T value) {
  return set(index, qjs::Value(context(), value));
}

template <typename T> bool Map::set(const std::string &index, T value) {
  return set(index, qjs::Value(context(), value));
}

qjs::Value
Callback::call(Value *val, // NOLINT(misc-definitions-in-headers)
               Array *array) {
  bool delete_array = array == nullptr;
  if (delete_array) {
    array = newEmptyArray(ctx);
  }
  auto ret = fn(val, array);
  if (delete_array) {
    delete array;
  }
  return ret;
};

class PointerArray : public Array {
public:
  virtual ~PointerArray() {};
  PointerArray(JSContext *ctx, int argc, JSValueConst *argv) {
    this->ctx = ctx;
    this->length = argc;
    this->array = argv;
  }
  virtual Value get(size_t index) const override {
    auto ctx = context();
    if (index >= length) {
      return Value(ctx, JS_UNDEFINED, true);
    } else {
      return Value(ctx, array[index], false);
    }
  };
  virtual bool set(size_t index, Value value) override {
    if (index >= length) {
      return false;
    } else {
      array[index] = value.rawdup();
      return true;
    }
  };
  virtual JSContext *context() const override { return ctx; };
  virtual size_t size() const override { return length; };

  template <typename T> bool set(size_t index, T value) {
    return set(index, qjs::Value(context(), value));
  }

private:
  size_t length = 0;
  JSValueConst *array;
  JSContext *ctx;
};

static void object_finalizer(JSRuntime *rt, JSValue val);

struct ClassHeap {
  JSClassDef def;
  std::function<void(void *)> finalize;
  void operator()(void *p) {
    if (finalize != nullptr) {
      finalize(p);
    }
  }
  ClassHeap(const std::string &name, const std::function<void(void *)> &func) {
    def.class_name = strdup(name.c_str());
    def.finalizer = object_finalizer;
    finalize = func;
  }
};

class QuickJS_CppClasses {
public:
  friend QuickJS_CppClasses *getClasses(JSRuntime *rt);
  JSClassID function_class;
  std::unordered_map<JSClassID, ClassHeap> finalizer_map;
  ~QuickJS_CppClasses() { free(); }

private:
  void free() {
        for (auto &iter : finalizer_map) {   // use reference
            auto name = iter.second.def.class_name;
            iter.second.def.class_name = nullptr;
            if (name != nullptr) {
                ::free((void *)name);        // ensure memory was dynamically allocated
            }
        }
        finalizer_map.clear();
  }
};

qjs::Value Function::invoke(){ // NOLINT(misc-definitions-in-headers)
  return this->invoke(nullptr, (Array*)nullptr);
}

template <typename T, std::enable_if_t<(is_array_or_derived<T> ), int> >
qjs::Value Function::invoke(Value *th, T* Ts) {
    bool delete_array = Ts == nullptr;
    bool delete_value = th == nullptr;
    Array * arr;
    
    if (delete_array) {
      arr = newEmptyArray(this->context());
    } else {
      arr = Ts;
    }

    if (delete_value){
      th = new qjs::Value(this->context(), JS_UNDEFINED, false);
    }

    auto ret = this->call(th, arr);
    if (delete_value){
      delete th;
    }
    if (delete_array){
      delete arr;
    }
    return ret;
};

template <typename... T,
          std::enable_if_t<(is_not_array<T>::value && ...), int> >
qjs::Value Function::invoke(Value *th, T... Ts) {
  auto context = this->context();
  VectorArray v(context);
  v.reserve(sizeof...(T));

  (v.emplace_back(context, std::forward<T>(Ts)), ...);
  return this->invoke(th, &v);
};

static void function_finalizer(JSRuntime *rt, JSValue val);

static JSClassDef function_def = {"qr243vbi_function", function_finalizer};

static void finalize_pointer(void *ptr,
                             std::function<void(void *)> fn = nullptr);

QuickJS_CppClasses *
getClasses(JSRuntime *rt) { // NOLINT(misc-definitions-in-headers)
  auto cls = (QuickJS_CppClasses *)JS_GetRuntimeOpaque(rt);
  if (cls == nullptr) {
    cls = new QuickJS_CppClasses();
    finalize_pointer((void *)cls, [](void *p) -> void {
      auto cls =  static_cast<QuickJS_CppClasses*>(p);
      cls->free();
    });
    JSClassID clsID = 0;
    JS_NewClassID(rt, &clsID);
    JS_NewClass(rt, clsID, &function_def);
    JS_SetRuntimeOpaque(rt, cls);
    cls->function_class = clsID;
  }
  return cls;
}

void object_finalizer(JSRuntime *rt, JSValue val) {
  JSClassID cls = JS_GetClassID(val);
  if (cls != JS_INVALID_CLASS_ID) {
    auto opaque = getClasses(rt);
    auto iter = opaque->finalizer_map.find(cls);
    if (iter != opaque->finalizer_map.end()) {
      iter->second(JS_GetOpaque(val, cls));
    }
  }
}

QuickJS_CppClasses *
getClasses(JSContext *rt) { // NOLINT(misc-definitions-in-headers)
  return getClasses(JS_GetRuntime(rt));
}

JSValue js_function_trampoline(
    JSContext *ctx,
    JSValueConst this_val, // NOLINT(misc-definitions-in-headers)
    int argc, JSValueConst *argv, int magic, JSValueConst *data) {
  JSClassID class_id = getClasses(ctx)->function_class;
  qjs::Function *cb = (qjs::Function *)JS_GetOpaque(data[0], class_id);
  std::shared_ptr<PointerArray> ptr =
      std::make_shared<PointerArray>(ctx, argc, argv);

  JSValue retvaljs;
  {
    auto thisval = Value(ctx, this_val, false);
    {
      auto retval = cb->invoke(&thisval, ptr.get());
      retvaljs = retval.rawdup();
    }
  }

  return retvaljs;
}

void function_finalizer(JSRuntime *rt, JSValue val) {
  QuickJS_CppClasses *cls = (QuickJS_CppClasses *)JS_GetRuntimeOpaque(rt);
  if (cls != nullptr) {
    Function *opaque = (Function *)JS_GetOpaque(val, cls->function_class);
    if (opaque != nullptr) {
      delete opaque;
    }
  }
}

class Runtime {
public:
  Runtime() {
    rt_ = JS_NewRuntime();
    if (!rt_) {
      throw std::runtime_error("JS_NewRuntime failed");
    }
  }

  ~Runtime() { this->free(); }

  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;

  Runtime(Runtime &&other) noexcept : rt_(other.rt_) { other.rt_ = nullptr; }

  Runtime &operator=(Runtime &&other) noexcept {
    if (this != &other) {
      if (rt_) {
        free();
      }
      rt_ = other.rt_;
      other.rt_ = nullptr;
    }
    return *this;
  }

  JSRuntime *get() const { return rt_; }

private:
  JSRuntime *rt_{};
  void free() {
    if (rt_) {
      auto data = JS_GetRuntimeOpaque(rt_);
      JS_FreeRuntime(rt_);
      finalize_pointer(data);
    }
  }
};

void finalize_pointer(void *ptr, std::function<void(void *)> fn) {
  static std::map<void *, std::function<void(void *)>> finalizers;
  if (ptr != nullptr) {
    if (fn == nullptr) {
      auto iter = finalizers.find(ptr);
      if (iter != finalizers.end()) {
        if (iter->second != nullptr) {
          iter->second(ptr);
        }
        finalizers.erase(iter);
      } 
      free(ptr);
    } else {
      finalizers[ptr] = fn;
    }
  }
}

class Context {
public:
  explicit Context() {
    rt_ = JS_NewRuntime();
    if (!rt_)
      throw std::runtime_error("JS_NewRuntime failed");
    ctx_ = JS_NewContext(rt_);
    if (!ctx_)
      throw std::runtime_error("JS_NewContext failed");
  }
  explicit Context(JSContext *ctx, bool takeOwnership) {
    this->owned = takeOwnership;
    this->ctx_ = ctx;
  }
  explicit Context(Runtime &runtime) : Context(runtime.get(), false) {}
  explicit Context(JSRuntime *rt_, bool takeOwnership) {
    ctx_ = JS_NewContext(rt_);
    if (!ctx_)
      throw std::runtime_error("JS_NewContext failed");
    if (takeOwnership) {
      this->rt_ = rt_;
    }
  }

  ~Context() { free(); }

  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;

  JSContext *get() const { return ctx_; }

  template <typename T> qjs::Value newValue(T t) { return qjs::Value(ctx_, t); }

  JSClassID newClassID(const std::string &name,
                       const std::function<void(void *)> &finalizer) {
    auto classmap = getClasses(ctx_);
    JSClassID id = 0;
    auto runtime = JS_GetRuntime(ctx_);
    JS_NewClassID(runtime, &id);
    auto proto = JS_NewObject(ctx_);
    JS_SetClassProto(ctx_, id, proto);
    auto pair = classmap->finalizer_map.emplace(
        std::piecewise_construct, std::forward_as_tuple(id),
        std::forward_as_tuple(name, finalizer));
    JS_NewClass(runtime, id, &pair.first->second.def);
    return id;
  }

  qjs::Value newFunction(qjs::Function *fn) {
    if (qjs::Value *d = dynamic_cast<qjs::Value *>(fn)) {
      return *d;
    }
    JSContext *ctx = this->ctx_;
    auto class_id = getClasses(ctx)->function_class;
    JSValue proto = JS_GetClassProto(ctx, class_id);
    if (JS_IsUndefined(proto)) {
      proto = JS_NewObject(ctx);
      JS_SetClassProto(ctx, class_id, proto);
    }

    JSValue data_obj = JS_NewObjectClass(ctx, class_id);

    JS_SetOpaque(data_obj, fn);

    JSValue data[1] = {data_obj};

    JSValue func = JS_NewCFunctionData(ctx, js_function_trampoline,
                                       0, // length
                                       0, // magic
                                       1, // data count
                                       data);

    JS_FreeValue(ctx, data_obj);

    return Value(ctx, func, true);
  }

  qjs::Value newFunction(std::function<Value(Value *, Array *)> fn) {
    auto call = new Callback(this->ctx_);
    call->fn = fn;
    return newFunction(call);
  }

  qjs::Value getGlobal() {
    return qjs::Value(ctx_, JS_GetGlobalObject(ctx_), true);
  }

  qjs::Value getGlobal(const std::string &name) {
    auto global = getGlobal();
    return global.get(name);
  }

  bool setGlobal(const std::string &name, const qjs::Value &val) {
    auto global = getGlobal();
    return global.set(name, val);
  };

  bool dropGlobal(const std::string &name) {
    auto global = getGlobal();
    return global.drop(name);
  }

  template <
      typename T,
      std::enable_if_t<!std::is_same_v<std::decay_t<T>, Value>, int> = 0,
      std::enable_if_t<!std::is_same_v<std::decay_t<T>, JSValue>, int> = 0>
  bool setGlobal(const std::string &name, T val) {
    return setGlobal(name, qjs::Value(ctx_, val));
  };

  qjs::Value newObject(JSClassID id = JS_INVALID_CLASS_ID,
                       void *opaque = nullptr) {
    qjs::Value val;
    val.assignObject(ctx_, id, opaque);
    return val;
  }

  qjs::Value newArray() {
    qjs::Value val;
    val.assignArray(ctx_);
    return val;
  }

  qjs::Value eval(const std::string &code,
                  const std::string &filename = "<eval>") {
    return Value(ctx_,
                 JS_Eval(ctx_, code.c_str(), code.size(), filename.c_str(),
                         JS_EVAL_TYPE_GLOBAL),
                 true);
  }

private:
  JSContext *ctx_ = nullptr;
  JSRuntime *rt_ = nullptr;
  bool owned = true;
  void free() {
    if (owned) {
      if (ctx_ != nullptr) {
        void *data = JS_GetContextOpaque(ctx_);
        JS_FreeContext(ctx_);
        finalize_pointer(data);
      }
      if (rt_ != nullptr) {
        void *data = JS_GetRuntimeOpaque(rt_);
        JS_FreeRuntime(rt_);
        finalize_pointer(data);
      }
    }
  }
};

  Context Value::getContext() const {  // NOLINT(misc-definitions-in-headers)
    return this->Array::getContext();
  }

  void Value::setPropertyFunc(const std::string & name, // NOLINT(misc-definitions-in-headers)
    const std::function<Value(Value*)> &getter1, 
    const std::function<Value(Value*, Value*)> &setter1,
    int flags
  ){
    auto ctx = getContext();

    qjs::Value getter = ctx.newFunction([getter1](Value * th, Array * ar)->qjs::Value { 
      return getter1(th); 
    });

    qjs::Value setter = ctx.newFunction([setter1](Value * th, Array * ar)->qjs::Value { 
      qjs::Value val = ar->get(0);
      return setter1(th, &val);
    });

    setPropertyFunc(name, getter, setter, flags);
  };

template <> inline bool Value::is<std::vector<Value>>() const {
  return isArray();
}

template <> inline bool Value::is<std::map<std::string, Value>>() const {
  return isObject();
}

template <> inline bool Value::is<std::nullptr_t>() const { return isNull(); }

template <> inline bool Value::is<bool>() const { return isBool(); }

template <> inline bool Value::is<int32_t>() const { return isNumber(); }

template <> inline bool Value::is<int64_t>() const { return isNumber(); }

template <> inline bool Value::is<double>() const { return isNumber(); }

template <> inline bool Value::is<std::string>() const { return isString(); }

class Exception : public std::runtime_error {
public:
  explicit Exception(JSContext *ctx, const std::string &error = "")
      : std::runtime_error(extract(ctx, error)) {}

private:
  static std::string extract(JSContext *ctx, const std::string &error) {
    JSValue exc = JS_GetException(ctx);

    JSAtom atom = JS_NewAtom(ctx, "stack");
    std::string result;

    JSValue stack = JS_GetProperty(ctx, exc, atom);

    JS_FreeAtom(ctx, atom);

    if (!JS_IsUndefined(stack)) {
      const char *s = JS_ToCString(ctx, stack);

      if (s) {
        result = s;
        JS_FreeCString(ctx, s);
      }
    }

    {
      const char *s = JS_ToCString(ctx, exc);

      if (s) {
        result += s;
        JS_FreeCString(ctx, s);
      }
    }

    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
    if (error.empty()) {
      return result;
    } else {
      return error + " " + result;
    }
  }
};

template <> inline std::string Value::as<std::string>() const {
  auto ctx_ = context();
  const char *str = JS_ToCString(context(), raw());

  if (!str)
    throw Exception(ctx_);

  std::string result(str);

  JS_FreeCString(ctx_, str);

  return result;
}
/*
template <> inline std::nullptr_t Value::as<std::nullptr_t>() const {
  if (!JS_IsNull(raw()))
    throw std::bad_cast();

  return nullptr;
}
*/
template <> inline bool Value::as<bool>() const {
  auto value_ = raw();
  auto ctx_ = context();
  int r = JS_ToBool(ctx_, value_);

  if (r < 0)
    throw Exception(ctx_);

  return r;
}

template <> inline int32_t Value::as<int32_t>() const {
  int32_t v;
  auto value_ = raw();
  auto ctx_ = context();

  if (JS_ToInt32(ctx_, &v, value_))
    throw Exception(ctx_);

  return v;
}

template <> inline int64_t Value::as<int64_t>() const {
  int64_t v;
  auto value_ = raw();
  auto ctx_ = context();

  if (JS_ToInt64(ctx_, &v, value_))
    throw Exception(ctx_);

  return v;
}

template <> inline double Value::as<double>() const {
  double v;
  auto value_ = raw();
  auto ctx_ = context();

  if (JS_ToFloat64(ctx_, &v, value_))
    throw Exception(ctx_);

  return v;
}

inline void throwIfException(JSContext *ctx, JSValueConst value) {
  if (!JS_IsException(value)) {
    return;
  }

  throw qjs::Exception(ctx);
}

Context ContextGetter::getContext() const { // NOLINT(misc-definitions-in-headers)
  return qjs::Context(this->context(), false);
}

inline void throwIfException(JSContext *ctx, qjs::Value &value) {
  throwIfException(ctx, value.raw());
}

} // namespace qjs