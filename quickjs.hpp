#pragma once

#include <map>
#include <memory>
#include <quickjs.h>
#include <stdexcept>
#include <vector>
#include <functional>
namespace qjs {
  
class Value;

class Array {
  public:
  virtual Value get(size_t index) const = 0;
  virtual bool set(size_t index, Value value) = 0;
  virtual size_t size() const = 0;
  template <typename T> bool set(size_t index, T value);
  virtual ~Array() = default;
  protected:
  virtual JSContext * context() const = 0;
};

class Map {
  public:
  virtual qjs::Value get(const std::string & name) const = 0;
  virtual bool set(const std::string& index, qjs::Value value) = 0;
  virtual bool drop(const std::string& index) = 0;
  virtual size_t size() const = 0;
  template <typename T> bool set(const std::string& index, T value);
  virtual ~Map() = default;
  protected:
  virtual JSContext * context() const = 0;
};

class Function {
  public:
  virtual qjs::Value call(const Value&, Array*) = 0;
  virtual ~Function() = default;
};

struct Callback: public Function {
  std::function<Value(const Value&, Array*)> fn;
  virtual qjs::Value call(const Value& val, Array*arr) override ;
};

static JSValue js_function_trampoline(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv,
                             int magic, JSValueConst *data);

class Value : public Array, public Map, public Function{
public:
  virtual bool drop(const std::string & val) override {
    if (isObject()){
      auto ctx = context();
      if (JS_DeleteProperty(ctx, raw(), JS_NewAtomLen(ctx, val.c_str(), val.size()), 0) == true){
        return true;
      };
    }
    return false;
  }
  virtual Value call(const Value& val, Array * array) override {
    auto ctx = context();
    if (!isFunction()){
      return Value(ctx, JS_UNDEFINED, true);
    }
    auto func = raw();
    auto size = array->size();
    JSValueConst * argv = new JSValueConst[size];

    for (int i = 0; i < size; i ++){
      argv[i] = array->get(i).raw();
    }

    JSValue result = JS_Call(ctx, func, val.raw(), array->size(), argv);
    delete [] argv;
    return Value(ctx, result, true);
  }

