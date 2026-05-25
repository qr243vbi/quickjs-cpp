#pragma once

#include <map>
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
  virtual JSContext * context() const = 0;
  virtual size_t size() const = 0;
  template <typename T> bool set(size_t index, T value);
};

class Object {
  public:
  virtual qjs::Value get(const std::string & name) const = 0;
  virtual bool set(std::string index, qjs::Value value) = 0;
  virtual JSContext * context() const = 0;
  virtual size_t size() const = 0;
  template <typename T> bool set(std::string index, T value);
};

struct Callback {
    std::function<Value(Array*)> fn;
};

class Function {
  virtual qjs::Value call(Array*) = 0;
};

static JSValue js_trampoline(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv,
                             int magic, JSValueConst *data);

class Value : public Array, public Object, public Function{
public:
  virtual Value call(Array * array) override {
    if (!isFunction()){
      return Value(nullptr, JS_UNDEFINED, true);
    }
    auto func = raw();
    auto ctx = context();
    auto size = array->size();
    JSValueConst * argv = new JSValueConst[size];

    for (int i = 0; i < size; i ++){
      argv[i] = array->get(i).raw();
    }

    JSValue result = JS_Call(ctx, func, JS_UNDEFINED, array->size(), argv);
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

  void assignObject(JSContext *ctx){
    assign(ctx, JS_NewArray(ctx), true);
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

  virtual JSContext *context() const override { return ctx_; }

  JSValue raw() const { return value_; }

  JSValue rawdup() const { return JS_DupValue(ctx_, value_); }

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

  virtual bool set(std::string index, qjs::Value value) override {
    if (isObject()){
        JS_SetPropertyStr(context(), raw(), index.c_str(), value.rawdup());
        return true;
    } else {
        return false;
    }
  }

  virtual qjs::Value get(size_t index) const override {
    if (isArray()){
        auto ctx = context();
        auto new_val = JS_GetPropertyUint32(ctx, raw(), index);
        return Value(ctx, new_val, true);
    } else {
        return Value(nullptr, JS_UNDEFINED, true);
    }
  }


  template <typename T> bool set(size_t index, T value){
    return set(index, qjs::Value(context(), value));
  }

  template <typename T> bool set(std::string index, T value){
    return set(index, qjs::Value(context(), value));
  }

  virtual qjs::Value get(const std::string & name) const override {
    if (isObject()){
        auto ctx = context();
        auto new_val = JS_GetPropertyStr(ctx, raw(), name.c_str());
        return Value(ctx, new_val, true);
    } else {
        return Value(nullptr, JS_UNDEFINED, true);
    }
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

  ~Value() { free(); }

private:
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

  template <typename T> bool Object::set(std::string index, T value){
    return set(index, qjs::Value(context(), value));
  }


class PointerArray: public Array {
  public:
  PointerArray(JSContext * ctx, int argc, JSValueConst *argv){
    
  }
  virtual Value get(size_t index) const override { 
    if (index >= length){
      return Value(nullptr, JS_UNDEFINED, true);
    } else {
      return Value(context(), array[index], false);
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

JSValue js_trampoline(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv,
                             int magic, JSValueConst *data) {
    Callback *cb = (Callback *)JS_VALUE_GET_PTR(data[0]);
    auto ptr = new PointerArray(ctx, argc, argv);
    return cb->fn(ptr).rawdup();
}

class Runtime {
public:
  Runtime() {
    rt_ = JS_NewRuntime();
    if (!rt_)
      throw std::runtime_error("JS_NewRuntime failed");
  }

  ~Runtime() {
    if (rt_)
      JS_FreeRuntime(rt_);
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
    if (ctx_ != nullptr)
      JS_FreeContext(ctx_);
    if (rt_ != nullptr)
      JS_FreeRuntime(rt_);
  }

  Context(const Context &) = delete;
  Context &operator=(const Context &) = delete;

  JSContext *get() const { return ctx_; }

  template<typename T> qjs::Value newValue(T t){
    return qjs::Value(ctx_, t);
  }

  qjs::Value newObject() {
    qjs::Value val;
    val.assignObject(ctx_);
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