  virtual size_t size() const override {
    auto ctx = context();
    if (isArray()){
      uint32_t len;
      JSValue length_val = JS_GetPropertyStr(ctx, raw(), "length");
      JS_ToUint32(ctx, &len, length_val);
      JS_FreeValue(ctx, length_val);
      return len;
    } else if (isObject()){
      JSPropertyEnum *props;
      uint32_t prop_count = 0;
        if (JS_GetOwnPropertyNames(
          ctx,
          &props,
          &prop_count,
          raw(),
          JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK
        ) == 0) {
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
    if (takeOwnership){
      value_ = value;
    } else {
      value_ = JS_DupValue(ctx, value);    
    }
  }

  void assign(JSContext *ctx, double value){
    assign(ctx, JS_NewFloat64(ctx, value), true);
  }

  void assign(JSContext *ctx, const std::string& value) {
    JSValue v = JS_NewStringLen(ctx, value.c_str(), value.size());
    assign(ctx, v, true); // consumes v
  }

  void assignArray(JSContext *ctx){
    assign(ctx, JS_NewArray(ctx), true);
  }

  void assignObject(JSContext *ctx, JSClassID id = JS_INVALID_CLASS_ID, void * opaque = nullptr){
    if (opaque != nullptr || id != JS_INVALID_CLASS_ID){
      assign(ctx, JS_NewObjectClass(ctx, id), true);
      JSValue val = raw();
      JS_SetOpaque(val, opaque);
    } else {
      assign(ctx, JS_NewObject(ctx), true);
    }
  }

  #define ASSIGN_MOVE(val) {                \
    assign(val.ctx_, val.value_, false);    \
    val.value_ = JS_UNDEFINED;              \
    val.ctx_ = nullptr;                     \
  }

  template <
    typename T,
    std::enable_if_t<!std::is_same_v<std::decay_t<T>, Value>, int> = 0,
    std::enable_if_t<!std::is_same_v<std::decay_t<T>, JSValue>, int> = 0
  >   
  Value(JSContext *ctx, T t){
    assign(ctx, t);
  }

  void assign(const Value &val) {
    assign(val.ctx_, val.value_, false);
  }

  Value(){
  }

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

  virtual bool set(size_t index, qjs::Value value) override {
    if (isArray()){
        auto ctx = context();
        auto val = raw();
        JS_SetPropertyUint32(ctx, val, index, value.rawdup());
        return true;
    } else {
        return false;
    }
  }

  virtual bool set(const std::string& index, qjs::Value value) override {
    if (isObject()){
        JS_SetPropertyStr(context(), raw(), index.c_str(), value.rawdup());
        return true;
    } else {
        return false;
    }
  }

  virtual qjs::Value get(size_t index) const override {
    auto ctx = context();
    if (isArray()){
        auto new_val = JS_GetPropertyUint32(ctx, raw(), index);
        return Value(ctx, new_val, true);
    } else {
        return Value(ctx, JS_UNDEFINED, true);
    }
  }

  template <typename T> bool set(size_t index, T value){
    return set(index, qjs::Value(context(), value));
  }

  template <typename T> bool set(std::string index, T value){
    return set(index, qjs::Value(context(), value));
  }

  virtual qjs::Value get(const std::string & name) const override {
    auto ctx = context();
    if (isObject()){
        auto new_val = JS_GetPropertyStr(ctx, raw(), name.c_str());
        return Value(ctx, new_val, true);
    } else {
        return Value(ctx, JS_UNDEFINED, true);
    }
  }

  typedef JSClassID ClassID;

  void * as(ClassID classid) {
    if (classid == JS_INVALID_CLASS_ID){
      return nullptr;
    }
    return JS_GetOpaque(value_, classid);
  }

  bool is(ClassID classid) {
    return as(classid) != nullptr;
  }

  ClassID getClassID(){
    return JS_GetClassID(value_);
  }

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
                             int argc, JSValueConst *argv,
                             int magic, JSValueConst *data);
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

  template <typename T> bool Array::set(size_t index, T value){
    return set(index, qjs::Value(context(), value));
  }

  template <typename T> bool Map::set(const std::string &index, T value){
    return set(index, qjs::Value(context(), value));
  }

  qjs::Value Callback::call(const Value& val, Array*arr) { // NOLINT(misc-definitions-in-headers)
    return fn(val, arr);
  };

class PointerArray: public Array {
  public:
  virtual ~PointerArray(){};
  PointerArray(JSContext * ctx, int argc, JSValueConst *argv){
    
  }
  virtual Value get(size_t index) const override { 
    auto ctx = context();
    if (index >= length){
      return Value(ctx, JS_UNDEFINED, true);
    } else {
      return Value(ctx, array[index], false);
    }
  };
  virtual bool set(size_t index, Value value) override {
    if (index >= length){
      return false;
    } else {
      array[index] = value.rawdup();
      return true;
    }
  };
  virtual JSContext * context() const override { return ctx; };
  virtual size_t size() const override { return length; };

  template <typename T> bool set(size_t index, T value){
    return set(index, qjs::Value(context(), value));
  }
  private:
  size_t length = 0;
  JSValueConst *array;
  JSContext * ctx;
};


static void object_finalizer(JSRuntime * rt, JSValue val);

struct ClassHeap {
  JSClassDef def;
  std::function<void(void*)> finalize;
  void operator () (void* p) {
    if (finalize != nullptr){
      finalize(p);
    }
  }
  ClassHeap(const std::string & name, const std::function<void(void*)> & func){
    def.class_name = strdup(name.c_str());
    def.finalizer = object_finalizer;
    finalize = func;
  }
};

struct QuickJS_CppClasses {
  JSClassID function_class;
  std::unordered_map<JSClassID, ClassHeap> finalizer_map;

  ~QuickJS_CppClasses(){
    for (auto ptr: finalizer_map){
      free((void*)ptr.second.def.class_name) ;
    }
    finalizer_map.clear();
  }
};

static void function_finalizer(JSRuntime *rt, JSValue val);

static JSClassDef function_def = {
   "qr243vbi_function",
   function_finalizer
};

static QuickJS_CppClasses * getClasses (JSRuntime * rt) {
  auto cls = (QuickJS_CppClasses *) JS_GetRuntimeOpaque(rt);
  if (cls == nullptr){
    cls = new QuickJS_CppClasses();
    JS_NewClassID(rt, &cls->function_class);
    JS_NewClass(rt, cls->function_class, &function_def);
    JS_SetRuntimeOpaque(rt, cls);
  }
  return cls;
}

void object_finalizer(JSRuntime * rt, JSValue val){ 
  JSClassID cls = JS_GetClassID(val);
  if (cls != JS_INVALID_CLASS_ID){
    auto opaque = getClasses(rt);
    auto iter = opaque->finalizer_map.find(cls);
    if (iter != opaque->finalizer_map.end()){
      iter->second(JS_GetOpaque(val, cls));
    }
  }
}

static QuickJS_CppClasses * getClasses (JSContext * rt) {
  return getClasses(JS_GetRuntime(rt));
}

JSValue js_function_trampoline(JSContext *ctx, JSValueConst this_val, // NOLINT(misc-definitions-in-headers)
                             int argc, JSValueConst *argv,
                             int magic, JSValueConst *data) { 
    JSClassID class_id = getClasses(ctx)->function_class;
    qjs::Function *cb = (qjs::Function*)JS_GetOpaque(data[0], class_id);
    std::shared_ptr<PointerArray> ptr = std::make_shared<PointerArray>(ctx, argc, argv);
    return cb->call(Value(ctx, this_val, false), ptr.get()).rawdup();
}

void function_finalizer(JSRuntime *rt, JSValue val) {
  QuickJS_CppClasses * cls = (QuickJS_CppClasses *)JS_GetRuntimeOpaque(rt);
  if (cls != nullptr){
    Function * opaque = (Function *)JS_GetOpaque(val, cls->function_class);
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

  ~Runtime() {
    if (rt_) {
      auto data = (QuickJS_CppClasses*) JS_GetRuntimeOpaque(rt_);
      if (data != nullptr) {
        delete data;
      }
      JS_FreeRuntime(rt_);
    }
  }

  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;

  Runtime(Runtime &&other) noexcept : rt_(other.rt_) { other.rt_ = nullptr; }

  Runtime &operator=(Runtime &&other) noexcept {
    if (this != &other) {
      if (rt_)
        JS_FreeRuntime(rt_);
      rt_ = other.rt_;
      other.rt_ = nullptr;
    }
    return *this;
  }

  JSRuntime *get() const { return rt_; }

private:
  JSRuntime *rt_{};
};

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
  explicit Context(Runtime &runtime): Context(runtime.get(), false) {}
  explicit Context(JSRuntime *rt_, bool takeOwnership) {
    ctx_ = JS_NewContext(rt_);
    if (!ctx_)
      throw std::runtime_error("JS_NewContext failed");
    if (takeOwnership){
      this->rt_ = rt_;
    }
  }

  ~Context() {
    if (ctx_ != nullptr){
      void *data = JS_GetContextOpaque(ctx_);
      if (data) {
        free(data);
      }
      JS_FreeContext(ctx_);
    }
    if (rt_ != nullptr){
      auto data = (QuickJS_CppClasses*) JS_GetRuntimeOpaque(rt_);
      if (data) {
        delete data;
      }
      JS_FreeRuntime(rt_);
    }
  }

  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;

  JSContext *get() const { return ctx_; }

  template<typename T> qjs::Value newValue(T t){
    return qjs::Value(ctx_, t);
  }

  JSClassID newClassID(const std::string & name, const std::function<void(void*)> &finalizer){
    auto classmap = getClasses(ctx_);
    JSClassID id ;
    auto runtime = JS_GetRuntime(ctx_);
    JS_NewClassID(runtime, &id);
    auto pair = classmap->finalizer_map.emplace(
      std::piecewise_construct,
    std::forward_as_tuple(id),
    std::forward_as_tuple(name, finalizer)
    );
    JS_NewClass(runtime, id, &pair.first->second.def);
    return id;
  }

  qjs::Value newFunction(qjs::Function * fn)
  {
    if (qjs::Value* d = dynamic_cast<qjs::Value*>(fn)) {
      return *d;
    }
    JSContext *ctx = this->ctx_;
    JSValue data_obj =
        JS_NewObjectClass(ctx, getClasses(ctx)->function_class);

    JS_SetOpaque(data_obj, fn);

    JSValue data[1] = {
        data_obj
    };

    JSValue func = JS_NewCFunctionData(
        ctx,
        js_function_trampoline,
        0,      // length
        0,      // magic
        1,      // data count
        data
    );

    JS_FreeValue(ctx, data_obj);

    return Value(ctx, func, true);
  }

  qjs::Value newFunction(std::function<Value(const Value&, Array*)> fn){
    auto call = new Callback();
    call->fn = fn;
    return newFunction(call);    
  }

  qjs::Value getGlobal() {
    return qjs::Value(ctx_, JS_GetGlobalObject(ctx_), true);
  }

  
  qjs::Value getGlobal(const std::string & name){
    auto global = getGlobal();
    return global.get(name);
  }


  bool setGlobal(const std::string & name, const qjs::Value & val){
    auto global = getGlobal();
    return global.set(name, val);
  };

  bool dropGlobal(const std::string &name){
    auto global = getGlobal();
    return global.drop(name);
  }


  template <
    typename T,
    std::enable_if_t<!std::is_same_v<std::decay_t<T>, Value>, int> = 0,
    std::enable_if_t<!std::is_same_v<std::decay_t<T>, JSValue>, int> = 0
  >
  bool setGlobal(const std::string & name, T val){
    return setGlobal(name, qjs::Value(ctx_, val));
  };

  qjs::Value newObject(JSClassID id = JS_INVALID_CLASS_ID, void * opaque = nullptr) {
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
    return Value(ctx_, JS_Eval(ctx_, code.c_str(), code.size(),
                  filename.c_str(), JS_EVAL_TYPE_GLOBAL), true);
  }

private:
  JSContext *ctx_ = nullptr;
  JSRuntime *rt_ = nullptr;
};

template <> inline bool Value::is<std::vector<Value>>() const {
  return isArray();
}

template <> inline bool Value::is<std::map<std::string, Value>>() const {
  return isObject();
}

template <> inline bool Value::is<std::nullptr_t>() const { return isNull(); }

template <> inline bool Value::is<bool>() const { return isBool(); }

template <> inline bool Value::is<int32_t>() const {
  return isNumber();
}

template <> inline bool Value::is<int64_t>() const {
  return isNumber();
}

template <> inline bool Value::is<double>() const { return isNumber(); }

template <> inline bool Value::is<std::string>() const { return isString(); }

class Exception : public std::runtime_error {
public:
  explicit Exception(JSContext *ctx, const std::string &error = "") : std::runtime_error(extract(ctx, error)) {}

private:
  static std::string extract(JSContext *ctx, const std::string& error) {
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
    if (error.empty()){
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
  if (!JS_IsException(value)){
    return;
  }

    throw qjs::Exception(ctx);
}

inline void throwIfException(JSContext *ctx, qjs::Value &value) {
  throwIfException(ctx, value.raw());
}

} // namespace qjs